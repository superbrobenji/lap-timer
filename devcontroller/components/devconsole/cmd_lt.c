/* devcontroller/components/devconsole/cmd_lt.c -- the `lt`/`link` commands: a manual relay to the
 * lap-timer's framed console over UART1, with round-trip timing and a debug hexdump trace
 * (Plan 5.6 Task 5).
 *
 *   lt status [--json]                  `status` -> the decoded §18.2 STATUS record (named fields)
 *   lt list [--json]                    `list` -> the session-list JSON, streamed (linkhost_download_cmd)
 *   lt config get [--json]              `config get` -> the live config JSON
 *   lt config set <json> [--json]       `config set <esc>` -- escaped via linkhost's wire_escape,
 *                                        the same one POST /api/config uses
 *   lt delete <id> [--json]             `delete <id>`
 *   lt open <id> <fmt> [--json]         `open <id> <fmt>` -> streamed (linkhost_download_cmd)
 *   link trace on|off [--json]          toggles linkhost's verbose ESP_LOGI("trace: ...")
 *
 * status/config get/config set/delete route through linkhost_cmd_timed (one bounded body, with
 * round-trip stats); list/open route through linkhost_download_cmd (unbounded streaming body --
 * text formats are printed as chunks arrive, binary formats are only byte-counted). Every
 * subcommand strips a trailing --json via console_wants_json and reports a LOCAL usage error as
 * `ERR <reason>` / `{"err":"<reason>"}` with a non-zero return, same as cmd_dc.c; a completed
 * round trip (success or a link-level failure) always ends with the "-- rt/attempts/rc" summary
 * (or its --json equivalent) so a timeout/CRC/busy failure is still visible with its timing.
 */
#include "cmd_lt.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "console.h"
#include "hexfmt.h"        /* status-body-wrong-length fallback dump */
#include "jsonw.h"
#include "linkhost.h"      /* linkhost_cmd_timed / linkhost_download_cmd, LT_CMD_.. / LT_FMT_.. , trace get/set */
#include "wire_escape.h"   /* wire_escape -- the same wire escaping POST /api/config uses (linkhost, not webapi:
                            * Plan 5.6 Task 5 fix 1 moved it out of webapi's config_diff so this component has no
                            * dependency on webapi) */

/* Max reply-body bytes embedded in a --json object's "body" field: the wire body itself can be up
 * to LINKHOST_ASM_MAX (1024 B, config get is ~939 B); this just keeps one console line reasonable.
 * The human form prints the full (untruncated) body instead -- see lt_report_cmd. */
#define LT_JSON_BODY_MAX 512u

/* `config set` line budget: mirrors webapi/include/config_diff.h's CFG_SET_* exactly, duplicated
 * (not imported) so this component has no dependency on webapi -- both sides derive from the same
 * two facts: the lap-timer console's max_cmdline_length is 256 B, Plan 5.5 Task 4 keeps every line
 * <=250 B, and "config set " is 11 bytes. */
#define LT_CFG_LINE_MAX   250u
#define LT_CFG_PREFIX_LEN 11u
#define LT_CFG_OBJ_MAX    (LT_CFG_LINE_MAX - LT_CFG_PREFIX_LEN)   /* 239 */

static void lt_err(bool json, const char *reason)
{
    if (json) printf("{\"err\":\"%s\"}\n", reason);
    else      printf("ERR %s\n", reason);
}

/* ================================================================================================
 *  linkhost_cmd_timed path: status / config get / config set / delete
 * ============================================================================================== */

/* Prints one linkhost_cmd_timed reply: human "<body>\n-- rt <ms> ms, attempts <n>, rc <rc>" (a
 * remote error prints its ERR line instead of a body), or a single jsonw object
 * {cmd,rc,attempts,rt_ms,name,body} (body bounded to LT_JSON_BODY_MAX, JSON-escaped by jsonw_str;
 * on a remote error it holds err_msg instead of the frame body, which linkhost_cmd_timed leaves
 * unset on any non-zero result). */
