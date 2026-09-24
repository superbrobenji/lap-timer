/* devcontroller/components/devconsole/include/selftest_eval.h -- pure verdict logic behind
 * `selftest link|stream|framing|all` (Plan 5.6 Task 9): the dev-kit-native qualification of the
 * UART1 link to the lap-timer, so it can be checked over USB with no WiFi AP.
 *
 * PINNED interface (host-tested: devcontroller/test/test_selftest_eval.c). Pure, IDF-free: no
 * heap, no shared state, every sort works on a bounded local copy. Thresholds are the Plan 5.6
 * Global Constraints, verbatim:
 *   link:    median < 100 ms, max < 500 ms, 0 failures
 *   stream:  fused >= 8/s, status >= 0.8/s, 0 gaps
 *   framing: BEGIN/END markers present, every reply line ends with exactly one CR before LF (a
 *            "\r\r\n" is a fail)
 * cmd_selftest.c (IDF glue) drives the link/timers/rawcap and calls these; it never re-implements
 * a threshold itself.
 */
#ifndef SELFTEST_EVAL_H
#define SELFTEST_EVAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `selftest link` is capped at N <= ST_LINK_MAX attempts (pragmatic-P10 bound: no heap, a fixed
 * on-stack array both here and in cmd_selftest.c). */
#define ST_LINK_MAX 64

typedef struct {
    bool     pass;
    uint32_t med_ms, max_ms, min_ms;
    int      failures;
    char     reason[64];
} st_link_t;

/* Evaluates n (<= ST_LINK_MAX) round-trip latencies in `lat_ms` -- the SUCCESSFUL attempts only,
 * already excluding any failed one -- against the link thresholds. `failures` is the count of
 * attempts that did NOT produce a lat_ms entry (a non-zero linkhost_cmd_timed result). Sorts a
 * local COPY of lat_ms (insertion sort, bounded by ST_LINK_MAX) -- never mutates the caller's
 * array, so the caller can print the original attempt order afterwards if it wants to.
 *
 * Checked in this order (first failing criterion wins, matching the brief): "max" (max_ms >=
 * 500), "median" (med_ms >= 100), "failures" (failures > 0). reason is "" on pass. n == 0 (every
 * attempt failed) reports med_ms == max_ms == min_ms == 0 and (since failures must then be > 0)
 * reason "failures". */
void selftest_link_eval(const uint32_t *lat_ms, int n, int failures, st_link_t *out);

typedef struct {
    bool     pass;
    float    fused_rate, status_rate;
    uint32_t gaps;
    char     reason[64];
} st_stream_t;

/* Evaluates one stream-sampling window: `*_delta` are linkstats_t counter deltas (after - before,
 * already clamped to >= 0 by the caller against a mid-window reset) over `secs` -- the MEASURED
 * elapsed window (linkhost_now_us() after minus before), NOT the nominal requested duration, so a
 * scheduler hiccup never silently inflates the reported rate. Checked in this order: "fused"
 * (fused_rate < 8.0), "status" (status_rate < 0.8), "gaps" (gaps_delta > 0). reason is "" on
 * pass. */
void selftest_stream_eval(uint32_t fused_delta, uint32_t status_delta, uint32_t gaps_delta,
                          float secs, st_stream_t *out);

typedef struct {
    bool pass;
    int  markers, bad;
    char reason[64];
} st_framing_t;

/* Evaluates the raw captured wire bytes of one framed reply (linkhost_trace_last_reply's rawcap
 * copy): `markers` counts "---BEGIN"/"---END" substring occurrences (2 for one ordinary reply);
 * `bad` counts lines (bounded by a trailing '\n') preceded by MORE than one '\r' -- a "\r\r\n",
 * the wire quirk documented in linkhost_parse_frame (export_serial's \n->\r\n translation doubling
 * a literal \r\n separator). Checked in this order: "markers" (no ---BEGIN or no ---END seen),
 * "cr" (bad > 0). reason is "" on pass. */
void selftest_framing_eval(const uint8_t *raw, size_t n, st_framing_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SELFTEST_EVAL_H */
