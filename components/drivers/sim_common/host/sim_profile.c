#include "sim_profile.h"
#include "core/core.h"
#include <stddef.h>
#define SIM_PROFILE_ASSERT_CODE 0x0C70

void sim_drag_at(int64_t t_us, sim_drag_state_t *out)
{
    CORE_ASSERT_VOID(out != NULL, SIM_PROFILE_ASSERT_CODE);
    CORE_ASSERT_VOID(t_us >= 0, SIM_PROFILE_ASSERT_CODE);
    const double a    = SIM_DRAG_ACCEL_MPS2, vmax = SIM_DRAG_VMAX_MPS, b = SIM_DRAG_BRAKE_MPS2;
    const double t_v  = vmax / a;                       /* accel phase length (s) */
    const double d_v  = 0.5 * a * t_v * t_v;            /* distance at vmax */
    const double t_cr = (SIM_DRAG_RUN_M > d_v) ? (SIM_DRAG_RUN_M - d_v) / vmax : 0.0;   /* cruise (s) */
    const double t_b  = vmax / b;                       /* brake phase length (s) */
    const double d_b  = 0.5 * vmax * t_b;               /* braking distance */
    double t = (double)(t_us - SIM_DRAG_HOLD_US) / 1e6; /* seconds since launch; < 0 while holding */
    out->parked = false;
    if (t < 0.0) {
        out->dist_m = 0.0; out->speed_mps = 0.0; out->accel_mps2 = 0.0;
    } else if (t < t_v) {
        out->dist_m = 0.5 * a * t * t; out->speed_mps = a * t; out->accel_mps2 = a;
    } else if (t < t_v + t_cr) {
        out->dist_m = d_v + vmax * (t - t_v); out->speed_mps = vmax; out->accel_mps2 = 0.0;
    } else if (t < t_v + t_cr + t_b) {
        double tb = t - t_v - t_cr;
        out->dist_m = d_v + vmax * t_cr + vmax * tb - 0.5 * b * tb * tb;
        out->speed_mps = vmax - b * tb; out->accel_mps2 = -b;
    } else {
        out->dist_m = d_v + vmax * t_cr + d_b; out->speed_mps = 0.0; out->accel_mps2 = 0.0;
        out->parked = true;
    }
}
