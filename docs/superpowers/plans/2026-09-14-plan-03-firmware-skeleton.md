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
CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600
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
