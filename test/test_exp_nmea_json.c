#include "unity.h"
#include "core/exp.h"
#include "core/ses.h"
#include "core/bw.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static size_t drain(exp_t *e, char *dst, size_t at)
{
    uint8_t chunk[256]; size_t n;
    while (exp_pull(e, chunk, sizeof chunk, &n) == 0 && n > 0) { memcpy(dst + at, chunk, n); at += n; }
    dst[at] = '\0'; return at;
}
static void feed_frame(exp_t *e, char *dst, size_t *at, const uint8_t *fr, int n)
{
    while (exp_feed(e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)) == EXP_FULL) *at = drain(e, dst, *at);
}
static uint8_t nmea_xor(const char *s)     /* between '$' and '*' */
{
    uint8_t x = 0; for (s++; *s && *s != '*'; s++) x ^= (uint8_t)*s; return x;
}

static void test_nmea_sentences_and_checksums(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    char out[1024]; size_t at = 0;
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = 1789380900LL * 1000000LL; f.lat_e7 = -338567000; f.lon_e7 = 185170000; f.alt_mm = 45000; f.gspeed_mms = 34292; f.head_e5 = 9012000;
    f.pdop_e2 = 120; f.fix_type = 3; f.sats = 8; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    uint8_t fr[64]; int n = ses_encode_fix(&fs, &f, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    exp_finish(&e); at = drain(&e, out, at);
    /* speed 34.292 m/s = 66.66 kn */
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPRMC,101500.00,A,3351.40200,S,01831.02000,E,66.66,090.12,140926,,,A*"));
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPGGA,101500.00,3351.40200,S,01831.02000,E,1,08,1.2,45.0,M,0.0,M,,*"));
    char *rmc = strstr(out, "$GPRMC"), *gga = strstr(out, "$GPGGA");
    unsigned cs;
    sscanf(strchr(rmc, '*') + 1, "%2x", &cs); TEST_ASSERT_EQUAL_HEX8(nmea_xor(rmc), cs);
    sscanf(strchr(gga, '*') + 1, "%2x", &cs); TEST_ASSERT_EQUAL_HEX8(nmea_xor(gga), cs);
    TEST_ASSERT_NOT_NULL(strstr(out, "\r\n$GPGGA"));
}

/* The bad FIX_KEY payload all three exporter cases feed (same bytes as above). */
static void bad_fix_payload(uint8_t *p)
{
    bw_t w; bw_init(&w, p, 39);
    bw_i64(&w, -1000000000000LL); bw_i32(&w, -338561234); bw_i32(&w, 185123456);
    bw_i32(&w, 45000); bw_i32(&w, 18500); bw_i32(&w, 9012000); bw_u32(&w, 100);
    bw_u16(&w, 10); bw_u16(&w, 120); bw_u8(&w, 3); bw_u8(&w, 8); bw_u8(&w, 0x07);
}

static void test_nmea_drops_a_record_with_a_bad_timestamp_and_counts_it(void)
{
    /* io:C1: this used to emit "$GPRMC,4294967283429496725..." with a VALID checksum. */
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    uint8_t p[39]; bad_fix_payload(p);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, SES_T_FIX_KEY, p, 39));   /* skipped, not an abort */
    TEST_ASSERT_EQUAL_UINT16(1, e.skipped);
    TEST_ASSERT_EQUAL_UINT(0, e.win_len);                           /* nothing emitted */
}

