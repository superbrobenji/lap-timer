/* cmd.c -- transport-agnostic command dispatch (spec §18.1).
 *
 * Implements every op of §18.1 except TIME_SYNC (0x30): STATUS, LIST, OPEN, READ, CLOSE, DELETE,
 * CONFIG_GET/SET, TRACKS_GET/PUT, ERRLOG_GET/CLEAR, DIAG_GET, COREDUMP_GET/CLEAR and the four
 * OTA_* ops. TIME_SYNC needs a provisional-clock consumer and is filed against Plan 8 (design
 * 2026-10-13 R5). (The previous text claimed LIST/OPEN/READ "are Task 5 and return an E_CONN_PROTO
 * ERROR chunk for now" and that "the TRACKS, COREDUMP, OTA and TIME_SYNC ops are later sessions" --
 * false for all but TIME_SYNC since 3.5/5.4.)
 *
 * Output is streamed through the caller's emit callback as data chunks (<= 496 B payload). Text
 * ops build a JSON document in a static buffer, then emit it split across chunks. STATUS builds
 * the 20-byte §18.2 status. No malloc: all assembly buffers are static (one request at a time).
 */
#include "app/cmd.h"

#include "build_config.h"          /* CFG_FW_VERSION, CFG_HWID */

#include "app/lt_assert.h"
#include "app/lt_err.h"
#include "app/lt_ipc.h"           /* log_request_t/LOGGER_DELETE_SESSION/logger_request_sync -- DELETE (#73) */
#include "app/lt_nvs.h"
#include "app/lt_proto.h"         /* lt_session_id_ok -- validated before any path is built (#60) */
#include "app/lt_sup.h"
#include "app/sup_policy.h"       /* OTA_WHY_* (ota_precond_msg) */
#include "app/ota.h"              /* OTA receive-side state machine (CMD_OTA_*, §19.4) */
#include "app/status.h"           /* status_build() -- storage-free (Plan 5.6 T1 fix 1) */

#include "core/cfg.h"
#include "core/event.h"           /* EV_CFG_CHANGED (#87, cfg_change_notify below) */
#include "core/exp.h"              /* streaming exporter (vbo/nmea/json) */
#include "core/ses.h"             /* .log/.sum frame reader + record codecs */
#include "core/trk.h"             /* TRACKS_GET: trk_to_json, the bundled + user tables */
#include "hal/storage.h"

#include "esp_core_dump.h"        /* COREDUMP_GET/CLEAR: esp_core_dump_image_get/erase */
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"          /* esp_rom_crc32_le -- matches lt_rtc's on-device CRC */
#include "esp_system.h"            /* esp_get_minimum_free_heap_size */
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "cmd";

#define CMD_ASSERT_CODE 0x0B40   /* Power of 10 rule 5 (app/lt_assert.h); cmd.c's own code */

/* Rule 2: a fixed, generous static bound for the file-read/streaming loops below. The `storage`
 * partition (partitions.csv) is 0x150000 B (~1.34 MiB); at 256 B per sto_read() chunk (sizeof
 * s_io) that is <= 5376 chunks for the ENTIRE filesystem, so no single file's read loop can ever
 * take more than that many iterations. 8192 is a generous round number above that ceiling. */
enum { CMD_STREAM_MAX_CHUNKS = 8192 };

/* ------------------------------------------------------------------ *
 *  static assembly state (one request in flight, §18.1)
 * ------------------------------------------------------------------ */
typedef struct {
    char     id[11];
    uint16_t log_kb, sum_kb;   /* file size in KB; storage partition ~1.34 MiB -> max 1375 KB, fits uint16_t */
    bool     has_log, has_sum;
    /* cached .sum scan (measuring pass fills it; printing pass renders from it) */
    uint32_t start_utc;        /* UTC epoch seconds (gps_us/1e6, non-negative); fits uint32_t until 2106 */
    bool     mode_drag;
    char     venue[33];
    uint16_t venue_id, layout_id;
    int16_t  laps;             /* laps in one session; far within int16_t */
    uint32_t best_ms;
    bool     have_best;
} sess_ent_t;
enum { LIST_MAX_SESSIONS = 64 };
/* §18.1 is explicit that exactly ONE request is in flight, and cmd_dispatch is not reentrant (the
 * console task is its only caller). The two largest assembly buffers in this file overlay each
 * other: `json` serves CONFIG_GET/SET, ERRLOG_GET, DIAG_GET and TRACKS_GET/PUT and is rebuilt from
 * scratch by every one of them; `sess` serves LIST alone and is live ONLY between LIST's measuring
 * and printing passes (op_list's printing pass reuses the table through s_memo). cmd_dispatch
 * therefore drops a LIST memo whenever any other op arrives, so a json-using op between the two
 * passes forces LIST to re-measure instead of printing a clobbered table. s_memo and s_open are
 * deliberately outside this union (they must survive between passes), and stream_export/stream_raw
 * re-run exp_open/ses_reader_init from s_open's id and fmt, so an OPEN -> LIST -> READ sequence
 * regenerates everything it needs. The overlay reclaims 3072 B of static DRAM, which pays for
 * LAP_ACC_CAP's rise to the 100 LAP frames §12.5 promises (logger.c). */
