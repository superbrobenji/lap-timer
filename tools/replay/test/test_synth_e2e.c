#include "unity.h"
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/synth_truth.h"
#include "replay/logio.h"
#include "replay/replay.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/json.h"
#include "core/tb.h"
#include "core/trk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_CAP        (4u * 1024u * 1024u)   /* 266 s at 5 Hz plus 10 Hz FUSED is about 30 kB */
#define CAP_MAX_FIX    20000
#define TEST_LAPS      3
#define FIX_US_5HZ     200000                 /* 1e6 / 5 Hz */
#define DROP_START_S   30.0
#define DROP_END_S     40.0
/* FIX_DELTA quantisation (spec 12.3): alt 1 dm, speed 1 cm/s, heading 1e-2 deg, hacc 1 dm. The
 * decoded value is the truth rounded to that step, so it differs by at most one whole step. */
#define Q_ALT_MM       100
#define Q_SPEED_MMS    10
#define Q_HEAD_E5      1000
#define Q_HACC_MM      100
/* Crossing tolerance for straight-line interpolation between raw fixes: one sample interval plus
 * the 1.5 m position noise at the ~30 m/s the default circuit runs. The lap engine's
 * constant-acceleration interpolation (session 2.4) must do far better - spec 22.2 asks for 30 ms
 * at 95 % at 5 Hz - so this only proves the fixture itself is self-consistent. */
#define SF_TOL_US_5HZ  250000
#define SF_TOL_US_10HZ 150000
/* Truth times are written with 6 decimals, so one value carries at most 5e-7 of rounding. */
#define TRUTH_TOL_S    1e-6
/* Summing 3 sector times and comparing with the lap time accumulates 4 such roundings. */
#define SUM_TOL_S      2e-6
#define JSON_MAX_TOKS  4096

typedef struct {
    gps_fix_t   fix[CAP_MAX_FIX];
    uint32_t    n_fix, n_fused, n_time_map;
    ses_hdr_t   hdr;   int have_hdr;
    ses_venue_t venue; int have_venue;
    ses_end_t   end;   int have_end;
} cap_t;

static uint8_t     *g_buf1, *g_buf2;
static synth_run_t *g_run;
static cap_t       *g_cap;

void setUp(void)
{
    g_buf1 = (uint8_t *)malloc(LOG_CAP);
    g_buf2 = (uint8_t *)malloc(LOG_CAP);
    g_run  = (synth_run_t *)malloc(sizeof *g_run);
    g_cap  = (cap_t *)malloc(sizeof *g_cap);
    TEST_ASSERT_NOT_NULL(g_buf1); TEST_ASSERT_NOT_NULL(g_buf2);
    TEST_ASSERT_NOT_NULL(g_run);  TEST_ASSERT_NOT_NULL(g_cap);
}

void tearDown(void)
{
    free(g_buf1); free(g_buf2); free(g_run); free(g_cap);
    g_buf1 = NULL; g_buf2 = NULL; g_run = NULL; g_cap = NULL;
}

/* ---------------- helpers ---------------- */

static void cb_hdr(const ses_hdr_t *h, void *ctx)        { cap_t *c = (cap_t *)ctx; c->hdr = *h; c->have_hdr = 1; }
static void cb_venue(const ses_venue_t *v, void *ctx)    { cap_t *c = (cap_t *)ctx; c->venue = *v; c->have_venue = 1; }
static void cb_time_map(const ses_time_map_t *t, void *ctx) { (void)t; ((cap_t *)ctx)->n_time_map++; }
static void cb_fused(const fused_sample_t *f, void *ctx) { (void)f; ((cap_t *)ctx)->n_fused++; }
static void cb_end(const ses_end_t *e, void *ctx)        { cap_t *c = (cap_t *)ctx; c->end = *e; c->have_end = 1; }
static void cb_fix(const gps_fix_t *f, void *ctx)
{
    cap_t *c = (cap_t *)ctx;
    if (c->n_fix < CAP_MAX_FIX) c->fix[c->n_fix] = *f;
    c->n_fix++;
}

static const logr_cb_t g_cb = {
    cb_hdr, cb_venue, cb_time_map, cb_fix, cb_fused,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, cb_end, NULL
};

