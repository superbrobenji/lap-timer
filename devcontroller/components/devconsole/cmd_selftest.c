/* devcontroller/components/devconsole/cmd_selftest.c -- the `selftest` command: dev-kit-native
 * qualification of the UART1 link to the lap-timer, so the link can be checked over USB with no
 * WiFi AP (Plan 5.6 Task 9).
 *
 *   selftest link [N]      N (default 20, clamped 1..64) round-trip `status` commands, 100 ms
 *                          apart; verdict from selftest_link_eval (median < 100 ms, max < 500 ms,
 *                          0 failures).
 *   selftest stream [s]    samples linkhost_stats_snapshot before/after an s-second (default 10,
 *                          clamped 1..60) window; verdict from selftest_stream_eval (fused >=
 *                          8/s, status >= 0.8/s, 0 gaps).
 *   selftest framing       arms linkhost's raw reply capture, runs one `status` round trip,
 *                          copies the capture (linkhost_trace_last_reply) and verifies BEGIN/END
 *                          markers + no "\r\r\n" double-CR lines via selftest_framing_eval.
 *   selftest all           link (N=20), stream (10 s), framing, in that order; one summary line
 *                          "SELFTEST PASS"/"SELFTEST FAIL", non-zero return on FAIL.
 *
 * Every verdict is computed by the PURE, host-tested selftest_eval.c (host/selftest_eval.c) --
 * this file only drives the link/timers/capture and formats the result. Every subcommand strips a
 * trailing --json via console_wants_json and reports a LOCAL usage error as `ERR <reason>` /
 * `{"err":"<reason>"}` with a non-zero return, same contract as cmd_dc.c/cmd_lt.c/cmd_stream.c.
 */
#include "cmd_selftest.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "console.h"
#include "jsonw.h"
#include "linkhost.h"          /* linkhost_cmd_timed/_now_us/_stats_snapshot/_trace_last_reply, LT_CMD_STATUS */
#include "linkhost_proto.h"    /* linkhost_rawcap_set -- independent of linkhost_trace_set's log trace */
#include "selftest_eval.h"     /* pure verdicts: selftest_link_eval/_stream_eval/_framing_eval, ST_LINK_MAX */

#define ST_LINK_DEFAULT_N   20
#define ST_LINK_MIN_N       1
/* upper bound is selftest_eval.h's ST_LINK_MAX (64) -- the pragmatic-P10 array bound shared with
 * the pure evaluator; used directly below rather than re-defined here. */
#define ST_LINK_SPACING_MS  100

#define ST_STREAM_DEFAULT_S 10
#define ST_STREAM_MIN_S     1
#define ST_STREAM_MAX_S     60

#define ST_RAWCAP_MAX 256u     /* mirrors linkhost_proto.c's rawcap buffer size */

static void st_err(bool json, const char *reason)
{
    if (json) printf("{\"err\":\"%s\"}\n", reason);
    else      printf("ERR %s\n", reason);
}

/* Parses an optional bare integer argv[argi] into *out (clamped to [lo,hi]); *out is left at its
 * default (already set by the caller) if the argument is absent. Returns false (usage error) for
 * a present-but-non-numeric token; a present, merely out-of-range value is CLAMPED, not
 * rejected (ambiguity resolution 4/5: "clamp 1..64" / "clamp 1..60"). */
static bool st_parse_n(int argc, char **argv, int argi, int lo, int hi, int *out)
{
    assert(out != NULL);
    assert(lo <= hi);
    if (argi >= argc) return true;              /* absent: keep default */
    char *end = NULL;
    long v = strtol(argv[argi], &end, 10);
    if (end == argv[argi] || *end != '\0') return false;   /* not a number: usage error */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *out = (int)v;
    return true;
}

/* ================================================================================================
 *  reporters (json / human) -- shared by the standalone subcommands and `selftest all`
 * ============================================================================================== */