static union {
    char       json[CMD_JSON_CAP];        /* 3072 (app/cmd.h, exported for the transport's own assert) */
    sess_ent_t sess[LIST_MAX_SESSIONS];   /* 64 x ~72 B */
} s_asm;
static cfg_t          s_cfg;          /* working config for CONFIG_GET/SET */
/* No error-ring snapshot array here: ERRLOG_GET/DIAG_GET stream entries via lt_errlog_count()/
 * lt_errlog_at() straight into s_asm.json, so the 512 B second copy of the ring is gone (§15.2). */

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
        steps++;
        LT_ASSERT_RET(steps <= EMIT_MAX_STEPS, CMD_ASSERT_CODE, -1);
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

/* Load the current config into s_cfg: defaults, then overwrite with the stored blob if valid. */
static void load_cfg(void)
{
    cfg_defaults(&s_cfg);
    (void)lt_cfg_load(&s_cfg);   /* < 0 => no valid blob; keep defaults */
}

/* ------------------------------------------------------------------ *
 *  ops
 * ------------------------------------------------------------------ */

/* STATUS (0x01) -> the 20-byte §18.2 status record (little-endian). status_build() (app/status.h,
 * Plan 5.6 T1 fix 1) is storage-free and shared with the pipeline's 1 Hz stream push, so the two
 * can never drift. */
static int op_status(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    uint8_t st[LT_STATUS_LEN];
    status_build(st);
    return emit_bytes(emit, ctx, tag, seq, st, sizeof st, true);
}

/* CONFIG_GET (0x10) -> cfg_to_json of the loaded config. */
static int op_config_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    load_cfg();
    int n = cfg_to_json(&s_cfg, s_asm.json, sizeof s_asm.json);
    if (n < 0) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config encode failed");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_asm.json, (size_t)n, true);
}

/* #87: after a persisted CONFIG_SET, tell the pipeline (CMD_CONFIG_RELOAD) and the ui
 * (EV_CFG_CHANGED, posted directly -- never emit_event(), so it is not logged or streamed).
 * Non-blocking: a full queue is reported, not fatal (the next reload or reboot catches up). */
static void cfg_change_notify(void)
{
    LT_ASSERT_VOID(g_cmd_q != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(g_ui_evt_q != NULL, CMD_ASSERT_CODE);
    command_t c = { .type = CMD_CONFIG_RELOAD };
    if (xQueueSend(g_cmd_q, &c, 0) != pdTRUE) ESP_LOGW(TAG, "config reload: cmd queue full");
    event_t ev = { .type = EV_CFG_CHANGED, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "config changed: ui queue full");
}

/* CONFIG_SET (0x11) -> merge JSON, clamp, persist; ack, or ERROR with the parse message. */
static int op_config_set(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    /* R10 (app:M11): load -> merge -> validate -> save must be atomic against ui.c's four menu
     * writers, or a menu edit landing between this load and this save is silently reverted (and
     * vice versa). On a lock timeout the write is SKIPPED and reported -- never forced. */
    if (!lt_cfg_rmw_begin())
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config busy; retry");
    load_cfg();
    cfg_t before = s_cfg;                                  /* §15.2 "config ... only on change" */
    char err[96];
    err[0] = '\0';
    if (cfg_from_json(&s_cfg, (const char *)payload, len, err, sizeof err) != 0) {
        lt_cfg_rmw_end();
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, err[0] ? err : "config json error");
    }
    (void)cfg_validate(&s_cfg);                            /* clamps in place; returns corrections */
    /* memcmp is exact: load_cfg() always starts from cfg_defaults(), so neither copy carries
     * uninitialised padding. */
    bool changed = memcmp(&before, &s_cfg, sizeof s_cfg) != 0;
    int  rc = changed ? lt_cfg_save(&s_cfg) : 0;
    lt_cfg_rmw_end();
    if (rc != 0) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config save failed");
    /* A no-op CONFIG_SET is still acked, and still does NOT wake the pipeline or the ui: #87's
     * notify exists to propagate a change, and firing it for an unchanged blob is what made every
     * cosmetic re-POST re-init the drag engine (bench B4-F2). */
    if (changed) cfg_change_notify();                      /* #87: pipeline + ui reload */
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true); /* ack */
}

