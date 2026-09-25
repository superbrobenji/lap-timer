/* devcontroller/components/devconsole/cmd_stream.c -- the `stream` command: stats on the demuxed
 * 0xFF record stream + a rate-limited decoded-row tap, on the dev-kit's own USB console
 * (Plan 5.6 Task 6).
 *
 *   stream stats [secs] [--json]            fused/event/status rates measured over a secs-second
 *                                            window (default 2, clamped 1..30; I5 final review) +
 *                                            gaps + ring high-water + tap drops
 *   stream tap on|off [fused|event|status]  toggle a rate-limited (5 rows/s) decoded-JSON tap
 *
 * console_stream_tap (declared in console.h, called by main.c's stream_consumer for every popped
 * record) is implemented here: a no-op unless `stream tap on` is active, filtered by type when
 * one is set, and rate-limited by a token bucket so a fused/event flood never floods the console
 * UART. It runs on the CONSUMER task, not the console task -- it must stay short: no logging, no
 * blocking, no heap.
 *
 * Every subcommand strips a trailing --json via console_wants_json and reports errors as
 * `ERR <reason>` (human) or `{"err":"<reason>"}` (json), returning non-zero -- same contract as
 * cmd_dc.c.
 */
#include "cmd_stream.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"    /* vTaskDelay -- stream_stats's windowed rate measurement (I5) */

#include "console.h"
#include "jsonw.h"
#include "linkhost.h"         /* linkstats_t, linkhost_stats_snapshot, linkhost_now_us */
#include "linkhost_proto.h"   /* lt_stream_rec_t, LT_SES_T_*, LT_REC_STATUS, linkhost_stream_to_json,
                                * linkhost_stream_ring_hw */
#include "rate.h"             /* rate_x10: pure records/sec*10, host-tested (test_rate.c) */

static void stream_err(bool json, const char *reason)
{
    if (json) printf("{\"err\":\"%s\"}\n", reason);
    else      printf("ERR %s\n", reason);
}

/* ---- `stream stats`: rates measured over a fresh secs-second window (I5, final review), same
 * snapshot -> vTaskDelay -> snapshot -> rate_x10-over-measured-elapsed shape as cmd_selftest.c's
 * stream_run -- replaces the earlier "since the PREVIOUS `stream stats` call" design, whose
 * reported rate depended on how long ago the operator last ran the command rather than on a fixed,
 * predictable window. ---- */
#define STREAM_STATS_DEFAULT_S 2
#define STREAM_STATS_MIN_S     1
#define STREAM_STATS_MAX_S     30

/* Parses an optional bare integer argv[argi] into *out (clamped to
 * [STREAM_STATS_MIN_S,STREAM_STATS_MAX_S]); *out is left at its default (already set by the
 * caller) if the argument is absent. Returns false (usage error) for a present-but-non-numeric
 * token; a present, merely out-of-range value is CLAMPED, not rejected -- mirrors
 * cmd_selftest.c's st_parse_n. */
static bool stream_parse_secs(int argc, char **argv, int argi, int *out)
{
    assert(out != NULL);
    if (argi >= argc) return true;              /* absent: keep default */
    char *end = NULL;
    long v = strtol(argv[argi], &end, 10);
    if (end == argv[argi] || *end != '\0') return false;   /* not a number: usage error */
    if (v < STREAM_STATS_MIN_S) v = STREAM_STATS_MIN_S;
    if (v > STREAM_STATS_MAX_S) v = STREAM_STATS_MAX_S;
    *out = (int)v;
    return true;
}

/* ---- tap state, shared with `stream tap` / console_stream_tap below (module-global so
 * stream_stats can report tap_dropped) ----
 * s_tap_on/s_tap_type are `static volatile`: written by `stream tap` on the CONSOLE task, read by
 * console_stream_tap on the CONSUMER task. Both are single-word stores/loads -- a benign relaxed
 * handoff (matches linkhost_stream_ring_hw's documented rationale), not a torn multi-field
 * struct. s_tap_dropped is the mirror image: written by console_stream_tap on the consumer task,
 * read by stream_stats below on the console task. */
