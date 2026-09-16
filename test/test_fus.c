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

static void test_gps_speed_and_course_are_held_with_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 91.0f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_FLOAT(91.0f, f.v_course_deg);
    TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

static void test_gps_yaw_rate_helper_signs_and_wrap(void)
{
    /* straight: no course change */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fus_yaw_rate_gps_dps(90.0f, 0, 90.0f, 1000000));
    /* right turn: compass rises 10->20 over 1 s -> yaw negative (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -10.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 1000000));
    /* left turn: compass falls 20->10 -> yaw positive (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, fus_yaw_rate_gps_dps(20.0f, 0, 10.0f, 1000000));
    /* wrap, right turn across north: 350 -> 10 is +20 deg clockwise */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(350.0f, 0, 10.0f, 1000000));
    /* wrap, left turn across north: 10 -> 350 is -20 deg */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 350.0f, 1000000));
    /* half a second doubles the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 500000));
    /* non-positive dt -> 0 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 1000000, 20.0f, 1000000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 2000000, 20.0f, 1000000));
}

static void test_gps_course_history_feeds_the_turn_rate(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* NAN until two valid fixes */
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1000000, true);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* one fix: still none */
    fus_set_gps_speed(&f, 30.0f, 106.0f, 1200000, true);  /* +6 deg in 0.2 s -> -30 dps (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    TEST_ASSERT_EQUAL_INT64(1200000, f.yaw_gps_mono_us);
    fus_set_gps_speed(&f, 30.0f, 999.0f, 1300000, false); /* invalid: does not update prev or the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1400000, true);  /* -6 deg from 106 over 0.2 s -> +30 dps (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.0f, f.yaw_gps_dps);
}

/* ---- integration cases: stillness, bias, orientation capture and forward learning (§22.1) ---- */

#define BURST_SAMPLES   120                       /* 1.2 s of acceleration at FUSION_HZ */
#define COAST_SAMPLES    50                       /* 0.5 s of coasting between bursts */
#define FIX_EVERY        (FUSION_HZ / 5)          /* one GPS fix every 20 samples = 5 Hz */
#define BURST_ACC_MPS2   2.0f                     /* > FWD_LEARN_ACC_MPS2, so the fix qualifies */
#define BURST_YAW_DPS    0.5f                     /* < FWD_LEARN_MAX_YAW_DPS, so the fix qualifies */

/* Deterministic LCG (Numerical Recipes constants): the noise sequence must be identical on every
 * host and on the ESP32, so no rand() and no library state. */
static uint32_t lcg_state;
static void lcg_reset(void) { lcg_state = 22222u; }
static float noise_pm(float amp)                  /* uniform in [-amp, +amp) */
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    const float u = (float)(lcg_state >> 8) / 16777216.0f;   /* top 24 bits → [0,1) */
    return amp * (2.0f * u - 1.0f);
}