static void lt_report_cmd(bool json, const char *cmd, const linkhost_frame_t *f,
                          const linkhost_cmd_stats_t *st)
{
    assert(cmd != NULL);
    assert(f != NULL);
    assert(st != NULL);
    long long rt_ms = (long long)(st->latency_us / 1000);

    if (!json) {
        if (st->result == 0 && f->body && f->body_len > 0) {
            fwrite(f->body, 1, f->body_len, stdout);
            if (f->body[f->body_len - 1] != '\n') printf("\n");
        } else if (st->result == LINKHOST_E_REMOTE) {
            printf("ERR remote 0x%04x: %s\n", (unsigned)f->err_code, f->err_msg);
        }
        printf("-- rt %lld ms, attempts %d, rc %d\n", rt_ms, st->attempts, st->result);
        return;
    }

    char body[LT_JSON_BODY_MAX + 1];
    size_t blen = 0;
    if (st->result == 0 && f->body) {
        blen = (f->body_len < LT_JSON_BODY_MAX) ? f->body_len : LT_JSON_BODY_MAX;
        memcpy(body, f->body, blen);
    } else if (st->result == LINKHOST_E_REMOTE) {
        blen = strlen(f->err_msg);
        if (blen > LT_JSON_BODY_MAX) blen = LT_JSON_BODY_MAX;
        memcpy(body, f->err_msg, blen);
    }
    body[blen] = '\0';

    char buf[LT_JSON_BODY_MAX + 256u];
    jsonw_t w;
    jsonw_begin(&w, buf, sizeof buf);
    jsonw_str(&w, "cmd", cmd);
    jsonw_int(&w, "rc", st->result);
    jsonw_int(&w, "attempts", st->attempts);
    jsonw_int(&w, "rt_ms", rt_ms);
    jsonw_str(&w, "name", f->name);
    jsonw_str(&w, "body", body);
    if (!jsonw_end(&w)) { lt_err(true, "reply too large"); return; }
    printf("%s\n", buf);
}

/* `status` replies with the raw 20-byte §18.2 STATUS record (binary, base64 on the wire --
 * frame_is_binary("status") in linkhost_proto.c), not text: lt_report_cmd's generic body
 * handling would either dump non-printable bytes (human) or hand jsonw_str a string that stops at
 * the first embedded NUL (json -- e.g. flags == 0x0000 at byte offset 2, silently truncating
 * everything after it). So `status` gets its own reporter that decodes the record
 * (linkhost_status_decode, same as cmd_dc.c's dc_status) and prints named fields instead. */
