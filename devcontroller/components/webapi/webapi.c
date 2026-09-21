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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "config_diff.h"
#include "linkhost.h"
#include "linkhost_proto.h"
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

/* ---- live-monitor broadcast: the consumer task pushes here; ONE SSE reader tracks a cursor. ---- */
#define BC_CAP 64u                    /* power of two */
static lt_stream_rec_t s_bc[BC_CAP];
static volatile uint32_t s_bc_head;   /* total pushed; publish AFTER the store */
static volatile bool s_sse_active;    /* one live monitor at a time */

void webapi_stream_push(const lt_stream_rec_t *r)
{
    if (!r) return;
    uint32_t h = s_bc_head;
    s_bc[h & (BC_CAP - 1u)] = *r;
    s_bc_head = h + 1u;
}

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

/* Sends {"error":"<msg>"} with `status`, escaping the message (control chars dropped, JSON
 * metacharacters escaped) so an odd remote message can't break the JSON handed to the SPA. */
static esp_err_t send_error_json(httpd_req_t *req, const char *status, const char *msg)
{
    char esc[LINKHOST_ERRMSG_MAX * 2 + 1];
    size_t o = 0;
    for (const char *p = msg; p && *p && o + 2u < sizeof esc; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { esc[o++] = '\\'; esc[o++] = (char)c; }
        else if (c >= 0x20 && c < 0x7F) esc[o++] = (char)c;   /* drop control / non-ASCII */
    }
    esc[o] = '\0';
    char body[LINKHOST_ERRMSG_MAX * 2 + 32];
    int n = snprintf(body, sizeof body, "{\"error\":\"%s\"}", esc);
    if (n < 0 || (size_t)n >= sizeof body) return ESP_FAIL;
    return send_json(req, status, body);
}

/* Response mapping for the small fixed linkhost_cmd ops (config get/set, status): a remote
 * `ERR 0x..` -> 400 with the lap-timer's message; a busy link -> 503; anything else -> the SPA's
 * not-connected shape. `f` carries err_msg when rc == LINKHOST_E_REMOTE. */
static esp_err_t send_cmd_link_error(httpd_req_t *req, int rc, const linkhost_frame_t *f)
{
    if (rc == LINKHOST_E_REMOTE)
        return send_error_json(req, "400 Bad Request",
                               f->err_msg[0] ? f->err_msg : "lap-timer rejected the request");
    if (rc == LINKHOST_E_BUSY)
        return send_json(req, "503 Service Unavailable", "{\"error\":\"link busy, retry\"}");
    return send_not_connected(req);   /* TIMEOUT / NOTCONN / CRC / PROTO */
}

/* Response mapping for a streaming relay that failed BEFORE any body byte was sent: a remote error
 * -> 502 for a storage code (0x04xx), else 404 (unknown session/file); CRC/PROTO -> 502; busy ->
 * 503; anything else -> not-connected. */
