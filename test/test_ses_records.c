#include "unity.h"
#include "core/ses.h"
#include <stdint.h>
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

static void test_negative_ground_speed_forces_a_keyframe(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    ses_fix_state_t dec; ses_fix_state_init(&dec);
    uint8_t buf[64]; gps_fix_t out;
    gps_fix_t a = mk_fix(1000000, -338567000, 185170000, 45000, 1000, 9000000, 9, 1);
    int n = ses_encode_fix(&enc, &a, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&dec, buf[1], buf + 3, buf[2], &out));

    /* a negative Doppler speed cannot be represented in the unsigned delta field */
    gps_fix_t b = a; b.gps_us += 200000; b.gspeed_mms = -1500;
    n = ses_encode_fix(&enc, &b, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&dec, buf[1], buf + 3, buf[2], &out));
    TEST_ASSERT_EQUAL_INT32(-1500, out.gspeed_mms);              /* decoded exactly */
    TEST_ASSERT_EQUAL_INT32(enc.prev.gspeed_mms, out.gspeed_mms);   /* encoder and decoder agree */
    TEST_ASSERT_EQUAL_INT64(b.gps_us, out.gps_us);
    TEST_ASSERT_EQUAL_INT32(b.lat_e7, out.lat_e7);
}

static void test_hdr_strings_using_every_wire_byte_round_trip_nul_terminated(void)
{
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10);
    memcpy(h.fw, "v0.3.1-abcdefghi", 16);          /* exactly 16 bytes, no NUL on the wire */
    memcpy(h.hwid, "moto_neo6m_epaper_int_bl", 24);
    h.venue_id = 6; h.layout_id = 1; h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = 1789640100000000LL;
    uint8_t buf[128];
    int w = ses_encode_hdr(&h, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(94 + SES_FRAME_OVERHEAD, w);            /* wire payload unchanged */
    ses_hdr_t out;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(buf + 3, 94, &out));
    TEST_ASSERT_EQUAL_STRING("v0.3.1-abcdefghi", out.fw);
    TEST_ASSERT_EQUAL_UINT(16, strlen(out.fw));
    TEST_ASSERT_EQUAL_STRING("moto_neo6m_epaper_int_bl", out.hwid);
    TEST_ASSERT_EQUAL_UINT(24, strlen(out.hwid));
    TEST_ASSERT_EQUAL_MEMORY(&h, &out, sizeof h);
}

static void test_small_record_round_trips(void)
{
    uint8_t buf[64];
    int w;

    w = ses_encode_sector(3, 1, 123456789LL, 32100, -210, buf, sizeof buf);
    ses_sector_t sec;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_sector(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &sec));
    TEST_ASSERT_EQUAL_UINT16(3, sec.lap_no); TEST_ASSERT_EQUAL_UINT8(1, sec.idx);
    TEST_ASSERT_EQUAL_INT64(123456789LL, sec.gps_us); TEST_ASSERT_EQUAL_UINT32(32100, sec.split_ms);
    TEST_ASSERT_EQUAL_INT32(-210, sec.delta_ms);

    w = ses_encode_drag_gate(1, 2, 987654321LL, 5910, 2778, 9800, buf, sizeof buf);
    ses_drag_gate_t dg;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_gate(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &dg));
    TEST_ASSERT_EQUAL_UINT16(1, dg.run_no); TEST_ASSERT_EQUAL_UINT8(2, dg.gate_id);
    TEST_ASSERT_EQUAL_INT64(987654321LL, dg.gps_us); TEST_ASSERT_EQUAL_UINT32(5910, dg.time_ms);
    TEST_ASSERT_EQUAL_UINT16(2778, dg.speed_cms); TEST_ASSERT_EQUAL_UINT32(9800, dg.dist_cm);

    w = ses_encode_event(-5, 1789380900000000LL, 0x0101, 0xDEADBEEF, buf, sizeof buf);
    ses_event_t ev;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_event(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &ev));
    TEST_ASSERT_EQUAL_INT64(-5, ev.mono_us); TEST_ASSERT_EQUAL_INT64(1789380900000000LL, ev.gps_us);
    TEST_ASSERT_EQUAL_HEX16(0x0101, ev.code); TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, ev.arg);

    w = ses_encode_time_map(4242, 1789380900000000LL, 2, buf, sizeof buf);
    ses_time_map_t tm;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_time_map(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &tm));
    TEST_ASSERT_EQUAL_INT64(4242, tm.mono_us); TEST_ASSERT_EQUAL_INT64(1789380900000000LL, tm.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, tm.quality);

    w = ses_encode_venue(6, 1, "Killarney", buf, sizeof buf);
    ses_venue_t vn;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &vn));
    TEST_ASSERT_EQUAL_UINT16(6, vn.venue_id); TEST_ASSERT_EQUAL_UINT16(1, vn.layout_id);
    TEST_ASSERT_EQUAL_STRING("Killarney", vn.name);
    /* a name filling all 32 wire bytes still decodes NUL-terminated */
    w = ses_encode_venue(9, 2, "0123456789012345678901234567890123", buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &vn));
    TEST_ASSERT_EQUAL_UINT(31, strlen(vn.name));    /* encoder keeps a NUL inside the 32-byte field */

    w = ses_encode_power(77, 3, 3900, buf, sizeof buf);
    ses_power_t pw;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_power(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &pw));
    TEST_ASSERT_EQUAL_INT64(77, pw.mono_us); TEST_ASSERT_EQUAL_UINT8(3, pw.state); TEST_ASSERT_EQUAL_UINT16(3900, pw.batt_mv);

    w = ses_encode_end(1789380900000000LL, 2, buf, sizeof buf);
    ses_end_t en;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_end(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &en));
    TEST_ASSERT_EQUAL_INT64(1789380900000000LL, en.gps_us); TEST_ASSERT_EQUAL_UINT8(2, en.reason);

    w = ses_encode_mark(1789380900000001LL, 1, buf, sizeof buf);
    ses_mark_t mk;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_mark(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &mk));
    TEST_ASSERT_EQUAL_INT64(1789380900000001LL, mk.gps_us); TEST_ASSERT_EQUAL_UINT8(1, mk.kind);
}

