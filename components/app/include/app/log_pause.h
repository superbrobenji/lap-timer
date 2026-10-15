#ifndef APP_LOG_PAUSE_H
#define APP_LOG_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

/* app/log_pause.h -- §12.6 "Logging pauses (no FIX_ or FUSED records) when `still` for 10 s; resumes with a
 * FIX_KEY". Pure: no IDF, no FreeRTOS, no statics of its own, so the whole rule is host-testable
 * (test/test_log_pause.c) instead of living inline in logger.c's task loop (app:M6).
 *
 * LOG_PAUSE_STILL_MS is the Appendix A constant LOG_PAUSE_STILL_S = 10 in milliseconds; it lives
 * here because core/consts.h (F1a) and app/lt_consts.h (F2a) are other plans' files in this wave.
 * F4 adds the Appendix A row; moving the macro there later is a pure relocation. */
#define LOG_PAUSE_STILL_MS 10000u

typedef struct {
    uint32_t still_since_ms;   /* now_ms of the EV_STILL edge; meaningful only while `still` */
    bool     still;
    bool     paused;
} log_pause_t;

void log_pause_init(log_pause_t *p);
/* EV_STILL edge. Idempotent: a repeat edge does NOT restart the 10 s countdown. */
void log_pause_still(log_pause_t *p, uint32_t now_ms);
/* EV_MOTION edge. True when a pause was active, i.e. the caller must re-key the FIX/FUSED delta
 * codecs so logging resumes with a FIX_KEY (§12.4/§12.6). */
bool log_pause_motion(log_pause_t *p);
/* Per loop tick. True exactly once: on the tick the pause engages (so the caller logs it once). */
bool log_pause_tick(log_pause_t *p, uint32_t now_ms);
bool log_pause_active(const log_pause_t *p);

#endif /* APP_LOG_PAUSE_H */
