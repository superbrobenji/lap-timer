#include "unity.h"
#include "core/ring.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct { int a; int b; } item_t;

static void test_fifo_order_and_wraparound(void)
{
    item_t storage[4]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 4, false);
    for (int i = 0; i < 3; i++) { item_t it = { i, i * 10 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }
    TEST_ASSERT_EQUAL_UINT32(3, ring_count(&r));
    item_t out;
    TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(0, out.a);
    TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(1, out.a);
    for (int i = 3; i < 6; i++) { item_t it = { i, 0 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }   /* wraps */
    TEST_ASSERT_EQUAL_UINT32(4, ring_count(&r));
    for (int i = 2; i < 6; i++) { TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(i, out.a); }
    TEST_ASSERT_FALSE(ring_pop(&r, &out));
}

static void test_drop_newest_policy_counts_drops(void)
{
    item_t storage[2]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 2, false);
    item_t it = { 1, 0 };
    TEST_ASSERT_TRUE(ring_push(&r, &it));
    it.a = 2; TEST_ASSERT_TRUE(ring_push(&r, &it));
    it.a = 3; TEST_ASSERT_FALSE(ring_push(&r, &it));
    TEST_ASSERT_EQUAL_UINT32(1, ring_dropped(&r));
    item_t out; ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(1, out.a);
}

static void test_overwrite_oldest_policy_keeps_newest(void)
{
    item_t storage[2]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 2, true);
    for (int i = 1; i <= 3; i++) { item_t it = { i, 0 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }
    TEST_ASSERT_EQUAL_UINT32(2, ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(1, ring_dropped(&r));
    item_t out;
    ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(2, out.a);
    ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(3, out.a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fifo_order_and_wraparound);
    RUN_TEST(test_drop_newest_policy_counts_drops);
    RUN_TEST(test_overwrite_oldest_policy_keeps_newest);
    return UNITY_END();
}
