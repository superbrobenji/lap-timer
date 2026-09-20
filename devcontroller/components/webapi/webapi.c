/* webapi.c -- Plan 5.5 Task 4 (CRUD half): the /api URI handlers + static SPA serving.
 *
 * A thin bridge over `linkhost` (the UART1 client to the lap-timer's export_serial console) and
 * `logstore` (B's own black-box log). The lap-timer already emits JSON for the query ops, so most
 * handlers relay a framed response body straight to the browser (design B-B, sections 3 and 7).
 * Long-lived downloads (a whole session / a log file) run on a worker task via
 * httpd_req_async_handler_begin so they never block the httpd's request task. The live-monitor
 * SSE stream and POST /api/flash are a LATER step -- not implemented here.
 *
 * Endpoints (binding contract = devcontroller/web/app.js):
 *   GET  /api/status              linkhost_status -> {connected,proto,state,flags,batt_pct,
 *                                 batt_mv,free_kb,sessions,fw}; 503 {"connected":false} if down.
 *   GET  /api/config              relay `config get` JSON.
 *   POST /api/config              diff vs a fresh `config get`, push changed keys as one-or-more
 *                                 `config set <obj>` lines; 413 if one change exceeds the cap.
 *   GET  /api/sessions            relay `list` JSON ({proto,sessions:[...]}).
 *   GET  /api/session/<id>?fmt=   async: `open <id> <fmt>` -> stream the decoded body; CRC error
 *                                 -> abort the socket.
 *   GET  /api/logs                logstore_list -> {logs:[{id,bytes}]}.
 *   GET  /api/log/<id>            async: stream a stored log file.
 *   GET  (catch-all)              static SPA from the `www` LittleFS mount (index at "/").
 */
#include "webapi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "config_diff.h"
#include "linkhost.h"
#include "logstore.h"

static const char *TAG = "webapi";

#define WWW_BASE       "/www"
#define CFG_POST_MAX   2048                 /* largest POST /api/config body accepted */
#define ASSEMBLE_MAX   (LINKHOST_ASM_MAX)   /* one framed response body (config get is ~939 B) */

/* Single-flight scratch for POST /api/config: it runs on the httpd request task, and the actual
 * `config get`/`config set` transfers are serialized by linkhost's one-request-in-flight mutex.
 * Downloads (worker task) touch none of these, so no cross-task sharing. */
static char s_post_body[CFG_POST_MAX + 1];
static char s_cur_cfg[ASSEMBLE_MAX + 1];
static char s_diff[ASSEMBLE_MAX + 1];

/* ---- async download worker ---- */
#define ASYNC_Q_LEN 4
typedef struct { httpd_req_t *req; } async_job_t;
static QueueHandle_t s_async_q;

