/* devcontroller/components/logstore/include/logstore_rot.h -- PURE, IDF-free rotation-decision
 * logic for logstore (Plan 5.5 Task 5: docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md).
 *
 * These functions decide whether/what to rotate over plain sizes and counts -- no esp_* /
 * LittleFS / UART dependency, so they build and run on the host (devcontroller/test/
 * test_logstore.c links devcontroller/components/logstore/host/logstore_rot.c directly, per the
 * devcontroller host-test harness convention: components/<c>/host/ *.c is pure, components/<c>/
 * <c>.c is the IDF/LittleFS glue that calls into it). Only <stdint.h>/<stddef.h>/<stdbool.h> --
 * never esp_* / littlefs -- may be included here.
 */
#ifndef LOGSTORE_ROT_H
#define LOGSTORE_ROT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bound on how many rotated (+ current) log files logstore.c ever tracks at once: a scan/pick
 * cap (rule 2), not a hard on-disk limit. logstore.c's real file counts stay far below this
 * (cap_bytes / max_file_bytes, e.g. ~14 files for the 900 KB/64 KB defaults) so the bound is
 * never actually hit in practice. */
#define LOGSTORE_ROT_MAX_FILES 64

/* One log file's id (its filename stem) + current size, as tracked for a rotation decision. */
typedef struct {
    char     id[24];
    uint32_t bytes;
} logstore_file_info_t;

/* True when appending `incoming_bytes` to the current file (currently `cur_file_bytes` long)
 * would exceed `max_file_bytes` -- i.e. the caller should close the current file and roll to a
 * new one before writing this record. `cur_total`/`cap_bytes` are accepted for a stable,
 * symmetrical signature with the total-cap accounting logstore.c also does, but this decision is
 * purely about the *current file's* size vs `max_file_bytes` -- they are unused here. */
bool logstore_should_rotate(uint32_t cur_total, uint32_t cur_file_bytes, uint32_t incoming_bytes,
                             uint32_t cap_bytes, uint32_t max_file_bytes);

/* Given `count` existing files in `files[]` (ordered OLDEST-FIRST -- index 0 is the oldest,
 * index count-1 is the newest/current file, matching a monotonic log-file id ordering) plus
 * `incoming_bytes` about to be added, fills `drop_idx[]` with the indices (into `files[]`, in
 * oldest-first order) of the oldest files a caller should delete so that the remaining total
 * (every file NOT dropped, plus incoming_bytes) fits within `cap_bytes`.
 *
 * The newest entry (index count-1, the file currently being written) is never selected for
 * dropping, even if the cap still can't be met without it -- logstore always keeps at least the
 * current file. Bounded by min(count, LOGSTORE_ROT_MAX_FILES, drop_idx_cap).
 *
 * Returns the number of indices written into drop_idx[] (0 if nothing needs to be dropped, or if
 * count <= 1 since there is nothing droppable besides the newest entry). */
int logstore_pick_drop(const logstore_file_info_t *files, int count, uint32_t incoming_bytes,
                        uint32_t cap_bytes, int *drop_idx, int drop_idx_cap);

#ifdef __cplusplus
}
#endif

#endif /* LOGSTORE_ROT_H */
