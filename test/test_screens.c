/* Host tests for the moto LAP and DRAG screens (spec §20.4-20.5, §11.4, §17.4): render fixed
 * screen_model_t states into a static 296x128 fb_t via screens_moto_render() and compare
 * byte-exact against committed PBM goldens in test/snapshots/ (see test/pbm.h for the
 * format/inversion).
 *
 * Golden workflow: these assertions were first run with no golden present (pbm_eq_file fails and
 * dumps test/snapshots/<name>.pbm.actual.pbm); those dumps were eyeballed (converted to PNG) then
 * promoted to the committed goldens, and the suite re-run to confirm a byte-exact pass. If
 * screens_moto.c's output ever changes intentionally, regenerate the same way rather than
 * hand-editing a .pbm.
 */
#include "unity.h"

#include "core/ui/canvas.h"
#include "core/ui/model.h"
#include "pbm.h"

#include <string.h>

#ifndef TEST_DIR
#error "TEST_DIR (test/CMakeLists.txt) must name this file's source directory"
#endif
/* Snapshot paths must be absolute: CTest's working directory for a test is the build tree, not
 * this source directory, so a bare "test/snapshots/..." would resolve against the wrong cwd.
 * TEST_DIR is injected by test/CMakeLists.txt as CMAKE_CURRENT_SOURCE_DIR. SNAP_SUBDIR (Plan 7 T3)
 * lets a second executable (test_screens_213, -DCANVAS_FORCE_213=1) built from this same source
 * point at its own golden set (test/snapshots/213/) without duplicating any test case. */
#ifndef SNAP_SUBDIR
#define SNAP_SUBDIR ""
#endif
#define SNAP(name) TEST_DIR "/snapshots/" SNAP_SUBDIR name

static uint8_t s_bits[(CANVAS_W / 8) * CANVAS_H];
static fb_t    s_fb;

void setUp(void)
{
    fb_init(&s_fb, s_bits, CANVAS_W, CANVAS_H);
}
void tearDown(void) {}

/* Rightmost column (0-based) holding ink (a 0 bit -- render.h/render.c's fb_set_px: "0 = black",
 * MSB-first within each byte) anywhere in fb, or -1 if the frame is entirely background. Used to
 * confirm every screen's actual drawn content stays within the true visible width
 * (CANVAS_VISIBLE_W, core/ui/canvas.h) -- a stronger, content-based check than fb->dirty, which
 * fb_clear() (render.c) always sets to span the whole padded buffer width (CANVAS_W, e.g. 256 on
 * the 213 canvas) on every render regardless of what ends up drawn, since clearing legitimately
 * touches every addressable byte, padding columns included. Plan 7 T3 fix 1, ruling T3-R1. */
static int fb_max_ink_col(const fb_t *fb)
{
    int max_col = -1;
    for (int y = 0; y < (int)fb->h; y++) {
        const uint8_t *row = fb->bits + (size_t)y * fb->stride;
        for (int i = 0; i < (int)fb->stride; i++) {
            uint8_t b = row[i];
            if (b == 0xFFu) {
                continue; /* every bit in this byte is background */
            }
            for (int bit = 0; bit < 8; bit++) {
                int col = i * 8 + bit;
                if (col >= (int)fb->w) {
                    break;
                }
                uint8_t mask = (uint8_t)(0x80u >> bit);
                if ((b & mask) == 0u && col > max_col) { /* 0 = ink */
                    max_col = col;
                }
            }
        }
    }
    return max_col;
}

/* ---- LAP page 0: the event card (spec 7b §3-4) ---- */

static void lap_model_base(screen_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->mode = SCR_MODE_LAP;
    m->page = 0;
    m->have_best = true;  m->best_ms = 111900; /* 1:51.90 */
    m->have_prev = true;  m->prev_ms = 112340; /* 1:52.34 */
    m->lap_no = 7;
    m->cur_sector_idx = 2;
    m->batt_pct = 87;
}

