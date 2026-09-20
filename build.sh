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
                --devux ON|OFF            interactive dev UX (dbg verbs); default ON
                --yes                     confirm a flash (required by 'flash')
USAGE
    exit 2
}

[ $# -ge 2 ] || usage
ENV_NAME="$1"; CMD="$2"; shift 2

PORT=""
FLASH_SIZE="4MB"
ASSUME_YES=0
DEVUX_FLAG="ON"   # default ON; --devux OFF selects the prod-slim variant
while [ $# -gt 0 ]; do
    case "$1" in
        --port)       PORT="${2:?--port needs a value}"; shift 2 ;;
        --flash-size) FLASH_SIZE="${2:?--flash-size needs a value}"; shift 2 ;;
        --devux)      DEVUX_FLAG="${2:?--devux needs a value}"; shift 2 ;;
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
# DEVUX is not a per-env FLAG; pass it explicitly every build (default ON) so a prior
# --devux OFF in this build dir never sticks via the CMake cache (§4.6, Plan 5 sub-project A).
DFLAGS+=("-DDEVUX=${DEVUX_FLAG}")

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