static volatile bool     s_tap_on;
static volatile uint8_t  s_tap_type;    /* 0 = all; else LT_SES_T_FUSED/LT_SES_T_EVENT/LT_REC_STATUS */
static volatile uint32_t s_tap_dropped;

static int stream_stats(int argc, char **argv, bool json)
{
    int secs = STREAM_STATS_DEFAULT_S;
    if (argc > 3 || !stream_parse_secs(argc, argv, 2, &secs)) {
        stream_err(json, "usage: stream stats [1..30] [--json]");
        return 1;
    }

    linkstats_t before;
    linkhost_stats_snapshot(&before);
    int64_t t0 = linkhost_now_us();
    vTaskDelay(pdMS_TO_TICKS((uint32_t)secs * 1000u));
    linkstats_t ls;
    linkhost_stats_snapshot(&ls);
    int64_t t1 = linkhost_now_us();

    /* Rates over the MEASURED elapsed time (t1 - t0), not the nominal `secs` -- vTaskDelay only
     * guarantees "at least". Reset guard: ANY counter going backwards during the window (e.g.
     * linkhost_reset() on a link re-attach) forces every rate to -1 rather than reporting a torn
     * mix of real and stale-baseline rates. */
    int64_t dt_us = t1 - t0;
    bool reset = (ls.n_fused < before.n_fused) || (ls.n_event < before.n_event)
              || (ls.n_status < before.n_status);
    long long fused_rate_x10  = -1;
    long long event_rate_x10  = -1;
    long long status_rate_x10 = -1;
    if (!reset) {
        fused_rate_x10  = rate_x10(ls.n_fused, before.n_fused, dt_us);
        event_rate_x10  = rate_x10(ls.n_event, before.n_event, dt_us);
        status_rate_x10 = rate_x10(ls.n_status, before.n_status, dt_us);
    }

    uint16_t ring_hw = linkhost_stream_ring_hw();
    long long secs_x10 = (long long)((dt_us * 10 + 500000) / 1000000);   /* measured elapsed, x10 */

    if (json) {
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_int(&w, "secs_x10", secs_x10);
        jsonw_int(&w, "fused_rate_x10", fused_rate_x10);
        jsonw_int(&w, "event_rate_x10", event_rate_x10);
        jsonw_int(&w, "status_rate_x10", status_rate_x10);
        jsonw_uint(&w, "gaps", ls.gaps);
        jsonw_uint(&w, "n_fused", ls.n_fused);
        jsonw_uint(&w, "n_event", ls.n_event);
        jsonw_uint(&w, "n_status", ls.n_status);
        jsonw_uint(&w, "ring_hw", ring_hw);
        jsonw_uint(&w, "tap_dropped", s_tap_dropped);
        if (!jsonw_end(&w)) {
            stream_err(true, "stats body too large");
            return 1;
        }
        printf("%s\n", buf);
        return 0;
    }

    printf("secs: %lld.%lld\n", secs_x10 / 10, secs_x10 % 10);
    if (fused_rate_x10 < 0) printf("fused_rate: n/a\n");
    else printf("fused_rate: %lld.%lld/s\n", fused_rate_x10 / 10, fused_rate_x10 % 10);
    if (event_rate_x10 < 0) printf("event_rate: n/a\n");
    else printf("event_rate: %lld.%lld/s\n", event_rate_x10 / 10, event_rate_x10 % 10);
    if (status_rate_x10 < 0) printf("status_rate: n/a\n");
    else printf("status_rate: %lld.%lld/s\n", status_rate_x10 / 10, status_rate_x10 % 10);
    printf("gaps: %u\n", (unsigned)ls.gaps);
    printf("n_fused: %u\n", (unsigned)ls.n_fused);
    printf("n_event: %u\n", (unsigned)ls.n_event);
    printf("n_status: %u\n", (unsigned)ls.n_status);
    printf("ring_hw: %u\n", (unsigned)ring_hw);
    printf("tap_dropped: %u\n", (unsigned)s_tap_dropped);
    return 0;
}

/* ---- `stream tap on|off [fused|event|status]` (tap state declared above, next to the tap-off
 * helper's TAP_RATE_HZ token bucket) ---- */

