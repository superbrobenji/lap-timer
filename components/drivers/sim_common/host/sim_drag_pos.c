/* sim_drag_pos.c -- see include/sim_drag_pos.h. Pure, IDF-free (built into the sim_common
 * component for the target and linked directly by test/test_sim_profile.c on the host). */
#include "sim_drag_pos.h"
#include <assert.h>

int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m)
{
    assert(dist_m >= 0.0);                 /* sim_profile.h's own contract on sim_drag_state_t */
    return origin_lat_e7 + (int32_t)(dist_m * 1e7 / SIM_DEG_LAT_M);
}
