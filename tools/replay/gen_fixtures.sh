#!/usr/bin/env bash
# Regenerate the committed replay regression fixtures under test/data/ (spec §22.2).
#
# usage: gen_fixtures.sh [synth] [replay] [outdir]
#   synth   path to the built synth   binary (default build/tools/replay/synth)
#   replay  path to the built replay  binary (default build/tools/replay/replay)
#   outdir  where the fixtures land         (default test/data)
#
# Regeneration is a MANUAL maintenance action, not a CI step. The committed test/data fixtures are the
# regression baseline; test_replay_fixtures reads them and compares replay --json to <name>.expected.json
# with a ±1 ms tolerance on time fields (see json_eq_tol), because the circuit .log positions and the
# crossing interpolation ride on libm, whose last ULP differs Apple-libm vs glibc — a byte-exact
# regenerate-and-diff would be flaky. So do NOT wire this into CI; when synth or the engines change on
# purpose, rerun this script and commit the regenerated set as a whole. (Same seed + same build gives
# byte-identical output; the drag fixtures are libm-transcendental-free and portable regardless.)
set -euo pipefail

SYNTH="${1:-build/tools/replay/synth}"
REPLAY="${2:-build/tools/replay/replay}"
OUT="${3:-test/data}"
mkdir -p "$OUT"

# lap NAME RATE SEED [extra synth args...] — clean-position circuit + its lap-engine replay.
lap() {
  local name="$1" rate="$2" seed="$3"; shift 3
  "$SYNTH" --out "$OUT/$name" --laps 4 --rate "$rate" --pos-sigma 0 --seed "$seed" "$@" --quiet
  "$REPLAY" --mode lap --venue-json "$OUT/$name.venue.json" --json "$OUT/$name.log" > "$OUT/$name.expected.json"
}

# drag NAME TARGET_KMH RATE SEED — straight-line run + its drag-engine replay (no venue side-car).
drag() {
  local name="$1" target="$2" rate="$3" seed="$4"
  "$SYNTH" --profile drag --target-kmh "$target" --rate "$rate" --seed "$seed" --out "$OUT/$name" --quiet
  "$REPLAY" --mode drag --json "$OUT/$name.log" > "$OUT/$name.expected.json"
}

lap  killarney_full     5 1
lap  killarney_short    5 2 --vertices 8  --length 1600
lap  killarney_full_rev 5 3 --anticlockwise
lap  zwartkops         10 4 --vertices 10 --length 3000

drag drag_0_180  180  5 1
drag drag_0_320  320 10 2

# gps_dropout: a circuit with realistic noise and a mid-run dropout (reader/engine robustness).
"$SYNTH" --out "$OUT/gps_dropout" --laps 4 --rate 5 --seed 5 --dropout 120:130 --quiet
"$REPLAY" --mode lap --venue-json "$OUT/gps_dropout.venue.json" --json "$OUT/gps_dropout.log" > "$OUT/gps_dropout.expected.json"

# truncated: a full lap .log chopped mid-frame (exercises ses_reader resync + flush at EOF). The
# offset drops the last laps and the END record and lands inside a frame, so the reader must both
# recover the frames before the cut and reject the partial tail (deterministic: fixed seed → fixed
# byte length → fixed cut → a stable expected.json with bad_frames = 1).
"$SYNTH" --out "$OUT/truncated" --laps 4 --rate 5 --pos-sigma 0 --seed 6 --quiet
sz=$(wc -c < "$OUT/truncated.log")
head -c "$(( sz * 3 / 5 - 7 ))" "$OUT/truncated.log" > "$OUT/truncated.log.tmp"
mv "$OUT/truncated.log.tmp" "$OUT/truncated.log"
"$REPLAY" --mode lap --venue-json "$OUT/truncated.venue.json" --json "$OUT/truncated.log" > "$OUT/truncated.expected.json"

echo "wrote 8 fixtures to $OUT"
