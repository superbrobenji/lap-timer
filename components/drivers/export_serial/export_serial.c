/* export_serial.c -- serial fallback export console (spec §18.4).
 *
 * An IDF esp_console REPL on UART0 (dumb mode, §18.4). The §18.1 protocol ops are wired through
 * app/cmd's cmd_dispatch: each text command builds a request, and an emit callback assembles the
 * streamed response chunks, then prints them framed as
 *
 *     ---BEGIN <name> <size>---\r\n <bytes> \r\n---END <crc32 hex>---\r\n
 *
 * (CRC32 over the body, esp_rom_crc32_le init 0, matching lt_rtc's on-device CRC). An ERROR chunk
 * prints an `ERR 0x<code>: <msg>` line instead. IDF logging is dropped to ERROR for the duration
 * of a transfer and restored after (§18.4).
 *
 * `list`/`open`/`read` stream session files: LIST emits the §14.3 JSON, OPEN/READ stream a file
 * through the same framing (Base64 body for the binary `log`/`sum` formats, raw text for
 * vbo/nmea/json). Because BEGIN carries the size, these ops run cmd_dispatch twice (a measuring
 * pass then a printing pass); both are read-only so the double run is side-effect free.
 *
 * `dbg` carries the diagnostics verbs migrated from the 3.2-3.4 console (status/logtest/fs/sum/
 * logck/laps) plus `rtc` (dump the validated RTC snapshot), `crash` (force a panic) and `hang`
 * (busy-loop to trip the task WDT). `gps raw`/`imu raw`/`power`/`sim` are stubbed until their
 * drivers land.
 */
#include "export_serial.h"

#include "app/cmd.h"
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_rtc.h"
#include "app/lt_sup.h"
#include "app/link.h"        /* link_sink_serial_emit (stream transport half), link_note_cmd_activity */
#include "app/pipeline.h"
#include "hal/storage.h"

#include "core/core.h"
#include "core/event.h"
#include "core/ses.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_system.h"          /* esp_get_free_heap_size / esp_get_minimum_free_heap_size */
#include "esp_task_wdt.h"       /* dbg hang: subscribe the calling task so the task WDT fires deterministically */
#include "esp_timer.h"
#include "driver/uart.h"        /* uart_write_bytes -- raw binary TX for the peer stream (no \n->\r\n mangling) */
#include "linenoise/linenoise.h"

#define EXP_SERIAL_ASSERT_CODE 0x0C20   /* Power of 10 rule 5 (core/core.h); export_serial.c's own code */

static int s_reset_reason;
static volatile bool s_uart_ready;   /* the console UART driver is installed (set after esp_console starts) */

/* ================================================================== *
 *  Base64 (§18.4: binary formats are Base64-encoded between the markers)
 *
 * A tiny streaming encoder (no malloc): push bytes, carrying 1-2 leftover bytes across chunk
 * boundaries, then flush to pad the final quantum. `print` decides fwrite vs count-only, so the
 * same code drives both the size-measuring pass and the printing pass.
 * ================================================================== */
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct { uint8_t carry[3]; int ncarry; bool print; size_t out; } b64_t;

static void b64_reset(b64_t *s, bool print) { s->ncarry = 0; s->print = print; s->out = 0; }

static void b64_quantum(b64_t *s, const uint8_t *t, int rem)
{
    uint32_t v = (uint32_t)t[0] << 16;
    if (rem > 1) v |= (uint32_t)t[1] << 8;
    if (rem > 2) v |= (uint32_t)t[2];
    char q[4];
    q[0] = B64[(v >> 18) & 63];
    q[1] = B64[(v >> 12) & 63];
    q[2] = (rem > 1) ? B64[(v >> 6) & 63] : '=';
    q[3] = (rem > 2) ? B64[v & 63]        : '=';
    if (s->print) fwrite(q, 1, 4, stdout);
    s->out += 4;
}

static void b64_push(b64_t *s, const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (s->ncarry > 0 && s->ncarry < 3 && i < n) s->carry[s->ncarry++] = p[i++];
    if (s->ncarry == 3) { b64_quantum(s, s->carry, 3); s->ncarry = 0; }
    for (; i + 3 <= n; i += 3) b64_quantum(s, p + i, 3);
    while (i < n) s->carry[s->ncarry++] = p[i++];
}

static void b64_flush(b64_t *s)
{
    if (s->ncarry > 0) { b64_quantum(s, s->carry, s->ncarry); s->ncarry = 0; }
}

/* One-shot Base64 of a whole buffer to stdout (buffered ops); returns encoded byte count. */
static size_t b64_write_all(const uint8_t *src, size_t n)
{
    b64_t s; b64_reset(&s, true);
    b64_push(&s, src, n);
    b64_flush(&s);
    return s.out;
}

/* ================================================================== *
 *  §18.1 command ops -> framed serial output (§18.4)
 * ================================================================== */

