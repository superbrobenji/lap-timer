#include "unity.h"
#include "app/sup_policy.h"
#include "app/lt_consts.h"
#include "assert_support.h"
void setUp(void) {} void tearDown(void) {}

/* ---- §17.2 stall episodes (R9: one log line per episode, not one per second) ---- */
static void test_stall_progressing_is_quiet(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 0, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 8, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT32(8, st.last_hb);
    TEST_ASSERT_EQUAL_UINT32(0, st.stalled_s);
}
static void test_stall_logs_once_per_episode(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 0, .msg_logged = false };
    for (int s = 1; s < 5; s++) TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 7, 5, 1, false));  /* 5 s: fires */
    TEST_ASSERT_EQUAL_UINT32(5, st.stalled_s);
    for (int s = 0; s < 10; s++) TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT32(15, st.stalled_s);                 /* keeps the true duration */
}
static void test_stall_new_episode_logs_again(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 4, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 9, 5, 1, false));          /* recovered */
    TEST_ASSERT_FALSE(st.msg_logged);
    st.stalled_s = 4;
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 9, 5, 1, false));
}
static void test_stall_restartable_adds_restart_bit(void)
{
    sup_stall_t st = { .last_hb = 1, .stalled_s = 0, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 2, 1, true));           /* 1 s < 2 s window: quiet */
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG | SUP_STALL_RESTART, sup_stall_step(&st, 1, 2, 1, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_RESTART, sup_stall_step(&st, 1, 2, 1, true));
}
static void test_stall_rejects_bad_args(void)
{
    sup_stall_t st = { .last_hb = 3, .stalled_s = 2, .msg_logged = true };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(NULL, 1, 5, 1, true));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 0, 1, true));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 5, 0, true));
    TEST_ASSERT_EQUAL_UINT32(3, st.last_hb);                    /* bad args leave the state untouched */
    TEST_ASSERT_EQUAL_UINT32(2, st.stalled_s);
    TEST_ASSERT_TRUE(st.msg_logged);
}
static void test_stall_window_not_multiple_of_period(void)
{
    /* window 5, period 2: the report fires on the first tick at or past the window (stalled 6). */
    sup_stall_t st = { .last_hb = 7, .stalled_s = 0, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 2, false));          /* stalled 2 */
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 2, false));          /* stalled 4 */
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 7, 5, 2, false)); /* stalled 6 */
    TEST_ASSERT_EQUAL_UINT32(6, st.stalled_s);
}

/* ---- §17.2 heap hysteresis ---- */
static void test_heap_hysteresis(void)
{
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(60u * 1024u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_ENTER, sup_heap_eval(HEAP_LOW_KB * 1024u - 1u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_LOW_KB * 1024u - 1u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_CLEAR_KB * 1024u - 1u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_CLEAR, sup_heap_eval(HEAP_CLEAR_KB * 1024u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_LOW_KB * 1024u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(0u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_ENTER, sup_heap_eval(0u, false));
}

/* ---- §17.2 stack ---- */
static void test_stack_low(void)
{
    TEST_ASSERT_TRUE(sup_stack_low(0u));
    TEST_ASSERT_TRUE(sup_stack_low(STACK_LOW_B - 1u));
    TEST_ASSERT_FALSE(sup_stack_low(STACK_LOW_B));
    TEST_ASSERT_FALSE(sup_stack_low(340u));                 /* #53's measured ui headroom: not low */
}

/* ---- §17.2 EV_FAULT: walk the changed bits ---- */
static void test_flag_next_change(void)
{
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x5u, 0x5u, 0));
    TEST_ASSERT_EQUAL_UINT8(0,  sup_flag_next_change(0x0u, 0x1u, 0));
    TEST_ASSERT_EQUAL_UINT8(3,  sup_flag_next_change(0x8u, 0x0u, 0));        /* a CLEARED bit counts */
    TEST_ASSERT_EQUAL_UINT8(10, sup_flag_next_change(0x0u, 0x401u, 1));
    TEST_ASSERT_EQUAL_UINT8(31, sup_flag_next_change(0x0u, 0x80000000u, 0));
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x0u, 0x1u, 1));
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x0u, 0xFFFFFFFFu, 32));
}

