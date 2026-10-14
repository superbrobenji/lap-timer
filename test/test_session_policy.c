#include "unity.h"
#include "app/session_policy.h"
#include "assert_support.h"
void setUp(void) {} void tearDown(void) {}

static void test_venue_found_opens_only_when_closed(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_OPEN, session_policy(false, SES_IN_VENUE_FOUND));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true,  SES_IN_VENUE_FOUND));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_announce_only_never_touches_the_session(void)
{
    /* The CFG_GPS_SIM boot venue and the §15.3 RTC resume announce a venue the pipeline set
     * itself; the sim boot path opens its session explicitly and a resume after a reboot opens a
     * NEW session itself (B5-5, resume_open_session), so neither may open or close anything here. */
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(false, SES_IN_VENUE_ANNOUNCE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true,  SES_IN_VENUE_ANNOUNCE));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_venue_lost_closes_only_when_open(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_CLOSE, session_policy(true,  SES_IN_VENUE_LOST));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE,  session_policy(false, SES_IN_VENUE_LOST));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_mode_change_rotates_only_a_live_session(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_REOPEN, session_policy(true,  SES_IN_MODE_CHANGE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE,   session_policy(false, SES_IN_MODE_CHANGE));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_venue_changed_rotates_a_live_session(void)
{
    /* I-4: a VENUE_FOUND naming a different venue while open ends the session and starts a new
     * one; with nothing open it is a plain open. */
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_REOPEN, session_policy(true,  SES_IN_VENUE_CHANGED));
    TEST_ASSERT_EQUAL_INT(SES_ACT_OPEN,   session_policy(false, SES_IN_VENUE_CHANGED));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_unknown_input_is_inert(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true, (ses_input_t)7));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(false, (ses_input_t)200));
    TEST_ASSERT_EQUAL_UINT(2u, lt_test_assert_count());
}
#define IV 60000000LL
static void test_live_old_session_after_rotate_is_not_a_confirm(void)
{
    /* N-1: right after a rotate the OLD session still reads open; its generation equals the one
     * recorded at post time, so it must not latch the confirm. */
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 5u, 5u, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 6u, 5u, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 6u, 5u, true, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 0u, 0xFFFFFFFFu, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_live_refused_open_after_rotate_retries_after_60s(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE,  session_liveness(false, 5u, 5u, false, IV - 1, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_RETRY, session_liveness(false, 5u, 5u, false, IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_live_confirmed_then_closed_drops_without_retry(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_DROP, session_liveness(false, 6u, 5u, true, 10 * IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_venue_found_opens_only_when_closed);
    RUN_TEST(test_announce_only_never_touches_the_session);
    RUN_TEST(test_venue_lost_closes_only_when_open);
    RUN_TEST(test_mode_change_rotates_only_a_live_session);
    RUN_TEST(test_venue_changed_rotates_a_live_session);
    RUN_TEST(test_unknown_input_is_inert);
    RUN_TEST(test_live_old_session_after_rotate_is_not_a_confirm);
    RUN_TEST(test_live_refused_open_after_rotate_retries_after_60s);
    RUN_TEST(test_live_confirmed_then_closed_drops_without_retry);
    return UNITY_END();
}
