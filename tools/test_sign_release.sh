#!/usr/bin/env bash
# tools/test_sign_release.sh -- dry-run harness for tools/sign_release.sh's §4.6/§21.5 guards
# (audit drv:M5). Exercises every refusal path without ESP-IDF, espsecure.py, or a signing key:
# --dry-run stops before the build, and a fixture build/<env>/build_config.h drives the
# authoritative post-build check. Run from the repo root; CI runs it in `tools-tests`.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The dirty-tree case needs a tracked file to modify. Never discard a contributor's work: refuse
# to run on a tree that already has tracked modifications (audit F4 final review I2), and restore
# the probe file from a byte-exact backup rather than `git checkout`.
if ! git diff --quiet HEAD -- . 2>/dev/null; then
    echo "test_sign_release: refusing to run -- the working tree has modified tracked files; commit or stash them first (this harness edits a tracked file as a probe)" >&2
    exit 2
fi

PASS=0
FAIL=0
DIRT="docs/power-of-10-checklist.md"
BACKUP=$(mktemp)
cp -p "$DIRT" "$BACKUP"
restore() { cp -p "$BACKUP" "$DIRT"; }
cleanup() { restore; rm -f "$BACKUP"; rm -rf build/t_sign_fixture build/t_sign_fixture_ws29v2; }
trap cleanup EXIT

check() { # <name> <expected rc> <cmd...>
    local name="$1" want="$2"; shift 2
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" = "$want" ]; then PASS=$((PASS+1)); echo "ok   $name (rc $rc)"
    else FAIL=$((FAIL+1)); echo "FAIL $name: want rc $want, got $rc"; echo "$out" | sed 's/^/     /'; fi
}

grep_check() { # <name> <needle> <cmd...>
    local name="$1" needle="$2"; shift 2
    local out; out=$("$@" 2>&1)
    if printf '%s' "$out" | grep -q -- "$needle"; then PASS=$((PASS+1)); echo "ok   $name"
    else FAIL=$((FAIL+1)); echo "FAIL $name: output lacks '$needle'"; echo "$out" | sed 's/^/     /'; fi
}

# 1. a sim environment is refused without --allow-sim
check "sim env refused"            1 tools/sign_release.sh moto_sim --dry-run
grep_check "sim refusal explains"  "--allow-sim" tools/sign_release.sh moto_sim --dry-run
# 2. ... and allowed with it
check "sim env with --allow-sim"   0 tools/sign_release.sh moto_sim --allow-sim --dry-run
# 3. a non-sim environment dry-runs clean
check "prod env dry-run"           0 tools/sign_release.sh moto_neo6m --dry-run
# 4. a dirty tracked file is refused
printf '\n<!-- sign_release dirty probe -->\n' >> "$DIRT"
check "dirty tree refused"         1 tools/sign_release.sh moto_neo6m --dry-run
grep_check "dirty refusal explains" "clean tree" tools/sign_release.sh moto_neo6m --dry-run
restore
# 5. the authoritative post-build check reads build_config.h, not the env name
mkdir -p build/t_sign_fixture
cat > build/t_sign_fixture/build_config.h <<'EOF'
#define CFG_GPS_SIM        1
#define CFG_IMU_SIM        1
#define CFG_HWID           "fixture_hwid"
#define CFG_FW_VERSION     "v0.0.0-fixture"
EOF
check "build_config sim refused"   1 tools/sign_release.sh t_sign_fixture --dry-run
check "build_config sim allowed"   0 tools/sign_release.sh t_sign_fixture --allow-sim --dry-run
# 5b. $PANEL selects build/<env>_<panel>, the directory build.sh builds into: the post-build check
# must read THAT build_config.h (a sim build there is refused even if build/<env> is clean)
mkdir -p build/t_sign_fixture_ws29v2
cp build/t_sign_fixture/build_config.h build/t_sign_fixture_ws29v2/build_config.h
printf '#define CFG_HWID "clean"\n#define CFG_FW_VERSION "v0"\n' > build/t_sign_fixture/build_config.h
check "default panel reads build/<env>"      0 tools/sign_release.sh t_sign_fixture --dry-run
check "PANEL=ws29v2 reads build/<env>_ws29v2" 1 env PANEL=ws29v2 tools/sign_release.sh t_sign_fixture --dry-run
grep_check "PANEL dry-run names the panel image" "build/t_sign_fixture_ws29v2/laptimer.bin" env PANEL=ws29v2 tools/sign_release.sh t_sign_fixture --allow-sim --dry-run
# 6. a bad argument is a usage error
check "unknown flag"               2 tools/sign_release.sh moto_sim --nope

echo "test_sign_release: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