/* ERRLOG_GET (0x14) -> JSON array of the error ring, oldest first. */
static int op_errlog_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    int n = lt_errlog_count();
    int w = 0;
    w += snprintf(s_asm.json + w, sizeof s_asm.json - (size_t)w, "[");
    for (int i = 0; i < n && w > 0 && (size_t)w < sizeof s_asm.json; i++) {
        lt_err_entry_t e;
        if (lt_errlog_at(i, &e) != 0) break;   /* ring shrank under a concurrent add: stop cleanly */
        w += snprintf(s_asm.json + w, sizeof s_asm.json - (size_t)w,
                      "%s{\"code\":%u,\"arg\":%u,\"uptime_s\":%u,\"boot\":%u}",
                      i ? "," : "", (unsigned)e.code, (unsigned)e.arg,
                      (unsigned)e.uptime_s, (unsigned)e.boot);
    }
    if (w > 0 && (size_t)w < sizeof s_asm.json)
        w += snprintf(s_asm.json + w, sizeof s_asm.json - (size_t)w, "]");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_asm.json, strlen(s_asm.json), true);
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
    /* Live storage query, not the status.h cache: DIAG_GET runs on the console/cmd task, which
     * hal/storage.h's contract allows for read-only queries (the same task LIST/OPEN/READ already
     * call sto_* from below), so a fresh sto_info() here is not the pipeline-task violation the
     * status_build() cache exists to avoid (Plan 5.6 T1 fix 1). */
    sto_info_t si;
    uint32_t free_kb = (sto_info(&si) == 0) ? si.free_kb : 0;
    int n = lt_errlog_count();
    int last = (n > 5) ? 5 : n;                            /* last (newest) 5 codes */

    int w = snprintf(s_asm.json, sizeof s_asm.json,
        "{\"fw\":\"%s\",\"hwid\":\"%s\",\"uptime_s\":%u,"
        "\"boots\":%u,\"crashes\":%u,\"wdt\":%u,\"gps_reset\":%u,\"i2c_recover\":%u,"
        "\"storage_free_kb\":%u,\"heap_min_free\":%u,\"sys_flags\":%u,\"last_errors\":[",
        CFG_FW_VERSION, CFG_HWID, (unsigned)up,
        (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt,
        (unsigned)c->gps_reset, (unsigned)c->i2c_recover,
        (unsigned)free_kb, (unsigned)heapmin, (unsigned)sys_flags_get());
    for (int i = 0; i < last && w > 0 && (size_t)w < sizeof s_asm.json; i++) {
        lt_err_entry_t e;
        if (lt_errlog_at(n - last + i, &e) != 0) break;    /* ring shrank under a concurrent add */
        w += snprintf(s_asm.json + w, sizeof s_asm.json - (size_t)w, "%s%u",
                      i ? "," : "", (unsigned)e.code);
    }
    if (w > 0 && (size_t)w < sizeof s_asm.json)
        w += snprintf(s_asm.json + w, sizeof s_asm.json - (size_t)w, "]}");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_asm.json, strlen(s_asm.json), true);
}

