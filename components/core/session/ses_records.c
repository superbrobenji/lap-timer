#include "core/ses.h"
#include "core/bw.h"
#include "core/core.h"
#include <string.h>
#include <math.h>

#define KEYFRAME_US ((int64_t)FIX_KEYFRAME_S * 1000000LL)
#define SES_ASSERT_CODE 0x0A50   /* shared with ses_frame.c; new assertions in this module use it too */

static int finish(uint8_t type, bw_t *w, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(w != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    if (bw_overflow(w)) return -1;
    return ses_frame_encode(type, w->p, (uint8_t)bw_len(w), out, cap);
}

static int64_t round_div(int64_t a, int64_t b)          /* round-to-nearest for positive b */
{
    CORE_ASSERT_RET(b != 0, SES_ASSERT_CODE, 0);
    return (a >= 0) ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static int16_t clamp_i16(int64_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static uint16_t clamp_u16(int64_t v)
{
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return (uint16_t)v;
}

static uint8_t clamp_u8(int64_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* ---------------- fix ---------------- */

void ses_fix_state_init(ses_fix_state_t *st) { CORE_ASSERT_VOID(st != NULL, SES_ASSERT_CODE); memset(st, 0, sizeof *st); }

static uint8_t fix_flags(const gps_fix_t *f)
{
    CORE_ASSERT_RET(f != NULL, SES_ASSERT_CODE, 0);
    uint8_t fl = 0;
    if (f->valid) fl |= 0x01;
    if (f->flags & GPS_FLAG_FIXOK) fl |= 0x02;
    if (f->fix_type == 3) fl |= 0x04;
    return fl;
}

static int encode_key(ses_fix_state_t *st, const gps_fix_t *f, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(st != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(f != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[39]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, f->gps_us); bw_i32(&w, f->lat_e7); bw_i32(&w, f->lon_e7); bw_i32(&w, f->alt_mm);
    bw_i32(&w, f->gspeed_mms); bw_i32(&w, f->head_e5); bw_u32(&w, f->hacc_mm); bw_u16(&w, clamp_u16(f->sacc_mms));
    bw_u16(&w, f->pdop_e2); bw_u8(&w, f->fix_type); bw_u8(&w, f->sats); bw_u8(&w, fix_flags(f));
    st->prev = *f; st->have_prev = true; st->last_key_gps_us = f->gps_us; st->prev_valid = f->valid != 0;
    return finish(SES_T_FIX_KEY, &w, out, cap);
}

int ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *f, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(st != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(f != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    if (!st->have_prev || !st->prev_valid || f->gps_us - st->last_key_gps_us >= KEYFRAME_US)
        return encode_key(st, f, out, cap);
    int64_t dt = round_div(f->gps_us - st->prev.gps_us, 1000);
    int64_t dlat = (int64_t)f->lat_e7 - st->prev.lat_e7;
    int64_t dlon = (int64_t)f->lon_e7 - st->prev.lon_e7;
    int64_t dalt = round_div((int64_t)f->alt_mm - st->prev.alt_mm, 100);
    int64_t v_cms = round_div(f->gspeed_mms, 10);
    if (dt < 0 || dt > 65535 || dlat > 32767 || dlat < -32768 || dlon > 32767 || dlon < -32768 ||
        dalt > 32767 || dalt < -32768 || v_cms > 65535 || v_cms < 0)
        return encode_key(st, f, out, cap);
    uint8_t p[15]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, (uint16_t)dt); bw_i16(&w, (int16_t)dlat); bw_i16(&w, (int16_t)dlon); bw_i16(&w, (int16_t)dalt);
    bw_u16(&w, (uint16_t)v_cms); bw_u16(&w, clamp_u16(round_div(f->head_e5, 1000)));
    bw_u8(&w, clamp_u8(round_div(f->hacc_mm, 100))); bw_u8(&w, f->sats); bw_u8(&w, fix_flags(f));
    /* advance the reconstructed previous exactly as the decoder will */
    st->prev.gps_us += dt * 1000; st->prev.lat_e7 = f->lat_e7; st->prev.lon_e7 = f->lon_e7;
    st->prev.alt_mm += (int32_t)(dalt * 100); st->prev.gspeed_mms = (int32_t)(v_cms * 10);
    st->prev.head_e5 = (int32_t)(round_div(f->head_e5, 1000) * 1000); st->prev.sats = f->sats;
    st->prev.hacc_mm = (uint32_t)(clamp_u8(round_div(f->hacc_mm, 100)) * 100u);
    st->prev.valid = f->valid; st->prev_valid = f->valid != 0;
    return finish(SES_T_FIX_DELTA, &w, out, cap);
}

int ses_decode_fix(ses_fix_state_t *st, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out)
{
    CORE_ASSERT_RET(st != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    if (type == SES_T_FIX_KEY) {
        CORE_ASSERT_RET(len == 39, SES_ASSERT_CODE, -1);
        memset(out, 0, sizeof *out);
        out->gps_us = br_i64(&r); out->lat_e7 = br_i32(&r); out->lon_e7 = br_i32(&r); out->alt_mm = br_i32(&r);
        out->gspeed_mms = br_i32(&r); out->head_e5 = br_i32(&r); out->hacc_mm = br_u32(&r); out->sacc_mms = br_u16(&r);
        out->pdop_e2 = br_u16(&r); out->fix_type = br_u8(&r); out->sats = br_u8(&r);
        uint8_t fl = br_u8(&r);
        out->valid = fl & 0x01; out->flags = (uint8_t)(((fl & 0x02) ? GPS_FLAG_FIXOK : 0) | GPS_FLAG_TIME | GPS_FLAG_DATE);
        st->prev = *out; st->have_prev = true; st->last_key_gps_us = out->gps_us; st->prev_valid = out->valid != 0;
        return 1;
    }
    if (type == SES_T_FIX_DELTA) {
        CORE_ASSERT_RET(len == 15, SES_ASSERT_CODE, -1);
        CORE_ASSERT_RET(st->have_prev, SES_ASSERT_CODE, -1);
        uint16_t dt = br_u16(&r); int16_t dlat = br_i16(&r); int16_t dlon = br_i16(&r); int16_t dalt = br_i16(&r);
        uint16_t v = br_u16(&r); uint16_t head = br_u16(&r); uint8_t hacc = br_u8(&r); uint8_t sats = br_u8(&r); uint8_t fl = br_u8(&r);
        gps_fix_t *p = &st->prev;
        p->gps_us += (int64_t)dt * 1000; p->lat_e7 += dlat; p->lon_e7 += dlon; p->alt_mm += (int32_t)dalt * 100;
        p->gspeed_mms = (int32_t)v * 10; p->head_e5 = (int32_t)head * 1000; p->hacc_mm = (uint32_t)hacc * 100u; p->sats = sats;
        p->sacc_mms = 0; p->pdop_e2 = 0;
        p->fix_type = (fl & 0x04) ? 3 : 2; p->valid = fl & 0x01;
        p->flags = (uint8_t)(((fl & 0x02) ? GPS_FLAG_FIXOK : 0) | GPS_FLAG_TIME | GPS_FLAG_DATE);
        st->prev_valid = p->valid != 0;
        *out = *p;
        return 1;
    }
    return 0;
}

/* ---------------- fused ---------------- */

void ses_fused_state_init(ses_fused_state_t *st) { CORE_ASSERT_VOID(st != NULL, SES_ASSERT_CODE); memset(st, 0, sizeof *st); }
void ses_fused_state_on_fix(ses_fused_state_t *st, int64_t fix_gps_us) { CORE_ASSERT_VOID(st != NULL, SES_ASSERT_CODE); st->ref_gps_us = fix_gps_us; st->have_ref = true; }

int ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(st != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(fs != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    if (!st->have_ref) return -1;
    int64_t dt = round_div(fs->gps_us - st->ref_gps_us, 1000);
    /* dt is unsigned on the wire (§12.3). A fused sample stamped before its reference (a fix that
     * arrived late, or a time-base step) is clamped to 0 rather than dropped: the sample still
     * carries usable lean/g values, and both sides advance ref_gps_us by the same clamped dt, so
     * encoder and decoder stay in step. The cost is that such a sample is timed at the reference. */
    if (dt < 0) dt = 0;
    if (dt > 65535) dt = 65535;
    uint8_t p[11]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, (uint16_t)dt);
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->g_lat * 1000.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->g_lon * 1000.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->lean_deg * 100.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->yaw_dps * 100.0f)));
    bw_u8(&w, fs->flags);
    st->ref_gps_us += dt * 1000;
    return finish(SES_T_FUSED, &w, out, cap);
}

int ses_decode_fused(ses_fused_state_t *st, const uint8_t *payload, uint8_t len, fused_sample_t *out)
{
    CORE_ASSERT_RET(st != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 11, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(st->have_ref, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    uint16_t dt = br_u16(&r);
    memset(out, 0, sizeof *out);
    st->ref_gps_us += (int64_t)dt * 1000;
    out->gps_us = st->ref_gps_us;
    out->g_lat = (float)br_i16(&r) / 1000.0f;
    out->g_lon = (float)br_i16(&r) / 1000.0f;
    out->lean_deg = (float)br_i16(&r) / 100.0f;
    out->yaw_dps = (float)br_i16(&r) / 100.0f;
    out->flags = br_u8(&r);
    out->g_comb = sqrtf(out->g_lat * out->g_lat + out->g_lon * out->g_lon);
    return 1;
}

/* ---------------- lap / sector ---------------- */

static void put_stats(bw_t *w, const lap_stats_t *s)
{
    CORE_ASSERT_VOID(w != NULL, SES_ASSERT_CODE);
    CORE_ASSERT_VOID(s != NULL, SES_ASSERT_CODE);
    bw_u16(w, s->max_speed_cms); bw_u16(w, s->min_speed_cms); bw_i16(w, s->max_lean_l_cdeg); bw_i16(w, s->max_lean_r_cdeg);
    bw_i16(w, s->max_glat_e3); bw_i16(w, s->max_gacc_e3); bw_i16(w, s->max_gbrake_e3);
}
static void get_stats(br_t *r, lap_stats_t *s)
{
    CORE_ASSERT_VOID(r != NULL, SES_ASSERT_CODE);
    CORE_ASSERT_VOID(s != NULL, SES_ASSERT_CODE);
    s->max_speed_cms = br_u16(r); s->min_speed_cms = br_u16(r); s->max_lean_l_cdeg = br_i16(r); s->max_lean_r_cdeg = br_i16(r);
    s->max_glat_e3 = br_i16(r); s->max_gacc_e3 = br_i16(r); s->max_gbrake_e3 = br_i16(r);
}

int ses_encode_lap(const lap_result_t *lap, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(lap != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    if (lap->n_sectors > LAP_MAX_SECTORS + 1) return -1;
    uint8_t p[30 + 4 * (LAP_MAX_SECTORS + 1)]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, lap->lap_no); bw_i64(&w, lap->start_gps_us); bw_u32(&w, lap->time_ms); bw_u8(&w, lap->flags); bw_u8(&w, lap->n_sectors);
    for (uint8_t i = 0; i < lap->n_sectors; i++) bw_u32(&w, lap->sector_ms[i]);
    put_stats(&w, &lap->stats);
    return finish(SES_T_LAP, &w, out, cap);
}

int ses_decode_lap(const uint8_t *payload, uint8_t len, lap_result_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len >= 30, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->lap_no = br_u16(&r); out->start_gps_us = br_i64(&r); out->time_ms = br_u32(&r); out->flags = br_u8(&r); out->n_sectors = br_u8(&r);
    CORE_ASSERT_RET(out->n_sectors <= LAP_MAX_SECTORS + 1, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 30 + 4 * out->n_sectors, SES_ASSERT_CODE, -1);
    for (uint8_t i = 0; i < out->n_sectors; i++) out->sector_ms[i] = br_u32(&r);
    get_stats(&r, &out->stats);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_sector(uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[19]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, lap_no); bw_u8(&w, idx); bw_i64(&w, gps_us); bw_u32(&w, split_ms); bw_i32(&w, delta_ms);
    return finish(SES_T_SECTOR, &w, out, cap);
}

int ses_decode_sector(const uint8_t *payload, uint8_t len, ses_sector_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 19, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->lap_no = br_u16(&r); out->idx = br_u8(&r); out->gps_us = br_i64(&r);
    out->split_ms = br_u32(&r); out->delta_ms = br_i32(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- drag ---------------- */

int ses_encode_drag_run(const drag_result_t *run, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(run != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    if (run->n_gates > DRAG_MAX_GATES) return -1;
    uint8_t p[14 + 12 * DRAG_MAX_GATES]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, run->run_no); bw_i64(&w, run->t0_gps_us); bw_u8(&w, run->flags); bw_u16(&w, run->trap_cms); bw_u8(&w, run->n_gates);
    for (uint8_t i = 0; i < run->n_gates; i++) {
        const drag_gate_res_t *g = &run->gates[i];
        bw_u8(&w, g->gate_id); bw_u32(&w, g->time_ms); bw_u16(&w, g->speed_cms); bw_u32(&w, g->dist_cm); bw_u8(&w, g->hit);
    }
    return finish(SES_T_DRAG_RUN, &w, out, cap);
}

int ses_decode_drag_run(const uint8_t *payload, uint8_t len, drag_result_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len >= 14, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->run_no = br_u16(&r); out->t0_gps_us = br_i64(&r); out->flags = br_u8(&r); out->trap_cms = br_u16(&r); out->n_gates = br_u8(&r);
    CORE_ASSERT_RET(out->n_gates <= DRAG_MAX_GATES, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 14 + 12 * out->n_gates, SES_ASSERT_CODE, -1);
    for (uint8_t i = 0; i < out->n_gates; i++) {
        drag_gate_res_t *g = &out->gates[i];
        g->gate_id = br_u8(&r); g->time_ms = br_u32(&r); g->speed_cms = br_u16(&r); g->dist_cm = br_u32(&r); g->hit = br_u8(&r);
    }
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_drag_gate(uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[21]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, run_no); bw_u8(&w, gate_id); bw_i64(&w, gps_us); bw_u32(&w, time_ms); bw_u16(&w, speed_cms); bw_u32(&w, dist_cm);
    return finish(SES_T_DRAG_GATE, &w, out, cap);
}

int ses_decode_drag_gate(const uint8_t *payload, uint8_t len, ses_drag_gate_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 21, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->run_no = br_u16(&r); out->gate_id = br_u8(&r); out->gps_us = br_i64(&r);
    out->time_ms = br_u32(&r); out->speed_cms = br_u16(&r); out->dist_cm = br_u32(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- misc ---------------- */

int ses_encode_event(int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[22]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_i64(&w, gps_us); bw_u16(&w, code); bw_u32(&w, arg);
    return finish(SES_T_EVENT, &w, out, cap);
}

int ses_decode_event(const uint8_t *payload, uint8_t len, ses_event_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 22, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->gps_us = br_i64(&r); out->code = br_u16(&r); out->arg = br_u32(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[17]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_i64(&w, gps_us); bw_u8(&w, quality);
    return finish(SES_T_TIME_MAP, &w, out, cap);
}

int ses_decode_time_map(const uint8_t *payload, uint8_t len, ses_time_map_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 17, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->gps_us = br_i64(&r); out->quality = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_venue(uint16_t venue_id, uint16_t layout_id, const char *name, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(name != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[36]; bw_t w; bw_init(&w, p, sizeof p);
    char nm[32]; memset(nm, 0, sizeof nm); strncpy(nm, name, sizeof nm - 1);
    bw_u16(&w, venue_id); bw_u16(&w, layout_id); bw_bytes(&w, nm, sizeof nm);
    return finish(SES_T_VENUE, &w, out, cap);
}

int ses_decode_venue(const uint8_t *payload, uint8_t len, ses_venue_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 36, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->venue_id = br_u16(&r); out->layout_id = br_u16(&r);
    br_bytes(&r, out->name, 32); out->name[32] = '\0';
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_power(int64_t mono_us, uint8_t state, uint16_t batt_mv, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[11]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_u8(&w, state); bw_u16(&w, batt_mv);
    return finish(SES_T_POWER, &w, out, cap);
}

int ses_decode_power(const uint8_t *payload, uint8_t len, ses_power_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 11, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->state = br_u8(&r); out->batt_mv = br_u16(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_end(int64_t gps_us, uint8_t reason, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[9]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, gps_us); bw_u8(&w, reason);
    return finish(SES_T_END, &w, out, cap);
}

int ses_decode_end(const uint8_t *payload, uint8_t len, ses_end_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 9, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->gps_us = br_i64(&r); out->reason = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_mark(int64_t gps_us, uint8_t kind, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[9]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, gps_us); bw_u8(&w, kind);
    return finish(SES_T_MARK, &w, out, cap);
}

int ses_decode_mark(const uint8_t *payload, uint8_t len, ses_mark_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 9, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    out->gps_us = br_i64(&r); out->kind = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- calibration ---------------- */

int ses_encode_calib(const ses_calib_t *c, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(c != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[25]; bw_t w; bw_init(&w, p, sizeof p);
    for (int i = 0; i < 9; i++) bw_i16(&w, c->r_e4[i]);
    for (int i = 0; i < 3; i++) bw_i16(&w, c->gbias[i]);
    bw_u8(&w, c->calib_flags);
    return finish(SES_T_CALIB, &w, out, cap);
}

int ses_decode_calib(const uint8_t *payload, uint8_t len, ses_calib_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 25, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    for (int i = 0; i < 9; i++) out->r_e4[i] = br_i16(&r);
    for (int i = 0; i < 3; i++) out->gbias[i] = br_i16(&r);
    out->calib_flags = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- header ---------------- */

int ses_encode_hdr(const ses_hdr_t *h, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(h != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    uint8_t p[94]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u8(&w, 1);                                   /* ver */
    bw_u8(&w, 0);                                   /* reserved, keeps payload at the documented 94 bytes */
    bw_bytes(&w, h->session_id, 10); bw_u8(&w, h->mode); bw_u8(&w, h->variant);
    bw_u16(&w, h->venue_id); bw_u16(&w, h->layout_id); bw_bytes(&w, h->fw, 16); bw_bytes(&w, h->hwid, 24);
    bw_u8(&w, h->log_profile); bw_u8(&w, h->fused_hz); bw_u8(&w, h->gps_hz); bw_i64(&w, h->start_gps_us);
    for (int i = 0; i < 9; i++) bw_i16(&w, h->r_e4[i]);
    for (int i = 0; i < 3; i++) bw_i16(&w, h->gbias[i]);
    bw_u8(&w, h->calib_flags);
    return finish(SES_T_SESSION_HDR, &w, out, cap);
}

int ses_decode_hdr(const uint8_t *payload, uint8_t len, ses_hdr_t *out)
{
    CORE_ASSERT_RET(payload != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, SES_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len == 94, SES_ASSERT_CODE, -1);
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    CORE_ASSERT_RET(br_u8(&r) == 1, SES_ASSERT_CODE, -1);
    br_u8(&r);                                      /* reserved, currently unused */
    br_bytes(&r, out->session_id, 10); out->session_id[10] = '\0'; out->mode = br_u8(&r); out->variant = br_u8(&r);
    out->venue_id = br_u16(&r); out->layout_id = br_u16(&r);
    br_bytes(&r, out->fw, 16); out->fw[16] = '\0';          /* the wire field may use all 16 bytes */
    br_bytes(&r, out->hwid, 24); out->hwid[24] = '\0';
    out->log_profile = br_u8(&r); out->fused_hz = br_u8(&r); out->gps_hz = br_u8(&r); out->start_gps_us = br_i64(&r);
    for (int i = 0; i < 9; i++) out->r_e4[i] = br_i16(&r);
    for (int i = 0; i < 3; i++) out->gbias[i] = br_i16(&r);
    out->calib_flags = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}
