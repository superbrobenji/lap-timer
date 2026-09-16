#include "unity.h"
#include "replay/replay.h"
#include "core/json.h"
#include "core/jsmn.h"
#include "core/trk.h"
#include "core/consts.h"
#include "core/drag.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixture regression + error-bound checks (spec §22.2). Runs against the committed test/data
 * fixtures (path from -DREPLAY_DATA_DIR). Two independent guarantees:
 *   1. replay --json matches <name>.expected.json STRUCTURALLY (see json_eq_tol): every field exact,
 *      except *_ms within ±1 ms and *_us within ±1000 µs, and the build-stamp "version" ignored. A
 *      byte-exact compare would be flaky across platforms because the crossing interpolation and the
 *      circuit .log positions ride on libm, whose last ULP differs Apple-libm vs glibc; the ±1 ms
 *      structural compare still catches real engine drift. The committed fixtures are the baseline
 *      (regenerated manually via gen_fixtures.sh, not by CI).
 *   2. recovered lap/sector crossing times are within ±30 ms (5 Hz) / ±15 ms (10 Hz) of
 *      <name>.truth.json for ≥95 % of crossings, and drag gate times within their analytic targets. */

#ifndef REPLAY_DATA_DIR
#define REPLAY_DATA_DIR "test/data"
#endif

void setUp(void) {}
void tearDown(void) {}

typedef struct { const char *name; int mode; int bounds; } fx_t;
static const fx_t FIX[] = {
    { "killarney_full",     REPLAY_MODE_LAP,  1 },
    { "killarney_short",    REPLAY_MODE_LAP,  1 },
    { "killarney_full_rev", REPLAY_MODE_LAP,  1 },
    { "zwartkops",          REPLAY_MODE_LAP,  1 },
    { "drag_0_180",         REPLAY_MODE_DRAG, 1 },
    { "drag_0_320",         REPLAY_MODE_DRAG, 1 },
    { "gps_dropout",        REPLAY_MODE_LAP,  0 },   /* robustness: regression only */
    { "truncated",          REPLAY_MODE_LAP,  0 },   /* robustness: regression only */
};
#define N_FIX ((int)(sizeof FIX / sizeof FIX[0]))

static char *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    TEST_ASSERT_TRUE(n >= 0);
    char *b = (char *)malloc((size_t)n + 1);
    TEST_ASSERT_NOT_NULL(b);
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = '\0';
    if (len_out) *len_out = got;
    return b;
}

static void path_for(char *out, size_t cap, const char *name, const char *suffix)
{
    int n = snprintf(out, cap, "%s/%s%s", REPLAY_DATA_DIR, name, suffix);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < cap);
}

static bool ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && memcmp(s + n - m, suf, m) == 0;
}

/* Structural compare of two replay JSONs of identical shape: every field exact, except a *_ms value
 * within ±1 ms and a *_us value within ±1000 µs (cross-libm portability, §22.2 determinism ruling),
 * and the "version" string ignored (build stamp, not engine output). Real engine drift still fails. */