/* DELETE (0x06) -> ask the logger (the storage owner) to unlink <id>.log/<id>.sum; the ack itself
 * is unchanged (empty LAST chunk), but the ERROR chunk it can return instead is now mapped from
 * the logger's rc (debt sweep A #73), with three new error texts: "session is open; close it
 * first" (rc -4, the logger's own authority now -- this op no longer races it with its own
 * logger_open_session_id() check), "no such session" (rc -3) and "delete timed out" (rc -2, new:
 * a bounded wait can now time out where the old fire-and-forget unlink never did) plus a generic
 * "delete failed" for any other error. Request payload is id char[10]. This runs on the console
 * task while it holds the export_serial UART mutex for the whole framed reply; the wait below is
 * bounded by logger_request_sync's own timeout (1000 ms), not an unbounded block, and every
 * storage mutation now runs on the logger task alone, so the eviction listing's iterator is never
 * crossed by this (or any other) foreign unlink. */
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
    if (!lt_session_id_ok(id, strlen(id)))
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "bad id");
    log_request_t req = { .type = LOGGER_DELETE_SESSION };
    (void)snprintf(req.id, sizeof req.id, "%s", id);
    int rc = logger_request_sync(&req, 1000);
    if (rc == -4) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "session is open; close it first");
    if (rc == -3) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "no such session");
    if (rc == -2) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "delete timed out");
    if (rc != 0)  return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "delete failed");
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
        steps++;
        LT_ASSERT_RET(steps <= CW_MAX_STEPS, CMD_ASSERT_CODE, -1);
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
        steps++;
        LT_ASSERT_RET(steps <= DRAIN_EXP_MAX_STEPS, CMD_ASSERT_CODE, -1);
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
        retries++;
        LT_ASSERT_VOID(retries <= EXP_FEED_MAX_RETRIES, CMD_ASSERT_CODE);
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
    uint8_t ft, fl; const uint8_t *fp;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (sto_read(f, s_io, sizeof s_io, &got) == 0 && got > 0) {
        chunks++;
        LT_ASSERT_RET(chunks <= CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        ses_reader_push(&s_sr, s_io, got);
        while (ses_reader_next(&s_sr, &ft, &fp, &fl) == 1) sum_scan_cb(ft, fp, fl, out);
    }
    ses_reader_finish(&s_sr);
    while (ses_reader_next(&s_sr, &ft, &fp, &fl) == 1) sum_scan_cb(ft, fp, fl, out);
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
static int        s_nsess;

/* Dedup/merge one /sessions dirent (name, size) into s_asm.sess[] by session-id prefix (the part of
 * name before ".log"/".sum"). Called once per entry from op_list's measuring-pass loop below. */
static void scan_one_entry(const char *name, uint32_t size)
{
    LT_ASSERT_VOID(name != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(s_nsess >= 0 && s_nsess <= LIST_MAX_SESSIONS, CMD_ASSERT_CODE);   /* within s_asm.sess[] */
    const char *dot = strrchr(name, '.');
    if (!dot) return;
    bool is_log = strcmp(dot, ".log") == 0;
    bool is_sum = strcmp(dot, ".sum") == 0;
    if (!is_log && !is_sum) return;
    size_t idlen = (size_t)(dot - name);
    if (idlen == 0 || idlen > 10) return;

    int idx = -1;
    for (int i = 0; i < s_nsess; i++)
        if (strncmp(s_asm.sess[i].id, name, idlen) == 0 && s_asm.sess[i].id[idlen] == '\0') { idx = i; break; }
    if (idx < 0) {
        if (s_nsess >= LIST_MAX_SESSIONS) return;   /* cap: extra sessions omitted */
        idx = s_nsess++;
        memcpy(s_asm.sess[idx].id, name, idlen);
        s_asm.sess[idx].id[idlen] = '\0';
        s_asm.sess[idx].log_kb = s_asm.sess[idx].sum_kb = 0;
        s_asm.sess[idx].has_log = s_asm.sess[idx].has_sum = false;
    }
    if (is_log) { s_asm.sess[idx].has_log = (size > 0); s_asm.sess[idx].log_kb = (uint16_t)((size + 1023) / 1024); }
    else        { s_asm.sess[idx].has_sum = true;       s_asm.sess[idx].sum_kb = (uint16_t)((size + 1023) / 1024); }
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
        sto_iter_t it;
        if (sto_list_open(&it, "/sessions") == 0) {
            sto_entry_t e;
            while (sto_list_next(&it, &e) == 1)
                scan_one_entry(e.name, e.size);
            sto_list_close(&it);
        }
        LT_ASSERT_RET(s_nsess >= 0 && s_nsess <= LIST_MAX_SESSIONS, CMD_ASSERT_CODE, -1);   /* within s_asm.sess[] */
        for (int i = 0; i < s_nsess; i++) {
            if (!s_asm.sess[i].has_sum) continue;
            memset(&s_ss, 0, sizeof s_ss);
            (void)read_sum_scan(s_asm.sess[i].id, &s_ss);
            s_asm.sess[i].start_utc  = (uint32_t)(s_ss.have_hdr ? s_ss.hdr.start_gps_us / 1000000 : 0);
            s_asm.sess[i].mode_drag  = s_ss.have_hdr && s_ss.hdr.mode == 1;
            memcpy(s_asm.sess[i].venue, s_ss.have_venue ? s_ss.venue : "", sizeof s_asm.sess[i].venue);
            if (!s_ss.have_venue) s_asm.sess[i].venue[0] = '\0';
            s_asm.sess[i].venue_id   = s_ss.have_venue ? s_ss.venue_id
                                                   : (s_ss.have_hdr ? s_ss.hdr.venue_id : 0);
            s_asm.sess[i].layout_id  = s_ss.have_venue ? s_ss.layout_id
                                                   : (s_ss.have_hdr ? s_ss.hdr.layout_id : 0);
            s_asm.sess[i].laps       = (int16_t)s_ss.laps;
            s_asm.sess[i].best_ms    = s_ss.best_ms;
            s_asm.sess[i].have_best  = s_ss.have_best;
        }
    }

    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/false);
    static const char PRE[]  = "{\"proto\":1,\"sessions\":[";
    static const char POST[] = "]}";
    if (cw_write(&s_cw, (const uint8_t *)PRE, sizeof PRE - 1) != 0) return -1;

    int emitted = 0;
    for (int i = 0; i < s_nsess; i++) {
        if (!s_asm.sess[i].has_sum) continue;             /* need a .sum to describe the session */
        char venue_esc[200];
        json_escape(venue_esc, sizeof venue_esc, s_asm.sess[i].venue);
        char obj[400];
        int w = snprintf(obj, sizeof obj,
            "%s{\"id\":\"%s\",\"start_utc\":%lld,\"mode\":\"%s\",\"venue\":\"%s\","
            "\"venue_id\":%u,\"layout_id\":%u,\"laps\":%d,\"best_ms\":%u,"
            "\"log_kb\":%u,\"sum_kb\":%u,\"has_log\":%s}",
            emitted ? "," : "", s_asm.sess[i].id, (long long)s_asm.sess[i].start_utc,
            s_asm.sess[i].mode_drag ? "drag" : "lap", venue_esc,
            (unsigned)s_asm.sess[i].venue_id, (unsigned)s_asm.sess[i].layout_id, s_asm.sess[i].laps,
            (unsigned)(s_asm.sess[i].have_best ? s_asm.sess[i].best_ms : 0),
            (unsigned)s_asm.sess[i].log_kb, (unsigned)s_asm.sess[i].sum_kb,
            s_asm.sess[i].has_log ? "true" : "false");
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
        chunks++;
        LT_ASSERT_RET(chunks <= CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        if ((uint32_t)got > limit - done) got = (size_t)(limit - done);   /* clamp to snapshot */
        if (cw_write(&s_cw, s_io, got) != 0) { (void)sto_close(f); return -1; }
        done += (uint32_t)got;
    }
    (void)sto_close(f);
    *nread = done;
    return cw_finish(&s_cw);
}

/* ---- TRACKS_GET/PUT (0x12/0x13) and COREDUMP_GET/CLEAR (0x17/0x18) ---- */

/* R5 (§18.1 0x12): a JSON array of every venue -- bundled first, then the user table -- each
 * object being trk_to_json()'s venue document with a "src" key spliced in front. Streamed one
 * venue at a time through s_cw, so there is no whole-table buffer: trk_to_json's output goes into
 * the SAME s_asm.json every other text op uses, so a venue whose trk_to_json document exceeds
 * CMD_JSON_CAP cannot be emitted (a maximal 8-layout x 8-sector venue is ~4.6 KB; the small
 * venues the tool and the bundled table carry fit). Such a venue is COUNTED, and the stream ends
 * in an ERR instead of a clean "]" -- the precedent finish_export() sets for skipped records --
 * never a complete-looking array with a venue silently missing. Read-only and side-effect free,
 * so export_serial's two-pass framing may run it twice.
 *
 * This makes the cmd/console task a THIRD unlocked reader of core/trk.h's module-static user
 * table, beside the pipeline and the ui. The ordering argument (the same shape as the logger's,
 * #97/PR #101 H2): the loop reads trk_user_count() first and only indices below it; the only
 * post-boot writers are the pipeline's finalize_create() -- which always allocates a fresh id via
 * trk_next_user_id(), so it only ever APPENDS at user[user_n] and then bumps user_n, never
 * touching a byte below the captured count -- and the logger's LOGGER_PUT_TRACK, which can
 * replace a same-id slot but runs only while THIS task is blocked inside logger_request_sync()
 * (op_tracks_put below, which also latches s_put_pending if its wait times out, so after a
 * timeout every s_asm user first waits the logger out via put_barrier), so it cannot overlap
 * this read. trk.h's ownership comment still names
 * two readers: F4 files the doc-debt issue (trk.h is F0's file in this wave).
 *
 * Returns 1 when a venue was written, 0 when it was skipped (the caller counts it), -1 on a
 * transport error. */
static int tracks_emit_one(const trk_venue_t *v, const char *src, bool first)
{
    LT_ASSERT_RET(v != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(src != NULL, CMD_ASSERT_CODE, -1);
    int n = trk_to_json(v, s_asm.json, sizeof s_asm.json);
    /* n <= 2 covers both an encode failure and a bare "{}" -- splicing into the latter would emit
     * {"src":"user",} and break the whole document, so skip it rather than corrupt the array. */
    if (n <= 2 || s_asm.json[0] != '{') return 0;
    if (!first && cw_write(&s_cw, (const uint8_t *)",", 1) != 0) return -1;
    char pre[24];
    int pw = snprintf(pre, sizeof pre, "{\"src\":\"%s\",", src);
    if (pw <= 0 || (size_t)pw >= sizeof pre) return -1;
    if (cw_write(&s_cw, (const uint8_t *)pre, (size_t)pw) != 0) return -1;
    /* +1 / -1: drop trk_to_json's own opening brace, keep everything from its first key on. */
    if (cw_write(&s_cw, (const uint8_t *)(s_asm.json + 1), (size_t)n - 1) != 0) return -1;
    return 1;
}

static int op_tracks_get(cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/true);
    if (cw_write(&s_cw, (const uint8_t *)"[", 1) != 0) return -1;
    bool first = true;
    unsigned skipped = 0;                                  /* venues too large for s_asm.json */
    for (uint16_t i = 0; i < trk_bundled_count; i++) {     /* rule 2: bounded by the table itself */
        int r = tracks_emit_one(&trk_bundled[i], "bundled", first);
        if (r < 0) return -1;
        if (r == 1) first = false; else skipped++;
    }
    int un = trk_user_count();
    LT_ASSERT_RET(un >= 0 && un <= TRK_MAX_USER, CMD_ASSERT_CODE, -1);   /* bounds the loop below */
    for (int i = 0; i < un; i++) {
        const trk_venue_t *v = trk_get(trk_user_id_at((uint8_t)i));
        if (v == NULL) continue;
        int r = tracks_emit_one(v, "user", first);
        if (r < 0) return -1;
        if (r == 1) first = false; else skipped++;
    }
    if (skipped != 0) {                                    /* same shape as finish_export's skipped ERR */
        char msg[56];
        (void)snprintf(msg, sizeof msg, "tracks: %u venues skipped (too large)", skipped);
        return emit_error(emit, ctx, tag, &s_cw.seq, E_CONN_PROTO, msg);
    }
    if (cw_write(&s_cw, (const uint8_t *)"]", 1) != 0) return -1;
    return cw_finish(&s_cw);
}

static size_t s_track_len;   /* bytes of s_asm.json staged for LOGGER_PUT_TRACK; 0 outside the call */
static bool   s_put_pending; /* a LOGGER_PUT_TRACK timed out: the logger may still be reading s_asm.json */

/* m3 (B6-8): after a PUT's -2 the logger may still be queued or mid-parse on s_asm.json, and
 * logger_request_sync has no "wait again" call. The logger drains its queue in order, so ANY later
 * request it answers proves the earlier PUT is finished. LOGGER_EVICT is the barrier because it
 * writes no flash itself: handle_request only sets s_last_evict_ms, so the logger's NEXT loop runs
 * eviction_check() early -- a sto_info() call and, below STO_EVICT_PCT, the normal §12.7
 * oldest-first eviction that would have run within 60 s anyway. Called at cmd_dispatch entry for
 * every op that writes s_asm; returns true when s_asm is safe to use (no PUT outstanding, or the
 * barrier answered). */
static bool put_barrier(void)
{
    LT_ASSERT_RET(s_track_len == 0, CMD_ASSERT_CODE, false);   /* never staged outside op_tracks_put */
    if (!s_put_pending) return true;
    log_request_t req = { .type = LOGGER_EVICT };
    if (logger_request_sync(&req, 1000) != 0) return false;
    s_put_pending = false;
    return true;
}

/* app/cmd.h's contract: valid for exactly the span of op_tracks_put's logger_request_sync(). */
const char *cmd_tracks_payload(size_t *len_out)
{
    if (len_out != NULL) *len_out = s_track_len;
    return s_asm.json;
}

/* R5 (§18.1 0x13): parse-and-install is forbidden here. core/trk.h's ownership comment makes the
 * user table writer-exclusive to boot, the pipeline and the logger, and trk_from_json needs a
 * 2752 B trk_venue_t landing slot that must not be a local on this 6 KB console stack (PR #101
 * M2's twin). So: stage the JSON text, hand it to the logger (LOGGER_PUT_TRACK), and block until
 * it answers (on a timeout the hand-off is NOT over: s_put_pending makes put_barrier hold every
 * later s_asm user back until the logger has demonstrably drained past this request). The logger
 * parses into the trk module's own static landing slot via trk_user_add_json() and then persists
 * the table (logger.c's put_track()). The block is what
 * makes the hand-off safe: the staged buffer cannot change while the logger reads it, and
 * ownership returns here with the rc. 2000 ms, not op_delete's 1000: put_track() writes up to
 * TRK_MAX_USER records plus a rename. */
static int op_tracks_put(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    if (len == 0 || len >= sizeof s_asm.json)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put: bad length");
    memcpy(s_asm.json, payload, len);
    s_asm.json[len] = '\0';
    s_track_len = len;
    log_request_t req = { .type = LOGGER_PUT_TRACK };
    int rc = logger_request_sync(&req, 2000);
    s_track_len = 0;                                       /* the staging window is closed */
    if (rc == -2) s_put_pending = true;                    /* the logger may still hold s_asm.json */
    if (rc == -5) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "table full");
    if (rc == -6) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "bad venue json");
    if (rc == -7) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "installed but not saved");
    if (rc == -2) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put timed out");
    if (rc != 0)  return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* R5 (§18.1 0x17, §17.10, §18.4's `coredump`). Streams the stored image out of the `coredump`
 * partition (partitions.csv: 0x2A0000, 0x10000 = the 64 KB §18.1 caps the response at) in 256 B
 * reads through the SAME s_io buffer LIST/OPEN/READ already use -- no new static DRAM. The console
 * path Base64-encodes the body between the frame markers (§18.4), exactly like `open <id> log`.
 * Read-only, so the two-pass framing may run it twice. */
static int op_coredump_get(cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    uint16_t seq = 0;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0)
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: none stored");
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (p == NULL) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: no partition");
    /* esp_core_dump_image_get reports a FLASH address; esp_partition_read takes an offset within
     * the partition. The image is written at the partition start, so rel is 0 in practice -- it is
     * derived rather than assumed so a future IDF layout change cannot read the wrong bytes. */
    size_t rel = (addr >= p->address) ? (addr - p->address) : 0u;
    if (rel >= p->size) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: bad offset");
    if (size > p->size - rel) size = p->size - rel;
    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/true);
    size_t done = 0;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (done < size) {
        chunks++;
        LT_ASSERT_RET(chunks <= CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        size_t n = size - done;
        if (n > sizeof s_io) n = sizeof s_io;
        if (esp_partition_read(p, rel + done, s_io, n) != ESP_OK) {
            /* A short body with a valid CRC would read as a complete image, and the two passes
             * could disagree on a transient failure: record it and end in an ERROR frame. */
            (void)errlog_add(E_CONN_PROTO, (uint32_t)(rel + done));
            return emit_error(emit, ctx, tag, &s_cw.seq, E_CONN_PROTO, "coredump: read failed");
        }
        if (cw_write(&s_cw, s_io, n) != 0) return -1;
        done += n;
    }
    return cw_finish(&s_cw);
}

