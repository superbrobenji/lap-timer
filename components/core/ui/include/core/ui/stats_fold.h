#ifndef CORE_UI_STATS_FOLD_H
#define CORE_UI_STATS_FOLD_H
#include <stdint.h>

#include "core/types.h"

/* Session-max accumulator (Plan 7c T1): the element-wise running max across every lap_stats_t
 * seen so far in a session, used by the session summary screen. */
typedef struct {
    uint16_t max_speed_cms;
    int16_t  max_lean_l_cdeg, max_lean_r_cdeg;
    int16_t  max_glat_e3, max_gacc_e3, max_gbrake_e3;
} session_max_t;

/* Folds `lap`'s per-field maxima into `*acc` in place (element-wise max; fields that are already
 * larger in `*acc` are left unchanged). NULL `acc`/`lap` is a no-op (CORE_ASSERT_VOID). */
void session_max_fold(session_max_t *acc, const lap_stats_t *lap);

#endif
