#ifndef APP_SESSION_POLICY_H
#define APP_SESSION_POLICY_H

#include <stdbool.h>
#include <stdint.h>

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
    SES_IN_VENUE_CHANGED  = 4,   /* an engine VENUE_FOUND naming a DIFFERENT venue than the open
                                  * session's (New track at venue A, I-4): rotate, never relabel */
} ses_input_t;

ses_action_t session_policy(bool open_now, ses_input_t in);

/* R-1: what the pipeline's periodic check does about an OPEN it has asked for. `logger_open` is
 * logger_session_is_open(); `open_req_at_open` is logger_open_req_at_open() -- how many
 * LOGGER_OPEN_SESSION requests the logger had dequeued when it completed its latest open;
 * `open_req_sent` is how many OPENs the pipeline has successfully enqueued. log_req_q is FIFO and
 * the pipeline its only OPEN producer, so the logger's latest open was made by the k-th enqueued
 * OPEN exactly when at_open == k: only equality with the LATEST sent OPEN confirms. A rotation's
 * old session (older count) and the first OPEN of an overlapping pair never confirm the second. */
/* N-A: `open_send_ok` is false while the latest OPEN send FAILED (queue full): the logger then still
 * shows an older session (or the first of an overlapping pair) at a count equal to `open_req_sent`,
 * so nothing may confirm; the closed + unconfirmed mirror retries at the interval. */
typedef enum {
    SES_LIVE_NONE    = 0,
    SES_LIVE_CONFIRM = 1,   /* the logger completed the latest OPEN sent: latch confirmed */
    SES_LIVE_DROP    = 2,   /* confirmed earlier, logger now closed: closed externally, drop the mirror */
    SES_LIVE_RETRY   = 3,   /* never confirmed, logger closed, retry interval elapsed: re-post OPEN */
} ses_live_t;

ses_live_t session_liveness(bool logger_open, uint32_t open_req_at_open, uint32_t open_req_sent,
                            bool open_send_ok, bool confirmed, int64_t now_us, int64_t last_try_us,
                            int64_t retry_interval_us);

#endif /* APP_SESSION_POLICY_H */