/* R5 (§18.1 0x18): erase the stored image so the next crash has somewhere to land. */
static int op_coredump_clear(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    if (esp_core_dump_image_erase() != ESP_OK)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "coredump erase failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* ---- OPEN/READ export streaming: stream_export takes core/exp.h's EXP_* format codes (EXP_JSON,
 * EXP_VBO, EXP_NMEA); stream_by_fmt maps the wire fmt byte onto them (0 -> EXP_JSON, 1 -> EXP_VBO,
 * 2 -> EXP_NMEA, and 3/4 stream the raw .log/.sum through stream_raw) ---- */
static exp_meta_t s_meta;

/* Completion path of stream_export: finishes the exporter, then picks the terminal frame
 * (transport failure, decode error, skipped-record error, or the LAST + CRC32 tail). */
static int finish_export(uint8_t expfmt, exp_pump_t *pump, uint32_t done, uint32_t *nread,
                         cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(pump != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(nread != NULL, CMD_ASSERT_CODE, -1);
    if (!s_cw.err && !pump->err) {                /* finish the exporter (may need EXP_FULL retry) */
        int r;
        /* Rule 2: same generous bound as exp_frame_cb's own EXP_FULL retry loop. */
        enum { EXP_FINISH_MAX_RETRIES = 16 };
        int retries = 0;
        while ((r = exp_finish(&s_exp)) == EXP_FULL) {
            retries++;                                /* R20: no side effect inside the assert condition */
            LT_ASSERT_RET(retries <= EXP_FINISH_MAX_RETRIES, CMD_ASSERT_CODE, -1);
            if (drain_exp(&s_exp, &s_cw) != 0) { s_cw.err = -1; break; }
        }
        if (r < 0) pump->err = 1;                 /* propagate a finish error like the feed path */
        else if (!s_cw.err) (void)drain_exp(&s_exp, &s_cw);
    }
    if (s_cw.err) return -1;                      /* transport failed */
    if (pump->err)                                /* undecodable/truncated export: signal, not fake OK */
        return emit_error(emit, ctx, tag, &s_cw.seq, E_CONN_PROTO, "export: decode error");
    /* VBO/NMEA have no in-band skipped counter (JSON carries "skipped":N itself), so a non-zero
     * count would otherwise be a silently short file with rc 0. An ERROR frame in the measuring
     * pass prints a clean "ERR 0x0703: <msg>" line with no ---BEGIN (no half frame), the surface
     * the pre-F1b "export: decode error" had; never route it through cw (it would enter the CRC). */
    if ((expfmt == EXP_VBO || expfmt == EXP_NMEA) && s_exp.skipped != 0) {
        char msg[40];
        (void)snprintf(msg, sizeof msg, "export: %u records skipped", (unsigned)s_exp.skipped);
        return emit_error(emit, ctx, tag, &s_cw.seq, E_CONN_PROTO, msg);
    }
    *nread = done;
    return cw_finish(&s_cw);                      /* LAST + CRC32 tail over the whole export */
}

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
    uint8_t  ft, fl; const uint8_t *fp;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (done < limit && sto_read(f, s_io, sizeof s_io, &got) == 0 && got > 0) {
        chunks++;
        LT_ASSERT_RET(chunks <= CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        if ((uint32_t)got > limit - done) got = (size_t)(limit - done);   /* clamp to snapshot */
        ses_reader_push(&s_sr, s_io, got);
        while (ses_reader_next(&s_sr, &ft, &fp, &fl) == 1) exp_frame_cb(ft, fp, fl, &pump);
        done += (uint32_t)got;
        if (pump.err || s_cw.err) break;
    }
    if (!pump.err && !s_cw.err) {
        ses_reader_finish(&s_sr);
        while (ses_reader_next(&s_sr, &ft, &fp, &fl) == 1) exp_frame_cb(ft, fp, fl, &pump);
    }
    (void)sto_close(f);

    return finish_export(expfmt, &pump, done, nread, emit, ctx, tag);
}

/* Kept from the last OPEN so READ knows which file/format to resume (§18.1: READ carries only
 * the offset). §18.1's "a new request while streaming aborts the stream" is unimplemented: the
 * serial transport is synchronous (one dispatch at a time, UART mutex held), so there is never a
 * stream in progress to abort -- but every streaming buffer here is a file static, so the first
 * asynchronous transport must add the abort before it ships. Filed by F4. */
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
    if (!lt_session_id_ok(id, strlen(id)))
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "bad id");
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
    /* L7: §18.1 requires E_CONN_PROTO for a malformed payload. A short READ used to be silently
     * read as offset 0, i.e. a full restream the client never asked for. */
    if (len < 4) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "read: short payload");
    uint32_t offset = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
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

