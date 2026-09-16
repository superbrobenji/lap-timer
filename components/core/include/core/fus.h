#ifndef CORE_FUS_H
#define CORE_FUS_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"
#include "core/ses.h"

/* Sensor fusion and calibration (spec §9.2–9.3). Pure C11, no allocation; all state in fus_t.
 *
 * Frames: body = the IMU's own axes as mounted; vehicle = X forward, Y left, Z up. The rotation R has
 * rows x, y, z = the vehicle axes expressed in body coordinates, so vehicle = R · body.
 * Units: accel in g (raw / IMU_ACC_LSB_PER_G); gyro in dps ((raw − gbias) / IMU_GYR_LSB_PER_DPS),
 * converted to rad/s only where the math needs it. Signs (core/types.h): g_lat and lean + right,
 * yaw + left turn, g_lon + accelerating.
 *
 * A zeroed fus_still_t or fus_fwd_t is a valid initialised state (the *_init functions memset). */

#define FUS_CALIB_VERSION   1
#define FUS_ORTHO_TOL       1e-3f                               /* row norm / dot tolerance for a valid R */
#define FUS_STILL_WINDOW_N  (STILL_WINDOW_S * FUSION_HZ)        /* 200 samples per stillness window */
#define FUS_FWD_MIN_SAMPLES (FWD_LEARN_MIN_S * FUSION_HZ)       /* 100 samples before a run counts */
#define FUS_CALIB_F_ORIENT  0x01                                /* ses_calib_t.calib_flags bits */
#define FUS_CALIB_F_FORWARD 0x02
#define FUS_CALIB_F_BIAS    0x04

typedef struct {
    float   r[9];             /* rows x, y, z (row-major) */
    float   gbias[3];         /* gyro bias, raw LSB */
    int16_t gbias_temp_c100;  /* IMU temperature when gbias was captured */
    uint8_t bias_ok;          /* gbias captured at least once */
    uint8_t orient_ok;        /* z row captured */
    uint8_t forward_ok;       /* x and y rows learned (implies orient_ok) */
    uint8_t version;          /* FUS_CALIB_VERSION */
} fus_calib_t;
void fus_calib_defaults(fus_calib_t *c);                        /* identity R, zero bias, flags 0, current version */
/* version matches, every value finite, |gbias| < 32768, forward_ok implies orient_ok, R orthonormal within FUS_ORTHO_TOL */
bool fus_calib_valid(const fus_calib_t *c);
/* CALIB record (§12.3): r × 1e4 → int16, gbias rounded and clamped to int16, flags FUS_CALIB_F_*. The record
 * carries no temperature: fus_calib_from_ses sets gbias_temp_c100 = 0 and version = FUS_CALIB_VERSION. */
void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out);
void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out);

/* ---- Stillness detector: tumbling windows of FUS_STILL_WINDOW_N raw samples (§9.2) ---- */
typedef struct {
    double   sum_amag, sum_amag2;    /* |accel| in g over the current window */
    double   sum_g[3], sum_g2[3];    /* gyro in dps (bias not removed) */
    double   sum_acc[3];             /* accel in g, for the orientation mean */
    double   sum_graw[3];            /* gyro raw LSB, for the bias mean */
    uint16_t n;                      /* samples in the current window */
    bool     have_window;            /* a window has completed at least once */
    bool     still;                  /* the last completed window was still */
    float    acc_var;                /* last completed window: variance of |a|, g² */
    float    gyr_var_max;            /* last completed window: largest gyro axis variance, dps² */
    float    mean_acc[3];            /* last completed window: mean accel, g (body) */
    float    mean_graw[3];           /* last completed window: mean gyro, raw LSB */
} fus_still_t;
void fus_still_init(fus_still_t *s);
/* Accumulates one raw sample. Returns 1 when this sample completed a window (the last-window fields are
 * then updated and the window restarts), else 0. still = acc_var < STILL_ACC_VAR && gyr_var_max < STILL_GYRO_VAR. */
int  fus_still_push(fus_still_t *s, const imu_raw_t *raw);