/* Response assembly for one buffered request (one in flight, §18.1). The emit callback appends
 * chunk payloads here; the LAST chunk triggers the frame print. Right-sized (A3): the two large
 * read-only responses that used to set this (CONFIG_GET ~939 B, ERRLOG_GET up to ~2145 B) now
 * stream via run_stream(), so the only ops still assembled here are STATUS (a fixed 20 B record),
 * DIAG_GET (variable JSON, <= ~335 B worst case) and the tiny CONFIG_SET/DELETE/CLOSE acks and
 * <= ~130 B error lines. 640 B holds the DIAG_GET worst case with wide margin. */
#define SER_ASM_MAX 640
typedef struct {
    const char *name;              /* frame name for BEGIN/END */
    uint8_t     buf[SER_ASM_MAX];
    size_t      len;
    bool        error;
    bool        binary;            /* Base64-encode the body between the markers (§18.4) */
    uint16_t    err_code;
} ser_ctx_t;

static ser_ctx_t s_ser;            /* static: keeps the assembly buffer off the console stack */

static void ser_flush(ser_ctx_t *c)
{
    if (c->error) {
        printf("ERR 0x%04x: %.*s\r\n", (unsigned)c->err_code, (int)c->len, (const char *)c->buf);
        fflush(stdout);
        return;
    }
    /* CRC32 is over the raw payload (the client Base64-decodes first, then verifies). */
    uint32_t crc  = esp_rom_crc32_le(0, c->buf, c->len);
    size_t   size = c->binary ? (4 * ((c->len + 2) / 3)) : c->len;
    printf("---BEGIN %s %u---\r\n", c->name ? c->name : "data", (unsigned)size);
    if (c->len) {
        if (c->binary) (void)b64_write_all(c->buf, c->len);
        else           fwrite(c->buf, 1, c->len, stdout);
    }
    printf("\r\n---END %08x---\r\n", (unsigned)crc);
    fflush(stdout);
}

static int ser_emit(void *vctx, uint8_t tag, uint16_t seq, uint8_t flags,
                    const uint8_t *p, size_t n)
{
    (void)tag; (void)seq;
    ser_ctx_t *c = (ser_ctx_t *)vctx;

    if (flags & CMD_FLAG_ERROR) {
        c->error = true;
        c->err_code = (n >= 2) ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
        size_t mlen = (n > 2) ? n - 2 : 0;
        if (mlen > sizeof c->buf) mlen = sizeof c->buf;
        if (mlen) memcpy(c->buf, p + 2, mlen);
        c->len = mlen;
    } else if (n) {
        size_t room = sizeof c->buf - c->len;
        size_t cp = (n > room) ? room : n;   /* clamp: a truncated frame still reports its CRC */
        if (cp) memcpy(c->buf + c->len, p, cp);
        c->len += cp;
    }

    if (flags & CMD_FLAG_LAST) ser_flush(c);
    return 0;
}

/* Run one op through cmd_dispatch with logging quiesced, then framed by ser_emit. `binary` routes
 * the assembled body through Base64 at flush (§18.4: STATUS record and other binary payloads). */
static void run_cmd(uint8_t op, const char *name, const uint8_t *payload, size_t len, bool binary)
{
    link_note_cmd_activity();                     /* §18: a cmd request marks the peer present (heartbeat) */
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);        /* §18.4: quiet logs during a framed transfer */

    s_ser.name = name;
    s_ser.len = 0;
    s_ser.error = false;
    s_ser.binary = binary;
    s_ser.err_code = 0;
    (void)cmd_dispatch(op, /*tag*/1, payload, len, ser_emit, &s_ser);

    esp_log_level_set("*", saved);
}

/* ================================================================== *
 *  streamed framing for large / file ops -- list / open / read (§18.4)
 *
 * OPEN/READ can stream a whole session file, which must not be buffered (496 B chunk cap, 6 KB
 * REPL stack). So the frame is emitted incrementally: because `---BEGIN <name> <size>---` needs
 * the size first, cmd_dispatch is run twice -- a measuring pass counts the body bytes, then a
 * printing pass writes them. Both ops are read-only (no side effects), so the double run is safe.
 * cmd_dispatch appends the §18.1 CRC32 in the final chunk's tail for OPEN/READ; this layer lifts
 * that value into `---END <crc32 hex>---` (it already covers the whole file, so a resumed READ
 * reports the correct whole-file CRC). LIST carries no tail, so its CRC is computed over the body.
 * ================================================================== */
typedef struct {
    bool     print;         /* print to stdout vs. count only (measuring pass) */
    bool     binary;        /* Base64-encode the body (log/sum) */
    bool     has_tail;      /* final chunk ends with the 4-byte §18.1 CRC32 (OPEN/READ) */
    b64_t    b64;
    size_t   out;           /* body bytes produced (Base64 chars, or raw bytes) */
    uint32_t crc;           /* CRC over the body -- used only when !has_tail (LIST) */
    bool     have_tail_crc;
    uint32_t tail_crc;      /* CRC lifted from the final chunk's tail (OPEN/READ) */
    int      err;
    uint16_t err_code;
    char     errmsg[96];
} sframe_t;

static void sframe_init(sframe_t *s, bool print, bool binary, bool has_tail)
{
    memset(s, 0, sizeof *s);
    s->print = print; s->binary = binary; s->has_tail = has_tail;
    b64_reset(&s->b64, print);
}

