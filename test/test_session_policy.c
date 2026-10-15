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
    /* R-1: the latch confirms only the logger's completion of THIS (latest) OPEN: the OPEN-request
     * count at the logger's last completed open must equal the count the pipeline has enqueued.
     * Right after a rotate the OLD session still reads open with an older count: no confirm. */
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 4u, 5u, true, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 5u, 5u, true, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 5u, 5u, true, true, 1000, 0, IV));
    /* wrap-safe: exact uint32 equality across 0xFFFFFFFF -> 0 */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 0u, 0u, true, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 0xFFFFFFFFu, 0u, true, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
/* R-1(a): two OPENs enqueued (sent == 2) before the logger confirms the first. The logger completes
 * OPEN1 (count at open 1), then CLOSE2 (closed), then OPEN2 (count at open 2). Walk the passes. */
static void test_live_two_opens_first_completion_does_not_confirm_second(void)
{
    lt_test_assert_reset();
    /* logger open from OPEN1 (at_open 1), sent 2: not ours */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 1u, 2u, true, false, 1000, 0, IV));
    /* CLOSE2 done, OPEN2 not yet complete: closed + unconfirmed must NOT drop (and no early retry) */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(false, 1u, 2u, true, false, 2000, 0, IV));
    /* OPEN2 completes: now confirmed */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 2u, 2u, true, false, 3000, 0, IV));
    /* confirmed, then an external close: drop */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_DROP, session_liveness(false, 2u, 2u, true, true, 4000, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
/* R-1(b): the logger is busy and the confirmation arrives several passes late; every pass before
 * it is NONE (open with an old count, or closed inside the retry interval), the late one confirms. */
static void test_live_busy_logger_late_confirmation_confirms_right_open(void)
{
    lt_test_assert_reset();
    int64_t t = 50000;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 7u, 8u, true, false, t, 0, IV));
        t += 50000;
        TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(false, 7u, 8u, true, false, t, 0, IV));
        t += 50000;
    }
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 8u, 8u, true, false, t, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_live_refused_open_after_rotate_retries_after_60s(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE,  session_liveness(false, 4u, 5u, true, false, IV - 1, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_RETRY, session_liveness(false, 4u, 5u, true, false, IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_live_confirmed_then_closed_drops_without_retry(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_DROP, session_liveness(false, 5u, 5u, true, true, 10 * IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}

/* N-A: a rotation's OPEN whose enqueue FAILED (queue full): the logger still reads open with the OLD
 * session at at_open == sent. It must not confirm (the CLOSE then drops it with no retry); once the
 * CLOSE lands the closed + unconfirmed mirror retries at 60 s, and a successful send resumes confirms. */
static void test_live_failed_open_send_never_confirms_and_retries(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 5u, 5u, false, false, 1000, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(false, 5u, 5u, false, false, IV - 1, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_RETRY, session_liveness(false, 5u, 5u, false, false, IV, 0, IV));
    /* the retry's send succeeded: the latch clears, normal confirmation resumes */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 6u, 6u, true, false, IV + 1000, IV, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
/* N-B: an OPEN the logger counted but dropped (never opened) leaves at_open behind sent by one only
 * until the retry; the closed mirror retries, it is not a permanent mismatch. */
static void test_live_counted_but_dropped_open_retries(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_RETRY, session_liveness(false, 3u, 4u, true, false, IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}

/* B-1 (closure 4, ruling F-15): a CLOSE that was not enqueued leaves the lifecycle as it was and
 * records a PENDING action; the OPEN is posted only after the CLOSE was enqueued. */
static void test_pend_failed_close_of_rotation_posts_no_open(void)
{
    lt_test_assert_reset();
    bool post_open = true;
    TEST_ASSERT_EQUAL_INT(SES_PEND_ROTATE, session_close_outcome(SES_ACT_REOPEN, false, &post_open));
    TEST_ASSERT_FALSE(post_open);   /* adjacent note 1: CLOSE failed, so the OPEN is NOT sent */
    post_open = true;
    TEST_ASSERT_EQUAL_INT(SES_PEND_CLOSE, session_close_outcome(SES_ACT_CLOSE, false, &post_open));
    TEST_ASSERT_FALSE(post_open);
    TEST_ASSERT_EQUAL_INT(SES_PEND_NONE, session_close_outcome(SES_ACT_REOPEN, true, &post_open));
    TEST_ASSERT_TRUE(post_open);
    TEST_ASSERT_EQUAL_INT(SES_PEND_NONE, session_close_outcome(SES_ACT_CLOSE, true, &post_open));
    TEST_ASSERT_FALSE(post_open);
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_pend_failed_rotation_old_session_then_external_close_drops(void)
{
    /* The mirror still says open+confirmed with the OLD session at at_open == sent: NONE while the
     * logger is open, and a later external close (OTA) is DROP, never RETRY, however long ago the
     * last try was. */
    lt_test_assert_reset();
    TEST_ASSERT_EQUAL_INT(SES_LIVE_NONE, session_liveness(true, 3u, 3u, true, true, 10 * IV, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_LIVE_DROP, session_liveness(false, 3u, 3u, true, true, 10 * IV, 0, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_pend_retry_cadence_then_open_after_close_enqueued(void)
{
    lt_test_assert_reset();
    TEST_ASSERT_FALSE(session_pend_due(SES_PEND_ROTATE, IV - 1, 0, IV));
    TEST_ASSERT_TRUE(session_pend_due(SES_PEND_ROTATE, IV, 0, IV));
    TEST_ASSERT_TRUE(session_pend_due(SES_PEND_CLOSE, IV, 0, IV));
    TEST_ASSERT_FALSE(session_pend_due(SES_PEND_NONE, 100 * IV, 0, IV));
    TEST_ASSERT_EQUAL_INT(SES_ACT_REOPEN, session_pend_act(SES_PEND_ROTATE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_CLOSE, session_pend_act(SES_PEND_CLOSE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_pend_act(SES_PEND_NONE));
    bool post_open = false;
    /* retry 1: queue still full -> stays pending; retry 2: enqueued -> OPEN follows, pending cleared */
    TEST_ASSERT_EQUAL_INT(SES_PEND_ROTATE, session_close_outcome(session_pend_act(SES_PEND_ROTATE), false, &post_open));
    TEST_ASSERT_FALSE(post_open);
    TEST_ASSERT_EQUAL_INT(SES_PEND_NONE, session_close_outcome(session_pend_act(SES_PEND_ROTATE), true, &post_open));
    TEST_ASSERT_TRUE(post_open);
    /* the OPEN then confirms normally */
    TEST_ASSERT_EQUAL_INT(SES_LIVE_CONFIRM, session_liveness(true, 4u, 4u, true, false, 2 * IV, IV, IV));
    TEST_ASSERT_EQUAL_UINT(0u, lt_test_assert_count());
}
static void test_pend_newer_event_supersedes(void)
{
    lt_test_assert_reset();
    /* a VENUE_LOST on a pending rotation becomes a pending close; a new rotation replaces the target */
    TEST_ASSERT_TRUE(session_pend_cancels_on_found(SES_PEND_CLOSE, 1u, 1u));
    TEST_ASSERT_TRUE(session_pend_cancels_on_found(SES_PEND_ROTATE, 2u, 1u));   /* back at A, rotation was to B */
    TEST_ASSERT_FALSE(session_pend_cancels_on_found(SES_PEND_ROTATE, 1u, 1u));  /* mode rotation keeps its venue */
    TEST_ASSERT_FALSE(session_pend_cancels_on_found(SES_PEND_NONE, 1u, 1u));
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
    RUN_TEST(test_live_two_opens_first_completion_does_not_confirm_second);
    RUN_TEST(test_live_busy_logger_late_confirmation_confirms_right_open);
    RUN_TEST(test_live_refused_open_after_rotate_retries_after_60s);
    RUN_TEST(test_live_confirmed_then_closed_drops_without_retry);
    RUN_TEST(test_live_failed_open_send_never_confirms_and_retries);
    RUN_TEST(test_live_counted_but_dropped_open_retries);
    RUN_TEST(test_pend_failed_close_of_rotation_posts_no_open);
    RUN_TEST(test_pend_failed_rotation_old_session_then_external_close_drops);
    RUN_TEST(test_pend_retry_cadence_then_open_after_close_enqueued);
    RUN_TEST(test_pend_newer_event_supersedes);
    return UNITY_END();
}