static void test_lap_p0_first_lap(void)
{
    /* Before any lap has a best/prev on record: big slot falls back to "LAP n", footer shows the
     * "-:--.--" placeholders, marker reads "L1 S0". */
    screen_model_t m;
    lap_model_base(&m);
    m.have_best = false; m.have_prev = false; m.lap_no = 1; m.cur_sector_idx = 0;
    m.big_kind = BIG_NONE;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_first_lap.pbm"), &s_fb));
}

static void test_lap_p0_sector_delta(void)
{
    /* Mid-lap: the last completed sector was 0.32s quicker than best -- the big slot shows the
     * signed sector delta, not a lap delta. */
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_SECTOR_DELTA; m.big_delta_ms = -320; m.big_sector_idx = 2;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_sector_delta.pbm"), &s_fb));
}

static void test_lap_p0_lap_delta_wide(void)
{
    /* A six-glyph lap delta ("+12.50", ends at x 238) leaves no room for the BEST tag beside the
     * number, so it must move to the marker row instead (spec 7b §4). */
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_LAP_DELTA; m.big_delta_ms = 12500; m.cur_sector_idx = 0; m.lap_no = 12;
    m.new_best = true; /* tag must move to the marker row: six glyphs leave no room beside the number */
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_lap_delta_wide.pbm"), &s_fb));
}

static void test_lap_p0_new_best(void)
{
    /* A narrow lap delta ("-0.44") with new_best set: the BEST tag fits beside the number, so the
     * marker stays put and the tag sits to the number's right. */
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_LAP_DELTA; m.big_delta_ms = -440; m.new_best = true; m.cur_sector_idx = 0;
    m.best_ms = 111460; m.prev_ms = 111460;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_new_best.pbm"), &s_fb));
}

static void test_lap_p0_fault(void)
{
    /* GPS has no fix and the battery is low -- exercises the fault strip together with the event
     * card; the footer rows sit above the strip (ruling T2-R1), so BEST needs no x retraction to
     * stay clear of the icons. */
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_SECTOR_DELTA; m.big_delta_ms = 210; m.big_sector_idx = 2;
    m.flags = (1u << SCR_SYS_GPS_NOFIX) | (1u << SCR_SYS_BATT_LOW); m.batt_pct = 14;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_fault.pbm"), &s_fb));
}

/* ---- LAP page 1 (sector board, spec 7b §5) ---- */

static void test_lap_p1_sectors(void)
{
    /* Three sectors, both fit comfortably within the three-column board: S1/S2/S3 best-lap times
     * on the value row, and a mix of a hit ("-0.12"/"+0.40") and a not-yet-reached ("----", S3)
     * last-lap delta on the row below. */
    screen_model_t m;
    lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 3;
    m.best_sector_ms[0] = 32100; m.best_sector_ms[1] = 41000; m.best_sector_ms[2] = 39240;
    m.have_theo = true; m.theo_best_ms = 111200;
    m.have_last_sector_delta[0] = true; m.last_sector_delta_ms[0] = -120;
    m.have_last_sector_delta[1] = true; m.last_sector_delta_ms[1] = 400;
    /* sector 3 of the current lap not reached yet: cell shows "----" */
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_sectors.pbm"), &s_fb));
}

static void test_lap_p1_many_sectors(void)
{
    /* Five sectors: the board still shows only three columns, S3's label gains a "+2" suffix for
     * the two sectors that do not fit, every last-lap delta is a huge +15.00s that clamps to the
     * fixed "+9.99" budget, and no theoretical best is on record yet (THEO reads "-:--.--"). */
    screen_model_t m;
    lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 5;
    for (uint8_t i = 0; i < 5; i++) { m.best_sector_ms[i] = 20000u + 1000u * i; m.have_last_sector_delta[i] = true; m.last_sector_delta_ms[i] = 15000; /* clamps to +9.99 */ }
    m.have_theo = false;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_many_sectors.pbm"), &s_fb));   /* S3 label reads "S3 +2"; THEO reads -:--.-- */
}

