/* devcontroller/components/flashcore/include/stage_bounds.h -- stage_bounds: the pure size
 * arithmetic behind otastage's exact-vs-bounded write/finish checks (Plan 5.6 Task 7 fix round 1).
 *
 * otastage.c itself is IDF-bound (esp_partition, mbedtls) so it cannot be linked into the host
 * test harness. This carves out the one part of its size accounting that is pure arithmetic --
 * whether a write would overflow the declared bound, and whether a finished stage's byte count
 * satisfies EXACT vs BOUNDED semantics -- into a tiny, host-testable module (test_stage_bounds.c)
 * that otastage.c calls into for both checks.
 */
#ifndef STAGE_BOUNDS_H
#define STAGE_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STAGE_BOUNDS_EXACT,     /* otastage_begin: `bound` is the exact expected image size */
    STAGE_BOUNDS_BOUNDED    /* otastage_begin_bounded: `bound` is only an upper limit */
} stage_bounds_mode_t;

/* True if writing `n` more bytes on top of `written` bytes already committed to flash plus
 * `pending` bytes already buffered would exceed `bound`. Mode-independent: an EXACT bound and a
 * BOUNDED bound are both just "the most this write is allowed to reach". */
bool stage_bounds_write_overflows(uint32_t written, size_t pending, size_t n, uint32_t bound);

/* True if a finished stage of `written` total committed bytes is size-valid for `mode` against
 * `bound`. EXACT requires written == bound (an interrupted or short exact-size upload is
 * rejected). BOUNDED only requires written <= bound -- already guaranteed by every prior
 * stage_bounds_write_overflows check along the way, so this is a defensive restatement rather
 * than a new constraint for BOUNDED callers. Does NOT check IMG_DESC_MIN_LEN: that floor is
 * mode-independent and applied by otastage_finish itself. */
bool stage_bounds_finish_ok(stage_bounds_mode_t mode, uint32_t written, uint32_t bound);

#ifdef __cplusplus
}
#endif

#endif /* STAGE_BOUNDS_H */
