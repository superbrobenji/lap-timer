#include "replay/synth_gps.h"
#include "replay/synth.h"
#include "core/consts.h"
#include "core/tb.h"
#include <math.h>
#include <string.h>

/* Deterministic GPS/fused sampling of a synth_run_t (spec §22.2).
 *
 * Everything here is reproducible from cfg.seed alone: the RNG is integer-only splitmix64, the
 * noise is applied with IEEE-754 doubles in a fixed order, and the number of RNG draws per sample
 * never depends on whether the sample is emitted (see synth_gps_next). */

/* splitmix64 (Steele, Lea & Flood 2014). The odd increment is 2^64/phi; the two multipliers are the
 * published finalisation constants. Fixed-width integer arithmetic, so the stream is bit-identical
 * on every platform and compiler. */
#define SM64_GAMMA 0x9E3779B97F4A7C15ULL
#define SM64_MIX1  0xBF58476D1CE4E5B9ULL
#define SM64_MIX2  0x94D049BB133111EBULL

/* 2^-53: scales the top 53 bits of a 64-bit word into [0, 1). The largest representable result is
 * (2^53-1)/2^53 < 1, so synth_rng_uniform never returns exactly 1.0. */
#define UNIFORM_SCALE (1.0 / 9007199254740992.0)

/* gps_fix_t.head_e5 is degrees*1e5 over a full turn (spec §12.3), so headings live in [0, 36e6). */
#define HEAD_E5_FULL_TURN 36000000

/* Reported speed accuracy. A u-blox 3D fix with the noise this model injects
 * (cfg.speed_sigma_mps = 0.05 m/s) reports ~50 mm/s; it is constant because the injected speed
 * noise is constant. */
#define SYNTH_GPS_SACC_MMS 50u

/* fix_type for a 3D fix (spec §6.5 requires 3 for a fix to be valid). */
#define SYNTH_GPS_FIX_TYPE_3D 3u

/* Compass heading folded into [0, 360). fmod keeps the sign of its argument, so a negative result
 * is lifted by one turn. */
static double wrap360_deg(double deg)
{
    double d = fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

void synth_rng_seed(synth_rng_t *g, uint64_t seed)
{
    g->s = seed;
}

static uint64_t rng_next(synth_rng_t *g)
{
    g->s += SM64_GAMMA;
    uint64_t z = g->s;
    z = (z ^ (z >> 30)) * SM64_MIX1;
    z = (z ^ (z >> 27)) * SM64_MIX2;
    return z ^ (z >> 31);
}

uint32_t synth_rng_u32(synth_rng_t *g)
{
    /* the high half is the best-mixed part of a splitmix64 word */
    return (uint32_t)(rng_next(g) >> 32);
}

double synth_rng_uniform(synth_rng_t *g)
{
    return (double)(rng_next(g) >> 11) * UNIFORM_SCALE;
}

double synth_rng_gauss(synth_rng_t *g)
{
    /* Marsaglia polar method. The second deviate is deliberately NOT cached: a cached value would
     * make the RNG position depend on how many gauss() calls came before, so adding or removing a
     * draw elsewhere would shift the whole stream in a history-dependent way. */
    double u, v, s2;
    do {
        u = 2.0 * synth_rng_uniform(g) - 1.0;
        v = 2.0 * synth_rng_uniform(g) - 1.0;
        s2 = u * u + v * v;
    } while (s2 >= 1.0 || s2 == 0.0);
    return u * sqrt(-2.0 * log(s2) / s2);
}

void synth_gm_init(synth_gm_t *m, double sigma, double tau_s, synth_rng_t *g)
{
    m->sigma = sigma;
    m->tau_s = tau_s;
    /* Start in the stationary distribution N(0, σ) so there is no warm-up transient in the first
     * seconds of a run. */
    m->x = sigma * synth_rng_gauss(g);
}

double synth_gm_step(synth_gm_t *m, double dt_s, synth_rng_t *g)
{
    /* No time passed: hold the state and draw nothing, so repeated samples at one instant cannot
     * desynchronise the stream. */
    if (dt_s <= 0.0) return m->x;
    /* τ ≤ 0 has no correlation time: degenerate to white noise of the same σ. */
    const double phi = (m->tau_s > 0.0) ? exp(-dt_s / m->tau_s) : 0.0;
    m->x = phi * m->x + m->sigma * sqrt(1.0 - phi * phi) * synth_rng_gauss(g);
    return m->x;
}

void synth_gps_cfg_defaults(synth_gps_cfg_t *c)
{
    c->rate_hz         = 5;
    c->pos_sigma_m     = 1.5;
    c->pos_tau_s       = 20.0;
    c->speed_sigma_mps = 0.05;
    c->head_sigma_deg  = 0.5;
    c->latency_ms      = 80.0;
    c->jitter_ms       = 20.0;
    c->hacc_m          = 1.5;
    c->sats            = 9;
    c->pdop_e2         = 180;
    c->alt_mm          = 100000;
    c->t0_gps_us       = tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0);
    c->t0_mono_us      = 1000000;
    c->dropout_start_s = 0.0;
    c->dropout_end_s   = 0.0;
    c->seed            = 1;
}

