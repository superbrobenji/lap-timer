/* devcontroller/test/test_bridge_filter.c -- host tests for the PURE bridge_filter logic
 * (components/linkhost/host/bridge_filter.c, Plan 5.6 Task 8): `lt shell`'s stream-frame removal
 * from the UART1 -> USB forward path. */
#include "unity.h"
#include "bridge_filter.h"
#include "linkhost_proto.h"   /* LT_STREAM_TAG, LT_REC_MAX */

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Builds a valid stream frame (tag, seq_lo, seq_hi, flags, len, payload[len]) into out, returns
 * its total on-wire length. */
static size_t build_frame(uint8_t *out, uint16_t seq, uint8_t flags, uint8_t len, uint8_t fill)
{
    out[0] = (uint8_t)LT_STREAM_TAG;
    out[1] = (uint8_t)(seq & 0xFF);
    out[2] = (uint8_t)(seq >> 8);
    out[3] = flags;
    out[4] = len;
    for (uint8_t i = 0; i < len; i++) out[5 + i] = fill;
    return (size_t)(5 + len);
}

/* Plain text with no 0xFF byte anywhere passes through byte-for-byte. */
void test_text_passes_through_unchanged(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);
    const char *text = "laptimer> status\r\n";
    uint8_t out[128];
    size_t w = bridge_filter_run(&f, (const uint8_t *)text, strlen(text), out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(strlen(text), w);
    TEST_ASSERT_EQUAL_MEMORY(text, out, w);
    TEST_ASSERT_EQUAL_UINT32(0, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
}

/* One whole frame in the middle of text is removed; the text before and after it survives, glued
 * together exactly as if the frame had never been on the wire. */
void test_one_frame_in_middle_removed(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);

    uint8_t in[64];
    size_t n = 0;
    memcpy(in + n, "before ", 7); n += 7;
    n += build_frame(in + n, 42, 0x01, 8, 0xAB);
    memcpy(in + n, " after\n", 7); n += 7;

    uint8_t out[64];
    size_t w = bridge_filter_run(&f, in, n, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT(14, w);   /* "before " (7) + " after\n" (7) */
    TEST_ASSERT_EQUAL_MEMORY("before  after\n", out, w);
    TEST_ASSERT_EQUAL_UINT32(1, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
}

/* A frame split across two calls (header in one, payload tail in the next, or any other split) is
 * still fully removed -- state persists in *f between calls. */
void test_frame_split_across_two_calls_removed(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);

    uint8_t frame[64];
    size_t flen = build_frame(frame, 7, 0x01, 20, 0xCD);

    uint8_t in1[64];
    size_t n1 = 0;
    memcpy(in1 + n1, "AA", 2); n1 += 2;
    memcpy(in1 + n1, frame, 9); n1 += 9;      /* tag + 4 header bytes + 4 payload bytes */

    uint8_t in2[64];
    size_t n2 = 0;
    memcpy(in2 + n2, frame + 9, flen - 9); n2 += (flen - 9);   /* remaining 16 payload bytes */
    memcpy(in2 + n2, "BB", 2); n2 += 2;

    uint8_t out1[64], out2[64];
    size_t w1 = bridge_filter_run(&f, in1, n1, out1, sizeof out1);
    size_t w2 = bridge_filter_run(&f, in2, n2, out2, sizeof out2);

    TEST_ASSERT_EQUAL_UINT(2, w1);
    TEST_ASSERT_EQUAL_MEMORY("AA", out1, w1);
    TEST_ASSERT_EQUAL_UINT(2, w2);
    TEST_ASSERT_EQUAL_MEMORY("BB", out2, w2);
    TEST_ASSERT_EQUAL_UINT32(1, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
}

/* A header whose len exceeds LT_REC_MAX cannot be a real stream frame: it is counted as bad, the
 * tag+header bytes are dropped (never forwarded), and text classification resumes immediately
 * after -- the bytes that would have been "payload" under the bogus len are ordinary text. */
void test_oversize_len_counted_bad_then_text_resumes(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);

    uint8_t in[16];
    size_t n = 0;
    in[n++] = (uint8_t)LT_STREAM_TAG;
    in[n++] = 0x00;                        /* seq_lo */
    in[n++] = 0x00;                        /* seq_hi */
    in[n++] = 0x00;                        /* flags */
    in[n++] = (uint8_t)(LT_REC_MAX + 1);   /* len: one over the bound -> bad */
    memcpy(in + n, "hello\n", 6); n += 6;   /* ordinary text, not a real payload */

    uint8_t out[16];
    size_t w = bridge_filter_run(&f, in, n, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT(6, w);
    TEST_ASSERT_EQUAL_MEMORY("hello\n", out, w);
    TEST_ASSERT_EQUAL_UINT32(0, f.frames);
    TEST_ASSERT_EQUAL_UINT32(1, f.bad);
}

/* A len exactly at LT_REC_MAX is a valid (if maximal) frame -- not bad. */
void test_len_exactly_at_max_is_valid(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);

    uint8_t in[8 + LT_REC_MAX];
    size_t n = 0;
    memcpy(in + n, "xy", 2); n += 2;
    n += build_frame(in + n, 1, 0, (uint8_t)LT_REC_MAX, 0x11);

    uint8_t out[8 + LT_REC_MAX];
    size_t w = bridge_filter_run(&f, in, n, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT(2, w);
    TEST_ASSERT_EQUAL_MEMORY("xy", out, w);
    TEST_ASSERT_EQUAL_UINT32(1, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
}

/* cap smaller than the text: bounded write, the remainder is dropped, and the function never
 * writes past cap. */
void test_cap_smaller_than_text_is_bounded(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);
    const char *text = "0123456789";
    uint8_t out[4] = { 0 };
    size_t w = bridge_filter_run(&f, (const uint8_t *)text, strlen(text), out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(4, w);
    TEST_ASSERT_EQUAL_MEMORY("0123", out, w);
}

/* A zero-length frame (len == 0) is a degenerate but valid frame: no payload to skip, counted
 * immediately. */
void test_zero_length_frame_counted_immediately(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);
    uint8_t in[8];
    size_t n = 0;
    memcpy(in + n, "a", 1); n += 1;
    n += build_frame(in + n, 3, 0, 0, 0);
    memcpy(in + n, "b", 1); n += 1;

    uint8_t out[8];
    size_t w = bridge_filter_run(&f, in, n, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(2, w);
    TEST_ASSERT_EQUAL_MEMORY("ab", out, w);
    TEST_ASSERT_EQUAL_UINT32(1, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_passes_through_unchanged);
    RUN_TEST(test_one_frame_in_middle_removed);
    RUN_TEST(test_frame_split_across_two_calls_removed);
    RUN_TEST(test_oversize_len_counted_bad_then_text_resumes);
    RUN_TEST(test_len_exactly_at_max_is_valid);
    RUN_TEST(test_cap_smaller_than_text_is_bounded);
    RUN_TEST(test_zero_length_frame_counted_immediately);
    return UNITY_END();
}
