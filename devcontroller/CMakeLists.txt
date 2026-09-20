cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Dev Controller (sub-project B, plan docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md)
# top-level project.
#
# B is a sibling ESP-IDF project to the lap-timer, living in devcontroller/. It versions
# independently from the lap-timer (dc-v* tags vs the lap-timer's v* tags) and reuses the
# lap-timer's shared protocol contract (components/app/include/app/lt_proto.h) via a direct
# include path -- see the EXTRA_COMPONENT_DIRS note below.
# =============================================================================

# ---- firmware version from git, dc-v* tags (Global Constraints: "B versions independently") ----
execute_process(
    COMMAND git describe --tags --match "dc-v*" --dirty --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE CFG_DC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _dcver_rc
    ERROR_VARIABLE  _dcver_err)
if(NOT _dcver_rc EQUAL 0 OR CFG_DC_VERSION STREQUAL "")
    set(CFG_DC_VERSION "dc-v0.0.0-unknown")   # no git, or no dc-v* tag yet
endif()
# mirror the version into the app descriptor (esp_app_desc_t.version)
set(PROJECT_VER "${CFG_DC_VERSION}")

# ---- generate build_config.h into the build tree ----
configure_file(main/build_config.h.in "${CMAKE_BINARY_DIR}/build_config.h" @ONLY)

# ---- component search path ----
# Plan 5.5 Task 2 Step 5 says to add the lap-timer's components/app here "so app/lt_proto.h +
# vendored jsmn resolve". Tried verbatim first -- it does NOT work: ESP-IDF's early-expansion
# pass evaluates every component discovered via EXTRA_COMPONENT_DIRS (not just ones something
# REQUIRES), so merely listing components/app here makes IDF resolve ITS CMakeLists.txt too,
# which REQUIRES core, lt_hal, nvs_flash, console, app_update, esp_app_format, esp_partition,
# mbedtls, and gps_$ENV{LT_GPS}/imu_$ENV{LT_IMU} -- lap-timer-only drivers/components devcontroller
# doesn't have and no env var sets here ("cmake -DEXTRA_COMPONENT_DIRS=...components/app" fails
# configure with "Failed to resolve component 'core'"). So EXTRA_COMPONENT_DIRS is intentionally
# NOT set to components/app: linkhost instead resolves app/lt_proto.h via a direct relative
# INCLUDE_DIRS entry (see components/linkhost/CMakeLists.txt) -- header-only, no lap-timer .c
# files compiled into B. (jsmn is actually vendored in components/core/util/jsmn.c, not
# components/app as the plan assumed; see this session's final report for the note to Task 4's
# implementer on pulling it in without the same problem.)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devcontroller)
