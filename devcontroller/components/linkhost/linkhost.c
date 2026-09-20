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

static const char *TAG = "linkhost";

/* ---- tunables ---- */
#define LINK_RX_BUF        1024
#define LINK_RX_CHUNK      256
#define LINK_CMD_TIMEOUT_MS 2000
#define LINK_CMD_RETRIES    2
#define LINK_PRESENT_US    (3 * 1000 * 1000)     /* peer_present window (<3 s, §Task 3 Step 7) */
#define OTA_READY_TMO_MS   3000
#define OTA_DONE_TMO_MS    10000                 /* lap-timer aborts after ~9 s without bytes */
#define OTA_CHUNK          512
#define DL_IDLE_TMO_MS     3000                  /* abort a download after this long with no bytes */
#define DL_HARD_TMO_MS     120000                /* absolute cap on one download (MB files at low baud) */

static SemaphoreHandle_t s_req_mtx;              /* one request in flight (§18.1) */
static TaskHandle_t      s_rx_task;
static volatile int64_t  s_last_activity_us;     /* stamped by the RX task on any link traffic */
static volatile bool     s_rx_paused;            /* pauses the demux RX task while flash/download owns UART1 */
static bool              s_inited;

/* ---- RX task: pump UART1 -> the demux ---- */
static void rx_task(void *arg)
{
    (void)arg;
    static uint8_t buf[LINK_RX_CHUNK];
    for (;;) {                                   /* service task: bounded per-iteration work */
        if (s_rx_paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            s_last_activity_us = esp_timer_get_time();
            (void)linkhost_feed(buf, (size_t)n);
        }
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
    esp_err_t err = uart_driver_install(DC_LINK_UART, LINK_RX_BUF, LINK_RX_BUF, 0, NULL, 0);
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

int linkhost_cmd(const char *cmd, linkhost_frame_t *out)
{
    assert(cmd != NULL);
    assert(out != NULL);
    if (!s_inited) return LINKHOST_E_NOTCONN;

    size_t clen = strlen(cmd);
    if (clen == 0 || clen > 250) return LINKHOST_E_PROTO;   /* console max_cmdline_length is 256 B */

    int result = LINKHOST_E_TIMEOUT;
    xSemaphoreTake(s_req_mtx, portMAX_DELAY);

    for (int attempt = 0; attempt <= LINK_CMD_RETRIES; attempt++) {   /* bounded retries */
        drain_pending();
        uart_write_bytes(DC_LINK_UART, cmd, clen);
        uart_write_bytes(DC_LINK_UART, "\r", 1);

        int64_t deadline = esp_timer_get_time() + (int64_t)LINK_CMD_TIMEOUT_MS * 1000;
        bool got = false;
        while (esp_timer_get_time() < deadline) {                    /* bounded by the deadline */
            int st;
            if (linkhost_pop_response(out, &st)) {
                result = st;   /* 0 or LINKHOST_E_CRC/_PROTO from the parser */
                got = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (got) break;
    }

    xSemaphoreGive(s_req_mtx);
    return result;
}

int linkhost_status(lt_status_t *out)
{
    assert(out != NULL);
    linkhost_frame_t f;
    int rc = linkhost_cmd(LT_CMD_STATUS, &f);
    if (rc != 0) return rc;
    if (f.body_len != LT_STATUS_LEN) return LINKHOST_E_PROTO;
    if (!linkhost_status_decode(f.body, out)) return LINKHOST_E_PROTO;
    return 0;
}

bool linkhost_peer_present(void)
{
    if (!s_inited) return false;
    int64_t last = s_last_activity_us;
    if (last == 0) return false;
    return (esp_timer_get_time() - last) < LINK_PRESENT_US;
}

/* ---- streaming session download ---- */
/* Mirrors export_serial's fmt_from_str: only the log/sum session formats are Base64-encoded on the
 * wire; json/vbo/nmea are raw text. (The parser stays format-agnostic via lh_dl_init's flag.) */
static bool fmt_is_binary(const char *fmt)
{
    return strcmp(fmt, LT_FMT_LOG) == 0 || strcmp(fmt, LT_FMT_SUM) == 0;
}

int linkhost_download(const char *id, const char *fmt, lh_dl_chunk_cb chunk_cb, void *ctx)
{
    assert(id != NULL);
    assert(fmt != NULL);
    assert(chunk_cb != NULL);
    if (!s_inited) return LINKHOST_E_NOTCONN;

    char cmd[64];
    int cn = snprintf(cmd, sizeof(cmd), "%s %s %s\r", LT_CMD_OPEN, id, fmt);
    if (cn <= 0 || (size_t)cn >= sizeof(cmd)) return LINKHOST_E_PROTO;

    lh_dl_ctx_t dl;
    lh_dl_init(&dl, fmt_is_binary(fmt), chunk_cb, ctx);

    xSemaphoreTake(s_req_mtx, portMAX_DELAY);
    s_rx_paused = true;                          /* the demux RX task must not steal these bytes */
    vTaskDelay(pdMS_TO_TICKS(25));               /* let the RX task release UART1 */
    uart_flush_input(DC_LINK_UART);              /* drop stale prompt/heartbeat before `open` */

    uart_write_bytes(DC_LINK_UART, cmd, (size_t)cn);

    static uint8_t buf[LINK_RX_CHUNK];
    int64_t now      = esp_timer_get_time();
    int64_t idle_dl  = now + (int64_t)DL_IDLE_TMO_MS * 1000;   /* reset on every read */
    int64_t hard_dl  = now + (int64_t)DL_HARD_TMO_MS * 1000;   /* absolute bound on the loop */
    lh_dl_state_t st = dl.state;
    while (st < LH_DL_DONE) {                     /* bounded by hard_dl / idle_dl */
        int64_t t = esp_timer_get_time();
        if (t >= hard_dl || t >= idle_dl) break;                    /* timeout -> incomplete */
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            idle_dl = esp_timer_get_time() + (int64_t)DL_IDLE_TMO_MS * 1000;
            st = lh_dl_feed(&dl, buf, (size_t)n);
        }
    }

    s_rx_paused = false;
    xSemaphoreGive(s_req_mtx);

    int result = lh_dl_result(&dl);
    ESP_LOGI(TAG, "linkhost_download: %s %s -> result=%d (state=%d decoded=%u)",
             id, fmt, result, (int)dl.state, (unsigned)dl.decoded_len);
    return result;
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
    vTaskDelay(pdMS_TO_TICKS(25));               /* let the RX task release UART1 */
    uart_flush_input(DC_LINK_UART);

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
    s_rx_paused = false;
    xSemaphoreGive(s_req_mtx);
    ESP_LOGI(TAG, "linkhost_flash: size=%u -> result=%d (state=%d code=0x%04x)",
             (unsigned)size, result, (int)f.state, (unsigned)f.code);
    return result;
}
