#include "replay/synth_truth.h"
#include "replay/replay.h"
#include "core/jw.h"
#include "core/trk.h"
#include "core/tb.h"
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Truth JSON is built in one pass into this buffer; 200 laps × 9 gates is about 250 kB. */
#define TRUTH_BUF_BYTES   (1024u * 1024u)
/* trk_to_json of one venue with one layout and 8 sectors is under 2 kB. */
#define VENUE_BUF_BYTES   8192
#define DEC_M             6        /* decimals for metres, seconds and headings */
#define DEC_LL            9        /* decimals for latitude/longitude (≈ 0.1 mm) */
#define TIME_MAP_PERIOD_S 60.0     /* §12.4: TIME_MAP at session start and every 60 s */
/* k / rate_hz is exact for 5 and 10 Hz; the epsilon keeps a sample that lands on a 60 s boundary
 * from missing its TIME_MAP if a future rate makes the division inexact. */
#define TIME_EPS_S        1e-9
#define SYNTH_SESSION_ID  "S00001_001"   /* §12.1 S%05u_%03u; synth is always boot 1, sequence 1 */
#define SYNTH_HWID        "synth"
#define SYNTH_VENUE_NAME  "Synthetic"
#define CALIB_IDENT_E4    10000    /* identity rotation × 1e4 (§12.3 calib block) */
#define CALIB_FLAGS_OK    0x03     /* level and forward axis both "learned": truth needs no calibration */
#define TIME_MAP_QUALITY  1        /* min-filter lock; synth has no PPS (§6.2) */
#define END_REASON_NORMAL 0
#define DROPOUT_MAX_S     1.0e7    /* option bound; the run itself is never this long */

static int64_t gps_us_at(int64_t t0_gps_us, double t_s)
{
    return t0_gps_us + (int64_t)llround(t_s * 1e6);
}

/* ---------------- truth JSON ---------------- */

static void put_cfg(jw_t *w, const synth_cfg_t *c)
{
    jw_obj_open(w);
    jw_key(w, "n_vertices");        jw_int(w, c->n_vertices);
    jw_key(w, "length_m");          jw_double(w, c->length_m, DEC_M);
    jw_key(w, "corner_radius_m");   jw_double(w, c->corner_radius_m, DEC_M);
    jw_key(w, "irregularity");      jw_double(w, c->irregularity, DEC_M);
    jw_key(w, "clockwise");         jw_bool(w, c->clockwise);
    jw_key(w, "seed");              jw_uint(w, c->seed);
    jw_key(w, "v_corner_mps");      jw_double(w, c->v_corner_mps, DEC_M);
    jw_key(w, "v_max_mps");         jw_double(w, c->v_max_mps, DEC_M);
    jw_key(w, "a_acc_mps2");        jw_double(w, c->a_acc_mps2, DEC_M);
    jw_key(w, "a_brk_mps2");        jw_double(w, c->a_brk_mps2, DEC_M);
    jw_key(w, "lap_var");           jw_double(w, c->lap_var, DEC_M);
    jw_key(w, "laps");              jw_int(w, c->laps);
    jw_key(w, "start_before_m");    jw_double(w, c->start_before_m, DEC_M);
    jw_key(w, "stop_after_m");      jw_double(w, c->stop_after_m, DEC_M);
    jw_key(w, "sf_frac");           jw_double(w, c->sf_frac, DEC_M);
    jw_key(w, "n_sector_gates");    jw_int(w, c->n_sector_gates);
    jw_key(w, "gate_half_width_m"); jw_double(w, c->gate_half_width_m, DEC_M);
    jw_key(w, "origin_lat_deg");    jw_double(w, c->origin_lat_deg, DEC_LL);
    jw_key(w, "origin_lon_deg");    jw_double(w, c->origin_lon_deg, DEC_LL);
    jw_key(w, "venue_id");          jw_uint(w, c->venue_id);
    jw_obj_close(w);
}

