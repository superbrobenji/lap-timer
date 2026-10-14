#!/usr/bin/env bash
# tools/sign_release.sh <env> [--allow-sim] [--dry-run] -- build, sign (ECDSA V1, no secure
# boot), verify, and stage a release image for OTA (spec §19.2). Signs build/<env>/laptimer.bin
# with the offline dev key keys/laptimer_priv.pem, verifies with keys/laptimer_pub.pem, and
# writes dist/laptimer-<version>-<hwid>.bin + .sha256. Requires the ESP-IDF env for espsecure.py;
# if it is not already on PATH the script sources tools/idf-env.sh.
#
# Guards (audit drv:M5):
#   --allow-sim   without it, an environment/flag/build_config.h that selects a sim GPS or IMU
#                 driver is refused (spec §4.6 -- sim drivers are never part of a release build).
#   --dry-run     runs every guard (clean tree, sim check against build_config.h if one already
#                 exists) and reports what would happen, without building, signing or writing dist/.
#   clean tree    a release image is built from a clean, non-dirty-describe tree (spec §21.5).
#   size gate     a non-dry-run build also runs `./build.sh <env> size` (the §19.1 app-image gate)
#                 before signing.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

ENV_NAME=""
ALLOW_SIM=0
DRY_RUN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --allow-sim) ALLOW_SIM=1 ;;
        --dry-run)   DRY_RUN=1 ;;
        -h|--help)   echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]"; exit 0 ;;
        -*)          echo "unknown option $1" >&2
                     echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]" >&2; exit 2 ;;
        *)           [ -z "$ENV_NAME" ] || { echo "one environment only (got '$ENV_NAME' and '$1')" >&2; exit 2; }
                     ENV_NAME="$1" ;;
    esac
    shift
done
[ -n "$ENV_NAME" ] || { echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]" >&2; exit 2; }

# §21.5: a release image is built from a clean tree. Untracked files are ignored (they cannot be
# in the image); a modified tracked file is a refusal.
if ! git diff --quiet HEAD -- . 2>/dev/null; then
    echo "refusing: the working tree has modified tracked files -- a release build requires a clean tree (spec §21.5)" >&2
    exit 1
fi
ver_probe=$(git describe --tags --match 'v*' --dirty --always 2>/dev/null || echo "")
case "$ver_probe" in
    *-dirty) echo "refusing: git describe reports '$ver_probe' -- a release build requires a clean tree (spec §21.5)" >&2; exit 1 ;;
esac

# §4.6: the sim drivers are never part of a release build. Cheap pre-check on the env name and the
# flag overrides; the authoritative check is build_config.h's CFG_GPS_SIM/CFG_IMU_SIM below.
sim_refuse() {
    echo "refusing: $1 selects a bench (sim) driver and is never part of a release build (spec §4.6); pass --allow-sim to sign a bench image anyway" >&2
    exit 1
}
if [ "$ALLOW_SIM" = 0 ]; then
    case "$ENV_NAME" in *sim*) sim_refuse "environment '$ENV_NAME'" ;; esac
    [ "${GPS:-}" != "sim" ] || sim_refuse "GPS=sim"
    [ "${IMU:-}" != "sim" ] || sim_refuse "IMU=sim"
fi

BUILD_DIR="build/${ENV_NAME}"
BIN="${BUILD_DIR}/laptimer.bin"
CFGH="${BUILD_DIR}/build_config.h"

# The authoritative sim check: whatever the build actually generated.
check_cfg_sim() {
    [ -f "$CFGH" ] || return 0
    if [ "$ALLOW_SIM" = 0 ]; then
        grep -qE '^#define[ \t]+CFG_GPS_SIM[ \t]+1' "$CFGH" && sim_refuse "build_config.h CFG_GPS_SIM=1"
        grep -qE '^#define[ \t]+CFG_IMU_SIM[ \t]+1' "$CFGH" && sim_refuse "build_config.h CFG_IMU_SIM=1"
    fi
    return 0
}

if [ "$DRY_RUN" = 1 ]; then
    check_cfg_sim
    echo "dry-run: guards passed; would run ./build.sh $ENV_NAME build && ./build.sh $ENV_NAME size, then sign $BIN"
    exit 0
fi

PRIV="keys/laptimer_priv.pem"
PUB="keys/laptimer_pub.pem"
[ -f "$PRIV" ] || { echo "missing signing key $PRIV (generate offline: espsecure.py generate_signing_key --version 1 $PRIV)" >&2; exit 1; }
[ -f "$PUB" ]  || { echo "missing public key $PUB" >&2; exit 1; }

command -v espsecure.py >/dev/null 2>&1 || source tools/idf-env.sh >/dev/null

./build.sh "$ENV_NAME" build
./build.sh "$ENV_NAME" size      # §19.1: the ≤ 0x130000 app-image gate applies to a release image
check_cfg_sim

[ -f "$BIN" ]  || { echo "no image at $BIN -- build failed?" >&2; exit 1; }
[ -f "$CFGH" ] || { echo "no build_config.h at $CFGH" >&2; exit 1; }

ver=$(sed -n 's/^#define CFG_FW_VERSION *"\(.*\)"/\1/p' "$CFGH")
hwid=$(sed -n 's/^#define CFG_HWID *"\(.*\)"/\1/p' "$CFGH")
[ -n "$ver" ] && [ -n "$hwid" ] || { echo "could not read CFG_FW_VERSION/CFG_HWID from $CFGH" >&2; exit 1; }

mkdir -p dist
OUT="dist/laptimer-${ver}-${hwid}.bin"

espsecure.py sign_data --version 1 --keyfile "$PRIV" --output "$OUT" "$BIN"
espsecure.py verify_signature --version 1 --keyfile "$PUB" "$OUT"

if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$OUT" | awk '{print $1}' > "${OUT%.bin}.sha256"
else
    sha256sum "$OUT" | awk '{print $1}' > "${OUT%.bin}.sha256"
fi

echo "signed OK: $OUT"
echo "sha256:   $(cat "${OUT%.bin}.sha256")"
