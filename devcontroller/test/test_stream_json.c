/* test_stream_json.c -- linkhost_stream_to_json (Plan 5.5 Task 5): decodes one demuxed
 * SES_T_FUSED/SES_T_EVENT stream record (raw fused_sample_t / event_t bytes) to the compact JSON
 * object the live-monitor SSE handler sends. Builds records by memcpy'ing known values at the
 * documented offsets (must match components/core/include/core/types.h fused_sample_t and
 * core/event.h event_t) and asserts the scaled-int fields land in the JSON via strstr.
 */
#include "unity.h"
#include "linkhost_proto.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Builds a SES_T_FUSED record, placing each field at its app/lt_proto.h LT_FUSED_OFF_* contract
 * offset (mono_us@LT_FUSED_OFF_MONO_US is left zero -- unused by the decoder). */
static void make_fused(lt_stream_rec_t *r, uint16_t seq, int64_t gps_us,
                        float g_lon, float g_lat, float g_comb, float lean, float yaw, uint8_t flags)
{
    memset(r, 0, sizeof *r);
    r->seq  = seq;
    r->type = LT_SES_T_FUSED;
    r->len  = LT_FUSED_REC_LEN;
    memcpy(r->data + LT_FUSED_OFF_GPS_US, &gps_us, 8);
    memcpy(r->data + LT_FUSED_OFF_G_LON, &g_lon, 4);
    memcpy(r->data + LT_FUSED_OFF_G_LAT, &g_lat, 4);
    memcpy(r->data + LT_FUSED_OFF_G_COMB, &g_comb, 4);
    memcpy(r->data + LT_FUSED_OFF_LEAN, &lean, 4);
    memcpy(r->data + LT_FUSED_OFF_YAW, &yaw, 4);
    r->data[LT_FUSED_OFF_FLAGS] = flags;
}

/* Builds a SES_T_EVENT record, placing each field at its app/lt_proto.h LT_EVENT_OFF_* contract
 * offset (mono_us@LT_EVENT_OFF_MONO_US is left zero -- unused by the decoder). */
static void make_event(lt_stream_rec_t *r, uint16_t seq, uint8_t code, uint8_t flags,
                        uint16_t arg16, int64_t gps_us, uint32_t arg32, uint32_t arg32b)
{
    memset(r, 0, sizeof *r);
    r->seq  = seq;
    r->type = LT_SES_T_EVENT;
    r->len  = LT_EVENT_REC_LEN;
    r->data[LT_EVENT_OFF_TYPE] = code;
    r->data[LT_EVENT_OFF_FLAGS] = flags;
    memcpy(r->data + LT_EVENT_OFF_ARG16, &arg16, 2);
    memcpy(r->data + LT_EVENT_OFF_GPS_US, &gps_us, 8);
    memcpy(r->data + LT_EVENT_OFF_ARG32, &arg32, 4);
    memcpy(r->data + LT_EVENT_OFF_ARG32B, &arg32b, 4);
}

void test_fused_record_scaled_ints(void)
{
    lt_stream_rec_t r;
    make_fused(&r, 7, 1700000000000000LL, 0.123f, -0.045f, 0.567f, 12.34f, 4.5f, 0x03);

    char buf[320];
    int n = linkhost_stream_to_json(&r, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, "expected a positive byte count");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"t\":\"fused\""), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"seq\":7"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"gps_us\":1700000000000000"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"lean_cdeg\":1234"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"g_mg\":567"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"g_lon_mg\":123"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"g_lat_mg\":-45"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"yaw_cdps\":450"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"flags\":3"), buf);
}

void test_event_record_fields(void)
{
    lt_stream_rec_t r;
    make_event(&r, 42, 5 /* EV_LAP_COMPLETE */, 0x02, 3, 1700000000000000LL, 62345u, 1200u);

    char buf[320];
    int n = linkhost_stream_to_json(&r, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, "expected a positive byte count");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"t\":\"event\""), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"seq\":42"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"code\":5"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"flags\":2"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"arg16\":3"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"arg32\":62345"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"arg32b\":1200"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"gps_us\":1700000000000000"), buf);
}

void test_unknown_type_rejected(void)
{
    lt_stream_rec_t r;
    memset(&r, 0, sizeof r);
    r.seq  = 1;
    r.type = 0x02;      /* not SES_T_FUSED (LT_SES_T_FUSED) or SES_T_EVENT (LT_SES_T_EVENT) */
    r.len  = 10;

    char buf[320];
    TEST_ASSERT_EQUAL_INT(-1, linkhost_stream_to_json(&r, buf, sizeof buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fused_record_scaled_ints);
    RUN_TEST(test_event_record_fields);
    RUN_TEST(test_unknown_type_rejected);
    return UNITY_END();
}
