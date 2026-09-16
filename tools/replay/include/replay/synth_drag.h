#ifndef REPLAY_SYNTH_DRAG_H
#define REPLAY_SYNTH_DRAG_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "replay/synth_gps.h"
#include "replay/logio.h"

/* Straight-line drag profile for `synth --profile drag` (spec §11, §6.6, §22.2). Host-only C11, no
 * global state. A run stages still, launches at constant a_launch, coasts to a peak just past the
 * 1/4-mile line, then brakes at a_brake to a stop. Everything is closed-form (piecewise
 * constant-acceleration), so every §11.1 gate crossing time is exact and the fixtures drive the drag
 * engine (mode 1) to those analytic gate times.
 *
 * Geometry is a due-north straight from the origin, so lon is constant and lat advances linearly with
 * distance (no libm transcendental touches the emitted fix — the drag fixtures are byte-identical on
 * every platform, unlike the circuit fixtures whose geometry uses sin/cos/atan2). Signs and units
 * follow core/types.h: g_lon is + when accelerating; the fused stream carries FUS_STILL while the
 * vehicle is stopped so the engine can arm (§11.2). Times are double seconds of run time (0 at the
 * first sample); distance is metres from the launch instant (t0). */

#define SYNTH_DRAG_QUARTER_M      402.34   /* §11.1 gate 10, the 1/4-mile trap gate (40234 cm) */
#define SYNTH_DRAG_ACCEL_MARGIN_M 60.0     /* accelerate this far past the trap line before braking, so
                                            * the trap gate is crossed cleanly mid-acceleration */

typedef struct {
    double target_mps;     /* headline speed reached AT the 1/4-mile line (the "0-N" number) */
    double a_launch_mps2;  /* constant launch acceleration (derived from target unless overridden) */
    double a_brake_mps2;   /* constant braking deceleration magnitude after the peak; default 6 */
    double stage_s;        /* still staging time before launch (t0); default 4, ≥ DRAG_ARM_STILL_S */
    double coast_s;        /* still time after the full stop; default 3 */
} synth_drag_cfg_t;
/* target 50 m/s (180 km/h), a_launch derived, a_brake 6, stage 4, coast 3. */
void synth_drag_cfg_defaults(synth_drag_cfg_t *c);

typedef struct {
    synth_drag_cfg_t cfg;
    double a_launch;       /* effective launch acceleration (cfg or target-derived) */
    double t_launch_s;     /* run time of t0 (= stage_s) */
    double t_peak_s;       /* run time when acceleration ends */
    double t_stop_s;       /* run time when the vehicle is back to rest */
    double duration_s;     /* t_stop_s + coast_s */
    double accel_dist_m;   /* distance covered under acceleration (QUARTER + margin) */
    double v_peak_mps;     /* speed at t_peak_s */
    double brake_dist_m;   /* accel_dist_m + braking distance (total distance from t0) */
} synth_drag_run_t;

/* Builds the analytic run. Returns 0, or -1 with a message in err (may be NULL) when the configuration
 * is invalid: non-positive target/accelerations, or a target so low the trap line is never reached. */
int    synth_drag_build(synth_drag_run_t *r, const synth_drag_cfg_t *cfg, char *err, size_t err_cap);

/* Truth at run time t (clamped to [0, duration_s]). Any out pointer may be NULL. dist_m is measured
 * from t0 (0 while staging). still is true while the vehicle is at rest (staging or after the stop). */
void   synth_drag_state_at(const synth_drag_run_t *r, double t_s,
                           double *v_mps, double *a_lon_mps2, double *dist_m, bool *still);

/* Analytic time from t0 to first reach speed v_mps / distance dist_m, in run seconds relative to t0.
 * Returns -1 when the value is never reached during the run. */
double synth_drag_t_at_speed(const synth_drag_run_t *r, double v_mps);
double synth_drag_t_at_dist(const synth_drag_run_t *r, double dist_m);
/* Mean truth speed over dist ∈ [D − TRAP_DIST_M, D] (the §6.6 trap window at the 1/4 line), m/s. */
double synth_drag_trap_mps(const synth_drag_run_t *r);

/* Writes one whole drag session into `w`: SESSION_HDR (mode 1, no venue), TIME_MAP every 60 s, the
 * time-ordered merge of the FIX_* and FUSED streams, END. fused_hz should be 100 for §6.6 timing.
 * Returns 0 and the record counts, or -1 on an invalid configuration or a write error. */
int    synth_drag_generate(const synth_drag_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz,
                           logw_t *w, synth_drag_run_t *run, uint32_t *n_fix, uint32_t *n_fused);
/* `<prefix>.truth.json`: generator, configuration and every analytic gate time (keyed by §11.1 id).
 * Returns 0, or -1 on a write error. */
int    synth_drag_truth_write(FILE *f, const synth_drag_run_t *r, const synth_gps_cfg_t *g,
                              uint32_t fixes_written, uint32_t fused_written);

/* Full `synth --profile drag ...` entry point: parses argv, builds the run and writes <out>.log and
 * <out>.truth.json (no venue side-car). Returns a process exit code (0 ok, 1 I/O, 2 usage). */
int    synth_drag_main(int argc, char **argv);
#endif
