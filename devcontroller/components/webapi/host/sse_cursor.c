/* devcontroller/components/webapi/host/sse_cursor.c -- see sse_cursor.h. */
#include "sse_cursor.h"

#include <assert.h>

uint32_t sse_cursor_fix(uint32_t head, uint32_t cursor, uint32_t cap)
{
    /* `cap > 0` alone understates what this function actually requires: the caller masks with
     * `cursor & (cap - 1)`, which behaves as a modulus only for a power of two, and the resync
     * below returns `head - cap + 2`, which only lands BEHIND head (the torn-read slot this
     * function exists to avoid) for cap >= 3 -- at cap == 2 it would return head itself, the
     * producer's own slot. BC_CAP (webapi.c) is 64, so this is unreachable in practice. */
    assert(cap >= 4 && (cap & (cap - 1u)) == 0);
    /* Distance >= cap means the producer has lapped this reader: at distance EXACTLY cap the
     * cursor's slot (cursor & (cap-1)) is the head's own slot -- the next one the producer writes
     * -- so reading it is a guaranteed torn read. Skip to cap-2 records behind the head: past the
     * producer's slot plus one record of margin (dk:M7). */
    return ((uint32_t)(head - cursor) >= cap) ? (uint32_t)(head - cap + 2u) : cursor;
}
