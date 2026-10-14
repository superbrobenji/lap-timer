/* devcontroller/components/webapi/host/sse_cursor.c -- see sse_cursor.h. */
#include "sse_cursor.h"

#include <assert.h>

uint32_t sse_cursor_fix(uint32_t head, uint32_t cursor, uint32_t cap)
{
    assert(cap > 0);
    /* Distance >= cap means the producer has lapped this reader: at distance EXACTLY cap the
     * cursor's slot (cursor & (cap-1)) is the head's own slot -- the next one the producer writes
     * -- so reading it is a guaranteed torn read. Skip to cap-2 records behind the head: past the
     * producer's slot plus one record of margin (dk:M7). */
    return ((uint32_t)(head - cursor) >= cap) ? (uint32_t)(head - cap + 2u) : cursor;
}
