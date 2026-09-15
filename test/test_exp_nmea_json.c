#include "unity.h"
#include "core/exp.h"
#include "core/ses.h"
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
    int laps = json_obj_get(out, toks, 0, "laps"); TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[laps].type); TEST_ASSERT_EQUAL_INT(2, toks[laps].size);
    int runs = json_obj_get(out, toks, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int hdr = json_obj_get(out, toks, 0, "hdr"); int venue = json_obj_get(out, toks, hdr, "venue");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[venue], "Killarney"));
    int lap1 = laps + 1; int ms = json_obj_get(out, toks, lap1, "ms"); int64_t v; json_tok_int(out, &toks[ms], &v); TEST_ASSERT_EQUAL_INT64(112341, v);
    int sectors = json_obj_get(out, toks, lap1, "sectors"); TEST_ASSERT_EQUAL_INT(2, toks[sectors].size);
    int valid = json_obj_get(out, toks, lap1, "valid"); bool b; json_tok_bool(out, &toks[valid], &b); TEST_ASSERT_TRUE(b);
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
    int runs = json_obj_get(out, toks, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int gates = json_obj_get(out, toks, runs + 1, "gates"); TEST_ASSERT_EQUAL_INT(DRAG_MAX_GATES, toks[gates].size);
    int last = gates + 1; for (int i = 0; i < DRAG_MAX_GATES - 1; i++) last = json_skip(toks, last);
    int dist = json_obj_get(out, toks, last, "dist_cm"); int64_t v; json_tok_int(out, &toks[dist], &v);
    TEST_ASSERT_EQUAL_INT64(2500 * DRAG_MAX_GATES, v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nmea_sentences_and_checksums);
    RUN_TEST(test_json_summary_structure);
    RUN_TEST(test_json_sixteen_gate_run_streams_across_pulls);
    return UNITY_END();
}
