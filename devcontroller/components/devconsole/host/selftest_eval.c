/* devcontroller/components/devconsole/host/selftest_eval.c -- see include/selftest_eval.h. Pure,
 * IDF-free: no heap, no shared state, bounded loops (Plan 5.6 Task 9 / Global Constraints
 * pragmatic-P10). Builds host-side (devcontroller/test) and on the dev-kit target.
 */
#include "selftest_eval.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

/* ---- thresholds (Plan 5.6 Global Constraints, verbatim) ---- */
#define ST_LINK_MED_MS       100u   /* median < 100 ms */
#define ST_LINK_MAX_LAT_MS   500u   /* max < 500 ms */
#define ST_STREAM_FUSED_MIN  8.0f   /* fused >= 8/s */
#define ST_STREAM_STATUS_MIN 0.8f   /* status >= 0.8/s */

static void set_reason(char reason[64], const char *v)
{
    assert(reason != NULL);
    assert(v != NULL);
    size_t n = strlen(v);
    if (n >= 64) n = 63;             /* every reason literal here is well under 64 -- defensive only */
    memcpy(reason, v, n);
    reason[n] = '\0';
}

void selftest_link_eval(const uint32_t *lat_ms, int n, int failures, st_link_t *out)
{
    assert(lat_ms != NULL || n == 0);
    assert(n >= 0 && n <= ST_LINK_MAX);
    assert(out != NULL);
    memset(out, 0, sizeof *out);
    out->failures = failures;

    /* Insertion sort on a local COPY -- bounded by ST_LINK_MAX, never mutates the caller's array. */
    uint32_t sorted[ST_LINK_MAX];
    for (int i = 0; i < n; i++) sorted[i] = lat_ms[i];          /* bounded: n <= ST_LINK_MAX */
    for (int i = 1; i < n; i++) {                               /* bounded: n <= ST_LINK_MAX */
        uint32_t key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = key;
    }

    if (n > 0) {
        out->min_ms = sorted[0];
        out->max_ms = sorted[n - 1];
        out->med_ms = (n % 2 == 1) ? sorted[n / 2]
                                    : (sorted[n / 2 - 1] + sorted[n / 2]) / 2u;
    }
    /* n == 0 (every attempt failed): med/max/min stay 0 from the memset -- trivially under
     * threshold, so only the "failures" check below can fail, which it must (failures > 0). */

    const char *reason = "";
    if (out->max_ms >= ST_LINK_MAX_LAT_MS)   reason = "max";
    else if (out->med_ms >= ST_LINK_MED_MS)  reason = "median";
    else if (failures > 0)                   reason = "failures";

    out->pass = (reason[0] == '\0');
    set_reason(out->reason, reason);
}

void selftest_stream_eval(uint32_t fused_delta, uint32_t status_delta, uint32_t gaps_delta,
                          float secs, st_stream_t *out)
{
    assert(out != NULL);
    assert(secs >= 0.0f);
    memset(out, 0, sizeof *out);
    out->gaps = gaps_delta;

    /* secs is the MEASURED window; guard the degenerate/zero case (should not happen on target --
     * the console always waits >= 1 s -- but a 0/0 rate must never be a divide-by-zero). */
    float s = (secs > 0.0f) ? secs : 0.0001f;
    out->fused_rate  = (float)fused_delta  / s;
    out->status_rate = (float)status_delta / s;

    const char *reason = "";
    if (out->fused_rate < ST_STREAM_FUSED_MIN)         reason = "fused";
    else if (out->status_rate < ST_STREAM_STATUS_MIN)  reason = "status";
    else if (gaps_delta > 0)                            reason = "gaps";

    out->pass = (reason[0] == '\0');
    set_reason(out->reason, reason);
}

/* Counts non-overlapping-allowed (a naive scan; two adjacent matches at i and i+1 both count, which
 * only matters for pathological/malformed input) occurrences of `needle` in raw[0..n). Bounded by
 * n. */
static int count_occurrences(const uint8_t *raw, size_t n, const char *needle)
{
    assert(raw != NULL || n == 0);
    assert(needle != NULL);
    size_t nn = strlen(needle);
    if (nn == 0 || n < nn) return 0;
    int count = 0;
    for (size_t i = 0; i + nn <= n; i++) {          /* bounded by n */
        if (memcmp(raw + i, needle, nn) == 0) count++;
    }
    return count;
}

/* True if raw[0..n) contains `needle`. Bounded by n. Uses the same bounded scan as count_occurrences
 * to detect presence (avoiding duplicate substring-scan logic). */
static bool mem_has(const uint8_t *hay, size_t n, const char *needle)
{
    return count_occurrences(hay, n, needle) > 0;
}

/* Counts CR defects line by line over the COMPLETE lines of raw[0..n) (a line is complete only
 * when terminated by '\n' -- rawcap truncates at 256 B, so the trailing partial line is never
 * judged). A line carrying a ---BEGIN/---END marker must end in EXACTLY one CR before its LF
 * (spec §5.2); any other line is flagged only for the doubled-CR wire quirk
 * linkhost_parse_frame documents ("\r\r\n"). */
static int framing_bad_lines(const uint8_t *raw, size_t n)
{
    int bad = 0;
    size_t start = 0, cr = 0;
    for (size_t i = 0; i < n; i++) {                     /* bounded by n */
        if (raw[i] == '\r') { cr++; continue; }
        if (raw[i] != '\n') { cr = 0; continue; }
        size_t text = (i >= start + cr) ? (i - cr - start) : 0;   /* the line without its CR run */
        bool marker = mem_has(raw + start, text, "---BEGIN") || mem_has(raw + start, text, "---END");
        if (marker ? (cr != 1) : (cr > 1)) bad++;
        start = i + 1;
        cr = 0;
    }
    return bad;
}

void selftest_framing_eval(const uint8_t *raw, size_t n, st_framing_t *out)
{
    assert(raw != NULL || n == 0);
    assert(out != NULL);
    memset(out, 0, sizeof *out);

    int begins = count_occurrences(raw, n, "---BEGIN");
    int ends   = count_occurrences(raw, n, "---END");
    out->markers = begins + ends;

    out->bad = framing_bad_lines(raw, n);

    const char *reason = "";
    if (begins < 1 || ends < 1) reason = "markers";
    else if (out->bad > 0)       reason = "cr";

    out->pass = (reason[0] == '\0');
    set_reason(out->reason, reason);
}
