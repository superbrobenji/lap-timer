/* devcontroller/components/logstore/host/logstore_json.c -- pure, IDF-free on-flash-record ->
 * NDJSON transcode (issue #67; see include/logstore_rec.h for the contract + the pinned
 * logstore_rec_hdr_t layout parsed here). Reuses linkhost_stream_to_json (components/linkhost,
 * also pure) for the actual field decode: this file only unwraps logstore's own record framing
 * (logstore_rec_hdr_t + `len` payload bytes) and maps it onto an lt_stream_rec_t. No
 * esp_* / littlefs -- devcontroller/test/test_logstore_json.c links this directly on the host;
 * webapi.c's do_log_download jsonl path calls it on-target too (component link: see
 * components/logstore/CMakeLists.txt's PRIV_REQUIRES linkhost).
 */
#include "logstore_rec.h"

#include <assert.h>
#include <string.h>

#include "linkhost_proto.h"   /* lt_stream_rec_t, linkhost_stream_to_json, LT_REC_MAX */

int logstore_rec_to_json(const uint8_t *buf, size_t avail, char *out, size_t out_cap,
                          size_t *consumed)
{
    assert(buf != NULL);
    assert(out != NULL);
    assert(consumed != NULL);
    *consumed = 0;

    if (avail < sizeof(logstore_rec_hdr_t)) return LOGSTORE_JSON_NEED_MORE;

    logstore_rec_hdr_t hdr;
    memcpy(&hdr, buf, sizeof hdr);   /* buf may be unaligned (a sliding stream buffer) */

    if (hdr.len > LT_REC_MAX) return LOGSTORE_JSON_ERR;

    size_t rec_size = sizeof hdr + (size_t)hdr.len;
    if (avail < rec_size) return LOGSTORE_JSON_NEED_MORE;

    lt_stream_rec_t rec;
    memset(&rec, 0, sizeof rec);
    rec.seq   = hdr.seq;
    rec.flags = hdr.flags;
    rec.type  = hdr.type;
    rec.len   = hdr.len;
    if (hdr.len > 0) memcpy(rec.data, buf + sizeof hdr, hdr.len);

    int n = linkhost_stream_to_json(&rec, out, out_cap);
    *consumed = rec_size;   /* advance past the record either way -- SKIP too */
    return (n < 0) ? LOGSTORE_JSON_SKIP : n;
}
