#include "unity.h"
#include "epd_pure.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}

static void test_rotate_all_white_fb_gives_all_ones(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);   /* core/ui: bit 1 = white */
    uint8_t out[16]; size_t n = epd_rotate_line(fb, 250, 122, 0, false, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(16, n);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}
static void test_rotate_all_black_fb_gives_zeros_inside_panel_width(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0x00, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 5, false, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, out[15]);   /* columns 120,121 black (bits 7,6); 122..127 beyond the panel stay white */
}
static void test_rotate_single_black_pixel_lands_in_the_right_column(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    /* logical (x=7, y=0) black -> panel row 7, column native_w-1-0 = 121 -> byte 15, bit 6 */
    fb[0 * 32 + 0] &= (uint8_t)~(1u << 0);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 7, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xBF, out[15]);
    epd_rotate_line(fb, 250, 122, 8, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[15]);
}
static void test_rotate_invert_flips_ink(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 0, true, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}
static void test_window_snaps_logical_y_to_ram_bytes(void)
{
    epd_window_t w;
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 3, 20, 10, 250, 122, &w));
    TEST_ASSERT_EQUAL_UINT16(0, w.xb0); TEST_ASSERT_EQUAL_UINT16(2, w.xb1);   /* y 3..13 -> RAM bytes [0,2) */
    TEST_ASSERT_EQUAL_UINT16(10, w.r0); TEST_ASSERT_EQUAL_UINT16(30, w.r1);   /* x 10..30 -> panel rows */
    TEST_ASSERT_FALSE(epd_window_from_rect(0, 0, 0, 5, 250, 122, &w));
    TEST_ASSERT_FALSE(epd_window_from_rect(240, 0, 20, 5, 250, 122, &w));
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
    RUN_TEST(test_window_snaps_logical_y_to_ram_bytes); RUN_TEST(test_panel_table_ws213v4); return UNITY_END(); }
