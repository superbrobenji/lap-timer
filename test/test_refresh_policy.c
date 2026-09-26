#include "unity.h"
#include "core/ui/refresh_policy.h"

void setUp(void) {}
void tearDown(void) {}

static rf_in_t base(void)
{
    rf_in_t i = {0};
    i.dirty = true;
    i.full_every = 10;
    i.now_us = 100000000;
    i.last_full_us = 90000000;
    i.last_partial_us = 99000000;
    return i;
}

static void test_dead_never_refreshes(void)
{
    rf_in_t i = base();
    i.dead = true;
    i.wants_full = true;
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
}

static void test_not_dirty_is_none(void)
{
    rf_in_t i = base();
    i.dirty = false;
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
}

static void test_plain_dirty_is_partial(void)
{
    rf_in_t i = base();
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
}

static void test_counter_full_only_when_still(void)
{
    rf_in_t i = base();
    i.partial_count = 10;
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
    i.still = true;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
}

static void test_moving_cap_forces_full(void)
{
    rf_in_t i = base();
    i.partial_count = 20;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
}

static void test_thirty_minutes_forces_full(void)
{
    rf_in_t i = base();
    i.last_full_us = i.now_us - 1800000000LL;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
}

static void test_wants_full_deferred_while_moving(void)
{
    rf_in_t i = base();
    i.wants_full = true;
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
    i.still = true;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
}

static void test_throttle_limits_partials_and_blocks_full(void)
{
    rf_in_t i = base();
    i.throttled = true;
    i.still = true;
    i.wants_full = true;
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
    i.last_partial_us = i.now_us - 31000000LL;
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dead_never_refreshes);
    RUN_TEST(test_not_dirty_is_none);
    RUN_TEST(test_plain_dirty_is_partial);
    RUN_TEST(test_counter_full_only_when_still);
    RUN_TEST(test_moving_cap_forces_full);
    RUN_TEST(test_thirty_minutes_forces_full);
    RUN_TEST(test_wants_full_deferred_while_moving);
    RUN_TEST(test_throttle_limits_partials_and_blocks_full);
    return UNITY_END();
}
