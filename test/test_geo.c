#include "unity.h"
#include "core/geo.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static void test_enu_100m_east_at_lat_minus34(void)
{
    geo_origin_t o; geo_origin_set(&o, -34.0, 18.5);
    double dlon_deg = 100.0 / (GEO_EARTH_R_M * cos(-34.0 * GEO_PI / 180.0)) * 180.0 / GEO_PI;
    geo_enu_t e = geo_to_enu(&o, -34.0, 18.5 + dlon_deg);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 100.0, e.x);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, e.y);
    geo_enu_t n = geo_to_enu(&o, -34.0 + 100.0 / GEO_EARTH_R_M * 180.0 / GEO_PI, 18.5);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 100.0, n.y);
}

static void test_haversine_known_distance(void)
{
    /* one degree of latitude at the equator ≈ 111.195 km */
    TEST_ASSERT_DOUBLE_WITHIN(5.0, 111195.0, geo_dist_m(0.0, 0.0, 1.0, 0.0));
}

static void test_segment_cross_forward_gives_plus_one(void)
{
    /* gate p1 = left end (west), p2 = right end (east); motion northbound */
    geo_enu_t p = { -10, 0 }, q = { 10, 0 }, a = { 1, -5 }, b = { 1, 5 };
    double t; int dir;
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a, b, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, t);
    TEST_ASSERT_EQUAL_INT(1, dir);
}

static void test_segment_cross_reverse_gives_minus_one(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 }, a = { 1, 5 }, b = { 1, -5 };
    double t; int dir;
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a, b, p, q, &t, &dir));
    TEST_ASSERT_EQUAL_INT(-1, dir);
}

static void test_segment_miss_parallel_and_touching(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 };
    double t; int dir;
    geo_enu_t a1 = { 20, -5 }, b1 = { 20, 5 };                 /* passes beside the gate */
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a1, b1, p, q, &t, &dir));
    geo_enu_t a2 = { -5, 1 }, b2 = { 5, 1 };                   /* parallel */
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a2, b2, p, q, &t, &dir));
    geo_enu_t a3 = { 0, -5 }, b3 = { 0, 0 };                   /* ends exactly on the gate: counts (t = 1) */
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a3, b3, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, t);
}

static void test_dist_point_segment(void)
{
    geo_enu_t p = { 0, 0 }, q = { 10, 0 };
    geo_enu_t on = { 5, 3 }, off = { 14, 3 };
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3.0, geo_dist_point_segment(on, p, q));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(off, p, q));
}

static void test_interp_constant_speed_is_linear(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.1, geo_interp_time(2.0, 20.0, 20.0, 0.2));
}

static void test_interp_decelerating_matches_closed_form(void)
{
    /* v0 = 30, v1 = 10 over 0.2 s → a = -100. At τ = 0.1: d = 30*0.1 - 0.5*100*0.01 = 2.5 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.1, geo_interp_time(2.5, 30.0, 10.0, 0.2));
}

static void test_interp_clamps_to_segment(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.2, geo_interp_time(100.0, 20.0, 20.0, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, geo_interp_time(-1.0, 20.0, 20.0, 0.2));
}

static void test_segment_cross_rejects_each_out_of_range_parameter(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 };
    double t = -99.0; int dir = -99;
    /* t > 1: the gate line is reached only past the end of the motion segment */
    geo_enu_t a1 = { 0, -10 }, b1 = { 0, -5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a1, b1, p, q, &t, &dir));
    /* t < 0: the gate line was already behind the start of the motion segment */
    geo_enu_t a2 = { 0, 5 }, b2 = { 0, 10 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a2, b2, p, q, &t, &dir));
    /* u < 0: the motion crosses the gate's line beyond its left end */
    geo_enu_t a3 = { -20, -5 }, b3 = { -20, 5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a3, b3, p, q, &t, &dir));
    /* u > 1: beyond the right end (also covered by the miss test above) */
    geo_enu_t a4 = { 20, -5 }, b4 = { 20, 5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a4, b4, p, q, &t, &dir));
    TEST_ASSERT_EQUAL_DOUBLE(-99.0, t);          /* outputs untouched on a rejection */
    TEST_ASSERT_EQUAL_INT(-99, dir);
    /* and the same geometry does cross when both parameters are in range */
    geo_enu_t a5 = { 0, -5 }, b5 = { 0, 5 };
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a5, b5, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, t);
}

static void test_dist_point_segment_degenerate_and_before_start(void)
{
    geo_enu_t degenerate = { 3, 4 };
    geo_enu_t origin = { 0, 0 };
    /* p == q: no direction to project onto, the distance is to the point itself */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(origin, degenerate, degenerate));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, geo_dist_point_segment(degenerate, degenerate, degenerate));
    /* projection before p: the t < 0 clamp pins it to p */
    geo_enu_t p = { 0, 0 }, q = { 10, 0 }, before = { -4, 3 };
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(before, p, q));
}

static void test_interp_time_degenerate_speeds(void)
{
    /* v0 ≈ 0 and acc ≈ 0: the vehicle is not moving, so the distance is reached no sooner than the
     * end of the interval; the fallback returns dt instead of dividing by zero (§6.4 step 4). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, 0.0, 0.0, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, 1e-9, 1e-9, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, -1.0, -1.0, 0.2));   /* v0 not positive */
    /* a distance the deceleration can never cover makes the discriminant negative; the guard keeps
     * the result finite and clamped to dt rather than NaN */
    double tau = geo_interp_time(30.0, 10.0, 8.0, 1.0);
    TEST_ASSERT_TRUE(tau == tau);                                  /* not NaN */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, tau);
    tau = geo_interp_time(1.0, 1.0, 0.0, 0.1);
    TEST_ASSERT_TRUE(tau == tau);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.1, tau);
    /* degenerate interval */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, geo_interp_time(2.0, 20.0, 20.0, 0.0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enu_100m_east_at_lat_minus34);
    RUN_TEST(test_haversine_known_distance);
    RUN_TEST(test_segment_cross_forward_gives_plus_one);
    RUN_TEST(test_segment_cross_reverse_gives_minus_one);
    RUN_TEST(test_segment_miss_parallel_and_touching);
    RUN_TEST(test_dist_point_segment);
    RUN_TEST(test_interp_constant_speed_is_linear);
    RUN_TEST(test_interp_decelerating_matches_closed_form);
    RUN_TEST(test_interp_clamps_to_segment);
    RUN_TEST(test_segment_cross_rejects_each_out_of_range_parameter);
    RUN_TEST(test_dist_point_segment_degenerate_and_before_start);
    RUN_TEST(test_interp_time_degenerate_speeds);
    return UNITY_END();
}
