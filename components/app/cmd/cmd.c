/* cmd.c -- transport-agnostic command dispatch (spec §18.1).
 *
 * Implements the non-file ops of §18.1: STATUS, CONFIG_GET/SET, ERRLOG_GET/CLEAR, DIAG_GET,
 * DELETE, CLOSE. LIST/OPEN/READ (file streaming, §14.3) are Task 5 and return an E_CONN_PROTO
 * ERROR chunk for now. The TRACKS, COREDUMP, OTA and TIME_SYNC ops (§18.1) are later sessions.
 *
 * Output is streamed through the caller's emit callback as data chunks (<= 496 B payload). Text
 * ops build a JSON document in a static buffer, then emit it split across chunks. STATUS builds
 * the 20-byte §18.2 status. No malloc: all assembly buffers are static (one request at a time).
 */
#include "app/cmd.h"

#include "build_config.h"          /* CFG_FW_VERSION, CFG_HWID */

#include "app/logger.h"           /* logger_open_session_id -- DELETE must skip the open session */
#include "app/lt_assert.h"
#include "app/lt_err.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"

#include "core/cfg.h"
#include "core/exp.h"              /* streaming exporter (vbo/nmea/json) */
#include "core/ses.h"             /* .log/.sum frame reader + record codecs */
#include "hal/storage.h"

#include "esp_rom_crc.h"          /* esp_rom_crc32_le -- matches lt_rtc's on-device CRC */
#include "esp_system.h"            /* esp_get_minimum_free_heap_size */
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define CMD_ASSERT_CODE 0x0B40   /* Power of 10 rule 5 (app/lt_assert.h); cmd.c's own code */

/* Rule 2: a fixed, generous static bound for the file-read/streaming loops below. The `storage`
 * partition (partitions.csv) is 0x150000 B (~1.34 MiB); at 256 B per sto_read() chunk (sizeof
 * s_io) that is <= 5376 chunks for the ENTIRE filesystem, so no single file's read loop can ever
 * take more than that many iterations. 8192 is a generous round number above that ceiling. */
enum { CMD_STREAM_MAX_CHUNKS = 8192 };

/* ------------------------------------------------------------------ *
 *  static assembly state (one request in flight, §18.1)
 * ------------------------------------------------------------------ */
static char           s_json[3072];   /* JSON / text response assembly (cfg, errlog, diag) */
static cfg_t          s_cfg;          /* working config for CONFIG_GET/SET */
static lt_err_entry_t s_err[32];      /* error-ring snapshot (ERR_RING_LEN, §15.2) */

/* ------------------------------------------------------------------ *
 *  chunked emit helpers (§18.1)
 * ------------------------------------------------------------------ */
static void put_u16le(uint8_t *p, uint16_t v)
{
    LT_ASSERT_VOID(p != NULL, CMD_ASSERT_CODE);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put_u32le(uint8_t *p, uint32_t v)
{
    LT_ASSERT_VOID(p != NULL, CMD_ASSERT_CODE);
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Emit `data` split into <= CMD_CHUNK_MAX chunks; LAST is set on the final chunk when `last`.
 * A zero-length body still emits one (possibly LAST) empty chunk (the CLOSE/ack case). */
static int emit_bytes(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq,
                      const uint8_t *data, size_t len, bool last)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(data != NULL || len == 0, CMD_ASSERT_CODE, -1);
    if (len == 0)
        return emit(ctx, tag, (*seq)++, last ? CMD_FLAG_LAST : 0, NULL, 0);

    size_t off = 0;
    /* Rule 2: off advances by >= 1 B every iteration (CMD_CHUNK_MAX > 0), so len + 2 always
     * suffices -- a generous, statically-evident bound tied to this call's own `len`. */
    const size_t EMIT_MAX_STEPS = len + 2;
    size_t steps = 0;
    while (off < len) {
        LT_ASSERT_RET(steps++ < EMIT_MAX_STEPS, CMD_ASSERT_CODE, -1);
        size_t n = len - off;
        if (n > CMD_CHUNK_MAX) n = CMD_CHUNK_MAX;
        bool is_last = last && (off + n >= len);
        if (emit(ctx, tag, (*seq)++, is_last ? CMD_FLAG_LAST : 0, data + off, n) != 0) return -1;
        off += n;
    }
    return 0;
}

/* Emit a single ERROR|LAST chunk: payload = code u16 (LE) | msg utf8 (§18.1). */
static int emit_error(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq,
                      uint16_t code, const char *msg)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    uint8_t buf[2 + 128];
    put_u16le(buf, code);
    size_t mlen = 0;
    if (msg) {
        mlen = strlen(msg);
        if (mlen > sizeof(buf) - 2) mlen = sizeof(buf) - 2;
        memcpy(buf + 2, msg, mlen);
    }
    return emit(ctx, tag, (*seq)++, CMD_FLAG_ERROR | CMD_FLAG_LAST, buf, 2 + mlen);
}

/* ------------------------------------------------------------------ *
 *  shared getters
 * ------------------------------------------------------------------ */
