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
