/* test_multipart.c -- Plan 5.5 Task 6: the PURE streaming multipart/form-data parser that feeds
 * POST /api/flash's `ota_stage` writer. The interesting cases are all about the delimiter scanner:
 * a firmware image is binary, so its bytes routinely contain "\r", "--", "\r\n-" and even
 * "\r\n--" runs that look like the start of a boundary and must be emitted verbatim once they
 * turn out not to be one -- at ANY feed split offset (the uploader hands us arbitrary TCP chunks).
 */
#include "unity.h"
#include "multipart.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define BOUND "abc123"
#define CT    "multipart/form-data; boundary=" BOUND

/* A part body full of near-misses: a lone CR, "\r\n-", a 9-of-10-byte delimiter prefix
 * ("\r\n--abc12" then 'X'), a bare "--" mid-run, a CRLF-led "--", a NUL and a 0xFF (binary
 * safety). */
static const uint8_t BODY[] = {
    'h','e','l','l','o',
    '\r','\n','-','n','o','t',
    '\r','\n','-','-','a','b','c','1','2','X',
    '\r',' ','m','o','r','e',
    '-','-','n','o','t','-','-','e','i','t','h','e','r','-','-',
    '\r','\n','\r','\n','-','-', 0x00, 0xFF,
    'e','n','d'
};

/* ---- a recording sink ---- */
#define SINK_CAP 1024
typedef struct {
    uint8_t buf[SINK_CAP];
    size_t  len;
    int     calls;
    int     fail_on_call;   /* >0: return nonzero on that call number (1-based) */
    bool    overflow;
} sink_t;

static int sink_cb(void *ctx, const uint8_t *data, size_t n)
{
    sink_t *s = (sink_t *)ctx;
    s->calls++;
    if (s->fail_on_call > 0 && s->calls >= s->fail_on_call) return 1;
    if (s->len + n > SINK_CAP) { s->overflow = true; return 1; }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    return 0;
}

/* ---- message assembly ---- */
static size_t app(uint8_t *msg, size_t at, const char *s)
{
    size_t n = strlen(s);
    memcpy(msg + at, s, n);
    return at + n;
}

static size_t app_raw(uint8_t *msg, size_t at, const uint8_t *b, size_t n)
{
    memcpy(msg + at, b, n);
    return at + n;
}

/* --B CRLF <disposition> CRLF CRLF <BODY> CRLF --B-- CRLF */
static size_t one_part_msg(uint8_t *msg)
{
    size_t at = 0;
    at = app(msg, at, "--" BOUND "\r\n");
    at = app(msg, at, "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n");
    at = app(msg, at, "Content-Type: application/octet-stream\r\n\r\n");
    at = app_raw(msg, at, BODY, sizeof BODY);
    at = app(msg, at, "\r\n--" BOUND "--\r\n");
    return at;
}

/* A "meta" part first, the wanted "firmware" part second. */
static size_t two_part_msg(uint8_t *msg)
{
    size_t at = 0;
    at = app(msg, at, "--" BOUND "\r\n");
    at = app(msg, at, "Content-Disposition: form-data; name=\"meta\"\r\n\r\n");
    at = app(msg, at, "this text must never reach the sink");
    at = app(msg, at, "\r\n--" BOUND "\r\n");
    at = app(msg, at, "Content-Disposition: form-data; name=\"firmware\"\r\n\r\n");
    at = app_raw(msg, at, BODY, sizeof BODY);
    at = app(msg, at, "\r\n--" BOUND "--\r\n");
    return at;
}

/* ---- tests ---- */

static void test_single_feed_exact_body(void)
{
    uint8_t msg[512];
    size_t len = one_part_msg(msg);

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    TEST_ASSERT_EQUAL_INT(MP_DONE, mp_feed(&c, msg, len, sink_cb, &s));
    TEST_ASSERT_TRUE(mp_found(&c));
    TEST_ASSERT_FALSE(s.overflow);
    TEST_ASSERT_EQUAL_size_t(sizeof BODY, s.len);
    TEST_ASSERT_EQUAL_MEMORY(BODY, s.buf, sizeof BODY);
}

static void test_byte_by_byte_feed(void)
{
    uint8_t msg[512];
    size_t len = one_part_msg(msg);

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    int rc = MP_MORE;
    for (size_t i = 0; i < len; i++) rc = mp_feed(&c, msg + i, 1, sink_cb, &s);
    TEST_ASSERT_EQUAL_INT(MP_DONE, rc);
    TEST_ASSERT_TRUE(mp_found(&c));
    TEST_ASSERT_EQUAL_size_t(sizeof BODY, s.len);
    TEST_ASSERT_EQUAL_MEMORY(BODY, s.buf, sizeof BODY);
}

static void test_every_split_offset(void)
{
    uint8_t msg[512];
    size_t len = one_part_msg(msg);

    for (size_t split = 0; split <= len; split++) {       /* bounded by the message length */
        mp_ctx_t c;
        TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
        sink_t s = { 0 };
        int rc = mp_feed(&c, msg, split, sink_cb, &s);
        TEST_ASSERT_TRUE(rc == MP_MORE || rc == MP_DONE);
        rc = mp_feed(&c, msg + split, len - split, sink_cb, &s);
        TEST_ASSERT_EQUAL_INT(MP_DONE, rc);
        TEST_ASSERT_TRUE(mp_found(&c));
        TEST_ASSERT_EQUAL_size_t(sizeof BODY, s.len);
        TEST_ASSERT_EQUAL_MEMORY(BODY, s.buf, sizeof BODY);
    }
}

