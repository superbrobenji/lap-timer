#include "unity.h"
#include "core/ui/canvas.h"
#include "core/ui/render.h"
#include <string.h>
static uint8_t a_bits[(CANVAS_W / 8) * CANVAS_H], b_bits[(CANVAS_W / 8) * CANVAS_H];
static fb_t a, b;
void setUp(void) { fb_init(&a, a_bits, CANVAS_W, CANVAS_H); fb_init(&b, b_bits, CANVAS_W, CANVAS_H); fb_clear(&a, 0); fb_clear(&b, 0); }
void tearDown(void) {}
static void test_identical_is_not_dirty(void)
{
    fb_rect_t r; r.valid = true;
    TEST_ASSERT_FALSE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_FALSE(r.valid);
}
static void test_single_pixel_gives_byte_cell(void)
{
    fb_rect(&b, 13, 7, 1, 1, 1, true);   /* one ink pixel at (13,7) */
    fb_rect_t r;
    TEST_ASSERT_TRUE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(8, r.x0); TEST_ASSERT_EQUAL_UINT16(16, r.x1);
    TEST_ASSERT_EQUAL_UINT16(7, r.y0); TEST_ASSERT_EQUAL_UINT16(8, r.y1);
}
static void test_two_corners_span_frame(void)
{
    fb_rect(&b, 0, 0, 1, 1, 1, true);
    fb_rect(&b, CANVAS_W - 1, CANVAS_H - 1, 1, 1, 1, true);
    fb_rect_t r;
    TEST_ASSERT_TRUE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_EQUAL_UINT16(0, r.x0); TEST_ASSERT_EQUAL_UINT16(CANVAS_W, r.x1);
    TEST_ASSERT_EQUAL_UINT16(0, r.y0); TEST_ASSERT_EQUAL_UINT16(CANVAS_H, r.y1);
}
static void test_mismatched_geometry_rejected(void)
{
    static uint8_t c_bits[(CANVAS_W / 8) * CANVAS_H]; fb_t c; fb_init(&c, c_bits, CANVAS_W, CANVAS_H - 8);
    fb_rect_t r; r.valid = true;
    TEST_ASSERT_FALSE(fb_diff_rect(&a, &c, &r));   /* assert path: different h -> false, r.valid false */
    TEST_ASSERT_FALSE(r.valid);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_identical_is_not_dirty); RUN_TEST(test_single_pixel_gives_byte_cell); RUN_TEST(test_two_corners_span_frame); RUN_TEST(test_mismatched_geometry_rejected); return UNITY_END(); }
