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

/* N-1: what the pipeline's periodic check does about an OPEN it has asked for. `logger_open` is
 * logger_session_is_open(), `gen_now` logger_session_open_gen(), `gen_at_post` the generation read
 * just BEFORE the (latest) OPEN was posted. A logger that is open at the SAME generation is the
 * session that was open when the OPEN was posted (a rotation's old session, still closing), not
 * this OPEN's -- so it never confirms. */
typedef enum {
    SES_LIVE_NONE    = 0,
    SES_LIVE_CONFIRM = 1,   /* the logger opened a session since the post: latch confirmed */
    SES_LIVE_DROP    = 2,   /* confirmed earlier, logger now closed: closed externally, drop the mirror */
    SES_LIVE_RETRY   = 3,   /* never confirmed, logger closed, retry interval elapsed: re-post OPEN */
} ses_live_t;

ses_live_t session_liveness(bool logger_open, uint32_t gen_now, uint32_t gen_at_post, bool confirmed,
                            int64_t now_us, int64_t last_try_us, int64_t retry_interval_us);

#endif /* APP_SESSION_POLICY_H */