static int sframe_emit(void *vctx, uint8_t tag, uint16_t seq, uint8_t flags,
                       const uint8_t *p, size_t n)
{
    (void)tag; (void)seq;
    CORE_ASSERT_RET(vctx != NULL, EXP_SERIAL_ASSERT_CODE, -1);
    CORE_ASSERT_RET(n == 0 || p != NULL, EXP_SERIAL_ASSERT_CODE, -1);   /* every memcpy/crc/fwrite below needs a real buffer whenever n > 0 */
    sframe_t *s = (sframe_t *)vctx;

    if (flags & CMD_FLAG_ERROR) {
        s->err = 1;
        s->err_code = (n >= 2) ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
        size_t mlen = (n > 2) ? n - 2 : 0;
        if (mlen > sizeof s->errmsg - 1) mlen = sizeof s->errmsg - 1;
        if (mlen) memcpy(s->errmsg, p + 2, mlen);
        s->errmsg[mlen] = '\0';
        return 0;
    }

    size_t bodyn = n;
    if ((flags & CMD_FLAG_LAST) && s->has_tail) {   /* final chunk: last 4 bytes are the CRC32 */
        if (n >= 4) {
            s->tail_crc = (uint32_t)p[n - 4] | ((uint32_t)p[n - 3] << 8) |
                          ((uint32_t)p[n - 2] << 16) | ((uint32_t)p[n - 1] << 24);
            s->have_tail_crc = true;
            bodyn = n - 4;
        } else {
            bodyn = 0;
        }
    }
    if (bodyn) {
        if (!s->has_tail) s->crc = esp_rom_crc32_le(s->crc, p, (uint32_t)bodyn);
        if (s->binary) b64_push(&s->b64, p, bodyn);
        else { if (s->print) fwrite(p, 1, bodyn, stdout); s->out += bodyn; }
    }
    if (flags & CMD_FLAG_LAST) {
        if (s->binary) { b64_flush(&s->b64); s->out = s->b64.out; }
    }
    return 0;
}

static void run_stream(uint8_t op, const char *name, const uint8_t *payload, size_t len,
                       bool binary, bool has_tail)
{
    CORE_ASSERT_VOID(payload != NULL || len == 0, EXP_SERIAL_ASSERT_CODE);   /* a non-empty payload needs a real buffer */
    link_note_cmd_activity();                     /* §18: a cmd request marks the peer present (heartbeat) */
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);        /* §18.4: quiet logs for the whole transfer */

    sframe_t m; sframe_init(&m, /*print*/false, binary, has_tail);
    (void)cmd_dispatch(op, /*tag*/1, payload, len, sframe_emit, &m);
    if (m.err) {
        printf("ERR 0x%04x: %s\r\n", (unsigned)m.err_code, m.errmsg);
        fflush(stdout);
        esp_log_level_set("*", saved);
        return;
    }

    printf("---BEGIN %s %u---\r\n", name ? name : "data", (unsigned)m.out);
    fflush(stdout);

    sframe_t pr; sframe_init(&pr, /*print*/true, binary, has_tail);
    (void)cmd_dispatch(op, /*tag*/1, payload, len, sframe_emit, &pr);

    if (pr.err) {
        /* The op succeeded while measuring but failed while printing (e.g. the file was deleted
         * or storage degraded between the two passes). Abort the frame with an error line rather
         * than closing a body-less "success" with a bogus ---END. */
        printf("\r\nERR 0x%04x: %s\r\n", (unsigned)pr.err_code, pr.errmsg);
        fflush(stdout);
        esp_log_level_set("*", saved);
        return;
    }

    uint32_t crc = has_tail ? (pr.have_tail_crc ? pr.tail_crc : 0) : pr.crc;
    printf("\r\n---END %08x---\r\n", (unsigned)crc);
    fflush(stdout);

    esp_log_level_set("*", saved);
    /* postcondition (checked after the log level is already restored, so a trip here changes
     * nothing further): a has_tail op that produced a body must have lifted a real tail CRC --
     * cmd_dispatch's OPEN/READ contract always appends one to the final chunk. */
    CORE_ASSERT_VOID(!has_tail || pr.have_tail_crc || pr.out == 0, EXP_SERIAL_ASSERT_CODE);
}

/* Map the `open` format word to the §18.1 fmt code, file extension and body encoding. */
static int fmt_from_str(const char *s, uint8_t *fmt, const char **ext, bool *binary)
{
    if (strcmp(s, "json") == 0) { *fmt = 0; *ext = "json"; *binary = false; return 0; }
    if (strcmp(s, "vbo")  == 0) { *fmt = 1; *ext = "vbo";  *binary = false; return 0; }
    if (strcmp(s, "nmea") == 0) { *fmt = 2; *ext = "nmea"; *binary = false; return 0; }
    if (strcmp(s, "log")  == 0) { *fmt = 3; *ext = "log";  *binary = true;  return 0; }
    if (strcmp(s, "sum")  == 0) { *fmt = 4; *ext = "sum";  *binary = true;  return 0; }
    return -1;
}

