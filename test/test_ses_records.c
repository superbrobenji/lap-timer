#include "unity.h"
#include "core/ses.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static gps_fix_t mk_fix(int64_t gps_us, int32_t lat, int32_t lon, int32_t alt_mm, int32_t v_mms, int32_t head_e5, uint8_t sats, uint8_t valid)
{
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = gps_us; f.lat_e7 = lat; f.lon_e7 = lon; f.alt_mm = alt_mm; f.gspeed_mms = v_mms; f.head_e5 = head_e5;
    f.hacc_mm = 2500; f.sacc_mms = 300; f.pdop_e2 = 150; f.fix_type = 3; f.sats = sats; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = valid;
    return f;
}

/* decoder side: collect fixes through the frame reader */
typedef struct { ses_fix_state_t st; gps_fix_t out[64]; int n; uint8_t types[64]; } dec_t;
static void dec_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    dec_t *d = ctx; d->types[d->n] = type;
    if (ses_decode_fix(&d->st, type, payload, len, &d->out[d->n]) == 1) d->n++;
}

static void test_fix_key_then_deltas_then_key_after_5s(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    uint8_t stream[2048]; size_t n = 0;
    int64_t t0 = 1789380900LL * 1000000LL;
    /* 5 Hz for 5.2 s = 27 fixes; moving 6 m north per fix at ~30 m/s */
    for (int i = 0; i < 27; i++) {
        gps_fix_t f = mk_fix(t0 + i * 200000LL, -338567000 + i * 540, 185170000, 45000 + i * 10, 30000, 1000, 8, 1);
        int w = ses_encode_fix(&enc, &f, stream + n, sizeof stream - n);
        TEST_ASSERT_GREATER_THAN(0, w); n += (size_t)w;
    }
    dec_t d; memset(&d, 0, sizeof d); ses_fix_state_init(&d.st);
    ses_reader_t r; ses_reader_init(&r);
    ses_reader_feed(&r, stream, n, dec_cb, &d);
    TEST_ASSERT_EQUAL_INT(27, d.n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, d.types[0]);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, d.types[1]);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, d.types[25]);       /* t = 5.0 s → keyframe */
    for (int i = 0; i < 27; i++) {
        TEST_ASSERT_EQUAL_INT64(t0 + i * 200000LL, d.out[i].gps_us);
        TEST_ASSERT_EQUAL_INT32(-338567000 + i * 540, d.out[i].lat_e7);
        TEST_ASSERT_EQUAL_INT32(185170000, d.out[i].lon_e7);
        TEST_ASSERT_INT32_WITHIN(100, 45000 + i * 10, d.out[i].alt_mm);     /* dm resolution in deltas */
        TEST_ASSERT_EQUAL_INT32(30000, d.out[i].gspeed_mms);
        TEST_ASSERT_EQUAL_INT32(1000, d.out[i].head_e5);
        TEST_ASSERT_EQUAL_UINT8(8, d.out[i].sats);
        TEST_ASSERT_EQUAL_UINT8(1, d.out[i].valid);
        TEST_ASSERT_EQUAL_UINT8(3, d.out[i].fix_type);
    }
}

