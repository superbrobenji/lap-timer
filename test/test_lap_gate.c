#include "unity.h"
#include "core/lap.h"
#include "core/trk.h"
#include "core/geo.h"
#include "core/consts.h"
#include <math.h>
#include <string.h>

/* Pure gate-geometry tests (spec §10.9, §10.2). No engine state; builds on lap_gate.c only, so this
 * file also builds and runs on the ESP32 (core_selftest). */

void setUp(void) {}
void tearDown(void) {}

/* The gate reaches GATE_HALF_WIDTH_M each side of the point, so the endpoints are 30 m apart. */
static void test_gate_line_is_30m(void)
{
    trk_line_t g = lap_gate_line(-34.0, 18.7, 0.0);              /* heading north */
    double len = geo_dist_m(g.p1.lat, g.p1.lon, g.p2.lat, g.p2.lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 2.0 * GATE_HALF_WIDTH_M, len);   /* 30 m */
}

/* A gate built for the current heading accepts that heading's motion: crossing it in the driving
 * direction yields dir_sign +1 (§6.4 step 3), the value a forward layout stores. Checked for a
 * northbound and an eastbound heading. */
static void assert_forward_gives_plus_one(double lat0, double lon0, double heading_deg,
                                          double motion_e, double motion_n)
{
    trk_line_t g = lap_gate_line(lat0, lon0, heading_deg);
    geo_origin_t o; geo_origin_set(&o, lat0, lon0);
    geo_enu_t p = geo_to_enu(&o, g.p1.lat, g.p1.lon);
    geo_enu_t q = geo_to_enu(&o, g.p2.lat, g.p2.lon);

    /* A short segment centred on the point, travelling along the heading, must properly cross. */
    geo_enu_t a = { -motion_e * 5.0, -motion_n * 5.0 };
    geo_enu_t b = {  motion_e * 5.0,  motion_n * 5.0 };
    double t; int dir;
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a, b, p, q, &t, &dir));
    TEST_ASSERT_EQUAL_INT(1, dir);                              /* forward => +1 */
}

static void test_gate_dir_sign_accepts_current_motion(void)
{
    assert_forward_gives_plus_one(-34.0, 18.7, 0.0, 0.0, 1.0);   /* north */
    assert_forward_gives_plus_one(-34.0, 18.7, 90.0, 1.0, 0.0);  /* east  */
    assert_forward_gives_plus_one(-34.0, 18.7, 45.0, sin(GEO_PI / 4), cos(GEO_PI / 4));  /* NE */
}

/* lap_layout_reverse: same S/F line, sectors reversed, dir negated, id + 1, name suffixed. */
static void test_layout_reverse_negates_dir_reverses_sectors(void)
{
    trk_layout_t fwd;
    memset(&fwd, 0, sizeof fwd);
    fwd.id = 1;
    snprintf(fwd.name, sizeof fwd.name, "Layout 1");
    fwd.dir_sign = 1;
    fwd.sf = lap_gate_line(-34.0, 18.7, 0.0);
    fwd.n_sectors = 3;
    fwd.sectors[0] = lap_gate_line(-34.001, 18.701, 10.0);
    fwd.sectors[1] = lap_gate_line(-34.002, 18.702, 20.0);
    fwd.sectors[2] = lap_gate_line(-34.003, 18.703, 30.0);
    fwd.length_m = 2500;

    trk_layout_t rev;
    lap_layout_reverse(&fwd, &rev);

    TEST_ASSERT_EQUAL_INT8(-1, rev.dir_sign);                   /* dir negated */
    TEST_ASSERT_EQUAL_UINT16(2, rev.id);                        /* id + 1 */
    TEST_ASSERT_EQUAL_UINT8(3, rev.n_sectors);
    TEST_ASSERT_EQUAL_STRING("Layout 1 Reverse", rev.name);
    /* S/F line unchanged */
    TEST_ASSERT_EQUAL_DOUBLE(fwd.sf.p1.lat, rev.sf.p1.lat);
    TEST_ASSERT_EQUAL_DOUBLE(fwd.sf.p2.lon, rev.sf.p2.lon);
    /* sectors reversed in order (lines themselves unchanged) */
    TEST_ASSERT_EQUAL_DOUBLE(fwd.sectors[2].p1.lat, rev.sectors[0].p1.lat);
    TEST_ASSERT_EQUAL_DOUBLE(fwd.sectors[1].p1.lat, rev.sectors[1].p1.lat);
    TEST_ASSERT_EQUAL_DOUBLE(fwd.sectors[0].p1.lat, rev.sectors[2].p1.lat);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gate_line_is_30m);
    RUN_TEST(test_gate_dir_sign_accepts_current_motion);
    RUN_TEST(test_layout_reverse_negates_dir_reverses_sectors);
    return UNITY_END();
}