static void st_report_link(bool json, int n, const st_link_t *r)
{
    assert(r != NULL);
    if (json) {
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "test", "link");
        jsonw_bool(&w, "pass", r->pass);
        jsonw_int(&w, "n", n);
        jsonw_int(&w, "failures", r->failures);
        jsonw_uint(&w, "med_ms", r->med_ms);
        jsonw_uint(&w, "max_ms", r->max_ms);
        jsonw_uint(&w, "min_ms", r->min_ms);
        jsonw_str(&w, "reason", r->reason);
        if (!jsonw_end(&w)) { st_err(true, "result too large"); return; }
        printf("%s\n", buf);
        return;
    }
    printf("median: %u ms\n", (unsigned)r->med_ms);
    printf("max: %u ms\n", (unsigned)r->max_ms);
    printf("min: %u ms\n", (unsigned)r->min_ms);
    printf("failures: %d/%d\n", r->failures, n);
    if (r->pass) printf("PASS\n");
    else printf("FAIL %s\n", r->reason);
}

/* Ambiguity resolution 3: human prints the float rates with %.1f directly (the dev-kit target has
 * float printf); --json encodes them as integers x10 since jsonw has no float writer. */
static void st_report_stream(bool json, float secs, const st_stream_t *r)
{
    assert(r != NULL);
    if (json) {
        long long secs_x10   = (long long)(secs * 10.0f + 0.5f);
        long long fused_x10  = (long long)(r->fused_rate * 10.0f + 0.5f);
        long long status_x10 = (long long)(r->status_rate * 10.0f + 0.5f);
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "test", "stream");
        jsonw_bool(&w, "pass", r->pass);
        jsonw_int(&w, "secs_x10", secs_x10);
        jsonw_int(&w, "fused_rate_x10", fused_x10);
        jsonw_int(&w, "status_rate_x10", status_x10);
        jsonw_uint(&w, "gaps", r->gaps);
        jsonw_str(&w, "reason", r->reason);
        if (!jsonw_end(&w)) { st_err(true, "result too large"); return; }
        printf("%s\n", buf);
        return;
    }
    printf("secs: %.1f\n", (double)secs);
    printf("fused_rate: %.1f/s\n", (double)r->fused_rate);
    printf("status_rate: %.1f/s\n", (double)r->status_rate);
    printf("gaps: %u\n", (unsigned)r->gaps);
    if (r->pass) printf("PASS\n");
    else printf("FAIL %s\n", r->reason);
}

static void st_report_framing(bool json, size_t bytes, const st_framing_t *r)
{
    assert(r != NULL);
    if (json) {
        char buf[192];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "test", "framing");
        jsonw_bool(&w, "pass", r->pass);
        jsonw_int(&w, "markers", r->markers);
        jsonw_int(&w, "bad", r->bad);
        jsonw_uint(&w, "bytes", (unsigned long long)bytes);
        jsonw_str(&w, "reason", r->reason);
        if (!jsonw_end(&w)) { st_err(true, "result too large"); return; }
        printf("%s\n", buf);
        return;
    }
    printf("markers: %d\n", r->markers);
    printf("bad: %d\n", r->bad);
    printf("bytes: %u\n", (unsigned)bytes);
    if (r->pass) printf("PASS\n");
    else printf("FAIL %s\n", r->reason);
}

/* ================================================================================================
 *  runners -- drive the link/timers/capture, then hand the raw numbers to the pure evaluator.
 *  Shared between the standalone subcommands and `selftest all` (ambiguity resolution 7).
 * ============================================================================================== */

/* N x `status` round trips, ST_LINK_SPACING_MS apart (ambiguity resolution 4). A non-zero
 * linkhost_cmd_timed result counts as a failure and its latency is excluded; latency is rounded
 * UP to whole ms, floored at 1 ms (a sub-millisecond round trip is still "at least 1 ms" for the
 * threshold check). */
