/* linkstats.h -- pure, IDF-free stream statistics for the dev-kit's linkhost demux (Plan 5.6
 * Task 2). Folds each demuxed stream record (lt_stream_rec_t, from linkhost_stream_pop) into
 * per-type counters, cross-type sequence-gap detection (the lap-timer runs ONE seq counter for
 * the whole 0xFF stream, across all record types), last-seen ages, and a decoded LT_REC_STATUS
 * cache with staleness. No heap; module-global state (mirrors linkhost_proto.c's demux ring).
 */
#ifndef LINKSTATS_H
#define LINKSTATS_H

#include <stdbool.h>
#include <stdint.h>

#include "linkhost_proto.h"   /* lt_stream_rec_t, lt_status_t, linkhost_status_decode, LT_STATUS_LEN */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t n_fused, n_event, n_status, n_other;
    uint32_t gaps;               /* seq discontinuities (dropped frames) */
    int64_t  last_fused_us, last_event_us, last_status_us;   /* 0 = never */
    uint8_t  status_rec[LT_STATUS_LEN];
    bool     status_valid;
} linkstats_t;

/* Clears all counters/ages and the STATUS cache (call at test start / link attach). */
void    linkstats_reset(void);
/* Folds one demuxed stream record into the running stats: per-type count + last-seen time,
 * cross-type seq-gap detection (any non-+1 step vs. the previous record of ANY type bumps
 * `gaps`), and (for LT_REC_STATUS, when the payload is at least LT_STATUS_LEN) the raw STATUS
 * cache. */
void    linkstats_on_record(const lt_stream_rec_t *r, int64_t now_us);
/* Copies the current stats out. */
void    linkstats_snapshot(linkstats_t *out);
/* Age of a last-seen timestamp in ms; -1 if last_us == 0 (never seen). */
int64_t linkstats_age_ms(int64_t last_us, int64_t now_us);
/* True (with *out decoded) iff a STATUS record has been cached and its age is < stale_ms; false
 * before any STATUS record has been seen, or once the cached one has aged to >= stale_ms. */
bool    linkstats_status_fresh(int64_t now_us, uint32_t stale_ms, lt_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LINKSTATS_H */