static bool json_eq_tol(const char *ja, size_t na_bytes, const char *jb, size_t nb_bytes)
{
    unsigned capa = (unsigned)(na_bytes / 2 + 16), capb = (unsigned)(nb_bytes / 2 + 16);
    jsmntok_t *ta = (jsmntok_t *)malloc(capa * sizeof *ta);
    jsmntok_t *tb = (jsmntok_t *)malloc(capb * sizeof *tb);
    bool ok = ta && tb;
    int na = ok ? json_parse(ja, na_bytes, ta, capa) : -1;
    int nb = ok ? json_parse(jb, nb_bytes, tb, capb) : -1;
    if (na <= 0 || na != nb) ok = false;
    char key[64] = "";
    for (int i = 0; ok && i < na; i++) {
        if (ta[i].type != tb[i].type) { ok = false; break; }
        int alen = ta[i].end - ta[i].start, blen = tb[i].end - tb[i].start;
        if (ta[i].type == JSMN_STRING) {
            if (ta[i].size == 1) {                         /* object key: defines structure + tolerance */
                if (alen != blen || memcmp(ja + ta[i].start, jb + tb[i].start, (size_t)alen) != 0) { ok = false; break; }
                size_t kl = (size_t)alen < sizeof key - 1 ? (size_t)alen : sizeof key - 1;
                memcpy(key, ja + ta[i].start, kl);
                key[kl] = '\0';
            } else if (strcmp(key, "version") != 0) {      /* string value (version is the build stamp) */
                if (alen != blen || memcmp(ja + ta[i].start, jb + tb[i].start, (size_t)alen) != 0) { ok = false; break; }
            }
        } else if (ta[i].type == JSMN_PRIMITIVE) {
            int64_t va, vb;
            bool ia = json_tok_int(ja, &ta[i], &va), ib = json_tok_int(jb, &tb[i], &vb);
            if (ia != ib) { ok = false; break; }
            if (ia) {
                int64_t tol = ends_with(key, "_ms") ? 1 : (ends_with(key, "_us") ? 1000 : 0);
                int64_t d = va > vb ? va - vb : vb - va;
                if (d > tol) { ok = false; break; }
            } else if (alen != blen || memcmp(ja + ta[i].start, jb + tb[i].start, (size_t)alen) != 0) {
                ok = false; break;                         /* true / false / null */
            }
        } else if (ta[i].size != tb[i].size) {             /* object / array shape */
            ok = false; break;
        }
    }
    free(ta); free(tb);
    return ok;
}

/* Runs replay_run on a fixture (loading the venue side-car in lap mode). Caller owns *out. */
static void run_fixture(const fx_t *fx, replay_run_t *out)
{
    char logp[1024];
    path_for(logp, sizeof logp, fx->name, ".log");
    trk_venue_t venue;
    const trk_venue_t *vp = NULL;
    if (fx->mode == REPLAY_MODE_LAP) {
        char vjson[1024];
        path_for(vjson, sizeof vjson, fx->name, ".venue.json");
        size_t vn;
        char *js = slurp(vjson, &vn);
        char err[128];
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, trk_from_json(&venue, js, vn, err, sizeof err), err);
        free(js);
        vp = &venue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, replay_run(logp, fx->mode, vp, out), fx->name);
}

/* --------- guarantee 1: exact-JSON regression --------- */
static void test_replay_json_matches_expected_fixtures(void)
{
    for (int i = 0; i < N_FIX; i++) {
        replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
        TEST_ASSERT_NOT_NULL(out);
        run_fixture(&FIX[i], out);

        FILE *tf = tmpfile();
        TEST_ASSERT_NOT_NULL(tf);
        replay_print_run_json(out, tf);
        fflush(tf);
        long gn = ftell(tf);
        TEST_ASSERT_TRUE(gn >= 0);
        rewind(tf);
        char *got = (char *)malloc((size_t)gn + 1);
        TEST_ASSERT_NOT_NULL(got);
        TEST_ASSERT_EQUAL_UINT((unsigned)gn, (unsigned)fread(got, 1, (size_t)gn, tf));
        got[gn] = '\0';
        fclose(tf);

        char expp[1024];
        path_for(expp, sizeof expp, FIX[i].name, ".expected.json");
        size_t en;
        char *exp = slurp(expp, &en);

        /* structural, cross-libm-tolerant compare (see json_eq_tol) rather than byte-exact */
        TEST_ASSERT_TRUE_MESSAGE(json_eq_tol(got, (size_t)gn, exp, en), FIX[i].name);
        free(got); free(exp); free(out);
    }
}

/* --------- guarantee 2a: lap/sector crossing error --------- */

