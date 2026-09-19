/* imu_mpu6050.c -- PLAN-03 STUB of hal/imu.h for the MPU-6050 (spec §4.6 IMU=mpu6050, §8).
 *
 * The real I2C driver (WHO_AM_I, register config, FIFO, self-test, motion INT) lands with the
 * physical sensor. Plan 03 only needs moto_neo6m (IMU=mpu6050) to BUILD green now that the pipeline
 * calls the IMU HAL: this stub satisfies every hal/imu.h symbol, passes self-test, and returns no
 * FIFO samples, so the pipeline starts and idles on that variant. moto_sim (imu_sim) runs fusion.
 */
#include "hal/imu.h"
#include "core/core.h"

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define IMU_MPU_ASSERT_CODE 0x0C50

int imu_init(void) { return 0; }

int imu_self_test(uint8_t *pass_mask)
{
    if (pass_mask) *pass_mask = 0x3F;   /* all six axes pass */
    return 0;
}

int imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us)
{
    (void)out; (void)max; (void)read_mono_us;
    CORE_ASSERT_RET(n_read != NULL, IMU_MPU_ASSERT_CODE, -1);   /* written just below */
    *n_read = 0;                        /* no sensor yet */
    return 0;
}

int imu_read_temp_c100(int16_t *out) { if (out) *out = 2500; return 0; }
int imu_set_mode(uint8_t mode) { (void)mode; return 0; }
int imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms) { (void)thr_lsb; (void)dur_ms; return 0; }
int imu_recover(void)   { return 0; }
int imu_int_pending(void) { return 0; }
