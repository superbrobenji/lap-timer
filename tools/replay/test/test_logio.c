/* mkstemp/unlink/close are POSIX, not C11; the build is -std=c11 so ask for them explicitly. */
#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "replay/logio.h"
#include "core/consts.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Wire quantisation of FIX_DELTA (spec §12.3/§12.4): altitude in decimetres, speed in cm/s,
 * heading in 0.01 deg (head_e5 / 1000), hacc in decimetres. A KEY frame carries all of them
 * exactly; these are the worst-case reconstruction errors of a DELTA. */
#define FIX_ALT_TOL_MM   100
#define FIX_SPEED_TOL_MMS 10
#define FIX_HEAD_TOL_E5  1000
#define FIX_HACC_TOL_MM  100

static char tmp_path[64];

void setUp(void) { tmp_path[0] = '\0'; }
void tearDown(void) { if (tmp_path[0]) { unlink(tmp_path); tmp_path[0] = '\0'; } }

static void make_tmp(void)
{
    strcpy(tmp_path, "/tmp/laptimer_logio_XXXXXX");
    int fd = mkstemp(tmp_path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
}

/* ---- collector ---- */

typedef struct {
    ses_hdr_t      hdr;    int n_hdr;
    ses_venue_t    venue;  int n_venue;
    ses_time_map_t tm;     int n_tm;
    gps_fix_t      fix[128];   int n_fix;
    fused_sample_t fused[64];  int n_fused;
    lap_result_t   lap;    int n_lap;
    ses_sector_t   sec;    int n_sec;
    drag_result_t  run;    int n_run;
    ses_drag_gate_t dg;    int n_dg;
    ses_event_t    ev;     int n_ev;
    ses_calib_t    cal;    int n_cal;
    ses_mark_t     mk;     int n_mk;
    ses_power_t    pw;     int n_pw;
    ses_end_t      end;    int n_end;
    uint8_t        bad_type, bad_len; int n_bad_cb;
} col_t;

static void c_hdr(const ses_hdr_t *h, void *ctx)        { col_t *c = ctx; c->hdr = *h; c->n_hdr++; }
static void c_venue(const ses_venue_t *v, void *ctx)    { col_t *c = ctx; c->venue = *v; c->n_venue++; }
static void c_tm(const ses_time_map_t *t, void *ctx)    { col_t *c = ctx; c->tm = *t; c->n_tm++; }
static void c_fix(const gps_fix_t *f, void *ctx)
{
    col_t *c = ctx;
    if (c->n_fix < (int)(sizeof c->fix / sizeof c->fix[0])) c->fix[c->n_fix] = *f;
    c->n_fix++;
}
static void c_fused(const fused_sample_t *fs, void *ctx)
{
    col_t *c = ctx;
    if (c->n_fused < (int)(sizeof c->fused / sizeof c->fused[0])) c->fused[c->n_fused] = *fs;
    c->n_fused++;
}
static void c_lap(const lap_result_t *l, void *ctx)     { col_t *c = ctx; c->lap = *l; c->n_lap++; }
static void c_sec(const ses_sector_t *s, void *ctx)     { col_t *c = ctx; c->sec = *s; c->n_sec++; }
static void c_run(const drag_result_t *r, void *ctx)    { col_t *c = ctx; c->run = *r; c->n_run++; }
static void c_dg(const ses_drag_gate_t *g, void *ctx)   { col_t *c = ctx; c->dg = *g; c->n_dg++; }
static void c_ev(const ses_event_t *e, void *ctx)       { col_t *c = ctx; c->ev = *e; c->n_ev++; }
static void c_cal(const ses_calib_t *cb, void *ctx)     { col_t *c = ctx; c->cal = *cb; c->n_cal++; }
static void c_mk(const ses_mark_t *m, void *ctx)        { col_t *c = ctx; c->mk = *m; c->n_mk++; }
static void c_pw(const ses_power_t *p, void *ctx)       { col_t *c = ctx; c->pw = *p; c->n_pw++; }
static void c_end(const ses_end_t *e, void *ctx)        { col_t *c = ctx; c->end = *e; c->n_end++; }
static void c_bad(uint8_t type, uint8_t len, void *ctx)
{
    col_t *c = ctx; c->bad_type = type; c->bad_len = len; c->n_bad_cb++;
}

static const logr_cb_t ALL_CB = {
    .on_hdr = c_hdr, .on_venue = c_venue, .on_time_map = c_tm, .on_fix = c_fix, .on_fused = c_fused,
    .on_lap = c_lap, .on_sector = c_sec, .on_drag_run = c_run, .on_drag_gate = c_dg, .on_event = c_ev,
    .on_calib = c_cal, .on_mark = c_mk, .on_power = c_pw, .on_end = c_end, .on_bad = c_bad,
};

/* ---- fixtures ---- */

#define T0_GPS_US 1789380900000000LL

static gps_fix_t mk_fix(int i, uint8_t valid)
{
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = T0_GPS_US + (int64_t)i * 200000;          /* 5 Hz */
    f.mono_us = 1000000 + (int64_t)i * 200000;
    f.lat_e7 = -338567000 + i * 500;
    f.lon_e7 = 185170000;
    f.alt_mm = 45000;
    f.gspeed_mms = 30000;                                 /* whole cm/s, so DELTA is exact */
    f.head_e5 = 9000000;                                  /* whole 0.01 deg, so DELTA is exact */
    f.hacc_mm = 1500;                                     /* whole dm, so DELTA is exact */
    f.sacc_mms = 300; f.pdop_e2 = 150; f.fix_type = 3; f.sats = 9;
    f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f.valid = valid;
    return f;
}

static void fill_hdr(ses_hdr_t *h)
{
    memset(h, 0, sizeof *h);
    memcpy(h->session_id, "S00042_007", 10);              /* all 10 wire bytes used */
    memcpy(h->fw, "v0.3.1-abcdefghi", 16);                /* all 16 wire bytes used */
    strcpy(h->hwid, "moto_neo6m_epaper_int_bl");
    h->mode = 1; h->variant = 2; h->venue_id = 1000; h->layout_id = 3;
    h->log_profile = 1; h->fused_hz = 10; h->gps_hz = 5; h->start_gps_us = T0_GPS_US;
    for (int i = 0; i < 9; i++) h->r_e4[i] = (int16_t)(i * 1000 - 4000);
    h->gbias[0] = -12; h->gbias[1] = 340; h->gbias[2] = 7;
    h->calib_flags = 0x03;
}

static fused_sample_t mk_fused(int64_t gps_us, float glat)
{
    fused_sample_t s; memset(&s, 0, sizeof s);
    s.gps_us = gps_us; s.mono_us = gps_us;
    s.g_lat = glat; s.g_lon = -0.25f; s.lean_deg = 18.5f; s.yaw_dps = -7.25f;
    s.flags = FUS_LEAN_VALID | FUS_ORIENT_OK;
    return s;
}

static void fill_lap(lap_result_t *l)
{
    memset(l, 0, sizeof *l);
    l->lap_no = 3; l->start_gps_us = T0_GPS_US; l->time_ms = 91234; l->flags = LAP_F_VALID; l->n_sectors = 3;
    l->sector_ms[0] = 30100; l->sector_ms[1] = 30500; l->sector_ms[2] = 30634;
    l->stats.max_speed_cms = 5011; l->stats.min_speed_cms = 1500;
    l->stats.max_lean_l_cdeg = -4200; l->stats.max_lean_r_cdeg = 4500;
    l->stats.max_glat_e3 = 1320; l->stats.max_gacc_e3 = 610; l->stats.max_gbrake_e3 = -1050;
}

static void fill_run(drag_result_t *r)
{
    memset(r, 0, sizeof *r);
    r->run_no = 2; r->t0_gps_us = T0_GPS_US + 1000000; r->flags = DRAG_F_QUARTER; r->trap_cms = 8472; r->n_gates = 4;
    r->gates[0] = (drag_gate_res_t){ 1, 1810, 1000, 300, 1 };
    r->gates[1] = (drag_gate_res_t){ 2, 5660, 2778, 9800, 1 };
    r->gates[2] = (drag_gate_res_t){ 3, 9200, 5000, 25000, 1 };
    r->gates[3] = (drag_gate_res_t){ 4, 12810, 8472, 40234, 1 };
}

static void fill_calib(ses_calib_t *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 9; i++) c->r_e4[i] = (int16_t)(10000 - i * 2500);
    c->gbias[0] = 11; c->gbias[1] = -22; c->gbias[2] = 33;
    c->calib_flags = 0x03;
}

