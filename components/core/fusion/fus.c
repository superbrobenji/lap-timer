#include "core/fus.h"
#include "core/core.h"
#include <math.h>
#include <string.h>

/* Power-of-10 rule 5 assertion code for the fusion module (design §3): the hook records
 * __FILE__/__LINE__, so this one per-module code plus file:line pins the exact failing check. */
#define FUS_ASSERT_CODE 0x0A10

/* Degree/radian conversion factors for the §9.3 lean/yaw algebra (no bare literals below). */
#define RAD_PER_DEG 0.017453292519943295f   /* pi / 180 */
#define DEG_PER_RAD 57.295779513082323f     /* 180 / pi */

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    CORE_ASSERT_RET(a != NULL, FUS_ASSERT_CODE, 0.0f);
    CORE_ASSERT_RET(b != NULL, FUS_ASSERT_CODE, 0.0f);
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Largest n any caller passes (the 9-element rotation R); the bound makes the loop's iteration
 * count statically evident (rule 2) and a larger n a caught anomaly rather than an over-read. */
#define FUS_ALL_FINITE_MAX_N 9

static bool all_finite(const float *v, int n)
{
    CORE_ASSERT_RET(v != NULL, FUS_ASSERT_CODE, false);
    CORE_ASSERT_RET(n >= 0 && n <= FUS_ALL_FINITE_MAX_N, FUS_ASSERT_CODE, false);
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    CORE_ASSERT_VOID(c != NULL, FUS_ASSERT_CODE);
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    CORE_ASSERT_RET(c != NULL, FUS_ASSERT_CODE, false);
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    /* A non-finite v would fall through both bounds into lroundf(NaN), which is undefined; the
     * value fed here is always a finite calibration quantity, so a NaN/Inf is a real anomaly. */
    CORE_ASSERT_RET(isfinite(v), FUS_ASSERT_CODE, 0);
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    CORE_ASSERT_VOID(c != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, FUS_ASSERT_CODE);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    CORE_ASSERT_VOID(in != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, FUS_ASSERT_CODE);
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    CORE_ASSERT_VOID(r != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(b != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(v != NULL, FUS_ASSERT_CODE);
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);   /* calib may be NULL: documented as "use defaults" */
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
    /* sentinels the zero from memset does not express (fus.h) */
    f->last_ref_mono_us = -1;
    f->yaw_gps_dps = NAN;
    f->yaw_gps_mono_us = -1;
    f->disagree_since_mono_us = -1;
}

/* Smallest signed compass difference cur - prev, wrapped to (-180, 180]. */
static float course_delta_deg(float cur_deg, float prev_deg)
{
    /* Compass courses from a GPS fix; a NaN would propagate silently through the wrap below. */
    CORE_ASSERT_RET(isfinite(cur_deg), FUS_ASSERT_CODE, 0.0f);
    CORE_ASSERT_RET(isfinite(prev_deg), FUS_ASSERT_CODE, 0.0f);
    float d = fmodf(cur_deg - prev_deg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d <= -180.0f) d += 360.0f;
    return d;
}

float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us)
{
    CORE_ASSERT_RET(isfinite(prev_course_deg), FUS_ASSERT_CODE, 0.0f);
    CORE_ASSERT_RET(isfinite(cur_course_deg), FUS_ASSERT_CODE, 0.0f);
    const double dt_s = (double)(cur_mono_us - prev_mono_us) / 1e6;
    if (dt_s <= 0.0) return 0.0f;
    /* compass heading rises clockwise (a right turn), the vehicle yaw is + to the left, so negate */
    return (float)(-(double)course_delta_deg(cur_course_deg, prev_course_deg) / dt_s);
}

void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(isfinite(v_mps), FUS_ASSERT_CODE);         /* GPS speed feeds the lean reference */
    CORE_ASSERT_VOID(isfinite(course_deg), FUS_ASSERT_CODE);    /* course feeds the yaw-rate derivative */
    if (valid) {
        if (f->have_prev_course && mono_us > f->prev_course_mono_us) {
            f->yaw_gps_dps = fus_yaw_rate_gps_dps(f->prev_course_deg, f->prev_course_mono_us, course_deg, mono_us);
            f->yaw_gps_mono_us = mono_us;
        }
        f->prev_course_deg = course_deg; f->prev_course_mono_us = mono_us; f->have_prev_course = true;
    }
    f->v_mps = v_mps; f->v_course_deg = course_deg; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

