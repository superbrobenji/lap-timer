#include "core/fus.h"
#include <math.h>
#include <string.h>

/* Stillness detector (spec §9.2): tumbling windows of FUS_STILL_WINDOW_N samples
 * (STILL_WINDOW_S · FUSION_HZ = 200 at 100 Hz). A window is still when the variance of the accel
 * magnitude is below STILL_ACC_VAR and every gyro axis variance is below STILL_GYRO_VAR. The
 * last-completed-window fields persist until the next window completes, so stillness is reported
 * with at most one window of latency and no per-sample sliding statistics are needed.
 *
 * Why the accumulators are double (fus.h fixes the type; this is the reason): the variance is taken
 * as E[x²] − E[x]², which cancels catastrophically in float32 when the mean is far from zero. At
 * full scale one raw axis is 32767 LSB, so a squared sample reaches 1.07e9 and a 200-sample sum
 * 2.1e11; in the units accumulated here a window holds Σ|a|² up to 1.5e5 g² and Σg² up to 8.0e8
 * dps². One float32 ULP at 8.0e8 is 64 dps², 16× the 4 dps² gyro threshold, and at 1.5e5 g² it is
 * 0.0156 g², 39× the 4e-4 g² accel threshold: a float32 accumulator could not tell still from
 * moving at all. A double's 53-bit mantissa puts one ULP at 1.8e-7 dps² and 3.3e-11 g², seven
 * orders below either threshold. The per-sample cost stays one sqrt plus ~20 flops at 100 Hz. */

static void window_restart(fus_still_t *s)
{
    s->sum_amag = 0.0;
    s->sum_amag2 = 0.0;
    memset(s->sum_g, 0, sizeof s->sum_g);
    memset(s->sum_g2, 0, sizeof s->sum_g2);
    memset(s->sum_acc, 0, sizeof s->sum_acc);
    memset(s->sum_graw, 0, sizeof s->sum_graw);
    s->n = 0;
}

void fus_still_init(fus_still_t *s)
{
    memset(s, 0, sizeof *s);   /* a zeroed struct is the initialised state (fus.h) */
}

/* Closes the current window: computes the two statistics, latches the means, restarts the sums. */
static void window_close(fus_still_t *s)
{
    const double n = (double)FUS_STILL_WINDOW_N;
    const double mean_amag = s->sum_amag / n;
    double acc_var = s->sum_amag2 / n - mean_amag * mean_amag;
    if (acc_var < 0.0) acc_var = 0.0;   /* a constant window can land microscopically negative */
    double gyr_var_max = 0.0;
    for (int i = 0; i < 3; i++) {
        const double mean_g = s->sum_g[i] / n;
        double v = s->sum_g2[i] / n - mean_g * mean_g;
        if (v < 0.0) v = 0.0;
        if (v > gyr_var_max) gyr_var_max = v;
        s->mean_acc[i] = (float)(s->sum_acc[i] / n);
        s->mean_graw[i] = (float)(s->sum_graw[i] / n);
    }
    s->acc_var = (float)acc_var;
    s->gyr_var_max = (float)gyr_var_max;
    s->still = (acc_var < (double)STILL_ACC_VAR) && (gyr_var_max < (double)STILL_GYRO_VAR);
    s->have_window = true;
    window_restart(s);
}

int fus_still_push(fus_still_t *s, const imu_raw_t *raw)
{
    /* One conversion per sample: accel LSB → g, gyro LSB → dps (the bias is not removed here — the
     * mean raw gyro of a still window is what becomes the bias, §9.2). */
    const double ax = (double)raw->ax / (double)IMU_ACC_LSB_PER_G;
    const double ay = (double)raw->ay / (double)IMU_ACC_LSB_PER_G;
    const double az = (double)raw->az / (double)IMU_ACC_LSB_PER_G;
    const double amag = sqrt(ax * ax + ay * ay + az * az);
    const double graw[3] = { (double)raw->gx, (double)raw->gy, (double)raw->gz };

    s->sum_amag += amag;
    s->sum_amag2 += amag * amag;
    s->sum_acc[0] += ax;
    s->sum_acc[1] += ay;
    s->sum_acc[2] += az;
    for (int i = 0; i < 3; i++) {
        const double gd = graw[i] / (double)IMU_GYR_LSB_PER_DPS;
        s->sum_g[i] += gd;
        s->sum_g2[i] += gd * gd;
        s->sum_graw[i] += graw[i];
    }
    s->n = (uint16_t)(s->n + 1u);

    if (s->n >= FUS_STILL_WINDOW_N) {
        window_close(s);
        return 1;
    }
    return 0;
}
