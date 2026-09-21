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

/* Builds a SES_T_FUSED record: mono_us@0 (unused by the decoder), gps_us@8, floats g_lon@16/
 * g_lat@20/g_comb@24/lean_deg@28/yaw_dps@32, flags@36. */
static void make_fused(lt_stream_rec_t *r, uint16_t seq, int64_t gps_us,
                        float g_lon, float g_lat, float g_comb, float lean, float yaw, uint8_t flags)
{
    memset(r, 0, sizeof *r);
    r->seq  = seq;
    r->type = 0x04;
    r->len  = 40;
    memcpy(r->data + 8, &gps_us, 8);
    memcpy(r->data + 16, &g_lon, 4);
    memcpy(r->data + 20, &g_lat, 4);
    memcpy(r->data + 24, &g_comb, 4);
    memcpy(r->data + 28, &lean, 4);
    memcpy(r->data + 32, &yaw, 4);
    r->data[36] = flags;
}

/* Builds a SES_T_EVENT record: type@0, flags@1, arg16@2, gps_us@8, mono_us@16 (unused), arg32@24,
 * arg32b@28. */
static void make_event(lt_stream_rec_t *r, uint16_t seq, uint8_t code, uint8_t flags,
                        uint16_t arg16, int64_t gps_us, uint32_t arg32, uint32_t arg32b)
{
    memset(r, 0, sizeof *r);
    r->seq  = seq;
    r->type = 0x09;
    r->len  = 32;
    r->data[0] = code;
    r->data[1] = flags;
    memcpy(r->data + 2, &arg16, 2);
    memcpy(r->data + 8, &gps_us, 8);
    memcpy(r->data + 24, &arg32, 4);
    memcpy(r->data + 28, &arg32b, 4);
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
    r.type = 0x02;      /* not SES_T_FUSED (0x04) or SES_T_EVENT (0x09) */
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