static void test_json_summary_structure(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    uint8_t fr[256]; int n;
    ses_hdr_t h; memset(&h, 0, sizeof h); memcpy(h.session_id, "S00042_001", 10); h.venue_id = 6; h.layout_id = 1; strcpy(h.fw, "v0.1.0"); h.start_gps_us = 1789380900LL * 1000000LL; h.gps_hz = 5; h.fused_hz = 10;
    n = ses_encode_hdr(&h, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    n = ses_encode_venue(6, 1, "Killarney", fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    for (int i = 1; i <= 2; i++) {
        lap_result_t lap; memset(&lap, 0, sizeof lap);
        lap.lap_no = (uint16_t)i; lap.time_ms = 112340u + (uint32_t)i; lap.flags = LAP_F_VALID; lap.n_sectors = 2; lap.sector_ms[0] = 50000; lap.sector_ms[1] = 62340u + (uint32_t)i;
        lap.stats.max_speed_cms = 6000; lap.stats.max_lean_r_cdeg = 5500;
        n = ses_encode_lap(&lap, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    }
    drag_result_t run; memset(&run, 0, sizeof run); run.run_no = 1; run.n_gates = 1; run.gates[0] = (drag_gate_res_t){ 2, 5910, 2778, 9800, 1 }; run.trap_cms = 0;
    n = ses_encode_drag_run(&run, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    /* exp_finish may return EXP_FULL when the window is nearly full (see core/exp.h): pull and retry */
    int fin;
    while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);

    jsmntok_t toks[256];
    int cnt = json_parse(out, at, toks, 256);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int laps = json_obj_get(out, toks, cnt, 0, "laps"); TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[laps].type); TEST_ASSERT_EQUAL_INT(2, toks[laps].size);
    int runs = json_obj_get(out, toks, cnt, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int hdr = json_obj_get(out, toks, cnt, 0, "hdr"); int venue = json_obj_get(out, toks, cnt, hdr, "venue");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[venue], "Killarney"));
    int lap1 = laps + 1; int ms = json_obj_get(out, toks, cnt, lap1, "ms"); int64_t v; json_tok_int(out, &toks[ms], &v); TEST_ASSERT_EQUAL_INT64(112341, v);
    int sectors = json_obj_get(out, toks, cnt, lap1, "sectors"); TEST_ASSERT_EQUAL_INT(2, toks[sectors].size);
    int valid = json_obj_get(out, toks, cnt, lap1, "valid"); bool b; json_tok_bool(out, &toks[valid], &b); TEST_ASSERT_TRUE(b);
}

static void test_json_summary_reports_skipped_records(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[2048]; size_t at = 0;
    uint8_t lp[30]; memset(lp, 0, sizeof lp);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, SES_T_LAP, lp, 7));       /* wrong len -> decode fails */
    TEST_ASSERT_EQUAL_UINT16(1, e.skipped);
    int fin; while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"skipped\":1}"));
}

static void test_json_sixteen_gate_run_streams_across_pulls(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_002");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[8192]; size_t at = 0;
    uint8_t fr[256]; int n;
    drag_result_t run; memset(&run, 0, sizeof run); run.run_no = 3; run.n_gates = DRAG_MAX_GATES; run.trap_cms = 8472; run.flags = DRAG_F_QUARTER;
    for (uint8_t i = 0; i < DRAG_MAX_GATES; i++) run.gates[i] = (drag_gate_res_t){ (uint8_t)(i + 1), 1000u * (i + 1u), (uint16_t)(500u * (i + 1u)), 2500u * (i + 1u), 1 };
    n = ses_encode_drag_run(&run, fr, sizeof fr);
    /* small pulls force several EXP_FULL/resume cycles inside the run */
    int r;
    while ((r = exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD))) == EXP_FULL) {
        uint8_t chunk[64]; size_t got; exp_pull(&e, chunk, sizeof chunk, &got); memcpy(out + at, chunk, got); at += got;
    }
    TEST_ASSERT_EQUAL_INT(0, r);
    while ((r = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, exp_finish(&e));                       /* idempotent */
    TEST_ASSERT_EQUAL_UINT(at, drain(&e, out, at));                  /* nothing more emitted */
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int runs = json_obj_get(out, toks, cnt, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int gates = json_obj_get(out, toks, cnt, runs + 1, "gates"); TEST_ASSERT_EQUAL_INT(DRAG_MAX_GATES, toks[gates].size);
    int last = gates + 1; for (int i = 0; i < DRAG_MAX_GATES - 1; i++) last = json_skip(toks, cnt, last);
    int dist = json_obj_get(out, toks, cnt, last, "dist_cm"); int64_t v; json_tok_int(out, &toks[dist], &v);
    TEST_ASSERT_EQUAL_INT64(2500 * DRAG_MAX_GATES, v);
}

static void test_json_header_strings_using_every_wire_byte_are_emitted_whole(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10);
    memcpy(h.fw, "v0.3.1-abcdefghi", 16);          /* exactly 16 bytes, no NUL on the wire */
    memcpy(h.hwid, "moto_neo6m_epaper_int_bl", 24);
    h.venue_id = 6; h.layout_id = 1; h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = 1789640100000000LL;
    uint8_t fr[256]; int n = ses_encode_hdr(&h, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    n = ses_encode_venue(6, 1, "01234567890123456789012345678901", fr, sizeof fr);   /* 32 chars */
    feed_frame(&e, out, &at, fr, n);
    int fin;
    while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);

    jsmntok_t toks[128];
    int cnt = json_parse(out, at, toks, 128);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int hdr = json_obj_get(out, toks, cnt, 0, "hdr");
    int fw = json_obj_get(out, toks, cnt, hdr, "fw");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[fw], "v0.3.1-abcdefghi"));      /* no spill, no truncation */
    int venue = json_obj_get(out, toks, cnt, hdr, "venue");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[venue], "01234567890123456789012345678901"));
}

