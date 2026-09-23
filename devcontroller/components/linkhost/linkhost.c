/* linkhost.c -- Plan 5.5 Task 3/6 IDF glue. The UART1 transport that drives the pure state
 * machines in host/linkhost_proto.c: a background RX task feeds linkhost_feed (the demux); a
 * mutex keeps one request in flight; linkhost_cmd/linkhost_status wait for the assembled framed
 * response; linkhost_download pauses the demux and drives the streaming lh_dl_* parser off UART1
 * for KB..MB session files; linkhost_flash streams the staged image from the `ota_stage` partition
 * and drives the OTA-token state machine. No wire-protocol logic lives here -- see linkhost_proto.c. */
#include "linkhost.h"

#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"

#include "build_config.h"
#include "linkstats.h"

static const char *TAG = "linkhost";

/* ---- tunables ---- */
/* RX ring >= 16 KB (T-E): a download's httpd_resp_send_chunk can block on a slow TCP client for
 * 100 ms+; at 115200 baud the 1 KB ring drained only 89 ms of data and overflowed (dropped bytes ->
 * CRC fail). 16 KB buys >1.4 s of slack; paired with CONFIG_UART_ISR_IN_IRAM the RX ISR keeps
 * running while flash writes disable the cache. */
#define LINK_RX_RING       (16 * 1024)
#define LINK_TX_RING       0                     /* blocking writes: apply backpressure, no async ring */
#define LINK_RX_CHUNK      256
#define LINK_CMD_TIMEOUT_MS 2000
#define LINK_CMD_RETRIES    2
#define LINK_ABSENT_TMO_MS  500                  /* one short attempt when no peer heartbeat is seen */
#define LINK_REQ_MTX_MS     400                  /* bounded s_req_mtx take: never park the httpd task */
#define LINK_PRESENT_US    (3 * 1000 * 1000)     /* peer_present window (<3 s, §Task 3 Step 7) */
#define LINK_STATUS_STALE_MS 3000                /* §4.2: a STATUS older than this = not connected */
#define OTA_READY_TMO_MS   3000
#define OTA_DONE_TMO_MS    10000                 /* lap-timer aborts after ~9 s without bytes */
#define OTA_CHUNK          512
#define DL_IDLE_TMO_MS     3000                  /* abort a download after this long with no bytes */
#define DL_HARD_MIN_MS     15000                 /* floor on the absolute download cap (small files) */
#define DL_HARD_MARGIN_US  (5 * 1000 * 1000)     /* slack added on top of the size-scaled transfer time */
#define RX_PARK_TMO_MS     200                   /* bounded wait for rx_task to park before flush (#65) */
#define RX_PARK_POLL_MS    5
#define RX_DRAIN_TMO_MS    10                    /* per-iteration read timeout while draining to quiet */
#define RX_DRAIN_CAP       10                    /* bounded drain iterations (~100 ms worst case) */

static SemaphoreHandle_t s_req_mtx;              /* one request in flight (§18.1) */
static TaskHandle_t      s_rx_task;
static volatile int64_t  s_last_activity_us;     /* stamped by the RX task on any link traffic */
static volatile bool     s_rx_paused;            /* pauses the demux RX task while flash/download owns UART1 */
static volatile bool     s_rx_parked;            /* true only while rx_task is in the paused-sleep, i.e.
                                                   * provably NOT inside uart_read_bytes (#65 handshake) */
static bool              s_inited;

/* linkhost_cmd copies the framed body out of the parser's shared s_body into here, so a late
 * duplicate reply parsed by the RX task can't tear an httpd send that is still reading the body.
 * Touched only on the httpd request task (linkhost_cmd is single-flight via s_req_mtx). */
static uint8_t s_cmd_body[LINKHOST_ASM_MAX];

/* ---- RX task: pump UART1 -> the demux ---- */
static void rx_task(void *arg)
{
    (void)arg;
    static uint8_t buf[LINK_RX_CHUNK];
    for (;;) {                                   /* service task: bounded per-iteration work */
        if (s_rx_paused) {
            s_rx_parked = true;                   /* provably not in uart_read_bytes (#65 handshake) */
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        s_rx_parked = false;                      /* about to own UART1 again */
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            s_last_activity_us = esp_timer_get_time();
            (void)linkhost_feed(buf, (size_t)n);
        }
    }
}

