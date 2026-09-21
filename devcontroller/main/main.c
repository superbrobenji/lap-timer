/* main.c -- dev controller app_main (Plan 5.5 Task 2 scaffold):
 *   1. nvs_flash_init
 *   2. SoftAP up (ssid "laptimer-dev", WPA2, channel 1, ip 192.168.4.1 -- esp_netif's AP default)
 *   3. mount the `www` (RO) and `logs` (RW) LittleFS partitions -- a missing/unformatted `www`
 *      is non-fatal (logged); the static handler below serves a placeholder page instead
 *   4. linkhost_init
 *   5. start esp_http_server, register GET /api/status (linkhost_status -> JSON) and a static
 *      file catch-all serving `www` (index.html + assets)
 *
 * logstore_init and webapi_register are also called here (both still Task 5/4 stubs) so the
 * whole devcontroller/ tree compiles+links as one image, and so Tasks 4/5 slot in without
 * further main.c surgery.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "build_config.h"
#include "linkhost.h"
#include "linkhost_proto.h"
#include "logstore.h"
#include "webapi.h"

static const char *TAG = "dc_main";

#define DC_WIFI_SSID     "laptimer-dev"
#define DC_WIFI_CHANNEL  1
#define DC_WIFI_MAX_CONN 4
#define DC_MDNS_HOST     "laptimer-dev"   /* -> http://laptimer-dev.local */

#define WWW_BASE  "/www"
#define WWW_PART  "www"
#define LOGS_BASE "/logs"
#define LOGS_PART "logs"
/* partitions.csv's `logs` partition is 0xF0000 = 960 KB. logstore reserves the full worst-case
 * file (header + 64 KB per-file cap) inside this budget, so keep it under partition - 64 KB - ~32 KB
 * LittleFS metadata slack (T-E/M6) to avoid ENOSPC at steady state. */
#define LOGS_CAP_BYTES (832u * 1024u)

static void nvs_init_or_erase(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void wifi_ap_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();   /* default AP IP: 192.168.4.1 */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid           = DC_WIFI_SSID,
            .ssid_len       = (uint8_t)strlen(DC_WIFI_SSID),
            .channel        = DC_WIFI_CHANNEL,
            .max_connection = DC_WIFI_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    /* Per-device WPA2 PSK derived from the eFuse MAC (T-F): a source-constant PSK let anyone within
     * radio range join the AP and drive `config set` / `ota recv`. "ltdev-" + 12 hex = 18 chars
     * (>=8 for WPA2), printed below so the operator can read it off the boot console. */
    uint8_t mac[6] = { 0 };
    esp_err_t me = esp_efuse_mac_get_default(mac);
    if (me != ESP_OK) ESP_LOGW(TAG, "efuse mac read failed: %s", esp_err_to_name(me));
    snprintf((char *)ap_config.ap.password, sizeof ap_config.ap.password,
             "ltdev-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP up: ssid=%s psk=%s channel=%d ip=192.168.4.1",
             DC_WIFI_SSID, (const char *)ap_config.ap.password, DC_WIFI_CHANNEL);
}

/* mDNS: advertise "laptimer-dev.local" + the _http._tcp service on the AP so the SPA is reachable
 * by name (no 192.168.4.1 to type). Non-fatal -- a client can always fall back to the IP. */
static void mdns_start(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) { ESP_LOGW(TAG, "mdns_init: %s", esp_err_to_name(err)); return; }
    (void)mdns_hostname_set(DC_MDNS_HOST);
    (void)mdns_instance_name_set("Lap-timer dev controller");
    (void)mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS up: http://%s.local", DC_MDNS_HOST);
}

static void littlefs_mount(const char *base_path, const char *label, bool read_only)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = base_path,
        .partition_label        = label,
        .partition              = NULL,
        .format_if_mount_failed = true,
        .read_only              = read_only,
        .dont_mount             = false,
        .grow_on_mount          = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        /* Non-fatal: `www` in particular may not exist yet (the SPA merges into it in a later
         * task) -- the static handler below serves a placeholder when the mount/file is missing. */
        ESP_LOGW(TAG, "mount %s (%s) failed: %s", label, base_path, esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    if (esp_littlefs_info(label, &total, &used) == ESP_OK)
        ESP_LOGI(TAG, "mounted %s at %s: %u/%u KB used", label, base_path,
                 (unsigned)(used / 1024u), (unsigned)(total / 1024u));
}

/* ---- start esp_http_server and register all /api + static handlers via webapi_register.
 * The httpd is tuned for webapi's async download worker: extra URI-handler slots, spare sockets
 * for an in-flight download, a deeper request stack for the config-diff path, and the wildcard
 * match fn the SPA catch-all needs. (Status + static serving now live in webapi.c.) ---- */
static void httpd_start_and_register(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn    = httpd_uri_match_wildcard;   /* /api/session wildcard + the SPA catch-all */
    config.max_uri_handlers = 16;                         /* 8 handlers today, headroom for more */
    config.max_open_sockets = 7;                          /* leave sockets spare for a download */
    config.stack_size       = 8192;                       /* config-diff + relays need the room */
    config.lru_purge_enable = true;                       /* reap the LRU socket under pressure */

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    esp_err_t wr = webapi_register(server);   /* registers handlers + starts the async worker */
    if (wr != ESP_OK)
        ESP_LOGE(TAG, "webapi_register failed: %s", esp_err_to_name(wr));
    else
        ESP_LOGI(TAG, "webapi handlers registered");
}

/* Drain the demuxed lap-timer stream: persist every record (black-box logging) and publish it to
 * the live monitor. linkhost_stream_pop is the single consumer of the demux ring. */
static void stream_consumer(void *arg)
{
    (void)arg;
    lt_stream_rec_t r;
    for (;;) {
        int any = 0;
        while (linkhost_stream_pop(&r) == 0) {   /* bounded: ring is finite, drains then returns */
            (void)logstore_append(&r);
            webapi_stream_push(&r);
            any = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(any ? 5 : 40));
    }
}

/* ESP-IDF calls app_main() as the framework entry point; it has no project header to declare it
 * in (-Wmissing-prototypes needs a prototype in scope at the definition). */
void app_main(void);

void app_main(void)
{
    ESP_LOGI(TAG, "dev controller %s booting", CFG_DC_VERSION);

    nvs_init_or_erase();
    wifi_ap_start();
    mdns_start();

    littlefs_mount(WWW_BASE, WWW_PART, true);
    littlefs_mount(LOGS_BASE, LOGS_PART, false);

    esp_err_t lr = logstore_init(LOGS_CAP_BYTES);
    if (lr != ESP_OK)
        ESP_LOGW(TAG, "logstore_init: %s (Task 5 not yet landed)", esp_err_to_name(lr));

    esp_err_t ir = linkhost_init();
    if (ir != ESP_OK)
        ESP_LOGW(TAG, "linkhost_init: %s", esp_err_to_name(ir));

    httpd_start_and_register();

    if (xTaskCreate(stream_consumer, "stream_consumer", 4096, NULL, 6, NULL) != pdPASS)
        ESP_LOGW(TAG, "stream_consumer task create failed");

    ESP_LOGI(TAG, "dev controller ready");
}
