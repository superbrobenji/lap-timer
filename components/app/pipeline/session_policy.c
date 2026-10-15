#include <stddef.h>
#include "app/session_policy.h"
#include "core/core.h"

#define SES_POLICY_ASSERT_CODE 0x0B11   /* P10 rule 5; pipeline.c's 0x0B10 + 1 */

ses_action_t session_policy(bool open_now, ses_input_t in)
{
    switch (in) {
    case SES_IN_VENUE_FOUND:    return open_now ? SES_ACT_NONE   : SES_ACT_OPEN;
    case SES_IN_VENUE_LOST:     return open_now ? SES_ACT_CLOSE  : SES_ACT_NONE;
    case SES_IN_MODE_CHANGE:    return open_now ? SES_ACT_REOPEN : SES_ACT_NONE;
    case SES_IN_VENUE_CHANGED:  return open_now ? SES_ACT_REOPEN : SES_ACT_OPEN;
    case SES_IN_VENUE_ANNOUNCE: return SES_ACT_NONE;
    default: break;
    }
    /* An input outside the enum is a caller bug, not data: report it and stay inert. */
    core_assert_fail(SES_POLICY_ASSERT_CODE, __FILE__, __LINE__);
    return SES_ACT_NONE;
}

ses_live_t session_liveness(bool logger_open, uint32_t open_req_at_open, uint32_t open_req_sent,
                            bool open_send_ok, bool confirmed, int64_t now_us, int64_t last_try_us,
                            int64_t retry_interval_us)
{
    if (logger_open) {
        /* R-1: exact equality (wrap-safe): the logger's last completed open was made by the LATEST
         * OPEN the pipeline enqueued, not by an earlier one of an overlapping pair. N-A: and never while
         * the latest OPEN's enqueue failed (the equal count then belongs to an older session). */
        return (open_send_ok && !confirmed && open_req_at_open == open_req_sent) ? SES_LIVE_CONFIRM : SES_LIVE_NONE;
    }
    if (confirmed) return SES_LIVE_DROP;
    return ((now_us - last_try_us) >= retry_interval_us) ? SES_LIVE_RETRY : SES_LIVE_NONE;
}

ses_pend_t session_close_outcome(ses_action_t act, bool close_sent, bool *post_open)
{
    bool rotate = (act == SES_ACT_REOPEN);
    CORE_ASSERT_RET(post_open != NULL, SES_POLICY_ASSERT_CODE, SES_PEND_NONE);   /* out-param is mandatory */
    CORE_ASSERT_RET(rotate || act == SES_ACT_CLOSE, SES_POLICY_ASSERT_CODE, SES_PEND_NONE);   /* only a close-bearing action */
    *post_open = close_sent && rotate;      /* the OPEN follows only a CLOSE that really entered the queue */
    if (close_sent) return SES_PEND_NONE;
    return rotate ? SES_PEND_ROTATE : SES_PEND_CLOSE;
}

ses_action_t session_pend_act(ses_pend_t pend)
{
    if (pend == SES_PEND_ROTATE) return SES_ACT_REOPEN;
    return (pend == SES_PEND_CLOSE) ? SES_ACT_CLOSE : SES_ACT_NONE;
}

bool session_pend_due(ses_pend_t pend, int64_t now_us, int64_t last_try_us, int64_t retry_interval_us)
{
    return pend != SES_PEND_NONE && (now_us - last_try_us) >= retry_interval_us;
}

bool session_pend_cancels_on_found(ses_pend_t pend, uint16_t venue_id, uint16_t pend_venue_id)
{
    return pend == SES_PEND_CLOSE || (pend == SES_PEND_ROTATE && pend_venue_id != venue_id);
}
