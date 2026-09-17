# imu_sim -- hal/imu.h synthesising a deterministic 100 Hz stream (spec §4.6 IMU=sim). Bench driver.
idf_component_register(
    SRCS "imu_sim.c"
    REQUIRES lt_hal core)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
