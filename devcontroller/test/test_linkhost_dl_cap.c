/* devcontroller/test/test_linkhost_dl_cap.c -- host tests for the streaming download's BOUNDS
 * (dk:M1): the announced-size cap, the absolute cap arithmetic, and the progress counter the IDF
 * loop's idle timer is allowed to trust. Pure: no UART, no IDF. */
#include "unity.h"
#include "linkhost_proto.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static int sink_count;
static int sink_cb(void *ctx, const uint8_t *d, size_t n) { (void)ctx; (void)d; (void)n; sink_count++; return 0; }

/* dk:M1 -- the reviewer's dl.c header: a 9-digit size parses today and scales the cap to 36 h.
 * It must now be rejected outright, in the same feed call, as a protocol error. */
static void test_oversize_header_is_rejected(void)
{
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    const char *hdr = "---BEGIN sessions 999999999---\n";
    lh_dl_state_t st = lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr));
    TEST_ASSERT_EQUAL_INT(LH_DL_ERR, st);
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_PROTO, lh_dl_result(&c));
}

/* Exactly LH_DL_BODY_MAX is accepted (the boundary is inclusive); one byte more is not. */
static void test_body_size_boundary(void)
{
    char hdr[64];
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    snprintf(hdr, sizeof hdr, "---BEGIN log %u---\n", (unsigned)LH_DL_BODY_MAX);
    TEST_ASSERT_EQUAL_INT(LH_DL_BODY, lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr)));
    lh_dl_init(&c, false, sink_cb, NULL);
    snprintf(hdr, sizeof hdr, "---BEGIN log %u---\n", (unsigned)LH_DL_BODY_MAX + 1u);
    TEST_ASSERT_EQUAL_INT(LH_DL_ERR, lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr)));
}

/* The cap: t_header + 1.5 * size*10/baud + LH_DL_MARGIN_US, never more than
 * t_header + LH_DL_HARD_MAX_MS, and never less than t_header + the margin. */
static void test_cap_arithmetic(void)
{
    TEST_ASSERT_EQUAL_INT64(1000 + LH_DL_MARGIN_US, lh_dl_cap_us(0, 1000, 115200));
    /* a 46 KB .log: 46*1024*10/115200 = 4.09 s; *1.5 = 6.13 s; + 5 s = 11.13 s */
    int64_t cap = lh_dl_cap_us(46u * 1024u, 0, 115200);
    TEST_ASSERT_TRUE(cap > 11000000 && cap < 11300000);
    /* 2 MiB at 115200 = 182 s; *1.5 + 5 s = 278 s -- comfortably under the 20 min ceiling */
    cap = lh_dl_cap_us(LH_DL_BODY_MAX, 0, 115200);
    TEST_ASSERT_TRUE(cap > 275000000 && cap < 285000000);
    /* the ceiling binds at a low baud: 2 MiB at 9600 would be 36 min */
    TEST_ASSERT_EQUAL_INT64((int64_t)LH_DL_HARD_MAX_MS * 1000, lh_dl_cap_us(LH_DL_BODY_MAX, 0, 9600));
    /* an impossible size can never widen the window either (belt and braces to the header check):
     * clamped to LH_DL_BODY_MAX, at the same slow baud, it hits the identical ceiling -- not a
     * wildly larger value an unclamped 0xFFFFFFFF*10 would overflow/scale to. */
    TEST_ASSERT_EQUAL_INT64((int64_t)LH_DL_HARD_MAX_MS * 1000, lh_dl_cap_us(0xFFFFFFFFu, 0, 9600));
}

/* dk:M1's second half -- the idle timer must be fed ONLY by body bytes before ---END. Noise in
 * the header phase (a prompt, a log line, the lap-timer's resumed 0xFF stream) leaves the counter
 * at 0; the trailer freezes it. */
static void test_progress_counts_only_body_bytes(void)
{
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    const char *noise = "laptimer> \r\nI (12) x: hi\r\n";
    lh_dl_feed(&c, (const uint8_t *)noise, strlen(noise));
    TEST_ASSERT_EQUAL_UINT32(0, lh_dl_progress(&c));
    const char *hdr = "---BEGIN status 4---\r\n";
    lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr));
    TEST_ASSERT_EQUAL_UINT32(0, lh_dl_progress(&c));
    lh_dl_feed(&c, (const uint8_t *)"ab", 2);
    TEST_ASSERT_EQUAL_UINT32(2, lh_dl_progress(&c));
    lh_dl_feed(&c, (const uint8_t *)"cd", 2);                  /* body complete -> LH_DL_TAIL */
    uint32_t frozen = lh_dl_progress(&c);
    TEST_ASSERT_EQUAL_UINT32(4, frozen);
    lh_dl_feed(&c, (const uint8_t *)"\r\n", 2);                /* trailer bytes: no progress */
    TEST_ASSERT_EQUAL_UINT32(frozen, lh_dl_progress(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_oversize_header_is_rejected);
    RUN_TEST(test_body_size_boundary);
    RUN_TEST(test_cap_arithmetic);
    RUN_TEST(test_progress_counts_only_body_bytes);
    return UNITY_END();
}