/* Newest error-ring entry (false when the ring is empty). */
static bool ota_newest(lt_err_entry_t *out)
{
    LT_ASSERT_RET(out != NULL, CMD_ASSERT_CODE, false);
    LT_ASSERT_RET(sizeof *out > 0, CMD_ASSERT_CODE, false);
    int n = lt_errlog_count();
    return n > 0 && lt_errlog_at(n - 1, out) == 0;
}

/* ota.c records the refusing OTA_WHY_* as the arg of an E_OTA_PRECOND errlog entry (S3-19) just
 * before it returns E_OTA_PRECOND, so the newest ring entry names the reason. Words are indexed by
 * OTA_WHY_* (app/sup_policy.h); an out-of-range arg reads "unknown". Returns false (msg untouched)
 * when the newest entry is not an E_OTA_PRECOND written by THIS call, so the caller keeps its plain
 * "ota" text. */
static bool ota_precond_msg(char *msg, size_t cap, bool had, const lt_err_entry_t *before)
{
    static const char *const WHY[] = { "unknown", "in flight", "size", "not still", "battery",
                                       "slot pending verify", "bad length", "session close failed" };
    enum { WHY_N = sizeof WHY / sizeof WHY[0] };
    _Static_assert(WHY_N == OTA_WHY_CLOSE + 1, "WHY[] must cover every OTA_WHY_* value");
    LT_ASSERT_RET(msg != NULL, CMD_ASSERT_CODE, false);
    LT_ASSERT_RET(cap >= 2, CMD_ASSERT_CODE, false);
    LT_ASSERT_RET(before != NULL, CMD_ASSERT_CODE, false);
    lt_err_entry_t e;
    if (!ota_newest(&e) || e.code != E_OTA_PRECOND) return false;
    /* Stale-entry guard: several E_OTA_PRECOND returns log nothing, so the newest entry only counts
     * if it is not the one that was already newest before this call (a full ring keeps the count
     * constant, hence identity rather than count). A repeat with identical code/arg/second reads as
     * stale and degrades to the plain "ota" text -- never a wrong reason. */
    if (had && e.code == before->code && e.arg == before->arg &&
        e.uptime_s == before->uptime_s && e.boot == before->boot) return false;
    const char *word = (e.arg > 0 && e.arg < WHY_N) ? WHY[e.arg] : WHY[0];
    (void)snprintf(msg, cap, "ota precondition: %s", word);
    return true;
}