static void put_gps_cfg(jw_t *w, const synth_gps_cfg_t *g)
{
    jw_obj_open(w);
    jw_key(w, "rate_hz");           jw_int(w, g->rate_hz);
    jw_key(w, "pos_sigma_m");       jw_double(w, g->pos_sigma_m, DEC_M);
    jw_key(w, "pos_tau_s");         jw_double(w, g->pos_tau_s, DEC_M);
    jw_key(w, "speed_sigma_mps");   jw_double(w, g->speed_sigma_mps, DEC_M);
    jw_key(w, "head_sigma_deg");    jw_double(w, g->head_sigma_deg, DEC_M);
    jw_key(w, "latency_ms");        jw_double(w, g->latency_ms, DEC_M);
    jw_key(w, "jitter_ms");         jw_double(w, g->jitter_ms, DEC_M);
    jw_key(w, "hacc_m");            jw_double(w, g->hacc_m, DEC_M);
    jw_key(w, "sats");              jw_uint(w, g->sats);
    jw_key(w, "pdop_e2");           jw_uint(w, g->pdop_e2);
    jw_key(w, "alt_mm");            jw_int(w, g->alt_mm);
    jw_key(w, "t0_gps_us");         jw_int(w, g->t0_gps_us);
    jw_key(w, "t0_mono_us");        jw_int(w, g->t0_mono_us);
    jw_key(w, "dropout_start_s");   jw_double(w, g->dropout_start_s, DEC_M);
    jw_key(w, "dropout_end_s");     jw_double(w, g->dropout_end_s, DEC_M);
    jw_key(w, "seed");              jw_uint(w, g->seed);
    jw_obj_close(w);
}

static void put_gates(jw_t *w, const synth_run_t *r)
{
    jw_arr_open(w);
    for (int i = 0; i < r->n_gates; i++) {
        const synth_gate_t *g = &r->gates[i];
        jw_obj_open(w);
        jw_key(w, "idx");         jw_int(w, i);
        jw_key(w, "kind");        jw_str(w, (i == 0) ? "sf" : "sector");
        jw_key(w, "s_m");         jw_double(w, g->s_m, DEC_M);
        jw_key(w, "e_m");         jw_double(w, g->e_m, DEC_M);
        jw_key(w, "n_m");         jw_double(w, g->n_m, DEC_M);
        jw_key(w, "heading_deg"); jw_double(w, g->heading_deg, DEC_M);
        jw_key(w, "p1"); jw_arr_open(w); jw_double(w, g->line.p1.lat, DEC_LL); jw_double(w, g->line.p1.lon, DEC_LL); jw_arr_close(w);
        jw_key(w, "p2"); jw_arr_open(w); jw_double(w, g->line.p2.lat, DEC_LL); jw_double(w, g->line.p2.lon, DEC_LL); jw_arr_close(w);
        jw_obj_close(w);
    }
    jw_arr_close(w);
}

int synth_truth_write(FILE *f, const synth_run_t *r, const synth_gps_cfg_t *g,
                      uint32_t fixes_written, uint32_t fused_written)
{
    if (!f || !r || !g) return -1;
    char *buf = (char *)malloc(TRUTH_BUF_BYTES);
    if (!buf) return -1;

    jw_t w;
    jw_init(&w, buf, TRUTH_BUF_BYTES);
    jw_obj_open(&w);

    jw_key(&w, "generator");
    jw_obj_open(&w);
    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "seed");    jw_uint(&w, r->cfg.seed);
    jw_obj_close(&w);

    jw_key(&w, "cfg"); put_cfg(&w, &r->cfg);
    jw_key(&w, "gps"); put_gps_cfg(&w, g);

    jw_key(&w, "length_m");   jw_double(&w, r->length_m, DEC_M);
    jw_key(&w, "duration_s"); jw_double(&w, r->duration_s, DEC_M);
    jw_key(&w, "t0_gps_us");  jw_int(&w, g->t0_gps_us);
    jw_key(&w, "t0_mono_us"); jw_int(&w, g->t0_mono_us);
    jw_key(&w, "n_gates");    jw_int(&w, r->n_gates);
    jw_key(&w, "gates");      put_gates(&w, r);

    jw_key(&w, "sf_crossings_s");
    jw_arr_open(&w);
    for (int n = 0; n <= r->cfg.laps; n++) jw_double(&w, synth_run_time_at_s(r, synth_run_sf_s_total(r, n)), DEC_M);
    jw_arr_close(&w);
    jw_key(&w, "sf_crossings_gps_us");
    jw_arr_open(&w);
    for (int n = 0; n <= r->cfg.laps; n++) jw_int(&w, gps_us_at(g->t0_gps_us, synth_run_time_at_s(r, synth_run_sf_s_total(r, n))));
    jw_arr_close(&w);

    jw_key(&w, "laps");
    jw_arr_open(&w);
    for (int lap = 1; lap <= r->cfg.laps; lap++) {
        double c[SYNTH_MAX_GATES + 1];
        int n = synth_run_lap_crossings(r, lap, c, sizeof c / sizeof c[0]);
        if (n < 2) { free(buf); return -1; }
        jw_obj_open(&w);
        jw_key(&w, "lap_no"); jw_int(&w, lap);
        jw_key(&w, "crossings_s");
        jw_arr_open(&w);
        for (int i = 0; i < n; i++) jw_double(&w, c[i], DEC_M);
        jw_arr_close(&w);
        jw_key(&w, "crossings_gps_us");
        jw_arr_open(&w);
        for (int i = 0; i < n; i++) jw_int(&w, gps_us_at(g->t0_gps_us, c[i]));
        jw_arr_close(&w);
        jw_key(&w, "lap_time_s"); jw_double(&w, c[n - 1] - c[0], DEC_M);
        jw_key(&w, "sector_times_s");
        jw_arr_open(&w);
        for (int i = 1; i < n; i++) jw_double(&w, c[i] - c[i - 1], DEC_M);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);

    jw_key(&w, "fixes_written"); jw_uint(&w, fixes_written);
    jw_key(&w, "fused_written"); jw_uint(&w, fused_written);
    jw_obj_close(&w);

    int rc = -1;
    if (!jw_overflow(&w) && fputs(buf, f) != EOF && fputc('\n', f) != EOF) rc = 0;
    free(buf);
    return rc;
}

