/* test_linkhost_demux.c -- Plan 5.5 Task 3 Steps 5-7: the length-aware, noise-tolerant demux.
 * Feeds one mixed byte stream (command echo + `laptimer> ` prompt + an ESP_LOG line + a
 * length-prefixed 0xFF stream frame + a ---BEGIN/---END response + an ERR line + an unknown-type
 * frame + a trailing known frame) and asserts the correct split and re-sync: the right stream
 * records pop in order, the unknown-type frame is consumed via its len and dropped, one response
 * parses, and the noise is skipped. Also proves state persistence across split feeds and re-sync
 * onto a 0xFF frame out of a noise line. */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) { linkhost_reset(); }
void tearDown(void) {}

/* Appends bytes to s[*L]. */
static void app(uint8_t *s, size_t *L, const void *p, size_t n)
{
    memcpy(s + *L, p, n);
    *L += n;
}

/* Builds the mixed stream into `s`, returns total length, and reports the response's json body. */
static size_t build_mixed(uint8_t *s, const char **json_out, uint32_t *crc_out)
{
    size_t L = 0;
    app(s, &L, "status\r", 7);                       /* linenoise command echo */
    app(s, &L, "laptimer> ", 10);                    /* prompt (no trailing newline) */
    app(s, &L, "I (1234) app: hello world\n", 26);   /* ESP_LOG INFO line */

    /* stream frame: seq=7 flags=1 len=5 payload={type 0x02, 0xAA,0xBB,0xFF,0x10} (0xFF in payload!) */
    const uint8_t f1[] = { 0xFF, 0x07, 0x00, 0x01, 0x05, 0x02, 0xAA, 0xBB, 0xFF, 0x10 };
    app(s, &L, f1, sizeof f1);

    /* framed response */
    static const char *json = "[{\"id\":1},{\"id\":2}]";
    uint32_t crc = linkhost_crc32((const uint8_t *)json, strlen(json));
    char fr[128];
    int fn = snprintf(fr, sizeof fr, "---BEGIN sessions %u---\r\n%s\r\n---END %08x---\r\n",
                      (unsigned)strlen(json), json, (unsigned)crc);
    app(s, &L, fr, (size_t)fn);
    *json_out = json;
    *crc_out = crc;

    app(s, &L, "ERR 0x0703: bad thing\n", 22);       /* non-framed error line */

    /* unknown-type frame: type 0x7E, len 4 -> consumed by len, dropped */
    const uint8_t fu[] = { 0xFF, 0x09, 0x00, 0x01, 0x04, 0x7E, 0x11, 0x22, 0x33 };
    app(s, &L, fu, sizeof fu);

    /* trailing known frame proves the demux stayed synced: seq=10 type=0x05 len=2 data={0x99} */
    const uint8_t f2[] = { 0xFF, 0x0A, 0x00, 0x01, 0x02, 0x05, 0x99 };
    app(s, &L, f2, sizeof f2);
    return L;
}

/* Asserts the demux produced exactly the two known records (seq 7 then 10) + the one response. */
static void assert_expected(const char *json)
{
    lt_stream_rec_t r;

    TEST_ASSERT_EQUAL_INT(0, linkhost_stream_pop(&r));
    TEST_ASSERT_EQUAL_UINT16(7, r.seq);
    TEST_ASSERT_EQUAL_UINT8(0x01, r.flags);
    TEST_ASSERT_EQUAL_UINT8(0x02, r.type);
    TEST_ASSERT_EQUAL_UINT8(4, r.len);
    const uint8_t want1[4] = { 0xAA, 0xBB, 0xFF, 0x10 };
    TEST_ASSERT_EQUAL_INT(0, memcmp(r.data, want1, 4));

    TEST_ASSERT_EQUAL_INT(0, linkhost_stream_pop(&r));   /* unknown-type frame was dropped */
    TEST_ASSERT_EQUAL_UINT16(10, r.seq);
    TEST_ASSERT_EQUAL_UINT8(0x05, r.type);
    TEST_ASSERT_EQUAL_UINT8(1, r.len);
    TEST_ASSERT_EQUAL_UINT8(0x99, r.data[0]);

    TEST_ASSERT_EQUAL_INT(-1, linkhost_stream_pop(&r));  /* ring now empty */

    linkhost_frame_t f;
    int st = -999;
    TEST_ASSERT_EQUAL_INT(1, linkhost_pop_response(&f, &st));
    TEST_ASSERT_EQUAL_INT(0, st);
    TEST_ASSERT_EQUAL_STRING("sessions", f.name);
    TEST_ASSERT_EQUAL_UINT(strlen(json), f.body_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(f.body, json, strlen(json)));

    TEST_ASSERT_EQUAL_INT(0, linkhost_pop_response(&f, &st));   /* no second response */
}