/* ---- tests ---- */

static void test_memory_round_trip_of_every_record_type(void)
{
    uint8_t mem[8192];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);

    ses_hdr_t h; fill_hdr(&h);
    lap_result_t lap; fill_lap(&lap);
    drag_result_t run; fill_run(&run);
    ses_calib_t cal; fill_calib(&cal);
    gps_fix_t f0 = mk_fix(0, 1), f1 = mk_fix(1, 1), f2 = mk_fix(2, 1);
    fused_sample_t u0 = mk_fused(f0.gps_us + 50000, 0.4f);
    fused_sample_t u1 = mk_fused(f0.gps_us + 150000, -0.6f);

    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 3, "Synthetic"));
    TEST_ASSERT_GREATER_THAN(0, logw_time_map(&w, 1000000, T0_GPS_US, 2));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f2));
    TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &lap));
    TEST_ASSERT_GREATER_THAN(0, logw_sector(&w, 3, 1, T0_GPS_US + 30100000, 30100, -210));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_run(&w, &run));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_gate(&w, 2, 3, T0_GPS_US + 9200000, 9200, 5000, 25000));
    TEST_ASSERT_GREATER_THAN(0, logw_event(&w, 1234, T0_GPS_US, 0x0101, 0xDEADBEEF));
    TEST_ASSERT_GREATER_THAN(0, logw_calib(&w, &cal));
    TEST_ASSERT_GREATER_THAN(0, logw_mark(&w, T0_GPS_US + 7, 1));
    TEST_ASSERT_GREATER_THAN(0, logw_power(&w, 4242, 3, 3900));
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 20000000, 2));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(17, w.frames);

    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);

    TEST_ASSERT_EQUAL_UINT32(17, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);

    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_SESSION_HDR]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_VENUE]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_TIME_MAP]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_FIX_KEY]);       /* f0 only */
    TEST_ASSERT_EQUAL_UINT32(2, r.n_by_type[SES_T_FIX_DELTA]);     /* f1, f2 */
    TEST_ASSERT_EQUAL_UINT32(2, r.n_by_type[SES_T_FUSED]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_LAP]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_SECTOR]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_DRAG_RUN]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_DRAG_GATE]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_EVENT]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_CALIB]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_MARK]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_POWER]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_END]);

    TEST_ASSERT_EQUAL_INT(1, c.n_hdr);
    TEST_ASSERT_EQUAL_MEMORY(&h, &c.hdr, sizeof h);
    TEST_ASSERT_EQUAL_STRING("S00042_007", c.hdr.session_id);
    TEST_ASSERT_EQUAL_STRING("v0.3.1-abcdefghi", c.hdr.fw);

    TEST_ASSERT_EQUAL_INT(1, c.n_venue);
    TEST_ASSERT_EQUAL_UINT16(1000, c.venue.venue_id);
    TEST_ASSERT_EQUAL_UINT16(3, c.venue.layout_id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", c.venue.name);

    TEST_ASSERT_EQUAL_INT(1, c.n_tm);
    TEST_ASSERT_EQUAL_INT64(1000000, c.tm.mono_us);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US, c.tm.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, c.tm.quality);

    TEST_ASSERT_EQUAL_INT(3, c.n_fix);
    const gps_fix_t *src[3] = { &f0, &f1, &f2 };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT64(src[i]->gps_us, c.fix[i].gps_us);
        TEST_ASSERT_EQUAL_INT32(src[i]->lat_e7, c.fix[i].lat_e7);
        TEST_ASSERT_EQUAL_INT32(src[i]->lon_e7, c.fix[i].lon_e7);
        TEST_ASSERT_INT32_WITHIN(FIX_ALT_TOL_MM, src[i]->alt_mm, c.fix[i].alt_mm);
        TEST_ASSERT_INT32_WITHIN(FIX_SPEED_TOL_MMS, src[i]->gspeed_mms, c.fix[i].gspeed_mms);
        TEST_ASSERT_INT32_WITHIN(FIX_HEAD_TOL_E5, src[i]->head_e5, c.fix[i].head_e5);
        TEST_ASSERT_INT32_WITHIN(FIX_HACC_TOL_MM, (int32_t)src[i]->hacc_mm, (int32_t)c.fix[i].hacc_mm);
        TEST_ASSERT_EQUAL_UINT8(src[i]->sats, c.fix[i].sats);
        TEST_ASSERT_EQUAL_UINT8(src[i]->valid, c.fix[i].valid);
        TEST_ASSERT_EQUAL_UINT8(3, c.fix[i].fix_type);
    }

    TEST_ASSERT_EQUAL_INT(2, c.n_fused);
    TEST_ASSERT_EQUAL_INT64(u0.gps_us, c.fused[0].gps_us);
    TEST_ASSERT_EQUAL_INT64(u1.gps_us, c.fused[1].gps_us);
    /* FUSED quantisation (§12.3): g in 1e-3, lean and yaw in 1e-2 deg. */
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u0.g_lat, c.fused[0].g_lat);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u0.g_lon, c.fused[0].g_lon);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, u0.lean_deg, c.fused[0].lean_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, u0.yaw_dps, c.fused[0].yaw_dps);
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID | FUS_ORIENT_OK, c.fused[0].flags);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u1.g_lat, c.fused[1].g_lat);

    TEST_ASSERT_EQUAL_INT(1, c.n_lap);
    TEST_ASSERT_EQUAL_MEMORY(&lap, &c.lap, sizeof lap);
    TEST_ASSERT_EQUAL_INT(1, c.n_sec);
    TEST_ASSERT_EQUAL_UINT16(3, c.sec.lap_no);
    TEST_ASSERT_EQUAL_UINT8(1, c.sec.idx);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 30100000, c.sec.gps_us);
    TEST_ASSERT_EQUAL_UINT32(30100, c.sec.split_ms);
    TEST_ASSERT_EQUAL_INT32(-210, c.sec.delta_ms);

    TEST_ASSERT_EQUAL_INT(1, c.n_run);
    TEST_ASSERT_EQUAL_MEMORY(&run, &c.run, sizeof run);
    TEST_ASSERT_EQUAL_INT(1, c.n_dg);
    TEST_ASSERT_EQUAL_UINT16(2, c.dg.run_no);
    TEST_ASSERT_EQUAL_UINT8(3, c.dg.gate_id);
    TEST_ASSERT_EQUAL_UINT32(9200, c.dg.time_ms);
    TEST_ASSERT_EQUAL_UINT16(5000, c.dg.speed_cms);
    TEST_ASSERT_EQUAL_UINT32(25000, c.dg.dist_cm);

    TEST_ASSERT_EQUAL_INT(1, c.n_ev);
    TEST_ASSERT_EQUAL_INT64(1234, c.ev.mono_us);
    TEST_ASSERT_EQUAL_HEX16(0x0101, c.ev.code);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, c.ev.arg);

    TEST_ASSERT_EQUAL_INT(1, c.n_cal);
    for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(cal.r_e4[i], c.cal.r_e4[i]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT16(cal.gbias[i], c.cal.gbias[i]);
    TEST_ASSERT_EQUAL_HEX8(cal.calib_flags, c.cal.calib_flags);

    TEST_ASSERT_EQUAL_INT(1, c.n_mk);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 7, c.mk.gps_us);
    TEST_ASSERT_EQUAL_UINT8(1, c.mk.kind);
    TEST_ASSERT_EQUAL_INT(1, c.n_pw);
    TEST_ASSERT_EQUAL_INT64(4242, c.pw.mono_us);
    TEST_ASSERT_EQUAL_UINT8(3, c.pw.state);
    TEST_ASSERT_EQUAL_UINT16(3900, c.pw.batt_mv);
    TEST_ASSERT_EQUAL_INT(1, c.n_end);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 20000000, c.end.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, c.end.reason);
    TEST_ASSERT_EQUAL_INT(0, c.n_bad_cb);
}

