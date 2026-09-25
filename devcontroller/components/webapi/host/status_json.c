/* status_json.c -- see include/status_json.h. Pure, IDF-free; no heap, no shared state. */
#include "status_json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* M5 (final review): st->fw is decoded off the wire (linkhost_status_decode -- the lap-timer's
 * own §18.2 STATUS record), not something this side generates, so it must be treated as untrusted
 * bytes before it is spliced into a JSON string literal via "%s" below. Escapes '"' and '\\' as
 * their JSON two-character forms and drops control bytes (< 0x20) -- the exact rule jsonw_str
 * (devconsole/host/jsonw.c) already applies for every other --json string field in this tree, kept
 * as a small local copy here since status_json.c is intentionally IDF-free/dependency-free (see
 * the file's own header note) and jsonw.c lives in a different component (devconsole). `out_cap`
 * of 16 comfortably covers the worst case: st->fw is at most 7 characters (lt_status_t.fw[8], see
 * linkhost_proto.h) and every one of them could need escaping to 2 bytes each (14) plus the NUL. */
static void fw_escape(const char *fw, char *out, size_t out_cap)
{
    assert(fw != NULL);
    assert(out != NULL);
    assert(out_cap > 0);
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)fw; *p != '\0'; p++) {   /* bounded: fw is
                                                                                    * a bounded C string */
        if (*p < 0x20) continue;                 /* drop control chars rather than emit invalid JSON */
        size_t need = (*p == '"' || *p == '\\') ? 2u : 1u;
        if (w + need >= out_cap) break;           /* never overflow out[]; leave room for the NUL */
        if (need == 2u) out[w++] = '\\';
        out[w++] = (char)*p;
    }
    out[w] = '\0';
}

int status_json_format(char *buf, size_t cap, const lt_status_t *st, const char *ferr,
                       int64_t status_age_ms, int64_t stream_age_ms, bool logging)
{
    assert(buf != NULL);
    assert(st != NULL);
    assert(ferr != NULL);

    char fw_esc[16];
    fw_escape(st->fw, fw_esc, sizeof fw_esc);

    int n = snprintf(buf, cap,
                     "{\"connected\":true,\"proto\":%u,\"state\":%u,\"flags\":%u,"
                     "\"batt_pct\":%u,\"batt_mv\":%u,\"free_kb\":%lu,\"sessions\":%u,"
                     "\"fw\":\"%s\"%s,"
                     "\"status_age_ms\":%lld,\"stream_age_ms\":%lld,\"logging\":%s}",
                     (unsigned)st->proto, (unsigned)st->state, (unsigned)st->flags,
                     (unsigned)st->batt_pct, (unsigned)st->batt_mv, (unsigned long)st->free_kb,
                     (unsigned)st->sessions, fw_esc, ferr,
                     (long long)status_age_ms, (long long)stream_age_ms,
                     logging ? "true" : "false");
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}
