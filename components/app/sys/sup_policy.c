#include "app/sup_policy.h"

#include <stddef.h>

#include "app/lt_assert.h"
#include "app/lt_consts.h"

#define SUP_POLICY_ASSERT_CODE 0x0BC0   /* Power of 10 rule 5 (app/lt_assert.h); this file's code */

/* §17.2 line 1, one watched task, one supervisor tick. Returns the actions the caller must take:
 * SUP_STALL_LOG exactly ONCE per stall episode (R9 -- the old code re-armed every stall_s and so
 * printed an ESP_LOGE at 1 Hz straight into framed transfers and the raw OTA stream, app:M12),
 * and SUP_STALL_RESTART while a restartable task (the pipeline) is over its window. stalled_s
 * keeps accumulating across the episode so the logged figure is the true stall duration. */
uint8_t sup_stall_step(sup_stall_t *st, uint32_t cur_hb, uint32_t stall_s, uint32_t period_s,
                       bool restartable)
{
    LT_ASSERT_RET(st != NULL, SUP_POLICY_ASSERT_CODE, 0u);              /* caller passes &s_watch[i].stall */
    LT_ASSERT_RET(stall_s > 0u && period_s > 0u, SUP_POLICY_ASSERT_CODE, 0u);  /* a 0 window/period would never fire */
    if (cur_hb != st->last_hb) {                 /* progressing: the episode (if any) is over */
        st->last_hb = cur_hb;
        st->stalled_s = 0u;
        st->msg_logged = false;
        return 0u;
    }
    st->stalled_s += period_s;
    if (st->stalled_s < stall_s) return 0u;
    uint8_t act = 0u;
    if (!st->msg_logged) { st->msg_logged = true; act = (uint8_t)(act | SUP_STALL_LOG); }
    if (restartable) act = (uint8_t)(act | SUP_STALL_RESTART);
    return act;
}

/* §17.2 heap line + §4.8's "min-free heap stays above 40 KB" hard rule, which had no enforcement
 * at all (app:M5: SYS_HEAP_LOW and E_SYS_HEAP_LOW have zero writers). free_b is the CURRENT free
 * 8-bit heap, not heap_caps_get_minimum_free_size(): the min-ever figure is monotonically
 * non-increasing, so a flag driven by it could never clear and R6's recovery threshold would be
 * dead code. The caller logs the min-ever figure as the errlog arg instead. */
uint8_t sup_heap_eval(uint32_t free_b, bool flagged)
{
    if (!flagged && free_b < (uint32_t)HEAP_LOW_KB * 1024u)   return SUP_HEAP_ENTER;
    if (flagged  && free_b >= (uint32_t)HEAP_CLEAR_KB * 1024u) return SUP_HEAP_CLEAR;
    return SUP_HEAP_NONE;
}

/* §17.2 stack line (app:M5: E_SYS_STACK_LOW had no writer -- this is the check that would have
 * surfaced #53's 340 B ui headroom automatically). high_water_b is
 * uxTaskGetStackHighWaterMark()'s value: on ESP-IDF Xtensa StackType_t is 1 byte, so the count
 * IS bytes. */
bool sup_stack_low(uint32_t high_water_b) { return high_water_b < (uint32_t)STACK_LOW_B; }

/* §17.2's "EV_FAULT on changes": the index of the lowest sys_flags bit at or above `from` that
 * differs between prev and cur, or 32 when there is none. Bounded by 32 iterations (rule 2);
 * a SET and a CLEARED bit both count as a change, so the ui's fault strip learns about recovery
 * as well as onset. */
uint8_t sup_flag_next_change(uint32_t prev, uint32_t cur, uint8_t from)
{
    uint32_t diff = prev ^ cur;
    for (uint8_t b = from; b < 32u; b++) { if ((diff >> b) & 1u) return b; }
    return 32u;
}

/* §19.5, the hardware-independent half (ruling R2; app:H1 found ota_begin checking almost none of
 * it). Ordering is the design: every cheap, side-effect-free refusal is evaluated BEFORE
 * OTA_PRE_CLOSE_SESSION, because closing the logger session ends a rider's session and must never
 * happen for a push that was going to be refused anyway. OTA_PRE_CLEAR_STALE is R1's escape from
 * the app:C1 dead end (an ota_pend flag with no PENDING_VERIFY slot refused every later push
 * until a USB flash). Struck from §19.5 by R2: power state CONNECTED (there is no power task --
 * Plan 6) and fus_is_still (fusion is never calibrated -- Plan 8/#93); the 30 s no-motion window
 * is honoured through still_ms. */
uint8_t ota_precond_eval(const ota_pre_t *in)
{
    LT_ASSERT_RET(in != NULL, SUP_POLICY_ASSERT_CODE, OTA_PRE_REFUSE);   /* caller passes its own local */
    if (in->in_flight) return OTA_PRE_REFUSE;
    if (in->size == 0u || in->size > OTA_MAX_IMG_BYTES) return OTA_PRE_REFUSE;
    if (in->still_ms < OTA_STILL_MIN_MS) return OTA_PRE_REFUSE;
    if (!in->batt_ok) return OTA_PRE_REFUSE;
    if (in->flag_pending && in->slot_pending_verify) return OTA_PRE_REFUSE;
    if (in->session_open) return OTA_PRE_CLOSE_SESSION;      /* side effect: only once all of the above hold */
    return in->flag_pending ? OTA_PRE_CLEAR_STALE : OTA_PRE_OK;
}

/* §19.6: an OTA_RECV that has seen no OTA_DATA for timeout_s is dead (app:M8 -- a vanished peer
 * used to wedge the state machine until a reboot, and every later push then returned
 * E_OTA_PRECOND). Milliseconds in a u32 so the stamp is a single naturally-atomic word across the
 * console/cmd writer and the supervisor reader; the subtraction is modular, so the 49-day wrap
 * reads correctly rather than as 49 days of freshness. */
bool ota_recv_stale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_s)
{
    if (timeout_s == 0u) return false;
    LT_ASSERT_RET(timeout_s <= 4000000u, SUP_POLICY_ASSERT_CODE, false);   /* * 1000 must not overflow u32 */
    return (uint32_t)(now_ms - last_ms) >= timeout_s * 1000u;
}
