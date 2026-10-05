#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H
#include <stdint.h>

/* sim_clock.h -- pure gps_us bookkeeping shared by every gps_sim.c scenario (final review C1,
 * ruling F-1: "every dbg sim <scenario> rewinds the delivered GPS time base").
 *
 * gps_sim.c used to derive each delivered fix's gps_us from a FIXED origin (SIM_FIXES[0].gps_us
 * for LAPS/DRAG, a frozen s_last_fix for PARK) plus counters that gps_sim_rearm() zeroed on every
 * scenario switch -- so a `dbg sim <scenario>` command could rewind gps_us behind the pipeline's
 * §6.5 "last valid" watermark (components/app/pipeline/pipeline.c, compute_validity()), which is
 * never reset on a switch, for anywhere from seconds to minutes of invalid fixes.
 *
 * The fix: ONE monotonic clock across every switch. gps_sim_rearm() re-anchors it to "the last fix
 * actually delivered, plus one drag-period" (sim_clock_rearm(), below); every scenario's own
 * gps_us then becomes that anchor plus how far this fix is into the scenario's own timeline
 * (capture-replay elapsed + repeat/wrap-pause offset for LAPS, profile time for DRAG, tick count
 * for PARK) -- never the capture's or profile's own fixed zero again.
 *
 * Pure and stateless: every function takes the caller's own sim_clock_t by pointer and no function
 * here keeps any state of its own (no statics, §17.9) -- gps_sim.c owns the one instance (in its
 * usual static storage) and this file adds none of its own. Shared between the ESP32 build
 * (gps_sim.c, idf_component_register SRCS "host/sim_clock.c" -- the "host/" name is historical,
 * matching sim_profile.c's; both compile unchanged on-device) and the host test, test_sim_clock.c.
 */
typedef struct {
    int64_t base_us;   /* gps_us the ACTIVE scenario's own elapsed-time-zero maps to */
} sim_clock_t;

/* Cold-boot seed (gps_init()): the very first fix this driver ever delivers keeps its capture
 * timestamp unchanged, matching gps_sim.c's long-documented replay behaviour. */
void sim_clock_init(sim_clock_t *sc, int64_t first_gps_us);

/* Re-anchor on ANY scenario switch (gps_sim_rearm(), called for every `dbg sim drag|laps|park`):
 * the next fix delivered, in whatever scenario comes next, carries gps_us =
 * last_delivered_gps_us + step_us -- never the new scenario's own fixed origin. `step_us` is the
 * driver's own base delivery period (SIM_DRAG_PERIOD_US, 200 ms); using it (rather than 0) keeps
 * the new fix strictly greater than the last one even when the new scenario's own first offset is
 * itself 0 (DRAG's t=0, PARK's tick=0). */
void sim_clock_rearm(sim_clock_t *sc, int64_t last_delivered_gps_us, int64_t step_us);

/* SIM_SCENARIO_LAPS: gps_us for a capture fix `cap_elapsed_us` into the capture (its own
 * SIM_FIXES[idx].gps_us minus SIM_FIXES[0].gps_us -- capture data is private to gps_sim.c, so the
 * subtraction happens there) on 0-based `repeat`, given the capture's own real-time `span_us`
 * (last fix - first fix), the post-repeat standstill `wrap_pause_us` and the inter-repeat gap
 * `gap_us` -- the same "off" arithmetic deliver_laps() always used, now anchored on this clock's
 * own base instead of the capture's fixed origin. */
int64_t sim_clock_laps_us(const sim_clock_t *sc, int64_t cap_elapsed_us, uint16_t repeat,
                           int64_t span_us, int64_t wrap_pause_us, int64_t gap_us);

/* The inverse of sim_clock_laps_us/sim_clock_drag_us/sim_clock_park_us: how far an already-computed
 * `gps_us` sits past this clock's base -- deliver_wrap_pause() needs "how far is the repeat that
 * just ended from base_us" to anchor its own pause arithmetic. */
int64_t sim_clock_elapsed_us(const sim_clock_t *sc, int64_t gps_us);

/* SIM_SCENARIO_LAPS wrap pause (fix round 1, I1; M3 pre-increment fix folded in here): gps_us for
 * the `tick`-th parked fix (1-based: the caller increments its own tick counter before calling, so
 * the very first wrap-pause fix is strictly greater than the repeat-ending fix it continues from,
 * never a duplicate of it) delivered between repeats, continuing from `elapsed0_us`
 * (sim_clock_elapsed_us() of the repeat-ending fix) at `period_us` cadence. */
int64_t sim_clock_wrap_us(const sim_clock_t *sc, int64_t elapsed0_us, uint32_t tick, int64_t period_us);

/* SIM_SCENARIO_DRAG: gps_us at drag-profile time `t_us` since the scenario's own anchor. */
int64_t sim_clock_drag_us(const sim_clock_t *sc, int64_t t_us);

/* SIM_SCENARIO_PARK (and the LAPS tail once every repeat is exhausted): gps_us for the `tick`-th
 * parked fix (0-based: tick 0 is this clock's own base_us exactly, so the FIRST parked fix after a
 * rearm lands precisely on last_delivered_gps_us + step_us, same as every other scenario) at
 * `period_us` cadence. */
int64_t sim_clock_park_us(const sim_clock_t *sc, uint32_t tick, int64_t period_us);

#endif
