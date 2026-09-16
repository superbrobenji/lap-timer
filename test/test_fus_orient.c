#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Tolerance for the analytic row values. Each row is a handful of float32 multiplies, one divide and
 * one sqrtf away from its exact value, i.e. a few ulps of 1.0 (~1e-7); 1e-6 leaves a clear margin. */
#define VEC_TOL 1e-6f

/* <math.h> M_PI is an extension, not C11, and this suite also builds for the ESP32. */
static const float PI_F = 3.14159265358979323846f;

static void cross3(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float det3(const float r[9])
{
    return r[0] * (r[4] * r[8] - r[5] * r[7])
         - r[1] * (r[3] * r[8] - r[5] * r[6])
         + r[2] * (r[3] * r[7] - r[4] * r[6]);
}

static void assert_row(const fus_calib_t *c, int row, float ex, float ey, float ez)
{
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ex, c->r[3 * row + 0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ey, c->r[3 * row + 1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ez, c->r[3 * row + 2]);
}

/* Unit rows, zero pairwise dots and y = z × x: a right-handed orthonormal triad. */
static void assert_orthonormal(const fus_calib_t *c)
{
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, ri[0] * ri[0] + ri[1] * ri[1] + ri[2] * ri[2]);
        for (int j = i + 1; j < 3; j++) {
            const float *rj = c->r + 3 * j;
            TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, ri[0] * rj[0] + ri[1] * rj[1] + ri[2] * rj[2]);
        }
    }
    float y[3];
    cross3(y, c->r + 6, c->r + 0);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[0], c->r[3]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[1], c->r[4]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[2], c->r[5]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, det3(c->r));
}

static void test_upright_capture_gives_the_identity_triad(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float mean[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    /* body X and Y are equally unaligned with z; the tie goes to the lowest index, so x is body X */
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 1, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
    float v[3];
    fus_rotate(c.r, mean, v);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_tilted_gravity_lands_entirely_on_vehicle_z(void)
{
    const float ang = 20.0f * PI_F / 180.0f;           /* IMU pitched 20° nose-up about body Y */
    const float mags[2] = { 1.0f, 0.98f };             /* unit mean, then a 0.98 g bench reading */
    for (int i = 0; i < 2; i++) {
        const float mean[3] = { mags[i] * sinf(ang), 0.0f, mags[i] * cosf(ang) };
        fus_calib_t c; fus_calib_defaults(&c);
        TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
        assert_row(&c, 2, sinf(ang), 0.0f, cosf(ang)); /* z is the normalised mean */
        TEST_ASSERT_TRUE(fus_calib_valid(&c));
        assert_orthonormal(&c);
        /* body Y is the axis least aligned with z here, so the provisional forward is body Y */
        assert_row(&c, 0, 0.0f, 1.0f, 0.0f);
        float v[3];
        fus_rotate(c.r, mean, v);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, mags[i], v[2]);
    }
}

static void test_imu_on_its_side_stays_right_handed(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float mean[3] = { 0.0f, 1.0f, 0.0f };        /* vehicle up is body +Y */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    assert_row(&c, 2, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);               /* body X least aligned (tie with Z, lowest index wins) */
    assert_row(&c, 1, 0.0f, 0.0f, -1.0f);              /* y = z × x = (0,1,0) × (1,0,0) */
    float y[3];
    cross3(y, c.r + 6, c.r + 0);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[0], c.r[3]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[1], c.r[4]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[2], c.r[5]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, det3(c.r));  /* +1, not -1: a rotation, not a reflection */
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
}

static void test_gravity_outside_the_window_or_not_finite_is_rejected(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 3.0f; c.bias_ok = 1;
    fus_calib_t before; memcpy(&before, &c, sizeof before);   /* memcpy so padding bytes compare too */

    const float low[3] = { 0.0f, 0.0f, 0.3f };         /* 0.3 g < FUS_ORIENT_MIN_G */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, low));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float high[3] = { 0.0f, 0.0f, 1.6f };        /* 1.6 g > FUS_ORIENT_MAX_G */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, high));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float bad[3] = { 0.0f, NAN, 1.0f };          /* NaN makes |mean| NaN, which fails both bounds */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, bad));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
}

static void test_forward_from_a_horizontal_sum_gives_the_ninety_degree_mount(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.0f, -5.0f, 0.0f };        /* accumulated a_h: the vehicle accelerates along body -Y */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 0.0f, -1.0f, 0.0f);              /* the 90° mount of test_fus.c */
    assert_row(&c, 1, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
    const float b[3] = { 0.0f, -0.3f, 1.0f };          /* 0.3 g along body -Y = forward, 1 g up */
    float v[3];
    fus_rotate(c.r, b, v);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.3f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_forward_projects_out_the_vertical_component(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.7f, 0.0f, 0.7f };         /* half of it leaked onto vehicle up */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 1, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
}

static void test_forward_on_a_tilted_z_keeps_the_captured_up(void)
{
    const float ang = 20.0f * PI_F / 180.0f;
    const float mean[3] = { sinf(ang), 0.0f, cosf(ang) };
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    const float sum[3] = { 0.0f, 1.0f, 0.0f };         /* already perpendicular to z: nothing to project out */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 2, sinf(ang), 0.0f, cosf(ang));     /* the captured z row is untouched */
    assert_orthonormal(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    float v[3];
    fus_rotate(c.r, mean, v);                          /* the capture gravity still lands on vehicle Z */
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_forward_is_rejected_without_orientation_or_a_direction(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    fus_calib_t before; memcpy(&before, &c, sizeof before);
    const float sum[3] = { 1.0f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, sum));   /* orient_ok = 0 */
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));

    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    memcpy(&before, &c, sizeof before);
    const float vertical[3] = { 0.0f, 0.0f, 3.0f };    /* parallel to z: the projection is exactly zero */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, vertical));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, zero));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);
}

static void test_recapture_clears_forward_and_keeps_the_bias(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 11.0f; c.gbias[1] = -3.0f; c.gbias[2] = 0.5f;
    c.gbias_temp_c100 = 2712; c.bias_ok = 1;
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.0f, -5.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);

    /* Re-capture on a 30° roll about body X: |mean| = 1 g, so it is accepted. */
    const float tilt[3] = { 0.0f, 0.5f, 0.8660254f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, tilt));
    assert_row(&c, 2, 0.0f, 0.5f, 0.8660254f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);          /* the learned forward row no longer applies */
    TEST_ASSERT_EQUAL_FLOAT(11.0f, c.gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, c.gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, c.gbias[2]);
    TEST_ASSERT_EQUAL_INT16(2712, c.gbias_temp_c100);
    TEST_ASSERT_EQUAL_UINT8(1, c.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_upright_capture_gives_the_identity_triad);
    RUN_TEST(test_tilted_gravity_lands_entirely_on_vehicle_z);
    RUN_TEST(test_imu_on_its_side_stays_right_handed);
    RUN_TEST(test_gravity_outside_the_window_or_not_finite_is_rejected);
    RUN_TEST(test_forward_from_a_horizontal_sum_gives_the_ninety_degree_mount);
    RUN_TEST(test_forward_projects_out_the_vertical_component);
    RUN_TEST(test_forward_on_a_tilted_z_keeps_the_captured_up);
    RUN_TEST(test_forward_is_rejected_without_orientation_or_a_direction);
    RUN_TEST(test_recapture_clears_forward_and_keeps_the_bias);
    return UNITY_END();
}
