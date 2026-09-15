#include "core/cfg.h"
#include "core/jw.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

#define MAX_TOKS 192

/* err is optional everywhere: a NULL or zero-capacity buffer just discards the message. */
static int set_err(char *err, size_t cap, const char *msg) { if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = '\0'; } return -1; }

static bool get_u16(const char *js, const jsmntok_t *t, uint16_t *out) { int64_t v; if (!json_tok_int(js, t, &v) || v < 0 || v > 65535) return false; *out = (uint16_t)v; return true; }
static bool get_u8(const char *js, const jsmntok_t *t, uint8_t *out) { int64_t v; if (!json_tok_int(js, t, &v) || v < 0 || v > 255) return false; *out = (uint8_t)v; return true; }

/* returns 0 ok, -1 type error */
static int apply(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, const char *path, int v)
{
    const jsmntok_t *t = &toks[v];
    if (!strcmp(path, "units")) {
        if (json_tok_eq(js, t, "kmh")) { c->units = CFG_UNITS_KMH; return 0; }
        if (json_tok_eq(js, t, "mph")) { c->units = CFG_UNITS_MPH; return 0; }
        return -1;
    }
    if (!strcmp(path, "mode")) {
        if (json_tok_eq(js, t, "lap")) { c->mode = CFG_MODE_LAP; return 0; }
        if (json_tok_eq(js, t, "drag")) { c->mode = CFG_MODE_DRAG; return 0; }
        return -1;
    }
    if (!strcmp(path, "lap.min_lap_s")) return get_u16(js, t, &c->lap.min_lap_s) ? 0 : -1;
    if (!strcmp(path, "lap.max_lap_s")) return get_u16(js, t, &c->lap.max_lap_s) ? 0 : -1;
    if (!strcmp(path, "lap.gate_rearm_m")) return get_u16(js, t, &c->lap.gate_rearm_m) ? 0 : -1;
    if (!strcmp(path, "lap.pit_speed_kmh")) return get_u8(js, t, &c->lap.pit_speed_kmh) ? 0 : -1;
    if (!strcmp(path, "lap.pit_time_s")) return get_u8(js, t, &c->lap.pit_time_s) ? 0 : -1;
    if (!strcmp(path, "lap.default_layout")) {
        if (t->type != JSMN_ARRAY) return -1;
        if (t->size > CFG_MAX_DEFAULT_LAYOUTS) return -1;
        int i = v + 1; uint8_t n = 0;
        for (int k = 0; k < t->size; k++) {
            int vv = json_obj_get(js, toks, ntoks, i, "venue"), ll = json_obj_get(js, toks, ntoks, i, "layout");
            if (vv < 0 || ll < 0 || !get_u16(js, &toks[vv], &c->lap.default_layout[n].venue) || !get_u16(js, &toks[ll], &c->lap.default_layout[n].layout)) return -1;
            n++; i = json_skip(toks, ntoks, i);
        }
        c->lap.n_default_layout = n; return 0;
    }
    if (!strcmp(path, "drag.benches_kmh") || !strcmp(path, "drag.benches_mph")) {
        if (t->type != JSMN_ARRAY) return -1;
        if (t->size > CFG_MAX_BENCHES) return -1;
        bool kmh = path[13] == 'k';
        uint16_t *dst = kmh ? c->drag.benches_kmh : c->drag.benches_mph; uint8_t n = 0;
        for (int k = 0; k < t->size; k++) { if (!get_u16(js, &toks[v + 1 + k], &dst[n])) return -1; n++; }
        if (kmh) c->drag.n_kmh = n; else c->drag.n_mph = n;
        return 0;
    }
    if (!strcmp(path, "drag.rollout")) return json_tok_bool(js, t, &c->drag.rollout) ? 0 : -1;
    if (!strcmp(path, "drag.launch_g")) return get_u8(js, t, &c->drag.launch_g_e2) ? 0 : -1;
    if (!strcmp(path, "power.pit_after_s")) return get_u16(js, t, &c->power.pit_after_s) ? 0 : -1;
    if (!strcmp(path, "power.park_after_s")) return get_u16(js, t, &c->power.park_after_s) ? 0 : -1;
    if (!strcmp(path, "power.shutdown_mv")) return get_u16(js, t, &c->power.shutdown_mv) ? 0 : -1;
    if (!strcmp(path, "power.conn_idle_s")) return get_u16(js, t, &c->power.conn_idle_s) ? 0 : -1;
    if (!strcmp(path, "display.live_clock")) return json_tok_bool(js, t, &c->display.live_clock) ? 0 : -1;
    if (!strcmp(path, "display.full_refresh_every")) return get_u8(js, t, &c->display.full_every) ? 0 : -1;
    if (!strcmp(path, "display.rotation")) return get_u8(js, t, &c->display.rotation) ? 0 : -1;
    if (!strcmp(path, "display.invert")) return json_tok_bool(js, t, &c->display.invert) ? 0 : -1;
    if (!strcmp(path, "battery.cal")) {
        if (t->type != JSMN_ARRAY || t->size != 2) return -1;
        int i = v + 1;
        for (int k = 0; k < 2; k++) {
            int a = json_obj_get(js, toks, ntoks, i, "adc_mv"), b = json_obj_get(js, toks, ntoks, i, "true_mv");
            if (a < 0 || b < 0 || !get_u16(js, &toks[a], &c->battery.adc_mv[k]) || !get_u16(js, &toks[b], &c->battery.true_mv[k])) return -1;
            i = json_skip(toks, ntoks, i);
        }
        return 0;
    }
    if (!strcmp(path, "ble.name")) { if (t->type != JSMN_STRING || t->end - t->start > 15) return -1; json_tok_str(js, t, c->ble.name, sizeof c->ble.name); return 0; }
    if (!strcmp(path, "ble.adv_s")) return get_u16(js, t, &c->ble.adv_s) ? 0 : -1;
    if (!strcmp(path, "log.fused_hz")) return get_u8(js, t, &c->log.fused_hz) ? 0 : -1;
    if (!strcmp(path, "gps.dyn_model")) return get_u8(js, t, &c->gps.dyn_model) ? 0 : -1;
    if (!strcmp(path, "gps.rate_hz")) return get_u8(js, t, &c->gps.rate_hz) ? 0 : -1;
    if (!strcmp(path, "imu.mot_thr")) return get_u8(js, t, &c->imu.mot_thr) ? 0 : -1;
    if (!strcmp(path, "imu.mot_dur_ms")) return get_u8(js, t, &c->imu.mot_dur_ms) ? 0 : -1;
    return 0;   /* unknown key: ignored */
}

