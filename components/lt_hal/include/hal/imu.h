/* hal/imu.h -- IMU HAL contract (spec §5.1, called from the pipeline task).
 *
 * The declarations below are the §5.1 imu.h slice verbatim; the include guard, the
 * <stdint.h>/<stddef.h> includes (size_t, fixed-width types) and the IMU_* mode values are
 * added here so this is a self-contained, compilable header. imu_raw_t is re-used from
 * core/types.h so the fusion engine and the driver share one definition. All functions return
 * int (0 = OK, negative = -errno-style) unless noted; each is called from one task only (the
 * pipeline) and is not reentrant. The concrete implementation is components/drivers/imu_${IMU}
 * (imu_sim synthesises a deterministic 100 Hz stream; imu_mpu6050 drives the real MPU-6050).
 */
#ifndef HAL_IMU_H
#define HAL_IMU_H

#include <stddef.h>
#include <stdint.h>

#include "core/types.h"   /* imu_raw_t (one shared definition) */

/* imu_set_mode values (§5.1, §8). */
enum {
    IMU_FULL     = 0,   /* 100 Hz FIFO stream */
    IMU_LOWPOWER = 1,   /* 40 Hz accel, motion interrupt */
};

int  imu_init(void);                                  /* bus + WHO_AM_I + register config (§8.3) */
int  imu_self_test(uint8_t *pass_mask);               /* §8.5; bit per axis */
int  imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us);
int  imu_read_temp_c100(int16_t *out);                /* deg C * 100 */
int  imu_set_mode(uint8_t mode);                      /* IMU_FULL / IMU_LOWPOWER */
int  imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms);
int  imu_recover(void);                               /* bus recovery + reinit; ladder step */
int  imu_int_pending(void);                           /* reads INT_STATUS; clears */

#endif /* HAL_IMU_H */
