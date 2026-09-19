#include "core/tb.h"
#include "core/consts.h"
#include "core/core.h"
#include <string.h>

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define TB_ASSERT_CODE 0x0A40

#define HALF_WINDOW_US ((int64_t)TB_WINDOW_S * 1000000LL / 2)

int64_t tb_days_from_civil(int y, unsigned m, unsigned d)
{
    CORE_ASSERT_RET(m >= 1 && m <= 12, TB_ASSERT_CODE, 0);   /* civil month */
    CORE_ASSERT_RET(d >= 1 && d <= 31, TB_ASSERT_CODE, 0);   /* civil day */
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? 0u - 3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

int64_t tb_gps_us_from_utc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, int32_t nano)
{
    CORE_ASSERT_RET(m >= 1 && m <= 12, TB_ASSERT_CODE, 0);   /* civil month */
    CORE_ASSERT_RET(d >= 1 && d <= 31, TB_ASSERT_CODE, 0);   /* civil day */
    CORE_ASSERT_RET(hh < 24, TB_ASSERT_CODE, 0);             /* hour of day */
    CORE_ASSERT_RET(mm < 60, TB_ASSERT_CODE, 0);             /* minute of hour */
    CORE_ASSERT_RET(ss <= 60, TB_ASSERT_CODE, 0);            /* second (60 tolerates a leap second) */
    int64_t secs = tb_days_from_civil(y, m, d) * 86400LL + (int64_t)hh * 3600 + (int64_t)mm * 60 + (int64_t)ss;
    int64_t us = secs * 1000000LL;
    /* floor division of nano by 1000 so negative nano rounds toward -inf */
    int64_t q = nano / 1000;
    if ((nano % 1000) < 0) q -= 1;
    return us + q;
}

void tb_init(tb_t *t)
{
    CORE_ASSERT_VOID(t != NULL, TB_ASSERT_CODE);
    memset(t, 0, sizeof *t);
}

static void recompute(tb_t *t)
{
    CORE_ASSERT_VOID(t != NULL, TB_ASSERT_CODE);
    CORE_ASSERT_VOID(t->cur == 0 || t->cur == 1, TB_ASSERT_CODE);   /* half index is one of two */
    int64_t m = 0; bool any = false;
    for (int i = 0; i < 2; i++) {
        if (!t->half_valid[i]) continue;
        if (!any || t->half_min[i] < m) { m = t->half_min[i]; any = true; }
    }
    if (any) t->filt_offset_us = m;
}

void tb_on_fix(tb_t *t, int64_t fix_gps_us, int64_t arrival_mono_us, int64_t serial_time_us)
{
    CORE_ASSERT_VOID(t != NULL, TB_ASSERT_CODE);
    CORE_ASSERT_VOID(arrival_mono_us >= 0, TB_ASSERT_CODE);   /* monotonic clock since boot */
    CORE_ASSERT_VOID(serial_time_us >= 0, TB_ASSERT_CODE);    /* message transmit time */
    int64_t o = (arrival_mono_us - serial_time_us) - fix_gps_us;
    int c = t->cur;
    CORE_ASSERT_VOID(c == 0 || c == 1, TB_ASSERT_CODE);       /* half index is one of two */
    if (!t->half_valid[c]) {
        t->half_min[c] = o; t->half_start_mono[c] = arrival_mono_us; t->half_valid[c] = true;
    } else if (arrival_mono_us - t->half_start_mono[c] >= HALF_WINDOW_US) {
        c = 1 - c; t->cur = c;
        t->half_min[c] = o; t->half_start_mono[c] = arrival_mono_us; t->half_valid[c] = true;
    } else if (o < t->half_min[c]) {
        t->half_min[c] = o;
    }
    t->fixes++;
    recompute(t);
    /* PPS staleness: without edges the PPS offset cannot track crystal drift; fall back to the filter */
    if (t->pps_valid && arrival_mono_us - t->pps_edge_mono_us > TB_PPS_STALE_US) t->pps_valid = false;
}

void tb_on_pps(tb_t *t, int64_t edge_mono_us, int64_t top_of_second_gps_us)
{
    CORE_ASSERT_VOID(t != NULL, TB_ASSERT_CODE);
    CORE_ASSERT_VOID(top_of_second_gps_us >= 0, TB_ASSERT_CODE);   /* valid UTC top-of-second */
    int64_t o = edge_mono_us - top_of_second_gps_us;
    int64_t ref = t->pps_valid ? t->pps_offset_us : t->filt_offset_us;
    bool must_check = t->pps_valid || tb_locked(t);
    if (must_check) {
        int64_t diff = o - ref;
        if (diff > TB_PPS_DISAGREE_US || diff < -TB_PPS_DISAGREE_US) { t->pps_valid = false; return; }
    }
    t->pps_offset_us = o; t->pps_valid = true; t->pps_edge_mono_us = edge_mono_us;
}

int64_t tb_mono_to_gps(const tb_t *t, int64_t mono_us)
{
    CORE_ASSERT_RET(t != NULL, TB_ASSERT_CODE, mono_us);
    return mono_us - (t->pps_valid ? t->pps_offset_us : t->filt_offset_us);
}

bool tb_locked(const tb_t *t)
{
    CORE_ASSERT_RET(t != NULL, TB_ASSERT_CODE, false);
    return t->fixes >= TB_LOCK_FIXES;
}

uint8_t tb_quality(const tb_t *t)
{
    CORE_ASSERT_RET(t != NULL, TB_ASSERT_CODE, 0);
    if (t->pps_valid) return 2;
    return tb_locked(t) ? 1 : 0;
}