/* fus_step is split into named per-stage helpers along the §9.3 filter seams (Power-of-10 rule 4).
 * Each is behavior-identical to the corresponding block of the original monolithic fus_step; the
 * orchestrator at the bottom wires them together in the original order. */

/* §9.3 input conditioning: raw IMU LSB -> vehicle-frame specific force a (g) and angular rate w
 * (dps). a_b (body-frame accel, g) is handed back too: the §9.2 forward-axis learner accumulates
 * it directly, so it must be the same body sample this step rotated. */
static void fus_step_rotate_inputs(const fus_calib_t *calib, const imu_raw_t *raw,
                                   float a_b[3], float a[3], float w[3])
{
    CORE_ASSERT_VOID(calib != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(raw != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a_b != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(w != NULL, FUS_ASSERT_CODE);
    a_b[0] = (float)raw->ax / IMU_ACC_LSB_PER_G;
    a_b[1] = (float)raw->ay / IMU_ACC_LSB_PER_G;
    a_b[2] = (float)raw->az / IMU_ACC_LSB_PER_G;
    const float w_b[3] = { ((float)raw->gx - calib->gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - calib->gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - calib->gbias[2]) / IMU_GYR_LSB_PER_DPS };
    fus_rotate(calib->r, a_b, a);
    fus_rotate(calib->r, w_b, w);
}

/* §9.3 steps 1/3: the earth-frame yaw rate psi-dot. phi is the PREVIOUS step's lean (a causal
 * filter breaks the phi -> psi-dot -> phi_ref -> phi loop); the car variant and any un-oriented
 * moto sample use phi == 0, so psi-dot reduces to omega.z. Returns rad/s, earth frame. */
static float fus_step_yaw_rate(const fus_t *f, const float w[3], bool oriented)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, 0.0f);
    CORE_ASSERT_RET(w != NULL, FUS_ASSERT_CODE, 0.0f);
    const float phi_prev = (f->moto && oriented) ? f->lean_rad : 0.0f;
    const float wy = w[1] * RAD_PER_DEG;
    const float wz = w[2] * RAD_PER_DEG;
    return wz * cosf(phi_prev) + wy * sinf(phi_prev);
}

/* §9.3 steps 2/4/5 + the Appendix A clamp: the in-plane g components (and, on a moto, the
 * complementary lean filter and lateral g), then each in-plane axis bounded to +-G_MAX *before*
 * g_comb is formed so the reported combined magnitude stays consistent with the reported
 * components. *flags accumulates FUS_CLAMPED / FUS_LEAN_VALID. */
