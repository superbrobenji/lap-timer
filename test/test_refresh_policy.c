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

/* Coverage gap 1 (fix round 1): dirty=false but wants_full=true alone must still refresh -- a
 * `&&` -> `||` mutation in rule 2 (`!dirty && !wants_full`) would turn this into RF_NONE, which
 * none of the eight original cases catches (test_wants_full_deferred_while_moving keeps dirty
 * true throughout). still gates full vs. partial exactly like every other wants_full case. */
static void test_wants_full_alone_refreshes(void)
{
    rf_in_t i = base();
    i.dirty = false;
    i.wants_full = true;
    i.still = true;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
    i.still = false;
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
}

/* Coverage gap 2: full_every == 0 is a caller bug guarded by the second CORE_ASSERT_RET, which
 * reports the fault and returns the conservative RF_NONE. Only the return value is asserted here
 * (this suite does not link assert_support.c, unlike test_smoke/test_ses_frame/test_ses_records). */
static void test_full_every_zero_is_none(void)
{
    rf_in_t i = base();
    i.full_every = 0;
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
}

/* Coverage gap 3: a NULL input is a caller bug guarded by the first CORE_ASSERT_RET. */
static void test_null_input_is_none(void)
{
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(NULL));
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
    RUN_TEST(test_wants_full_alone_refreshes);
    RUN_TEST(test_full_every_zero_is_none);
    RUN_TEST(test_null_input_is_none);
    return UNITY_END();
}