static void link_run(int n, st_link_t *out)
{
    assert(out != NULL);
    assert(n >= ST_LINK_MIN_N && n <= ST_LINK_MAX);
    uint32_t lat_ms[ST_LINK_MAX];
    int ok = 0;
    int failures = 0;
    for (int i = 0; i < n; i++) {                       /* bounded: n <= ST_LINK_MAX (64) */
        linkhost_frame_t f = { 0 };
        linkhost_cmd_stats_t st;
        linkhost_cmd_timed(LT_CMD_STATUS, &f, &st);
        if (st.result == 0) {
            uint32_t ms = (uint32_t)((st.latency_us + 999) / 1000);   /* ceil to whole ms */
            if (ms == 0) ms = 1;
            lat_ms[ok++] = ms;
        } else {
            failures++;
        }
        if (i + 1 < n) vTaskDelay(pdMS_TO_TICKS(ST_LINK_SPACING_MS));
    }
    selftest_link_eval(lat_ms, ok, failures, out);
}

/* Samples linkstats before/after an secs-second window (ambiguity resolution 5): deltas of
 * n_fused/n_status/gaps, clamped at 0 against a mid-window counter reset (e.g. a link re-attach)
 * rather than an unsigned-wraparound spurious rate. `*elapsed_out` is the MEASURED elapsed time
 * (linkhost_now_us before/after), not the nominal `secs` -- selftest_stream_eval's own contract. */
static void stream_run(int secs, st_stream_t *out, float *elapsed_out)
{
    assert(out != NULL);
    assert(elapsed_out != NULL);
    assert(secs >= ST_STREAM_MIN_S && secs <= ST_STREAM_MAX_S);

    linkstats_t before, after;
    linkhost_stats_snapshot(&before);
    int64_t t0 = linkhost_now_us();
    vTaskDelay(pdMS_TO_TICKS((uint32_t)secs * 1000u));
    linkhost_stats_snapshot(&after);
    int64_t t1 = linkhost_now_us();

    uint32_t fused_delta  = (after.n_fused  >= before.n_fused)  ? after.n_fused  - before.n_fused  : 0;
    uint32_t status_delta = (after.n_status >= before.n_status) ? after.n_status - before.n_status : 0;
    uint32_t gaps_delta   = (after.gaps     >= before.gaps)     ? after.gaps     - before.gaps     : 0;
    float elapsed_s = (float)(t1 - t0) / 1000000.0f;

    selftest_stream_eval(fused_delta, status_delta, gaps_delta, elapsed_s, out);
    *elapsed_out = elapsed_s;
}

/* Arms the raw capture, runs one `status` round trip, copies the capture, disarms it, then hands
 * the raw bytes to the pure evaluator (ambiguity resolution 2). `link trace` (the ESP_LOGI log)
 * is untouched -- the capture is independent of it. */
static void framing_run(st_framing_t *out, size_t *bytes_out)
{
    assert(out != NULL);
    assert(bytes_out != NULL);

    linkhost_rawcap_set(true);
    linkhost_frame_t f = { 0 };
    linkhost_cmd_stats_t st;
    linkhost_cmd_timed(LT_CMD_STATUS, &f, &st);
    uint8_t raw[ST_RAWCAP_MAX];
    size_t n = linkhost_trace_last_reply(raw, sizeof raw);
    linkhost_rawcap_set(false);

    selftest_framing_eval(raw, n, out);
    *bytes_out = n;
}

/* ================================================================================================
 *  subcommands
 * ============================================================================================== */

static int st_link(int argc, char **argv, bool json)
{
    int n = ST_LINK_DEFAULT_N;
    if (argc > 3 || !st_parse_n(argc, argv, 2, ST_LINK_MIN_N, ST_LINK_MAX, &n)) {
        st_err(json, "usage: selftest link [1..64]");
        return 1;
    }
    st_link_t r;
    link_run(n, &r);
    st_report_link(json, n, &r);
    return r.pass ? 0 : 1;
}

static int st_stream(int argc, char **argv, bool json)
{
    int secs = ST_STREAM_DEFAULT_S;
    if (argc > 3 || !st_parse_n(argc, argv, 2, ST_STREAM_MIN_S, ST_STREAM_MAX_S, &secs)) {
        st_err(json, "usage: selftest stream [1..60]");
        return 1;
    }
    st_stream_t r;
    float elapsed;
    stream_run(secs, &r, &elapsed);
    st_report_stream(json, elapsed, &r);
    return r.pass ? 0 : 1;
}

