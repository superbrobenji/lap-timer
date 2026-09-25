/* test_stage_bounds.c -- Plan 5.6 Task 7 fix round 1: the pure size arithmetic behind otastage's
 * exact (console `flash stage <size> <sha>`) vs bounded (POST /api/flash) semantics. otastage.c
 * itself is IDF-bound (esp_partition/mbedtls) so this exercises the carved-out pure helpers
 * directly instead.
 */
#include "unity.h"
#include "stage_bounds.h"

void setUp(void) {}
void tearDown(void) {}

static void test_write_overflow_boundary(void)
{
    /* exactly at the bound: not an overflow */
    TEST_ASSERT_FALSE(stage_bounds_write_overflows(90u, 5u, 5u, 100u));
    /* one byte past: overflow */
    TEST_ASSERT_TRUE(stage_bounds_write_overflows(90u, 5u, 6u, 100u));
    /* nothing committed or pending yet, first write exactly fills the bound */
    TEST_ASSERT_FALSE(stage_bounds_write_overflows(0u, 0u, 100u, 100u));
    /* nothing committed or pending yet, first write exceeds a tiny bound */
    TEST_ASSERT_TRUE(stage_bounds_write_overflows(0u, 0u, 1u, 0u));
}

static void test_write_overflow_no_wrap_near_u32_max(void)
{
    /* written + pending + n could overflow a 32-bit accumulator; the pure check must widen to
     * 64-bit before comparing, not wrap. */
    TEST_ASSERT_TRUE(stage_bounds_write_overflows(0xFFFFFFFFu, 0u, 1u, 0xFFFFFFFFu));
    TEST_ASSERT_FALSE(stage_bounds_write_overflows(0xFFFFFFFEu, 0u, 1u, 0xFFFFFFFFu));
}

static void test_finish_exact_requires_equality(void)
{
    TEST_ASSERT_TRUE(stage_bounds_finish_ok(STAGE_BOUNDS_EXACT, 500u, 500u));
    /* interrupted upload: fewer bytes staged than declared -- must be rejected */
    TEST_ASSERT_FALSE(stage_bounds_finish_ok(STAGE_BOUNDS_EXACT, 480u, 500u));
    /* stage_bounds_write_overflows already prevents written from ever exceeding the exact bound,
     * but the finish check itself must not accept an over-count either. */
    TEST_ASSERT_FALSE(stage_bounds_finish_ok(STAGE_BOUNDS_EXACT, 520u, 500u));
}

static void test_finish_bounded_only_requires_at_most(void)
{
    TEST_ASSERT_TRUE(stage_bounds_finish_ok(STAGE_BOUNDS_BOUNDED, 500u, 500u));
    /* a bounded stage may legitimately finish with fewer bytes than the ceiling */
    TEST_ASSERT_TRUE(stage_bounds_finish_ok(STAGE_BOUNDS_BOUNDED, 100u, 500u));
    TEST_ASSERT_FALSE(stage_bounds_finish_ok(STAGE_BOUNDS_BOUNDED, 520u, 500u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_overflow_boundary);
    RUN_TEST(test_write_overflow_no_wrap_near_u32_max);
    RUN_TEST(test_finish_exact_requires_equality);
    RUN_TEST(test_finish_bounded_only_requires_at_most);
    return UNITY_END();
}
