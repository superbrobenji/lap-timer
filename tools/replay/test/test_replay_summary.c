/* mkstemp/unlink/close/open_memstream are POSIX, not C11; the build is -std=c11. */
#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "replay/replay.h"
#include "replay/logio.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define T0_GPS_US 1789380900000000LL
#define N_FIX     25
#define N_FUSED   10
#define INVALID_I 7                      /* the one fix written with valid = 0 */
#define JSON_MAX_TOKS 1024               /* the summary object is ~200 tokens; 1024 is ample headroom */

static char tmp_path[64];

void setUp(void) { tmp_path[0] = '\0'; }
void tearDown(void) { if (tmp_path[0]) { unlink(tmp_path); tmp_path[0] = '\0'; } }

static void make_tmp(void)
{
    strcpy(tmp_path, "/tmp/laptimer_rsum_XXXXXX");
    int fd = mkstemp(tmp_path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
}

/* Writes the fixture log: hdr, venue, 25 fixes at 5 Hz (one invalid), 10 fused, 2 laps, end. */
static void write_fixture(void)
{
    make_tmp();
    logw_t w;
    TEST_ASSERT_EQUAL_INT(0, logw_open_file(&w, tmp_path));

    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00001_001", 10);
    strcpy(h.fw, "v0.2.0"); strcpy(h.hwid, "moto_neo6m_epaper");
    h.mode = 0; h.variant = 0; h.venue_id = 1000; h.layout_id = 1;
    h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = T0_GPS_US;
    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 1, "Synthetic"));

    for (int i = 0; i < N_FIX; i++) {
        gps_fix_t f; memset(&f, 0, sizeof f);
        f.gps_us = T0_GPS_US + (int64_t)i * 200000;
        f.mono_us = 1000000 + (int64_t)i * 200000;
        f.lat_e7 = -338567000 + i * 500; f.lon_e7 = 185170000; f.alt_mm = 45000;
        f.gspeed_mms = 30000 + i * 100;          /* whole cm/s: the delta field is exact */
        f.head_e5 = 9000000; f.hacc_mm = 1500; f.sacc_mms = 300; f.pdop_e2 = 150;
        f.fix_type = 3; f.sats = 9;
        f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
        f.valid = (uint8_t)((i == INVALID_I) ? 0 : 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
        if (i < N_FUSED) {
            fused_sample_t u; memset(&u, 0, sizeof u);
            u.gps_us = f.gps_us + 100000; u.mono_us = u.gps_us;
            u.g_lat = 0.5f; u.g_lon = -0.2f; u.lean_deg = 20.0f; u.yaw_dps = -8.0f;
            u.flags = FUS_LEAN_VALID | FUS_ORIENT_OK;
            TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u));
        }
    }

    for (int k = 0; k < 2; k++) {
        lap_result_t l; memset(&l, 0, sizeof l);
        l.lap_no = (uint16_t)(k + 1);
        l.start_gps_us = T0_GPS_US + (int64_t)k * 91000000;
        l.time_ms = (uint32_t)(91234 - k * 247);
        l.flags = LAP_F_VALID; l.n_sectors = 3;
        l.sector_ms[0] = 30100; l.sector_ms[1] = 30500; l.sector_ms[2] = l.time_ms - 60600;
        TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &l));
    }
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 200000000, 1));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(2 + N_FIX + N_FUSED + 2 + 1, w.frames);
}

/* Renders with `pr` into a heap string the caller frees. */
static char *render(void (*pr)(const replay_summary_t *, FILE *), const replay_summary_t *s)
{
    char *out = NULL; size_t n = 0;
    FILE *ms = open_memstream(&out, &n);
    TEST_ASSERT_NOT_NULL(ms);
    pr(s, ms);
    TEST_ASSERT_EQUAL_INT(0, fclose(ms));
    TEST_ASSERT_NOT_NULL(out);
    return out;
}

