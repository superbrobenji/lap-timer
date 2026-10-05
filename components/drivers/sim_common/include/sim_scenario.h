#ifndef SIM_SCENARIO_H
#define SIM_SCENARIO_H
#include <stdint.h>
/* Fix round 1, M2: `scenario` below takes CMD_SIM_SCENARIO's arg8 values -- the canonical enum is
 * `app/lt_ipc.h`'s `SIM_SC_LAPS/DRAG/PARK` (next to MODE_LAP/MODE_DRAG, the same arg8-lives-with-
 * the-command precedent). This is a SEPARATELY NAMED mirror, not a #include of app/lt_ipc.h:
 * gps_sim/imu_sim are drivers that `app` itself depends on (app REQUIRES gps_$ENV{LT_GPS}), so
 * pulling in lt_ipc.h here -- FreeRTOS headers and all -- would invert/cycle that dependency.
 * pipeline.c (the one file that already includes both headers, under #if CFG_GPS_SIM)
 * _Static_asserts the two numeric sets stay identical, so a future renumber here or there is
 * caught at compile time rather than silently diverging. */
enum { SIM_SCENARIO_LAPS = 0, SIM_SCENARIO_DRAG = 1, SIM_SCENARIO_PARK = 2 };
void     sim_scenario_set(uint8_t scenario, uint16_t laps, int64_t now_us);   /* re-anchors; laps >= 1 for LAPS */
uint8_t  sim_scenario_get(void);
int64_t  sim_scenario_anchor_us(void);
uint16_t sim_scenario_laps(void);
/* Provided by the gps_sim driver (link-time), not sim_common itself: resets its own replay/drag
 * bookkeeping (s_idx/s_started/s_parked/s_repeat/s_wrap_tick) so a scenario switch re-anchors on
 * the driver's very next gps_poll() -- deliberately leaves the driver's "last delivered position"
 * state alone (fix round 1, I2: a park must freeze where the sim currently is, not reset it).
 * Declared here, not in gps_sim's own sim_capture.h (generated, capture-data only): every
 * sim_scenario_set() caller must call this right after (pipeline.c's handle_sim_scenario(),
 * Task 3), and gps_sim.c already includes this header for SIM_SCENARIO_*. */
void gps_sim_rearm(void);
#endif