static void test_keyframe_cadence_and_forced_key_after_invalid(void)
{
    uint8_t mem[8192];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    for (int i = 0; i < 100; i++) {                       /* 100 fixes at 5 Hz = 19.8 s of span */
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
    }
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(100, r.n_frames);
    /* §12.4: FIX_KEY for the first fix and every FIX_KEYFRAME_S = 5 s. Keys land at t = 0, 5, 10,
     * 15 s (fix 0, 25, 50, 75); t = 20 s would be fix 100, one past the end. */
    TEST_ASSERT_EQUAL_UINT32(4, r.n_by_type[SES_T_FIX_KEY]);
    TEST_ASSERT_EQUAL_UINT32(96, r.n_by_type[SES_T_FIX_DELTA]);
    TEST_ASSERT_EQUAL_INT(100, c.n_fix);

    /* An invalid fix forces the next fix to be a key (§12.4). */
    logw_t w2; logw_open_mem(&w2, mem, sizeof mem);
    gps_fix_t a = mk_fix(0, 1);
    size_t at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &a));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, mem[at + 1]);           /* first fix */
    gps_fix_t b = mk_fix(1, 1);
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &b));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, mem[at + 1]);
    gps_fix_t bad = mk_fix(2, 0);                                  /* invalid, still logged */
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &bad));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, mem[at + 1]);
    gps_fix_t after = mk_fix(3, 1);
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &after));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, mem[at + 1]);
}

