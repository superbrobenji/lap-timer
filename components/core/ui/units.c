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
