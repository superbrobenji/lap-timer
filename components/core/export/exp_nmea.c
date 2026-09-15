#include "core/exp.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int exp_nmea_open(exp_t *e) { (void)e; return 0; }

static void latlon_fields(int32_t lat_e7, int32_t lon_e7, char *lat, char *ns, char *lon, char *ew)
{
    double la = fabs((double)lat_e7 / 1e7), lo = fabs((double)lon_e7 / 1e7);
    int lad = (int)la, lod = (int)lo;
    double lam = (la - lad) * 60.0, lom = (lo - lod) * 60.0;
    snprintf(lat, 16, "%02d%08.5f", lad, lam);
    snprintf(lon, 16, "%03d%08.5f", lod, lom);
    *ns = lat_e7 < 0 ? 'S' : 'N'; *ew = lon_e7 < 0 ? 'W' : 'E';
}

static int put_sentence(exp_t *e, const char *body)      /* body excludes '$' and '*hh' */
{
    uint8_t x = 0; for (const char *s = body; *s; s++) x ^= (uint8_t)*s;
    char line[128];
    snprintf(line, sizeof line, "$%s*%02X\r\n", body, x);
    return exp_win_puts(e, line);
}

int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return 0;
    if (exp_win_free(e) < 200) return EXP_FULL;
    gps_fix_t f;
    if (ses_decode_fix(&e->fix_st, type, p, len, &f) != 1) return -1;
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(f.gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    char lat[16], lon[16], ns, ew;
    latlon_fields(f.lat_e7, f.lon_e7, lat, &ns, lon, &ew);
    double knots = (double)f.gspeed_mms / 1000.0 * 1.943844;
    double course = (double)f.head_e5 / 1e5;
    char body[110];
    snprintf(body, sizeof body, "GPRMC,%02u%02u%02u.%02u,%c,%s,%c,%s,%c,%.2f,%06.2f,%02u%02u%02u,,,A",
             hh, mm, ss, cs, f.valid ? 'A' : 'V', lat, ns, lon, ew, knots, course, d, mo, (unsigned)(y % 100));
    if (put_sentence(e, body) < 0) return -1;
    snprintf(body, sizeof body, "GPGGA,%02u%02u%02u.%02u,%s,%c,%s,%c,%u,%02u,%.1f,%.1f,M,0.0,M,,",
             hh, mm, ss, cs, lat, ns, lon, ew, f.valid ? 1u : 0u, f.sats, (double)f.pdop_e2 / 100.0, (double)f.alt_mm / 1000.0);
    return put_sentence(e, body);
}

int exp_nmea_finish(exp_t *e) { (void)e; return 0; }