static void test_fused_and_fix_states_stay_synchronised(void)
{
    uint8_t mem[4096];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    int64_t want[12]; int n_want = 0;
    for (int i = 0; i < 6; i++) {
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
        for (int k = 0; k < 2; k++) {                    /* 10 Hz fused between 5 Hz fixes */
            int64_t t = f.gps_us + (k + 1) * 50000;
            fused_sample_t u = mk_fused(t, 0.1f * (float)k);
            TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u));
            want[n_want++] = t;
        }
    }
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_INT(6, c.n_fix);
    TEST_ASSERT_EQUAL_INT(12, c.n_fused);
    /* Exact only if both sides call ses_fused_state_on_fix on every FIX_* record (§12.4). */
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_INT64(want[i], c.fused[i].gps_us);
}

static void test_memory_cap_makes_writes_fail_stickily(void)
{
    uint8_t mem[64];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    gps_fix_t f0 = mk_fix(0, 1);
    /* FIX_KEY is 39 + SES_FRAME_OVERHEAD = 44 bytes; a second key does not fit in 64. */
    TEST_ASSERT_EQUAL_INT(39 + SES_FRAME_OVERHEAD, logw_fix(&w, &f0));
    gps_fix_t f1 = mk_fix(0, 1);
    f1.gps_us = f0.gps_us + 6000000;                     /* > FIX_KEYFRAME_S, so this one is a key too */
    TEST_ASSERT_EQUAL_INT(-1, logw_fix(&w, &f1));
    TEST_ASSERT_EQUAL_INT(-1, w.err);
    TEST_ASSERT_EQUAL_UINT32(1, w.frames);
    size_t len_after_failure = w.len;
    TEST_ASSERT_EQUAL_INT(-1, logw_end(&w, f0.gps_us, 0));
    TEST_ASSERT_EQUAL_INT(-1, logw_mark(&w, f0.gps_us, 1));
    TEST_ASSERT_EQUAL_UINT32(1, w.frames);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)len_after_failure, (uint64_t)w.len);
    TEST_ASSERT_EQUAL_INT(-1, logw_close(&w));
}

