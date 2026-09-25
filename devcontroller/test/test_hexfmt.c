/* test_hexfmt.c -- Unity host tests for components/linkhost/host/hexfmt.c (Plan 5.6 Task 5).
 * Pure, IDF-free: exercises hexfmt_line's exact formatting (lowercase, single spaces, no trailing
 * space), the >32-byte truncation ellipsis, and the cap-limited NUL-termination guarantee.
 */
#include "hexfmt.h"

#include <stdio.h>
#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* {0xff,0x01,0x0a} -> "ff 01 0a": lowercase hex pairs, single spaces, no trailing space. */
void test_three_bytes(void)
{
    uint8_t b[] = { 0xff, 0x01, 0x0a };
    char out[64];
    size_t n = hexfmt_line(b, sizeof b, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ff 01 0a", out);
    TEST_ASSERT_EQUAL_UINT(strlen("ff 01 0a"), n);
}

/* 40 bytes -> only the first 32 are formatted, then a " \xE2\x80\xA6" (UTF-8 ellipsis) marks the
 * truncation. */
void test_over_32_bytes_truncates_with_ellipsis(void)
{
    uint8_t b[40];
    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)i;
    char out[256];
    size_t n = hexfmt_line(b, sizeof b, out, sizeof out);

    char want[256];
    size_t w = 0;
    for (size_t i = 0; i < 32; i++) {
        if (i > 0) want[w++] = ' ';
        w += (size_t)snprintf(want + w, sizeof(want) - w, "%02x", (unsigned)b[i]);
    }
    memcpy(want + w, " \xE2\x80\xA6", 4);
    w += 4;
    want[w] = '\0';

    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_UINT(w, n);
}

/* Exactly 32 bytes: no truncation, no ellipsis. */
void test_exactly_32_bytes_no_ellipsis(void)
{
    uint8_t b[32];
    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)(0xA0 + i);
    char out[128];
    size_t n = hexfmt_line(b, sizeof b, out, sizeof out);
    TEST_ASSERT_NULL(strchr(out, '\xE2'));
    TEST_ASSERT_EQUAL_UINT(32u * 3u - 1u, n);   /* 32 pairs, 31 separating spaces, no trailing space */
}

/* cap smaller than the formatted output: the written prefix stays NUL-terminated within cap, and
 * the function never writes past out[cap-1]. */
void test_cap_limited_stays_terminated(void)
{
    uint8_t b[] = { 0xde, 0xad, 0xbe, 0xef };
    char out[6];
    memset(out, 0x7A, sizeof out);   /* sentinel: anything past the terminator must be untouched */
    size_t n = hexfmt_line(b, sizeof b, out, sizeof out);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(out) - 1u, n);     /* n + NUL fits within cap */
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)out[n]);
    TEST_ASSERT_EQUAL_STRING("de ad", out);                  /* "de ad be ef" doesn't fit in 6 */
}

/* cap == 0: writes nothing, returns 0. */
void test_cap_zero(void)
{
    uint8_t b[] = { 0x01 };
    char out[1] = { 0x55 };
    size_t n = hexfmt_line(b, sizeof b, out, 0);
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL_UINT8(0x55, (uint8_t)out[0]);   /* untouched */
}

/* n == 0: empty string, no crash on a NULL b (never dereferenced when n == 0). */
void test_empty_input(void)
{
    char out[8];
    size_t n = hexfmt_line(NULL, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_three_bytes);
    RUN_TEST(test_over_32_bytes_truncates_with_ellipsis);
    RUN_TEST(test_exactly_32_bytes_no_ellipsis);
    RUN_TEST(test_cap_limited_stays_terminated);
    RUN_TEST(test_cap_zero);
    RUN_TEST(test_empty_input);
    return UNITY_END();
}
