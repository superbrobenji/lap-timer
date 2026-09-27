#include "unity.h"
#include "epd_pure.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}

/* fb is allocated 256 px wide (32 bytes/row): fb_init (core/ui/render.h) requires w % 8 == 0, so
 * the 2.13" panel's 250-column logical_w is padded up to 256 -- the buffer, not the panel's
 * visible width, is what epd_rotate_line/epd_window_from_rect's fb_w parameter takes (Plan 7 T4
 * fix round 1). */
static void test_rotate_all_white_fb_gives_all_ones(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);   /* core/ui: bit 1 = white */
    uint8_t out[16]; size_t n = epd_rotate_line(fb, 256, 122, 0, false, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(16, n);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}
static void test_rotate_all_black_fb_gives_zeros_inside_panel_width(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0x00, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 256, 122, 5, false, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, out[15]);   /* columns 120,121 black (bits 7,6); 122..127 beyond the panel stay white */
}
static void test_rotate_single_black_pixel_lands_in_the_right_column(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    /* logical (x=7, y=0) black -> panel row 7, column native_w-1-0 = 121 -> byte 15, bit 6 */
    fb[0 * 32 + 0] &= (uint8_t)~(1u << 0);
    uint8_t out[16]; epd_rotate_line(fb, 256, 122, 7, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xBF, out[15]);
    epd_rotate_line(fb, 256, 122, 8, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[15]);
}
static void test_rotate_invert_flips_ink(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 256, 122, 0, true, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}
static void test_rotate_rejects_unaligned_width(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16];
    /* 250 (the panel's un-padded logical width) is not a multiple of 8 -- the real buffer stride
     * must be 256, so this is rejected rather than silently computing the wrong stride. */
    TEST_ASSERT_EQUAL_size_t(0, epd_rotate_line(fb, 250, 122, 0, false, out, 16));
}
static void test_window_snaps_logical_y_to_ram_bytes(void)
{
    epd_window_t w;
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 3, 20, 10, 256, 122, &w));
    TEST_ASSERT_EQUAL_UINT16(0, w.xb0); TEST_ASSERT_EQUAL_UINT16(2, w.xb1);   /* y 3..13 -> RAM bytes [0,2) */
    TEST_ASSERT_EQUAL_UINT16(10, w.r0); TEST_ASSERT_EQUAL_UINT16(30, w.r1);   /* x 10..30 -> panel rows */
    TEST_ASSERT_FALSE(epd_window_from_rect(0, 0, 0, 5, 256, 122, &w));
    TEST_ASSERT_FALSE(epd_window_from_rect(240, 0, 20, 5, 256, 122, &w));
}
static void test_window_rejects_spill_past_visible_width(void)
{
    epd_window_t w;
    /* 240+16 = 256 fits the padded 256-wide buffer but spills past the panel's 250 visible
     * columns (logical_w) -- must still be rejected. */
    TEST_ASSERT_FALSE(epd_window_from_rect(240, 0, 16, 5, 256, 122, &w));
    /* 234+16 = 250 lands exactly on the visible-width boundary -- accepted. */
    TEST_ASSERT_TRUE(epd_window_from_rect(234, 0, 16, 5, 256, 122, &w));
    TEST_ASSERT_EQUAL_UINT16(234, w.r0); TEST_ASSERT_EQUAL_UINT16(250, w.r1);
}
/* Fix round 2 (finding 10b): a rect that fits the panel's x/w range but whose y/h range overflows
 * the FRAMEBUFFER's height (fb_h) must still be rejected -- a distinct guard from the x+w-past-
 * logical_w case above, and from epd_rotate_line's fb_h >= native_w assertion (a buffer-shape
 * invariant, not this routine rect-shape rejection). */
static void test_window_rejects_row_past_fb_h(void)
{
    epd_window_t w;
    TEST_ASSERT_FALSE(epd_window_from_rect(10, 118, 20, 10, 256, 122, &w));   /* y+h=128 > fb_h=122 */
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 110, 20, 12, 256, 122, &w));    /* y+h=122 == fb_h: accepted */
}
static void test_panel_table_ws213v4(void)
{
    const epd_panel_t *p = epd_panel();
    TEST_ASSERT_EQUAL_STRING("ws213v4", p->name);
    TEST_ASSERT_EQUAL_UINT16(250, p->logical_w); TEST_ASSERT_EQUAL_UINT16(122, p->logical_h);
    TEST_ASSERT_EQUAL_UINT16(128, p->ram_w); TEST_ASSERT_EQUAL_HEX8(0xF7, p->lut_full); TEST_ASSERT_EQUAL_HEX8(0xFF, p->lut_partial);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_rotate_all_white_fb_gives_all_ones); RUN_TEST(test_rotate_all_black_fb_gives_zeros_inside_panel_width);
    RUN_TEST(test_rotate_single_black_pixel_lands_in_the_right_column); RUN_TEST(test_rotate_invert_flips_ink);
    RUN_TEST(test_rotate_rejects_unaligned_width);
    RUN_TEST(test_window_snaps_logical_y_to_ram_bytes); RUN_TEST(test_window_rejects_spill_past_visible_width);
    RUN_TEST(test_window_rejects_row_past_fb_h);
    RUN_TEST(test_panel_table_ws213v4); return UNITY_END(); }