/* Raw sample from accel in g and gyro in raw LSB (the gyro bias lives in LSB, §9.2). */
static imu_raw_t raw_at(int64_t mono_us, const float a_g[3], const float g_lsb[3])
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = (int16_t)lroundf(a_g[0] * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lroundf(a_g[1] * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lroundf(a_g[2] * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lroundf(g_lsb[0]);
    r.gy = (int16_t)lroundf(g_lsb[1]);
    r.gz = (int16_t)lroundf(g_lsb[2]);
    return r;
}

/* Feeds n samples at FUSION_HZ, each = (a_g, g_lsb) plus uniform noise, advancing *mono_us. */
static void feed(fus_t *f, fused_sample_t *o, int n, const float a_g[3], const float g_lsb[3],
                 float a_noise_g, float g_noise_lsb, int64_t *mono_us)
{
    for (int i = 0; i < n; i++) {
        /* Sequential declarations are sequenced (unlike the three noise_pm() calls in one brace
         * initializer would be), so each axis draws its LCG value in a fixed, portable order. */
        const float na0 = noise_pm(a_noise_g), na1 = noise_pm(a_noise_g), na2 = noise_pm(a_noise_g);
        const float an[3] = { a_g[0] + na0, a_g[1] + na1, a_g[2] + na2 };
        const float ng0 = noise_pm(g_noise_lsb), ng1 = noise_pm(g_noise_lsb), ng2 = noise_pm(g_noise_lsb);
        const float gn[3] = { g_lsb[0] + ng0, g_lsb[1] + ng1, g_lsb[2] + ng2 };
        imu_raw_t r = raw_at(*mono_us, an, gn);
        fus_step(f, &r, o);
        *mono_us += 1000000 / FUSION_HZ;
    }
}

static const float ZERO3[3] = { 0.0f, 0.0f, 0.0f };
static const float QUIET_ACC_NOISE_G = 0.005f;    /* var 8.3e-6 g² ≪ STILL_ACC_VAR = 4e-4 g² */
static const float QUIET_GYR_NOISE_LSB = 0.5f;    /* var 3.1e-4 dps² ≪ STILL_GYRO_VAR = 4 dps² */
static const float MOVING_GYR_NOISE_LSB = 5.0f * IMU_GYR_LSB_PER_DPS;  /* ±5 dps → var 8.3 dps² > 4 */

static void test_still_detection_and_gyro_bias_update(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_temp(&f, 2500);                       /* the pipeline's ~1 Hz poll, before the capture */
    fused_sample_t o; int64_t t = 1000000;
    const float quiet_a[3] = { 0.0f, 0.0f, 1.0f };
    const float bias_lsb[3] = { 20.0f, -8.0f, 3.0f };   /* the gyro bias the window must recover */

    feed(&f, &o, FUS_STILL_WINDOW_N - 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));           /* 199 samples: no window has completed */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    /* with no bias captured the 3 LSB on Z read as yaw: 3 / 16.4 = 0.183 dps. Tolerance 0.02 dps =
     * 0.33 LSB, more than the ±0.5 LSB noise can survive the int16 rounding of the raw sample. */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.0f / IMU_GYR_LSB_PER_DPS, o.yaw_dps);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));            /* the 200th sample closes the window */
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);
    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);   /* the verdict holds until the next window */

    fus_gyro_bias_update(&f);
    /* Tolerance 0.1 LSB: the ±0.5 LSB noise is zero-mean, so the mean of 200 samples has a standard
     * error of 0.5/sqrt(3·200) ≈ 0.02 LSB, and the int16 rounding of the raw sample removes most of
     * the noise before it is ever averaged. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, fus_calib(&f)->gbias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -8.0f, fus_calib(&f)->gbias[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, fus_calib(&f)->gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->bias_ok);
    TEST_ASSERT_EQUAL_INT16(2500, fus_calib(&f)->gbias_temp_c100);
    TEST_ASSERT_FALSE(f.bias_stale);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, o.yaw_dps);           /* 0.1 dps = 1.6 LSB of headroom */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);      /* captured at the current temperature */

    /* A moving window (±5 dps of gyro) is not still, so the bias must not move. */
    const fus_calib_t before = *fus_calib(&f);
    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    fus_gyro_bias_update(&f);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[0], fus_calib(&f)->gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[1], fus_calib(&f)->gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[2], fus_calib(&f)->gbias[2]);
}

/* Gravity as read by an IMU pitched 30° about body Y: (sin 30°, 0, cos 30°) g. Body -Y stays
 * perpendicular to that z (e_y · z = 0), so it can serve as the vehicle forward direction below. */
static const float TILT_RAD = 30.0f * 3.14159265358979f / 180.0f;   /* M_PI is not C11 */
static void tilted_gravity(float g[3])
{
    g[0] = sinf(TILT_RAD); g[1] = 0.0f; g[2] = cosf(TILT_RAD);
}

/* Fills one still window at the tilted attitude and captures the orientation. */
static void capture_tilted_orientation(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3])
{
    feed(f, o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, t);
    TEST_ASSERT_TRUE(fus_is_still(f));
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(f));
}

