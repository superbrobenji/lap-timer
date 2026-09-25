/* devcontroller/components/devconsole/host/flash_fmt.c -- see include/flash_fmt.h. Pure,
 * IDF-free: no heap, no shared state.
 */
#include "flash_fmt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

size_t flash_result_str(int result, char *out, size_t cap)
{
    assert(out != NULL || cap == 0);
    if (cap == 0) return 0;

    /* Widest possible rendering: "link -2147483648" (INT_MIN) is 16 chars; 24 gives headroom
     * without being a magic-fit size. snprintf into a local scratch buffer first so the
     * cap-bounded copy below is a single bounded memcpy, not a second format pass. */
    char tmp[24];
    int n = (result >= 0) ? snprintf(tmp, sizeof tmp, "0x%04x", (unsigned)result)
                          : snprintf(tmp, sizeof tmp, "link %d", result);
    assert(n >= 0 && (size_t)n < sizeof tmp);   /* tmp is sized to never truncate here */

    size_t len  = (size_t)n;
    size_t copy = (len < cap) ? len : cap - 1u;   /* leave room for the NUL at out[cap-1] */
    memcpy(out, tmp, copy);
    out[copy] = '\0';
    return copy;
}
