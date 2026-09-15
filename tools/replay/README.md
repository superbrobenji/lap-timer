# tools/replay

Host-only C11 tools (spec §21.4, §22.2), built by `test/CMakeLists.txt` and never by the firmware
build. They link `core` and libm only, and `ctest` runs them beside the core suites.

- **`synth`** writes a synthetic session from a closed-form model of a rounded-polygon circuit:
  corners are circular arcs driven at a constant corner speed, straights carry a trapezoidal
  accelerate/cruise/brake profile, so every gate crossing time is analytic rather than integrated.
- **`replay`** decodes a `.log` and reports what it holds; `--json` prints a machine-readable
  summary. From session 2.7 it also drives the lap and drag engines in the pipeline's call order.

## Outputs of `synth --out PREFIX`

| File | Contents |
|------|----------|
| `PREFIX.log` | SESSION_HDR, VENUE, TIME_MAP every 60 s, FIX_KEY/FIX_DELTA, FUSED, END (§12.3) |
| `PREFIX.truth.json` | generator, full configuration, gate geometry, every crossing in run seconds and `gps_us`, lap and sector times |
| `PREFIX.venue.json` | the venue in the §10.2 track schema, loadable with `trk_from_json` |

Unless `--quiet`, one line goes to stdout:
`wrote PREFIX.log: <n> fixes, <n> fused, <n> laps, <duration> s`.
Exit status: 0 success, 1 an invalid circuit or a write failure, 2 a bad command line (usage to stderr).

## Options (default in brackets)

Circuit: `--vertices N` [12] · `--length M` [2500] · `--radius M` [40] ·
`--irregularity F` [0] · `--anticlockwise` [clockwise] · `--seed S` [1].

Run: `--v-corner MPS` [15] · `--v-max MPS` [50] · `--a-acc MPS2` [3] · `--a-brk MPS2` [6] ·
`--lap-var F` [0.03] · `--laps N` [10] · `--sectors N` [2] · `--start-before M` [300] ·
`--stop-after M` [200] · `--sf-frac F` [0.5 / vertices].

GPS: `--rate HZ` 5 or 10 [5] · `--pos-sigma M` [1.5] · `--pos-tau S` [20] ·
`--speed-sigma MPS` [0.05] · `--head-sigma DEG` [0.5] · `--latency MS` [80] · `--jitter MS` [20] ·
`--dropout T0:T1` [none] · `--fused-hz N`, 0 disables FUSED [10] ·
`--start-utc YYYY-MM-DDTHH:MM:SS` [2026-09-15T10:00:00].

Output: `--out PREFIX` (required) · `--quiet` · `--help`.

`--seed` seeds both the circuit (irregularity, per-lap `v_max`) and the GPS noise, so one value
reproduces a whole session byte for byte.

## Examples

    synth --out /tmp/killarney --laps 10 --rate 10 --sectors 3
    replay /tmp/killarney.log --json

    synth --out /tmp/gap --laps 3 --dropout 30:40 --fused-hz 0 --seed 7 --quiet

## ctest

`CMakeLists.txt` builds one test executable per `test/test_*.c` and adds two smoke tests:
`synth_smoke` runs `synth --out <build>/synth_smoke --laps 2 --quiet`, and `replay_smoke` reads the
log it wrote and passes only when the JSON summary contains `"bad_frames":0`. Run everything with
`ctest --test-dir test/build --output-on-failure`.