static uint32_t storage_free_kb(void)
{
    sto_info_t si;
    return (sto_info(&si) == 0) ? si.free_kb : 0;
}

/* session_count: number of `.sum` files under /sessions (one per session, §12.1). */
static void count_sum_cb(const char *name, uint32_t size, void *ctx)
{
    LT_ASSERT_VOID(name != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(ctx != NULL, CMD_ASSERT_CODE);
    (void)size;
    const char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".sum") == 0) (*(int *)ctx)++;
}
static uint16_t session_count(void)
{
    int c = 0;
    (void)sto_list("/sessions", count_sum_cb, &c);
    return (c > 0xFFFF) ? 0xFFFF : (uint16_t)c;
}

/* fw char[7]: the git version trimmed of a leading 'v', truncated to fit 7 bytes incl. NUL. */
static void fw_short(char *dst, size_t cap)
{
    LT_ASSERT_VOID(dst != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(cap > 0, CMD_ASSERT_CODE);   /* dst[i] = '\0' below would write out of bounds at cap == 0 */
    const char *v = CFG_FW_VERSION;
    if (*v == 'v' || *v == 'V') v++;
    size_t i = 0;
    for (; i + 1 < cap && v[i]; i++) dst[i] = v[i];
    dst[i] = '\0';
}

/* Load the current config into s_cfg: defaults, then overwrite with the stored blob if valid. */
static void load_cfg(void)
{
    cfg_defaults(&s_cfg);
    (void)lt_cfg_load(&s_cfg);   /* < 0 => no valid blob; keep defaults */
}

/* ------------------------------------------------------------------ *
 *  ops
 * ------------------------------------------------------------------ */

/* STATUS (0x01) -> the 20-byte §18.2 status record (little-endian). */
static int op_status(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    uint8_t st[20];
    memset(st, 0, sizeof st);
    st[0] = 1;                                              /* proto_ver = 1 */
    st[1] = 0;                                              /* state: device state machine lands later */
    put_u16le(&st[2], (uint16_t)(sys_flags_get() & 0xFFFFu));
    st[4] = 0;                                              /* batt_pct: placeholder (power lands later) */
    put_u16le(&st[5], 0);                                  /* batt_mv:  placeholder */
    put_u32le(&st[7], storage_free_kb());
    put_u16le(&st[11], session_count());
    fw_short((char *)&st[13], 7);                          /* fw char[7] */
    return emit_bytes(emit, ctx, tag, seq, st, sizeof st, true);
}

/* CONFIG_GET (0x10) -> cfg_to_json of the loaded config. */
static int op_config_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    load_cfg();
    int n = cfg_to_json(&s_cfg, s_json, sizeof s_json);
    if (n < 0) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config encode failed");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_json, (size_t)n, true);
}

/* CONFIG_SET (0x11) -> merge JSON, clamp, persist; ack, or ERROR with the parse message. */
static int op_config_set(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    load_cfg();
    char err[96];
    err[0] = '\0';
    if (cfg_from_json(&s_cfg, (const char *)payload, len, err, sizeof err) != 0)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, err[0] ? err : "config json error");
    (void)cfg_validate(&s_cfg);                            /* clamps in place; returns corrections */
    if (lt_cfg_save(&s_cfg) != 0)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config save failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true); /* ack */
}

/* ERRLOG_GET (0x14) -> JSON array of the error ring, oldest first. */
static int op_errlog_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    int n = lt_errlog_snapshot(s_err, (int)(sizeof s_err / sizeof s_err[0]));
    int w = 0;
    w += snprintf(s_json + w, sizeof s_json - (size_t)w, "[");
    for (int i = 0; i < n && w > 0 && (size_t)w < sizeof s_json; i++) {
        w += snprintf(s_json + w, sizeof s_json - (size_t)w,
                      "%s{\"code\":%u,\"arg\":%u,\"uptime_s\":%u,\"boot\":%u}",
                      i ? "," : "", (unsigned)s_err[i].code, (unsigned)s_err[i].arg,
                      (unsigned)s_err[i].uptime_s, (unsigned)s_err[i].boot);
    }
    if (w > 0 && (size_t)w < sizeof s_json)
        w += snprintf(s_json + w, sizeof s_json - (size_t)w, "]");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_json, strlen(s_json), true);
}