/* Deterministic replacement for a fixed "hope it parked" delay (#65): rx_task and a download/flash
 * loop both call uart_read_bytes on the SAME UART -- ESP-IDF hands concurrent readers DISTINCT
 * bytes, so if rx_task is still mid-read when the response arrives it silently steals bytes from
 * the download parser -> truncated body -> "bad response" (worst on the first call after a
 * backlog, when rx_task's exact parking time is least predictable).
 *
 * Waits (bounded) for rx_task to observe s_rx_paused and park itself out of uart_read_bytes
 * (proceeds anyway with a warning on timeout -- never blocks forever), flushes the driver's RX
 * ring, then drains-to-quiet: reads and discards until a read times out empty (the link has gone
 * idle) or a bounded iteration cap is hit, clearing any 0xFF stream-frame tail that was mid-
 * transmission when the flush landed. Must be called with s_rx_paused already true and s_req_mtx
 * held (single request in flight). */
static void rx_park_and_drain(void)
{
    assert(s_inited);
    assert(s_rx_paused);

    int waited = 0;
    while (!s_rx_parked && waited < RX_PARK_TMO_MS) {     /* bounded: RX_PARK_TMO_MS / poll interval */
        vTaskDelay(pdMS_TO_TICKS(RX_PARK_POLL_MS));
        waited += RX_PARK_POLL_MS;
    }
    if (!s_rx_parked) {
        ESP_LOGW(TAG, "rx_park_and_drain: rx_task did not park within %d ms", RX_PARK_TMO_MS);
    }

    uart_flush_input(DC_LINK_UART);               /* drop stale prompt/heartbeat before the command */

    uint8_t scratch[LINK_RX_CHUNK];
    for (int i = 0; i < RX_DRAIN_CAP; i++) {              /* bounded: RX_DRAIN_CAP iterations */
        int n = uart_read_bytes(DC_LINK_UART, scratch, sizeof(scratch), pdMS_TO_TICKS(RX_DRAIN_TMO_MS));
        if (n <= 0) break;                                /* link quiet: nothing left in flight */
    }
}

