#include "unity.h"
#include "app/log_pause.h"
#include "assert_support.h"
void setUp(void) {} void tearDown(void) {}

static void test_init_not_paused(void)
{
    log_pause_t p;
    log_pause_init(&p);
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000));          /* not still: never engages */
    TEST_ASSERT_FALSE(log_pause_active(&p));
}
static void test_engages_at_ten_seconds_exactly_once(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 1000);
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS - 1));
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_TRUE (log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS));
    TEST_ASSERT_TRUE (log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS + 5000));   /* only once */
    TEST_ASSERT_TRUE (log_pause_active(&p));
}
static void test_repeat_still_does_not_restart(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 1000);
    log_pause_still(&p, 1000 + LOG_PAUSE_STILL_MS - 1);    /* a second EV_STILL must not defer it */
    TEST_ASSERT_TRUE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS));
}
static void test_motion_resumes_and_demands_a_key(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0);
    (void)log_pause_tick(&p, LOG_PAUSE_STILL_MS);
    TEST_ASSERT_TRUE (log_pause_motion(&p));               /* was paused -> re-key */
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_motion(&p));               /* already moving -> nothing to re-key */
}
static void test_motion_before_the_pause_engages(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0);
    (void)log_pause_tick(&p, LOG_PAUSE_STILL_MS - 1);
    TEST_ASSERT_FALSE(log_pause_motion(&p));               /* never paused: no re-key needed */
    TEST_ASSERT_FALSE(log_pause_tick(&p, LOG_PAUSE_STILL_MS + 1));   /* and the countdown is gone */
}
static void test_wraps_with_the_32_bit_ms_clock(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0xFFFFF000u);
    TEST_ASSERT_FALSE(log_pause_tick(&p, 0x00000000u));    /* delta 4096 ms */
    TEST_ASSERT_TRUE (log_pause_tick(&p, 0xFFFFF000u + LOG_PAUSE_STILL_MS));
}
static void test_null_is_safe(void)
{
    lt_test_assert_reset();
    log_pause_init(NULL); log_pause_still(NULL, 0);
    TEST_ASSERT_FALSE(log_pause_motion(NULL));
    TEST_ASSERT_FALSE(log_pause_tick(NULL, 0));
    TEST_ASSERT_FALSE(log_pause_active(NULL));
    TEST_ASSERT_EQUAL_UINT(5u, lt_test_assert_count());
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_not_paused);
    RUN_TEST(test_engages_at_ten_seconds_exactly_once);
    RUN_TEST(test_repeat_still_does_not_restart);
    RUN_TEST(test_motion_resumes_and_demands_a_key);
    RUN_TEST(test_motion_before_the_pause_engages);
    RUN_TEST(test_wraps_with_the_32_bit_ms_clock);
    RUN_TEST(test_null_is_safe);
    return UNITY_END();
}
