#include "unity.h"
#include "core/core.h"
#include <stddef.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_version_string(void)
{
    TEST_ASSERT_EQUAL_STRING("0.0.1", core_version());
}

/* ---- core assertions (spec §17.9) ---- */

static uint16_t    seen_code;
static const char *seen_file;
static int         seen_line, seen_calls;
static void record_hook(uint16_t code, const char *file, int line)
{
    seen_code = code; seen_file = file; seen_line = line; seen_calls++;
}

static int guarded_ret(int ok)   { CORE_ASSERT_RET(ok, 0x0A99, -7); return 0; }
static int void_calls;
static void guarded_void(int ok) { CORE_ASSERT_VOID(ok, 0x0A98); void_calls++; }

static void test_assert_hook_records_the_code_on_a_forced_failure(void)
{
    seen_calls = 0; void_calls = 0;
    core_set_assert_hook(record_hook);

    TEST_ASSERT_EQUAL_INT(0, guarded_ret(1));            /* a passing check stays silent */
    TEST_ASSERT_EQUAL_INT(0, seen_calls);

    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));           /* failing: reports and returns the value */
    TEST_ASSERT_EQUAL_INT(1, seen_calls);
    TEST_ASSERT_EQUAL_HEX16(0x0A99, seen_code);
    TEST_ASSERT_NOT_NULL(seen_file);
    TEST_ASSERT_GREATER_THAN(0, seen_line);

    guarded_void(1); TEST_ASSERT_EQUAL_INT(1, void_calls);
    guarded_void(0); TEST_ASSERT_EQUAL_INT(1, void_calls);   /* returned before the body */
    TEST_ASSERT_EQUAL_INT(2, seen_calls);
    TEST_ASSERT_EQUAL_HEX16(0x0A98, seen_code);

    core_set_assert_hook(NULL);                          /* NULL = silent, never a null call */
    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));
    TEST_ASSERT_EQUAL_INT(2, seen_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string);
    RUN_TEST(test_assert_hook_records_the_code_on_a_forced_failure);
    return UNITY_END();
}
