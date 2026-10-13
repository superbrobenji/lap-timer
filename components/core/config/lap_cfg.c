#include "core/lap_cfg.h"
#include "core/core.h"

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define LAPCFG_ASSERT_CODE 0x0A21

_Static_assert(LAP_MAX_DEFAULT_LAYOUTS == CFG_MAX_DEFAULT_LAYOUTS,
               "lap_cfg_t.default_layout[] must mirror cfg_t.lap.default_layout[]");

static uint16_t pick16(uint16_t v, uint16_t lo, uint16_t hi, uint16_t dflt)
{ return (v < lo || v > hi) ? dflt : v; }
static uint8_t pick8(uint8_t v, uint8_t lo, uint8_t hi, uint8_t dflt)
{ return (v < lo || v > hi) ? dflt : v; }

void lap_cfg_from_user(const cfg_t *cfg, lap_cfg_t *out)
{
    CORE_ASSERT_VOID(out != NULL, LAPCFG_ASSERT_CODE);
    lap_cfg_defaults(out);
    if (cfg == NULL) return;
    /* Ranges are cfg_validate()'s (core/config/cfg.c) and spec §15.1's table, field for field. */
    out->min_lap_s     = pick16(cfg->lap.min_lap_s,    5u,  600u,  (uint16_t)MIN_LAP_S);
    out->max_lap_s     = pick16(cfg->lap.max_lap_s,    60u, 3600u, (uint16_t)MAX_LAP_S);
    out->gate_rearm_m  = pick16(cfg->lap.gate_rearm_m, 10u, 500u,  (uint16_t)GATE_REARM_DIST_M);
    out->pit_speed_kmh = pick8(cfg->lap.pit_speed_kmh, 1u,  30u,   (uint8_t)PIT_SPEED_KMH);
    out->pit_time_s    = pick8(cfg->lap.pit_time_s,    3u,  60u,   (uint8_t)PIT_TIME_S);
    uint8_t n = cfg->lap.n_default_layout;
    if (n > LAP_MAX_DEFAULT_LAYOUTS) n = LAP_MAX_DEFAULT_LAYOUTS;   /* rule 2: fixes the loop bound */
    for (uint8_t i = 0; i < n; i++) {
        out->default_layout[i].venue  = cfg->lap.default_layout[i].venue;
        out->default_layout[i].layout = cfg->lap.default_layout[i].layout;
    }
    out->n_default_layout = n;
    CORE_ASSERT_VOID(out->min_lap_s < out->max_lap_s, LAPCFG_ASSERT_CODE);   /* a lap window exists */
}
