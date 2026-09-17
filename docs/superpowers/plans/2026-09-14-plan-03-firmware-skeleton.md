# Plan 03: Firmware Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is a **firmware** plan: the code must build with ESP-IDF v5.3.2, not just compile on the host. Sessions 3.2-3.6 are appended to this document at the start of each session (like plan 02), once the real code the session builds on exists.

**Goal:** Bring up the real ESP-IDF firmware on the ESP32 dev board. Session 3.1 lays the top-level build system (variant selection + validation, generated `build_config.h`, partition table, `sdkconfig`, `build.sh`) and a bootable version-banner `app_main`. Later sessions add the board HAL and drivers (3.2), internal storage and the logger (3.3), the simulated-sensor pipeline running fusion/lap/drag on core 1 (3.4), the serial console and crash-loop safe mode (3.5), and a bench-test day (3.6) — everything the `moto_neo6m` and `moto_sim` environments need to boot, run simulated laps, and log.

**Architecture:** `components/core/` stays pure C11 (no ESP-IDF, FreeRTOS, or `malloc`) exactly as plans 01/02 built it, and is linked into the firmware as an IDF component unchanged. The firmware adds `main/` (boot sequence §4.7 + task spawn), `components/hal/` (INTERFACE headers), `components/drivers/*` (board, GPS, IMU, display, storage, conn, export — each driver registers the same HAL implementation interface name, so `app` links against `hal_gps_impl` etc. without knowing which driver is present), and `components/app/*` (pipeline, logger, ui, power, supervisor, ota, cmd). The §4.6 build flags select the driver set; the top-level `CMakeLists.txt` validates the combination, generates `build_config.h`, and appends the driver component dirs (3.1 references only `components/core`; each later session appends the dirs it creates). `build.sh <env>` maps a named environment to its `-D<flag>=<value>` set and builds into `build/<env>/`, each env with its own isolated `sdkconfig`. Target is the classic dual-core ESP32 with 4 MB flash and the OTA partition layout of §19.1.

**Tech Stack:** ESP-IDF **v5.3.2** (pinned in `.idf-version`, sourced via `tools/idf-env.sh`), target **`esp32`**, C11, CMake >= 3.16, FreeRTOS, NimBLE, LittleFS (joltwallet managed component, added 3.3). The same `components/core` is proven on the Xtensa toolchain by `test_apps/core_selftest`. macOS host (Apple clang for host tests; the Xtensa cross-toolchain for firmware).

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` — sections referenced per session. Session 3.1 reads §4.2 (repo layout), §4.6 (build flags + named-environments table + generated `build_config.h`), §4.7 (boot sequence; 3.1 does the banner only), §19.1 (`partitions.csv`), §21.1 (top-level CMake behaviour), §21.2 (`sdkconfig.defaults`), §21.3 (`build.sh`), §21.5 (versioning). Later sessions: 3.2 §3.3/§4.7/§15.2/§17.1-17.2; 3.3 §13.1/§13.3/§12.5-12.7; 3.4 §4.3-4.5/§9.1; 3.5 §18.4/§15.3/§17.5; 3.6 §22.3-22.4.

**Roadmap:** `docs/superpowers/plans/2026-09-14-roadmap.md` — plan 3 of window A (dev board), sessions 3.1-3.6. Sequenced after plan 02 (`2.7 -> 3.1`). The `firmware.yml` matrix (`build (moto_neo6m)`, `build (moto_sim)`) is guarded by `[ -f build.sh ]`; session 3.1 makes those checks real and they become REQUIRED.

## Global Constraints

- **ESP-IDF v5.3.2, target `esp32`.** All firmware is built through `build.sh` (the entry point) or, equivalently, `idf.py -B build/<env> -D<flags>`; never a bare `idf.py build` from the repo root (there is no default variant — `VARIANT`/`GPS`/`DISPLAY` are required and configuration fails without them).
- **`components/core/**` stays pure C11** — no ESP-IDF/FreeRTOS/driver header, no `malloc`/`free` — exactly as plans 01/02 built it (§4.1). The firmware links it as an IDF component unchanged; anything ESP-specific lives in `main/`, `components/drivers/*`, or `components/app/*`.
- **`build_config.h` is the authoritative compile-time configuration.** It is generated from `main/build_config.h.in` by the top-level `CMakeLists.txt` (`configure_file`) from the validated §4.6 flags; variant/capability decisions in firmware read `CFG_*`, never bare literals.
- **Version** comes from `git describe --tags --match 'v*' --dirty --always` (§21.5) into `CFG_FW_VERSION` and `esp_app_desc_t.version`. No `v*` tag exists until `plan-03-done` (the first `v0.1.0`), so the fallback (short commit hash, e.g. `41c7f21`) is expected throughout plan 03.
- **`sdkconfig.defaults` omits the four `CONFIG_SECURE_*` lines and the verification key** until roadmap session **5.5** (dev builds are unsigned); `keys/laptimer_pub.pem` and `sign_release.sh` land then. `grep -c SECURE_ sdkconfig.defaults` MUST be 0 in plan 03.
- **App image size ceiling.** The app image MUST stay <= `0x130000` bytes (1,245,184 B), keeping margin under the 1.25 MB OTA slot (§19.1); `build.sh <env> size` fails the build above that. It is a REQUIRED CI check.
- **Flashing waits for the user's explicit "ready"** (the user holds the BOOT button on this dev board). No `esptool`/`idf.py flash`/`build.sh flash` command runs before the user says ready. `build.sh flash` prints "Disconnect the battery pack before USB" and refuses without `--yes`.
- **Subagent models:** never dispatch a subagent on Fable; use haiku/sonnet/opus by task tier.
- **Every commit** ends with the two trailers:
  ```
  Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
  ```
- **Matrix/CI:** any change that adds or renames a firmware matrix environment updates the branch-protection required checks in the same PR (`gh api -X PATCH repos/superbrobenji/lap-timer/branches/main/protection/required_status_checks`).
- Working directory for all commands: repository root `/Users/benji/projects/personal/lap-timer`.

## Parallel execution rules

Session 3.1 is **serial scaffolding**. There is one build system and one boot path; the two tasks touch the same handful of files and are strictly ordered — the build system (Task 1) must exist before `main/` (Task 2) can configure, and the mandatory build/size verification only runs once both are present. Run them serially: one subagent per task, review between, a single session commit once the tree builds green (the ESP-IDF image cannot build until `main/` exists, so there is no green intermediate state to commit after Task 1 alone). Later sessions parallelise where the work is genuinely independent (e.g. separate driver components in 3.4); each such session states its own PARALLEL/SERIAL split at its start.

## File structure produced by this plan

Session 3.1 (this session) creates:

```
CMakeLists.txt              top-level: §4.6 flag cache vars, validation, build_config.h
                            generation, git-describe version, per-env SDKCONFIG,
                            EXTRA_COMPONENT_DIRS (grows per session), sdkconfig fragments
build.sh                    <env> -> -D<flag> map; build|flash|monitor|flash-monitor|
                            clean|size|menuconfig; 0x130000 app-image size gate; flash --yes
partitions.csv              OTA-capable 4 MB layout (§19.1, verbatim)
sdkconfig.defaults          shared IDF options (§21.2 minus the four SECURE_* lines)
sdkconfig.defaults.moto     variant fragment: CONFIG_LAPTIMER_VARIANT_MOTO=y
sdkconfig.defaults.car      variant fragment: CONFIG_LAPTIMER_VARIANT_CAR=y
main/
  CMakeLists.txt            app component: SRCS app_main.c, REQUIRES core esp_system,
                            includes the generated build_config.h from the build dir
  app_main.c                boot banner (§4.7 step 1 reset reason + step 8 version line);
                            full boot sequence lands across 3.2-3.5
  build_config.h.in         generated-header template (§4.6): CFG_VARIANT_*, CFG_GPS_NAME,
                            CFG_DISPLAY_NAME, CFG_PANEL_*, CFG_HWID, CFG_FW_VERSION,
                            CFG_HAS_PPS/SD/WIFI/BLE/BLE_RC, CFG_FUSED_LOG_HZ
  Kconfig.projbuild         CONFIG_LAPTIMER_VARIANT_* choice (menuconfig visibility; the
                            authoritative switch stays build_config.h)
```

Later sessions append (per §4.2, gated by the §4.6 flags in the top-level `CMakeLists.txt`):

```
3.2  components/hal/{CMakeLists.txt, include/hal/*.h}
     components/drivers/board_devkit_v1/**            + NVS layer, supervisor, task WDT
3.3  components/drivers/storage_internal/**           (LittleFS; adds joltwallet/littlefs)
     components/app/logger/**
3.4  components/drivers/gps_sim/**  imu_sim/**
     components/app/pipeline/**                        (tb + fusion + lap + drag on core 1)
3.5  components/drivers/export_serial/**  components/app/cmd/** (partial)  supervisor safe mode
3.6  docs/measurements.md                              (bench-day results)
```

---

---

## Session 3.1 — ESP-IDF build system and a bootable version banner

Roadmap exit: `./build.sh moto_neo6m flash monitor` shows the banner on the board; `build.sh` defines `moto_sim`; `sdkconfig.defaults` omits `SECURE_*`; tag `p03-d1`. Serial scaffolding. Ruling: the `CONFIG_BT_CTRL_PINNED_TO_CORE_0` symbol in spec §21.2 is `CONFIG_BTDM_CTRL_PINNED_TO_CORE_0` in ESP-IDF v5.3.2 — corrected in the file and the spec. `CONFIG_LITTLEFS_MAX_PARTITIONS` warns as unknown until session 3.3 adds the LittleFS component (expected). Per-env `SDKCONFIG` isolation (build/<env>/sdkconfig) added so the 7 named envs do not share one root config.

### Task 1: ESP-IDF build system + configuration

**Files:**
- Create: `CMakeLists.txt` (top level), `build.sh`, `partitions.csv`
- Create: `sdkconfig.defaults`, `sdkconfig.defaults.moto`, `sdkconfig.defaults.car`
- Create: `main/build_config.h.in`, `main/Kconfig.projbuild`

**Interfaces:**
- Produces: `build.sh <env> [build|flash|monitor|flash-monitor|clean|size|menuconfig] [--port ...] [--flash-size ...] [--yes]`, the single entry point for every firmware build. Named envs: `moto_neo6m`, `moto_sim`, `moto_neo6m_wifi`, `moto_m10`, `moto_m10_sd`, `car_neo6m`, `car_m10` (§4.6 table).
- Produces: the generated `build/<env>/build_config.h` (from `main/build_config.h.in`) that `main/` and every later driver/app component include for `CFG_*`.
- Produces: the OTA partition layout and the shared/variant `sdkconfig` fragments every environment builds against.

**Rulings (spec is the authority; ESP-IDF v5.3.2 reality noted):**
1. **`main/Kconfig.projbuild` is needed.** §21.2 keeps the variant in `CONFIG_LAPTIMER_VARIANT_*` Kconfig symbols "for menuconfig visibility"; those symbols do not exist in ESP-IDF, so this session defines them minimally as a `choice` in `main/Kconfig.projbuild`. The authoritative switch stays `build_config.h` (`CFG_VARIANT_*`).
2. **Per-environment `sdkconfig` isolation** (not spelled out in §21.1 but required by the multi-env design): the top-level CMake sets `SDKCONFIG` into `${CMAKE_BINARY_DIR}/sdkconfig`, so each `build/<env>/` keeps its own generated config and switching `-D` flags between envs never cross-contaminates (a `moto` build's fragment cannot leak into a `car` build). Without this, all seven envs would share one root `sdkconfig`.
3. **`sdkconfig.defaults` is verbatim from §21.2 minus the four `CONFIG_SECURE_*` lines** (`..._SIGNED_APPS_NO_SECURE_BOOT`, `..._SIGNED_APPS_ECDSA_SCHEME`, `..._SIGNED_ON_UPDATE_NO_SECURE_BOOT`, `..._BOOT_VERIFICATION_KEY`), which land in session 5.5 with the signing key. `grep -c SECURE_ sdkconfig.defaults` == 0.
4. **Two benign "unknown kconfig symbol" warnings** are expected on every configure and are non-fatal:
   - `LITTLEFS_MAX_PARTITIONS` — the joltwallet/littlefs managed component that defines it is added in session 3.3. Expected until then; kept verbatim.
   - `BT_CTRL_PINNED_TO_CORE_0` — in ESP-IDF v5.3.2 the ESP32 controller core-pin symbol is `CONFIG_BTDM_CTRL_PINNED_TO_CORE_0` (a choice option), and the controller already defaults to core 0, so the line is cosmetic. Kept verbatim per the "verbatim §21.2" instruction; **flag to write back to spec §21.2** (`BT_CTRL_PINNED_TO_CORE_0` -> `BTDM_CTRL_PINNED_TO_CORE_0`) in a later firmware session under the roadmap's "discoveries change the spec" protocol.
5. **`CONFIG_ESP_WIFI_ENABLED` is a promptless symbol in v5.3.2** (`default y if SOC_WIFI_SUPPORTED`), so the `=n` assignment is a no-op and the final config shows `=y` on every env. This is harmless: wifi is compile-available but is only linked when a wifi component is present (never in the ble-only banner build). The `# overridden in *_wifi envs` annotation is kept as written; the real wifi switch is which `conn_*` component is linked (CONN flag) and the `*_wifi` env's own config.
6. **`--flash-size`** is accepted and validated; `4MB` (the fixed §19.1 layout) is fully wired. `8MB`/`16MB` are rejected with a message pointing at §19.1 — enlarging the `storage` partition needs a generated CSV that lands with the larger flash module (window-driven), consistent with treating hardware upgrades as later build-flag work.

- [ ] **Step 1: top-level `CMakeLists.txt`** — implements §21.1's behaviour: declare the §4.6 flag cache vars; validate the combination (fatal on a bad combo — moto needs epaper|oled, car needs oled, `CONN_BLE_RC=ON` needs a ble CONN, enum membership); derive the `CFG_*` values; `git describe --tags --match 'v*' --dirty --always` with a fallback into `CFG_FW_VERSION` (and `PROJECT_VER`); `configure_file(main/build_config.h.in ${CMAKE_BINARY_DIR}/build_config.h)`; set the per-env `SDKCONFIG`; `set(EXTRA_COMPONENT_DIRS components/core)` with the comment listing the full §21.1 set that grows per session; `set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.${VARIANT}")`; `include($ENV{IDF_PATH}/tools/cmake/project.cmake); project(laptimer)`.

`CMakeLists.txt`:
```cmake
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
```

- [ ] **Step 2: `main/build_config.h.in`** — the §4.6 generated-header template, filled by `configure_file(@ONLY)` from the CMake vars in Step 1.

`main/build_config.h.in`:
```c
/* build_config.h -- generated from main/build_config.h.in by the top-level
 * CMakeLists.txt (spec §4.6). Do not edit; edit the build flags instead.
 * This is the authoritative compile-time configuration for the firmware. */
#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

#define CFG_VARIANT_MOTO   @CFG_VARIANT_MOTO@
#define CFG_VARIANT_CAR    @CFG_VARIANT_CAR@

#define CFG_GPS_NAME       "@GPS@"
#define CFG_DISPLAY_NAME   "@DISPLAY@"

#define CFG_PANEL_WS213V4  @CFG_PANEL_WS213V4@
#define CFG_PANEL_WS29V2   @CFG_PANEL_WS29V2@

#define CFG_HWID           "@CFG_HWID@"
#define CFG_FW_VERSION     "@CFG_FW_VERSION@"

#define CFG_HAS_PPS        @CFG_HAS_PPS@
#define CFG_HAS_SD         @CFG_HAS_SD@
#define CFG_HAS_WIFI       @CFG_HAS_WIFI@
#define CFG_HAS_BLE        @CFG_HAS_BLE@
#define CFG_HAS_BLE_RC     @CFG_HAS_BLE_RC@

#define CFG_FUSED_LOG_HZ   @CFG_FUSED_LOG_HZ@

#endif /* BUILD_CONFIG_H */
```

- [ ] **Step 3: `main/Kconfig.projbuild`** — the `CONFIG_LAPTIMER_VARIANT_*` choice (ruling 1).

`main/Kconfig.projbuild`:
```text
menu "LapTimer"

    choice LAPTIMER_VARIANT
        prompt "Vehicle variant"
        default LAPTIMER_VARIANT_MOTO
        help
            Vehicle variant. Selected by build.sh via -D VARIANT=<moto|car>
            (through the per-variant sdkconfig fragment) and kept here for
            menuconfig visibility only. The authoritative compile-time switch
            is build_config.h (CFG_VARIANT_*), generated from the build flags
            (spec §21.2).

        config LAPTIMER_VARIANT_MOTO
            bool "moto (motorcycle)"

        config LAPTIMER_VARIANT_CAR
            bool "car"
    endchoice

endmenu
```

- [ ] **Step 4: `partitions.csv`** — §19.1 verbatim (4 MB, OTA-capable; `ota_0`/`ota_1` = 1.25 MB each).

`partitions.csv`:
```text
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
otadata,    data, ota,      0xF000,   0x2000,
phy_init,   data, phy,      0x11000,  0x1000,
ota_0,      app,  ota_0,    0x20000,  0x140000,
ota_1,      app,  ota_1,    0x160000, 0x140000,
coredump,   data, coredump, 0x2A0000, 0x10000,
storage,    data, spiffs,   0x2B0000, 0x150000,
```

- [ ] **Step 5: `sdkconfig.defaults` + variant fragments** — §21.2 verbatim minus the four SECURE_* lines (ruling 3), plus the two one-line variant fragments (ruling 1).

`sdkconfig.defaults`:
```text
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_COMPILER_CXX_EXCEPTIONS=n
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_INT_WDT=y
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_UART_ISR_IN_IRAM=y
CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y
CONFIG_SPI_MASTER_ISR_IN_IRAM=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y
CONFIG_BTDM_CTRL_PINNED_TO_CORE_0=y
CONFIG_ESP_WIFI_ENABLED=n          # overridden in *_wifi envs
CONFIG_LITTLEFS_MAX_PARTITIONS=1
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
```

`sdkconfig.defaults.moto`:
```text
CONFIG_LAPTIMER_VARIANT_MOTO=y
```

`sdkconfig.defaults.car`:
```text
CONFIG_LAPTIMER_VARIANT_CAR=y
```

- [ ] **Step 6: `build.sh`** — §21.3: map each §4.6 named env to its `-D<flag>=<value>` set; `idf.py -B build/<env> -D... <cmd>`; `size` runs `idf.py size` then fails if `build/<env>/laptimer.bin` exceeds `0x130000` bytes; `flash`/`flash-monitor` print "Disconnect the battery pack before USB" and require `--yes`. `chmod +x build.sh` after creating it. Guards empty-array expansion so it runs under macOS bash 3.2.

`build.sh`:
```bash
#!/usr/bin/env bash
# LapTimer firmware build wrapper (spec §21.3).
#
#   ./build.sh <env> [build|flash|monitor|flash-monitor|clean|size|menuconfig] \
#              [--port /dev/ttyUSB0] [--flash-size 4MB|8MB|16MB] [--yes]
#
# <env> is a named environment from the §4.6 table. It is mapped to the
# -D<flag>=<value> set below and built into build/<env>/ with idf.py. `size`
# runs `idf.py size` and then fails the build if the app image exceeds
# 0x130000 bytes, keeping margin under the 1.25 MB OTA slot (§19.1). `flash`
# prints the battery-safety reminder and requires --yes before it proceeds.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APP_MAX_BYTES=$((0x130000))   # 1,245,184 B; hard-fail above this (§19.1)

usage() {
    cat >&2 <<'USAGE'
usage: ./build.sh <env> <command> [options]

  environments: moto_neo6m moto_sim moto_neo6m_wifi moto_m10 moto_m10_sd
                car_neo6m car_m10
  commands:     build flash monitor flash-monitor clean size menuconfig
  options:      --port <dev>              serial port for flash / monitor
                --flash-size 4MB|8MB|16MB flash module size (default 4MB)
                --yes                     confirm a flash (required by 'flash')
USAGE
    exit 2
}

[ $# -ge 2 ] || usage
ENV_NAME="$1"; CMD="$2"; shift 2

PORT=""
FLASH_SIZE="4MB"
ASSUME_YES=0
while [ $# -gt 0 ]; do
    case "$1" in
        --port)       PORT="${2:?--port needs a value}"; shift 2 ;;
        --flash-size) FLASH_SIZE="${2:?--flash-size needs a value}"; shift 2 ;;
        --yes)        ASSUME_YES=1; shift ;;
        *) echo "unknown option: $1" >&2; usage ;;
    esac
done

# ---- §4.6 named environments -> flags (VARIANT GPS IMU DISPLAY STORAGE CONN) ----
# Every other flag (IMU default, CONN_BLE_RC, EXPORT_SERIAL, PANEL) keeps its
# CMake default from the top-level CMakeLists.txt.
case "$ENV_NAME" in
    moto_neo6m)      FLAGS=(VARIANT=moto GPS=neo6m IMU=mpu6050 DISPLAY=epaper_ssd1680 STORAGE=internal CONN=ble) ;;
    moto_sim)        FLAGS=(VARIANT=moto GPS=sim   IMU=sim     DISPLAY=epaper_ssd1680 STORAGE=internal CONN=ble) ;;
    moto_neo6m_wifi) FLAGS=(VARIANT=moto GPS=neo6m IMU=mpu6050 DISPLAY=epaper_ssd1680 STORAGE=internal CONN=ble_wifi) ;;
    moto_m10)        FLAGS=(VARIANT=moto GPS=m10   IMU=mpu6050 DISPLAY=epaper_ssd1680 STORAGE=internal CONN=ble) ;;
    moto_m10_sd)     FLAGS=(VARIANT=moto GPS=m10   IMU=mpu6050 DISPLAY=epaper_ssd1680 STORAGE=sd       CONN=ble) ;;
    car_neo6m)       FLAGS=(VARIANT=car  GPS=neo6m IMU=mpu6050 DISPLAY=oled_ssd1309   STORAGE=internal CONN=ble) ;;
    car_m10)         FLAGS=(VARIANT=car  GPS=m10   IMU=mpu6050 DISPLAY=oled_ssd1309   STORAGE=internal CONN=ble) ;;
    *) echo "unknown environment: $ENV_NAME" >&2; usage ;;
esac

case "$FLASH_SIZE" in
    4MB) ;;
    8MB|16MB)
        echo "--flash-size $FLASH_SIZE is not wired yet: partitions.csv is the fixed 4 MB layout (§19.1)." >&2
        echo "A larger module enlarges the 'storage' partition via a generated CSV; that lands with the hardware." >&2
        exit 2 ;;
    *) echo "unknown --flash-size: $FLASH_SIZE (use 4MB|8MB|16MB)" >&2; usage ;;
esac

BUILD_DIR="build/${ENV_NAME}"
DFLAGS=()
for f in "${FLAGS[@]}"; do DFLAGS+=("-D${f}"); done

PORT_ARGS=()
[ -n "$PORT" ] && PORT_ARGS=(-p "$PORT")

run_idf() { idf.py -B "$BUILD_DIR" "${DFLAGS[@]}" "$@"; }

case "$CMD" in
    build)      run_idf build ;;
    clean)      run_idf fullclean ;;
    menuconfig) run_idf menuconfig ;;
    monitor)    run_idf ${PORT_ARGS[@]+"${PORT_ARGS[@]}"} monitor ;;
    size)
        run_idf size
        BIN="${BUILD_DIR}/laptimer.bin"
        [ -f "$BIN" ] || { echo "no image at $BIN -- run './build.sh $ENV_NAME build' first" >&2; exit 1; }
        bytes=$(wc -c < "$BIN"); bytes=$((bytes))
        if [ "$bytes" -gt "$APP_MAX_BYTES" ]; then
            printf 'FAIL: app image %d B exceeds the %d B (0x130000) limit (§19.1)\n' "$bytes" "$APP_MAX_BYTES" >&2
            exit 1
        fi
        printf 'OK: app image %d B within the %d B (0x130000) limit (§19.1)\n' "$bytes" "$APP_MAX_BYTES" ;;
    flash|flash-monitor)
        echo "Disconnect the battery pack before USB."
        if [ "$ASSUME_YES" -ne 1 ]; then
            echo "Refusing to flash without --yes (safety, §21.3)." >&2
            exit 1
        fi
        if [ "$CMD" = "flash" ]; then
            run_idf ${PORT_ARGS[@]+"${PORT_ARGS[@]}"} flash
        else
            run_idf ${PORT_ARGS[@]+"${PORT_ARGS[@]}"} flash monitor
        fi ;;
    *) echo "unknown command: $CMD" >&2; usage ;;
esac
```

**Task 1 verification (static — a full image needs `main/`, which Task 2 adds):**
- `bash -n build.sh` clean; `./build.sh` with no args prints usage and exits 2; an unknown env / unknown command / `--flash-size 8MB` each exit non-zero with a message.
- The `CMakeLists.txt` parses (no CMake syntax error); `configure_file` and the validation branches are exercised end-to-end in Task 2's build.
- `grep -c SECURE_ sdkconfig.defaults` == 0.
- No trailing whitespace / CRLF; final newline present (`.editorconfig`: 4-space, LF).

---

### Task 2: bootable version-banner `app_main` + build/size verification

**Files:**
- Create: `main/CMakeLists.txt`, `main/app_main.c`

**Interfaces:**
- Consumes: `core_version()` (from `components/core`, unchanged), `esp_reset_reason()` (`esp_system`), and the generated `build_config.h` (`CFG_FW_VERSION`, `CFG_HWID`, `CFG_GPS_NAME`, `CFG_DISPLAY_NAME`, `CFG_FUSED_LOG_HZ`).
- Produces: the first real firmware image `build/<env>/laptimer.bin` (distinct from `test_apps/core_selftest`). This is the point at which the tree first builds an image; the session commits here.

**Notes:**
- `main/CMakeLists.txt` registers `app_main.c` with `REQUIRES core esp_system` and adds `${CMAKE_BINARY_DIR}` to the include path so `#include "build_config.h"` resolves to the generated header. `esp_log`, `freertos` come in through IDF's common requires.
- `app_main.c` prints the §4.7-step-8 banner (`LapTimer <CFG_FW_VERSION> (<CFG_HWID>)`), a capability line (`core <version> | GPS <name> | display <name> | fused-log <n> Hz`), and the §4.7-step-1 reset reason via `esp_reset_reason()`, then idles in a `vTaskDelay` loop. No NVS, storage, drivers, tasks, queues, or rings yet — those are 3.2-3.5.

- [ ] **Step 1: `main/CMakeLists.txt`**

`main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES core esp_system)

# build_config.h is generated by the top-level CMakeLists.txt into the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
```

- [ ] **Step 2: `main/app_main.c`** — the version banner + reset reason, then idle.

`main/app_main.c`:
```c
/* app_main.c -- LapTimer firmware entry point.
 *
 * Session 3.1 is the first bootable firmware: it prints the version banner and
 * idles. The full boot sequence (§4.7) -- NVS, crash-loop check, RTC resume,
 * config, board bring-up, storage, display, self-test, task WDT, queues/rings,
 * and the pipeline/logger/ui/power/conn tasks -- lands across sessions 3.2-3.5.
 */
#include "build_config.h"
#include "core/core.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "laptimer";

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-WDT";
    case ESP_RST_TASK_WDT:  return "task-WDT";
    case ESP_RST_WDT:       return "other-WDT";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
    }
}

void app_main(void)
{
    /* §4.7 step 1: record the reset reason (NVS counters land in 3.2). */
    esp_reset_reason_t reason = esp_reset_reason();

    /* §4.7 step 8: the version banner. */
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", reset_reason_str(reason), (int)reason);

    /* No drivers, tasks, queues or rings yet: idle and let the idle task feed
     * the (not-yet-armed) watchdog. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 3: build and size-check `moto_neo6m`, build `moto_sim`.** From the repo root, `source tools/idf-env.sh` first (ESP-IDF v5.3.2). The mandatory checks (also the REQUIRED `firmware.yml` matrix jobs):

```bash
source tools/idf-env.sh
./build.sh moto_neo6m build      # -> build/moto_neo6m/laptimer.bin
./build.sh moto_neo6m size       # app image <= 0x130000 B, else fails
./build.sh moto_sim   build      # sim env configures + builds the same banner (sim drivers arrive 3.4)
```

**Verified results (ESP-IDF v5.3.2, target esp32, in a scratch worktree at `main` = `41c7f21`):**
- `./build.sh moto_neo6m build` — success; `build/moto_neo6m/laptimer.bin` generated. `check_sizes.py`: `laptimer.bin binary size 0x355f0 bytes. Smallest app partition is 0x140000 bytes. 0x10aa10 bytes (83%) free.`
- `./build.sh moto_neo6m size` — **PASS**: `OK: app image 218608 B within the 1245184 B (0x130000) limit (§19.1)` (`idf.py size` Total image size 218,492 B).
- `./build.sh moto_sim build` — success; app image **218,592 B** (also `size`-clean).
- Generated `build/moto_neo6m/build_config.h`: `CFG_VARIANT_MOTO 1`, `CFG_GPS_NAME "neo6m"`, `CFG_DISPLAY_NAME "epaper_ssd1680"`, `CFG_PANEL_WS29V2 1`, `CFG_HWID "moto_neo6m_epaper"`, `CFG_FW_VERSION "41c7f21"`, `CFG_HAS_PPS/SD/WIFI 0`, `CFG_HAS_BLE 1`, `CFG_HAS_BLE_RC 0`, `CFG_FUSED_LOG_HZ 10`. The `moto_sim` header differs as expected: `CFG_GPS_NAME "sim"`, `CFG_HWID "moto_sim_epaper"`.
- `strings build/moto_neo6m/laptimer.bin` contains the banner format `LapTimer %s (%s)`, the version `41c7f21`, and the hwid `moto_neo6m_epaper`; the version is also embedded in `esp_app_desc_t.version`.
- `grep -c SECURE_ sdkconfig.defaults` == `0`.
- `git diff --check` clean; the roadmap hygiene check silent; each `build/<env>/` holds its own `sdkconfig` and no root `sdkconfig` is created.

**App image size for the plan record: `moto_neo6m` 218,608 B (0x355f0), `moto_sim` 218,592 B — ~17% of the 0x140000 OTA slot, ~18% of the 0x130000 gate. Plenty of headroom for the drivers, tasks, and NimBLE that land in 3.2-3.5.**

- [ ] **Step 4: flash + monitor on the board — only after the user says "ready".** The roadmap exit criterion is `./build.sh moto_neo6m flash monitor` showing the banner on the board. Flashing waits for the user's explicit "ready" (they hold the BOOT button and disconnect any battery pack first). Do not run any `esptool`/`build.sh flash` command before then. When ready:

```bash
./build.sh moto_neo6m flash-monitor --port <dev> --yes
```

Expected on the console (baud 921600): `I (…) laptimer: LapTimer 41c7f21 (moto_neo6m_epaper)`, the capability line, and `reset reason: power-on (1)`.

- [ ] **Step 5: commit the session and tag `p03-d1`.** The ESP-IDF image only builds once `main/` exists, so Tasks 1 and 2 land in a **single** session commit on branch `s3.1-fw-scaffold` (the first green build). Open the PR, wait for the five required checks (which now include the real `build (moto_neo6m)` and `build (moto_sim)`), squash-merge, then tag.

```bash
git checkout s3.1-fw-scaffold   # already created from main 41c7f21
git add CMakeLists.txt build.sh partitions.csv sdkconfig.defaults \
        sdkconfig.defaults.moto sdkconfig.defaults.car main/
