/* test_dl_leading_noise.c -- Issue #65 Step 1: localize "first /api/sessions after connect returns
 * bad response; reload works" to the parser vs. the live RX-task/download-loop UART race.
 *
 * The streaming download parser (lh_dl_*, host/linkhost_proto.c) is what /api/sessions rides via
 * linkhost_download_cmd(LT_CMD_LIST, ...). Its header scan (dl_feed_hdr) accumulates bytes into a
 * fixed line buffer until a literal '\n', classifying each completed line as ---BEGIN, a non-framed
 * "ERR 0x<code>: <msg>" (parse_err_line, which requires the literal "ERR 0x" prefix AT THE START of
 * the line), or noise to discard. This test feeds lh_dl_feed a realistic FIRST-call byte stream --
 * exactly what a live UART1 link can hand the parser right after connect, before any request of ours
 * has gone out: a raw §18.1 0xFF stream frame (autonomous telemetry, arriving whether or not we asked
 * for it) carrying a fused_sample_t-shaped payload that deliberately embeds a raw 0x0A (a "line"
 * break inside binary data) and the characters 'E','R','R',' ','0','x',':' at scattered, non-adjacent
 * offsets (present in the bytes, but never forming the contiguous "ERR 0x" substring the classifier
 * requires); then a bare "laptimer> " prompt fragment with no trailing newline; then a stray "\r\r\n"
 * CRLF artifact (the console's \n -> \r\n translation applied to an already-CR-terminated line); then
 * a correctly framed `list` JSON response with a real crc32.
 *
 * If this test FAILS, the parser cannot cleanly resync past leading noise -- a real bug in
 * dl_feed_hdr/parse_err_line to fix, with this test as the regression guard. If it PASSES (expected:
 * parse_err_line's prefix match cannot be satisfied by non-adjacent bytes deep in a differently-
 * started line), the parser is robust and the bug is elsewhere -- the two-task UART race in
 * linkhost.c's linkhost_download_cmd/linkhost_flash (rx_task vs. the download loop both reading
 * UART1), which a host test cannot reproduce. Kept here regardless as a permanent guard.
 */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define STREAM_MAX 512u

/* ---- chunk sink: accumulates every decoded block ---- */
typedef struct {
    uint8_t buf[STREAM_MAX];
    size_t  len;
    int     calls;
} sink_t;

static int sink_cb(void *ctx, const uint8_t *data, size_t n)
{
    sink_t *s = (sink_t *)ctx;
    s->calls++;
    for (size_t i = 0; i < n; i++) {
        if (s->len < sizeof s->buf) s->buf[s->len++] = data[i];
    }
    return 0;
}

/* Builds "---BEGIN <name> <wire_len>---\r\n<wire>\r\n---END <crc>---\r\n" (mirrors
 * test_linkhost_download.c's build_frame). CRC is over the decoded body (== wire for text). */
static size_t build_frame(uint8_t *buf, const char *name, const char *wire, size_t wire_len,
                          uint32_t crc)
{
    int hn = snprintf((char *)buf, 64, "---BEGIN %s %u---\r\n", name, (unsigned)wire_len);
    memcpy(buf + (size_t)hn, wire, wire_len);
    int tn = snprintf((char *)buf + (size_t)hn + wire_len, 40, "\r\n---END %08x---\r\n",
                      (unsigned)crc);
    return (size_t)hn + wire_len + (size_t)tn;
}

/* A raw §18.1 0xFF stream frame: 5-byte header (tag, seq_lo, seq_hi, flags, len=41) + a
 * SES_T_FUSED (0x04) type byte + 40 data bytes shaped like a fused_sample_t. Returns the byte
 * count written (always 46). */
static size_t build_noise_stream_frame(uint8_t *out)
{
    size_t o = 0;
    out[o++] = 0xFF;   /* LT_STREAM_TAG */
    out[o++] = 0x34;   /* seq_lo */
    out[o++] = 0x12;   /* seq_hi */
    out[o++] = 0x01;   /* flags */
    out[o++] = 41;     /* len: 1 type byte + 40 data bytes */
    out[o++] = 0x04;   /* SES_T_FUSED */
    uint8_t data[40];
    for (size_t i = 0; i < 40; i++) data[i] = (uint8_t)((i * 37u + 11u) ^ (i >> 2));
    data[3]  = 0x0A;   /* a raw newline byte inside the binary payload */
    data[7]  = 'E';
    data[13] = 'R';
    data[19] = 'R';
    data[22] = ' ';
    data[27] = '0';
    data[33] = 'x';
    data[36] = ':';
    memcpy(out + o, data, sizeof data);
    o += sizeof data;
    return o;
}

