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

/* ---- GET /api/status: linkhost_status -> JSON ---- */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    lt_status_t st;
    int rc = linkhost_status(&st);
    char body[192];
    int n;
    if (rc != 0) {
        n = snprintf(body, sizeof body, "{\"connected\":false}");
        httpd_resp_set_status(req, "503 Service Unavailable");
    } else {
        n = snprintf(body, sizeof body,
                     "{\"connected\":true,\"proto\":%u,\"state\":%u,\"batt_pct\":%u,"
                     "\"batt_mv\":%u,\"free_kb\":%lu,\"sessions\":%u,\"fw\":\"%s\"}",
                     (unsigned)st.proto, (unsigned)st.state, (unsigned)st.batt_pct,
                     (unsigned)st.batt_mv, (unsigned long)st.free_kb, (unsigned)st.sessions,
                     st.fw);
    }
    if (n < 0 || (size_t)n >= sizeof body) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
}

/* ---- static file catch-all: serves web/ contents baked into `www` (index.html at "/"); a
 * placeholder page when the file (or the whole partition) is missing -- the SPA merges into
 * `www` in a later task. ---- */
static esp_err_t static_file_handler(httpd_req_t *req)
{
    char path[160];
    const char *uri = (strcmp(req->uri, "/") == 0) ? "/index.html" : req->uri;
    int n = snprintf(path, sizeof path, "%s%s", WWW_BASE, uri);
    if (n < 0 || (size_t)n >= sizeof path) return ESP_FAIL;

    FILE *f = fopen(path, "r");
    if (!f) {
        static const char placeholder[] =
            "<!doctype html><html><body><h1>laptimer-dev</h1>"
            "<p>www is empty (SPA not merged yet). "
            "<a href=\"/api/status\">/api/status</a></p></body></html>";
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, placeholder, sizeof placeholder - 1);
    }

    httpd_resp_set_type(req, "text/html");
    char buf[512];
    size_t rd;
    esp_err_t rc = ESP_OK;
    while ((rd = fread(buf, 1, sizeof buf, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)rd) != ESP_OK) {
            rc = ESP_FAIL;
            break;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);   /* terminate the chunked response either way */
    return rc;
}

static void httpd_start_and_register(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;   /* needed for the wildcard catch-all below */

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    /* Registered before the wildcard catch-all so the exact match wins (Task 4 moves this
     * handler into webapi_register). */
    static const httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);

    static const httpd_uri_t static_uri = {
        .uri = "/*", .method = HTTP_GET, .handler = static_file_handler,
    };
    httpd_register_uri_handler(server, &static_uri);

    esp_err_t wr = webapi_register(server);
    if (wr != ESP_OK)
        ESP_LOGW(TAG, "webapi_register: %s (Task 4 not yet landed)", esp_err_to_name(wr));
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