static int st_framing(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        st_err(json, "usage: selftest framing [--json]");
        return 1;
    }
    st_framing_t r;
    size_t bytes;
    framing_run(&r, &bytes);
    st_report_framing(json, bytes, &r);
    return r.pass ? 0 : 1;
}

/* Runs link (N=20) + stream (10 s) + framing in that order; prints each result then one summary
 * line (ambiguity resolution 7). --json emits ONE object nesting the three per-test results via
 * jsonw_obj/jsonw_close, rather than the three separate lines the standalone subcommands print. */
static int st_all(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        st_err(json, "usage: selftest all [--json]");
        return 1;
    }

    st_link_t lr;
    link_run(ST_LINK_DEFAULT_N, &lr);

    st_stream_t sr;
    float elapsed;
    stream_run(ST_STREAM_DEFAULT_S, &sr, &elapsed);

    st_framing_t fr;
    size_t bytes;
    framing_run(&fr, &bytes);

    bool pass = lr.pass && sr.pass && fr.pass;

    if (json) {
        char buf[768];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "test", "all");
        jsonw_bool(&w, "pass", pass);

        jsonw_obj(&w, "link");
        jsonw_bool(&w, "pass", lr.pass);
        jsonw_int(&w, "n", ST_LINK_DEFAULT_N);
        jsonw_int(&w, "failures", lr.failures);
        jsonw_uint(&w, "med_ms", lr.med_ms);
        jsonw_uint(&w, "max_ms", lr.max_ms);
        jsonw_uint(&w, "min_ms", lr.min_ms);
        jsonw_str(&w, "reason", lr.reason);
        jsonw_close(&w);

        jsonw_obj(&w, "stream");
        jsonw_bool(&w, "pass", sr.pass);
        jsonw_int(&w, "secs_x10", (long long)(elapsed * 10.0f + 0.5f));
        jsonw_int(&w, "fused_rate_x10", (long long)(sr.fused_rate * 10.0f + 0.5f));
        jsonw_int(&w, "status_rate_x10", (long long)(sr.status_rate * 10.0f + 0.5f));
        jsonw_uint(&w, "gaps", sr.gaps);
        jsonw_str(&w, "reason", sr.reason);
        jsonw_close(&w);

        jsonw_obj(&w, "framing");
        jsonw_bool(&w, "pass", fr.pass);
        jsonw_int(&w, "markers", fr.markers);
        jsonw_int(&w, "bad", fr.bad);
        jsonw_uint(&w, "bytes", (unsigned long long)bytes);
        jsonw_str(&w, "reason", fr.reason);
        jsonw_close(&w);

        if (!jsonw_end(&w)) { st_err(true, "result too large"); return 1; }
        printf("%s\n", buf);
        return pass ? 0 : 1;
    }

    printf("-- link --\n");
    st_report_link(false, ST_LINK_DEFAULT_N, &lr);
    printf("-- stream --\n");
    st_report_stream(false, elapsed, &sr);
    printf("-- framing --\n");
    st_report_framing(false, bytes, &fr);
    if (pass) printf("SELFTEST PASS\n");
    else printf("SELFTEST FAIL\n");
    return pass ? 0 : 1;
}

/* ================================================================================================
 *  dispatch + registration
 * ============================================================================================== */

static int cmd_selftest_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);
    if (argc < 2) {
        st_err(json, "usage: selftest link [N]|stream [s]|framing|all [--json]");
        return 1;
    }
    if (strcmp(argv[1], "link")    == 0) return st_link(argc, argv, json);
    if (strcmp(argv[1], "stream")  == 0) return st_stream(argc, argv, json);
    if (strcmp(argv[1], "framing") == 0) return st_framing(argc, argv, json);
    if (strcmp(argv[1], "all")     == 0) return st_all(argc, argv, json);
    st_err(json, "unknown subcommand (want link|stream|framing|all)");
    return 1;
}

void cmd_selftest_register(void)
{
    console_register("selftest", "selftest link [N]|stream [s]|framing|all [--json]",
                     cmd_selftest_main);
}
