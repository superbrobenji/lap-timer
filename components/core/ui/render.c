/* Framebuffer + drawing primitives (spec §20.2). See core/ui/render.h for the contract and the
 * `0 = black` / opaque-blit conventions. No malloc, no float/libm: every primitive computes with
 * plain integer arithmetic so the PBM snapshots in test/snapshots/ are byte-identical across
 * compilers. */
#include "core/ui/render.h"
#include "core/core.h"

#include <string.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions report UI_ASSERT_CODE.
 * They guard genuine anomalies -- NULL params, a framebuffer dimension that breaks the `w` % 8 == 0
 * contract, a post-clip region that somehow still falls outside the frame -- never the ordinary
 * per-pixel clipping every primitive here already performs, which is this module's whole job and
 * is exercised by the byte-exact PBM goldens on every draw, on- or off-frame alike; converting that
 * clipping into an assertion would fire the fault hook on routine, correct rendering. */
#define UI_ASSERT_CODE 0x0AA0

/* Sets one pixel to ink (black, nonzero `black`) or background (white, black == 0). Out-of-bounds
 * (x,y) is a silent no-op — this is the hard backstop against OOB writes; every caller in this
 * file additionally pre-clips its draw region so this check should never actually trigger, but it
 * makes fb_set_px safe to call unconditionally. */
static void fb_set_px(fb_t *fb, int x, int y, uint8_t black)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (x < 0 || y < 0 || x >= (int)fb->w || y >= (int)fb->h) {
        return;
    }
    uint8_t *byte = &fb->bits[(uint16_t)y * fb->stride + (uint16_t)(x / 8)];
    uint8_t  mask = (uint8_t)(0x80u >> (x % 8));
    if (black) {
        *byte = (uint8_t)(*byte & (uint8_t)~mask); /* 0 = black */
    } else {
        *byte = (uint8_t)(*byte | mask); /* 1 = white/background */
    }
}

/* Unions the clipped, already-in-bounds region [x0,x1) x [y0,y1) into fb->dirty. A zero-area
 * region (nothing actually drawn) leaves dirty untouched. */
static void fb_extend_dirty(fb_t *fb, int x0, int y0, int x1, int y1)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    uint16_t nx0 = (uint16_t)x0, ny0 = (uint16_t)y0;
    uint16_t nx1 = (uint16_t)x1, ny1 = (uint16_t)y1;
    if (!fb->dirty.valid) {
        fb->dirty.x0 = nx0;
        fb->dirty.y0 = ny0;
        fb->dirty.x1 = nx1;
        fb->dirty.y1 = ny1;
        fb->dirty.valid = true;
        return;
    }
    if (nx0 < fb->dirty.x0) fb->dirty.x0 = nx0;
    if (ny0 < fb->dirty.y0) fb->dirty.y0 = ny0;
    if (nx1 > fb->dirty.x1) fb->dirty.x1 = nx1;
    if (ny1 > fb->dirty.y1) fb->dirty.y1 = ny1;
}

void fb_init(fb_t *fb, uint8_t *bits, uint16_t w, uint16_t h)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(bits != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(w > 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(h > 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(w % 8 == 0, UI_ASSERT_CODE); /* render.h contract: w must be a multiple of 8 */
    fb->bits = bits;
    fb->w = w;
    fb->h = h;
    fb->stride = (uint16_t)(w / 8);
    fb->dirty.x0 = 0;
    fb->dirty.y0 = 0;
    fb->dirty.x1 = 0;
    fb->dirty.y1 = 0;
    fb->dirty.valid = false;
}

void fb_clear(fb_t *fb, uint8_t black)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(fb->bits != NULL, UI_ASSERT_CODE);
    memset(fb->bits, black ? 0x00 : 0xFF, (size_t)fb->stride * (size_t)fb->h);
    fb->dirty.x0 = 0;
    fb->dirty.y0 = 0;
    fb->dirty.x1 = fb->w;
    fb->dirty.y1 = fb->h;
    fb->dirty.valid = true;
}

void fb_rect(fb_t *fb, int x, int y, int w, int h, uint8_t black, bool fill)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (w <= 0 || h <= 0) {
        return;
    }
    int cx0 = x < 0 ? 0 : x;
    int cy0 = y < 0 ? 0 : y;
    int cx1 = x + w;
    int cy1 = y + h;
    if (cx1 > (int)fb->w) cx1 = (int)fb->w;
    if (cy1 > (int)fb->h) cy1 = (int)fb->h;
    if (cx1 <= cx0 || cy1 <= cy0) {
        return;
    }
    /* postcondition of the clip above: the fill/outline loops below never touch an out-of-frame pixel */
    CORE_ASSERT_VOID(cx0 >= 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cx1 <= (int)fb->w, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cy0 >= 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cy1 <= (int)fb->h, UI_ASSERT_CODE);

    if (fill) {
        for (int yy = cy0; yy < cy1; yy++) {
            for (int xx = cx0; xx < cx1; xx++) {
                fb_set_px(fb, xx, yy, black);
            }
        }
    } else {
        int top = y, bottom = y + h - 1;
        int left = x, right = x + w - 1;
        if (top >= cy0 && top < cy1) {
            for (int xx = cx0; xx < cx1; xx++) fb_set_px(fb, xx, top, black);
        }
        if (bottom != top && bottom >= cy0 && bottom < cy1) {
            for (int xx = cx0; xx < cx1; xx++) fb_set_px(fb, xx, bottom, black);
        }
        if (left >= cx0 && left < cx1) {
            for (int yy = cy0; yy < cy1; yy++) fb_set_px(fb, left, yy, black);
        }
        if (right != left && right >= cx0 && right < cx1) {
            for (int yy = cy0; yy < cy1; yy++) fb_set_px(fb, right, yy, black);
        }
    }
    fb_extend_dirty(fb, cx0, cy0, cx1, cy1);
}

