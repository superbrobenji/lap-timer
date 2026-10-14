/* imu_mpu6050.c -- PLAN-03 STUB of hal/imu.h for the MPU-6050 (spec §4.6 IMU=mpu6050, §8).
 *
 * The real I2C driver (WHO_AM_I, register config, FIFO, self-test, motion INT) lands with the
 * physical sensor in Plan 08. Ruling R28 (drv:H3): this stub reports FAILURE. It used to return
 * a full 0x3F self-test pass mask with no chip on the bus, so §17.6's "IMU present"/"IMU genuine"
 * rows and the BOOT screen both read OK on a board with no IMU -- while imu_read_fifo returned
 * zero samples, leaving fusion, lean/g and every per-lap stat silently at zero.
 */
#include "hal/imu.h"
#include "core/core.h"

#include <errno.h>

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define IMU_MPU_ASSERT_CODE 0x0C50

int imu_init(void) { return -ENODEV; }

int imu_self_test(uint8_t *pass_mask)
{
    if (pass_mask) *pass_mask = 0x00;   /* R28: no axis passes, because no axis was tested */
    return -ENODEV;
}

int imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us)
{
    (void)out; (void)max; (void)read_mono_us;
    CORE_ASSERT_RET(n_read != NULL, IMU_MPU_ASSERT_CODE, -1);   /* written just below */
    *n_read = 0;
    return -ENODEV;
}

int imu_read_temp_c100(int16_t *out) { (void)out; return -ENODEV; }   /* no fake 25.00 C */
int imu_set_mode(uint8_t mode) { (void)mode; return -ENODEV; }
int imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms) { (void)thr_lsb; (void)dur_ms; return -ENODEV; }
int imu_recover(void)   { return -ENODEV; }
int imu_int_pending(void) { return 0; }    /* "no interrupt pending" is the honest answer */
