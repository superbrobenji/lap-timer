#ifndef APP_SESSION_POLICY_H
#define APP_SESSION_POLICY_H

#include <stdbool.h>

/* app/session_policy.h -- §12.1 "A session starts at boot (or resume) and ends at PARK/SHUTDOWN or
 * when the venue changes. A new session also starts on mode change." (R4; audit §3.3 #24/#25.)
 *
 * Pure decision table, host-tested, so the pipeline's engine_cb/handle_set_mode only act on the
 * verdict. `open_now` is the pipeline's own advisory mirror of what it has asked the logger for --
 * the logger stays the authority (it ignores an OPEN while open and a CLOSE while closed), this
 * just stops the pipeline asking for a second session per venue scan or rotating a session that
 * never existed. */
typedef enum {
    SES_ACT_NONE   = 0,
    SES_ACT_OPEN   = 1,
    SES_ACT_CLOSE  = 2,
    SES_ACT_REOPEN = 3,   /* CLOSE{SES_END_NORMAL} then OPEN, in that queue order */
} ses_action_t;

typedef enum {
    SES_IN_VENUE_FOUND    = 0,   /* the ENGINE's own acquisition (event_t.flags has no announce bit) */
    SES_IN_VENUE_ANNOUNCE = 1,   /* the pipeline's ui-only announce (EV_VENUE_ANNOUNCE_ONLY) */
    SES_IN_VENUE_LOST     = 2,   /* EV_VENUE_LOST (F1a, design §3) */
    SES_IN_MODE_CHANGE    = 3,   /* CMD_SET_MODE, or a CMD_CONFIG_RELOAD that flips cfg.mode */
} ses_input_t;

ses_action_t session_policy(bool open_now, ses_input_t in);

#endif /* APP_SESSION_POLICY_H */