int synth_venue_write(FILE *f, const synth_run_t *r)
{
    if (!f || !r) return -1;
    trk_venue_t v;
    synth_run_venue(r, &v);
    char buf[VENUE_BUF_BYTES];
    if (trk_to_json(&v, buf, sizeof buf) < 0) return -1;
    if (fputs(buf, f) == EOF) return -1;
    if (fputc('\n', f) == EOF) return -1;
    return 0;
}

/* ---------------- generation ---------------- */

/* Emits every TIME_MAP due at or before t_s. Called with the time of a sample that really exists,
 * so the record count is exactly 1 + floor(duration_s / 60). */
static int emit_time_maps(logw_t *w, const synth_gps_cfg_t *g, double *next_s, double t_s)
{
    while (*next_s <= t_s + TIME_EPS_S) {
        int64_t mono = g->t0_mono_us + (int64_t)llround(*next_s * 1e6);
        if (logw_time_map(w, mono, gps_us_at(g->t0_gps_us, *next_s), TIME_MAP_QUALITY) < 0) return -1;
        *next_s += TIME_MAP_PERIOD_S;
    }
    return 0;
}

int synth_generate(const synth_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz,
                   logw_t *w, synth_run_t *run, uint32_t *n_fix, uint32_t *n_fused)
{
    if (!cfg || !gcfg || !w || !run || !n_fix || !n_fused) return -1;
    if (gcfg->rate_hz <= 0 || fused_hz < 0 || fused_hz > 255) return -1;
    *n_fix = 0;
    *n_fused = 0;
    if (synth_run_build(run, cfg, NULL, 0) != 0) return -1;

    ses_hdr_t h;
    memset(&h, 0, sizeof h);
    snprintf(h.session_id, sizeof h.session_id, "%s", SYNTH_SESSION_ID);
    h.mode = 0;                         /* lap mode */
    h.variant = 0;                      /* moto */
    h.venue_id = cfg->venue_id;
    h.layout_id = SYNTH_LAYOUT_ID;
    snprintf(h.fw, sizeof h.fw, "%s", replay_version());   /* char[17] in memory, 16 bytes on the wire */
    snprintf(h.hwid, sizeof h.hwid, "%s", SYNTH_HWID);
    h.log_profile = 0;                  /* internal */
    h.fused_hz = (uint8_t)fused_hz;
    h.gps_hz = (uint8_t)gcfg->rate_hz;
    h.start_gps_us = gcfg->t0_gps_us;
    for (int i = 0; i < 9; i++) h.r_e4[i] = (i % 4 == 0) ? (int16_t)CALIB_IDENT_E4 : (int16_t)0;
    h.calib_flags = CALIB_FLAGS_OK;
    if (logw_hdr(w, &h) < 0) return -1;
    if (logw_venue(w, cfg->venue_id, SYNTH_LAYOUT_ID, SYNTH_VENUE_NAME) < 0) return -1;

    synth_gps_t gs;
    synth_gps_init(&gs, gcfg);
    double next_tm_s = 0.0;
    uint32_t k = 0, j = 0;              /* next fix index and next fused index */
    bool fix_done = false, fused_done = (fused_hz <= 0);

    while (!fix_done || !fused_done) {
        double t_fix   = fix_done   ? 0.0 : (double)k / (double)gcfg->rate_hz;
        double t_fused = fused_done ? 0.0 : (double)j / (double)fused_hz;
        /* Two-stream merge, earliest first; a tie puts the fix first so FUSED always has a
         * FIX_* reference to delta against (§12.4). */
        bool take_fix = !fix_done && (fused_done || t_fix <= t_fused);
        if (take_fix) {
            gps_fix_t fix;
            int rc = synth_gps_next(&gs, run, &fix, NULL);
            if (rc < 0) { fix_done = true; continue; }
            k++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fix) < 0) return -1;
            if (rc == 1) {                       /* rc == 0 is a dropout: the slot exists, the fix does not */
                if (logw_fix(w, &fix) < 0) return -1;
                (*n_fix)++;
            }
        } else {
            if (t_fused > run->duration_s) { fused_done = true; continue; }
            j++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fused) < 0) return -1;
            /* FUSED.dt_ms is relative to the previous FIX_* or FUSED record, so a fused sample
             * before the session's first fix has nothing to reference and is skipped. */
            if (*n_fix > 0) {
                fused_sample_t fs;
                synth_fused_at(run, t_fused, gcfg->t0_gps_us, gcfg->t0_mono_us, &fs);
                if (logw_fused(w, &fs) < 0) return -1;
                (*n_fused)++;
            }
        }
    }
    if (logw_end(w, gps_us_at(gcfg->t0_gps_us, run->duration_s), END_REASON_NORMAL) < 0) return -1;
    return w->err;
}