git commit -F - <<'MSG'
plan 03 session 3.1: ESP-IDF scaffolding + version banner

First real firmware (distinct from test_apps/core_selftest). Top-level
CMakeLists.txt does §4.6 variant selection + validation, build_config.h
generation, git-describe versioning, per-env sdkconfig isolation, and the
EXTRA_COMPONENT_DIRS that grows per session. build.sh maps each named env to
its -D<flag> set and gates the app image at 0x130000 bytes. Adds partitions.csv
(§19.1), sdkconfig.defaults + moto/car fragments (SECURE_* deferred to 5.5), and
a bootable main/app_main.c that prints the LapTimer version banner, hardware id,
core version, and reset reason. moto_neo6m and moto_sim both build; app image
~218 KB.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
MSG

gh pr create --base main --title "plan 03 session 3.1: ESP-IDF scaffolding + version banner" \
             --body-file .github/PULL_REQUEST_TEMPLATE.md
# after the five required checks pass and the PR is squash-merged:
git checkout main && git pull
git tag p03-d1 && git push origin p03-d1
```

(The first `v0.1.0` tag is created at `plan-03-done`, not here; until then `CFG_FW_VERSION` is the short commit hash.)

---

## Session 3.2 — board driver, NVS, boot sequence, supervisor, `dbg status`

Roadmap exit: `dbg status` on the console shows boot count, reset reason, heartbeats; tag `p03-d2`. Serial (4 tasks). Uses ESP-IDF built-ins per the user's directive: `nvs`, `esp_console`+`linenoise`, `esp_task_wdt`, `adc_oneshot`+`adc_cali`, `i2c_master`, built-in `gpio` ISR + `esp_timer`, `esp_sleep`/`rtc_gpio`. Rulings (spec write-backs in Task 1): (1) the board HAL component is `components/lt_hal` not `components/hal` — a project component literally named `hal` shadows ESP-IDF's built-in `hal` and breaks the build; the include prefix stays `hal/`. (2) `ADC_ATTEN_DB_11` → `ADC_ATTEN_DB_12` (v5.3.2 rename; DB_11 is deprecated and would fail `-Werror`). (3) `components/app` is introduced here (3.2), earlier than §4.2's note. `components/core` stays pure C11 (no IDF).

### Task 1: HAL interface (`lt_hal`) + `components/app` skeleton + CMake wiring

**Files:**
- Create: `components/lt_hal/CMakeLists.txt`, `components/lt_hal/include/hal/board.h`
- Create: `components/app/CMakeLists.txt`, `components/app/include/app/{lt_err.h,lt_sup.h,lt_nvs.h,lt_rtc.h,dbg_console.h}`, `components/app/sys/lt_sys.c`
- Edit: top-level `CMakeLists.txt` (append the three new component dirs), `main/CMakeLists.txt` (REQUIRES)

**Interfaces:**
- Produces: the `hal/board.h` contract (§5.1) drivers implement and app tasks call; the `app` component's shared runtime state — `g_hb[]`, `sys_flags` (§4.4/§17.4), `g_btn_q` (§4.4) — and the header declarations the NVS layer (T3) and supervisor (T4) fill in.
- Consumes: nothing new; wires the components into the §21.1 search path.

**Rulings (spec is the authority; the ESP-IDF v5.3.2 reality is noted):**
1. **`components/hal` → `components/lt_hal` (component rename).** ESP-IDF v5.3.2 ships a built-in component **literally named `hal`** (`$IDF_PATH/components/hal`, the hardware-abstraction layer every driver/`esp_hw_support`/`freertos` depends on). A project component whose directory basename is `hal` **silently overrides** it: a reconfigure with `EXTRA_COMPONENT_DIRS ... components/hal` resolved `hal` to our path and dropped IDF's `hal` from the component list — the build would then fail catastrophically. Renamed the component to **`lt_hal`** (consistent with the project's `lt_*` namespace). The include prefix is unchanged: `INCLUDE_DIRS include` with `include/hal/board.h`, so drivers/app still `#include "hal/board.h"`. **Flag to write back to spec §4.2** (`components/hal/` → `components/lt_hal/`, or a note that the dir must not be named `hal`). `components/app` and `components/core` do **not** collide (no IDF component named `app` or `core`).
2. **`components/app` arrives in 3.2, not 3.4.** §4.2 lists `components/app` under 3.4, but the boot sequence and supervisor this session builds live in it, so it is introduced now (the brief calls for it). The top-level comment is updated accordingly.
3. **`app` uses a `file(GLOB_RECURSE *.c)` source list** (the same idiom `components/core` uses), so T3/T4 add `.c` files without editing `components/app/CMakeLists.txt`. `lt_hal` is header-only: `idf_component_register(INCLUDE_DIRS include)` with no SRCS (an IDF INTERFACE component).
4. **`hal/board.h` is the §5.1 slice verbatim**, wrapped with an include guard and `<stdint.h>`/`<stdbool.h>` (the §5.1 excerpt shows declarations only; those are needed for `bool` and the fixed-width types) so it is a self-contained, compilable header.
5. **Strict flags on `app`** match `components/core` (§17.9): `-Wall -Wextra -Werror -Wshadow -Wconversion` with `-Wno-error=conversion/-sign-conversion/-float-conversion` so IDF macros/inline helpers do not break `-Werror`. `main` REQUIRES `app lt_hal board_devkit_v1` (plus `nvs_flash esp_timer esp_hw_support`) — one board driver, linked directly (the §4.1 swappable-driver interface-name registration is a 3.4 concern).

- [ ] **Step 1: `components/lt_hal/CMakeLists.txt`** (INTERFACE component; see ruling 1).

```cmake
# lt_hal -- HAL interface headers (spec §4.1/§5.1). Header-only INTERFACE component:
# drivers implement these contracts, app/ links against them, coupling lives only here.
#
# NOTE: the spec §4.2 names this directory components/hal, but ESP-IDF ships a built-in
# component named `hal`; a project component of the same name silently OVERRIDES it and
# breaks the build (every driver depends on IDF's hal). Renamed to lt_hal; the include
# prefix stays `hal/` (drivers do #include "hal/board.h"). See draft32 task 1 ruling.
idf_component_register(INCLUDE_DIRS include)
```

- [ ] **Step 2: `components/lt_hal/include/hal/board.h`** (§5.1 verbatim + guard/includes).

```c
/* hal/board.h -- board HAL contract (spec §5.1, called from app tasks).
 *
 * The declarations below are the §5.1 board.h slice verbatim; the include guard and the
 * <stdint.h>/<stdbool.h> includes (needed for bool / the fixed-width types the §5.1 slice
 * uses) are added here so this is a self-contained, compilable header. All functions return
 * int (0 = OK, negative = -errno-style) unless noted; each is called from one task only and
 * is not reentrant. The concrete implementation is components/drivers/board_devkit_v1.
 */
#ifndef HAL_BOARD_H
#define HAL_BOARD_H

#include <stdbool.h>
#include <stdint.h>

int  board_init(void);
int  board_gps_power(bool on);
int  board_battery_read_mv(uint16_t *mv);          /* 64-sample average, calibrated */
int  board_charger_present(bool *out);             /* CHRG pin, or false if not wired */
int  board_buttons_read(uint8_t *mask);            /* bit0 MODE, bit1 UP, bit2 DOWN */
int  board_buttons_enable_isr(void (*cb)(uint8_t mask, int64_t mono_us));
int  board_prepare_deep_sleep(void);               /* holds, EXT0/EXT1 masks, RTC pulls */
int  board_pps_enable(void (*cb)(int64_t mono_us));   /* no-op when CFG_HAS_PPS == 0 */
const char *board_name(void);

#endif /* HAL_BOARD_H */
```

- [ ] **Step 3: `components/app/CMakeLists.txt`** (GLOB sources; strict flags; REQUIRES).

```cmake
# app -- IDF-dependent glue (spec §4.1): NVS layer (§15.2), RTC state (§15.3), supervisor
# (§4.3/§17.2), shared runtime state (hb[]/sys_flags, §4.4/§17.4), and the minimal dbg console.
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
    REQUIRES core lt_hal nvs_flash esp_timer esp_system console)

# build_config.h (CFG_HAS_PPS etc.) from the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

- [ ] **Step 4: shared headers.** `app/lt_err.h` — the §17.7 `E_SYS_*` subset 3.2 needs (full table lands with the 3.5 console).

```c
/* app/lt_err.h -- system error codes (spec §17.7). 3.2 needs the E_SYS_* subset used by the
 * boot sequence, crash-loop detection and supervisor; the full code table lands with the
 * serial console (3.5). Codes are stable u16 values logged into the error ring (§15.2). */
#ifndef APP_LT_ERR_H
#define APP_LT_ERR_H

enum {
    E_SYS_WDT_RESET   = 0x0501,
    E_SYS_PANIC       = 0x0502,
    E_SYS_BROWNOUT    = 0x0503,
    E_SYS_HEAP_LOW    = 0x0504,
    E_SYS_SAFE_MODE   = 0x0505,
    E_SYS_TASK_STALL  = 0x0506,
    E_SYS_RTC_INVALID = 0x0507,
    E_SYS_CFG_RESET   = 0x0508,
    E_SYS_STACK_LOW   = 0x0509,
};

#endif /* APP_LT_ERR_H */
```

`app/lt_sup.h` — `g_hb[]`, `sys_flags`, `g_btn_q` and the supervisor API (implemented in T4).

```c
/* app/lt_sup.h -- shared runtime state + supervisor (spec §4.3, §4.4, §17.2, §17.4).
 *
 * hb[] and sys_flags are the cross-task state of §4.4: every task bumps its heartbeat, the
 * supervisor watches them and owns the fault flags. btn_q is the button ISR -> ui channel.
 */
#ifndef APP_LT_SUP_H
#define APP_LT_SUP_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Heartbeat slots -- one per canonical §4.3 task (hb[6], §4.4). A task bumps g_hb[HB_x] once
 * per loop; the supervisor detects a stall when the count stops advancing. In 3.2 only the
 * supervisor is live; pipeline/logger/ui/conn/power register as they land (3.4+). */
enum { HB_PIPELINE = 0, HB_SUPERVISOR, HB_LOGGER, HB_UI, HB_CONN, HB_POWER, HB_COUNT };
extern volatile uint32_t g_hb[HB_COUNT];

/* sys_flags bits (§17.4), backed by an atomic u32. */
enum {
    SYS_GPS_DEAD = 0, SYS_GPS_NOFIX, SYS_IMU_DEAD, SYS_IMU_SUSPECT, SYS_DISP_DEAD,
    SYS_STORAGE_DEAD, SYS_STORAGE_FULL, SYS_STORAGE_DEGRADED, SYS_BATT_LOW,
    SYS_SAFE_MODE, SYS_HEAP_LOW, SYS_DISP_TEMP_THROTTLE, SYS_OTA_PENDING, SYS_FUSION_DISAGREE,
};
uint32_t sys_flags_get(void);
void     sys_flags_set(uint8_t bit);
void     sys_flags_clear(uint8_t bit);

/* btn_q: button ISR -> ui (§4.4, depth 8, 4 B item). Created in boot step 11 by lt_queues_init();
 * the button_evt_t layout is finalised by the ui (3.4) -- 3.2 fixes only its 4-byte size. */
typedef struct { uint8_t mask; uint8_t flags; uint16_t age_ms; } button_evt_t;
extern QueueHandle_t g_btn_q;
void lt_queues_init(void);

/* Supervisor task: core 0, prio 22, stack 3072, 1000 ms (§4.3), subscribed to the task WDT. */
void sup_start(void);
/* Register a task for heartbeat-stall watch (§17.2). stall_s = HB_STALL_S for that task. */
int  sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s);

/* Install the core assertion hook (§17.9): core asserts -> NVS error ring. Call once at boot. */
void sup_install_assert_hook(void);

#endif /* APP_LT_SUP_H */
```

`app/lt_nvs.h` — the NVS-layer API (implemented in T3).

```c
/* app/lt_nvs.h -- persistent state on NVS (spec §15.2). Thin lap-timer layer over the IDF
 * `nvs`/`nvs_flash` API: the boot counter, the 9 crash/health counters, the 32-entry error
 * ring, the crash log (crash-loop detection, §17.5), the safe-mode gate, and the packed cfg
 * blob (version + CRC16 via core). Write policy per §15.2: error-ring entries persist
 * immediately; counters are batched (>=60 s) except on crash paths; cfg only on change.
 */
#ifndef APP_LT_NVS_H
#define APP_LT_NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/cfg.h"

/* The 9 u32 counters of lt_err/ctr (§15.2), in blob order. */
typedef struct {
    uint32_t boots, crashes, wdt, brownout, gps_reset, i2c_recover, sto_format, ota_ok, ota_rollback;
} lt_counters_t;

typedef enum {
    LT_CTR_BOOTS = 0, LT_CTR_CRASHES, LT_CTR_WDT, LT_CTR_BROWNOUT, LT_CTR_GPS_RESET,
    LT_CTR_I2C_RECOVER, LT_CTR_STO_FORMAT, LT_CTR_OTA_OK, LT_CTR_OTA_ROLLBACK,
} lt_counter_id_t;

/* Opens namespaces and loads counters + error ring + crash log into RAM. 0 ok, <0 on NVS error. */
int  lt_nvs_init(void);

/* Boot counter (lt_sys/boot_cnt). */
uint32_t lt_nvs_boot_inc(void);   /* ++ and persist; returns the new value */
uint32_t lt_nvs_boot_get(void);

/* Counters (lt_err/ctr). inc updates RAM + marks dirty; crash-path callers pass persist=true
 * to write immediately (§15.2). flush persists a dirty set when force || >=60 s since last write. */
void     lt_counters_inc(lt_counter_id_t id, bool persist);
int      lt_counters_flush(bool force);
const lt_counters_t *lt_counters(void);

/* Error ring (lt_err/ring): append {code, uptime_s, boot, arg}; persists immediately. */
int  errlog_add(uint16_t code, uint32_t arg);

/* Crash log (lt_sys/crash_log): shift in {reset_reason, prev_uptime_s} at boot (§17.5). */
void lt_crashlog_push(uint8_t reset_reason, uint32_t prev_uptime_s);
/* True when the last 3 logged resets are all abnormal with uptime < 60 s (§17.5). */
bool lt_crashlog_is_loop(void);

/* Safe-mode gate (lt_sys/safe_until): boot counter through which safe mode applies (§17.5). */
uint32_t lt_safe_until_get(void);
int      lt_safe_until_set(uint32_t boot_cnt);

/* cfg blob (lt_cfg/cfg): load validates version+CRC16 then cfg_validate (returns corrections,
 * <0 => absent/corrupt so the caller keeps its defaults). save packs + CRC16 + writes. */
int  lt_cfg_load(cfg_t *c);
int  lt_cfg_save(const cfg_t *c);

/* True for reset reasons counted as a crash for §17.5 (panic / any WDT / brownout). */
bool lt_reset_is_abnormal(int reset_reason);
/* Human-readable reset reason (shared by the banner and dbg status). */
const char *lt_reset_reason_str(int reset_reason);
/* Boot-time bookkeeping: count + log + crash-log the reset reason (§4.7 step 1). */
void lt_boot_record_reset(int reset_reason, uint32_t prev_uptime_s);

#endif /* APP_LT_NVS_H */
```

`app/lt_rtc.h` — RTC state (§15.3) + the crash-loop uptime tracker (implemented in T3).

```c
/* app/lt_rtc.h -- RTC-memory state (spec §15.3) + the crash-loop uptime tracker.
 *
 * rtc_state_t is the deep-sleep/resume snapshot. 3.2 defines it and validates/clears it at
 * boot (§4.7 step 4); full resume is 3.5. Separately, a tiny RTC_DATA_ATTR uptime cell is
 * kept so the boot-time crash-loop check (§17.5) can learn how long the *previous* boot ran
 * (esp_timer resets on every reset; RTC slow memory survives WDT/panic and, usually, sleep).
 */
#ifndef APP_LT_RTC_H
#define APP_LT_RTC_H

#include <stdbool.h>
#include <stdint.h>

#include "core/types.h"   /* lap_result_t, LAP_MAX_SECTORS */

#define RTC_STATE_MAGIC   0x4C505452u   /* 'LPTR' */
#define RTC_STATE_VERSION 1

typedef struct {
    uint32_t magic;            /* RTC_STATE_MAGIC */
    uint8_t  version;          /* RTC_STATE_VERSION */
    uint8_t  mode, power_state, _pad;
    int64_t  saved_gps_us;     /* resume only if now - saved < RTC_RESUME_MAX_S (checked in 3.5) */
    int64_t  session_epoch_mono_us;
    char     session_id[10];
    uint16_t venue_id, layout_id;
    uint16_t lap_no; uint8_t sector_idx; uint8_t _pad2;
    int64_t  lap_start_gps_us;
    int64_t  gate_times[LAP_MAX_SECTORS + 1];
    lap_result_t best, prev;   /* trimmed copies */
    uint32_t partial_count;    /* e-paper partial refreshes since last full */
    uint32_t crc32;            /* CRC32 over all bytes except crc32 */
} rtc_state_t;

typedef enum { RTC_ABSENT = 0, RTC_VALID, RTC_INVALID } rtc_validity_t;

/* Validate the RTC snapshot: ABSENT (cold / magic clear), VALID (magic+version+crc ok), or
 * INVALID (present but bad -> caller logs E_SYS_RTC_INVALID). If out != NULL a VALID copy is
 * returned for 3.5's resume; 3.2 does not resume. */
rtc_validity_t lt_rtc_validate(rtc_state_t *out);
void           lt_rtc_clear(void);   /* zero the snapshot (magic cleared) */

/* Crash-loop uptime tracker. prev_s() returns the uptime the previous boot reached (0 if the
 * RTC cell is cold/invalid); the supervisor calls update_s() each loop with this boot's uptime. */
uint32_t lt_rtc_uptime_prev_s(void);
void     lt_rtc_uptime_update_s(uint32_t uptime_s);

#endif /* APP_LT_RTC_H */
```

`app/dbg_console.h` — the minimal console entry point (implemented in T4).

```c
/* app/dbg_console.h -- diagnostics console entry point (spec §18.4; 3.2 status, 3.3 storage/logger verbs).
 *
 * Starts an IDF esp_console REPL on UART0 and registers a single `dbg` command with the verbs
 * `status` (boot count, reset reason, uptime, crash counters, sys_flags, heartbeats), `logtest [n]`
 * (drive the logger end-to-end for the power-cut exit test), `fs` (mount/free/eviction state),
 * `sum <id>` and `logck <id>` (read a session's `.sum`/`.log` back through a `core/ses` reader). The
 * full §18.4 export console (status/list/open/get/... and export) replaces this in session 3.5. */
#ifndef APP_DBG_CONSOLE_H
#define APP_DBG_CONSOLE_H

void dbg_console_start(int reset_reason);   /* reset_reason: esp_reset_reason() from boot */

#endif /* APP_DBG_CONSOLE_H */
```

- [ ] **Step 5: `components/app/sys/lt_sys.c`** — the shared-state definitions (this is the one source that lets `app` build from T1 onward).

```c
/* lt_sys.c -- shared cross-task runtime state (spec §4.4, §17.4).
 *
 * Defines hb[] (heartbeats), sys_flags (atomic fault bits), and btn_q (button ISR -> ui).
 * Kept in its own translation unit so it exists from the app-skeleton task onward, before the
 * supervisor and NVS layer land. Static allocation only (no malloc, §17.9).
 */
#include <stdatomic.h>

#include "app/lt_sup.h"

volatile uint32_t g_hb[HB_COUNT];

static _Atomic uint32_t s_sys_flags;

uint32_t sys_flags_get(void) { return atomic_load(&s_sys_flags); }
void     sys_flags_set(uint8_t bit) { atomic_fetch_or(&s_sys_flags, (uint32_t)1u << bit); }
void     sys_flags_clear(uint8_t bit) { atomic_fetch_and(&s_sys_flags, ~((uint32_t)1u << bit)); }

QueueHandle_t g_btn_q;
static StaticQueue_t s_btn_q_ctrl;
static uint8_t       s_btn_q_store[8 * sizeof(button_evt_t)];   /* depth 8 (§4.4) */

void lt_queues_init(void)
{
    if (!g_btn_q) {
        g_btn_q = xQueueCreateStatic(8, sizeof(button_evt_t), s_btn_q_store, &s_btn_q_ctrl);
    }
}
```

- [ ] **Step 6: wire the top-level and `main` CMake.** Append the three dirs to `EXTRA_COMPONENT_DIRS` (keep the per-session-growth comment, updated for the rename and the early `app`) and give `main` its new REQUIRES.

> **Plan block drift (applied during Task 1 execution):** `components/drivers/board_devkit_v1` does not exist until Task 2. ESP-IDF v5.3.2's `project.cmake` raises a `FATAL_ERROR` at configure time when an `EXTRA_COMPONENT_DIRS` entry names a directory that does not exist (this is stricter than a missing `REQUIRES`, which resolves lazily and only fails if something actually needs it). Listing `components/drivers/board_devkit_v1` in `EXTRA_COMPONENT_DIRS` and in `main`'s `REQUIRES` — exactly as drafted below — makes `./build.sh moto_neo6m build` fail to configure. The minimal fix actually committed: omit `components/drivers/board_devkit_v1` from both `EXTRA_COMPONENT_DIRS` and `main`'s `REQUIRES` in Task 1 (a code comment marks the omission at both sites); Task 2 adds the directory and appends it back in both places once it exists. `main/CMakeLists.txt` and the top-level `CMakeLists.txt` blocks below are left as originally drafted for the record — the repo state after Task 1 differs from them only by that one component name in each.

`main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES core esp_system app lt_hal board_devkit_v1 nvs_flash esp_timer esp_hw_support)

# build_config.h is generated by the top-level CMakeLists.txt into the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
```

Top-level `CMakeLists.txt` (the `EXTRA_COMPONENT_DIRS` line and its comment):
```cmake
# ---- component search path (§21.1) ----
# 3.1 references only the components that exist now. §21.1's full set is appended
# by the session that creates each one:
#   3.2  components/lt_hal (HAL interface; named lt_hal because ESP-IDF ships a built-in
#        component literally named `hal` that a same-named project component would override),
#        components/app (NVS/RTC/supervisor/dbg -- brought in here, one session earlier than
#        §4.2's "3.4", because the boot sequence and supervisor need it), and
#        components/drivers/board_devkit_v1.
#   3.3  components/drivers/storage_${STORAGE}
#   3.4  components/drivers/gps_ubx_common   components/drivers/gps_${GPS}
#        components/drivers/imu_${IMU}   components/drivers/display_${DISPLAY}
#        + conn_ble / conn_wifi (per CONN), conn_ble_rc (if CONN_BLE_RC),
#          export_serial (if EXPORT_SERIAL)  -- gated exactly as in §21.1.
set(EXTRA_COMPONENT_DIRS components/core components/lt_hal components/app components/drivers/board_devkit_v1)
```

**Task 1 verification:** `lt_hal` + `app` (with only `lt_sys.c`) compile and link; IDF's built-in `hal` is retained (the rename resolves the override). `main` still runs the 3.1 banner (it calls nothing from `app` yet), so the tree builds green. Verified as part of the integrated build (below) — all components compile with no warnings from new code. **As executed (see drift note above), the build was verified with `board_devkit_v1` omitted from both CMake sites; Task 2 must add it back to both before its own driver can be exercised.**

Commit block:
```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

### Task 2: `board_devkit_v1` driver (§3.3 pin map, §5.1 `board.h`)

**Files:**
- Create: `components/drivers/board_devkit_v1/CMakeLists.txt`, `components/drivers/board_devkit_v1/board.c`

**Interfaces:**
- Consumes: `hal/board.h` (T1), generated `build_config.h` (`CFG_HAS_PPS`), IDF `driver` (gpio, rtc_io, i2c_master, spi_master), `esp_adc`, `esp_hw_support` (esp_sleep), `esp_timer`.
- Produces: the concrete `board_*` implementation `main` links against; brings up the I2C bus (IMU), the VSPI bus (e-paper/SD), the battery ADC, buttons, GPS-power MOSFET and deep-sleep wake config.

**Rulings (spec is the authority; ESP-IDF v5.3.2 reality noted):**
1. **`ADC_ATTEN_DB_11` → `ADC_ATTEN_DB_12`.** §3.3 specifies 11 dB attenuation; in v5.3.2 `ADC_ATTEN_DB_11` is `__attribute__((deprecated))` and aliased to `ADC_ATTEN_DB_12` (identical attenuation), so under `-Werror` it must be written `ADC_ATTEN_DB_12`. **Flag to write back to spec §3.3/§16.4.**
2. **New-API drivers only (no deprecated legacy driver).** Battery uses `adc_oneshot` + `adc_cali` line-fitting (ESP32 uses the line-fitting scheme, not curve-fitting) with the board's eFuse Vref; I2C uses the v5.3 `i2c_master` bus API; SPI uses `spi_bus_initialize` on `SPI3_HOST` (= VSPI). The deprecated `esp_adc_cal`/legacy `i2c`/`adc1_config_*` APIs (as literally named in §16.4) are **not** used — they would warn under `-Werror`.
3. **400 kHz is a per-device property in the new I2C API.** `board_init` creates the bus (SDA 21/SCL 22, glitch filter, internal pull-up as a weak backup to the GY-521's 4.7 kΩ); the IMU driver (3.4) sets `scl_speed_hz = 400000` when it adds its device.
4. **Battery reading** is the §16.4 trimmed mean: 64 raw samples, discard the 8 highest + 8 lowest, mean the middle 48, `adc_cali_raw_to_voltage`, ×2 for the 470k/470k tap. The two-point `battery.cal` correction (§15.1) is left to the power task (3.5), which owns cfg; the board returns the calibrated tap voltage ×2.
5. **Buttons: `gpio` ISR + `esp_timer` debounce (built-ins, no managed component).** `board_buttons_enable_isr` installs a shared `IRAM_ATTR` handler on 32/33/25 (`GPIO_INTR_ANYEDGE`), debounces in the ISR (25 ms), and calls the caller's `cb(mask, mono_us)` from ISR context — the caller's cb must be ISR-safe/`IRAM_ATTR` and only push to a queue (§17.9). No board-internal task; the ui's `btn_q` (T1/§4.4) is the destination in 3.4.
6. **GPS power on RTC-GPIO 26** (drive low = on, held high in sleep); **CS 5 and 15 idle-high** before any VSPI device attaches (both are strapping pins whose idle-high state is boot-safe); e-paper DC/RST/BUSY left to `display_epaper` (3.4). **Forbidden pins 0/2/12 and 6-11 are never touched; GPIO 12 is never pulled up.**
7. **Deep-sleep wake** per §3.3/§16.3: `esp_sleep_enable_ext1_wakeup({27,32,33,25}, ANY_HIGH)` + RTC pull-downs + `rtc_gpio_hold_en(26)`; EXT0 on charger 39. Implemented now though only called from 3.6+. `board_pps_enable` is compiled behind `#if CFG_HAS_PPS` (attaches a rising-edge ISR on 36 for the M10; a no-op on NEO-6M/sim).

**Hardware-only checks (orchestrator confirms on the board — cannot be verified by a build):**
- **Button presses** (32/33/25 active-high, pull-down) actually toggle `board_buttons_read`/fire the debounced ISR.
- **Battery voltage** — `board_battery_read_mv` returns a plausible pack mV (eFuse-calibrated ×2 divider); confirm against a meter.
- **GPS power pin** — GPIO26 low actually powers the GY-NEO6M (P-MOSFET gate), high cuts it, and `rtc_gpio_hold_en` holds it through sleep.
- **Charger sense** — GPIO39 reads low when charging; if the CHRG pin is **not wired** on the prototype it floats (spec marks it optional), so `board_charger_present` may be unreliable and EXT0 wake spurious until wired.
- **Deep-sleep wake** (EXT1 any-high on buttons/IMU-INT, EXT0 on charger) — exercised only from 3.6+.

- [ ] **Step 1: `components/drivers/board_devkit_v1/CMakeLists.txt`**

```cmake
# board_devkit_v1 -- ESP32 DevKit V1 board HAL implementation (spec §3.3 pin map, §5.1 board.h).
idf_component_register(
    SRCS "board.c"
    REQUIRES lt_hal driver esp_adc esp_timer esp_hw_support)

# build_config.h (CFG_HAS_PPS etc.) is generated into the build tree by the top-level CMake (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9): -Werror, conversion warnings non-fatal so IDF
# macros/inline helpers do not break the build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

- [ ] **Step 2: `components/drivers/board_devkit_v1/board.c`**

```c
/* board.c -- ESP32 DevKit V1 board HAL (spec §3.3 pin map, §3.4 wiring, §5.1 board.h).
 *
 * Owns the board-level peripherals the app tasks share: GPIO directions, the GPS power
 * MOSFET (RTC GPIO), the I2C bus (IMU/mag), the VSPI bus (e-paper/SD), the battery ADC,
 * the buttons, and the deep-sleep wake config. Sensor chips are driven by their own HAL
 * drivers (gps_*, imu_*, display_*) which attach to the buses this file brings up.
 *
 * IDF drivers used: driver/gpio, driver/rtc_io, driver/i2c_master (new v5.3 API),
 * driver/spi_master, esp_adc (adc_oneshot + adc_cali line-fitting), esp_sleep. No legacy
 * (deprecated) driver is used.
 *
 * Forbidden pins (§3.3): GPIO 0, 2, 12 (strapping) and 6-11 (SPI flash) are never touched;
 * GPIO 12 in particular is never pulled up (it selects 1.8 V flash and bricks boot).
 */
#include "hal/board.h"
#include "build_config.h"

#include <errno.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

static const char *TAG = "board";

/* ---- §3.3 pin map ---- */
#define PIN_GPS_PWR   GPIO_NUM_26   /* RTC GPIO; drive LOW = GPS on, held HIGH = off in sleep */
#define PIN_I2C_SDA   GPIO_NUM_21
#define PIN_I2C_SCL   GPIO_NUM_22
#define PIN_IMU_INT   GPIO_NUM_27   /* active-high; EXT1 wake */
#define PIN_SPI_SCK   GPIO_NUM_18   /* VSPI, shared e-paper / SD */
#define PIN_SPI_MOSI  GPIO_NUM_23
#define PIN_SPI_MISO  GPIO_NUM_19
#define PIN_EPD_CS    GPIO_NUM_5    /* idle high (strapping-safe) */
#define PIN_SD_CS     GPIO_NUM_15   /* idle high (strapping-safe) */
#define PIN_BTN_MODE  GPIO_NUM_32   /* active-high, pull-down; EXT1 wake */
#define PIN_BTN_UP    GPIO_NUM_33
#define PIN_BTN_DOWN  GPIO_NUM_25
#define PIN_CHRG      GPIO_NUM_39   /* open-drain active-low; input-only; EXT0 wake */
#define PIN_PPS       GPIO_NUM_36   /* input-only; used only on the M10 (CFG_HAS_PPS) */

#define ADC_BATT_CHANNEL  ADC_CHANNEL_6   /* GPIO34 = ADC1_CH6 (§3.3) */
#define BATT_SAMPLES      64              /* §16.4: 64 samples ... */
#define BATT_TRIM         8               /* ... discard 8 highest and 8 lowest */
#define BATT_DIVIDER      2               /* 470k/470k tap -> x2 (§3.4) */
#define BTN_DEBOUNCE_US   25000           /* 25 ms contact-bounce guard */

static i2c_master_bus_handle_t   s_i2c_bus;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_adc_cali;      /* NULL if eFuse calibration unavailable */
static bool                      s_inited;

/* button ISR -> caller cb (§4.4 button ISR -> ui). Debounced in the ISR; cb runs in ISR
 * context and MUST be ISR-safe / IRAM_ATTR (§17.9: ISRs only push to queues via *FromISR). */
static void (*s_btn_cb)(uint8_t mask, int64_t mono_us);
static volatile int64_t s_btn_last_us;

#if CFG_HAS_PPS
static void (*s_pps_cb)(int64_t mono_us);
#endif

static uint8_t read_button_mask(void)
{
    uint8_t m = 0;
    if (gpio_get_level(PIN_BTN_MODE)) m |= 0x1;
    if (gpio_get_level(PIN_BTN_UP))   m |= 0x2;
    if (gpio_get_level(PIN_BTN_DOWN)) m |= 0x4;
    return m;
}

static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    if (now - s_btn_last_us < BTN_DEBOUNCE_US) return;   /* debounce */
    s_btn_last_us = now;
    if (s_btn_cb) s_btn_cb(read_button_mask(), now);
}

