cmake_minimum_required(VERSION 3.16)

# =============================================================================
# LapTimer top-level project (spec §21.1).
#
# Selects the build variant from the §4.6 build flags, validates the flag
# combination (fatal on a bad combo), generates build_config.h into the build
# tree, picks the per-variant sdkconfig fragment, then hands off to ESP-IDF.
#
# build.sh maps a named environment (e.g. moto_neo6m) to the -D<flag>=<value>
# set consumed below; idf.py -D<flag>=<value> works the same way by hand.
# =============================================================================

# ---- §4.6 build flags (cache vars; build.sh / idf.py -D override the defaults) ----
set(VARIANT       ""         CACHE STRING "vehicle variant: moto | car (required)")
set(GPS           ""         CACHE STRING "GPS driver: neo6m | m10 | sim (required)")
set(IMU           "mpu6050"  CACHE STRING "IMU driver: mpu6050 | sim")
set(DISPLAY       ""         CACHE STRING "display driver: epaper_ssd1680 | oled_ssd1309 (required)")
set(STORAGE       "internal" CACHE STRING "storage backend: internal | sd")
set(CONN          "ble"      CACHE STRING "connectivity: ble | wifi | ble_wifi")
set(CONN_BLE_RC   "OFF"      CACHE STRING "RaceChrono BLE bridge: ON | OFF")
set(EXPORT_SERIAL "ON"       CACHE STRING "serial export console: ON | OFF")
set(PANEL         "ws29v2"   CACHE STRING "e-paper panel: ws213v4 | ws29v2")

# ---- validate the flag combination (§4.6) ----
if(NOT VARIANT MATCHES "^(moto|car)$")
    message(FATAL_ERROR "VARIANT must be 'moto' or 'car' (got '${VARIANT}')")
endif()
if(NOT GPS MATCHES "^(neo6m|m10|sim)$")
    message(FATAL_ERROR "GPS must be 'neo6m', 'm10' or 'sim' (got '${GPS}')")
endif()
if(NOT IMU MATCHES "^(mpu6050|sim)$")
    message(FATAL_ERROR "IMU must be 'mpu6050' or 'sim' (got '${IMU}')")
endif()
if(NOT DISPLAY MATCHES "^(epaper_ssd1680|oled_ssd1309)$")
    message(FATAL_ERROR "DISPLAY must be 'epaper_ssd1680' or 'oled_ssd1309' (got '${DISPLAY}')")
endif()
if(NOT STORAGE MATCHES "^(internal|sd)$")
    message(FATAL_ERROR "STORAGE must be 'internal' or 'sd' (got '${STORAGE}')")
endif()
if(NOT CONN MATCHES "^(ble|wifi|ble_wifi)$")
    message(FATAL_ERROR "CONN must be 'ble', 'wifi' or 'ble_wifi' (got '${CONN}')")
endif()
if(NOT PANEL MATCHES "^(ws213v4|ws29v2)$")
    message(FATAL_ERROR "PANEL must be 'ws213v4' or 'ws29v2' (got '${PANEL}')")
endif()
# moto accepts either display; car requires the OLED (§4.6)
if(VARIANT STREQUAL "car" AND NOT DISPLAY STREQUAL "oled_ssd1309")
    message(FATAL_ERROR "VARIANT=car requires DISPLAY=oled_ssd1309 (got '${DISPLAY}')")
endif()
# the RaceChrono BLE bridge needs a BLE-capable CONN (§4.6)
if(CONN_BLE_RC AND NOT CONN MATCHES "ble")
    message(FATAL_ERROR "CONN_BLE_RC=ON requires CONN to contain 'ble' (got CONN='${CONN}')")
endif()

# ---- build_config.h values derived from the flags (§4.6) ----
if(VARIANT STREQUAL "moto")
    set(CFG_VARIANT_MOTO 1)
    set(CFG_VARIANT_CAR  0)
else()
    set(CFG_VARIANT_MOTO 0)
    set(CFG_VARIANT_CAR  1)
endif()

if(PANEL STREQUAL "ws213v4")
    set(CFG_PANEL_WS213V4 1)
    set(CFG_PANEL_WS29V2  0)
else()
    set(CFG_PANEL_WS213V4 0)
    set(CFG_PANEL_WS29V2  1)
endif()

# hardware id: <variant>_<gps>_<display tag>; the OTA hwid check compares it (§19.3).
if(DISPLAY MATCHES "epaper")
    set(_display_tag "epaper")
else()
    set(_display_tag "oled")
endif()
set(CFG_HWID "${VARIANT}_${GPS}_${_display_tag}")

# capability flags (§4.6)
if(GPS STREQUAL "m10")          # only the M10 exposes a usable PPS pin
    set(CFG_HAS_PPS 1)
else()
    set(CFG_HAS_PPS 0)
endif()
if(STORAGE STREQUAL "sd")
    set(CFG_HAS_SD 1)
else()
    set(CFG_HAS_SD 0)
endif()
if(CONN MATCHES "wifi")
    set(CFG_HAS_WIFI 1)
else()
    set(CFG_HAS_WIFI 0)
endif()
if(CONN MATCHES "ble")
    set(CFG_HAS_BLE 1)
else()
    set(CFG_HAS_BLE 0)
endif()
if(CONN_BLE_RC)
    set(CFG_HAS_BLE_RC 1)
else()
    set(CFG_HAS_BLE_RC 0)
endif()

# fused-sample logging rate: 10 Hz to internal flash, 25 Hz to SD (Appendix A)
if(STORAGE STREQUAL "sd")
    set(CFG_FUSED_LOG_HZ 25)
else()
    set(CFG_FUSED_LOG_HZ 10)
endif()

# ---- firmware version from git, with a fallback when no v* tag exists yet (§21.5) ----
execute_process(
    COMMAND git describe --tags --match "v*" --dirty --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE CFG_FW_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _fwver_rc
    ERROR_VARIABLE  _fwver_err)
if(NOT _fwver_rc EQUAL 0 OR CFG_FW_VERSION STREQUAL "")
    set(CFG_FW_VERSION "v0.0.0-unknown")   # no git or no history (e.g. a source tarball)
endif()
# mirror the version into the app descriptor (esp_app_desc_t.version, §19.3)
set(PROJECT_VER "${CFG_FW_VERSION}")

# ---- generate build_config.h into the build tree (§21.1) ----
configure_file(main/build_config.h.in "${CMAKE_BINARY_DIR}/build_config.h" @ONLY)

# ---- component search path (§21.1) ----
# 3.1 references only the components that exist now. §21.1's full set is appended
# by the session that creates each one:
#   3.2  components/hal   components/drivers/board_devkit_v1
#   3.3  components/drivers/storage_${STORAGE}
#   3.4  components/app   components/drivers/gps_ubx_common   components/drivers/gps_${GPS}
#        components/drivers/imu_${IMU}   components/drivers/display_${DISPLAY}
#        + conn_ble / conn_wifi (per CONN), conn_ble_rc (if CONN_BLE_RC),
#          export_serial (if EXPORT_SERIAL)  -- gated exactly as in §21.1.
# The version-banner build links no drivers.
set(EXTRA_COMPONENT_DIRS components/core)

# keep each named environment's generated sdkconfig inside its own build/<env>/
# tree, so switching -D flags between envs never cross-contaminates (each build dir
# is a clean regenerate from the fragments below).
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

# ---- per-variant sdkconfig fragment (§21.1) ----
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.${VARIANT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(laptimer)
