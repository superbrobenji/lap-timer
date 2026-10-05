/* sim_clock.c -- pure gps_us bookkeeping for gps_sim.c's scenarios (see sim_clock.h; final review
 * C1, ruling F-1). No esp_timer, no statics (§17.9): every function reads/writes only the caller's
 * own sim_clock_t, passed by pointer -- gps_sim.c owns the one instance. "host/" matches
 * sim_profile.c's placement (the name is historical: this file builds unchanged on the ESP32 too,
 * idf_component_register SRCS "host/sim_clock.c", components/drivers/sim_common/CMakeLists.txt).
 */
#include "sim_clock.h"
#include "core/core.h"

#include <stddef.h>

#define SIM_CLOCK_ASSERT_CODE 0x0C72

void sim_clock_init(sim_clock_t *sc, int64_t first_gps_us)
{
    CORE_ASSERT_VOID(sc != NULL, SIM_CLOCK_ASSERT_CODE);
    sc->base_us = first_gps_us;
}

void sim_clock_rearm(sim_clock_t *sc, int64_t last_delivered_gps_us, int64_t step_us)
{
    CORE_ASSERT_VOID(sc != NULL, SIM_CLOCK_ASSERT_CODE);
    CORE_ASSERT_VOID(step_us > 0, SIM_CLOCK_ASSERT_CODE);   /* every call site passes a real period */
    sc->base_us = last_delivered_gps_us + step_us;
}

int64_t sim_clock_laps_us(const sim_clock_t *sc, int64_t cap_elapsed_us, uint16_t repeat,
                           int64_t span_us, int64_t wrap_pause_us, int64_t gap_us)
{
    CORE_ASSERT_RET(sc != NULL, SIM_CLOCK_ASSERT_CODE, 0);
    CORE_ASSERT_RET(cap_elapsed_us >= 0, SIM_CLOCK_ASSERT_CODE, 0);   /* elapsed since the capture's own fix 0 */
    return sc->base_us + cap_elapsed_us + (int64_t)repeat * (span_us + wrap_pause_us + gap_us);
}

int64_t sim_clock_elapsed_us(const sim_clock_t *sc, int64_t gps_us)
{
    CORE_ASSERT_RET(sc != NULL, SIM_CLOCK_ASSERT_CODE, 0);
    return gps_us - sc->base_us;
}

int64_t sim_clock_wrap_us(const sim_clock_t *sc, int64_t elapsed0_us, uint32_t tick, int64_t period_us)
{
    CORE_ASSERT_RET(sc != NULL, SIM_CLOCK_ASSERT_CODE, 0);
    CORE_ASSERT_RET(tick > 0, SIM_CLOCK_ASSERT_CODE, 0);   /* 1-based: see sim_clock.h */
    return sc->base_us + elapsed0_us + (int64_t)tick * period_us;
}

int64_t sim_clock_drag_us(const sim_clock_t *sc, int64_t t_us)
{
    CORE_ASSERT_RET(sc != NULL, SIM_CLOCK_ASSERT_CODE, 0);
    CORE_ASSERT_RET(t_us >= 0, SIM_CLOCK_ASSERT_CODE, 0);
    return sc->base_us + t_us;
}

int64_t sim_clock_park_us(const sim_clock_t *sc, uint32_t tick, int64_t period_us)
{
    CORE_ASSERT_RET(sc != NULL, SIM_CLOCK_ASSERT_CODE, 0);
    return sc->base_us + (int64_t)tick * period_us;
}