static void test_finish_refuses_while_a_drag_run_is_mid_emission(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_003");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[8192]; size_t at = 0;
    drag_result_t run; memset(&run, 0, sizeof run);
    run.run_no = 4; run.n_gates = DRAG_MAX_GATES; run.trap_cms = 8472;
    for (uint8_t i = 0; i < DRAG_MAX_GATES; i++)
        run.gates[i] = (drag_gate_res_t){ (uint8_t)(i + 1), 1000u * (i + 1u), (uint16_t)(500u * (i + 1u)), 2500u * (i + 1u), 1 };
    uint8_t fr[256]; int n = ses_encode_drag_run(&run, fr, sizeof fr);

    /* the window fills part-way through the gate list: the frame is left half-emitted */
    TEST_ASSERT_EQUAL_INT(EXP_FULL, exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)));
    TEST_ASSERT_EQUAL_UINT8(1, e.run_pending);
    TEST_ASSERT_EQUAL_INT(-1, exp_finish(&e));           /* closing now would truncate the run */
    TEST_ASSERT_EQUAL_UINT8(0, e.finished);              /* and the exporter is not marked finished */

    /* re-feeding the same frame after a pull resumes it, and then finish succeeds */
    int r;
    while ((r = exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD))) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_UINT8(0, e.run_pending);
    while ((r = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    at = drain(&e, out, at);
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int runs = json_obj_get(out, toks, cnt, 0, "runs");
    TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int gates = json_obj_get(out, toks, cnt, runs + 1, "gates");
    TEST_ASSERT_EQUAL_INT(DRAG_MAX_GATES, toks[gates].size);
}

static void test_json_header_with_control_chars_still_emits_every_lap(void)
{
    /* io:C2: session_id (10) + fw (16) + venue (32) all control chars expand 1 -> 6 () in
     * jw_str, so the header is 113 + 6*58 + 34 = 495 bytes -- over the old buf[400]. The old code
     * flipped json_stage BEFORE emit(), so the whole LAP array was dropped and exp_finish returned
     * 0 (SUCCESS) on a 12-byte fragment: [],"runs":[]}]. */
    exp_meta_t m; memset(&m, 0, sizeof m); memset(m.session_id, 0x01, 10);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memset(h.session_id, 0x01, 10); memset(h.fw, 0x01, 16); memset(h.hwid, 'h', 24);
    h.venue_id = 65535; h.layout_id = 65535; h.gps_hz = 25; h.fused_hz = 25;
    h.start_gps_us = 1789640100000000LL;
    uint8_t fr[256]; int n = ses_encode_hdr(&h, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    char nm[33]; memset(nm, 0x01, 32); nm[32] = '\0';          /* 32 control bytes (Task 4) */
    n = ses_encode_venue(65535, 65535, nm, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    lap_result_t lap; memset(&lap, 0, sizeof lap);
    lap.lap_no = 1; lap.time_ms = 112341; lap.flags = LAP_F_VALID;
    lap.n_sectors = 1; lap.sector_ms[0] = 50000;
    n = ses_encode_lap(&lap, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    int fin; while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);                          /* a whole, parseable document */
    int laps = json_obj_get(out, toks, cnt, 0, "laps");
    TEST_ASSERT_GREATER_THAN(0, laps);
    TEST_ASSERT_EQUAL_INT(1, toks[laps].size);                 /* the LAP was NOT dropped */
    TEST_ASSERT_EQUAL_UINT16(0, e.skipped);
}

static int one_fix_frame(uint8_t *fr, size_t cap)         /* 101500.00, 66.66 kn, 8 sats */
{
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = 1789380900LL * 1000000LL; f.lat_e7 = -338567000; f.lon_e7 = 185170000;
    f.alt_mm = 45000; f.gspeed_mms = 34292; f.head_e5 = 9012000;
    f.pdop_e2 = 120; f.fix_type = 3; f.sats = 8;
    f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    return ses_encode_fix(&fs, &f, fr, cap);
}

/* exp_win_free ignores win_pos, so padding is the only way to hit the boundary deterministically. */
static void pad_window_to_free(exp_t *e, int want)
{
    char pad[EXP_WINDOW];
    size_t n = (size_t)(EXP_WINDOW - want);
    memset(pad, 'x', n); pad[n] = '\0';
    TEST_ASSERT_EQUAL_INT(0, exp_win_puts(e, pad));
    TEST_ASSERT_EQUAL_INT(want, exp_win_free(e));
}

static void test_nmea_reserve_bounds_both_sentences(void)
{
    /* io:M5: the old 200-byte reserve let the first sentence in and then failed the second's
     * exp_win_puts, returning -1 with one orphan sentence already in the window. */
    exp_meta_t m; memset(&m, 0, sizeof m);
    uint8_t fr[64]; int n = one_fix_frame(fr, sizeof fr);
    uint8_t len = (uint8_t)(n - SES_FRAME_OVERHEAD);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    pad_window_to_free(&e, 231);                              /* one below the reserve */
    size_t before = e.win_len;
    TEST_ASSERT_EQUAL_INT(EXP_FULL, exp_feed(&e, fr[1], fr + 3, len));
    TEST_ASSERT_EQUAL_UINT(before, e.win_len);                /* nothing written, no orphan */

    exp_t e2; TEST_ASSERT_EQUAL_INT(0, exp_open(&e2, EXP_NMEA, &m));
    pad_window_to_free(&e2, 232);                             /* exactly the reserve */
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e2, fr[1], fr + 3, len));
    char out[EXP_WINDOW + 1]; size_t at = drain(&e2, out, 0);
    TEST_ASSERT_GREATER_THAN(0, at);
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPRMC,101500.00"));
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPGGA,101500.00"));    /* both sentences made it */
}

static void test_pull_with_a_cap_smaller_than_the_window_streams_then_resets(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    uint8_t fr[64]; int n = one_fix_frame(fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)));
    size_t len = e.win_len, total = 0, got = 0;
    uint8_t buf[16];
    TEST_ASSERT_GREATER_THAN(sizeof buf, len);
    while (total < len) {
        TEST_ASSERT_EQUAL_INT(0, exp_pull(&e, buf, sizeof buf, &got));
        TEST_ASSERT_TRUE(got > 0 && got <= sizeof buf);
        total += got;
    }
    TEST_ASSERT_EQUAL_UINT(len, total);
    TEST_ASSERT_EQUAL_UINT(0, e.win_len);                     /* a full drain resets the window */
    TEST_ASSERT_EQUAL_INT(0, exp_pull(&e, buf, sizeof buf, &got));
    TEST_ASSERT_EQUAL_UINT(0, got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nmea_sentences_and_checksums);
    RUN_TEST(test_nmea_drops_a_record_with_a_bad_timestamp_and_counts_it);
    RUN_TEST(test_nmea_reserve_bounds_both_sentences);
    RUN_TEST(test_pull_with_a_cap_smaller_than_the_window_streams_then_resets);
    RUN_TEST(test_json_summary_structure);
    RUN_TEST(test_json_summary_reports_skipped_records);
    RUN_TEST(test_json_sixteen_gate_run_streams_across_pulls);
    RUN_TEST(test_json_header_strings_using_every_wire_byte_are_emitted_whole);
    RUN_TEST(test_finish_refuses_while_a_drag_run_is_mid_emission);
    RUN_TEST(test_json_header_with_control_chars_still_emits_every_lap);
    return UNITY_END();
}
