#include "unity.h"
#include "core/ses.h"
#include "core/core.h"
#include "assert_support.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_crc16_ccitt_false_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, ses_crc16((const uint8_t *)"123456789", 9));
}

static void test_frame_layout(void)
{
    uint8_t out[16];
    uint8_t payload[3] = { 1, 2, 3 };
    int n = ses_frame_encode(0x09, payload, 3, out, sizeof out);
    TEST_ASSERT_EQUAL_INT(3 + SES_FRAME_OVERHEAD, n);
    TEST_ASSERT_EQUAL_HEX8(SES_SYNC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x09, out[1]);
    TEST_ASSERT_EQUAL_HEX8(3, out[2]);
    uint16_t crc = ses_crc16(out + 1, 2 + 3);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, out[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), out[7]);
    TEST_ASSERT_EQUAL_INT(-1, ses_frame_encode(0x09, payload, 3, out, 7));   /* too small */
}

typedef struct { int calls; uint8_t types[8]; uint8_t lens[8]; uint8_t last_payload[SES_MAX_PAYLOAD]; } cap_t;
static void cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    cap_t *c = ctx; c->types[c->calls] = type; c->lens[c->calls] = len; memcpy(c->last_payload, payload, len); c->calls++;
}

static void test_reader_decodes_two_frames_fed_byte_by_byte(void)
{
    uint8_t stream[64]; int n = 0;
    uint8_t p1[2] = { 0xA5, 0xA5 };                    /* sync byte inside payload must not confuse the reader */
    uint8_t p2[1] = { 7 };
    n += ses_frame_encode(0x02, p1, 2, stream + n, sizeof stream - (size_t)n);
    n += ses_frame_encode(0x04, p2, 1, stream + n, sizeof stream - (size_t)n);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t t, l; const uint8_t *p;
    for (int i = 0; i < n; i++) {
        ses_reader_push(&r, stream + i, 1);
        while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    }
    TEST_ASSERT_EQUAL_INT(2, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x02, c.types[0]); TEST_ASSERT_EQUAL_UINT8(2, c.lens[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, c.types[1]); TEST_ASSERT_EQUAL_UINT8(1, c.lens[1]);
    TEST_ASSERT_EQUAL_UINT8(7, c.last_payload[0]);
    TEST_ASSERT_EQUAL_UINT32(2, r.frames_ok); TEST_ASSERT_EQUAL_UINT32(0, r.frames_bad);
}

static void test_reader_resyncs_after_corruption(void)
{
    uint8_t stream[64]; int n = 0;
    uint8_t p1[4] = { 1, 2, 3, 4 }; uint8_t p2[1] = { 9 };
    n += ses_frame_encode(0x03, p1, 4, stream + n, sizeof stream - (size_t)n);
    int second = n;
    n += ses_frame_encode(0x05, p2, 1, stream + n, sizeof stream - (size_t)n);
    stream[4] ^= 0xFF;                                  /* corrupt a payload byte of frame 1 */
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t t, l; const uint8_t *p;
    ses_reader_push(&r, stream, (size_t)n);
    while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x05, c.types[0]);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
    (void)second;
}

