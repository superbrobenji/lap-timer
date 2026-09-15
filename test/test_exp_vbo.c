#include "unity.h"
#include "core/exp.h"
#include "core/ses.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/* drain everything currently pullable into dst */
static size_t drain(exp_t *e, char *dst, size_t cap, size_t at)
{
    (void)cap;
    uint8_t chunk[128]; size_t n;
    while (exp_pull(e, chunk, sizeof chunk, &n) == 0 && n > 0) { memcpy(dst + at, chunk, n); at += n; }
    dst[at] = '\0';
    return at;
}

static int feed(exp_t *e, char *dst, size_t cap, size_t *at, uint8_t type, const uint8_t *frame, int frame_len)
{
    int r;
    while ((r = exp_feed(e, type, frame + 3, (uint8_t)(frame_len - SES_FRAME_OVERHEAD))) == EXP_FULL) *at = drain(e, dst, cap, *at);
    return r;
}

static void test_vbo_golden_single_fix_with_fused(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    strcpy(m.session_id, "S00042_001"); strcpy(m.fw, "v0.1.0"); strcpy(m.hwid, "moto_neo6m_epaper");
    strcpy(m.venue, "Killarney"); strcpy(m.layout, "Full");
    m.created_gps_us = 1789380900LL * 1000000LL;        /* 2026-09-14 10:15:00 UTC */
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_VBO, &m));
    char out[4096]; size_t at = 0; at = drain(&e, out, sizeof out, at);

    uint8_t fr[64];
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    ses_fused_state_t fu; ses_fused_state_init(&fu);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = m.created_gps_us; f.lat_e7 = -338567000; f.lon_e7 = 185170000; f.alt_mm = 45000; f.gspeed_mms = 34300; f.head_e5 = 9012000;
    f.hacc_mm = 2500; f.fix_type = 3; f.sats = 8; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    int n = ses_encode_fix(&fs, &f, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    ses_fused_state_on_fix(&fu, f.gps_us);
    fused_sample_t s; memset(&s, 0, sizeof s); s.gps_us = f.gps_us + 40000; s.g_lat = 0.12f; s.g_lon = -0.05f; s.lean_deg = 12.3f; s.yaw_dps = 5.2f;
    n = ses_encode_fused(&fu, &s, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    gps_fix_t f2 = f; f2.gps_us += 200000; f2.lat_e7 += 540;
    n = ses_encode_fix(&fs, &f2, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    TEST_ASSERT_EQUAL_INT(0, exp_finish(&e));
    at = drain(&e, out, sizeof out, at);

    const char *expected =
        "File created on 14/09/2026 at 10:15:00\r\n"
        "\r\n"
        "[header]\r\n"
        "satellites\r\ntime\r\nlatitude\r\nlongitude\r\nvelocity kmh\r\nheading\r\nheight\r\nlat_g\r\nlon_g\r\nlean\r\nyaw\r\n"
        "\r\n"
        "[channel units]\r\n"
        "\r\n"
        "[comments]\r\n"
        "LapTimer v0.1.0 (moto_neo6m_epaper)\r\n"
        "Session S00042_001\r\n"
        "Venue Killarney / Full\r\n"
        "\r\n"
        "[column names]\r\n"
        "sats time lat long velocity heading height lat_g lon_g lean yaw\r\n"
        "\r\n"
        "[data]\r\n"
        "008 101500.00 -2031.40200 -1111.02000 123.48 090.12 0045.00 +0.000 +0.000 +00.00 +00.00\r\n"
        "008 101500.20 -2031.39876 -1111.02000 123.48 090.12 0045.00 +0.120 -0.050 +12.30 +05.20\r\n";
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void test_vbo_laptiming_section_when_sf_known(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    m.created_gps_us = 1789380900LL * 1000000LL; m.has_sf = 1;
    m.sf_lat1 = -33.8578; m.sf_lon1 = 18.5152; m.sf_lat2 = -33.85795; m.sf_lon2 = 18.51535;
    exp_t e; exp_open(&e, EXP_VBO, &m);
    char out[4096]; size_t at = drain(&e, out, sizeof out, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "[laptiming]\r\nStart -1110.91200 -2031.46800 -1110.92100 -2031.47700\r\n"));
    (void)at;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vbo_golden_single_fix_with_fused);
    RUN_TEST(test_vbo_laptiming_section_when_sf_known);
    return UNITY_END();
}
