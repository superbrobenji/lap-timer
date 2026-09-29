#ifndef CORE_UI_UNITS_H
#define CORE_UI_UNITS_H
#include <stdint.h>

/* Speed unit conversion (Plan 7c T1): converts a cm/s speed (as carried on the wire/in
 * lap_stats_t) to a whole display unit, rounded to nearest, for the display's unit toggle.
 * `units`: 0 = km/h, 1 = mph. An invalid `units` (> 1) reports UNITS_ASSERT_CODE and returns 0.
 * The result saturates at 65535 rather than wrapping. */
uint16_t speed_display(uint16_t cms, uint8_t units);

#endif