void synth_gps_init(synth_gps_t *s, const synth_gps_cfg_t *cfg)
{
    s->cfg = *cfg;
    synth_rng_seed(&s->rng, s->cfg.seed);
    /* east first, then north: the order fixes the stream */
    synth_gm_init(&s->gm_e, s->cfg.pos_sigma_m, s->cfg.pos_tau_s, &s->rng);
    synth_gm_init(&s->gm_n, s->cfg.pos_sigma_m, s->cfg.pos_tau_s, &s->rng);
    s->k = 0;
    s->last_t_s = 0.0;
}

int synth_gps_next(synth_gps_t *s, const synth_run_t *r, gps_fix_t *fix, double *t_true_s)
{
    const double t_k = (double)s->k / (double)s->cfg.rate_hz;
    if (t_k > r->duration_s) return -1;            /* past the end of the run: k does not advance */

    /* Advance the error processes and draw every noise term BEFORE deciding whether this sample is
     * emitted, in this fixed order, so a dropout window changes which fixes appear but never the
     * RNG stream behind them. */
    const double dt = t_k - s->last_t_s;           /* 0 for k = 0 */
    synth_gm_step(&s->gm_e, dt, &s->rng);
    synth_gm_step(&s->gm_n, dt, &s->rng);
    s->last_t_s = t_k;
    const double n_speed = synth_rng_gauss(&s->rng);
    const double n_head  = synth_rng_gauss(&s->rng);
    const double u_jit   = synth_rng_uniform(&s->rng);

    /* Reported for dropped samples too, so a caller can log the gap; the header only promises it
     * for an emitted fix. */
    if (t_true_s) *t_true_s = t_k;

    if (t_k >= s->cfg.dropout_start_s && t_k < s->cfg.dropout_end_s) {
        s->k++;
        return 0;
    }

    synth_state_t st;
    synth_run_state_at(r, t_k, &st);

    double lat_deg, lon_deg;
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg,
                    st.e_m + s->gm_e.x, st.n_m + s->gm_n.x, &lat_deg, &lon_deg);

    memset(fix, 0, sizeof *fix);                   /* zero the padding so identical runs memcmp equal */
    /* 1e6/rate_hz is a whole number of microseconds for 5 and 10 Hz, so integer arithmetic keeps the
     * fix epoch exact for the whole run (no accumulated rounding). */
    fix->gps_us  = s->cfg.t0_gps_us + (int64_t)s->k * 1000000LL / (int64_t)s->cfg.rate_hz;
    fix->mono_us = s->cfg.t0_mono_us + (int64_t)llround(
        t_k * 1e6 + (s->cfg.latency_ms + s->cfg.jitter_ms * (2.0 * u_jit - 1.0)) * 1000.0);
    fix->lat_e7 = (int32_t)llround(lat_deg * 1e7);
    fix->lon_e7 = (int32_t)llround(lon_deg * 1e7);
    fix->alt_mm = s->cfg.alt_mm;

    double v = st.v_mps + s->cfg.speed_sigma_mps * n_speed;
    if (v < 0.0) v = 0.0;                          /* ground speed is an unsigned magnitude */
    fix->gspeed_mms = (int32_t)llround(v * 1000.0);

    /* Wrap in the integer domain as well: llround of a heading a hair under 360° can land on
     * exactly 36e6, which is out of range. */
    int64_t h = llround(wrap360_deg(st.heading_deg + s->cfg.head_sigma_deg * n_head) * 1e5);
    h %= HEAD_E5_FULL_TURN;
    if (h < 0) h += HEAD_E5_FULL_TURN;
    fix->head_e5 = (int32_t)h;

    fix->hacc_mm  = (uint32_t)llround(s->cfg.hacc_m * 1000.0);
    fix->sacc_mms = SYNTH_GPS_SACC_MMS;
    fix->pdop_e2  = s->cfg.pdop_e2;
    fix->fix_type = SYNTH_GPS_FIX_TYPE_3D;
    fix->sats     = s->cfg.sats;
    fix->flags    = (uint8_t)(GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE);
    /* The device logs post-validation fixes; §6.5 invalidity is produced by the dropout options,
     * never by noise, so every emitted synthetic fix is valid. */
    fix->valid    = 1;

    s->k++;
    return 1;
}

void synth_fused_at(const synth_run_t *r, double t_s, int64_t t0_gps_us, int64_t t0_mono_us, fused_sample_t *out)
{
    synth_state_t st;
    synth_run_state_at(r, t_s, &st);

    /* Truth, so the two clocks advance together from the same run-time zero. */
    const int64_t dt_us = (int64_t)llround(t_s * 1e6);
    memset(out, 0, sizeof *out);
    out->mono_us  = t0_mono_us + dt_us;
    out->gps_us   = t0_gps_us + dt_us;
    out->g_lon    = (float)(st.a_lon_mps2 / G_MPS2);
    out->g_lat    = (float)st.g_lat;
    out->g_comb   = (float)hypot((double)out->g_lon, (double)out->g_lat);
    out->lean_deg = (float)st.lean_deg;
    out->yaw_dps  = (float)st.yaw_rate_dps;
    out->flags    = (uint8_t)(FUS_LEAN_VALID | FUS_ORIENT_OK);
}