static void test_orientation_capture_from_tilted_gravity(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    /* Capture while moving is refused and leaves the calibration alone. */
    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_INT(-1, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->orient_ok);

    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));
    imu_raw_t clean = raw_at(t, grav, ZERO3);      /* noise-free sample, so only LSB rounding is left */
    fus_step(&f, &clean, &o);
    /* identity rows: the whole 30° tilt shows up as sign-less horizontal magnitude, sin 30° = 0.5 g.
     * 1e-3 g covers the 1/2048 g = 4.9e-4 g raw quantisation. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, o.g_lon);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);

    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    fus_step(&f, &clean, &o);                      /* the same sample through the captured rows */
    /* gravity is the z row now, so nothing is left in the horizontal plane. 1e-3 g covers the raw
     * quantisation (4.9e-4 g) plus the mean of the ±0.005 g window noise (0.005/sqrt(3·200) = 2e-4 g
     * per axis), which is all the captured z can be off by. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);            /* zero until forward is learned */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward row still unknown */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);      /* lean invalid before forward learned */
}

/* Three straight-line acceleration bursts along body -Y, with fus_calib_forward_step called at 5 Hz
 * exactly as the pipeline calls it (once per GPS fix). Counts the calls that returned 1 and -1.
 * The samples carry no noise: the forward row is then exact up to the raw quantisation. A constant
 * acceleration has no variance, so the stillness detector also calls these windows still — that is
 * inherent to a variance test and why the assertions below mask FUS_STILL out. */
static int run_forward_bursts(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3], int *neg)
{
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;        /* specific force of the burst, in g */
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    int ones = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        for (int i = 0; i < BURST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, acc, ZERO3, 0.0f, 0.0f, t);
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, grav, ZERO3, 0.0f, 0.0f, t);
        }
    }
    return ones;
}

static void test_forward_learning_from_straight_line_acceleration(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    TEST_ASSERT_EQUAL_INT(-1, fus_calib_forward_step(&f, BURST_ACC_MPS2, 0.0f));   /* before any capture */
    capture_tilted_orientation(&f, &o, &t, grav);

    int neg = 0;
    const int ones = run_forward_bursts(&f, &o, &t, grav, &neg);
    TEST_ASSERT_EQUAL_INT(1, ones);                /* reported exactly once, during the third burst */
    TEST_ASSERT_EQUAL_INT(0, neg);                 /* never -1 once the orientation is captured */
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
    TEST_ASSERT_TRUE(fus_fwd_ready(&f.fwd));

    /* The learned forward row is body -Y, which is perpendicular to the tilted z, so the burst's
     * specific force lands entirely on g_lon: 2 / 9.80665 = 0.2039 g. 2e-3 g covers the raw
     * quantisation of the sample (4.9e-4 g) and of the samples the row was learned from. */
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.2039f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lat);

    /* A left lateral specific force of 0.1 g: the body vector is 0.1 · y_row on top of gravity, and
     * lateral g is + to the right, so the output is -0.1 g. */
    const float *rows = fus_calib(&f)->r;
    const float lat[3] = { grav[0] + 0.1f * rows[3], grav[1] + 0.1f * rows[4], grav[2] + 0.1f * rows[5] };
    r = raw_at(t, lat, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, -0.1f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lon);
}