static uint8_t s_open_payload[11];   /* id char[10] | fmt u8 (§18.1 OPEN) */
static uint8_t s_read_payload[4];    /* offset u32 LE (§18.1 READ) */
static uint8_t s_last_fmt;
static char    s_last_name[24];      /* "<id>.<ext>" carried into the READ frame */

static int cmd_list_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_stream(CMD_LIST, "sessions", NULL, 0, /*binary*/false, /*has_tail*/false);
    return 0;
}

static int cmd_open_c(int argc, char **argv)
{
    if (argc < 3) { printf("usage: open <id> <vbo|nmea|json|log|sum>\n"); return 1; }
    uint8_t fmt; const char *ext; bool binary;
    if (fmt_from_str(argv[2], &fmt, &ext, &binary) != 0) {
        printf("open: bad format '%s' (want vbo|nmea|json|log|sum)\n", argv[2]);
        return 1;
    }
    memset(s_open_payload, 0, sizeof s_open_payload);
    size_t idl = strlen(argv[1]);
    if (idl > 10) idl = 10;
    memcpy(s_open_payload, argv[1], idl);
    s_open_payload[10] = fmt;
    s_last_fmt = fmt;
    (void)snprintf(s_last_name, sizeof s_last_name, "%.10s.%s", argv[1], ext);
    run_stream(CMD_OPEN, s_last_name, s_open_payload, sizeof s_open_payload, binary, /*has_tail*/true);
    return 0;
}

static int cmd_read_c(int argc, char **argv)
{
    if (argc < 2) { printf("usage: read <offset>   (resume the last open transfer)\n"); return 1; }
    unsigned long off = strtoul(argv[1], NULL, 0);
    s_read_payload[0] = (uint8_t)off;
    s_read_payload[1] = (uint8_t)(off >> 8);
    s_read_payload[2] = (uint8_t)(off >> 16);
    s_read_payload[3] = (uint8_t)(off >> 24);
    bool binary = (s_last_fmt == 3 || s_last_fmt == 4);
    run_stream(CMD_READ, s_last_name[0] ? s_last_name : "data",
               s_read_payload, sizeof s_read_payload, binary, /*has_tail*/true);
    return 0;
}

/* ---- text commands (§18.4) ----
 * run_cmd (single dispatch, buffered) vs run_stream (two-pass, unbuffered): STATUS and DIAG_GET
 * stay on run_cmd. STATUS is tiny; DIAG_GET embeds live uptime_s/heap values whose decimal WIDTH
 * can change between the measuring and printing passes, which would make the BEGIN size disagree
 * with the printed body -- so it must be framed from a single captured snapshot. CONFIG_GET and
 * ERRLOG_GET are large but their content is stable across the two passes (config / log-time ring
 * fields), matching LIST's already-accepted two-pass read, so they stream and no longer size the
 * assembly buffer. */
static int cmd_status_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_STATUS, "status", NULL, 0, true);
    return 0;
}

/* `config set <json>` argv reassembly. Right-sized (A3): the reassembly is built from one console
 * input line, and export_serial_start() sets repl_cfg.max_cmdline_length = 256 (asserted below to
 * be < sizeof s_setbuf), so 320 holds any real line with 64 B of margin. */
static char s_setbuf[320];
static int cmd_config_c(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "get") == 0) { run_stream(CMD_CONFIG_GET, "config", NULL, 0, /*binary*/false, /*has_tail*/false); return 0; }
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        /* rejoin argv[2..] so a JSON body split on spaces is reassembled (quotes must be escaped
         * on the command line, e.g. config set {\"units\":1} -- esp_console strips bare quotes). */
        size_t w = 0;
        for (int i = 2; i < argc && w + 1 < sizeof s_setbuf; i++) {
            if (i > 2) s_setbuf[w++] = ' ';
            size_t al = strlen(argv[i]);
            if (w + al >= sizeof s_setbuf) al = sizeof s_setbuf - 1 - w;
            memcpy(s_setbuf + w, argv[i], al);
            w += al;
        }
        s_setbuf[w] = '\0';
        run_cmd(CMD_CONFIG_SET, "config", (const uint8_t *)s_setbuf, w, false);
        return 0;
    }
    printf("usage: config get | config set <json>\n");
    return 1;
}

static int cmd_errlog_c(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) { run_cmd(CMD_ERRLOG_CLEAR, "errlog", NULL, 0, false); return 0; }
    run_stream(CMD_ERRLOG_GET, "errlog", NULL, 0, /*binary*/false, /*has_tail*/false);
    return 0;
}

static int cmd_diag_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_DIAG_GET, "diag", NULL, 0, false);
    return 0;
}

static int cmd_delete_c(int argc, char **argv)
{
    if (argc < 2) { printf("usage: delete <id>\n"); return 1; }
    run_cmd(CMD_DELETE, "delete", (const uint8_t *)argv[1], strlen(argv[1]), false);
    return 0;
}

static int cmd_close_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_CLOSE, "close", NULL, 0, false);
    return 0;
}

/* ================================================================== *
 *  dbg -- diagnostics verbs (migrated from the 3.2-3.4 console)
 * ================================================================== */