/* ---- LAP page 2 (2x2 stats grid, spec 7b §6) ---- */

static void test_lap_p2_stats(void)
{
    /* Values taken straight from spec 7b §6's own worked example. */
    screen_model_t m;
    lap_model_base(&m); m.page = 2;
    m.max_speed_kmh = 214; m.lean_l_deg = 52; m.lean_r_deg = 55;
    m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12; m.laps_valid = 10;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats.pbm"), &s_fb));
}

static void test_lap_p2_stats_many_laps(void)
{
    /* Ruling T3-R2: a 3-digit laps_total/laps_valid pushes the LAPS cell's "(N valid)" suffix past
     * CANVAS_VISIBLE_W on the narrower 213 canvas (GRID_COL2_X 128 + "120"'s width + the suffix
     * does not fit before x 250) -- render_grid_laps degrades to the shorter "(118)" there, while
     * the wider 296 canvas still fits the full "(118 valid)". */
    screen_model_t m;
    lap_model_base(&m); m.page = 2;
    m.max_speed_kmh = 214; m.lean_l_deg = 52; m.lean_r_deg = 55;
    m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 120; m.laps_valid = 118;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats_many_laps.pbm"), &s_fb));
}

/* ---- DRAG page 0 (benches, spec §11.4) ---- */

static void test_drag_p0_benches(void)
{
    /* A run that has hit 0-100 and 0-200 (2 of the 3 default benches_kmh, so no trim needed) and
     * crossed the 1/4 with a trap speed -- spec's own DRAG page-0 example ("@ 305"-style trap) and
     * the session's named golden "0-100 + 0-200 hit, 1/4 present with a trap speed". Times follow
     * a constant 0.5g run (matching §22.2's test_drag.c synthetic fixture: 0-100 at 5.66s, 1/4 at
     * 12.81s, trap ~223 km/h) so the numbers are physically consistent, not just plausible-looking.
     */
    screen_model_t m = {0};
    m.mode = SCR_MODE_DRAG;
    m.page = 0;
    m.drag_n = 3;
    m.drag[0] = (drag_row_t){.label = "0-100", .t_ms = 5660, .present = true};
    m.drag[1] = (drag_row_t){.label = "0-200", .t_ms = 11900, .present = true};
    m.drag[2] = (drag_row_t){
        .label = "1/4", .t_ms = 12810, .present = true, .trap_kmh = 223, .has_trap = true};
    m.drag_armed = false;
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_benches.pbm"), &s_fb));
}

/* ---- DRAG page 1 (all gates of the last run) ---- */

static void test_drag_p1_gates(void)
{
    /* All seven §6.6 gates hit on one run, in the spec's own listed order (60ft, 330ft, 1/8,
     * 1000ft, 1/4, 100-200, 100-0). Same constant-0.5g run as the page-0 case (60ft/330ft/1/8/
     * 1000ft/1/4 times derived from t = sqrt(2d / (0.5 * 9.81)); 100-200 is the time between the
     * v=100 and v=200 km/h crossings under the same acceleration. #40: 100-0 (DRAG_BRAKE,
     * core/drag.h) is a stopping DISTANCE in metres, not an elapsed time -- .is_distance/.dist_m
     * now carry that (~39 m at 0.5g from 100 km/h, matching the §22.1 bench measurement recorded
     * in docs/measurements.md), replacing the earlier placeholder .t_ms reading that this comment
     * used to describe. */
    screen_model_t m = {0};
    m.mode = SCR_MODE_DRAG;
    m.page = 1;
    m.drag_n = 7;
    m.drag[0] = (drag_row_t){.label = "60ft", .t_ms = 2731, .present = true};
    m.drag[1] = (drag_row_t){.label = "330ft", .t_ms = 6405, .present = true};
    m.drag[2] = (drag_row_t){.label = "1/8", .t_ms = 9057, .present = true};
    m.drag[3] = (drag_row_t){.label = "1000ft", .t_ms = 11148, .present = true};
    m.drag[4] = (drag_row_t){
        .label = "1/4", .t_ms = 12810, .present = true, .trap_kmh = 223, .has_trap = true};
    m.drag[5] = (drag_row_t){.label = "100-200", .t_ms = 5664, .present = true};
    m.drag[6] = (drag_row_t){.label = "100-0", .present = true, .dist_m = 39, .is_distance = true};
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p1_gates.pbm"), &s_fb));
}