/* ERRLOG_CLEAR (0x15) -> clear the ring; ack. */
static int op_errlog_clear(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    lt_errlog_clear();
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* DIAG_GET (0x16) -> §17.10 diagnostics as JSON: fw, hwid, uptime, counters, storage, heap,
 * sys_flags, and the last 5 error codes. */
static int op_diag_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    const lt_counters_t *c = lt_counters();
    uint32_t up      = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t heapmin = (uint32_t)esp_get_minimum_free_heap_size();
    int n = lt_errlog_snapshot(s_err, (int)(sizeof s_err / sizeof s_err[0]));
    int last = (n > 5) ? 5 : n;                            /* last (newest) 5 codes */

    int w = snprintf(s_json, sizeof s_json,
        "{\"fw\":\"%s\",\"hwid\":\"%s\",\"uptime_s\":%u,"
        "\"boots\":%u,\"crashes\":%u,\"wdt\":%u,\"gps_reset\":%u,\"i2c_recover\":%u,"
        "\"storage_free_kb\":%u,\"heap_min_free\":%u,\"sys_flags\":%u,\"last_errors\":[",
        CFG_FW_VERSION, CFG_HWID, (unsigned)up,
        (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt,
        (unsigned)c->gps_reset, (unsigned)c->i2c_recover,
        (unsigned)storage_free_kb(), (unsigned)heapmin, (unsigned)sys_flags_get());
    for (int i = 0; i < last && w > 0 && (size_t)w < sizeof s_json; i++)
        w += snprintf(s_json + w, sizeof s_json - (size_t)w, "%s%u",
                      i ? "," : "", (unsigned)s_err[n - last + i].code);
    if (w > 0 && (size_t)w < sizeof s_json)
        w += snprintf(s_json + w, sizeof s_json - (size_t)w, "]}");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_json, strlen(s_json), true);
}

/* DELETE (0x06) -> unlink <id>.log and <id>.sum; ack. Request payload is id char[10]. */
static int op_delete(const uint8_t *payload, size_t len,
                     cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    char id[11];
    size_t idl = (len < 10) ? len : 10;
    memcpy(id, payload, idl);
    id[idl] = '\0';
    id[strcspn(id, " ")] = '\0';   /* drop any padding/trailing space */

    /* F5: never unlink the session the logger currently has open for writing -- the fd would be
     * orphaned and the in-flight session's data lost. Refuse it instead of racing the logger. */
    const char *open_id = logger_open_session_id();
    if (open_id && strcmp(open_id, id) == 0)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "session is open; close it first");

    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s.log", id);
    (void)sto_unlink(path);
    (void)snprintf(path, sizeof path, "/sessions/%s.sum", id);
    (void)sto_unlink(path);
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* ================================================================== *
 *  file streaming -- LIST / OPEN / READ (§18.1, §18.4, §14.3)
 *
 * OPEN/READ stream a session file as data chunks and, per §18.1, append the 4-byte CRC32 of the
 * whole payload in the final chunk's tail. The stream is generated from byte offset 0 every time
 * (deterministic, side-effect free); READ resumes at `offset` by regenerating and emitting only
 * the bytes at or beyond it, while the running CRC still covers the whole payload (0..EOF) so the
 * client can verify the reassembled file. There is no persistent per-byte cursor: this keeps the
 * exporter re-entrancy-free and needs no heap; a resume re-reads the file up to `offset`.
 * ================================================================== */

/* Chunk writer: buffers up to CMD_CHUNK_MAX raw payload bytes, folds every byte into a running
 * CRC32, and emits <= 496 B data chunks. `skip` bytes are folded into the CRC but not emitted
 * (READ offset resume). `tail` appends the 4-byte CRC32 (LE) inside the final chunk (§18.1). */
typedef struct {
    cmd_emit_fn emit;
    void       *ctx;
    uint8_t     tag;
    uint16_t    seq;
    uint8_t     buf[CMD_CHUNK_MAX];
    size_t      len;
    uint32_t    crc;      /* running CRC32 over every payload byte, including skipped ones */
    uint32_t    skip;     /* bytes still to skip before emitting (resume offset) */
    bool        tail;     /* append the 4-byte CRC32 tail in the final chunk */
    int         err;      /* set when the emit callback (transport) reports failure */
} cw_t;
static cw_t s_cw;