static void test_skips_earlier_part(void)
{
    uint8_t msg[512];
    size_t len = two_part_msg(msg);

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    TEST_ASSERT_EQUAL_INT(MP_DONE, mp_feed(&c, msg, len, sink_cb, &s));
    TEST_ASSERT_TRUE(mp_found(&c));
    TEST_ASSERT_EQUAL_size_t(sizeof BODY, s.len);
    TEST_ASSERT_EQUAL_MEMORY(BODY, s.buf, sizeof BODY);
}

static void test_wanted_field_absent(void)
{
    uint8_t msg[512];
    size_t at = 0;
    at = app(msg, at, "--" BOUND "\r\n");
    at = app(msg, at, "Content-Disposition: form-data; name=\"meta\"\r\n\r\n");
    at = app(msg, at, "nothing to see");
    at = app(msg, at, "\r\n--" BOUND "--\r\n");

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    TEST_ASSERT_EQUAL_INT(MP_DONE, mp_feed(&c, msg, at, sink_cb, &s));
    TEST_ASSERT_FALSE(mp_found(&c));
    TEST_ASSERT_EQUAL_INT(0, s.calls);
}

static void test_init_content_type_variants(void)
{
    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(-1, mp_init(&c, "text/plain", "firmware"));
    TEST_ASSERT_EQUAL_INT(-1, mp_init(&c, "multipart/form-data", "firmware"));
    TEST_ASSERT_EQUAL_INT(-1, mp_init(&c, "multipart/form-data; charset=utf-8", "firmware"));
    TEST_ASSERT_EQUAL_INT(0,  mp_init(&c, "Multipart/Form-Data; BOUNDARY=" BOUND, "firmware"));
    TEST_ASSERT_EQUAL_INT(0,  mp_init(&c, "multipart/form-data; boundary=\"" BOUND "\"", "firmware"));

    /* a quoted boundary really is usable end to end */
    uint8_t msg[512];
    size_t len = one_part_msg(msg);
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, "multipart/form-data; boundary=\"" BOUND "\"", "firmware"));
    sink_t s = { 0 };
    TEST_ASSERT_EQUAL_INT(MP_DONE, mp_feed(&c, msg, len, sink_cb, &s));
    TEST_ASSERT_EQUAL_MEMORY(BODY, s.buf, sizeof BODY);

    /* a 71-character boundary exceeds MP_BOUNDARY_MAX */
    char toolong[48 + MP_BOUNDARY_MAX];
    int n = snprintf(toolong, sizeof toolong, "multipart/form-data; boundary=");
    for (int i = 0; i < MP_BOUNDARY_MAX + 1; i++) toolong[n + i] = 'x';
    toolong[n + MP_BOUNDARY_MAX + 1] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, mp_init(&c, toolong, "firmware"));

    /* an over-long wanted field name is rejected too */
    char field[MP_FIELD_MAX + 4];
    memset(field, 'f', sizeof field - 1);
    field[sizeof field - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, mp_init(&c, CT, field));
}

static void test_garbage_after_delimiter(void)
{
    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    const char *msg = "--" BOUND "XY\r\n";
    TEST_ASSERT_EQUAL_INT(MP_E_MALFORMED,
                          mp_feed(&c, (const uint8_t *)msg, strlen(msg), sink_cb, &s));
}

static void test_sink_abort(void)
{
    uint8_t msg[512];
    size_t len = one_part_msg(msg);

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    s.fail_on_call = 1;
    TEST_ASSERT_EQUAL_INT(MP_E_SINK, mp_feed(&c, msg, len, sink_cb, &s));
}

static void test_input_after_done_is_ignored(void)
{
    uint8_t msg[512];
    size_t len = one_part_msg(msg);

    mp_ctx_t c;
    TEST_ASSERT_EQUAL_INT(0, mp_init(&c, CT, "firmware"));
    sink_t s = { 0 };
    TEST_ASSERT_EQUAL_INT(MP_DONE, mp_feed(&c, msg, len, sink_cb, &s));
    int calls = s.calls;
    size_t got = s.len;

    const char *epilogue = "\r\nepilogue junk\r\n--" BOUND "\r\n";
    TEST_ASSERT_EQUAL_INT(MP_DONE,
                          mp_feed(&c, (const uint8_t *)epilogue, strlen(epilogue), sink_cb, &s));
    TEST_ASSERT_EQUAL_INT(calls, s.calls);
    TEST_ASSERT_EQUAL_size_t(got, s.len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_feed_exact_body);
    RUN_TEST(test_byte_by_byte_feed);
    RUN_TEST(test_every_split_offset);
    RUN_TEST(test_skips_earlier_part);
    RUN_TEST(test_wanted_field_absent);
    RUN_TEST(test_init_content_type_variants);
    RUN_TEST(test_garbage_after_delimiter);
    RUN_TEST(test_sink_abort);
    RUN_TEST(test_input_after_done_is_ignored);
    return UNITY_END();
}
