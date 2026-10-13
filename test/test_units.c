#include "unity.h"
#include "core/ui/units.h"
void setUp(void) {} void tearDown(void) {}
static void test_zero(void)        { TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 0)); TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 1)); }
static void test_kmh(void)         { TEST_ASSERT_EQUAL_UINT16(130, speed_display(3611, 0)); /* 36.11 m/s = 129.996 km/h -> 130 */ }
static void test_mph(void)         { TEST_ASSERT_EQUAL_UINT16(81, speed_display(3611, 1));  /* 80.78 mph -> 81 */ }
static void test_round_half_up(void){ TEST_ASSERT_EQUAL_UINT16(1, speed_display(14, 0));    /* 0.504 km/h -> 1 */ TEST_ASSERT_EQUAL_UINT16(0, speed_display(13, 0)); }
static void test_max_input(void)   { TEST_ASSERT_EQUAL_UINT16(2359, speed_display(65535, 0)); TEST_ASSERT_EQUAL_UINT16(1466, speed_display(65535, 1)); }
static void test_invalid_units(void){ TEST_ASSERT_EQUAL_UINT16(0, speed_display(3611, 2)); /* units > 1: CORE_ASSERT_RET's guard return */ }

/* dist_display (bench B4-F5, #96): the gate list's distance-row VALUE conversion, the counterpart
 * to drag_gate_label's label-side DIST-gate naming (core/dragengine/drag_cfg.c). */
static void test_dist_zero(void)         { TEST_ASSERT_EQUAL_UINT16(0, dist_display(0, 0)); TEST_ASSERT_EQUAL_UINT16(0, dist_display(0, 1)); }
static void test_dist_m_passthrough(void){ TEST_ASSERT_EQUAL_UINT16(100, dist_display(100, 0)); /* CFG_DIST_M: no conversion */ }
static void test_dist_ft(void)           { TEST_ASSERT_EQUAL_UINT16(125, dist_display(38, 1));  /* 38 m = 124.67 ft -> 125 (the 100-0 brake-gate fixture, test_screens.c drag_gate) */ }
static void test_dist_ft_round(void)     { TEST_ASSERT_EQUAL_UINT16(3, dist_display(1, 1));     /* 1 m = 3.2808 ft -> 3 */ TEST_ASSERT_EQUAL_UINT16(7, dist_display(2, 1)); /* 2 m = 6.5617 ft -> 7 */ }
static void test_dist_max_input(void)    { TEST_ASSERT_EQUAL_UINT16(328, dist_display(100, 1));  /* 100 m = 328.084 ft -> 328, well under the overflow guard */ }
static void test_dist_overflow_guard(void){ TEST_ASSERT_EQUAL_UINT16(65535, dist_display(60000, 1)); /* 60000 m -> ~196850 ft, past uint16_t: CORE_ASSERT_RET's guard return (unreachable for any real DRAG_BRAKE distance, core/dragengine/drag.c) */ }
static void test_dist_invalid_units(void){ TEST_ASSERT_EQUAL_UINT16(0, dist_display(38, 2));    /* dist_units > 1: CORE_ASSERT_RET's guard return */ }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero); RUN_TEST(test_kmh); RUN_TEST(test_mph); RUN_TEST(test_round_half_up);
    RUN_TEST(test_max_input); RUN_TEST(test_invalid_units);
    RUN_TEST(test_dist_zero); RUN_TEST(test_dist_m_passthrough); RUN_TEST(test_dist_ft);
    RUN_TEST(test_dist_ft_round); RUN_TEST(test_dist_max_input); RUN_TEST(test_dist_overflow_guard);
    RUN_TEST(test_dist_invalid_units);
    return UNITY_END();
}