/* ---- dbg status (from 3.2) ---- */
static int dbg_status(void)
{
    const lt_counters_t *c = lt_counters();
    long long up = esp_timer_get_time() / 1000000;
    printf("boot count : %u\n", (unsigned)lt_nvs_boot_get());
    printf("reset      : %s (%d)\n", lt_reset_reason_str(s_reset_reason), s_reset_reason);
    printf("uptime     : %lld s\n", up);
    printf("counters   : boots=%u crashes=%u wdt=%u brownout=%u sto_format=%u\n",
           (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt, (unsigned)c->brownout,
           (unsigned)c->sto_format);
    printf("sys_flags  : 0x%08x%s\n", (unsigned)sys_flags_get(),
           (sys_flags_get() & (1u << SYS_SAFE_MODE)) ? " [SAFE_MODE]" : "");
    for (int i = 0; i < HB_COUNT; i++) printf("hb[%d]      : %u\n", i, (unsigned)g_hb[i]);
    return 0;
}

/* ---- dbg logtest [n] (from 3.3) ---- */
static void synth_fix(gps_fix_t *f, uint32_t i)
{
    memset(f, 0, sizeof *f);
    f->gps_us     = (int64_t)1700000000000000LL + (int64_t)i * 200000;   /* 5 Hz */
    f->mono_us    = (int64_t)esp_timer_get_time();
    f->lat_e7     = -338900000 + (int32_t)(i % 2000);                     /* ~ -33.89 deg, wandering */
    f->lon_e7     =  184000000 + (int32_t)(i % 2000);                     /* ~ 18.40 deg */
    f->alt_mm     = 45000 + (int32_t)(i % 50);
    f->gspeed_mms = 20000 + (int32_t)(i % 1000);
    f->head_e5    = (int32_t)((i * 137) % 36000000);
    f->hacc_mm    = 2000;
    f->sacc_mms   = 300;
    f->pdop_e2    = 120;
    f->fix_type   = 3;
    f->sats       = 10;
    f->flags      = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f->valid      = 1;
}

static int dbg_logtest(int argc, char **argv)
{
    CORE_ASSERT_RET(argv != NULL, EXP_SERIAL_ASSERT_CODE, 1);
    long n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 1000;
    if (n < 1) n = 1;
    if (n > 100000) n = 100000;

    log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = 1, .venue_id = 1, .layout_id = 1,
                          .gps_us = (int64_t)1700000000000000LL };
    CORE_ASSERT_RET(g_log_req_q != NULL, EXP_SERIAL_ASSERT_CODE, 1);   /* xQueueSend needs a real queue handle */
    xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100));
    logger_notify();
    vTaskDelay(pdMS_TO_TICKS(30));      /* let the logger open + write the HDR + initial .sum */

    logger_set_venue(1, 1, "logtest");
    for (uint16_t lap = 1; lap <= 2; lap++) {
        lap_result_t lr;
        memset(&lr, 0, sizeof lr);
        lr.lap_no       = lap;
        lr.time_ms      = 92000u + lap * 137u;
        lr.flags        = LAP_F_VALID;
        lr.start_gps_us = req.gps_us + (int64_t)lap * 92000000;
        lr.n_sectors    = 3;
        lr.sector_ms[0] = 30000u; lr.sector_ms[1] = 31000u; lr.sector_ms[2] = 31000u + lap * 137u;
        lr.stats.max_speed_cms = 5000; lr.stats.min_speed_cms = 1200;
        lr.stats.max_lean_r_cdeg = 3500; lr.stats.max_gacc_e3 = 900;
        logger_submit_lap(&lr);
    }
    logger_notify();

    long pushed = 0;
    for (long i = 0; i < n; i++) {
        gps_fix_t f;
        synth_fix(&f, (uint32_t)i);
        int spins = 0;
        bool ok = true;
        /* rule 2: bounded retry -- FIX_RING_CAP (32) fixes, drained continuously by the logger
         * task woken via logger_notify() each spin, so 1000 spins (>>30x the ring depth) is far
         * more yielding than the logger could ever need to catch up under any normal load. On
         * exhaustion this fix is dropped (logged) rather than spinning forever. */
        while (!ring_push(&g_fix_ring, &f)) {   /* full: wake the logger and yield */
            logger_notify();
            vTaskDelay(1);
            if (++spins > 1000) {
                printf("logtest: fix %ld dropped (ring stayed full for %d spins)\n", i, spins);
                ok = false;
                break;
            }
        }
        if (ok) pushed++;
        if ((i & 0x1F) == 0x1F) logger_notify();
    }
    logger_notify();
    printf("logtest: opened a session, queued 2 laps, pushed %ld/%ld fixes; NOT closed.\n", pushed, n);
    printf("  -> watch for \"session Sxxxxx_yyy open\", then pull power to test the cut.\n");
    printf("  -> after reboot: dbg fs ; dbg sum <id> ; dbg logck <id>\n");
    return 0;
}

/* ---- dbg fs (from 3.3) ---- */
/* Streams and prints every /sessions entry via the O(1)-RAM sto_list iterator. */
static void print_sessions(void)
{
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") != 0) { printf("  (cannot list)\n"); return; }
    sto_entry_t ent;
    int n = 0;
    while (sto_list_next(&it, &ent) == 1) {
        printf("  %-24s %8u B\n", ent.name, (unsigned)ent.size);
        n++;
    }
    sto_list_close(&it);
    if (n == 0) printf("  (empty)\n");
}