#if CFG_HAS_PPS
static void IRAM_ATTR pps_isr(void *arg)
{
    (void)arg;
    if (s_pps_cb) s_pps_cb(esp_timer_get_time());
}
#endif

int board_init(void)
{
    if (s_inited) return 0;

    /* --- release any RTC GPIO hold left latched by board_prepare_deep_sleep from a prior
     *     sleep cycle: classic ESP32 RTC holds survive the deep-sleep reset, so without this
     *     the level set below and later board_gps_power() calls would be latched out --- */
    rtc_gpio_hold_dis(PIN_GPS_PWR);

    /* --- GPS power MOSFET gate on RTC GPIO 26: output, start OFF (driven high) --- */
    ESP_ERROR_CHECK(rtc_gpio_init(PIN_GPS_PWR));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(PIN_GPS_PWR, RTC_GPIO_MODE_OUTPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_set_level(PIN_GPS_PWR, 1));   /* high = GPS off until board_gps_power(true) */

    /* --- inputs: IMU INT (27) + buttons (32/33/25) with pull-downs --- */
    gpio_config_t in_pd = {
        .pin_bit_mask = (1ULL << PIN_IMU_INT) | (1ULL << PIN_BTN_MODE) |
                        (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* backs the external 100k; keeps 27 defined w/o IMU */
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_pd));

    /* --- charger sense (39): input-only pin, no internal pulls (external 100k pull-up) --- */
    gpio_config_t in_float = {
        .pin_bit_mask = (1ULL << PIN_CHRG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_float));

    /* --- shared-VSPI chip selects idle-high before any device attaches (5 and 15 are
     *     strapping pins whose idle-high state is boot-safe, §3.3) --- */
    gpio_config_t cs_out = {
        .pin_bit_mask = (1ULL << PIN_EPD_CS) | (1ULL << PIN_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cs_out));
    gpio_set_level(PIN_EPD_CS, 1);
    gpio_set_level(PIN_SD_CS, 1);
    /* e-paper DC(14)/RST(4)/BUSY(35) are owned by display_epaper (3.4); left alone here. */

    /* --- I2C master bus (new v5.3 API): SDA 21 / SCL 22. The 400 kHz SCL is a per-device
     *     property in this API; the IMU driver (3.4) sets scl_speed_hz=400000 when it adds
     *     its device. Internal pull-ups enabled as a weak backup to the GY-521's 4.7k. --- */
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus));

    /* --- VSPI (SPI3) bus: SCK 18, MOSI 23, MISO 19. Devices (e-paper/SD) attach in 3.3/3.4. --- */
    spi_bus_config_t spi_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    /* --- battery ADC: ADC1_CH6 (GPIO34), 12 dB atten, 12-bit, eFuse line-fit calibration.
     *     Spec §3.3 says "11 dB"; ADC_ATTEN_DB_11 is deprecated in v5.3.2 and aliased to
     *     ADC_ATTEN_DB_12 (identical ~150-2450 mV range), so DB_12 is used. --- */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_BATT_CHANNEL, &chan_cfg));
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,   /* only used if eFuse Vref is absent; this board has eFuse Vref */
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali) != ESP_OK) {
        s_adc_cali = NULL;
        ESP_LOGW(TAG, "ADC eFuse calibration unavailable; battery mV will be approximate");
    }

    s_inited = true;
    ESP_LOGI(TAG, "board_devkit_v1 init: I2C0(21/22) VSPI(18/23/19) ADC1_CH6(34) buttons(32/33/25)");
    return 0;
}

int board_gps_power(bool on)
{
    /* §3.4: P-MOSFET gate on GPIO26. Drive LOW = on, HIGH = off. */
    return rtc_gpio_set_level(PIN_GPS_PWR, on ? 0 : 1) == ESP_OK ? 0 : -EIO;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int board_battery_read_mv(uint16_t *mv)
{
    if (!mv) return -EINVAL;
    if (!s_adc) return -EIO;

    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_BATT_CHANNEL, &raw) != ESP_OK) return -EIO;
        s[i] = raw;
    }
    /* §16.4: discard the 8 highest and 8 lowest, mean the middle 48 */
    qsort(s, BATT_SAMPLES, sizeof(s[0]), cmp_int);
    int64_t sum = 0;
    for (int i = BATT_TRIM; i < BATT_SAMPLES - BATT_TRIM; i++) sum += s[i];
    int mean_raw = (int)(sum / (BATT_SAMPLES - 2 * BATT_TRIM));

    int v_tap_mv;
    if (s_adc_cali) {
        if (adc_cali_raw_to_voltage(s_adc_cali, mean_raw, &v_tap_mv) != ESP_OK) return -EIO;
    } else {
        /* uncalibrated fallback: 12-bit over the ~2450 mV DB_12 full scale */
        v_tap_mv = mean_raw * 2450 / 4095;
    }
    /* board returns the tap voltage x2 (§3.4 divider); the two-point battery.cal correction
     * (§15.1) is applied by the power task, which owns cfg. */
    int batt = v_tap_mv * BATT_DIVIDER;
    if (batt < 0) batt = 0;
    if (batt > 65535) batt = 65535;
    *mv = (uint16_t)batt;
    return 0;
}

int board_charger_present(bool *out)
{
    if (!out) return -EINVAL;
    /* CHRG is open-drain active-low (§3.3): present/charging => pin low. */
    *out = (gpio_get_level(PIN_CHRG) == 0);
    return 0;
}

int board_buttons_read(uint8_t *mask)
{
    if (!mask) return -EINVAL;
    *mask = read_button_mask();
    return 0;
}

int board_buttons_enable_isr(void (*cb)(uint8_t mask, int64_t mono_us))
{
    s_btn_cb = cb;
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;   /* INVALID_STATE = already installed */
    const gpio_num_t pins[] = { PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        ESP_ERROR_CHECK(gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], btn_isr, NULL));
    }
    return 0;
}

int board_prepare_deep_sleep(void)
{
    /* Called from the power task in 3.6+; implemented now per §16.3 step 5 / §3.3 wake rule.
     * Classic ESP32 EXT1 is one polarity across the whole mask, so buttons + IMU INT are all
     * active-high and wake ANY_HIGH; the charger uses EXT0 (single pin, level 0). */
    board_gps_power(false);
    rtc_gpio_hold_en(PIN_GPS_PWR);                 /* keep GPS off through sleep */

    const gpio_num_t ext1_pins[] = { PIN_IMU_INT, PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
    uint64_t ext1_mask = 0;
    for (size_t i = 0; i < sizeof(ext1_pins) / sizeof(ext1_pins[0]); i++) {
        rtc_gpio_pulldown_en(ext1_pins[i]);        /* RTC pull-downs in addition to externals (§3.3) */
        rtc_gpio_pullup_dis(ext1_pins[i]);
        ext1_mask |= (1ULL << ext1_pins[i]);
    }
    esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext0_wakeup(PIN_CHRG, 0);     /* charger low; harmless if CHRG unwired */
    return 0;
}

int board_pps_enable(void (*cb)(int64_t mono_us))
{
#if CFG_HAS_PPS
    s_pps_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_PPS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,            /* TIMEPULSE rising edge */
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_PPS, pps_isr, NULL));
    return 0;
#else
    (void)cb;
    return 0;   /* no usable PPS on this build (NEO-6M v2 / sim) */
#endif
}

const char *board_name(void)
{
    return "devkit_v1";
}
```

**Task 2 verification:** `board.c` compiles clean under `-Werror` (no ADC-deprecation warning — DB_12 is used) for `moto_neo6m` (PPS path compiled out) and, as a bonus check of the `#if CFG_HAS_PPS` branch, `moto_m10` (PPS ISR compiled). Runtime pin behaviour is a hardware-only check (above).

Commit block:
```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

### Task 3: NVS layer + RTC state + boot steps 1–5

**Files:**
- Create: `components/app/sys/lt_nvs.c`, `components/app/sys/lt_rtc.c`
- Edit: `main/app_main.c` (boot sequence §4.7 steps 1–5)

**Interfaces:**
- Consumes: IDF `nvs`/`nvs_flash`, `esp_timer`, `esp_rom` (`esp_rom_crc32_le`), `esp_system` (reset reasons), `core/cfg.h` (`cfg_t`, `cfg_defaults`, `cfg_apply_profile`, `cfg_validate`, `cfg_migrate`), `core/ses.h` (`ses_crc16`).
- Produces: the §15.2 persistent state — boot counter, 9 counters, 32-entry error ring, crash log, safe-mode gate, packed cfg blob — plus `errlog_add`, and the RTC snapshot (§15.3) validate/clear + crash-loop uptime tracker. `app_main` steps 1–5 use them.

**Rulings:**
1. **CRC16 for the cfg blob = `ses_crc16` (core).** §15.2 wants "packed `cfg_t` with leading version u8, trailing CRC16"; `cfg_t`'s first member is already `version`, and core exposes CRC-16/CCITT as `ses_crc16` (`core/ses.h`), reused here. The blob is `sizeof(cfg_t)` raw bytes + 2-byte LE CRC; load rejects a wrong size, CRC or version and the caller keeps defaults.
2. **Blob layouts are packed to the exact §15.2 sizes.** Error entry `{u16 code, u32 uptime_s, u16 boot, u32 arg}` = **12 B** → ring[32] = **384 B** + `head u8` = **385 B**; crash entry `{u8 reset_reason, u32 uptime_s}` = **5 B** → log[3] = **15 B**. `__attribute__((packed))` guarantees the sizes across toolchains. Counters are 9 contiguous `u32` written as one blob and indexed by the `lt_counter_id_t` enum.
3. **Crash-loop "previous uptime" comes from an RTC cell (§17.5).** `esp_timer` restarts at every reset, so how long the *previous* boot ran cannot be read directly. A tiny `RTC_DATA_ATTR` uptime cell (magic-guarded) is written each second by the supervisor (T4) and read at boot; the crash log pairs *this* boot's reset reason (how the previous session ended) with that uptime. `lt_crashlog_is_loop()` = the last 3 entries all abnormal (`PANIC/TASK_WDT/INT_WDT/WDT/BROWNOUT`) with uptime < 60 s. **Hardware-only check:** RTC slow memory must survive WDT/panic (it does) and brownout (marginal — a lost cell reads 0 < 60 s, i.e. conservatively counts as a fast crash).
4. **RTC state (step 4):** `rtc_state_t` is defined per §15.3 (`lap_result_t`/`LAP_MAX_SECTORS` from `core/types.h`, CRC32 via `esp_rom_crc32_le`). 3.2 validates and, on ABSENT/INVALID, clears it; only a present-but-bad snapshot logs `E_SYS_RTC_INVALID`. A VALID snapshot is left in place — full resume is 3.5.
5. **Config merge order (step 5, §15.1):** `cfg_defaults` → `cfg_apply_profile` (build-flag + MAC-derived BLE name) → NVS blob, so stored user settings win. Absent/corrupt blob → keep profile defaults, save them, log `E_SYS_CFG_RESET`; `cfg_validate` corrections > 0 → log `E_SYS_CFG_RESET` with the count and re-save.

- [ ] **Step 1: `components/app/sys/lt_nvs.c`**

```c
/* lt_nvs.c -- persistent lap-timer state on NVS (spec §15.2).
 *
 * A thin layer over the IDF nvs API. NVS storage primitives only; the policy that consumes
 * them (crash-loop -> safe mode, counter-flush cadence) lives in app_main / the supervisor.
 * Blob layouts are fixed on-flash formats: packed structs so their byte size matches §15.2.
 */
#include "app/lt_nvs.h"
#include "app/lt_err.h"

#include <string.h>

#include "core/ses.h"        /* ses_crc16 -- the core CRC-16/CCITT, reused for the cfg blob */
#include "esp_log.h"
#include "esp_system.h"      /* esp_reset_reason_t / ESP_RST_* */
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "lt_nvs";

/* ---- on-flash blob layouts (§15.2). Packed so sizes are exact. ---- */
#define ERR_RING_LEN   32
#define CRASH_LOG_LEN  3

typedef struct __attribute__((packed)) {
    uint16_t code;
    uint32_t uptime_s;
    uint16_t boot;
    uint32_t arg;
} err_entry_t;                                  /* 12 B -> ring = 384 B (§15.2) */

typedef struct __attribute__((packed)) {
    err_entry_t entry[ERR_RING_LEN];
    uint8_t     head;                           /* next write slot */
} err_ring_t;                                   /* 385 B */

typedef struct __attribute__((packed)) {
    uint8_t  reset_reason;
    uint32_t uptime_s;
} crash_entry_t;                                /* 5 B -> log = 15 B (§15.2) */

/* ---- namespaces / keys (§15.2) ---- */
#define NS_SYS  "lt_sys"
#define NS_ERR  "lt_err"
#define NS_CFG  "lt_cfg"
#define K_BOOT  "boot_cnt"
#define K_CRASH "crash_log"
#define K_SAFE  "safe_until"
#define K_RING  "ring"
#define K_CTR   "ctr"
#define K_CFG   "cfg"

#define COUNTER_FLUSH_US (60 * 1000000LL)       /* >=60 s batching (§15.2) */

/* ---- RAM mirrors ---- */
static nvs_handle_t   s_h_sys, s_h_err, s_h_cfg;
static bool           s_ready;
static uint32_t       s_boot_cnt;
static lt_counters_t  s_counters;
static bool           s_counters_dirty;
static int64_t        s_counters_last_us;
static err_ring_t     s_ring;
static crash_entry_t  s_crash[CRASH_LOG_LEN];

static uint32_t uptime_s_now(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

static int load_blob(nvs_handle_t h, const char *key, void *dst, size_t expect)
{
    size_t sz = 0;
    if (nvs_get_blob(h, key, NULL, &sz) != ESP_OK || sz != expect) return -1;
    return nvs_get_blob(h, key, dst, &sz) == ESP_OK ? 0 : -1;
}

int lt_nvs_init(void)
{
    if (s_ready) return 0;
    if (nvs_open(NS_SYS, NVS_READWRITE, &s_h_sys) != ESP_OK) return -1;
    if (nvs_open(NS_ERR, NVS_READWRITE, &s_h_err) != ESP_OK) return -1;
    if (nvs_open(NS_CFG, NVS_READWRITE, &s_h_cfg) != ESP_OK) return -1;

    if (nvs_get_u32(s_h_sys, K_BOOT, &s_boot_cnt) != ESP_OK) s_boot_cnt = 0;
    if (load_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) != 0) memset(&s_counters, 0, sizeof(s_counters));
    if (load_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) != 0) memset(&s_ring, 0, sizeof(s_ring));
    if (load_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) != 0) memset(s_crash, 0, sizeof(s_crash));

    s_counters_last_us = esp_timer_get_time();
    s_ready = true;
    return 0;
}

uint32_t lt_nvs_boot_inc(void)
{
    s_boot_cnt++;
    if (nvs_set_u32(s_h_sys, K_BOOT, s_boot_cnt) == ESP_OK) nvs_commit(s_h_sys);
    return s_boot_cnt;
}

uint32_t lt_nvs_boot_get(void) { return s_boot_cnt; }

static void persist_counters(void)
{
    if (nvs_set_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) == ESP_OK) nvs_commit(s_h_err);
    s_counters_dirty = false;
    s_counters_last_us = esp_timer_get_time();
}

void lt_counters_inc(lt_counter_id_t id, bool persist)
{
    ((uint32_t *)&s_counters)[id]++;   /* lt_counters_t is 9 contiguous u32 in enum order */
    s_counters_dirty = true;
    if (persist) persist_counters();
}

int lt_counters_flush(bool force)
{
    if (!s_counters_dirty) return 0;
    if (force || esp_timer_get_time() - s_counters_last_us >= COUNTER_FLUSH_US) persist_counters();
    return 0;
}

const lt_counters_t *lt_counters(void) { return &s_counters; }

int errlog_add(uint16_t code, uint32_t arg)
{
    err_entry_t *e = &s_ring.entry[s_ring.head];
    e->code = code;
    e->uptime_s = uptime_s_now();
    e->boot = (uint16_t)s_boot_cnt;
    e->arg = arg;
    s_ring.head = (uint8_t)((s_ring.head + 1) % ERR_RING_LEN);
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) == ESP_OK) nvs_commit(s_h_err);
    ESP_LOGW(TAG, "errlog 0x%04x arg=%u", code, (unsigned)arg);
    return 0;
}

void lt_crashlog_push(uint8_t reset_reason, uint32_t prev_uptime_s)
{
    s_crash[2] = s_crash[1];
    s_crash[1] = s_crash[0];
    s_crash[0].reset_reason = reset_reason;
    s_crash[0].uptime_s = prev_uptime_s;
    if (nvs_set_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) == ESP_OK) nvs_commit(s_h_sys);
}

bool lt_reset_is_abnormal(int r)
{
    switch (r) {
    case ESP_RST_PANIC:
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

bool lt_crashlog_is_loop(void)
{
    for (int i = 0; i < CRASH_LOG_LEN; i++) {
        if (!lt_reset_is_abnormal(s_crash[i].reset_reason)) return false;
        if (s_crash[i].uptime_s >= 60) return false;      /* uptime < 60 s each (§17.5) */
    }
    return true;
}

uint32_t lt_safe_until_get(void)
{
    uint32_t v = 0;
    nvs_get_u32(s_h_sys, K_SAFE, &v);
    return v;
}

int lt_safe_until_set(uint32_t boot_cnt)
{
    if (nvs_set_u32(s_h_sys, K_SAFE, boot_cnt) != ESP_OK) return -1;
    nvs_commit(s_h_sys);
    return 0;
}

const char *lt_reset_reason_str(int r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-WDT";
    case ESP_RST_TASK_WDT:  return "task-WDT";
    case ESP_RST_WDT:       return "other-WDT";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
    }
}

void lt_boot_record_reset(int reset_reason, uint32_t prev_uptime_s)
{
    /* §4.7 step 1 / §17.5: shift the crash log every boot; count + log abnormal resets on the
     * crash path (persist immediately -- these must survive the next reset). */
    lt_crashlog_push((uint8_t)reset_reason, prev_uptime_s);
    switch (reset_reason) {
    case ESP_RST_PANIC:
        lt_counters_inc(LT_CTR_CRASHES, true);
        errlog_add(E_SYS_PANIC, 0);
        break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        lt_counters_inc(LT_CTR_WDT, true);
        errlog_add(E_SYS_WDT_RESET, (uint32_t)reset_reason);
        break;
    case ESP_RST_BROWNOUT:
        lt_counters_inc(LT_CTR_BROWNOUT, true);
        errlog_add(E_SYS_BROWNOUT, 0);
        break;
    default:
        break;
    }
}

/* ---- cfg blob (lt_cfg/cfg): packed cfg_t (leading version, §15.1) + trailing CRC16 (§15.2) ---- */
int lt_cfg_load(cfg_t *c)
{
    uint8_t buf[sizeof(cfg_t) + 2];
    size_t sz = 0;
    if (nvs_get_blob(s_h_cfg, K_CFG, NULL, &sz) != ESP_OK || sz != sizeof(buf)) return -1;
    if (nvs_get_blob(s_h_cfg, K_CFG, buf, &sz) != ESP_OK) return -1;

    uint16_t want = ses_crc16(buf, sizeof(cfg_t));
    uint16_t got  = (uint16_t)(buf[sizeof(cfg_t)] | (buf[sizeof(cfg_t) + 1] << 8));
    if (want != got) return -1;
    if (buf[0] != CFG_VERSION) return -1;     /* leading version byte == cfg_t.version */

    memcpy(c, buf, sizeof(cfg_t));
    return cfg_validate(c);                   /* >=0 corrections; stored user settings win (§15.1) */
}

int lt_cfg_save(const cfg_t *c)
{
    uint8_t buf[sizeof(cfg_t) + 2];
    memcpy(buf, c, sizeof(cfg_t));
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFF);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);
    if (nvs_set_blob(s_h_cfg, K_CFG, buf, sizeof(buf)) != ESP_OK) return -1;
    nvs_commit(s_h_cfg);
    return 0;
}
```

- [ ] **Step 2: `components/app/sys/lt_rtc.c`**

```c
/* lt_rtc.c -- RTC-memory state (spec §15.3) + crash-loop uptime tracker.
 *
 * rtc_state_t lives in RTC slow memory so it survives deep sleep (and, on this SoC, WDT/panic
 * resets). 3.2 defines and validates it (§4.7 step 4) but does not resume from it -- resume is
 * 3.5. The separate uptime cell lets the boot-time crash-loop check (§17.5) know how long the
 * previous boot ran, which plain esp_timer cannot report (it restarts at every reset).
 */
#include "app/lt_rtc.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_rom_crc.h"

static RTC_DATA_ATTR rtc_state_t s_rtc;

#define UPTIME_MAGIC 0x5054494Du   /* 'PTIM' */
static RTC_DATA_ATTR uint32_t s_uptime_magic;
static RTC_DATA_ATTR uint32_t s_uptime_s;

static uint32_t rtc_crc(const rtc_state_t *s)
{
    /* CRC32 over all bytes except the trailing crc32 field (§15.3). */
    return esp_rom_crc32_le(0, (const uint8_t *)s, sizeof(*s) - sizeof(s->crc32));
}

rtc_validity_t lt_rtc_validate(rtc_state_t *out)
{
    if (s_rtc.magic != RTC_STATE_MAGIC) return RTC_ABSENT;   /* cold boot / cleared */
    if (s_rtc.version != RTC_STATE_VERSION) return RTC_INVALID;
    if (s_rtc.crc32 != rtc_crc(&s_rtc)) return RTC_INVALID;
    if (out) memcpy(out, &s_rtc, sizeof(*out));
    return RTC_VALID;
}

void lt_rtc_clear(void)
{
    memset(&s_rtc, 0, sizeof(s_rtc));   /* magic cleared -> ABSENT next validate */
}

uint32_t lt_rtc_uptime_prev_s(void)
{
    return (s_uptime_magic == UPTIME_MAGIC) ? s_uptime_s : 0;
}

void lt_rtc_uptime_update_s(uint32_t uptime_s)
{
    s_uptime_magic = UPTIME_MAGIC;
    s_uptime_s = uptime_s;
}
```

> **Plan block drift (applied during Task 3 execution):** the steps-1–5 body below calls `sup_install_assert_hook()` (step 2, "core asserts -> error ring"), but that symbol's only implementation is Task 4's `components/app/supervisor/sup_errlog.c`, which is out of Task 3's file list. Transcribed as drafted, `main/app_main.c` fails to *link* standalone (all symbols from `lt_nvs.c`/`lt_rtc.c` resolve; only `sup_install_assert_hook` is undefined) — consistent with this block's own "(verified in the integrated build below)" caveat on the verification line, but the orchestrator's Task 3 gate requires a standalone `./build.sh moto_neo6m build` to succeed. The minimal fix actually committed: `main/app_main.c` gains a temporary `assert_hook_shim` + `sup_install_assert_hook` definition (identical in behavior to Task 4's planned `sup_errlog.c`: `core_set_assert_hook` wired to log into the NVS error ring) placed directly in `app_main.c`, clearly commented as a Task-3-only shim. This does not collide with Task 4: Task 4's own Step 4 replaces `main/app_main.c` wholesale (it is a full rewrite, not a diff), so the shim is superseded rather than duplicated once `sup_errlog.c` lands — Task 4 must not also define `sup_install_assert_hook` anywhere the shim still exists. The `app_main.c` block below is left as originally drafted for the record; the repo state after Task 3 differs from it only by that shim (and the closing idle loop already called out above).

- [ ] **Step 3: `main/app_main.c` boot steps 1–5.** The boot sequence's reset-reason/NVS/crash-loop/RTC/config portion. (T4 appends step 6 board bring-up and steps 10–11 + console; the final `app_main.c` is shown in T4. For a standalone T3 gate, end `app_main` after step 5 with a temporary `for(;;) vTaskDelay(...)` idle.) The steps-1–5 body is:

```c
    int64_t t_boot = esp_timer_get_time();

    /* §4.7 step 1: reset reason (crash counters recorded below, after NVS is up). */
    esp_reset_reason_t reason = esp_reset_reason();

    /* §4.7 step 8 banner (kept from 3.1). */
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", lt_reset_reason_str((int)reason), (int)reason);

    /* §4.7 step 2: NVS init, erase + re-init on a version/space fault (log E_SYS_CFG_RESET). */
    esp_err_t nerr = nvs_flash_init();
    bool nvs_erased = false;
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
        nvs_erased = true;
    } else {
        ESP_ERROR_CHECK(nerr);
    }
    if (lt_nvs_init() != 0) ESP_LOGE(TAG, "lt_nvs_init failed");
    sup_install_assert_hook();                 /* core asserts -> error ring (§17.9) */
    if (nvs_erased) errlog_add(E_SYS_CFG_RESET, 0);

    /* §4.7 step 1 (cont): count + log + crash-log the reset reason. */
    uint32_t prev_uptime_s = lt_rtc_uptime_prev_s();
    lt_boot_record_reset((int)reason, prev_uptime_s);

    /* §4.7 step 3: boot counter + crash-loop check (§17.5); 3.2 detects + flags only. */
    uint32_t boot_cnt = lt_nvs_boot_inc();
    lt_counters_inc(LT_CTR_BOOTS, false);
    bool safe = false;
    if (lt_crashlog_is_loop()) {
        lt_safe_until_set(boot_cnt + 1);
        safe = true;
    } else if (boot_cnt <= lt_safe_until_get()) {
        safe = true;
    }
    if (safe) { sys_flags_set(SYS_SAFE_MODE); errlog_add(E_SYS_SAFE_MODE, boot_cnt); }

    /* §4.7 step 4: RTC memory validate/clear (full resume is 3.5). */
    switch (lt_rtc_validate(NULL)) {
    case RTC_INVALID: errlog_add(E_SYS_RTC_INVALID, 0); lt_rtc_clear(); break;
    case RTC_ABSENT:  lt_rtc_clear(); break;
    case RTC_VALID:   break;
    }

    /* §4.7 step 5: config load (defaults -> profile -> NVS blob; stored settings win). */
    static cfg_t cfg;
    char ble_name[16];
    cfg_profile_t prof;
    cfg_defaults(&cfg);
    make_profile(&prof, ble_name, sizeof(ble_name));   /* MAC-derived BLE name */
    cfg_apply_profile(&cfg, &prof);
    int corr = lt_cfg_load(&cfg);
    if (corr < 0) { lt_cfg_save(&cfg); errlog_add(E_SYS_CFG_RESET, 0); }
    else if (corr > 0) { errlog_add(E_SYS_CFG_RESET, (uint32_t)corr); lt_cfg_save(&cfg); }
```

**Task 3 verification:** `lt_nvs.c` + `lt_rtc.c` compile clean under `-Werror`; blob sizes are the §15.2 values (12/385/15 B, packed). Boot steps 1–5 build and link. **As executed (see drift note above), `main/app_main.c` also carries a temporary `assert_hook_shim` so the standalone Task 3 tree links**: `./build.sh moto_neo6m build` succeeds (app image `0x3b240` B), `./build.sh moto_neo6m size` passes (`242240 B` within the `1245184 B` limit), `./build.sh moto_sim build` succeeds (same `0x3b240` B image). The only warning from new code is the expected `t_boot` unused-variable warning in `main/app_main.c` (that variable is consumed by Task 4's boot-complete log line; `main` is not built with `-Werror`). **Hardware-only checks:** the crash counters advancing across a real panic/WDT/brownout, and RTC-cell survival across those resets.

Commit block:
```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