/* Generates one whole session into buf and returns the configuration it used. */
static size_t gen(uint8_t *buf, int laps, int rate, int fused_hz, double drop0, double drop1,
                  synth_cfg_t *cfg, synth_gps_cfg_t *gps, uint32_t *n_fix, uint32_t *n_fused)
{
    synth_cfg_defaults(cfg);
    cfg->laps = laps;
    synth_gps_cfg_defaults(gps);
    gps->rate_hz = rate;
    gps->dropout_start_s = drop0;
    gps->dropout_end_s = drop1;
    logw_t w;
    logw_open_mem(&w, buf, LOG_CAP);
    TEST_ASSERT_EQUAL_INT(0, synth_generate(cfg, gps, fused_hz, &w, g_run, n_fix, n_fused));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    return w.len;
}

static void read_back(const uint8_t *buf, size_t len, logr_t *r)
{
    memset(g_cap, 0, sizeof *g_cap);
    logr_init(r, &g_cb, g_cap);
    logr_feed(r, buf, len);
    logr_finish(r);
    TEST_ASSERT_EQUAL_UINT32(0, r->n_bad);
    TEST_ASSERT_TRUE(g_cap->n_fix <= CAP_MAX_FIX);
}

/* S/F crossings recovered from the decoded fixes alone: consecutive fixes to ENU about the venue
 * origin, geo_segment_cross against the S/F line, linear interpolation of gps_us. */
static int sf_crossings_us(const synth_run_t *r, double *out, int out_cap)
{
    geo_origin_t o;
    geo_origin_set(&o, r->cfg.origin_lat_deg, r->cfg.origin_lon_deg);
    geo_enu_t p = geo_to_enu(&o, r->gates[0].line.p1.lat, r->gates[0].line.p1.lon);
    geo_enu_t q = geo_to_enu(&o, r->gates[0].line.p2.lat, r->gates[0].line.p2.lon);
    int n = 0;
    for (uint32_t i = 1; i < g_cap->n_fix; i++) {
        const gps_fix_t *fa = &g_cap->fix[i - 1], *fb = &g_cap->fix[i];
        geo_enu_t a = geo_to_enu(&o, (double)fa->lat_e7 / 1e7, (double)fa->lon_e7 / 1e7);
        geo_enu_t b = geo_to_enu(&o, (double)fb->lat_e7 / 1e7, (double)fb->lon_e7 / 1e7);
        double t = 0.0;
        int dir = 0;
        if (!geo_segment_cross(a, b, p, q, &t, &dir)) continue;
        TEST_ASSERT_EQUAL_INT(1, dir);                  /* p1 is the left end, so dir_sign is +1 */
        TEST_ASSERT_TRUE(n < out_cap);
        out[n++] = (double)fa->gps_us + t * (double)(fb->gps_us - fa->gps_us);
    }
    return n;
}

static void check_sf_crossings(int rate, int64_t tol_us)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t n_fix = 0, n_fused = 0;
    size_t len = gen(g_buf1, TEST_LAPS, rate, 10, 0.0, 0.0, &cfg, &gps, &n_fix, &n_fused);
    logr_t r;
    read_back(g_buf1, len, &r);

    double got[TEST_LAPS + 2];
    int n = sf_crossings_us(g_run, got, (int)(sizeof got / sizeof got[0]));
    TEST_ASSERT_EQUAL_INT(TEST_LAPS + 1, n);
    for (int i = 0; i < n; i++) {
        double truth_s = synth_run_time_at_s(g_run, synth_run_sf_s_total(g_run, i));
        double want = (double)gps.t0_gps_us + truth_s * 1e6;
        TEST_ASSERT_DOUBLE_WITHIN((double)tol_us, want, got[i]);
    }
}

/* Reads a whole tmpfile back into a NUL-terminated heap buffer. */
static char *slurp(FILE *f, size_t *len_out)
{
    TEST_ASSERT_EQUAL_INT(0, fflush(f));
    long n = ftell(f);
    TEST_ASSERT_TRUE(n > 0);
    rewind(f);
    char *b = (char *)malloc((size_t)n + 1);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_size_t((size_t)n, fread(b, 1, (size_t)n, f));
    b[n] = '\0';
    *len_out = (size_t)n;
    return b;
}

/* ---------------- tests ---------------- */