static void test_mixed_stream_single_feed(void)
{
    uint8_t s[512];
    const char *json;
    uint32_t crc;
    size_t L = build_mixed(s, &json, &crc);
    TEST_ASSERT_EQUAL_UINT(L, linkhost_feed(s, L));
    assert_expected(json);
}

/* Feeding the identical stream in two arbitrary chunks must yield identical results (state persists
 * across calls, including splits inside a frame). */
static void test_mixed_stream_split_feed(void)
{
    uint8_t s[512];
    const char *json;
    uint32_t crc;
    size_t L = build_mixed(s, &json, &crc);
    size_t cut = L / 2;
    linkhost_feed(s, cut);
    linkhost_feed(s + cut, L - cut);
    assert_expected(json);
}

/* A noise line with NO newline followed by a 0xFF frame: the demux must break out of line-skipping
 * on the 0xFF (ASCII noise never contains 0xFF) and decode the frame. */
static void test_resync_on_stream_tag_from_noise(void)
{
    uint8_t s[64];
    size_t L = 0;
    app(s, &L, "garbage without newline", 23);
    const uint8_t f2[] = { 0xFF, 0x2A, 0x00, 0x01, 0x02, 0x05, 0x77 };
    app(s, &L, f2, sizeof f2);
    linkhost_feed(s, L);

    lt_stream_rec_t r;
    TEST_ASSERT_EQUAL_INT(0, linkhost_stream_pop(&r));
    TEST_ASSERT_EQUAL_UINT16(0x2A, r.seq);
    TEST_ASSERT_EQUAL_UINT8(0x05, r.type);
    TEST_ASSERT_EQUAL_UINT8(0x77, r.data[0]);
    TEST_ASSERT_EQUAL_INT(-1, linkhost_stream_pop(&r));
}

/* A non-framed "ERR 0x<code>: <msg>" line -- what export_serial prints when a command fails -- is
 * completed as a remote-error response (M3), not dropped as noise, so linkhost_cmd fails fast with
 * a real error/message instead of waiting out the link timeout and reporting a misleading 503. */
static void test_err_line_becomes_remote_response(void)
{
    const char *line = "ERR 0x0703: config json error\r\n";
    linkhost_feed((const uint8_t *)line, strlen(line));

    linkhost_frame_t f;
    int st = -999;
    TEST_ASSERT_EQUAL_INT(1, linkhost_pop_response(&f, &st));
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_REMOTE, st);
    TEST_ASSERT_EQUAL_UINT16(0x0703, f.err_code);
    TEST_ASSERT_EQUAL_STRING("config json error", f.err_msg);
    TEST_ASSERT_EQUAL_INT(0, linkhost_pop_response(&f, &st));   /* consumed */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mixed_stream_single_feed);
    RUN_TEST(test_mixed_stream_split_feed);
    RUN_TEST(test_resync_on_stream_tag_from_noise);
    RUN_TEST(test_err_line_becomes_remote_response);
    return UNITY_END();
}
