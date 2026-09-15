#include "unity.h"
#include "replay/synth_gps.h"
#include "replay/synth.h"
#include "core/geo.h"
#include "core/consts.h"
#include "core/tb.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* synth_run_t holds every lap table (a few MB): heap, never the stack. */
static synth_run_t *run_new(void)
{
    synth_cfg_t c;
    synth_cfg_defaults(&c);
    synth_run_t *r = (synth_run_t *)malloc(sizeof *r);
    TEST_ASSERT_NOT_NULL(r);
    char err[128];
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, synth_run_build(r, &c, err, sizeof err), err);
    TEST_ASSERT_TRUE(r->duration_s > 0.0);
    return r;
}

/* signed angular difference folded into [-180, 180) */
static double wrap180(double deg)
{
    double d = fmod(deg + 180.0, 360.0);
    if (d < 0.0) d += 360.0;
    return d - 180.0;
}

/* ---- 1. RNG ---------------------------------------------------------------------------------- */

static void test_rng_is_reproducible_and_seed_separated(void)
{
    synth_rng_t a, b;
    synth_rng_seed(&a, 1);
    synth_rng_seed(&b, 1);
    for (int i = 0; i < 1000; i++) TEST_ASSERT_EQUAL_UINT32(synth_rng_u32(&a), synth_rng_u32(&b));

    synth_rng_t s1, s2;
    synth_rng_seed(&s1, 1);
    synth_rng_seed(&s2, 2);
    /* splitmix64 mixes the whole 64-bit state, so adjacent seeds cannot collide in the first word */
    TEST_ASSERT_NOT_EQUAL_UINT32(synth_rng_u32(&s1), synth_rng_u32(&s2));
}

static void test_rng_uniform_is_uniform_on_the_unit_interval(void)
{
    enum { N = 200000 };
    synth_rng_t g;
    synth_rng_seed(&g, 7);
    double sum = 0.0, lo = 2.0, hi = -1.0;
    for (int i = 0; i < N; i++) {
        double u = synth_rng_uniform(&g);
        sum += u;
        if (u < lo) lo = u;
        if (u > hi) hi = u;
    }
    /* mean 1/2; SE = (1/sqrt(12))/sqrt(N) = 6.5e-4, so 0.005 is ~7.7 SE. This seed gives 0.50054. */
    TEST_ASSERT_DOUBLE_WITHIN(0.005, 0.5, sum / (double)N);
    TEST_ASSERT_TRUE(lo >= 0.0);
    TEST_ASSERT_TRUE(hi < 1.0);
}

static void test_rng_gauss_is_standard_normal(void)
{
    enum { N = 200000 };
    synth_rng_t g;
    synth_rng_seed(&g, 11);
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < N; i++) {
        double x = synth_rng_gauss(&g);
        sum += x;
        sum2 += x * x;
    }
    double mean = sum / (double)N;
    double var = (sum2 - (double)N * mean * mean) / (double)(N - 1);
    /* SE(mean) = 1/sqrt(N) = 2.2e-3 so 0.01 is 4.5 SE; SE(sigma) = 1/sqrt(2N) = 1.6e-3 so 0.01 is
     * 6 SE. This seed gives mean -0.00099 and sigma 1.00170. */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, mean);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, sqrt(var));
}

/* ---- 2. Gauss-Markov ------------------------------------------------------------------------- */

