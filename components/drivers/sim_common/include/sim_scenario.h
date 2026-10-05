#ifndef SIM_SCENARIO_H
#define SIM_SCENARIO_H
#include <stdint.h>
enum { SIM_SC_LAPS = 0, SIM_SC_DRAG = 1, SIM_SC_PARK = 2 };   /* CMD_SIM_SCENARIO arg8 */
void     sim_scenario_set(uint8_t scenario, uint16_t laps, int64_t now_us);   /* re-anchors; laps >= 1 for LAPS */
uint8_t  sim_scenario_get(void);
int64_t  sim_scenario_anchor_us(void);
uint16_t sim_scenario_laps(void);
/* Provided by the gps_sim driver (link-time), not sim_common itself: resets its own replay/drag
 * bookkeeping (s_idx/s_started/s_parked/s_repeat) so a scenario switch re-anchors on the driver's
 * very next gps_poll(). Declared here, not in gps_sim's own sim_capture.h (generated, capture-data
 * only): every sim_scenario_set() caller must call this right after (pipeline.c's
 * handle_sim_scenario(), Task 3), and gps_sim.c already includes this header for SIM_SC_*. */
void gps_sim_rearm(void);
#endif
