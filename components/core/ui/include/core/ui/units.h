#ifndef CORE_UI_UNITS_H
#define CORE_UI_UNITS_H
#include <stdint.h>

/* Speed unit conversion (Plan 7c T1): converts a cm/s speed (as carried on the wire/in
 * lap_stats_t) to a whole display unit, rounded to nearest, for the display's unit toggle.
 * `units`: 0 = km/h, 1 = mph. An invalid `units` (> 1) reports UNITS_ASSERT_CODE and returns 0.
 * The result saturates at 65535 rather than wrapping. */
uint16_t speed_display(uint16_t cms, uint8_t units);

/* Distance unit conversion (bench B4-F5, #96): converts a metre distance (as stored in
 * drag_row_t.dist_m, e.g. the 100-0 braking gate) to a whole display unit, rounded to nearest, at
 * render time -- same rule as speed_display above (freezing the value in the display unit at
 * event time would mislabel it after a later Distance: menu toggle). `dist_units`: 0 = metres
 * (CFG_DIST_M, pass-through), 1 = feet (CFG_DIST_FT). An invalid `dist_units` (> 1) reports
 * UNITS_ASSERT_CODE and returns 0. The result saturates at 65535 rather than wrapping. This is the
 * one place the metre/feet VALUE conversion lives; drag_gate_label (core/dragengine/drag_cfg.c)
 * is the analogous LABEL-side conversion for the three feet-preset DIST gate names. */
uint16_t dist_display(uint16_t dist_m, uint8_t dist_units);

#endif
