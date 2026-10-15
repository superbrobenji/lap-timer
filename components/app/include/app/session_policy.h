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

/* B-1 (F2b closure 4, ruling F-15): the pipeline's CLOSE send can fail (queue full). A CLOSE that was
 * NOT enqueued leaves the lifecycle exactly as it was (mirror open, confirmation, venue/layout
 * unchanged) and records a PENDING action here; session_periodic re-attempts it on the 60 s cadence
 * through one path. Kept as pure helpers (not session_liveness inputs): the liveness decision
 * stays about the logger's reported state, and the pending state is a separate lifecycle intent
 * whose transitions are host-tested below. */
typedef enum {
    SES_PEND_NONE   = 0,
    SES_PEND_CLOSE  = 1,   /* a plain close (EV_VENUE_LOST) whose CLOSE was not enqueued */
    SES_PEND_ROTATE = 2,   /* a rotation whose CLOSE was not enqueued; the OPEN has NOT been posted */
} ses_pend_t;

/* After a CLOSE for `act` (SES_ACT_CLOSE or SES_ACT_REOPEN): the pending action to record, and
 * whether the rotation's OPEN may be posted now (only once the CLOSE was enqueued). */
ses_pend_t session_close_outcome(ses_action_t act, bool close_sent, bool *post_open);
/* The action a pending record re-attempts. */
ses_action_t session_pend_act(ses_pend_t pend);
/* True when a pending action is due for its re-attempt (same cadence as the OPEN retry). */
bool session_pend_due(ses_pend_t pend, int64_t now_us, int64_t last_try_us, int64_t retry_interval_us);
/* A venue acquisition that needs no action of its own (the open session's venue was found again)
 * while `pend` is pending: true when it supersedes (cancels) the pending action -- a pending close
 * (the venue is back), or a rotation aimed at `pend_venue_id` other than the venue found. A mode
 * rotation keeps its venue, so it is not cancelled. */
bool session_pend_cancels_on_found(ses_pend_t pend, uint16_t venue_id, uint16_t pend_venue_id);

#endif /* APP_SESSION_POLICY_H */