static int dbg_fs(void)
{
    uint32_t sf = sys_flags_get();
    printf("storage flags: %s%s%s\n",
           (sf & (1u << SYS_STORAGE_DEAD))     ? "DEAD " : "",
           (sf & (1u << SYS_STORAGE_FULL))     ? "FULL " : "",
           (sf & (1u << SYS_STORAGE_DEGRADED)) ? "DEGRADED " : "");
    sto_info_t si;
    if (sto_info(&si) == 0)
        printf("mount ok    : total=%u KB free=%u KB degraded=%u\n",
               (unsigned)si.total_kb, (unsigned)si.free_kb, (unsigned)si.degraded);
    else
        printf("mount       : NOT mounted (sto_info failed)\n");
    printf("fix_ring drop: %u   fused_ring drop: %u\n",
           (unsigned)ring_dropped(&g_fix_ring), (unsigned)ring_dropped(&g_fused_ring));
    printf("/sessions:\n");
    print_sessions();
    return 0;
}

/* ---- ses reader tally (shared by dbg sum + dbg logck, from 3.3) ---- */
typedef struct {
    int hdr, venue, end;
    int laps, drags, fixes, fused, events, sectors, gates, others;
    ses_hdr_t   h;
    ses_venue_t v;
} tally_t;

static void tally_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    tally_t *t = (tally_t *)ctx;
    switch (type) {
    case SES_T_SESSION_HDR: if (ses_decode_hdr(payload, len, &t->h) == 1) t->hdr++; break;
    case SES_T_VENUE:       if (ses_decode_venue(payload, len, &t->v) == 1) t->venue++; break;
    case SES_T_LAP:         t->laps++; break;
    case SES_T_DRAG_RUN:    t->drags++; break;
    case SES_T_FIX_KEY:     /* fallthrough */
    case SES_T_FIX_DELTA:   t->fixes++; break;
    case SES_T_FUSED:       t->fused++; break;
    case SES_T_EVENT:       t->events++; break;
    case SES_T_SECTOR:      t->sectors++; break;
    case SES_T_DRAG_GATE:   t->gates++; break;
    case SES_T_END:         t->end++; break;
    default:                t->others++; break;
    }
}

static ses_reader_t s_rdr;   /* static: the reader struct is ~0.8 KB, kept off the console stack */

static int read_through_ses(const char *id, const char *ext, tally_t *out)
{
    char path[40];
    (void)snprintf(path, sizeof path, "/sessions/%s%s", id, ext);
    sto_file_t f;
    if (sto_open(path, STO_RD, &f) != 0) { printf("cannot open %s\n", path); return -1; }
    memset(out, 0, sizeof *out);
    ses_reader_init(&s_rdr);
    uint8_t buf[256];
    size_t got;
    uint8_t ft, fl; const uint8_t *fp;
    for (;;) {
        if (sto_read(f, buf, sizeof buf, &got) != 0) break;
        if (got == 0) break;
        ses_reader_push(&s_rdr, buf, got);
        while (ses_reader_next(&s_rdr, &ft, &fp, &fl) == 1) tally_cb(ft, fp, fl, out);
    }
    ses_reader_finish(&s_rdr);
    while (ses_reader_next(&s_rdr, &ft, &fp, &fl) == 1) tally_cb(ft, fp, fl, out);
    sto_close(f);
    return 0;
}

static int dbg_sum(int argc, char **argv)
{
    if (argc < 3) { printf("usage: dbg sum <id>   (e.g. S00001_001)\n"); return 1; }
    tally_t t;
    if (read_through_ses(argv[2], ".sum", &t) != 0) return 1;
    printf(".sum %s: HDR %s", argv[2], t.hdr ? "ok" : "MISSING");
    if (t.hdr) printf(" (fw=%s hwid=%s)", t.h.fw, t.h.hwid);
    printf("\n  VENUE %s", t.venue ? "ok" : "missing");
    if (t.venue) printf(" (venue_id=%u layout_id=%u)", (unsigned)t.v.venue_id, (unsigned)t.v.layout_id);
    printf("\n  LAP count = %d   DRAG_RUN count = %d\n", t.laps, t.drags);
    printf("  END present = %s\n", t.end ? "yes" : "no (session still open -- expected mid-session)");
    printf("  frames: ok=%u bad=%u\n", (unsigned)s_rdr.frames_ok, (unsigned)s_rdr.frames_bad);
    return 0;
}

static int dbg_logck(int argc, char **argv)
{
    if (argc < 3) { printf("usage: dbg logck <id>   (e.g. S00001_001)\n"); return 1; }
    tally_t t;
    if (read_through_ses(argv[2], ".log", &t) != 0) return 1;
    printf(".log %s: frames ok=%u bad=%u\n", argv[2],
           (unsigned)s_rdr.frames_ok, (unsigned)s_rdr.frames_bad);
    printf("  HDR=%d FIX=%d FUSED=%d LAP=%d SECTOR=%d DRAG_RUN=%d DRAG_GATE=%d EVENT=%d END=%d other=%d\n",
           t.hdr, t.fixes, t.fused, t.laps, t.sectors, t.drags, t.gates, t.events, t.end, t.others);
    if (s_rdr.frames_bad > 0)
        printf("  (bad>0: a truncated/torn tail -- the reader resynced past it, .log stays decodable)\n");
    return 0;
}

