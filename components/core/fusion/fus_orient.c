#include "core/fus.h"
#include "core/core.h"
#include <math.h>

/* Power-of-10 rule 5 assertion code for the fusion module (design §3). */
#define FUS_ASSERT_CODE 0x0A10

/* Orientation rows of the calibration matrix (spec §9.2 "Orientation").
 *
 * R's rows x, y, z are the vehicle axes (X forward, Y left, Z up) written in body coordinates, so
 * vehicle = R · body and the triad is right-handed: y = z × x and x = y × z. This file owns the two
 * ways a row is learned: z from a still upright gravity mean, x from the accumulated horizontal
 * acceleration of a straight-line run. fus_rotate and fus_calib_valid live in fusion/fus.c. */

/* Shortest horizontal accumulator (in the caller's units, g·samples) that still points somewhere.
 * Below this the projection is float rounding noise, not a direction, so forward is refused. */
static const float FUS_FWD_MIN_NORM = 1e-6f;

static float v_dot(const float a[3], const float b[3])
{
    CORE_ASSERT_RET(a != NULL, FUS_ASSERT_CODE, 0.0f);
    CORE_ASSERT_RET(b != NULL, FUS_ASSERT_CODE, 0.0f);
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float v_norm(const float a[3])
{
    CORE_ASSERT_RET(a != NULL, FUS_ASSERT_CODE, 0.0f);
    return sqrtf(v_dot(a, a));
}

static void v_cross(float out[3], const float a[3], const float b[3])
{
    CORE_ASSERT_VOID(out != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(b != NULL, FUS_ASSERT_CODE);
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* out = a / |a|. Every caller has already checked that |a| is finite and far enough from zero. */
static void v_normalize(float out[3], const float a[3])
{
    CORE_ASSERT_VOID(out != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a != NULL, FUS_ASSERT_CODE);
    const float n = v_norm(a);
    CORE_ASSERT_VOID(n > 0.0f, FUS_ASSERT_CODE);   /* every caller has gated |a| away from zero */
    out[0] = a[0] / n;
    out[1] = a[1] / n;
    out[2] = a[2] / n;
}

/* Given a unit z and an x already perpendicular to it, build the right-handed completion:
 * y = z × x, then x = y × z. Both crosses are renormalised because float rounding leaves them a few
 * ulps off unit length and fus_calib_valid measures the rows against FUS_ORTHO_TOL. */
static void complete_triad(float x[3], float y[3], const float z[3])
{
    CORE_ASSERT_VOID(x != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(y != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(z != NULL, FUS_ASSERT_CODE);
    float t[3];
    v_cross(t, z, x);
    v_normalize(y, t);
    v_cross(t, y, z);
    v_normalize(x, t);
}

/* Rows of R, row-major: r[0..2] = x, r[3..5] = y, r[6..8] = z. */
static void set_rows(fus_calib_t *c, const float x[3], const float y[3], const float z[3])
{
    CORE_ASSERT_VOID(c != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(x != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(y != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(z != NULL, FUS_ASSERT_CODE);
    c->r[0] = x[0]; c->r[1] = x[1]; c->r[2] = x[2];
    c->r[3] = y[0]; c->r[4] = y[1]; c->r[5] = y[2];
    c->r[6] = z[0]; c->r[7] = z[1]; c->r[8] = z[2];
}

int fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3])
{
    CORE_ASSERT_RET(c != NULL, FUS_ASSERT_CODE, -1);
    CORE_ASSERT_RET(mean_acc_g != NULL, FUS_ASSERT_CODE, -1);   /* the mean itself may be non-finite: gated below */
    /* A still, upright vehicle reads +1 g along vehicle up. A mean outside the window (or a
     * non-finite one) came from a moving or broken capture, so nothing is written. */
    const float m = v_norm(mean_acc_g);
    if (!isfinite(m) || m < FUS_ORIENT_MIN_G || m > FUS_ORIENT_MAX_G) return -1;

    float z[3];
    v_normalize(z, mean_acc_g);

    /* Provisional forward: the body axis least aligned with z, ties to the lowest index. Its
     * projection has length sqrt(1 - z[k]^2) >= sqrt(2/3), so the normalise below is always safe. */
    int k = 0;
    for (int i = 1; i < 3; i++) {
        if (fabsf(z[i]) < fabsf(z[k])) k = i;
    }
    float x0[3];
    for (int i = 0; i < 3; i++) x0[i] = ((i == k) ? 1.0f : 0.0f) - z[k] * z[i];   /* e_k · z = z[k] */

    float x[3], y[3];
    v_normalize(x, x0);
    complete_triad(x, y, z);

    set_rows(c, x, y, z);
    c->orient_ok = 1;
    c->forward_ok = 0;   /* the old forward row means nothing against a new z (§9.2) */
    return 0;
}

int fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3])
{
    CORE_ASSERT_RET(c != NULL, FUS_ASSERT_CODE, -1);
    CORE_ASSERT_RET(sum_ah != NULL, FUS_ASSERT_CODE, -1);
    if (!c->orient_ok) return -1;                /* no z row: nothing to project against */

    const float z[3] = { c->r[6], c->r[7], c->r[8] };
    const float d = v_dot(sum_ah, z);
    float h[3];
    for (int i = 0; i < 3; i++) h[i] = sum_ah[i] - d * z[i];   /* drop whatever leaked onto vehicle up */

    const float hn = v_norm(h);
    if (!isfinite(hn) || hn < FUS_FWD_MIN_NORM) return -1;

    float x[3], y[3];
    v_normalize(x, h);
    complete_triad(x, y, z);

    set_rows(c, x, y, z);
    c->forward_ok = 1;
    return 0;
}
