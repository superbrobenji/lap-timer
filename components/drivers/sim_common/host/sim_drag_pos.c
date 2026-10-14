/* sim_drag_pos.c -- see include/sim_drag_pos.h. Pure, IDF-free (built into the sim_common
 * component for the target and linked directly by test/test_sim_profile.c on the host). */
#include "sim_drag_pos.h"
#include "core/core.h"

#define SIM_DRAG_POS_ASSERT_CODE 0x0C73   /* Power of 10 rule 5 (core/core.h); unused by any other
                                           * module -- confirmed by grepping components/ for
                                           * "0x0C73" before picking it (next free slot after
                                           * sim_profile.c's 0x0C70, sim_scenario.c's 0x0C71 and
                                           * sim_clock.c's 0x0C72, its siblings in this component) */

int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m)
{
    /* sim_profile.h's own contract on sim_drag_state_t: dist_m is always >= 0. Lap-timer code
     * never aborts on a bad precondition (full Power-of-10, CORE_ASSERT_*) -- this file is
     * reachable from shipped firmware (gps_sim.c), unlike dev-kit-only host-tested files, so a
     * bad input must return the unmoved origin, not panic. */
    CORE_ASSERT_RET(dist_m >= 0.0, SIM_DRAG_POS_ASSERT_CODE, origin_lat_e7);
    return origin_lat_e7 + (int32_t)(dist_m * 1e7 / SIM_DEG_LAT_M);
}