static void test_gm_matches_its_stationary_sigma_and_correlation_time(void)
{
    enum { N = 400000, LAG = 100 };                 /* dt = 0.2 s → LAG = 20 s = tau */
    const double sigma = 1.5, tau = 20.0, dt = 0.2;
    double *x = (double *)malloc(sizeof(double) * (size_t)N);
    TEST_ASSERT_NOT_NULL(x);
    synth_rng_t g;
    synth_rng_seed(&g, 3);
    synth_gm_t m;
    synth_gm_init(&m, sigma, tau, &g);
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        x[i] = synth_gm_step(&m, dt, &g);
        sum += x[i];
    }
    double mean = sum / (double)N;
    double var = 0.0;
    for (int i = 0; i < N; i++) var += (x[i] - mean) * (x[i] - mean);
    var /= (double)(N - 1);
    /* Samples are correlated: n_eff = N*dt/(2*tau) = 2000, so SE(sigma)/sigma = 1/sqrt(2*n_eff) ~ 1.6 %
     * = 0.024 m and 0.05 is ~2 SE. This seed gives 1.47298. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, sigma, sqrt(var));

    double num = 0.0, den = 0.0;
    for (int i = 0; i + LAG < N; i++) num += (x[i] - mean) * (x[i + LAG] - mean);
    for (int i = 0; i < N; i++) den += (x[i] - mean) * (x[i] - mean);
    /* rho(tau) = exp(-1) = 0.36788 for a first-order Gauss-Markov process; 0.05 is ~2 SE at
     * n_eff = 2000. This seed gives 0.36361. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, exp(-1.0), num / den);
    free(x);
}

static void test_gm_zero_dt_holds_and_a_long_gap_decorrelates(void)
{
    synth_rng_t g;
    synth_rng_seed(&g, 5);
    synth_gm_t m;
    synth_gm_init(&m, 1.5, 20.0, &g);
    uint64_t state_before = g.s;
    double x0 = m.x;
    TEST_ASSERT_EQUAL_DOUBLE(x0, synth_gm_step(&m, 0.0, &g));
    TEST_ASSERT_EQUAL_DOUBLE(x0, m.x);
    TEST_ASSERT_EQUAL_UINT64(state_before, g.s);     /* dt = 0 draws nothing */
    TEST_ASSERT_EQUAL_DOUBLE(x0, synth_gm_step(&m, -1.0, &g));

    enum { T = 10000 };
    double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
    for (int i = 0; i < T; i++) {
        synth_gm_t n;
        synth_gm_init(&n, 1.5, 20.0, &g);
        double before = n.x;
        double after = synth_gm_step(&n, 1e6, &g);   /* 50 000 tau: phi underflows to 0 */
        sa += before; sb += after; saa += before * before; sbb += after * after; sab += before * after;
    }
    double n_d = (double)T;
    double cov = sab / n_d - (sa / n_d) * (sb / n_d);
    double corr = cov / sqrt((saa / n_d - (sa / n_d) * (sa / n_d)) * (sbb / n_d - (sb / n_d) * (sb / n_d)));
    /* independent samples: SE of a correlation estimate is 1/sqrt(T) = 0.01, so 0.05 is 5 SE.
     * This seed gives -0.0018. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.0, corr);
}

/* ---- 3. Timing ------------------------------------------------------------------------------- */

static void test_fix_epochs_are_exact_and_strictly_increasing(void)
{
    synth_run_t *r = run_new();
    const int rates[2] = { 5, 10 };
    const int64_t step_us[2] = { 200000, 100000 };
    for (int j = 0; j < 2; j++) {
        synth_gps_cfg_t c;
        synth_gps_cfg_defaults(&c);
        c.rate_hz = rates[j];
        synth_gps_t s;
        synth_gps_init(&s, &c);
        int64_t prev = 0;
        for (int k = 0; k < 10; k++) {
            gps_fix_t f;
            double t_true = -1.0;
            TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&s, r, &f, &t_true));
            TEST_ASSERT_EQUAL_INT64(c.t0_gps_us + (int64_t)k * step_us[j], f.gps_us);
            TEST_ASSERT_DOUBLE_WITHIN(1e-12, (double)k / rates[j], t_true);
            if (k > 0) TEST_ASSERT_TRUE(f.gps_us > prev);
            prev = f.gps_us;
        }
    }
    free(r);
}

static void test_arrival_time_is_latency_plus_bounded_jitter(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 5;
    synth_gps_t s;
    synth_gps_init(&s, &c);
    const int64_t lo_us = (int64_t)((c.latency_ms - c.jitter_ms) * 1000.0) - 1;   /* -1: llround slack */
    const int64_t hi_us = (int64_t)((c.latency_ms + c.jitter_ms) * 1000.0) + 1;
    gps_fix_t f;
    double t_true = 0.0;
    int n = 0;
    int64_t seen_lo = INT64_MAX, seen_hi = INT64_MIN;
    while (synth_gps_next(&s, r, &f, &t_true) >= 0) {
        int64_t d = f.mono_us - c.t0_mono_us - (int64_t)llround(t_true * 1e6);
        TEST_ASSERT_TRUE(d >= lo_us);
        TEST_ASSERT_TRUE(d <= hi_us);
        if (d < seen_lo) seen_lo = d;
        if (d > seen_hi) seen_hi = d;
        n++;
    }
    TEST_ASSERT_TRUE(n > 100);
    /* U(-jitter, +jitter) over >100 draws must cover well over half the 40 ms band */
    TEST_ASSERT_TRUE(seen_hi - seen_lo > 20000);
    free(r);
}

