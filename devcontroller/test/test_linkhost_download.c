/* test_linkhost_download.c -- Plan 5.5: the STREAMING ---BEGIN/---END download parser (lh_dl_*).
 *
 * The buffered linkhost_parse_frame caps a body at LINKHOST_ASM_MAX (1024 B); real session files
 * are KB..MB (a `.log` was 46 KB) and used to fail with E_PROTO/503. lh_dl_* never buffers the
 * whole body: it is fed raw bytes and hands decoded blocks to a chunk callback. These tests feed a
 * framed response LARGER than 1024 B (4 KB body) in odd-sized pieces that deliberately split
 * mid-Base64-quantum and mid-marker, and assert: the callback receives the EXACT decoded body, the
 * running CRC verifies, and a corrupted trailer yields LINKHOST_E_CRC -- for both a Base64 (binary)
 * body and a raw-text (json) body.
 */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define BIN_LEN 4096u          /* decoded binary body: > 4x the old 1024 B cap */
#define TXT_LEN 4000u          /* raw-text (json) body: also > 1024 B */
#define WIRE_MAX 8192u         /* base64(4096) == 5464 B + markers */

/* ---- chunk sink: accumulates every decoded block; can abort after a threshold ---- */
typedef struct {
    uint8_t buf[WIRE_MAX];
    size_t  len;
    int     abort_after;       /* >=0: return abort once len reaches it; <0: never abort */
    int     calls;
} sink_t;

static int sink_cb(void *ctx, const uint8_t *data, size_t n)
{
    sink_t *s = (sink_t *)ctx;
    s->calls++;
    for (size_t i = 0; i < n; i++) {
        if (s->len < sizeof s->buf) s->buf[s->len++] = data[i];
    }
    if (s->abort_after >= 0 && (int)s->len >= s->abort_after) return 1;   /* transport closed */
    return 0;
}

/* ---- standard Base64 encoder (test-only; the parser decodes) ---- */
static size_t b64_encode(const uint8_t *src, size_t n, char *dst)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
        dst[o++] = T[(v >> 18) & 63]; dst[o++] = T[(v >> 12) & 63];
        dst[o++] = T[(v >> 6) & 63];  dst[o++] = T[v & 63];
    }
    size_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)src[i] << 16;
        dst[o++] = T[(v >> 18) & 63]; dst[o++] = T[(v >> 12) & 63]; dst[o++] = '='; dst[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        dst[o++] = T[(v >> 18) & 63]; dst[o++] = T[(v >> 12) & 63];
        dst[o++] = T[(v >> 6) & 63];  dst[o++] = '=';
    }
    dst[o] = '\0';
    return o;
}

/* Builds "---BEGIN <name> <wire_len>---\r\n<wire>\r\n---END <crc>---\r\n". CRC is over the DECODED
 * body (== the wire body for text). Returns the total byte count. */
static size_t build_frame(char *buf, const char *name, const char *wire, size_t wire_len, uint32_t crc)
{
    int hn = snprintf(buf, 64, "---BEGIN %s %u---\r\n", name, (unsigned)wire_len);
    memcpy(buf + hn, wire, wire_len);
    int tn = snprintf(buf + hn + wire_len, 40, "\r\n---END %08x---\r\n", (unsigned)crc);
    return (size_t)hn + wire_len + (size_t)tn;
}

/* Feeds [buf,n) in a rotating set of odd sizes so quanta AND markers get split across feeds. */
static lh_dl_state_t feed_split(lh_dl_ctx_t *c, const uint8_t *buf, size_t n)
{
    static const size_t sizes[] = { 1, 2, 3, 5, 7, 11, 13, 17, 4, 6, 9 };
    size_t si = 0, off = 0;
    while (off < n) {
        size_t take = sizes[si % (sizeof sizes / sizeof sizes[0])];
        if (take > n - off) take = n - off;
        lh_dl_feed(c, buf + off, take);
        off += take;
        si++;
    }
    return c->state;
}

static void fill_pattern(uint8_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)((i * 31u + 7u) ^ (i >> 3));
}

/* ---- a 4 KB Base64 (binary) body, split-fed, decodes exactly and its CRC verifies ---- */
static void test_stream_binary_4k(void)
{
    static uint8_t decoded[BIN_LEN];
    static char    wire[WIRE_MAX];
    static char    frame[WIRE_MAX + 64];
    fill_pattern(decoded, BIN_LEN);
    size_t wlen = b64_encode(decoded, BIN_LEN, wire);
    uint32_t crc = linkhost_crc32(decoded, BIN_LEN);
    size_t flen = build_frame(frame, "S12345.log", wire, wlen, crc);
    TEST_ASSERT_GREATER_THAN_UINT(1024u, wlen);           /* proves the old cap would overflow */

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/true, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(0, lh_dl_result(&c));
    TEST_ASSERT_EQUAL_STRING("S12345.log", c.name);
    TEST_ASSERT_EQUAL_UINT((unsigned)wlen, c.body_size);
    TEST_ASSERT_EQUAL_UINT(BIN_LEN, c.decoded_len);
    TEST_ASSERT_EQUAL_UINT(BIN_LEN, s.len);
    TEST_ASSERT_GREATER_THAN_INT(1, s.calls);             /* many chunks, not one buffered blob */
    TEST_ASSERT_EQUAL_INT(0, memcmp(s.buf, decoded, BIN_LEN));
}