static void test_calib_round_trip(void)
{
    ses_calib_t c; memset(&c, 0, sizeof c);
    for (int i = 0; i < 9; i++) c.r_e4[i] = (int16_t)(i * 1000 - 4000);
    c.gbias[0] = -12; c.gbias[1] = 340; c.gbias[2] = 0;
    c.calib_flags = 0x03;
    uint8_t buf[64];
    int w = ses_encode_calib(&c, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(25 + SES_FRAME_OVERHEAD, w);
    TEST_ASSERT_EQUAL_HEX8(SES_T_CALIB, buf[1]);
    ses_calib_t out; memset(&out, 0xAA, sizeof out);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_calib(buf + 3, 25, &out));
    for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(c.r_e4[i], out.r_e4[i]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT16(c.gbias[i], out.gbias[i]);
    TEST_ASSERT_EQUAL_HEX8(c.calib_flags, out.calib_flags);
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(buf + 3, 24, &out));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(buf + 3, 26, &out));
}

static void test_decoders_reject_malformed_payloads(void)
{
    uint8_t p[256]; memset(p, 0, sizeof p);
    gps_fix_t f; lap_result_t lap; drag_result_t run; ses_hdr_t hdr; fused_sample_t fs;
    ses_fix_state_t fst; ses_fix_state_init(&fst);
    ses_fused_state_t ust; ses_fused_state_init(&ust);

    /* FIX_KEY / FIX_DELTA: exact lengths only */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 38, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 40, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 15, &f));   /* DELTA before any KEY */
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 39, &f));      /* now a reference exists */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 14, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 16, &f));
    TEST_ASSERT_EQUAL_INT(0, ses_decode_fix(&fst, SES_T_LAP, p, 39, &f));          /* not a fix record */

    /* FUSED: exact length and a reference */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 11, &fs));                 /* no reference yet */
    ses_fused_state_on_fix(&ust, 1000000);
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 10, &fs));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 12, &fs));
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fused(&ust, p, 11, &fs));

    /* LAP: short header, declared sector count, declared-vs-actual length */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, 29, &lap));
    p[15] = LAP_MAX_SECTORS + 2;                                                   /* n_sectors field */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, (uint8_t)(30 + 4 * (LAP_MAX_SECTORS + 2)), &lap));
    p[15] = 3;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, 30, &lap));                        /* length does not match n_sectors */
    TEST_ASSERT_EQUAL_INT(1, ses_decode_lap(p, 42, &lap));
    p[15] = 0;

    /* DRAG_RUN: short header, declared gate count, declared-vs-actual length */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, 13, &run));
    p[13] = DRAG_MAX_GATES + 1;                                                    /* n_gates field */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, (uint8_t)(14 + 12 * 1), &run));
    p[13] = 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, 14, &run));
    TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_run(p, 38, &run));
    p[13] = 0;

    /* SESSION_HDR: exact length and version 1 */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 93, &hdr));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 95, &hdr));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 94, &hdr));                        /* version byte is 0 */
    p[0] = 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 94, &hdr));
    p[0] = 1;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(p, 94, &hdr));

    /* encoders refuse counts they cannot frame */
    uint8_t out[512];
    memset(&lap, 0, sizeof lap); lap.n_sectors = LAP_MAX_SECTORS + 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_encode_lap(&lap, out, sizeof out));
    lap.n_sectors = LAP_MAX_SECTORS + 1;
    TEST_ASSERT_GREATER_THAN(0, ses_encode_lap(&lap, out, sizeof out));
    memset(&run, 0, sizeof run); run.n_gates = DRAG_MAX_GATES + 1;
    TEST_ASSERT_EQUAL_INT(-1, ses_encode_drag_run(&run, out, sizeof out));
    run.n_gates = DRAG_MAX_GATES;
    TEST_ASSERT_GREATER_THAN(0, ses_encode_drag_run(&run, out, sizeof out));

    /* the small-record decoders all validate their length exactly */
    ses_sector_t sec; ses_drag_gate_t dg; ses_event_t ev; ses_time_map_t tm;
    ses_venue_t vn; ses_power_t pw; ses_end_t en; ses_mark_t mk; ses_calib_t cal;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_sector(p, 18, &sec));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_gate(p, 20, &dg));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_event(p, 21, &ev));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_time_map(p, 18, &tm));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_venue(p, 35, &vn));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_power(p, 12, &pw));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_end(p, 8, &en));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_mark(p, 10, &mk));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(p, 24, &cal));
}

