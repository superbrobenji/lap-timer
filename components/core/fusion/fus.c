#include "core/fus.h"
#include <math.h>
#include <string.h>

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
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */
        out->g_lat = -a[1];                      /* +Y is left; lateral g is + to the right (§9.3 step 5, car form) */
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;
    }
    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);
    out->lean_deg = 0.0f;                        /* lean filter arrives in session 2.3 */
    out->yaw_dps  = w[2];                        /* session 2.3 applies the lean correction of §9.3 step 3 */
    uint8_t flags = 0;
    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;
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
