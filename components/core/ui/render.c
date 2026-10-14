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
}

void fb_clear(fb_t *fb, uint8_t black)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(fb->bits != NULL, UI_ASSERT_CODE);
    memset(fb->bits, black ? 0x00 : 0xFF, (size_t)fb->stride * (size_t)fb->h);
}

void fb_rect(fb_t *fb, int x, int y, int w, int h, uint8_t black, bool fill)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(fb->bits != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID((int)fb->stride * 8 == (int)fb->w, UI_ASSERT_CODE); /* fb_init's invariant: keeps fb_set_px's stride indexing in bounds */
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
}

void fb_hline(fb_t *fb, int x, int y, int w, uint8_t black)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(fb->bits != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID((int)fb->stride * 8 == (int)fb->w, UI_ASSERT_CODE); /* fb_init's invariant: keeps fb_set_px's stride indexing in bounds */
    if (w <= 0 || y < 0 || y >= (int)fb->h) {
        return;
    }
    int cx0 = x < 0 ? 0 : x;
    int cx1 = x + w;
    if (cx1 > (int)fb->w) cx1 = (int)fb->w;
    if (cx1 <= cx0) {
        return;
    }
    for (int xx = cx0; xx < cx1; xx++) {
        fb_set_px(fb, xx, y, black);
    }
}

/* Shared opaque 1bpp cell blit used by fb_text (per glyph) and fb_icon: draws a cw x ch cell at
 * (x,y) from `bitmap` (MSB-first, row-major, stride bytes/row, bit=1 is ink), or an all-
 * background cell when `bitmap` is NULL (used for fb_text's "no glyph" case). Clips per-pixel to
 * the framebuffer bounds. `invert` flips the cell's sense (fb_text_inv): ink pixels become
 * background and background pixels become ink, so the NULL-bitmap "no glyph" case (ink 0
 * throughout) still comes out right -- a solid ink cell. */
static void fb_blit_1bpp(fb_t *fb, int x, int y, int cw, int ch, int stride, const uint8_t *bitmap, bool invert)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(cw > 0 && ch > 0 && stride > 0, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(fb->bits != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(stride * 8 >= cw, UI_ASSERT_CODE); /* keeps bitmap[row*stride + col/8] inside its row */
    int cx0 = x < 0 ? 0 : x;
    int cy0 = y < 0 ? 0 : y;
    int cx1 = x + cw;
    int cy1 = y + ch;
    if (cx1 > (int)fb->w) cx1 = (int)fb->w;
    if (cy1 > (int)fb->h) cy1 = (int)fb->h;
    if (cx1 <= cx0 || cy1 <= cy0) {
        return;
    }
    /* `bitmap` is intentionally not asserted non-NULL -- NULL is the documented "no glyph" cell. */

    for (int iy = cy0; iy < cy1; iy++) {
        int row = iy - y;
        for (int ix = cx0; ix < cx1; ix++) {
            int     col = ix - x;
            uint8_t ink = 0;
            if (bitmap != NULL) {
                uint8_t byte = bitmap[(size_t)row * (size_t)stride + (size_t)(col / 8)];
                /* col >= 0 by construction: cx0 >= x (clip clamps up, not down), so
                 * col = ix - x >= cx0 - x >= 0 for every ix in [cx0, cx1). Shift in the
                 * unsigned domain so the result stays unsigned end to end (gcc -Wsign-conversion). */
                ink = (uint8_t)(((unsigned int)byte >> (7 - (col % 8))) & 1u); /* p10 */
            }
            fb_set_px(fb, ix, iy, invert ? (uint8_t)(ink ^ 1u) : ink);
        }
    }
}

/* io:L1 (parked by Session 3, closed by Ruling S4-G): bounds fb_text_draw's character walk and
 * fb_text_right's length scan against the SOURCE string -- the read-side counterpart to io:M2
 * (put_str's unbounded destination write, screens_moto.c, Task 12). Both used to trust the
 * caller's NUL unconditionally (fb_text_draw's `for (p = s; *p != '\0'; p++)`, fb_text_right's
 * strlen(s)); an unterminated source runs either walk past its end. This is reachable today, not
 * just latent: render_drag_page0 passes drag_row_t.label (model.h:44, char[8]) straight to fb_text
 * with no copy and hence no bound at all (screens_moto.c:764) -- the DCARD footer's own put_str
 * call a few lines above it already documents that label as "untrusted model data ... with no
 * guaranteed terminator in the latent case" (screens_moto.c:713-714), and Session 3 confirmed the
 * read overflow empirically by filling the model with 'W'.
 *
 * FB_TEXT_MAX_CHARS (render.h) is the shared bound; this file (core/ui's generic framebuffer
 * primitives) must not include model.h just to check it against screen_model_t's fields -- that
 * would invert the layering model.h/screens_moto.c already build on top of this one. The
 * machine-checked tie to the real widest field (screen_model_t.venue_name) lives in
 * screens_moto.c's own _Static_assert instead, the one file that legitimately sees both. */

/* Bounded stand-in for strlen(s): stops at the first NUL, same as strlen, but never looks past
 * FB_TEXT_MAX_CHARS characters -- see render.h and screens_moto.c's _Static_assert for the bound's
 * derivation and machine-checked tie-in. Rule 2: the loop is bounded by that constant. */
size_t fb_text_len(const char *s)
{
    size_t n = 0;
    while (n < FB_TEXT_MAX_CHARS && s[n] != '\0') {
        n++;
    }
    return n;
}

/* Draws `s` left-to-right starting at (x,y), one fixed-width font cell per character, opaque
 * (invert == false, fb_text/fb_text_right) or inverted (invert == true, fb_text_inv: ink pixels
 * white, background pixels black). Returns the pen x after the last character. io:L1: the walk is
 * bounded by fb_text_len(s), not by `s` alone -- see FB_TEXT_MAX_CHARS above. */
static int fb_text_draw(fb_t *fb, const font_t *font, int x, int y, const char *s, bool invert)
{
    CORE_ASSERT_RET(font != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    size_t len = fb_text_len(s);
    int    pen = x;
    for (size_t i = 0; i < len; i++) {
        int            idx = font_glyph_index(font, s[i]);
        const uint8_t *glyph = NULL;
        if (idx >= 0) {
            glyph = font->bitmaps + (size_t)idx * (size_t)font->h * (size_t)font->stride;
        }
        fb_blit_1bpp(fb, pen, y, font->w, font->h, font->stride, glyph, invert);
        pen += font->w;
    }
    return pen;
}

int fb_text(fb_t *fb, const font_t *f, int x, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    return fb_text_draw(fb, f, x, y, s, false);
}

int fb_text_right(fb_t *fb, const font_t *f, int x_right, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x_right);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x_right);
    int x = x_right - (int)(fb_text_len(s) * (size_t)f->w);
    fb_text_draw(fb, f, x, y, s, false);
    return x;
}

