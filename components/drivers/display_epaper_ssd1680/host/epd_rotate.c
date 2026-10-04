/* host/epd_rotate.c -- landscape-framebuffer -> portrait-panel-RAM-row transposition and
 * dirty-rect -> RAM-window rounding (Plan 7 Task 4; epd_pure.h). Pure/host-testable beyond
 * core/core.h's CORE_ASSERT_RET (spec §17.9): every build that links this file also links
 * core.c (core_assert_fail + the weak-by-default core_assert_report), on host (test_epd_pure)
 * and on target (display_epaper_ssd1680 REQUIRES core) alike.
 */
#include "epd_pure.h"
#include "core/core.h"

/* Power of 10 rule 5 (spec §17.9): this file's own assertion-report code. 0x0AA0 (ui), 0x0AB0
 * (json), 0x0AC0 (refresh policy) and 0x0C90 (display_epaper.c) are already taken -- see
 * `grep -rn "ASSERT_CODE 0x0A" components`; 0x0AD0 was free. */
#define EPD_ASSERT_CODE 0x0AD0

size_t epd_rotate_line(const uint8_t *fb, uint16_t fb_w, uint16_t fb_h, uint16_t pr, bool invert,
                        uint8_t *out, size_t cap)
{
    const epd_panel_t *p = epd_panel();
    size_t ram_bytes = (size_t)(p->ram_w / 8);

    CORE_ASSERT_RET(fb != NULL && out != NULL, EPD_ASSERT_CODE, 0);
    /* fb_w must be the buffer's real (padded-to-8) stride width, per fb_init's w % 8 == 0
     * contract (core/ui/render.h) -- a caller passing the panel's un-padded logical width
     * (not a multiple of 8) would compute the wrong stride below. */
    CORE_ASSERT_RET(fb_w % 8 == 0, EPD_ASSERT_CODE, 0);
    /* One CORE_ASSERT_RET per invariant (not combined with &&): the fault-report path only
     * records code+file+line, so a combined condition would hide which invariant actually
     * tripped from the §17.7 error ring. */
    CORE_ASSERT_RET(pr < p->native_h, EPD_ASSERT_CODE, 0);
    /* Every panel row pr (0..native_h-1) becomes a logical x < fb_w, never touching the
     * buffer's padding columns. */
    CORE_ASSERT_RET(fb_w >= p->native_h, EPD_ASSERT_CODE, 0);
    /* Every y = native_w-1-c the loop below reads must be a valid fb row. */
    CORE_ASSERT_RET(fb_h >= p->native_w, EPD_ASSERT_CODE, 0);
    CORE_ASSERT_RET(cap >= ram_bytes, EPD_ASSERT_CODE, 0);

    uint16_t stride = (uint16_t)(fb_w / 8);

    for (uint16_t c = 0; c < p->ram_w; c++) {
        uint8_t white = 1; /* columns beyond native_w (RAM padding) stay white (1) */
        if (c < p->native_w) {
            uint16_t y    = (uint16_t)(p->native_w - 1u - c);
            uint8_t  byte = fb[(size_t)y * stride + (size_t)(pr / 8)];
            uint8_t  bit  = (uint8_t)(((unsigned int)byte >> (7 - (pr % 8))) & 1u);
            white = invert ? (uint8_t)(bit ^ 1u) : bit;
        }
        uint8_t mask = (uint8_t)(0x80u >> (c % 8));
        if (white) {
            out[c / 8] = (uint8_t)(out[c / 8] | mask);
        } else {
            out[c / 8] = (uint8_t)(out[c / 8] & (uint8_t)~mask);
        }
    }
    return ram_bytes;
}

bool epd_window_from_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t fb_w,
                           uint16_t fb_h, epd_window_t *out)
{
    const epd_panel_t *p = epd_panel();

    CORE_ASSERT_RET(out != NULL, EPD_ASSERT_CODE, false);
    /* fb_w must be the buffer's real (padded-to-8) stride width -- see epd_rotate_line. A
     * buffer-shape violation is an assertion, unlike the rect-shape checks below. */
    CORE_ASSERT_RET(fb_w % 8 == 0, EPD_ASSERT_CODE, false);

    /* Routine rejection of a bad/empty caller rect, not an assertion (same reasoning as
     * render.c's per-pixel clipping: a dirty rect landing outside the frame is a normal call,
     * not a genuine anomaly). x + w is bounded by the panel's VISIBLE width (logical_w, ==
     * native_h), not by fb_w: fb_w may be padded past the panel's real column count (e.g. 256
     * vs. the 2.13" panel's 250), so a rect that fits the padded buffer but spills past column
     * logical_w must still be rejected -- it would never actually land on the panel. */
    if (w == 0 || h == 0 || x + w > p->logical_w || y + h > fb_h) {
        return false;
    }

    /* Mirrored, not a direct y/8 snap (bench B-F2, ruling B-10): epd_rotate_line puts landscape
     * row y at panel RAM column c = native_w-1-y (MIRRORED -- see its own header comment above),
     * so the rect's landscape rows y..y+h-1 land at RAM columns native_w-(y+h) .. native_w-1-y --
     * the HIGH end of the row range maps to the LOW end of the column range, and vice versa.
     * xb0 is the byte holding the lowest such column (native_w-(y+h)); xb1 is one past the byte
     * holding the highest (native_w-1-y), i.e. ceil((native_w-y)/8) -- clamped to ram_w/8 since y
     * can be 0, which would otherwise ask for a byte past the panel's RAM whenever native_w <
     * ram_w (ws213v4: native_w=122, ram_w=128 -- the 122..127 tail epd_rotate_line already
     * forces white, so clamping here just avoids sending that padding byte as part of the
     * window). x/w are untouched: panel RAM rows map to the landscape x 1:1, no mirror there. */
    uint16_t ram_bytes = (uint16_t)(p->ram_w / 8);
    out->xb0 = (uint16_t)((p->native_w - (y + h)) / 8);
    out->xb1 = (uint16_t)((p->native_w - y + 7) / 8);
    if (out->xb1 > ram_bytes) {
        out->xb1 = ram_bytes;
    }
    out->r0 = x;
    out->r1 = (uint16_t)(x + w);
    return true;
}
