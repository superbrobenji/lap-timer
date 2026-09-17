/* Host tests for the moto LAP screens (spec §20.4-20.5, §17.4): render fixed screen_model_t
 * states into a static 296x128 fb_t via screens_moto_render() and compare byte-exact against
 * committed PBM goldens in test/snapshots/ (see test/pbm.h for the format/inversion). DRAG cases
 * land in Task 2.
 *
 * Golden workflow: these assertions were first run with no golden present (pbm_eq_file fails and
 * dumps test/snapshots/<name>.pbm.actual.pbm); those dumps were eyeballed (converted to PNG) then
 * promoted to the committed goldens, and the suite re-run to confirm a byte-exact pass. If
 * screens_moto.c's output ever changes intentionally, regenerate the same way rather than
 * hand-editing a .pbm.
 */
#include "unity.h"

#include "core/ui/model.h"
#include "pbm.h"

#include <string.h>

#ifndef TEST_DIR
#error "TEST_DIR (test/CMakeLists.txt) must name this file's source directory"
#endif
/* Snapshot paths must be absolute: CTest's working directory for a test is the build tree, not
 * this source directory, so a bare "test/snapshots/..." would resolve against the wrong cwd.
 * TEST_DIR is injected by test/CMakeLists.txt as CMAKE_CURRENT_SOURCE_DIR. */
#define SNAP(name) TEST_DIR "/snapshots/" name

#define FB_W 296
#define FB_H 128

static uint8_t s_bits[(FB_W / 8) * FB_H];
static fb_t    s_fb;

void setUp(void)
{
    fb_init(&s_fb, s_bits, FB_W, FB_H);
}
void tearDown(void) {}

/* ---- LAP page 0 ---- */

static void test_lap_p0_mid(void)
{
    /* Mid-session: a best and a previous lap on record, partway through the current lap, the
     * last completed sector was 0.21s slower than best (spec's own example delta). */
    screen_model_t m = {0};
    m.mode = SCR_MODE_LAP;
    m.page = 0;
    m.have_best = true;
    m.best_ms = 111900; /* 1:51.90 */
    m.have_prev = true;
    m.prev_ms = 112340; /* 1:52.34 */
    m.cur_ms_at_gate = 72300; /* 1:12.30 */
    m.cur_sector_idx = 2;
    m.sector_delta_ms = -210; /* -0.21 */
    m.new_best = false;
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_mid.pbm"), &s_fb));
}

static void test_lap_p0_empty(void)
{
    /* Before any lap: BEST/PREV show "--:--.--"; CUR is reset to 0:00.00 S0 at S/F. */
    screen_model_t m = {0};
    m.mode = SCR_MODE_LAP;
    m.page = 0;
    m.have_best = false;
    m.have_prev = false;
    m.cur_ms_at_gate = 0;
    m.cur_sector_idx = 0;
    m.sector_delta_ms = 0;
    m.new_best = false;
    m.flags = 0;
    m.batt_pct = 100;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_empty.pbm"), &s_fb));
}

static void test_lap_p0_newbest_fault(void)
{
    /* Just completed a new best lap (dS row shows "BEST" instead of a delta) while GPS has no fix
     * (SYS_GPS_NOFIX, bit 1) -- exercises the fault-icon strip together with the new-best path. */
    screen_model_t m = {0};
    m.mode = SCR_MODE_LAP;
    m.page = 0;
    m.have_best = true;
    m.best_ms = 109870; /* 1:49.87, the lap that was just completed */
    m.have_prev = true;
    m.prev_ms = 111900; /* 1:51.90, the previous best */
    m.cur_ms_at_gate = 15230; /* 0:15.23 into the new lap */
    m.cur_sector_idx = 1;
    m.sector_delta_ms = -1230; /* irrelevant while new_best is set, but populated for realism */
    m.new_best = true;
    m.flags = 1u << SCR_SYS_GPS_NOFIX;
    m.batt_pct = 54;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p0_newbest_fault.pbm"), &s_fb));
}

/* ---- LAP page 1 (best-lap sector splits + THEO) ---- */

static void test_lap_p1_sectors(void)
{
    screen_model_t m = {0};
    m.mode = SCR_MODE_LAP;
    m.page = 1;
    m.have_best = true;
    m.best_ms = 111900; /* 1:51.90 */
    m.best_n_sectors = 4;
    m.best_sector_ms[0] = 32100; /* S1 0:32.10 */
    m.best_sector_ms[1] = 41000; /* S2 0:41.00 */
    m.best_sector_ms[2] = 39240; /* S3 0:39.24 */
    m.best_sector_ms[3] = 33200; /* S4 (unused by the spec's example, exercises 4 sectors / 2 rows) */
    m.have_theo = true;
    m.theo_best_ms = 111200; /* THEO 1:51.20 */
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p1_sectors.pbm"), &s_fb));
}

/* ---- LAP page 2 (session stats) ---- */

static void test_lap_p2_stats(void)
{
    /* Values taken straight from spec §20.5's own worked example. */
    screen_model_t m = {0};
    m.mode = SCR_MODE_LAP;
    m.page = 2;
    m.max_speed_kmh = 214;
    m.lean_l_deg = 52;
    m.lean_r_deg = 55;
    m.lat_g_e2 = 132; /* 1.32 */
    m.acc_g_e2 = 61;  /* 0.61 */
    m.brk_g_e2 = 105; /* 1.05 */
    m.laps_total = 12;
    m.laps_valid = 10;
    m.flags = 0;
    m.batt_pct = 87;

    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("lap_p2_stats.pbm"), &s_fb));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lap_p0_mid);
    RUN_TEST(test_lap_p0_empty);
    RUN_TEST(test_lap_p0_newbest_fault);
    RUN_TEST(test_lap_p1_sectors);
    RUN_TEST(test_lap_p2_stats);
    return UNITY_END();
}