static const char *tap_type_name(uint8_t t)
{
    switch (t) {
    case 0:               return "all";
    case LT_SES_T_FUSED:  return "fused";
    case LT_SES_T_EVENT:  return "event";
    case LT_REC_STATUS:   return "status";
    default:              return "?";
    }
}

static bool tap_type_parse(const char *s, uint8_t *out)
{
    if (strcmp(s, "fused")  == 0) { *out = LT_SES_T_FUSED; return true; }
    if (strcmp(s, "event")  == 0) { *out = LT_SES_T_EVENT; return true; }
    if (strcmp(s, "status") == 0) { *out = LT_REC_STATUS;  return true; }
    return false;
}

static int stream_tap(int argc, char **argv, bool json)
{
    if (argc < 3) {
        stream_err(json, "usage: stream tap on|off [fused|event|status]");
        return 1;
    }
    if (strcmp(argv[2], "on") == 0) {
        if (argc > 4) {
            stream_err(json, "usage: stream tap on [fused|event|status]");
            return 1;
        }
        uint8_t t = 0;
        if (argc == 4 && !tap_type_parse(argv[3], &t)) {
            stream_err(json, "bad type (want fused|event|status)");
            return 1;
        }
        s_tap_type = t;
        s_tap_on = true;
        if (json) printf("{\"tap\":true,\"type\":\"%s\"}\n", tap_type_name(t));
        else      printf("OK tap on %s\n", tap_type_name(t));
        return 0;
    }
    if (strcmp(argv[2], "off") == 0) {
        if (argc > 3) {
            stream_err(json, "usage: stream tap off");
            return 1;
        }
        s_tap_on = false;
        if (json) printf("{\"tap\":false,\"type\":\"%s\"}\n", tap_type_name(s_tap_type));
        else      printf("OK tap off\n");
        return 0;
    }
    stream_err(json, "usage: stream tap on|off [fused|event|status]");
    return 1;
}

/* Consumer-task hook (declared in console.h). Token bucket state (s_tap_last_us/s_tap_tokens) is
 * touched ONLY from this function, which itself runs only on the consumer task (resolution 3) --
 * no cross-task access, no lock needed for those two. */
#define TAP_RATE_HZ 5
static int64_t s_tap_last_us;
static int     s_tap_tokens = TAP_RATE_HZ;   /* start with a full bucket */

void console_stream_tap(const lt_stream_rec_t *r)
{
    assert(r != NULL);
    if (!s_tap_on) return;
    uint8_t want = s_tap_type;
    if (want != 0 && r->type != want) return;

    int64_t now = linkhost_now_us();
    if (s_tap_last_us == 0) s_tap_last_us = now;      /* first call: no refill yet */
    int64_t dt_us = now - s_tap_last_us;
    if (dt_us > 0) {
        int add = (int)((dt_us * TAP_RATE_HZ) / 1000000);
        if (add > 0) {
            s_tap_tokens += add;
            if (s_tap_tokens > TAP_RATE_HZ) s_tap_tokens = TAP_RATE_HZ;   /* cap: no burst after idle */
            s_tap_last_us = now;
        }
    }
    if (s_tap_tokens <= 0) return;                    /* rate-limited: drop silently, not counted */
    s_tap_tokens--;

    char buf[256];
    int n = linkhost_stream_to_json(r, buf, sizeof buf);
    if (n <= 0 || (size_t)n >= sizeof buf) {           /* unknown/short record or (defensively) oversize */
        s_tap_dropped++;
        return;
    }
    printf("%s\n", buf);
}

void cmd_stream_tap_off(void)
{
    s_tap_on = false;
}

static int cmd_stream_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);
    if (argc < 2) {
        stream_err(json, "usage: stream stats [1..30] [--json] | stream tap on|off [fused|event|status]");
        return 1;
    }
    if (strcmp(argv[1], "stats") == 0) return stream_stats(argc, argv, json);
    if (strcmp(argv[1], "tap")   == 0) return stream_tap(argc, argv, json);
    stream_err(json, "unknown subcommand (want stats|tap)");
    return 1;
}

void cmd_stream_register(void)
{
    console_register("stream",
                     "stream stats [1..30] [--json] | stream tap on|off [fused|event|status]",
                     cmd_stream_main);
}
