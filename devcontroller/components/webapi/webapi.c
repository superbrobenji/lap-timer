/* webapi.c -- Plan 5.5 Tasks 4/5/6: the /api URI handlers + static SPA serving.
 *
 * A thin bridge over `linkhost` (the UART1 client to the lap-timer's export_serial console) and
 * `logstore` (B's own black-box log). The lap-timer already emits JSON for the query ops, so most
 * handlers relay a framed response body straight to the browser (design B-B, sections 3 and 7).
 * Long-lived downloads (a whole session / a log file) run on a worker task via
 * httpd_req_async_handler_begin so they never block the httpd's request task; the live monitor and
 * the cmd-OTA flash push each get a dedicated one-shot task for the same reason.
 *
 * Endpoints (binding contract = devcontroller/web/app.js):
 *   GET  /api/status              linkhost_status -> {connected,proto,state,flags,batt_pct,
 *                                 batt_mv,free_kb,sessions,fw}; 503 {"connected":false} if down.
 *                                 While a flash push runs: {connected,flashing,flash_pct}; after a
 *                                 failed push the normal body also carries "flash_err".
 *   GET  /api/config              relay `config get` JSON.
 *   POST /api/config              diff vs a fresh `config get`, push changed keys as one-or-more
 *                                 `config set <obj>` lines; 413 if one change exceeds the cap.
 *   GET  /api/sessions            relay `list` JSON ({proto,sessions:[...]}).
 *   GET  /api/session/<id>?fmt=   async: `open <id> <fmt>` -> stream the decoded body; CRC error
 *                                 -> abort the socket.
 *   GET  /api/logs                logstore_list -> {logs:[{id,bytes}]}.
 *   GET  /api/log/<id>?fmt=       async: stream a stored log file. Default fmt=jsonl transcodes
 *                                 each on-flash record to NDJSON (application/x-ndjson,
 *                                 reusing linkhost_stream_to_json via logstore_rec_to_json);
 *                                 fmt=bin streams the raw file verbatim (application/octet-stream).
 *   GET  /api/stream              live-monitor SSE (one reader at a time), on its own task.
 *   POST /api/flash               multipart/form-data `firmware` -> stream into the `ota_stage`
 *                                 partition, then push it cmd-OTA on a worker task; 202 as soon
 *                                 as the image is staged, progress via GET /api/status.
 *   GET  (catch-all)              static SPA from the `www` LittleFS mount (index at "/").
 */
#include "webapi.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include "config_diff.h"
#include "image_desc.h"
#include "linkhost.h"
#include "linkhost_proto.h"
#include "logstore.h"
#include "logstore_rec.h"
#include "multipart.h"

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

/* ---- cmd-OTA flash (POST /api/flash): one at a time, staged then pushed ----
 * `busy` covers the whole operation, from the first upload byte to the end of the push, so a
 * second POST is refused rather than corrupting the staging partition. The fields the push task
 * reads are written only while `busy` is true and the task does not exist yet. */
typedef enum {
    FLASH_IDLE = 0,
    FLASH_STAGING,      /* the multipart body is streaming into `ota_stage` */
    FLASH_PUSHING,      /* the worker task owns the link (linkhost_flash) */
    FLASH_DONE_OK,
    FLASH_DONE_ERR
} flash_state_t;