static void cw_init(cw_t *w, cmd_emit_fn emit, void *ctx, uint8_t tag, uint32_t skip, bool tail)
{
    LT_ASSERT_VOID(w != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(emit != NULL, CMD_ASSERT_CODE);
    w->emit = emit; w->ctx = ctx; w->tag = tag; w->seq = 0;
    w->len = 0; w->crc = 0; w->skip = skip; w->tail = tail; w->err = 0;
}

static int cw_flush(cw_t *w, bool last)
{
    LT_ASSERT_RET(w != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(w->len <= CMD_CHUNK_MAX, CMD_ASSERT_CODE, -1);   /* chunk buffer capacity before the emit */
    if (w->err) return -1;
    if (w->emit(w->ctx, w->tag, w->seq++, last ? CMD_FLAG_LAST : 0, w->buf, w->len) != 0) {
        w->err = -1; return -1;
    }
    w->len = 0;
    return 0;
}

static int cw_write(cw_t *w, const uint8_t *p, size_t n)
{
    LT_ASSERT_RET(w != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(p != NULL || n == 0, CMD_ASSERT_CODE, -1);
    if (w->err) return -1;
    w->crc = esp_rom_crc32_le(w->crc, p, (uint32_t)n);   /* whole payload, even skipped bytes */
    size_t off = 0;
    if (w->skip) {
        size_t s = (w->skip < n) ? (size_t)w->skip : n;
        w->skip -= (uint32_t)s;
        off = s;
    }
    /* Rule 2: each iteration either advances off by >= 1 B, or (when the chunk buffer is exactly
     * full) flushes and frees CMD_CHUNK_MAX B of room, so n + 2 always suffices. */
    const size_t CW_MAX_STEPS = n + 2;
    size_t steps = 0;
    while (off < n) {
        LT_ASSERT_RET(steps++ < CW_MAX_STEPS, CMD_ASSERT_CODE, -1);
        size_t room = CMD_CHUNK_MAX - w->len;
        size_t take = n - off;
        if (take > room) take = room;
        memcpy(w->buf + w->len, p + off, take);
        w->len += take;
        off    += take;
        if (w->len == CMD_CHUNK_MAX && cw_flush(w, false) != 0) return -1;
    }
    return 0;
}

static int cw_finish(cw_t *w)
{
    LT_ASSERT_RET(w != NULL, CMD_ASSERT_CODE, -1);
    if (w->err) return -1;
    if (w->tail) {
        uint8_t t[4];
        put_u32le(t, w->crc);                            /* CRC of the payload (tail excluded) */
        if (w->len + 4 > CMD_CHUNK_MAX && cw_flush(w, false) != 0) return -1;
        LT_ASSERT_RET(w->len + 4 <= CMD_CHUNK_MAX, CMD_ASSERT_CODE, -1);   /* chunk buffer capacity before this memcpy */
        memcpy(w->buf + w->len, t, 4);
        w->len += 4;
    }
    return cw_flush(w, true);
}

/* ---- exp pump: decode .log frames -> exp_feed -> exp_pull -> chunk writer ---- */
static exp_t        s_exp;   /* ~1.3 KB: kept static, off the 6 KB REPL stack */
static ses_reader_t s_sr;    /* ~0.8 KB */
static uint8_t      s_io[256];

typedef struct { exp_t *e; cw_t *w; int err; } exp_pump_t;

/* Drain everything currently in the exporter window into the chunk writer. */
static int drain_exp(exp_t *e, cw_t *w)
{
    LT_ASSERT_RET(e != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(w != NULL, CMD_ASSERT_CODE, -1);
    uint8_t out[128];
    size_t  got;
    /* Rule 2: each round either drains >= 1 B out of the exporter's EXP_WINDOW-sized window or
     * returns, so EXP_WINDOW + 4 (pessimistically assuming 1 B/pull) is a generous static bound. */
    const int DRAIN_EXP_MAX_STEPS = EXP_WINDOW + 4;
    int steps = 0;
    for (;;) {
        LT_ASSERT_RET(steps++ < DRAIN_EXP_MAX_STEPS, CMD_ASSERT_CODE, -1);
        if (exp_pull(e, out, sizeof out, &got) != 0) return -1;
        if (got == 0) return 0;
        if (cw_write(w, out, got) != 0) return -1;
    }
}

/* ses reader callback: feed each decoded frame, honouring the EXP_FULL retry contract
 * (on EXP_FULL pull the window empty, then re-feed the same frame). */
static void exp_frame_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *vctx)
{
    LT_ASSERT_VOID(payload != NULL || len == 0, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(vctx != NULL, CMD_ASSERT_CODE);
    exp_pump_t *pp = (exp_pump_t *)vctx;
    if (pp->err || pp->w->err) return;
    int r;
    /* Rule 2: EXP_WINDOW (1024 B) is well above the largest single encoded frame (SES_MAX_PAYLOAD
     * bytes), so a single drain always frees enough room to re-feed; 16 retries is generous. */
    enum { EXP_FEED_MAX_RETRIES = 16 };
    int retries = 0;
    while ((r = exp_feed(pp->e, type, payload, len)) == EXP_FULL) {
        LT_ASSERT_VOID(retries++ < EXP_FEED_MAX_RETRIES, CMD_ASSERT_CODE);
        if (drain_exp(pp->e, pp->w) != 0) { pp->err = 1; return; }
    }
    if (r < 0) { pp->err = 1; return; }
    if (drain_exp(pp->e, pp->w) != 0) { pp->err = 1; return; }
}

/* ---- .sum scan: header, venue, lap count + best lap (LIST + export meta) ---- */
typedef struct {
    ses_hdr_t hdr;   bool have_hdr;
    char      venue[33];  bool have_venue;
    uint16_t  venue_id, layout_id;
    int       laps;
    uint32_t  best_ms;    bool have_best;
} sumscan_t;
static sumscan_t s_ss;

static void sum_scan_cb(uint8_t type, const uint8_t *p, uint8_t len, void *vctx)
{
    LT_ASSERT_VOID(p != NULL || len == 0, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(vctx != NULL, CMD_ASSERT_CODE);
    sumscan_t *s = (sumscan_t *)vctx;
    switch (type) {
    case SES_T_SESSION_HDR:
        if (ses_decode_hdr(p, len, &s->hdr) == 1) s->have_hdr = true;
        break;
    case SES_T_VENUE: {
        ses_venue_t v;
        if (ses_decode_venue(p, len, &v) == 1) {
            memcpy(s->venue, v.name, sizeof s->venue);
            s->venue_id = v.venue_id; s->layout_id = v.layout_id; s->have_venue = true;
        }
        break;
    }
    case SES_T_LAP: {
        lap_result_t l;
        if (ses_decode_lap(p, len, &l) == 1) {
            s->laps++;
            if ((l.flags & LAP_F_VALID) && (!s->have_best || l.time_ms < s->best_ms)) {
                s->best_ms = l.time_ms; s->have_best = true;
            }
        }
        break;
    }
    default: break;
    }
}

/* Read `/sessions/<id>.sum` through the ses reader into *out (caller zeroes it). 0 ok, -1 open. */
static int read_sum_scan(const char *id, sumscan_t *out)
{
    LT_ASSERT_RET(id != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(out != NULL, CMD_ASSERT_CODE, -1);
    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s.sum", id);
    sto_file_t f;
    if (sto_open(path, STO_RD, &f) != 0) return -1;
    ses_reader_init(&s_sr);
    size_t got;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (sto_read(f, s_io, sizeof s_io, &got) == 0 && got > 0) {
        LT_ASSERT_RET(chunks++ < CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        ses_reader_feed(&s_sr, s_io, got, sum_scan_cb, out);
    }
    ses_reader_flush(&s_sr, sum_scan_cb, out);
    (void)sto_close(f);
    return 0;
}

/* Two-pass memo (§18.4 serial framing runs cmd_dispatch twice -- a measuring pass then a printing
 * pass -- because `---BEGIN <size>---` needs the size first). To keep both passes byte-identical
 * even if the underlying file/sessions change between them, the first pass records what it produced
 * and the second reuses it: for OPEN/READ the input byte length (`limit`) so the second pass reads
 * the same range; for LIST the cached session table. Established on the first of a matching pair,
 * consumed on the second. Streaming is synchronous, so the two passes always run back to back. */
enum { MEMO_FILE = 0xFF };   /* s_memo.op value for an OPEN/READ file stream */
static struct {
    bool     valid;
    uint8_t  op;         /* CMD_LIST, or MEMO_FILE for OPEN/READ */
    uint8_t  fmt;        /* OPEN fmt 0..4 (file streams) */
    char     id[11];
    uint32_t offset;
    uint32_t limit;      /* OPEN/READ: input bytes the measuring pass read */
} s_memo;

/* ---- LIST (0x02): enumerate /sessions, emit the §14.3 JSON array (may span chunks) ---- */
typedef struct {
    char     id[11];
    uint32_t log_kb, sum_kb;
    bool     has_log, has_sum;
    /* cached .sum scan (measuring pass fills it; printing pass renders from it) */
    int64_t  start_utc;
    bool     mode_drag;
    char     venue[33];
    uint16_t venue_id, layout_id;
    int      laps;
    uint32_t best_ms;
    bool     have_best;
} sess_ent_t;
enum { LIST_MAX_SESSIONS = 64 };
static sess_ent_t s_sess[LIST_MAX_SESSIONS];
static int        s_nsess;

static void list_scan_cb(const char *name, uint32_t size, void *ctx)
{
    LT_ASSERT_VOID(name != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(s_nsess >= 0 && s_nsess <= LIST_MAX_SESSIONS, CMD_ASSERT_CODE);   /* within s_sess[] */
    (void)ctx;
    const char *dot = strrchr(name, '.');
    if (!dot) return;
    bool is_log = strcmp(dot, ".log") == 0;
    bool is_sum = strcmp(dot, ".sum") == 0;
    if (!is_log && !is_sum) return;
    size_t idlen = (size_t)(dot - name);
    if (idlen == 0 || idlen > 10) return;

    int idx = -1;
    for (int i = 0; i < s_nsess; i++)
        if (strncmp(s_sess[i].id, name, idlen) == 0 && s_sess[i].id[idlen] == '\0') { idx = i; break; }
    if (idx < 0) {
        if (s_nsess >= LIST_MAX_SESSIONS) return;   /* cap: extra sessions omitted */
        idx = s_nsess++;
        memcpy(s_sess[idx].id, name, idlen);
        s_sess[idx].id[idlen] = '\0';
        s_sess[idx].log_kb = s_sess[idx].sum_kb = 0;
        s_sess[idx].has_log = s_sess[idx].has_sum = false;
    }
    if (is_log) { s_sess[idx].has_log = (size > 0); s_sess[idx].log_kb = (size + 1023) / 1024; }
    else        { s_sess[idx].has_sum = true;       s_sess[idx].sum_kb = (size + 1023) / 1024; }
}

/* Minimal JSON string escaper (§14.3 "strings escaped"): copies src into dst with ", \ and
 * control bytes escaped. dst is always NUL-terminated; output is truncated to fit cap. */
static void json_escape(char *dst, size_t cap, const char *src)
{
    LT_ASSERT_VOID(dst != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(src != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(cap > 0, CMD_ASSERT_CODE);   /* dst[w] = '\0' below would write out of bounds at cap == 0 */
    size_t w = 0;
    for (size_t i = 0; src[i] && w + 7 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[w++] = '\\'; dst[w++] = (char)c; }
        else if (c == '\n')        { dst[w++] = '\\'; dst[w++] = 'n'; }
        else if (c == '\r')        { dst[w++] = '\\'; dst[w++] = 'r'; }
        else if (c == '\t')        { dst[w++] = '\\'; dst[w++] = 't'; }
        else if (c < 0x20)         { w += (size_t)snprintf(dst + w, cap - w, "\\u%04x", c); }
        else                       { dst[w++] = (char)c; }
    }
    dst[w] = '\0';
}

static int op_list(cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    bool second = s_memo.valid && s_memo.op == CMD_LIST;   /* printing pass: reuse the table */
    s_memo.valid = false;

    if (!second) {                                         /* measuring pass: enumerate + scan once */
        s_nsess = 0;
        (void)sto_list("/sessions", list_scan_cb, NULL);
        LT_ASSERT_RET(s_nsess >= 0 && s_nsess <= LIST_MAX_SESSIONS, CMD_ASSERT_CODE, -1);   /* within s_sess[] */
        for (int i = 0; i < s_nsess; i++) {
            if (!s_sess[i].has_sum) continue;
            memset(&s_ss, 0, sizeof s_ss);
            (void)read_sum_scan(s_sess[i].id, &s_ss);
            s_sess[i].start_utc  = s_ss.have_hdr ? s_ss.hdr.start_gps_us / 1000000 : 0;
            s_sess[i].mode_drag  = s_ss.have_hdr && s_ss.hdr.mode == 1;
            memcpy(s_sess[i].venue, s_ss.have_venue ? s_ss.venue : "", sizeof s_sess[i].venue);
            if (!s_ss.have_venue) s_sess[i].venue[0] = '\0';
            s_sess[i].venue_id   = s_ss.have_venue ? s_ss.venue_id
                                                   : (s_ss.have_hdr ? s_ss.hdr.venue_id : 0);
            s_sess[i].layout_id  = s_ss.have_venue ? s_ss.layout_id
                                                   : (s_ss.have_hdr ? s_ss.hdr.layout_id : 0);
            s_sess[i].laps       = s_ss.laps;
            s_sess[i].best_ms    = s_ss.best_ms;
            s_sess[i].have_best  = s_ss.have_best;
        }
    }

    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/false);
    static const char PRE[]  = "{\"proto\":1,\"sessions\":[";
    static const char POST[] = "]}";
    if (cw_write(&s_cw, (const uint8_t *)PRE, sizeof PRE - 1) != 0) return -1;

    int emitted = 0;
    for (int i = 0; i < s_nsess; i++) {
        if (!s_sess[i].has_sum) continue;             /* need a .sum to describe the session */
        char venue_esc[200];
        json_escape(venue_esc, sizeof venue_esc, s_sess[i].venue);
        char obj[400];
        int w = snprintf(obj, sizeof obj,
            "%s{\"id\":\"%s\",\"start_utc\":%lld,\"mode\":\"%s\",\"venue\":\"%s\","
            "\"venue_id\":%u,\"layout_id\":%u,\"laps\":%d,\"best_ms\":%u,"
            "\"log_kb\":%u,\"sum_kb\":%u,\"has_log\":%s}",
            emitted ? "," : "", s_sess[i].id, (long long)s_sess[i].start_utc,
            s_sess[i].mode_drag ? "drag" : "lap", venue_esc,
            (unsigned)s_sess[i].venue_id, (unsigned)s_sess[i].layout_id, s_sess[i].laps,
            (unsigned)(s_sess[i].have_best ? s_sess[i].best_ms : 0),
            (unsigned)s_sess[i].log_kb, (unsigned)s_sess[i].sum_kb,
            s_sess[i].has_log ? "true" : "false");
        if (w > 0) {
            size_t wl = (size_t)w;
            if (wl > sizeof obj - 1) wl = sizeof obj - 1;
            if (cw_write(&s_cw, (const uint8_t *)obj, wl) != 0) return -1;
        }
        emitted++;
    }
    if (cw_write(&s_cw, (const uint8_t *)POST, sizeof POST - 1) != 0) return -1;

    int rc = cw_finish(&s_cw);
    if (!second && rc == 0) { s_memo.valid = true; s_memo.op = CMD_LIST; }   /* arm for pass 2 */
    return rc;
}

/* ---- OPEN/READ raw formats (3 log, 4 sum): stream the file bytes verbatim ---- */
/* Reads at most `limit` bytes (UINT32_MAX = unclamped); on success sets *nread to the bytes read
 * so the caller can pin the printing pass to the same range (a growing file never overruns the
 * announced size, and the CRC covers exactly [0, limit)). *nread is left untouched on error. */
static int stream_raw(const char *id, const char *ext, uint32_t offset, uint32_t limit,
                      uint32_t *nread, cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(id != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(ext != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(nread != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s%s", id, ext);
    sto_file_t f;
    if (sto_open(path, STO_RD, &f) != 0) {
        uint16_t seq = 0;
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "open: no such file");
    }
    cw_init(&s_cw, emit, ctx, tag, offset, /*tail*/true);
    uint32_t done = 0;
    size_t   got;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (done < limit && sto_read(f, s_io, sizeof s_io, &got) == 0 && got > 0) {
        LT_ASSERT_RET(chunks++ < CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        if ((uint32_t)got > limit - done) got = (size_t)(limit - done);   /* clamp to snapshot */
        if (cw_write(&s_cw, s_io, got) != 0) { (void)sto_close(f); return -1; }
        done += (uint32_t)got;
    }
    (void)sto_close(f);
    *nread = done;
    return cw_finish(&s_cw);
}

/* ---- OPEN/READ export formats (0 json, 1 vbo, 2 nmea): stream .log through core/exp ---- */
static exp_meta_t s_meta;

/* Reads at most `limit` bytes of the .log (UINT32_MAX = unclamped); on success sets *nread to the
 * input bytes fed so the printing pass feeds the identical range -> identical output + CRC. *nread
 * is left untouched on any error path. */
static int stream_export(const char *id, uint8_t expfmt, uint32_t offset, uint32_t limit,
                         uint32_t *nread, cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(id != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(nread != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    uint16_t seq = 0;

    /* Build the export header meta from the .sum (small): fw/hwid/venue/created time.
     * exp_json reads the fed HDR/VENUE frames directly; vbo/nmea take them from meta. */
    memset(&s_meta, 0, sizeof s_meta);
    (void)snprintf(s_meta.session_id, sizeof s_meta.session_id, "%s", id);
    memset(&s_ss, 0, sizeof s_ss);
    (void)read_sum_scan(id, &s_ss);
    if (s_ss.have_hdr) {
        memcpy(s_meta.fw,   s_ss.hdr.fw,   sizeof s_meta.fw);
        memcpy(s_meta.hwid, s_ss.hdr.hwid, sizeof s_meta.hwid);
        s_meta.created_gps_us = s_ss.hdr.start_gps_us;
    }
    if (s_ss.have_venue) memcpy(s_meta.venue, s_ss.venue, sizeof s_meta.venue);
    s_meta.has_sf = 0;   /* S/F coordinates come from the tracks DB (not in plan 03) */

    if (exp_open(&s_exp, expfmt, &s_meta) != 0)
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "export: open failed");

    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s.log", id);
    sto_file_t f;
    if (sto_open(path, STO_RD, &f) != 0)
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "open: no .log");

    cw_init(&s_cw, emit, ctx, tag, offset, /*tail*/true);
    exp_pump_t pump = { .e = &s_exp, .w = &s_cw, .err = 0 };
    (void)drain_exp(&s_exp, &s_cw);              /* emit the format header written at exp_open */

    ses_reader_init(&s_sr);
    uint32_t done = 0;
    size_t   got;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (done < limit && sto_read(f, s_io, sizeof s_io, &got) == 0 && got > 0) {
        LT_ASSERT_RET(chunks++ < CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        if ((uint32_t)got > limit - done) got = (size_t)(limit - done);   /* clamp to snapshot */
        ses_reader_feed(&s_sr, s_io, got, exp_frame_cb, &pump);
        done += (uint32_t)got;
        if (pump.err || s_cw.err) break;
    }
    if (!pump.err && !s_cw.err) ses_reader_flush(&s_sr, exp_frame_cb, &pump);
    (void)sto_close(f);

    if (!s_cw.err && !pump.err) {                /* finish the exporter (may need EXP_FULL retry) */
        int r;
        /* Rule 2: same generous bound as exp_frame_cb's own EXP_FULL retry loop. */
        enum { EXP_FINISH_MAX_RETRIES = 16 };
        int retries = 0;
        while ((r = exp_finish(&s_exp)) == EXP_FULL) {
            LT_ASSERT_RET(retries++ < EXP_FINISH_MAX_RETRIES, CMD_ASSERT_CODE, -1);
            if (drain_exp(&s_exp, &s_cw) != 0) { s_cw.err = -1; break; }
        }
        if (r < 0) pump.err = 1;                 /* propagate a finish error like the feed path */
        else if (!s_cw.err) (void)drain_exp(&s_exp, &s_cw);
    }
    if (s_cw.err) return -1;                      /* transport failed */
    if (pump.err)                                 /* undecodable/truncated export: signal, not fake OK */
        return emit_error(emit, ctx, tag, &s_cw.seq, E_CONN_PROTO, "export: decode error");
    *nread = done;
    return cw_finish(&s_cw);                      /* LAST + CRC32 tail over the whole export */
}

/* Kept from the last OPEN so READ knows which file/format to resume (§18.1: READ carries only
 * the offset). Streaming is synchronous, so there is never a stream literally "in progress" to
 * abort; a concurrent-request abort (E_CONN_XFER_ABORT) is a BLE/WiFi-transport concern. */
static struct { bool active; uint8_t fmt; char id[11]; } s_open;

static int stream_by_fmt(const char *id, uint8_t fmt, uint32_t offset,
                         cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(id != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    if (fmt > 4) { uint16_t seq = 0; return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "bad fmt"); }

    /* Printing pass? Reuse the measuring pass's byte length so both stream the same range. */
    bool second = s_memo.valid && s_memo.op == MEMO_FILE && s_memo.fmt == fmt &&
                  s_memo.offset == offset && strncmp(s_memo.id, id, sizeof s_memo.id) == 0;
    uint32_t limit = second ? s_memo.limit : UINT32_MAX;
    s_memo.valid = false;                       /* consume / reset before running the pass */

    uint32_t nread = UINT32_MAX;                /* stays UINT32_MAX on error -> not memoised */
    int rc;
    switch (fmt) {
    case 0: rc = stream_export(id, EXP_JSON, offset, limit, &nread, emit, ctx, tag); break;
    case 1: rc = stream_export(id, EXP_VBO,  offset, limit, &nread, emit, ctx, tag); break;
    case 2: rc = stream_export(id, EXP_NMEA, offset, limit, &nread, emit, ctx, tag); break;
    case 3: rc = stream_raw(id, ".log", offset, limit, &nread, emit, ctx, tag); break;
    default: rc = stream_raw(id, ".sum", offset, limit, &nread, emit, ctx, tag); break;
    }

    /* Measuring pass that actually streamed: memoise its length for the printing pass. */
    if (!second && rc == 0 && nread != UINT32_MAX) {
        s_memo.valid  = true;
        s_memo.op     = MEMO_FILE;
        s_memo.fmt    = fmt;
        s_memo.offset = offset;
        (void)snprintf(s_memo.id, sizeof s_memo.id, "%s", id);
        s_memo.limit  = nread;
    }
    return rc;
}

/* OPEN (0x03): id char[10], fmt u8 -> stream from offset 0. */
static int op_open(const uint8_t *payload, size_t len, cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    uint16_t seq = 0;
    if (len < 11) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "open: short payload");
    char id[11];
    memcpy(id, payload, 10);
    id[10] = '\0';
    id[strcspn(id, " ")] = '\0';                  /* trim any padding/trailing space */
    uint8_t fmt = payload[10];
    if (fmt > 4) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "open: bad fmt");

    s_open.active = true;
    s_open.fmt    = fmt;
    (void)snprintf(s_open.id, sizeof s_open.id, "%s", id);
    return stream_by_fmt(id, fmt, 0, emit, ctx, tag);
}

/* READ (0x04): offset u32 -> resume the open stream from that byte offset. */
static int op_read(const uint8_t *payload, size_t len, cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    uint16_t seq = 0;
    if (!s_open.active) return emit_error(emit, ctx, tag, &seq, E_CONN_XFER_ABORT, "read: no open stream");
    uint32_t offset = 0;
    if (len >= 4)
        offset = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                 ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    return stream_by_fmt(s_open.id, s_open.fmt, offset, emit, ctx, tag);
}

/* CLOSE (0x05): drop the open-stream state, then ack (empty LAST). */
static int op_close(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    s_open.active = false;
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* ------------------------------------------------------------------ */
int cmd_dispatch(uint8_t op, uint8_t tag, const uint8_t *payload, size_t len,
                 cmd_emit_fn emit, void *ctx)
{
    uint16_t seq = 0;
    /* A NULL emit fn is a caller/transport bug, not a client protocol error (contrast the
     * unknown-op/malformed-payload cases below, which the client can legitimately trigger and
     * which stay plain error returns): report it as a genuine anomaly. */
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);

    switch (op) {
    case CMD_STATUS:       return op_status(emit, ctx, tag, &seq);
    case CMD_CONFIG_GET:   return op_config_get(emit, ctx, tag, &seq);
    case CMD_CONFIG_SET:   return op_config_set(payload, len, emit, ctx, tag, &seq);
    case CMD_ERRLOG_GET:   return op_errlog_get(emit, ctx, tag, &seq);
    case CMD_ERRLOG_CLEAR: return op_errlog_clear(emit, ctx, tag, &seq);
    case CMD_DIAG_GET:     return op_diag_get(emit, ctx, tag, &seq);
    case CMD_DELETE:       return op_delete(payload, len, emit, ctx, tag, &seq);
    case CMD_CLOSE:        return op_close(emit, ctx, tag, &seq);

    case CMD_LIST:         return op_list(emit, ctx, tag);
    case CMD_OPEN:         return op_open(payload, len, emit, ctx, tag);
    case CMD_READ:         return op_read(payload, len, emit, ctx, tag);
    default:
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "unknown op");
    }
}
