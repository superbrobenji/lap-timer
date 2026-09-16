#include "replay/synth_drag.h"
#include "replay/synth.h"       /* synth_enu_to_ll */
#include "replay/replay.h"      /* replay_version */
#include "core/consts.h"        /* G_MPS2, TRAP_DIST_M, DRAG_ARM_STILL_S */
#include "core/types.h"         /* GPS_FLAG_*, FUS_* */
#include "core/drag.h"          /* drag_cfg_defaults, gate kinds, DRAG_MAX_GATES */
#include "core/tb.h"            /* tb_gps_us_from_utc */
#include "core/jw.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Fixed straight-line geometry: due north from a constant origin (drag has no venue). Only lat moves,
 * through a plain division in synth_enu_to_ll, so the emitted fix is byte-identical on every platform. */
#define DRAG_ORIGIN_LAT  (-34.030000)
#define DRAG_ORIGIN_LON  ( 18.730000)
#define DRAG_SESSION_ID  "S00001_001"
#define DRAG_HWID        "synth"
#define DRAG_MODE_DRAG   1
#define DRAG_VARIANT     0            /* moto (units only) */
#define CALIB_IDENT_E4   10000
#define CALIB_FLAGS_OK   0x03
#define TIME_MAP_PERIOD_S 60.0
#define TIME_MAP_QUALITY  1
#define TIME_EPS_S        1e-9
#define END_REASON_NORMAL 0
#define TRUTH_BUF_BYTES   65536u
#define DEC_M             6
#define DEC_LL            9

void synth_drag_cfg_defaults(synth_drag_cfg_t *c)
{
    if (!c) return;
    c->target_mps    = 50.0;     /* 180 km/h at the 1/4 line */
    c->a_launch_mps2 = 0.0;      /* 0 = derive from target */
    c->a_brake_mps2  = 6.0;
    c->stage_s       = 4.0;
    c->coast_s       = 3.0;
}

int synth_drag_build(synth_drag_run_t *r, const synth_drag_cfg_t *cfg, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!r || !cfg) return -1;
    if (!(cfg->target_mps > 0.0) || !(cfg->a_brake_mps2 > 0.0) ||
        !(cfg->stage_s >= (double)DRAG_ARM_STILL_S) || !(cfg->coast_s >= 0.0)) {
        if (err && err_cap) snprintf(err, err_cap, "drag: target/a_brake/stage out of range");
        return -1;
    }
    double a = cfg->a_launch_mps2 > 0.0
             ? cfg->a_launch_mps2
             : (cfg->target_mps * cfg->target_mps) / (2.0 * SYNTH_DRAG_QUARTER_M);
    if (!(a > 0.0)) {
        if (err && err_cap) snprintf(err, err_cap, "drag: launch acceleration must be positive");
        return -1;
    }
    /* the launch must actually reach the trap line */
    double v_at_quarter = sqrt(2.0 * a * SYNTH_DRAG_QUARTER_M);
    if (!(v_at_quarter > 0.0)) {
        if (err && err_cap) snprintf(err, err_cap, "drag: target too low to reach the trap line");
        return -1;
    }
    r->cfg          = *cfg;
    r->a_launch     = a;
    r->accel_dist_m = SYNTH_DRAG_QUARTER_M + SYNTH_DRAG_ACCEL_MARGIN_M;
    r->v_peak_mps   = sqrt(2.0 * a * r->accel_dist_m);
    r->t_launch_s   = cfg->stage_s;
    double t_acc    = r->v_peak_mps / a;
    r->t_peak_s     = r->t_launch_s + t_acc;
    double t_brk    = r->v_peak_mps / cfg->a_brake_mps2;
    r->t_stop_s     = r->t_peak_s + t_brk;
    r->duration_s   = r->t_stop_s + cfg->coast_s;
    double brk_dist = r->v_peak_mps * r->v_peak_mps / (2.0 * cfg->a_brake_mps2);
    r->brake_dist_m = r->accel_dist_m + brk_dist;
    return 0;
}

