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
    m.have_best_sector[0] = true; m.have_best_sector[1] = true; m.have_best_sector[2] = true;
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
    for (uint8_t i = 0; i < 5; i++) { m.best_sector_ms[i] = 20000u + 1000u * i; m.have_best_sector[i] = true; m.have_last_sector_delta[i] = true; m.last_sector_delta_ms[i] = 15000; /* clamps to +9.99 */ }
    m.have_theo = false;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_many_sectors.pbm"), &s_fb));   /* S3 label reads "S3 +2"; THEO reads -:--.-- */
}

static void test_lap_p1_deltas_only(void)
{
    /* Ruling FR-2: the delta row depends only on have_last_sector_delta[i], never on
     * best_n_sectors -- no producer fills best_n_sectors yet (roadmap follow-up #58), so on target
     * best_n_sectors is 0 while the deltas for the sectors already crossed this lap are still
     * real. best_sector_ms row stays "--.--" throughout (unaffected by this fix); sector 2's delta
     * is unset ("----"), sectors 0/1 show real deltas. */
    screen_model_t m;
    lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 0;
    m.have_last_sector_delta[0] = true; m.last_sector_delta_ms[0] = -120;
    m.have_last_sector_delta[1] = true; m.last_sector_delta_ms[1] = 400;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_deltas_only.pbm"), &s_fb));
}

static void test_lap_p1_filled(void)
{
    /* Real pipeline data (Plan 7c T3, design §2): S1/S2 have a best-lap sector time on record, S3
     * does not yet (have_best_sector[2] false) -- its value cell must show "--.--" even though i <
     * n, not m.best_sector_ms[2]'s (unset, 0) value. THEO is on record too. */
    screen_model_t m; lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 3;
    m.best_sector_ms[0] = 32100; m.best_sector_ms[1] = 41000; m.best_sector_ms[2] = 39240;
    m.have_best_sector[0] = true; m.have_best_sector[1] = true; m.have_best_sector[2] = false;   /* S3 not yet */
    m.have_theo = true; m.theo_best_ms = 111200;
    m.have_last_sector_delta[0] = true; m.last_sector_delta_ms[0] = -120;
    m.have_last_sector_delta[1] = true; m.last_sector_delta_ms[1] = 400;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_filled.pbm"), &s_fb));   /* S1 32.10 / S2 41.00 / S3 --.-- ; deltas -0.12 +0.40 ---- ; THEO 1:51.20 */
}

/* ---- LAP page 2 (2x2 stats grid, spec 7b §6) ---- */

static void test_lap_p2_stats(void)
{
    /* Values taken straight from spec 7b §6's own worked example. */
    screen_model_t m;
    lap_model_base(&m); m.page = 2;
    m.max_speed_cms = 5944; m.lean_l_deg = 52; m.lean_r_deg = 55;
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
    m.max_speed_cms = 5944; m.lean_l_deg = 52; m.lean_r_deg = 55;
    m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 120; m.laps_valid = 118;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats_many_laps.pbm"), &s_fb));
}

static void test_lap_p2_stats_huge_laps(void)
{
    /* Test gap (finding 13b): a 5-digit laps_total/laps_valid (12000/11999) pushes the LAPS
     * suffix past CANVAS_VISIBLE_W on both canvases. On the 213 (GRID_COL2_X 128, "12000" is 70 px
     * of FONT_MED) neither "(11999 valid)" (91 px) nor the shorter "(11999)" (49 px) fits before
     * x 246 -- render_grid_laps draws nothing after the number. On the wider 296 (GRID_COL2_X 152,
     * limit x 292) "(11999 valid)" still does not fit but "(11999)" does, so it draws that. */
    screen_model_t m;
    lap_model_base(&m); m.page = 2;
    m.max_speed_cms = 5944; m.lean_l_deg = 52; m.lean_r_deg = 55;
    m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12000; m.laps_valid = 11999;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats_huge_laps.pbm"), &s_fb));   /* 213: nothing after "12000"; 296: "(11999)" */
}

static void test_lap_p2_stats_filled(void)
{
    /* Real pipeline data (Plan 7c T3, design §2): max_speed_cms is the raw session-max value the
     * pipeline folds from lap_stats_t; the renderer converts it to km/h at render time
     * (speed_display, core/ui/units.h) -- 5944 cm/s -> 214 km/h, the same value test_lap_p2_stats
     * used to set directly, so this and the three cases above keep the same MAX SPD value; Plan 7c
     * T4 (design §3) puts the unit in the label ("MAX SPD km/h") on every one of them since m.units
     * defaults to 0 (km/h) via memset -- see test_lap_p2_stats_mph below for the mph label/value. */
    screen_model_t m; lap_model_base(&m); m.page = 2;
    m.max_speed_cms = 5944;   /* 214 km/h */
    m.lean_l_deg = 52; m.lean_r_deg = 55; m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12; m.laps_valid = 10;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats_filled.pbm"), &s_fb));
}

