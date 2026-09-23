/* devcontroller/components/logstore/include/logstore_rec.h -- the on-flash record header
 * logstore_append() writes ahead of every record's payload (Plan 5.5 Task 5), plus the pure,
 * IDF-free NDJSON transcode helper (issue #67) that a host test and webapi's do_log_download
 * jsonl path both need without pulling in esp_* / littlefs. Split out of logstore.h (which drags in
 * esp_err.h) so this header stays includable from host/logstore_json.c and
 * devcontroller/test/test_logstore_json.c -- only <stddef.h>/<stdint.h> here, ever.
 *
 * PINNED layout: logstore_rec_hdr_t's exact byte layout (field order, size, packing) must never
 * change -- logstore_append() has already written it to flash in every existing log file, and
 * logstore_rec_to_json (below) parses OLD logs with this same struct. If a future record framing
 * needs new fields, add a new versioned wrapper rather than editing this struct in place; do not
 * "just add a field" here.
 */
#ifndef LOGSTORE_REC_H
#define LOGSTORE_REC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Precedes every appended record's `len` payload bytes (logstore_append, logstore.c). */
typedef struct __attribute__((packed)) {
    uint64_t ts_us;   /* esp_timer_get_time() at append time: monotonic microseconds since boot */
    uint16_t seq;
    uint8_t  flags;
    uint8_t  type;
    uint8_t  len;
} logstore_rec_hdr_t;

/* sizeof(logstore_rec_hdr_t) with the packed attribute above; logstore.c _Static_asserts its
 * private on-disk knowledge against this so a layout change fails the build loudly rather than
 * silently corrupting every future record read. */
#define LOGSTORE_REC_HDR_LEN 13u
_Static_assert(sizeof(logstore_rec_hdr_t) == LOGSTORE_REC_HDR_LEN,
               "logstore_rec_hdr_t layout changed -- old logs would become unreadable");

/* Byte offset, within a log file as opened by logstore_open_read(), where the first
 * logstore_rec_hdr_t begins. Every file starts with an internal logstore_file_hdr_t (magic +
 * created_unix, private to logstore.c) that a raw `?fmt=bin` download includes verbatim but a
 * jsonl transcode reader must skip before calling logstore_rec_to_json. logstore.c
 * _Static_asserts its private file-header struct against this constant. */
#define LOGSTORE_REC_AREA_OFFSET 8u

/* ================================================================================================
 *  issue #67: pure, IDF-free on-flash-record -> NDJSON transcode. Host-testable
 *  (devcontroller/test/test_logstore_json.c links host/logstore_json.c directly); webapi.c's
 *  do_log_download jsonl path calls the same function on-target. Reuses linkhost_stream_to_json
 *  (components/linkhost, also pure) for the actual field decode -- this function only unwraps
 *  logstore's own record framing and maps it onto an lt_stream_rec_t.
 * ============================================================================================== */
enum {
    LOGSTORE_JSON_ERR       = -1,   /* malformed record (hdr.len > LT_REC_MAX) */
    LOGSTORE_JSON_NEED_MORE = 0,    /* a full record isn't buffered yet; *consumed left at 0 */
    LOGSTORE_JSON_SKIP      = -2,   /* complete record, but its type has no JSON rendering */
};

/* Transcodes ONE on-flash record at buf[0..avail) to an NDJSON object (no trailing newline).
 * Returns:
 *   >0                       json byte count written to out; *consumed = full record size
 *                            (sizeof(logstore_rec_hdr_t) + hdr.len).
 *   LOGSTORE_JSON_NEED_MORE  avail doesn't yet hold a full record; *consumed = 0. Caller should
 *                            buffer more bytes and retry (unless already at EOF).
 *   LOGSTORE_JSON_SKIP       a complete record whose type the decoder can't render; *consumed is
 *                            still advanced past it (nothing written to out) so the caller makes
 *                            forward progress.
 *   LOGSTORE_JSON_ERR        malformed (hdr.len > LT_REC_MAX); *consumed = 0, caller should stop.
 * Bounded (no loops), no heap. */
int logstore_rec_to_json(const uint8_t *buf, size_t avail, char *out, size_t out_cap,
                          size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* LOGSTORE_REC_H */
