#include "core/exp.h"
#include "core/core.h"
#include <stdio.h>
#include <string.h>

#define EXP_ASSERT_CODE 0x0A60

static const char *HEADER_PART1 =
    "\r\n[header]\r\nsatellites\r\ntime\r\nlatitude\r\nlongitude\r\nvelocity kmh\r\nheading\r\nheight\r\nlat_g\r\nlon_g\r\nlean\r\nyaw\r\n"
    "\r\n[channel units]\r\n\r\n[comments]\r\n";

int exp_vbo_open(exp_t *e)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    char line[160];
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(e->meta.created_gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    snprintf(line, sizeof line, "File created on %02u/%02u/%04d at %02u:%02u:%02u\r\n", d, mo, y, hh, mm, ss);
    if (exp_win_puts(e, line) < 0) return -1;
    if (exp_win_puts(e, HEADER_PART1) < 0) return -1;
    snprintf(line, sizeof line, "LapTimer %s (%s)\r\nSession %s\r\nVenue %s / %s\r\n\r\n", e->meta.fw, e->meta.hwid, e->meta.session_id, e->meta.venue, e->meta.layout);
    if (exp_win_puts(e, line) < 0) return -1;
    if (e->meta.has_sf) {
        snprintf(line, sizeof line, "[laptiming]\r\nStart %.5f %.5f %.5f %.5f\r\n\r\n",
                 -e->meta.sf_lon1 * 60.0, e->meta.sf_lat1 * 60.0, -e->meta.sf_lon2 * 60.0, e->meta.sf_lat2 * 60.0);
        if (exp_win_puts(e, line) < 0) return -1;
    }
    if (exp_win_puts(e, "[column names]\r\nsats time lat long velocity heading height lat_g lon_g lean yaw\r\n\r\n[data]\r\n") < 0) return -1;
    return 0;
}

int exp_vbo_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(p != NULL || len == 0, EXP_ASSERT_CODE, -1);
    if (type == SES_T_FUSED) {
        fused_sample_t s;
        if (ses_decode_fused(&e->fus_st, p, len, &s) == 1) { e->held = s; e->have_held = 1; }
        return 0;
    }
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return 0;
    if (exp_win_free(e) < 120) return EXP_FULL;
    gps_fix_t f;
    if (ses_decode_fix(&e->fix_st, type, p, len, &f) != 1) return -1;
    ses_fused_state_on_fix(&e->fus_st, f.gps_us);
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(f.gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    double lat_min = (double)f.lat_e7 / 1e7 * 60.0;
    double long_min = -((double)f.lon_e7 / 1e7 * 60.0);      /* west positive */
    double kmh = (double)f.gspeed_mms * 3.6 / 1000.0;
    double head = (double)f.head_e5 / 1e5;
    double height = (double)f.alt_mm / 1000.0;
    float glat = e->have_held ? e->held.g_lat : 0.0f, glon = e->have_held ? e->held.g_lon : 0.0f;
    float lean = e->have_held ? e->held.lean_deg : 0.0f, yaw = e->have_held ? e->held.yaw_dps : 0.0f;
    char line[120];
    snprintf(line, sizeof line, "%03u %02u%02u%02u.%02u %.5f %.5f %.2f %06.2f %07.2f %+.3f %+.3f %+06.2f %+06.2f\r\n",
             f.sats, hh, mm, ss, cs, lat_min, long_min, kmh, head, height, (double)glat, (double)glon, (double)lean, (double)yaw);
    return exp_win_puts(e, line);
}

int exp_vbo_finish(exp_t *e) { (void)e; return 0; }
