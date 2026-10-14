#ifndef SIM_DRAG_POS_H
#define SIM_DRAG_POS_H
#include <stdint.h>
/* Pure position step for SIM_SCENARIO_DRAG (drv:H2). The drag profile (sim_profile.h) reports the
 * CUMULATIVE distance from the launch point, so a delivered fix must be origin + f(dist_m) with
 * the origin latched once for the whole run (gps_sim.c's s_drag_origin, latched in
 * gps_sim_rearm()). Integrating against the previous frame's position instead makes every
 * per-frame step equal the whole cumulative distance -- beyond ~104 km/h that exceeds the §6.5
 * jump filter (FIX_MAX_JUMP_MPS*dt + 20 = 70 m at 5 Hz) and every fix of the run is dropped with
 * EV_FIX_LOST. No state, no allocation; host-tested by test/test_sim_profile.c. */
#define SIM_DEG_LAT_M 111320.0   /* metres per degree of latitude (due-north runs only) */
/* Latitude in 1e-7 deg for a point `dist_m` (>= 0) due north of `origin_lat_e7`. */
int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m);
#endif /* SIM_DRAG_POS_H */
