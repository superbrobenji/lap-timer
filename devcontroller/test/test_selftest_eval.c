/* test_selftest_eval.c -- Unity host tests for components/devconsole/host/selftest_eval.c
 * (Plan 5.6 Task 9): the pure verdict logic behind `selftest link|stream|framing|all`. Exercises
 * every threshold and the reason-string priority order (max > median > failures for link; fused >
 * status > gaps for stream; markers > cr for framing) directly, with no UART/IDF involved.
 */
#include "selftest_eval.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ================================================================================================
 *  selftest_link_eval -- median < 100 ms, max < 500 ms, 0 failures
 * ============================================================================================== */

/* Brief Step 1: {40,50,60}, 0 failures -> pass, med 50. */
void test_link_pass(void)
{
    uint32_t lat[] = { 40, 50, 60 };
    st_link_t r;
    selftest_link_eval(lat, 3, 0, &r);
    TEST_ASSERT_TRUE(r.pass);
    TEST_ASSERT_EQUAL_UINT32(50, r.med_ms);
    TEST_ASSERT_EQUAL_UINT32(60, r.max_ms);
    TEST_ASSERT_EQUAL_UINT32(40, r.min_ms);
    TEST_ASSERT_EQUAL_INT(0, r.failures);
    TEST_ASSERT_EQUAL_STRING("", r.reason);
}

/* Brief Step 1: {40,50,900} -> fail "max" (900 >= 500; median of the three is still 50, under
 * threshold -- proves "max" is reported, not "median", when both could theoretically apply). */