/* ---- dbg laps (from 3.4) ---- */
static int dbg_laps(void)
{
    int n = pipeline_lap_count();   /* A3: stream laps via pipeline_lap_at(), no local snapshot array */
    if (n == 0) { printf("laps: none completed yet\n"); return 0; }
    printf("laps: %d completed\n", n);
    for (int i = 0; i < n; i++) {
        lap_result_t lap;
        if (pipeline_lap_at(i, &lap) != 0) break;   /* ring advanced under us: stop cleanly */
        const lap_result_t *l = &lap;
        printf("  lap %-3u %lu.%03lu s  flags=0x%02x  splits[",
               (unsigned)l->lap_no, (unsigned long)(l->time_ms / 1000u),
               (unsigned long)(l->time_ms % 1000u), (unsigned)l->flags);
        for (uint8_t k = 0; k < l->n_sectors; k++)
            printf("%s%lu", k ? " " : "", (unsigned long)l->sector_ms[k]);
        printf("]  vmax=%u vmin=%u cm/s leanL=%d leanR=%d cdeg gacc=%d gbrake=%d glat=%d e-3\n",
               (unsigned)l->stats.max_speed_cms, (unsigned)l->stats.min_speed_cms,
               (int)l->stats.max_lean_l_cdeg, (int)l->stats.max_lean_r_cdeg,
               (int)l->stats.max_gacc_e3, (int)l->stats.max_gbrake_e3, (int)l->stats.max_glat_e3);
    }
    return 0;
}

/* ---- dbg rtc (new: dump the validated RTC snapshot, §15.3) ---- */
static int dbg_rtc(void)
{
    rtc_state_t st;
    rtc_validity_t v = lt_rtc_validate(&st);
    const char *vs = (v == RTC_VALID) ? "VALID" : (v == RTC_ABSENT) ? "ABSENT" : "INVALID";
    printf("rtc: %s\n", vs);
    if (v == RTC_VALID) {
        printf("  session_id      : %.10s\n", st.session_id);
        printf("  venue_id/layout : %u / %u\n", (unsigned)st.venue_id, (unsigned)st.layout_id);
        printf("  lap_no          : %u\n", (unsigned)st.lap_no);
        printf("  sector_idx      : %u\n", (unsigned)st.sector_idx);
        printf("  mode/power_state: %u / %u\n", (unsigned)st.mode, (unsigned)st.power_state);
        printf("  saved_gps_us    : %lld\n", (long long)st.saved_gps_us);
        printf("  lap_start_gps_us: %lld\n", (long long)st.lap_start_gps_us);
        printf("  partial_count   : %u\n", (unsigned)st.partial_count);
    }
    return 0;
}

/* ---- dbg mem (new: §22.4 resource measurement) ---- */
static int dbg_mem(void)
{
    /* Per-task stack headroom (uxTaskGetStackHighWaterMark is the minimum-ever free stack in
     * StackType_t units; on ESP-IDF Xtensa StackType_t is 1 byte, so the count is already bytes) plus the heap. Pipeline/logger/supervisor come from their supervisor
     * registration; the console REPL task is the one running this command. */
    static const struct { const char *name; uint8_t hb; } tasks[] = {
        { "pipeline",   HB_PIPELINE },
        { "logger",     HB_LOGGER },
        { "supervisor", HB_SUPERVISOR },
        { "ui",         HB_UI },
    };
    printf("heap free    : %u B\n", (unsigned)esp_get_free_heap_size());
    printf("heap min free: %u B\n", (unsigned)esp_get_minimum_free_heap_size());
    for (size_t i = 0; i < sizeof tasks / sizeof tasks[0]; i++) {
        TaskHandle_t h = sup_task_handle(tasks[i].hb);
        if (!h) { printf("stack %-11s: (not running)\n", tasks[i].name); continue; }
        printf("stack %-11s: %u B free\n", tasks[i].name,
               (unsigned)(uxTaskGetStackHighWaterMark(h) * (unsigned)sizeof(StackType_t)));
    }
    printf("stack %-11s: %u B free\n", "console",
           (unsigned)(uxTaskGetStackHighWaterMark(xTaskGetCurrentTaskHandle()) * (unsigned)sizeof(StackType_t)));
    return 0;
}