static void test_generate_is_deterministic_and_reads_back(void)
{
    synth_cfg_t cfg, cfg2;
    synth_gps_cfg_t gps, gps2;
    uint32_t nf1 = 0, nu1 = 0, nf2 = 0, nu2 = 0;
    size_t len1 = gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &nf1, &nu1);
    size_t len2 = gen(g_buf2, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg2, &gps2, &nf2, &nu2);
    TEST_ASSERT_EQUAL_size_t(len1, len2);
    TEST_ASSERT_EQUAL_UINT32(nf1, nf2);
    TEST_ASSERT_EQUAL_UINT32(nu1, nu2);
    TEST_ASSERT_EQUAL_INT(0, memcmp(g_buf1, g_buf2, len1));      /* same seeds, same bytes */

    logr_t r;
    read_back(g_buf1, len1, &r);

    char fw[17];
    snprintf(fw, sizeof fw, "%s", replay_version());             /* the wire field is 16 bytes */
    TEST_ASSERT_TRUE(g_cap->have_hdr);
    TEST_ASSERT_EQUAL_STRING("S00001_001", g_cap->hdr.session_id);
    TEST_ASSERT_EQUAL_STRING(fw, g_cap->hdr.fw);
    TEST_ASSERT_EQUAL_STRING("synth", g_cap->hdr.hwid);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.mode);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.variant);
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, g_cap->hdr.venue_id);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, g_cap->hdr.layout_id);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.log_profile);
    TEST_ASSERT_EQUAL_UINT8(10, g_cap->hdr.fused_hz);
    TEST_ASSERT_EQUAL_UINT8(5, g_cap->hdr.gps_hz);
    TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us, g_cap->hdr.start_gps_us);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[0]);          /* identity x 1e4 */
    TEST_ASSERT_EQUAL_INT16(0, g_cap->hdr.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[4]);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[8]);
    TEST_ASSERT_EQUAL_UINT8(0x03, g_cap->hdr.calib_flags);

    TEST_ASSERT_TRUE(g_cap->have_venue);
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, g_cap->venue.venue_id);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, g_cap->venue.layout_id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", g_cap->venue.name);

    TEST_ASSERT_EQUAL_UINT32(nf1, g_cap->n_fix);
    TEST_ASSERT_EQUAL_UINT32(nu1, g_cap->n_fused);
    for (uint32_t i = 0; i < g_cap->n_fix; i++)                  /* no dropout: k = 0, 1, 2, ... */
        TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us + (int64_t)i * FIX_US_5HZ, g_cap->fix[i].gps_us);

    /* The decoded stream must agree with a fresh sampler on the same run and seeds, to within the
     * FIX_DELTA quantisation. */
    synth_gps_t s;
    synth_gps_init(&s, &gps);
    uint32_t idx = 0;
    for (;;) {
        gps_fix_t f;
        int rc = synth_gps_next(&s, g_run, &f, NULL);
        if (rc < 0) break;
        if (rc == 0) continue;
        TEST_ASSERT_TRUE(idx < g_cap->n_fix);
        const gps_fix_t *d = &g_cap->fix[idx];
        TEST_ASSERT_EQUAL_INT64(f.gps_us, d->gps_us);
        TEST_ASSERT_EQUAL_INT32(f.lat_e7, d->lat_e7);
        TEST_ASSERT_EQUAL_INT32(f.lon_e7, d->lon_e7);
        TEST_ASSERT_INT32_WITHIN(Q_ALT_MM, f.alt_mm, d->alt_mm);
        TEST_ASSERT_INT32_WITHIN(Q_SPEED_MMS, f.gspeed_mms, d->gspeed_mms);
        TEST_ASSERT_INT32_WITHIN(Q_HEAD_E5, f.head_e5, d->head_e5);
        TEST_ASSERT_INT32_WITHIN(Q_HACC_MM, (int32_t)f.hacc_mm, (int32_t)d->hacc_mm);
        TEST_ASSERT_EQUAL_UINT8(1, d->valid);
        idx++;
    }
    TEST_ASSERT_EQUAL_UINT32(g_cap->n_fix, idx);

    /* TIME_MAP at run start and every 60 s (spec 12.4). */
    TEST_ASSERT_EQUAL_UINT32(1u + (uint32_t)floor(g_run->duration_s / 60.0), g_cap->n_time_map);

    TEST_ASSERT_TRUE(g_cap->have_end);
    TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us + (int64_t)llround(g_run->duration_s * 1e6), g_cap->end.gps_us);
}

