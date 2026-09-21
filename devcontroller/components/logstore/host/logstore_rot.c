/* devcontroller/components/logstore/host/logstore_rot.c -- PURE rotation-decision logic for
 * logstore (Plan 5.5 Task 5). See include/logstore_rot.h for the contract. No esp_* / LittleFS /
 * UART includes -- this file (and only this file, plus <stdint.h>/<stddef.h>/<stdbool.h>/
 * <assert.h>) is what devcontroller/test/test_logstore.c exercises directly on the host.
 */
#include "logstore_rot.h"

#include <assert.h>

bool logstore_should_rotate(uint32_t cur_total, uint32_t cur_file_bytes, uint32_t incoming_bytes,
                             uint32_t cap_bytes, uint32_t max_file_bytes)
{
    (void)cur_total;   /* total-cap accounting is logstore_pick_drop's job, not this decision's */
    (void)cap_bytes;
    assert(max_file_bytes > 0);   /* a zero cap would make every append rotate -- caller bug */

    /* 64-bit sum so a near-UINT32_MAX cur_file_bytes/incoming_bytes pair can't wrap past
     * max_file_bytes and falsely report "no rotation needed". */
    uint64_t after = (uint64_t)cur_file_bytes + (uint64_t)incoming_bytes;
    return after > (uint64_t)max_file_bytes;
}

int logstore_pick_drop(const logstore_file_info_t *files, int count, uint32_t incoming_bytes,
                        uint32_t cap_bytes, int *drop_idx, int drop_idx_cap)
{
    assert(files != NULL || count <= 0);
    assert(drop_idx != NULL || drop_idx_cap <= 0);

    if (count < 0) count = 0;
    if (count > LOGSTORE_ROT_MAX_FILES) count = LOGSTORE_ROT_MAX_FILES;   /* rule 2: hard cap */
    if (drop_idx_cap < 0) drop_idx_cap = 0;

    uint64_t total = (uint64_t)incoming_bytes;
    for (int i = 0; i < count; i++) total += files[i].bytes;

    /* Walk oldest-first (index 0 upward), never touching the last (newest/current) entry --
     * `i < count - 1` both bounds the loop (rule 2) and enforces "always keep the current file".
     * A `count` of 0 or 1 makes the loop body unreachable, so nothing is ever dropped. */
    int n_written = 0;
    for (int i = 0; i < count - 1 && total > (uint64_t)cap_bytes && n_written < drop_idx_cap; i++) {
        drop_idx[n_written] = i;
        n_written++;
        total -= files[i].bytes;
    }
    return n_written;
}
