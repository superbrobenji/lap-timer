/* test_linkstats.c -- Plan 5.6 Task 2: linkstats (record counters, cross-type seq-gap detection,
 * last-seen ages, STATUS cache with staleness) over lt_stream_rec_t records as demuxed by
 * linkhost_stream_pop.
 */
#include "unity.h"
#include "linkstats.h"
#include "linkhost_proto.h"
#include <string.h>

static lt_stream_rec_t rec(uint8_t type, uint16_t seq, uint8_t len) {
    lt_stream_rec_t r; memset(&r, 0, sizeof r); r.type = type; r.seq = seq; r.len = len; return r;
}
void setUp(void) { linkstats_reset(); }
void tearDown(void) {}

void test_counts_and_ages(void) {
    lt_stream_rec_t f = rec(LT_SES_T_FUSED, 1, LT_FUSED_REC_LEN);
    linkstats_on_record(&f, 1000000);
    linkstats_t s; linkstats_snapshot(&s);
    TEST_ASSERT_EQUAL_UINT32(1, s.n_fused);
    TEST_ASSERT_EQUAL_INT64(1000000, s.last_fused_us);
    TEST_ASSERT_EQUAL_INT64(500, linkstats_age_ms(s.last_fused_us, 1500000));
    TEST_ASSERT_EQUAL_INT64(-1, linkstats_age_ms(0, 1500000));
}
void test_seq_gap_detected(void) {
    lt_stream_rec_t a = rec(LT_SES_T_FUSED, 10, LT_FUSED_REC_LEN), b = rec(LT_SES_T_FUSED, 11, LT_FUSED_REC_LEN),
                    c = rec(LT_SES_T_FUSED, 13, LT_FUSED_REC_LEN);   /* 12 missing */
    linkstats_on_record(&a, 1); linkstats_on_record(&b, 2); linkstats_on_record(&c, 3);
    linkstats_t s; linkstats_snapshot(&s);
    TEST_ASSERT_EQUAL_UINT32(1, s.gaps);
}
void test_status_cache_and_staleness(void) {
    lt_stream_rec_t st = rec(LT_REC_STATUS, 5, LT_STATUS_LEN);
    st.data[LT_ST_OFF_PROTO] = 1; st.data[LT_ST_OFF_SESS] = 7;
    linkstats_on_record(&st, 10000000);                      /* t = 10 s */
    lt_status_t out;
    TEST_ASSERT_TRUE(linkstats_status_fresh(12000000, 3000, &out));   /* 2 s old */
    TEST_ASSERT_EQUAL_UINT16(7, out.sessions);
    TEST_ASSERT_FALSE(linkstats_status_fresh(13500000, 3000, &out));  /* 3.5 s old */
}
void test_status_before_any_record_is_not_fresh(void) {
    lt_status_t out;
    TEST_ASSERT_FALSE(linkstats_status_fresh(1, 3000, &out));
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_counts_and_ages);
    RUN_TEST(test_seq_gap_detected);
    RUN_TEST(test_status_cache_and_staleness);
    RUN_TEST(test_status_before_any_record_is_not_fresh);
    return UNITY_END();
}