/* ---- 4. Dropout ------------------------------------------------------------------------------ */

static void test_dropout_window_skips_fixes_without_moving_the_rng_stream(void)
{
    synth_run_t *r = run_new();
    TEST_ASSERT_TRUE(r->duration_s > 20.0);          /* the window and the fix after it must fit */

    synth_gps_cfg_t base;
    synth_gps_cfg_defaults(&base);
    base.rate_hz = 5;
    synth_gps_cfg_t cut = base;
    cut.dropout_start_s = 10.0;
    cut.dropout_end_s = 12.0;

    synth_gps_t sa, sb;
    synth_gps_init(&sa, &base);
    synth_gps_init(&sb, &cut);

    int zeros = 0, compared = 0;
    int64_t first_after_us = 0;
    for (int k = 0; k < 100; k++) {                  /* 0 .. 19.8 s */
        gps_fix_t fa, fb;
        double ta = 0.0, tb = 0.0;
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&sa, r, &fa, &ta));
        int rb = synth_gps_next(&sb, r, &fb, &tb);
        TEST_ASSERT_EQUAL_UINT32(sa.k, sb.k);        /* k advances through the window too */
        TEST_ASSERT_EQUAL_DOUBLE(ta, tb);
        if (ta >= 10.0 && ta < 12.0) {
            TEST_ASSERT_EQUAL_INT(0, rb);
            zeros++;
        } else {
            TEST_ASSERT_EQUAL_INT(1, rb);
            TEST_ASSERT_EQUAL_INT(0, memcmp(&fa, &fb, sizeof fa));
            if (zeros == 10 && first_after_us == 0) first_after_us = fb.gps_us;
            compared++;
        }
    }
    /* [10, 12) at 5 Hz holds t = 10.0 .. 11.8 inclusive = 10 samples */
    TEST_ASSERT_EQUAL_INT(10, zeros);
    TEST_ASSERT_EQUAL_INT(90, compared);
    TEST_ASSERT_EQUAL_INT64(base.t0_gps_us + 12000000LL, first_after_us);
    free(r);
}

/* ---- 5. End of run --------------------------------------------------------------------------- */

static void test_sampling_stops_one_step_past_the_run_duration(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 5;
    synth_gps_t s;
    synth_gps_init(&s, &c);
    gps_fix_t f;
    uint32_t last_k = 0;
    while (synth_gps_next(&s, r, &f, NULL) >= 0) {
        last_k = s.k;
        TEST_ASSERT_TRUE(last_k < 1000000u);         /* guards against a non-terminating loop */
    }
    /* the last emitted sample is at or before duration, the next one is past it */
    TEST_ASSERT_TRUE((double)(last_k - 1) / c.rate_hz <= r->duration_s);
    TEST_ASSERT_TRUE((double)last_k / c.rate_hz > r->duration_s);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(-1, synth_gps_next(&s, r, &f, NULL));
        TEST_ASSERT_EQUAL_UINT32(last_k, s.k);       /* k frozen after the end */
    }
    free(r);
}

/* ---- 6. Field packing ------------------------------------------------------------------------ */

