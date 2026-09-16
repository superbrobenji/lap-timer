#include "unity.h"
#include "core/fus.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw-LSB sample (mono_us is unused by the detector: it counts samples, not time). */
static imu_raw_t raw_lsb(int ax, int ay, int az, int gx, int gy, int gz)
{
    imu_raw_t r;
    r.mono_us = 0;
    r.ax = (int16_t)ax; r.ay = (int16_t)ay; r.az = (int16_t)az;
    r.gx = (int16_t)gx; r.gy = (int16_t)gy; r.gz = (int16_t)gz;
    return r;
}

/* Physical-unit sample rounded to the MPU-6050 LSB scales of core/types.h. */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    return raw_lsb((int)lround(ax_g * IMU_ACC_LSB_PER_G), (int)lround(ay_g * IMU_ACC_LSB_PER_G),
                   (int)lround(az_g * IMU_ACC_LSB_PER_G), (int)lround(gx * IMU_GYR_LSB_PER_DPS),
                   (int)lround(gy * IMU_GYR_LSB_PER_DPS), (int)lround(gz * IMU_GYR_LSB_PER_DPS));
}

/* Deterministic LCG (Numerical Recipes constants) so the noise is identical on every run and host. */
static uint32_t lcg_next(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return *st;
}

/* Uniform draw on [-half, +half] from the top 16 bits (an LCG's low bits are weakly random).
 * A uniform on [a, b] has variance (b − a)² / 12, so here the variance is half² / 3. */
static double lcg_uniform(uint32_t *st, double half)
{
    const uint32_t u = lcg_next(st) >> 16;                 /* 0 .. 65535 */
    return ((double)u / 65535.0 * 2.0 - 1.0) * half;
}

/* One full window of accel-magnitude noise: |a| = 1 g + U(-h, h) with h = sigma·sqrt(3). */
static void push_accel_noise_window(fus_still_t *s, uint32_t *st, double sigma_g)
{
    const double h = sigma_g * sqrt(3.0);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) {
        const imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0 + lcg_uniform(st, h), 0.0, 0.0, 0.0);
        (void)fus_still_push(s, &r);
    }
}

/* One full window of gyro noise on the Y axis only, accel held at a clean 1 g on Z. */
static void push_gyro_noise_window(fus_still_t *s, uint32_t *st, double sigma_dps)
{
    const double h = sigma_dps * sqrt(3.0);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) {
        const imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 0.0, lcg_uniform(st, h), 0.0);
        (void)fus_still_push(s, &r);
    }
}

static void test_constant_window_completes_on_the_two_hundredth_sample(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 10, -5, 3);          /* exactly 1 g on Z, a tiny gyro offset */
    for (int i = 0; i < FUS_STILL_WINDOW_N - 1; i++) TEST_ASSERT_EQUAL_INT(0, fus_still_push(&s, &r));
    TEST_ASSERT_EQUAL_INT(1, fus_still_push(&s, &r));
    TEST_ASSERT_TRUE(s.have_window);
    TEST_ASSERT_TRUE(s.still);
    /* identical samples: E[x²] − E[x]² is zero up to double round-off, ~1e-16 relative at |a|² = 1 g² */
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.gyr_var_max);
    /* means are exact in float: 2048/2048 = 1 g, and the raw gyro mean is the repeated raw value */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.mean_acc[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.mean_acc[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.mean_acc[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, s.mean_graw[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -5.0f, s.mean_graw[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, s.mean_graw[2]);
    TEST_ASSERT_EQUAL_UINT16(0, s.n);                            /* the window restarted */
}

static void test_nothing_is_reported_before_the_first_window_completes(void)
{
    fus_still_t s; fus_still_init(&s);
    TEST_ASSERT_FALSE(s.have_window);
    TEST_ASSERT_FALSE(s.still);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 0, 0, 0);             /* a perfectly still stream */
    for (int i = 0; i < FUS_STILL_WINDOW_N - 1; i++) {
        TEST_ASSERT_EQUAL_INT(0, fus_still_push(&s, &r));
        TEST_ASSERT_FALSE(s.have_window);                        /* still-looking input reports nothing yet */
        TEST_ASSERT_FALSE(s.still);
    }
    TEST_ASSERT_EQUAL_UINT16(FUS_STILL_WINDOW_N - 1, s.n);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.acc_var);                    /* last-window fields untouched since init */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gyr_var_max);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean_acc[2]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean_graw[0]);
}

static void test_noise_variances_match_the_analytic_value_and_the_thresholds(void)
{
    /* 20 % band: the sample variance of N = 200 uniform draws has relative sd
     * sqrt((mu4/sigma⁴ − (N−3)/(N−1))/N) = sqrt((1.8 − 1)/200) ≈ 6.3 %, so 20 % is about 3 sd.
     * The LSB quantisation adds q²/12 = 2.0e-8 g² and 3.1e-4 dps², both negligible here. */
    uint32_t st = 2026u;
    fus_still_t s; fus_still_init(&s);

    push_accel_noise_window(&s, &st, 0.01);                      /* variance 1e-4 g² < STILL_ACC_VAR = 4e-4 */
    TEST_ASSERT_TRUE(s.have_window);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 1e-4f, 1e-4f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.gyr_var_max);        /* gyro was constant zero */

    fus_still_init(&s);
    push_accel_noise_window(&s, &st, 0.03);                      /* variance 9e-4 g² > 4e-4 */
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 9e-4f, 9e-4f, s.acc_var);

    fus_still_init(&s);
    push_gyro_noise_window(&s, &st, 1.0);                        /* variance 1 dps² < STILL_GYRO_VAR = 4 */
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 1.0f, 1.0f, s.gyr_var_max);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);            /* accel was constant 1 g */

    fus_still_init(&s);
    push_gyro_noise_window(&s, &st, 3.0);                        /* variance 9 dps² > 4 */
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 9.0f, 9.0f, s.gyr_var_max);
}