/* ---- §19.5 OTA preconditions ---- */
static ota_pre_t good_pre(void)
{
    ota_pre_t p = { .still_ms = OTA_STILL_MIN_MS, .size = 600000u, .in_flight = false,
                    .session_open = false, .batt_ok = true, .flag_pending = false,
                    .slot_pending_verify = false };
    return p;
}
static void test_precond_ok(void)
{
    ota_pre_t p = good_pre();
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_OK, ota_precond_eval(&p, NULL));
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(NULL, NULL));
}
static void test_precond_refusals(void)
{
    ota_pre_t p = good_pre(); p.in_flight = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.size = 0u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.size = OTA_MAX_IMG_BYTES + 1u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.size = OTA_MAX_IMG_BYTES;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_OK, ota_precond_eval(&p, NULL));           /* inclusive bound */
    p = good_pre(); p.still_ms = OTA_STILL_MIN_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));       /* §19.5 moving */
    p = good_pre(); p.batt_ok = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.flag_pending = true; p.slot_pending_verify = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));       /* a real image awaits */
}
static void test_precond_slot_pending_refuses_without_flag(void)
{
    /* S3-17 (F-2): the slot is the authority. A lost flag (NVS GC) with the running slot still
     * PENDING_VERIFY must refuse BEFORE the session close, not after it via esp_ota_begin. */
    ota_pre_t p = good_pre(); p.flag_pending = false; p.slot_pending_verify = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));   /* never reaches the close */
}
static void test_precond_reports_the_refusing_reason(void)
{
    /* S3-19 (F-11): the reason is the E_OTA_PRECOND errlog arg; first failing check wins. */
    uint8_t why = 0xFFu;
    ota_pre_t p = good_pre();
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_OK, ota_precond_eval(&p, &why));
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_NONE, why);
    p = good_pre(); p.in_flight = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, &why));
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_IN_FLIGHT, why);
    p = good_pre(); p.size = 0u; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_SIZE, why);
    p = good_pre(); p.size = OTA_MAX_IMG_BYTES + 1u; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_SIZE, why);
    p = good_pre(); p.still_ms = 0u; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_STILL, why);
    p = good_pre(); p.batt_ok = false; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_BATT, why);
    p = good_pre(); p.slot_pending_verify = true; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_SLOT_PENDING, why);
    p = good_pre(); p.in_flight = true; p.batt_ok = false; (void)ota_precond_eval(&p, &why);
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_IN_FLIGHT, why);                      /* first failing check wins */
    p = good_pre(); p.session_open = true; why = 0xFFu;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p, &why));
    TEST_ASSERT_EQUAL_UINT8(OTA_WHY_NONE, why);                           /* a go-ahead carries no reason */
}
static void test_precond_close_session_runs_last(void)
{
    /* The close is a SIDE EFFECT on a live session, so it must only be asked for once every
     * other precondition already holds -- a refused push must never end a riding session. */
    ota_pre_t p = good_pre(); p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p, NULL));
    p.still_ms = 0u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.session_open = true; p.batt_ok = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
    p = good_pre(); p.session_open = true; p.flag_pending = true; p.slot_pending_verify = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p, NULL));
}
static void test_precond_stale_flag_is_cleared_not_refused(void)
{
    /* app:C1's dead end: ota_pend set with no PENDING_VERIFY slot refused every later push
     * forever. R1: clear it and proceed. */
    ota_pre_t p = good_pre(); p.flag_pending = true; p.slot_pending_verify = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLEAR_STALE, ota_precond_eval(&p, NULL));
    p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p, NULL));   /* close first */
}
static void test_precond_second_eval_terminates(void)
{
    /* The caller's loop is: eval -> CLOSE_SESSION -> close -> eval. The second eval must never
     * return CLOSE_SESSION again, or the caller would loop. */
    ota_pre_t p = good_pre(); p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p, NULL));
    p.session_open = false;
    uint8_t act = ota_precond_eval(&p, NULL);
    TEST_ASSERT_TRUE(act == OTA_PRE_OK || act == OTA_PRE_CLEAR_STALE);
}

/* ---- §19.6 receive staleness (wrap-safe u32 ms) ---- */
static void test_recv_stale(void)
{
    TEST_ASSERT_FALSE(ota_recv_stale(1000u, 1000u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(1000u + 29999u, 1000u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_TRUE(ota_recv_stale(1000u + 30000u, 1000u, OTA_RECV_TIMEOUT_S));
    /* the 49-day millisecond wrap must not read as "fresh for 49 days" */
    TEST_ASSERT_TRUE(ota_recv_stale(30000u, 0xFFFFFFFFu, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(10u, 0xFFFFFFF0u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(1000u, 1000u, 0u));          /* a 0 s timeout never fires */
    /* the timeout_s bound (* 1000 must fit u32): 4000000 s is accepted, 4000001 s trips the assert */
    lt_test_assert_reset();
    TEST_ASSERT_FALSE(ota_recv_stale(0u, 0u, 4000000u));
    TEST_ASSERT_EQUAL_UINT(0, lt_test_assert_count());
    TEST_ASSERT_FALSE(ota_recv_stale(0u, 0u, 4000001u));
    TEST_ASSERT_EQUAL_UINT(1, lt_test_assert_count());
    TEST_ASSERT_EQUAL_HEX16(0x0BC0, lt_test_assert_last_code());   /* SUP_POLICY_ASSERT_CODE (sup_policy.c) */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stall_progressing_is_quiet);
    RUN_TEST(test_stall_logs_once_per_episode);
    RUN_TEST(test_stall_new_episode_logs_again);
    RUN_TEST(test_stall_restartable_adds_restart_bit);
    RUN_TEST(test_stall_rejects_bad_args);
    RUN_TEST(test_stall_window_not_multiple_of_period);
    RUN_TEST(test_heap_hysteresis);
    RUN_TEST(test_stack_low);
    RUN_TEST(test_flag_next_change);
    RUN_TEST(test_precond_ok);
    RUN_TEST(test_precond_refusals);
    RUN_TEST(test_precond_slot_pending_refuses_without_flag);
    RUN_TEST(test_precond_reports_the_refusing_reason);
    RUN_TEST(test_precond_close_session_runs_last);
    RUN_TEST(test_precond_stale_flag_is_cleared_not_refused);
    RUN_TEST(test_precond_second_eval_terminates);
    RUN_TEST(test_recv_stale);
    return UNITY_END();
}