static void test_sf_crossings_at_5hz(void)  { check_sf_crossings(5, SF_TOL_US_5HZ); }
static void test_sf_crossings_at_10hz(void) { check_sf_crossings(10, SF_TOL_US_10HZ); }

/* Records the type of the first FIX_* frame at or after the end of the dropout window. */
typedef struct { ses_fix_state_t st; int64_t hi; uint8_t first_type; int64_t first_us; } scan_t;

static void scan_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    scan_t *s = (scan_t *)ctx;
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return;
    gps_fix_t f;
    if (ses_decode_fix(&s->st, type, payload, len, &f) != 1) return;
    if (s->first_type == 0 && f.gps_us >= s->hi) { s->first_type = type; s->first_us = f.gps_us; }
}

static void test_dropout_window_is_empty_and_resumes_with_a_keyframe(void)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t nf0 = 0, nu0 = 0, nf1 = 0, nu1 = 0;
    size_t len0 = gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &nf0, &nu0);
    (void)len0;
    size_t len1 = gen(g_buf2, TEST_LAPS, 5, 10, DROP_START_S, DROP_END_S, &cfg, &gps, &nf1, &nu1);

    /* 10 s of a 5 Hz stream: exactly 50 slots are skipped. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)((DROP_END_S - DROP_START_S) * 5.0), nf0 - nf1);

    logr_t r;
    read_back(g_buf2, len1, &r);
    TEST_ASSERT_EQUAL_UINT32(nf1, g_cap->n_fix);
    int64_t lo = gps.t0_gps_us + (int64_t)llround(DROP_START_S * 1e6);
    int64_t hi = gps.t0_gps_us + (int64_t)llround(DROP_END_S * 1e6);
    int resumed = 0;
    for (uint32_t i = 0; i < g_cap->n_fix; i++) {
        int64_t t = g_cap->fix[i].gps_us;
        TEST_ASSERT_TRUE(t < lo || t >= hi);
        if (t == hi) resumed = 1;
    }
    TEST_ASSERT_TRUE(resumed);              /* the run continues exactly at the window's end */

    /* The 10 s gap is past FIX_KEYFRAME_S, so the resuming fix must be a keyframe (spec 12.4). */
    scan_t sc;
    memset(&sc, 0, sizeof sc);
    ses_fix_state_init(&sc.st);
    sc.hi = hi;
    ses_reader_t rd;
    ses_reader_init(&rd);
    ses_reader_feed(&rd, g_buf2, len1, scan_cb, &sc);
    ses_reader_flush(&rd, scan_cb, &sc);
    TEST_ASSERT_EQUAL_UINT8(SES_T_FIX_KEY, sc.first_type);
    TEST_ASSERT_EQUAL_INT64(hi, sc.first_us);
}

