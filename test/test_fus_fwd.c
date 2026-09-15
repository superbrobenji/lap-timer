#include "unity.h"
#include "core/fus.h"
#include "core/consts.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Vehicle Z row for an upright, axis-aligned mount: a_h is then just the X/Y part of the accel. */
static const float Z_UP[3] = { 0.0f, 0.0f, 1.0f };

/* Pushes n identical samples; returns how many of them reported "forward learned". */
static int push_n(fus_fwd_t *w, const float acc[3], const float z[3], int n)
{
    int ones = 0;
    for (int i = 0; i < n; i++) ones += fus_fwd_on_sample(w, acc, z);
    return ones;
}

/* One complete run of n qualifying samples, bracketed by the fixes that open and close it. */
static void run_of(fus_fwd_t *w, const float acc[3], int n, int *ones)
{
    fus_fwd_on_fix(w, 2.0f, 0.0f);
    *ones += push_n(w, acc, Z_UP, n);
    fus_fwd_on_fix(w, 0.0f, 0.0f);
}

static void test_fix_condition_needs_straight_line_acceleration(void)
{
    /* The boundary cases below are only meaningful at the spec's thresholds. */
    TEST_ASSERT_EQUAL_FLOAT(1.5f, FWD_LEARN_ACC_MPS2);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, FWD_LEARN_MAX_YAW_DPS);

    fus_fwd_t w; fus_fwd_init(&w);
    TEST_ASSERT_FALSE(w.cond);
    fus_fwd_on_fix(&w, 1.6f, 1.9f);    TEST_ASSERT_TRUE(w.cond);    /* 1.6 > 1.5 and |1.9| < 2.0 */
    fus_fwd_on_fix(&w, 1.5f, 0.0f);    TEST_ASSERT_FALSE(w.cond);   /* acceleration is a strict > */
    fus_fwd_on_fix(&w, 1.6f, 2.0f);    TEST_ASSERT_FALSE(w.cond);   /* yaw rate is a strict < */
    fus_fwd_on_fix(&w, 1.6f, -1.9f);   TEST_ASSERT_TRUE(w.cond);    /* the yaw test is on |yaw|, either way */
    fus_fwd_on_fix(&w, -3.0f, 0.0f);   TEST_ASSERT_FALSE(w.cond);   /* braking: §9.2 learns from acceleration only */
}

static void test_samples_outside_the_condition_do_nothing(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.4f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 500));      /* 5 s of acceleration with no qualifying fix */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);
    TEST_ASSERT_FALSE(w.counted);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
    }
    TEST_ASSERT_FALSE(fus_fwd_ready(&w));
}

static void test_a_run_shorter_than_the_minimum_is_discarded(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.2f, 0.0f, 1.0f };                 /* a·z = 1 g, so a_h = (0.2, 0, 0) g */
    fus_fwd_on_fix(&w, 2.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, FUS_FWD_MIN_SAMPLES - 1));
    TEST_ASSERT_EQUAL_UINT32(FUS_FWD_MIN_SAMPLES - 1, w.run_samples);
    /* 99 × 0.2 g; 1e-6 covers 99 × (0.2f − 0.2) ≈ 3.0e-7 of float quantisation of the input */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 19.8, w.run_sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[1]);             /* exact: 0 − 1·0 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[2]);             /* exact: 1 − 1·1 */
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);

    fus_fwd_on_fix(&w, 0.0f, 0.0f);                          /* the condition drops: the run is thrown away */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    TEST_ASSERT_FALSE(w.counted);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
    }
}

static void test_a_run_counts_on_its_minimum_sample_and_keeps_accumulating(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.2f, 0.0f, 1.0f };
    fus_fwd_on_fix(&w, 1.6f, 0.5f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, FUS_FWD_MIN_SAMPLES - 1));
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_INT(0, fus_fwd_on_sample(&w, a, Z_UP));   /* the 100th counts, but 1 < FWD_LEARN_WINDOWS */
    TEST_ASSERT_TRUE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&w));
    /* the whole run_sum moved into sum: 100 × 0.2 g, 1e-6 covers 100 × (0.2f − 0.2) ≈ 3.0e-7 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 20.0, w.sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[1]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[2]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);

    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 50));       /* samples 101..150 add straight to sum */
    TEST_ASSERT_EQUAL_UINT32(150, w.run_samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 30.0, w.sum[0]);         /* 150 × 0.2 g; quantisation ≈ 4.5e-7 */
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);                   /* one run, counted once */
}

static void test_three_counted_runs_report_exactly_once(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.15f, -0.05f, 1.0f };              /* a_h = (0.15, −0.05, 0) g */
    const int len[3] = { 120, 100, 250 };
    int ones = 0;
    for (int r = 0; r < 3; r++) {
        fus_fwd_on_fix(&w, 1.8f, 0.2f);                      /* run r opens */
        for (int i = 0; i < len[r]; i++) {
            if (fus_fwd_on_sample(&w, a, Z_UP)) {
                ones++;
                TEST_ASSERT_EQUAL_INT(2, r);                             /* only the third run reports */
                TEST_ASSERT_EQUAL_INT(FUS_FWD_MIN_SAMPLES, i + 1);       /* on its 100th sample */
            }
        }
        fus_fwd_on_fix(&w, 0.0f, 0.0f);                      /* run r closes */
    }
    TEST_ASSERT_EQUAL_INT(1, ones);
    TEST_ASSERT_EQUAL_UINT8(3, w.windows);
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    /* Every sample of a counted run contributes, before and after the count: 120 + 100 + 250 = 470.
     * A run that had ended before FUS_FWD_MIN_SAMPLES would have contributed none of its samples. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 470.0 * 0.15, w.sum[0]);   /* 1e-5 covers 470 × (0.15f − 0.15) = 2.8e-6 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 470.0 * (double)a[0], w.sum[0]);  /* against the exact float input the sum is exact */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 470.0 * -0.05, w.sum[1]);  /* 470 × (0.05f − 0.05) = 3.5e-7 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[2]);                   /* exact: every a_h.z is 1 − 1·1 */
}

