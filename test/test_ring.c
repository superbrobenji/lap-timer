#include "unity.h"
#include "core/ring.h"
#include <pthread.h>

#ifndef RING_STRESS_N
#define RING_STRESS_N 2000000ULL          /* target build overrides with -DRING_STRESS_N=20000ULL */
#endif

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

typedef struct { uint64_t tag; uint64_t check; } stress_item_t;   /* invariant: check == ~tag */
typedef struct { ring_t *r; uint64_t n; } stress_arg_t;

static void *stress_producer(void *p)
{
    stress_arg_t *a = p;
    for (uint64_t i = 1; i <= a->n; i++) {
        stress_item_t it = { i, ~i };
        while (!ring_push(a->r, &it)) { /* drop-newest: spin until there is room */ }
    }
    return NULL;
}

/* Pops until it has seen the final tag; counts torn items and order violations. */
typedef struct { ring_t *r; uint64_t last_tag; uint64_t torn; uint64_t out_of_order; uint64_t received; uint64_t last_expected; } stress_res_t;
static void *stress_consumer(void *p)
{
    stress_res_t *res = p;
    stress_item_t it;
    for (;;) {
        if (!ring_pop(res->r, &it)) continue;
        res->received++;
        if (it.check != ~it.tag) res->torn++;
        if (it.tag <= res->last_tag) res->out_of_order++;
        res->last_tag = it.tag;
        if (it.tag == res->last_expected) break;
    }
    return NULL;
}

static void test_concurrent_overwrite_oldest_never_returns_torn_items(void)
{
    static stress_item_t storage[2]; static ring_t r;
    ring_init(&r, storage, sizeof(stress_item_t), 2, true);
    const uint64_t N = RING_STRESS_N;
    stress_arg_t pa = { &r, N };
    stress_res_t cr = { &r, 0, 0, 0, 0, N };
    pthread_t pt, ct;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&ct, NULL, stress_consumer, &cr));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&pt, NULL, stress_producer, &pa));
    pthread_join(pt, NULL); pthread_join(ct, NULL);
    TEST_ASSERT_EQUAL_UINT64(0, cr.torn);
    TEST_ASSERT_EQUAL_UINT64(0, cr.out_of_order);
    TEST_ASSERT_EQUAL_UINT64(N, cr.last_tag);
    TEST_ASSERT_EQUAL_UINT64(N, cr.received + ring_dropped(&r));   /* every item was delivered or counted dropped */
}

static void test_concurrent_drop_newest_delivers_everything_in_order(void)
{
    static stress_item_t storage[4]; static ring_t r;
    ring_init(&r, storage, sizeof(stress_item_t), 4, false);
    const uint64_t N = RING_STRESS_N;
    stress_arg_t pa = { &r, N };
    stress_res_t cr = { &r, 0, 0, 0, 0, N };
    pthread_t pt, ct;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&ct, NULL, stress_consumer, &cr));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&pt, NULL, stress_producer, &pa));
    pthread_join(pt, NULL); pthread_join(ct, NULL);
    TEST_ASSERT_EQUAL_UINT64(0, cr.torn);
    TEST_ASSERT_EQUAL_UINT64(0, cr.out_of_order);
    TEST_ASSERT_EQUAL_UINT64(N, cr.received);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fifo_order_and_wraparound);
    RUN_TEST(test_drop_newest_policy_counts_drops);
    RUN_TEST(test_overwrite_oldest_policy_keeps_newest);
    RUN_TEST(test_concurrent_overwrite_oldest_never_returns_torn_items);
    RUN_TEST(test_concurrent_drop_newest_delivers_everything_in_order);
    return UNITY_END();
}
