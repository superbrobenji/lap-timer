#ifndef CORE_RING_H
#define CORE_RING_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "core/core.h"

/* R27/io:M1: every other for(;;) in core carries an explicit bound (ses_frame.c's NEXT_MAX_STEPS);
 * this one did not, so with overwrite_oldest and a producer on the other core a consumer could be
 * starved indefinitely. 16 attempts is far past any realistic CAS contention on two cores. */
#define RING_POP_RETRIES 16u
/* Assert-code occupancy as of this task (grep -rn "ASSERT_CODE 0x0A" components): 0x0A10 fus,
 * 0x0A20 lap, 0x0A30 drag, 0x0A40 tb, 0x0A50 ses, 0x0A60 exp, 0x0A70 cfg, 0x0A80 trk, 0x0A90 geo,
 * 0x0AA0 ui, 0x0AB0 json, 0x0AC0 rf, 0x0AD0 epd (display_epaper_ssd1680/host/epd_rotate.c),
 * 0x0AE0 btn_parse, 0x0AF0-0x0AF3 stats_fold/units/drag_cfg/menu_labels. 0x0AF4 continues that
 * overflow block and was free at the time of this grep. */
#define RING_ASSERT_CODE 0x0AF4

/* Single-producer single-consumer ring. Capacity must be a power of two.
 * Indices grow monotonically; the mask maps them to slots.
 *
 * Policies: drop-newest (push returns false when full) or overwrite-oldest
 * (push evicts the oldest item). With overwrite-oldest the producer may evict
 * the very slot the consumer is copying; the consumer detects that by publishing
 * its consumption with a compare-and-swap on tail, and retries when it lost the race,
 * at most RING_POP_RETRIES times before returning false (the caller polls again), so
 * a torn copy is never returned and the consumer is never unboundedly starved.
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
    CORE_ASSERT_VOID(r != NULL, RING_ASSERT_CODE);   /* nothing to initialise without r */
    /* mask = cap - 1 indexes correctly only for a power of two; any other capacity would alias two
     * indices onto one slot. Store the complete safe single-slot state UNCONDITIONALLY -- even
     * r->buf = NULL is a defined pointer, where skipping this assignment would leave *r->buf*
     * indeterminate -- before reporting any of the three preconditions below. CORE_ASSERT_VOID
     * returns on a failing report, so reporting first and initialising after would leave every
     * field an early return skipped indeterminate; any later read of such a ring_t is undefined
     * behaviour even though the ring is already known to be unusable. Storing first and reporting
     * last removes that UB-on-read; it does NOT make a NULL storage or a zero item_size usable. */
    uint32_t cap = cap_pow2;
    if (cap == 0u || (cap & (cap - 1u)) != 0u) { cap = 1u; }
    r->buf = (uint8_t *)storage; r->item_size = item_size; r->mask = cap - 1u;
    atomic_store(&r->head, 0u); atomic_store(&r->tail, 0u);
    r->overwrite = overwrite_oldest; atomic_store(&r->dropped, 0u);
    CORE_ASSERT_VOID(storage != NULL, RING_ASSERT_CODE);
    CORE_ASSERT_VOID(item_size > 0u, RING_ASSERT_CODE);
    CORE_ASSERT_VOID(cap == cap_pow2, RING_ASSERT_CODE);   /* the caller's capacity was usable */
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
    CORE_ASSERT_RET(r != NULL, RING_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, RING_ASSERT_CODE, false);
    for (uint32_t tries = 0; tries < RING_POP_RETRIES; tries++) {   /* rule 2 */
        uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
        uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
        if (t == h) return false;
        memcpy(out, r->buf + (size_t)(t & r->mask) * r->item_size, r->item_size);
        /* Publish only if the producer did not evict this slot while we copied it. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &t, t + 1u, memory_order_acq_rel, memory_order_acquire))
            return true;
        /* Lost the race: the copy may be torn. Retry from the new tail. */
    }
    return false;   /* contended out after RING_POP_RETRIES: the caller's next poll retries */
}
#endif