static void test_fix_key_forced_on_overflow_and_after_invalid(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    uint8_t buf[64];
    gps_fix_t a = mk_fix(1000000, 0, 0, 0, 1000, 0, 6, 1);
    ses_encode_fix(&enc, &a, buf, sizeof buf);
    gps_fix_t b = mk_fix(1200000, 40000, 0, 0, 1000, 0, 6, 1);          /* dlat 40000 > int16 → KEY */
    ses_encode_fix(&enc, &b, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    gps_fix_t c = mk_fix(1400000, 40100, 0, 0, 1000, 0, 6, 0);          /* invalid fix, still logged as delta */
    ses_encode_fix(&enc, &c, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, buf[1]);
    gps_fix_t dfix = mk_fix(1600000, 40200, 0, 0, 1000, 0, 6, 1);       /* first valid after invalid → KEY */
    ses_encode_fix(&enc, &dfix, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
}

static void test_fused_dt_relative_to_last_fix_then_previous_fused(void)
{
    ses_fused_state_t enc; ses_fused_state_init(&enc);
    ses_fused_state_t dec; ses_fused_state_init(&dec);
    uint8_t buf[64];
    ses_fused_state_on_fix(&enc, 5000000); ses_fused_state_on_fix(&dec, 5000000);
    fused_sample_t s1; memset(&s1, 0, sizeof s1);
    s1.gps_us = 5040000; s1.g_lat = 0.123f; s1.g_lon = -0.5f; s1.lean_deg = 12.34f; s1.yaw_dps = -5.5f; s1.flags = FUS_LEAN_VALID;
    int w = ses_encode_fused(&enc, &s1, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(11 + SES_FRAME_OVERHEAD, w);
    fused_sample_t o1; TEST_ASSERT_EQUAL_INT(1, ses_decode_fused(&dec, buf + 3, 11, &o1));
    TEST_ASSERT_EQUAL_INT64(5040000, o1.gps_us);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, 0.123f, o1.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, 12.34f, o1.lean_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, -5.5f, o1.yaw_dps);
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, o1.flags);
    fused_sample_t s2 = s1; s2.gps_us = 5140000;
    ses_encode_fused(&enc, &s2, buf, sizeof buf);
    fused_sample_t o2; ses_decode_fused(&dec, buf + 3, 11, &o2);
    TEST_ASSERT_EQUAL_INT64(5140000, o2.gps_us);
}

static void test_lap_round_trip(void)
{
    lap_result_t lap; memset(&lap, 0, sizeof lap);
    lap.lap_no = 7; lap.start_gps_us = 123456789012LL; lap.time_ms = 112340; lap.flags = LAP_F_VALID; lap.n_sectors = 3;
    lap.sector_ms[0] = 32100; lap.sector_ms[1] = 41000; lap.sector_ms[2] = 39240;
    lap.stats.max_speed_cms = 6000; lap.stats.min_speed_cms = 1800; lap.stats.max_lean_l_cdeg = -5200; lap.stats.max_lean_r_cdeg = 5500;
    lap.stats.max_glat_e3 = 1320; lap.stats.max_gacc_e3 = 610; lap.stats.max_gbrake_e3 = -1050;
    uint8_t buf[128];
    int w = ses_encode_lap(&lap, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(30 + 4 * 3 + SES_FRAME_OVERHEAD, w);
    lap_result_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_lap(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &out));
    TEST_ASSERT_EQUAL_MEMORY(&lap, &out, sizeof lap);
}

static void test_drag_run_round_trip(void)
{
    drag_result_t run; memset(&run, 0, sizeof run);
    run.run_no = 2; run.t0_gps_us = 99; run.flags = DRAG_F_QUARTER; run.trap_cms = 8472; run.n_gates = 2;
    run.gates[0] = (drag_gate_res_t){ 2, 5910, 2778, 9800, 1 };
    run.gates[1] = (drag_gate_res_t){ 10, 14200, 8472, 40234, 1 };
    uint8_t buf[256];
    int w = ses_encode_drag_run(&run, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(14 + 12 * 2 + SES_FRAME_OVERHEAD, w);
    drag_result_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_run(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &out));
    TEST_ASSERT_EQUAL_MEMORY(&run, &out, sizeof run);
}

static void test_hdr_round_trip_and_size(void)
{
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10); h.mode = 0; h.variant = 0; h.venue_id = 6; h.layout_id = 1;
    strcpy(h.fw, "v0.1.0"); strcpy(h.hwid, "moto_neo6m_epaper"); h.log_profile = 0; h.fused_hz = 10; h.gps_hz = 5;
    h.start_gps_us = 1789380900000000LL; h.r_e4[0] = 10000; h.r_e4[4] = 10000; h.r_e4[8] = 10000; h.gbias[1] = -12; h.calib_flags = 3;
    uint8_t buf[128];
    int w = ses_encode_hdr(&h, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(94 + SES_FRAME_OVERHEAD, w);
    ses_hdr_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(buf + 3, 94, &out));
    TEST_ASSERT_EQUAL_MEMORY(&h, &out, sizeof h);
}

static void test_small_records_sizes(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_INT(19 + SES_FRAME_OVERHEAD, ses_encode_sector(3, 1, 123, 32100, -210, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(21 + SES_FRAME_OVERHEAD, ses_encode_drag_gate(1, 2, 123, 5910, 2778, 9800, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(22 + SES_FRAME_OVERHEAD, ses_encode_event(1, 2, 0x0101, 7, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(17 + SES_FRAME_OVERHEAD, ses_encode_time_map(1, 2, 1, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(36 + SES_FRAME_OVERHEAD, ses_encode_venue(6, 1, "Killarney", buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(11 + SES_FRAME_OVERHEAD, ses_encode_power(1, 2, 3900, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(9 + SES_FRAME_OVERHEAD, ses_encode_end(1, 0, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(9 + SES_FRAME_OVERHEAD, ses_encode_mark(1, 0, buf, sizeof buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fix_key_then_deltas_then_key_after_5s);
    RUN_TEST(test_fix_key_forced_on_overflow_and_after_invalid);
    RUN_TEST(test_fused_dt_relative_to_last_fix_then_previous_fused);
    RUN_TEST(test_lap_round_trip);
    RUN_TEST(test_drag_run_round_trip);
    RUN_TEST(test_hdr_round_trip_and_size);
    RUN_TEST(test_small_records_sizes);
    return UNITY_END();
}
