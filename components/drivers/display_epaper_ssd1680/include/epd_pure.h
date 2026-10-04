/* epd_pure.h -- pure, host-testable helpers for the SSD1680 e-paper driver (Plan 7 Task 4):
 * the panel capability table, landscape-framebuffer -> portrait-panel-RAM-row transposition, and
 * dirty-rect -> RAM-window rounding. IDF-free (stdint.h/stddef.h/stdbool.h only) so these link
 * and run in the host test harness (test/test_epd_pure.c) as well as on target
 * (components/drivers/display_epaper_ssd1680/display_epaper.c, Task 5).
 */
#ifndef EPD_PURE_H
#define EPD_PURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One row per supported panel (Waveshare 2.13" V4 / 2.9" V2). native_w/native_h are the panel's
 * native portrait orientation, i.e. how the SSD1680 RAM is laid out; logical_w/logical_h are the
 * landscape orientation the UI renders into (hal/display.h's disp_caps_t width/height -- always
 * native_h x native_w). ram_w is the SSD1680 RAM row width in pixels (128 -> 16 bytes/row) for
 * both panels. border/lut_full/lut_partial are raw SSD1680 command arguments (border waveform
 * control / display update control 2's 0x22 argument for a full vs. partial refresh). */
typedef struct {
    const char *name;               /* "ws213v4" / "ws29v2" */
    uint16_t    native_w, native_h; /* portrait: 122x250 / 128x296 */
    uint16_t    logical_w, logical_h; /* landscape: 250x122 / 296x128 */
    uint16_t    ram_w;               /* 128 (16 bytes/row) for both */
    uint8_t     border;              /* 0x05 */
    uint8_t     lut_full, lut_partial; /* 0xF7, 0xFF (0x22 argument) */
} epd_panel_t;

/* Returns the panel table row selected by the build: CFG_PANEL_WS213V4 (build_config.h, the
 * on-target PANEL build flag) or EPD_FORCE_PANEL_213 (test define, for the host harness where
 * build_config.h does not exist). Never returns NULL. */
const epd_panel_t *epd_panel(void);

/* Transposes ONE panel RAM row (portrait row `pr`, 0..native_h-1) out of the landscape
 * framebuffer `fb` (fb_w x fb_h pixels, 1bpp row-major, MSB = leftmost, stride = fb_w/8
 * bytes/row -- core/ui/render.h's convention, bit 0 = black): panel column c (0..native_w-1) <-
 * logical pixel (x = pr, y = native_w-1-c) with rotation 0, i.e. the landscape image rotated 90
 * degrees clockwise into the portrait RAM. fb_w is the BUFFER width, not the panel's visible
 * width: fb_init (core/ui/render.h) requires w % 8 == 0, so fb_w is rounded up to the next
 * multiple of 8 at/above the panel's logical width (e.g. 256 for the 2.13" panel's 250-column
 * logical_w) -- only panel rows 0..native_h-1 are ever requested as `pr`, so the logical x this
 * produces is always < fb_w, never touching the padding columns. Writes ram_w/8 bytes MSB-first
 * into `out` (columns beyond native_w are padding, written as white = 1). Bit sense: the SSD1680
 * RAM is 1 = white -- the same sense as the framebuffer's 1 = white bit -- so the framebuffer bit
 * is copied through unchanged when `invert` is false, and flipped when `invert` is true. Returns
 * bytes written (ram_w/8, 16 today), or 0 if an assertion fails (fb/out NULL, fb_w not a multiple
 * of 8, fb_w or fb_h too small to hold every (row, column) this transposition reads, pr out of
 * range, or cap too small). */
size_t epd_rotate_line(const uint8_t *fb, uint16_t fb_w, uint16_t fb_h, uint16_t pr, bool invert,
                        uint8_t *out, size_t cap);

/* Panel-space (portrait RAM) window: half-open byte range [xb0,xb1) of RAM columns and half-open
 * row range [r0,r1) of RAM rows. */
typedef struct {
    uint16_t xb0, xb1, r0, r1;
} epd_window_t;

/* Rounds a logical (landscape) dirty rect {x,y,w,h} within an fb_w x fb_h framebuffer outward to
 * whole 8-px columns of panel RAM. After the 90-degree rotation the panel's RAM columns are the
 * logical y, so the rect's y range must snap outward to multiples of 8 (whole RAM bytes) while
 * the logical x range maps one-to-one to panel rows (x/w pass straight through to r0/r1 -- no
 * mirror there). The y -> RAM-column snap IS mirrored, the same way epd_rotate_line above maps
 * panel column c to logical row y = native_w-1-c: rows y..y+h-1 land at RAM columns
 * native_w-(y+h)..native_w-1-y, so the HIGH end of the row range produces the LOW end of the
 * byte range and vice versa (ruling B-10 -- a naive un-mirrored y/8 snap sends the wrong RAM
 * bytes for every sub-rect partial refresh). fb_w is the BUFFER width (see epd_rotate_line
 * above), rounded up to a multiple of 8 and possibly wider than the panel's
 * VISIBLE width (epd_panel()->logical_w, == native_h) -- x + w is bounded by that visible width,
 * not by fb_w, so a rect that fits the padded buffer but spills past the panel's real column
 * count is still rejected. M2 (bench-fix review): symmetrically, the row range y..y+h-1 is bounded
 * by the panel's VISIBLE height (epd_panel()->logical_h, == native_w), not just by fb_h -- fb_h is
 * the BUFFER height and may likewise exceed the panel's real row count on a hypothetical
 * height-padded canvas, so y + h > logical_h is rejected even when y + h <= fb_h. Fills `*out` and
 * returns true on success. On ANY rejection -- the rect is empty (w == 0 || h == 0), out of the
 * panel's visible range (x + w > logical_w || y + h > logical_h || y + h > fb_h), `fb_w` is not a
 * multiple of 8 (a buffer-shape bug, checked as an assertion, not a rect-shape one), or `out` is
 * NULL -- `*out` is left completely untouched and the function returns false; the caller must not
 * read `*out` after a false return. */
bool epd_window_from_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t fb_w,
                           uint16_t fb_h, epd_window_t *out);

#endif
