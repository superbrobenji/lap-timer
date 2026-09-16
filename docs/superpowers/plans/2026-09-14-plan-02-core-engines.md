

## Session 2.7 — replay engine end-to-end and regression fixtures (plan-closing)

Roadmap exit criterion: `ctest` runs the replay regressions and the 5 Hz/10 Hz error bounds hold; tag `p02-d7` = `plan-02-done`. Two serial tasks. NOTE: the design drafter stalled at the plan-writing step after its code was compile-verified (clang and gcc-16, 29/29, on a scratch tree); the orchestrator applied that verified code directly in commits `53df91c` (engine) and `657f105` (fixtures) and records it here. Full source is in those commits; the API contract and the fixture generator are embedded below.

Rulings (spec is the authority):
- A synthetic `.log` has FIX_* and FUSED but no raw IMU, so `replay` does not run `fus_step`; it drives decoded fixes through the §9.1 `on_fix` order (validity, `tb_on_fix`, `fus_set_gps_speed`, `lap_on_fix`/`drag_on_fix`) and feeds decoded FUSED to the active engine. `--imu` (raw IMU) is future.
- Lap-mode venues are the synthetic ones, from the sidecar `<log>.venue.json` (or `--venue-json`), or a bundled venue via `--venue <id>`.
- Fixtures are synthetic stand-ins (§22.2), generated on this host; the `.expected.json` regression is compared structurally with a plus-or-minus 1 ms tolerance on time fields, not byte-exact, so it is stable across Apple-libm and glibc (circuit geometry uses transcendentals whose last ULP differs). Hence no `fixtures-generated` CI job (a regen plus `git diff` would be cross-libm fragile) and no new required check; the tolerant regression and the error-bound test run in the existing required `host-tests` job. `gen_fixtures.sh` regenerates the set as a manual maintenance action.
- §22.2's 30/15 ms bound is engine accuracy, so the accuracy lap fixtures use `--pos-sigma 0` (receiver noise is a §6.7 limit, covered by `test_lap`'s realistic case); `gps_dropout`/`truncated` are regression-only. The engine consumes the first synth lap as its out-lap, so flying lap N = `synth_run_lap_time(N+1)`.
- §22.1 trap reconciliation (strip 223 vs instantaneous 226) from session 2.6 stands.

### Task 1: `replay` engine run and the `synth` drag profile (commit `53df91c`)

**Files:** created `tools/replay/include/replay/synth_drag.h`, `tools/replay/lib/replay_run.c`, `tools/replay/lib/synth_drag.c`, `tools/replay/test/test_replay_run.c`; modified `tools/replay/include/replay/replay.h`, `tools/replay/replay_main.c`, `tools/replay/synth_main.c`, `tools/replay/lib/synth_truth.c`, `tools/replay/CMakeLists.txt`, spec §4.2. `test_replay_run` (3 cases) drives `replay_run` on an in-memory synth log (lap and drag) and checks results against the synth truth.

`replay_run` API (from `tools/replay/include/replay/replay.h`, verbatim):

```c
REPLAY_MODE_SUMMARY = 0, REPLAY_MODE_LAP = 1, REPLAY_MODE_DRAG = 2 };

#define REPLAY_MAX_LAPS      256
#define REPLAY_MAX_RUNS      16
#define REPLAY_RUN_JSON_CAP  262144       /* 256 laps × sectors + scalars fit with headroom */

typedef struct {
    uint16_t lap_no;
    uint8_t  flags;                        /* LAP_F_* */
    uint8_t  n_sectors;                    /* splits = sector gates + 1 */
    int64_t  start_gps_us;                 /* S/F crossing that opened the lap */
    int64_t  end_gps_us;                   /* S/F crossing that closed it */
    uint32_t time_ms;
    uint32_t sector_ms[LAP_MAX_SECTORS + 1];
    int64_t  sector_gps_us[LAP_MAX_SECTORS];   /* absolute crossing gps_us of each sector gate, in order */
    uint8_t  n_sector_cross;               /* sector-gate crossings captured this lap */
} replay_lap_t;

typedef struct {
    uint16_t        run_no;
    int64_t         t0_gps_us;             /* launch instant (back-dated by the engine) */
    uint8_t         flags;                 /* DRAG_F_* */
    uint8_t         n_gates;
    uint16_t        trap_cms;
    drag_gate_res_t gates[DRAG_MAX_GATES];
} replay_drag_t;

typedef struct {
    int       mode;                        /* REPLAY_MODE_LAP / _DRAG */
    uint32_t  n_frames, n_bad;
    uint32_t  n_by_type[128];
    ses_hdr_t hdr; int have_hdr;
    uint32_t  n_fix, n_fix_valid, n_fused, n_events;
    int64_t   first_fix_gps_us, last_fix_gps_us;
    int32_t   max_gspeed_mms;
    uint16_t  venue_id, layout_id;         /* from the lap engine's EV_VENUE_FOUND / EV_LAYOUT_LOCKED */
    uint16_t  n_laps; replay_lap_t  laps[REPLAY_MAX_LAPS];
    uint16_t  n_runs; replay_drag_t runs[REPLAY_MAX_RUNS];
} replay_run_t;

/* mode is REPLAY_MODE_LAP or _DRAG; venue is required (and used) only in lap mode.
 * Returns 0, -1 on an I/O error, -2 if lap mode was asked with no venue, -3 on a bad mode. */
int  replay_run(const char *path, int mode, const trk_venue_t *venue, replay_run_t *out);
int  replay_run_mem(const uint8_t *buf, size_t n, int mode, const trk_venue_t *venue, replay_run_t *out);
void replay_print_run_json(const replay_run_t *r, FILE *f);

#endif
```

### Task 2: regression fixtures, tolerant JSON regression, error bounds (commit `657f105`)

**Files:** created 30 files under `test/data/` (8 fixtures with `.log`/`.expected.json` plus `.venue.json`/`.truth.json` where applicable), `tools/replay/gen_fixtures.sh`, `tools/replay/test/test_replay_fixtures.c` (3 cases: the tolerant `.expected.json` regression via `json_eq_tol`; the 5/10 Hz error bounds vs `.truth.json`; truncated/dropout robustness). No `ci.yml` change.

Fixture generator (`tools/replay/gen_fixtures.sh`, verbatim):

```bash
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
```

Verification: clang `test/build` and gcc-16 `test/build-gcc` both 29/29 (`test_replay_run`, `test_replay_fixtures` added); ASan/UBSan clean; hygiene clean (`.log` is `binary` per `.gitattributes`; JSON side-cars whitespace-clean). Measured: 5 Hz lap crossing error worst 0.5 ms, 10 Hz worst 0.3 ms; drag gate worst 15.0 ms.

---