static void test_the_gravity_component_is_removed_for_a_tilted_z(void)
{
    /* Mount tilted 20° about body Y: z is the unit vehicle-up row, t a unit vector perpendicular to it. */
    const double ang = 20.0 * 3.14159265358979 / 180.0;   /* M_PI is not C11 */
    const float z[3] = { (float)sin(ang), 0.0f, (float)cos(ang) };
    const float t[3] = { (float)cos(ang), 0.0f, (float)-sin(ang) };
    const float a[3] = { z[0] + 0.2f * t[0], z[1] + 0.2f * t[1], z[2] + 0.2f * t[2] };   /* 1 g down + 0.2 g forward */

    fus_fwd_t w; fus_fwd_init(&w);
    fus_fwd_on_fix(&w, 2.5f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, z, FUS_FWD_MIN_SAMPLES));
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);
    /* sum = 100 × 0.2 × t = 20 t: the 1 g along z is removed whatever the tilt.
     * 1e-5 covers the measured 1.3e-6, which is 100 samples × ~1.3e-8 of float rounding in a·z and a − (a·z)z. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 20.0 * cos(ang), w.sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[1]);                 /* exact: a.y and z.y are both 0 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, -20.0 * sin(ang), w.sum[2]);
    /* the residual along z is what "the vertical component is removed" means, to the same bound */
    const double along_z = w.sum[0] * (double)z[0] + w.sum[1] * (double)z[1] + w.sum[2] * (double)z[2];
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 0.0, along_z);
    /* and the full 20 g·samples survive the projection: nothing but gravity was subtracted */
    const double mag = sqrt(w.sum[0] * w.sum[0] + w.sum[1] * w.sum[1] + w.sum[2] * w.sum[2]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 20.0, mag);
}

static void test_a_fourth_run_counts_but_reports_nothing(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.25f, 0.0f, 1.0f };                /* 0.25 is exact in float: no quantisation error */
    int ones = 0;
    for (int r = 0; r < 3; r++) run_of(&w, a, FUS_FWD_MIN_SAMPLES, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));

    run_of(&w, a, 200, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);                          /* the fourth run reports nothing */
    TEST_ASSERT_EQUAL_UINT8(4, w.windows);                   /* but it is counted and accumulated */
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    TEST_ASSERT_EQUAL_DOUBLE(500.0 * 0.25, w.sum[0]);        /* 300 + 200 samples, all exactly representable */
}

static void test_an_interrupted_run_restarts_from_zero(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.3f, 0.0f, 1.0f };
    fus_fwd_on_fix(&w, 2.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 60));
    fus_fwd_on_fix(&w, 1.0f, 0.0f);                          /* below FWD_LEARN_ACC_MPS2: the run ends */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    fus_fwd_on_fix(&w, 2.0f, 0.0f);                          /* a new run starts at 0, not at 60 */
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 60));
    TEST_ASSERT_EQUAL_UINT32(60, w.run_samples);
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);                   /* 120 samples, neither run reached 100 */
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
    /* only the live run is held; 1e-6 covers 60 × (0.3f − 0.3) ≈ 7.2e-7 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 60.0 * 0.3, w.run_sum[0]);
}

static void test_the_window_counter_saturates(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.5f, 0.0f, 1.0f };                 /* exact in float */
    int ones = 0;
    for (int r = 0; r < 300; r++) run_of(&w, a, FUS_FWD_MIN_SAMPLES, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);                          /* still exactly one report, on run 3 */
    TEST_ASSERT_EQUAL_UINT8(255, w.windows);                 /* uint8_t saturates instead of wrapping to 0 */
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    TEST_ASSERT_EQUAL_DOUBLE(300.0 * FUS_FWD_MIN_SAMPLES * 0.5, w.sum[0]);   /* every counted sample is in */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fix_condition_needs_straight_line_acceleration);
    RUN_TEST(test_samples_outside_the_condition_do_nothing);
    RUN_TEST(test_a_run_shorter_than_the_minimum_is_discarded);
    RUN_TEST(test_a_run_counts_on_its_minimum_sample_and_keeps_accumulating);
    RUN_TEST(test_three_counted_runs_report_exactly_once);
    RUN_TEST(test_the_gravity_component_is_removed_for_a_tilted_z);
    RUN_TEST(test_a_fourth_run_counts_but_reports_nothing);
    RUN_TEST(test_an_interrupted_run_restarts_from_zero);
    RUN_TEST(test_the_window_counter_saturates);
    return UNITY_END();
}