static const char *const SECTIONS[] = { "lap", "drag", "power", "display", "battery", "ble", "log", "gps", "imu" };

static int walk(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *prefix, char *err, size_t err_cap)
{
    int i = obj + 1;
    for (int k = 0; k < toks[obj].size && i + 1 < ntoks; k++) {
        char key[32], path[64];
        json_tok_str(js, &toks[i], key, sizeof key);
        int v = i + 1;
        if (prefix[0]) snprintf(path, sizeof path, "%s.%s", prefix, key); else snprintf(path, sizeof path, "%s", key);
        bool is_section = false;
        if (!prefix[0]) for (size_t s = 0; s < sizeof SECTIONS / sizeof SECTIONS[0]; s++) if (!strcmp(key, SECTIONS[s])) is_section = true;
        if (is_section) {
            if (toks[v].type != JSMN_OBJECT) {
                char msg[64]; snprintf(msg, sizeof msg, "%s must be an object", key);
                return set_err(err, err_cap, msg);
            }
            if (walk(c, js, toks, ntoks, v, key, err, err_cap) < 0) return -1;
        } else if (apply(c, js, toks, ntoks, path, v) < 0) {
            char msg[96]; snprintf(msg, sizeof msg, "bad value for %s", path);
            return set_err(err, err_cap, msg);
        }
        i = json_skip(toks, ntoks, i + 1);        /* i is the key; step past its value subtree */
    }
    return 0;
}

/* Not reentrant: uses a static token array (called from the single conn task). */
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap)
{
    static jsmntok_t toks[MAX_TOKS];
    if (err && err_cap) err[0] = '\0';
    int cnt = json_parse(json, n, toks, MAX_TOKS);
    if (cnt < 1 || toks[0].type != JSMN_OBJECT) return set_err(err, err_cap, "malformed json");
    cfg_t tmp = *c;
    if (walk(&tmp, json, toks, cnt, 0, "", err, err_cap) < 0) return -1;
    *c = tmp;
    return 0;
}

