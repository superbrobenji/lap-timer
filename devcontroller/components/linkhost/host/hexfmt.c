/* hexfmt.c -- see include/hexfmt.h. Pure, IDF-free (Plan 5.6 Task 5): no heap, no shared state,
 * bounded to 32 formatted bytes. Built both into the linkhost IDF component (linkhost.c's `link
 * trace` timeout hexdump) and directly by the host test (devcontroller/test/test_hexfmt.c).
 */
#include "hexfmt.h"

#include <assert.h>
#include <string.h>

#define HEXFMT_MAX_BYTES 32u
static const char HEXFMT_DIGITS[] = "0123456789abcdef";
/* " " + U+2026 HORIZONTAL ELLIPSIS, UTF-8 encoded (0xE2 0x80 0xA6): appended after the 32nd byte
 * when the caller has more than HEXFMT_MAX_BYTES to show. */
static const char HEXFMT_ELLIPSIS[] = { ' ', '\xE2', '\x80', '\xA6' };

size_t hexfmt_line(const uint8_t *b, size_t n, char *out, size_t cap)
{
    assert(b != NULL || n == 0);
    assert(out != NULL || cap == 0);
    if (cap == 0) return 0;

    size_t show = (n > HEXFMT_MAX_BYTES) ? HEXFMT_MAX_BYTES : n;
    size_t w = 0;
    for (size_t i = 0; i < show; i++) {                   /* bounded: <= HEXFMT_MAX_BYTES */
        size_t need = (i > 0) ? 3u : 2u;                   /* leading space (but the first pair) + 2 hex digits */
        if (w + need > cap - 1u) break;                    /* leave room for the NUL at out[cap-1] */
        if (i > 0) out[w++] = ' ';
        uint8_t byte = b[i];
        out[w++] = HEXFMT_DIGITS[(byte >> 4) & 0x0Fu];
        out[w++] = HEXFMT_DIGITS[byte & 0x0Fu];
    }
    if (n > HEXFMT_MAX_BYTES && w + sizeof(HEXFMT_ELLIPSIS) <= cap - 1u) {
        memcpy(out + w, HEXFMT_ELLIPSIS, sizeof(HEXFMT_ELLIPSIS));
        w += sizeof(HEXFMT_ELLIPSIS);
    }
    assert(w < cap);
    out[w] = '\0';
    return w;
}
