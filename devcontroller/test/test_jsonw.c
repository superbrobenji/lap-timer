/* test_jsonw.c -- Unity host tests for components/devconsole/host/jsonw.c (Plan 5.6 Task 4).
 * Pure, IDF-free: exercises jsonw_t's bounded, no-heap JSON writer directly (no esp_* deps).
 */
/* strnlen() is POSIX.1-2008, not ISO C: glibc hides it under the host harness's strict -std=c11
 * unless a feature-test macro asks for it (macOS libc and ESP-IDF's newlib expose it regardless).
 * Declared here, at the source, so both host harnesses and CI (Linux gcc) agree. */
#define _POSIX_C_SOURCE 200809L

#include "jsonw.h"

#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_flat_object(void)
{
    char b[128];
    jsonw_t w;
    jsonw_begin(&w, b, sizeof b);
    jsonw_int(&w, "a", -1);
    jsonw_bool(&w, "b", true);
    jsonw_str(&w, "c", "x\"y\\z\x01");
    TEST_ASSERT_TRUE(jsonw_end(&w));
    TEST_ASSERT_EQUAL_STRING("{\"a\":-1,\"b\":true,\"c\":\"x\\\"y\\\\z\"}", b);
}

/* Fix 1 (task-4-brief ambiguity): the brief's char b[24] does not actually overflow --
 * {"o":{"n":12345678}} is 20 chars + NUL = 21 bytes, which fits in 24. char b[16] genuinely
 * overflows. Everything else stays as the brief wrote it, plus an explicit within-cap
 * NUL-termination check: the writer must never leave an unterminated string on overflow. */
void test_nested_and_overflow(void)
{
    char b[16];
    jsonw_t w;
    jsonw_begin(&w, b, sizeof b);
    jsonw_obj(&w, "o");
    jsonw_int(&w, "n", 12345678);
    jsonw_close(&w);
    TEST_ASSERT_FALSE(jsonw_end(&w));                 /* does not fit in 16 bytes */
    TEST_ASSERT_EQUAL_CHAR('\0', b[sizeof b - 1]);     /* buffer's last byte is always NUL */
    TEST_ASSERT_TRUE(strnlen(b, sizeof b) < sizeof b); /* NUL-terminated somewhere within cap */
}

/* Fix 7 (task-4-brief resolution): jsonw_uint's max value round-trips exactly. */
void test_uint_max(void)
{
    char b[64];
    jsonw_t w;
    jsonw_begin(&w, b, sizeof b);
    jsonw_uint(&w, "u", 18446744073709551615ULL);
    TEST_ASSERT_TRUE(jsonw_end(&w));
    TEST_ASSERT_EQUAL_STRING("{\"u\":18446744073709551615}", b);
}

/* Fix 7: jsonw_str with an empty string writes an empty JSON string, not a dropped key. */
void test_str_empty(void)
{
    char b[32];
    jsonw_t w;
    jsonw_begin(&w, b, sizeof b);
    jsonw_str(&w, "s", "");
    TEST_ASSERT_TRUE(jsonw_end(&w));
    TEST_ASSERT_EQUAL_STRING("{\"s\":\"\"}", b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flat_object);
    RUN_TEST(test_nested_and_overflow);
    RUN_TEST(test_uint_max);
    RUN_TEST(test_str_empty);
    return UNITY_END();
}
