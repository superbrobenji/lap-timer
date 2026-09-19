#include "unity.h"
#include "core/core.h"
#include "assert_support.h"
#include <stddef.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_version_string(void)
{
    TEST_ASSERT_EQUAL_STRING("0.0.1", core_version());
}

/* ---- core assertions (spec §17.9) ---- */

static int guarded_ret(int ok)   { CORE_ASSERT_RET(ok, 0x0A99, -7); return 0; }
static int void_calls;
static void guarded_void(int ok) { CORE_ASSERT_VOID(ok, 0x0A98); void_calls++; }

static void test_assert_reporter_records_the_code_on_a_forced_failure(void)
{
    void_calls = 0;
    lt_test_assert_reset();

    TEST_ASSERT_EQUAL_INT(0, guarded_ret(1));            /* a passing check never reports */
    TEST_ASSERT_EQUAL_UINT(0, lt_test_assert_count());

    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));           /* failing: reports and returns the value */
    TEST_ASSERT_EQUAL_UINT(1, lt_test_assert_count());
    TEST_ASSERT_EQUAL_HEX16(0x0A99, lt_test_assert_last_code());
    TEST_ASSERT_NOT_NULL(lt_test_assert_last_file());
    TEST_ASSERT_GREATER_THAN(0, lt_test_assert_last_line());

    guarded_void(1); TEST_ASSERT_EQUAL_INT(1, void_calls);
    guarded_void(0); TEST_ASSERT_EQUAL_INT(1, void_calls);   /* returned before the body */
    TEST_ASSERT_EQUAL_UINT(2, lt_test_assert_count());
    TEST_ASSERT_EQUAL_HEX16(0x0A98, lt_test_assert_last_code());

    /* Another failure still returns the safe value and is still reported: the reporter is a
     * link-time function with no runtime "off" state (the silent path is now core.c's weak
     * default, exercised only by a core-only link that supplies no override). */
    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));
    TEST_ASSERT_EQUAL_UINT(3, lt_test_assert_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string);
    RUN_TEST(test_assert_reporter_records_the_code_on_a_forced_failure);
    return UNITY_END();
}