static struct {
    volatile bool          busy;
    volatile flash_state_t state;
    volatile uint32_t      sent;    /* bytes pushed to the lap-timer so far */
    volatile uint32_t      total;   /* staged image size, for the percentage */
    volatile int           result;  /* linkhost_flash rc; meaningful in FLASH_DONE_* */
    uint32_t               size;
    uint8_t                sha[32];
    char                   ver[IMG_VER_LEN];
    char                   hwid[IMG_HWID_LEN + 1];
} s_flash;

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
    /* While the push owns the link, answer from our own state: linkhost_flash holds the request
     * mutex (portMAX_DELAY) and pauses the RX demux for the whole transfer, so a `status`
     * round-trip here would just burn the bounded mutex take and come back BUSY. */
    if (s_flash.state == FLASH_PUSHING) {
        uint32_t total = s_flash.total;
        uint32_t sent  = s_flash.sent;
        unsigned pct = (total > 0u) ? (unsigned)(((uint64_t)sent * 100u) / total) : 0u;
        if (pct > 100u) pct = 100u;
        char pbody[64];
        int pn = snprintf(pbody, sizeof pbody,
                          "{\"connected\":true,\"flashing\":true,\"flash_pct\":%u}", pct);
        if (pn < 0 || (size_t)pn >= sizeof pbody) return ESP_FAIL;
        return send_json(req, NULL, pbody);
    }

    lt_status_t st;
    int rc = linkhost_status(&st);
    if (rc != 0) return send_not_connected(req);

    /* A successful push reboots the lap-timer, so the SPA sees a 503 and then a reconnect. A
     * FAILED one leaves it running, so the failure has to ride the next 200 instead -- it stays
     * here until the next POST /api/flash clears it. */
    char ferr[40] = "";
    if (s_flash.state == FLASH_DONE_ERR) {
        int frc = s_flash.result;
        if (frc > 0) snprintf(ferr, sizeof ferr, ",\"flash_err\":\"0x%04x\"", (unsigned)frc);
        else         snprintf(ferr, sizeof ferr, ",\"flash_err\":\"link %d\"", frc);
    }

    char body[288];
    int n = snprintf(body, sizeof body,
                     "{\"connected\":true,\"proto\":%u,\"state\":%u,\"flags\":%u,"
                     "\"batt_pct\":%u,\"batt_mv\":%u,\"free_kb\":%lu,\"sessions\":%u,"
                     "\"fw\":\"%s\"%s}",
                     (unsigned)st.proto, (unsigned)st.state, (unsigned)st.flags,
                     (unsigned)st.batt_pct, (unsigned)st.batt_mv, (unsigned long)st.free_kb,
                     (unsigned)st.sessions, st.fw, ferr);
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

/* ---------- async: log file download (issue #67: raw or transcoded NDJSON) ---------- */

/* Sliding read buffer for the jsonl transcode: must hold at least one max-size on-flash record
 * (logstore_rec_hdr_t + LT_REC_MAX payload) so logstore_rec_to_json always has a chance to make
 * forward progress once it has seen a whole record -- a load-bearing bound, not just a
 * convenient buffer size (pragmatic-P10: explicit cap). */
#define LOG_JSONL_BUF (2u * 512u)
_Static_assert(LOG_JSONL_BUF >= sizeof(logstore_rec_hdr_t) + LT_REC_MAX,
               "LOG_JSONL_BUF must hold at least one max-size logstore record");

/* One transcoded record's JSON text; linkhost_stream_to_json's largest object (the fused-sample
 * line) is well under 200 B. */
#define LOG_JSON_LINE_MAX 256u

/* Defensive loop cap for stream_log_jsonl's outer for(;;): each pass either reads more bytes or
 * consumes >=1 buffered record, so this bounds the whole transfer by (worst case) one pass per
 * minimum-size (header-only) record in the largest possible log file, generously rounded up --
 * never expected to trip, but pragmatic-P10 wants an explicit bound articulated, not an unbounded
 * loop trusting the file to be well-formed. */
#define LOG_JSONL_MAX_ITERS 200000u

/* Reads `fd` (already positioned at the first on-flash record -- LOGSTORE_REC_AREA_OFFSET past
 * the file's own header) to EOF, transcoding each complete record to an NDJSON line via
 * logstore_rec_to_json and sending it as an HTTP chunk. Stops early on a send failure (client
 * gone), a malformed record (LOGSTORE_JSON_ERR), or a truncated tail at EOF. */
static void stream_log_jsonl(httpd_req_t *req, int fd, const char *id)
{
    assert(req != NULL);
    assert(fd >= 0);
    assert(id != NULL);

    char   buf[LOG_JSONL_BUF];
    char   json[LOG_JSON_LINE_MAX];
    size_t len  = 0;       /* bytes buffered at buf[0..len) */
    bool   eof  = false;

    for (uint32_t iter = 0; iter < LOG_JSONL_MAX_ITERS; iter++) {
        assert(len <= sizeof buf);
        if (!eof && len < sizeof buf) {
            ssize_t r = read(fd, buf + len, sizeof buf - len);
            if (r < 0) { ESP_LOGW(TAG, "log %s jsonl: read: %s", id, strerror(errno)); break; }
            if (r == 0) eof = true;
            else        len += (size_t)r;
        }

        size_t consumed = 0;
        int n = logstore_rec_to_json((const uint8_t *)buf, len, json, sizeof json, &consumed);

        if (n > 0) {
            if (httpd_resp_send_chunk(req, json, n) != ESP_OK) break;
            if (httpd_resp_send_chunk(req, "\n", 1) != ESP_OK) break;
            memmove(buf, buf + consumed, len - consumed);
            len -= consumed;
        } else if (n == LOGSTORE_JSON_SKIP) {
            memmove(buf, buf + consumed, len - consumed);
            len -= consumed;
        } else if (n == LOGSTORE_JSON_NEED_MORE) {
            if (eof || len == sizeof buf) break;   /* truncated tail, or a record that can't fit */
            /* else: loop back around and read more into buf[len..) */
        } else {                                    /* LOGSTORE_JSON_ERR: len > LT_REC_MAX */
            ESP_LOGW(TAG, "log %s jsonl: malformed record, stopping stream early", id);
            break;
        }
    }
}

