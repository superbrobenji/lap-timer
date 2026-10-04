/* Refresh decision (spec §20.3). See refresh_policy.h for the rule order and rationale; this file
 * just applies the rules verbatim. Pure C11, no ESP-IDF/FreeRTOS/malloc/float/libm — a function of
 * its argument only, which is what makes it host-testable (test/test_refresh_policy.c) and reused
 * unchanged in the on-target core_selftest build. */
#include "core/ui/refresh_policy.h"
#include "core/core.h"

#include <stddef.h> /* NULL */

/* Power of 10 rule 5 (spec §17.9): this module's assertions report RF_ASSERT_CODE. They guard a
 * NULL input pointer and a full_every below its cfg-enforced 1..50 range (both caller bugs this
 * module backstops with the conservative RF_NONE) — never the ordinary rule evaluation below,
 * which is this function's whole job and is exercised by the table test's eight cases.
 * 0x0AC0 continues the codebase's per-module assertion-code sequence (fus 0x0A10, lap 0x0A20,
 * drag 0x0A30, tb 0x0A40, ses 0x0A50, exp 0x0A60, cfg 0x0A70, trk 0x0A80, geo 0x0A90, ui 0x0AA0,
 * json 0x0AB0) — the next free slot after json's 0x0AB0. */
#define RF_ASSERT_CODE 0x0AC0

#define RF_THROTTLE_MIN_US  30000000LL   /* 30 s: minimum spacing between partials while throttled */
#define RF_FULL_MAX_AGE_US  1800000000LL /* 30 min: force a full regardless of motion */

rf_kind_t ui_refresh_decide(const rf_in_t *in)
{
    CORE_ASSERT_RET(in != NULL, RF_ASSERT_CODE, RF_NONE);
    CORE_ASSERT_RET(in->full_every >= 1, RF_ASSERT_CODE, RF_NONE);

    if (in->dead) {
        return RF_NONE;
    }
    if (!in->dirty && !in->wants_full && !in->screen_changed) {
        /* M1 (review fix round): screen_changed must not be dropped here -- rule 3b below is the
         * one that promotes it to RF_FULL on its own, mirroring ui.c's own render_and_refresh()
         * guard, which already treats screen_changed exactly like wants_full. */
        return RF_NONE;
    }
    if (in->throttled) {
        if (in->now_us - in->last_partial_us >= RF_THROTTLE_MIN_US) {
            return RF_PARTIAL;
        }
        return RF_NONE;
    }
    if (in->screen_changed) {
        /* Rule 3b (ruling B-9): a whole-screen replacement is never a partial, regardless of
         * motion -- checked after the throttle rule above, so a throttled screen change still
         * only gets (at most) a partial. */
        return RF_FULL;
    }

    bool moving_cap = in->partial_count >= (uint16_t)(2u * in->full_every);
    bool still_full = in->still && ((in->wants_full) || (in->partial_count >= in->full_every));
    bool aged_out    = (in->now_us - in->last_full_us) >= RF_FULL_MAX_AGE_US;
    if (still_full || aged_out || moving_cap) {
        return RF_FULL;
    }
    return RF_PARTIAL;
}
