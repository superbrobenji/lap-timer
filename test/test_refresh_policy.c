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

/* Bench fix 1 (B-9): a whole-screen replacement forces a full even while moving -- the bug this
 * fix addresses was that `wants_full` alone (test_wants_full_deferred_while_moving) is deferred to
 * a partial until `still`, which is correct for the still-gated triggers but wrong for a screen
 * replacement (one-shot -> riding, menu enter/exit, page change): those must never go out as a
 * partial, which left ghosting outside the dirty rect. */
static void test_screen_change_forces_full_even_when_moving(void)
{
    rf_in_t i = base();
    i.still          = false;
    i.partial_count  = 0;
    i.screen_changed = true;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
}

/* Bench fix 1 (B-9): the throttle rule is still checked before screen_changed (rule 3 before rule
 * 3b), so a throttled screen replacement is downgraded to a partial, same as any other throttled
 * refresh, once the 30 s minimum spacing has elapsed. */
static void test_screen_change_still_partial_when_throttled(void)
{
    rf_in_t i = base();
    i.throttled       = true;
    i.screen_changed  = true;
    i.last_partial_us = i.now_us - 30000000LL;
    TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i));
}

/* Bench-fix review M1: rule 2 (`!dirty && !wants_full`) ignores `screen_changed`, contradicting
 * rule 3b (test_screen_change_forces_full_even_when_moving above) and ui.c's own first guard
 * (render_and_refresh(): `if (!changed && !s_wants_full && !s_screen_changed)`), which treats the
 * two flags identically. A `dirty=false, wants_full=false, screen_changed=true` input must still
 * refresh, mirroring test_wants_full_alone_refreshes for the third flag. */
static void test_screen_change_alone_refreshes(void)
{
    rf_in_t i = base();
    i.dirty          = false;
    i.wants_full     = false;
    i.screen_changed = true;
    i.still          = false;
    TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i));
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
    RUN_TEST(test_screen_change_forces_full_even_when_moving);
    RUN_TEST(test_screen_change_still_partial_when_throttled);
    RUN_TEST(test_screen_change_alone_refreshes);
    RUN_TEST(test_full_every_zero_is_none);
    RUN_TEST(test_null_input_is_none);
    return UNITY_END();
}