void fb_hline(fb_t *fb, int x, int y, int w, uint8_t black)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (w <= 0 || y < 0 || y >= (int)fb->h) {
        return;
    }
    int cx0 = x < 0 ? 0 : x;
    int cx1 = x + w;
    if (cx1 > (int)fb->w) cx1 = (int)fb->w;
    if (cx1 <= cx0) {
        return;
    }
    CORE_ASSERT_VOID(cx0 >= 0, UI_ASSERT_CODE);        /* postcondition of the clip above */
    CORE_ASSERT_VOID(cx1 <= (int)fb->w, UI_ASSERT_CODE);
    for (int xx = cx0; xx < cx1; xx++) {
        fb_set_px(fb, xx, y, black);
    }
    fb_extend_dirty(fb, cx0, y, cx1, y + 1);
}

/* Shared opaque 1bpp cell blit used by fb_text (per glyph) and fb_icon: draws a cw x ch cell at
 * (x,y) from `bitmap` (MSB-first, row-major, stride bytes/row, bit=1 is ink), or an all-
 * background cell when `bitmap` is NULL (used for fb_text's "no glyph" case). Clips per-pixel to
 * the framebuffer bounds and extends dirty by the clipped bounding box. */
static void fb_blit_1bpp(fb_t *fb, int x, int y, int cw, int ch, int stride, const uint8_t *bitmap)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    int cx0 = x < 0 ? 0 : x;
    int cy0 = y < 0 ? 0 : y;
    int cx1 = x + cw;
    int cy1 = y + ch;
    if (cx1 > (int)fb->w) cx1 = (int)fb->w;
    if (cy1 > (int)fb->h) cy1 = (int)fb->h;
    if (cx1 <= cx0 || cy1 <= cy0) {
        return;
    }
    /* postcondition of the clip above: the blit loop below never touches an out-of-frame pixel.
     * `bitmap` is intentionally not asserted non-NULL -- NULL is the documented "no glyph" cell. */
    CORE_ASSERT_VOID(cx0 >= 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cx1 <= (int)fb->w, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cy0 >= 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cy1 <= (int)fb->h, UI_ASSERT_CODE);

    for (int iy = cy0; iy < cy1; iy++) {
        int row = iy - y;
        for (int ix = cx0; ix < cx1; ix++) {
            int     col = ix - x;
            uint8_t ink = 0;
            if (bitmap != NULL) {
                uint8_t byte = bitmap[(size_t)row * (size_t)stride + (size_t)(col / 8)];
                ink = (uint8_t)((byte >> (7 - (col % 8))) & 1u);
            }
            fb_set_px(fb, ix, iy, ink);
        }
    }
    fb_extend_dirty(fb, cx0, cy0, cx1, cy1);
}

/* Draws `s` left-to-right starting at (x,y), one fixed-width font cell per character. Shared by
 * fb_text (draws at the caller's x) and fb_text_right (draws at a pre-computed left x so the
 * string's right edge lands at x_right). Returns the pen x after the last character. */
static int fb_text_draw(fb_t *fb, const font_t *font, int x, int y, const char *s)
{
    CORE_ASSERT_RET(font != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    int pen = x;
    for (const char *p = s; *p != '\0'; p++) {
        int            idx = font_glyph_index(font, *p);
        const uint8_t *glyph = NULL;
        if (idx >= 0) {
            glyph = font->bitmaps + (size_t)idx * (size_t)font->h * (size_t)font->stride;
        }
        fb_blit_1bpp(fb, pen, y, font->w, font->h, font->stride, glyph);
        pen += font->w;
    }
    return pen;
}

int fb_text(fb_t *fb, const font_t *f, int x, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    return fb_text_draw(fb, f, x, y, s);
}

int fb_text_right(fb_t *fb, const font_t *f, int x_right, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x_right);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x_right);
    int x = x_right - (int)(strlen(s) * f->w);
    fb_text_draw(fb, f, x, y, s);
    return x;
}

void fb_icon(fb_t *fb, uint8_t icon_id, int x, int y)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (icon_id >= ICON_COUNT) {
        return;
    }
    fb_blit_1bpp(fb, x, y, ICON_W, ICON_H, ICON_STRIDE, &icon_bitmaps[icon_id][0][0]);
}

void fb_bar(fb_t *fb, int x, int y, int w, int h, uint8_t pct)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    fb_rect(fb, x, y, w, h, 1, false); /* 1px ink outline; also handles the too-small/off-frame no-op */
    if (w < 3 || h < 3) {
        return; /* no room for an interior */
    }
    uint8_t p = pct > 100 ? 100 : pct;
    int     iw = w - 2, ih = h - 2;
    int     fill_w = (iw * (int)p) / 100;
    if (fill_w > 0) {
        fb_rect(fb, x + 1, y + 1, fill_w, ih, 1, true);
    }
    if (iw - fill_w > 0) {
        fb_rect(fb, x + 1 + fill_w, y + 1, iw - fill_w, ih, 0, true);
    }
}