### Task 4: supervisor + task WDT + boot steps 6/10/11 + minimal `dbg status`

**Files:**
- Create: `components/app/supervisor/sup.c`, `components/app/supervisor/sup_errlog.c`, `components/app/sys/dbg_console.c`
- Edit: `main/app_main.c` (boot step 6 board bring-up, steps 10–11, start the console) — the final `app_main.c` is shown here

**Interfaces:**
- Consumes: `esp_task_wdt` (§17.1), `esp_timer`, FreeRTOS static task/queue, the NVS layer + RTC tracker (T3), `hal/board.h` (T2), `esp_console` + `linenoise`.
- Produces: the supervisor task (§4.3), the task-WDT subscription, the `hb[]`/`sys_flags` wiring, the static `btn_q`, and the `dbg status` console — the 3.2 roadmap exit command.

**Rulings:**
1. **`dbg status` is built on `esp_console` + `linenoise` (IDF), not a hand-rolled UART reader** — it dovetails with the full §18.4 console (3.5), which extends the same REPL. A single `dbg` command is registered; `dbg status` prints boot count, reset reason (string), uptime, crash counters, `sys_flags`, and every `hb[]`. Line editing is disabled (`linenoiseSetDumbMode(1)`, per §18.4).
2. **Task-WDT subscription (§17.1):** the TWDT is auto-initialised by startup (`CONFIG_ESP_TASK_WDT_INIT`, timeout 5 s, panic on timeout — already in sdkconfig) and both idle tasks are subscribed (`CHECK_IDLE_TASK_CPU0/1=y`). The **supervisor** subscribes itself (`esp_task_wdt_add(NULL)`) and resets each 1 s loop. `app_main` **returns** after setup (its task is deleted, freeing its stack); the console task blocks on UART and is **not** WDT-subscribed (the core-0 idle task feeds the WDT while it blocks). Pipeline/logger/ui/power subscribe as they land (3.4).
3. **`hb[6]` + registration model:** `g_hb[]` has one slot per §4.3 task (`HB_PIPELINE..HB_POWER`). In 3.2 only the supervisor is live; `sup_register_task(hb_id, task, stall_s)` structures the §17.2 stall watch so later tasks register with their `HB_STALL_S`. A stalled `HB_PIPELINE` triggers `esp_restart()` (RTC-snapshot save on restart lands in 3.5); it cannot fire in 3.2 (no pipeline registered).
4. **Supervisor loop (§17.2 reduced):** heartbeat-stall watch over registered tasks, `esp_task_wdt_reset()`, `lt_rtc_uptime_update_s()` (the crash-loop tracker), `lt_counters_flush(false)` (batched ≥ 60 s), `g_hb[HB_SUPERVISOR]++`, 1000 ms delay. GPS/IMU/storage ladders and heap/stack/temp/OTA checks are stubbed with a pointer to 3.3/3.4. Static task (core 0, prio 22, stack 3072 B via `xTaskCreateStaticPinnedToCore`).
5. **Boot step 6 (partial):** `board_init()` + `board_gps_power(true)` run here (the board driver exists, and `dbg status` needs the battery path up). Steps 7–9 (storage/display/self-test) and 12–15 are later sessions.

**Hardware-only checks (orchestrator confirms after flashing):**
- `dbg status` on the console prints boot count, reset reason, uptime, counters, `sys_flags`, heartbeats (the roadmap exit criterion); `hb[1]` (supervisor) increments ~1/s between two `dbg status` calls.
- The task WDT does not trip at boot (idle + supervisor feed it); no unexpected reset loop.

- [ ] **Step 1: `components/app/supervisor/sup.c`**

```c
/* sup.c -- supervisor task (spec §4.3: core 0, prio 22, stack 3072, 1000 ms; loop §17.2).
 *
 * 3.2 runs the reduced §17.2 loop: heartbeat-stall watch over the registered tasks, task-WDT
 * reset each loop, the crash-loop uptime tick, and the batched counter flush. The GPS/IMU/
 * storage ladders and heap/stack/temperature/OTA checks are stubbed until their subsystems
 * land (3.3/3.4). Only the supervisor itself is live now; other tasks call sup_register_task
 * as they are created.
 */
#include "app/lt_sup.h"
#include "app/lt_nvs.h"
#include "app/lt_err.h"
#include "app/lt_rtc.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "sup";

#define SUP_PERIOD_MS   1000
#define SUP_PRIO        22
#define SUP_CORE        0
#define SUP_STACK_BYTES 3072
#define SUP_STACK_WORDS (SUP_STACK_BYTES / sizeof(StackType_t))

typedef struct {
    TaskHandle_t task;
    uint8_t      hb_id;
    uint32_t     stall_s;
    uint32_t     last_hb;
    uint32_t     stalled_s;
    bool         used;
} watch_t;

static watch_t      s_watch[HB_COUNT];
static StaticTask_t s_tcb;
static StackType_t  s_stack[SUP_STACK_WORDS];

int sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s)
{
    if (hb_id >= HB_COUNT) return -1;
    s_watch[hb_id] = (watch_t){ .task = task, .hb_id = hb_id, .stall_s = stall_s,
                                .last_hb = g_hb[hb_id], .stalled_s = 0, .used = true };
    return 0;
}

static void check_stalls(void)
{
    for (int i = 0; i < HB_COUNT; i++) {
        watch_t *w = &s_watch[i];
        if (!w->used || w->stall_s == 0) continue;
        uint32_t cur = g_hb[w->hb_id];
        if (cur != w->last_hb) {                 /* progressing */
            w->last_hb = cur;
            w->stalled_s = 0;
            continue;
        }
        w->stalled_s += SUP_PERIOD_MS / 1000;
        if (w->stalled_s >= w->stall_s) {
            ESP_LOGE(TAG, "task %d stalled %us", w->hb_id, (unsigned)w->stalled_s);
            errlog_add(E_SYS_TASK_STALL, w->hb_id);
            w->stalled_s = 0;
            if (w->hb_id == HB_PIPELINE) {
                /* §17.2: a stalled pipeline restarts (RTC snapshot save lands in 3.5). */
                lt_counters_flush(true);
                esp_restart();
            }
        }
    }
}

static void sup_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);                       /* §17.1: supervisor subscribes to the task WDT */
    sup_register_task(HB_SUPERVISOR, xTaskGetCurrentTaskHandle(), 0);   /* self: WDT covers a stuck sup */
    ESP_LOGI(TAG, "supervisor up (core %d prio %d)", SUP_CORE, SUP_PRIO);

    for (;;) {
        check_stalls();
        esp_task_wdt_reset();
        lt_rtc_uptime_update_s((uint32_t)(esp_timer_get_time() / 1000000));   /* crash-loop tracker */
        lt_counters_flush(false);                 /* persist if dirty and >=60 s (§15.2) */

        /* Ladders (GPS §7.5 / IMU §8.7 / storage §13) and heap/stack/temp/OTA checks: their
         * subsystems arrive in 3.3/3.4; wired here then. */

        g_hb[HB_SUPERVISOR]++;
        vTaskDelay(pdMS_TO_TICKS(SUP_PERIOD_MS));
    }
}

void sup_start(void)
{
    xTaskCreateStaticPinnedToCore(sup_task, "sup", SUP_STACK_WORDS, NULL, SUP_PRIO,
                                  s_stack, &s_tcb, SUP_CORE);
}
```

- [ ] **Step 2: `components/app/supervisor/sup_errlog.c`**

```c
/* sup_errlog.c -- error-logging glue for the supervisor (spec §17.9).
 *
 * Installs the core assertion hook so a failing CORE_ASSERT_* in components/core (which never
 * aborts on target) is recorded into the NVS error ring (§15.2/§17.7). Host tests install their
 * own recording hook; the default hook is silent.
 */
#include "app/lt_nvs.h"

#include "core/core.h"
#include "esp_log.h"

static const char *TAG = "assert";

static void app_assert_hook(uint16_t code, const char *file, int line)
{
    ESP_LOGE(TAG, "core assert 0x%04x at %s:%d", code, file ? file : "?", line);
    errlog_add(code, (uint32_t)line);
}

void sup_install_assert_hook(void)
{
    core_set_assert_hook(app_assert_hook);
}
```

- [ ] **Step 3: `components/app/sys/dbg_console.c`**

```c
/* dbg_console.c -- minimal diagnostics console for 3.2 (spec §18.4, exit-criterion command).
 *
 * IDF esp_console REPL on UART0 with one registered command, `dbg status`. The full §18.4
 * command set (status/list/open/... and the other dbg verbs) extends this same REPL in 3.5.
 */
#include "app/dbg_console.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"

static int s_reset_reason;

static int cmd_dbg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        const lt_counters_t *c = lt_counters();
        long long up = esp_timer_get_time() / 1000000;
        printf("boot count : %u\n", (unsigned)lt_nvs_boot_get());
        printf("reset      : %s (%d)\n", lt_reset_reason_str(s_reset_reason), s_reset_reason);
        printf("uptime     : %lld s\n", up);
        printf("counters   : boots=%u crashes=%u wdt=%u brownout=%u\n",
               (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt, (unsigned)c->brownout);
        printf("sys_flags  : 0x%08x%s\n", (unsigned)sys_flags_get(),
               (sys_flags_get() & (1u << SYS_SAFE_MODE)) ? " [SAFE_MODE]" : "");
        for (int i = 0; i < HB_COUNT; i++) {
            printf("hb[%d]      : %u\n", i, (unsigned)g_hb[i]);
        }
        return 0;
    }
    printf("usage: dbg status\n");
    return 1;
}

void dbg_console_start(int reset_reason)
{
    s_reset_reason = reset_reason;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "laptimer>";
    repl_cfg.task_priority = 2;
    repl_cfg.task_stack_size = 4096;
    repl_cfg.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;

    linenoiseSetDumbMode(1);   /* §18.4: line editing disabled (plain serial terminal) */

    const esp_console_cmd_t cmd = {
        .command = "dbg",
        .help = "diagnostics; 'dbg status' prints boot/reset/uptime/counters/flags/heartbeats",
        .hint = NULL,
        .func = cmd_dbg,
    };
    esp_console_cmd_register(&cmd);
    esp_console_start_repl(repl);
}
```

- [ ] **Step 4: final `main/app_main.c`** (steps 1–5 from T3 + step 6 + steps 10–11 + console).

```c
/* app_main.c -- LapTimer firmware entry point and boot sequence (§4.7).
 *
 * Session 3.2 implements boot steps 1-5 (reset reason + crash counters, NVS init, boot
 * counter + crash-loop check, RTC-state validate, config load), step 6 in part (board
 * bring-up + GPS power, needed for the battery reading in `dbg status`), and steps 10-11
 * (task WDT + supervisor, hb[]/sys_flags, static queues). Steps 7-9 (storage, display,
 * self-test) and 12-15 (pipeline/logger/ui/power/conn/OTA) land in 3.3-3.5.
 */
#include "build_config.h"

#include <stdio.h>

#include "core/cfg.h"
#include "core/core.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "hal/board.h"

#include "app/dbg_console.h"
#include "app/lt_err.h"
#include "app/lt_nvs.h"
#include "app/lt_rtc.h"
#include "app/lt_sup.h"

static const char *TAG = "laptimer";

/* Build the profile the app applies after cfg_defaults and before the NVS blob (§15.1), so
 * stored user settings always win. BLE name is MAC-derived ("LapTimer-XXXX"). */
static void make_profile(cfg_profile_t *p, char *name, size_t name_cap)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, name_cap, "LapTimer-%02X%02X", mac[4], mac[5]);
    p->display_live_clock = (CFG_VARIANT_CAR != 0);   /* profile sets true for the OLED (car) */
    p->log_fused_hz = CFG_FUSED_LOG_HZ;
    p->ble_name = name;
}

void app_main(void)
{
    int64_t t_boot = esp_timer_get_time();

    /* §4.7 step 1: reset reason (crash counters recorded below, after NVS is up). */
    esp_reset_reason_t reason = esp_reset_reason();

    /* §4.7 step 8 banner (kept from 3.1). */
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", lt_reset_reason_str((int)reason), (int)reason);

    /* §4.7 step 2: NVS init, erase + re-init on a version/space fault (log E_SYS_CFG_RESET). */
    esp_err_t nerr = nvs_flash_init();
    bool nvs_erased = false;
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
        nvs_erased = true;
    } else {
        ESP_ERROR_CHECK(nerr);
    }
    if (lt_nvs_init() != 0) ESP_LOGE(TAG, "lt_nvs_init failed");
    sup_install_assert_hook();                 /* core asserts -> error ring (§17.9) */
    if (nvs_erased) errlog_add(E_SYS_CFG_RESET, 0);

    /* §4.7 step 1 (cont): count + log + crash-log the reset reason; prev-boot uptime comes
     * from the RTC uptime cell the supervisor maintained last boot. */
    uint32_t prev_uptime_s = lt_rtc_uptime_prev_s();
    lt_boot_record_reset((int)reason, prev_uptime_s);

    /* §4.7 step 3: boot counter + crash-loop check (§17.5). 3.2 only detects + flags safe
     * mode; the safe-mode behaviour tree is 3.5. */
    uint32_t boot_cnt = lt_nvs_boot_inc();
    lt_counters_inc(LT_CTR_BOOTS, false);      /* batched with the rest */
    bool safe = false;
    if (lt_crashlog_is_loop()) {
        lt_safe_until_set(boot_cnt + 1);
        safe = true;
        ESP_LOGE(TAG, "crash loop: 3 abnormal resets < 60 s -> SAFE MODE");
    } else if (boot_cnt <= lt_safe_until_get()) {
        safe = true;                           /* still inside a prior safe-mode window */
    }
    if (safe) {
        sys_flags_set(SYS_SAFE_MODE);
        errlog_add(E_SYS_SAFE_MODE, boot_cnt);
    }

    /* §4.7 step 4: RTC memory validate/clear. Full resume is 3.5; here invalid/absent are
     * cleared and only a present-but-bad snapshot logs E_SYS_RTC_INVALID. */
    switch (lt_rtc_validate(NULL)) {
    case RTC_INVALID:
        errlog_add(E_SYS_RTC_INVALID, 0);
        lt_rtc_clear();
        break;
    case RTC_ABSENT:
        lt_rtc_clear();
        break;
    case RTC_VALID:
        break;                                 /* left in place; 3.5 resumes from it */
    }

    /* §4.7 step 5: config load. defaults -> profile -> NVS blob (stored settings win, §15.1);
    *  defaults + a fresh save on absent/corrupt; log corrections. */
    static cfg_t cfg;
    char ble_name[16];
    cfg_profile_t prof;
    cfg_defaults(&cfg);
    make_profile(&prof, ble_name, sizeof(ble_name));
    cfg_apply_profile(&cfg, &prof);
    int corr = lt_cfg_load(&cfg);
    if (corr < 0) {
        lt_cfg_save(&cfg);                     /* no valid blob -> persist the profile defaults */
        errlog_add(E_SYS_CFG_RESET, 0);
    } else if (corr > 0) {
        errlog_add(E_SYS_CFG_RESET, (uint32_t)corr);
        lt_cfg_save(&cfg);                     /* persist the clamped config */
    }

    /* §4.7 step 6 (partial): board bring-up + GPS power on (battery read feeds `dbg status`). */
    board_init();
    board_gps_power(true);

    /* §4.7 step 10: the task WDT is already enabled via sdkconfig; hb[]/sys_flags exist
     * (lt_sys). Start the supervisor first -- it subscribes itself to the task WDT. */
    sup_start();

    /* §4.7 step 11: static queues the supervisor + button ISR need (btn_q). The pipeline
     * rings arrive in 3.4. */
    lt_queues_init();

    /* Minimal diagnostics console -- the 3.2 exit criterion (`dbg status`). Replaced by the
     * full §18.4 console in 3.5. */
    dbg_console_start((int)reason);

    ESP_LOGI(TAG, "boot #%u complete in %lld ms (safe_mode=%d)", (unsigned)boot_cnt,
             (long long)((esp_timer_get_time() - t_boot) / 1000), (int)safe);

    /* The main task returns: the supervisor and console tasks run on, and the idle tasks on
     * both cores feed the task WDT. Pipeline/logger/ui/power start here in 3.4. */
}
```

- [ ] **Step 5: build, size, and the other envs.** From the scratch worktree:

```bash
source tools/idf-env.sh
./build.sh moto_neo6m build      # -> build/moto_neo6m/laptimer.bin
./build.sh moto_neo6m size       # app image <= 0x130000 B
./build.sh moto_sim   build
```

**Verified results (ESP-IDF v5.3.2, target esp32, scratch worktree at `main` = `edc0010`):**
- `./build.sh moto_neo6m build` — success; `check_sizes.py`: `laptimer.bin binary size 0x4fef0 bytes. Smallest app partition is 0x140000 bytes. 0xf0110 bytes (75%) free.`
- `./build.sh moto_neo6m size` — **PASS**: `OK: app image 327408 B within the 1245184 B (0x130000) limit (§19.1)` (`idf.py size` Total image size 327,276 B).
- `./build.sh moto_sim build` — success; app image **327,408 B** (size-clean).
- Bonus `./build.sh moto_m10 build` (exercises `#if CFG_HAS_PPS` in `board.c`) — success; **327,440 B**.
- **No warnings from new code.** The only build warnings are pre-existing: `components/core`'s vendored `jsmn.h` sign-conversion (relaxed per §17.9) and the expected `LITTLEFS_MAX_PARTITIONS` unknown-kconfig warning (littlefs lands 3.3, documented in 3.1). Every new source (`board.c`, `lt_*.c`, `sup*.c`, `dbg_console.c`, `app_main.c`) compiles clean under `-Werror`.
- `strings` on the image shows the `dbg`/`dbg status`/`laptimer>` command, the `supervisor up` and `board_devkit_v1 init` log lines, and the retained `LapTimer %s (%s)` banner.
- Draft sources are LF, final-newline, no trailing whitespace, no conflict markers.

**App image size for the plan record: `moto_neo6m` 327,408 B (0x4fef0), `moto_sim` 327,408 B, `moto_m10` 327,440 B — ~26 % of the 0x130000 gate, ~25 % of the 0x140000 OTA slot. The ~109 KB over 3.1's 218 KB is esp_console/linenoise + nvs + adc_cali + i2c_master/spi + the supervisor.**

- [ ] **Step 6: flash + `dbg status` on the board — only after the user says "ready".** Roadmap exit: `dbg status` shows boot count, reset reason, heartbeats. Flashing waits for the user's explicit "ready" (they hold BOOT, disconnect any battery pack). When ready: `./build.sh moto_neo6m flash-monitor --port <dev> --yes`, then type `dbg status`.

- [ ] **Step 7: commit the session and tag `p03-d2`.** Single session commit on branch `s3.2-board-boot` once the tree builds green; PR, wait for the required checks, squash-merge, tag `p03-d2`.

Commit block:
```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

## Session 3.3 — storage_internal (LittleFS) and the logger task

Roadmap exit: a power-cut during logging leaves `.sum` intact and `.log` decodable; tag `p03-d3`. Serial (3 tasks). LittleFS via `joltwallet/littlefs` (pinned 1.16.5, the ecosystem-standard component — IDF has no built-in LittleFS; `dependencies.lock` committed, `managed_components/` gitignored). Rulings: (1) §13.1 geometry via `CONFIG_LITTLEFS_*` (block_size is hard-coded 4096 in the component); (2) HAL paths are backend-relative, the driver prepends `/lfs`; (3) the driver is app-agnostic (`sto_mount` 0/1/-N), boot step 7 owns SYS_STORAGE_DEAD; (4) **evt_q→record fidelity: `event_t` lacks the full `lap_result_t`/sectors, so 3.3 logs generic EVENT records + a minimal LAP/DRAG_RUN for the `.sum`; sector/stats-populated LAP records and the real VENUE record need the pipeline's engine structs — deferred to session 3.4** (the pipeline must hand the full `lap_result_t`/`drag_result_t` to the logger, per §10.4 step 8, not just the `event_t`); (5) ring-notify = FreeRTOS task notification + 1000 ms timeout (fully static, §17.9). `components/core` stays pure C11.

### Task 1: `storage_internal` (LittleFS) + `hal/storage.h` + mount at boot


**Files:**
- Create: `components/lt_hal/include/hal/storage.h`, `components/drivers/storage_internal/{CMakeLists.txt, idf_component.yml, storage_internal.c}`
- Edit: top-level `CMakeLists.txt` (append the 3.3 storage dir to `EXTRA_COMPONENT_DIRS` + its comment), `main/CMakeLists.txt` (REQUIRES `storage_internal`), `components/app/include/app/lt_err.h` (the `E_STO_*` subset), `sdkconfig.defaults` (the §13.1 LittleFS geometry), `.gitignore` (commit `dependencies.lock`), `main/app_main.c` (boot step 7: mount + probe)

**Interfaces:**
- Produces: the `hal/storage.h` contract (§5.1) the logger (T2) and the 3.5 cmd console call; the concrete `sto_*` implementation over LittleFS that `main` links; a committed `dependencies.lock`.
- Consumes: `joltwallet/littlefs` (managed component, pinned); `esp_littlefs` + POSIX VFS; `components/app` shared state only from `main` (the driver itself is app-agnostic).

**Rulings (spec is the authority; the ESP-IDF v5.3.2 / esp_littlefs reality is noted):**
1. **`joltwallet/littlefs` is pulled via the IDF component manager, pinned `==1.16.5`.** LittleFS is not in ESP-IDF core; joltwallet/littlefs is the ecosystem-standard component, so an `idf_component.yml` (in `storage_internal`) requiring a PINNED version is *using the standard*, not reinventing it (consistent with the "prefer IDF/standard components" directive). The resolved tree is captured in the repo-root `dependencies.lock`, which is **committed** (reproducible builds); the fetched `managed_components/` is **gitignored**. The manager fetches on first configure (allowed).
2. **The §13.1 geometry lives in `sdkconfig.defaults`, not the conf struct.** esp_littlefs builds its `lfs_config` from `CONFIG_LITTLEFS_*` Kconfig, not the `esp_vfs_littlefs_conf_t` struct (verified in the fetched `src/esp_littlefs.c`): `read_size←CONFIG_LITTLEFS_READ_SIZE`, `prog_size←CONFIG_LITTLEFS_WRITE_SIZE`, `cache_size←…CACHE_SIZE`, `lookahead_size←…LOOKAHEAD_SIZE`, `block_cycles←…BLOCK_CYCLES`; **`block_size` is hard-`#define`d 4096** in the component ("ESP32 can only operate at 4kb") and `block_count` is auto-derived from the partition. So the §13.1 values (read/prog 128, cache 512, lookahead 128, block_cycles 512, block 4096) are set through `sdkconfig.defaults`; only `format_if_mount_failed=false` is a struct field (the ladder decides). These CONFIG_ defaults already match §13.1, but they are pinned explicitly so the spec, not a component default, is authoritative.
3. **HAL paths are backend-relative; the driver maps them onto its mount point.** §5.1 says paths are `/sessions/<id>.log` etc. and §13.1 mounts at `/lfs` — the driver prepends `/lfs`, so app code (`/sessions/…`) is backend-agnostic and `storage_sd` will resolve the same paths on the card (§5.1 SD note). Mount base is a single `#define LFS_BASE "/lfs"`.
4. **The driver stays app-agnostic; the boot sequence owns the ladder's side-effects.** `storage_internal` depends only on `lt_hal` + `joltwallet__littlefs` (no `components/app`), exactly like `board_devkit_v1`. `sto_mount` runs the §13.1 ladder and reports the result: **`0`** clean mount, **`1`** mounted only after a reformat, **`<0`** dead. Boot step 7 (in `app_main`) then owns `SYS_STORAGE_DEAD`, the `E_STO_MOUNT`/`E_STO_FORMAT` ring entries, and the `LT_CTR_STO_FORMAT` counter. `main` REQUIRES `storage_internal` so the plain `sto_*` symbols link app-wide (the §4.1 swappable interface-name registration is a 3.4 concern, per draft32 T1 ruling 5).
5. **`STO_*` open-flag values are defined in `storage.h`.** §5.1 names the flags (`STO_RD`, `STO_WR|STO_APPEND|STO_CREATE`) but leaves their values to the header; they are stable HAL-abstract bits the driver maps to POSIX `open()`. `STO_CREATE` without `STO_APPEND` adds `O_TRUNC` (the `.sum.tmp` rewrite path starts fresh).
6. **`E_STO_*` subset added to `lt_err.h` now** (`E_STO_WRITE/MOUNT/FORMAT/FULL/EVICT`, §17.7); the full code table still lands with the 3.5 console (draft32 pattern). `E_STO_SD_DEGRADED` is SD-only, deferred.


- [ ] **Step 1: `components/lt_hal/include/hal/storage.h`** (§5.1 verbatim + guard/includes + `STO_*`).

```c
/* hal/storage.h -- storage HAL contract (spec §5.1, called from logger and cmd).
 *
 * The declarations below are the §5.1 storage.h slice verbatim; the include guard, the
 * <stdint.h>/<stddef.h> includes (size_t, fixed-width types) and the STO_* open-flag values
 * are added here so this is a self-contained, compilable header. The §5.1 excerpt names the
 * flags (STO_RD, STO_WR|STO_APPEND|STO_CREATE) but leaves their values to the header; they are
 * stable HAL-abstract bits the driver maps onto POSIX open() flags. All functions return int
 * (0 = OK, negative = -errno-style) unless noted; each is called from one task only (logger,
 * plus cmd for read-only listing/export in 3.5) and is not reentrant. The concrete backend is
 * components/drivers/storage_${STORAGE} (storage_internal = LittleFS, §13.1).
 *
 * Paths are backend-relative: "/sessions/<id>.log", "/sessions/<id>.sum", "/tracks/user.bin".
 * The driver maps them onto its mount point (storage_internal -> /lfs/...). Callers never embed
 * the mount base, so app code is backend-agnostic (storage_sd resolves the same paths on SD).
 */
#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stddef.h>
#include <stdint.h>

/* open() flag bits for sto_open (HAL-abstract; the driver maps them to O_* flags). */
enum {
    STO_RD     = 0x01,   /* read */
    STO_WR     = 0x02,   /* write */
    STO_APPEND = 0x04,   /* append; without it, STO_CREATE truncates an existing file */
    STO_CREATE = 0x08,   /* create if absent */
};

typedef struct { uint32_t total_kb, free_kb; uint8_t degraded; } sto_info_t;
typedef int sto_file_t;

int  sto_mount(void);                 /* ladder inside driver: mount -> retry -> format (internal) / degrade (sd) */
int  sto_info(sto_info_t *out);
int  sto_open(const char *path, int flags, sto_file_t *out);   /* flags: STO_RD, STO_WR|STO_APPEND|STO_CREATE */
int  sto_write(sto_file_t f, const void *buf, size_t n);
int  sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read);
int  sto_seek(sto_file_t f, uint32_t offset);
int  sto_sync(sto_file_t f);
int  sto_close(sto_file_t f);
int  sto_rename(const char *from, const char *to);              /* atomic on LittleFS */
int  sto_unlink(const char *path);
int  sto_list(const char *dir, void (*cb)(const char *name, uint32_t size, void *ctx), void *ctx);
int  sto_probe(void);                 /* self-test: write+read+unlink probe file */

#endif /* HAL_STORAGE_H */
```

- [ ] **Step 2: `components/drivers/storage_internal/idf_component.yml`** (pin the managed component).

```yaml
## storage_internal pulls the ecosystem-standard LittleFS component (joltwallet/littlefs)
## via the IDF component manager, pinned to an exact version for reproducible builds
## (spec §13.1; global constraint: prefer IDF/standard components). The resolved tree is
## captured in the repo-root dependencies.lock (committed); managed_components/ is gitignored.
dependencies:
  joltwallet/littlefs:
    version: "==1.16.5"
```

- [ ] **Step 3: `components/drivers/storage_internal/CMakeLists.txt`** (private dep on `joltwallet__littlefs`; strict flags).

```cmake
# storage_internal -- hal/storage.h implemented over LittleFS (spec §13.1).
#
# LittleFS is not in ESP-IDF core; joltwallet/littlefs is the ecosystem-standard component,
# pulled (pinned) via idf_component.yml by the IDF component manager -- using the standard, not
# reinventing it. The dependency is private: hal/storage.h exposes no esp_littlefs type, so a
# consumer of this driver never sees the managed component.
idf_component_register(
    SRCS "storage_internal.c"
    REQUIRES lt_hal
    PRIV_REQUIRES joltwallet__littlefs)

# Same strictness as components/core (§17.9): -Werror, conversion warnings non-fatal so IDF /
# esp_littlefs macros and inline helpers do not break the build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

- [ ] **Step 4: `components/drivers/storage_internal/storage_internal.c`** (the §13.1 mount ladder, atomic rename, fsync, dir creation, probe/list/info).

```c
/* storage_internal.c -- hal/storage.h over LittleFS (spec §13.1).
 *
 * Backs the §5.1 storage contract with the joltwallet/littlefs managed component mounted at
 * /lfs on the `storage` partition (partitions.csv: data/spiffs subtype id 0x82, which LittleFS
 * reuses). The §13.1 LittleFS geometry (read/prog 128, cache 512, lookahead 128, block_cycles
 * 512; block_size 4096 is fixed by the component for the ESP32) is set through the CONFIG_
 * LITTLEFS_* options in sdkconfig.defaults, so it is not repeated in the conf struct here;
 * only format_if_mount_failed is a struct field, and it is false because the mount ladder --
 * not the component -- decides when to format.
 *
 * Layering: the driver reports the ladder result through sto_mount's return value and never
 * touches sys_flags or the NVS counters (no dependency on components/app). The caller (boot
 * step 7 in app_main) owns SYS_STORAGE_DEAD / the E_STO_* error-ring entries / LT_CTR_STO_FORMAT.
 * Symbols are the plain sto_* names the app links against; main REQUIRES this component (the
 * §4.1 swappable interface-name registration is a 3.4 concern, exactly as for board_devkit_v1).
 */
#include "hal/storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "sto";

#define LFS_BASE   "/lfs"
#define LFS_PART   "storage"           /* partitions.csv label */
#define STO_PATHMAX 128

static const esp_vfs_littlefs_conf_t s_conf = {
    .base_path = LFS_BASE,
    .partition_label = LFS_PART,
    .partition = NULL,
    .format_if_mount_failed = false,   /* the ladder decides (§13.1) */
    .read_only = false,
    .dont_mount = false,
    .grow_on_mount = false,
};