static void test_file_round_trip_and_missing_file(void)
{
    make_tmp();
    logw_t w;
    TEST_ASSERT_EQUAL_INT(0, logw_open_file(&w, tmp_path));

    ses_hdr_t h; fill_hdr(&h);
    lap_result_t lap; fill_lap(&lap);
    drag_result_t run; fill_run(&run);
    ses_calib_t cal; fill_calib(&cal);
    gps_fix_t f0 = mk_fix(0, 1), f1 = mk_fix(1, 1), f2 = mk_fix(2, 1);
    fused_sample_t u0 = mk_fused(f0.gps_us + 50000, 0.4f);
    fused_sample_t u1 = mk_fused(f0.gps_us + 150000, -0.6f);
    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 3, "Synthetic"));
    TEST_ASSERT_GREATER_THAN(0, logw_time_map(&w, 1000000, T0_GPS_US, 2));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f2));
    TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &lap));
    TEST_ASSERT_GREATER_THAN(0, logw_sector(&w, 3, 1, T0_GPS_US + 30100000, 30100, -210));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_run(&w, &run));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_gate(&w, 2, 3, T0_GPS_US + 9200000, 9200, 5000, 25000));
    TEST_ASSERT_GREATER_THAN(0, logw_event(&w, 1234, T0_GPS_US, 0x0101, 0xDEADBEEF));
    TEST_ASSERT_GREATER_THAN(0, logw_calib(&w, &cal));
    TEST_ASSERT_GREATER_THAN(0, logw_mark(&w, T0_GPS_US + 7, 1));
    TEST_ASSERT_GREATER_THAN(0, logw_power(&w, 4242, 3, 3900));
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 20000000, 2));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(17, w.frames);

    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    TEST_ASSERT_EQUAL_INT(0, logr_read_file(&r, tmp_path));
    TEST_ASSERT_EQUAL_UINT32(17, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_INT(1, c.n_hdr);
    TEST_ASSERT_EQUAL_MEMORY(&h, &c.hdr, sizeof h);
    TEST_ASSERT_EQUAL_INT(3, c.n_fix);
    TEST_ASSERT_EQUAL_INT64(f2.gps_us, c.fix[2].gps_us);
    TEST_ASSERT_EQUAL_INT32(f2.lat_e7, c.fix[2].lat_e7);
    TEST_ASSERT_EQUAL_INT(2, c.n_fused);
    TEST_ASSERT_EQUAL_INT64(u1.gps_us, c.fused[1].gps_us);
    TEST_ASSERT_EQUAL_INT(1, c.n_lap);
    TEST_ASSERT_EQUAL_MEMORY(&lap, &c.lap, sizeof lap);
    TEST_ASSERT_EQUAL_INT(1, c.n_run);
    TEST_ASSERT_EQUAL_MEMORY(&run, &c.run, sizeof run);
    TEST_ASSERT_EQUAL_INT(1, c.n_end);

    col_t c2; memset(&c2, 0, sizeof c2);
    logr_t r2; logr_init(&r2, &ALL_CB, &c2);
    TEST_ASSERT_EQUAL_INT(-1, logr_read_file(&r2, "/tmp/laptimer_logio_does_not_exist"));
}

