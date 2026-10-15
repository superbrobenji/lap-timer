#!/usr/bin/env bash
# tools/test_build_refusals.sh -- build.sh must name an unimplemented driver and exit 1 BEFORE any
# ESP-IDF work (R34; final review M7: moto_neo6m_wifi used to slip through to idf.py). No IDF needed.
# Run from the repo root; CI runs it in `tools-tests`.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PASS=0; FAIL=0
refused() { # <env> <needle>
    local out rc
    out=$(./build.sh "$1" size 2>&1); rc=$?
    if [ "$rc" = 1 ] && printf '%s' "$out" | grep -q -- "$2"; then PASS=$((PASS+1)); echo "ok   $1 refused ($2)"
    else FAIL=$((FAIL+1)); echo "FAIL $1: want rc 1 mentioning '$2', got rc $rc"; echo "$out" | sed 's/^/     /'; fi
}
refused moto_m10        "GPS=m10"
refused moto_m10_sd     "GPS=m10"
refused car_neo6m       "DISPLAY=oled_ssd1309"
refused car_m10         "GPS=m10"
refused moto_neo6m_wifi "CONN=ble_wifi"
echo "test_build_refusals: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