static void test_lap_p2_stats_mph(void)
{
    /* Plan 7c T4 (design §3): same model as test_lap_p2_stats_filled but m.units = 1 (mph) -- the
     * label reads "MAX SPD mph" and the value converts to 133 (5944 cm/s -> 133 mph, speed_display
     * rounds to nearest; see units.c). */
    screen_model_t m; lap_model_base(&m); m.page = 2;
    m.units = 1;
    m.max_speed_cms = 5944;   /* 133 mph */
    m.lean_l_deg = 52; m.lean_r_deg = 55; m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12; m.laps_valid = 10;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats_mph.pbm"), &s_fb));
}

/* ---- DRAG page 0 (spec 7b §7): the run card ---- */

/* Appends one gate to m->drag[]/drag_n (present=true; the two `false, 0` distance params are
 * overridden in the caller when a case needs a distance gate). trap_cms is raw cm/s (Plan 7c T4
 * fix 1, ruling R-4) -- the renderer converts it with speed_display(trap_cms, m->units) at render
 * time, so callers pass whatever cm/s value renders to the digits they want, not the digits
 * themselves. */
static void drag_gate(screen_model_t *m, const char *label, uint32_t t_ms, uint16_t trap_cms, bool dist, uint16_t dist_m)
{
    drag_row_t *r = &m->drag[m->drag_n++];
    memset(r, 0, sizeof *r);
    strcpy(r->label, label); r->t_ms = t_ms; r->present = true;
    r->trap_cms = trap_cms; r->has_trap = trap_cms != 0; r->is_distance = dist; r->dist_m = dist_m;
}

static void test_drag_p0_ready(void)
{
    /* Before the first gate: big slot reads "READY" (FONT_MED, no letters in FONT_HUGE), ARMED
     * top-right while drag_armed, footer empty. */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.drag_armed = true; m.batt_pct = 90;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_ready.pbm"), &s_fb));
}

static void test_drag_p0_gate_speed(void)
{
    /* Four gates hit, newest ("1/4") carries a trap speed: big slot shows "12.84" (FONT_HUGE),
     * "@173" + "km/h" row below it (FONT_MED digits, FONT_SMALL unit suffix -- Plan 7c T4, design
     * §3; m.units defaults to 0/km/h via memset). trap_cms is raw cm/s (ruling R-4, fix round 1):
     * 4806 cm/s -> 173 km/h (speed_display: (4806*36 + 500) / 1000 = 173). Footer lists the three
     * earlier gates "60ft 2.01   330ft 5.43   1/8 8.29" in hit order. */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1/4", 12840, 4806, false, 0);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_gate_speed.pbm"), &s_fb));   /* big 12.84, "@173 km/h" row, footer "60ft 2.01   330ft 5.43   1/8 8.29" */
}

static void test_drag_p0_trap_mph(void)
{
    /* Plan 7c T4 (design §3): same layout as test_drag_p0_gate_speed but m.units = 1 (mph) and a
     * trap_cms that converts to 107 mph at render time (ruling R-4, fix round 1): 4783 cm/s -> 107
     * mph (speed_display: (4783*22369 + 500000) / 1000000 = 107): "@107" + "mph". */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90; m.units = 1;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1/4", 12840, 4783, false, 0);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_trap_mph.pbm"), &s_fb));   /* big 12.84, "@107 mph" row */
}

static void test_drag_p0_distance(void)
{
    /* Newest gate is the 100-0 braking distance (#40): big slot shows "38" (FONT_HUGE) + a small
     * "m" (FONT_SMALL, since FONT_HUGE has no lowercase); footer shows the one earlier gate,
     * "100-200 6.12". */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90;
    drag_gate(&m, "100-200", 6120, 0, false, 0); drag_gate(&m, "100-0", 0, 0, true, 38);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_distance.pbm"), &s_fb));     /* big "38" + small "m"; footer "100-200 6.12" */
}

