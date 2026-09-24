/* test_rawcap.c -- Unity host tests for linkhost_proto.c's raw reply capture (Plan 5.6 Task 9's
 * `selftest framing`): linkhost_rawcap_set/_copy/_dropped. Feeds bytes straight through
 * linkhost_feed (no linkhost.c/UART involved) and asserts the captured copy is byte-identical to
 * what was fed, that a stream frame captures nothing, that overflow truncates to the first 256
 * bytes and counts the rest as dropped, and that a second ---BEGIN restarts the capture.
 */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) { linkhost_reset(); }
void tearDown(void) {}

/* Builds one framed reply "---BEGIN <name> <bodylen>---\r\n<body>\r\n---END <crc8hex>---\r\n" into
 * `out` (cap `out_cap`), returning its length. `body` need not decode/CRC-verify for rawcap's
 * purposes (capture happens independent of linkhost_parse_frame's own success), but a real CRC is
 * computed anyway so the same buffer stays usable if a future test wants a valid frame too. */
static size_t build_frame(char *out, size_t out_cap, const char *name, const char *body)
{
    uint32_t crc = linkhost_crc32((const uint8_t *)body, strlen(body));
    int n = snprintf(out, out_cap, "---BEGIN %s %u---\r\n%s\r\n---END %08x---\r\n",
                     name, (unsigned)strlen(body), body, (unsigned)crc);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < out_cap);
    return (size_t)n;
}

/* Feeding a framed reply with capture ON: the copy equals the raw bytes fed, byte-for-byte, and
 * nothing was dropped. */
void test_capture_matches_raw_bytes_fed(void)
{
    char frame[128];
    size_t flen = build_frame(frame, sizeof frame, "status", "abc");

    linkhost_rawcap_set(true);
    TEST_ASSERT_EQUAL_UINT(flen, linkhost_feed((const uint8_t *)frame, flen));

    uint8_t out[128];
    size_t n = linkhost_rawcap_copy(out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(flen, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp(out, frame, flen));
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_dropped());
}

/* Capture OFF (the default after linkhost_reset()): the same frame produces nothing captured. */
void test_capture_off_by_default(void)
{
    char frame[128];
    size_t flen = build_frame(frame, sizeof frame, "status", "abc");
    linkhost_feed((const uint8_t *)frame, flen);

    uint8_t out[128];
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_copy(out, sizeof out));
}

/* A stream frame (0xFF record) must NEVER be captured, even with capture on. */
void test_stream_frame_not_captured(void)
{
    linkhost_rawcap_set(true);
    const uint8_t sframe[] = { 0xFF, 0x01, 0x00, 0x01, 0x02, 0x05, 0x77 };   /* seq=1 type=0x05 data={0x77} */
    linkhost_feed(sframe, sizeof sframe);

    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_copy(out, sizeof out));
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_dropped());

    /* the stream record itself still demuxed normally -- capture is orthogonal to the ring */
    lt_stream_rec_t r;
    TEST_ASSERT_EQUAL_INT(0, linkhost_stream_pop(&r));
    TEST_ASSERT_EQUAL_UINT16(1, r.seq);
}

/* A captured region > 256 bytes: only the first 256 bytes are kept; the rest are counted as
 * dropped (300 bytes fed -> 256 kept, 44 dropped -- the "feed 300 bytes" case from Task 9's
 * ambiguity resolution 1, sized here as a >256-byte frame with an exact drop count). */
void test_overflow_truncates_and_counts_dropped(void)
{
    char body[281];
    memset(body, 'a', sizeof body - 1);
    body[sizeof body - 1] = '\0';   /* 280 'a's */

    char frame[400];
    size_t flen = build_frame(frame, sizeof frame, "status", body);
    TEST_ASSERT_TRUE(flen > 256);   /* header(~24) + body(280) + tail(~22) comfortably exceeds 256 */

    linkhost_rawcap_set(true);
    linkhost_feed((const uint8_t *)frame, flen);

    uint8_t out[256];
    size_t n = linkhost_rawcap_copy(out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(256, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp(out, frame, 256));
    TEST_ASSERT_EQUAL_UINT(flen - 256, linkhost_rawcap_dropped());
}

/* A second ---BEGIN restarts the capture: after two back-to-back frames, the copy holds ONLY the
 * second one, not a concatenation or a mix. */
void test_second_begin_restarts_capture(void)
{
    char f1[128], f2[128];
    size_t l1 = build_frame(f1, sizeof f1, "status", "aaa");
    size_t l2 = build_frame(f2, sizeof f2, "config", "bbbbb");

    linkhost_rawcap_set(true);
    linkhost_feed((const uint8_t *)f1, l1);
    linkhost_feed((const uint8_t *)f2, l2);

    uint8_t out[128];
    size_t n = linkhost_rawcap_copy(out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(l2, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp(out, f2, l2));
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_dropped());
}

/* linkhost_reset() clears an armed/populated capture back to off + empty. */
void test_reset_clears_capture(void)
{
    char frame[128];
    size_t flen = build_frame(frame, sizeof frame, "status", "abc");
    linkhost_rawcap_set(true);
    linkhost_feed((const uint8_t *)frame, flen);

    linkhost_reset();

    uint8_t out[128];
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_copy(out, sizeof out));
    /* capture is off again post-reset: feeding the same frame captures nothing */
    linkhost_feed((const uint8_t *)frame, flen);
    TEST_ASSERT_EQUAL_UINT(0, linkhost_rawcap_copy(out, sizeof out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_capture_matches_raw_bytes_fed);
    RUN_TEST(test_capture_off_by_default);
    RUN_TEST(test_stream_frame_not_captured);
    RUN_TEST(test_overflow_truncates_and_counts_dropped);
    RUN_TEST(test_second_begin_restarts_capture);
    RUN_TEST(test_reset_clears_capture);
    return UNITY_END();
}