/* Every truth crossing gps_us (S/F + sector gates), collected from laps[].crossings_gps_us. */
static int truth_crossings(const char *js, const jsmntok_t *toks, int nt, int64_t *out, int cap)
{
    int laps = json_obj_get(js, toks, nt, 0, "laps");
    if (laps < 0 || toks[laps].type != JSMN_ARRAY) return -1;
    int n = 0, idx = laps + 1;
    for (int L = 0; L < toks[laps].size; L++) {
        int cr = json_obj_get(js, toks, nt, idx, "crossings_gps_us");
        if (cr >= 0 && toks[cr].type == JSMN_ARRAY) {
            int ci = cr + 1;
            for (int j = 0; j < toks[cr].size; j++) {
                int64_t v;
                if (json_tok_int(js, &toks[ci], &v) && n < cap) out[n++] = v;
                ci = json_skip(toks, nt, ci);
            }
        }
        idx = json_skip(toks, nt, idx);
    }
    return n;
}

static int64_t nearest_err(int64_t x, const int64_t *set, int n)
{
    int64_t best = -1;
    for (int i = 0; i < n; i++) {
        int64_t d = x > set[i] ? x - set[i] : set[i] - x;
        if (best < 0 || d < best) best = d;
    }
    return best;
}

static void check_lap_bounds(const fx_t *fx)
{
    replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
    TEST_ASSERT_NOT_NULL(out);
    run_fixture(fx, out);

    char tp[1024];
    path_for(tp, sizeof tp, fx->name, ".truth.json");
    size_t tn;
    char *js = slurp(tp, &tn);
    unsigned ncap = (unsigned)(tn / 2 + 64);
    jsmntok_t *toks = (jsmntok_t *)malloc(ncap * sizeof *toks);
    TEST_ASSERT_NOT_NULL(toks);
    int nt = json_parse(js, tn, toks, ncap);
    TEST_ASSERT_TRUE(nt > 0);

    /* tolerance from the fix rate (§22.2): 30 ms at 5 Hz, 15 ms at 10 Hz */
    int gps = json_obj_get(js, toks, nt, 0, "gps");
    TEST_ASSERT_TRUE(gps >= 0);
    int rt = json_obj_get(js, toks, nt, gps, "rate_hz");
    int64_t rate = 5;
    TEST_ASSERT_TRUE(rt >= 0 && json_tok_int(js, &toks[rt], &rate));
    int64_t tol_us = (rate >= 10 ? 15 : 30) * 1000;

    static int64_t truth[1024];
    int tcount = truth_crossings(js, toks, nt, truth, (int)(sizeof truth / sizeof truth[0]));
    TEST_ASSERT_TRUE(tcount > 0);

    int total = 0, within = 0;
    int64_t worst = 0;
    for (uint16_t i = 0; i < out->n_laps; i++) {
        const replay_lap_t *L = &out->laps[i];
        int64_t cross[LAP_MAX_SECTORS + 2];
        int nc = 0;
        cross[nc++] = L->start_gps_us;
        for (uint8_t k = 0; k < L->n_sector_cross; k++) cross[nc++] = L->sector_gps_us[k];
        cross[nc++] = L->end_gps_us;
        for (int k = 0; k < nc; k++) {
            int64_t e = nearest_err(cross[k], truth, tcount);
            total++;
            if (e <= tol_us) within++;
            if (e > worst) worst = e;
        }
    }
    TEST_ASSERT_TRUE(total > 0);
    double frac = (double)within / (double)total;
    printf("[%s %lld Hz] crossings %d, within %.0f%% of %lld ms, worst %.1f ms\n",
           fx->name, (long long)rate, total, frac * 100.0, (long long)(tol_us / 1000), (double)worst / 1000.0);
    TEST_ASSERT_TRUE_MESSAGE(frac >= 0.95, fx->name);

    free(toks); free(js); free(out);
}

static void test_lap_fixture_crossing_error_bounds(void)
{
    for (int i = 0; i < N_FIX; i++)
        if (FIX[i].bounds && FIX[i].mode == REPLAY_MODE_LAP) check_lap_bounds(&FIX[i]);
}