static void lt_report_status(bool json, const linkhost_frame_t *f, const linkhost_cmd_stats_t *st)
{
    assert(f != NULL);
    assert(st != NULL);
    long long rt_ms = (long long)(st->latency_us / 1000);

    if (st->result == 0 && f->body && f->body_len == LT_STATUS_LEN) {
        lt_status_t ls;
        if (linkhost_status_decode(f->body, &ls)) {
            if (json) {
                char buf[384];
                jsonw_t w;
                jsonw_begin(&w, buf, sizeof buf);
                jsonw_str(&w, "cmd", LT_CMD_STATUS);
                jsonw_int(&w, "rc", st->result);
                jsonw_int(&w, "attempts", st->attempts);
                jsonw_int(&w, "rt_ms", rt_ms);
                jsonw_uint(&w, "proto", ls.proto);
                jsonw_uint(&w, "state", ls.state);
                jsonw_uint(&w, "flags", ls.flags);
                jsonw_uint(&w, "batt_pct", ls.batt_pct);
                jsonw_uint(&w, "batt_mv", ls.batt_mv);
                jsonw_uint(&w, "free_kb", ls.free_kb);
                jsonw_uint(&w, "sessions", ls.sessions);
                jsonw_str(&w, "fw", ls.fw);
                if (!jsonw_end(&w)) { lt_err(true, "reply too large"); return; }
                printf("%s\n", buf);
                return;
            }
            printf("proto: %u\n", (unsigned)ls.proto);
            printf("state: %u\n", (unsigned)ls.state);
            printf("flags: %u\n", (unsigned)ls.flags);
            printf("batt_pct: %u\n", (unsigned)ls.batt_pct);
            printf("batt_mv: %u\n", (unsigned)ls.batt_mv);
            printf("free_kb: %lu\n", (unsigned long)ls.free_kb);
            printf("sessions: %u\n", (unsigned)ls.sessions);
            printf("fw: %s\n", ls.fw);
            printf("-- rt %lld ms, attempts %d, rc %d\n", rt_ms, st->attempts, st->result);
            return;
        }
    }

    /* Not a decodable 20-byte STATUS record: a successful reply of the wrong length (a protocol
     * mismatch, not expected in practice), a remote error, or a link failure. Falls back to the
     * generic {cmd,rc,attempts,rt_ms,name,body} shape, with body a bounded hexfmt_line dump of
     * whatever bytes did come back rather than raw (possibly non-printable) bytes. */
    char hex[3u * 32u + 8u];   /* hexfmt_line's own bound: <=32 bytes -> <=~100 chars + ellipsis */
    hex[0] = '\0';
    if (st->result == 0 && f->body && f->body_len > 0) hexfmt_line(f->body, f->body_len, hex, sizeof hex);

    if (json) {
        char buf[512];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "cmd", LT_CMD_STATUS);
        jsonw_int(&w, "rc", st->result);
        jsonw_int(&w, "attempts", st->attempts);
        jsonw_int(&w, "rt_ms", rt_ms);
        jsonw_str(&w, "name", f->name);
        jsonw_str(&w, "body", (st->result == LINKHOST_E_REMOTE) ? f->err_msg : hex);
        if (!jsonw_end(&w)) { lt_err(true, "reply too large"); return; }
        printf("%s\n", buf);
        return;
    }

    if (st->result == 0 && hex[0] != '\0') {
        printf("%s\n", hex);
    } else if (st->result == LINKHOST_E_REMOTE) {
        printf("ERR remote 0x%04x: %s\n", (unsigned)f->err_code, f->err_msg);
    }
    printf("-- rt %lld ms, attempts %d, rc %d\n", rt_ms, st->attempts, st->result);
}

static int lt_status(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) { lt_err(json, "usage: lt status [--json]"); return 1; }
    linkhost_frame_t f = { 0 };
    linkhost_cmd_stats_t st;
    linkhost_cmd_timed(LT_CMD_STATUS, &f, &st);
    lt_report_status(json, &f, &st);
    return (st.result == 0) ? 0 : 1;
}

static int lt_config_get(bool json)
{
    linkhost_frame_t f = { 0 };
    linkhost_cmd_stats_t st;
    linkhost_cmd_timed(LT_CMD_CONFIG_GET, &f, &st);
    lt_report_cmd(json, LT_CMD_CONFIG_GET, &f, &st);
    return (st.result == 0) ? 0 : 1;
}

/* Re-joins argv[3..argc) with single spaces into out[0..out_cap) (mirrors export_serial.c's own
 * cmd_config_set_c re-join): the JSON payload usually arrives as one console token, but an
 * unquoted payload containing spaces would already have been split by OUR OWN console's tokenizer
 * before cmd_lt_main ever saw it. Returns the joined length, or <0 if it does not fit out_cap. */
static int lt_join_json(int argc, char **argv, char *out, size_t out_cap)
{
    assert(out != NULL);
    assert(out_cap > 0);
    size_t w = 0;
    for (int i = 3; i < argc; i++) {                            /* bounded by argc */
        size_t al = strlen(argv[i]);
        size_t need = al + ((i > 3) ? 1u : 0u);
        if (w + need >= out_cap) return -1;
        if (i > 3) out[w++] = ' ';
        memcpy(out + w, argv[i], al);
        w += al;
    }
    out[w] = '\0';
    return (w > 0) ? (int)w : -1;
}

/* `config set <json>`: escapes the payload via linkhost's wire_escape -- the same escaping
 * POST /api/config uses (webapi.c) -- before sending it as `config set <esc>`. The LAP-TIMER's own
 * esp_console_split_argv strips bare double quotes, so an un-escaped {"k":"v"} would arrive there
 * as {k:v} and fail to parse. */
