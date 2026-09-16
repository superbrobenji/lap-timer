#include "core/fus.h"
#include <math.h>
#include <string.h>

/* Degree/radian conversion factors for the §9.3 lean/yaw algebra (no bare literals below). */
#define RAD_PER_DEG 0.017453292519943295f   /* pi / 180 */
#define DEG_PER_RAD 57.295779513082323f     /* 180 / pi */

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
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
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
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
    float d = fmodf(cur_deg - prev_deg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d <= -180.0f) d += 360.0f;
    return d;
}

float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us)
{
    const double dt_s = (double)(cur_mono_us - prev_mono_us) / 1e6;
    if (dt_s <= 0.0) return 0.0f;
    /* compass heading rises clockwise (a right turn), the vehicle yaw is + to the left, so negate */
    return (float)(-(double)course_delta_deg(cur_course_deg, prev_course_deg) / dt_s);
}

void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid)
{
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
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    /* Stillness runs on every raw sample (§9.3 step 8); the flags below report the last completed
     * tumbling window, which is what fus_is_still, the bias capture and drag arming all use. */
    fus_still_push(&f->still, raw);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;

    /* §9.3 steps 1/3: rotated gyro to rad/s and the earth-frame yaw rate psi-dot. phi is the
     * PREVIOUS step's lean (a causal filter breaks the phi -> psi-dot -> phi_ref -> phi loop); the
     * car variant and any un-oriented moto sample use phi == 0, so psi-dot reduces to omega.z. */
    const float dt       = 1.0f / (float)FUSION_HZ;      /* 0.01 s at FUSION_HZ */
    const float phi_prev = (f->moto && oriented) ? f->lean_rad : 0.0f;
    const float wx = w[0] * RAD_PER_DEG;                 /* body-forward roll rate, rad/s */
    const float wy = w[1] * RAD_PER_DEG;
    const float wz = w[2] * RAD_PER_DEG;
    const float psidot = wz * cosf(phi_prev) + wy * sinf(phi_prev);   /* rad/s, earth frame */

    uint8_t flags = 0;

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
            float phi = f->lean_rad + wx * dt;   /* phi_gyro */
            if (ref_ok) {
                const float phi_ref = atan2f(-f->v_mps * psidot, (float)G_MPS2);
                phi = LEAN_ALPHA * phi + (1.0f - LEAN_ALPHA) * phi_ref;
                f->last_ref_mono_us = raw->mono_us;
            }
            const float lean_max = LEAN_MAX_DEG * RAD_PER_DEG;
            if (phi > lean_max)       { phi =  lean_max; flags |= FUS_CLAMPED; }
            else if (phi < -lean_max) { phi = -lean_max; flags |= FUS_CLAMPED; }
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
                flags |= FUS_LEAN_VALID;
        } else {
            /* Car (§9.3 step 5, car): lateral g is the accelerometer specific force. Lean is not
             * meaningful for a car, so lean_deg stays 0 and FUS_LEAN_VALID is never set. */
            out->g_lat = -a[1];
        }

        /* G_MAX clamp (Appendix A, "clamp in §9.3"): bound each in-plane axis to +-G_MAX *before*
         * forming g_comb, so the reported combined magnitude stays consistent with the reported
         * (clamped) components. */
        if (out->g_lon > G_MAX)       { out->g_lon =  G_MAX; flags |= FUS_CLAMPED; }
        else if (out->g_lon < -G_MAX) { out->g_lon = -G_MAX; flags |= FUS_CLAMPED; }
        if (out->g_lat > G_MAX)       { out->g_lat =  G_MAX; flags |= FUS_CLAMPED; }
        else if (out->g_lat < -G_MAX) { out->g_lat = -G_MAX; flags |= FUS_CLAMPED; }
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;                       /* no lateral, no lean, FUS_LEAN_VALID clear until oriented */
    }

    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);   /* §9.3 step 6 */
    out->yaw_dps = psidot * DEG_PER_RAD;         /* §9.3 step 7; computed every step, incl. un-oriented */

    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;

    /* GPS yaw cross-check (§9.3 step 4). While a fresh GPS turn rate exists above the reference
     * speed, a fused/GPS yaw disagreement beyond LEAN_DISAGREE_DPS that holds for LEAN_DISAGREE_S
     * sets FUS_DISAGREE and latches disagree_latched (the plan-03 pipeline logs E_FUSION_DISAGREE
     * once from the latch). Reading of the spec's "for 5 s": the flag is present only while the
     * disagreement currently holds and its timer has exceeded the hold, and clears as soon as the
     * difference drops back under threshold (the timer resets to -1); disagree_latched stays set. */
    const int64_t yaw_age = raw->mono_us - f->yaw_gps_mono_us;
    const bool yaw_ref_ok = f->v_valid && isfinite(f->yaw_gps_dps) &&
                            yaw_age >= 0 && yaw_age < FUS_REF_MAX_AGE_US &&
                            f->v_mps > LEAN_REF_MIN_SPEED_MPS;
    if (yaw_ref_ok && fabsf(out->yaw_dps - f->yaw_gps_dps) > LEAN_DISAGREE_DPS) {
        if (f->disagree_since_mono_us < 0) f->disagree_since_mono_us = raw->mono_us;
        if (raw->mono_us - f->disagree_since_mono_us >= (int64_t)LEAN_DISAGREE_S * 1000000) {
            flags |= FUS_DISAGREE;
            f->disagree_latched = true;
        }
    } else {
        f->disagree_since_mono_us = -1;          /* agreeing, or no fresh GPS turn rate: reset */
    }

    out->flags = flags;

    /* Forward-axis learning (§9.2): only between the upright capture and the forward row being known.
     * The rows may change in this block, after this sample was already rotated with the old ones —
     * that is deliberate and harmless: the sample stays consistent with the calibration it was
     * computed from (sign-less g_lon, no FUS_ORIENT_OK) and the learned rows take effect from the
     * next sample, 10 ms later at FUSION_HZ. */
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
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- stillness, bias and calibration entry points (wired to the modules above) ---- */

bool fus_is_still(const fus_t *f)
{
    return f->still.still;                       /* last completed window; false until one completes */
}

void fus_gyro_bias_update(fus_t *f)
{
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
    if (!f->calib.orient_ok) return -1;          /* nothing to project against until z is captured */
    fus_fwd_on_fix(&f->fwd, gps_acc_mps2, yaw_dps);
    if (f->fwd_learned_pending) {                /* learning completes in fus_step; reported once here */
        f->fwd_learned_pending = false;
        return 1;
    }
    return 0;
}