/* deterministic LCG, same pattern as the other suites */
static uint32_t lcg = 1664525u;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }
static int32_t rnd_span(int32_t span) { return (int32_t)(rnd() % (uint32_t)(2 * span + 1)) - span; }

typedef struct { ses_fix_state_t st; gps_fix_t last; int n; } walk_dec_t;
static void walk_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    walk_dec_t *d = ctx;
    if (ses_decode_fix(&d->st, type, payload, len, &d->last) == 1) d->n++;
}

static void test_fuzz_ten_thousand_fix_random_walk_round_trips(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    walk_dec_t d; memset(&d, 0, sizeof d); ses_fix_state_init(&d.st);
    ses_reader_t r; ses_reader_init(&r);

    gps_fix_t f = mk_fix(1789380900LL * 1000000LL, -338567000, 185170000, 45000, 30000, 9012000, 9, 1);
    for (int i = 0; i < 10000; i++) {
        f.gps_us += (int64_t)(100 + rnd() % 200u) * 1000;       /* 100..299 ms, whole milliseconds */
        f.lat_e7 += rnd_span(3000);
        f.lon_e7 += rnd_span(3000);
        f.alt_mm += rnd_span(2000);
        f.gspeed_mms = (int32_t)(rnd() % 60001u);
        f.head_e5 = (int32_t)(rnd() % 36000001u);
        f.hacc_mm = 1000 + rnd() % 5000u;
        f.sats = (uint8_t)(4 + rnd() % 16u);
        f.valid = (uint8_t)((rnd() % 32u) != 0);                 /* the occasional invalid fix */
        uint8_t frame[64];
        int w = ses_encode_fix(&enc, &f, frame, sizeof frame);
        TEST_ASSERT_GREATER_THAN(0, w);
        int before = d.n;
        ses_reader_feed(&r, frame, (size_t)w, walk_cb, &d);
        TEST_ASSERT_EQUAL_INT(before + 1, d.n);
        TEST_ASSERT_EQUAL_INT64(f.gps_us, d.last.gps_us);
        TEST_ASSERT_EQUAL_INT32(f.lat_e7, d.last.lat_e7);
        TEST_ASSERT_EQUAL_INT32(f.lon_e7, d.last.lon_e7);
        TEST_ASSERT_EQUAL_UINT8(f.sats, d.last.sats);
        TEST_ASSERT_EQUAL_UINT8(f.valid, d.last.valid);
        TEST_ASSERT_INT32_WITHIN(50, f.alt_mm, d.last.alt_mm);          /* decimetre field */
        TEST_ASSERT_INT32_WITHIN(5, f.gspeed_mms, d.last.gspeed_mms);   /* cm/s field */
    }
    TEST_ASSERT_EQUAL_INT(10000, d.n);
    TEST_ASSERT_EQUAL_UINT32(10000, r.frames_ok);
    TEST_ASSERT_EQUAL_UINT32(0, r.frames_bad);
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
    RUN_TEST(test_negative_ground_speed_forces_a_keyframe);
    RUN_TEST(test_hdr_strings_using_every_wire_byte_round_trip_nul_terminated);
    RUN_TEST(test_small_record_round_trips);
    RUN_TEST(test_calib_round_trip);
    RUN_TEST(test_decoders_reject_malformed_payloads);
    RUN_TEST(test_fuzz_ten_thousand_fix_random_walk_round_trips);
    return UNITY_END();
}