/* Map a backend-relative path ("/sessions/x.log") onto the mount point ("/lfs/sessions/x.log"). */
static int full_path(const char *path, char *out, size_t cap)
{
    if (!path) return -1;
    int n = snprintf(out, cap, "%s%s", LFS_BASE, path);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

int sto_mount(void)
{
    esp_err_t e = esp_vfs_littlefs_register(&s_conf);          /* attempt 1 */
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "mount failed (%s); retrying", esp_err_to_name(e));
        e = esp_vfs_littlefs_register(&s_conf);                /* attempt 2 (retry) */
    }
    int formatted = 0;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "mount failed again (%s); formatting", esp_err_to_name(e));
        esp_err_t fe = esp_littlefs_format(LFS_PART);
        if (fe == ESP_OK) {
            e = esp_vfs_littlefs_register(&s_conf);            /* attempt 3 (post-format) */
            formatted = 1;
        } else {
            ESP_LOGE(TAG, "format failed (%s)", esp_err_to_name(fe));
        }
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "storage DEAD (%s)", esp_err_to_name(e));
        return -1;                                             /* caller -> SYS_STORAGE_DEAD */
    }

    /* §13.1: create the session/track directories at mount if missing (EEXIST is fine). */
    if (mkdir(LFS_BASE "/sessions", 0777) != 0 && errno != EEXIST)
        ESP_LOGW(TAG, "mkdir sessions: %s", strerror(errno));
    if (mkdir(LFS_BASE "/tracks", 0777) != 0 && errno != EEXIST)
        ESP_LOGW(TAG, "mkdir tracks: %s", strerror(errno));

    size_t total = 0, used = 0;
    esp_err_t ie = esp_littlefs_info(LFS_PART, &total, &used);
    if (ie != ESP_OK) ESP_LOGW(TAG, "esp_littlefs_info: %s", esp_err_to_name(ie));
    ESP_LOGI(TAG, "mounted %s at %s: %u KB total, %u KB free%s", LFS_PART, LFS_BASE,
             (unsigned)(total / 1024u), (unsigned)((total - used) / 1024u),
             formatted ? " (formatted)" : "");
    return formatted ? 1 : 0;                                  /* 1 => caller logs E_STO_FORMAT + counter */
}

int sto_info(sto_info_t *out)
{
    if (!out) return -1;
    size_t total = 0, used = 0;
    esp_err_t e = esp_littlefs_info(LFS_PART, &total, &used);
    if (e != ESP_OK) return -1;
    out->total_kb = (uint32_t)(total / 1024u);
    out->free_kb  = (uint32_t)((total - used) / 1024u);
    out->degraded = 0;                          /* internal LittleFS is never "degraded" (an SD concept) */
    return 0;
}

int sto_open(const char *path, int flags, sto_file_t *out)
{
    if (!out) return -1;
    char full[STO_PATHMAX];
    if (full_path(path, full, sizeof full) != 0) return -1;

    int of;
    if ((flags & STO_WR) && (flags & STO_RD)) of = O_RDWR;
    else if (flags & STO_WR)                  of = O_WRONLY;
    else                                       of = O_RDONLY;
    if (flags & STO_CREATE) of |= O_CREAT;
    if (flags & STO_APPEND) of |= O_APPEND;
    /* create-without-append means "start fresh" (the .sum.tmp rewrite path) -> truncate. */
    if ((flags & STO_CREATE) && !(flags & STO_APPEND)) of |= O_TRUNC;

    int fd = open(full, of, 0644);
    if (fd < 0) { ESP_LOGE(TAG, "open %s: %s", full, strerror(errno)); return -1; }
    *out = fd;
    return 0;
}

int sto_write(sto_file_t f, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(f, p + off, n - off);
        if (w < 0) { ESP_LOGE(TAG, "write: %s", strerror(errno)); return -1; }
        off += (size_t)w;
    }
    return 0;
}

int sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read)
{
    ssize_t r = read(f, buf, n);
    if (r < 0) { if (n_read) *n_read = 0; return -1; }
    if (n_read) *n_read = (size_t)r;            /* 0 => EOF */
    return 0;
}

int sto_seek(sto_file_t f, uint32_t offset)
{
    off_t r = lseek(f, (off_t)offset, SEEK_SET);
    return (r < 0) ? -1 : 0;
}

int sto_sync(sto_file_t f)
{
    return (fsync(f) != 0) ? -1 : 0;            /* §13.1: sto_sync maps to fsync */
}

int sto_close(sto_file_t f)
{
    return (close(f) != 0) ? -1 : 0;
}

int sto_rename(const char *from, const char *to)
{
    char f_full[STO_PATHMAX], t_full[STO_PATHMAX];
    if (full_path(from, f_full, sizeof f_full) != 0) return -1;
    if (full_path(to, t_full, sizeof t_full) != 0) return -1;
    if (rename(f_full, t_full) != 0) {          /* atomic on LittleFS (§13.1) */
        ESP_LOGE(TAG, "rename %s -> %s: %s", f_full, t_full, strerror(errno));
        return -1;
    }
    return 0;
}

int sto_unlink(const char *path)
{
    char full[STO_PATHMAX];
    if (full_path(path, full, sizeof full) != 0) return -1;
    if (unlink(full) != 0) { if (errno == ENOENT) return 0; return -1; }
    return 0;
}

int sto_list(const char *dir, void (*cb)(const char *name, uint32_t size, void *ctx), void *ctx)
{
    char full[STO_PATHMAX];
    if (full_path(dir, full, sizeof full) != 0) return -1;
    DIR *d = opendir(full);
    if (!d) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;                            /* skip "." and ".." */
        uint32_t sz = 0;
        if (cb) {
            char item[STO_PATHMAX];
            int m = snprintf(item, sizeof item, "%s/%s", full, ent->d_name);
            struct stat st;
            if (m > 0 && (size_t)m < sizeof item && stat(item, &st) == 0)
                sz = (uint32_t)st.st_size;
            cb(ent->d_name, sz, ctx);
        }
        count++;
    }
    closedir(d);
    return count;
}

int sto_probe(void)
{
    static const char *PROBE = "/.probe";       /* -> /lfs/.probe */
    uint8_t wr[256], rd[256];
    for (size_t i = 0; i < sizeof wr; i++) wr[i] = (uint8_t)(i * 7u + 1u);

    sto_file_t f;
    if (sto_open(PROBE, STO_WR | STO_CREATE, &f) != 0) return -1;
    int rc = sto_write(f, wr, sizeof wr);
    if (rc == 0) rc = sto_sync(f);
    sto_close(f);
    if (rc != 0) { sto_unlink(PROBE); return -1; }

    if (sto_open(PROBE, STO_RD, &f) != 0) { sto_unlink(PROBE); return -1; }
    size_t got = 0;
    rc = sto_read(f, rd, sizeof rd, &got);
    sto_close(f);
    sto_unlink(PROBE);
    if (rc != 0 || got != sizeof rd || memcmp(wr, rd, sizeof wr) != 0) return -1;
    return 0;
}
```

- [ ] **Step 5: `sdkconfig.defaults`** — append the §13.1 LittleFS geometry after the existing `CONFIG_LITTLEFS_MAX_PARTITIONS=1` line.

```
CONFIG_LITTLEFS_MAX_PARTITIONS=1
# LittleFS geometry per spec §13.1 (block_size 4096 is fixed by the component for the ESP32).
CONFIG_LITTLEFS_READ_SIZE=128
CONFIG_LITTLEFS_WRITE_SIZE=128
CONFIG_LITTLEFS_CACHE_SIZE=512
CONFIG_LITTLEFS_LOOKAHEAD_SIZE=128
CONFIG_LITTLEFS_BLOCK_CYCLES=512
```

- [ ] **Step 6: `.gitignore`** — keep `managed_components/` ignored, but STOP ignoring `dependencies.lock` so it is committed (reproducible builds). Replace the `dependencies.lock` line with a comment:

```
managed_components/
# dependencies.lock IS committed (reproducible builds); only the fetched sources are ignored.
```

- [ ] **Step 7: wire the CMake search path + `main` REQUIRES.**

Top-level `CMakeLists.txt` — append the 3.3 storage dir and update the 3.3 comment:
```cmake
#   3.3  components/drivers/storage_internal (LittleFS via joltwallet/littlefs;
#        the STORAGE=sd variant's storage_sd lands with the SD hardware).
...
set(EXTRA_COMPONENT_DIRS components/core components/lt_hal components/app components/drivers/board_devkit_v1 components/drivers/storage_internal)
```

`main/CMakeLists.txt` — add `storage_internal` to REQUIRES:
```cmake
    REQUIRES core esp_system app lt_hal nvs_flash esp_timer esp_hw_support board_devkit_v1 storage_internal)
```

- [ ] **Step 8: `components/app/include/app/lt_err.h`** — add the storage codes above the `E_SYS_*` block:
```c
    /* storage (§17.7); the full code table lands with the 3.5 console. */
    E_STO_WRITE       = 0x0401,
    E_STO_MOUNT       = 0x0402,
    E_STO_FORMAT      = 0x0403,
    E_STO_FULL        = 0x0404,
    E_STO_EVICT       = 0x0405,
```

- [ ] **Step 9: `main/app_main.c`** — `#include "hal/storage.h"`, and add boot step 7 right after `board_init(); board_gps_power(true);`:
```c
    /* §4.7 step 7 (internal storage): mount LittleFS; the mount ladder (mount -> retry ->
     * format -> dead) lives in the driver. The driver stays app-agnostic, so the boot sequence
     * owns the sys_flags / error-ring / counter effects of the ladder result (§13.1). */
    int mrc = sto_mount();
    if (mrc < 0) {
        sys_flags_set(SYS_STORAGE_DEAD);
        errlog_add(E_STO_MOUNT, 0);
        ESP_LOGE(TAG, "storage DEAD (summaries fall back to RTC/NVS best-effort, 3.5)");
    } else {
        if (mrc == 1) {                        /* mounted only after a reformat */
            lt_counters_inc(LT_CTR_STO_FORMAT, true);
            errlog_add(E_STO_FORMAT, 0);
        }
        if (sto_probe() != 0) ESP_LOGW(TAG, "storage probe failed");   /* §17.6 self-test */
        sto_info_t si;
        if (sto_info(&si) == 0)
            ESP_LOGI(TAG, "storage: %u/%u KB free", (unsigned)si.free_kb, (unsigned)si.total_kb);
    }
```

**Task 1 verification:** `joltwallet/littlefs 1.16.5` fetched on first configure; `dependencies.lock` written + committed, `managed_components/` gitignored. `./build.sh moto_neo6m build` + `size` pass (app image 327,632 → **363,632 B**, LittleFS adds ~36 KB), `./build.sh moto_sim build` passes; no warnings from new code. Mount ladder / probe / directory creation are on-board checks for the orchestrator.


```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

### Task 2: ring/queue set (§4.4) + logger task (§13.3)


**Files:**
- Create: `components/app/include/app/lt_ipc.h`, `components/app/include/app/logger.h`, `components/app/sys/lt_ipc.c`, `components/app/logger/logger.c`
- Edit: `main/app_main.c` (boot step 11: `lt_ipc_init()`; step 12: `logger_start()`)

**Interfaces:**
- Produces: the §4.4 channels `g_fix_ring`/`g_fused_ring`/`g_evt_q`/`g_log_req_q` + `log_request_t`/`LOGGER_*`; the logger task and `logger_notify()`. The pipeline (3.4) is the producer/broadcaster; 3.3 creates the channels and the logger consumes.
- Consumes: `core/ring.h` (unchanged), `core/ses.h` codecs, `hal/storage.h` (T1), `app/lt_sup.h` (hb/sys_flags/`sup_register_task`), `app/lt_nvs.h` (boot counter, counters, `errlog_add`), `build_config.h` (CFG_*).

**Rulings (spec is the authority):**
1. **`core/ring.h` already supports both §4.4 policies — no change needed.** It is the plan-01 lock-free SPSC ring with an `overwrite` flag: `overwrite=false` gives drop-newest **with** a `dropped` counter (fix loss, logged/inspectable), `overwrite=true` gives overwrite-oldest with the CAS-on-`tail` seqlock so a torn copy is never returned. `lt_ipc_init` sets `fix_ring` `overwrite=false` (32×`gps_fix_t`) and `fused_ring` `overwrite=true` (64×`fused_sample_t`) — exactly §4.4. Rings are static storage; the FreeRTOS queues are `xQueueCreateStatic` (§17.9, no malloc).
2. **`log_request_t` is 16 B (`_Static_assert`).** `{u8 type, u8 mode, u8 reason, u8 _pad, u16 venue_id, u16 layout_id, i64 gps_us}` = 16 B, covering the §4.4 open/close/rebuild/evict set (`LOGGER_OPEN_SESSION/CLOSE_SESSION/REBUILD_SUMMARY/EVICT`). OPEN carries the session mode + venue/layout for the `.sum` header; CLOSE carries the `END` reason + gps_us.
3. **Ring-notify = a task notification with a 1000 ms timeout — fully static, no queue-set object.** §4.3 says "ring notify or 1000 ms". The logger blocks on `ulTaskNotifyTake(pdTRUE, 1000 ms)`; `logger_notify()` (`xTaskNotifyGive`) is called by any producer after a ring push or after enqueuing a request. This keeps the wait built into the static task (a `xQueueCreateSet` allocates from the heap), honouring §17.9's static-only rule while giving the sub-1000 ms latency the spec wants. Requests are drained non-blocking each loop, so the 1000 ms timeout is the backstop.
4. **Event→record fidelity in 3.3 (documented gap the pipeline closes in 3.4).** §13.3 maps drained events to records, but `evt_q` carries only `event_t` — not the full `lap_result_t`/`drag_result_t` (with sectors/stats/gates) that `ses_encode_lap`/`_drag_run` need, and `EV_SECTOR` carries no lap_no. With no pipeline/lapengine yet, the logger encodes **every** drained event as a generic `EVENT` record (type 0x09 = `{mono,gps,code,arg}`, which represents any event verbatim), and **additionally** emits a *minimal* `LAP` (from `EV_LAP_COMPLETE`: lap_no/time_ms/flags, `n_sectors=0`) and `DRAG_RUN` (from `EV_DRAG_DONE`: run_no) so the `.sum` carries them and sets the rebuild flag. The sector-/stats-populated `LAP`, the `SECTOR`/`DRAG_GATE` records, and the real `VENUE` name come from the engine result structs the pipeline owns (3.4). This is sufficient for the roadmap exit criterion (LAP present in an intact `.sum`; a decodable `.log`).
5. **`.sum` accumulators are sized so the assembly always fits the 4 KB buffer.** §12.5 = HDR + VENUE + every LAP + every DRAG_RUN [+ END], built in a 4 KB buffer, `.sum.tmp` → fsync → atomic rename. The logger keeps two accumulators (`s_lap_acc` 3072 B, `s_drag_acc` 512 B) of encoded frames; with HDR(99)+VENUE(41)+END(14) that is ≤ 3738 B < 4096. §12.5's "laps beyond the first 100 are summarised only in the `.log`" is realized by the accumulator cap: overflow frames stay in the `.log` and are dropped from the `.sum` accumulator only.
6. **The logger does not auto-open a session at boot** (no producer exists in 3.3; the power task commands OPEN in 3.4). It starts idle and opens on `LOGGER_OPEN_SESSION`. It writes `SESSION_HDR` to the `.log` and an initial `.sum` (HDR+VENUE) at open, flushing the HDR immediately so a cut right after open still yields a valid `.log` start. Session id `S%05u_%03u` = boot counter (u16, `lt_nvs_boot_get()`) + a per-boot sequence.
7. **The logger registers with the supervisor** (`sup_register_task(HB_LOGGER, self, 10 s)`) and bumps `g_hb[HB_LOGGER]` every loop, so a stalled logger is caught by the §17.2 heartbeat watch.


- [ ] **Step 1: `components/app/include/app/lt_ipc.h`** (the §4.4 channels + `log_request_t`).

```c
/* app/lt_ipc.h -- pipeline<->logger channels (spec §4.4).
 *
 * The §4.4 sample rings and the logger's control/event queues. The pipeline (3.4) is the single
 * producer for the rings and the broadcaster for the event queue; the logger (3.3) is the single
 * consumer. Objects are defined in lt_ipc.c with static storage (no malloc, §17.9) and created at
 * boot step 11 by lt_ipc_init(). The rings are the core/ring.h lock-free SPSC ring.
 */
#ifndef APP_LT_IPC_H
#define APP_LT_IPC_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "core/event.h"
#include "core/ring.h"
#include "core/types.h"

/* §4.4 sample rings, pipeline -> logger:
 *   fix_ring   32 x gps_fix_t      drop-newest + counter (fix loss is logged)
 *   fused_ring 64 x fused_sample_t overwrite-oldest    (sample loss is tolerable) */
#define FIX_RING_CAP   32u
#define FUSED_RING_CAP 64u
extern ring_t g_fix_ring;
extern ring_t g_fused_ring;

/* §4.4 evt_q -- the LOGGER's copy of the event broadcast (depth 16, event_t). The pipeline
 * xQueueSends each event to the ui/logger/power queues separately; this is the logger's. */
#define EVT_Q_DEPTH 16
extern QueueHandle_t g_evt_q;

/* §4.4 log_req_q -- power/conn -> logger control channel (depth 4, log_request_t 16 B). */
#define LOG_REQ_Q_DEPTH 4

typedef enum {
    LOGGER_OPEN_SESSION    = 0,   /* open .log + start .sum; id = boot_cnt + per-boot seq (§12.1) */
    LOGGER_CLOSE_SESSION   = 1,   /* write END, finalise .sum, close .log */
    LOGGER_REBUILD_SUMMARY = 2,   /* force a .sum rewrite now */
    LOGGER_EVICT           = 3,   /* run the §12.7 eviction check now */
} log_req_type_t;

typedef struct {
    uint8_t  type;        /* log_req_type_t */
    uint8_t  mode;        /* OPEN: session mode (§12.1) */
    uint8_t  reason;      /* CLOSE: END.reason (§12.3) */
    uint8_t  _pad;
    uint16_t venue_id;    /* OPEN: venue id for the .sum HDR/VENUE frame */
    uint16_t layout_id;   /* OPEN: layout id */
    int64_t  gps_us;      /* OPEN: start_gps_us; CLOSE: END gps_us (0 if unknown) */
} log_request_t;
_Static_assert(sizeof(log_request_t) == 16, "log_request_t must be 16 B (§4.4)");

extern QueueHandle_t g_log_req_q;

/* Create the rings + queues. Idempotent; call once at boot step 11 (§4.7). */
void lt_ipc_init(void);

#endif /* APP_LT_IPC_H */
```

- [ ] **Step 2: `components/app/sys/lt_ipc.c`** (static storage + `lt_ipc_init`).

```c
/* lt_ipc.c -- definitions + static storage for the §4.4 pipeline<->logger channels. */
#include "app/lt_ipc.h"

ring_t g_fix_ring;
ring_t g_fused_ring;
static gps_fix_t      s_fix_store[FIX_RING_CAP];
static fused_sample_t s_fused_store[FUSED_RING_CAP];

QueueHandle_t g_evt_q;
QueueHandle_t g_log_req_q;
static StaticQueue_t s_evt_ctrl;
static uint8_t       s_evt_store[EVT_Q_DEPTH * sizeof(event_t)];
static StaticQueue_t s_logreq_ctrl;
static uint8_t       s_logreq_store[LOG_REQ_Q_DEPTH * sizeof(log_request_t)];

void lt_ipc_init(void)
{
    /* Rings: fix_ring drop-newest (overwrite=false), fused_ring overwrite-oldest (overwrite=true). */
    ring_init(&g_fix_ring, s_fix_store, sizeof(gps_fix_t), FIX_RING_CAP, false);
    ring_init(&g_fused_ring, s_fused_store, sizeof(fused_sample_t), FUSED_RING_CAP, true);

    if (!g_evt_q)
        g_evt_q = xQueueCreateStatic(EVT_Q_DEPTH, sizeof(event_t), s_evt_store, &s_evt_ctrl);
    if (!g_log_req_q)
        g_log_req_q = xQueueCreateStatic(LOG_REQ_Q_DEPTH, sizeof(log_request_t),
                                         s_logreq_store, &s_logreq_ctrl);
}
```

- [ ] **Step 3: `components/app/include/app/logger.h`**.

```c
/* app/logger.h -- logger task (spec §4.3, §13.3).
 *
 * Core 0, prio 8, stack 4096, static. Drains the §4.4 fix/fused rings and the logger's event
 * queue into 4 KB .log batches through core/ses, rebuilds the .sum atomically on LAP/DRAG_RUN,
 * fsyncs every 2 s, and evicts every 60 s (§12.5-12.7). Session open/close is commanded over
 * log_req_q. The producer (pipeline, 3.4) calls logger_notify() after pushing to a ring so the
 * logger wakes before its 1000 ms timeout ("ring notify or 1000 ms").
 */
#ifndef APP_LOGGER_H
#define APP_LOGGER_H

void logger_start(void);    /* create + start the task (boot step 12) */
void logger_notify(void);   /* wake the logger (task notification); safe from any task */

#endif /* APP_LOGGER_H */
```

- [ ] **Step 4: `components/app/logger/logger.c`** (the §13.3 loop; open/close; batch/sum/evict).

```c
/* logger.c -- logger task (spec §4.3 core 0/prio 8/stack 4096; loop §13.3; files §12.1/§12.5-12.7).
 *
 * Consumes the §4.4 fix/fused rings and the logger's event queue, encoding through core/ses into
 * a 4 KB .log batch; rebuilds the .sum (HDR + VENUE + every LAP + every DRAG_RUN [+ END]) via
 * tmp+fsync+atomic-rename on LAP/DRAG_RUN and at close; fsyncs the .log every 2 s; evicts every
 * 60 s. Session open/close arrives over log_req_q. The task is static; the only dynamic wait is
 * a task notification (ring-notify) with a 1000 ms timeout -- no heap, no queue-set object.
 *
 * Session record fidelity in 3.3: with no pipeline yet, the logger encodes each drained event as
 * a generic EVENT record (§12.3 0x09 = {mono,gps,code,arg}, which represents any event verbatim),
 * and additionally emits a minimal LAP (from EV_LAP_COMPLETE) / DRAG_RUN (from EV_DRAG_DONE) built
 * from the event payload so the .sum carries them. The sector-/stats-populated LAP, the SECTOR and
 * DRAG_GATE records, and the real VENUE name need the lapengine/dragengine result structs the
 * pipeline owns; those are wired in 3.4. This is enough to exercise the power-cut exit criterion
 * (.sum intact via atomic rename; .log decodable with a resync-past-bad truncated tail).
 */
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_sup.h"
#include "app/lt_nvs.h"
#include "app/lt_err.h"
#include "hal/storage.h"

#include "core/event.h"
#include "core/ses.h"
#include "core/types.h"

#include "build_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "log";

/* §4.3 task */
#define LOG_CORE         0
#define LOG_PRIO         8
#define LOG_STACK_BYTES  4096
#define LOG_STACK_WORDS  (LOG_STACK_BYTES / sizeof(StackType_t))
#define LOG_STALL_S      5             /* supervisor heartbeat-stall window (§17.2) */

/* §13.3 cadence + buffers */
#define BATCH_CAP        4096
#define BATCH_FLUSH_B    3584          /* write when the batch reaches this */
#define WRITE_INTERVAL_MS 1000
#define SYNC_INTERVAL_MS  2000
#define EVICT_INTERVAL_MS 60000
#define LOOP_TIMEOUT_MS   1000
#define FRAME_TMP_CAP    256           /* >= max framed record (247 payload + 5) */

/* §12.5 .sum assembly (sized so HDR+VENUE+laps+drags+END always fit BATCH_CAP). */
#define SUM_BUILD_CAP    4096
#define LAP_ACC_CAP      3072
#define DRAG_ACC_CAP     512

static StaticTask_t s_tcb;
static StackType_t  s_stack[LOG_STACK_WORDS];
static TaskHandle_t s_task;

/* open session */
static sto_file_t s_log_fd;
static bool       s_open;
static char       s_id[11];            /* "S%05u_%03u" + NUL */
static uint8_t    s_seq;               /* per-boot session sequence (§12.1) */
static ses_hdr_t  s_hdr;
static uint16_t   s_venue_id, s_layout_id;
static char       s_venue_name[33];

/* codecs + batch */
static ses_fix_state_t   s_fix_st;
static ses_fused_state_t s_fused_st;
static uint8_t s_batch[BATCH_CAP];
static size_t  s_batch_len;

/* .sum accumulators (encoded LAP / DRAG_RUN frames, in emission order) */
static uint8_t s_lap_acc[LAP_ACC_CAP];   static size_t s_lap_len;
static uint8_t s_drag_acc[DRAG_ACC_CAP]; static size_t s_drag_len;
static bool    s_sum_dirty;

/* timing + state */
static uint32_t s_last_write_ms, s_last_sync_ms, s_last_evict_ms;
static bool     s_samples_full;        /* SYS_STORAGE_FULL: sample logging paused, summaries continue */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void log_path(char *out, size_t cap, const char *id, const char *ext)
{
    (void)snprintf(out, cap, "/sessions/%s%s", id, ext);
}

/* Write the accumulated batch to the open .log. */
static void do_write(void)
{
    if (!s_open || s_batch_len == 0) return;
    if (sto_write(s_log_fd, s_batch, s_batch_len) != 0) errlog_add(E_STO_WRITE, (uint32_t)s_batch_len);
    s_batch_len = 0;
    s_last_write_ms = now_ms();
}

/* Append one framed record to the batch, flushing first if it would not fit. */
static void batch_append(const uint8_t *frame, int n)
{
    if (n <= 0) return;
    if (s_batch_len + (size_t)n > BATCH_CAP) do_write();
    memcpy(s_batch + s_batch_len, frame, (size_t)n);
    s_batch_len += (size_t)n;
}

static void acc_append(uint8_t *acc, size_t *len, size_t cap, const uint8_t *frame, int n)
{
    if (n <= 0 || *len + (size_t)n > cap) return;   /* beyond cap: kept in .log only (§12.5) */
    memcpy(acc + *len, frame, (size_t)n);
    *len += (size_t)n;
}

/* §12.5: rebuild .sum = HDR + VENUE + every LAP + every DRAG_RUN [+ END] via tmp+sync+rename. */
static void rebuild_sum(bool closing, int64_t end_gps_us, uint8_t end_reason)
{
    if (s_id[0] == 0) return;
    static uint8_t buf[SUM_BUILD_CAP];
    size_t off = 0;
    int n;

    n = ses_encode_hdr(&s_hdr, buf + off, SUM_BUILD_CAP - off);
    if (n < 0) return;
    off += (size_t)n;
    n = ses_encode_venue(s_venue_id, s_layout_id, s_venue_name, buf + off, SUM_BUILD_CAP - off);
    if (n > 0) off += (size_t)n;
    if (off + s_lap_len <= SUM_BUILD_CAP)  { memcpy(buf + off, s_lap_acc, s_lap_len);   off += s_lap_len; }
    if (off + s_drag_len <= SUM_BUILD_CAP) { memcpy(buf + off, s_drag_acc, s_drag_len); off += s_drag_len; }
    if (closing) {
        n = ses_encode_end(end_gps_us, end_reason, buf + off, SUM_BUILD_CAP - off);
        if (n > 0) off += (size_t)n;
    }

    char tmp_path[40], sum_path[40];
    log_path(tmp_path, sizeof tmp_path, s_id, ".sum.tmp");
    log_path(sum_path, sizeof sum_path, s_id, ".sum");
    sto_file_t f;
    if (sto_open(tmp_path, STO_WR | STO_CREATE, &f) != 0) { errlog_add(E_STO_WRITE, 0); return; }
    int rc = sto_write(f, buf, off);
    if (rc != 0) errlog_add(E_STO_WRITE, (uint32_t)off);
    if (rc == 0) {
        rc = sto_sync(f);
        if (rc != 0) errlog_add(E_STO_WRITE, 0);
    }
    sto_close(f);
    if (rc == 0 && sto_rename(tmp_path, sum_path) != 0) errlog_add(E_STO_WRITE, 0);   /* atomic (§13.1) */
}

static void eviction_check(void);   /* forward decl: called from open_session (§12.7 "at session start") and the main loop */