/* ---- DRAG page 2 (best per gate this session) ---- */

static void test_drag_p2_best(void)
{
    /* Same seven-gate row set as page 1, but the session-best value per gate (spec: "best per
     * gate this session") -- a little faster than the single run in test_drag_p1_gates, as a
     * multi-run session's best-of would be. #40: 100-0 is again a distance (a shorter session-best
     * stopping distance, 37 m vs. p1's 39 m -- shorter is better for braking, same direction as
     * every other gate here being a little quicker). */
    screen_model_t m = {0};
    m.mode = SCR_MODE_DRAG;
    m.page = 2;
    m.drag_n = 7;
    m.drag[0] = (drag_row_t){.label = "60ft", .t_ms = 2700, .present = true};
    m.drag[1] = (drag_row_t){.label = "330ft", .t_ms = 6350, .present = true};
    m.drag[2] = (drag_row_t){.label = "1/8", .t_ms = 9000, .present = true};
    m.drag[3] = (drag_row_t){.label = "1000ft", .t_ms = 11080, .present = true};
    m.drag[4] = (drag_row_t){
        .label = "1/4", .t_ms = 12750, .present = true, .trap_kmh = 225, .has_trap = true};
    m.drag[5] = (drag_row_t){.label = "100-200", .t_ms = 5600, .present = true};
    m.drag[6] = (drag_row_t){.label = "100-0", .present = true, .dist_m = 37, .is_distance = true};
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p2_best.pbm"), &s_fb));
}

/* ---- one-shot screens (§20.6) ---- */

static void test_oneshot_boot(void)
{
    /* Name/version banner + 4 pre-formatted self-test lines (§17.6); a mix of OK and FAIL to
     * exercise both on the one committed BOOT golden. */
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_BOOT;
    strcpy(m.boot_name, "LAPTIMER");
    strcpy(m.boot_ver, "v0.1.0-98b28b5");
    strcpy(m.boot_line[0], "IMU      OK");
    strcpy(m.boot_line[1], "GPS      OK");
    strcpy(m.boot_line[2], "DISPLAY  FAIL");
    strcpy(m.boot_line[3], "STORAGE  OK");
    m.boot_n_lines = 4;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("boot.pbm"), &s_fb));
}

static void test_oneshot_venue(void)
{
    /* §20.6's own example ("KILLARNEY"). layout_name left empty: this is the "venue found" phase,
     * not the later "layout locked" phase (render_oneshot_venue, screens_moto.c). */
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_VENUE;
    strcpy(m.venue_name, "KILLARNEY");

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("venue.pbm"), &s_fb));
}

static void test_oneshot_safe(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_SAFE;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("safe.pbm"), &s_fb));
}

static void test_oneshot_lowbatt(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_LOWBATT;
    m.batt_pct = 14;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lowbatt.pbm"), &s_fb));
}

static void test_oneshot_ota(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_OTA;
    m.ota_pct = 63;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("ota.pbm"), &s_fb));
}

static void test_oneshot_ota_fail(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_OTA_FAIL;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("otafail.pbm"), &s_fb));
}

static void test_oneshot_calibrate(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_CALIBRATE;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("calibrate.pbm"), &s_fb));
}

