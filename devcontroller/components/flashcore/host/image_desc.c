/* image_desc.c -- the PURE app-image header parser behind POST /api/flash and (later) the
 * console's `flash stage` (Plan 5.5 Task 6; moved into flashcore in Plan 5.6 Task 7).
 * Reads the two fields the lap-timer's `ota recv` line needs (version + hardware id) straight out
 * of the staged image, at the fixed spec 19.3 offsets. Kept IDF-free (offsets, not
 * esp_app_desc.h) so devcontroller/test/test_image_desc.c links it on the host.
 * Mirrors tools/ota_push.py extract_ver_hwid. */
#include "image_desc.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define DESC_OFF    0x20u        /* esp_app_desc_t */
#define DESC_MAGIC  0xABCD5432u  /* ESP_APP_DESC_MAGIC_WORD, u32 little-endian */
#define VER_OFF     0x30u        /* esp_app_desc_t.version[32] */
#define VER_FIELD   32u
#define HWID_OFF    0x120u       /* spec 19.3 hwid custom descriptor, 24 B */

/* Both fields are pasted verbatim onto an `ota recv <size> <sha> <ver> <hwid>` console line, so
 * each must survive esp_console_split_argv as ONE token: printable, no whitespace, and no quote
 * or backslash (which would rewrite the surrounding arguments rather than be escaped). */
static bool field_ok(const char *s)
{
    if (s[0] == '\0') return false;
    for (const char *p = s; *p != '\0'; p++) {          /* bounded: field_copy NUL-terminated it */
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x21u || ch > 0x7Eu) return false;
        if (ch == '"' || ch == '\\') return false;
    }
    return true;
}

/* Copies a fixed-width ASCII field, truncated at the first NUL, into out[cap] (cap includes the
 * terminator). A field with no NUL at all is taken whole when it fits -- the 24 B hwid descriptor
 * is a char[24] that a 24-character hwid fills exactly. */
static void field_copy(const uint8_t *src, size_t field, char *out, size_t cap)
{
    size_t max = cap - 1u;
    if (field < max) max = field;
    size_t i = 0;
    while (i < max && src[i] != 0u) i++;                /* bounded by max */
    memcpy(out, src, i);
    out[i] = '\0';
}

int img_desc_parse(const uint8_t *hdr, size_t n, char ver[IMG_VER_LEN], char hwid[IMG_HWID_LEN + 1])
{
    assert(hdr != NULL);
    assert(ver != NULL);
    assert(hwid != NULL);

    ver[0] = '\0';
    hwid[0] = '\0';
    if (n < (size_t)IMG_DESC_MIN_LEN) return -1;

    uint32_t magic = (uint32_t)hdr[DESC_OFF] |
                     ((uint32_t)hdr[DESC_OFF + 1u] << 8) |
                     ((uint32_t)hdr[DESC_OFF + 2u] << 16) |
                     ((uint32_t)hdr[DESC_OFF + 3u] << 24);
    if (magic != DESC_MAGIC) return -1;

    field_copy(hdr + VER_OFF, VER_FIELD, ver, IMG_VER_LEN);
    if (!field_ok(ver)) return -2;

    field_copy(hdr + HWID_OFF, IMG_HWID_LEN, hwid, IMG_HWID_LEN + 1u);
    if (!field_ok(hwid)) return -3;

    return 0;
}
