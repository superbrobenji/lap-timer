/* app/status.h -- §18.2 STATUS record builder + the free-space/session-count cache it reads
 * (Plan 5.6 T1 fix 1).
 *
 * status_build() is storage-free -- it touches only sys_flags_get() and this file's own atomic
 * cache, never hal/storage.h -- so it is safe to call from ANY task: cmd.c's op_status (the
 * console task, the framed `status` reply) and pipeline.c (the pipeline task, the 1 Hz stream
 * push, Plan 5.6 §4.1). Before this fix status_build() called sto_info()/sto_list_* directly,
 * which violated hal/storage.h's "called from one task only (logger, plus cmd for read-only
 * listing/export)" once the pipeline task started calling it too.
 *
 * The cache is kept fresh by the logger task (components/app/logger/logger.c), the storage
 * owner: it calls status_cache_update() after open_session/close_session/eviction_check and on
 * a 5 s cadence, so a peer never sees free_kb/sessions more than ~5 s stale (session open/close
 * and eviction refresh it immediately on the events that actually move those numbers).
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include "app/lt_proto.h"   /* LT_STATUS_LEN */

#include <stdint.h>

/* Builds the §18.2 STATUS record (LT_STATUS_LEN bytes). Shared by the framed `status` reply
 * (cmd.c's op_status) and the 1 Hz stream push (pipeline.c, Plan 5.6) so the two can never
 * drift. No I/O, no assertion of caller identity: safe from any task. */
void status_build(uint8_t out[LT_STATUS_LEN]);

/* Refresh the free_kb/sessions cache status_build() reads (single-word atomics, no lock). Called
 * by the logger task -- the sole hal/storage.h owner -- after anything that can move either
 * number (session open, session close, eviction) and on its own periodic cadence besides. */
void status_cache_update(uint32_t free_kb, uint16_t sessions);

#endif /* APP_STATUS_H */