/* Writes n fixes at 5 Hz into mem, recording each frame's offset. Returns the byte length. */
static size_t write_fix_stream(uint8_t *mem, size_t cap, int n, size_t *off)
{
    logw_t w; logw_open_mem(&w, mem, cap);
    for (int i = 0; i < n; i++) {
        off[i] = w.len;
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
    }
    return w.len;
}

static void test_truncated_tail_is_one_bad_frame(void)
{
    uint8_t mem[4096]; size_t off[50];
    size_t len = write_fix_stream(mem, sizeof mem, 50, off);
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, len - 3);                          /* cut the last frame's flags byte and CRC */
    logr_finish(&r);
    TEST_ASSERT_EQUAL_INT(49, c.n_fix);
    TEST_ASSERT_EQUAL_UINT32(49, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(1, r.rd.frames_bad);         /* the flush treats the stub as one bad frame */
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);                 /* no decoder rejected anything */
    TEST_ASSERT_EQUAL_INT(0, c.n_bad_cb);
}

static void test_corrupted_delta_drops_one_frame_until_the_next_key(void)
{
    uint8_t mem[4096]; size_t off[50];
    size_t len = write_fix_stream(mem, sizeof mem, 50, off);
    mem[off[9] + 3] ^= 0xFF;                              /* flip the first payload byte of frame 10 */
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(49, r.n_frames);             /* the bad CRC costs exactly that frame */
    TEST_ASSERT_EQUAL_UINT32(1, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_INT(49, c.n_fix);
    /* §12.4 guarantees only that a FIX_KEY is written for the first fix and every FIX_KEYFRAME_S
     * seconds, so a lost FIX_DELTA leaves the reconstruction one delta behind until the next KEY.
     * Delivery 9 is the record of fix 10 applied to the state of fix 8, i.e. it reads as fix 9. */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 9 * 200000, c.fix[9].gps_us);
    TEST_ASSERT_EQUAL_INT32(-338567000 + 9 * 500, c.fix[9].lat_e7);
    /* The next key is fix 25 (t = 5 s), delivered at index 24 because one frame was dropped. */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 25 * 200000, c.fix[24].gps_us);
    TEST_ASSERT_EQUAL_INT32(-338567000 + 25 * 500, c.fix[24].lat_e7);
    /* Everything after the key is exact again, up to the last fix. */
    for (int j = 24; j < 49; j++) {
        int i = j + 1;
        TEST_ASSERT_EQUAL_INT64(T0_GPS_US + (int64_t)i * 200000, c.fix[j].gps_us);
        TEST_ASSERT_EQUAL_INT32(-338567000 + i * 500, c.fix[j].lat_e7);
    }
}