esp_err_t linkhost_init(void)
{
    if (s_inited) return ESP_OK;

    linkhost_reset();

    const uart_config_t cfg = {
        .baud_rate = DC_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* intr_alloc_flags stays 0: with CONFIG_UART_ISR_IN_IRAM=y the driver adds ESP_INTR_FLAG_IRAM
     * itself (esp_driver_uart/src/uart.c) so the RX ISR survives cache-disabled flash writes. */
    esp_err_t err = uart_driver_install(DC_LINK_UART, LINK_RX_RING, LINK_TX_RING, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(DC_LINK_UART, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(DC_LINK_UART, DC_LINK_TX, DC_LINK_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    s_req_mtx = xSemaphoreCreateMutex();
    if (!s_req_mtx) return ESP_ERR_NO_MEM;

    if (xTaskCreate(rx_task, "linkhost_rx", 6144, NULL, 4, &s_rx_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_inited = true;
    ESP_LOGI(TAG, "linkhost up: UART%d tx=%d rx=%d baud=%d",
             DC_LINK_UART, DC_LINK_TX, DC_LINK_RX, DC_LINK_BAUD);
    return ESP_OK;
}

/* Drains any stale pending response before issuing a fresh request. */
static void drain_pending(void)
{
    linkhost_frame_t f;
    int st;
    while (linkhost_pop_response(&f, &st)) { /* discard */ }
}

/* The frame name the lap-timer answers a given command with (export_serial.c register_cmd names):
 * status->"status", config get/set->"config". A returned frame whose name differs is a stale/late
 * reply for a different command and must not be accepted. NULL == don't match (e.g. list, which
 * streams via linkhost_download_cmd, not here). */
static const char *expected_frame_name(const char *cmd)
{
    if (strncmp(cmd, LT_CMD_STATUS, sizeof(LT_CMD_STATUS) - 1) == 0)      return "status";
    if (strncmp(cmd, LT_CMD_CONFIG_GET, sizeof(LT_CMD_CONFIG_GET) - 1) == 0 ||
        strncmp(cmd, LT_CMD_CONFIG_SET, sizeof(LT_CMD_CONFIG_SET) - 1) == 0) return "config";
    return NULL;
}

int linkhost_cmd(const char *cmd, linkhost_frame_t *out)
{
    assert(cmd != NULL);
    assert(out != NULL);
    if (!s_inited) return LINKHOST_E_NOTCONN;

    size_t clen = strlen(cmd);
    if (clen == 0 || clen > 250) return LINKHOST_E_PROTO;   /* console max_cmdline_length is 256 B */

    /* Bounded take: a download/flash on the worker task can hold s_req_mtx for many seconds; the
     * httpd request task must never park on it (M2) -- report BUSY and let the caller answer 503. */
    if (xSemaphoreTake(s_req_mtx, pdMS_TO_TICKS(LINK_REQ_MTX_MS)) != pdTRUE)
        return LINKHOST_E_BUSY;

    /* No heartbeat seen -> the peer is (probably) absent: one short attempt instead of 3x2 s, so a
     * disconnected link fails fast rather than freezing the UI for 6 s per request. */
    const char *want = expected_frame_name(cmd);
    bool present = linkhost_peer_present();
    int  attempts = present ? (LINK_CMD_RETRIES + 1) : 1;
    int  per_tmo  = present ? LINK_CMD_TIMEOUT_MS : LINK_ABSENT_TMO_MS;

    int result = present ? LINKHOST_E_TIMEOUT : LINKHOST_E_NOTCONN;
    for (int attempt = 0; attempt < attempts; attempt++) {           /* bounded retries */
        drain_pending();
        uart_write_bytes(DC_LINK_UART, cmd, clen);
        uart_write_bytes(DC_LINK_UART, "\r", 1);

        int64_t deadline = esp_timer_get_time() + (int64_t)per_tmo * 1000;
        bool got = false;
        while (esp_timer_get_time() < deadline) {                    /* bounded by the deadline */
            int st;
            if (linkhost_pop_response(out, &st)) {
                if (st == 0 && want && strcmp(out->name, want) != 0)
                    continue;      /* a late reply for a different command: keep waiting */
                result = st;       /* 0, or LINKHOST_E_CRC/_PROTO/_REMOTE from the parser/classifier */
                got = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (got) break;
    }

    /* Copy the body out of the parser's shared static buffer into a request-task-owned buffer so a
     * late duplicate reply (the REPL runs at priority 2; list opens files twice) can't overwrite it
     * mid-send. Do it under the mutex, before the RX task can produce another frame. */
    if (result == 0 && out->body && out->body_len <= sizeof s_cmd_body) {
        memcpy(s_cmd_body, out->body, out->body_len);
        out->body = s_cmd_body;
    } else if (result == 0) {
        result = LINKHOST_E_PROTO;   /* body larger than the assembly bound -> treat as malformed */
    }

    xSemaphoreGive(s_req_mtx);
    return result;
}

/* Served from the pushed-STATUS cache (Plan 5.6 T3): the lap-timer streams LT_REC_STATUS at 1 Hz
 * (and on-edge) rather than answering a framed `status` round-trip, so this never touches UART1. */
int linkhost_status(lt_status_t *out)
{
    assert(out != NULL);
    if (!s_inited) return LINKHOST_E_NOTCONN;
    return linkstats_status_fresh(esp_timer_get_time(), LINK_STATUS_STALE_MS, out) ? 0 : LINKHOST_E_NOTCONN;
}

int64_t linkhost_now_us(void)
{
    return esp_timer_get_time();
}

bool linkhost_peer_present(void)
{
    lt_status_t st;
    if (linkstats_status_fresh(esp_timer_get_time(), LINK_STATUS_STALE_MS, &st)) return true;
    int64_t last = s_last_activity_us;                 /* transitional fallback: any traffic < 3 s */
    return last != 0 && (esp_timer_get_time() - last) < LINK_PRESENT_US;
}

/* ---- streaming session download ---- */
/* Mirrors export_serial's fmt_from_str: only the log/sum session formats are Base64-encoded on the
 * wire; json/vbo/nmea are raw text. (The parser stays format-agnostic via lh_dl_init's flag.) */
static bool fmt_is_binary(const char *fmt)
{
    return strcmp(fmt, LT_FMT_LOG) == 0 || strcmp(fmt, LT_FMT_SUM) == 0;
}

int linkhost_download_cmd(const char *cmd, bool is_binary, lh_dl_chunk_cb chunk_cb, void *ctx,
                          linkhost_remote_err_t *rerr)
{
    assert(cmd != NULL);
    assert(chunk_cb != NULL);
    if (rerr) { rerr->code = 0; rerr->msg[0] = '\0'; }
    if (!s_inited) return LINKHOST_E_NOTCONN;

    size_t clen = strlen(cmd);
    if (clen == 0 || clen > 250) return LINKHOST_E_PROTO;   /* console max_cmdline_length is 256 B */

    lh_dl_ctx_t dl;
    lh_dl_init(&dl, is_binary, chunk_cb, ctx);

    xSemaphoreTake(s_req_mtx, portMAX_DELAY);    /* worker task: OK to wait for the link (not httpd) */
    s_rx_paused = true;                          /* the demux RX task must not steal these bytes */
    rx_park_and_drain();                         /* wait for rx_task to park, then flush + drain (#65) */

    uart_write_bytes(DC_LINK_UART, cmd, clen);
    uart_write_bytes(DC_LINK_UART, "\r", 1);

    static uint8_t buf[LINK_RX_CHUNK];
    int64_t start    = esp_timer_get_time();
    int64_t idle_dl  = start + (int64_t)DL_IDLE_TMO_MS * 1000;   /* reset on every read */
    int64_t hard_dl  = start + (int64_t)DL_HARD_MIN_MS * 1000;   /* floor; scaled once size is known */
    bool    scaled   = false;
    lh_dl_state_t st = dl.state;
    while (st < LH_DL_DONE) {                     /* bounded by hard_dl / idle_dl */
        int64_t t = esp_timer_get_time();
        if (t >= hard_dl || t >= idle_dl) break;                    /* timeout -> incomplete */
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            idle_dl = esp_timer_get_time() + (int64_t)DL_IDLE_TMO_MS * 1000;
            st = lh_dl_feed(&dl, buf, (size_t)n);
        }
        /* Scale the absolute cap to the announced body once the header parses (M5): a fixed 120 s
         * was < a 1 MB .log (~1.37 MB base64 ~= 119 s at this baud). transfer_us = size*10/baud;
         * cap = start + 1.5*transfer + margin, never below the floor. */
        if (!scaled && dl.state >= LH_DL_BODY && dl.body_size > 0) {
            int64_t xfer_us = (int64_t)dl.body_size * 10 * 1000000 / DC_LINK_BAUD;
            int64_t cap = start + xfer_us + xfer_us / 2 + DL_HARD_MARGIN_US;
            if (cap > hard_dl) hard_dl = cap;
            scaled = true;
        }
    }

    s_rx_paused = false;                         /* rx_task clears s_rx_parked on its next non-paused
                                                   * iteration (before it reads again) -- no need to
                                                   * clear it here too. */
    xSemaphoreGive(s_req_mtx);

    int result = lh_dl_result(&dl);
    if (result == LINKHOST_E_REMOTE && rerr) {
        rerr->code = dl.err_code;
        memcpy(rerr->msg, dl.err_msg, sizeof rerr->msg);
    }
    ESP_LOGI(TAG, "linkhost_download_cmd: '%s' -> result=%d (state=%d decoded=%u)",
             cmd, result, (int)dl.state, (unsigned)dl.decoded_len);
    return result;
}

int linkhost_download(const char *id, const char *fmt, lh_dl_chunk_cb chunk_cb, void *ctx,
                      linkhost_remote_err_t *rerr)
{
    assert(id != NULL);
    assert(fmt != NULL);
    assert(chunk_cb != NULL);

    char cmd[64];
    int cn = snprintf(cmd, sizeof(cmd), "%s %s %s", LT_CMD_OPEN, id, fmt);
    if (cn <= 0 || (size_t)cn >= sizeof(cmd)) return LINKHOST_E_PROTO;

    return linkhost_download_cmd(cmd, fmt_is_binary(fmt), chunk_cb, ctx, rerr);
}

/* ---- cmd-OTA flash (stage-then-push) ---- */
static void sha_to_hex(const uint8_t sha[32], char out[65])
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {               /* bounded: 32 bytes */
        out[i * 2]     = H[(sha[i] >> 4) & 0xF];
        out[i * 2 + 1] = H[sha[i] & 0xF];
    }
    out[64] = '\0';
}

/* Reads UART1 for up to tmo_ms, feeding the flash state machine until it leaves WAIT/STREAMING. */
static void flash_pump(lh_flash_ctx_t *f, int tmo_ms)
{
    static uint8_t buf[LINK_RX_CHUNK];
    int64_t deadline = esp_timer_get_time() + (int64_t)tmo_ms * 1000;
    while (esp_timer_get_time() < deadline) {    /* bounded by the deadline */
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            lh_flash_state_t st = linkhost_flash_feed(f, buf, (size_t)n);
            if (st == LH_FLASH_DONE_OK || st == LH_FLASH_DONE_ERR) return;
        }
    }
}

int linkhost_flash(const char *ver, const char *hwid, uint32_t size,
                   const uint8_t sha256[32], flash_progress_cb cb, void *ctx)
{
    assert(ver != NULL);
    assert(hwid != NULL);
    assert(sha256 != NULL);
    if (!s_inited) return LINKHOST_E_NOTCONN;

    const esp_partition_t *stage =
        esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "ota_stage");
    if (!stage) return LINKHOST_E_PROTO;
    if (size == 0 || size > stage->size) return LINKHOST_E_PROTO;

    char shahex[65];
    sha_to_hex(sha256, shahex);
    char cmd[160];
    int cn = snprintf(cmd, sizeof(cmd), "ota recv %u %s %s %s\r",
                      (unsigned)size, shahex, ver, hwid);
    if (cn <= 0 || (size_t)cn >= sizeof(cmd)) return LINKHOST_E_PROTO;

    int result;
    xSemaphoreTake(s_req_mtx, portMAX_DELAY);
    s_rx_paused = true;
    rx_park_and_drain();                         /* wait for rx_task to park, then flush + drain (#65) */

    lh_flash_ctx_t f;
    linkhost_flash_ctx_init(&f);

    uart_write_bytes(DC_LINK_UART, cmd, (size_t)cn);
    flash_pump(&f, OTA_READY_TMO_MS);

    if (f.state == LH_FLASH_STREAMING) {
        uint32_t sent = 0;
        uint8_t chunk[OTA_CHUNK];
        while (sent < size && f.state == LH_FLASH_STREAMING) {   /* bounded by `size` */
            uint32_t want = size - sent;
            if (want > OTA_CHUNK) want = OTA_CHUNK;
            if (esp_partition_read(stage, sent, chunk, want) != ESP_OK) {
                result = LINKHOST_E_PROTO;
                goto done;
            }
            uart_write_bytes(DC_LINK_UART, chunk, want);
            sent += want;
            if (cb) cb(sent, size, ctx);
            /* opportunistically drain a mid-stream OTA-ERR without stalling the feed */
            uint8_t rb[64];
            int rn = uart_read_bytes(DC_LINK_UART, rb, sizeof(rb), 0);
            if (rn > 0) (void)linkhost_flash_feed(&f, rb, (size_t)rn);
        }
        if (f.state == LH_FLASH_STREAMING) flash_pump(&f, OTA_DONE_TMO_MS);
    }

    result = linkhost_flash_result(&f);
done:
    s_rx_paused = false;                         /* rx_task clears s_rx_parked on its next non-paused
                                                   * iteration (before it reads again) -- no need to
                                                   * clear it here too. */
    xSemaphoreGive(s_req_mtx);
    ESP_LOGI(TAG, "linkhost_flash: size=%u -> result=%d (state=%d code=0x%04x)",
             (unsigned)size, result, (int)f.state, (unsigned)f.code);
    return result;
}
