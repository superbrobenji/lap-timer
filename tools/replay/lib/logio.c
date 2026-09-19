#include "replay/logio.h"
#include "core/consts.h"
#include <string.h>

/* Session .log writer and reader (spec §12.2–§12.5).
 *
 * Every ses_encode_* in components/core/session/ses_records.c ends in ses_frame_encode(), so the
 * record encoders return a COMPLETE frame (sync | type | len | payload | crc16), not a bare
 * payload. logio therefore never frames anything itself: it emits what the encoder produced. */

/* Largest frame the format allows: type + len + payload + crc16, plus one byte for the sync. */
#define LOGW_FRAME_CAP (2 + SES_MAX_PAYLOAD + 2 + 1)
/* Read granularity of logr_read_file; matches the logger's 4 KB buffer (§12.5). */
#define LOGR_CHUNK 4096

/* ---------------- writer ---------------- */

static void logw_reset(logw_t *w)
{
    memset(w, 0, sizeof *w);
    ses_fix_state_init(&w->fix_st);
    ses_fused_state_init(&w->fused_st);
}

int logw_open_file(logw_t *w, const char *path)
{
    logw_reset(w);
    w->f = fopen(path, "wb");
    if (!w->f) { w->err = -1; return -1; }
    return 0;
}

void logw_open_mem(logw_t *w, uint8_t *buf, size_t cap)
{
    logw_reset(w);
    w->mem = buf; w->cap = cap;
}

/* Appends one encoded frame. n is the encoder's return value, so a -1 from the encoder (payload too
 * large, cap too small) becomes a sticky write error here. Returns n, or -1. */
static int emit(logw_t *w, const uint8_t *frame, int n)
{
    if (w->err) return -1;
    if (n <= 0) { w->err = -1; return -1; }
    size_t len = (size_t)n;
    if (w->f) {
        if (fwrite(frame, 1, len, w->f) != len) { w->err = -1; return -1; }
    } else if (w->mem) {
        if (w->len + len > w->cap) { w->err = -1; return -1; }
        memcpy(w->mem + w->len, frame, len);
    } else {
        w->err = -1; return -1;                       /* neither logw_open_file nor logw_open_mem */
    }
    w->len += len;
    w->frames++;
    return n;
}

int logw_hdr(logw_t *w, const ses_hdr_t *h)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_hdr(h, fr, sizeof fr));
}

int logw_venue(logw_t *w, uint16_t venue_id, uint16_t layout_id, const char *name)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_venue(venue_id, layout_id, name, fr, sizeof fr));
}

int logw_time_map(logw_t *w, int64_t mono_us, int64_t gps_us, uint8_t quality)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_time_map(mono_us, gps_us, quality, fr, sizeof fr));
}

int logw_fix(logw_t *w, const gps_fix_t *fix)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    int n = emit(w, fr, ses_encode_fix(&w->fix_st, fix, fr, sizeof fr));
    /* §12.4: FUSED.dt_ms is relative to the last FIX_* when no FUSED followed it, so the writer's
     * fused reference moves to every fix it emits — exactly what the reader does on decode. */
    if (n > 0) ses_fused_state_on_fix(&w->fused_st, fix->gps_us);
    return n;
}

int logw_fused(logw_t *w, const fused_sample_t *fs)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_fused(&w->fused_st, fs, fr, sizeof fr));
}

int logw_lap(logw_t *w, const lap_result_t *lap)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_lap(lap, fr, sizeof fr));
}

int logw_sector(logw_t *w, uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_sector(lap_no, idx, gps_us, split_ms, delta_ms, fr, sizeof fr));
}

int logw_drag_run(logw_t *w, const drag_result_t *run)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_drag_run(run, fr, sizeof fr));
}

int logw_drag_gate(logw_t *w, uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_drag_gate(run_no, gate_id, gps_us, time_ms, speed_cms, dist_cm, fr, sizeof fr));
}

int logw_event(logw_t *w, int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_event(mono_us, gps_us, code, arg, fr, sizeof fr));
}

int logw_calib(logw_t *w, const ses_calib_t *c)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_calib(c, fr, sizeof fr));
}

int logw_mark(logw_t *w, int64_t gps_us, uint8_t kind)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_mark(gps_us, kind, fr, sizeof fr));
}

int logw_power(logw_t *w, int64_t mono_us, uint8_t state, uint16_t batt_mv)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_power(mono_us, state, batt_mv, fr, sizeof fr));
}

int logw_end(logw_t *w, int64_t gps_us, uint8_t reason)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_end(gps_us, reason, fr, sizeof fr));
}

int logw_close(logw_t *w)
{
    if (w->f) {
        if (fflush(w->f) != 0) w->err = -1;
        if (fclose(w->f) != 0) w->err = -1;
        w->f = NULL;
    }
    return w->err;
}

/* ---------------- reader ---------------- */