static void test_truth_json_matches_the_analytic_lap_times(void)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t n_fix = 0, n_fused = 0;
    (void)gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &n_fix, &n_fused);

    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, synth_truth_write(f, g_run, &gps, n_fix, n_fused));
    size_t n = 0;
    char *js = slurp(f, &n);
    fclose(f);

    jsmntok_t *toks = (jsmntok_t *)malloc(sizeof(jsmntok_t) * JSON_MAX_TOKS);
    TEST_ASSERT_NOT_NULL(toks);
    int ntoks = json_parse(js, n, toks, JSON_MAX_TOKS);
    TEST_ASSERT_TRUE(ntoks > 0);

    int gates = json_obj_get(js, toks, ntoks, 0, "gates");
    TEST_ASSERT_TRUE(gates > 0);
    TEST_ASSERT_EQUAL_INT(g_run->n_gates, toks[gates].size);
    TEST_ASSERT_EQUAL_INT(3, toks[gates].size);                  /* S/F plus the two default sectors */

    int laps = json_obj_get(js, toks, ntoks, 0, "laps");
    TEST_ASSERT_TRUE(laps > 0);
    TEST_ASSERT_EQUAL_INT(TEST_LAPS, toks[laps].size);

    int e = laps + 1;
    for (int lap = 1; lap <= TEST_LAPS; lap++) {
        int no = json_obj_get(js, toks, ntoks, e, "lap_no");
        int64_t lap_no = 0;
        TEST_ASSERT_TRUE(no > 0 && json_tok_int(js, &toks[no], &lap_no));
        TEST_ASSERT_EQUAL_INT64(lap, lap_no);

        int lt = json_obj_get(js, toks, ntoks, e, "lap_time_s");
        double lap_time = 0.0;
        TEST_ASSERT_TRUE(lt > 0 && json_tok_double(js, &toks[lt], &lap_time));
        TEST_ASSERT_DOUBLE_WITHIN(TRUTH_TOL_S, synth_run_lap_time(g_run, lap), lap_time);

        int st = json_obj_get(js, toks, ntoks, e, "sector_times_s");
        TEST_ASSERT_TRUE(st > 0);
        TEST_ASSERT_EQUAL_INT(g_run->n_gates, toks[st].size);
        double sum = 0.0;
        for (int i = 0; i < toks[st].size; i++) {
            double v = 0.0;
            TEST_ASSERT_TRUE(json_tok_double(js, &toks[st + 1 + i], &v));
            sum += v;
        }
        TEST_ASSERT_DOUBLE_WITHIN(SUM_TOL_S, lap_time, sum);
        e = json_skip(toks, ntoks, e);
    }
    free(toks);
    free(js);
}

static void test_venue_json_loads_and_validates(void)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = TEST_LAPS;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(g_run, &cfg, NULL, 0));

    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, synth_venue_write(f, g_run));
    size_t n = 0;
    char *js = slurp(f, &n);
    fclose(f);

    trk_venue_t v;
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v, js, n, err, sizeof err));
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(&v));
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, v.id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", v.name);
    TEST_ASSERT_EQUAL_UINT8(1, v.n_layouts);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, v.layouts[0].id);
    TEST_ASSERT_EQUAL_INT8(1, v.layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_UINT8(2, v.layouts[0].n_sectors);
    free(js);
}