static void do_log_download(httpd_req_t *req)
{
    assert(req != NULL);

    char id[32];
    if (path_tail(req, "/api/log/", id, sizeof id) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing log id");
        return;
    }
    assert(id[0] != '\0');

    char fmt[8] = "jsonl";                                     /* default: transcoded NDJSON */
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 256) {
        char q[256];
        if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK)
            (void)httpd_query_key_value(q, "fmt", fmt, sizeof fmt);
    }
    bool raw = (strcmp(fmt, "bin") == 0);
    if (!raw && strcmp(fmt, "jsonl") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad fmt");
        return;
    }

    logstore_file_t fd;
    if (logstore_open_read(id, &fd) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such log");
        return;
    }

    char disp[64];
    if (raw) {
        snprintf(disp, sizeof disp, "attachment; filename=\"%s.log\"", id);
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", disp);

        char buf[512];
        for (;;) {                                             /* bounded by file size */
            ssize_t r = read(fd, buf, sizeof buf);
            if (r <= 0) break;
            if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) break;
        }
    } else {
        snprintf(disp, sizeof disp, "attachment; filename=\"%s.jsonl\"", id);
        httpd_resp_set_type(req, "application/x-ndjson");
        httpd_resp_set_hdr(req, "Content-Disposition", disp);

        /* Skip the file's own logstore_file_hdr_t (magic + created_unix) -- only the raw ?fmt=bin
         * path includes it verbatim; the record parser must not see it as a fake first record. */
        if (lseek(fd, LOGSTORE_REC_AREA_OFFSET, SEEK_SET) < 0) {
            ESP_LOGW(TAG, "log %s jsonl: lseek: %s", id, strerror(errno));
            close(fd);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "seek failed");
            return;
        }
        stream_log_jsonl(req, fd, id);
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
        int iter = 0;   /* M1: `sent` only counts successfully-decoded records, so bound the loop
                          * itself too -- a run of records that all fail to decode must not spin
                          * past one ring's worth of work per wake. */
        while (cursor != s_bc_head && sent < 32 && iter < BC_CAP) {
            iter++;
            /* M2: on overrun, `s_bc_head - BC_CAP` is congruent mod BC_CAP to `s_bc_head` itself --
             * i.e. the exact slot the producer writes next -- so landing there is a guaranteed
             * torn read against a live producer. +2 skips it plus one extra record of margin. */
            if ((uint32_t)(s_bc_head - cursor) > BC_CAP) cursor = s_bc_head - BC_CAP + 2u;  /* overrun: skip */
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
    /* M3: clear BEFORE completing the request. httpd_req_async_handler_complete() is what lets a
     * new /api/stream request land; clearing the flag after it would leave a window where a
     * request accepted in-between sets s_sse_active true and this dying task then immediately
     * clears it, leaving two monitor tasks running concurrently. */
    s_sse_active = false;
    httpd_req_async_handler_complete(req);
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

/* ---------- POST /api/flash : stage the upload, then push it cmd-OTA ----------
 * Two halves. The upload half runs on the httpd request task (it needs httpd_req_recv) and
 * streams the multipart body straight into the `ota_stage` partition -- a 1.2 MB image never fits
 * in RAM -- while hashing it. The push half (linkhost_flash) runs on a one-shot worker task,
 * because it takes the link mutex with portMAX_DELAY and pauses the RX demux for ~110 s: doing
 * that on the httpd task would freeze the whole SPA, including the /api/status poll that reports
 * the progress. The handler therefore answers 202 as soon as the image is staged. */

#define FLASH_FIELD          "firmware"   /* the multipart field name app.js posts */
#define UPLOAD_BUF           1024         /* one httpd_req_recv chunk */
#define STAGE_BLOCK          4096         /* flash sector: the erase+write granularity */
#define UPLOAD_TIMEOUTS_MAX  10           /* consecutive HTTPD_SOCK_ERR_TIMEOUT retries */
#define DRAIN_READS_MAX      8192         /* cap on the reads used to drain a rejected body */

/* Single-flight scratch, guarded by s_flash.busy. The 4 KB block buffer must not live on the
 * httpd task's stack, and nothing here is ever touched by two tasks at once. */
static char       s_up_buf[UPLOAD_BUF];
static uint8_t    s_stage_blk[STAGE_BLOCK];
static mp_ctx_t   s_mp;

typedef enum { STAGE_OK = 0, STAGE_FULL, STAGE_WRITE } stage_err_t;

typedef struct {
    const esp_partition_t *part;
    uint32_t               written;   /* bytes committed to flash (always a multiple of STAGE_BLOCK) */
    size_t                 blk_len;   /* bytes pending in s_stage_blk */
    stage_err_t            err;
    mbedtls_sha256_context sha;
} stage_ctx_t;

static stage_ctx_t s_stage;

/* Erases the sector at s->written and writes the first `len` bytes of the block buffer into it.
 * Lazy per-sector erase: erasing the whole 1.25 MB partition up front would stall the upload for
 * seconds and TCP would time out. */
static int stage_commit(stage_ctx_t *s, size_t len)
{
    assert(s != NULL && s->part != NULL);
    assert(len > 0u && len <= (size_t)STAGE_BLOCK);
    assert((s->written % (uint32_t)STAGE_BLOCK) == 0u);          /* erase_range needs alignment */

    if (esp_partition_erase_range(s->part, s->written, STAGE_BLOCK) != ESP_OK) return -1;
    if (esp_partition_write(s->part, s->written, s_stage_blk, len) != ESP_OK) return -1;
    s->written += (uint32_t)len;
    return 0;
}

/* The multipart sink: buffer the wanted part's bytes a sector at a time into `ota_stage`, hashing
 * exactly what gets staged. Returns nonzero to abort the parse (-> MP_E_SINK), with the reason in
 * s->err so the handler can pick 413 vs 500. */
static int stage_sink(void *ctx, const uint8_t *data, size_t n)
{
    stage_ctx_t *s = (stage_ctx_t *)ctx;
    assert(s != NULL && s->part != NULL);
    assert(data != NULL);

    if ((uint64_t)s->written + s->blk_len + n > (uint64_t)s->part->size) {
        s->err = STAGE_FULL;
        return 1;
    }
    if (mbedtls_sha256_update(&s->sha, data, n) != 0) {
        s->err = STAGE_WRITE;
        return 1;
    }
    size_t off = 0;
    while (off < n) {                                            /* bounded by n */
        size_t room = (size_t)STAGE_BLOCK - s->blk_len;
        size_t take = ((n - off) < room) ? (n - off) : room;
        memcpy(s_stage_blk + s->blk_len, data + off, take);
        s->blk_len += take;
        off += take;
        if (s->blk_len == (size_t)STAGE_BLOCK) {
            if (stage_commit(s, (size_t)STAGE_BLOCK) != 0) { s->err = STAGE_WRITE; return 1; }
            s->blk_len = 0;
        }
    }
    return 0;
}

/* Drains up to `left` unread request bytes before a rejection is sent. httpd_req_delete purges
 * only CONFIG_HTTPD_PURGE_BUF_LEN (32 B) per call, so refusing a 1 MB upload without draining
 * would leave the rest of the body on the socket to be misread as the next request. */
static void flash_drain(httpd_req_t *req, size_t left)
{
    int timeouts = 0;
    for (int i = 0; i < DRAIN_READS_MAX && left > 0u; i++) {      /* bounded both ways */
        size_t want = (left < sizeof s_up_buf) ? left : sizeof s_up_buf;
        int r = httpd_req_recv(req, s_up_buf, want);
        if (r > 0) { left -= (size_t)r; timeouts = 0; continue; }
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= UPLOAD_TIMEOUTS_MAX) continue;
        break;
    }
}

