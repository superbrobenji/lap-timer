#ifndef CORE_RING_H
#define CORE_RING_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Single-producer single-consumer ring. Capacity must be a power of two.
 * Indices grow monotonically; the mask maps them to slots. */
typedef struct {
    uint8_t         *buf;
    uint32_t         item_size;
    uint32_t         mask;
    _Atomic uint32_t head;      /* next write index (producer) */
    _Atomic uint32_t tail;      /* next read index (consumer) */
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
        atomic_fetch_add(&r->dropped, 1u);
        if (!r->overwrite) return false;
        /* advance tail by one; if the consumer moved it concurrently, there is room anyway */
        uint32_t expected = t;
        atomic_compare_exchange_strong(&r->tail, &expected, t + 1u);
    }
    memcpy(r->buf + (size_t)(h & r->mask) * r->item_size, item, r->item_size);
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);
    return true;
}

static inline bool ring_pop(ring_t *r, void *out)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (t == h) return false;
    memcpy(out, r->buf + (size_t)(t & r->mask) * r->item_size, r->item_size);
    atomic_store_explicit(&r->tail, t + 1u, memory_order_release);
    return true;
}
#endif
