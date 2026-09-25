/* stage_args.c -- see include/stage_args.h. PURE `<size> <64 hex>` argument grammar (Plan 5.6
 * Task 7 Step 1-4). No heap, no I/O, bounded loops throughout. The hex decode mirrors
 * tools/ota_push.py's sha256 hex digest shape (two nibbles per output byte) -- linkhost.c's
 * sha_to_hex runs the same table in the opposite direction (encode, not decode).
 */
#include "stage_args.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SHA_HEX_LEN 64u   /* 32 bytes, two hex characters each */

/* One hex nibble, or -1 if `c` is not [0-9a-fA-F]. */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes exactly SHA_HEX_LEN hex characters (upper or lower case) from `s` into sha[0..32).
 * Returns true on success; false on a short string, a long one, or any non-hex character. */
static bool hex32_decode(const char *s, uint8_t sha[32])
{
    size_t len = strlen(s);
    if (len != SHA_HEX_LEN) return false;
    for (size_t i = 0; i < 32u; i++) {                    /* bounded: exactly 32 bytes */
        int hi = hex_nibble(s[i * 2u]);
        int lo = hex_nibble(s[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return false;
        sha[i] = (uint8_t)(((unsigned)hi << 4) | (unsigned)lo);
    }
    return true;
}

/* Decimal, fully-consumed, in (0, 0xFFFFFFFF]. Rejects leading/trailing junk, empty input, a
 * leading '-' (strtoul would otherwise silently accept and wrap it) and overflow. */
static bool size_decode(const char *s, uint32_t *out)
{
    if (s[0] == '\0' || s[0] == '-') return false;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return false;           /* not fully consumed */
    if (errno == ERANGE || v > 0xFFFFFFFFUL) return false;
    if (v == 0UL) return false;
    *out = (uint32_t)v;
    return true;
}

int stage_args_parse(int argc, char **argv, uint32_t *size, uint8_t sha[32])
{
    assert(argv != NULL || argc == 0);
    assert(size != NULL);
    assert(sha != NULL);

    if (argc != 2) return -1;
    if (!size_decode(argv[0], size)) return -2;
    if (!hex32_decode(argv[1], sha)) return -3;
    return 0;
}