static void test_parse_args_defaults_and_round_trip(void)
{
    synth_cfg_t c, d;
    synth_gps_cfg_t g, dg;
    int fh = 0;
    char out[64];
    synth_cfg_defaults(&d);
    synth_gps_cfg_defaults(&dg);

    char *a1[] = { "synth", "--out", "x" };
    TEST_ASSERT_EQUAL_INT(0, synth_parse_args(3, a1, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("x", out);
    TEST_ASSERT_EQUAL_INT(SYNTH_FUSED_HZ, fh);
    TEST_ASSERT_EQUAL_INT(d.n_vertices, c.n_vertices);
    TEST_ASSERT_EQUAL_DOUBLE(d.length_m, c.length_m);
    TEST_ASSERT_EQUAL_INT(d.laps, c.laps);
    TEST_ASSERT_TRUE(c.clockwise);
    TEST_ASSERT_EQUAL_INT(dg.rate_hz, g.rate_hz);
    TEST_ASSERT_EQUAL_DOUBLE(dg.pos_sigma_m, g.pos_sigma_m);
    TEST_ASSERT_EQUAL_INT64(dg.t0_gps_us, g.t0_gps_us);

    char *a2[] = { "synth", "--out", "prefix",
                   "--vertices", "8", "--length", "3000", "--radius", "50",
                   "--irregularity", "0.25", "--seed", "7",
                   "--v-corner", "18", "--v-max", "45", "--a-acc", "4", "--a-brk", "7.5",
                   "--lap-var", "0.1", "--laps", "5", "--sectors", "3",
                   "--start-before", "150", "--stop-after", "250", "--sf-frac", "0.25",
                   "--rate", "10", "--pos-sigma", "2.5", "--pos-tau", "30",
                   "--speed-sigma", "0.1", "--head-sigma", "1.25",
                   "--latency", "90", "--jitter", "15", "--fused-hz", "25",
                   "--dropout", "30:40", "--start-utc", "2026-01-02T03:04:05",
                   "--anticlockwise", "--quiet" };
    TEST_ASSERT_EQUAL_INT(SYNTH_ARG_QUIET,
                          synth_parse_args((int)(sizeof a2 / sizeof a2[0]), a2, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("prefix", out);
    TEST_ASSERT_EQUAL_INT(8, c.n_vertices);
    TEST_ASSERT_EQUAL_DOUBLE(3000.0, c.length_m);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, c.corner_radius_m);
    TEST_ASSERT_EQUAL_DOUBLE(0.25, c.irregularity);
    TEST_ASSERT_EQUAL_UINT32(7, c.seed);
    TEST_ASSERT_EQUAL_UINT32(7, g.seed);                      /* one seed drives both models */
    TEST_ASSERT_EQUAL_DOUBLE(18.0, c.v_corner_mps);
    TEST_ASSERT_EQUAL_DOUBLE(45.0, c.v_max_mps);
    TEST_ASSERT_EQUAL_DOUBLE(4.0, c.a_acc_mps2);
    TEST_ASSERT_EQUAL_DOUBLE(7.5, c.a_brk_mps2);
    TEST_ASSERT_EQUAL_DOUBLE(0.1, c.lap_var);
    TEST_ASSERT_EQUAL_INT(5, c.laps);
    TEST_ASSERT_EQUAL_INT(3, c.n_sector_gates);
    TEST_ASSERT_EQUAL_DOUBLE(150.0, c.start_before_m);
    TEST_ASSERT_EQUAL_DOUBLE(250.0, c.stop_after_m);
    TEST_ASSERT_EQUAL_DOUBLE(0.25, c.sf_frac);
    TEST_ASSERT_FALSE(c.clockwise);
    TEST_ASSERT_EQUAL_INT(10, g.rate_hz);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, g.pos_sigma_m);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, g.pos_tau_s);
    TEST_ASSERT_EQUAL_DOUBLE(0.1, g.speed_sigma_mps);
    TEST_ASSERT_EQUAL_DOUBLE(1.25, g.head_sigma_deg);
    TEST_ASSERT_EQUAL_DOUBLE(90.0, g.latency_ms);
    TEST_ASSERT_EQUAL_DOUBLE(15.0, g.jitter_ms);
    TEST_ASSERT_EQUAL_INT(25, fh);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, g.dropout_start_s);
    TEST_ASSERT_EQUAL_DOUBLE(40.0, g.dropout_end_s);
    TEST_ASSERT_EQUAL_INT64(tb_gps_us_from_utc(2026, 1, 2, 3, 4, 5, 0), g.t0_gps_us);

    char *help[] = { "synth", "--help" };
    TEST_ASSERT_EQUAL_INT(SYNTH_ARG_HELP, synth_parse_args(2, help, &c, &g, &fh, out, sizeof out));
}

static void test_parse_args_rejects_bad_input(void)
{
    synth_cfg_t c;
    synth_gps_cfg_t g;
    int fh = 0;
    char out[64];
    char *no_out[]   = { "synth", "--laps", "3" };
    char *bad_laps[] = { "synth", "--out", "p", "--laps", "0" };
    char *bad_rate[] = { "synth", "--out", "p", "--rate", "7" };
    char *bad_num[]  = { "synth", "--out", "p", "--length", "abc" };
    char *trail[]    = { "synth", "--out", "p", "--length", "3000x" };
    char *unknown[]  = { "synth", "--out", "p", "--bogus", "1" };
    char *no_value[] = { "synth", "--out", "p", "--laps" };
    char *bad_utc[]  = { "synth", "--out", "p", "--start-utc", "2026-1-2T3:4:5" };
    char *bad_drop[] = { "synth", "--out", "p", "--dropout", "40:30" };
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(3, no_out,   &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_laps, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_rate, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_num,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, trail,    &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, unknown,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(4, no_value, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_utc,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_drop, &c, &g, &fh, out, sizeof out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generate_is_deterministic_and_reads_back);
    RUN_TEST(test_sf_crossings_at_5hz);
    RUN_TEST(test_sf_crossings_at_10hz);
    RUN_TEST(test_dropout_window_is_empty_and_resumes_with_a_keyframe);
    RUN_TEST(test_truth_json_matches_the_analytic_lap_times);
    RUN_TEST(test_venue_json_loads_and_validates);
    RUN_TEST(test_parse_args_defaults_and_round_trip);
    RUN_TEST(test_parse_args_rejects_bad_input);
    return UNITY_END();
}
