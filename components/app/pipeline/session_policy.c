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