/* Same as fb_text, but every cell is drawn inverted: glyph ink comes out white and the glyph's
 * background (including a no-glyph blank cell) comes out solid black -- white digits on a black
 * tile, e.g. the BEST tag (spec §4). Returns the pen x after the last character, same as
 * fb_text(). */
int fb_text_inv(fb_t *fb, const font_t *f, int x, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    return fb_text_draw(fb, f, x, y, s, true);
}

void fb_icon(fb_t *fb, uint8_t icon_id, int x, int y)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    if (icon_id >= ICON_COUNT) {
        return;
    }
    fb_blit_1bpp(fb, x, y, ICON_W, ICON_H, ICON_STRIDE, &icon_bitmaps[icon_id][0][0], false);
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

bool fb_diff_rect(const fb_t *prev, const fb_t *cur, fb_rect_t *out)
{
    CORE_ASSERT_RET(prev != NULL && cur != NULL && out != NULL, UI_ASSERT_CODE, false);
    out->valid = false;
    CORE_ASSERT_RET(prev->w == cur->w && prev->h == cur->h && prev->stride == cur->stride, UI_ASSERT_CODE, false);
    int bx0 = (int)cur->stride, bx1 = -1, y0 = (int)cur->h, y1 = -1;
    for (int y = 0; y < (int)cur->h; y++) {
        const uint8_t *pa = prev->bits + (size_t)y * prev->stride;
        const uint8_t *pb = cur->bits + (size_t)y * cur->stride;
        for (int i = 0; i < (int)cur->stride; i++) {
            if (pa[i] == pb[i]) continue;
            if (i < bx0) bx0 = i;
            if (i > bx1) bx1 = i;
            if (y < y0) y0 = y;
            y1 = y;
        }
    }
    if (bx1 < 0) return false;
    out->x0 = (uint16_t)(bx0 * 8); out->x1 = (uint16_t)((bx1 + 1) * 8);
    out->y0 = (uint16_t)y0;        out->y1 = (uint16_t)(y1 + 1);
    out->valid = true;
    return true;
}
