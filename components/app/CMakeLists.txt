# app -- IDF-dependent glue (spec §4.1): NVS layer (§15.2), RTC state (§15.3), supervisor
# (§4.3/§17.2), shared runtime state (hb[]/sys_flags, §4.4/§17.4), the pipeline (§9), logger (§13),
# and app/cmd (§18.1). The serial console itself lives in components/drivers/export_serial.
# No collision with an IDF built-in (there is no IDF component named `app`).
if(CMAKE_BUILD_EARLY_EXPANSION)
    # CONFIGURE_DEPENDS is invalid in the script-mode early-expansion pass IDF uses to pull
    # each component's REQUIRES (§ build-system "early expansion"); SRCS is unused there anyway.
    file(GLOB_RECURSE APP_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
else()
    file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
endif()
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS include
    REQUIRES core lt_hal nvs_flash esp_timer esp_system console
             app_update esp_app_format esp_partition spi_flash mbedtls
             esp_driver_gpio
             gps_$ENV{LT_GPS} imu_$ENV{LT_IMU})
# The pipeline (globbed into `app`) calls the GPS/IMU HAL (hal/gps.h, hal/imu.h), so the selected
# gps_${GPS}/imu_${IMU} driver is a real dependency of `app`: declaring it here places the driver
# archive after `app` in the link so the pipeline's calls resolve regardless of how deep the
# dependency graph makes `app` (the 3.5 export_serial console adds a main -> export_serial -> app
# edge). LT_GPS/LT_IMU are exported into the environment by the top-level CMakeLists (a flag-derived
# component name cannot be read from the cache during IDF's early-expansion pass, but the
# environment is inherited); main also requires them so the driver is pulled into the image.

# §6 peer connector detect line (board_devkit_v1): GPIO4, free + internal-pull-up capable +
# non-strapping. link.c enables its (active-low) detect path once this is >= 0. See
# docs/superpowers/specs/2026-09-21-peer-presence-detect-design.md.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_DETECT_GPIO=4)

# build_config.h (CFG_HAS_PPS etc.) from the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