void synth_drag_state_at(const synth_drag_run_t *r, double t_s,
                         double *v_mps, double *a_lon_mps2, double *dist_m, bool *still)
{
    double v = 0.0, a = 0.0, d = 0.0;
    bool st = true;
    if (!r) { if (v_mps) *v_mps = 0; if (a_lon_mps2) *a_lon_mps2 = 0; if (dist_m) *dist_m = 0; if (still) *still = true; return; }
    if (t_s < 0.0) t_s = 0.0;
    if (t_s > r->duration_s) t_s = r->duration_s;

    if (t_s < r->t_launch_s) {                 /* staging: at rest */
        v = 0.0; a = 0.0; d = 0.0; st = true;
    } else if (t_s < r->t_peak_s) {            /* launch / acceleration */
        double dt = t_s - r->t_launch_s;
        a = r->a_launch;
        v = a * dt;
        d = 0.5 * a * dt * dt;
        st = false;
    } else if (t_s < r->t_stop_s) {            /* braking */
        double dt = t_s - r->t_peak_s;
        a = -r->cfg.a_brake_mps2;
        v = r->v_peak_mps - r->cfg.a_brake_mps2 * dt;
        if (v < 0.0) v = 0.0;
        d = r->accel_dist_m + r->v_peak_mps * dt - 0.5 * r->cfg.a_brake_mps2 * dt * dt;
        st = false;
    } else {                                   /* stopped / coast */
        v = 0.0; a = 0.0; d = r->brake_dist_m; st = true;
    }
    if (v_mps) *v_mps = v;
    if (a_lon_mps2) *a_lon_mps2 = a;
    if (dist_m) *dist_m = d;
    if (still) *still = st;
}

double synth_drag_t_at_speed(const synth_drag_run_t *r, double v_mps)
{
    if (!r || v_mps < 0.0) return -1.0;
    if (v_mps <= r->v_peak_mps) return v_mps / r->a_launch;   /* first reached during acceleration */
    return -1.0;
}

double synth_drag_t_at_dist(const synth_drag_run_t *r, double dist_m)
{
    if (!r || dist_m < 0.0) return -1.0;
    if (dist_m <= r->accel_dist_m) return sqrt(2.0 * dist_m / r->a_launch);
    if (dist_m <= r->brake_dist_m) {
        double dd = dist_m - r->accel_dist_m;
        double disc = r->v_peak_mps * r->v_peak_mps - 2.0 * r->cfg.a_brake_mps2 * dd;
        if (disc < 0.0) return -1.0;
        double dt = (r->v_peak_mps - sqrt(disc)) / r->cfg.a_brake_mps2;
        return (r->t_peak_s - r->t_launch_s) + dt;
    }
    return -1.0;
}

double synth_drag_trap_mps(const synth_drag_run_t *r)
{
    /* Mean of v = sqrt(2 a s) over s ∈ [D − TRAP_DIST_M, D], closed form:
     * (1/W)∫ sqrt(2a) sqrt(s) ds = sqrt(2a)·(2/3)(s2^1.5 − s1^1.5)/W. */
    if (!r) return 0.0;
    double s2 = SYNTH_DRAG_QUARTER_M;
    double s1 = SYNTH_DRAG_QUARTER_M - (double)TRAP_DIST_M;
    if (s1 < 0.0) s1 = 0.0;
    double w = s2 - s1;
    if (!(w > 0.0)) return sqrt(2.0 * r->a_launch * s2);
    return sqrt(2.0 * r->a_launch) * (2.0 / 3.0) * (pow(s2, 1.5) - pow(s1, 1.5)) / w;
}

/* ---------------- record generation ---------------- */

static int64_t gps_us_at(int64_t t0, double t_s) { return t0 + (int64_t)llround(t_s * 1e6); }

