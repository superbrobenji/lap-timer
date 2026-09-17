/* dbg_console.c -- diagnostics console (spec §18.4; 3.2 exit `dbg status` + 3.3 storage/logger verbs).
 *
 * IDF esp_console REPL on UART0. 3.2 registered `dbg status`; 3.3 adds the verbs that exercise the
 * logger end-to-end so the roadmap power-cut exit test is possible before the 3.4 pipeline exists:
 *
 *   dbg logtest [n]  open a session, push n synthetic FIX records into fix_ring + 2 LAP events into
 *                    the logger's evt_q, and let the logger drain/write/rebuild-.sum. Does NOT close,
 *                    so a power cut lands mid-session (mid-.log-write).
 *   dbg fs           mount state, total/free KB, degraded, sys storage flags, and `/sessions` listing.
 *   dbg sum <id>     read <id>.sum back through a core/ses reader: HDR ok, VENUE, LAP count, END present.
 *   dbg logck <id>   read <id>.log back: good-/bad-frame counts (a truncated tail shows as one bad
 *                    frame the reader resyncs past -> proves the .log stays decodable) + per-type tally.
 *
 * The full §18.4 export console (status/list/open/get/... and export) replaces this in 3.5.
 */
#include "app/dbg_console.h"
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
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
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"

static int s_reset_reason;

/* ---------------- dbg status (kept from 3.2) ---------------- */
static int cmd_status(void)
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

/* ---------------- dbg logtest [n] ---------------- */
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

static int cmd_logtest(int argc, char **argv)
{
    long n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 1000;
    if (n < 1) n = 1;
    if (n > 100000) n = 100000;

    /* open a session */
    log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = 1, .venue_id = 1, .layout_id = 1,
                          .gps_us = (int64_t)1700000000000000LL };
    xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100));
    logger_notify();
    vTaskDelay(pdMS_TO_TICKS(30));      /* let the logger open + write the HDR + initial .sum */

    /* real VENUE + two full LAP records via the 3.4 result path (rebuild .sum with laps early,
     * before the long .log tail). Mirrors what the pipeline hands the logger. */
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

    /* stream n synthetic fixes with backpressure (drop-newest ring) */
    long pushed = 0;
    for (long i = 0; i < n; i++) {
        gps_fix_t f;
        synth_fix(&f, (uint32_t)i);
        int spins = 0;
        bool ok = true;
        while (!ring_push(&g_fix_ring, &f)) {   /* full: wake the logger and yield */
            logger_notify();
            vTaskDelay(1);
            if (++spins > 1000) { ok = false; break; }  /* logger stuck (e.g. storage dead): give up */
        }
        if (ok) pushed++;
        if ((i & 0x1F) == 0x1F) logger_notify(); /* nudge every 32 */
    }
    logger_notify();
    printf("logtest: opened a session, queued 2 laps, pushed %ld/%ld fixes; NOT closed.\n", pushed, n);
    printf("  -> watch for \"session Sxxxxx_yyy open\", then pull power to test the cut.\n");
    printf("  -> after reboot: dbg fs ; dbg sum <id> ; dbg logck <id>\n");
    return 0;
}

/* ---------------- dbg fs ---------------- */
static void fs_list_cb(const char *name, uint32_t size, void *ctx)
{
    (void)ctx;
    printf("  %-24s %8u B\n", name, (unsigned)size);
}

static int cmd_fs(void)
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

/* ---------------- ses reader tally (shared by sum + logck) ---------------- */
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

static int cmd_sum(int argc, char **argv)
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

static int cmd_logck(int argc, char **argv)
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

/* ---------------- dbg laps ---------------- */
static int cmd_laps(void)
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

static int cmd_dbg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "status") == 0)  return cmd_status();
    if (argc >= 2 && strcmp(argv[1], "logtest") == 0) return cmd_logtest(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "fs") == 0)      return cmd_fs();
    if (argc >= 2 && strcmp(argv[1], "sum") == 0)     return cmd_sum(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "logck") == 0)   return cmd_logck(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "laps") == 0)    return cmd_laps();
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id> | laps\n");
    return 1;
}

void dbg_console_start(int reset_reason)
{
    s_reset_reason = reset_reason;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "laptimer>";
    repl_cfg.task_priority = 2;
    repl_cfg.task_stack_size = 6144;   /* headroom for the sum/logck file readers + printf (3.2 used 4096) */
    repl_cfg.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;

    linenoiseSetDumbMode(1);   /* §18.4: line editing disabled (plain serial terminal) */

    const esp_console_cmd_t cmd = {
        .command = "dbg",
        .help = "diagnostics: status | logtest [n] | fs | sum <id> | logck <id> | laps",
        .hint = NULL,
        .func = cmd_dbg,
    };
    esp_console_cmd_register(&cmd);
    esp_console_start_repl(repl);
}
