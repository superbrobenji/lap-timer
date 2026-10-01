#include "unity.h"
#include "core/blob.h"
#include <string.h>
#include <stdint.h>
void setUp(void) {} void tearDown(void) {}
static const uint8_t PAY[4] = { 0x11, 0x22, 0x33, 0x44 };
static void test_round_trip(void)
{
    uint8_t buf[16]; uint8_t out[4]; uint8_t ver = 0;
    TEST_ASSERT_EQUAL_UINT(7, blob_wrap(3, PAY, 4, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
    TEST_ASSERT_EQUAL_MEMORY(PAY, buf + 1, 4);
    TEST_ASSERT_EQUAL_INT(0, blob_unwrap(3, buf, 7, out, 4, &ver));
    TEST_ASSERT_EQUAL_UINT8(3, ver);
    TEST_ASSERT_EQUAL_MEMORY(PAY, out, 4);
}
static void test_cap_too_small_and_empty(void)
{
    uint8_t buf[6];
    TEST_ASSERT_EQUAL_UINT(0, blob_wrap(1, PAY, 4, buf, sizeof buf));   /* needs 7 */
    TEST_ASSERT_EQUAL_UINT(0, blob_wrap(1, PAY, 0, buf, sizeof buf));   /* empty payload rejected */
}
static void test_size_mismatch(void)
{
    uint8_t buf[16]; uint8_t out[4];
    (void)blob_wrap(1, PAY, 4, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, 6, out, 4, NULL));
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, 8, out, 4, NULL));
}
static void test_crc_mismatch(void)
{
    uint8_t buf[16]; uint8_t out[4] = { 9, 9, 9, 9 };
    (void)blob_wrap(1, PAY, 4, buf, sizeof buf);
    buf[2] ^= 0x80;
    TEST_ASSERT_EQUAL_INT(-2, blob_unwrap(1, buf, 7, out, 4, NULL));
    TEST_ASSERT_EQUAL_UINT8(9, out[0]);                                   /* untouched */
}
static void test_version_mismatch_reports_stored(void)
{
    uint8_t buf[16]; uint8_t out[4] = { 9, 9, 9, 9 }; uint8_t ver = 0;
    (void)blob_wrap(2, PAY, 4, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(-3, blob_unwrap(3, buf, 7, out, 4, &ver));
    TEST_ASSERT_EQUAL_UINT8(2, ver);
    TEST_ASSERT_EQUAL_UINT8(9, out[0]);                                   /* untouched */
    TEST_ASSERT_EQUAL_INT(0, blob_unwrap(2, buf, 7, out, 4, NULL));       /* accepted at its own version */
}
static void test_huge_n_rejected(void)
{
    uint8_t buf[16]; uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0, blob_wrap(1, PAY, SIZE_MAX - 1, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, SIZE_MAX, out, SIZE_MAX - 2, NULL));
}
static void test_unwrap_zero_payload_len_rejected(void)
{
    uint8_t buf[16] = { 0 }; uint8_t out[4];
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, 3, out, 0, NULL));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_round_trip); RUN_TEST(test_cap_too_small_and_empty); RUN_TEST(test_size_mismatch); RUN_TEST(test_crc_mismatch); RUN_TEST(test_version_mismatch_reports_stored); RUN_TEST(test_huge_n_rejected); RUN_TEST(test_unwrap_zero_payload_len_rejected); return UNITY_END(); }