static void fill_fix(const synth_drag_run_t *r, const synth_gps_cfg_t *g, synth_rng_t *rng,
                     uint32_t k, gps_fix_t *fix)
{
    double t_s = (double)k / (double)g->rate_hz;
    double v = 0.0, dist = 0.0;
    synth_drag_state_at(r, t_s, &v, NULL, &dist, NULL);
    double lat_deg, lon_deg;
    synth_enu_to_ll(DRAG_ORIGIN_LAT, DRAG_ORIGIN_LON, 0.0, dist, &lat_deg, &lon_deg);

    /* deterministic arrival jitter (integer RNG, no libm) on mono only; gps_us stays exact */
    double jit = 0.0;
    if (g->jitter_ms > 0.0) jit = (2.0 * synth_rng_uniform(rng) - 1.0) * g->jitter_ms;

    memset(fix, 0, sizeof *fix);
    fix->gps_us     = g->t0_gps_us + (int64_t)k * (1000000LL / (int64_t)g->rate_hz);
    fix->mono_us    = g->t0_mono_us + (int64_t)llround(t_s * 1e6 + (g->latency_ms + jit) * 1000.0);
    fix->lat_e7     = (int32_t)llround(lat_deg * 1e7);
    fix->lon_e7     = (int32_t)llround(lon_deg * 1e7);
    fix->alt_mm     = g->alt_mm;
    fix->gspeed_mms = (int32_t)llround(v * 1000.0);
    fix->head_e5    = 0;                                   /* due north */
    fix->hacc_mm    = (uint32_t)llround(g->hacc_m * 1000.0);
    fix->sacc_mms   = 50;
    fix->pdop_e2    = g->pdop_e2;
    fix->fix_type   = 3;
    fix->sats       = g->sats;
    fix->flags      = (uint8_t)(GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE);
    fix->valid      = 1;
}

static void fill_fused(const synth_drag_run_t *r, int64_t t0_gps, int64_t t0_mono, double t_s,
                       fused_sample_t *fs)
{
    double v = 0.0, a = 0.0, dist = 0.0;
    bool still = true;
    synth_drag_state_at(r, t_s, &v, &a, &dist, &still);
    memset(fs, 0, sizeof *fs);
    fs->gps_us   = gps_us_at(t0_gps, t_s);
    fs->mono_us  = t0_mono + (int64_t)llround(t_s * 1e6);
    fs->g_lon    = (float)(a / G_MPS2);
    fs->g_lat    = 0.0f;
    fs->g_comb   = (float)(fabs(a) / G_MPS2);
    fs->lean_deg = 0.0f;
    fs->yaw_dps  = 0.0f;
    fs->flags    = (uint8_t)(FUS_ORIENT_OK | FUS_LEAN_VALID | (still ? FUS_STILL : 0));
}

static int emit_time_maps(logw_t *w, const synth_gps_cfg_t *g, double *next_s, double t_s)
{
    while (*next_s <= t_s + TIME_EPS_S) {
        int64_t mono = g->t0_mono_us + (int64_t)llround(*next_s * 1e6);
        if (logw_time_map(w, mono, gps_us_at(g->t0_gps_us, *next_s), TIME_MAP_QUALITY) < 0) return -1;
        *next_s += TIME_MAP_PERIOD_S;
    }
    return 0;
}

int synth_drag_generate(const synth_drag_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz,
                        logw_t *w, synth_drag_run_t *run, uint32_t *n_fix, uint32_t *n_fused)
{
    if (!cfg || !gcfg || !w || !run || !n_fix || !n_fused) return -1;
    if (gcfg->rate_hz != 5 && gcfg->rate_hz != 10) return -1;
    if (fused_hz < 0 || fused_hz > 255) return -1;
    *n_fix = 0;
    *n_fused = 0;
    if (synth_drag_build(run, cfg, NULL, 0) != 0) return -1;

    ses_hdr_t h;
    memset(&h, 0, sizeof h);
    snprintf(h.session_id, sizeof h.session_id, "%s", DRAG_SESSION_ID);
    h.mode      = DRAG_MODE_DRAG;
    h.variant   = DRAG_VARIANT;
    h.venue_id  = 0;                    /* drag has no venue */
    h.layout_id = 0;
    snprintf(h.fw, sizeof h.fw, "%s", replay_version());
    snprintf(h.hwid, sizeof h.hwid, "%s", DRAG_HWID);
    h.log_profile = 0;
    h.fused_hz  = (uint8_t)fused_hz;
    h.gps_hz    = (uint8_t)gcfg->rate_hz;
    h.start_gps_us = gcfg->t0_gps_us;
    for (int i = 0; i < 9; i++) h.r_e4[i] = (i % 4 == 0) ? (int16_t)CALIB_IDENT_E4 : (int16_t)0;
    h.calib_flags = CALIB_FLAGS_OK;
    if (logw_hdr(w, &h) < 0) return -1;   /* no VENUE record for a drag session */

    synth_rng_t rng;
    synth_rng_seed(&rng, gcfg->seed);
    double next_tm_s = 0.0;
    uint32_t k = 0, j = 0;
    bool fix_done = false, fused_done = (fused_hz <= 0);

    while (!fix_done || !fused_done) {
        double t_fix   = fix_done   ? 0.0 : (double)k / (double)gcfg->rate_hz;
        double t_fused = fused_done ? 0.0 : (double)j / (double)fused_hz;
        bool take_fix = !fix_done && (fused_done || t_fix <= t_fused);
        if (take_fix) {
            if (t_fix > run->duration_s) { fix_done = true; continue; }
            gps_fix_t fix;
            fill_fix(run, gcfg, &rng, k, &fix);
            k++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fix) < 0) return -1;
            if (logw_fix(w, &fix) < 0) return -1;
            (*n_fix)++;
        } else {
            if (t_fused > run->duration_s) { fused_done = true; continue; }
            j++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fused) < 0) return -1;
            if (*n_fix > 0) {                   /* FUSED deltas need a prior FIX reference (§12.4) */
                fused_sample_t fs;
                fill_fused(run, gcfg->t0_gps_us, gcfg->t0_mono_us, t_fused, &fs);
                if (logw_fused(w, &fs) < 0) return -1;
                (*n_fused)++;
            }
        }
    }
    if (logw_end(w, gps_us_at(gcfg->t0_gps_us, run->duration_s), END_REASON_NORMAL) < 0) return -1;
    return w->err;
}