static void test_drag_p0_fault(void)
{
    /* Finding 5: PF-2's footer clip and fault_strip_left_x() with a bit set were untested on DRAG
     * page 0 -- same four gates as test_drag_p0_gate_speed, plus GPS no-fix and a low battery. The
     * footer must stop clear of the fault icons (PF-2), and the battery label/icon must both be
     * fully legible (finding 17's overprint fix). The trap row also gains the "km/h" suffix (Plan
     * 7c T4, design §3; m.units defaults to 0 via memset), same as test_drag_p0_gate_speed --
     * trap_cms 4806 -> 173 km/h (ruling R-4, fix round 1; same value, see that test's comment). */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 14;
    m.flags = (1u << SCR_SYS_GPS_NOFIX) | (1u << SCR_SYS_BATT_LOW);
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1/4", 12840, 4806, false, 0);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_fault.pbm"), &s_fb));
}

static void test_drag_p0_footer_scroll(void)
{
    /* Test gap (finding 13c): nine gates hit this run -- the footer windows to the last
     * DCARD_FOOTER_MAX (6) gates before the newest (which fills the big slot on its own), so
     * gates 0/1 ("1"/"2") scroll off and the footer starts at gate 2 ("3"). */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90;
    drag_gate(&m, "1", 1000, 0, false, 0); drag_gate(&m, "2", 2000, 0, false, 0);
    drag_gate(&m, "3", 3000, 0, false, 0); drag_gate(&m, "4", 4000, 0, false, 0);
    drag_gate(&m, "5", 5000, 0, false, 0); drag_gate(&m, "6", 6000, 0, false, 0);
    drag_gate(&m, "7", 7000, 0, false, 0); drag_gate(&m, "8", 8000, 0, false, 0);
    drag_gate(&m, "9", 9000, 0, false, 0);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_footer_scroll.pbm"), &s_fb));   /* big "9.00"; footer starts "3 3.00", never "1"/"2" */
}

static void test_drag_p0_armed_gates(void)
{
    /* Test gap (finding 13d): drag_armed stays true (re-armed for the next run) while gates from
     * the run just completed are still on screen -- ARMED top-right must coexist with the newest
     * gate's label/huge value/footer rather than the two being mutually exclusive. */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90; m.drag_armed = true;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p0_armed_gates.pbm"), &s_fb));
}

/* ---- DRAG pages 1/2 (gate list, spec 7b §7) ---- */

static void test_drag_p1_gates(void)   /* seven gates, 1000ft not reached this run */
{
    /* All seven §6.6 gates named, in the spec's own listed order (60ft, 330ft, 1/8, 1000ft, 1/4,
     * 100-200, 100-0); the 1000ft gate (index 3) was not reached this run (present forced false
     * after the fact) -- the list shows "--.--" for it. Ruling T4-R2: 100-200 is present with
     * 12.34s, the right column's worst case on the 213 canvas (its 49px label plus a 5-glyph
     * FONT_MED time). */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 1; m.batt_pct = 90;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1000ft", 10900, 0, false, 0);
    m.drag[3].present = false;
    drag_gate(&m, "1/4", 12840, 4806, false, 0);
    drag_gate(&m, "100-200", 12340, 0, false, 0);
    drag_gate(&m, "100-0", 0, 0, true, 38);
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W); /* T3-R1: no ink past the true visible width */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("drag_p1_gates.pbm"), &s_fb));
}

static void test_drag_p2_best(void)    /* same rows as drag_p1_gates, page 2, every gate present */
{
    /* Session-best per gate (title "SESSION BEST"): same seven rows as test_drag_p1_gates, but
     * gate 5 (100-200) is now present with t_ms 6120 -- every gate hit this session. */
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 2; m.batt_pct = 90;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1000ft", 10900, 0, false, 0);
    drag_gate(&m, "1/4", 12840, 4806, false, 0);
    drag_gate(&m, "100-200", 6120, 0, false, 0);
    drag_gate(&m, "100-0", 0, 0, true, 38);
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
    RUN_TEST(test_lap_p1_deltas_only);
    RUN_TEST(test_lap_p1_filled);
    RUN_TEST(test_lap_p2_stats);
    RUN_TEST(test_lap_p2_stats_many_laps);
    RUN_TEST(test_lap_p2_stats_huge_laps);
    RUN_TEST(test_lap_p2_stats_filled);
    RUN_TEST(test_lap_p2_stats_mph);
    RUN_TEST(test_drag_p0_ready);
    RUN_TEST(test_drag_p0_gate_speed);
    RUN_TEST(test_drag_p0_trap_mph);
    RUN_TEST(test_drag_p0_distance);
    RUN_TEST(test_drag_p0_fault);
    RUN_TEST(test_drag_p0_footer_scroll);
    RUN_TEST(test_drag_p0_armed_gates);
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
