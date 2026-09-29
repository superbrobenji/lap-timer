#include "unity.h"
#include "core/ui/units.h"
void setUp(void) {} void tearDown(void) {}
static void test_zero(void)        { TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 0)); TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 1)); }
static void test_kmh(void)         { TEST_ASSERT_EQUAL_UINT16(130, speed_display(3611, 0)); /* 36.11 m/s = 129.996 km/h -> 130 */ }
static void test_mph(void)         { TEST_ASSERT_EQUAL_UINT16(81, speed_display(3611, 1));  /* 80.78 mph -> 81 */ }
static void test_round_half_up(void){ TEST_ASSERT_EQUAL_UINT16(1, speed_display(14, 0));    /* 0.504 km/h -> 1 */ TEST_ASSERT_EQUAL_UINT16(0, speed_display(13, 0)); }
static void test_saturates(void)   { TEST_ASSERT_EQUAL_UINT16(2359, speed_display(65535, 0)); TEST_ASSERT_EQUAL_UINT16(1466, speed_display(65535, 1)); }
int main(void) { UNITY_BEGIN(); RUN_TEST(test_zero); RUN_TEST(test_kmh); RUN_TEST(test_mph); RUN_TEST(test_round_half_up); RUN_TEST(test_saturates); return UNITY_END(); }
