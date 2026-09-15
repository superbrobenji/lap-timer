#include "unity.h"
#include "core/ses.h"
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
    for (int i = 0; i < n; i++) ses_reader_feed(&r, stream + i, 1, cb, &c);
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
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
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
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
}

static void test_reader_rejects_oversize_len_without_stalling(void)
{
    uint8_t stream[8] = { SES_SYNC, 0x02, 255, 0, 0, 0, 0, 0 };
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, stream, sizeof stream, cb, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
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
    return UNITY_END();
}