static void fus_step_planar(fus_t *f, const imu_raw_t *raw, const float a[3], const float w[3],
                            float psidot, bool oriented, uint8_t *flags, fused_sample_t *out)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(raw != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(w != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(flags != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(isfinite(psidot), FUS_ASSERT_CODE);   /* the lean/lateral algebra assumes a finite turn rate */
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */

        /* GPS reference gate shared by the lean phi_ref and the moto lateral g (§9.3 steps 4/5):
         * a valid, fresh (0 <= age < FUS_REF_MAX_AGE_US) fix above LEAN_REF_MIN_SPEED_MPS. */
        const int64_t v_age = raw->mono_us - f->v_mono_us;
        const bool ref_ok = f->v_valid && f->v_mps > LEAN_REF_MIN_SPEED_MPS &&
                            v_age >= 0 && v_age < FUS_REF_MAX_AGE_US;

        if (f->moto) {
            /* Complementary lean filter (§9.3 step 4): gyro-integrated roll blended toward the
             * GPS-derived reference when one qualifies, else pure gyro (alpha == 1). */
            const float dt = 1.0f / (float)FUSION_HZ;   /* 0.01 s at FUSION_HZ */
            const float wx = w[0] * RAD_PER_DEG;         /* body-forward roll rate, rad/s */
            float phi = f->lean_rad + wx * dt;   /* phi_gyro */
            if (ref_ok) {
                const float phi_ref = atan2f(-f->v_mps * psidot, (float)G_MPS2);
                phi = LEAN_ALPHA * phi + (1.0f - LEAN_ALPHA) * phi_ref;
                f->last_ref_mono_us = raw->mono_us;
            }
            const float lean_max = LEAN_MAX_DEG * RAD_PER_DEG;
            if (phi > lean_max)       { phi =  lean_max; *flags |= FUS_CLAMPED; }
            else if (phi < -lean_max) { phi = -lean_max; *flags |= FUS_CLAMPED; }
            f->lean_rad = phi;
            out->lean_deg = phi * DEG_PER_RAD;

            /* Lateral g (§9.3 step 5, moto): centripetal from the held speed and the fused turn rate
             * when a reference qualifies; otherwise the accelerometer specific force (the Task 1
             * form), which is all that is available with no usable GPS speed. + = right. */
            out->g_lat = ref_ok ? (-f->v_mps * psidot / (float)G_MPS2) : -a[1];

            /* FUS_LEAN_VALID: a reference was applied within LEAN_REF_TIMEOUT_S; never before the
             * first reference (last_ref_mono_us starts at -1). */
            if (f->last_ref_mono_us >= 0 &&
                raw->mono_us - f->last_ref_mono_us < (int64_t)LEAN_REF_TIMEOUT_S * 1000000)
                *flags |= FUS_LEAN_VALID;
        } else {
            /* Car (§9.3 step 5, car): lateral g is the accelerometer specific force. Lean is not
             * meaningful for a car, so lean_deg stays 0 and FUS_LEAN_VALID is never set. */
            out->g_lat = -a[1];
        }

        /* G_MAX clamp (Appendix A, "clamp in §9.3"): bound each in-plane axis to +-G_MAX *before*
         * forming g_comb, so the reported combined magnitude stays consistent with the reported
         * (clamped) components. */
        if (out->g_lon > G_MAX)       { out->g_lon =  G_MAX; *flags |= FUS_CLAMPED; }
        else if (out->g_lon < -G_MAX) { out->g_lon = -G_MAX; *flags |= FUS_CLAMPED; }
        if (out->g_lat > G_MAX)       { out->g_lat =  G_MAX; *flags |= FUS_CLAMPED; }
        else if (out->g_lat < -G_MAX) { out->g_lat = -G_MAX; *flags |= FUS_CLAMPED; }
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;                       /* no lateral, no lean, FUS_LEAN_VALID clear until oriented */
    }
}

/* §9.3 step 4 GPS yaw cross-check. While a fresh GPS turn rate exists above the reference speed, a
 * fused/GPS yaw disagreement beyond LEAN_DISAGREE_DPS that holds for LEAN_DISAGREE_S sets
 * FUS_DISAGREE and latches disagree_latched (the plan-03 pipeline logs E_FUSION_DISAGREE once from
 * the latch). The flag is present only while the disagreement currently holds and its timer has
 * exceeded the hold, and clears as soon as the difference drops back under threshold (timer -> -1);
 * disagree_latched stays set. yaw_dps is the fused rate (out->yaw_dps). */
static void fus_step_yaw_crosscheck(fus_t *f, const imu_raw_t *raw, float yaw_dps, uint8_t *flags)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(raw != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(flags != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(isfinite(yaw_dps), FUS_ASSERT_CODE);   /* fused yaw is finite; the GPS rate may be NAN and is gated below */
    const int64_t yaw_age = raw->mono_us - f->yaw_gps_mono_us;
    const bool yaw_ref_ok = f->v_valid && isfinite(f->yaw_gps_dps) &&
                            yaw_age >= 0 && yaw_age < FUS_REF_MAX_AGE_US &&
                            f->v_mps > LEAN_REF_MIN_SPEED_MPS;
    if (yaw_ref_ok && fabsf(yaw_dps - f->yaw_gps_dps) > LEAN_DISAGREE_DPS) {
        if (f->disagree_since_mono_us < 0) f->disagree_since_mono_us = raw->mono_us;
        if (raw->mono_us - f->disagree_since_mono_us >= (int64_t)LEAN_DISAGREE_S * 1000000) {
            *flags |= FUS_DISAGREE;
            f->disagree_latched = true;
        }
    } else {
        f->disagree_since_mono_us = -1;          /* agreeing, or no fresh GPS turn rate: reset */
    }
}

/* §9.2 forward-axis learning: only between the upright capture and the forward row being known.
 * The rows may change here, after this sample was already rotated with the old ones — deliberate
 * and harmless: the sample stays consistent with the calibration it was computed from (sign-less
 * g_lon, no FUS_ORIENT_OK) and the learned rows take effect from the next sample. a_b is this
 * step's body-frame accel (g). */
static void fus_step_learn_forward(fus_t *f, const float a_b[3])
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(a_b != NULL, FUS_ASSERT_CODE);
    if (f->calib.orient_ok && !f->calib.forward_ok) {
        const float z[3] = { f->calib.r[6], f->calib.r[7], f->calib.r[8] };
        if (fus_fwd_on_sample(&f->fwd, a_b, z) == 1) {
            const float sum[3] = { (float)f->fwd.sum[0], (float)f->fwd.sum[1], (float)f->fwd.sum[2] };
            if (fus_orient_set_forward(&f->calib, sum) == 0) {
                f->fwd_learned_pending = true;   /* fus_calib_forward_step reports it on the next fix */
            } else {
                fus_fwd_init(&f->fwd);           /* degenerate accumulation: drop it and learn again */
            }
        }
    }
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, 0);
    CORE_ASSERT_RET(raw != NULL, FUS_ASSERT_CODE, 0);
    CORE_ASSERT_RET(out != NULL, FUS_ASSERT_CODE, 0);

    float a_b[3], a[3], w[3];
    fus_step_rotate_inputs(&f->calib, raw, a_b, a, w);

    /* Stillness runs on every raw sample (§9.3 step 8); the flags below report the last completed
     * tumbling window, which is what fus_is_still, the bias capture and drag arming all use. */
    (void)fus_still_push(&f->still, raw);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;

    const float psidot = fus_step_yaw_rate(f, w, oriented);

    uint8_t flags = 0;
    fus_step_planar(f, raw, a, w, psidot, oriented, &flags, out);

    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);   /* §9.3 step 6 */
    out->yaw_dps = psidot * DEG_PER_RAD;         /* §9.3 step 7; computed every step, incl. un-oriented */

    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;

    fus_step_yaw_crosscheck(f, raw, out->yaw_dps, &flags);

    out->flags = flags;

    fus_step_learn_forward(f, a_b);

    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, NULL);
    return &f->calib;
}