/* ------------------------------------------------------------------ */
/* CMD_OTA_* (§19.4): drive the receive-side state machine, then acknowledge. An empty LAST chunk
 * is the success ack; any E_OTA_* code from the state machine is reported as an ERROR chunk. The
 * heavy lifting (esp_ota, signature/hwid/SHA/precondition) is in app/ota (weak-stubbed until 5.4). */
static int op_ota(uint8_t op, const uint8_t *payload, size_t len,
                  cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    lt_err_entry_t before;
    bool had = ota_newest(&before);
    int rc;
    switch (op) {
    case CMD_OTA_BEGIN: rc = ota_begin(payload, len); break;
    case CMD_OTA_DATA:  rc = ota_data(payload, len);  break;
    case CMD_OTA_END:   rc = ota_end();               break;
    case CMD_OTA_ABORT: rc = ota_abort();             break;
    default:            rc = E_CONN_PROTO;            break;
    }
    char msg[48];
    if (rc == E_OTA_PRECOND && ota_precond_msg(msg, sizeof msg, had, &before))
        return emit_error(emit, ctx, tag, seq, (uint16_t)rc, msg);
    if (rc != 0) return emit_error(emit, ctx, tag, seq, (uint16_t)rc, "ota");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);   /* empty LAST chunk = ack */
}