/* ---------- small helpers ---------- */

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *json)
{
    if (status) httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* Maps a link failure to the response the SPA understands: any link error -> 503 not-connected. */
static esp_err_t send_not_connected(httpd_req_t *req)
{
    return send_json(req, "503 Service Unavailable", "{\"connected\":false}");
}

/* Copies req->uri after `prefix` up to '?' or end into out[outsz]. Returns the length, or -1. */
static int path_tail(const httpd_req_t *req, const char *prefix, char *out, size_t outsz)
{
    size_t plen = strlen(prefix);
    if (strncmp(req->uri, prefix, plen) != 0) return -1;
    const char *p = req->uri + plen;
    size_t i = 0;
    while (p[i] != '\0' && p[i] != '?' && p[i] != '#') {   /* bounded by outsz */
        if (i + 1 >= outsz) return -1;
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return (int)i;
}

/* ---------- GET /api/status ---------- */

static esp_err_t api_status(httpd_req_t *req)
{
    lt_status_t st;
    int rc = linkhost_status(&st);
    if (rc != 0) return send_not_connected(req);

    char body[224];
    int n = snprintf(body, sizeof body,
                     "{\"connected\":true,\"proto\":%u,\"state\":%u,\"flags\":%u,"
                     "\"batt_pct\":%u,\"batt_mv\":%u,\"free_kb\":%lu,\"sessions\":%u,\"fw\":\"%s\"}",
                     (unsigned)st.proto, (unsigned)st.state, (unsigned)st.flags,
                     (unsigned)st.batt_pct, (unsigned)st.batt_mv, (unsigned long)st.free_kb,
                     (unsigned)st.sessions, st.fw);
    if (n < 0 || (size_t)n >= sizeof body) return ESP_FAIL;
    return send_json(req, NULL, body);
}

/* ---------- GET /api/config : relay `config get` JSON ---------- */

static esp_err_t api_config_get(httpd_req_t *req)
{
    linkhost_frame_t f;
    int rc = linkhost_cmd(LT_CMD_CONFIG_GET, &f);
    if (rc != 0) return send_not_connected(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, (const char *)f.body, (ssize_t)f.body_len);
}

/* ---------- POST /api/config : diff + push changed keys ---------- */

static esp_err_t api_config_post(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > CFG_POST_MAX)
        return send_json(req, "413 Payload Too Large",
                         "{\"error\":\"config body too large\"}");

    /* read the desired document */
    size_t total = 0;
    while (total < req->content_len) {                         /* bounded by content_len */
        int r = httpd_req_recv(req, s_post_body + total, req->content_len - total);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }
        total += (size_t)r;
    }
    s_post_body[total] = '\0';

    /* fetch the live config (copy it out -- linkhost's body buffer is reused by the next cmd) */
    linkhost_frame_t cur;
    int rc = linkhost_cmd(LT_CMD_CONFIG_GET, &cur);
    if (rc != 0) return send_not_connected(req);
    if (cur.body_len > ASSEMBLE_MAX) return ESP_FAIL;
    memcpy(s_cur_cfg, cur.body, cur.body_len);
    s_cur_cfg[cur.body_len] = '\0';

    int dlen = config_diff_minify(s_cur_cfg, s_post_body, s_diff, sizeof s_diff);
    if (dlen < 0)
        return send_json(req, "413 Payload Too Large",
                         "{\"error\":\"a changed value exceeds the console line limit\"}");
    if (dlen <= 2)                                             /* "{}" -> nothing to change */
        return send_json(req, NULL, "{\"changed\":0}");

    /* push the changed keys, split into <=250 B `config set` lines */
    size_t cursor = 0;
    char obj[CFG_SET_OBJ_MAX + 1];
    char cmd[CFG_SET_LINE_MAX + 1];
    int lines = 0;
    for (;;) {                                                 /* bounded: cursor advances or caps */
        int m = config_diff_next_line(s_diff, &cursor, obj, sizeof obj);
        if (m == 0) break;
        if (m < 0)
            return send_json(req, "413 Payload Too Large",
                             "{\"error\":\"a changed value exceeds the console line limit\"}");
        int cn = snprintf(cmd, sizeof cmd, "%s %s", LT_CMD_CONFIG_SET, obj);
        if (cn < 0 || (size_t)cn >= sizeof cmd)
            return send_json(req, "413 Payload Too Large",
                             "{\"error\":\"config set line too long\"}");
        linkhost_frame_t ack;
        int sr = linkhost_cmd(cmd, &ack);
        if (sr != 0) return send_not_connected(req);
        if (++lines > 32) break;                               /* hard cap on lines per POST */
    }

    char resp[48];
    int rn = snprintf(resp, sizeof resp, "{\"changed\":%d}", lines);
    if (rn < 0 || (size_t)rn >= sizeof resp) return ESP_FAIL;
    return send_json(req, NULL, resp);
}

/* ---------- GET /api/sessions : relay `list` JSON ---------- */

static esp_err_t api_sessions(httpd_req_t *req)
{
    linkhost_frame_t f;
    int rc = linkhost_cmd(LT_CMD_LIST, &f);
    if (rc != 0) return send_not_connected(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, (const char *)f.body, (ssize_t)f.body_len);
}

/* ---------- GET /api/logs : logstore_list -> {logs:[{id,bytes}]} ---------- */

static esp_err_t api_logs(httpd_req_t *req)
{
    logstore_entry_t entries[16];
    int n = logstore_list(entries, (int)(sizeof entries / sizeof entries[0]));
    if (n < 0) n = 0;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"logs\":[");
    char item[96];
    for (int i = 0; i < n; i++) {                              /* bounded by n */
        int m = snprintf(item, sizeof item, "%s{\"id\":\"%s\",\"bytes\":%lu}",
                         (i ? "," : ""), entries[i].id, (unsigned long)entries[i].size);
        if (m > 0 && (size_t)m < sizeof item) httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/* ---------- async: session download ---------- */

static const char *fmt_content_type(const char *fmt)
{
    if (strcmp(fmt, LT_FMT_JSON) == 0) return "application/json";
    return "application/octet-stream";
}

static bool fmt_valid(const char *fmt)
{
    return strcmp(fmt, LT_FMT_VBO) == 0 || strcmp(fmt, LT_FMT_NMEA) == 0 ||
           strcmp(fmt, LT_FMT_JSON) == 0 || strcmp(fmt, LT_FMT_LOG) == 0 ||
           strcmp(fmt, LT_FMT_SUM) == 0;
}

static void do_session_download(httpd_req_t *req)
{
    char id[32];
    if (path_tail(req, "/api/session/", id, sizeof id) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing session id");
        return;
    }

    char fmt[8] = "vbo";                                       /* default per app.js links */
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 256) {
        char q[256];
        if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK)
            (void)httpd_query_key_value(q, "fmt", fmt, sizeof fmt);
    }
    if (!fmt_valid(fmt)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad fmt");
        return;
    }

    char cmd[64];
    int cn = snprintf(cmd, sizeof cmd, "%s %s %s", LT_CMD_OPEN, id, fmt);
    if (cn < 0 || (size_t)cn >= sizeof cmd) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad session id");
        return;
    }

    linkhost_frame_t f;
    int rc = linkhost_cmd(cmd, &f);
    if (rc == LINKHOST_E_CRC) {
        /* the CRC arrives at ---END after the body; on mismatch abort the socket so the browser
         * sees a failed/truncated download (design section 5, "Download semantics"). */
        ESP_LOGW(TAG, "session %s %s CRC mismatch -> abort socket", id, fmt);
        httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
        return;
    }
    if (rc != 0) {
        send_not_connected(req);
        return;
    }

    char disp[80];
    snprintf(disp, sizeof disp, "attachment; filename=\"session_%s.%s\"", id, fmt);
    httpd_resp_set_type(req, fmt_content_type(fmt));
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_send(req, (const char *)f.body, (ssize_t)f.body_len);
}