/* ---- Orientation rows (§9.2) ---- */
/* z = normalize(mean_acc); rows x, y become a provisional orthonormal completion (the body axis least aligned
 * with z, projected); orient_ok = 1, forward_ok = 0. Returns -1 (calib untouched) unless
 * FUS_ORIENT_MIN_G ≤ |mean_acc| ≤ FUS_ORIENT_MAX_G. */
int  fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3]);
/* x = normalize(sum_ah − (sum_ah·z)z), y = z × x, x = y × z; forward_ok = 1. Returns -1 (calib untouched) if
 * !orient_ok or the horizontal component is shorter than 1e-6. */
int  fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3]);
void fus_rotate(const float r[9], const float b[3], float v[3]);   /* v = R · b */

/* ---- Forward-axis learning window tracker (§9.2) ---- */
typedef struct {
    bool     cond;            /* the latest fix qualifies: |yaw| < FWD_LEARN_MAX_YAW_DPS and gps_acc > FWD_LEARN_ACC_MPS2 */
    uint32_t run_samples;     /* consecutive fus_step samples with cond true */
    double   run_sum[3];      /* a_h accumulated during the current run (g, body) */
    double   sum[3];          /* a_h accumulated over counted runs */
    uint8_t  windows;         /* runs counted so far */
    bool     counted;         /* the current run has been counted (it reached FUS_FWD_MIN_SAMPLES) */
} fus_fwd_t;
void fus_fwd_init(fus_fwd_t *w);
/* Per GPS fix: sets cond. A run ends when cond turns false; its samples count only if it was counted. */
void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps);
/* Per fus_step while orientation is known: accumulates a_h = a − (a·z)z when cond. When a run reaches
 * FUS_FWD_MIN_SAMPLES it is counted (its run_sum so far moves into sum, later samples add to sum directly).
 * Returns 1 exactly once, when the FWD_LEARN_WINDOWS-th run is counted; else 0. */
int  fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3]);
bool fus_fwd_ready(const fus_fwd_t *w);                          /* windows >= FWD_LEARN_WINDOWS */

/* ---- Fusion state ---- */
typedef struct {
    fus_calib_t calib;
    uint8_t     moto;                /* variant: 1 moto (lean filter), 0 car */
    fus_still_t still;
    fus_fwd_t   fwd;
    float       v_mps;               /* latest GPS speed (fus_set_gps_speed) */
    int64_t     v_mono_us;
    bool        v_valid;
    int16_t     temp_c100;           /* latest IMU temperature (fus_set_temp) */
    bool        temp_known;
    bool        bias_stale;          /* |temp − gbias_temp| > BIAS_TEMP_STALE_C since the last bias update */
    bool        fwd_learned_pending; /* set by fus_step when the forward row was just learned; consumed by fus_calib_forward_step */
    float       lean_rad;            /* session 2.3: lean filter state */
    int64_t     last_ref_mono_us;    /* session 2.3: last time a lean reference was applied */
    uint32_t    samples;             /* fus_step calls since init */
} fus_t;

/* calib NULL or invalid → defaults. */
void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto);
void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid);
/* IMU temperature (~1 Hz from the pipeline). Sets bias_stale when a captured bias is more than
 * BIAS_TEMP_STALE_C away from its capture temperature. */
void fus_set_temp(fus_t *f, int16_t temp_c100);
/* Processes one raw sample into out; always produces a sample and returns 1. out->gps_us is left 0 for the
 * pipeline to fill from the time base. */
int  fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out);
bool fus_is_still(const fus_t *f);                               /* last completed stillness window was still */
/* gbias = last still window's mean raw gyro, gbias_temp = current temperature (0 if unknown), bias_ok = 1,
 * bias_stale cleared. No-op unless fus_is_still. */
void fus_gyro_bias_update(fus_t *f);
/* Upright capture (menu action): z row from the last still window's mean accel. 0 ok / -1 not still or
 * fus_orient_from_gravity rejected the mean. */
int  fus_calib_orient_capture(fus_t *f);
/* Per GPS fix: feeds the forward-learning tracker. Returns 1 when the forward row was just learned (caller
 * persists the calibration and emits EV_CALIB_DONE), 0 otherwise, -1 if orientation is not captured. */
int  fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps);
const fus_calib_t *fus_calib(const fus_t *f);
#endif