static void test_tumbling_windows_latch_until_the_next_completion(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t calm = raw_lsb(0, 0, 2048, 0, 0, 0);         /* 1 g */
    const imu_raw_t hi   = raw_lsb(0, 0, 3 * 2048, 0, 0, 0);     /* 3 g */

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);

    /* |a| alternates 1 g / 3 g: mean 2 g, E[x²] = 5 g², so the variance is exactly 1 g² */
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, (i & 1) ? &hi : &calm);
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, s.acc_var);            /* 1e-5: float32 ULP at 1 g² is 6e-8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, s.mean_acc[2]);

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);

    /* 150 moving samples (three quarters of a window) cannot change the latched flag */
    for (int i = 0; i < 150; i++) {
        (void)fus_still_push(&s, (i & 1) ? &hi : &calm);
        TEST_ASSERT_TRUE(s.still);
        TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
    }
    TEST_ASSERT_EQUAL_UINT16(150, s.n);
}

static void test_one_spike_breaks_the_window_and_the_next_one_recovers(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t calm  = raw_lsb(0, 0, 2048, 0, 0, 0);        /* |a| = 1 g */
    const imu_raw_t spike = raw_lsb(0, 0, 9 * 2048, 0, 0, 0);    /* |a| = 9 g, i.e. delta = +8 g */
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, (i == 100) ? &spike : &calm);
    TEST_ASSERT_FALSE(s.still);
    /* one outlier of delta in N identical samples: var = delta²·(1/N)(1 − 1/N) = 64·199/200² = 0.3184 g² */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3184f, s.acc_var);         /* 1e-5: float32 ULP at 0.32 g² is 3e-8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f + 8.0f / (float)FUS_STILL_WINDOW_N, s.mean_acc[2]);

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
}

static void test_full_scale_constant_window_does_not_overflow(void)
{
    fus_still_t s; fus_still_init(&s);
    /* Sigma|a|² reaches 200·768 g² = 1.5e5 g² and Sigma g² reaches 200·(32767/16.4)² = 8.0e8 dps²;
     * a double's ULP there is 3.3e-11 g² and 1.8e-7 dps², so a constant window still reads zero. */
    const imu_raw_t hi = raw_lsb(32767, 32767, 32767, 32767, 32767, 32767);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &hi);
    TEST_ASSERT_TRUE(s.still);                                   /* constant, however large */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, s.gyr_var_max);
    const float a_g = 32767.0f / IMU_ACC_LSB_PER_G;              /* 15.99951 g, exact in float32 */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, a_g, s.mean_acc[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, 32767.0f, s.mean_graw[i]);   /* float32 ULP at 32767 is 3.9e-3 */
    }

    const imu_raw_t lo = raw_lsb(-32767, -32767, -32767, -32767, -32767, -32767);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &lo);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.acc_var);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a_g, s.mean_acc[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, -32767.0f, s.mean_graw[i]);
    }
}

static void test_window_is_exactly_fus_still_window_n_pushes_long(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 0, 0, 0);
    const int total = 5 * FUS_STILL_WINDOW_N;                    /* 1000 pushes → 5 completions */
    int completions = 0;
    int last_completion = 0;
    for (int i = 1; i <= total; i++) {
        const int rc = fus_still_push(&s, &r);
        if (rc == 1) {
            completions++;
            last_completion = i;
            TEST_ASSERT_EQUAL_INT(0, i % FUS_STILL_WINDOW_N);    /* completes only on multiples of 200 */
        } else {
            TEST_ASSERT_EQUAL_INT(0, rc);
            TEST_ASSERT_NOT_EQUAL_INT(0, i % FUS_STILL_WINDOW_N);
        }
    }
    TEST_ASSERT_EQUAL_INT(5, completions);
    TEST_ASSERT_EQUAL_INT(total, last_completion);
    TEST_ASSERT_EQUAL_UINT16(0, s.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_constant_window_completes_on_the_two_hundredth_sample);
    RUN_TEST(test_nothing_is_reported_before_the_first_window_completes);
    RUN_TEST(test_noise_variances_match_the_analytic_value_and_the_thresholds);
    RUN_TEST(test_tumbling_windows_latch_until_the_next_completion);
    RUN_TEST(test_one_spike_breaks_the_window_and_the_next_one_recovers);
    RUN_TEST(test_full_scale_constant_window_does_not_overflow);
    RUN_TEST(test_window_is_exactly_fus_still_window_n_pushes_long);
    return UNITY_END();
}