static void test_unknown_record_type_is_reported_bad(void)
{
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t frame[32];
    int n = ses_frame_encode(0x5A, payload, (uint8_t)sizeof payload, frame, sizeof frame);
    TEST_ASSERT_EQUAL_INT((int)sizeof payload + SES_FRAME_OVERHEAD, n);
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, frame, (size_t)n);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_frames);              /* the framing layer accepted it */
    TEST_ASSERT_EQUAL_UINT32(1, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_by_type[0x5A]);
    TEST_ASSERT_EQUAL_INT(1, c.n_bad_cb);
    TEST_ASSERT_EQUAL_HEX8(0x5A, c.bad_type);
    TEST_ASSERT_EQUAL_UINT8(4, c.bad_len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_memory_round_trip_of_every_record_type);
    RUN_TEST(test_keyframe_cadence_and_forced_key_after_invalid);
    RUN_TEST(test_fused_and_fix_states_stay_synchronised);
    RUN_TEST(test_memory_cap_makes_writes_fail_stickily);
    RUN_TEST(test_file_round_trip_and_missing_file);
    RUN_TEST(test_truncated_tail_is_one_bad_frame);
    RUN_TEST(test_corrupted_delta_drops_one_frame_until_the_next_key);
    RUN_TEST(test_unknown_record_type_is_reported_bad);
    return UNITY_END();
}
