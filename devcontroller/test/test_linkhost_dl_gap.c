/* test_linkhost_dl_gap.c -- bench B-F8 (ruling B-18): which no-bytes gap applies to
 * linkhost_download_cmd's timeout loop given the streaming parser's current state. Before the
 * header parses (state < LH_DL_BODY) the lap-timer's `list` scans every session summary before it
 * can emit the length-prefixed header -- ~80 ms/session, measured 3.0-3.6 s at 42 sessions -- so the
 * caller needs a longer no-bytes allowance than the steady-state body/tail gap. lh_dl_gap_ms is the
 * pure 3-line decision so linkhost.c's IDF timeout loop has one thing to call instead of repeating
 * the `state >= LH_DL_BODY` ternary inline.
 */
#include "unity.h"
#include "linkhost_proto.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- before the header parses: the longer "first byte" allowance applies ---- */
static void test_gap_before_header_uses_first_ms(void)
{
    TEST_ASSERT_EQUAL_INT(12000, lh_dl_gap_ms(LH_DL_HDR, 12000, 3000));
}

/* ---- once the header has parsed (state >= LH_DL_BODY): the shorter idle allowance applies ---- */
static void test_gap_at_body_uses_idle_ms(void)
{
    TEST_ASSERT_EQUAL_INT(3000, lh_dl_gap_ms(LH_DL_BODY, 12000, 3000));
}

/* ---- TAIL is also >= BODY: still the idle allowance ---- */
static void test_gap_at_tail_uses_idle_ms(void)
{
    TEST_ASSERT_EQUAL_INT(3000, lh_dl_gap_ms(LH_DL_TAIL, 12000, 3000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gap_before_header_uses_first_ms);
    RUN_TEST(test_gap_at_body_uses_idle_ms);
    RUN_TEST(test_gap_at_tail_uses_idle_ms);
    return UNITY_END();
}
