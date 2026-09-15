#include "core/fus.h"
#include <math.h>
#include <string.h>

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
}

void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid)
{
    f->v_mps = v_mps; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */
        out->g_lat = -a[1];                      /* +Y is left; lateral g is + to the right (§9.3 step 5, car form) */
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;
    }
    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);
    out->lean_deg = 0.0f;                        /* lean filter arrives in session 2.3 */
    out->yaw_dps  = w[2];                        /* session 2.3 applies the lean correction of §9.3 step 3 */
    uint8_t flags = 0;
    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;
    out->flags = flags;
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- wired to the still / orient / fwd modules in Task 5 ---- */

bool fus_is_still(const fus_t *f)
{
    (void)f;
    return false;
}

void fus_gyro_bias_update(fus_t *f)
{
    (void)f;
}

int fus_calib_orient_capture(fus_t *f)
{
    (void)f;
    return -1;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    (void)f; (void)gps_acc_mps2; (void)yaw_dps;
    return -1;
}
