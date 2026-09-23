/* linkstats.c -- see linkstats.h. Pure, IDF-free; no heap; module-global state (mirrors
 * linkhost_proto.c's demux ring). Pragmatic P10 (Plan 5.5/5.6 Global Constraints): >=2 assertions
 * in functions over ~20 code lines.
 */
#include "linkstats.h"

#include <assert.h>
#include <string.h>

static linkstats_t s;
static uint16_t s_last_seq;
static bool     s_seq_valid;

void linkstats_reset(void)
{
    memset(&s, 0, sizeof s);
    s_last_seq = 0;
    s_seq_valid = false;
}

void linkstats_on_record(const lt_stream_rec_t *r, int64_t now_us)
{
    assert(r != NULL);
    assert(now_us >= 0);
    /* Cross-type: the lap-timer runs one seq counter for the whole stream (§14), so any
     * non-+1 step -- regardless of the previous/current record's type -- is a dropped frame. */
    if (s_seq_valid && (uint16_t)(r->seq - s_last_seq) != 1u) s.gaps++;
    s_last_seq = r->seq;
    s_seq_valid = true;
    switch (r->type) {
    case LT_SES_T_FUSED: s.n_fused++;  s.last_fused_us = now_us; break;
    case LT_SES_T_EVENT: s.n_event++;  s.last_event_us = now_us; break;
    case LT_REC_STATUS:
        s.n_status++; s.last_status_us = now_us;
        if (r->len >= LT_STATUS_LEN) { memcpy(s.status_rec, r->data, LT_STATUS_LEN); s.status_valid = true; }
        break;
    default: s.n_other++; break;
    }
}

void linkstats_snapshot(linkstats_t *out)
{
    assert(out != NULL);
    *out = s;
}

int64_t linkstats_age_ms(int64_t last_us, int64_t now_us)
{
    if (last_us == 0) return -1;                 /* never seen */
    int64_t d = (now_us - last_us) / 1000;
    return d < 0 ? 0 : d;
}

bool linkstats_status_fresh(int64_t now_us, uint32_t stale_ms, lt_status_t *out)
{
    assert(out != NULL);
    if (!s.status_valid) return false;
    int64_t age = linkstats_age_ms(s.last_status_us, now_us);
    if (age < 0 || age >= (int64_t)stale_ms) return false;
    return linkhost_status_decode(s.status_rec, out);
}
