/* test_rate.c -- Unity host tests for components/devconsole/host/rate.c (Plan 5.6 Task 4 fix 1).
 * Pure, IDF-free: exercises rate_x10's counter/dt arithmetic and its two "no rate" guards
 * (elapsed time <= 0, and a counter that went backwards -- e.g. a link reset) directly.
 */
#include "rate.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* 105 records over 10 s = 10.5/s -> rate_x10 105. */
void test_normal_rate(void)
{
    TEST_ASSERT_EQUAL_INT64(105, rate_x10(1105u, 1000u, 10 * 1000000));
}

/* n_now < n_prev (the counter went backwards -- e.g. linkstats_reset() on link re-attach mid
 * sampling window): -1, never a huge spurious rate from unsigned wraparound. */
void test_wrap_returns_neg1(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, rate_x10(500u, 1000u, 1000000));
}

/* dt_us == 0 (no elapsed time): -1, not a divide-by-zero / bogus rate. */
void test_dt_zero_returns_neg1(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, rate_x10(100u, 50u, 0));
}

/* First sample (no previous baseline): callers pass dt_us == 0 as the "never sampled" sentinel,
 * same as the dt-zero guard above, with both counters at their zero-initialized starting value. */
void test_first_sample_returns_neg1(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, rate_x10(0u, 0u, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_rate);
    RUN_TEST(test_wrap_returns_neg1);
    RUN_TEST(test_dt_zero_returns_neg1);
    RUN_TEST(test_first_sample_returns_neg1);
    return UNITY_END();
}