/* ---- dbg dispatch ---- */
static int cmd_dbg(int argc, char **argv)
{
    CORE_ASSERT_RET(argv != NULL, EXP_SERIAL_ASSERT_CODE, 1);
    if (argc >= 2) {
        const char *s = argv[1];
        CORE_ASSERT_RET(s != NULL, EXP_SERIAL_ASSERT_CODE, 1);
        if (strcmp(s, "status")  == 0) return dbg_status();
        if (strcmp(s, "logtest") == 0) return dbg_logtest(argc, argv);
        if (strcmp(s, "fs")      == 0) return dbg_fs();
        if (strcmp(s, "sum")     == 0) return dbg_sum(argc, argv);
        if (strcmp(s, "logck")   == 0) return dbg_logck(argc, argv);
        if (strcmp(s, "laps")    == 0) return dbg_laps();
        if (strcmp(s, "rtc")     == 0) return dbg_rtc();
        if (strcmp(s, "mem")     == 0) return dbg_mem();
        if (strcmp(s, "crash")   == 0) {
            printf("dbg: forcing a panic (abort) -> ESP_RST_PANIC\n");
            fflush(stdout);
            abort();                    /* never returns */
        }
        if (strcmp(s, "hang") == 0) {
            printf("dbg: subscribing to the task WDT then busy-looping -> task WDT fires in ~5s...\n");
            fflush(stdout);
            (void)esp_task_wdt_add(NULL);   /* a subscribed task that never resets trips the WDT deterministically (an unpinned busy-loop only migrates and never starves either idle for 5s) */
            for (;;) { }
        }
        if (strcmp(s, "gps") == 0 || strcmp(s, "imu") == 0 ||
            strcmp(s, "power") == 0 || strcmp(s, "sim") == 0) {
            printf("dbg %s: not in plan 03 (GPS/IMU raw, power states and sim control land later)\n", s);
            return 0;
        }
    }
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id> | laps | rtc | mem | crash | hang\n");
    printf("       dbg gps raw <on|off> | imu raw <on|off> | power <..> | sim <on|off>  (not in plan 03)\n");
    return 1;
}

/* ================================================================== *
 *  link stream sink (transport half, §18) -- NOT part of the dev UX
 *
 * The STRONG link_sink_serial_emit: overrides link.c's weak no-op at link time and writes
 * one fully-framed 0xFF stream frame to the console UART. The link module (components/app/
 * link) owns presence + fan-out and calls this only when a serial peer is attached; here we
 * just push the bytes. Raw uart_write_bytes -- not stdout -- so the binary frame is not
 * mangled by the console's \n->\r\n TX translation. It shares UART0 with the REPL only
 * temporally (the stream flows to a machine peer, the REPL serves a human, §8). Runs on the
 * link task, so a brief UART block is off the pipeline's critical path; with no peer reading,
 * the UART still transmits (no flow control) so it never blocks indefinitely. Guarded by
 * s_uart_ready so a stream frame can never reach an uninstalled driver.
 * ================================================================== */
int link_sink_serial_emit(const uint8_t *frame, size_t len)
{
    CORE_ASSERT_RET(frame != NULL, EXP_SERIAL_ASSERT_CODE, -1);
    CORE_ASSERT_RET(len > 0, EXP_SERIAL_ASSERT_CODE, -1);
    if (!s_uart_ready) return -1;                 /* console UART not up yet (pre-boot-step-12) */
    int w = uart_write_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, frame, len);
    return (w == (int)len) ? 0 : -1;
}

/* ================================================================== *
 *  REPL bring-up
 * ================================================================== */
static void register_cmd(const char *command, const char *help, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = { .command = command, .help = help, .hint = NULL, .func = func };
    esp_console_cmd_register(&cmd);
}

void export_serial_start(int reset_reason)
{
    s_reset_reason = reset_reason;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "laptimer>";
    repl_cfg.task_priority = 2;
    repl_cfg.task_stack_size = 6144;    /* headroom for the sum/logck readers + JSON/printf */
    repl_cfg.max_cmdline_length = 256;  /* room for `config set <json>` */
    /* the `config set <json>` reassembly buffer must stay bigger than a whole command line, or
     * a max-length line could overflow the copy loop's own bound in cmd_config_c. */
    CORE_ASSERT_VOID(sizeof(s_setbuf) > repl_cfg.max_cmdline_length, EXP_SERIAL_ASSERT_CODE);
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;
    CORE_ASSERT_VOID(repl != NULL, EXP_SERIAL_ASSERT_CODE);   /* ESP_OK must imply a usable REPL/UART handle */

    linenoiseSetDumbMode(1);            /* §18.4: line editing disabled (plain serial terminal) */

    register_cmd("status", "device status (§18.2, 20-byte record)", cmd_status_c);
    register_cmd("list",   "session list JSON (§14.3)", cmd_list_c);
    register_cmd("open",   "open <id> <vbo|nmea|json|log|sum>  (stream a session file)", cmd_open_c);
    register_cmd("read",   "read <offset>  (resume the last open transfer)", cmd_read_c);
    register_cmd("config", "config get | config set <json>", cmd_config_c);
    register_cmd("errlog", "error ring dump | errlog clear", cmd_errlog_c);
    register_cmd("diag",   "diagnostics JSON (§17.10)", cmd_diag_c);
    register_cmd("delete", "delete <id>  (unlink <id>.log/.sum)", cmd_delete_c);
    register_cmd("close",  "close the current transfer (ack)", cmd_close_c);
    register_cmd("dbg",    "status|logtest [n]|fs|sum <id>|logck <id>|laps|rtc|mem|crash|hang", cmd_dbg);

    esp_console_start_repl(repl);
    s_uart_ready = true;   /* the console UART driver is installed: the stream sink may now write */
}