/* --------- guarantee 2b: drag gate error --------- */
static void check_drag_bounds(const fx_t *fx)
{
    replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
    TEST_ASSERT_NOT_NULL(out);
    run_fixture(fx, out);
    TEST_ASSERT_EQUAL_UINT16(1, out->n_runs);
    const replay_drag_t *R = &out->runs[0];

    char tp[1024];
    path_for(tp, sizeof tp, fx->name, ".truth.json");
    size_t tn;
    char *js = slurp(tp, &tn);
    unsigned ncap = (unsigned)(tn / 2 + 64);
    jsmntok_t *toks = (jsmntok_t *)malloc(ncap * sizeof *toks);
    TEST_ASSERT_NOT_NULL(toks);
    int nt = json_parse(js, tn, toks, ncap);
    TEST_ASSERT_TRUE(nt > 0);

    int gates = json_obj_get(js, toks, nt, 0, "gates");
    TEST_ASSERT_TRUE(gates >= 0 && toks[gates].type == JSMN_ARRAY);

    const int64_t tol_us = 40 * 1000;         /* drag gate analytic target (§6.6 resolution) */
    int checked = 0;
    double worst = 0.0;
    int gi = gates + 1;
    for (int g = 0; g < toks[gates].size; g++) {
        int64_t id = 0, tms = -1;
        bool reached = false;
        int t;
        if ((t = json_obj_get(js, toks, nt, gi, "id")) >= 0) json_tok_int(js, &toks[t], &id);
        if ((t = json_obj_get(js, toks, nt, gi, "reached")) >= 0) json_tok_bool(js, &toks[t], &reached);
        if ((t = json_obj_get(js, toks, nt, gi, "time_ms")) >= 0) json_tok_int(js, &toks[t], &tms);
        gi = json_skip(toks, nt, gi);

        const drag_gate_res_t *eng = NULL;
        for (uint8_t k = 0; k < R->n_gates; k++) if (R->gates[k].gate_id == (uint8_t)id) { eng = &R->gates[k]; break; }
        TEST_ASSERT_NOT_NULL(eng);
        if (reached) {
            TEST_ASSERT_TRUE_MESSAGE(eng->hit, fx->name);
            int64_t e = (int64_t)eng->time_ms - tms;
            if (e < 0) e = -e;
            if ((double)e > worst) worst = (double)e;
            TEST_ASSERT_TRUE_MESSAGE(e <= tol_us / 1000, fx->name);
            checked++;
        } else {
            TEST_ASSERT_FALSE_MESSAGE(eng->hit, fx->name);
        }
    }
    TEST_ASSERT_TRUE(checked >= 5);

    int t;
    int64_t trap_truth = 0;
    if ((t = json_obj_get(js, toks, nt, 0, "trap_cms")) >= 0) json_tok_int(js, &toks[t], &trap_truth);
    int64_t trap_err = (int64_t)R->trap_cms - trap_truth;
    if (trap_err < 0) trap_err = -trap_err;
    TEST_ASSERT_TRUE_MESSAGE(trap_err <= 200, fx->name);   /* ≤ 2 m/s */

    printf("[%s drag] gates checked %d, worst gate error %.1f ms, trap %u cm/s (truth %lld)\n",
           fx->name, checked, worst, (unsigned)R->trap_cms, (long long)trap_truth);
    free(toks); free(js); free(out);
}

static void test_drag_fixture_gate_error_bounds(void)
{
    for (int i = 0; i < N_FIX; i++)
        if (FIX[i].bounds && FIX[i].mode == REPLAY_MODE_DRAG) check_drag_bounds(&FIX[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_replay_json_matches_expected_fixtures);
    RUN_TEST(test_lap_fixture_crossing_error_bounds);
    RUN_TEST(test_drag_fixture_gate_error_bounds);
    return UNITY_END();
}
