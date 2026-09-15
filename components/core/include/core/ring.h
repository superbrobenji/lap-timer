#ifndef CORE_RING_H
#define CORE_RING_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Single-producer single-consumer ring. Capacity must be a power of two.
 * Indices grow monotonically; the mask maps them to slots.
 *
 * Policies: drop-newest (push returns false when full) or overwrite-oldest
 * (push evicts the oldest item). With overwrite-oldest the producer may evict
 * the very slot the consumer is copying; the consumer detects that by publishing
 * its consumption with a compare-and-swap on tail and retries when it lost the
 * race, so a torn copy is never returned.
 * The discarded copy is a formal C11 data race (seqlock-style read of a slot being
 * overwritten); it is never observed, and the two-thread stress tests guard the invariant.
 *
 * A ring_t must not be copied or moved once either side has started using it.
 * ring_count() and ring_dropped() are approximate snapshots for diagnostics. */
typedef struct {
    uint8_t         *buf;
    uint32_t         item_size;
    uint32_t         mask;
    _Atomic uint32_t head;      /* next write index (producer) */
    _Atomic uint32_t tail;      /* next read index (consumer; producer advances it on eviction) */
    bool             overwrite;
    _Atomic uint32_t dropped;
} ring_t;

static inline void ring_init(ring_t *r, void *storage, uint32_t item_size, uint32_t cap_pow2, bool overwrite_oldest)
{
    r->buf = (uint8_t *)storage; r->item_size = item_size; r->mask = cap_pow2 - 1u;
    atomic_store(&r->head, 0u); atomic_store(&r->tail, 0u);
    r->overwrite = overwrite_oldest; atomic_store(&r->dropped, 0u);
}

static inline uint32_t ring_count(const ring_t *r)
{
    return atomic_load(&r->head) - atomic_load(&r->tail);
}

static inline uint32_t ring_dropped(const ring_t *r) { return atomic_load(&r->dropped); }

static inline bool ring_push(ring_t *r, const void *item)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t > r->mask) {                              /* full */
        if (!r->overwrite) { atomic_fetch_add(&r->dropped, 1u); return false; }
        uint32_t expected = t;
        /* Evict the oldest slot. If the consumer publishes its pop of that slot first,
         * our CAS fails, the item was delivered (not dropped), and the slot is free anyway. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &expected, t + 1u, memory_order_acq_rel, memory_order_acquire))
            atomic_fetch_add(&r->dropped, 1u);
    }
    memcpy(r->buf + (size_t)(h & r->mask) * r->item_size, item, r->item_size);
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);
    return true;
}

static inline bool ring_pop(ring_t *r, void *out)
{
    for (;;) {
        uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
        uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
        if (t == h) return false;
        memcpy(out, r->buf + (size_t)(t & r->mask) * r->item_size, r->item_size);
        /* Publish only if the producer did not evict this slot while we copied it. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &t, t + 1u, memory_order_acq_rel, memory_order_acquire))
            return true;
        /* Lost the race: the copy may be torn. Retry from the new tail. */
    }
}
#endif
