#include "core/ui/stats_fold.h"
#include "core/core.h"

#include <stddef.h> /* NULL */

#define STATS_ASSERT_CODE 0x0AF0
static int16_t max_i16(int16_t a, int16_t b) { return a > b ? a : b; }
void session_max_fold(session_max_t *acc, const lap_stats_t *lap)
{
    CORE_ASSERT_VOID(acc != NULL, STATS_ASSERT_CODE);
    CORE_ASSERT_VOID(lap != NULL, STATS_ASSERT_CODE);
    if (lap->max_speed_cms > acc->max_speed_cms) acc->max_speed_cms = lap->max_speed_cms;
    acc->max_lean_l_cdeg = max_i16(acc->max_lean_l_cdeg, lap->max_lean_l_cdeg);
    acc->max_lean_r_cdeg = max_i16(acc->max_lean_r_cdeg, lap->max_lean_r_cdeg);
    acc->max_glat_e3     = max_i16(acc->max_glat_e3, lap->max_glat_e3);
    acc->max_gacc_e3     = max_i16(acc->max_gacc_e3, lap->max_gacc_e3);
    acc->max_gbrake_e3   = max_i16(acc->max_gbrake_e3, lap->max_gbrake_e3);
}