static void send_stream_error(httpd_req_t *req, int rc, const linkhost_remote_err_t *rerr)
{
    if (rc == LINKHOST_E_REMOTE) {
        const char *status = ((rerr->code & 0xFF00u) == 0x0400u) ? "502 Bad Gateway" : "404 Not Found";
        send_error_json(req, status, rerr->msg[0] ? rerr->msg : "lap-timer error");
    } else if (rc == LINKHOST_E_CRC || rc == LINKHOST_E_PROTO) {
        send_json(req, "502 Bad Gateway", "{\"error\":\"read failed\"}");
    } else if (rc == LINKHOST_E_BUSY) {
        send_json(req, "503 Service Unavailable", "{\"error\":\"link busy, retry\"}");
    } else {
        send_not_connected(req);
    }
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
    if (rc != 0) return send_cmd_link_error(req, rc, &f);
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
    if (rc != 0) return send_cmd_link_error(req, rc, &cur);
    if (cur.body_len > ASSEMBLE_MAX) return ESP_FAIL;
    memcpy(s_cur_cfg, cur.body, cur.body_len);
    s_cur_cfg[cur.body_len] = '\0';

    int dlen = config_diff_minify(s_cur_cfg, s_post_body, s_diff, sizeof s_diff);
    if (dlen < 0)
        return send_json(req, "413 Payload Too Large",
                         "{\"error\":\"a changed value exceeds the console line limit\"}");
    if (dlen <= 2)                                             /* "{}" -> nothing to change */
        return send_json(req, NULL, "{\"changed\":0}");

    /* Console-injection guard (T-F): the minified diff copies string values verbatim, so a raw
     * byte < 0x20 here came from inside a JSON string (jsmn accepts control chars) and would split
     * the `config set` line into extra commands on the lap-timer. Reject the whole POST. */
    for (int i = 0; i < dlen; i++) {
        if ((unsigned char)s_diff[i] < 0x20)
            return send_json(req, "400 Bad Request",
                             "{\"error\":\"control character in config value\"}");
    }

    /* push the changed keys, split into `config set` lines each <=250 B AFTER escaping */
    size_t cursor = 0;
    char obj[CFG_SET_OBJ_MAX + 1];
    char esc[CFG_SET_OBJ_MAX * 2 + 1];                         /* escaped object: each byte can double */
    char cmd[CFG_SET_LINE_MAX + 1];
    int lines = 0;
    for (;;) {                                                 /* bounded: cursor advances or caps */
        int m = config_diff_next_line(s_diff, &cursor, obj, sizeof obj);
        if (m == 0) break;
        if (m < 0)
            return send_json(req, "413 Payload Too Large",
                             "{\"error\":\"a changed value exceeds the console line limit\"}");
        /* Escape the object for esp_console (B1): a bare {"k":"v"} has its quotes stripped by
         * esp_console_split_argv -> malformed JSON at the lap-timer. next_line budgeted the escaped
         * length, so `config set <escaped>` stays within the 256 B console line limit. */
        int en = config_diff_escape(obj, esc, sizeof esc);
        if (en < 0)
            return send_json(req, "413 Payload Too Large",
                             "{\"error\":\"a changed value exceeds the console line limit\"}");
        int cn = snprintf(cmd, sizeof cmd, "%s %s", LT_CMD_CONFIG_SET, esc);
        if (cn < 0 || (size_t)cn >= sizeof cmd)
            return send_json(req, "413 Payload Too Large",
                             "{\"error\":\"config set line too long\"}");
        linkhost_frame_t ack;
        int sr = linkhost_cmd(cmd, &ack);
        if (sr != 0) return send_cmd_link_error(req, sr, &ack);
        if (++lines > 32) break;                               /* hard cap on lines per POST */
    }

    char resp[48];
    int rn = snprintf(resp, sizeof resp, "{\"changed\":%d}", lines);
    if (rn < 0 || (size_t)rn >= sizeof resp) return ESP_FAIL;
    return send_json(req, NULL, resp);
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

/* linkhost_download streaming sink: relays each decoded block to the browser as an HTTP chunk. It
 * lives on do_session_download's stack for the whole transfer, so `disp` stays valid for
 * httpd_resp_set_hdr (which keeps the pointer, not a copy) until the response is sent. */
typedef struct {
    httpd_req_t *req;
    const char  *id;
    const char  *fmt;
    char         disp[96];
    bool         headers_set;
    bool         started;         /* a body chunk was actually sent -> the point of no return */
    bool         transport_dead;  /* httpd_resp_send_chunk failed -> the socket is already gone */
} dl_sink_t;

/* Commits the download headers exactly once, before the first chunk goes out (chunked responses
 * emit headers with the first send_chunk). */
static void dl_set_headers(dl_sink_t *s)
{
    if (s->headers_set) return;
    snprintf(s->disp, sizeof s->disp, "attachment; filename=\"session_%s.%s\"", s->id, s->fmt);
    httpd_resp_set_type(s->req, fmt_content_type(s->fmt));
    httpd_resp_set_hdr(s->req, "Content-Disposition", s->disp);
    s->headers_set = true;
}

static int session_chunk_cb(void *ctx, const uint8_t *data, size_t n)
{
    dl_sink_t *s = (dl_sink_t *)ctx;
    dl_set_headers(s);
    if (httpd_resp_send_chunk(s->req, (const char *)data, (ssize_t)n) != ESP_OK) {
        s->transport_dead = true;
        return 1;                                              /* abort: the client disconnected */
    }
    s->started = true;
    return 0;
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

    /* Stream the framed response body straight to the browser -- never buffer the whole file, so a
     * KB..MB session survives (the old linkhost_cmd path capped at LINKHOST_ASM_MAX = 1024 B). */
    dl_sink_t sink = { .req = req, .id = id, .fmt = fmt };
    linkhost_remote_err_t rerr;
    int rc = linkhost_download(id, fmt, session_chunk_cb, &sink, &rerr);

    if (sink.transport_dead) {                                 /* client vanished mid-stream */
        ESP_LOGW(TAG, "session %s %s: client disconnected mid-download", id, fmt);
        return;                                                /* socket gone; async just completes */
    }
    if (rc == 0) {
        dl_set_headers(&sink);                                 /* also covers an empty-body session */
        httpd_resp_send_chunk(req, NULL, 0);                   /* terminate the chunked response */
        return;
    }
    if (sink.started) {
        /* A CRC/proto failure surfaces only at ---END, AFTER the body has streamed. The bytes
         * already sent cannot be un-sent (httpd_resp_send_chunk(req, NULL, 0) would just cleanly
         * finish a valid-looking response), so abort the socket -- the browser then sees a
         * truncated/failed download instead of a complete but corrupt file. */
        ESP_LOGW(TAG, "session %s %s rc=%d after streaming -> abort socket", id, fmt, rc);
        httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
        return;
    }
    /* Nothing streamed yet -> a clean error response is still possible (404/502/503). */
    send_stream_error(req, rc, &rerr);
}

/* ---------- async: GET /api/sessions (stream `list` JSON) ----------
 * `list` is JSON but its frame (measured 3662 B on hardware, growing with session count) overflows
 * the buffered linkhost_cmd/LINKHOST_ASM_MAX path -> it used to 503 past ~6 sessions. Stream it
 * through lh_dl_* exactly like a session download (raw text, not base64). */
static int sessions_chunk_cb(void *ctx, const uint8_t *data, size_t n)
{
    dl_sink_t *s = (dl_sink_t *)ctx;
    if (!s->headers_set) { httpd_resp_set_type(s->req, "application/json"); s->headers_set = true; }
    if (httpd_resp_send_chunk(s->req, (const char *)data, (ssize_t)n) != ESP_OK) {
        s->transport_dead = true;
        return 1;                                              /* abort: the client disconnected */
    }
    s->started = true;
    return 0;
}

static void do_sessions_stream(httpd_req_t *req)
{
    dl_sink_t sink = { .req = req };
    linkhost_remote_err_t rerr;
    int rc = linkhost_download_cmd(LT_CMD_LIST, /*is_binary*/false, sessions_chunk_cb, &sink, &rerr);

    if (sink.transport_dead) {
        ESP_LOGW(TAG, "sessions: client disconnected mid-stream");
        return;
    }
    if (rc == 0) {
        if (!sink.headers_set) httpd_resp_set_type(req, "application/json");  /* empty list */
        httpd_resp_send_chunk(req, NULL, 0);
        return;
    }
    if (sink.started) {
        ESP_LOGW(TAG, "sessions rc=%d after streaming -> abort socket", rc);
        httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
        return;
    }
    send_stream_error(req, rc, &rerr);
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

/* ---------- live monitor: GET /api/stream (SSE) ---------- */

/* SSE loop: stream new broadcast records as `data: {json}\n\n`, with a periodic heartbeat comment
 * so a dead client is detected via send failure. Runs on its own task (not the shared download
 * worker) so an open monitor never starves session/log downloads. */
static void do_stream(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (httpd_resp_send_chunk(req, "retry: 3000\n\n", 13) != ESP_OK) return;
    uint32_t cursor = s_bc_head;                 /* start at newest; skip any backlog */
    int64_t last_beat = esp_timer_get_time();
    char line[320];
    for (;;) {                                   /* until the client disconnects (send fails) */
        int sent = 0;
        while (cursor != s_bc_head && sent < 32) {
            if ((uint32_t)(s_bc_head - cursor) > BC_CAP) cursor = s_bc_head - BC_CAP;  /* overrun: skip */
            lt_stream_rec_t rec = s_bc[cursor & (BC_CAP - 1u)];
            cursor++;
            int jn = linkhost_stream_to_json(&rec, line + 6, sizeof(line) - 12);
            if (jn > 0) {
                memcpy(line, "data: ", 6);
                line[6 + jn] = '\n';
                line[7 + jn] = '\n';
                if (httpd_resp_send_chunk(req, line, (ssize_t)(8 + jn)) != ESP_OK) return;
                sent++;
            }
        }
        int64_t now = esp_timer_get_time();
        if (now - last_beat > 12000000) {         /* 12s keepalive comment */
            if (httpd_resp_send_chunk(req, ": beat\n\n", 8) != ESP_OK) return;
            last_beat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static void sse_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    do_stream(req);
    httpd_req_async_handler_complete(req);
    s_sse_active = false;
    vTaskDelete(NULL);
}

/* GET /api/stream: one live monitor at a time; runs on a dedicated task. */
static esp_err_t api_stream_begin(httpd_req_t *req)
{
    if (s_sse_active) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"monitor already active\"}");
    }
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return ESP_FAIL;
    s_sse_active = true;
    if (xTaskCreate(sse_task, "webapi_sse", 4096, copy, 5, NULL) != pdPASS) {
        s_sse_active = false;
        httpd_req_async_handler_complete(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------- async plumbing ---------- */

static void async_worker(void *arg)
{
    (void)arg;
    for (;;) {
        async_job_t job;
        if (xQueueReceive(s_async_q, &job, portMAX_DELAY) != pdTRUE) continue;
        httpd_req_t *req = job.req;
        if (strcmp(req->uri, "/api/sessions") == 0)        do_sessions_stream(req);
        else if (strncmp(req->uri, "/api/session/", 13) == 0) do_session_download(req);
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
        { .uri = "/api/sessions",   .method = HTTP_GET,  .handler = api_async_begin },
        { .uri = "/api/session/*",  .method = HTTP_GET,  .handler = api_async_begin },
        { .uri = "/api/logs",       .method = HTTP_GET,  .handler = api_logs },
        { .uri = "/api/log/*",      .method = HTTP_GET,  .handler = api_async_begin },
        { .uri = "/api/stream",     .method = HTTP_GET,  .handler = api_stream_begin },
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
