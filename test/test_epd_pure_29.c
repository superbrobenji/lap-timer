/* test_epd_pure_29.c -- epd_pure.h coverage for the ws29v2 panel row (Plan 7 final fix round,
 * finding 10a). Mirrors test_epd_pure.c's cases, but for 296x128 landscape / 128x296 portrait
 * geometry: unlike ws213v4 (native_w 122 < ram_w 128, a 6-bit forced-white RAM tail), ws29v2 has
 * native_w == ram_w == 128, so every RAM column is real panel data -- no padding tail -- and the
 * 296-wide framebuffer is already byte-aligned (296 % 8 == 0), so fb_w never needs padding above
 * the panel's own visible width the way the 213's 250 -> 256 does. EPD_FORCE_PANEL_29 (host/
 * epd_panel.c) selects this row since the host build has no build_config.h to read
 * CFG_PANEL_WS29V2 from.
 */
#include "unity.h"
#include "epd_pure.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}

#define FB29_STRIDE 37   /* 296 / 8 */
#define FB29_H      128  /* == native_w */

static void test_rotate_all_white_fb_gives_all_ones(void)
{
    uint8_t fb[FB29_STRIDE * FB29_H]; memset(fb, 0xFF, sizeof fb);   /* core/ui: bit 1 = white */
    uint8_t out[16]; size_t n = epd_rotate_line(fb, 296, FB29_H, 0, false, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(16, n);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}
static void test_rotate_all_black_fb_gives_all_zeros_no_padding(void)
{
    uint8_t fb[FB29_STRIDE * FB29_H]; memset(fb, 0x00, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 296, FB29_H, 5, false, out, sizeof out);
    /* ram_w == native_w (128) on this panel: no forced-white padding tail, unlike ws213v4's
     * partial last byte (0x3F there) -- every one of the 16 bytes is real panel data. */
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}
static void test_rotate_single_black_pixel_lands_in_the_right_column(void)
{
    uint8_t fb[FB29_STRIDE * FB29_H]; memset(fb, 0xFF, sizeof fb);
    /* logical (x=7, y=0) black -> panel row 7, column native_w-1-0 = 127 -> byte 15, bit 0 */
    fb[0 * FB29_STRIDE + 0] &= (uint8_t)~(1u << 0);
    uint8_t out[16]; epd_rotate_line(fb, 296, FB29_H, 7, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out[15]);
    epd_rotate_line(fb, 296, FB29_H, 8, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[15]);
}
static void test_rotate_invert_flips_ink(void)
{
    uint8_t fb[FB29_STRIDE * FB29_H]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 296, FB29_H, 0, true, out, sizeof out);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}
static void test_rotate_rejects_unaligned_width(void)
{
    uint8_t fb[FB29_STRIDE * FB29_H]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, epd_rotate_line(fb, 297, FB29_H, 0, false, out, 16));   /* 297 % 8 != 0 */
}
static void test_window_ws29v2_snaps_and_bounds(void)
{
    epd_window_t w;
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 3, 20, 10, 296, 128, &w));
    /* Mirrored (ruling B-10): rows y=3..12 (h=10) land at RAM columns native_w-1-y = 115..124
     * (native_w=128), byte range [115/8, 124/8] = [14,15] -> half-open [14,16). */
    TEST_ASSERT_EQUAL_UINT16(14, w.xb0); TEST_ASSERT_EQUAL_UINT16(16, w.xb1);
    TEST_ASSERT_EQUAL_UINT16(10, w.r0); TEST_ASSERT_EQUAL_UINT16(30, w.r1);   /* x 10..30 -> panel rows */
    TEST_ASSERT_FALSE(epd_window_from_rect(0, 0, 0, 5, 296, 128, &w));        /* degenerate w==0 */
    TEST_ASSERT_FALSE(epd_window_from_rect(290, 0, 10, 5, 296, 128, &w));     /* x+w=300 > logical_w(296) */
    TEST_ASSERT_TRUE(epd_window_from_rect(286, 0, 10, 5, 296, 128, &w));      /* x+w=296 == logical_w: accepted */
}
static void test_window_ws29v2_rejects_row_past_fb_h(void)
{
    epd_window_t w;
    TEST_ASSERT_FALSE(epd_window_from_rect(10, 124, 20, 10, 296, 128, &w));   /* y+h=134 > fb_h=128 */
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 116, 20, 12, 296, 128, &w));    /* y+h=128 == fb_h: accepted */
}
/* Fix round 3 (bench B-F2, ruling B-10): same coverage as test_epd_pure.c's
 * test_window_covers_rotated_pixel, for the ws29v2 panel (native_w=128 == ram_w, no padding
 * tail). */
static void test_window_covers_rotated_pixel(void)
{
    const epd_panel_t *p = epd_panel();
    static const uint16_t pts[][2] = { { 0, 36 }, { 16, 56 }, { 0, 0 }, { 295, 127 }, { 100, 60 } };

    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        uint16_t x = pts[i][0], y = pts[i][1];
        uint8_t  fb[FB29_STRIDE * FB29_H];
        memset(fb, 0xFF, sizeof fb);
        fb[(size_t)y * FB29_STRIDE + x / 8] &= (uint8_t)~(1u << (7 - (x % 8)));

        epd_window_t w;
        TEST_ASSERT_TRUE(epd_window_from_rect(x, y, 1, 1, 296, FB29_H, &w));

        uint16_t c  = (uint16_t)(p->native_w - 1u - y);
        uint16_t cb = (uint16_t)(c / 8);
        TEST_ASSERT_TRUE(w.xb0 <= cb);
        TEST_ASSERT_TRUE(cb < w.xb1);

        uint8_t out[16];
        TEST_ASSERT_EQUAL_size_t(16, epd_rotate_line(fb, 296, FB29_H, x, false, out, sizeof out));
        TEST_ASSERT_TRUE(out[cb] != 0xFF);   /* the painted bit registers inside the window byte */
        for (uint16_t b = 0; b < 16; b++) {
            if (b < w.xb0 || b >= w.xb1) {
                TEST_ASSERT_EQUAL_HEX8(0xFF, out[b]);
            }
        }
    }
}
static void test_panel_table_ws29v2(void)
{
    const epd_panel_t *p = epd_panel();
    TEST_ASSERT_EQUAL_STRING("ws29v2", p->name);
    TEST_ASSERT_EQUAL_UINT16(128, p->native_w); TEST_ASSERT_EQUAL_UINT16(296, p->native_h);
    TEST_ASSERT_EQUAL_UINT16(296, p->logical_w); TEST_ASSERT_EQUAL_UINT16(128, p->logical_h);
    TEST_ASSERT_EQUAL_UINT16(128, p->ram_w); TEST_ASSERT_EQUAL_HEX8(0xF7, p->lut_full); TEST_ASSERT_EQUAL_HEX8(0xFF, p->lut_partial);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_rotate_all_white_fb_gives_all_ones);
    RUN_TEST(test_rotate_all_black_fb_gives_all_zeros_no_padding);
    RUN_TEST(test_rotate_single_black_pixel_lands_in_the_right_column); RUN_TEST(test_rotate_invert_flips_ink);
    RUN_TEST(test_rotate_rejects_unaligned_width);
    RUN_TEST(test_window_ws29v2_snaps_and_bounds); RUN_TEST(test_window_ws29v2_rejects_row_past_fb_h);
    RUN_TEST(test_window_covers_rotated_pixel);
    RUN_TEST(test_panel_table_ws29v2); return UNITY_END(); }
