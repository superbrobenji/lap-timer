/* Host tests for the framebuffer renderer (spec §20.2): render fixed content into a static
 * 296x128 fb_t (the 2.9" panel's logical size) and compare byte-exact against committed PBM
 * goldens in test/snapshots/ (see pbm.h for the format/inversion). Every test also re-inits the
 * shared static buffer, so tests do not depend on run order.
 *
 * Golden workflow: these assertions were first run with no golden present (pbm_eq_file fails and
 * dumps test/snapshots/<name>.pbm.actual.pbm); those dumps were eyeballed then promoted to the
 * committed goldens, and the suite re-run to confirm a byte-exact pass. If render.c's output ever
 * changes intentionally, regenerate the same way rather than hand-editing a .pbm.
 */
#include "unity.h"

#include "core/ui/fonts.h"
#include "core/ui/icons.h"
#include "core/ui/render.h"
#include "pbm.h"

#include <stdint.h>

#ifndef TEST_DIR
#error "TEST_DIR (test/CMakeLists.txt) must name this file's source directory"
#endif
/* Snapshot paths must be absolute: CTest's working directory for a test is the build tree, not
 * this source directory, so a bare "test/snapshots/..." would resolve against the wrong cwd.
 * TEST_DIR is injected by test/CMakeLists.txt as CMAKE_CURRENT_SOURCE_DIR. */
#define SNAP(name) TEST_DIR "/snapshots/" name

#define FB_W 296
#define FB_H 128

static uint8_t   s_bits[(FB_W / 8) * FB_H];
static fb_t      s_fb;

void setUp(void)
{
    fb_init(&s_fb, s_bits, FB_W, FB_H);
    fb_clear(&s_fb, 0); /* white */
}
void tearDown(void) {}

/* ---- fb_init / fb_clear / dirty-box bookkeeping (spec §20.2 dirty tracking) ---- */

static void test_fb_init_starts_with_no_dirty_region(void)
{
    /* setUp() already ran fb_clear (dirty = whole frame); re-init to observe fb_init's own
     * invariant, that nothing is dirty until the first clear/draw. */
    fb_init(&s_fb, s_bits, FB_W, FB_H);
    TEST_ASSERT_FALSE(s_fb.dirty.valid);
    TEST_ASSERT_EQUAL_UINT16(FB_W, s_fb.w);
    TEST_ASSERT_EQUAL_UINT16(FB_H, s_fb.h);
    TEST_ASSERT_EQUAL_UINT16(FB_W / 8, s_fb.stride);
}

static void test_fb_clear_marks_the_whole_frame_dirty(void)
{
    /* setUp already cleared once; clear again explicitly so this test does not depend on that. */
    fb_clear(&s_fb, 1);
    TEST_ASSERT_TRUE(s_fb.dirty.valid);
    TEST_ASSERT_EQUAL_UINT16(0, s_fb.dirty.x0);
    TEST_ASSERT_EQUAL_UINT16(0, s_fb.dirty.y0);
    TEST_ASSERT_EQUAL_UINT16(FB_W, s_fb.dirty.x1);
    TEST_ASSERT_EQUAL_UINT16(FB_H, s_fb.dirty.y1);
    /* black=1 -> every byte fully ink (0x00), since 0 = black */
    TEST_ASSERT_EQUAL_UINT8(0x00, s_fb.bits[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, s_fb.bits[(size_t)s_fb.stride * s_fb.h - 1]);
}

static void test_dirty_box_accumulates_bounding_box_of_draws(void)
{
    /* fb_clear (in setUp) already set dirty to the whole frame; re-init to start from "nothing
     * drawn yet" so this test can observe accumulation from an empty box. */
    fb_init(&s_fb, s_bits, FB_W, FB_H);
    TEST_ASSERT_FALSE(s_fb.dirty.valid);

    fb_rect(&s_fb, 10, 20, 5, 5, 1, true); /* touches [10,15) x [20,25) */
    TEST_ASSERT_TRUE(s_fb.dirty.valid);
    TEST_ASSERT_EQUAL_UINT16(10, s_fb.dirty.x0);
    TEST_ASSERT_EQUAL_UINT16(20, s_fb.dirty.y0);
    TEST_ASSERT_EQUAL_UINT16(15, s_fb.dirty.x1);
    TEST_ASSERT_EQUAL_UINT16(25, s_fb.dirty.y1);

    fb_rect(&s_fb, 100, 5, 5, 5, 1, true); /* touches [100,105) x [5,10) -> union grows both ways */
    TEST_ASSERT_EQUAL_UINT16(10, s_fb.dirty.x0);
    TEST_ASSERT_EQUAL_UINT16(5, s_fb.dirty.y0);
    TEST_ASSERT_EQUAL_UINT16(105, s_fb.dirty.x1);
    TEST_ASSERT_EQUAL_UINT16(25, s_fb.dirty.y1);

    /* A draw entirely off-frame is a no-op: it must not touch bits and must not move dirty. */
    fb_icon(&s_fb, ICON_GPS, 1000, 1000);
    TEST_ASSERT_EQUAL_UINT16(10, s_fb.dirty.x0);
    TEST_ASSERT_EQUAL_UINT16(5, s_fb.dirty.y0);
    TEST_ASSERT_EQUAL_UINT16(105, s_fb.dirty.x1);
    TEST_ASSERT_EQUAL_UINT16(25, s_fb.dirty.y1);
}

/* ---- font-size coverage (roadmap exit for session 4.1) + icon/bar goldens ---- */

static void test_text_big_renders_lap_time(void)
{
    /* FONT_BIG's glyph set is "0-9 : . - + S" (fonts.h) — a lap time fits it exactly. */
    fb_text(&s_fb, &FONT_BIG, 4, 4, "1:23.45");
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("text_big.pbm"), &s_fb));
}