/* Assembles the full first-call byte stream: [raw 0xFF noise frame][prompt fragment, no '\n']
 * [stray "\r\r\n"][correct ---BEGIN sessions / ---END JSON frame]. Fills the json_out and
 * json_len out-params with the JSON body (what the sink must receive byte-for-byte) and
 * noise_len_out with the byte offset right after the raw stream frame (the split point for the
 * frame-boundary test). Returns the total stream length. */
static size_t build_stream(uint8_t *out, const char **json_out, size_t *json_len,
                           size_t *noise_len_out)
{
    static const char JSON[] =
        "{\"sessions\":[{\"id\":\"S00001\",\"name\":\"Practice 1\"},"
        "{\"id\":\"S00002\",\"name\":\"Practice 2\"}]}";
    size_t off = build_noise_stream_frame(out);
    *noise_len_out = off;
    memcpy(out + off, "laptimer> ", 10); off += 10;      /* prompt fragment, no newline */
    memcpy(out + off, "\r\r\n", 3);      off += 3;        /* stray CRLF artifact */
    uint32_t crc = linkhost_crc32((const uint8_t *)JSON, sizeof(JSON) - 1);
    off += build_frame(out + off, "sessions", JSON, sizeof(JSON) - 1, crc);
    *json_out = JSON;
    *json_len = sizeof(JSON) - 1;
    return off;
}

static void assert_decoded_ok(const lh_dl_ctx_t *c, const sink_t *s, const char *json,
                              size_t json_len)
{
    TEST_ASSERT_EQUAL_INT(0, lh_dl_result(c));
    TEST_ASSERT_EQUAL_STRING("sessions", c->name);
    TEST_ASSERT_EQUAL_UINT(json_len, c->decoded_len);
    TEST_ASSERT_EQUAL_UINT(json_len, s->len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(s->buf, json, json_len));
}

/* ---- fed in one call ---- */
static void test_leading_noise_whole(void)
{
    static uint8_t stream[STREAM_MAX];
    const char *json; size_t json_len, noise_len;
    size_t slen = build_stream(stream, &json, &json_len, &noise_len);

    sink_t s = {0};
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    lh_dl_feed(&c, stream, slen);

    assert_decoded_ok(&c, &s, json, json_len);
}

/* ---- fed one byte at a time ---- */
static void test_leading_noise_byte_by_byte(void)
{
    static uint8_t stream[STREAM_MAX];
    const char *json; size_t json_len, noise_len;
    size_t slen = build_stream(stream, &json, &json_len, &noise_len);

    sink_t s = {0};
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    for (size_t i = 0; i < slen && c.state < LH_DL_DONE; i++) {   /* bounded by slen */
        lh_dl_feed(&c, stream + i, 1);
    }

    assert_decoded_ok(&c, &s, json, json_len);
}

/* ---- split so a 0xFF-frame boundary lands mid-`line` accumulation: the first feed call ends
 * exactly at the end of the raw stream frame, mid-way through the still-open "line" that goes on
 * to carry the prompt fragment + the stray CRLF (it hasn't seen its terminating '\n' yet). ---- */
static void test_leading_noise_split_at_frame_boundary(void)
{
    static uint8_t stream[STREAM_MAX];
    const char *json; size_t json_len, noise_len;
    size_t slen = build_stream(stream, &json, &json_len, &noise_len);

    sink_t s = {0};
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    lh_dl_feed(&c, stream, noise_len);
    lh_dl_feed(&c, stream + noise_len, slen - noise_len);

    assert_decoded_ok(&c, &s, json, json_len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_leading_noise_whole);
    RUN_TEST(test_leading_noise_byte_by_byte);
    RUN_TEST(test_leading_noise_split_at_frame_boundary);
    return UNITY_END();
}
