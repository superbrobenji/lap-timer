#include "core/cfg.h"
#include "core/core.h"
#include <string.h>

#define CFG_ASSERT_CODE 0x0A70

int cfg_defaults(cfg_t *c)
{
    CORE_ASSERT_RET(c != NULL, CFG_ASSERT_CODE, -1);
    memset(c, 0, sizeof *c);
    c->version = CFG_VERSION; c->units = CFG_UNITS_KMH; c->mode = CFG_MODE_LAP;
    c->lap.min_lap_s = 20; c->lap.max_lap_s = 1800; c->lap.gate_rearm_m = 50; c->lap.pit_speed_kmh = 5; c->lap.pit_time_s = 10;
    c->drag.benches_kmh[0] = 100; c->drag.benches_kmh[1] = 200; c->drag.benches_kmh[2] = 300; c->drag.n_kmh = 3;
    c->drag.benches_mph[0] = 60; c->drag.benches_mph[1] = 120; c->drag.benches_mph[2] = 180; c->drag.n_mph = 3;
    c->drag.rollout = false; c->drag.launch_g_e2 = 15;
    c->power.pit_after_s = 30; c->power.park_after_s = 600; c->power.shutdown_mv = 3300; c->power.conn_idle_s = 300;
    c->display.live_clock = false; c->display.full_every = 10; c->display.rotation = 0; c->display.invert = false;
    c->battery.adc_mv[0] = 3000; c->battery.true_mv[0] = 3000; c->battery.adc_mv[1] = 4200; c->battery.true_mv[1] = 4200;
    strcpy(c->ble.name, "LapTimer"); c->ble.adv_s = 60;
    c->log.fused_hz = 10;
    c->gps.dyn_model = 4; c->gps.rate_hz = 0;        /* 0 = profile maximum */
    c->imu.mot_thr = 20; c->imu.mot_dur_ms = 40;
    return 0;
}

#define CLAMP_U(field, lo, hi) do { if ((field) < (lo)) { (field) = (lo); n++; } else if ((field) > (hi)) { (field) = (hi); n++; } } while (0)

int cfg_validate(cfg_t *c)
{
    CORE_ASSERT_RET(c != NULL, CFG_ASSERT_CODE, -1);
    int n = 0;
    if (c->version != CFG_VERSION) { c->version = CFG_VERSION; n++; }
    if (c->units > CFG_UNITS_MPH) { c->units = CFG_UNITS_KMH; n++; }
    if (c->mode > CFG_MODE_DRAG) { c->mode = CFG_MODE_LAP; n++; }
    CLAMP_U(c->lap.min_lap_s, 5, 600);
    CLAMP_U(c->lap.max_lap_s, 60, 3600);
    CLAMP_U(c->lap.gate_rearm_m, 10, 500);
    CLAMP_U(c->lap.pit_speed_kmh, 1, 30);
    CLAMP_U(c->lap.pit_time_s, 3, 60);
    if (c->lap.n_default_layout > CFG_MAX_DEFAULT_LAYOUTS) { c->lap.n_default_layout = CFG_MAX_DEFAULT_LAYOUTS; n++; }
    if (c->drag.n_kmh > CFG_MAX_BENCHES) { c->drag.n_kmh = CFG_MAX_BENCHES; n++; }
    if (c->drag.n_mph > CFG_MAX_BENCHES) { c->drag.n_mph = CFG_MAX_BENCHES; n++; }
    /* Buffer capacity before a write: the two clamps just above must have already brought these
     * counts within the array capacity before the loops below index benches_kmh[]/benches_mph[]. */
    CORE_ASSERT_RET(c->drag.n_kmh <= CFG_MAX_BENCHES, CFG_ASSERT_CODE, n);
    CORE_ASSERT_RET(c->drag.n_mph <= CFG_MAX_BENCHES, CFG_ASSERT_CODE, n);
    for (int i = 0; i < c->drag.n_kmh; i++) CLAMP_U(c->drag.benches_kmh[i], 10, 400);
    for (int i = 0; i < c->drag.n_mph; i++) CLAMP_U(c->drag.benches_mph[i], 10, 250);
    CLAMP_U(c->drag.launch_g_e2, 5, 50);
    CLAMP_U(c->power.pit_after_s, 10, 600);
    CLAMP_U(c->power.park_after_s, 60, 7200);
    CLAMP_U(c->power.shutdown_mv, 3000, 3600);
    CLAMP_U(c->power.conn_idle_s, 30, 1800);
    CLAMP_U(c->display.full_every, 1, 50);
    if (c->display.rotation != 0 && c->display.rotation != 180) { c->display.rotation = 0; n++; }
    /* Two-point battery calibration: the two points must be ordered and far enough apart for the
     * interpolation to be meaningful, and both in a plausible cell range. A pair that fails any of
     * that is not clamped field by field (which could invent a worse curve) but reset wholesale. */
    if (c->battery.adc_mv[1] < c->battery.adc_mv[0] + 100 || c->battery.true_mv[1] < c->battery.true_mv[0] + 100 ||
        c->battery.adc_mv[0] < 1000 || c->battery.adc_mv[0] > 5000 || c->battery.adc_mv[1] < 1000 || c->battery.adc_mv[1] > 5000 ||
        c->battery.true_mv[0] < 1000 || c->battery.true_mv[0] > 5000 || c->battery.true_mv[1] < 1000 || c->battery.true_mv[1] > 5000) {
        c->battery.adc_mv[0] = 3000; c->battery.true_mv[0] = 3000;
        c->battery.adc_mv[1] = 4200; c->battery.true_mv[1] = 4200;
        n++;
    }
    if (c->ble.name[0] == '\0') { strcpy(c->ble.name, "LapTimer"); n++; }
    if (c->ble.name[15] != '\0') { c->ble.name[15] = '\0'; n++; }
    CLAMP_U(c->ble.adv_s, 15, 600);
    if (c->log.fused_hz != 5 && c->log.fused_hz != 10 && c->log.fused_hz != 25) { c->log.fused_hz = 10; n++; }
    /* uint8_t fields: only the bounds a uint8_t can actually violate are checked. */
    if (c->gps.dyn_model > 8) { c->gps.dyn_model = 8; n++; }
    if (c->gps.rate_hz > 25) { c->gps.rate_hz = 25; n++; }
    if (c->imu.mot_thr < 2) { c->imu.mot_thr = 2; n++; }
    if (c->imu.mot_dur_ms < 1) { c->imu.mot_dur_ms = 1; n++; }
    return n;
}

int cfg_migrate(cfg_t *c, uint8_t from_version)
{
    CORE_ASSERT_RET(c != NULL, CFG_ASSERT_CODE, -1);
    if (from_version == 1) { c->version = CFG_VERSION; return 0; }
    return -1;
}

int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p)
{
    CORE_ASSERT_RET(c != NULL, CFG_ASSERT_CODE, -1);
    CORE_ASSERT_RET(p != NULL, CFG_ASSERT_CODE, -1);         /* p->ble_name itself is optionally NULL: checked below, not here */
    /* Validate before touching anything: a rejected profile must leave the config untouched. */
    if (p->ble_name && strlen(p->ble_name) > 15) return -1;
    c->display.live_clock = p->display_live_clock;
    c->log.fused_hz = p->log_fused_hz;
    if (p->ble_name) strcpy(c->ble.name, p->ble_name);
    return 0;
}