/* ---------------- truth JSON ---------------- */

int synth_drag_truth_write(FILE *f, const synth_drag_run_t *r, const synth_gps_cfg_t *g,
                           uint32_t fixes_written, uint32_t fused_written)
{
    if (!f || !r || !g) return -1;
    char *buf = (char *)malloc(TRUTH_BUF_BYTES);
    if (!buf) return -1;
    jw_t w;
    jw_init(&w, buf, TRUTH_BUF_BYTES);
    jw_obj_open(&w);

    jw_key(&w, "generator"); jw_obj_open(&w);
    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "seed"); jw_uint(&w, g->seed);
    jw_obj_close(&w);

    jw_key(&w, "mode"); jw_str(&w, "drag");
    jw_key(&w, "cfg"); jw_obj_open(&w);
    jw_key(&w, "target_mps");   jw_double(&w, r->cfg.target_mps, DEC_M);
    jw_key(&w, "a_launch_mps2"); jw_double(&w, r->a_launch, DEC_M);
    jw_key(&w, "a_brake_mps2");  jw_double(&w, r->cfg.a_brake_mps2, DEC_M);
    jw_key(&w, "stage_s");       jw_double(&w, r->cfg.stage_s, DEC_M);
    jw_key(&w, "coast_s");       jw_double(&w, r->cfg.coast_s, DEC_M);
    jw_key(&w, "rate_hz");       jw_int(&w, g->rate_hz);
    jw_obj_close(&w);

    int64_t t0 = gps_us_at(g->t0_gps_us, r->t_launch_s);      /* launch instant (drag t0) */
    jw_key(&w, "t0_gps_us");     jw_int(&w, g->t0_gps_us);
    jw_key(&w, "launch_gps_us"); jw_int(&w, t0);
    jw_key(&w, "duration_s");    jw_double(&w, r->duration_s, DEC_M);
    jw_key(&w, "v_peak_mps");    jw_double(&w, r->v_peak_mps, DEC_M);
    jw_key(&w, "trap_mps");      jw_double(&w, synth_drag_trap_mps(r), DEC_M);
    jw_key(&w, "trap_cms");      jw_uint(&w, (uint32_t)(synth_drag_trap_mps(r) * 100.0 + 0.5));
    jw_key(&w, "origin_lat_deg"); jw_double(&w, DRAG_ORIGIN_LAT, DEC_LL);
    jw_key(&w, "origin_lon_deg"); jw_double(&w, DRAG_ORIGIN_LON, DEC_LL);

    drag_cfg_t dc;
    drag_cfg_defaults(&dc);
    jw_key(&w, "gates"); jw_arr_open(&w);
    for (uint8_t i = 0; i < dc.n_gates; i++) {
        const drag_gate_def_t *d = &dc.gates[i];
        double t = -1.0;                 /* run-seconds from t0, or -1 if never reached */
        const char *kind = "";
        switch (d->kind) {
        case DRAG_SPEED_FROM0: kind = "speed_from0"; t = synth_drag_t_at_speed(r, (double)d->a / 3.6); break;
        case DRAG_SPEED_RANGE: {
            kind = "speed_range";
            double ta = synth_drag_t_at_speed(r, (double)d->a / 3.6);
            double tb = synth_drag_t_at_speed(r, (double)d->b / 3.6);
            t = (ta >= 0.0 && tb >= 0.0) ? (tb - ta) : -1.0;   /* engine reports the a→b interval */
            break;
        }
        case DRAG_DIST:  kind = "dist";  t = synth_drag_t_at_dist(r, (double)d->a / 100.0); break;
        case DRAG_BRAKE: {
            kind = "brake";
            /* 100→0: distance from 100 km/h down to rest at a_brake, timed from that crossing. */
            double vhi = (double)d->a / 3.6;
            t = (r->v_peak_mps >= vhi) ? (vhi / r->cfg.a_brake_mps2) : -1.0;
            break;
        }
        default: break;
        }
        bool reached = t >= 0.0;
        jw_obj_open(&w);
        jw_key(&w, "id");   jw_uint(&w, d->id);
        jw_key(&w, "kind"); jw_str(&w, kind);
        jw_key(&w, "a");    jw_uint(&w, d->a);
        jw_key(&w, "b");    jw_uint(&w, d->b);
        jw_key(&w, "reached"); jw_bool(&w, reached);
        jw_key(&w, "time_ms");  jw_int(&w, reached ? (int64_t)llround(t * 1000.0) : -1);
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

/* ---------------- CLI ---------------- */

static int parse_dbl(const char *s, double lo, double hi, double *out)
{
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return -1;
    if (!(v >= lo && v <= hi)) return -1;
    *out = v;
    return 0;
}

static int parse_int(const char *s, long lo, long hi, long *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) return -1;
    *out = v;
    return 0;
}

