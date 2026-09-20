#!/usr/bin/env bash
# tools/sign_release.sh <env> -- build, sign (ECDSA V1, no secure boot), verify, and stage a
# release image for OTA (spec §19.2). Signs build/<env>/laptimer.bin with the offline dev key
# keys/laptimer_priv.pem, verifies with keys/laptimer_pub.pem, and writes
# dist/laptimer-<version>-<hwid>.bin + .sha256. Requires the ESP-IDF env for espsecure.py; if it
# is not already on PATH the script sources tools/idf-env.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

[ $# -eq 1 ] || { echo "usage: tools/sign_release.sh <env>" >&2; exit 2; }
ENV_NAME="$1"

PRIV="keys/laptimer_priv.pem"
PUB="keys/laptimer_pub.pem"
[ -f "$PRIV" ] || { echo "missing signing key $PRIV (generate offline: espsecure.py generate_signing_key --version 1 $PRIV)" >&2; exit 1; }
[ -f "$PUB" ]  || { echo "missing public key $PUB" >&2; exit 1; }

# espsecure.py comes from the IDF env; source it if not already present.
command -v espsecure.py >/dev/null 2>&1 || source tools/idf-env.sh >/dev/null

./build.sh "$ENV_NAME" build

BUILD_DIR="build/${ENV_NAME}"
BIN="${BUILD_DIR}/laptimer.bin"
CFGH="${BUILD_DIR}/build_config.h"
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
