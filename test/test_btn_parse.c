#include "unity.h"
#include "core/btn_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_mode_maps_to_bit0(void)
{
    uint8_t mask = 0xAA; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", NULL, &mask, &hold));
    TEST_ASSERT_EQUAL_UINT8(0x1, mask);
    TEST_ASSERT_EQUAL_UINT32(100, hold);   /* NULL ms defaults to 100 */
}

static void test_up_maps_to_bit1(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("up", NULL, &mask, &hold));
    TEST_ASSERT_EQUAL_UINT8(0x2, mask);
}

static void test_down_maps_to_bit2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("down", NULL, &mask, &hold));
    TEST_ASSERT_EQUAL_UINT8(0x4, mask);
}

static void test_up_plus_down_maps_to_bit1_or_bit2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("up+down", NULL, &mask, &hold));
    TEST_ASSERT_EQUAL_UINT8(0x6, mask);
}

static void test_bad_name_returns_minus1_and_leaves_outputs_untouched(void)
{
    uint8_t mask = 0x55; uint32_t hold = 0x1234;
    TEST_ASSERT_EQUAL_INT(-1, btn_parse("select", "100", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT8(0x55, mask);      /* untouched on error */
    TEST_ASSERT_EQUAL_UINT32(0x1234, hold);   /* untouched on error */
}

static void test_hold_default_is_100_when_ms_null(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", NULL, &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(100, hold);
}

static void test_hold_passes_through_in_range(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", "1200", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(1200, hold);

    TEST_ASSERT_EQUAL_INT(0, btn_parse("up+down", "2000", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(2000, hold);
}

static void test_hold_clamps_below_20_up_to_20(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", "0", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(20, hold);
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", "19", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(20, hold);
}

static void test_hold_clamps_above_5000_down_to_5000(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", "5001", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(5000, hold);
    TEST_ASSERT_EQUAL_INT(0, btn_parse("mode", "999999", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(5000, hold);
}

static void test_hold_garbage_non_numeric_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", "abc", &mask, &hold));
    TEST_ASSERT_EQUAL_UINT32(0xFFFF, hold);   /* untouched on error */
}

static void test_hold_garbage_partial_consumption_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", "12x", &mask, &hold));
}

static void test_hold_garbage_empty_string_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", "", &mask, &hold));
}

static void test_hold_garbage_leading_sign_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", "-5", &mask, &hold));
}

static void test_hold_garbage_leading_plus_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", "+100", &mask, &hold));
}

static void test_hold_garbage_leading_whitespace_returns_minus2(void)
{
    uint8_t mask = 0; uint32_t hold = 0;
    TEST_ASSERT_EQUAL_INT(-2, btn_parse("mode", " 100", &mask, &hold));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mode_maps_to_bit0);
    RUN_TEST(test_up_maps_to_bit1);
    RUN_TEST(test_down_maps_to_bit2);
    RUN_TEST(test_up_plus_down_maps_to_bit1_or_bit2);
    RUN_TEST(test_bad_name_returns_minus1_and_leaves_outputs_untouched);
    RUN_TEST(test_hold_default_is_100_when_ms_null);
    RUN_TEST(test_hold_passes_through_in_range);
    RUN_TEST(test_hold_clamps_below_20_up_to_20);
    RUN_TEST(test_hold_clamps_above_5000_down_to_5000);
    RUN_TEST(test_hold_garbage_non_numeric_returns_minus2);
    RUN_TEST(test_hold_garbage_partial_consumption_returns_minus2);
    RUN_TEST(test_hold_garbage_empty_string_returns_minus2);
    RUN_TEST(test_hold_garbage_leading_sign_returns_minus2);
    RUN_TEST(test_hold_garbage_leading_plus_returns_minus2);
    RUN_TEST(test_hold_garbage_leading_whitespace_returns_minus2);
    return UNITY_END();
}
