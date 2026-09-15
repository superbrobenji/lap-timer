#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw sample helpers: accel in g and gyro in dps expressed as MPU-6050 LSB (types.h scales). */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    imu_raw_t r;
    r.mono_us = 1000000;
    r.ax = (int16_t)lround(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lround(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lround(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lround(gx * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lround(gy * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lround(gz * IMU_GYR_LSB_PER_DPS);
    return r;
}

static void test_defaults_are_identity_and_valid(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[0]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[4]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[8]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, c.orient_ok); TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok); TEST_ASSERT_EQUAL_UINT8(0, c.bias_ok);
}

static void test_invalid_calibration_is_rejected_and_init_falls_back(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.version = 0;
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[0] = 2.0f;                       /* row x not unit length */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[3] = 1.0f;                       /* row y = (1,0,0) parallel to row x */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.forward_ok = 1;                    /* forward without orientation */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.gbias[1] = 40000.0f;               /* beyond the raw range */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));

    fus_t f; c.version = 0;
    fus_init(&f, &c, 1);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fus_calib(&f)->r[0]);
    fus_init(&f, NULL, 0);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_UINT8(0, f.moto);
}

static void test_identity_mount_gravity_bias_and_yaw_sign(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 5.0f * IMU_GYR_LSB_PER_DPS;                     /* 5 dps bias on X */
    c.bias_ok = 1;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 5.0, 0.0, 10.0);
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_EQUAL_INT64(1000000, o.mono_us);
    /* orientation not learned: sign-less horizontal magnitude, no lateral, no lean, ORIENT_OK clear */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & (FUS_ORIENT_OK | FUS_LEAN_VALID));
    /* yaw: +10 dps about body Z = left turn, bias-free because the bias is on X only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);

    c.orient_ok = 1; c.forward_ok = 1;                            /* identity mount fully known */
    fus_init(&f, &c, 1);
    r = raw_g_dps(0.3, 0.5, 1.0, 5.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    /* accel tolerance 1e-3 g: raw LSB quantisation is 1/2048 g ≈ 4.9e-4 g */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* +X forward: accelerating */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, o.g_lat);             /* +Y is left, so lateral g is -a.y */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(0.34f), o.g_comb);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.yaw_dps);            /* 5 dps on X minus the 5 dps bias */
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_EQUAL_UINT32(1, f.samples);
}

static void test_ninety_degree_mount_rotates_into_the_vehicle_frame(void)
{
    /* IMU mounted with body +X pointing left (vehicle +Y) and body +Y pointing backwards (vehicle -X);
     * body +Z up. Rows are the vehicle axes in body coordinates: x = (0,-1,0), y = (1,0,0), z = (0,0,1). */
    fus_calib_t c; fus_calib_defaults(&c);
    const float R[9] = { 0.0f, -1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.orient_ok = 1; c.forward_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    /* vehicle accelerating at 0.3 g forward appears on body -Y; a 10 dps left turn is body +Z */
    imu_raw_t r = raw_g_dps(0.0, -0.3, 1.0, 0.0, 0.0, 10.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* 1e-3 g: LSB quantisation */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    /* a right-hand lateral specific force (vehicle -Y = body -X) reads as positive g_lat */
    r = raw_g_dps(-0.4, 0.0, 1.0, 0.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lon);

    float v[3]; const float b[3] = { 1.0f, 2.0f, 3.0f };
    fus_rotate(R, b, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, v[2]);
}

static void test_temperature_drift_marks_the_bias_stale(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.bias_ok = 1; c.gbias_temp_c100 = 2500;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; imu_raw_t r = raw_g_dps(0, 0, 1, 0, 0, 0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);        /* temperature unknown: not stale */
    fus_set_temp(&f, 3900);                                      /* 14 °C away: within BIAS_TEMP_STALE_C */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 4100);                                      /* 16 °C away */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 900);                                       /* 16 °C the other way */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    /* no bias captured: temperature can never make it stale */
    fus_calib_defaults(&c); fus_init(&f, &c, 1); fus_set_temp(&f, 9000);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
}

static void test_calibration_round_trips_through_the_calib_record(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float ang = 30.0f * 3.14159265358979f / 180.0f;         /* rotation about Z by 30° (M_PI is not C11) */
    const float R[9] = { cosf(ang), sinf(ang), 0.0f,  -sinf(ang), cosf(ang), 0.0f,  0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.gbias[0] = 12.4f; c.gbias[1] = -7.6f; c.gbias[2] = 0.4f; c.gbias_temp_c100 = 2712;
    c.orient_ok = 1; c.forward_ok = 1; c.bias_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    ses_calib_t w; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(8660, w.r_e4[0]);                     /* cos 30° × 1e4 rounded */
    TEST_ASSERT_EQUAL_INT16(5000, w.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(12, w.gbias[0]); TEST_ASSERT_EQUAL_INT16(-8, w.gbias[1]); TEST_ASSERT_EQUAL_INT16(0, w.gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(0x07, w.calib_flags);
    fus_calib_t d; fus_calib_from_ses(&w, &d);
    for (int i = 0; i < 9; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, c.r[i], d.r[i]);
    TEST_ASSERT_TRUE(fus_calib_valid(&d));                        /* 1e-4 quantisation stays inside FUS_ORTHO_TOL */
    TEST_ASSERT_EQUAL_FLOAT(12.0f, d.gbias[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d.orient_ok); TEST_ASSERT_EQUAL_UINT8(1, d.forward_ok); TEST_ASSERT_EQUAL_UINT8(1, d.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, d.version);
    TEST_ASSERT_EQUAL_INT16(0, d.gbias_temp_c100);                 /* not carried by the record */
    /* a saturating bias clamps instead of wrapping */
    c.gbias[2] = 40000.0f; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(32767, w.gbias[2]);
}

static void test_gps_speed_is_held_with_its_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_identity_and_valid);
    RUN_TEST(test_invalid_calibration_is_rejected_and_init_falls_back);
    RUN_TEST(test_identity_mount_gravity_bias_and_yaw_sign);
    RUN_TEST(test_ninety_degree_mount_rotates_into_the_vehicle_frame);
    RUN_TEST(test_temperature_drift_marks_the_bias_stale);
    RUN_TEST(test_calibration_round_trips_through_the_calib_record);
    RUN_TEST(test_gps_speed_is_held_with_its_validity_and_time);
    return UNITY_END();
}
