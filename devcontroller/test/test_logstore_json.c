/* devcontroller/test/test_logstore_json.c -- host tests for logstore_rec_to_json (issue #67):
 * parses ONE on-flash record (logstore_rec_hdr_t + `len` payload bytes, exactly as
 * logstore_append wrote it and webapi's jsonl download reads it back) into the NDJSON object
 * linkhost_stream_to_json renders. Builds synthetic records by memcpy'ing a logstore_rec_hdr_t
 * followed by payload bytes placed at their app/lt_proto.h LT_*_OFF_* contract offsets --
 * mirrors test_stream_json.c's make_fused/make_event, but at the on-flash-record level
 * logstore_rec_to_json actually parses (raw bytes, not a pre-built lt_stream_rec_t).
 */
#include "unity.h"
#include "logstore_rec.h"
#include "linkhost_proto.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Appends one synthetic record (hdr + `len` payload bytes) to buf at byte offset `off`, returning
 * the new offset just past it. `payload` may be NULL when len == 0. */
static size_t append_rec(uint8_t *buf, size_t off, uint16_t seq, uint8_t flags, uint8_t type,
                          const uint8_t *payload, uint8_t len)
{
    logstore_rec_hdr_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.ts_us = 1234567890ULL;
    hdr.seq   = seq;
    hdr.flags = flags;
    hdr.type  = type;
    hdr.len   = len;
    memcpy(buf + off, &hdr, sizeof hdr);
    off += sizeof hdr;
    if (len > 0) memcpy(buf + off, payload, len);
    return off + len;
}

/* Builds a SES_T_FUSED payload (LT_FUSED_REC_LEN bytes), fields at their LT_FUSED_OFF_* offsets --
 * mirrors test_stream_json.c's make_fused, minus the lt_stream_rec_t wrapper. */
static void make_fused_payload(uint8_t *p, int64_t gps_us, float g_lon, float g_lat, float g_comb,
                                float lean, float yaw, uint8_t flags)
{
    memset(p, 0, LT_FUSED_REC_LEN);
    memcpy(p + LT_FUSED_OFF_GPS_US, &gps_us, 8);
    memcpy(p + LT_FUSED_OFF_G_LON, &g_lon, 4);
    memcpy(p + LT_FUSED_OFF_G_LAT, &g_lat, 4);
    memcpy(p + LT_FUSED_OFF_G_COMB, &g_comb, 4);
    memcpy(p + LT_FUSED_OFF_LEAN, &lean, 4);
    memcpy(p + LT_FUSED_OFF_YAW, &yaw, 4);
    p[LT_FUSED_OFF_FLAGS] = flags;
}

/* Builds a SES_T_EVENT payload (LT_EVENT_REC_LEN bytes) -- mirrors test_stream_json.c's
 * make_event, minus the lt_stream_rec_t wrapper. */
static void make_event_payload(uint8_t *p, uint8_t code, uint8_t flags, uint16_t arg16,
                                int64_t gps_us, uint32_t arg32, uint32_t arg32b)
{
    memset(p, 0, LT_EVENT_REC_LEN);
    p[LT_EVENT_OFF_TYPE]  = code;
    p[LT_EVENT_OFF_FLAGS] = flags;
    memcpy(p + LT_EVENT_OFF_ARG16, &arg16, 2);
    memcpy(p + LT_EVENT_OFF_GPS_US, &gps_us, 8);
    memcpy(p + LT_EVENT_OFF_ARG32, &arg32, 4);
    memcpy(p + LT_EVENT_OFF_ARG32B, &arg32b, 4);
}

void test_fused_record_transcodes(void)
{
    uint8_t payload[LT_FUSED_REC_LEN];
    make_fused_payload(payload, 1700000000000000LL, 0.123f, -0.045f, 0.567f, 12.34f, 4.5f, 0x03);

    uint8_t buf[128];
    size_t total = append_rec(buf, 0, 7, 0, LT_SES_T_FUSED, payload, LT_FUSED_REC_LEN);

    char out[320];
    size_t consumed = 0;
    int n = logstore_rec_to_json(buf, total, out, sizeof out, &consumed);
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, out);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(logstore_rec_hdr_t) + LT_FUSED_REC_LEN),
                              (uint32_t)consumed);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"t\":\"fused\""), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"seq\":7"), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"lean_cdeg\":1234"), out);
}

