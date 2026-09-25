/* test_flash_fmt.c -- Unity host tests for components/devconsole/host/flash_fmt.c (Plan 5.6 Task
 * 10). Pure, IDF-free: exercises flash_result_str's two formats (0x%04x for result >= 0, "link %d"
 * for result < 0) and its cap-bounded truncation (always NUL-terminated within cap, returns the
 * number of characters actually written -- the hexfmt_line convention this component already
 * uses, not strlcpy's "would-have-written" length).
 */
#include "flash_fmt.h"

#include <stdint.h>
#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* result == 0 -> "0x0000" (brief's worked example). */
void test_zero_result(void)
{
    char buf[24];
    size_t n = flash_result_str(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x0000", buf);
    TEST_ASSERT_EQUAL_size_t(6, n);
}

/* result == 0x0801 -> "0x0801" (brief's worked example: a nonzero LT_ERR_* code). */
void test_positive_result(void)
{
    char buf[24];
    size_t n = flash_result_str(0x0801, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x0801", buf);
    TEST_ASSERT_EQUAL_size_t(6, n);
}

/* result == -3 -> "link -3" (brief's worked example: a negative link-layer error). */
void test_negative_result(void)
{
    char buf[24];
    size_t n = flash_result_str(-3, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("link -3", buf);
    TEST_ASSERT_EQUAL_size_t(7, n);
}

/* The widest positive value (INT_MAX cast to unsigned -> 8 hex digits) still fits the internal
 * scratch buffer with room to spare. */
void test_large_positive_result(void)
{
    char buf[24];
    size_t n = flash_result_str(0x7fffffff, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x7fffffff", buf);
    TEST_ASSERT_EQUAL_size_t(10, n);
}

/* cap smaller than the formatted string: the written prefix stays NUL-terminated within cap, and
 * the function never writes past out[cap-1]. */
void test_cap_bounded_truncates(void)
{
    char buf[4];   /* room for 3 chars + NUL */
    memset(buf, 0x7A, sizeof buf);   /* sentinel: anything past the terminator must be untouched */
    size_t n = flash_result_str(-3, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("lin", buf);
    TEST_ASSERT_EQUAL_size_t(3, n);
}

/* cap == 1: room for the NUL only. */
void test_cap_one_writes_only_nul(void)
{
    char buf[1] = { 'x' };
    size_t n = flash_result_str(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

/* cap == 0: writes nothing at all (out may even be a 1-byte sentinel buffer -- must be untouched). */
void test_cap_zero(void)
{
    char buf[1] = { (char)0x55 };
    size_t n = flash_result_str(0, buf, 0);
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL_UINT8(0x55, (uint8_t)buf[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_result);
    RUN_TEST(test_positive_result);
    RUN_TEST(test_negative_result);
    RUN_TEST(test_large_positive_result);
    RUN_TEST(test_cap_bounded_truncates);
    RUN_TEST(test_cap_one_writes_only_nul);
    RUN_TEST(test_cap_zero);
    return UNITY_END();
}