/* ---------------- option parsing ---------------- */

typedef enum { A_DBL, A_INT, A_U32 } argkind_t;
typedef struct { const char *name; argkind_t kind; void *dst; double lo, hi; } argopt_t;

static int parse_dbl(const char *s, double lo, double hi, double *out)
{
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return -1;
    if (!(v >= lo && v <= hi)) return -1;             /* also rejects NaN */
    *out = v;
    return 0;
}

static int parse_i64(const char *s, int64_t lo, int64_t hi, int64_t *out)
{
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) return -1;
    if ((int64_t)v < lo || (int64_t)v > hi) return -1;
    *out = (int64_t)v;
    return 0;
}

static int apply_opt(const argopt_t *o, const char *v)
{
    if (o->kind == A_DBL) {
        double d;
        if (parse_dbl(v, o->lo, o->hi, &d) != 0) return -1;
        *(double *)o->dst = d;
        return 0;
    }
    int64_t n;
    if (parse_i64(v, (int64_t)o->lo, (int64_t)o->hi, &n) != 0) return -1;
    if (o->kind == A_INT) *(int *)o->dst = (int)n;
    else                  *(uint32_t *)o->dst = (uint32_t)n;
    return 0;
}

static int parse_dropout(const char *s, synth_gps_cfg_t *g)
{
    const char *colon = strchr(s, ':');
    if (!colon || colon == s) return -1;
    char head[32];
    size_t n = (size_t)(colon - s);
    if (n + 1 > sizeof head) return -1;
    memcpy(head, s, n);
    head[n] = '\0';
    double a, b;
    if (parse_dbl(head, 0.0, DROPOUT_MAX_S, &a) != 0) return -1;
    if (parse_dbl(colon + 1, 0.0, DROPOUT_MAX_S, &b) != 0) return -1;
    if (b < a) return -1;
    g->dropout_start_s = a;
    g->dropout_end_s = b;
    return 0;
}