void test_event_record_transcodes(void)
{
    uint8_t payload[LT_EVENT_REC_LEN];
    make_event_payload(payload, 5, 0x02, 3, 1700000000000000LL, 62345u, 1200u);

    uint8_t buf[128];
    size_t total = append_rec(buf, 0, 42, 0, LT_SES_T_EVENT, payload, LT_EVENT_REC_LEN);

    char out[320];
    size_t consumed = 0;
    int n = logstore_rec_to_json(buf, total, out, sizeof out, &consumed);
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, out);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(logstore_rec_hdr_t) + LT_EVENT_REC_LEN),
                              (uint32_t)consumed);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"t\":\"event\""), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"code\":5"), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"arg32b\":1200"), out);
}

void test_unknown_type_skipped_and_consumed(void)
{
    uint8_t payload[10];
    memset(payload, 0xAB, sizeof payload);
    uint8_t buf[64];
    size_t total = append_rec(buf, 0, 1, 0, 0x02 /* not SES_T_FUSED/SES_T_EVENT */, payload,
                               sizeof payload);

    char out[64];
    size_t consumed = 999;
    int n = logstore_rec_to_json(buf, total, out, sizeof out, &consumed);
    TEST_ASSERT_EQUAL_INT(LOGSTORE_JSON_SKIP, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(logstore_rec_hdr_t) + sizeof payload),
                              (uint32_t)consumed);
}

void test_truncated_tail_needs_more(void)
{
    uint8_t payload[LT_EVENT_REC_LEN];
    make_event_payload(payload, 1, 0, 0, 0, 0, 0);
    uint8_t buf[64];
    size_t total = append_rec(buf, 0, 1, 0, LT_SES_T_EVENT, payload, LT_EVENT_REC_LEN);

    /* full header buffered, but the payload is chopped one byte short */
    size_t consumed = 999;
    char out[64];
    int n = logstore_rec_to_json(buf, total - 1, out, sizeof out, &consumed);
    TEST_ASSERT_EQUAL_INT(LOGSTORE_JSON_NEED_MORE, n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)consumed);

    /* not even a full logstore_rec_hdr_t buffered yet */
    consumed = 999;
    n = logstore_rec_to_json(buf, sizeof(logstore_rec_hdr_t) - 1, out, sizeof out, &consumed);
    TEST_ASSERT_EQUAL_INT(LOGSTORE_JSON_NEED_MORE, n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)consumed);
}

void test_oversize_len_is_error(void)
{
    logstore_rec_hdr_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.len = (uint8_t)(LT_REC_MAX + 1);   /* > LT_REC_MAX -- malformed */
    uint8_t buf[sizeof(logstore_rec_hdr_t)];
    memcpy(buf, &hdr, sizeof hdr);

    size_t consumed = 999;
    char out[16];
    int n = logstore_rec_to_json(buf, sizeof buf, out, sizeof out, &consumed);
    TEST_ASSERT_EQUAL_INT(LOGSTORE_JSON_ERR, n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)consumed);
}

void test_two_records_back_to_back(void)
{
    uint8_t f_payload[LT_FUSED_REC_LEN];
    make_fused_payload(f_payload, 100, 0.1f, 0.2f, 0.3f, 1.0f, 2.0f, 0x01);
    uint8_t e_payload[LT_EVENT_REC_LEN];
    make_event_payload(e_payload, 9, 0, 0, 200, 0, 0);

    uint8_t buf[256];
    size_t off = append_rec(buf, 0, 1, 0, LT_SES_T_FUSED, f_payload, LT_FUSED_REC_LEN);
    off = append_rec(buf, off, 2, 0, LT_SES_T_EVENT, e_payload, LT_EVENT_REC_LEN);

    char out[320];
    size_t consumed = 0;
    int n = logstore_rec_to_json(buf, off, out, sizeof out, &consumed);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    size_t first_size = sizeof(logstore_rec_hdr_t) + LT_FUSED_REC_LEN;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)first_size, (uint32_t)consumed);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"t\":\"fused\""));

    n = logstore_rec_to_json(buf + consumed, off - consumed, out, sizeof out, &consumed);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(logstore_rec_hdr_t) + LT_EVENT_REC_LEN),
                              (uint32_t)consumed);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"t\":\"event\""));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fused_record_transcodes);
    RUN_TEST(test_event_record_transcodes);
    RUN_TEST(test_unknown_type_skipped_and_consumed);
    RUN_TEST(test_truncated_tail_needs_more);
    RUN_TEST(test_oversize_len_is_error);
    RUN_TEST(test_two_records_back_to_back);
    return UNITY_END();
}