void test_link_fail_max(void)
{
    uint32_t lat[] = { 40, 50, 900 };
    st_link_t r;
    selftest_link_eval(lat, 3, 0, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_STRING("max", r.reason);
}

/* Brief Step 1: failures 1 -> fail. The three successful latencies alone are well under both the
 * median and max thresholds, so "failures" is the only possible reason. */
void test_link_fail_failures(void)
{
    uint32_t lat[] = { 40, 50, 60 };
    st_link_t r;
    selftest_link_eval(lat, 3, 1, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_INT(1, r.failures);
    TEST_ASSERT_EQUAL_STRING("failures", r.reason);
}

/* Added per task instructions: a "median" case -- max stays under 500 (170) but the median (160)
 * is >= the 100 ms threshold. */
void test_link_fail_median(void)
{
    uint32_t lat[] = { 150, 160, 170 };
    st_link_t r;
    selftest_link_eval(lat, 3, 0, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_UINT32(160, r.med_ms);
    TEST_ASSERT_EQUAL_STRING("median", r.reason);
}

/* Every attempt failed (n == 0): med/max/min report 0 (trivially under threshold), so the ONLY
 * possible fail reason is "failures" -- proves the n==0 path doesn't crash or misreport. */
void test_link_all_failed(void)
{
    st_link_t r;
    selftest_link_eval(NULL, 0, 5, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_UINT32(0, r.med_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r.max_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r.min_ms);
    TEST_ASSERT_EQUAL_STRING("failures", r.reason);
}

/* Even n exercises the two-middle-value averaging branch of the median (not covered by the
 * brief's odd-n cases). */
void test_link_even_n_median(void)
{
    uint32_t lat[] = { 10, 20, 30, 40 };   /* sorted already; median = (20+30)/2 = 25 */
    st_link_t r;
    selftest_link_eval(lat, 4, 0, &r);
    TEST_ASSERT_TRUE(r.pass);
    TEST_ASSERT_EQUAL_UINT32(25, r.med_ms);
}

/* ================================================================================================
 *  selftest_stream_eval -- fused >= 8/s, status >= 0.8/s, 0 gaps
 * ============================================================================================== */

/* Brief Step 1: (100,10,0,10 s) -> pass. */
void test_stream_pass(void)
{
    st_stream_t r;
    selftest_stream_eval(100, 10, 0, 10.0f, &r);
    TEST_ASSERT_TRUE(r.pass);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, r.fused_rate);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.status_rate);
    TEST_ASSERT_EQUAL_UINT32(0, r.gaps);
    TEST_ASSERT_EQUAL_STRING("", r.reason);
}

/* Brief Step 1: (50,10,0,10) -> fail "fused" (rate 5.0 < 8.0). */
void test_stream_fail_fused(void)
{
    st_stream_t r;
    selftest_stream_eval(50, 10, 0, 10.0f, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_STRING("fused", r.reason);
}

/* status rate 0.5 < 0.8, fused rate 10.0 passes -- proves "status" is reachable (checked after
 * "fused" per the priority order). */
void test_stream_fail_status(void)
{
    st_stream_t r;
    selftest_stream_eval(100, 5, 0, 10.0f, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_STRING("status", r.reason);
}

/* Brief Step 1: gaps 1 -> fail "gaps" (fused/status both pass). */
void test_stream_fail_gaps(void)
{
    st_stream_t r;
    selftest_stream_eval(100, 10, 1, 10.0f, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_UINT32(1, r.gaps);
    TEST_ASSERT_EQUAL_STRING("gaps", r.reason);
}

/* ================================================================================================
 *  selftest_framing_eval -- BEGIN/END markers present, every reply line has exactly one CR
 *  before its LF (a "\r\r\n" is a fail)
 * ============================================================================================== */

/* Brief Step 1: a well-formed reply -> pass, 2 markers (one ---BEGIN, one ---END), 0 bad lines. */
void test_framing_pass(void)
{
    const char *buf = "---BEGIN x 3---\r\nabc\r\n---END 00000000---\r\n";
    st_framing_t r;
    selftest_framing_eval((const uint8_t *)buf, strlen(buf), &r);
    TEST_ASSERT_TRUE(r.pass);
    TEST_ASSERT_EQUAL_INT(2, r.markers);
    TEST_ASSERT_EQUAL_INT(0, r.bad);
    TEST_ASSERT_EQUAL_STRING("", r.reason);
}

/* Brief Step 1 + task instruction ("a 'cr' case"): the same reply with a doubled CR ("\r\r\n")
 * before ---END -> bad 1, fail "cr" (markers is unaffected: still 2). */
void test_framing_fail_cr(void)
{
    const char *buf = "---BEGIN x 3---\r\nabc\r\r\n---END 00000000---\r\n";
    st_framing_t r;
    selftest_framing_eval((const uint8_t *)buf, strlen(buf), &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_INT(2, r.markers);
    TEST_ASSERT_EQUAL_INT(1, r.bad);
    TEST_ASSERT_EQUAL_STRING("cr", r.reason);
}

/* No markers at all (an empty/garbage capture, e.g. a timed-out `status`) -> fail "markers". */
void test_framing_fail_markers(void)
{
    const char *buf = "no markers here\r\n";
    st_framing_t r;
    selftest_framing_eval((const uint8_t *)buf, strlen(buf), &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_INT(0, r.markers);
    TEST_ASSERT_EQUAL_STRING("markers", r.reason);
}

/* An n == 0 capture (nothing captured at all) must not crash and must fail "markers". */
void test_framing_empty(void)
{
    st_framing_t r;
    selftest_framing_eval(NULL, 0, &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_INT(0, r.markers);
    TEST_ASSERT_EQUAL_STRING("markers", r.reason);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_link_pass);
    RUN_TEST(test_link_fail_max);
    RUN_TEST(test_link_fail_failures);
    RUN_TEST(test_link_fail_median);
    RUN_TEST(test_link_all_failed);
    RUN_TEST(test_link_even_n_median);
    RUN_TEST(test_stream_pass);
    RUN_TEST(test_stream_fail_fused);
    RUN_TEST(test_stream_fail_status);
    RUN_TEST(test_stream_fail_gaps);
    RUN_TEST(test_framing_pass);
    RUN_TEST(test_framing_fail_cr);
    RUN_TEST(test_framing_fail_markers);
    RUN_TEST(test_framing_empty);
    return UNITY_END();
}