int cfg_to_json(const cfg_t *c, char *out, size_t cap)
{
    jw_t w; jw_init(&w, out, cap);
    jw_obj_open(&w);
    jw_key(&w, "version"); jw_uint(&w, c->version);
    jw_key(&w, "units"); jw_str(&w, c->units == CFG_UNITS_MPH ? "mph" : "kmh");
    jw_key(&w, "mode"); jw_str(&w, c->mode == CFG_MODE_DRAG ? "drag" : "lap");
    jw_key(&w, "lap"); jw_obj_open(&w);
      jw_key(&w, "min_lap_s"); jw_uint(&w, c->lap.min_lap_s);
      jw_key(&w, "max_lap_s"); jw_uint(&w, c->lap.max_lap_s);
      jw_key(&w, "gate_rearm_m"); jw_uint(&w, c->lap.gate_rearm_m);
      jw_key(&w, "pit_speed_kmh"); jw_uint(&w, c->lap.pit_speed_kmh);
      jw_key(&w, "pit_time_s"); jw_uint(&w, c->lap.pit_time_s);
      jw_key(&w, "default_layout"); jw_arr_open(&w);
      for (int i = 0; i < c->lap.n_default_layout; i++) { jw_obj_open(&w); jw_key(&w, "venue"); jw_uint(&w, c->lap.default_layout[i].venue); jw_key(&w, "layout"); jw_uint(&w, c->lap.default_layout[i].layout); jw_obj_close(&w); }
      jw_arr_close(&w);
    jw_obj_close(&w);
    jw_key(&w, "drag"); jw_obj_open(&w);
      jw_key(&w, "benches_kmh"); jw_arr_open(&w); for (int i = 0; i < c->drag.n_kmh; i++) jw_uint(&w, c->drag.benches_kmh[i]); jw_arr_close(&w);
      jw_key(&w, "benches_mph"); jw_arr_open(&w); for (int i = 0; i < c->drag.n_mph; i++) jw_uint(&w, c->drag.benches_mph[i]); jw_arr_close(&w);
      jw_key(&w, "rollout"); jw_bool(&w, c->drag.rollout);
      jw_key(&w, "launch_g"); jw_uint(&w, c->drag.launch_g_e2);
    jw_obj_close(&w);
    jw_key(&w, "power"); jw_obj_open(&w);
      jw_key(&w, "pit_after_s"); jw_uint(&w, c->power.pit_after_s);
      jw_key(&w, "park_after_s"); jw_uint(&w, c->power.park_after_s);
      jw_key(&w, "shutdown_mv"); jw_uint(&w, c->power.shutdown_mv);
      jw_key(&w, "conn_idle_s"); jw_uint(&w, c->power.conn_idle_s);
    jw_obj_close(&w);
    jw_key(&w, "display"); jw_obj_open(&w);
      jw_key(&w, "live_clock"); jw_bool(&w, c->display.live_clock);
      jw_key(&w, "full_refresh_every"); jw_uint(&w, c->display.full_every);
      jw_key(&w, "rotation"); jw_uint(&w, c->display.rotation);
      jw_key(&w, "invert"); jw_bool(&w, c->display.invert);
    jw_obj_close(&w);
    jw_key(&w, "battery"); jw_obj_open(&w);
      jw_key(&w, "cal"); jw_arr_open(&w);
      for (int i = 0; i < 2; i++) { jw_obj_open(&w); jw_key(&w, "adc_mv"); jw_uint(&w, c->battery.adc_mv[i]); jw_key(&w, "true_mv"); jw_uint(&w, c->battery.true_mv[i]); jw_obj_close(&w); }
      jw_arr_close(&w);
    jw_obj_close(&w);
    jw_key(&w, "ble"); jw_obj_open(&w);
      jw_key(&w, "name"); jw_str(&w, c->ble.name);
      jw_key(&w, "adv_s"); jw_uint(&w, c->ble.adv_s);
    jw_obj_close(&w);
    jw_key(&w, "log"); jw_obj_open(&w); jw_key(&w, "fused_hz"); jw_uint(&w, c->log.fused_hz); jw_obj_close(&w);
    jw_key(&w, "gps"); jw_obj_open(&w); jw_key(&w, "dyn_model"); jw_uint(&w, c->gps.dyn_model); jw_key(&w, "rate_hz"); jw_uint(&w, c->gps.rate_hz); jw_obj_close(&w);
    jw_key(&w, "imu"); jw_obj_open(&w); jw_key(&w, "mot_thr"); jw_uint(&w, c->imu.mot_thr); jw_key(&w, "mot_dur_ms"); jw_uint(&w, c->imu.mot_dur_ms); jw_obj_close(&w);
    jw_obj_close(&w);
    return jw_overflow(&w) ? -1 : (int)jw_len(&w);
}
