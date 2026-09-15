#include "unity.h"
#include "core/bw.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_write_then_read_round_trip(void)
{
    uint8_t buf[32];
    bw_t w; bw_init(&w, buf, sizeof buf);
    bw_u8(&w, 0xA5); bw_u16(&w, 0x1234); bw_u32(&w, 0xDEADBEEF);
    bw_i16(&w, -2); bw_i32(&w, -100000); bw_i64(&w, -1234567890123LL);
    TEST_ASSERT_FALSE(bw_overflow(&w));
    TEST_ASSERT_EQUAL_UINT(1 + 2 + 4 + 2 + 4 + 8, bw_len(&w));
    /* little-endian on the wire */
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, buf[3]);

    br_t r; br_init(&r, buf, bw_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0xA5, br_u8(&r));
    TEST_ASSERT_EQUAL_HEX16(0x1234, br_u16(&r));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, br_u32(&r));
    TEST_ASSERT_EQUAL_INT16(-2, br_i16(&r));
    TEST_ASSERT_EQUAL_INT32(-100000, br_i32(&r));
    TEST_ASSERT_EQUAL_INT64(-1234567890123LL, br_i64(&r));
    TEST_ASSERT_EQUAL_UINT(0, br_remaining(&r));
    TEST_ASSERT_FALSE(br_underflow(&r));
}

static void test_overflow_and_underflow_are_flagged_not_fatal(void)
{
    uint8_t buf[3];
    bw_t w; bw_init(&w, buf, sizeof buf);
    bw_u32(&w, 1);
    TEST_ASSERT_TRUE(bw_overflow(&w));
    TEST_ASSERT_EQUAL_UINT(0, bw_len(&w));   /* nothing partially written */

    br_t r; br_init(&r, buf, 2);
    (void)br_u32(&r);
    TEST_ASSERT_TRUE(br_underflow(&r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_round_trip);
    RUN_TEST(test_overflow_and_underflow_are_flagged_not_fatal);
    return UNITY_END();
}
