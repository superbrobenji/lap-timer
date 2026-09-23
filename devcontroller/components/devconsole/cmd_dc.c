/* devcontroller/components/console/cmd_dc.c -- the `dc` command: the dev-kit's own
 * self-diagnostics on its own USB console, independent of the WiFi AP/SPA (Plan 5.6 Task 4).
 *
 *   dc status [--json]                device/link/logstore snapshot (see dc_status below)
 *   dc log <error|warn|info|debug>    esp_log_level_set("*", ...) -- quiet the REPL's own logs
 *   dc baud <rate>                    reopen the console UART at a new baud (host must follow)
 *
 * Every subcommand strips a trailing --json via console_wants_json and reports errors as
 * `ERR <reason>` (human) or `{"err":"<reason>"}` (json), returning non-zero -- so a later
 * tools/devkit.py can always tell a failure from a successful body on one parsed line.
 */
#include "cmd_dc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"   /* esp_get_free_heap_size */
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"

#include "build_config.h"   /* CFG_DC_VERSION */
#include "console.h"
#include "jsonw.h"
#include "linkhost.h"        /* lt_status_t, linkstats_t + the linkhost_stats_* locked wrappers */
#include "logstore.h"

/* GET /api/logs (webapi.c) uses the same 16-entry scratch array for the same reason: logstore_list
 * bounds its own scan to LOGSTORE_ROT_MAX_FILES, and 16 is already more rotated files than the
 * black-box log ever keeps day to day. */
#define DC_LOGSTORE_MAX 16

/* `stream stats` sampling (brief Step 6): the previous dc_status snapshot, so fused/status rates
 * can be reported as records/sec since the last call. s_prev_us == 0 means "no previous sample"
 * (matches linkhost_stats_age_ms's own 0 == never convention) -- the first `dc status` of a boot
 * always reports rate -1, not a spurious huge rate against an unset baseline. */
static linkstats_t s_prev;
static int64_t     s_prev_us;

static void dc_err(bool json, const char *reason)
{
    if (json) printf("{\"err\":\"%s\"}\n", reason);
    else      printf("ERR %s\n", reason);
}

/* Rate * 10 (one implied decimal) since jsonw has no float writer (task-4-brief resolution 3).
 * -1 when there is no previous sample yet. dt_us > 0 is asserted by the caller (s_prev_us != 0
 * guarantees now has advanced, since linkhost_now_us is monotonic and dc_status always samples a
 * fresh `now` after any previous call already stored one). */
static long long dc_rate_x10(uint32_t n_now, uint32_t n_prev, int64_t dt_us)
{
    assert(dt_us > 0);
    return ((long long)(n_now - n_prev) * 10LL * 1000000LL) / dt_us;
}

