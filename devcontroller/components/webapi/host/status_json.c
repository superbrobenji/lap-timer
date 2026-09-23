/* status_json.c -- see include/status_json.h. Pure, IDF-free; no heap, no shared state. */
#include "status_json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int status_json_format(char *buf, size_t cap, const lt_status_t *st, const char *ferr,
                       int64_t status_age_ms, int64_t stream_age_ms, bool logging)
{
    assert(buf != NULL);
    assert(st != NULL);
    assert(ferr != NULL);

    int n = snprintf(buf, cap,
                     "{\"connected\":true,\"proto\":%u,\"state\":%u,\"flags\":%u,"
                     "\"batt_pct\":%u,\"batt_mv\":%u,\"free_kb\":%lu,\"sessions\":%u,"
                     "\"fw\":\"%s\"%s,"
                     "\"status_age_ms\":%lld,\"stream_age_ms\":%lld,\"logging\":%s}",
                     (unsigned)st->proto, (unsigned)st->state, (unsigned)st->flags,
                     (unsigned)st->batt_pct, (unsigned)st->batt_mv, (unsigned long)st->free_kb,
                     (unsigned)st->sessions, st->fw, ferr,
                     (long long)status_age_ms, (long long)stream_age_ms,
                     logging ? "true" : "false");
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}