static void open_session(const log_request_t *req)
{
    if (s_open) return;                            /* one open .log at a time */
    if (s_seq < 0xFF) s_seq++;
    (void)snprintf(s_id, sizeof s_id, "S%05u_%03u",
                   (unsigned)(lt_nvs_boot_get() & 0xFFFFu), (unsigned)s_seq);

    memset(&s_hdr, 0, sizeof s_hdr);
    (void)snprintf(s_hdr.session_id, sizeof s_hdr.session_id, "%s", s_id);
    s_hdr.mode      = req->mode;
    s_hdr.variant   = (uint8_t)(CFG_VARIANT_MOTO ? 0 : 1);
    s_hdr.venue_id  = req->venue_id;
    s_hdr.layout_id = req->layout_id;
    (void)snprintf(s_hdr.fw,   sizeof s_hdr.fw,   "%s", CFG_FW_VERSION);
    (void)snprintf(s_hdr.hwid, sizeof s_hdr.hwid, "%s", CFG_HWID);
    s_hdr.log_profile  = 0;
    s_hdr.fused_hz     = (uint8_t)CFG_FUSED_LOG_HZ;
    s_hdr.gps_hz       = 0;                         /* real rate filled by the pipeline (3.4) */
    s_hdr.start_gps_us = req->gps_us;               /* calib block left zero until 3.4 */

    s_venue_id = req->venue_id;
    s_layout_id = req->layout_id;
    s_venue_name[0] = 0;

    ses_fix_state_init(&s_fix_st);
    ses_fused_state_init(&s_fused_st);
    s_batch_len = 0;
    s_lap_len = 0;
    s_drag_len = 0;
    s_sum_dirty = false;
    s_samples_full = false;

    char path[40];
    log_path(path, sizeof path, s_id, ".log");
    if (sto_open(path, STO_WR | STO_APPEND | STO_CREATE, &s_log_fd) != 0) {
        errlog_add(E_STO_WRITE, 0);
        s_id[0] = 0;
        return;
    }
    s_open = true;
    s_last_write_ms = s_last_sync_ms = now_ms();

    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_hdr(&s_hdr, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();                                     /* flush HDR now: a cut right after open still yields a valid .log */
    sto_sync(s_log_fd);                              /* and sync it: a cut right after open must not lose the .log HDR either */
    rebuild_sum(false, 0, 0);                        /* initial .sum: HDR + VENUE */
    eviction_check();                                /* §12.7: eviction runs at session start, not only every 60 s */
    ESP_LOGI(TAG, "session %s open", s_id);
}

static void close_session(const log_request_t *req)
{
    if (!s_open) return;
    do_write();
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_end(req->gps_us, req->reason, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();
    sto_sync(s_log_fd);
    sto_close(s_log_fd);
    s_open = false;
    rebuild_sum(true, req->gps_us, req->reason);     /* finalise .sum with END */
    ESP_LOGI(TAG, "session %s closed (reason %u)", s_id, (unsigned)req->reason);
}

static void handle_request(const log_request_t *req)
{
    switch (req->type) {
    case LOGGER_OPEN_SESSION:    open_session(req); break;
    case LOGGER_CLOSE_SESSION:   close_session(req); break;
    case LOGGER_REBUILD_SUMMARY: if (s_open) rebuild_sum(false, 0, 0); break;
    case LOGGER_EVICT:           s_last_evict_ms = now_ms() - EVICT_INTERVAL_MS; break;   /* force an eviction pass this loop */
    default: break;
    }
}

static void handle_event(const event_t *ev)
{
    uint8_t tmp[FRAME_TMP_CAP];
    int n;
    if (ev->type == EV_LAP_COMPLETE) {
        lap_result_t lap;
        memset(&lap, 0, sizeof lap);
        lap.lap_no       = ev->arg16;
        lap.time_ms      = ev->arg32;
        lap.flags        = ev->flags;
        lap.start_gps_us = ev->gps_us;
        lap.n_sectors    = 0;                        /* sectors/stats added by the pipeline (3.4) */
        n = ses_encode_lap(&lap, tmp, sizeof tmp);
        batch_append(tmp, n);
        acc_append(s_lap_acc, &s_lap_len, LAP_ACC_CAP, tmp, n);
        s_sum_dirty = true;
    } else if (ev->type == EV_DRAG_DONE) {
        drag_result_t run;
        memset(&run, 0, sizeof run);
        run.run_no  = ev->arg16;
        run.n_gates = 0;
        n = ses_encode_drag_run(&run, tmp, sizeof tmp);
        batch_append(tmp, n);
        acc_append(s_drag_acc, &s_drag_len, DRAG_ACC_CAP, tmp, n);
        s_sum_dirty = true;
    } else {
        n = ses_encode_event(ev->mono_us, ev->gps_us, ev->type, ev->arg32, tmp, sizeof tmp);
        batch_append(tmp, n);
    }
}

static void drain_rings(void)
{
    gps_fix_t fix;
    while (ring_pop(&g_fix_ring, &fix)) {
        if (s_open && !s_samples_full) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fix(&s_fix_st, &fix, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
        ses_fused_state_on_fix(&s_fused_st, fix.gps_us);   /* keep fused deltas referenced to fixes */
    }
    fused_sample_t fs;
    while (ring_pop(&g_fused_ring, &fs)) {
        if (s_open && !s_samples_full) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fused(&s_fused_st, &fs, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
    }
}

static void drain_events(void)
{
    event_t ev;
    while (xQueueReceive(g_evt_q, &ev, 0) == pdTRUE) {
        if (s_open) handle_event(&ev);
    }
}

typedef struct { char oldest[24]; char curlog[24]; } evict_ctx_t;
static void evict_cb(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    evict_ctx_t *e = (evict_ctx_t *)ctx;
    size_t len = strlen(name);
    if (len < 4 || strcmp(name + len - 4, ".log") != 0) return;   /* only .log (never .sum) */
    if (strcmp(name, e->curlog) == 0) return;                     /* never the current session */
    if (e->oldest[0] == 0 || strcmp(name, e->oldest) < 0)
        (void)snprintf(e->oldest, sizeof e->oldest, "%s", name);  /* smallest id == oldest (§12.7) */
}

static void eviction_check(void)
{
    sto_info_t si;
    if (sto_info(&si) != 0 || si.total_kb == 0) return;
    if (si.free_kb >= si.total_kb / 10u) {
        if (s_samples_full) { s_samples_full = false; sys_flags_clear(SYS_STORAGE_FULL); }
        return;
    }
    /* free < 10 %: delete the oldest .log that is not the current session. */
    evict_ctx_t e;
    memset(&e, 0, sizeof e);
    if (s_id[0]) (void)snprintf(e.curlog, sizeof e.curlog, "%s.log", s_id);
    sto_list("/sessions", evict_cb, &e);
    if (e.oldest[0]) {
        char p[40];
        (void)snprintf(p, sizeof p, "/sessions/%s", e.oldest);
        sto_unlink(p);
        errlog_add(E_STO_EVICT, 0);
        ESP_LOGW(TAG, "evicted %s (free %u/%u KB)", e.oldest, (unsigned)si.free_kb, (unsigned)si.total_kb);
    } else if (si.free_kb < si.total_kb / 20u) {    /* nothing to delete and < 5 %: pause samples */
        if (!s_samples_full) { s_samples_full = true; sys_flags_set(SYS_STORAGE_FULL); errlog_add(E_STO_FULL, 0); }
    }
}

static void logger_task(void *arg)
{
    (void)arg;
    sup_register_task(HB_LOGGER, xTaskGetCurrentTaskHandle(), LOG_STALL_S);
    s_last_evict_ms = now_ms();
    ESP_LOGI(TAG, "logger up (core %d prio %d)", LOG_CORE, LOG_PRIO);

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOOP_TIMEOUT_MS));   /* ring-notify or 1000 ms */

        log_request_t req;
        while (xQueueReceive(g_log_req_q, &req, 0) == pdTRUE) handle_request(&req);

        drain_rings();
        drain_events();

        uint32_t now = now_ms();
        if (s_open && s_batch_len &&
            (s_batch_len >= BATCH_FLUSH_B || (now - s_last_write_ms) >= WRITE_INTERVAL_MS))
            do_write();
        if (s_open && (now - s_last_sync_ms) >= SYNC_INTERVAL_MS) {
            sto_sync(s_log_fd);
            s_last_sync_ms = now;
        }
        if (s_sum_dirty && s_open) { rebuild_sum(false, 0, 0); s_sum_dirty = false; }
        if ((now - s_last_evict_ms) >= EVICT_INTERVAL_MS) { eviction_check(); s_last_evict_ms = now_ms(); }

        g_hb[HB_LOGGER]++;
    }
}

void logger_start(void)
{
    if (s_task) return;
    s_task = xTaskCreateStaticPinnedToCore(logger_task, "logger", LOG_STACK_WORDS, NULL,
                                           LOG_PRIO, s_stack, &s_tcb, LOG_CORE);
}

void logger_notify(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
```

- [ ] **Step 5: `main/app_main.c`** — `#include "app/logger.h"` and `#include "app/lt_ipc.h"`, and after `lt_queues_init();`:
```c
    /* §4.7 step 11 (cont): the §4.4 pipeline<->logger rings + the logger's event/control
     * queues. The pipeline producer lands in 3.4; 3.3 creates them and the logger consumes. */
    lt_ipc_init();

    /* §4.7 step 12 (logger): start the logger task (core 0, prio 8). It idles until a
     * LOGGER_OPEN_SESSION request arrives (from the power task in 3.4, or `dbg logtest` now);
     * with storage dead it stays idle (open fails gracefully). */
    logger_start();
```

**Task 2 verification:** `./build.sh moto_neo6m build` + `size` pass (app image **370,128 B**), `./build.sh moto_sim build` passes; no warnings from new code; `_Static_assert(sizeof(log_request_t)==16)` holds. End-to-end drain/write/rebuild is exercised by the T3 `dbg` verbs and confirmed on-board by the orchestrator.


```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

### Task 3: `dbg` verbs (`logtest`/`fs`/`sum`/`logck`) for the power-cut exit test


**Files:**
- Edit: `components/app/sys/dbg_console.c` (extend the 3.2 `dbg` command)

**Interfaces:**
- Produces: the on-device verbs that exercise the logger end-to-end (write and read-back) so the roadmap power-cut exit test is possible before the 3.4 pipeline exists.
- Consumes: `app/lt_ipc.h` (rings/queues), `app/logger.h` (`logger_notify`), `hal/storage.h`, `core/ses.h` reader/decoders, `core/{types,event}.h`.

**Rulings (spec is the authority):**
1. **The verbs extend the single `dbg` command dispatch** (the 3.2 `esp_console` REPL pattern): `status` (kept) | `logtest [n]` | `fs` | `sum <id>` | `logck <id>`. Kept minimal; the full §18.4 export console (`list/open/get/export/...`) replaces this REPL in 3.5.
2. **`dbg logtest [n]` backpressures the drop-newest `fix_ring`** so all `n` fixes actually flow through (on full, it `logger_notify()`s and yields, then retries) — this keeps the logger writing continuously, widening the window in which a power cut lands mid-`.log`-write. It opens a session, queues the **2 LAP events first** (so the `.sum` is rebuilt with laps early, before the long sample tail), then streams `n` synthetic fixes, and **does NOT close** — so there is no `END` and the cut lands mid-session. Default `n=1000`, clamped `[1,100000]`.
3. **`sum`/`logck` read the file back on-device through a `core/ses` reader** (a file-scope `static ses_reader_t`, ~0.8 KB kept off the console stack). `sum` reports HDR ok (+fw/hwid), VENUE (+ids), LAP/DRAG_RUN counts, END present (expected *no* mid-session), and the reader's ok/bad frame counts — proving the atomically-renamed `.sum` is intact. `logck` reports good/bad frame counts + a per-type tally; a truncated/torn tail shows as `bad≥1` that the reader **resyncs past**, proving the `.log` stays decodable.
4. **REPL task stack 4096 → 6144** for the `sum`/`logck` file readers + `printf` (the reader struct itself is static/off-stack).


- [ ] **Step 1: `components/app/sys/dbg_console.c`** (full rewrite of the 3.2 file — adds the verbs, keeps `dbg status`).

```c
/* dbg_console.c -- diagnostics console (spec §18.4; 3.2 exit `dbg status` + 3.3 storage/logger verbs).
 *
 * IDF esp_console REPL on UART0. 3.2 registered `dbg status`; 3.3 adds the verbs that exercise the
 * logger end-to-end so the roadmap power-cut exit test is possible before the 3.4 pipeline exists:
 *
 *   dbg logtest [n]  open a session, push n synthetic FIX records into fix_ring + 2 LAP events into
 *                    the logger's evt_q, and let the logger drain/write/rebuild-.sum. Does NOT close,
 *                    so a power cut lands mid-session (mid-.log-write).
 *   dbg fs           mount state, total/free KB, degraded, sys storage flags, and `/sessions` listing.
 *   dbg sum <id>     read <id>.sum back through a core/ses reader: HDR ok, VENUE, LAP count, END present.
 *   dbg logck <id>   read <id>.log back: good-/bad-frame counts (a truncated tail shows as one bad
 *                    frame the reader resyncs past -> proves the .log stays decodable) + per-type tally.
 *
 * The full §18.4 export console (status/list/open/get/... and export) replaces this in 3.5.
 */
#include "app/dbg_console.h"
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"
#include "hal/storage.h"

#include "core/event.h"
#include "core/ses.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"

static int s_reset_reason;

/* ---------------- dbg status (kept from 3.2) ---------------- */
static int cmd_status(void)
{
    const lt_counters_t *c = lt_counters();
    long long up = esp_timer_get_time() / 1000000;
    printf("boot count : %u\n", (unsigned)lt_nvs_boot_get());
    printf("reset      : %s (%d)\n", lt_reset_reason_str(s_reset_reason), s_reset_reason);
    printf("uptime     : %lld s\n", up);
    printf("counters   : boots=%u crashes=%u wdt=%u brownout=%u sto_format=%u\n",
           (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt, (unsigned)c->brownout,
           (unsigned)c->sto_format);
    printf("sys_flags  : 0x%08x%s\n", (unsigned)sys_flags_get(),
           (sys_flags_get() & (1u << SYS_SAFE_MODE)) ? " [SAFE_MODE]" : "");
    for (int i = 0; i < HB_COUNT; i++) printf("hb[%d]      : %u\n", i, (unsigned)g_hb[i]);
    return 0;
}

/* ---------------- dbg logtest [n] ---------------- */
static void synth_fix(gps_fix_t *f, uint32_t i)
{
    memset(f, 0, sizeof *f);
    f->gps_us     = (int64_t)1700000000000000LL + (int64_t)i * 200000;   /* 5 Hz */
    f->mono_us    = (int64_t)esp_timer_get_time();
    f->lat_e7     = -338900000 + (int32_t)(i % 2000);                     /* ~ -33.89 deg, wandering */
    f->lon_e7     =  184000000 + (int32_t)(i % 2000);                     /* ~ 18.40 deg */
    f->alt_mm     = 45000 + (int32_t)(i % 50);
    f->gspeed_mms = 20000 + (int32_t)(i % 1000);
    f->head_e5    = (int32_t)((i * 137) % 36000000);
    f->hacc_mm    = 2000;
    f->sacc_mms   = 300;
    f->pdop_e2    = 120;
    f->fix_type   = 3;
    f->sats       = 10;
    f->flags      = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f->valid      = 1;
}

static int cmd_logtest(int argc, char **argv)
{
    long n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 1000;
    if (n < 1) n = 1;
    if (n > 100000) n = 100000;

    /* open a session */
    log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = 1, .venue_id = 1, .layout_id = 1,
                          .gps_us = (int64_t)1700000000000000LL };
    xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100));
    logger_notify();
    vTaskDelay(pdMS_TO_TICKS(30));      /* let the logger open + write the HDR + initial .sum */

    /* two LAP events (rebuild .sum with laps early, before the long .log tail) */
    for (uint16_t lap = 1; lap <= 2; lap++) {
        event_t ev = { .type = EV_LAP_COMPLETE, .flags = LAP_F_VALID, .arg16 = lap,
                       .arg32 = 92000u + lap * 137u, .gps_us = req.gps_us + (int64_t)lap * 92000000 };
        xQueueSend(g_evt_q, &ev, pdMS_TO_TICKS(100));
    }
    logger_notify();

    /* stream n synthetic fixes with backpressure (drop-newest ring) */
    long pushed = 0;
    for (long i = 0; i < n; i++) {
        gps_fix_t f;
        synth_fix(&f, (uint32_t)i);
        int spins = 0;
        bool ok = true;
        while (!ring_push(&g_fix_ring, &f)) {   /* full: wake the logger and yield */
            logger_notify();
            vTaskDelay(1);
            if (++spins > 1000) { ok = false; break; }  /* logger stuck (e.g. storage dead): give up */
        }
        if (ok) pushed++;
        if ((i & 0x1F) == 0x1F) logger_notify(); /* nudge every 32 */
    }
    logger_notify();
    printf("logtest: opened a session, queued 2 laps, pushed %ld/%ld fixes; NOT closed.\n", pushed, n);
    printf("  -> watch for \"session Sxxxxx_yyy open\", then pull power to test the cut.\n");
    printf("  -> after reboot: dbg fs ; dbg sum <id> ; dbg logck <id>\n");
    return 0;
}

/* ---------------- dbg fs ---------------- */
static void fs_list_cb(const char *name, uint32_t size, void *ctx)
{
    (void)ctx;
    printf("  %-24s %8u B\n", name, (unsigned)size);
}

static int cmd_fs(void)
{
    uint32_t sf = sys_flags_get();
    printf("storage flags: %s%s%s\n",
           (sf & (1u << SYS_STORAGE_DEAD))     ? "DEAD " : "",
           (sf & (1u << SYS_STORAGE_FULL))     ? "FULL " : "",
           (sf & (1u << SYS_STORAGE_DEGRADED)) ? "DEGRADED " : "");
    sto_info_t si;
    if (sto_info(&si) == 0)
        printf("mount ok    : total=%u KB free=%u KB degraded=%u\n",
               (unsigned)si.total_kb, (unsigned)si.free_kb, (unsigned)si.degraded);
    else
        printf("mount       : NOT mounted (sto_info failed)\n");
    printf("fix_ring drop: %u   fused_ring drop: %u\n",
           (unsigned)ring_dropped(&g_fix_ring), (unsigned)ring_dropped(&g_fused_ring));
    printf("/sessions:\n");
    int cnt = sto_list("/sessions", fs_list_cb, NULL);
    if (cnt < 0) printf("  (cannot list)\n");
    else if (cnt == 0) printf("  (empty)\n");
    return 0;
}

/* ---------------- ses reader tally (shared by sum + logck) ---------------- */
typedef struct {
    int hdr, venue, end;
    int laps, drags, fixes, fused, events, sectors, gates, others;
    ses_hdr_t   h;
    ses_venue_t v;
} tally_t;

static void tally_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    tally_t *t = (tally_t *)ctx;
    switch (type) {
    case SES_T_SESSION_HDR: if (ses_decode_hdr(payload, len, &t->h) == 1) t->hdr++; break;
    case SES_T_VENUE:       if (ses_decode_venue(payload, len, &t->v) == 1) t->venue++; break;
    case SES_T_LAP:         t->laps++; break;
    case SES_T_DRAG_RUN:    t->drags++; break;
    case SES_T_FIX_KEY:     /* fallthrough */
    case SES_T_FIX_DELTA:   t->fixes++; break;
    case SES_T_FUSED:       t->fused++; break;
    case SES_T_EVENT:       t->events++; break;
    case SES_T_SECTOR:      t->sectors++; break;
    case SES_T_DRAG_GATE:   t->gates++; break;
    case SES_T_END:         t->end++; break;
    default:                t->others++; break;
    }
}

static ses_reader_t s_rdr;   /* static: the reader struct is ~0.8 KB, kept off the console stack */

static int read_through_ses(const char *id, const char *ext, tally_t *out)
{
    char path[40];
    (void)snprintf(path, sizeof path, "/sessions/%s%s", id, ext);
    sto_file_t f;
    if (sto_open(path, STO_RD, &f) != 0) { printf("cannot open %s\n", path); return -1; }
    memset(out, 0, sizeof *out);
    ses_reader_init(&s_rdr);
    uint8_t buf[256];
    size_t got;
    for (;;) {
        if (sto_read(f, buf, sizeof buf, &got) != 0) break;
        if (got == 0) break;
        ses_reader_feed(&s_rdr, buf, got, tally_cb, out);
    }
    ses_reader_flush(&s_rdr, tally_cb, out);
    sto_close(f);
    return 0;
}

static int cmd_sum(int argc, char **argv)
{
    if (argc < 3) { printf("usage: dbg sum <id>   (e.g. S00001_001)\n"); return 1; }
    tally_t t;
    if (read_through_ses(argv[2], ".sum", &t) != 0) return 1;
    printf(".sum %s: HDR %s", argv[2], t.hdr ? "ok" : "MISSING");
    if (t.hdr) printf(" (fw=%s hwid=%s)", t.h.fw, t.h.hwid);
    printf("\n  VENUE %s", t.venue ? "ok" : "missing");
    if (t.venue) printf(" (venue_id=%u layout_id=%u)", (unsigned)t.v.venue_id, (unsigned)t.v.layout_id);
    printf("\n  LAP count = %d   DRAG_RUN count = %d\n", t.laps, t.drags);
    printf("  END present = %s\n", t.end ? "yes" : "no (session still open -- expected mid-session)");
    printf("  frames: ok=%u bad=%u\n", (unsigned)s_rdr.frames_ok, (unsigned)s_rdr.frames_bad);
    return 0;
}

static int cmd_logck(int argc, char **argv)
{
    if (argc < 3) { printf("usage: dbg logck <id>   (e.g. S00001_001)\n"); return 1; }
    tally_t t;
    if (read_through_ses(argv[2], ".log", &t) != 0) return 1;
    printf(".log %s: frames ok=%u bad=%u\n", argv[2],
           (unsigned)s_rdr.frames_ok, (unsigned)s_rdr.frames_bad);
    printf("  HDR=%d FIX=%d FUSED=%d LAP=%d SECTOR=%d DRAG_RUN=%d DRAG_GATE=%d EVENT=%d END=%d other=%d\n",
           t.hdr, t.fixes, t.fused, t.laps, t.sectors, t.drags, t.gates, t.events, t.end, t.others);
    if (s_rdr.frames_bad > 0)
        printf("  (bad>0: a truncated/torn tail -- the reader resynced past it, .log stays decodable)\n");
    return 0;
}

static int cmd_dbg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "status") == 0)  return cmd_status();
    if (argc >= 2 && strcmp(argv[1], "logtest") == 0) return cmd_logtest(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "fs") == 0)      return cmd_fs();
    if (argc >= 2 && strcmp(argv[1], "sum") == 0)     return cmd_sum(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "logck") == 0)   return cmd_logck(argc, argv);
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id>\n");
    return 1;
}

void dbg_console_start(int reset_reason)
{
    s_reset_reason = reset_reason;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "laptimer>";
    repl_cfg.task_priority = 2;
    repl_cfg.task_stack_size = 6144;   /* headroom for the sum/logck file readers + printf (3.2 used 4096) */
    repl_cfg.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;

    linenoiseSetDumbMode(1);   /* §18.4: line editing disabled (plain serial terminal) */

    const esp_console_cmd_t cmd = {
        .command = "dbg",
        .help = "diagnostics: status | logtest [n] | fs | sum <id> | logck <id>",
        .hint = NULL,
        .func = cmd_dbg,
    };
    esp_console_cmd_register(&cmd);
    esp_console_start_repl(repl);
}
```

**Task 3 verification (build):** `./build.sh moto_neo6m build` + `size` pass (app image **373,728 B**; clean rebuild shows zero warnings from new code — only the pre-existing relaxed-`jsmn.h` `-Wsign-conversion` notes, §17.9), `./build.sh moto_sim build` passes.

**Hardware-only checks (orchestrator, on the board — the exit criterion):**
1. Flash `moto_neo6m`; at the console run `dbg logtest 2000`; watch for `session Sxxxxx_yyy open`.
2. **Pull power mid-write**, then reboot.
3. `dbg fs` → mount ok, sane `free_kb`, no `SYS_STORAGE_*`, `/sessions` lists the `.log`+`.sum`.
4. `dbg sum <id>` → `HDR ok`, `VENUE ok`, `LAP count = 2`, `END present = no`, `frames ok≥2 bad=0` (the atomic `.sum` is intact).
5. `dbg logck <id>` → `frames ok>0`; `bad` is 0 or 1 (a torn tail the reader resyncs past); per-type shows `HDR=1` + many `FIX` → the `.log` is decodable.
6. `dbg status` → `hb[HB_LOGGER]` (index 2) is incrementing; `counters` shows `sto_format` (first boot on a blank/corrupt `storage` partition triggers the ladder's format path).
7. Sanity: a second `dbg logtest` without reboot opens `S…_002` (per-boot sequence increments); `dbg logtest` while `SYS_STORAGE_DEAD` is a no-op (open fails gracefully).


```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
```

---

## Session 3.4 — sim drivers and the pipeline task (closes #27)

Roadmap exit: laps from simulated data on the console within ±30 ms of `replay`; closes #27; tag `p03-d4`. Serial (2 tasks). Rulings: (1) `gps_sim` replays an embedded synthetic capture (`sim_capture.h`, generated offline by `synth` via `tools/sim/gen_sim_capture.sh` + a new `replay` gensim tool) DIRECTLY from `gps_poll` at real-time rate — NOT through UBX/UART (that is `gps_neo6m`'s path, plan 08); (2) `gps_neo6m`/`imu_mpu6050` are plan-03 build stubs so `moto_neo6m` links until the sensors arrive (plan 08); (3) the root CMake exports `LT_GPS`/`LT_IMU` into the environment so `main`'s REQUIRES can name the driver through IDF's early-expansion pass (a build-flag name isn't readable from the cache there); (4) per-lap stats (§9.4) are accumulated in `on_raw` and written into the completing lap, closing #27; the pipeline hands the full `lap_result_t`/`drag_result_t` and venue to the logger, resolving the 3.3 deferral. `test/data/sim_capture.expected.json` holds `replay`'s reference: out-lap + 3 valid laps 28071/28044/28028 ms. `components/core` stays pure C11.

### Task 1: sim GPS/IMU drivers, HAL contracts, capture generator + CMake wiring

**PARALLEL/SERIAL:** Task 1 and Task 2 are independent (drivers/tools/HAL/CMake vs logger/IPC) and may run in PARALLEL; Task 3 (pipeline) is SERIAL after both. Implement 1 before 3 (gps_sim embeds the capture; the pipeline calls the HAL).

**Files:**
- Create: `components/lt_hal/include/hal/gps.h`, `components/lt_hal/include/hal/imu.h`
- Create: `components/drivers/gps_sim/{gps_sim.c, CMakeLists.txt, sim_capture.h}` (`sim_capture.h` is GENERATED + committed)
- Create: `components/drivers/imu_sim/{imu_sim.c, CMakeLists.txt}`
- Create: `components/drivers/gps_neo6m/{gps_neo6m.c, CMakeLists.txt}` (plan-03 stub), `components/drivers/imu_mpu6050/{imu_mpu6050.c, CMakeLists.txt}` (plan-03 stub)
- Create: `tools/replay/gensim_main.c`, `tools/sim/gen_sim_capture.sh`; commit `test/data/sim_capture.expected.json`
- Edit: top `CMakeLists.txt` (driver dirs + `LT_GPS`/`LT_IMU` env export + `CFG_GPS_SIM`), `main/CMakeLists.txt` (REQUIRES the selected driver), `main/build_config.h.in` (`CFG_GPS_SIM`), `tools/replay/CMakeLists.txt` (build `gensim`)

**Interfaces:**
- Produces: the `hal/gps.h`/`hal/imu.h` contracts (§5.1) the pipeline (T3) calls; `gps_sim`/`imu_sim` HAL implementations; `gps_sim_venue_json()` (the sim venue, consumed by T3); the committed capture + expected lap times.
- Consumes: `core/types.h` (shared `gps_fix_t`/`imu_raw_t`), `esp_timer`, the host `replay`/`synth`/`replaylib` (generator only).

**Rulings (spec is the authority; ESP-IDF v5.3.2 reality noted):**
1. **`gps_sim` replays fixes DIRECTLY, not through UBX.** It returns a committed `gps_fix_t` capture from `gps_poll` when the sim clock reaches each fix's schedule (real-time rate off `esp_timer`); it does NOT parse UBX / feed `gps_feed_bytes` / touch a UART (that path is the real `gps_neo6m`, plan 08, and the §22.3 `gps_sim.py` byte injector). Every other `hal/gps.h` function is a no-op/stub sufficient for the pipeline. There is no `/sim/gps.ubx` file path in 3.4 (no upload path until 3.5); the embedded capture is the only source.
2. **The capture is generated OFFLINE and committed as a C header.** `tools/sim/gen_sim_capture.sh` runs `synth --pos-sigma 0` (no position noise, so the on-device core lap engine reproduces `replay` exactly) then the new host tool `gensim`, which (a) decodes the synth `.log` the way `replay` does (`logio` `on_fix` = the reconstructed absolute fix) into a compact `sim_fix_t[]`, and (b) replays the SAME `.log`+venue through the core lap engine (`replay_run`) to emit `test/data/sim_capture.expected.json`. Device and `replay` therefore feed the identical fixes through the identical engine → laps match to the millisecond (±30 ms is slack for float/Xtensa rounding). Exact synth command: 3 valid laps, 5 Hz, seed 7, 2 sectors, a 6-vertex 700 m circuit (601 fixes / ~120 s / ~64 KB header). Committed expected: lap 0=0 ms, lap 1=28071 ms, lap 2=28044 ms, lap 3=28028 ms (lap 0 is the out-lap, flags OUT_LAP, time 0).
3. **The venue travels as a JSON string, not a C struct.** `gensim` embeds `SIM_VENUE_JSON` (the synth `.venue.json`) in the header; the pipeline parses it with `trk_from_json` — the SAME code path `replay --venue-json` uses — so the device venue == the replay venue by construction (no fragile hand-generated `trk_venue_t` initialiser; the §4.8-budgeted jsmn token buffer is only linked on the sim build). `gps_sim_venue_json()` exposes it to the pipeline.
4. **moto_neo6m builds via plan-03 stub drivers.** moto_neo6m uses GPS=neo6m/IMU=mpu6050, whose real drivers land with the sensors (plan 08). Since the pipeline (in the globbed `app` component) now calls the GPS+IMU HAL, both symbols must resolve on moto_neo6m too. Ruling: provide minimal `gps_neo6m`/`imu_mpu6050` stubs (advertise the profile, pass self-test, return no fix / no FIFO samples) so the pipeline starts and idles on that variant and the REQUIRED `build (moto_neo6m)` check stays green. The exit criterion runs on moto_sim. (Chosen over CMake-gating the pipeline: keeps app_main + the pipeline uniform across variants and exercises the task on the real target.)
5. **The selected driver reaches `main`'s REQUIRES via an environment variable.** A build-flag-derived component name (`gps_${GPS}`) cannot be read from the CMake cache during IDF's early-expansion REQUIRES pass (verified: both `${GPS}` and `$CACHE{GPS}` expand empty → `Failed to resolve component 'gps_'`). The top-level CMake exports `LT_GPS`/`LT_IMU` into the environment *before* `project.cmake` runs the early expansion; the child `cmake -P` inherits it, so `main` REQUIRES `gps_$ENV{LT_GPS} imu_$ENV{LT_IMU}`. Works for `build.sh` and a bare `idf.py -D` alike. The driver dirs are added to `EXTRA_COMPONENT_DIRS` as `components/drivers/gps_${GPS}`/`imu_${IMU}` (cache var is fine in the root's own scope). The pipeline lives in `components/app` (globbed), so no separate component dir is needed for it.
6. **`imu_sim` is deliberately simple.** Lap timing is a function of GPS only (the lap engine ignores its fused argument), so the sim IMU only keeps fusion + the stats accumulator running: upright/still (+1 g on Z, zero gyro), 100 Hz, real device mono stamps. A truth-derived lean/g stream (`synth_fused_at`) is possible but unnecessary for the exit criterion.

- [ ] **Step 1: `components/lt_hal/include/hal/gps.h`** (§5.1 verbatim + guard/includes + `GPS_PM_*`).

```c
/* hal/gps.h -- GPS HAL contract (spec §5.1, called from the pipeline task).
 *
 * The declarations below are the §5.1 gps.h slice verbatim; the include guard, the
 * <stdint.h>/<stddef.h> includes (size_t, fixed-width types), the GPS_PM_* power-mode values
 * and the GPS_FLAG_* fix-flag bits (mirrored from core/types.h so a driver that only sees this
 * header can still set them) are added here so this is a self-contained, compilable header.
 * gps_fix_t / gps_profile_t are the §5.1 structs; gps_fix_t is re-used from core/types.h so the
 * engines and the driver share one definition. All functions return int (0 = OK, negative =
 * -errno-style) unless noted; each is called from one task only (the pipeline) and is not
 * reentrant. The concrete implementation is components/drivers/gps_${GPS} (gps_sim replays a
 * committed synthetic capture; gps_neo6m/gps_m10 parse UBX from the real receiver).
 */