int synth_drag_main(int argc, char **argv)
{
    synth_drag_cfg_t cfg;
    synth_gps_cfg_t  gcfg;
    synth_drag_cfg_defaults(&cfg);
    synth_gps_cfg_defaults(&gcfg);
    /* Drag timing is Doppler-sensitive; the fixtures are noise-free so gate times match the analytic
     * truth tightly (the noise model is exercised by the circuit fixtures and test_synth_gps). */
    gcfg.pos_sigma_m = 0.0;
    gcfg.speed_sigma_mps = 0.0;
    gcfg.head_sigma_deg = 0.0;
    int fused_hz = FUSION_HZ;           /* 100 Hz for §6.6 gate timing */

    char prefix[480];
    prefix[0] = '\0';
    bool have_out = false;
    int quiet = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--quiet") == 0) { quiet = 1; continue; }
        if (strcmp(a, "--help") == 0) {
            printf("usage: synth --profile drag --out PREFIX [options]\n"
                   "  --target-kmh N   headline speed at the 1/4 line   (180)\n"
                   "  --a-launch MPS2  launch accel (0 = derive)        (derived)\n"
                   "  --a-brake MPS2   braking decel                    (6)\n"
                   "  --stage S        still staging before launch      (4)\n"
                   "  --rate HZ        fix rate, 5 or 10                 (5)\n"
                   "  --fused-hz N     FUSED rate                        (100)\n"
                   "  --latency MS     mean arrival latency              (80)\n"
                   "  --jitter MS      uniform arrival jitter, +/-       (20)\n"
                   "  --seed S         jitter seed                       (1)\n"
                   "  --start-utc TS   run time 0, YYYY-MM-DDTHH:MM:SS   (2026-09-15T10:00:00)\n"
                   "  --out PREFIX     writes PREFIX.log and PREFIX.truth.json (required)\n");
            return 0;
        }
        if (i + 1 >= argc) { fprintf(stderr, "synth: %s needs a value\n", a); return 2; }
        const char *v = argv[++i];
        int bad = 0;
        if (strcmp(a, "--profile") == 0) { if (strcmp(v, "drag") != 0) bad = 1; }
        else if (strcmp(a, "--out") == 0) {
            size_t n = strlen(v);
            if (n == 0 || n + 1 > sizeof prefix) bad = 1;
            else { memcpy(prefix, v, n + 1); have_out = true; }
        } else if (strcmp(a, "--target-kmh") == 0) { double d; if (parse_dbl(v, 1.0, 1000.0, &d)) bad = 1; else cfg.target_mps = d / 3.6; }
        else if (strcmp(a, "--a-launch") == 0)     { double d; if (parse_dbl(v, 0.0, 60.0, &d)) bad = 1; else cfg.a_launch_mps2 = d; }
        else if (strcmp(a, "--a-brake") == 0)      { double d; if (parse_dbl(v, 0.1, 60.0, &d)) bad = 1; else cfg.a_brake_mps2 = d; }
        else if (strcmp(a, "--stage") == 0)        { double d; if (parse_dbl(v, (double)DRAG_ARM_STILL_S, 60.0, &d)) bad = 1; else cfg.stage_s = d; }
        else if (strcmp(a, "--latency") == 0)      { double d; if (parse_dbl(v, 0.0, 5000.0, &d)) bad = 1; else gcfg.latency_ms = d; }
        else if (strcmp(a, "--jitter") == 0)       { double d; if (parse_dbl(v, 0.0, 5000.0, &d)) bad = 1; else gcfg.jitter_ms = d; }
        else if (strcmp(a, "--rate") == 0)         { long n; if (parse_int(v, 5, 10, &n) || (n != 5 && n != 10)) bad = 1; else gcfg.rate_hz = (int)n; }
        else if (strcmp(a, "--fused-hz") == 0)     { long n; if (parse_int(v, 1, 100, &n)) bad = 1; else fused_hz = (int)n; }
        else if (strcmp(a, "--seed") == 0)         { long n; if (parse_int(v, 0, 4294967295L, &n)) bad = 1; else gcfg.seed = (uint32_t)n; }
        else if (strcmp(a, "--start-utc") == 0)    {
            /* exactly YYYY-MM-DDTHH:MM:SS */
            if (strlen(v) != 19) bad = 1;
            else {
                int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;
                if (sscanf(v, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &hh, &mm, &ss) != 6 ||
                    mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 60) bad = 1;
                else gcfg.t0_gps_us = tb_gps_us_from_utc(y, (unsigned)mo, (unsigned)d, (unsigned)hh, (unsigned)mm, (unsigned)ss, 0);
            }
        } else bad = 1;
        if (bad) { fprintf(stderr, "synth: bad option %s %s\n", a, v); return 2; }
    }
    if (!have_out) { fprintf(stderr, "synth: --out is required\n"); return 2; }

    char log_path[520], truth_path[520];
    if (snprintf(log_path, sizeof log_path, "%s.log", prefix) >= (int)sizeof log_path ||
        snprintf(truth_path, sizeof truth_path, "%s.truth.json", prefix) >= (int)sizeof truth_path) {
        fprintf(stderr, "synth: --out prefix is too long\n");
        return 2;
    }

    synth_drag_run_t *run = (synth_drag_run_t *)malloc(sizeof *run);
    if (!run) { fprintf(stderr, "synth: out of memory\n"); return 1; }
    char err[160];
    if (synth_drag_build(run, &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "synth: %s\n", err);
        free(run);
        return 1;
    }

    logw_t w;
    if (logw_open_file(&w, log_path) != 0) { fprintf(stderr, "synth: cannot write %s\n", log_path); free(run); return 1; }
    uint32_t n_fix = 0, n_fused = 0;
    int rc = synth_drag_generate(&cfg, &gcfg, fused_hz, &w, run, &n_fix, &n_fused);
    if (logw_close(&w) != 0) rc = -1;
    if (rc != 0) { fprintf(stderr, "synth: cannot write %s\n", log_path); free(run); return 1; }

    FILE *tf = fopen(truth_path, "w");
    int trc = tf ? synth_drag_truth_write(tf, run, &gcfg, n_fix, n_fused) : -1;
    if (tf && fclose(tf) != 0) trc = -1;
    if (trc != 0) { fprintf(stderr, "synth: cannot write %s\n", truth_path); free(run); return 1; }

    if (!quiet)
        printf("wrote %s: %u fixes, %u fused, target %.1f km/h, %.3f s\n",
               log_path, n_fix, n_fused, run->cfg.target_mps * 3.6, run->duration_s);
    free(run);
    return 0;
}
