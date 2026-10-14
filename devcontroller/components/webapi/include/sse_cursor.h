/* devcontroller/components/webapi/include/sse_cursor.h -- the PURE SSE-overrun-guard arithmetic
 * for GET /api/stream's live-monitor loop (F3 Task 4 Step 3, dk:M7).
 *
 * do_stream (webapi.c) keeps a reader cursor into the fixed-size ring s_bc[BC_CAP]; the producer
 * (webapi_stream_push) advances s_bc_head and never blocks for a slow/stalled reader, so a reader
 * that falls far enough behind can be overrun: the ring has already wrapped under it and the slot
 * its cursor now points at holds a DIFFERENT, newer record than the one the cursor's distance
 * implies. sse_cursor_fix is the host-testable guard that detects this and resyncs the cursor;
 * it has no IDF dependency so devcontroller/test/test_sse_cursor.c links it directly.
 */
#ifndef SSE_CURSOR_H
#define SSE_CURSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a cursor safe to read from s_bc[cursor & (cap-1)] given the producer is at `head`.
 *
 * All three arguments are the same uint32_t counters do_stream already carries (head == s_bc_head,
 * cap == BC_CAP); head and cursor wrap at 2^32 like any other free-running counter, so the distance
 * below is computed with unsigned-wraparound subtraction (head - cursor), which is correct even
 * once head has wrapped past 2^32 and cursor has not: aliasing is deliberate, not a bug.
 *
 * At distance == cap, cursor's slot (cursor & (cap-1)) is congruent mod cap to head's own slot --
 * the exact slot the producer writes next -- so reading it races the producer and is a guaranteed
 * torn read. At any distance >= cap the ring has already wrapped under the reader at least once,
 * so cursor no longer names the record it thinks it does. Both cases resync to head - cap + 2:
 * cap - 2 records behind head, i.e. past the producer's own slot plus one extra record of margin
 * (dk:M7 -- the old guard used `>`, which let distance == cap through uncaught). A cursor inside
 * the window (distance < cap) is returned unchanged. */
uint32_t sse_cursor_fix(uint32_t head, uint32_t cursor, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SSE_CURSOR_H */