int cmd_dispatch(uint8_t op, uint8_t tag, const uint8_t *payload, size_t len,
                 cmd_emit_fn emit, void *ctx)
{
    uint16_t seq = 0;
    /* A NULL emit fn is a caller/transport bug, not a client protocol error (contrast the
     * unknown-op/malformed-payload cases below, which the client can legitimately trigger and
     * which stay plain error returns): report it as a genuine anomaly. */
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    /* s_asm.json overlays LIST's session table (see s_asm): any other op arriving between LIST's
     * measuring and printing passes may rewrite it, so the memo is dropped and LIST re-measures. */
    if (s_memo.valid && s_memo.op == CMD_LIST && op != CMD_LIST) s_memo.valid = false;
    if ((op == CMD_LIST || op == CMD_CONFIG_GET || op == CMD_ERRLOG_GET || op == CMD_DIAG_GET ||
         op == CMD_TRACKS_GET || op == CMD_TRACKS_PUT) && !put_barrier())
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "busy; retry");

    switch (op) {
    case CMD_STATUS:       return op_status(emit, ctx, tag, &seq);
    case CMD_CONFIG_GET:   return op_config_get(emit, ctx, tag, &seq);
    case CMD_CONFIG_SET:   return op_config_set(payload, len, emit, ctx, tag, &seq);
    case CMD_TRACKS_GET:     return op_tracks_get(emit, ctx, tag);
    case CMD_TRACKS_PUT:     return op_tracks_put(payload, len, emit, ctx, tag, &seq);
    case CMD_COREDUMP_GET:   return op_coredump_get(emit, ctx, tag);
    case CMD_COREDUMP_CLEAR: return op_coredump_clear(emit, ctx, tag, &seq);
    case CMD_ERRLOG_GET:   return op_errlog_get(emit, ctx, tag, &seq);
    case CMD_ERRLOG_CLEAR: return op_errlog_clear(emit, ctx, tag, &seq);
    case CMD_DIAG_GET:     return op_diag_get(emit, ctx, tag, &seq);
    case CMD_DELETE:       return op_delete(payload, len, emit, ctx, tag, &seq);
    case CMD_CLOSE:        return op_close(emit, ctx, tag, &seq);

    case CMD_LIST:         return op_list(emit, ctx, tag);
    case CMD_OPEN:         return op_open(payload, len, emit, ctx, tag);
    case CMD_READ:         return op_read(payload, len, emit, ctx, tag);
    case CMD_OTA_BEGIN:
    case CMD_OTA_DATA:
    case CMD_OTA_END:
    case CMD_OTA_ABORT:    return op_ota(op, payload, len, emit, ctx, tag, &seq);
    default:
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "unknown op");
    }
}
