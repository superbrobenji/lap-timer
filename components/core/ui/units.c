#include "core/ui/units.h"
#include "core/core.h"
#define UNITS_ASSERT_CODE 0x0AF1
uint16_t speed_display(uint16_t cms, uint8_t units)
{
    CORE_ASSERT_RET(units <= 1u, UNITS_ASSERT_CODE, 0u);
    /* km/h = cms * 36 / 1000; mph = cms * 36 / 1000 / 1.609344 = cms * 22369 / 1000000 (both exact to 1e-5) */
    uint32_t num = (units == 0u) ? (uint32_t)cms * 36u : (uint32_t)cms * 22369u;
    uint32_t den = (units == 0u) ? 1000u : 1000000u;
    uint32_t v   = (num + den / 2u) / den;
    /* Unreachable with a 16-bit `cms` input: the largest possible v (cms == 65535, units == 0) is
     * 2359, far under 65535u. Kept as a guard against a future formula change (e.g. a wider `cms`
     * or a unit conversion with a larger multiplier) silently truncating on the uint16_t return. */
    CORE_ASSERT_RET(v <= 65535u, UNITS_ASSERT_CODE, 65535u);
    return (uint16_t)v;
}

uint16_t dist_display(uint16_t dist_m, uint8_t dist_units)
{
    CORE_ASSERT_RET(dist_units <= 1u, UNITS_ASSERT_CODE, 0u);
    if (dist_units == 0u) {
        return dist_m; /* CFG_DIST_M: already the display unit, no conversion */
    }
    /* CFG_DIST_FT: feet = metres * 1250 / 381. The international foot is defined as exactly
     * 0.3048 m (core/consts.h's DRAG_ROLLOUT_M, which this module cannot reference -- it is a
     * double literal and core/ui stays free of float/libm), so 1/0.3048 == 1250/381 exactly, in
     * lowest terms (381 = 3*127; gcd(1250,381) == 1) -- no approximation error, unlike
     * speed_display's two rational approximations above. 381 is odd, so rounding to nearest has
     * no exact tie to break: (num + 381/2) / 381 rounds every input to the correct nearest foot. */
    uint32_t num = (uint32_t)dist_m * 1250u;
    uint32_t v   = (num + 381u / 2u) / 381u;
    /* Trips only once dist_m exceeds ~19975 m (65535 * 381 / 1250, the point feet overflows a
     * uint16_t) -- far beyond any real DRAG_BRAKE distance (a few hundred metres at most,
     * core/dragengine/drag.c). Kept as a guard against a future formula/input change silently
     * truncating on return, same as speed_display's own guard above. */
    CORE_ASSERT_RET(v <= 65535u, UNITS_ASSERT_CODE, 65535u);
    return (uint16_t)v;
}