/* ---- a >1 KB raw-text (json) body, split-fed, arrives byte-identical and its CRC verifies ---- */
static void test_stream_text_json(void)
{
    static uint8_t body[TXT_LEN];
    static char    frame[WIRE_MAX + 64];
    for (size_t i = 0; i < TXT_LEN; i++) body[i] = (uint8_t)("0123456789abcdef{},:\"_ "[i % 23]);
    uint32_t crc = linkhost_crc32(body, TXT_LEN);
    size_t flen = build_frame(frame, "sessions", (const char *)body, TXT_LEN, crc);

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(0, lh_dl_result(&c));
    TEST_ASSERT_EQUAL_STRING("sessions", c.name);
    TEST_ASSERT_EQUAL_UINT(TXT_LEN, c.body_size);
    TEST_ASSERT_EQUAL_UINT(TXT_LEN, s.len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(s.buf, body, TXT_LEN));
}

/* ---- a corrupted ---END CRC value yields LINKHOST_E_CRC (body still streamed) ---- */
static void test_stream_bad_crc(void)
{
    static uint8_t decoded[BIN_LEN];
    static char    wire[WIRE_MAX];
    static char    frame[WIRE_MAX + 64];
    fill_pattern(decoded, BIN_LEN);
    size_t wlen = b64_encode(decoded, BIN_LEN, wire);
    uint32_t good = linkhost_crc32(decoded, BIN_LEN);
    size_t flen = build_frame(frame, "S99999.log", wire, wlen, good ^ 0x00000001u);  /* corrupt */

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/true, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(LINKHOST_E_CRC, lh_dl_result(&c));
    TEST_ASSERT_EQUAL_UINT(BIN_LEN, s.len);               /* the whole body still reached the sink */
}

/* text body with a corrupted trailer -> LINKHOST_E_CRC too */
static void test_stream_text_bad_crc(void)
{
    static uint8_t body[TXT_LEN];
    static char    frame[WIRE_MAX + 64];
    for (size_t i = 0; i < TXT_LEN; i++) body[i] = (uint8_t)('A' + (i % 26));
    uint32_t good = linkhost_crc32(body, TXT_LEN);
    size_t flen = build_frame(frame, "sessions", (const char *)body, TXT_LEN, good ^ 0xDEADBEEFu);

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(LINKHOST_E_CRC, lh_dl_result(&c));
}

/* ---- leading echo/prompt/log noise before the frame is tolerated ---- */
static void test_stream_leading_noise(void)
{
    static uint8_t decoded[BIN_LEN];
    static char    wire[WIRE_MAX];
    static char    frame[WIRE_MAX + 128];
    fill_pattern(decoded, BIN_LEN);
    size_t wlen = b64_encode(decoded, BIN_LEN, wire);
    uint32_t crc = linkhost_crc32(decoded, BIN_LEN);
    int off = snprintf(frame, sizeof frame, "open S12345 log\r\nlaptimer> I (7) app: heartbeat\n");
    size_t flen = (size_t)off + build_frame(frame + off, "S12345.log", wire, wlen, crc);

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/true, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(0, lh_dl_result(&c));
    TEST_ASSERT_EQUAL_UINT(BIN_LEN, s.len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(s.buf, decoded, BIN_LEN));
}

/* ---- a chunk callback that aborts drives the machine terminal (LH_DL_ERR), not complete ---- */
static void test_stream_callback_abort(void)
{
    static uint8_t decoded[BIN_LEN];
    static char    wire[WIRE_MAX];
    static char    frame[WIRE_MAX + 64];
    fill_pattern(decoded, BIN_LEN);
    size_t wlen = b64_encode(decoded, BIN_LEN, wire);
    uint32_t crc = linkhost_crc32(decoded, BIN_LEN);
    size_t flen = build_frame(frame, "S12345.log", wire, wlen, crc);

    sink_t s = { .abort_after = 1000 };                   /* abort partway through the body */
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/true, sink_cb, &s);
    lh_dl_state_t st = feed_split(&c, (const uint8_t *)frame, flen);

    TEST_ASSERT_EQUAL_INT(LH_DL_ERR, st);
    TEST_ASSERT_NOT_EQUAL(0, lh_dl_result(&c));           /* never reports a clean completion */
}

/* ---- a truncated frame (no ---END) stays non-terminal -> LINKHOST_E_TIMEOUT (incomplete) ---- */
static void test_stream_incomplete(void)
{
    static uint8_t body[TXT_LEN];
    static char    frame[WIRE_MAX + 64];
    for (size_t i = 0; i < TXT_LEN; i++) body[i] = (uint8_t)('x');
    uint32_t crc = linkhost_crc32(body, TXT_LEN);
    size_t flen = build_frame(frame, "sessions", (const char *)body, TXT_LEN, crc);

    sink_t s = { .abort_after = -1 };
    lh_dl_ctx_t c;
    lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    feed_split(&c, (const uint8_t *)frame, flen - 12);    /* drop the trailer */

    TEST_ASSERT_EQUAL_INT(LINKHOST_E_TIMEOUT, lh_dl_result(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stream_binary_4k);
    RUN_TEST(test_stream_text_json);
    RUN_TEST(test_stream_bad_crc);
    RUN_TEST(test_stream_text_bad_crc);
    RUN_TEST(test_stream_leading_noise);
    RUN_TEST(test_stream_callback_abort);
    RUN_TEST(test_stream_incomplete);
    return UNITY_END();
}
