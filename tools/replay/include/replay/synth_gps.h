#ifndef REPLAY_SYNTH_GPS_H
#define REPLAY_SYNTH_GPS_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "replay/synth.h"

/* Deterministic sampling of a synth_run_t into GPS fixes and fused samples (spec §22.2).
 * Same seed → byte-identical output on every platform (integer RNG, IEEE doubles, no libm
 * randomness). */

typedef struct { uint64_t s; } synth_rng_t;              /* splitmix64 */
void     synth_rng_seed(synth_rng_t *g, uint64_t seed);
uint32_t synth_rng_u32(synth_rng_t *g);
double   synth_rng_uniform(synth_rng_t *g);              /* [0, 1) with 53 random bits */
double   synth_rng_gauss(synth_rng_t *g);                /* N(0, 1), Box–Muller (polar form, no cached value) */

/* First-order Gauss–Markov error: x_{k+1} = x_k·exp(-dt/τ) + σ·sqrt(1 - exp(-2dt/τ))·N(0,1). */
typedef struct { double sigma, tau_s, x; } synth_gm_t;
void   synth_gm_init(synth_gm_t *m, double sigma, double tau_s, synth_rng_t *g);   /* stationary start: x ~ N(0, σ) */
double synth_gm_step(synth_gm_t *m, double dt_s, synth_rng_t *g);                 /* advances and returns x */

typedef struct {
    int      rate_hz;               /* 5 or 10; default 5 */
    double   pos_sigma_m;           /* default 1.5 */
    double   pos_tau_s;             /* default 20 */
    double   speed_sigma_mps;       /* default 0.05 */
    double   head_sigma_deg;        /* default 0.5 */
    double   latency_ms;            /* mean arrival latency; default 80 */
    double   jitter_ms;             /* arrival = gps time + latency + U(-jitter, +jitter); default 20 */
    double   hacc_m;                /* reported hacc; default 1.5 */
    uint8_t  sats;                  /* default 9 */
    uint16_t pdop_e2;               /* default 180 */
    int32_t  alt_mm;                /* reported altitude (constant); default 100000 */
    int64_t  t0_gps_us;             /* UTC of run time 0; default tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0) */
    int64_t  t0_mono_us;            /* monotonic clock at run time 0; default 1000000 */
    double   dropout_start_s, dropout_end_s;   /* no fixes while start ≤ t < end; default 0, 0 (none) */
    uint32_t seed;                  /* default 1 */
} synth_gps_cfg_t;
void synth_gps_cfg_defaults(synth_gps_cfg_t *c);

typedef struct {
    synth_gps_cfg_t cfg;
    synth_rng_t     rng;
    synth_gm_t      gm_e, gm_n;
    uint32_t        k;              /* index of the next sample; t_k = k / rate_hz */
    double          last_t_s;       /* time of the previous Gauss–Markov step */
} synth_gps_t;
void synth_gps_init(synth_gps_t *s, const synth_gps_cfg_t *cfg);

/* Produces the next fix at t_k = k / rate_hz. Returns 1 and fills fix (and t_true_s if not NULL);
 * returns 0 when t_k lies in the dropout window (k still advances, the error process still steps);
 * returns -1 when t_k > synth run duration. Fields: gps_us = t0_gps_us + k·1e6/rate (exact integer),
 * mono_us = t0_mono_us + t_k·1e6 + latency + jitter, lat/lon from truth + Gauss–Markov error,
 * alt_mm = cfg.alt_mm, gspeed_mms = truth + N(0, σv), head_e5 = truth + N(0, σh) wrapped to [0, 360),
 * hacc_mm = hacc_m·1000, sacc_mms = 50, pdop_e2, fix_type 3, sats, flags = FIXOK|TIME|DATE, valid 1. */
int  synth_gps_next(synth_gps_t *s, const synth_run_t *r, gps_fix_t *fix, double *t_true_s);

/* Truth fused sample at run time t (no noise): mono_us/gps_us from the same t0 pair, g_lon = a_lon/G,
 * g_lat, lean, yaw from the state, g_comb = sqrt(g_lon² + g_lat²), flags = FUS_LEAN_VALID|FUS_ORIENT_OK. */
void synth_fused_at(const synth_run_t *r, double t_s, int64_t t0_gps_us, int64_t t0_mono_us, fused_sample_t *out);
#endif
