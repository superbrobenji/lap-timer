#ifndef CORE_UI_FONTS_H
#define CORE_UI_FONTS_H
#include <stdint.h>

/* Bitmap font (spec §20.2): 1bpp glyph cells, MSB-first, row-major, bit=1 is glyph ink.
 * Every glyph in a font shares one cell size w x h and a fixed advance equal to w (monospace).
 * `first`/`count` record the font's minimum ASCII code point and glyph count for reference only;
 * BIG and MED store a non-contiguous character subset densely (sorted by code point), so a
 * caller must never index bitmaps with `c - first` directly. Use font_glyph_index() instead,
 * which consults the per-font char->glyph-index map that gen_fonts.py emits alongside the
 * bitmaps in fonts.c. */
typedef struct {
    uint8_t        w, h;      /* glyph cell size in px (fixed advance = w) */
    uint8_t        first, count; /* first ASCII code, number of glyphs */
    uint8_t        stride;    /* bytes per glyph row = (w + 7) / 8 */
    const uint8_t *bitmaps;   /* count * h * stride bytes, 1bpp, MSB-first, row-major */
} font_t;

extern const font_t FONT_BIG;   /* 40 px: glyphs "0-9 : . - + S" */
extern const font_t FONT_MED;   /* 24 px: "0-9 : . - + A-Z" */
extern const font_t FONT_SMALL; /* 12 px: ASCII 32..126 */

/* Maps character `c` to its glyph index (0..font->count-1) into font->bitmaps, via the per-font
 * char->glyph-index map generated alongside the bitmap tables. Returns -1 when `c` has no glyph
 * in `font` (the renderer draws a blank cell in that case). `font` must be one of
 * &FONT_BIG/&FONT_MED/&FONT_SMALL. */
int font_glyph_index(const font_t *font, char c);

#endif