static void test_summary_counts_and_spans(void)
{
    write_fixture();
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(0, replay_summarize_file(tmp_path, &s));

    TEST_ASSERT_EQUAL_UINT32(2 + N_FIX + N_FUSED + 2 + 1, s.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, s.n_bad);
    TEST_ASSERT_EQUAL_INT(1, s.have_hdr);
    TEST_ASSERT_EQUAL_STRING("S00001_001", s.hdr.session_id);
    TEST_ASSERT_EQUAL_UINT8(5, s.hdr.gps_hz);
    TEST_ASSERT_EQUAL_INT(1, s.have_venue);
    TEST_ASSERT_EQUAL_STRING("Synthetic", s.venue.name);

    TEST_ASSERT_EQUAL_UINT32(N_FIX, s.n_fix);
    TEST_ASSERT_EQUAL_UINT32(N_FIX - 1, s.n_fix_valid);              /* one fix written invalid */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US, s.first_fix_gps_us);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + (int64_t)(N_FIX - 1) * 200000, s.last_fix_gps_us);
    TEST_ASSERT_EQUAL_INT32(30000 + (N_FIX - 1) * 100, s.max_gspeed_mms);
    TEST_ASSERT_EQUAL_UINT32(N_FUSED, s.n_fused);

    TEST_ASSERT_EQUAL_UINT32(2, s.n_lap);
    TEST_ASSERT_EQUAL_UINT16(2, s.n_laps_listed);
    TEST_ASSERT_EQUAL_UINT16(1, s.laps[0].lap_no);
    TEST_ASSERT_EQUAL_UINT32(91234, s.laps[0].time_ms);
    TEST_ASSERT_EQUAL_UINT16(2, s.laps[1].lap_no);
    TEST_ASSERT_EQUAL_UINT32(90987, s.laps[1].time_ms);
    TEST_ASSERT_EQUAL_UINT8(3, s.laps[1].n_sectors);

    TEST_ASSERT_EQUAL_INT(1, s.have_end);
    TEST_ASSERT_EQUAL_UINT8(1, s.end.reason);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 200000000, s.end.gps_us);

    TEST_ASSERT_EQUAL_UINT32(1, s.n_by_type[SES_T_SESSION_HDR]);
    TEST_ASSERT_EQUAL_UINT32(N_FUSED, s.n_by_type[SES_T_FUSED]);
    TEST_ASSERT_EQUAL_UINT32(2, s.n_by_type[SES_T_LAP]);
    TEST_ASSERT_EQUAL_UINT32(N_FIX, s.n_by_type[SES_T_FIX_KEY] + s.n_by_type[SES_T_FIX_DELTA]);
}

static void test_json_and_text_rendering(void)
{
    write_fixture();
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(0, replay_summarize_file(tmp_path, &s));

    char *js = render(replay_print_json, &s);
    TEST_ASSERT_NOT_NULL(strstr(js, "\"bad_frames\":0"));
    char want_frames[32];
    snprintf(want_frames, sizeof want_frames, "\"frames\":%u", (unsigned)s.n_frames);
    TEST_ASSERT_NOT_NULL(strstr(js, want_frames));

    jsmntok_t *toks = malloc(sizeof(jsmntok_t) * JSON_MAX_TOKS);
    TEST_ASSERT_NOT_NULL(toks);
    int ntoks = json_parse(js, strlen(js), toks, JSON_MAX_TOKS);
    TEST_ASSERT_GREATER_THAN(0, ntoks);
    TEST_ASSERT_EQUAL_INT(JSMN_OBJECT, toks[0].type);

    int laps = json_obj_get(js, toks, ntoks, 0, "laps");
    TEST_ASSERT_GREATER_THAN(0, laps);
    TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[laps].type);
    TEST_ASSERT_EQUAL_INT(2, toks[laps].size);

    int hdr = json_obj_get(js, toks, ntoks, 0, "hdr");
    TEST_ASSERT_GREATER_THAN(0, hdr);
    TEST_ASSERT_EQUAL_INT(JSMN_OBJECT, toks[hdr].type);
    int sid = json_obj_get(js, toks, ntoks, hdr, "session_id");
    TEST_ASSERT_GREATER_THAN(0, sid);
    TEST_ASSERT_TRUE(json_tok_eq(js, &toks[sid], "S00001_001"));

    int nlaps = json_obj_get(js, toks, ntoks, 0, "n_laps");
    TEST_ASSERT_GREATER_THAN(0, nlaps);
    int64_t v = 0;
    TEST_ASSERT_TRUE(json_tok_int(js, &toks[nlaps], &v));
    TEST_ASSERT_EQUAL_INT64(2, v);

    int fix = json_obj_get(js, toks, ntoks, 0, "fix");
    TEST_ASSERT_GREATER_THAN(0, fix);
    int nvalid = json_obj_get(js, toks, ntoks, fix, "valid");
    TEST_ASSERT_TRUE(json_tok_int(js, &toks[nvalid], &v));
    TEST_ASSERT_EQUAL_INT64(N_FIX - 1, v);

    free(toks);
    free(js);

    char *txt = render(replay_print_text, &s);
    TEST_ASSERT_NOT_NULL(strstr(txt, "frames"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "lap 1"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "lap 2"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "end"));
    free(txt);
}

static void test_missing_file_fails(void)
{
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(-1, replay_summarize_file("/tmp/laptimer_rsum_does_not_exist", &s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_summary_counts_and_spans);
    RUN_TEST(test_json_and_text_rendering);
    RUN_TEST(test_missing_file_fails);
    return UNITY_END();
}