static void test_calibration_persists_through_the_calib_record_after_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));

    ses_calib_t rec; fus_calib_to_ses(fus_calib(&f), &rec);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_F_ORIENT | FUS_CALIB_F_FORWARD, rec.calib_flags);
    fus_calib_t decoded; fus_calib_from_ses(&rec, &decoded);
    TEST_ASSERT_TRUE(fus_calib_valid(&decoded));
    fus_t g; fus_init(&g, &decoded, 1);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&g)->forward_ok);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fused_sample_t restored;
    fus_step(&f, &r, &o);
    fus_step(&g, &r, &restored);
    /* 1e-3 g: the record stores each row entry as r × 1e4 rounded to int16, so a row entry moves by
     * up to 5e-5 and a 1 g sample by up to ~1e-4 g; the tolerance keeps a comfortable margin. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lon, restored.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lat, restored.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_comb, restored.g_comb);
    TEST_ASSERT_EQUAL_UINT8(o.flags & FUS_ORIENT_OK, restored.flags & FUS_ORIENT_OK);
}

static void test_recapture_resets_forward_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));
    TEST_ASSERT_EQUAL_UINT8(FWD_LEARN_WINDOWS, f.fwd.windows);

    /* The vehicle is re-mounted upright and captured again: the new z row invalidates the forward
     * row and every window accumulated against the old one. */
    const float upright[3] = { 0.0f, 0.0f, 1.0f };
    feed(&f, &o, FUS_STILL_WINDOW_N, upright, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));
    TEST_ASSERT_FALSE(f.fwd_learned_pending);
    imu_raw_t r = raw_at(t, upright, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward must be learned again */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
}

/* Degenerate accumulation (§9.2, fus_orient.c's FUS_FWD_MIN_NORM guard): when the accumulated
 * horizontal sum collapses to ~zero, fus_orient_set_forward refuses to learn a row and fus_step
 * restarts the tracker (fus_fwd_init) instead of leaving stale state behind. Two runs of exactly
 * opposite specific force are built as bit-exact int16 negations of each other, so ah(-a) = -ah(a)
 * to full float precision and the accumulated sum returns to exactly zero, not merely close to it.
 * FWD_LEARN_WINDOWS = 3 needs a third counted run; it alternates sign every sample (an even count),
 * which cancels to exactly zero by the time it is counted too, so the sum stays exactly zero right
 * through the trigger and the test is deterministic on every host. */
static void test_degenerate_forward_sum_restarts_the_tracker(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc_pos[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    const imu_raw_t r_pos = raw_at(t, acc_pos, ZERO3);
    imu_raw_t r_neg = r_pos;
    r_neg.ax = (int16_t)(-(int)r_pos.ax);
    r_neg.ay = (int16_t)(-(int)r_pos.ay);
    r_neg.az = (int16_t)(-(int)r_pos.az);

    int ones = 0, neg = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        const int n = (b < 2) ? BURST_SAMPLES : FUS_FWD_MIN_SAMPLES;
        for (int i = 0; i < n; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            imu_raw_t r;
            if (b == 0) r = r_pos;                     /* run 0: +X in the body plane */
            else if (b == 1) r = r_neg;                 /* run 1: -X, cancelling run 0 exactly */
            else r = (i % 2 == 0) ? r_pos : r_neg;      /* run 2: alternates, cancelling within itself */
            r.mono_us = t;
            fus_step(&f, &r, &o);
            t += 1000000 / FUSION_HZ;
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            feed(&f, &o, 1, grav, ZERO3, 0.0f, 0.0f, &t);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, ones);              /* the degenerate sum never reports a learned row */
    TEST_ASSERT_EQUAL_INT(0, neg);               /* orientation stays captured the whole time */
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);   /* fus_orient_set_forward's -1 restarted the tracker */
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));

    imu_raw_t clean = raw_at(t, grav, ZERO3);
    fus_step(&f, &clean, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);   /* forward still unlearned */

    /* A subsequent clean run still learns the forward row normally. */
    int neg2 = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg2));
    TEST_ASSERT_EQUAL_INT(0, neg2);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
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
    RUN_TEST(test_gps_speed_and_course_are_held_with_validity_and_time);
    RUN_TEST(test_gps_yaw_rate_helper_signs_and_wrap);
    RUN_TEST(test_gps_course_history_feeds_the_turn_rate);
    RUN_TEST(test_still_detection_and_gyro_bias_update);
    RUN_TEST(test_orientation_capture_from_tilted_gravity);
    RUN_TEST(test_forward_learning_from_straight_line_acceleration);
    RUN_TEST(test_calibration_persists_through_the_calib_record_after_learning);
    RUN_TEST(test_recapture_resets_forward_learning);
    RUN_TEST(test_degenerate_forward_sum_restarts_the_tracker);
    return UNITY_END();
}
