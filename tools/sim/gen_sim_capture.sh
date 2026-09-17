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
