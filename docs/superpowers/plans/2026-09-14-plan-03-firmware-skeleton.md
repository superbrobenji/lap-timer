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
file(GLOB_RECURSE APP_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
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
/* app/dbg_console.h -- minimal diagnostics console for session 3.2.
 *
 * Starts an IDF esp_console REPL on UART0 and registers a single `dbg status` command
 * (boot count, reset reason, uptime, crash counters, sys_flags, heartbeats). This is the
 * 3.2 roadmap exit command; the full §18.4 console (status/list/open/... and the other dbg
 * verbs) replaces it in session 3.5 on the same REPL. */
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

**Task 1 verification:** `lt_hal` + `app` (with only `lt_sys.c`) compile and link; IDF's built-in `hal` is retained (the rename resolves the override). `main` still runs the 3.1 banner (it calls nothing from `app` yet), so the tree builds green. Verified as part of the integrated build (below) — all components compile with no warnings from new code.

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
#include <string.h>

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

**Task 3 verification:** `lt_nvs.c` + `lt_rtc.c` compile clean under `-Werror`; blob sizes are the §15.2 values (12/385/15 B, packed). Boot steps 1–5 build and link (verified in the integrated build below). **Hardware-only checks:** the crash counters advancing across a real panic/WDT/brownout, and RTC-cell survival across those resets.

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
        if (!w->used) continue;
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