static int two(const char *s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

static int parse_utc(const char *s, int64_t *out)
{
    /* Exactly YYYY-MM-DDTHH:MM:SS, so a typo is rejected rather than silently truncated. */
    static const char pat[] = "0000-00-00T00:00:00";
    if (strlen(s) != sizeof pat - 1) return -1;
    for (size_t i = 0; i < sizeof pat - 1; i++) {
        if (pat[i] == '0') { if (s[i] < '0' || s[i] > '9') return -1; }
        else if (s[i] != pat[i]) return -1;
    }
    int y = two(s) * 100 + two(s + 2);
    int mo = two(s + 5), d = two(s + 8), hh = two(s + 11), mi = two(s + 14), ss = two(s + 17);
    if (y < 1970 || y > 2999 || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
    if (hh > 23 || mi > 59 || ss > 60) return -1;                 /* 60 = leap second */
    *out = tb_gps_us_from_utc(y, (unsigned)mo, (unsigned)d, (unsigned)hh, (unsigned)mi, (unsigned)ss, 0);
    return 0;
}

int synth_parse_args(int argc, char **argv, synth_cfg_t *cfg, synth_gps_cfg_t *gps,
                     int *fused_hz, char *out, size_t out_cap)
{
    if (!argv || !cfg || !gps || !fused_hz || !out || out_cap == 0) return -1;
    synth_cfg_defaults(cfg);
    synth_gps_cfg_defaults(gps);
    *fused_hz = SYNTH_FUSED_HZ;
    out[0] = '\0';

    /* One seed drives both the circuit and the GPS noise so a single --seed reproduces a session. */
    uint32_t seed = cfg->seed;
    const argopt_t tab[] = {
        { "--vertices",     A_INT, &cfg->n_vertices,       3.0,   (double)SYNTH_MAX_VERTICES },
        { "--length",       A_DBL, &cfg->length_m,        50.0,   100000.0 },
        { "--radius",       A_DBL, &cfg->corner_radius_m,  1.0,   1000.0 },
        { "--irregularity", A_DBL, &cfg->irregularity,     0.0,   0.9 },
        { "--seed",         A_U32, &seed,                  0.0,   4294967295.0 },
        { "--v-corner",     A_DBL, &cfg->v_corner_mps,     1.0,   150.0 },
        { "--v-max",        A_DBL, &cfg->v_max_mps,        1.0,   150.0 },
        { "--a-acc",        A_DBL, &cfg->a_acc_mps2,       0.1,   30.0 },
        { "--a-brk",        A_DBL, &cfg->a_brk_mps2,       0.1,   30.0 },
        { "--lap-var",      A_DBL, &cfg->lap_var,          0.0,   0.5 },
        { "--laps",         A_INT, &cfg->laps,             1.0,   (double)SYNTH_MAX_LAPS },
        { "--sectors",      A_INT, &cfg->n_sector_gates,   0.0,   (double)LAP_MAX_SECTORS },
        { "--start-before", A_DBL, &cfg->start_before_m,   0.0,   100000.0 },
        { "--stop-after",   A_DBL, &cfg->stop_after_m,     0.0,   100000.0 },
        { "--sf-frac",      A_DBL, &cfg->sf_frac,          0.0,   1.0 },
        { "--rate",         A_INT, &gps->rate_hz,          5.0,   10.0 },
        { "--pos-sigma",    A_DBL, &gps->pos_sigma_m,      0.0,   100.0 },
        { "--pos-tau",      A_DBL, &gps->pos_tau_s,        0.001, 100000.0 },
        { "--speed-sigma",  A_DBL, &gps->speed_sigma_mps,  0.0,   50.0 },
        { "--head-sigma",   A_DBL, &gps->head_sigma_deg,   0.0,   180.0 },
        { "--latency",      A_DBL, &gps->latency_ms,       0.0,   5000.0 },
        { "--jitter",       A_DBL, &gps->jitter_ms,        0.0,   5000.0 },
        { "--fused-hz",     A_INT, fused_hz,               0.0,   100.0 },
    };
    const size_t n_tab = sizeof tab / sizeof tab[0];

    int flags = 0;
    bool have_out = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0)          { flags |= SYNTH_ARG_HELP;  continue; }
        if (strcmp(a, "--quiet") == 0)         { flags |= SYNTH_ARG_QUIET; continue; }
        if (strcmp(a, "--anticlockwise") == 0) { cfg->clockwise = false;   continue; }
        if (i + 1 >= argc) return -1;                       /* every other option takes a value */
        const char *v = argv[++i];
        if (strcmp(a, "--out") == 0) {
            size_t n = strlen(v);
            if (n == 0 || n + 1 > out_cap) return -1;
            memcpy(out, v, n + 1);
            have_out = true;
            continue;
        }
        if (strcmp(a, "--dropout") == 0)   { if (parse_dropout(v, gps) != 0) return -1; continue; }
        if (strcmp(a, "--start-utc") == 0) { if (parse_utc(v, &gps->t0_gps_us) != 0) return -1; continue; }
        size_t t = 0;
        while (t < n_tab && strcmp(a, tab[t].name) != 0) t++;
        if (t == n_tab) return -1;
        if (apply_opt(&tab[t], v) != 0) return -1;
    }
    if (flags & SYNTH_ARG_HELP) return flags;               /* --help never needs --out */
    if (!have_out) return -1;
    if (gps->rate_hz != 5 && gps->rate_hz != 10) return -1; /* §12.6: the only logged GPS rates */
    cfg->seed = seed;
    gps->seed = seed;
    return flags;
}
