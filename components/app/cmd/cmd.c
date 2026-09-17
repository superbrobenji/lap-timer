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

#include "app/lt_err.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"

#include "core/cfg.h"
#include "hal/storage.h"

#include "esp_system.h"            /* esp_get_minimum_free_heap_size */
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 *  static assembly state (one request in flight, §18.1)
 * ------------------------------------------------------------------ */
static char           s_json[3072];   /* JSON / text response assembly (cfg, errlog, diag) */
static cfg_t          s_cfg;          /* working config for CONFIG_GET/SET */
static lt_err_entry_t s_err[32];      /* error-ring snapshot (ERR_RING_LEN, §15.2) */

/* ------------------------------------------------------------------ *
 *  chunked emit helpers (§18.1)
 * ------------------------------------------------------------------ */
static void put_u16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Emit `data` split into <= CMD_CHUNK_MAX chunks; LAST is set on the final chunk when `last`.
 * A zero-length body still emits one (possibly LAST) empty chunk (the CLOSE/ack case). */
static int emit_bytes(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq,
                      const uint8_t *data, size_t len, bool last)
{
    if (len == 0)
        return emit(ctx, tag, (*seq)++, last ? CMD_FLAG_LAST : 0, NULL, 0);

    size_t off = 0;
    while (off < len) {
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
    (void)size;
    const char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".sum") == 0) (*(int *)ctx)++;
}
static uint16_t session_count(void)
{
    int c = 0;
    sto_list("/sessions", count_sum_cb, &c);
    return (c > 0xFFFF) ? 0xFFFF : (uint16_t)c;
}

/* fw char[7]: the git version trimmed of a leading 'v', truncated to fit 7 bytes incl. NUL. */
static void fw_short(char *dst, size_t cap)
{
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
    load_cfg();
    int n = cfg_to_json(&s_cfg, s_json, sizeof s_json);
    if (n < 0) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config encode failed");
    return emit_bytes(emit, ctx, tag, seq, (const uint8_t *)s_json, (size_t)n, true);
}

/* CONFIG_SET (0x11) -> merge JSON, clamp, persist; ack, or ERROR with the parse message. */
static int op_config_set(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
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
    lt_errlog_clear();
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* DIAG_GET (0x16) -> §17.10 diagnostics as JSON: fw, hwid, uptime, counters, storage, heap,
 * sys_flags, and the last 5 error codes. */
static int op_diag_get(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
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
    char id[11];
    size_t idl = (len < 10) ? len : 10;
    memcpy(id, payload, idl);
    id[idl] = '\0';
    id[strcspn(id, " ")] = '\0';   /* drop any padding/trailing space */

    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s.log", id);
    (void)sto_unlink(path);
    (void)snprintf(path, sizeof path, "/sessions/%s.sum", id);
    (void)sto_unlink(path);
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}

/* ------------------------------------------------------------------ */
int cmd_dispatch(uint8_t op, uint8_t tag, const uint8_t *payload, size_t len,
                 cmd_emit_fn emit, void *ctx)
{
    uint16_t seq = 0;
    if (!emit) return -1;

    switch (op) {
    case CMD_STATUS:       return op_status(emit, ctx, tag, &seq);
    case CMD_CONFIG_GET:   return op_config_get(emit, ctx, tag, &seq);
    case CMD_CONFIG_SET:   return op_config_set(payload, len, emit, ctx, tag, &seq);
    case CMD_ERRLOG_GET:   return op_errlog_get(emit, ctx, tag, &seq);
    case CMD_ERRLOG_CLEAR: return op_errlog_clear(emit, ctx, tag, &seq);
    case CMD_DIAG_GET:     return op_diag_get(emit, ctx, tag, &seq);
    case CMD_DELETE:       return op_delete(payload, len, emit, ctx, tag, &seq);
    case CMD_CLOSE:        return emit_bytes(emit, ctx, tag, &seq, NULL, 0, true);   /* ack */

    case CMD_LIST:
    case CMD_OPEN:
    case CMD_READ:
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "file ops land in task 5");
    default:
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "unknown op");
    }
}