static int dc_status(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        dc_err(json, "usage: dc status [--json]");
        return 1;
    }

    int64_t now = linkhost_now_us();

    lt_status_t st;
    bool connected = (linkhost_status(&st) == 0);

    linkstats_t ls;
    linkhost_stats_snapshot(&ls);
    int64_t status_age_ms = linkhost_stats_age_ms(ls.last_status_us, now);
    int64_t stream_age_ms = linkhost_stats_age_ms(ls.last_fused_us, now);

    long long fused_rate_x10 = -1;
    long long status_rate_x10 = -1;
    if (s_prev_us != 0) {
        int64_t dt_us = now - s_prev_us;
        if (dt_us > 0) {
            fused_rate_x10  = dc_rate_x10(ls.n_fused, s_prev.n_fused, dt_us);
            status_rate_x10 = dc_rate_x10(ls.n_status, s_prev.n_status, dt_us);
        }
    }
    s_prev = ls;
    s_prev_us = now;

    logstore_entry_t entries[DC_LOGSTORE_MAX];
    int nfiles = logstore_list(entries, DC_LOGSTORE_MAX);
    if (nfiles < 0) nfiles = 0;
    uint64_t total_bytes = 0;
    for (int i = 0; i < nfiles; i++) total_bytes += entries[i].size;   /* bounded by DC_LOGSTORE_MAX */

    wifi_sta_list_t sta = { .num = 0 };
    (void)esp_wifi_ap_get_sta_list(&sta);   /* best-effort: 0 clients if the AP isn't up */

    uint32_t heap_free = esp_get_free_heap_size();

    if (json) {
        char buf[512];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "version", CFG_DC_VERSION);
        jsonw_uint(&w, "uptime_s", (unsigned long long)(now / 1000000));
        jsonw_uint(&w, "heap_free", (unsigned long long)heap_free);
        jsonw_uint(&w, "ap_clients", (unsigned long long)sta.num);
        jsonw_obj(&w, "link");
        jsonw_bool(&w, "connected", connected);
        jsonw_int(&w, "status_age_ms", status_age_ms);
        jsonw_int(&w, "stream_age_ms", stream_age_ms);
        jsonw_int(&w, "fused_rate_x10", fused_rate_x10);
        jsonw_int(&w, "status_rate_x10", status_rate_x10);
        jsonw_uint(&w, "gaps", ls.gaps);
        jsonw_close(&w);
        jsonw_obj(&w, "logstore");
        jsonw_bool(&w, "ready", logstore_ready());
        jsonw_uint(&w, "files", (unsigned long long)nfiles);
        jsonw_uint(&w, "bytes", total_bytes);
        jsonw_close(&w);
        if (!jsonw_end(&w)) {
            dc_err(true, "status body too large");
            return 1;
        }
        printf("%s\n", buf);
        return 0;
    }

    printf("version: %s\n", CFG_DC_VERSION);
    printf("uptime_s: %lld\n", (long long)(now / 1000000));
    printf("heap_free: %u\n", (unsigned)heap_free);
    printf("ap_clients: %d\n", sta.num);
    printf("link.connected: %s\n", connected ? "true" : "false");
    printf("link.status_age_ms: %lld\n", (long long)status_age_ms);
    printf("link.stream_age_ms: %lld\n", (long long)stream_age_ms);
    if (fused_rate_x10 < 0) printf("link.fused_rate: -1\n");
    else printf("link.fused_rate: %lld.%lld/s\n", fused_rate_x10 / 10, fused_rate_x10 % 10);
    if (status_rate_x10 < 0) printf("link.status_rate: -1\n");
    else printf("link.status_rate: %lld.%lld/s\n", status_rate_x10 / 10, status_rate_x10 % 10);
    printf("link.gaps: %u\n", (unsigned)ls.gaps);
    printf("logstore.ready: %s\n", logstore_ready() ? "true" : "false");
    printf("logstore.files: %d\n", nfiles);
    printf("logstore.bytes: %llu\n", (unsigned long long)total_bytes);
    return 0;
}

static int dc_log(int argc, char **argv, bool json)
{
    if (argc != 3) {
        dc_err(json, "usage: dc log <error|warn|info|debug>");
        return 1;
    }
    const char *lvl = argv[2];
    esp_log_level_t l;
    if      (strcmp(lvl, "error") == 0) l = ESP_LOG_ERROR;
    else if (strcmp(lvl, "warn")  == 0) l = ESP_LOG_WARN;
    else if (strcmp(lvl, "info")  == 0) l = ESP_LOG_INFO;
    else if (strcmp(lvl, "debug") == 0) l = ESP_LOG_DEBUG;
    else {
        dc_err(json, "bad log level (want error|warn|info|debug)");
        return 1;
    }
    esp_log_level_set("*", l);
    printf("OK %s\n", lvl);
    return 0;
}

static bool dc_baud_valid(long rate)
{
    return rate == 115200 || rate == 230400 || rate == 460800 || rate == 921600;
}

static int dc_baud(int argc, char **argv, bool json)
{
    if (argc != 3) {
        dc_err(json, "usage: dc baud <115200|230400|460800|921600>");
        return 1;
    }
    char *end = NULL;
    long rate = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || !dc_baud_valid(rate)) {
        dc_err(json, "bad baud rate (want 115200|230400|460800|921600)");
        return 1;
    }

    /* Print + flush BEFORE reopening the UART at the new rate, or this OK line is garbled: it
     * would still be draining out of the TX FIFO at the OLD baud while the host is already
     * listening at the NEW one (task-4-brief resolution 4). */
    printf("OK %ld\n", rate);
    fflush(stdout);
    uart_wait_tx_done((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(100));
    uart_set_baudrate((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, (uint32_t)rate);
    return 0;
}

static void dc_usage(void)
{
    printf("usage: dc status [--json] | dc log <error|warn|info|debug> | dc baud <rate>\n");
}

static int cmd_dc_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);
    if (argc < 2) {
        dc_usage();
        return 1;
    }
    if (strcmp(argv[1], "status") == 0) return dc_status(argc, argv, json);
    if (strcmp(argv[1], "log")    == 0) return dc_log(argc, argv, json);
    if (strcmp(argv[1], "baud")   == 0) return dc_baud(argc, argv, json);
    dc_err(json, "unknown subcommand (want status|log|baud)");
    return 1;
}

void cmd_dc_register(void)
{
    console_register("dc", "dc status [--json] | dc log <level> | dc baud <rate>", cmd_dc_main);
}
