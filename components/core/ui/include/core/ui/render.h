#ifndef CORE_UI_RENDER_H
#define CORE_UI_RENDER_H
#include <stdbool.h>
#include <stdint.h>

#include "core/ui/fonts.h"
#include "core/ui/icons.h"

/* Framebuffer + drawing primitives (spec §20.2). Pure C11, no malloc/float/libm — every primitive
 * clips to the framebuffer bounds and is a pure function of its arguments and the caller-owned
 * `bits` buffer, which is what makes the PBM goldens in test/snapshots/ deterministic and portable
 * across clang and gcc.
 *
 * Convention: `0 = black` in the framebuffer (e-paper convention; inverted at blit time by the
 * display driver in a later plan). Every primitive that takes a `black`/`pct` "ink" argument
 * treats a nonzero value as "set to ink" (black, bit 0) and zero as "set to background" (white,
 * bit 1) — the opposite sense of the raw bit value. This is the opposite of fonts.h/icons.h,
 * where bit=1 in a glyph/icon bitmap means ink; render.c performs that translation when it blits
 * a glyph or icon cell.
 */

typedef struct {
    uint16_t x0, y0, x1, y1; /* half-open: [x0,x1) x [y0,y1) */
    bool     valid;          /* false = no draw has happened yet (nothing is dirty) */
} fb_rect_t;

typedef struct {
    uint8_t  *bits;   /* caller-owned, (w/8)*h bytes; 0 = black */
    uint16_t  w, h;   /* pixels; w must be a multiple of 8 */
    uint16_t  stride; /* bytes per row = w/8 */
    fb_rect_t dirty;  /* accumulated bounding box of all draws since fb_clear */
} fb_t;

/* Initialises fb to draw into `bits` ((w/8)*h caller-owned bytes, uninitialised). w must be a
 * multiple of 8. Does not touch the contents of `bits` — call fb_clear to establish a known
 * starting color. dirty starts invalid (nothing drawn yet). */
void fb_init(fb_t *fb, uint8_t *bits, uint16_t w, uint16_t h);

/* Fills the whole framebuffer with `black` (nonzero = all ink/black, 0 = all background/white)
 * and resets dirty to the whole frame (a clear touches every pixel). */
void fb_clear(fb_t *fb, uint8_t black);

/* Draws a w x h rectangle at (x,y): filled solid if `fill`, else a 1px outline. `black` selects
 * ink (nonzero) or background (0). Clipped to the framebuffer bounds; a fully off-frame rect is a
 * no-op that leaves `dirty` unchanged. */
void fb_rect(fb_t *fb, int x, int y, int w, int h, uint8_t black, bool fill);

/* Draws a 1px-tall, w-px-wide horizontal line at (x,y). Same clipping/dirty rules as fb_rect. */
void fb_hline(fb_t *fb, int x, int y, int w, uint8_t black);

/* Draws `s` in font `f` starting at pen position (x,y) (y = top of the glyph cell), one fixed-
 * width cell (f->w x f->h) per character, each cell blitted opaquely (ink pixels set to black,
 * non-ink pixels set to background) and clipped per-pixel to the framebuffer bounds — a glyph
 * cell that only partially overlaps the frame still draws its visible portion, never writing out
 * of bounds. A character with no glyph in `f` (font_glyph_index returns -1) draws a blank
 * (all-background) cell so the fixed advance is preserved. Returns the pen x after the last
 * character (x + strlen(s) * f->w). */
int fb_text(fb_t *fb, const font_t *f, int x, int y, const char *s);

/* Same as fb_text, right-aligned so the string's right edge lands at x_right (i.e. it draws as if
 * called with x = x_right - strlen(s) * f->w). Returns that computed left x. */
int fb_text_right(fb_t *fb, const font_t *f, int x_right, int y, const char *s);

/* Blits the ICON_W x ICON_H icon `icon_id` at (x,y), same opaque per-pixel-clipped blit as a
 * glyph cell. icon_id >= ICON_COUNT is a no-op. */
void fb_icon(fb_t *fb, uint8_t icon_id, int x, int y);

/* Draws a w x h outlined bar at (x,y) (1px ink border) whose interior is filled with ink from the
 * left across `pct` percent of the interior width (pct is clamped to [0,100]; the remainder of
 * the interior is drawn as background), so the bar's appearance does not depend on what was
 * previously in the framebuffer. Clipped like fb_rect. */
void fb_bar(fb_t *fb, int x, int y, int w, int h, uint8_t pct);

#endif