static void flash_progress(uint32_t sent, uint32_t total, void *ctx)
{
    (void)ctx;
    s_flash.sent  = sent;
    s_flash.total = total;
}

/* The push half. Owns the link for the whole transfer, then publishes the outcome for
 * GET /api/status and releases the single-flight guard. */
static void flash_task(void *arg)
{
    (void)arg;
    int rc = linkhost_flash(s_flash.ver, s_flash.hwid, s_flash.size, s_flash.sha,
                            flash_progress, NULL);
    s_flash.result = rc;
    s_flash.state  = (rc == 0) ? FLASH_DONE_OK : FLASH_DONE_ERR;
    s_flash.busy   = false;
    ESP_LOGI(TAG, "flash push of %lu B finished rc=%d", (unsigned long)s_flash.size, rc);
    vTaskDelete(NULL);
}

/* Streams the multipart body into `ota_stage` and, on success, fills s_flash.{size,sha,ver,hwid}.
 * Returns NULL on success; otherwise the error message, with *status set to the HTTP status and
 * *left left holding the body bytes still unread (for the caller to drain). */
static const char *flash_stage_body(httpd_req_t *req, size_t *left, const char **status)
{
    assert(req != NULL);
    assert(left != NULL && status != NULL);
    /* Only api_flash_post reaches here, and only after it has claimed the single flight and reset
     * the staging context. A partly-used s_stage would mean a second, overlapping upload -- which
     * would interleave two images in the partition instead of failing. */
    assert(s_flash.busy && s_flash.state == FLASH_STAGING);
    assert(s_stage.part != NULL && s_stage.written == 0u && s_stage.blk_len == 0u);

    const char *emsg = NULL;
    int mrc = MP_MORE;
    int timeouts = 0;

    mbedtls_sha256_init(&s_stage.sha);
    if (mbedtls_sha256_starts(&s_stage.sha, 0) != 0) {
        *status = "500 Internal Server Error";
        emsg = "sha init failed";
    }

    while (emsg == NULL && *left > 0u) {                          /* bounded by content_len */
        size_t want = (*left < sizeof s_up_buf) ? *left : sizeof s_up_buf;
        int r = httpd_req_recv(req, s_up_buf, want);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= UPLOAD_TIMEOUTS_MAX) continue;
            *status = "400 Bad Request";
            emsg = "upload aborted";
            *left = 0u;                                           /* the socket is gone: no drain */
            break;
        }
        timeouts = 0;
        *left -= (size_t)r;
        mrc = mp_feed(&s_mp, (const uint8_t *)s_up_buf, (size_t)r, stage_sink, &s_stage);
        if (mrc == MP_E_SINK) {
            *status = (s_stage.err == STAGE_FULL) ? "413 Payload Too Large"
                                                  : "500 Internal Server Error";
            emsg = (s_stage.err == STAGE_FULL) ? "image too large" : "stage write failed";
        } else if (mrc < 0) {
            *status = "400 Bad Request";
            emsg = "malformed multipart body";
        }
    }

    /* flush the partial final sector */
    if (emsg == NULL && s_stage.blk_len > 0u) {
        if (stage_commit(&s_stage, s_stage.blk_len) != 0) {
            *status = "500 Internal Server Error";
            emsg = "stage write failed";
        }
        s_stage.blk_len = 0;
    }
    if (emsg == NULL && !mp_found(&s_mp)) {
        *status = "400 Bad Request";
        emsg = "missing firmware field";
    }
    if (emsg == NULL && mrc != MP_DONE) {
        *status = "400 Bad Request";
        emsg = "truncated multipart body";
    }

    uint8_t sha[32];
    if (emsg == NULL && mbedtls_sha256_finish(&s_stage.sha, sha) != 0) {
        *status = "500 Internal Server Error";
        emsg = "sha failed";
    }
    mbedtls_sha256_free(&s_stage.sha);       /* also releases the SHA engine on every error path */
    if (emsg != NULL) return emsg;

    if (s_stage.written < (uint32_t)IMG_DESC_MIN_LEN) {
        *status = "412 Precondition Failed";
        return "not an ESP32 app image";
    }

    /* Read the descriptor back OUT of the partition, so ver/hwid describe the bytes that will
     * actually be pushed rather than the bytes we thought we wrote. */
    uint8_t hdr[IMG_DESC_MIN_LEN];
    if (esp_partition_read(s_stage.part, 0, hdr, sizeof hdr) != ESP_OK) {
        *status = "500 Internal Server Error";
        return "stage read failed";
    }
    int prc = img_desc_parse(hdr, sizeof hdr, s_flash.ver, s_flash.hwid);
    if (prc != 0) {
        *status = "412 Precondition Failed";
        if (prc == -2) return "bad image version";
        if (prc == -3) return "bad image hwid";
        return "not an ESP32 app image";
    }

    s_flash.size = s_stage.written;
    memcpy(s_flash.sha, sha, sizeof s_flash.sha);
    return NULL;
}