#ifndef HAL_GPS_H
#define HAL_GPS_H

#include <stddef.h>
#include <stdint.h>

#include "core/types.h"   /* gps_fix_t + GPS_FLAG_* (one shared definition) */

/* gps_set_power_mode values (§5.1, §7.5 fault ladder). */
enum {
    GPS_PM_FULL       = 0,   /* continuous navigation */
    GPS_PM_CYCLIC_1HZ = 1,   /* 1 Hz cyclic tracking (power save) */
    GPS_PM_BACKUP     = 2,   /* receiver in backup; gps_wake() resumes */
};

/* GPS receiver profile advertised by gps_init (§5.1). */
typedef struct {
    uint8_t  max_rate_hz;      /* 5 for NEO-6M, 10 for M10 */
    uint32_t baud;             /* 38400 / 115200 */
    uint8_t  has_pps;
    const char *name;
} gps_profile_t;

int  gps_init(const gps_profile_t **out_profile);   /* UART setup, no config push */
int  gps_configure(uint8_t rate_hz);                 /* push full config (§7.3 / §7.4); blocks <= 2 s */
int  gps_poll(gps_fix_t *out);                       /* non-blocking; 1 if a new fix was assembled, 0 if none, <0 error */
int  gps_feed_bytes(const uint8_t *buf, size_t n);   /* pipeline pushes UART bytes; parser runs here */
int  gps_set_power_mode(uint8_t mode);               /* GPS_PM_* */
int  gps_wake(void);                                 /* from BACKUP */
int  gps_get_version(char *buf, size_t n);           /* UBX-MON-VER swVersion; used by self-test */
int  gps_reinit_uart(uint32_t baud);                 /* ladder step */
uint32_t gps_stats_frames_ok(void);
uint32_t gps_stats_frames_bad(void);
int64_t  gps_last_frame_mono_us(void);

#endif /* HAL_GPS_H */
```

- [ ] **Step 2: `components/lt_hal/include/hal/imu.h`** (§5.1 verbatim + guard/includes + `IMU_*`).

```c
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
```

- [ ] **Step 3: `components/drivers/gps_sim/gps_sim.c`** (direct capture replay at real-time rate).

```c
/* gps_sim.c -- hal/gps.h over a committed synthetic capture (spec §4.6 GPS=sim, §22.3).
 *
 * A bench driver so the whole firmware runs before the real GPS arrives. It does NOT parse UBX or
 * touch a UART (that is gps_neo6m/gps_m10, plans 07-08, and the §22.3 gps_sim.py byte injector):
 * it replays the committed SIM_FIXES[] (sim_capture.h) DIRECTLY, one gps_fix_t at a time, pacing
 * them at the capture's real-time rate off esp_timer. The capture was generated by synth with
 * --pos-sigma 0, so the fixes carry no position noise and the on-device lap engine reproduces the
 * committed replay lap times (test/data/sim_capture.expected.json) within the +/-30 ms exit gate.
 *
 * The driver assigns each delivered fix a fresh mono_us (real device time at delivery) and leaves
 * valid = 0 for the pipeline to fill via the §6.5 rule -- exactly as a real driver hands over a
 * freshly assembled fix. gps_sim_venue_json() hands the pipeline the capture's synthetic venue
 * (same JSON string replay used for --venue-json), so device and replay share one venue.
 */
#include "hal/gps.h"
#include "sim_capture.h"

#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const gps_profile_t s_profile = {
    .max_rate_hz = SIM_FIX_RATE_HZ,
    .baud        = 38400,
    .has_pps     = 0,
    .name        = "sim",
};

static uint32_t s_idx;            /* next capture fix to deliver */
static bool     s_started;        /* the playback clock has been anchored */
static int64_t  s_t0_mono_us;     /* device mono time mapped to SIM_FIXES[0].gps_us */
static int64_t  s_last_frame_us;  /* mono time of the last delivered fix */
static uint32_t s_frames_ok;

int gps_init(const gps_profile_t **out_profile)
{
    s_idx = 0;
    s_started = false;
    s_last_frame_us = 0;
    s_frames_ok = 0;
    if (out_profile) *out_profile = &s_profile;
    return 0;
}

int gps_configure(uint8_t rate_hz)
{
    (void)rate_hz;                /* the capture rate is fixed; nothing to push to a receiver */
    s_idx = 0;
    s_started = false;
    return 0;
}

int gps_poll(gps_fix_t *out)
{
    if (!out) return -1;
    if (s_idx >= SIM_FIX_COUNT) return 0;                 /* capture exhausted: idle */

    int64_t now = esp_timer_get_time();
    if (!s_started) { s_started = true; s_t0_mono_us = now; }

    /* Fix s_idx is due once real elapsed time has reached its offset from the first fix. */
    int64_t due_us = SIM_FIXES[s_idx].gps_us - SIM_FIXES[0].gps_us;
    if (now - s_t0_mono_us < due_us) return 0;

    const sim_fix_t *s = &SIM_FIXES[s_idx];
    memset(out, 0, sizeof *out);
    out->gps_us     = s->gps_us;
    out->mono_us    = now;                                /* real arrival time on this board */
    out->lat_e7     = s->lat_e7;
    out->lon_e7     = s->lon_e7;
    out->alt_mm     = s->alt_mm;
    out->gspeed_mms = s->gspeed_mms;
    out->head_e5    = s->head_e5;
    out->hacc_mm    = s->hacc_mm;
    out->sacc_mms   = s->sacc_mms;
    out->pdop_e2    = s->pdop_e2;
    out->fix_type   = s->fix_type;
    out->sats       = s->sats;
    out->flags      = s->flags;
    out->valid      = 0;                                  /* pipeline applies the §6.5 rule */

    s_last_frame_us = now;
    s_idx++;
    s_frames_ok++;
    return 1;
}

/* The sim path never ingests UART bytes; these are no-ops sufficient for the pipeline. */
int gps_feed_bytes(const uint8_t *buf, size_t n) { (void)buf; (void)n; return 0; }
int gps_set_power_mode(uint8_t mode)             { (void)mode; return 0; }
int gps_wake(void)                               { return 0; }
int gps_reinit_uart(uint32_t baud)               { (void)baud; return 0; }

int gps_get_version(char *buf, size_t n)
{
    if (!buf || n == 0) return -1;
    (void)snprintf(buf, n, "sim");
    return 0;
}

uint32_t gps_stats_frames_ok(void)  { return s_frames_ok; }
uint32_t gps_stats_frames_bad(void) { return 0; }
int64_t  gps_last_frame_mono_us(void) { return s_last_frame_us; }

/* The synthetic venue the capture was built around, as a JSON string. The pipeline parses it with
 * trk_from_json -- the same code path replay used for --venue-json -- so both share one venue. */
const char *gps_sim_venue_json(void) { return SIM_VENUE_JSON; }
```

- [ ] **Step 4: `components/drivers/gps_sim/CMakeLists.txt`.**

```cmake
# gps_sim -- hal/gps.h implemented by replaying a committed synthetic capture (spec §4.6 GPS=sim).
# Bench driver, never in a release build. sim_capture.h is generated by tools/sim/gen_sim_capture.sh.
idf_component_register(
    SRCS "gps_sim.c"
    INCLUDE_DIRS "."
    REQUIRES lt_hal core esp_timer)

# build_config.h (CFG_* flags) from the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9); conversion warnings non-fatal for IDF macros.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

- [ ] **Step 5: `components/drivers/imu_sim/imu_sim.c` + `CMakeLists.txt`.**

```c
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

#include <string.h>

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
    if (!out || !n_read) return -1;

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
```

```cmake
# imu_sim -- hal/imu.h synthesising a deterministic 100 Hz stream (spec §4.6 IMU=sim). Bench driver.
idf_component_register(
    SRCS "imu_sim.c"
    REQUIRES lt_hal core)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

- [ ] **Step 6: plan-03 stubs `components/drivers/gps_neo6m/gps_neo6m.c` + `imu_mpu6050/imu_mpu6050.c` (+ their `CMakeLists.txt`, identical shape to the sim ones).**

```c
/* gps_neo6m.c -- PLAN-03 STUB of hal/gps.h for the NEO-6M (spec §4.6 GPS=neo6m, §7.3).
 *
 * The real UBX driver (UART setup, config push, gps_ubx_common parser, fault ladder) lands with the
 * physical sensor in plan 08. Plan 03 only needs moto_neo6m to BUILD green (a REQUIRED CI check) now
 * that the pipeline calls the GPS HAL: this stub satisfies every hal/gps.h symbol, advertises the
 * NEO-6M profile, and simply never produces a fix (gps_poll returns 0), so the pipeline task starts
 * and idles on that variant. The moto_sim bench build (gps_sim) is the one that runs real laps.
 */
#include "hal/gps.h"

#include <stdio.h>

static const gps_profile_t s_profile = {
    .max_rate_hz = 5,
    .baud        = 38400,
    .has_pps     = 0,
    .name        = "neo6m",
};

int gps_init(const gps_profile_t **out_profile)
{
    if (out_profile) *out_profile = &s_profile;
    return 0;
}

int gps_configure(uint8_t rate_hz) { (void)rate_hz; return 0; }
int gps_poll(gps_fix_t *out)       { (void)out; return 0; }          /* no receiver yet (plan 08) */
int gps_feed_bytes(const uint8_t *buf, size_t n) { (void)buf; (void)n; return 0; }
int gps_set_power_mode(uint8_t mode) { (void)mode; return 0; }
int gps_wake(void)                 { return 0; }
int gps_reinit_uart(uint32_t baud) { (void)baud; return 0; }

int gps_get_version(char *buf, size_t n)
{
    if (!buf || n == 0) return -1;
    (void)snprintf(buf, n, "neo6m-stub");
    return 0;
}

uint32_t gps_stats_frames_ok(void)    { return 0; }
uint32_t gps_stats_frames_bad(void)   { return 0; }
int64_t  gps_last_frame_mono_us(void) { return 0; }
```

```c
/* imu_mpu6050.c -- PLAN-03 STUB of hal/imu.h for the MPU-6050 (spec §4.6 IMU=mpu6050, §8).
 *
 * The real I2C driver (WHO_AM_I, register config, FIFO, self-test, motion INT) lands with the
 * physical sensor. Plan 03 only needs moto_neo6m (IMU=mpu6050) to BUILD green now that the pipeline
 * calls the IMU HAL: this stub satisfies every hal/imu.h symbol, passes self-test, and returns no
 * FIFO samples, so the pipeline starts and idles on that variant. moto_sim (imu_sim) runs fusion.
 */
#include "hal/imu.h"

int imu_init(void) { return 0; }

int imu_self_test(uint8_t *pass_mask)
{
    if (pass_mask) *pass_mask = 0x3F;   /* all six axes pass */
    return 0;
}

int imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us)
{
    (void)out; (void)max; (void)read_mono_us;
    if (!n_read) return -1;
    *n_read = 0;                        /* no sensor yet */
    return 0;
}

int imu_read_temp_c100(int16_t *out) { if (out) *out = 2500; return 0; }
int imu_set_mode(uint8_t mode) { (void)mode; return 0; }
int imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms) { (void)thr_lsb; (void)dur_ms; return 0; }
int imu_recover(void)   { return 0; }
int imu_int_pending(void) { return 0; }
```

- [ ] **Step 7: host generator `tools/replay/gensim_main.c`** (decode .log fixes + replay for expected).

```c
/* gensim -- generate the committed gps_sim capture header + its replay-expected lap times.
 *
 * usage: gensim <in.log> <in.venue.json> <out sim_capture.h> <out expected.json>
 *
 * Reads a synthetic session .log (produced by `synth --pos-sigma 0`, so the fixes carry no
 * position noise and the on-device lap engine reproduces `replay` exactly) plus its venue side-car.
 * It emits two committed artifacts:
 *
 *   sim_capture.h       the FIX records decoded byte-for-byte the way `replay` decodes them
 *                       (logio on_fix = the reconstructed absolute fix), as a compact sim_fix_t[]
 *                       the gps_sim driver replays at real time, plus the venue JSON string the
 *                       pipeline feeds to trk_from_json (identical code path to replay's
 *                       --venue-json, so the device venue == the replay venue by construction).
 *   expected.json       `replay --mode lap` over the SAME .log + venue: the reference lap times
 *                       the orchestrator compares the on-device console laps against (+/-30 ms).
 *
 * Deterministic: same synth seed + same build => identical bytes. The device feeds the SAME
 * decoded fixes and the SAME venue through the SAME core engine, so device laps == replay laps.
 */
#include "replay/replay.h"
#include "replay/logio.h"
#include "core/trk.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIX 8192

static gps_fix_t g_fix[MAX_FIX];
static uint32_t  g_nfix;

static void on_fix(const gps_fix_t *fix, void *ctx)
{
    (void)ctx;
    if (g_nfix < MAX_FIX) g_fix[g_nfix++] = *fix;
}

/* Read a whole text file into a heap buffer (NUL-terminated). */
static char *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

/* Emit `s` as a C string literal, minifying JSON whitespace so the embedded venue is compact. */
static void emit_json_string(FILE *h, const char *s)
{
    int in_str = 0;
    fputc('"', h);
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (!in_str && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) continue;
        if (c == '"') { in_str = !in_str; fputs("\\\"", h); continue; }
        if (c == '\\') { fputs("\\\\", h); continue; }
        fputc(c, h);
    }
    fputc('"', h);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <in.log> <in.venue.json> <out.h> <out.expected.json>\n", argv[0]);
        return 2;
    }
    const char *log_path = argv[1], *venue_path = argv[2], *h_path = argv[3], *exp_path = argv[4];

    /* venue: parse exactly as replay does (trk_from_json), and keep the raw JSON to embed. */
    size_t vlen = 0;
    char *vjson = slurp(venue_path, &vlen);
    if (!vjson) { fprintf(stderr, "gensim: cannot read %s\n", venue_path); return 1; }
    trk_venue_t venue;
    char verr[160] = {0};
    if (trk_from_json(&venue, vjson, vlen, verr, sizeof verr) != 0) {
        fprintf(stderr, "gensim: bad venue json: %s\n", verr);
        free(vjson);
        return 1;
    }

    /* fixes: decode the .log exactly as replay does (reconstructed absolute fixes). */
    g_nfix = 0;
    static const logr_cb_t cb = { .on_fix = on_fix };   /* all other members NULL */
    logr_t r;
    logr_init(&r, &cb, NULL);
    if (logr_read_file(&r, log_path) != 0) {
        fprintf(stderr, "gensim: cannot read %s\n", log_path);
        free(vjson);
        return 1;
    }
    if (g_nfix < 2) { fprintf(stderr, "gensim: %s has too few fixes\n", log_path); free(vjson); return 1; }

    /* fix rate from the median-ish first spacing (uniform for a synth capture). */
    int64_t dt_us = g_fix[1].gps_us - g_fix[0].gps_us;
    int rate_hz = (dt_us > 0) ? (int)((1000000 + dt_us / 2) / dt_us) : 5;

    /* expected lap times: replay the SAME .log + venue through the core lap engine. */
    replay_run_t *rr = (replay_run_t *)malloc(sizeof *rr);
    if (!rr) { free(vjson); return 1; }
    if (replay_run(log_path, REPLAY_MODE_LAP, &venue, rr) != 0) {
        fprintf(stderr, "gensim: replay_run failed\n");
        free(rr); free(vjson);
        return 1;
    }
    FILE *ef = fopen(exp_path, "w");
    if (!ef) { fprintf(stderr, "gensim: cannot write %s\n", exp_path); free(rr); free(vjson); return 1; }
    replay_print_run_json(rr, ef);
    fclose(ef);

    /* header */
    FILE *h = fopen(h_path, "w");
    if (!h) { fprintf(stderr, "gensim: cannot write %s\n", h_path); free(rr); free(vjson); return 1; }
    fprintf(h,
        "/* sim_capture.h -- GENERATED, DO NOT EDIT. Committed synthetic GPS capture for gps_sim.\n"
        " *\n"
        " * Regenerate with tools/sim/gen_sim_capture.sh (see that script for the exact synth\n"
        " * command and seed). %u fixes decoded byte-for-byte from the synth .log the way replay\n"
        " * decodes them; the pipeline feeds SIM_VENUE_JSON to trk_from_json (identical to replay's\n"
        " * --venue-json path), so the on-device laps reproduce test/data/sim_capture.expected.json.\n"
        " */\n"
        "#ifndef GPS_SIM_CAPTURE_H\n"
        "#define GPS_SIM_CAPTURE_H\n"
        "#include <stdint.h>\n\n"
        "#define SIM_FIX_COUNT   %uu\n"
        "#define SIM_FIX_RATE_HZ %d\n\n"
        "/* Compact fix: everything the pipeline validity rule (§6.5) and the engines read. mono_us is\n"
        " * assigned by the driver at delivery (real device time), so it is not stored here. */\n"
        "typedef struct {\n"
        "    int64_t  gps_us;\n"
        "    int32_t  lat_e7, lon_e7, alt_mm, gspeed_mms, head_e5;\n"
        "    uint32_t hacc_mm, sacc_mms;\n"
        "    uint16_t pdop_e2;\n"
        "    uint8_t  fix_type, sats, flags;\n"
        "} sim_fix_t;\n\n"
        "static const sim_fix_t SIM_FIXES[SIM_FIX_COUNT] = {\n",
        g_nfix, g_nfix, rate_hz);

    for (uint32_t i = 0; i < g_nfix; i++) {
        const gps_fix_t *f = &g_fix[i];
        fprintf(h,
            "    { %lldLL, %d, %d, %d, %d, %d, %uu, %uu, %uu, %u, %u, 0x%02Xu },\n",
            (long long)f->gps_us, f->lat_e7, f->lon_e7, f->alt_mm, f->gspeed_mms, f->head_e5,
            f->hacc_mm, f->sacc_mms, (unsigned)f->pdop_e2, (unsigned)f->fix_type,
            (unsigned)f->sats, (unsigned)f->flags);
    }
    fputs("};\n\n", h);

    fputs("/* Venue as JSON; the pipeline parses it with trk_from_json (same as replay --venue-json). */\n", h);
    fputs("static const char SIM_VENUE_JSON[] =\n    ", h);
    emit_json_string(h, vjson);
    fputs(";\n\n#endif /* GPS_SIM_CAPTURE_H */\n", h);
    fclose(h);

    printf("gensim: wrote %s (%u fixes) and %s\n", h_path, g_nfix, exp_path);
    free(rr);
    free(vjson);
    return 0;
}
```

- [ ] **Step 8: `tools/replay/CMakeLists.txt`** — add `gensim` to the tool loop:

```cmake
foreach(tool synth replay gensim)
```

- [ ] **Step 9: `tools/sim/gen_sim_capture.sh`** (frozen synth command; regenerates both committed artifacts).

```bash
#!/usr/bin/env bash
# Regenerate the committed gps_sim capture + its replay-expected lap times (spec §4.6, §22.2/§22.3).
#
# usage: gen_sim_capture.sh [synth] [replay-build-dir]
#   synth             path to the built synth binary   (default build/tools/replay/synth)
#   gensim            path to the built gensim binary  (default build/tools/replay/gensim)
#
# This is a MANUAL maintenance action, not a CI step (same as tools/replay/gen_fixtures.sh). The
# committed artifacts are the on-device exit-criterion baseline:
#   components/drivers/gps_sim/sim_capture.h   the fixes gps_sim replays + the venue JSON
#   test/data/sim_capture.expected.json        the replay lap times to compare against (+/-30 ms)
#
# The capture is deliberately small: a 6-vertex circuit, 3 valid laps at 5 Hz, --pos-sigma 0 so the
# fixes carry NO position noise and the on-device core lap engine reproduces `replay` exactly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

