# imu_mpu6050 -- PLAN-03 STUB of hal/imu.h (spec §4.6 IMU=mpu6050). The real I2C driver lands with the
# sensor; this only keeps moto_neo6m building now that the pipeline calls the IMU HAL. See the .c.
idf_component_register(
    SRCS "imu_mpu6050.c"
    REQUIRES lt_hal core)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