static int lt_config_set(int argc, char **argv, bool json)
{
    if (argc < 4) { lt_err(json, "usage: lt config set <json> [--json]"); return 1; }

    char raw[LT_CFG_OBJ_MAX + 1];
    if (lt_join_json(argc, argv, raw, sizeof raw) < 0) {
        lt_err(json, "config payload too large");
        return 1;
    }

    char esc[LT_CFG_OBJ_MAX * 2 + 1];
    if (wire_escape(raw, esc, sizeof esc) == 0) {
        lt_err(json, "config payload too large");
        return 1;
    }

    char cmd[LT_CFG_LINE_MAX + 1];
    int cn = snprintf(cmd, sizeof cmd, "%s %s", LT_CMD_CONFIG_SET, esc);
    if (cn < 0 || (size_t)cn >= sizeof cmd) { lt_err(json, "config set line too long"); return 1; }

    linkhost_frame_t f = { 0 };
    linkhost_cmd_stats_t st;
    linkhost_cmd_timed(cmd, &f, &st);
    lt_report_cmd(json, cmd, &f, &st);
    return (st.result == 0) ? 0 : 1;
}

static int lt_config(int argc, char **argv, bool json)
{
    if (argc < 3) { lt_err(json, "usage: lt config get|set <json> [--json]"); return 1; }
    if (strcmp(argv[2], "get") == 0) {
        if (argc != 3) { lt_err(json, "usage: lt config get [--json]"); return 1; }
        return lt_config_get(json);
    }
    if (strcmp(argv[2], "set") == 0) return lt_config_set(argc, argv, json);
    lt_err(json, "usage: lt config get|set <json> [--json]");
    return 1;
}

static int lt_delete(int argc, char **argv, bool json)
{
    if (argc != 3) { lt_err(json, "usage: lt delete <id> [--json]"); return 1; }

    /* export_serial.c has no LT_CMD_DELETE constant (it register_cmd("delete", ...) directly) --
     * "delete" is the literal command name on the lap-timer side too. */
    char cmd[64];
    int cn = snprintf(cmd, sizeof cmd, "delete %s", argv[2]);
    if (cn < 0 || (size_t)cn >= sizeof cmd) { lt_err(json, "id too long"); return 1; }

    linkhost_frame_t f = { 0 };
    linkhost_cmd_stats_t st;
    linkhost_cmd_timed(cmd, &f, &st);
    lt_report_cmd(json, cmd, &f, &st);
    return (st.result == 0) ? 0 : 1;
}

/* ================================================================================================
 *  linkhost_download_cmd path: list / open
 * ============================================================================================== */

typedef struct {
    bool     is_binary;
    uint32_t nbytes;
} lt_dl_sink_t;

/* lh_dl_chunk_cb: text formats are printed as they arrive (ambiguity 3); binary formats (log/sum)
 * are only counted -- their decoded bytes are not printable console text. */
static int lt_dl_chunk_cb(void *ctx, const uint8_t *data, size_t n)
{
    lt_dl_sink_t *s = (lt_dl_sink_t *)ctx;
    assert(s != NULL);
    assert(data != NULL || n == 0);
    s->nbytes += (uint32_t)n;
    if (!s->is_binary) fwrite(data, 1, n, stdout);
    return 0;
}

/* Runs `cmd` through linkhost_download_cmd and reports it: human "-- <n> bytes" for a binary
 * format (the body-equivalent line -- text formats already streamed via lt_dl_chunk_cb), then
 * always "-- rt <ms> ms, attempts 1, rc <rc>" (attempts is always 1: linkhost_download_cmd does
 * not retry); --json is {cmd,rc,attempts,rt_ms,bytes} -- body is omitted (a download's decoded
 * body can be far larger than one console line) in favour of the total byte count. */
