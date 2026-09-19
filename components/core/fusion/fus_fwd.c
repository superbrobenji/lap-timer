#include "core/fus.h"
#include "core/core.h"
#include <math.h>
#include <string.h>

/* Power-of-10 rule 5 assertion code for the fusion module (design §3). */
#define FUS_ASSERT_CODE 0x0A10

/* Forward-axis learning window tracker (spec §9.2).
 *
 * The pipeline calls fus_fwd_on_fix once per GPS fix with the fix-to-fix longitudinal acceleration and
 * the yaw rate, and fus_fwd_on_sample once per fusion step while the Z row is known. A *run* is a maximal
 * stretch of samples over which the fix condition holds; it counts as one learning window the moment it
 * reaches FUS_FWD_MIN_SAMPLES (FWD_LEARN_MIN_S seconds at FUSION_HZ) and keeps accumulating until the
 * condition drops. Only counted runs contribute to sum, so a short burst of acceleration that ends before
 * FWD_LEARN_MIN_S is discarded instead of biasing the forward axis.
 *
 * All state is the caller's fus_fwd_t and there is no allocation: this file is built for the ESP32 too. */

#define FUS_FWD_MAX_WINDOWS 255   /* fus_fwd_t.windows is uint8_t: the count saturates instead of wrapping */

void fus_fwd_init(fus_fwd_t *w)
{
    CORE_ASSERT_VOID(w != NULL, FUS_ASSERT_CODE);
    memset(w, 0, sizeof *w);      /* a zeroed tracker is a valid initialised state (fus.h) */
}

/* Ends the current run: its samples are already in sum if it was counted, and are dropped otherwise. */
static void run_reset(fus_fwd_t *w)
{
    CORE_ASSERT_VOID(w != NULL, FUS_ASSERT_CODE);
    w->run_samples = 0;
    w->run_sum[0] = 0.0; w->run_sum[1] = 0.0; w->run_sum[2] = 0.0;
    w->counted = false;
}

void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps)
{
    CORE_ASSERT_VOID(w != NULL, FUS_ASSERT_CODE);
    CORE_ASSERT_VOID(isfinite(gps_acc_mps2), FUS_ASSERT_CODE);   /* both feed the threshold gate below */
    CORE_ASSERT_VOID(isfinite(yaw_dps), FUS_ASSERT_CODE);
    /* §9.2 learns only from near-straight acceleration, so that a_h points along the forward axis. Both
     * comparisons are strict, and braking (gps_acc_mps2 <= 0) can never qualify. */
    w->cond = (fabsf(yaw_dps) < FWD_LEARN_MAX_YAW_DPS) && (gps_acc_mps2 > FWD_LEARN_ACC_MPS2);
    if (!w->cond) run_reset(w);
}

int fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3])
{
    CORE_ASSERT_RET(w != NULL, FUS_ASSERT_CODE, 0);
    CORE_ASSERT_RET(acc_g != NULL, FUS_ASSERT_CODE, 0);
    CORE_ASSERT_RET(z != NULL, FUS_ASSERT_CODE, 0);
    if (!w->cond) return 0;

    /* a_h = a − (a·z)z: the horizontal (gravity-free) part of the specific force, in g, body frame. */
    const float az = acc_g[0] * z[0] + acc_g[1] * z[1] + acc_g[2] * z[2];
    const float ah[3] = { acc_g[0] - az * z[0], acc_g[1] - az * z[1], acc_g[2] - az * z[2] };

    w->run_samples++;
    if (w->counted) {                       /* run already counted: later samples go straight to sum */
        for (int i = 0; i < 3; i++) w->sum[i] += (double)ah[i];
        return 0;
    }
    for (int i = 0; i < 3; i++) w->run_sum[i] += (double)ah[i];
    if (w->run_samples != (uint32_t)FUS_FWD_MIN_SAMPLES) return 0;

    w->counted = true;
    for (int i = 0; i < 3; i++) { w->sum[i] += w->run_sum[i]; w->run_sum[i] = 0.0; }
    if (w->windows < FUS_FWD_MAX_WINDOWS) w->windows = (uint8_t)(w->windows + 1);
    /* windows only ever grows, so it equals FWD_LEARN_WINDOWS on exactly one call per tracker: later
     * runs still count (and still accumulate) but report nothing. */
    return (w->windows == FWD_LEARN_WINDOWS) ? 1 : 0;
}

bool fus_fwd_ready(const fus_fwd_t *w)
{
    CORE_ASSERT_RET(w != NULL, FUS_ASSERT_CODE, false);
    return w->windows >= FWD_LEARN_WINDOWS;
}