/* One framed record. `payload` is the reader's internal buffer and is valid only here. */
static void logr_frame_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    logr_t *r = ctx;
    const logr_cb_t *cb = r->cb;
    bool ok = false;
    r->n_frames++;

    switch (type) {
    case SES_T_FIX_KEY:
    case SES_T_FIX_DELTA: {
        gps_fix_t f;
        if (ses_decode_fix(&r->fix_st, type, payload, len, &f) == 1) {
            ok = true;
            /* §12.4: keep the fused reference in step with the writer's logw_fix. */
            ses_fused_state_on_fix(&r->fused_st, f.gps_us);
            if (cb && cb->on_fix) cb->on_fix(&f, r->ctx);
        }
        break;
    }
    case SES_T_FUSED: {
        fused_sample_t fs;
        if (ses_decode_fused(&r->fused_st, payload, len, &fs) == 1) {
            ok = true;
            if (cb && cb->on_fused) cb->on_fused(&fs, r->ctx);
        }
        break;
    }
    case SES_T_SESSION_HDR: {
        ses_hdr_t h;
        if (ses_decode_hdr(payload, len, &h) == 1) { ok = true; if (cb && cb->on_hdr) cb->on_hdr(&h, r->ctx); }
        break;
    }
    case SES_T_VENUE: {
        ses_venue_t v;
        if (ses_decode_venue(payload, len, &v) == 1) { ok = true; if (cb && cb->on_venue) cb->on_venue(&v, r->ctx); }
        break;
    }
    case SES_T_TIME_MAP: {
        ses_time_map_t t;
        if (ses_decode_time_map(payload, len, &t) == 1) { ok = true; if (cb && cb->on_time_map) cb->on_time_map(&t, r->ctx); }
        break;
    }
    case SES_T_LAP: {
        lap_result_t l;
        if (ses_decode_lap(payload, len, &l) == 1) { ok = true; if (cb && cb->on_lap) cb->on_lap(&l, r->ctx); }
        break;
    }
    case SES_T_SECTOR: {
        ses_sector_t s;
        if (ses_decode_sector(payload, len, &s) == 1) { ok = true; if (cb && cb->on_sector) cb->on_sector(&s, r->ctx); }
        break;
    }
    case SES_T_DRAG_RUN: {
        drag_result_t d;
        if (ses_decode_drag_run(payload, len, &d) == 1) { ok = true; if (cb && cb->on_drag_run) cb->on_drag_run(&d, r->ctx); }
        break;
    }
    case SES_T_DRAG_GATE: {
        ses_drag_gate_t g;
        if (ses_decode_drag_gate(payload, len, &g) == 1) { ok = true; if (cb && cb->on_drag_gate) cb->on_drag_gate(&g, r->ctx); }
        break;
    }
    case SES_T_EVENT: {
        ses_event_t e;
        if (ses_decode_event(payload, len, &e) == 1) { ok = true; if (cb && cb->on_event) cb->on_event(&e, r->ctx); }
        break;
    }
    case SES_T_CALIB: {
        ses_calib_t c;
        if (ses_decode_calib(payload, len, &c) == 1) { ok = true; if (cb && cb->on_calib) cb->on_calib(&c, r->ctx); }
        break;
    }
    case SES_T_MARK: {
        ses_mark_t m;
        if (ses_decode_mark(payload, len, &m) == 1) { ok = true; if (cb && cb->on_mark) cb->on_mark(&m, r->ctx); }
        break;
    }
    case SES_T_POWER: {
        ses_power_t p;
        if (ses_decode_power(payload, len, &p) == 1) { ok = true; if (cb && cb->on_power) cb->on_power(&p, r->ctx); }
        break;
    }
    case SES_T_END: {
        ses_end_t e;
        if (ses_decode_end(payload, len, &e) == 1) { ok = true; if (cb && cb->on_end) cb->on_end(&e, r->ctx); }
        break;
    }
    default:
        break;                                        /* unknown type: counted as bad below */
    }

    if (ok) {
        /* Every known type is <= SES_T_END (0x7F); the guard keeps a future 8-bit type out of the
         * 128-entry histogram rather than trusting the switch above to stay in range. */
        if (type < 128) r->n_by_type[type]++;
    } else {
        r->n_bad++;
        if (cb && cb->on_bad) cb->on_bad(type, len, r->ctx);
    }
}

void logr_init(logr_t *r, const logr_cb_t *cb, void *ctx)
{
    memset(r, 0, sizeof *r);
    ses_reader_init(&r->rd);
    ses_fix_state_init(&r->fix_st);
    ses_fused_state_init(&r->fused_st);
    r->cb = cb; r->ctx = ctx;
}

void logr_feed(logr_t *r, const uint8_t *buf, size_t n)
{
    uint8_t t, l; const uint8_t *p;
    ses_reader_push(&r->rd, buf, n);
    while (ses_reader_next(&r->rd, &t, &p, &l) == 1) logr_frame_cb(t, p, l, r);
}

void logr_finish(logr_t *r)
{
    uint8_t t, l; const uint8_t *p;
    ses_reader_finish(&r->rd);
    while (ses_reader_next(&r->rd, &t, &p, &l) == 1) logr_frame_cb(t, p, l, r);
}

int logr_read_file(logr_t *r, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[LOGR_CHUNK];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n) logr_feed(r, buf, n);
        if (n < sizeof buf) break;                    /* short read: EOF or error, ferror decides */
    }
    logr_finish(r);
    int rc = ferror(f) ? -1 : 0;
    fclose(f);
    return rc;
}
