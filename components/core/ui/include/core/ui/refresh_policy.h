#ifndef CORE_UI_REFRESH_POLICY_H
#define CORE_UI_REFRESH_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* Refresh decision (spec §20.3): a pure function of the current dirty/motion/fault state that
 * picks between no refresh, a partial (dirty-window) refresh and a full e-paper refresh. `ui.c`
 * (Plan 7 Task 7) builds one rf_in_t per drained event batch — carrying the counters/timestamps
 * as plain fields rather than this module reading them itself — and calls ui_refresh_decide()
 * once, which is what keeps this module a pure function and host-testable like the rest of
 * core/ui (no ESP-IDF/FreeRTOS, no malloc/float/libm, no I/O).
 */

typedef enum { RF_NONE = 0, RF_PARTIAL = 1, RF_FULL = 2 } rf_kind_t;

typedef struct {
    bool     dirty;              /* something changed this batch */
    bool     wants_full;         /* event asked for a full (page/menu entry, wake, UP+DOWN combo) */
    bool     screen_changed;     /* the frame replaces the whole screen: one-shot <-> riding, menu
                                   * enter/exit, page change — never partial-refreshed, regardless
                                   * of motion (ruling B-9) */
    bool     still;              /* gspeed below MENU_LOCK_SPEED_KMH */
    bool     throttled;          /* SYS_DISP_TEMP_THROTTLE set */
    bool     dead;               /* SYS_DISP_DEAD set */
    uint8_t  full_every;         /* cfg display.full_every, clamped 1..50 */
    uint16_t partial_count;      /* partials since the last full */
    int64_t  now_us;             /* microseconds, monotonic; same clock as the two below */
    int64_t  last_full_us;
    int64_t  last_partial_us;
} rf_in_t;

/* Rules, in this order (spec §20.3):
 *   1. `dead`                                        -> RF_NONE (display is not usable)
 *   2. `!dirty && !wants_full && !screen_changed`      -> RF_NONE (nothing to show; M1 review fix:
 *      screen_changed must not be dropped here the way dirty/wants_full are -- rule 3b below is
 *      the one that promotes a lone screen_changed to RF_FULL)
 *   3. `throttled`                                    -> RF_PARTIAL if now - last_partial_us >=
 *      30 s, else RF_NONE; fulls are never issued while throttled, so a `wants_full` (or
 *      `screen_changed`, rule 3b below) request is downgraded to (at most) a partial rather than
 *      promoted — ruling B-9: a throttled screen replacement still only gets a partial.
 *   3b. `screen_changed`                              -> RF_FULL (a whole-screen replacement —
 *      one-shot <-> riding, menu enter/exit, page change — is never partial-refreshed, regardless
 *      of motion; checked after rule 3, so the throttle above still wins).
 *   4. Otherwise RF_FULL if any of: (`wants_full` && `still`); (`partial_count >= full_every` &&
 *      `still`); `now_us - last_full_us >= 30 min`; or `partial_count >= 2 * full_every` (the
 *      moving cap — fires even while still moving, unlike the other still-gated full triggers).
 *   5. Otherwise RF_PARTIAL.
 * `in` must be non-NULL and `in->full_every` must be >= 1 (cfg already clamps it to 1..50; a 0
 * reaching here is a caller bug) — both are backstopped by CORE_ASSERT_RET, which reports and
 * returns the conservative RF_NONE rather than let a bad input pick a refresh kind.
 */
rf_kind_t ui_refresh_decide(const rf_in_t *in);

#endif /* CORE_UI_REFRESH_POLICY_H */
