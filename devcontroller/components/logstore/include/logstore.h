/* devcontroller/components/logstore/include/logstore.h -- logstore: the bounded, rotating
 * black-box log on B's own `logs` LittleFS partition
 * (docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md, sub-project B, Task 5).
 *
 * PINNED interface for Task 5 (and its Task 4/webapi consumer: GET /api/logs, GET /api/log/<id>).
 * Task 2 (this scaffold) ships a compiling not-implemented stub; Task 5 implements
 * append-with-rotation for real.
 */
#ifndef LOGSTORE_H
#define LOGSTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "linkhost.h"   /* lt_stream_rec_t */

#ifdef __cplusplus
extern "C" {
#endif

/* one rotated log file, as listed for GET /api/logs. */
typedef struct {
    char     id[24];          /* monotonic log-file id; also its filename stem */
    uint32_t size;             /* bytes */
    uint32_t created_unix;     /* seconds since epoch when the file was opened */
} logstore_entry_t;

/* opaque read handle from logstore_open_read (a POSIX fd under the hood; mirrors the lap-timer's
 * hal/storage.h sto_file_t convention). */
typedef int logstore_file_t;

/* Mounts/prepares the `logs` partition and opens the current log. cap_bytes bounds total log
 * storage; logstore_append rotates (deletes the oldest file) rather than exceed it. */
esp_err_t logstore_init(size_t cap_bytes);

/* Appends one demuxed stream record (framed with a timestamp) to the current log; rotates when
 * cap_bytes would be exceeded. Returns 0 on success, <0 on error. */
int logstore_append(const lt_stream_rec_t *rec);

/* Fills out[] with up to `max` log entries (newest first), returns the count written. */
int logstore_list(logstore_entry_t *out, int max);

/* True once logstore_init has opened a current file and logstore_append is accepting records;
 * false before init, or once an append failure has wedged it (Plan 5.6 T3, /api/status "logging").
 * Safe to call from any task -- s_ready is read cross-task from the stream_consumer writer. */
bool logstore_ready(void);

/* Opens a previously-rotated (or current) log file for reading by id; *out is a read handle
 * usable with the standard read()/close() calls. Returns 0 on success, <0 on error. */
int logstore_open_read(const char *id, logstore_file_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LOGSTORE_H */