static void test_text_med_renders_line(void)
{
    /* FONT_MED's glyph set is "0-9 : . - + A-Z" (no space, no lowercase) — a space still renders
     * correctly as a blank cell via font_glyph_index()'s -1 fallback. */
    fb_text(&s_fb, &FONT_MED, 4, 4, "S2 12:30");
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("text_med.pbm"), &s_fb));
}

static void test_text_small_renders_line(void)
{
    /* FONT_SMALL covers full ASCII 32..126. */
    fb_text(&s_fb, &FONT_SMALL, 4, 4, "BEST 1:51.90  PREV 1:52.34");
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("text_small.pbm"), &s_fb));
}

static void test_icons_and_bar(void)
{
    /* Every icon in a row (regression coverage for the whole icon_bitmaps table), plus a few bars
     * at different fill percentages (0, partial, full) to exercise fb_bar's outline + fill math. */
    for (int i = 0; i < ICON_COUNT; i++) {
        fb_icon(&s_fb, (uint8_t)i, 4 + i * 16, 4);
    }
    fb_bar(&s_fb, 4, 30, 100, 14, 0);
    fb_bar(&s_fb, 4, 50, 100, 14, 35);
    fb_bar(&s_fb, 4, 70, 100, 14, 100);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("icons_and_bar.pbm"), &s_fb));
}

static void test_composite_lap_screen(void)
{
    /* A LAP-page-0-shaped composite (spec §20.5), exercising all three font sizes, fb_text_right,
     * an icon and a bar together on one screen — the scenario plan Task 2 Step 1 describes.
     * Coordinates are chosen so nothing overlaps, to keep this readable as a review artefact
     * (deliberate overlap is exercised separately by the clipping test below). */
    fb_text(&s_fb, &FONT_SMALL, 4, 4, "BEST");
    fb_text_right(&s_fb, &FONT_BIG, 292, 4, "1:51.90"); /* x[124,292) y[4,44) */

    fb_text(&s_fb, &FONT_SMALL, 4, 50, "PREV");
    fb_text_right(&s_fb, &FONT_MED, 200, 48, "1:52.34"); /* x[88,200) y[48,72) */

    fb_text(&s_fb, &FONT_SMALL, 4, 80, "S2  1:12.30"); /* x[4,81) y[80,92) */

    fb_text(&s_fb, &FONT_MED, 4, 96, "-0.21"); /* x[4,74) y[96,120) */
    fb_icon(&s_fb, ICON_GPS, 270, 100);         /* x[270,282) y[100,112) */

    fb_bar(&s_fb, 90, 100, 170, 16, 62); /* stand-in session progress/level indicator, x[90,260) y[100,116) */

    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_screen.pbm"), &s_fb));
}

/* ---- clipping: every primitive must clip to the frame, never write out of bounds ---- */

static void test_clipping_partial_and_fully_off_frame_draws_stay_in_bounds(void)
{
    /* FONT_BIG cell is 24px wide; starting at x=280 the last ~8px of the glyph fall past
     * FB_W=296 -> right-edge clip. */
    fb_text(&s_fb, &FONT_BIG, 280, 4, "88:88.88");

    /* FONT_MED cell is 24px tall; starting at y=120 the bottom ~16px fall past FB_H=128 ->
     * bottom-edge clip. */
    fb_text(&s_fb, &FONT_MED, 4, 120, "CLIP");

    /* Starts left of the frame -> left-edge clip on the first glyph. */
    fb_text(&s_fb, &FONT_SMALL, -3, 60, "NEG");

    /* Entirely off-frame (x starts past FB_W) -> must be a complete no-op, not a crash. */
    fb_text(&s_fb, &FONT_SMALL, 400, 4, "OFF");

    /* Icon straddling the bottom-right corner. */
    fb_icon(&s_fb, ICON_BLE, 290, 120);

    /* Bar whose nominal width runs past the right edge; the fill fraction is still computed from
     * the full logical width (80), only the drawing is clipped. */
    fb_bar(&s_fb, 250, 90, 80, 20, 75);

    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("clip_edge.pbm"), &s_fb));

    /* Belt-and-braces on top of the ASan/UBSan build (test/CMakeLists.txt Debug default): the
     * dirty box itself must never claim to extend past the frame. */
    TEST_ASSERT_TRUE(s_fb.dirty.x1 <= s_fb.w);
    TEST_ASSERT_TRUE(s_fb.dirty.y1 <= s_fb.h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fb_init_starts_with_no_dirty_region);
    RUN_TEST(test_fb_clear_marks_the_whole_frame_dirty);
    RUN_TEST(test_dirty_box_accumulates_bounding_box_of_draws);
    RUN_TEST(test_text_big_renders_lap_time);
    RUN_TEST(test_text_med_renders_line);
    RUN_TEST(test_text_small_renders_line);
    RUN_TEST(test_icons_and_bar);
    RUN_TEST(test_composite_lap_screen);
    RUN_TEST(test_clipping_partial_and_fully_off_frame_draws_stay_in_bounds);
    return UNITY_END();
}