static esp_err_t api_flash_post(httpd_req_t *req)
{
    assert(req != NULL);
    /* The upload half runs on the single httpd request task, so no second upload can be part-way
     * through when a new request arrives -- FLASH_STAGING here would mean reentrancy, which would
     * tear s_mp/s_stage mid-parse. And a set `busy` always has an owner: flash_task clears it, so
     * a busy+IDLE controller would wedge this endpoint at 409 forever.
     * (FLASH_DONE_* with `busy` still set IS legal: flash_task publishes the state just before
     * releasing the guard.) */
    assert(s_flash.state != FLASH_STAGING);
    assert(!s_flash.busy || s_flash.state != FLASH_IDLE);

    size_t left = req->content_len;

    if (s_flash.busy) {
        flash_drain(req, left);
        return send_error_json(req, "409 Conflict", "flash in progress");
    }
    if (left == 0u)
        return send_error_json(req, "400 Bad Request", "empty upload");

    char ct[192];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof ct) != ESP_OK ||
        mp_init(&s_mp, ct, FLASH_FIELD) != 0) {
        flash_drain(req, left);
        return send_error_json(req, "400 Bad Request", "expected multipart/form-data");
    }

    /* Fail fast on a dead or busy link BEFORE pulling a 1.2 MB body over WiFi. */
    lt_status_t st;
    int lrc = linkhost_status(&st);
    if (lrc == LINKHOST_E_BUSY) {
        flash_drain(req, left);
        return send_error_json(req, "423 Locked", "link busy");
    }
    if (lrc != 0) {
        flash_drain(req, left);
        return send_not_connected(req);
    }

    const esp_partition_t *stage =
        esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "ota_stage");
    if (!stage) {
        flash_drain(req, left);
        return send_error_json(req, "500 Internal Server Error", "no ota_stage partition");
    }
    assert((stage->size % (uint32_t)STAGE_BLOCK) == 0u);   /* stage_commit erases whole sectors */

    s_flash.busy   = true;
    s_flash.state  = FLASH_STAGING;
    s_flash.sent   = 0;
    s_flash.total  = 0;
    s_flash.result = 0;
    s_flash.size   = 0;

    memset(&s_stage, 0, sizeof s_stage);
    s_stage.part = stage;

    const char *status = NULL;
    const char *emsg = flash_stage_body(req, &left, &status);
    if (emsg != NULL) {
        s_flash.state = FLASH_IDLE;
        s_flash.busy  = false;
        ESP_LOGW(TAG, "flash upload rejected: %s %s", status, emsg);
        flash_drain(req, left);
        return send_error_json(req, status, emsg);
    }

    /* flash_stage_body's postcondition: a complete, parseable image is in the partition and the
     * single flight is still ours (nothing but this handler and flash_task touches `busy`). */
    assert(s_flash.busy && s_flash.size >= (uint32_t)IMG_DESC_MIN_LEN);
    assert(s_flash.ver[0] != '\0' && s_flash.hwid[0] != '\0');

    s_flash.total = s_flash.size;

    /* M5: build the 202 body BEFORE starting the push. Building it after xTaskCreate() succeeded
     * meant a (currently unreachable) snprintf overflow would leave this request with no response
     * at all while the push proceeded anyway -- the client would hang with nothing to show for a
     * push already under way. Build first; on overflow, respond 500 and never start the task. */
    char body[160];
    int n = snprintf(body, sizeof body,
                     "{\"staged\":true,\"size\":%lu,\"ver\":\"%s\",\"hwid\":\"%s\"}",
                     (unsigned long)s_flash.size, s_flash.ver, s_flash.hwid);
    if (n < 0 || (size_t)n >= sizeof body) {
        s_flash.state = FLASH_IDLE;
        s_flash.busy  = false;
        return send_error_json(req, "500 Internal Server Error", "response body build failed");
    }

    s_flash.state = FLASH_PUSHING;              /* must be set before the task can finish */
    if (xTaskCreate(flash_task, "webapi_flash", 4096, NULL, 5, NULL) != pdPASS) {
        s_flash.state = FLASH_IDLE;
        s_flash.busy  = false;
        return send_error_json(req, "500 Internal Server Error", "cannot start flash task");
    }

    ESP_LOGI(TAG, "staged %lu B (ver=%s hwid=%s) -> pushing",
             (unsigned long)s_flash.size, s_flash.ver, s_flash.hwid);
    return send_json(req, "202 Accepted", body);
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
        { .uri = "/api/flash",      .method = HTTP_POST, .handler = api_flash_post },
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
