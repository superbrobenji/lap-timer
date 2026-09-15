#include "unity.h"
#include "core/tb.h"
#include "core/consts.h"
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_days_from_civil_known_dates(void)
{
    TEST_ASSERT_EQUAL_INT64(0, tb_days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(11017, tb_days_from_civil(2000, 3, 1));     /* leap day 2000 counted */
    TEST_ASSERT_EQUAL_INT64(20710, tb_days_from_civil(2026, 9, 14));
    TEST_ASSERT_EQUAL_INT64(19782, tb_days_from_civil(2024, 2, 29));
}

static void test_gps_us_from_utc_with_negative_nano(void)
{
    /* 2026-09-14 10:15:00 with nano = -500 → half a microsecond before the second */
    int64_t expect = (20710LL * 86400 + 10 * 3600 + 15 * 60) * 1000000LL;
    TEST_ASSERT_EQUAL_INT64(expect, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, 0));
    TEST_ASSERT_EQUAL_INT64(expect - 1, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, -500));   /* truncates toward -inf */
    TEST_ASSERT_EQUAL_INT64(expect + 1500, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, 1500000));
}

/* deterministic LCG so the test is reproducible */
static uint32_t lcg = 12345u;
static int64_t rnd_range(int64_t lo, int64_t hi) { lcg = lcg * 1103515245u + 12345u; return lo + (int64_t)((lcg >> 8) % (uint32_t)(hi - lo + 1)); }

static void test_min_filter_rejects_one_sided_jitter(void)
{
    tb_t t; tb_init(&t);
    const int64_t true_offset = 1000000;         /* mono = gps + 1 s */
    const int64_t serial_us = 25000;             /* transmit time of the message, known */
    int64_t gps = 1789380900LL * 1000000LL;
    for (int i = 0; i < 200; i++) {              /* 40 s of fixes at 5 Hz; the 30 s window sees ~150 */
        int64_t latency = serial_us + rnd_range(5000, 95000);   /* 30..120 ms total, one-sided */
        tb_on_fix(&t, gps, gps + true_offset + latency, serial_us);
        gps += 200000;
    }
    TEST_ASSERT_TRUE(tb_locked(&t));
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
    int64_t est = (gps + true_offset) - tb_mono_to_gps(&t, gps + true_offset);
    TEST_ASSERT_INT64_WITHIN(10000, true_offset, est);   /* residual = smallest latency drawn above the serial time */
}

static void test_not_locked_before_ten_fixes(void)
{
    tb_t t; tb_init(&t);
    for (int i = 0; i < TB_LOCK_FIXES - 1; i++) tb_on_fix(&t, 1000000LL * i, 1000000LL * i + 500000, 0);
    TEST_ASSERT_FALSE(tb_locked(&t));
    tb_on_fix(&t, 1000000LL * TB_LOCK_FIXES, 1000000LL * TB_LOCK_FIXES + 500000, 0);
    TEST_ASSERT_TRUE(tb_locked(&t));
}

static void test_window_rollover_forgets_old_minimum(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 0;
    /* first 10 s: offset 1.000 s exactly */
    for (int i = 0; i < 50; i++) { tb_on_fix(&t, gps, gps + 1000000, 0); gps += 200000; }
    /* next 40 s: the clock drifted, true offset now 1.020 s */
    for (int i = 0; i < 200; i++) { tb_on_fix(&t, gps, gps + 1020000, 0); gps += 200000; }
    int64_t est = (gps + 1020000) - tb_mono_to_gps(&t, gps + 1020000);
    TEST_ASSERT_INT64_WITHIN(1000, 1020000, est);
}

static void test_pps_takes_precedence_and_falls_back_on_disagreement(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 5000000000LL;
    for (int i = 0; i < 20; i++) { tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); gps += 200000; }
    tb_on_pps(&t, gps + 1000000, gps);                 /* exact edge: offset 1.000 s */
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    TEST_ASSERT_EQUAL_INT64(gps, tb_mono_to_gps(&t, gps + 1000000));
    tb_on_pps(&t, gps + 1000000 + 200000, gps);        /* 200 ms disagreement → rejected */
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
}

static void test_pps_expires_without_edges_but_survives_filter_glitch(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 7000000000LL;
    for (int i = 0; i < 20; i++) { tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); gps += 200000; }
    tb_on_pps(&t, gps + 1000000, gps);
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* a glitched fix with an absurdly early arrival drags the min-filter 200 ms away; PPS must survive */
    tb_on_fix(&t, gps, gps + 1000000 - 160000, 0);
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* edges keep coming for 4 s: still PPS */
    for (int s = 1; s <= 4; s++) { gps += 1000000; tb_on_pps(&t, gps + 1000000, gps); tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); }
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* no edge for 5.2 s of fixes: PPS expires, filter takes over */
    for (int i = 0; i < 26; i++) { gps += 200000; tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); }
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil_known_dates);
    RUN_TEST(test_gps_us_from_utc_with_negative_nano);
    RUN_TEST(test_min_filter_rejects_one_sided_jitter);
    RUN_TEST(test_not_locked_before_ten_fixes);
    RUN_TEST(test_window_rollover_forgets_old_minimum);
    RUN_TEST(test_pps_takes_precedence_and_falls_back_on_disagreement);
    RUN_TEST(test_pps_expires_without_edges_but_survives_filter_glitch);
    return UNITY_END();
}