static int lt_run_download(bool json, const char *cmd, bool is_binary)
{
    lt_dl_sink_t sink = { .is_binary = is_binary, .nbytes = 0 };
    linkhost_remote_err_t rerr = { 0 };
    int64_t t0 = linkhost_now_us();
    int rc = linkhost_download_cmd(cmd, is_binary, lt_dl_chunk_cb, &sink, &rerr);
    long long rt_ms = (long long)((linkhost_now_us() - t0) / 1000);

    if (json) {
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "cmd", cmd);
        jsonw_int(&w, "rc", rc);
        jsonw_int(&w, "attempts", 1);
        jsonw_int(&w, "rt_ms", rt_ms);
        jsonw_uint(&w, "bytes", sink.nbytes);
        if (!jsonw_end(&w)) { lt_err(true, "reply too large"); return 1; }
        printf("%s\n", buf);
        return (rc == 0) ? 0 : 1;
    }

    if (is_binary) printf("-- %u bytes\n", (unsigned)sink.nbytes);
    if (rc == LINKHOST_E_REMOTE) printf("ERR remote 0x%04x: %s\n", (unsigned)rerr.code, rerr.msg);
    printf("-- rt %lld ms, attempts 1, rc %d\n", rt_ms, rc);
    return (rc == 0) ? 0 : 1;
}

static int lt_list(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) { lt_err(json, "usage: lt list [--json]"); return 1; }
    return lt_run_download(json, LT_CMD_LIST, /*is_binary*/false);
}

static bool lt_fmt_valid(const char *fmt)
{
    return strcmp(fmt, LT_FMT_VBO) == 0 || strcmp(fmt, LT_FMT_NMEA) == 0 ||
           strcmp(fmt, LT_FMT_JSON) == 0 || strcmp(fmt, LT_FMT_LOG) == 0 ||
           strcmp(fmt, LT_FMT_SUM) == 0;
}

/* Mirrors linkhost.c's own (private) fmt_is_binary: only log/sum are base64 on the wire. */
static bool lt_fmt_is_binary(const char *fmt)
{
    return strcmp(fmt, LT_FMT_LOG) == 0 || strcmp(fmt, LT_FMT_SUM) == 0;
}

static int lt_open(int argc, char **argv, bool json)
{
    if (argc != 4) { lt_err(json, "usage: lt open <id> <vbo|nmea|json|log|sum> [--json]"); return 1; }
    if (!lt_fmt_valid(argv[3])) {
        lt_err(json, "bad format (want vbo|nmea|json|log|sum)");
        return 1;
    }

    char cmd[64];
    int cn = snprintf(cmd, sizeof cmd, "%s %s %s", LT_CMD_OPEN, argv[2], argv[3]);
    if (cn < 0 || (size_t)cn >= sizeof cmd) { lt_err(json, "id too long"); return 1; }

    return lt_run_download(json, cmd, lt_fmt_is_binary(argv[3]));
}

/* ================================================================================================
 *  dispatch + registration
 * ============================================================================================== */

static int cmd_lt_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);
    if (argc < 2) {
        lt_err(json, "usage: lt status|list|config get|config set <json>|delete <id>|open <id> <fmt>");
        return 1;
    }
    if (strcmp(argv[1], "status") == 0) return lt_status(argc, argv, json);
    if (strcmp(argv[1], "list")   == 0) return lt_list(argc, argv, json);
    if (strcmp(argv[1], "config") == 0) return lt_config(argc, argv, json);
    if (strcmp(argv[1], "delete") == 0) return lt_delete(argc, argv, json);
    if (strcmp(argv[1], "open")   == 0) return lt_open(argc, argv, json);
    lt_err(json, "unknown subcommand (want status|list|config|delete|open)");
    return 1;
}

static int cmd_link_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);
    if (argc == 3 && strcmp(argv[1], "trace") == 0 &&
        (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        bool on = (strcmp(argv[2], "on") == 0);
        linkhost_trace_set(on);
        if (json) {
            char buf[32];
            jsonw_t w;
            jsonw_begin(&w, buf, sizeof buf);
            jsonw_bool(&w, "trace", on);
            jsonw_end(&w);
            printf("%s\n", buf);
        } else {
            printf("OK trace %s\n", on ? "on" : "off");
        }
        return 0;
    }
    lt_err(json, "usage: link trace on|off");
    return 1;
}

void cmd_lt_register(void)
{
    console_register("lt", "lt status|list|config get|config set <json>|delete <id>|"
                            "open <id> <fmt> [--json]", cmd_lt_main);
    console_register("link", "link trace on|off [--json]", cmd_link_main);
}
