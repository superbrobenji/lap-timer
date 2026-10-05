/* sim_scenario.c -- bench scenario switch for the GPS/IMU sims (spec §4.6, bench checklist item 1).
 *
 * One shared "what is the sim build doing right now" switch: the current scenario, the device
 * mono time it was last (re)anchored at, and (for SIM_SC_LAPS) how many capture replays to run
 * before parking. gps_sim.c and imu_sim.c both poll this every sample; CMD_SIM_SCENARIO
 * (pipeline.c, sim build only) is the only writer, via `dbg sim drag | laps <n> | park`. Three
 * statics, no allocation (§17.9).
 */
#include "sim_scenario.h"
#include "core/core.h"

#define SIM_SCENARIO_ASSERT_CODE 0x0C71

static uint8_t  s_scenario = SIM_SC_LAPS;
static int64_t  s_anchor_us;
static uint16_t s_laps = 1;

void sim_scenario_set(uint8_t scenario, uint16_t laps, int64_t now_us)
{
    CORE_ASSERT_VOID(scenario == SIM_SC_LAPS || scenario == SIM_SC_DRAG || scenario == SIM_SC_PARK,
                      SIM_SCENARIO_ASSERT_CODE);   /* reject an unknown scenario: assert + ignore */
    s_scenario  = scenario;
    s_laps      = (laps < 1) ? 1 : laps;
    s_anchor_us = now_us;
}

uint8_t  sim_scenario_get(void)       { return s_scenario; }
int64_t  sim_scenario_anchor_us(void) { return s_anchor_us; }
uint16_t sim_scenario_laps(void)      { return s_laps; }