SYNTH="${1:-build/tools/replay/synth}"
GENSIM="${2:-build/tools/replay/gensim}"
[ -x "$SYNTH" ]  || { echo "synth not found at $SYNTH  (build the host tools first)"  >&2; exit 1; }
[ -x "$GENSIM" ] || { echo "gensim not found at $GENSIM (build the host tools first)" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The exact, frozen capture definition. Change it here and rerun to regenerate both artifacts.
"$SYNTH" --out "$TMP/sim" \
    --laps 4 --rate 5 --pos-sigma 0 --seed 7 --sectors 2 \
    --length 700 --v-corner 20 --v-max 45 --a-acc 6 --a-brk 10 \
    --vertices 6 --radius 22 --start-before 120 --stop-after 80 --quiet

"$GENSIM" "$TMP/sim.log" "$TMP/sim.venue.json" \
    "$ROOT/components/drivers/gps_sim/sim_capture.h" \
    "$ROOT/test/data/sim_capture.expected.json"

echo "regenerated:"
echo "  components/drivers/gps_sim/sim_capture.h"
echo "  test/data/sim_capture.expected.json"
```

- [ ] **Step 10: generate + commit `components/drivers/gps_sim/sim_capture.h`** (GENERATED — run `tools/sim/gen_sim_capture.sh` after building the host tools; do not hand-edit). Head:

```c
/* sim_capture.h -- GENERATED, DO NOT EDIT. Committed synthetic GPS capture for gps_sim.
 *
 * Regenerate with tools/sim/gen_sim_capture.sh (see that script for the exact synth
 * command and seed). 601 fixes decoded byte-for-byte from the synth .log the way replay
 * decodes them; the pipeline feeds SIM_VENUE_JSON to trk_from_json (identical to replay's
 * --venue-json path), so the on-device laps reproduce test/data/sim_capture.expected.json.
 */
#ifndef GPS_SIM_CAPTURE_H
#define GPS_SIM_CAPTURE_H
#include <stdint.h>

#define SIM_FIX_COUNT   601u
#define SIM_FIX_RATE_HZ 5

/* Compact fix: everything the pipeline validity rule (§6.5) and the engines read. mono_us is
 * assigned by the driver at delivery (real device time), so it is not stored here. */
typedef struct {
    int64_t  gps_us;
    int32_t  lat_e7, lon_e7, alt_mm, gspeed_mms, head_e5;
    uint32_t hacc_mm, sacc_mms;
    uint16_t pdop_e2;
    uint8_t  fix_type, sats, flags;
} sim_fix_t;

static const sim_fix_t SIM_FIXES[SIM_FIX_COUNT] = {
    { 1789466400000000LL, -340292915, 187292435, 100000, 26750, 5981219, 1500u, 50u, 180u, 3, 9, 0x07u },
    /* ... 601 SIM_FIXES rows ... */
};

static const char SIM_VENUE_JSON[] =
    "{\"id\":1000,\"name\":\"Synthetic\", ... ,\"layouts\":[{...\"sf\":[...],\"sectors\":[...]}]}";
#endif /* GPS_SIM_CAPTURE_H */
```

- [ ] **Step 11: top-level `CMakeLists.txt`** — derive `CFG_GPS_SIM`, append the driver dirs, export the env vars:

```cmake
# gps_sim bench build marker: the pipeline pulls its venue from gps_sim only here (§4.6, 3.4).
if(GPS STREQUAL "sim")
    set(CFG_GPS_SIM 1)
else()
    set(CFG_GPS_SIM 0)
endif()
# ... EXTRA_COMPONENT_DIRS gains: components/drivers/gps_${GPS} components/drivers/imu_${IMU}
set(ENV{LT_GPS} "${GPS}")
set(ENV{LT_IMU} "${IMU}")
```

- [ ] **Step 12: `main/CMakeLists.txt`** REQUIRES `... gps_$ENV{LT_GPS} imu_$ENV{LT_IMU}`; **`main/build_config.h.in`** adds `#define CFG_GPS_SIM @CFG_GPS_SIM@`.

**Verify (all in `SCRATCH/draft34`, IDF v5.3.2):**
- Host tools: `cmake --build test/build-host --target synth replay gensim` → OK; `bash tools/sim/gen_sim_capture.sh …` regenerates `sim_capture.h` + `sim_capture.expected.json` BYTE-IDENTICAL (deterministic).
- `./build.sh moto_sim build` → green; `./build.sh moto_neo6m build` → green (stub drivers).
- Committed expected lap times (from `replay`): .

---

### Task 2: full lap/drag results to the logger (resolves the 3.3 deferral) + IPC channels

**PARALLEL/SERIAL:** Independent of Task 1 → PARALLEL. Task 3 (pipeline) is SERIAL after this (it is the producer for `result_q`/`cmd_q` and calls the new logger API).

**Files:**
- Edit: `components/app/include/app/lt_ipc.h` (`result_q` + `cmd_q` + `command_t`), `components/app/sys/lt_ipc.c` (create both queues)
- Edit: `components/app/include/app/logger.h` (submit API), `components/app/logger/logger.c` (result path + drop minimal-LAP synthesis)

**Interfaces:**
- Produces: `logger_submit_lap`/`logger_submit_drag`/`logger_set_venue`; the `g_result_q`/`g_cmd_q` channels + `command_t` (§4.5). Consumed by the pipeline (T3).
- Consumes: `core/ses` codecs (`ses_encode_lap/sector/drag_run/drag_gate/venue`), `hal/storage.h`, `core/types.h`.

**Rulings:**
1. **A queue, not `log_request_t`.** `log_request_t` is frozen at 16 B (`_Static_assert`); a full `lap_result_t`/`drag_result_t` cannot ride it. The pipeline (core 1) → logger (core 0) hand-off uses a new depth-4 FreeRTOS queue `result_q` of a tagged `log_result_t` (copy-by-value, cross-core safe, static storage). One queue with a `{LAP|DRAG|VENUE}` union keeps it small (~234 B/slot).
2. **Full records come from `result_q`; events stay verbatim.** `handle_event` no longer synthesises a minimal LAP/DRAG_RUN from `EV_LAP_COMPLETE`/`EV_DRAG_DONE` — every event (those included) is logged as a generic EVENT record (§12.3). The authoritative LAP (sectors + per-lap stats §9.4) and DRAG_RUN (gates) arrive over `result_q`; the logger additionally writes a SECTOR record per gate (crossing gps_us reconstructed from the cumulative splits — exact, since the splits ARE the crossing differences; delta left 0) and a DRAG_GATE per hit gate. `.sum` stays HDR + VENUE + every LAP + every DRAG_RUN [+ END] (§12.5); SECTOR/DRAG_GATE are .log-only. The 3.3 power-cut exit path (atomic `.sum` rename; resync-past-truncated `.log`) is unchanged.
3. **Real VENUE record.** `logger_set_venue(id, layout, name)` (a third `result_q` kind) fills `s_venue_name`/id/layout and marks `.sum` dirty, so the pipeline's venue name lands in the VENUE record instead of the empty string 3.3 left.
4. **`cmd_q` + `command_t` created here, drained by the pipeline (T3).** The §4.5 `command_t` and `CMD_*`/`MODE_*` enums live in `lt_ipc.h` (the IPC-channel header); the full command protocol/transport is `components/app/cmd` (3.5). The channel exists before its producer (ui/conn/power, later).

- [ ] **Step 1: `components/app/include/app/lt_ipc.h`** — add after `g_log_req_q`:

```c
/* result_q -- pipeline -> logger, full engine results (depth 4). The 3.3 logger could only build a
 * minimal LAP/DRAG_RUN from the EV_LAP_COMPLETE/EV_DRAG_DONE payload (§4.5); 3.4 hands it the whole
 * lap_result_t / drag_result_t (sectors + per-lap stats §9.4, gates) plus the real venue for the
 * VENUE record, so the logger writes complete LAP/SECTOR and DRAG_RUN/DRAG_GATE records (§12.3). A
 * queue (copy-by-value, cross-core safe) rather than log_request_t, which is frozen at 16 B. */
#define RESULT_Q_DEPTH 4

typedef enum {
    LOG_RES_LAP   = 0,   /* u.lap  -> LAP (+ SECTOR) records */
    LOG_RES_DRAG  = 1,   /* u.drag -> DRAG_RUN (+ DRAG_GATE) records */
    LOG_RES_VENUE = 2,   /* u.venue -> the .sum VENUE record's id/layout/name */
} log_result_kind_t;

typedef struct {
    uint8_t kind;        /* log_result_kind_t */
    union {
        lap_result_t  lap;
        drag_result_t drag;
        struct { uint16_t venue_id, layout_id; char name[24]; } venue;
    } u;
} log_result_t;

extern QueueHandle_t g_result_q;

/* §4.4 cmd_q -- ui/conn/power -> pipeline (depth 8, command_t 24 B). The producers (ui/conn/power)
 * land in later sessions; 3.4 creates the queue and the pipeline drains it (CMD_SET_MODE /
 * CMD_SET_LAYOUT / CMD_RESET_ENGINE at least). command_t is the §4.5 struct; the full command
 * protocol + transport is components/app/cmd (3.5). */
#define CMD_Q_DEPTH 8

typedef struct {
    uint8_t type;        /* command_type_t */
    uint8_t arg8;
    uint16_t arg16;
    int32_t arg32;
    double  lat;
    double  lon;
} command_t;

typedef enum {
    CMD_SET_MODE      = 0,   /* arg8 = MODE_LAP / MODE_DRAG */
    CMD_SET_LAYOUT    = 1,   /* arg16 = layout id */
    CMD_MARK_GATE     = 2,   /* arg8 = 0 S/F, n sector n */
    CMD_CALIB_ORIENT  = 3,
    CMD_RESET_ENGINE  = 4,
    CMD_CONFIG_RELOAD = 5,
    CMD_GPS_POWER     = 6,   /* arg8 = 0/1 */
    CMD_IMU_MODE      = 7,   /* arg8 = IMU_FULL / IMU_LOWPOWER */
} command_type_t;

enum { MODE_LAP = 0, MODE_DRAG = 1 };   /* CMD_SET_MODE arg8 (matches core CFG_MODE_*) */

extern QueueHandle_t g_cmd_q;

/* Create the rings + queues. Idempotent; call once at boot step 11 (§4.7). */
void lt_ipc_init(void);
```

- [ ] **Step 2: `components/app/sys/lt_ipc.c`** — define + create the two queues (static storage), e.g.:

```c
QueueHandle_t g_result_q;
static StaticQueue_t s_result_ctrl;
static uint8_t       s_result_store[RESULT_Q_DEPTH * sizeof(log_result_t)];
QueueHandle_t g_cmd_q;
static StaticQueue_t s_cmd_ctrl;
static uint8_t       s_cmd_store[CMD_Q_DEPTH * sizeof(command_t)];
/* in lt_ipc_init(): */
    if (!g_result_q)
        g_result_q = xQueueCreateStatic(RESULT_Q_DEPTH, sizeof(log_result_t), s_result_store, &s_result_ctrl);
    if (!g_cmd_q)
        g_cmd_q = xQueueCreateStatic(CMD_Q_DEPTH, sizeof(command_t), s_cmd_store, &s_cmd_ctrl);
```

- [ ] **Step 3: `components/app/include/app/logger.h`** — the submit API:

```c
/* Pipeline -> logger, full engine results (3.4, resolving the 3.3 deferral). Each copies the result
 * onto result_q (cross-core safe) and wakes the logger; the logger writes the complete LAP (+ SECTOR)
 * / DRAG_RUN (+ DRAG_GATE) records (§12.3) and the real VENUE record. Safe from the pipeline task;
 * drop silently if the queue is momentarily full (the .log keeps the EVENT record either way). */
void logger_submit_lap(const lap_result_t *lap);
void logger_submit_drag(const drag_result_t *run);
void logger_set_venue(uint16_t venue_id, uint16_t layout_id, const char *name);
```

- [ ] **Step 4: `components/app/logger/logger.c`** — replace `handle_event`'s LAP/DRAG synthesis with a generic EVENT record, and add `handle_result` + `drain_results` (called in the loop after `drain_events`):

```c
static void handle_event(const event_t *ev)
{
    /* Every event -- EV_LAP_COMPLETE / EV_DRAG_DONE included -- is logged verbatim as a generic
     * EVENT record (§12.3). The authoritative full LAP / DRAG_RUN records now arrive over result_q
     * (handle_result), so the logger no longer synthesises a minimal one from the event payload. */
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_event(ev->mono_us, ev->gps_us, ev->type, ev->arg32, tmp, sizeof tmp);
    batch_append(tmp, n);
}

/* §12.3 SECTOR / DRAG_GATE / real VENUE from the pipeline's full engine result (result_q). */
static void handle_result(const log_result_t *res)
{
    if (!s_open) return;                             /* nothing to write without an open .log */
    uint8_t tmp[FRAME_TMP_CAP];
    int n;

    if (res->kind == LOG_RES_LAP) {
        const lap_result_t *lap = &res->u.lap;
        n = ses_encode_lap(lap, tmp, sizeof tmp);    /* full record: sectors + per-lap stats §9.4 */
        batch_append(tmp, n);
        acc_append(s_lap_acc, &s_lap_len, LAP_ACC_CAP, tmp, n);   /* .sum carries every LAP (§12.5) */
        /* One SECTOR record per sector gate: crossing gps_us reconstructed from the cumulative
         * splits (exact -- the splits are the crossing differences), delta left 0. */
        int64_t cum_us = lap->start_gps_us;
        uint8_t gates = (lap->n_sectors > 0) ? (uint8_t)(lap->n_sectors - 1u) : 0u;
        for (uint8_t j = 0; j < gates && j < LAP_MAX_SECTORS; j++) {
            cum_us += (int64_t)lap->sector_ms[j] * 1000;
            n = ses_encode_sector(lap->lap_no, (uint8_t)(j + 1u), cum_us, lap->sector_ms[j], 0,
                                  tmp, sizeof tmp);
            batch_append(tmp, n);                    /* SECTOR is .log-only */
        }
        s_sum_dirty = true;
    } else if (res->kind == LOG_RES_DRAG) {
        const drag_result_t *run = &res->u.drag;
        n = ses_encode_drag_run(run, tmp, sizeof tmp);
        batch_append(tmp, n);
        acc_append(s_drag_acc, &s_drag_len, DRAG_ACC_CAP, tmp, n);
        for (uint8_t i = 0; i < run->n_gates && i < DRAG_MAX_GATES; i++) {
            const drag_gate_res_t *g = &run->gates[i];
            if (!g->hit) continue;
            int64_t g_gps_us = run->t0_gps_us + (int64_t)g->time_ms * 1000;
            n = ses_encode_drag_gate(run->run_no, g->gate_id, g_gps_us, g->time_ms,
                                     g->speed_cms, g->dist_cm, tmp, sizeof tmp);
            batch_append(tmp, n);                    /* DRAG_GATE is .log-only */
        }
        s_sum_dirty = true;
    } else if (res->kind == LOG_RES_VENUE) {
        s_venue_id  = res->u.venue.venue_id;
        s_layout_id = res->u.venue.layout_id;
        (void)snprintf(s_venue_name, sizeof s_venue_name, "%s", res->u.venue.name);
        s_hdr.venue_id  = s_venue_id;                /* keep the .sum HDR consistent */
        s_hdr.layout_id = s_layout_id;
        s_sum_dirty = true;                          /* rebuild .sum with the real VENUE record */
    }
}

static void drain_results(void)
    log_result_t res;
    while (xQueueReceive(g_result_q, &res, 0) == pdTRUE) handle_result(&res);
}
```

- [ ] **Step 5: `components/app/logger/logger.c`** — the submit functions (near `logger_notify`):

```c
static void submit(const log_result_t *r)
{
    if (!g_result_q) return;
    if (xQueueSend(g_result_q, r, 0) == pdTRUE) logger_notify();
}
void logger_submit_lap(const lap_result_t *lap)
{
    if (!lap) return;
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_LAP;
    r.u.lap = *lap;
    submit(&r);
}
void logger_submit_drag(const drag_result_t *run)
{
    if (!run) return;
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_DRAG;
    r.u.drag = *run;
    submit(&r);
}
void logger_set_venue(uint16_t venue_id, uint16_t layout_id, const char *name)
{
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_VENUE;
    r.u.venue.venue_id = venue_id;
    r.u.venue.layout_id = layout_id;
    if (name) (void)snprintf(r.u.venue.name, sizeof r.u.venue.name, "%s", name);
    submit(&r);
}
```

Also: in `logger_task`'s loop add `drain_results();` right after `drain_events();`. The `dbg logtest` verb is updated in Task 3 (alongside `dbg laps`) to push a full `lap_result_t` via `logger_submit_lap` + `logger_set_venue` so `.sum` still carries real LAP/VENUE records.

**Verify:** `./build.sh moto_sim build` and `./build.sh moto_neo6m build` green (logger changes are variant-independent).

---

## Session 3.5 — serial export console, RTC continuity, crash-loop safe mode (closes #6)

Roadmap exit: `dbg crash` mid-lap → the lap resumes flagged interrupted; three abnormal resets within 60 s → safe mode; tag `p03-d5`. Spec §18.4 (serial fallback), §18.1 (command protocol / `app/cmd`), §15.3 (RTC memory), §17.5 (crash-loop safe mode), §4.7 steps 3-4, §17.4 sys_flags. Closes #6.

**Recon ruling (what already exists — 3.5 is plumbing, not new mechanism).** The engine and NVS layers already carry most of the mechanism; 3.5 wires them up:
- `core/lap.h` has full resume support (`lap_rtc_t`, `lap_export_rtc`/`lap_import_rtc`, `LAP_F_INTERRUPTED`), added and unit-tested in session 2.5 (`test/test_lap.c:686-730`). Nothing calls it yet. `lap_import_rtc` sets `state=RUNNING`, `flags=LAP_F_INTERRUPTED`, restores lap_no/lap_start/gate_times/best/prev, and returns -1 iff the venue id is unknown.
- `lt_rtc.c` defines `rtc_state_t` matching §15.3 field-for-field with CRC32 implemented, but has **no writer** (only `lt_rtc_validate`/`lt_rtc_clear`); constant is named `RTC_STATE_MAGIC` (= spec `RTC_MAGIC`, 0x4C505452).
- `lt_nvs.c` already implements crash-loop detection (`lt_crashlog_push`, `lt_crashlog_is_loop` — abnormal×3 within a hardcoded 60 s) and a one-shot safe gate (`lt_safe_until_get/set`); `app_main.c` already sets `SYS_SAFE_MODE` (bit 9) at boot. Missing: named constants, an uptime-based auto-clear, and any runtime safe-mode behavior.
- `dbg_console.c` is a single flat `dbg` command (verbs `status/logtest/fs/sum/logck/laps`, argv dispatch). The §18.4 export console and `components/app/cmd` do not exist. The `EXPORT_SERIAL` CMake flag exists (default ON) but no component consumes it.
- Core JSON/export codecs exist and are pure C11: `core/cfg.h` (`cfg_to_json`/`cfg_from_json`), `core/exp.h` (`exp_json_*`/`exp_vbo_*`/`exp_nmea_*` streaming from decoded `ses` frames), `core/ses` reader, `core/json.h`.

**Freshness-gate ruling (§15.3 / §4.7 step 4).** `saved_gps_us` is a GPS-domain timestamp and `mono_us` resets to 0 on every reset, so the "younger than `RTC_RESUME_MAX_S`" test cannot run at boot (no clock yet). The pipeline defers it: at init it loads the validated pending `rtc_state_t`; on the **first valid fix** it resumes iff `fix.gps_us − saved_gps_us < RTC_RESUME_MAX_S·1e6`, else clears. This matches the `lap.c:252` comment ("the RTC_RESUME_MAX_S freshness gate is the pipeline's job").

**Safe-mode behavior ruling (§17.5).** In safe mode the pipeline and both engines still run and summaries are still written; only **sample logging is suppressed** (the logger drops FIX/FUSED sample records but keeps SESSION_HDR, VENUE, LAP, DRAG_RUN, END). BLE/WiFi and the "SAFE MODE" e-paper render are later plans (no radio/display component exists yet) — noted, not built here. `dbg status` already surfaces the `SAFE` tag.

**Auto-clear ruling (§17.5).** Keep the existing boot-gate that carries safe mode into the crash-loop-triggered boot, and add the spec's uptime clear: once this boot reaches `SAFE_MODE_CLEAR_S` of uptime, the supervisor clears the persisted safe gate and `SYS_SAFE_MODE` so the next boot is normal.

**Execution structure (PARALLEL).** Serial scaffold → parallel disjoint-file wave in worktrees → serial integration:
- **Task 1 (serial scaffold):** `lt_consts.h` — the four Appendix-A firmware policy constants. Tiny; unblocks the rest.
- **Parallel wave (worktrees), disjoint file sets:** **Task 2** RTC continuity (`lt_rtc.*`, `pipeline.c`) · **Task 3** safe-mode behavior (`lt_nvs.*`, `sup.c`, `logger.c`) · **Task 6** idf-env.sh guard (`tools/idf-env.sh`).
- **Serial integration:** **Task 4** console core + `dbg crash` (`components/app/cmd/**`, `components/drivers/export_serial/**`, `app_main.c`, CMake) then **Task 5** console file streaming (`open/read/list` over `core/exp` + `ses`). Task 4 removes `dbg_console.c`; it runs after the parallel wave integrates, so it never races Task 2/3.

The on-hardware exit test needs Task 4's `dbg crash` + `dbg status`; the file-streaming console (Task 5) is roadmap scope verified over serial with `tools/serial_export.py`.

### Task 1: firmware policy constants (`lt_consts.h`)

**Files:** Create `components/app/include/app/lt_consts.h`. Modify nothing else (Task 3 swaps the `lt_nvs.c` literals).

**Interfaces produced:** the four constants, consumed by Tasks 2 (RTC_RESUME_MAX_S) and 3 (crash-loop trio).

- [ ] **Step 1** — create the header (values from spec Appendix A, §15.3/§17.5):

```c
/* lt_consts.h -- firmware policy constants (spec Appendix A). Engine/geometry constants stay in
 * core/consts.h; these are app/firmware policy (RTC resume freshness, crash-loop, safe mode). */
#ifndef APP_LT_CONSTS_H
#define APP_LT_CONSTS_H

#define RTC_RESUME_MAX_S     14400   /* §15.3: resume an interrupted session only if the first fix is
                                        within this many seconds of the saved gps time */
#define CRASH_LOOP_N         3       /* §17.5: this many consecutive abnormal resets ... */
#define CRASH_LOOP_WINDOW_S  60      /*        ... each with uptime below this -> safe mode */
#define SAFE_MODE_CLEAR_S    600     /* §17.5: uptime in safe mode after which the supervisor clears it */

#endif /* APP_LT_CONSTS_H */
```

- [ ] **Step 2** — `git commit` (`feat(fw): lt_consts.h firmware policy constants (§Appendix A)`). No build change to verify beyond a header-only compile; the scaffold is verified when a consumer builds in Task 2/3.

### Task 2: RTC continuity — save on every gate, resume the interrupted lap (§15.3)

**Files:** Modify `components/app/sys/lt_rtc.c`, `components/app/include/app/lt_rtc.h`, `components/app/pipeline/pipeline.c`. (Disjoint from Tasks 3/6.)

**Interfaces consumed:** `RTC_RESUME_MAX_S` (Task 1); `lap_export_rtc`/`lap_import_rtc`/`lap_rtc_t`/`LAP_F_INTERRUPTED` (core/lap.h, existing); `lt_rtc_validate`/`rtc_state_t`/`RTC_STATE_MAGIC` (lt_rtc.h, existing).

**Interfaces produced (add to `lt_rtc.h`):**
```c
/* Populate and CRC the RTC state from the current lap-engine snapshot + session identity, then
 * store it in RTC_DATA_ATTR memory (survives reset/deep-sleep). Called by the pipeline on every
 * S/F and sector event and before a supervised restart. */
void lt_rtc_save(const lap_rtc_t *lr, const char *session_id, int64_t saved_gps_us,
                 uint8_t mode, uint8_t power_state, uint32_t partial_count);
```
(`lt_rtc.h` must include `core/lap.h` for `lap_rtc_t`, or forward-declare + include in the .c — pick include, it is already an app-layer header.)

- [ ] **Step 1** — write the failing host-adjacent reasoning into a target test is not practical here (RTC_DATA_ATTR + pipeline are on-target); the acceptance is the on-hardware exit test. Instead, add `lt_rtc_save` and verify the round-trip **structurally**: `lt_rtc_save(...)` then `lt_rtc_validate(&out)` returns `RTC_VALID` and `out` fields equal what was saved (CRC passes). Prove this in `dbg` (Task 4 adds `dbg rtc`) — for Task 2 the reviewer checks the save populates every field the resume path reads.
- [ ] **Step 2** — implement `lt_rtc_save`: memcpy the `lap_rtc_t` fields (venue_id, layout_id, lap_no, sector_idx, mode, lap_start_gps_us, gate_times[], best, prev) into `s_rtc`; set `magic=RTC_STATE_MAGIC`, `version=RTC_STATE_VERSION`, `session_id` (bounded copy, 10 bytes), `saved_gps_us`, `session_epoch_mono_us` (keep existing if set), `power_state`, `partial_count`, `_pad*=0`; compute `crc32` over all bytes except `crc32` (reuse the file's `rtc_crc()`).
- [ ] **Step 3** — pipeline init resume-arm: include `app/lt_rtc.h` + `app/lt_consts.h`. In `pipeline_start`/task init, after `lt_rtc_validate(&s_resume)`: if `RTC_VALID`, keep `s_resume` and set `s_resume_pending=true`; else `lt_rtc_clear()` and `s_resume_pending=false`. Do **not** import yet (no clock).
- [ ] **Step 4** — pipeline resume-on-first-fix: in `on_fix`, when the fix is valid and `s_resume_pending`, gate on freshness `fix->gps_us - s_resume.saved_gps_us < (int64_t)RTC_RESUME_MAX_S * 1000000`; if fresh, build a `lap_rtc_t` from `s_resume` and call `lap_import_rtc(&s_lap, &lr)` (on -1, i.e. unknown venue, fall back to the cold `lap_set_venue` path already there); emit `EV_LAP_COMPLETE`? no — emit nothing; the resumed lap continues and completes normally later carrying `LAP_F_INTERRUPTED`. Clear `s_resume_pending` either way; if stale, `lt_rtc_clear()`. Guard so this runs once and only in lap mode.
- [ ] **Step 5** — pipeline save-on-gate: on `EV_LAP_COMPLETE` and `EV_SECTOR` (and when a new lap opens at S/F), call `lap_export_rtc(&s_lap, &lr)` then `lt_rtc_save(&lr, s_session_id, fix->gps_us, mode, power_state_placeholder, 0)`. `power_state` has no owner yet (power task is a later plan) — pass a fixed `0` and note it. This makes the most-recent gate the resume point after any reset.
- [ ] **Step 6** — build `moto_sim` and `moto_neo6m`; both green, size within `0x130000`. Commit (`feat(fw): RTC continuity -- save engine state per gate, resume interrupted lap (§15.3)`).

### Task 3: crash-loop safe mode — named constants, behavior, uptime auto-clear (§17.5)

**Files:** Modify `components/app/sys/lt_nvs.c`, `components/app/include/app/lt_nvs.h`, `components/app/supervisor/sup.c`, `components/app/logger/logger.c`. (Disjoint from Tasks 2/6.)

**Interfaces consumed:** `CRASH_LOOP_N`/`CRASH_LOOP_WINDOW_S`/`SAFE_MODE_CLEAR_S` (Task 1); `sys_flags_get/clear`, `SYS_SAFE_MODE` (lt_sup.h); `lt_crashlog_is_loop`, `lt_safe_until_get/set` (lt_nvs, existing); `errlog_add` (existing).

**Interfaces produced:** `void lt_safe_clear(void);` in `lt_nvs.h` (clears the persisted safe gate; NVS `lt_sys/safe_until` set so `boot_cnt <= get()` is false next boot).

- [ ] **Step 1** — `lt_nvs.c`: replace the hardcoded `3` (crash-log length is `CRASH_LOG_LEN`, leave that) and the `60` in `lt_crashlog_is_loop` with `CRASH_LOOP_WINDOW_S`, and assert/comment that `CRASH_LOG_LEN == CRASH_LOOP_N` (both 3). Include `app/lt_consts.h`.
- [ ] **Step 2** — `lt_nvs.c`: add `lt_safe_clear()` (erase or zero `lt_sys/safe_until`).
- [ ] **Step 3** — supervisor auto-clear: in `sup_task`, after the uptime update, if `sys_flags_get(SYS_SAFE_MODE)` and current uptime `>= SAFE_MODE_CLEAR_S`, call `lt_safe_clear()`, `sys_flags_clear(SYS_SAFE_MODE)`, `errlog_add(E_SYS_SAFE_MODE, 0)` once (guard with a static bool so it fires a single time). Uptime source: reuse the supervisor's existing seconds counter feeding `lt_rtc_uptime_update_s`.
- [ ] **Step 4** — logger safe-mode suppression: in the logger's record path, when `sys_flags_get(SYS_SAFE_MODE)` is set, skip writing FIX and FUSED sample records (drop them, still advance/ack the ring) but continue writing SESSION_HDR, VENUE, LAP, DRAG_RUN and END. Verify the `.sum` and summaries still form.
- [ ] **Step 5** — build both envs green, size gate. Commit (`feat(fw): crash-loop safe mode -- constants, logger suppression, uptime auto-clear (§17.5)`).

### Task 4: `app/cmd` dispatch + `export_serial` console core + `dbg` (§18.4/§18.1)

**Files:** Create `components/app/cmd/{cmd.c,CMakeLists.txt}` and `components/app/include/app/cmd.h`; create `components/drivers/export_serial/{export_serial.c,CMakeLists.txt}` and its `include/`; modify `main/app_main.c` (swap `dbg_console_start` → `export_serial_start`), `CMakeLists.txt` (append `components/drivers/export_serial` to `EXTRA_COMPONENT_DIRS` when `EXPORT_SERIAL` is ON), `main/CMakeLists.txt` (REQUIRES export_serial when present), and **remove** `components/app/sys/dbg_console.c` + its header (its debug verbs migrate into `export_serial`'s `dbg` command). (Runs after the parallel wave integrates.)

**Interfaces produced (`app/cmd.h`):**
```c
/* Transport-agnostic command dispatch (§18.1). The transport (serial now, BLE/WiFi later) supplies an
 * emit callback; cmd_dispatch runs one op and streams its output through emit as (tag, seq, flags,
 * payload) chunks. flags bit0 LAST, bit1 ERROR (payload = code u16 | utf8). Returns 0, or -1 on a
 * transport/emit error; protocol errors are reported through an ERROR chunk (E_CONN_PROTO). */
typedef int (*cmd_emit_fn)(void *ctx, uint8_t tag, uint16_t seq, uint8_t flags,
                           const uint8_t *payload, size_t len);
enum { CMD_STATUS=0x01, CMD_LIST=0x02, CMD_OPEN=0x03, CMD_READ=0x04, CMD_CLOSE=0x05, CMD_DELETE=0x06,
       CMD_CONFIG_GET=0x10, CMD_CONFIG_SET=0x11, CMD_ERRLOG_GET=0x14, CMD_ERRLOG_CLEAR=0x15,
       CMD_DIAG_GET=0x16 };
int cmd_dispatch(uint8_t op, uint8_t tag, const uint8_t *payload, size_t len,
                 cmd_emit_fn emit, void *ctx);
```
Task 4 implements the non-file ops (STATUS, CONFIG_GET/SET, ERRLOG_GET/CLEAR, DIAG_GET, DELETE, CLOSE); Task 5 fills LIST/OPEN/READ. A not-yet-implemented op returns an ERROR chunk with `E_CONN_PROTO` until Task 5.

**export_serial** (`export_serial.h`: `void export_serial_start(int reset_reason);`):
- `esp_console` REPL (reuse the 3.2 setup: prompt `laptimer>`, dumb mode, UART0; console baud stays `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200` per §21.2). Register per-verb commands via `esp_console_cmd_register`: `status`, `config` (`get`|`set <json>`), `errlog` (dump | `clear`), `diag`, `delete <id>`, `close`, and `dbg`. (`list`/`open`/`read` are registered in Task 5.)
- Each text command builds the request and calls `cmd_dispatch` with an emit callback that prints chunk payloads to the console. JSON/text ops print between the §18.4 framing `---BEGIN <name> <size>---\r\n` … `---END <crc32 hex>---\r\n`.
- `dbg <sub>`: implement `hang` (busy-loop to trip the task WDT), `crash` (`abort()` / null-deref to force `ESP_RST_PANIC`), and migrate the existing debug verbs `status`(engine)/`logtest [n]`/`fs`/`sum <id>`/`logck <id>`/`laps`/`rtc` (new: dump the validated `rtc_state_t`). `gps raw`/`imu raw`/`power`/`sim` per §18.4 are stubbed with a "not in plan 03" line (drivers/power land later).

- [ ] **Step 1** — `app/cmd`: implement STATUS (build the §18.2 20-byte status: proto_ver=1, state, sys_flags low16, batt_pct/mv placeholder 0, storage_free_kb from `sto_*`, session_count from the sessions dir, fw = `CFG_FW_VERSION`). CONFIG_GET/SET via `cfg_to_json`/`cfg_from_json` over the loaded `cfg_t` (+ `lt_cfg_save` on set, ERROR chunk with the validate message on failure). ERRLOG_GET (JSON of the error ring), ERRLOG_CLEAR, DIAG_GET (§17.10 counters + states as JSON), DELETE (`sto_*` unlink), CLOSE (ack).
- [ ] **Step 2** — `export_serial`: REPL + the verbs above wired to `cmd_dispatch`; framing + CRC32; redirect `esp_log_level_set("*", ESP_LOG_ERROR)` for the duration of a framed transfer, restore after.
- [ ] **Step 3** — `dbg crash`/`dbg hang` + migrate the 3.2/3.3/3.4 debug verbs; `dbg rtc`.
- [ ] **Step 4** — remove `dbg_console.c`/`.h`; swap `app_main.c` to `export_serial_start(reason)`; CMake gate on `EXPORT_SERIAL`; `main/CMakeLists.txt` REQUIRES `export_serial`.
- [ ] **Step 5** — build both envs green, size gate. Commit (`feat(fw): app/cmd dispatch + export_serial console (§18.4) + dbg crash/hang`).

### Task 5: `export_serial` file streaming — list / open / read (§18.4)

**Files:** Modify `components/app/cmd/cmd.c` (+ `cmd.h` if needed), `components/drivers/export_serial/export_serial.c`. (Serial, after Task 4.)

**Interfaces consumed:** `cmd_dispatch` (Task 4); `core/exp.h` (`exp_json_*`/`exp_vbo_*`/`exp_nmea_*`), the `core/ses` reader, `core/json.h`, `sto_*` listing/read.

- [ ] **Step 1** — `app/cmd` LIST: enumerate `/lfs/sessions`, for each read the `.sum` header via the `ses` reader and emit the §14.3 JSON array (id, start time, venue/layout, laps, best, log_kb), chunked.
- [ ] **Step 2** — `app/cmd` OPEN `id,fmt` / READ `offset`: for fmt json/vbo/nmea, stream the `.log` through `exp_open`/`exp_feed`(decoded ses frames)/`exp_pull` producing the export format, `LAST` on the final chunk followed by the 4-byte CRC32; for fmt log/sum, stream the raw file bytes. Keep the open stream resumable via READ `offset`.
- [ ] **Step 3** — `export_serial` `open <id> <vbo|nmea|json|log|sum>` / `read <offset>` / `list`: call `cmd_dispatch`, emit through the §18.4 framing; Base64-encode the payload for the binary formats (log/sum), raw text for vbo/nmea/json.
- [ ] **Step 4** — build both envs green, size gate; a quick `tools/serial_export.py <port> list` / `open` smoke check is part of the on-hardware session close. Commit (`feat(fw): export_serial file streaming -- list/open/read via core/exp (§18.4)`).

### Task 6: fix `tools/idf-env.sh` source-only guard under zsh (closes #6)

**Files:** Modify `tools/idf-env.sh`. (Fully disjoint — parallel with Tasks 2/3.)

- [ ] **Step 1** — replace the guard so it also refuses `zsh tools/idf-env.sh`. Keep the bash path working; add a zsh-sourced test. The guard must pass only when sourced in either shell:

```sh
# refuse direct execution in bash (return fails outside a sourced file) OR zsh (ZSH_EVAL_CONTEXT
# ends in :file only when the file is sourced; a directly-run script ends in :toplevel).
if [ -n "${ZSH_VERSION:-}" ]; then
  case "${ZSH_EVAL_CONTEXT:-}" in *:file) ;; *) echo "source this file: source tools/idf-env.sh" >&2; return 1 2>/dev/null || exit 1 ;; esac
else
  (return 0 2>/dev/null) || { echo "source this file: source tools/idf-env.sh" >&2; exit 1; }
fi
```

- [ ] **Step 2** — verify all four cases: `source tools/idf-env.sh` (bash and zsh) exports and prints the version; `./tools/idf-env.sh` (shebang→bash) refuses; `zsh tools/idf-env.sh` now refuses. Commit (`fix(tools): idf-env.sh refuses 'zsh idf-env.sh' via ZSH_EVAL_CONTEXT (closes #6)`).

---


## Session 3.6 — bench day, whole-plan review, and plan-03 close

Roadmap exit: run every §22.3 bench procedure achievable without the real sensors, record results in `docs/measurements.md`, fix what fails; tag `p03-d6` = `plan-03-done` and cut the first `v0.1.0`. Spec §22.3 (bench tests), §22.4 (resource measurement). This is the plan-closing session: per the deferred-minor policy it runs a whole-plan opus review + one fix wave closing plan-03's parked issues before the tag.

**Bench scope ruling.** §22.3 lists twelve target tests; only the sensor-free / driver-present subset is runnable in plan 03 (no power, IMU-I2C, real-GPS, BLE, OTA, or e-paper driver yet — those arrive plans 04-08). Runnable now: **WDT path** (`dbg hang`), **crash path** (`dbg crash` ×3 → safe mode), **storage truncation** (power-cut during logging, several trials), **GPS-sim laps** (moto_sim, already p03-d4), and a **serial-export smoke** (`list`/`open`/`read` over the §18.4 console — the plan-03 stand-in for the deferred BLE-export row, and the on-HW confirmation of Task-5's reassembled-file CRC). Plus **§22.4 resource measurement** (per-task stack high-water + heap). The remaining rows (power/sleep/I2C/GPS-power-cycle/brownout/BLE/OTA/e-paper) are recorded as "deferred to plan NN (driver not present)".

**Tasks.**
- **Task 1 — whole-plan opus review.** A cross-cutting integration review of the assembled plan-03 firmware (base `plan-02-done` 41c7f21 → HEAD), not a per-task re-review (each task was already gated): shared-state concurrency (sys_flags/hb[]/rings/RTC across the pipeline/logger/supervisor/console tasks), the §4.7 boot sequence as assembled, whole-firmware spec compliance, and the two parked issues. Findings feed the single fix wave.
- **Task 2 — one fix wave** (closes the plan-03 issues + review findings):
  - **#32** — `pipeline_laps_snapshot` reads `s_laps[]`/`s_lap_total` unsynchronised against the pipeline task's writes. Add a seqcount (or a brief critical section) so `dbg laps` never reads a torn `lap_result_t`.
  - **#35** — after an RTC resume, guard against a fix whose `gps_us` predates the resumed lap's `lap_start_gps_us` (rewound clock / GPS week rollover): invalidate/restart the lap instead of emitting a wrapped-negative split. Keep the resume freshness gate lenient enough that the sim replay still demonstrates resume.
  - **`dbg mem`** — add a console command dumping `uxTaskGetStackHighWaterMark` for pipeline/logger/supervisor/console + free & min-free heap (§22.4 inputs). This is the only new firmware surface the bench needs.
  - Any Critical/Important review findings; Minors adjudicated (fix or park with a ruling).
- **Task 3 — bench day (on hardware).** Flash moto_sim; run WDT, crash/safe, storage-truncation trials, sim-lap re-confirm, serial-export smoke, and the §22.4 resource measurement; record every result (pass + measured numbers) in `docs/measurements.md`. Then update each task's stack size to high-water + 25 % (§22.4) and rebuild.
- **Task 4 — version + close.** Tag `v0.1.0` (the first `v*` tag; `git describe` then feeds `CFG_FW_VERSION`/`esp_app_desc_t.version` instead of the fallback hash), verify a build stamps `0.1.0`, then `p03-d6` = `plan-03-done`. Delete the plan's SDD workspace.

Exit criterion: `docs/measurements.md` carries the bench-day results; both envs build green on ESP-IDF 5.3.2; the plan-03 issues (#32, #35) are closed; tags `v0.1.0` and `p03-d6`/`plan-03-done` pushed.

---