static void test_oneshot_newtrack(void)
{
    screen_model_t m = {0};
    m.screen = SCR_ONESHOT;
    m.oneshot = ONESHOT_NEWTRACK;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("newtrack.pbm"), &s_fb));
}

/* ---- menu (§20.7) ---- */

/* All-caps, no '/' captions: every character has a FONT_MED glyph, so this golden exercises the
 * item_fits_font_med(true) path (screens_moto.c render_menu). */
static const char *const MENU_ITEMS_CAPS[] = {
    "MODE",     "LAYOUT",      "NEW TRACK", "CALIBRATE", "UNITS",   "EXPORT",
    "LIVE",     "DIAGNOSTICS", "SESSIONS",  "DISPLAY",   "SLEEP NOW",
};

static void test_menu_top(void)
{
    /* Top of the list, item 0 ("MODE") selected: exercises the marker on the first visible row
     * and the un-scrolled (menu_top == 0) case. */
    screen_model_t m = {0};
    m.screen = SCR_MENU;
    m.menu_n = (uint8_t)(sizeof(MENU_ITEMS_CAPS) / sizeof(MENU_ITEMS_CAPS[0]));
    for (uint8_t i = 0; i < m.menu_n; i++) {
        m.menu_items[i] = MENU_ITEMS_CAPS[i];
    }
    m.menu_sel = 0;
    m.menu_top = 0;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("menu_top.pbm"), &s_fb));
}

/* §20.7's own item list verbatim: lowercase and '/' throughout, so every row exercises the
 * item_fits_font_med(false) / FONT_SMALL fallback path. */
static const char *const MENU_ITEMS_FULL[] = {
    "Mode: Lap / Drag", "Layout: Auto", "New track",     "Calibrate", "Units: km/h / mph",
    "Export (BLE)",     "Live to phone", "Diagnostics",  "Sessions",  "Display",
    "Sleep now",
};

static void test_menu_scrolled(void)
{
    /* A lower item ("Diagnostics", index 7) selected with the list scrolled so it is visible
     * (menu_top = 6 -> visible rows are indices 6..9): exercises scrolling + the marker on a
     * non-first visible row together. */
    screen_model_t m = {0};
    m.screen = SCR_MENU;
    m.menu_n = (uint8_t)(sizeof(MENU_ITEMS_FULL) / sizeof(MENU_ITEMS_FULL[0]));
    for (uint8_t i = 0; i < m.menu_n; i++) {
        m.menu_items[i] = MENU_ITEMS_FULL[i];
    }
    m.menu_sel = 7;
    m.menu_top = 6;

    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("menu_scrolled.pbm"), &s_fb));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lap_p0_first_lap);
    RUN_TEST(test_lap_p0_sector_delta);
    RUN_TEST(test_lap_p0_lap_delta_wide);
    RUN_TEST(test_lap_p0_new_best);
    RUN_TEST(test_lap_p0_fault);
    RUN_TEST(test_lap_p1_sectors);
    RUN_TEST(test_lap_p1_many_sectors);
    RUN_TEST(test_lap_p2_stats);
    RUN_TEST(test_lap_p2_stats_many_laps);
    RUN_TEST(test_drag_p0_benches);
    RUN_TEST(test_drag_p1_gates);
    RUN_TEST(test_drag_p2_best);
    RUN_TEST(test_oneshot_boot);
    RUN_TEST(test_oneshot_venue);
    RUN_TEST(test_oneshot_safe);
    RUN_TEST(test_oneshot_lowbatt);
    RUN_TEST(test_oneshot_ota);
    RUN_TEST(test_oneshot_ota_fail);
    RUN_TEST(test_oneshot_calibrate);
    RUN_TEST(test_oneshot_newtrack);
    RUN_TEST(test_menu_top);
    RUN_TEST(test_menu_scrolled);
    return UNITY_END();
}