/* ---- stillness, bias and calibration entry points (wired to the modules above) ---- */

bool fus_is_still(const fus_t *f)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, false);
    return f->still.still;                       /* last completed window; false until one completes */
}

void fus_gyro_bias_update(fus_t *f)
{
    CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
    if (!fus_is_still(f)) return;                /* §9.2: the bias is only meaningful over a still window */
    for (int i = 0; i < 3; i++) f->calib.gbias[i] = f->still.mean_graw[i];
    /* 0 marks an unknown capture temperature; the bias reads stale once a temperature is known,
     * which is the conservative answer (it only prompts a recapture) and matches the same
     * convention fus_calib_from_ses uses for a restored bias. */
    f->calib.gbias_temp_c100 = f->temp_known ? f->temp_c100 : (int16_t)0;
    f->calib.bias_ok = 1;
    f->bias_stale = false;                       /* freshly captured at the current temperature */
}

int fus_calib_orient_capture(fus_t *f)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, -1);
    if (!fus_is_still(f)) return -1;
    const int rc = fus_orient_from_gravity(&f->calib, f->still.mean_acc);
    if (rc == 0) {
        /* A new z row invalidates the forward row (fus_orient_from_gravity cleared forward_ok), so
         * everything accumulated against the old z is thrown away and learning starts again. */
        fus_fwd_init(&f->fwd);
        f->fwd_learned_pending = false;
    }
    return rc;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    CORE_ASSERT_RET(f != NULL, FUS_ASSERT_CODE, -1);
    CORE_ASSERT_RET(isfinite(gps_acc_mps2), FUS_ASSERT_CODE, -1);   /* fed to the fwd-learn gate below */
    CORE_ASSERT_RET(isfinite(yaw_dps), FUS_ASSERT_CODE, -1);
    if (!f->calib.orient_ok) return -1;          /* nothing to project against until z is captured */
    fus_fwd_on_fix(&f->fwd, gps_acc_mps2, yaw_dps);
    if (f->fwd_learned_pending) {                /* learning completes in fus_step; reported once here */
        f->fwd_learned_pending = false;
        return 1;
    }
    return 0;
}
