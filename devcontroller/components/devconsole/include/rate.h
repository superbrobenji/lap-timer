/* devcontroller/components/devconsole/include/rate.h -- rate_x10: the pure records/sec*10
 * arithmetic behind `dc status`'s fused_rate/status_rate fields (Plan 5.6 Task 4 fix 1).
 *
 * PINNED interface (host-tested: devcontroller/test/test_rate.c). Pure, IDF-free: no shared
 * state, no heap. Split out of cmd_dc.c (which stays IDF glue) specifically so the "-1, not a
 * spurious huge rate" guards below are exercised by a host test rather than only by hand on
 * hardware.
 */
#ifndef RATE_H
#define RATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* records/sec * 10 (one implied decimal -- jsonw has no float writer) between an older sample
 * n_prev and a newer sample n_now, dt_us microseconds apart. Returns -1 ("no rate available"),
 * never a computed value, when:
 *   - dt_us <= 0 -- no elapsed time to divide by. Callers also use dt_us == 0 as the "no previous
 *     sample yet" sentinel for a first call, so this same guard covers both cases.
 *   - n_now < n_prev -- the counter went backwards (e.g. a link reset zeroed it out from under an
 *     in-flight sampling window). Reported as -1 rather than the huge value an unsigned
 *     subtraction's wraparound would otherwise silently produce. */
long long rate_x10(uint32_t n_now, uint32_t n_prev, int64_t dt_us);

#ifdef __cplusplus
}
#endif

#endif /* RATE_H */
