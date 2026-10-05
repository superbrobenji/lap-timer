#include "unity.h"
#include "core/ui/stats_fold.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}
static void test_fold_from_zero_takes_lap(void)
{
    session_max_t acc; memset(&acc, 0, sizeof acc);
    lap_stats_t lap = { .max_speed_cms = 3306, .min_speed_cms = 1991, .max_lean_l_cdeg = 4800, .max_lean_r_cdeg = 5200,
                        .max_glat_e3 = 1320, .max_gacc_e3 = 610, .max_gbrake_e3 = 1050 };
    session_max_fold(&acc, &lap);
    TEST_ASSERT_EQUAL_UINT16(3306, acc.max_speed_cms);
    TEST_ASSERT_EQUAL_INT16(4800, acc.max_lean_l_cdeg);
    TEST_ASSERT_EQUAL_INT16(5200, acc.max_lean_r_cdeg);
    TEST_ASSERT_EQUAL_INT16(1320, acc.max_glat_e3);
    TEST_ASSERT_EQUAL_INT16(610, acc.max_gacc_e3);
    TEST_ASSERT_EQUAL_INT16(1050, acc.max_gbrake_e3);
}
static void test_fold_keeps_larger(void)
{
    session_max_t acc = { .max_speed_cms = 4000, .max_lean_l_cdeg = 5000, .max_lean_r_cdeg = 100, .max_glat_e3 = 1500, .max_gacc_e3 = 700, .max_gbrake_e3 = 900 };
    lap_stats_t lap = { .max_speed_cms = 3306, .max_lean_l_cdeg = 4800, .max_lean_r_cdeg = 5200, .max_glat_e3 = 1320, .max_gacc_e3 = 610, .max_gbrake_e3 = 1050 };
    session_max_fold(&acc, &lap);
    TEST_ASSERT_EQUAL_UINT16(4000, acc.max_speed_cms);
    TEST_ASSERT_EQUAL_INT16(5000, acc.max_lean_l_cdeg);
    TEST_ASSERT_EQUAL_INT16(5200, acc.max_lean_r_cdeg);
    TEST_ASSERT_EQUAL_INT16(1500, acc.max_glat_e3);
    TEST_ASSERT_EQUAL_INT16(700, acc.max_gacc_e3);
    TEST_ASSERT_EQUAL_INT16(1050, acc.max_gbrake_e3);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_fold_from_zero_takes_lap); RUN_TEST(test_fold_keeps_larger); return UNITY_END(); }
