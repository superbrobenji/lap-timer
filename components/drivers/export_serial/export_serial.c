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
 * `dbg` carries the diagnostics verbs migrated from the 3.2-3.4 console (status/logtest/fs/sum/
 * logck/laps) plus `rtc` (dump the validated RTC snapshot), `crash` (force a panic) and `hang`
 * (busy-loop to trip the task WDT). `gps raw`/`imu raw`/`power`/`sim` are stubbed until their
 * drivers land. `list`/`open`/`read` (file streaming) are registered in Task 5.
 */
#include "export_serial.h"

#include "app/cmd.h"
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_rtc.h"
#include "app/lt_sup.h"
#include "app/pipeline.h"
#include "hal/storage.h"

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
#include "esp_timer.h"
#include "linenoise/linenoise.h"

static int s_reset_reason;

/* ================================================================== *
 *  §18.1 command ops -> framed serial output (§18.4)
 * ================================================================== */

/* Response assembly for one request (one in flight, §18.1). The emit callback appends chunk
 * payloads here; the LAST chunk triggers the frame print. */
#define SER_ASM_MAX 3072
typedef struct {
    const char *name;              /* frame name for BEGIN/END */
    uint8_t     buf[SER_ASM_MAX];
    size_t      len;
    bool        error;
    uint16_t    err_code;
} ser_ctx_t;

static ser_ctx_t s_ser;            /* static: keeps the 3 KB buffer off the console stack */

static void ser_flush(ser_ctx_t *c)
{
    if (c->error) {
        printf("ERR 0x%04x: %.*s\r\n", (unsigned)c->err_code, (int)c->len, (const char *)c->buf);
        fflush(stdout);
        return;
    }
    uint32_t crc = esp_rom_crc32_le(0, c->buf, c->len);
    printf("---BEGIN %s %u---\r\n", c->name ? c->name : "data", (unsigned)c->len);
    if (c->len) fwrite(c->buf, 1, c->len, stdout);
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

/* Run one op through cmd_dispatch with logging quiesced, then framed by ser_emit. */
static void run_cmd(uint8_t op, const char *name, const uint8_t *payload, size_t len)
{
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);        /* §18.4: quiet logs during a framed transfer */

    s_ser.name = name;
    s_ser.len = 0;
    s_ser.error = false;
    s_ser.err_code = 0;
    (void)cmd_dispatch(op, /*tag*/1, payload, len, ser_emit, &s_ser);

    esp_log_level_set("*", saved);
}

/* ---- text commands (§18.4) ---- */
static int cmd_status_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_STATUS, "status", NULL, 0);
    return 0;
}

static char s_setbuf[1024];   /* `config set <json>` reassembly */
static int cmd_config_c(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "get") == 0) { run_cmd(CMD_CONFIG_GET, "config", NULL, 0); return 0; }
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
        run_cmd(CMD_CONFIG_SET, "config", (const uint8_t *)s_setbuf, w);
        return 0;
    }
    printf("usage: config get | config set <json>\n");
    return 1;
}

static int cmd_errlog_c(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) { run_cmd(CMD_ERRLOG_CLEAR, "errlog", NULL, 0); return 0; }
    run_cmd(CMD_ERRLOG_GET, "errlog", NULL, 0);
    return 0;
}

static int cmd_diag_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_DIAG_GET, "diag", NULL, 0);
    return 0;
}

static int cmd_delete_c(int argc, char **argv)
{
    if (argc < 2) { printf("usage: delete <id>\n"); return 1; }
    run_cmd(CMD_DELETE, "delete", (const uint8_t *)argv[1], strlen(argv[1]));
    return 0;
}

static int cmd_close_c(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_cmd(CMD_CLOSE, "close", NULL, 0);
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
    long n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 1000;
    if (n < 1) n = 1;
    if (n > 100000) n = 100000;

    log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = 1, .venue_id = 1, .layout_id = 1,
                          .gps_us = (int64_t)1700000000000000LL };
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
        while (!ring_push(&g_fix_ring, &f)) {   /* full: wake the logger and yield */
            logger_notify();
            vTaskDelay(1);
            if (++spins > 1000) { ok = false; break; }
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
static void fs_list_cb(const char *name, uint32_t size, void *ctx)
{
    (void)ctx;
    printf("  %-24s %8u B\n", name, (unsigned)size);
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
    int cnt = sto_list("/sessions", fs_list_cb, NULL);
    if (cnt < 0) printf("  (cannot list)\n");
    else if (cnt == 0) printf("  (empty)\n");
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
    for (;;) {
        if (sto_read(f, buf, sizeof buf, &got) != 0) break;
        if (got == 0) break;
        ses_reader_feed(&s_rdr, buf, got, tally_cb, out);
    }
    ses_reader_flush(&s_rdr, tally_cb, out);
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
    static lap_result_t laps[24];
    int n = pipeline_laps_snapshot(laps, (int)(sizeof laps / sizeof laps[0]));
    if (n == 0) { printf("laps: none completed yet\n"); return 0; }
    printf("laps: %d completed\n", n);
    for (int i = 0; i < n; i++) {
        const lap_result_t *l = &laps[i];
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

/* ---- dbg dispatch ---- */
static int cmd_dbg(int argc, char **argv)
{
    if (argc >= 2) {
        const char *s = argv[1];
        if (strcmp(s, "status")  == 0) return dbg_status();
        if (strcmp(s, "logtest") == 0) return dbg_logtest(argc, argv);
        if (strcmp(s, "fs")      == 0) return dbg_fs();
        if (strcmp(s, "sum")     == 0) return dbg_sum(argc, argv);
        if (strcmp(s, "logck")   == 0) return dbg_logck(argc, argv);
        if (strcmp(s, "laps")    == 0) return dbg_laps();
        if (strcmp(s, "rtc")     == 0) return dbg_rtc();
        if (strcmp(s, "crash")   == 0) {
            printf("dbg: forcing a panic (abort) -> ESP_RST_PANIC\n");
            fflush(stdout);
            abort();                    /* never returns */
        }
        if (strcmp(s, "hang") == 0) {
            printf("dbg: busy-looping (no WDT reset) to trip the task WDT...\n");
            fflush(stdout);
            for (;;) { }                /* starve the idle task -> task WDT fires */
        }
        if (strcmp(s, "gps") == 0 || strcmp(s, "imu") == 0 ||
            strcmp(s, "power") == 0 || strcmp(s, "sim") == 0) {
            printf("dbg %s: not in plan 03 (GPS/IMU raw, power states and sim control land later)\n", s);
            return 0;
        }
    }
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id> | laps | rtc | crash | hang\n");
    printf("       dbg gps raw <on|off> | imu raw <on|off> | power <..> | sim <on|off>  (not in plan 03)\n");
    return 1;
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
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;

    linenoiseSetDumbMode(1);            /* §18.4: line editing disabled (plain serial terminal) */

    register_cmd("status", "device status (§18.2, 20-byte record)", cmd_status_c);
    register_cmd("config", "config get | config set <json>", cmd_config_c);
    register_cmd("errlog", "error ring dump | errlog clear", cmd_errlog_c);
    register_cmd("diag",   "diagnostics JSON (§17.10)", cmd_diag_c);
    register_cmd("delete", "delete <id>  (unlink <id>.log/.sum)", cmd_delete_c);
    register_cmd("close",  "close the current transfer (ack)", cmd_close_c);
    register_cmd("dbg",    "status|logtest [n]|fs|sum <id>|logck <id>|laps|rtc|crash|hang", cmd_dbg);
    /* list/open/read (file streaming, §18.1) are registered in Task 5. */

    esp_console_start_repl(repl);
}