static void test_every_emitted_fix_carries_the_configured_constant_fields(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    synth_gps_t s;
    synth_gps_init(&s, &c);
    gps_fix_t f;
    int n = 0;
    while (synth_gps_next(&s, r, &f, NULL) >= 0) {
        TEST_ASSERT_EQUAL_UINT8(3, f.fix_type);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE), f.flags);
        TEST_ASSERT_EQUAL_UINT8(c.sats, f.sats);
        TEST_ASSERT_EQUAL_UINT32(1500u, f.hacc_mm);          /* hacc_m 1.5 m -> mm */
        TEST_ASSERT_EQUAL_UINT32(50u, f.sacc_mms);
        TEST_ASSERT_EQUAL_UINT16(c.pdop_e2, f.pdop_e2);
        TEST_ASSERT_EQUAL_INT32(c.alt_mm, f.alt_mm);
        TEST_ASSERT_EQUAL_UINT8(1, f.valid);
        TEST_ASSERT_TRUE(f.head_e5 >= 0 && f.head_e5 < 36000000);
        TEST_ASSERT_TRUE(f.gspeed_mms >= 0);
        /* §6.5 rejects a fix whose hacc or sats fail; a synthetic fix must always pass */
        TEST_ASSERT_TRUE(f.hacc_mm <= (uint32_t)FIX_HACC_MAX_M * 1000u);
        TEST_ASSERT_TRUE(f.sats >= FIX_MIN_SATS);
        n++;
    }
    TEST_ASSERT_TRUE(n > 100);
    free(r);
}

/* ---- 7. Noise statistics --------------------------------------------------------------------- */

/* The position error is a tau = 20 s process, so one 100 s pass holds only ~3 independent samples.
 * Pooling PATHS independent seeds of SAMPLES fixes each gives n_eff ~ PATHS*SAMPLES*dt/(2*tau) = 500
 * while keeping every pass inside the shortest run this suite builds. */
#define NOISE_PATHS   200
#define NOISE_SAMPLES 1000                                   /* at 10 Hz = 100 s per path */

static void test_injected_noise_matches_the_configured_distributions(void)
{
    synth_run_t *r = run_new();
    TEST_ASSERT_TRUE(r->duration_s >= 100.0);
    geo_origin_t o;
    geo_origin_set(&o, r->cfg.origin_lat_deg, r->cfg.origin_lon_deg);

    double se = 0.0, sn = 0.0, se2 = 0.0, sn2 = 0.0;
    double sv = 0.0, sv2 = 0.0, sh = 0.0, sh2 = 0.0;
    long n = 0;
    for (int p = 0; p < NOISE_PATHS; p++) {
        synth_gps_cfg_t c;
        synth_gps_cfg_defaults(&c);
        c.rate_hz = 10;
        c.seed = (uint32_t)(1000 + p);
        synth_gps_t s;
        synth_gps_init(&s, &c);
        for (int i = 0; i < NOISE_SAMPLES; i++) {
            gps_fix_t f;
            double t = 0.0;
            TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&s, r, &f, &t));
            synth_state_t st;
            synth_run_state_at(r, t, &st);
            geo_enu_t e = geo_to_enu(&o, (double)f.lat_e7 * 1e-7, (double)f.lon_e7 * 1e-7);
            double de = e.x - st.e_m, dn = e.y - st.n_m;
            double dv = (double)f.gspeed_mms * 1e-3 - st.v_mps;
            double dh = wrap180((double)f.head_e5 * 1e-5 - st.heading_deg);
            se += de; sn += dn; se2 += de * de; sn2 += dn * dn;
            sv += dv; sv2 += dv * dv; sh += dh; sh2 += dh * dh;
            n++;
        }
    }
    double nd = (double)n;
    TEST_ASSERT_EQUAL_INT32(NOISE_PATHS * NOISE_SAMPLES, (int32_t)n);

    /* Position: RMS about truth is the configured sigma. n_eff ~ 500 -> SE(sigma)/sigma ~ 3.2 %
     * = 0.047 m and SE(mean) = 1.5/sqrt(500) = 0.067 m, so 0.15 m is ~3 SE and ~2 SE. These seeds
     * give rms 1.5242 / 1.4582 and mean -0.0112 / 0.0787. The error stream depends only on the
     * sample times, not on the track, so these numbers do not move with the circuit geometry. */
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 1.5, sqrt(se2 / nd));
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 1.5, sqrt(sn2 / nd));
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 0.0, se / nd);
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 0.0, sn / nd);

    /* Speed and heading noise are white: every one of the 200 000 samples is independent, so
     * SE(sigma_v) = 0.05/sqrt(2n) = 8e-5 and SE(sigma_h) = 0.5/sqrt(2n) = 8e-4. These seeds give
     * 0.049958 m/s and 0.500271 deg. */
    double var_v = (sv2 - nd * (sv / nd) * (sv / nd)) / (nd - 1.0);
    double var_h = (sh2 - nd * (sh / nd) * (sh / nd)) / (nd - 1.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.005, 0.05, sqrt(var_v));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.5, sqrt(var_h));
    free(r);
}