/* ---------- async: log file download ---------- */

static void do_log_download(httpd_req_t *req)
{
    char id[32];
    if (path_tail(req, "/api/log/", id, sizeof id) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing log id");
        return;
    }

    logstore_file_t fd;
    if (logstore_open_read(id, &fd) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such log");
        return;
    }

    char disp[64];
    snprintf(disp, sizeof disp, "attachment; filename=\"%s.log\"", id);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    char buf[512];
    for (;;) {                                                 /* bounded by file size */
        ssize_t r = read(fd, buf, sizeof buf);
        if (r <= 0) break;
        if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) break;
    }
    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);
}

/* ---------- async plumbing ---------- */

static void async_worker(void *arg)
{
    (void)arg;
    for (;;) {
        async_job_t job;
        if (xQueueReceive(s_async_q, &job, portMAX_DELAY) != pdTRUE) continue;
        httpd_req_t *req = job.req;
        if (strncmp(req->uri, "/api/session/", 13) == 0) do_session_download(req);
        else if (strncmp(req->uri, "/api/log/", 9) == 0)  do_log_download(req);
        else httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        httpd_req_async_handler_complete(req);
    }
}

/* GET handler for the async endpoints: hand the request to the worker task and return so the
 * httpd request task is never blocked by the transfer. */
static esp_err_t api_async_begin(httpd_req_t *req)
{
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return ESP_FAIL;

    async_job_t job = { .req = copy };
    if (xQueueSend(s_async_q, &job, 0) != pdTRUE) {
        send_json(copy, "503 Service Unavailable", "{\"error\":\"download queue full\"}");
        httpd_req_async_handler_complete(copy);
    }
    return ESP_OK;
}

/* ---------- static SPA ---------- */

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".js") == 0)   return "text/javascript";
    if (strcmp(dot, ".css") == 0)  return "text/css";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0)  return "image/x-icon";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    return "application/octet-stream";
}

static esp_err_t static_file(httpd_req_t *req)
{
    if (strncmp(req->uri, "/api/", 5) == 0) {                  /* unmatched /api path -> 404 */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such endpoint");
        return ESP_FAIL;
    }

    char path[160];
    const char *uri = (strcmp(req->uri, "/") == 0) ? "/index.html" : req->uri;
    int n = snprintf(path, sizeof path, "%s%s", WWW_BASE, uri);
    if (n < 0 || (size_t)n >= sizeof path) return ESP_FAIL;
    char *q = strchr(path, '?');                               /* drop any query string */
    if (q) *q = '\0';

    FILE *fp = fopen(path, "r");
    if (!fp) {
        static const char placeholder[] =
            "<!doctype html><html><body><h1>laptimer-dev</h1>"
            "<p>SPA asset not found. <a href=\"/api/status\">/api/status</a></p></body></html>";
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, placeholder, HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, mime_for(path));
    char buf[1024];
    size_t rd;
    esp_err_t rc = ESP_OK;
    while ((rd = fread(buf, 1, sizeof buf, fp)) > 0) {         /* bounded by file size */
        if (httpd_resp_send_chunk(req, buf, (ssize_t)rd) != ESP_OK) { rc = ESP_FAIL; break; }
    }
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0);
    return rc;
}

/* ---------- registration ---------- */

esp_err_t webapi_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    if (!s_async_q) {
        s_async_q = xQueueCreate(ASYNC_Q_LEN, sizeof(async_job_t));
        if (!s_async_q) return ESP_ERR_NO_MEM;
        if (xTaskCreate(async_worker, "webapi_async", 8192, NULL, 5, NULL) != pdPASS)
            return ESP_ERR_NO_MEM;
    }

    /* Order matters: exact/specific /api paths first, the wildcard SPA catch-all last (the wildcard
     * match fn returns the first registered handler that matches the URI). */
    const httpd_uri_t uris[] = {
        { .uri = "/api/status",     .method = HTTP_GET,  .handler = api_status },
        { .uri = "/api/config",     .method = HTTP_GET,  .handler = api_config_get },
        { .uri = "/api/config",     .method = HTTP_POST, .handler = api_config_post },
        { .uri = "/api/sessions",   .method = HTTP_GET,  .handler = api_sessions },
        { .uri = "/api/session/*",  .method = HTTP_GET,  .handler = api_async_begin },
        { .uri = "/api/logs",       .method = HTTP_GET,  .handler = api_logs },
        { .uri = "/api/log/*",      .method = HTTP_GET,  .handler = api_async_begin },
        { .uri = "/*",              .method = HTTP_GET,  .handler = static_file },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "registered %u handlers", (unsigned)(sizeof uris / sizeof uris[0]));
    return ESP_OK;
}
