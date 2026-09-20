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
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "build_config.h"
#include "linkhost.h"
#include "logstore.h"
#include "webapi.h"

static const char *TAG = "dc_main";

#define DC_WIFI_SSID     "laptimer-dev"
#define DC_WIFI_PASS     "laptimer-dev-ap"   /* WPA2-PSK, >=8 chars */
#define DC_WIFI_CHANNEL  1
#define DC_WIFI_MAX_CONN 4

#define WWW_BASE  "/www"
#define WWW_PART  "www"
#define LOGS_BASE "/logs"
#define LOGS_PART "logs"
/* partitions.csv's `logs` partition is ~960 KB; leave rotation headroom below the raw size. */
#define LOGS_CAP_BYTES (900u * 1024u)

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
            .password       = DC_WIFI_PASS,
            .max_connection = DC_WIFI_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP up: ssid=%s channel=%d ip=192.168.4.1", DC_WIFI_SSID, DC_WIFI_CHANNEL);
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

/* ESP-IDF calls app_main() as the framework entry point; it has no project header to declare it
 * in (-Wmissing-prototypes needs a prototype in scope at the definition). */
void app_main(void);

void app_main(void)
{
    ESP_LOGI(TAG, "dev controller %s booting", CFG_DC_VERSION);

    nvs_init_or_erase();
    wifi_ap_start();

    littlefs_mount(WWW_BASE, WWW_PART, true);
    littlefs_mount(LOGS_BASE, LOGS_PART, false);

    esp_err_t lr = logstore_init(LOGS_CAP_BYTES);
    if (lr != ESP_OK)
        ESP_LOGW(TAG, "logstore_init: %s (Task 5 not yet landed)", esp_err_to_name(lr));

    esp_err_t ir = linkhost_init();
    if (ir != ESP_OK)
        ESP_LOGW(TAG, "linkhost_init: %s", esp_err_to_name(ir));

    httpd_start_and_register();

    ESP_LOGI(TAG, "dev controller ready");
}
