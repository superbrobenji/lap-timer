#include "core/exp.h"
#include "core/core.h"
#include <string.h>

#define EXP_ASSERT_CODE 0x0A60

int exp_win_free(const exp_t *e)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, 0);
    return (int)(EXP_WINDOW - e->win_len);
}

int exp_win_puts(exp_t *e, const char *s)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(s != NULL, EXP_ASSERT_CODE, -1);
    size_t n = strlen(s);
    /* Buffer/window capacity before a write: every caller pre-checks exp_win_free() against its
     * own worst-case write size before calling this, so this should never trip in practice. */
    CORE_ASSERT_RET(e->win_len + n <= EXP_WINDOW, EXP_ASSERT_CODE, -1);
    memcpy(e->win + e->win_len, s, n); e->win_len += n;
    return 0;
}

void exp_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    CORE_ASSERT_VOID(y != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(d != NULL, EXP_ASSERT_CODE);
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

void exp_utc_parts(int64_t gps_us, int *y, unsigned *mo, unsigned *d, unsigned *hh, unsigned *mm, unsigned *ss, unsigned *cs)
{
    CORE_ASSERT_VOID(y != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(mo != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(d != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(hh != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(mm != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(ss != NULL, EXP_ASSERT_CODE);
    CORE_ASSERT_VOID(cs != NULL, EXP_ASSERT_CODE);
    int64_t total_cs = (gps_us + 5000) / 10000;           /* round to centiseconds */
    int64_t secs = total_cs / 100;
    *cs = (unsigned)(total_cs % 100);
    int64_t days = secs / 86400; int64_t sod = secs % 86400;
    exp_civil_from_days(days, y, mo, d);
    *hh = (unsigned)(sod / 3600); *mm = (unsigned)((sod % 3600) / 60); *ss = (unsigned)(sod % 60);
}

int exp_open(exp_t *e, uint8_t fmt, const exp_meta_t *meta)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(meta != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(fmt == EXP_VBO || fmt == EXP_NMEA || fmt == EXP_JSON, EXP_ASSERT_CODE, -1);
    memset(e, 0, sizeof *e);
    e->fmt = fmt; e->meta = *meta;
    ses_fix_state_init(&e->fix_st); ses_fused_state_init(&e->fus_st);
    switch (fmt) {
    case EXP_VBO:  return exp_vbo_open(e);
    case EXP_NMEA: return exp_nmea_open(e);
    case EXP_JSON: return exp_json_open(e);
    default: return -1;
    }
}

int exp_feed(exp_t *e, uint8_t type, const uint8_t *payload, uint8_t len)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(payload != NULL || len == 0, EXP_ASSERT_CODE, -1);
    if (e->finished) return -1;
    switch (e->fmt) {
    case EXP_VBO:  return exp_vbo_feed(e, type, payload, len);
    case EXP_NMEA: return exp_nmea_feed(e, type, payload, len);
    case EXP_JSON: return exp_json_feed(e, type, payload, len);
    default: return -1;
    }
}

int exp_pull(exp_t *e, uint8_t *out, size_t cap, size_t *n_out)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL || cap == 0, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(n_out != NULL, EXP_ASSERT_CODE, -1);
    size_t avail = e->win_len - e->win_pos;
    size_t n = avail < cap ? avail : cap;
    memcpy(out, e->win + e->win_pos, n);
    e->win_pos += n;
    if (e->win_pos == e->win_len) { e->win_pos = 0; e->win_len = 0; }
    *n_out = n;
    return 0;
}

int exp_finish(exp_t *e)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    if (e->finished) return 0;
    int r;
    switch (e->fmt) {
    case EXP_VBO:  r = exp_vbo_finish(e); break;
    case EXP_NMEA: r = exp_nmea_finish(e); break;
    case EXP_JSON: r = exp_json_finish(e); break;
    default: r = -1;
    }
    if (r == 0) e->finished = 1;
    return r;
}
