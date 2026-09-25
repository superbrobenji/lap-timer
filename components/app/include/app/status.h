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
 * owner. sessions is name-only-counted once at logger start (status_cache_prime()); after that it
 * only moves on the two events that can actually change the on-flash .sum count: +1 on
 * close_session, once its .sum has landed, and a full recount on LOGGER_RECOUNT (posted by
 * cmd.c's DELETE after it unlinks a .sum -- close_session's increment alone would otherwise miss
 * every delete). free_kb is a real storage_free_kb() read at those same events (prime / close /
 * recount) plus every eviction pass (its own ~60 s tick, sooner if LOGGER_EVICT forces one), and
 * is estimated from .log bytes appended since the last real read the rest of the time
 * (status_cache_estimate()). Either way, status_cache_update() runs every logger loop tick, so a
 * peer is never more than one loop tick behind whatever the cache last held -- not a fixed
 * cadence on free_kb/sessions themselves, which only change on the events above.
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
 * by the logger task -- the sole hal/storage.h owner -- every loop tick: with a real reading at
 * prime / close_session / eviction / recount (the events that can actually move either number,
 * see this file's header comment) and an estimate the rest of the time
 * (logger.c's status_cache_estimate()). */
void status_cache_update(uint32_t free_kb, uint16_t sessions);

#endif /* APP_STATUS_H */
