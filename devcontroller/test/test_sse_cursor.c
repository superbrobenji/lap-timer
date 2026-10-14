/* devcontroller/test/test_sse_cursor.c -- host tests for the PURE SSE overrun-guard logic
 * (components/webapi/host/sse_cursor.c, F3 Task 4 Step 1).
 *
 * dk:M7 -- the SSE overrun guard was `> BC_CAP`, so a cursor at distance EXACTLY cap (whose slot
 * index is congruent to the head's: the slot the producer writes next) was not skipped, which is
 * precisely the "guaranteed torn read" its own comment described. */
#include "unity.h"
#include "sse_cursor.h"
void setUp(void) {}
void tearDown(void) {}
static void test_cursor_at_exactly_cap_is_skipped(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 64u + 2u, sse_cursor_fix(1000u, 1000u - 64u, 64u));
}
static void test_cursor_inside_the_window_is_untouched(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 63u, sse_cursor_fix(1000u, 1000u - 63u, 64u));
    TEST_ASSERT_EQUAL_UINT32(1000u, sse_cursor_fix(1000u, 1000u, 64u));
}
static void test_far_overrun_and_wraparound(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 64u + 2u, sse_cursor_fix(1000u, 100u, 64u));
    /* unsigned wrap: head has rolled past 2^32, cursor has not */
    TEST_ASSERT_EQUAL_UINT32(5u - 64u + 2u, sse_cursor_fix(5u, 0xFFFFFF00u, 64u));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_cursor_at_exactly_cap_is_skipped);
    RUN_TEST(test_cursor_inside_the_window_is_untouched); RUN_TEST(test_far_overrun_and_wraparound);
    return UNITY_END(); }
