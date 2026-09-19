/* imu_sim.c -- hal/imu.h synthesising a deterministic 100 Hz stream (spec §4.6 IMU=sim, §22.3).
 *
 * Lap TIMING is a function of GPS only (the lap engine ignores its fused argument, §9.1/§9.4), so
 * the sim IMU exists purely to keep fusion and the per-lap-stats accumulator running at 100 Hz. A
 * deliberately simple model is used: the board sits upright and still -- specific force +1 g on the
 * vehicle Z axis (2048 LSB at the 16 g / 2048-LSB-per-g scale of imu_raw_t), zero on X/Y, zero gyro.
 * That yields well-formed fused samples (lean ~ 0, g ~ 0) without pretending to model cornering; a
 * truth-derived lean/g stream is possible (synth_fused_at) but is not needed for the exit criterion.
 *
 * imu_read_fifo returns the samples that fell due since the previous read (10 ms spacing), stamped
 * with real device mono time so tb_mono_to_gps maps them onto the same GPS timeline as the fixes.
 */
#include "hal/imu.h"
#include "core/core.h"

#include <string.h>

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define IMU_SIM_ASSERT_CODE 0x0C60

#define IMU_SIM_PERIOD_US 10000        /* 100 Hz */
#define IMU_SIM_1G_LSB    2048         /* +/-16 g range => 2048 LSB/g (imu_raw_t) */

static int64_t s_last_us;              /* mono time of the last generated sample */
static bool    s_have_last;

int imu_init(void)
{
    s_have_last = false;
    s_last_us = 0;
    return 0;
}

int imu_self_test(uint8_t *pass_mask)
{
    if (pass_mask) *pass_mask = 0x3F;   /* all six axes pass */
    return 0;
}

int imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us)
{
    size_t n = 0;
    CORE_ASSERT_RET(out != NULL, IMU_SIM_ASSERT_CODE, -1);      /* written by the loop below */
    CORE_ASSERT_RET(n_read != NULL, IMU_SIM_ASSERT_CODE, -1);   /* written on every path out */

    if (!s_have_last) {                 /* anchor one period back so the first read yields a sample */
        s_have_last = true;
        s_last_us = read_mono_us - IMU_SIM_PERIOD_US;
    }

    while (n < max && s_last_us + IMU_SIM_PERIOD_US <= read_mono_us) {
        s_last_us += IMU_SIM_PERIOD_US;
        imu_raw_t *r = &out[n++];
        r->mono_us = s_last_us;
        r->ax = 0;
        r->ay = 0;
        r->az = IMU_SIM_1G_LSB;         /* upright: gravity on +Z */
        r->gx = 0;
        r->gy = 0;
        r->gz = 0;
    }
    *n_read = n;
    return 0;
}

int imu_read_temp_c100(int16_t *out)
{
    if (out) *out = 2500;               /* a constant 25.00 C */
    return 0;
}

/* Mode / threshold / recovery / interrupt: no-ops sufficient for the pipeline. */
int imu_set_mode(uint8_t mode)                          { (void)mode; return 0; }
int imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms) { (void)thr_lsb; (void)dur_ms; return 0; }
int imu_recover(void)                                   { s_have_last = false; return 0; }
int imu_int_pending(void)                               { return 0; }