static void test_the_same_seed_reproduces_fixes_byte_for_byte(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 10;
    c.seed = 424242;
    synth_gps_t a, b;
    synth_gps_init(&a, &c);
    synth_gps_init(&b, &c);
    for (int i = 0; i < 500; i++) {
        gps_fix_t fa, fb;
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&a, r, &fa, NULL));
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&b, r, &fb, NULL));
        TEST_ASSERT_EQUAL_INT(0, memcmp(&fa, &fb, sizeof fa));
    }
    free(r);
}

/* ---- 8. Fused truth -------------------------------------------------------------------------- */

static void test_fused_truth_mirrors_the_state_with_no_noise(void)
{
    synth_run_t *r = run_new();
    const int64_t t0_gps = tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0);
    const int64_t t0_mono = 1000000;
    int straights = 0, arcs = 0;
    for (int i = 0; i <= 1000; i++) {
        double t = r->duration_s * (double)i / 1000.0;
        synth_state_t st;
        synth_run_state_at(r, t, &st);
        fused_sample_t fs;
        synth_fused_at(r, t, t0_gps, t0_mono, &fs);

        TEST_ASSERT_EQUAL_INT64(t0_gps + (int64_t)llround(t * 1e6), fs.gps_us);
        TEST_ASSERT_EQUAL_INT64(t0_mono + (int64_t)llround(t * 1e6), fs.mono_us);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(FUS_LEAN_VALID | FUS_ORIENT_OK), fs.flags);
        /* g_lon is the state's longitudinal acceleration in g (spec §9.3) */
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)(st.a_lon_mps2 / G_MPS2), fs.g_lon);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.g_lat, fs.g_lat);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.lean_deg, fs.lean_deg);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.yaw_rate_dps, fs.yaw_dps);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)hypot((double)fs.g_lon, (double)fs.g_lat), fs.g_comb);

        if (!st.on_arc) {
            /* straights are driven in a straight line: no lateral load, no lean, no yaw */
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.g_lat);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.lean_deg);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.yaw_dps);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)fabs((double)fs.g_lon), (float)fabs((double)fs.g_comb));
            straights++;
        } else if (fabs((double)fs.g_lat) > 1e-6) {
            /* a balanced two-wheeler leans to atan(g_lat) (spec §9.3) */
            TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(atan((double)fs.g_lat) * 180.0 / GEO_PI), fs.lean_deg);
            /* +lat = right, +yaw = left turn (core/types.h), so a corner loads them oppositely */
            TEST_ASSERT_TRUE((double)fs.g_lat * (double)fs.yaw_dps < 0.0);
            arcs++;
        }
    }
    TEST_ASSERT_TRUE(straights > 0);
    TEST_ASSERT_TRUE(straights + arcs > 0);
    free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rng_is_reproducible_and_seed_separated);
    RUN_TEST(test_rng_uniform_is_uniform_on_the_unit_interval);
    RUN_TEST(test_rng_gauss_is_standard_normal);
    RUN_TEST(test_gm_matches_its_stationary_sigma_and_correlation_time);
    RUN_TEST(test_gm_zero_dt_holds_and_a_long_gap_decorrelates);
    RUN_TEST(test_fix_epochs_are_exact_and_strictly_increasing);
    RUN_TEST(test_arrival_time_is_latency_plus_bounded_jitter);
    RUN_TEST(test_dropout_window_skips_fixes_without_moving_the_rng_stream);
    RUN_TEST(test_sampling_stops_one_step_past_the_run_duration);
    RUN_TEST(test_every_emitted_fix_carries_the_configured_constant_fields);
    RUN_TEST(test_injected_noise_matches_the_configured_distributions);
    RUN_TEST(test_the_same_seed_reproduces_fixes_byte_for_byte);
    RUN_TEST(test_fused_truth_mirrors_the_state_with_no_noise);
    return UNITY_END();
}