static void test_reader_resync_finds_frame_starting_inside_bad_frame(void)
{
    /* garbage that looks like a frame header with a large len, immediately followed by a real frame */
    uint8_t stream[64]; int n = 0;
    stream[n++] = SES_SYNC; stream[n++] = 0x02; stream[n++] = 40;    /* claims 40 bytes; only a few follow */
    uint8_t p[1] = { 42 };
    n += ses_frame_encode(0x0B, p, 1, stream + n, sizeof stream - (size_t)n);
    /* pad so the bogus frame "completes" with wrong CRC */
    while (n < 3 + 40 + 2) stream[n++] = 0;
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t ft, fl; const uint8_t *fp;
    ses_reader_push(&r, stream, (size_t)n);
    while (ses_reader_next(&r, &ft, &fp, &fl) == 1) cb(ft, fp, fl, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
}

static void test_reader_rejects_oversize_len_without_stalling(void)
{
    uint8_t stream[8] = { SES_SYNC, 0x02, 255, 0, 0, 0, 0, 0 };
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t t, l; const uint8_t *p;
    ses_reader_push(&r, stream, sizeof stream);
    while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static void test_flush_recovers_frame_hidden_behind_spurious_sync_at_eof(void)
{
    uint8_t stream[16]; int n = 0;
    stream[n++] = SES_SYNC; stream[n++] = 0x99; stream[n++] = 200;    /* spurious header claiming 200 bytes */
    uint8_t p[1] = { 42 };
    n += ses_frame_encode(0x0B, p, 1, stream + n, sizeof stream - (size_t)n);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t ft, fl; const uint8_t *fp;
    ses_reader_push(&r, stream, (size_t)n);
    while (ses_reader_next(&r, &ft, &fp, &fl) == 1) cb(ft, fp, fl, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);                                  /* stuck waiting for 200 bytes */
    ses_reader_finish(&r);
    while (ses_reader_next(&r, &ft, &fp, &fl) == 1) cb(ft, fp, fl, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0, r.state);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static void test_flush_on_truncated_frame_counts_bad_and_is_idempotent(void)
{
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t frame[16]; int n = ses_frame_encode(0x03, payload, 4, frame, sizeof frame);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    uint8_t t, l; const uint8_t *p;
    ses_reader_push(&r, frame, (size_t)(n - 2));                       /* CRC bytes missing */
    while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    ses_reader_finish(&r);
    while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
    TEST_ASSERT_EQUAL_UINT8(0, r.state);
    ses_reader_finish(&r);                                             /* no-op when idle */
    while (ses_reader_next(&r, &t, &p, &l) == 1) cb(t, p, l, &c);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static void test_on_bad_guard_resets_the_reader_before_returning(void)
{
    /* Force the internal bounds guard in on_bad(): collected + pending must exceed sizeof(r.replay),
     * a combination normal traffic cannot reach (idx caps at 251; a prior on_bad's own output is
     * itself bounded). ses_reader_t's fields are public precisely so a whitebox test can build this
     * otherwise-unreachable state directly, the same way the surrounding "resync" tests read them. */
    ses_reader_t r; ses_reader_init(&r);
    r.state = 1;
    r.buf[0] = 0x02;
    r.buf[1] = SES_MAX_PAYLOAD;                                 /* len byte: claims the largest payload */
    r.idx = (uint16_t)(2 + SES_MAX_PAYLOAD);                    /* type+len+payload already collected: 2 CRC bytes short */
    r.need = (uint16_t)(2 + SES_MAX_PAYLOAD + 2);
    uint16_t crc = ses_crc16(r.buf, (size_t)2 + SES_MAX_PAYLOAD);
    r.replay[0] = (uint8_t)~crc; r.replay[1] = (uint8_t)(~(crc >> 8));   /* guaranteed CRC mismatch: bad frame */
    r.replay_len = sizeof r.replay;                             /* pending alone already exceeds sizeof(replay) - idx: forces the guard */
    r.replay_pos = 0;

    lt_test_assert_reset();
    cap_t c = { 0 };
    uint8_t ot, ol; const uint8_t *op;
    ses_reader_push(&r, NULL, 0);
    while (ses_reader_next(&r, &ot, &op, &ol) == 1) cb(ot, op, ol, &c);

    TEST_ASSERT_EQUAL_UINT(1, lt_test_assert_count());
    TEST_ASSERT_EQUAL_HEX16(0x0A02, lt_test_assert_last_code());
    TEST_ASSERT_EQUAL_UINT8(0, r.state);              /* the guard did not leave the reader mid-frame */
    TEST_ASSERT_EQUAL_UINT16(0, r.idx);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);

    /* the reader must still recognise a normal frame after the guard trips */
    uint8_t stream[16]; uint8_t p[1] = { 42 };
    int n = ses_frame_encode(0x0B, p, 1, stream, sizeof stream);
    ses_reader_push(&r, stream, (size_t)n);
    while (ses_reader_next(&r, &ot, &op, &ol) == 1) cb(ot, op, ol, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
}

/* deterministic LCG, same pattern as the other suites */
static uint32_t lcg = 22695477u;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

#define FUZZ_MAX_FRAMES 6
#define FUZZ_MAX_PAYLOAD 24
typedef struct { uint8_t type, len, first, last; } frame_sig_t;
typedef struct { int n; frame_sig_t sig[32]; } fuzz_cap_t;

static void fuzz_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    fuzz_cap_t *c = ctx;
    if (c->n < (int)(sizeof c->sig / sizeof c->sig[0])) {
        c->sig[c->n].type = type; c->sig[c->n].len = len;
        c->sig[c->n].first = len ? payload[0] : 0;
        c->sig[c->n].last = len ? payload[len - 1] : 0;
    }
    c->n++;
}

static void test_fuzz_frames_buried_in_garbage_are_all_recovered(void)
{
    for (int it = 0; it < 1000; it++) {
        uint8_t stream[512]; size_t sn = 0;
        frame_sig_t want[FUZZ_MAX_FRAMES]; int nwant = 0;
        int nframes = 1 + (int)(rnd() % FUZZ_MAX_FRAMES);
        for (int f = 0; f < nframes; f++) {
            int gap = (int)(rnd() % 13u);                       /* garbage before each frame */
            for (int g = 0; g < gap; g++) stream[sn++] = (uint8_t)(rnd() % 256u);
            uint8_t payload[FUZZ_MAX_PAYLOAD];
            uint8_t len = (uint8_t)(rnd() % (FUZZ_MAX_PAYLOAD + 1u));
            for (uint8_t i = 0; i < len; i++) payload[i] = (uint8_t)(rnd() % 256u);
            uint8_t type = (uint8_t)(1u + rnd() % 0x7Fu);
            int w = ses_frame_encode(type, payload, len, stream + sn, sizeof stream - sn);
            TEST_ASSERT_GREATER_THAN(0, w);
            sn += (size_t)w;
            want[nwant].type = type; want[nwant].len = len;
            want[nwant].first = len ? payload[0] : 0;
            want[nwant].last = len ? payload[len - 1] : 0;
            nwant++;
        }
        int tail = (int)(rnd() % 13u);                          /* trailing garbage */
        for (int g = 0; g < tail; g++) stream[sn++] = (uint8_t)(rnd() % 256u);

        ses_reader_t r; ses_reader_init(&r);
        fuzz_cap_t c; memset(&c, 0, sizeof c);
        uint8_t ft, fl; const uint8_t *fp;
        size_t pos = 0;
        while (pos < sn) {                                      /* random chunk sizes */
            size_t chunk = 1u + rnd() % 17u;
            if (pos + chunk > sn) chunk = sn - pos;
            ses_reader_push(&r, stream + pos, chunk);
            while (ses_reader_next(&r, &ft, &fp, &fl) == 1) fuzz_cb(ft, fp, fl, &c);
            pos += chunk;
        }
        ses_reader_finish(&r);
        while (ses_reader_next(&r, &ft, &fp, &fl) == 1) fuzz_cb(ft, fp, fl, &c);
        TEST_ASSERT_EQUAL_INT(nwant, c.n);
        for (int i = 0; i < nwant; i++) {
            TEST_ASSERT_EQUAL_HEX8(want[i].type, c.sig[i].type);
            TEST_ASSERT_EQUAL_UINT8(want[i].len, c.sig[i].len);
            TEST_ASSERT_EQUAL_HEX8(want[i].first, c.sig[i].first);
            TEST_ASSERT_EQUAL_HEX8(want[i].last, c.sig[i].last);
        }
        TEST_ASSERT_EQUAL_UINT32((uint32_t)nwant, r.frames_ok);
        TEST_ASSERT_EQUAL_UINT8(0, r.state);                    /* flush always leaves the reader idle */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_false_check_value);
    RUN_TEST(test_frame_layout);
    RUN_TEST(test_reader_decodes_two_frames_fed_byte_by_byte);
    RUN_TEST(test_reader_resyncs_after_corruption);
    RUN_TEST(test_reader_resync_finds_frame_starting_inside_bad_frame);
    RUN_TEST(test_reader_rejects_oversize_len_without_stalling);
    RUN_TEST(test_flush_recovers_frame_hidden_behind_spurious_sync_at_eof);
    RUN_TEST(test_flush_on_truncated_frame_counts_bad_and_is_idempotent);
    RUN_TEST(test_on_bad_guard_resets_the_reader_before_returning);
    RUN_TEST(test_fuzz_frames_buried_in_garbage_are_all_recovered);
    return UNITY_END();
}
