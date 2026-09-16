# Plan 02: Core Engines Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and test, entirely on the host, the engines that turn GPS fixes and IMU samples into lap, sector and drag results, plus the synthetic-circuit generator and `replay` tool that prove them against analytic ground truth.

**Architecture:** `components/core/` gains `fusion/`, `lapengine/` and `dragengine/` (pure C11, no IDF, no `malloc`, caller-owned state) with one Unity suite each. `tools/replay/` is a host-only C11 library plus two executables: `synth` writes a synthetic session (`.log`, `.truth.json`, `.venue.json`) from a closed-form kinematic model of a rounded polygon circuit, and `replay` decodes a `.log` and (from session 2.7) drives the engines in the exact call order of `app/pipeline`. Both build inside the existing `test/` CMake project so `ctest` runs their tests and the regression fixtures.

**Tech Stack:** C11, CMake ≥ 3.16, Unity 2.6.0 (submodule), jsmn/jw from plan 01. macOS (Apple clang) and Linux (gcc-16 parity build).

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` — §4.2 (layout), §5.2 (core API), §6.4–6.7 (gate crossing, fix validity, drag math, accuracy), §9 (pipeline, calibration, fusion), §10 (track model and lap engine), §11 (drag engine), §12 (log format), §22.1–22.2 (tests, replay and synth), Appendix A.

**Roadmap:** `docs/superpowers/plans/2026-09-14-roadmap.md` — plan 2, sessions 2.1–2.7. This document is written one session at a time, per the roadmap session protocol (step 1): the tasks of session 2.N are appended at the start of session 2.N, after the previous session's tag, so each session's task text can build on what actually landed. Session scopes and exit criteria are fixed by the roadmap table.

## Global Constraints

- `components/core/**` MUST NOT include any ESP-IDF, FreeRTOS, or driver header, and MUST NOT call `malloc`/`free`. State lives in caller-provided structs. (§4.1)
- `tools/replay/**` is host-only C11: it may use `<stdio.h>`, `<stdlib.h>` (including `malloc`) and `<math.h>`, but it links only `core` and libm and MUST NOT be compiled into any firmware or into `test_apps/core_selftest`. It keeps no mutable global state (every function takes its context as a parameter) so tests can build many models in one process.
- Warnings: `-Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion` on core, tools and tests (the `LAPTIMER_STRICT_FLAGS` variable defined in Task 1). (§17.9)
- All time values are `int64_t` microseconds unless the field name ends in `_ms`, `_s` or the type is `double` seconds inside `tools/replay` (documented per field). (§0)
- Every numeric threshold is a named constant from Appendix A (`core/consts.h`) or a documented default in a `*_cfg_defaults()` function; no bare literals in logic.
- Symbol prefixes: `fus_`, `lap_`, `drag_` in core; `synth_`, `logw_`, `logr_`, `replay_` in tools. (§0)
- Sign conventions (from `core/types.h`): lateral g and lean are **+ to the right**, yaw rate is **+ for a left turn**, `a_lon` is + when accelerating. Heading is compass degrees (0 = north, clockwise positive).
- Tests are written first; every test asserts real behaviour against analytic expectations (no "runs without crashing" tests). Run `ctest --test-dir test/build --output-on-failure` before every commit and only commit on a green run.
- Plan blocks are byte-identical to files: a task that changes a file which has a code block in this plan (or in plan 00/01) updates that block in the same commit.
- Commit trailers on every commit: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED`.
- Working directory for all commands: repository root `/Users/benji/projects/personal/lap-timer` (or the task's worktree, see below).

## Parallel execution rules (this plan)

Tasks are designed so that several implementers can run at once without touching the same file:

1. A session's **first task is serial**: it lands every shared header, CMake file and spec edit the session needs. Its "Produces" block is the contract every parallel task codes against; parallel tasks MUST NOT edit those headers (a needed change is reported as `NEEDS_CONTEXT` and applied by the orchestrator to the session branch before the parallel work resumes).
2. Tasks marked **[parallel group N]** own disjoint file sets (listed under **Files**) and only *add* files. They never edit `CMakeLists.txt` (the tool and test source lists are CMake globs) and never edit the plan or spec.
3. Each parallel task runs in its own git worktree on a branch `s<plan>.<day>-t<N>` cut from the session branch after the serial task's commit. When its review passes, the orchestrator merges it into the session branch (`git merge --no-ff`); by construction there are no conflicts. Task reviews of parallel tasks also run in parallel, each against its own branch range.
4. The session's **last task is serial** again: it integrates (CLI wiring, end-to-end tests, ctest registrations, plan-block sync, spec write-backs) on the session branch after every parallel branch is merged.
5. The final whole-session verification (clang + gcc parity + hygiene) and the PR happen once, from the session branch.

## File structure produced by this plan

```
components/core/
  include/core/fus.h    fusion/fus.c  fusion/fus_still.c  fusion/fus_orient.c  fusion/fus_fwd.c   (session 2.2; 2.3 extends fus.h/fus.c for the lean filter and GPS cross-check)
  include/core/lap.h    lapengine/lap.c  lapengine/lap_gate.c     (sessions 2.4–2.5)
  include/core/drag.h   dragengine/drag.c                          (session 2.6)
test/
  test_fus.c  test_fus_still.c  test_fus_orient.c  test_fus_fwd.c    (session 2.2)
  test_lap.c  test_drag.c                                          (sessions 2.4–2.6)
  data/*.log  data/*.expected.json                                 (session 2.7)
tools/replay/
  CMakeLists.txt                       replaylib + synth + replay + tool tests (Task 1)
  include/replay/synth.h               closed-form circuit and run model (Task 1; implemented in Task 2)
  include/replay/synth_gps.h           GPS/fused sampling with noise, latency, dropouts (Task 1; implemented in Task 3)
  include/replay/logio.h               .log writer/reader over the ses_* codecs (Task 1; implemented in Task 4)
  include/replay/replay.h              replay summary API and version (Task 1; implemented in Task 4)
  include/replay/synth_truth.h         generation loop, truth/venue writers, CLI parser (Task 5)
  lib/replay_version.c                 (Task 1)
  lib/synth_track.c                    (Task 2)
  lib/synth_gps.c                      (Task 3)
  lib/logio.c  lib/replay_summary.c    (Task 4)
  replay_main.c                        (Task 4)
  synth_main.c  lib/synth_truth.c      (Task 5)
  README.md                            (Task 5)
  test/test_replay_smoke.c             (Task 1)
  test/test_synth_track.c              (Task 2)
  test/test_synth_gps.c                (Task 3)
  test/test_logio.c  test/test_replay_summary.c   (Task 4)
  test/test_synth_e2e.c                (Task 5)
```

---

## Session 2.1 — synthetic circuit generator, fixture writer, replay skeleton

Roadmap exit criterion: `synth` emits a `.log` that the `test_ses_records` decoders (through `logio`) read back; tag `p02-d1`.

Task graph: **Task 1** (serial) → **Tasks 2, 3, 4** [parallel group 1] → **Task 5** (serial).

Decisions fixed for the session (rulings, spec is the authority):

- The polygon's corners are circular arcs (`corner_radius_m`, default 40 m) driven at constant `v_corner`, so lateral g, lean and yaw rate are finite and realistic for the fusion and drag sessions; straights carry a trapezoidal speed profile (accelerate / cruise / brake). Everything is closed-form; gate crossing times are computed by inverting the piecewise kinematics, never by numeric integration.
- The run is a sequence of kinematic tables (`synth_lap_t`), one per lap-frame period (s = 0 to `length_m`); tables switch at s = 0, where every table is at `v_corner`, so truth speed and acceleration are continuous across the switch. A per-table rider factor (`lap_var`) scales the accelerations and the cruise cap, giving distinct lap times while every lap stays analytic (the corner speed is constant, which also keeps the lateral-g signature identical lap to lap).
- Gates (S/F and sector gates) are placed on straights only; a requested arc-length that falls in an arc or within 20 m of a straight's end is moved forward to the first admissible point (the truth file records the position actually used).
- Fix timestamps: `gps_us = t0_gps_us + k·(1e6 / rate)` exactly (5 Hz → 200 000 µs steps); `mono_us = t0_mono_us + t_k·1e6 + latency + jitter`. Position error is a first-order Gauss–Markov process per ENU axis (σ = 1.5 m, τ = 20 s); speed noise is white (σ = 0.05 m/s); heading noise is white (σ = 0.5°). `fix.valid = 1` on every emitted fix (the device logs post-validation fixes; §6.5 invalid fixes are produced by the `gps_dropout` options, not by noise).
- The synth writes FUSED records from truth (no noise), at `fused_hz` (default 10); it does not synthesise raw IMU samples in this session (session 2.3 adds an `--imu` CSV emitter once `fus_step` exists to consume it).
- Tool tests live in `tools/replay/test/` and are built only by the host CMake project; `test_apps/core_selftest` keeps globbing `test/test_*.c` only.

### Task 1: Tool scaffolding, shared headers, build and spec wiring (serial)

**Files:**
- Create: `tools/replay/CMakeLists.txt`, `tools/replay/include/replay/synth.h`, `tools/replay/include/replay/synth_gps.h`, `tools/replay/include/replay/logio.h`, `tools/replay/include/replay/replay.h`, `tools/replay/lib/replay_version.c`, `tools/replay/test/test_replay_smoke.c`
- Modify: `test/CMakeLists.txt` (strict-flags variable, `add_subdirectory`), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§4.2, §21.4, §22.2, §23), `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` (its `test/CMakeLists.txt` block)

**Interfaces:**
- Consumes: `core/types.h` (`gps_fix_t`, `fused_sample_t`, `lap_result_t`, `drag_result_t`), `core/trk.h` (`trk_venue_t`, `trk_line_t`), `core/ses.h` (codecs and reader), `core/geo.h`, `core/consts.h`, `core/core.h` (`core_version`).
- Produces: the four headers below, verbatim. Tasks 2–5 implement them and MUST NOT change them.

- [ ] **Step 1: Write the smoke test**

`tools/replay/test/test_replay_smoke.c`:

```c
#include "unity.h"
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/logio.h"
#include "replay/replay.h"
#include "core/core.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_headers_agree_with_core_limits(void)
{
    TEST_ASSERT_EQUAL_INT(LAP_MAX_SECTORS + 1, SYNTH_MAX_GATES);
    TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VERTICES * 4, SYNTH_MAX_PIECES);
    TEST_ASSERT_TRUE(sizeof(((logr_t *)0)->n_by_type) / sizeof(uint32_t) > SES_T_END);
}

static void test_version_string_is_the_core_version(void)
{
    TEST_ASSERT_EQUAL_STRING(core_version(), replay_version());
    TEST_ASSERT_TRUE(strlen(replay_version()) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_headers_agree_with_core_limits);
    RUN_TEST(test_version_string_is_the_core_version);
    return UNITY_END();
}
```

- [ ] **Step 2: Write the headers**

`tools/replay/include/replay/synth.h`:

```c
#ifndef REPLAY_SYNTH_H
#define REPLAY_SYNTH_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/types.h"
#include "core/trk.h"

/* Synthetic circuit and run model (spec §22.2). Host-only C11, no global state.
 *
 * Geometry: an N-vertex polygon whose corners are replaced by circular arcs of radius
 * corner_radius_m, driven clockwise (right-hand corners) or anticlockwise. Speed profile on every
 * straight: enter at v_corner, accelerate at a_acc, cruise at the lap's v_max if it is reached, brake
 * at a_brk so the next arc is entered at v_corner; every arc is driven at constant v_corner. All
 * kinematics are closed-form (piecewise constant acceleration), so gate crossing times are exact.
 *
 * Frames and signs: ENU metres about the venue centre (cfg origin_lat/lon = polygon centroid).
 * Heading is compass degrees, 0 = north, clockwise positive (gps_fix_t.head_e5 convention). a_lon is
 * + when accelerating. Lateral g and lean are + to the right, yaw rate is + for a left turn
 * (core/types.h). Arc lengths are "lap frame" (0 = start of straight 0, wraps at length_m) unless
 * the name says s_total (unwrapped from the run's first lap table). Times are double seconds of run
 * time ("run time"): 0 at the run start, which is start_before_m before the first S/F crossing. */

#define SYNTH_MAX_VERTICES 32
#define SYNTH_MAX_GATES    (LAP_MAX_SECTORS + 1)          /* S/F plus sector gates */
#define SYNTH_MAX_LAPS     200                             /* full laps; the run adds an out-lap and a run-out */
#define SYNTH_MAX_TABLES   (SYNTH_MAX_LAPS + 3)
#define SYNTH_MAX_PIECES   (SYNTH_MAX_VERTICES * 4)       /* per lap: a straight is up to 3 pieces, an arc 1 */
#define SYNTH_GATE_MARGIN_M 20.0                           /* gates keep this far from a straight's ends */

typedef struct {
    int      n_vertices;        /* 3..SYNTH_MAX_VERTICES; default 12 */
    double   length_m;          /* lap length along the driven path (straights + arcs); default 2500 */
    double   corner_radius_m;   /* default 40 */
    double   irregularity;      /* 0 = regular polygon; else vertex radius k scaled by 1 + irregularity·U(-1,1); default 0 */
    bool     clockwise;         /* default true (right-hand corners: positive lean, negative yaw rate) */
    uint32_t seed;              /* drives irregularity and lap_var; default 1 */
    double   v_corner_mps;      /* speed through every arc; default 15 */
    double   v_max_mps;         /* cruise cap; default 50 */
    double   a_acc_mps2;        /* default 3 */
    double   a_brk_mps2;        /* default 6 (magnitude) */
    double   lap_var;           /* per-table rider factor k = 1 + lap_var·U(-1,1) applied to a_acc, a_brk and v_max (v_corner stays constant, so speed is continuous where tables meet); default 0.03 */
    int      laps;              /* full laps after the first S/F crossing; 1..SYNTH_MAX_LAPS; default 10 */
    double   start_before_m;    /* run starts this far before S/F (out-lap length); default 300 */
    double   stop_after_m;      /* run continues this far past the last S/F; default 200 */
    double   sf_frac;           /* requested S/F arc length as a fraction of length_m; default 0.5 / n_vertices */
    int      n_sector_gates;    /* 0..LAP_MAX_SECTORS; default 2, requested evenly after S/F */
    double   gate_half_width_m; /* default GATE_HALF_WIDTH_M */
    double   origin_lat_deg;    /* default -34.0300 */
    double   origin_lon_deg;    /* default 18.7300 (about 30 km from every bundled venue) */
    uint16_t venue_id;          /* default TRK_USER_ID_BASE */
} synth_cfg_t;
void synth_cfg_defaults(synth_cfg_t *c);

typedef enum { SYNTH_P_ACCEL = 0, SYNTH_P_CRUISE = 1, SYNTH_P_BRAKE = 2, SYNTH_P_ARC = 3 } synth_piece_kind_t;

typedef struct {
    synth_piece_kind_t kind;
    double s0, len;             /* lap-frame arc length at piece start; piece length (> 0) */
    double t0, dur;             /* lap-frame time at piece start (0 at s = 0); piece duration */
    double e0, n0;              /* ENU point at piece start */
    double head0_rad;           /* compass heading at piece start, radians */
    double v0, a;               /* speed at piece start; longitudinal acceleration (signed; 0 for cruise and arcs) */
    double ce, cn, turn_rad;    /* arcs only: centre and signed turn over the piece (+ = left) */
} synth_piece_t;

typedef struct {                /* one lap-frame period's kinematic table (s = 0 .. length_m) */
    synth_piece_t pieces[SYNTH_MAX_PIECES];
    int    n_pieces;
    double lap_time_s;          /* time from s = 0 to s = length_m in this table */
    double k;                   /* this table's rider factor 1 + lap_var·U(-1,1) */
    double v_max_mps;           /* cfg.v_max_mps · k */
    double a_acc_mps2;          /* cfg.a_acc_mps2 · k */
    double a_brk_mps2;          /* cfg.a_brk_mps2 · k */
} synth_lap_t;

typedef struct {                /* truth at one instant */
    int    table;               /* lap table index (0 = the table the run starts in) */
    double s_m;                 /* lap-frame arc length, 0 ≤ s < length_m */
    double s_total_m;           /* unwrapped: table·length_m + s_m */
    double e_m, n_m;
    double heading_deg;         /* compass, [0, 360) */
    double v_mps, a_lon_mps2;
    double g_lat, lean_deg, yaw_rate_dps;
    bool   on_arc;
} synth_state_t;

typedef struct {
    double     s_m;             /* lap-frame arc length of the gate centre after adjustment onto a straight */
    double     e_m, n_m;        /* centre */
    double     heading_deg;     /* driving heading at the centre */
    trk_line_t line;            /* p1 = left end, p2 = right end in the driving direction (dir_sign +1) */
} synth_gate_t;

typedef struct {
    synth_cfg_t cfg;
    /* geometry, shared by every table */
    int    n_vertices;
    double ve[SYNTH_MAX_VERTICES], vn[SYNTH_MAX_VERTICES];      /* scaled polygon vertices, ENU, driving order */
    double straight_len[SYNTH_MAX_VERTICES];                    /* straight k runs from arc k's end to arc k+1's start */
    double arc_len[SYNTH_MAX_VERTICES];                         /* arc k is at vertex k */
    double turn_rad[SYNTH_MAX_VERTICES];                        /* signed turn at vertex k (+ = left) */
    double length_m;            /* Σ straight_len + Σ arc_len (== cfg.length_m within 1e-6) */
    int    n_gates;             /* 1 + cfg.n_sector_gates; gates[0] is S/F */
    synth_gate_t gates[SYNTH_MAX_GATES];
    /* run */
    int    n_tables;            /* cfg.laps + 3 */
    synth_lap_t tables[SYNTH_MAX_TABLES];
    double table_t0[SYNTH_MAX_TABLES];   /* lap-frame-accumulated time at which table i starts (table 0 starts at 0) */
    double s_total_start;       /* where t = 0 is: lap-frame s_start = wrap(s_sf - start_before_m), in table 0 */
    double s_total_end;         /* s_total of S/F crossing cfg.laps plus stop_after_m */
    double t_offset_s;          /* accumulated time at s_total_start; run time = accumulated time - t_offset_s */
    double duration_s;          /* run time at s_total_end */
} synth_run_t;

/* Builds geometry, gates and every lap table. Returns 0, or -1 with a message in err (may be NULL)
 * when the configuration is invalid: n_vertices, laps, gates or fractions out of range; corner arcs
 * that do not fit (tangent lengths exceed an edge); a straight shorter than 2·SYNTH_GATE_MARGIN_M;
 * v_corner ≥ v_max; non-positive accelerations; start_before_m + stop_after_m ≥ length_m. */
int    synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap);

/* Truth at run time t (clamped to [0, duration_s]). */
void   synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out);
/* Inverse: run time at which unwrapped arc length s_total is reached (s_total clamped to the run). */
double synth_run_time_at_s(const synth_run_t *r, double s_total_m);
/* s_total of the n-th S/F passage after the run start, n = 0..cfg.laps. */
double synth_run_sf_s_total(const synth_run_t *r, int n);
/* Run times of a full lap's crossings: out[0] = S/F starting lap_no, out[1..n_gates-1] = sector gates
 * in driving order, out[n_gates] = the S/F ending the lap. lap_no = 1..cfg.laps. Returns n_gates + 1. */
int    synth_run_lap_crossings(const synth_run_t *r, int lap_no, double *out, size_t out_cap);
/* Lap time = out[n_gates] - out[0] of the above; negative if lap_no is out of range. */
double synth_run_lap_time(const synth_run_t *r, int lap_no);
/* Fills a venue that passes trk_validate_venue: id cfg.venue_id, name "Synthetic", centre = origin,
 * radius VENUE_RADIUS_DEFAULT_M, one layout {id 1, "Full", dir_sign +1, sf = gates[0].line,
 * sectors = gates[1..].line, length_m rounded}. */
void   synth_run_venue(const synth_run_t *r, trk_venue_t *v);
/* Exact inverse of geo_to_enu for the same origin (see geo.c): lat = lat0 + n/R, lon = lon0 + e/(R·cos lat0). */
void   synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg);
#endif
```

`tools/replay/include/replay/synth_gps.h`:

```c
#ifndef REPLAY_SYNTH_GPS_H
#define REPLAY_SYNTH_GPS_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "replay/synth.h"

/* Deterministic sampling of a synth_run_t into GPS fixes and fused samples (spec §22.2).
 * Same seed → byte-identical output on every platform (integer RNG, IEEE doubles, no libm
 * randomness). */

typedef struct { uint64_t s; } synth_rng_t;              /* splitmix64 */
void     synth_rng_seed(synth_rng_t *g, uint64_t seed);
uint32_t synth_rng_u32(synth_rng_t *g);
double   synth_rng_uniform(synth_rng_t *g);              /* [0, 1) with 53 random bits */
double   synth_rng_gauss(synth_rng_t *g);                /* N(0, 1), Box–Muller (polar form, no cached value) */

/* First-order Gauss–Markov error: x_{k+1} = x_k·exp(-dt/τ) + σ·sqrt(1 - exp(-2dt/τ))·N(0,1). */
typedef struct { double sigma, tau_s, x; } synth_gm_t;
void   synth_gm_init(synth_gm_t *m, double sigma, double tau_s, synth_rng_t *g);   /* stationary start: x ~ N(0, σ) */
double synth_gm_step(synth_gm_t *m, double dt_s, synth_rng_t *g);                 /* advances and returns x */

typedef struct {
    int      rate_hz;               /* 5 or 10; default 5 */
    double   pos_sigma_m;           /* default 1.5 */
    double   pos_tau_s;             /* default 20 */
    double   speed_sigma_mps;       /* default 0.05 */
    double   head_sigma_deg;        /* default 0.5 */
    double   latency_ms;            /* mean arrival latency; default 80 */
    double   jitter_ms;             /* arrival = gps time + latency + U(-jitter, +jitter); default 20 */
    double   hacc_m;                /* reported hacc; default 1.5 */
    uint8_t  sats;                  /* default 9 */
    uint16_t pdop_e2;               /* default 180 */
    int32_t  alt_mm;                /* reported altitude (constant); default 100000 */
    int64_t  t0_gps_us;             /* UTC of run time 0; default tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0) */
    int64_t  t0_mono_us;            /* monotonic clock at run time 0; default 1000000 */
    double   dropout_start_s, dropout_end_s;   /* no fixes while start ≤ t < end; default 0, 0 (none) */
    uint32_t seed;                  /* default 1 */
} synth_gps_cfg_t;
void synth_gps_cfg_defaults(synth_gps_cfg_t *c);

typedef struct {
    synth_gps_cfg_t cfg;
    synth_rng_t     rng;
    synth_gm_t      gm_e, gm_n;
    uint32_t        k;              /* index of the next sample; t_k = k / rate_hz */
    double          last_t_s;       /* time of the previous Gauss–Markov step */
} synth_gps_t;
void synth_gps_init(synth_gps_t *s, const synth_gps_cfg_t *cfg);

/* Produces the next fix at t_k = k / rate_hz. Returns 1 and fills fix (and t_true_s if not NULL);
 * returns 0 when t_k lies in the dropout window (k still advances, the error process still steps);
 * returns -1 when t_k > synth run duration. Fields: gps_us = t0_gps_us + k·1e6/rate (exact integer),
 * mono_us = t0_mono_us + t_k·1e6 + latency + jitter, lat/lon from truth + Gauss–Markov error,
 * alt_mm = cfg.alt_mm, gspeed_mms = truth + N(0, σv), head_e5 = truth + N(0, σh) wrapped to [0, 360),
 * hacc_mm = hacc_m·1000, sacc_mms = 50, pdop_e2, fix_type 3, sats, flags = FIXOK|TIME|DATE, valid 1. */
int  synth_gps_next(synth_gps_t *s, const synth_run_t *r, gps_fix_t *fix, double *t_true_s);

/* Truth fused sample at run time t (no noise): mono_us/gps_us from the same t0 pair, g_lon = a_lon/G,
 * g_lat, lean, yaw from the state, g_comb = sqrt(g_lon² + g_lat²), flags = FUS_LEAN_VALID|FUS_ORIENT_OK. */
void synth_fused_at(const synth_run_t *r, double t_s, int64_t t0_gps_us, int64_t t0_mono_us, fused_sample_t *out);
#endif
```

`tools/replay/include/replay/logio.h`:

```c
#ifndef REPLAY_LOGIO_H
#define REPLAY_LOGIO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "core/types.h"
#include "core/ses.h"

/* Session .log writer and reader built on the ses_* codecs (spec §12). Host only. */

typedef struct {
    FILE    *f;                 /* file mode, or NULL */
    uint8_t *mem; size_t cap;   /* memory mode, or NULL */
    size_t   len;               /* bytes written so far (both modes) */
    ses_fix_state_t   fix_st;
    ses_fused_state_t fused_st;
    uint32_t frames;
    int      err;               /* sticky: 0 ok, -1 after any failed write */
} logw_t;

int  logw_open_file(logw_t *w, const char *path);            /* 0 / -1 */
void logw_open_mem(logw_t *w, uint8_t *buf, size_t cap);      /* writes fail (err = -1) once cap is exceeded */
/* Each returns the frame length written, or -1 (and sets err). */
int  logw_hdr(logw_t *w, const ses_hdr_t *h);
int  logw_venue(logw_t *w, uint16_t venue_id, uint16_t layout_id, const char *name);
int  logw_time_map(logw_t *w, int64_t mono_us, int64_t gps_us, uint8_t quality);
int  logw_fix(logw_t *w, const gps_fix_t *fix);             /* FIX_KEY / FIX_DELTA via fix_st; also ses_fused_state_on_fix */
int  logw_fused(logw_t *w, const fused_sample_t *fs);
int  logw_lap(logw_t *w, const lap_result_t *lap);
int  logw_sector(logw_t *w, uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms);
int  logw_drag_run(logw_t *w, const drag_result_t *run);
int  logw_drag_gate(logw_t *w, uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm);
int  logw_event(logw_t *w, int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg);
int  logw_calib(logw_t *w, const ses_calib_t *c);
int  logw_mark(logw_t *w, int64_t gps_us, uint8_t kind);
int  logw_power(logw_t *w, int64_t mono_us, uint8_t state, uint16_t batt_mv);
int  logw_end(logw_t *w, int64_t gps_us, uint8_t reason);
int  logw_close(logw_t *w);                                  /* flushes and closes the file; returns err */

typedef struct {
    void (*on_hdr)(const ses_hdr_t *h, void *ctx);
    void (*on_venue)(const ses_venue_t *v, void *ctx);
    void (*on_time_map)(const ses_time_map_t *t, void *ctx);
    void (*on_fix)(const gps_fix_t *fix, void *ctx);          /* reconstructed absolute fix */
    void (*on_fused)(const fused_sample_t *fs, void *ctx);
    void (*on_lap)(const lap_result_t *lap, void *ctx);
    void (*on_sector)(const ses_sector_t *s, void *ctx);
    void (*on_drag_run)(const drag_result_t *run, void *ctx);
    void (*on_drag_gate)(const ses_drag_gate_t *g, void *ctx);
    void (*on_event)(const ses_event_t *e, void *ctx);
    void (*on_calib)(const ses_calib_t *c, void *ctx);
    void (*on_mark)(const ses_mark_t *m, void *ctx);
    void (*on_power)(const ses_power_t *p, void *ctx);
    void (*on_end)(const ses_end_t *e, void *ctx);
    void (*on_bad)(uint8_t type, uint8_t len, void *ctx);     /* framed OK but the decoder rejected it, or unknown type */
} logr_cb_t;                                                  /* any member may be NULL */

typedef struct {
    ses_reader_t      rd;
    ses_fix_state_t   fix_st;
    ses_fused_state_t fused_st;
    const logr_cb_t  *cb;
    void             *ctx;
    uint32_t n_frames;          /* frames the framing layer accepted */
    uint32_t n_bad;             /* decoder rejections + unknown types */
    uint32_t n_by_type[128];    /* accepted frames per record type */
} logr_t;

void logr_init(logr_t *r, const logr_cb_t *cb, void *ctx);
void logr_feed(logr_t *r, const uint8_t *buf, size_t n);
void logr_finish(logr_t *r);                                  /* ses_reader_flush at EOF */
int  logr_read_file(logr_t *r, const char *path);             /* feed in 4096-byte chunks then finish; 0 / -1 on open or read error */
#endif
```

`tools/replay/include/replay/replay.h`:

```c
#ifndef REPLAY_REPLAY_H
#define REPLAY_REPLAY_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "core/ses.h"
#include "core/types.h"

const char *replay_version(void);                 /* == core_version() */

/* Summary of one .log (session 2.1 skeleton; session 2.7 adds the engine replay). */
typedef struct {
    ses_hdr_t hdr;      int have_hdr;
    ses_venue_t venue;  int have_venue;
    uint32_t n_frames, n_bad, n_by_type[128];
    uint32_t n_fix, n_fix_valid, n_fused, n_lap, n_sector, n_drag_run, n_drag_gate, n_event, n_time_map;
    int64_t  first_fix_gps_us, last_fix_gps_us;   /* 0 when no fix */
    int32_t  max_gspeed_mms;
    uint16_t n_laps_listed;
    lap_result_t laps[64];                         /* first 64 LAP records */
    ses_end_t end;      int have_end;
} replay_summary_t;

int  replay_summarize_file(const char *path, replay_summary_t *out);   /* 0 / -1 on I/O error */
void replay_print_text(const replay_summary_t *s, FILE *f);
void replay_print_json(const replay_summary_t *s, FILE *f);            /* one JSON object, keys documented in replay_summary.c */
#endif
```

`tools/replay/lib/replay_version.c`:

```c
#include "replay/replay.h"
#include "core/core.h"

const char *replay_version(void)
{
    return core_version();
}
```

- [ ] **Step 3: Write the tools CMake file**

`tools/replay/CMakeLists.txt`:

```cmake
# Host-only tools (spec §21.4, §22.2). Built by test/CMakeLists.txt through add_subdirectory; never an
# ESP-IDF component. Sources are globbed so parallel tasks add files without touching this list.
file(GLOB REPLAY_LIB_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)
add_library(replaylib STATIC ${REPLAY_LIB_SRCS})
target_include_directories(replaylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(replaylib PUBLIC core m)
target_compile_options(replaylib PRIVATE ${LAPTIMER_STRICT_FLAGS})

foreach(tool synth replay)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_main.c)
    add_executable(${tool} ${tool}_main.c)
    target_link_libraries(${tool} PRIVATE replaylib)
    target_compile_options(${tool} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  endif()
endforeach()

file(GLOB REPLAY_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/test_*.c)
foreach(src ${REPLAY_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE replaylib unity Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

if(TARGET synth AND TARGET replay)
  add_test(NAME synth_smoke COMMAND synth --out ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke --laps 2 --quiet)
  add_test(NAME replay_smoke COMMAND replay ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke.log --json)
  set_tests_properties(replay_smoke PROPERTIES DEPENDS synth_smoke PASS_REGULAR_EXPRESSION "\"bad_frames\":0")
endif()
```

- [ ] **Step 4: Update the host CMake file**

Replace `test/CMakeLists.txt` with exactly:

```cmake
cmake_minimum_required(VERSION 3.16)
project(laptimer_host C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# One strict flag set for core, tools and tests (spec §17.9, §21.4).
set(LAPTIMER_STRICT_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/core)
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC ${CORE_DIR}/include)
target_compile_options(core PRIVATE ${LAPTIMER_STRICT_FLAGS})
# vendored third-party sources get relaxed warnings (file added in Task 8)
set_source_files_properties(${CORE_DIR}/util/jsmn.c PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-unused-function")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # -fno-sanitize-recover makes a UBSan finding abort the test run instead of printing and continuing,
  # so a ctest pass really means no undefined behaviour was executed.
  target_compile_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  target_link_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)

enable_testing()
find_package(Threads REQUIRED)
function(add_core_test name)
  add_executable(${name} ${name}.c)
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE core unity m Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no edit here)
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_core_test(${name})
endforeach()

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)
```

Update the `test/CMakeLists.txt` block in `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` (Task 1 there, and any later step that restates the file) to this exact content, so that plan stays byte-identical to the file.

- [ ] **Step 5: Configure, build, run**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -2 && cmake --build test/build --parallel 2>&1 | tail -3 && ctest --test-dir test/build --output-on-failure 2>&1 | tail -4`
Expected: 13 tests pass (the 12 core suites plus `test_replay_smoke`); no `synth`/`replay` executables yet (their `_main.c` files do not exist).

- [ ] **Step 6: Spec write-backs**

In `docs/superpowers/specs/2026-09-14-lap-timer-design.md`:

(a) §4.2, replace the line

```
    replay/CMakeLists.txt replay.c
```

with

```
    replay/CMakeLists.txt        host-only tools, built by test/CMakeLists.txt (§21.4, §22.2)
    replay/include/replay/*.h    synth.h synth_gps.h logio.h replay.h synth_truth.h
    replay/lib/*.c               synth_track.c synth_gps.c synth_truth.c logio.c replay_summary.c replay_version.c
    replay/synth_main.c replay/replay_main.c
    replay/test/test_*.c         Unity tests for the tools (host only; not compiled into core_selftest)
```

(b) §21.4, append after "No IDF, no network.":

```
`tools/replay` is added with `add_subdirectory` and builds `replaylib` (the synthetic model, the
`.log` writer/reader and the replay summary), the `synth` and `replay` executables, and one test
executable per `tools/replay/test/test_*.c`, all registered with the same `ctest`.
```

(c) §22.2, replace the whole section body (the three bullets) with:

```
- `replay <session.log|capture.ubx> [--imu capture.csv] [--venue id] [--mode lap|drag] [--json]` runs the pipeline core (same call sequence as `app/pipeline` minus IDF) and prints laps/sectors/runs. `--json` emits a machine-readable result compared against `test/data/<name>.expected.json` by `ctest`. Until the engines exist (session 2.7), `replay <file.log> [--json]` prints a summary of the decoded records (counts per type, fix time span, laps listed).
- `synth` (`tools/replay/synth_main.c`, model in `lib/synth_track.c`, sampling in `lib/synth_gps.c`) generates a polygonal circuit (default 12 vertices, 2.5 km) whose corners are circular arcs (default radius 40 m) driven at a constant corner speed (default 15 m/s); every straight carries a trapezoidal profile (accelerate at 3 m/s², cruise at a per-lap `v_max` of 50 m/s ± 3 %, brake at 6 m/s²). The kinematics are piecewise constant-acceleration, so the position, speed, heading, longitudinal g, lateral g, lean and yaw rate at any instant and every gate crossing time are closed-form. GPS sampling at 5 or 10 Hz adds a first-order Gauss–Markov position error (σ = 1.5 m, τ = 20 s), white speed noise (σ = 0.05 m/s), white heading noise (σ = 0.5°), a fixed arrival latency (80 ms) with uniform jitter (±20 ms), and an optional dropout window. Outputs for `--out <prefix>`: `<prefix>.log` (SESSION_HDR, VENUE, TIME_MAP, FIX_*, FUSED at 10 Hz from truth, END), `<prefix>.truth.json` (configuration, lap length, gate positions and lines, per-lap crossing times in run seconds and `gps_us`, lap and sector times) and `<prefix>.venue.json` (§10.2 schema, loadable with `trk_from_json`). Acceptance: at 5 Hz, |error| ≤ 30 ms for 95 % of crossings; at 10 Hz ≤ 15 ms.
- Fixtures in `test/data/`: `killarney_full.log`, `killarney_short.log`, `killarney_full_rev.log`, `zwartkops.log`, `drag_0_180.log`, `drag_0_320.log`, `gps_dropout.log`, `truncated.log`. Initially synthetic; replaced by real device captures as they are recorded (first real captures are the Phase 1 exit criterion).
```

(d) §23, replace the row

```
| `tools/replay/` | C | offline pipeline (§22.2) | CLI |
```

with

```
| `tools/replay/` | C | offline pipeline `replay` and synthetic fixture generator `synth` (§22.2) | CLI |
```

Verify with `grep -n "replay/include/replay" docs/superpowers/specs/2026-09-14-lap-timer-design.md` (one hit) and `grep -c "synth_main.c" docs/superpowers/specs/2026-09-14-lap-timer-design.md` (2).

- [ ] **Step 7: Hygiene and commit**

Run: `git diff --check && git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin' ':!components/core/include/core/jsmn.h'`
Expected: no output.

```bash
git add tools/replay test/CMakeLists.txt docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md
git commit -m "build(tools): replay/synth scaffolding, shared headers and host CMake wiring

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 2: Closed-form circuit and run model (`tools/replay/lib/synth_track.c`) [parallel group 1]

**Files:**
- Create: `tools/replay/lib/synth_track.c`
- Test: `tools/replay/test/test_synth_track.c`
- Modify: nothing (`tools/replay/CMakeLists.txt` globs both `lib/*.c` and `test/test_*.c`)

**Interfaces:**
- Consumes: `replay/synth.h` (Task 1, verbatim — not edited here); `core/geo.h` (`GEO_PI`,
  `GEO_EARTH_R_M`, `geo_origin_t`, `geo_origin_set`, `geo_to_enu`, `geo_segment_cross`,
  `geo_interp_time` — the last three in the test only); `core/consts.h` (`G_MPS2`,
  `GATE_HALF_WIDTH_M`, `VENUE_RADIUS_DEFAULT_M`); `core/trk.h` (`trk_venue_t`, `trk_layout_t`,
  `trk_line_t`, `TRK_USER_ID_BASE`, `TRK_F_UNVERIFIED`, and in the test `trk_validate_venue`,
  `trk_to_json`, `trk_from_json`); `core/types.h` (`LAP_MAX_SECTORS`). **The RNG used here is
  private to `synth_track.c` (`sr_seed`/`sr_next`/`sr_sym`, splitmix64) and is deliberately NOT
  `synth_rng_*` from `replay/synth_gps.h`** — that stream belongs to Task 3's `synth_gps.c`, which
  this task must not link against or depend on. Both are deterministic; they need not agree.
- Produces (every symbol declared in `replay/synth.h`): `void synth_cfg_defaults(synth_cfg_t *c)`;
  `int synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap)`;
  `void synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out)`;
  `double synth_run_time_at_s(const synth_run_t *r, double s_total_m)`;
  `double synth_run_sf_s_total(const synth_run_t *r, int n)`;
  `int synth_run_lap_crossings(const synth_run_t *r, int lap_no, double *out, size_t out_cap)`;
  `double synth_run_lap_time(const synth_run_t *r, int lap_no)`;
  `void synth_run_venue(const synth_run_t *r, trk_venue_t *v)`;
  `void synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg)`.

**Model (binding).** Geometry: vertex *k* of the unit polygon sits at compass bearing φ_k = 2πk/N
(walked clockwise when `cfg.clockwise`, anticlockwise otherwise, so driving k = 0, 1, … runs the
requested way) at radius ρ_k = 1 + `irregularity`·U(−1, 1) from the private RNG seeded with
`cfg.seed`. Edge *k* runs vertex *k* → *k*+1; `turn_rad[k]` is the signed angle from edge *k*−1's
direction to edge *k*'s (+ = left, so a clockwise regular N-gon turns −2π/N at every vertex).
Corner *k* is a circular arc of radius `corner_radius_m`, `arc_len[k]` = r·|turn|, tangent length
T_k = r·tan(|turn|/2) taken off each adjacent edge. With unit edge lengths u_k the driven length is
P(S) = S·Σu_k − 2ΣT_k + Σarc_len_k, so S = (`cfg.length_m` + 2ΣT − Σarc)/Σu; `ve`/`vn` = S·unit
vertices and `straight_len[k]` = S·u_k − T_k − T_{k+1}. Reject when any `straight_len` <
2·`SYNTH_GATE_MARGIN_M`, when T_k + T_{k+1} ≥ S·u_k, or when a cfg range check fails. `length_m` is
the exact sum of the straights and arcs.

Lap frame: s = 0 at the start of straight 0 (the exit tangent point of the arc at vertex 0); driving
order is straight 0, arc 1, straight 1, arc 2, …, straight N−1, arc 0. **Table *i* draws one speed
scale k_i = 1 + `cfg.lap_var`·U(−1, 1) from the same RNG after the N vertex radii (table 0 draws
first) and sets `tables[i].v_corner_mps` = `cfg.v_corner_mps`·k_i and `tables[i].v_max_mps` =
`cfg.v_max_mps`·k_i; every piece of that table uses the table's two speeds** — the arcs are driven at
`v_corner_mps`, the straights enter and leave at it, and the peak/cruise formulas take the table's
cap. Because the ratio `v_max`/`v_corner` is scale-invariant, `check_cfg` only needs
`v_max_mps > v_corner_mps`, and lap times differ under a non-zero `lap_var` even on a track whose
straights never cruise. A straight is up to three constant-acceleration pieces — ACCEL
(v0 = v_corner, a = a_acc), CRUISE (v = the table's v_max, a = 0) when reached, BRAKE (a = −a_brk)
ending at v_corner exactly at the straight's end. Without cruise the peak satisfies
v_p² = (2·a_acc·a_brk·L + v_corner²·(a_acc + a_brk))/(a_acc + a_brk); with cruise
d_acc = (v_max² − v_corner²)/(2a_acc), d_brk = (v_max² − v_corner²)/(2a_brk), d_cruise =
L − d_acc − d_brk. An arc is one piece at constant v_corner with signed `turn_rad` and its centre
`corner_radius_m` to the inside of the turn, perpendicular to the entry heading. Durations:
ACCEL/BRAKE (v_end − v0)/a, CRUISE/ARC len/v. `n_tables` = `cfg.laps` + 3; `table_t0[i]` = Σ lap
times of the earlier tables.

Run frame: s_sf = the adjusted S/F arc length; s_start = wrap(s_sf − `start_before_m`) in table 0;
`t_offset_s` = accumulated time at s_start; run time t = accumulated time T − `t_offset_s`. S/F
crossing *n* is at s_total = s_sf + n·`length_m` + (s_start > s_sf ? `length_m` : 0)
(`synth_run_sf_s_total`); `s_total_end` = sf_s_total(laps) + `stop_after_m`; `duration_s` =
`synth_run_time_at_s(s_total_end)`. `synth_run_state_at` and `synth_run_time_at_s` are exact
inverses (measured round-trip residual 3.4e-13 s).

Gates: requested S/F at `cfg.sf_frac`·`length_m`, sector gate *i* at s_sf + i·`length_m`/(n+1),
wrapped. A request on an arc, or within `SYNTH_GATE_MARGIN_M` of a straight's start or end, moves
forward to the first admissible point (a straight's start + margin, wrapping past the lap end); two
gates landing on the same point is a cfg error. The line is perpendicular to the driving heading
with half-length `cfg.gate_half_width_m`: for compass heading h the ENU heading unit vector is
(sin h, cos h) and the left normal is (−cos h, sin h), so `p1` = centre + half·left and `p2` =
centre − half·left give sign(cross(p2 − p1, motion)) = +1, the `dir_sign` of §6.4 / §10.2.

- [ ] **Step 1: Write the failing test**

`tools/replay/test/test_synth_track.c`:

```c
#include "unity.h"
#include "replay/synth.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/trk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* synth_run_t is ~2.7 MB (203 lap tables x 128 pieces), so every run lives on the heap. */
static synth_run_t *run;
static char errbuf[128];

void setUp(void)   { run = malloc(sizeof *run); TEST_ASSERT_NOT_NULL(run); errbuf[0] = '\0'; }
void tearDown(void){ free(run); run = NULL; }

#define PI 3.14159265358979323846

/* The default 12-gon's 187 m straights never reach v_max, so the cruise branch needs a track with
 * long straights: a 4-gon of 4000 m has 937 m straights. */
static void long_straight_cfg(synth_cfg_t *c)
{
    synth_cfg_defaults(c);
    c->n_vertices = 4;
    c->length_m   = 4000.0;
}

static double norm_deg(double d) { d = fmod(d, 360.0); if (d < 0.0) d += 360.0; return d; }

/* End point, heading and speed of a piece, recomputed from its stored fields alone. */
static void piece_end(const synth_piece_t *p, double *e, double *n, double *head_deg, double *v)
{
    if (p->kind == SYNTH_P_ARC) {
        double cs = cos(p->turn_rad), sn = sin(p->turn_rad);
        double vx = p->e0 - p->ce, vy = p->n0 - p->cn;
        *e = p->ce + vx * cs - vy * sn;
        *n = p->cn + vx * sn + vy * cs;
        *head_deg = norm_deg((p->head0_rad - p->turn_rad) * 180.0 / PI);
    } else {
        *e = p->e0 + p->len * sin(p->head0_rad);
        *n = p->n0 + p->len * cos(p->head0_rad);
        *head_deg = norm_deg(p->head0_rad * 180.0 / PI);
    }
    *v = p->v0 + p->a * p->dur;
}

/* ------------------------------------------------------------------ 1 */
static void test_default_geometry_is_a_regular_12gon(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    TEST_ASSERT_EQUAL_INT(12, run->n_vertices);
    /* length_m is the exact sum of the driven pieces; the scale factor solves for cfg.length_m, so
     * the only error is floating point on a ~2.5e3 sum (about 1e-12 m). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2500.0, run->length_m);

    const double turn = -2.0 * PI / 12.0;          /* clockwise: every corner is a right turn */
    const double arc  = 40.0 * (2.0 * PI / 12.0);  /* r * |turn| */
    double sum = 0.0;
    for (int k = 0; k < 12; k++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, turn, run->turn_rad[k]);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, arc, run->arc_len[k]);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->straight_len[0], run->straight_len[k]);   /* 12 equal straights */
        sum += run->straight_len[k] + run->arc_len[k];
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->length_m, sum);
    /* straight = (2500 - 12*arc)/12 for a regular polygon */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (2500.0 - 12.0 * arc) / 12.0, run->straight_len[0]);
    TEST_ASSERT_EQUAL_INT(3, run->n_gates);
    TEST_ASSERT_EQUAL_INT(13, run->n_tables);      /* cfg.laps + 3 */
}

/* ------------------------------------------------------------------ 2 */
static void test_lap_var_scales_accelerations_and_cap(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    for (int i = 0; i < run->n_tables; i++) {
        const synth_lap_t *tb = &run->tables[i];
        TEST_ASSERT_TRUE(fabs(tb->k - 1.0) <= 0.03 + 1e-12);              /* k = 1 + lap_var*U(-1,1) */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0 * tb->k, tb->a_acc_mps2);    /* the same factor on every scaled field */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 6.0 * tb->k, tb->a_brk_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0 * tb->k, tb->v_max_mps);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 15.0, tb->pieces[0].v0);   /* accel enters at the constant v_corner */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 15.0, tb->pieces[2].v0);   /* the arc is driven at v_corner too */
    }
    TEST_ASSERT_TRUE(fabs(run->tables[0].k - run->tables[1].k) > 1e-6);
    /* Default lap_var: consecutive laps of the default track differ even though v_corner never moves,
     * because the accelerations and the cruise cap do. */
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(run, 1) - synth_run_lap_time(run, 2)) > 1e-3);

    synth_cfg_t c0; synth_cfg_defaults(&c0); c0.lap_var = 0.0;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c0, errbuf, sizeof errbuf));
    for (int i = 0; i < run->n_tables; i++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, run->tables[i].k);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0, run->tables[i].a_acc_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 6.0, run->tables[i].a_brk_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0, run->tables[i].v_max_mps);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[0].lap_time_s, run->tables[i].lap_time_s);  /* identical with lap_var 0 */
    }
}

static void test_default_straights_are_accel_brake_only(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[0];
    /* 187.4 m straights at ~3 m/s^2 from ~15 m/s peak at ~31 m/s, far below v_max = 50, so the
     * default track has NO cruise piece: 12 straights x 2 pieces + 12 arcs = 36. */
    TEST_ASSERT_EQUAL_INT(36, tb->n_pieces);
    const double L = run->straight_len[0], vc = c.v_corner_mps, aa = tb->a_acc_mps2, ab = tb->a_brk_mps2;
    double vp2 = (2.0 * aa * ab * L + vc * vc * (aa + ab)) / (aa + ab);
    TEST_ASSERT_TRUE(sqrt(vp2) < tb->v_max_mps);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ACCEL, tb->pieces[0].kind);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_BRAKE, tb->pieces[1].kind);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ARC,   tb->pieces[2].kind);
    double v_peak = tb->pieces[0].v0 + tb->pieces[0].a * tb->pieces[0].dur;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, sqrt(vp2), v_peak);          /* no-cruise peak formula */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vp2 - vc * vc) / (2.0 * aa), tb->pieces[0].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, L, tb->pieces[0].len + tb->pieces[1].len);
    /* The arc is one piece at the constant corner speed. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->arc_len[1] / vc, tb->pieces[2].dur);
}

static void test_long_straights_reach_cruise(void)
{
    synth_cfg_t c; long_straight_cfg(&c); c.lap_var = 0.0;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[0];
    TEST_ASSERT_EQUAL_INT(16, tb->n_pieces);                      /* 4 x (accel, cruise, brake) + 4 arcs */
    TEST_ASSERT_EQUAL_INT(SYNTH_P_CRUISE, tb->pieces[1].kind);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0, tb->pieces[1].v0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, tb->pieces[1].a);
    const double vc = 15.0, aa = 3.0, ab = 6.0, vm = 50.0;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vm * vm - vc * vc) / (2.0 * aa), tb->pieces[0].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vm * vm - vc * vc) / (2.0 * ab), tb->pieces[2].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->straight_len[0],
                              tb->pieces[0].len + tb->pieces[1].len + tb->pieces[2].len);
}

static void test_piece_table_is_continuous_and_matches_integration(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[1];
    for (int i = 0; i < tb->n_pieces; i++) {
        const synth_piece_t *p = &tb->pieces[i];
        const synth_piece_t *q = &tb->pieces[(i + 1) % tb->n_pieces];
        TEST_ASSERT_TRUE(p->len > 0.0);
        TEST_ASSERT_TRUE(p->dur > 0.0);
        double e, n, hd, v;
        piece_end(p, &e, &n, &hd, &v);
        /* 1e-9 m / 1e-9 m/s / 1e-9 deg: these are exact identities, the slack is float rounding on
         * coordinates of order 500 m (eps*500 ~ 1e-13). */
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->v0, v);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->e0, e);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->n0, n);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, norm_deg(q->head0_rad * 180.0 / PI), hd);
        if (i + 1 < tb->n_pieces) {
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->s0, p->s0 + p->len);
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->t0, p->t0 + p->dur);
        }
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->length_m,
                              tb->pieces[tb->n_pieces - 1].s0 + tb->pieces[tb->n_pieces - 1].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, tb->lap_time_s,
                              tb->pieces[tb->n_pieces - 1].t0 + tb->pieces[tb->n_pieces - 1].dur);

    /* Independent midpoint integration of int ds/v(s) with v(s) = sqrt(v0^2 + 2a(s-s0)), ~1e5 steps
     * split at the piece boundaries so the integrand is smooth inside each cell. The midpoint error
     * is O(h^2) per piece; measured residual is ~1.2e-6 s, so 1e-4 s is a safe bound. */
    double t_num = 0.0;
    const int nsub = 100000 / tb->n_pieces;
    for (int i = 0; i < tb->n_pieces; i++) {
        const synth_piece_t *p = &tb->pieces[i];
        double h = p->len / (double)nsub;
        for (int k = 0; k < nsub; k++) {
            double ds = ((double)k + 0.5) * h;
            t_num += h / sqrt(p->v0 * p->v0 + 2.0 * p->a * ds);
        }
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, tb->lap_time_s, t_num);
}

/* ------------------------------------------------------------------ 2b (review I1) */
/* Table boundaries sit at s = 0, the arc-at-vertex-0 -> straight-0 transition. Truth speed must be
 * continuous there (I1's fix: v_corner is a single cfg-level constant, never scaled per table, so
 * every table both ends its closing arc and starts its opening ACCEL at exactly v_corner). a_lon is
 * NOT continuous there -- every arc-to-straight transition in the lap, including this one, carries the
 * ordinary a = 0 (arc) -> a = a_acc_mps2 (straight ACCEL) step -- but each side must resolve to
 * exactly the value its own piece/table defines, with no boundary-selection glitch. */
static void check_truth_continuous_at_table_boundaries(const synth_run_t *r)
{
    int checked = 0;
    for (int i = 1; i < r->n_tables; i++) {
        double t = r->table_t0[i] - r->t_offset_s;
        if (t <= 0.0 || t >= r->duration_s) continue;      /* this boundary isn't reached by the run */
        /* eps = 1e-9 s: the "after" side is already inside the straight's ACCEL piece, so v is
         * drifting there at up to a_acc_mps2 (~3 m/s^2, ~4.5 with lap_var = 0.5); a 1e-6 s window
         * would itself admit a ~3e-6 to 4.5e-6 m/s drift that has nothing to do with the table
         * boundary, swamping a 1e-6 m/s tolerance. 1e-9 s keeps that drift near 3e-9-4.5e-9 m/s,
         * three orders below the tolerance, while remaining far above double's ~1e-13 s resolution
         * at these run-time magnitudes (up to ~1e3 s). */
        synth_state_t a, b;
        synth_run_state_at(r, t - 1e-9, &a);
        synth_run_state_at(r, t + 1e-9, &b);
        TEST_ASSERT_TRUE(fabs(b.v_mps - a.v_mps) < 1e-6);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, a.a_lon_mps2);                     /* still on the closing arc */
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, r->tables[i].a_acc_mps2, b.a_lon_mps2); /* table i's own ACCEL rate */
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 0);          /* the run must actually reach at least one table boundary */
}

static void test_truth_speed_continuous_across_tables(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    check_truth_continuous_at_table_boundaries(run);

    synth_cfg_t c5; synth_cfg_defaults(&c5); c5.lap_var = 0.5;
    synth_run_t *r5 = malloc(sizeof *r5);
    TEST_ASSERT_NOT_NULL(r5);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(r5, &c5, errbuf, sizeof errbuf));
    check_truth_continuous_at_table_boundaries(r5);
    free(r5);
}

/* ------------------------------------------------------------------ 3 */
static void test_state_at_and_time_at_s_are_inverses(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    /* Round trip t -> s_total -> t. Worst measured residual is 3.4e-13 s (cancellation in
     * s_total - table*length_m at s_total ~ 2.8e4 m divided by v >= 14.5 m/s). */
    const double v_min = c.v_corner_mps;    /* v_corner is constant across every table; nothing is slower */
    for (int i = 0; i <= 500; i++) {
        double t = run->duration_s * (double)i / 500.0;
        synth_state_t st;
        synth_run_state_at(run, t, &st);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, t, synth_run_time_at_s(run, st.s_total_m));
        TEST_ASSERT_TRUE(st.s_m >= 0.0 && st.s_m < run->length_m);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, st.s_total_m, (double)st.table * run->length_m + st.s_m);
        TEST_ASSERT_TRUE(st.v_mps >= v_min - 1e-9);
        TEST_ASSERT_TRUE(st.heading_deg >= 0.0 && st.heading_deg < 360.0);
    }
}

/* ------------------------------------------------------------------ 4 */
static void test_arc_kinematics_signs(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    /* Table 1 piece 2 is the arc at vertex 1; pick its midpoint in run time. */
    const synth_piece_t *p = &run->tables[1].pieces[2];
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ARC, p->kind);
    const double vc = c.v_corner_mps;      /* constant: every table drives every arc at v_corner */
    double t_mid = run->table_t0[1] + p->t0 + 0.5 * p->dur - run->t_offset_s;
    synth_state_t st;
    synth_run_state_at(run, t_mid, &st);
    TEST_ASSERT_TRUE(st.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, vc, st.v_mps);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, st.a_lon_mps2);
    const double g_exp = vc * vc / (40.0 * G_MPS2);               /* v^2/(r*g), + because right turn */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, g_exp, st.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, atan(g_exp) * 180.0 / PI, st.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -(vc / 40.0) * 180.0 / PI, st.yaw_rate_dps);   /* -v/r, + = left */

    /* A point on a straight carries no lateral load. */
    const synth_piece_t *s0 = &run->tables[1].pieces[0];
    synth_state_t stz;
    synth_run_state_at(run, run->table_t0[1] + s0->t0 + 0.5 * s0->dur - run->t_offset_s, &stz);
    TEST_ASSERT_FALSE(stz.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.yaw_rate_dps);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[1].a_acc_mps2, stz.a_lon_mps2);   /* table 1's own scaled rate */

    /* Anticlockwise is the same track driven the other way round: all three signs flip. The rider
     * factor comes from the same RNG stream (clockwise doesn't change how many draws are consumed or
     * in what order), so table 1's factor -- and hence v_corner, which is unaffected by it anyway --
     * is the same draw. */
    synth_cfg_t ca; synth_cfg_defaults(&ca); ca.clockwise = false;
    synth_run_t *acw = malloc(sizeof *acw);
    TEST_ASSERT_NOT_NULL(acw);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(acw, &ca, errbuf, sizeof errbuf));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0 * PI / 12.0, acw->turn_rad[1]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[1].k, acw->tables[1].k);
    const synth_piece_t *pa = &acw->tables[1].pieces[2];
    synth_state_t sa;
    synth_run_state_at(acw, acw->table_t0[1] + pa->t0 + 0.5 * pa->dur - acw->t_offset_s, &sa);
    TEST_ASSERT_TRUE(sa.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -g_exp, sa.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -atan(g_exp) * 180.0 / PI, sa.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, (vc / 40.0) * 180.0 / PI, sa.yaw_rate_dps);
    free(acw);
}

/* ------------------------------------------------------------------ 5 */
static void test_lap_crossings_and_lap_times(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    double t[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(run->n_gates + 1, synth_run_lap_crossings(run, 1, t, sizeof t / sizeof t[0]));
    for (int i = 0; i < run->n_gates; i++) TEST_ASSERT_TRUE(t[i + 1] > t[i]);
    double sectors = 0.0;
    for (int i = 0; i < run->n_gates; i++) sectors += t[i + 1] - t[i];
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[run->n_gates] - t[0], sectors);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[run->n_gates] - t[0], synth_run_lap_time(run, 1));

    /* S/F crossing 0 is exactly start_before_m past the run start, and lands on the S/F gate. */
    double t_sf0 = synth_run_time_at_s(run, synth_run_sf_s_total(run, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[0], t_sf0);
    synth_state_t st;
    synth_run_state_at(run, t_sf0, &st);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->gates[0].s_m, st.s_m);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300.0, st.s_total_m - run->s_total_start);

    /* Default lap_var: consecutive laps of the default track differ (the accelerations and cruise cap
     * move with the table, so it bites even though these straights never cruise). */
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(run, 1) - synth_run_lap_time(run, 2)) > 1e-3);

    /* Out of range and short buffers are rejected. */
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 0, t, sizeof t / sizeof t[0]));
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 11, t, sizeof t / sizeof t[0]));
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 1, t, (size_t)run->n_gates));
    /* synth_run_lap_time itself returns a negative sentinel, not a plausible 0.0, for a bad lap_no. */
    TEST_ASSERT_TRUE(synth_run_lap_time(run, 0) < 0.0);
    TEST_ASSERT_TRUE(synth_run_lap_time(run, 11) < 0.0);

    /* lap_var = 0: every lap is the table lap time exactly. */
    synth_cfg_t c0; synth_cfg_defaults(&c0); c0.lap_var = 0.0;
    synth_run_t *r0 = malloc(sizeof *r0);
    TEST_ASSERT_NOT_NULL(r0);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(r0, &c0, errbuf, sizeof errbuf));
    for (int lap = 1; lap <= c0.laps; lap++)
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, r0->tables[0].lap_time_s, synth_run_lap_time(r0, lap));
    free(r0);

    /* The same holds on the cruising track. */
    synth_cfg_t cv; long_straight_cfg(&cv);
    synth_run_t *rv = malloc(sizeof *rv);
    TEST_ASSERT_NOT_NULL(rv);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(rv, &cv, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(rv, 1) - synth_run_lap_time(rv, 2)) > 1e-3);
    free(rv);

    /* review I2: sf_frac = 0.5 puts s_sf = 1250 m, well past start_before_m = 300, so s_start does NOT
     * wrap behind S/F -- this exercises the "extra = 0" branch of synth_run_sf_s_total (every other
     * config in this file has s_sf < start_before_m, always the wrap branch) -- and because some
     * sector gates then land behind s_sf, it also exercises the wrap_s(...) branch inside
     * synth_run_lap_crossings that brings a "behind S/F" gate forward into the lap. */
    synth_cfg_t cw; synth_cfg_defaults(&cw); cw.sf_frac = 0.5;
    synth_run_t *rw = malloc(sizeof *rw);
    TEST_ASSERT_NOT_NULL(rw);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(rw, &cw, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(rw->s_total_start <= rw->gates[0].s_m);                                /* no wrap */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, rw->gates[0].s_m, synth_run_sf_s_total(rw, 0));          /* no "+ length_m" */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, cw.start_before_m, synth_run_sf_s_total(rw, 0) - rw->s_total_start);
    double tw[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(rw->n_gates + 1, synth_run_lap_crossings(rw, 1, tw, sizeof tw / sizeof tw[0]));
    for (int i = 0; i < rw->n_gates; i++) TEST_ASSERT_TRUE(tw[i + 1] > tw[i]);
    free(rw);
}

/* ------------------------------------------------------------------ 6 */
static void test_gate_geometry_and_crossing(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    geo_origin_t o;
    geo_origin_set(&o, c.origin_lat_deg, c.origin_lon_deg);
    double t[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(run->n_gates + 1, synth_run_lap_crossings(run, 1, t, sizeof t / sizeof t[0]));

    for (int i = 0; i < run->n_gates; i++) {
        geo_enu_t p1 = geo_to_enu(&o, run->gates[i].line.p1.lat, run->gates[i].line.p1.lon);
        geo_enu_t p2 = geo_to_enu(&o, run->gates[i].line.p2.lat, run->gates[i].line.p2.lon);
        /* 1e-6 m: the equirectangular round trip through degrees loses ~2e-10 m at this origin. */
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2.0 * GATE_HALF_WIDTH_M, hypot(p2.x - p1.x, p2.y - p1.y));
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, run->gates[i].e_m, 0.5 * (p1.x + p2.x));
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, run->gates[i].n_m, 0.5 * (p1.y + p2.y));

        /* Every gate sits at least SYNTH_GATE_MARGIN_M inside a straight, so a +/-0.5 s chord around
         * the crossing is a straight line at constant acceleration and §6.4 recovers it exactly. */
        synth_state_t a, b;
        synth_run_state_at(run, t[i] - 0.5, &a);
        synth_run_state_at(run, t[i] + 0.5, &b);
        TEST_ASSERT_FALSE(a.on_arc);
        TEST_ASSERT_FALSE(b.on_arc);
        geo_enu_t ea = { a.e_m, a.n_m }, eb = { b.e_m, b.n_m };
        double frac; int dir = 0;
        TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(ea, eb, p1, p2, &frac, &dir));
        TEST_ASSERT_EQUAL_INT(1, dir);                      /* p1 left, p2 right (§6.4, §10.2) */
        double d   = frac * hypot(eb.x - ea.x, eb.y - ea.y);
        double tau = geo_interp_time(d, a.v_mps, b.v_mps, 1.0);
        /* 1e-6 s: the interpolation is exact here, residual measured at 3.5e-12 s. */
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, t[i], t[i] - 0.5 + tau);
    }

    /* A requested S/F on the arc at vertex 1 moves forward to the start of straight 1 plus the
     * margin (straight 0 + arc 1 + SYNTH_GATE_MARGIN_M). 0.08 * 2500 = 200 m is inside that arc. */
    synth_cfg_t ca; synth_cfg_defaults(&ca); ca.sf_frac = 0.08; ca.n_sector_gates = 0;
    synth_run_t *ra = malloc(sizeof *ra);
    TEST_ASSERT_NOT_NULL(ra);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(ra, &ca, errbuf, sizeof errbuf));
    TEST_ASSERT_EQUAL_INT(1, ra->n_gates);
    TEST_ASSERT_TRUE(200.0 > ra->straight_len[0]);                      /* the request really is on the arc */
    TEST_ASSERT_TRUE(200.0 < ra->straight_len[0] + ra->arc_len[1]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, ra->straight_len[0] + ra->arc_len[1] + SYNTH_GATE_MARGIN_M,
                              ra->gates[0].s_m);
    free(ra);
}

/* ------------------------------------------------------------------ 7 */
static void test_venue_validates_and_round_trips(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    trk_venue_t v;
    synth_run_venue(run, &v);
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(&v));
    TEST_ASSERT_EQUAL_UINT16(TRK_USER_ID_BASE, v.id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", v.name);
    TEST_ASSERT_EQUAL_UINT8(1, v.n_layouts);
    TEST_ASSERT_EQUAL_STRING("Full", v.layouts[0].name);
    TEST_ASSERT_EQUAL_INT8(1, v.layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_UINT8(2, v.layouts[0].n_sectors);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)llround(run->length_m), v.layouts[0].length_m);
    TEST_ASSERT_EQUAL_UINT32(VENUE_RADIUS_DEFAULT_M, v.radius_m);

    static char js[8192];
    int n = trk_to_json(&v, js, sizeof js);
    TEST_ASSERT_TRUE(n > 0);
    trk_venue_t back;
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&back, js, (size_t)n, errbuf, sizeof errbuf));
    /* trk_to_json writes coordinates with 7 decimals (jw_double), so the round trip is exact to
     * 5e-8 deg (~6 mm); 1e-7 deg is the tightest bound the writer's precision allows. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p1.lat, back.layouts[0].sf.p1.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p1.lon, back.layouts[0].sf.p1.lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p2.lat, back.layouts[0].sf.p2.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p2.lon, back.layouts[0].sf.p2.lon);
    TEST_ASSERT_EQUAL_UINT32(v.layouts[0].length_m, back.layouts[0].length_m);
    TEST_ASSERT_EQUAL_UINT8(v.layouts[0].n_sectors, back.layouts[0].n_sectors);
}

/* ------------------------------------------------------------------ 8 */
static void test_enu_to_ll_inverts_geo_to_enu(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    geo_origin_t o;
    geo_origin_set(&o, c.origin_lat_deg, c.origin_lon_deg);
    double lat, lon;
    synth_enu_to_ll(c.origin_lat_deg, c.origin_lon_deg, 1000.0, -700.0, &lat, &lon);
    geo_enu_t back = geo_to_enu(&o, lat, lon);
    /* 1e-6 m: both directions are the same equirectangular formula, so only float rounding on
     * degrees of order 1e1 remains (measured 2.3e-10 m). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1000.0, back.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -700.0, back.y);
}

/* ------------------------------------------------------------------ 9 */
static void expect_reject(synth_cfg_t *c)
{
    errbuf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, synth_run_build(run, c, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(strlen(errbuf) > 0);
}

static void test_invalid_configs_are_rejected(void)
{
    synth_cfg_t c;
    synth_cfg_defaults(&c); c.n_vertices = 2;                       expect_reject(&c);
    synth_cfg_defaults(&c); c.laps = 0;                             expect_reject(&c);
    synth_cfg_defaults(&c); c.v_corner_mps = 60.0;                  expect_reject(&c);   /* >= v_max */
    synth_cfg_defaults(&c); c.n_sector_gates = LAP_MAX_SECTORS + 1; expect_reject(&c);
    synth_cfg_defaults(&c); c.length_m = 200000.0;                  expect_reject(&c);   /* > MAX_LENGTH_M */
    /* A 3-vertex 300 m track: r = 40 leaves 16 m straights, r = 80 makes the tangents longer than
     * the edge itself. start/stop are shortened so the geometry check is the one that fires. */
    synth_cfg_defaults(&c); c.n_vertices = 3; c.length_m = 300.0; c.start_before_m = 50.0; c.stop_after_m = 50.0;
    expect_reject(&c);
    c.corner_radius_m = 80.0;                                       expect_reject(&c);
    synth_cfg_defaults(&c); c.start_before_m = 1500.0; c.stop_after_m = 1200.0; expect_reject(&c);
    /* 9 gates on a 1000 m triangle: the sector gate at 353.3 m is already at a straight's first
     * admissible point, and the one before it is pushed onto the same point. */
    synth_cfg_defaults(&c); c.n_vertices = 3; c.length_m = 1000.0; c.n_sector_gates = 8;
    c.sf_frac = 0.01; c.start_before_m = 100.0; c.stop_after_m = 100.0;
    expect_reject(&c);

    /* An irregular 8-gon is still a valid track and still closes on cfg.length_m exactly. */
    synth_cfg_defaults(&c); c.n_vertices = 8; c.length_m = 1800.0; c.irregularity = 0.3;
    errbuf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1800.0, run->length_m);
    double sum = 0.0;
    for (int k = 0; k < 8; k++) sum += run->straight_len[k] + run->arc_len[k];
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1800.0, sum);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_geometry_is_a_regular_12gon);
    RUN_TEST(test_lap_var_scales_accelerations_and_cap);
    RUN_TEST(test_default_straights_are_accel_brake_only);
    RUN_TEST(test_long_straights_reach_cruise);
    RUN_TEST(test_piece_table_is_continuous_and_matches_integration);
    RUN_TEST(test_truth_speed_continuous_across_tables);
    RUN_TEST(test_state_at_and_time_at_s_are_inverses);
    RUN_TEST(test_arc_kinematics_signs);
    RUN_TEST(test_lap_crossings_and_lap_times);
    RUN_TEST(test_gate_geometry_and_crossing);
    RUN_TEST(test_venue_validates_and_round_trips);
    RUN_TEST(test_enu_to_ll_inverts_geo_to_enu);
    RUN_TEST(test_invalid_configs_are_rejected);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel 2>&1 | tail -5`
Expected: FAIL — `Undefined symbols … "_synth_cfg_defaults", "_synth_run_build", "_synth_run_state_at",
"_synth_run_time_at_s", "_synth_run_sf_s_total", "_synth_run_lap_crossings", "_synth_run_lap_time",
"_synth_run_venue", "_synth_enu_to_ll"` while linking `test_synth_track` (the header exists, the
implementation does not).

- [ ] **Step 3: Implement**

`tools/replay/lib/synth_track.c`:

```c
#include "replay/synth.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/trk.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Closed-form circuit and run model (spec §22.2, plan 02 session 2.1).
 *
 * The lap is a scaled N-gon whose corners are circular arcs of cfg.corner_radius_m. Lap-frame arc
 * length s runs 0 .. length_m starting at the end of the arc at vertex 0 (= the start of straight 0);
 * the driving order is straight 0, arc 1, straight 1, arc 2, ... straight N-1, arc 0. Every piece has
 * constant acceleration, so position, speed and time invert in closed form and gate crossing times
 * are exact. Each table applies one rider-factor draw k = 1 + lap_var*U(-1,1) to cfg.a_acc_mps2,
 * cfg.a_brk_mps2 and cfg.v_max_mps; cfg.v_corner_mps is never scaled, so every table starts and ends
 * its straights, and drives every arc, at the same constant speed -- truth speed is therefore
 * continuous at every table boundary (s = 0, the arc-at-vertex-0 exit). Lap times still differ across
 * tables because the accelerations and the cruise cap differ. */

#define TWO_PI      (2.0 * GEO_PI)
#define RAD_PER_DEG (GEO_PI / 180.0)
#define DEG_PER_RAD (180.0 / GEO_PI)

/* A vertex whose turn is smaller than this has an arc well under a millimetre at any sane
 * corner_radius_m (40 um at the default 40 m radius): the piece table needs len > 0 everywhere, so
 * such a configuration is rejected instead of silently degenerate. */
static const double MIN_TURN_RAD = 1e-6;
/* atan2 returns (-pi, pi]; a turn of pi is a hairpin back along the same edge and tan(pi/2) = inf. */
static const double MAX_TURN_RAD = GEO_PI - 1e-9;
/* Beyond this the radius factor 1 + irregularity*U(-1,1) can reach 0 and the polygon collapses. */
static const double MAX_IRREGULARITY = 0.9;
/* Same reason for the per-table rider factor 1 + lap_var*U(-1,1): k stays in [0.5, 1.5]. */
static const double MAX_LAP_VAR = 0.5;
/* trk_validate_venue rejects a gate line shorter than 1 m (§10.1), so each half must reach 0.5 m. */
static const double MIN_GATE_HALF_M = 0.5;
/* Two gates are "the same point" when their adjusted arc lengths agree to within a nanometre. */
static const double SAME_POINT_M = 1e-9;
/* Sane upper bound so llround(length_m) always fits the venue's uint32_t length_m without truncating
 * or silently producing an absurd venue. */
static const double MAX_LENGTH_M = 100000.0;
/* Geodetic sanity bounds for the venue origin: well clear of the poles and the antimeridian. */
static const double MAX_ABS_LAT_DEG = 89.0;
static const double MAX_ABS_LON_DEG = 180.0;

/* ---------------------------------------------------------------------------
 * Private RNG. splitmix64, deliberately NOT synth_rng_* from synth_gps.h: that stream belongs to
 * synth_gps.c and this file must not depend on it. Both are deterministic; they need not agree.
 * ------------------------------------------------------------------------- */
static void sr_seed(uint64_t *s, uint32_t seed) { *s = 0x9E3779B97F4A7C15ull ^ (uint64_t)seed; }

static uint64_t sr_next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* U(-1, 1) with 53 random bits. */
static double sr_sym(uint64_t *s)
{
    double u = (double)(sr_next(s) >> 11) * (1.0 / 9007199254740992.0);   /* [0, 1) */
    return 2.0 * u - 1.0;
}

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */
static double wrap_s(double s, double len)
{
    double w = fmod(s, len);
    if (w < 0.0) w += len;
    if (w >= len) w = 0.0;                 /* fmod can return len after rounding a tiny negative */
    return w;
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

static int fail(char *err, size_t err_cap, const char *msg)
{
    if (err && err_cap) snprintf(err, err_cap, "%s", msg);
    return -1;
}

/* Build-time geometry: everything the piece tables and the gates are cut from. Not stored in
 * synth_run_t (the header fixes that layout) and not global (the tools keep no mutable globals). */
typedef struct {
    int    n;
    double sx[SYNTH_MAX_VERTICES], sy[SYNTH_MAX_VERTICES];   /* ENU point where straight k starts */
    double ax[SYNTH_MAX_VERTICES], ay[SYNTH_MAX_VERTICES];   /* ENU point where arc k starts */
    double head[SYNTH_MAX_VERTICES];                          /* compass heading of edge k, radians */
    double st_s0[SYNTH_MAX_VERTICES];                         /* lap-frame arc length where straight k starts */
} synth_geom_t;

/* ---------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */
void synth_cfg_defaults(synth_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->n_vertices       = 12;
    c->length_m         = 2500.0;
    c->corner_radius_m  = 40.0;
    c->irregularity     = 0.0;
    c->clockwise        = true;
    c->seed             = 1;
    c->v_corner_mps     = 15.0;
    c->v_max_mps        = 50.0;
    c->a_acc_mps2       = 3.0;
    c->a_brk_mps2       = 6.0;
    c->lap_var          = 0.03;
    c->laps             = 10;
    c->start_before_m   = 300.0;
    c->stop_after_m     = 200.0;
    c->sf_frac          = 0.5 / 12.0;      /* half of the first straight of the default 12-gon */
    c->n_sector_gates   = 2;
    c->gate_half_width_m = GATE_HALF_WIDTH_M;
    c->origin_lat_deg   = -34.0300;
    c->origin_lon_deg   = 18.7300;
    c->venue_id         = TRK_USER_ID_BASE;
}

static int check_cfg(const synth_cfg_t *c, char *err, size_t err_cap)
{
    if (c->n_vertices < 3 || c->n_vertices > SYNTH_MAX_VERTICES) return fail(err, err_cap, "n_vertices out of range");
    if (c->laps < 1 || c->laps > SYNTH_MAX_LAPS) return fail(err, err_cap, "laps out of range");
    if (c->n_sector_gates < 0 || c->n_sector_gates > LAP_MAX_SECTORS) return fail(err, err_cap, "n_sector_gates out of range");
    if (!isfinite(c->length_m) || c->length_m <= 0.0 || c->length_m > MAX_LENGTH_M) return fail(err, err_cap, "length_m out of range");
    if (!isfinite(c->corner_radius_m) || c->corner_radius_m <= 0.0) return fail(err, err_cap, "corner_radius_m must be positive");
    if (!isfinite(c->irregularity) || c->irregularity < 0.0 || c->irregularity > MAX_IRREGULARITY) return fail(err, err_cap, "irregularity out of range");
    if (!isfinite(c->lap_var) || c->lap_var < 0.0 || c->lap_var > MAX_LAP_VAR) return fail(err, err_cap, "lap_var out of range");
    if (!isfinite(c->v_corner_mps) || c->v_corner_mps <= 0.0) return fail(err, err_cap, "v_corner_mps must be positive");
    /* v_corner no longer moves with lap_var (only a_acc, a_brk and v_max do), so the cruise cap must
     * still clear it at the smallest rider factor (k = 1 - lap_var, the worst case). */
    if (!isfinite(c->v_max_mps) || c->v_max_mps * (1.0 - c->lap_var) <= c->v_corner_mps) return fail(err, err_cap, "v_max_mps must exceed v_corner_mps at the smallest lap_var scale");
    if (!isfinite(c->a_acc_mps2) || c->a_acc_mps2 <= 0.0) return fail(err, err_cap, "a_acc_mps2 must be positive");
    if (!isfinite(c->a_brk_mps2) || c->a_brk_mps2 <= 0.0) return fail(err, err_cap, "a_brk_mps2 must be positive");
    if (!isfinite(c->start_before_m) || c->start_before_m < 0.0) return fail(err, err_cap, "start_before_m must be >= 0");
    if (!isfinite(c->stop_after_m) || c->stop_after_m < 0.0) return fail(err, err_cap, "stop_after_m must be >= 0");
    if (c->start_before_m + c->stop_after_m >= c->length_m) return fail(err, err_cap, "start_before_m + stop_after_m >= length_m");
    if (!isfinite(c->sf_frac) || c->sf_frac < 0.0 || c->sf_frac >= 1.0) return fail(err, err_cap, "sf_frac out of range");
    if (!isfinite(c->gate_half_width_m) || c->gate_half_width_m < MIN_GATE_HALF_M) return fail(err, err_cap, "gate_half_width_m too small");
    if (!isfinite(c->origin_lat_deg) || c->origin_lat_deg < -MAX_ABS_LAT_DEG || c->origin_lat_deg > MAX_ABS_LAT_DEG) return fail(err, err_cap, "origin_lat_deg out of range");
    if (!isfinite(c->origin_lon_deg) || c->origin_lon_deg < -MAX_ABS_LON_DEG || c->origin_lon_deg > MAX_ABS_LON_DEG) return fail(err, err_cap, "origin_lon_deg out of range");
    if (c->venue_id == 0) return fail(err, err_cap, "venue_id must be non-zero");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Geometry
 * ------------------------------------------------------------------------- */
static int build_geometry(synth_run_t *r, synth_geom_t *g, uint64_t *rs, char *err, size_t err_cap)
{
    const synth_cfg_t *c = &r->cfg;
    const int n = c->n_vertices;
    double ux[SYNTH_MAX_VERTICES], uy[SYNTH_MAX_VERTICES];   /* unit polygon (circumradius ~1) */
    double dx[SYNTH_MAX_VERTICES], dy[SYNTH_MAX_VERTICES];   /* unit direction of edge k */
    double ulen[SYNTH_MAX_VERTICES];                          /* unit-polygon length of edge k */
    double tan_len[SYNTH_MAX_VERTICES];                       /* tangent length taken off each side of vertex k */

    /* Vertex k sits at compass bearing 2*pi*k/n from the centre, walked clockwise when cfg.clockwise,
     * so driving k = 0, 1, ... runs the requested way round and every corner is a right-hand turn. */
    for (int k = 0; k < n; k++) {
        double rho = 1.0 + c->irregularity * sr_sym(rs);
        double phi = TWO_PI * (double)k / (double)n;
        double bearing = c->clockwise ? phi : -phi;
        ux[k] = rho * sin(bearing);
        uy[k] = rho * cos(bearing);
    }
    for (int k = 0; k < n; k++) {
        int k1 = (k + 1) % n;
        double ex = ux[k1] - ux[k], ey = uy[k1] - uy[k];
        double L = sqrt(ex * ex + ey * ey);
        if (!(L > 0.0)) return fail(err, err_cap, "degenerate polygon edge");
        ulen[k] = L;
        dx[k] = ex / L;
        dy[k] = ey / L;
        g->head[k] = atan2(ex, ey);         /* compass heading: east over north */
    }

    double sum_u = 0.0, sum_t = 0.0, sum_arc = 0.0;
    for (int k = 0; k < n; k++) {
        int km = (k + n - 1) % n;
        double cr = dx[km] * dy[k] - dy[km] * dx[k];      /* + = left turn (CCW in ENU) */
        double dt = dx[km] * dx[k] + dy[km] * dy[k];
        double turn = atan2(cr, dt);
        if (fabs(turn) < MIN_TURN_RAD) return fail(err, err_cap, "a vertex is collinear (no corner)");
        if (fabs(turn) > MAX_TURN_RAD) return fail(err, err_cap, "a vertex turns back on itself");
        r->turn_rad[k] = turn;
        r->arc_len[k]  = c->corner_radius_m * fabs(turn);
        tan_len[k]     = c->corner_radius_m * tan(fabs(turn) / 2.0);
        sum_u   += ulen[k];
        sum_t   += tan_len[k];
        sum_arc += r->arc_len[k];
    }

    /* Driven length P(S) = S*sum_u - 2*sum_t + sum_arc; solve P(S) = cfg.length_m. */
    double S = (c->length_m + 2.0 * sum_t - sum_arc) / sum_u;
    if (!(S > 0.0)) return fail(err, err_cap, "corner_radius_m too large for length_m");

    double total = 0.0;
    for (int k = 0; k < n; k++) {
        r->ve[k] = S * ux[k];
        r->vn[k] = S * uy[k];
    }
    for (int k = 0; k < n; k++) {
        int k1 = (k + 1) % n;
        double edge = S * ulen[k];
        if (tan_len[k] + tan_len[k1] >= edge) return fail(err, err_cap, "corner arcs do not fit on an edge");
        r->straight_len[k] = edge - tan_len[k] - tan_len[k1];
        if (r->straight_len[k] < 2.0 * SYNTH_GATE_MARGIN_M) return fail(err, err_cap, "a straight is shorter than 2*SYNTH_GATE_MARGIN_M");
        total += r->straight_len[k] + r->arc_len[k];
    }
    r->length_m   = total;
    r->n_vertices = n;

    /* Anchor points: straight k starts tan_len[k] past vertex k along edge k; arc k starts
     * tan_len[k] before vertex k along edge k-1, which is exactly where straight k-1 ends. */
    g->n = n;
    for (int k = 0; k < n; k++) {
        int km = (k + n - 1) % n;
        g->sx[k] = r->ve[k] + tan_len[k] * dx[k];
        g->sy[k] = r->vn[k] + tan_len[k] * dy[k];
        g->ax[k] = r->ve[k] - tan_len[k] * dx[km];
        g->ay[k] = r->vn[k] - tan_len[k] * dy[km];
    }
    g->st_s0[0] = 0.0;
    for (int k = 1; k < n; k++) g->st_s0[k] = g->st_s0[k - 1] + r->straight_len[k - 1] + r->arc_len[k];
    return 0;
}

/* ---------------------------------------------------------------------------
 * Lap tables
 * ------------------------------------------------------------------------- */
static void push(synth_lap_t *tb, synth_piece_kind_t kind, double s0, double len, double t0, double dur,
                 double e0, double n0, double head0, double v0, double a,
                 double ce, double cn, double turn)
{
    synth_piece_t *p = &tb->pieces[tb->n_pieces++];
    p->kind = kind;
    p->s0 = s0; p->len = len;
    p->t0 = t0; p->dur = dur;
    p->e0 = e0; p->n0 = n0;
    p->head0_rad = head0;
    p->v0 = v0; p->a = a;
    p->ce = ce; p->cn = cn; p->turn_rad = turn;
}

/* One lap-frame period driven with rider factor k applied to the accelerations and the cruise cap.
 * v_corner stays at cfg's value on every table -- so truth speed is continuous where tables meet,
 * s = 0 -- but accelerating harder or softer out of every corner still gives each table a distinct
 * lap time, even when its straights never reach the cap. */
static void build_table(synth_lap_t *tb, const synth_run_t *r, const synth_geom_t *g, double k)
{
    const synth_cfg_t *c = &r->cfg;
    const double vc = c->v_corner_mps;                              /* constant across every table */
    const double aa = c->a_acc_mps2 * k, ab = c->a_brk_mps2 * k, vmax = c->v_max_mps * k;
    const double rc = c->corner_radius_m;
    double s = 0.0, t = 0.0;

    tb->n_pieces = 0;
    tb->k = k;
    tb->v_max_mps = vmax;
    tb->a_acc_mps2 = aa;
    tb->a_brk_mps2 = ab;
    for (int j = 0; j < g->n; j++) {
        const double L = r->straight_len[j];
        const double h = g->head[j], sh = sin(h), ch = cos(h);

        /* Peak speed if the straight were pure accelerate-then-brake (no cruise):
         * v_p^2 = (2*a_acc*a_brk*L + v_c^2*(a_acc + a_brk)) / (a_acc + a_brk). */
        double vp2 = (2.0 * aa * ab * L + vc * vc * (aa + ab)) / (aa + ab);
        double vp = sqrt(vp2);
        double d_acc, d_cru, d_brk, v_top;
        if (vp <= vmax) {
            v_top = vp;
            d_acc = (vp2 - vc * vc) / (2.0 * aa);
            d_cru = 0.0;
            d_brk = L - d_acc;                       /* closes the straight exactly */
        } else {
            v_top = vmax;
            d_acc = (vmax * vmax - vc * vc) / (2.0 * aa);
            d_brk = (vmax * vmax - vc * vc) / (2.0 * ab);
            d_cru = L - d_acc - d_brk;
        }
        double t_acc = (v_top - vc) / aa;
        double t_brk = (v_top - vc) / ab;

        push(tb, SYNTH_P_ACCEL, s, d_acc, t, t_acc, g->sx[j], g->sy[j], h, vc, aa, 0.0, 0.0, 0.0);
        s += d_acc; t += t_acc;
        if (d_cru > 0.0) {
            push(tb, SYNTH_P_CRUISE, s, d_cru, t, d_cru / v_top, g->sx[j] + d_acc * sh, g->sy[j] + d_acc * ch,
                 h, v_top, 0.0, 0.0, 0.0, 0.0);
            s += d_cru; t += d_cru / v_top;
        }
        push(tb, SYNTH_P_BRAKE, s, d_brk, t, t_brk, g->sx[j] + (d_acc + d_cru) * sh, g->sy[j] + (d_acc + d_cru) * ch,
             h, v_top, -ab, 0.0, 0.0, 0.0);
        s += d_brk; t += t_brk;

        /* The arc at vertex j+1 closes this edge; arc 0 is last and closes the lap. */
        int ka = (j + 1) % g->n;
        double turn = r->turn_rad[ka], alen = r->arc_len[ka];
        /* Centre lies corner_radius_m to the inside of the turn, perpendicular to the entry heading:
         * left normal of compass heading h is (-cos h, sin h), right normal is (cos h, -sin h). */
        double nx = (turn > 0.0) ? -ch : ch;
        double ny = (turn > 0.0) ?  sh : -sh;
        push(tb, SYNTH_P_ARC, s, alen, t, alen / vc, g->ax[ka], g->ay[ka], h, vc, 0.0,
             g->ax[ka] + rc * nx, g->ay[ka] + rc * ny, turn);
        s += alen; t += alen / vc;
    }
    tb->lap_time_s = t;
}

/* Position and compass heading ds metres into a piece. */
static void piece_point(const synth_piece_t *p, double ds, double *e, double *nn, double *head_rad)
{
    if (p->kind == SYNTH_P_ARC) {
        double rot = p->turn_rad * (ds / p->len);      /* CCW-positive rotation of the ENU radius vector */
        double vx = p->e0 - p->ce, vy = p->n0 - p->cn;
        double cs = cos(rot), sn = sin(rot);
        *e  = p->ce + vx * cs - vy * sn;
        *nn = p->cn + vx * sn + vy * cs;
        *head_rad = p->head0_rad - rot;                /* compass angle runs the opposite way to ENU */
    } else {
        *e  = p->e0 + ds * sin(p->head0_rad);
        *nn = p->n0 + ds * cos(p->head0_rad);
        *head_rad = p->head0_rad;
    }
}

static int piece_by_s(const synth_lap_t *tb, double s)
{
    for (int i = 0; i < tb->n_pieces; i++) if (s < tb->pieces[i].s0 + tb->pieces[i].len) return i;
    return tb->n_pieces - 1;
}

static int piece_by_t(const synth_lap_t *tb, double t)
{
    for (int i = 0; i < tb->n_pieces; i++) if (t < tb->pieces[i].t0 + tb->pieces[i].dur) return i;
    return tb->n_pieces - 1;
}

/* Lap-frame time at lap-frame arc length s. */
static double lap_time_at_s(const synth_lap_t *tb, double s)
{
    int j = piece_by_s(tb, s);
    const synth_piece_t *p = &tb->pieces[j];
    double ds = clampd(s - p->s0, 0.0, p->len);
    double tau;
    if (p->a != 0.0) {
        double disc = p->v0 * p->v0 + 2.0 * p->a * ds;
        if (disc < 0.0) disc = 0.0;
        tau = (sqrt(disc) - p->v0) / p->a;
    } else {
        tau = ds / p->v0;
    }
    return p->t0 + tau;
}

/* Geometry at lap-frame arc length s. Table 0's pieces carry the same path as every other table
 * (only the piece boundaries move with the table's speeds). */
static void geom_at_s(const synth_run_t *r, double s, double *e, double *nn, double *head_rad)
{
    const synth_lap_t *tb = &r->tables[0];
    int j = piece_by_s(tb, s);
    piece_point(&tb->pieces[j], clampd(s - tb->pieces[j].s0, 0.0, tb->pieces[j].len), e, nn, head_rad);
}

static double head_deg_norm(double head_rad)
{
    double d = fmod(head_rad * DEG_PER_RAD, 360.0);
    if (d < 0.0) d += 360.0;
    if (d >= 360.0) d = 0.0;
    return d;
}

/* ---------------------------------------------------------------------------
 * Gates
 * ------------------------------------------------------------------------- */
/* First arc length at or after s that is on a straight and at least SYNTH_GATE_MARGIN_M from both of
 * its ends (session ruling: gates live on straights only). */
static double first_admissible(const synth_run_t *r, const synth_geom_t *g, double s)
{
    const double m = SYNTH_GATE_MARGIN_M;
    for (int k = 0; k < g->n; k++) {
        double lo = g->st_s0[k] + m, hi = g->st_s0[k] + r->straight_len[k] - m;
        if (s >= lo && s <= hi) return s;
    }
    double best = 0.0, best_d = -1.0;
    for (int k = 0; k < g->n; k++) {
        double lo = wrap_s(g->st_s0[k] + m, r->length_m);
        double d  = wrap_s(lo - s, r->length_m);
        if (best_d < 0.0 || d < best_d) { best_d = d; best = lo; }
    }
    return best;
}

static void fill_gate(const synth_run_t *r, synth_gate_t *gate, double s)
{
    double e, nn, head;
    geom_at_s(r, s, &e, &nn, &head);
    double sh = sin(head), ch = cos(head);
    double half = r->cfg.gate_half_width_m;
    /* Left normal of compass heading h in ENU is (-cos h, sin h): with p1 = centre + half*left and
     * p2 = centre - half*left, sign(cross(p2 - p1, motion)) = +1, which is the dir_sign of §6.4. */
    const double lx = -ch, ly = sh;                 /* left normal unit vector */
    gate->s_m = s;
    gate->e_m = e;
    gate->n_m = nn;
    gate->heading_deg = head_deg_norm(head);
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg, e + half * lx, nn + half * ly,
                    &gate->line.p1.lat, &gate->line.p1.lon);
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg, e - half * lx, nn - half * ly,
                    &gate->line.p2.lat, &gate->line.p2.lon);
}

static int build_gates(synth_run_t *r, const synth_geom_t *g, char *err, size_t err_cap)
{
    const synth_cfg_t *c = &r->cfg;
    r->n_gates = 1 + c->n_sector_gates;
    double s_sf = first_admissible(r, g, wrap_s(c->sf_frac * r->length_m, r->length_m));
    fill_gate(r, &r->gates[0], s_sf);
    double step = r->length_m / (double)(c->n_sector_gates + 1);
    for (int i = 1; i <= c->n_sector_gates; i++) {
        double want = wrap_s(s_sf + (double)i * step, r->length_m);
        fill_gate(r, &r->gates[i], first_admissible(r, g, want));
    }
    /* Driving order: every gate's wrapped distance from S/F must be strictly increasing. This is
     * guaranteed for every reachable config (first_admissible is monotone non-decreasing and the
     * requested sector offsets are themselves strictly increasing), but is made an explicit,
     * defended invariant here rather than an implicit one; it also subsumes a plain "two gates
     * landed on the same point" check. */
    double prev = -1.0;
    for (int i = 0; i < r->n_gates; i++) {
        double d = wrap_s(r->gates[i].s_m - s_sf, r->length_m);
        if (d < prev + SAME_POINT_M)
            return fail(err, err_cap, "gates are not in strictly increasing driving order from S/F");
        prev = d;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Build
 * ------------------------------------------------------------------------- */
int synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (check_cfg(cfg, err, err_cap) != 0) return -1;

    memset(r, 0, sizeof *r);
    r->cfg = *cfg;

    uint64_t rs;
    sr_seed(&rs, cfg->seed);                       /* vertex radii first, then one rider factor per table */

    synth_geom_t g;
    memset(&g, 0, sizeof g);
    if (build_geometry(r, &g, &rs, err, err_cap) != 0) return -1;

    /* out-lap, the laps themselves, and the run-out; tied to SYNTH_MAX_TABLES/SYNTH_MAX_LAPS in the
     * header instead of a bare "+ 3" so the two stay in sync. */
    r->n_tables = cfg->laps + (SYNTH_MAX_TABLES - SYNTH_MAX_LAPS);
    double acc = 0.0;
    for (int i = 0; i < r->n_tables; i++) {
        r->table_t0[i] = acc;
        build_table(&r->tables[i], r, &g, 1.0 + cfg->lap_var * sr_sym(&rs));
        acc += r->tables[i].lap_time_s;
    }

    if (build_gates(r, &g, err, err_cap) != 0) return -1;

    r->s_total_start = wrap_s(r->gates[0].s_m - cfg->start_before_m, r->length_m);   /* in table 0 */
    r->t_offset_s    = r->table_t0[0] + lap_time_at_s(&r->tables[0], r->s_total_start);
    r->s_total_end   = synth_run_sf_s_total(r, cfg->laps) + cfg->stop_after_m;
    r->duration_s    = synth_run_time_at_s(r, r->s_total_end);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Run queries
 * ------------------------------------------------------------------------- */
double synth_run_sf_s_total(const synth_run_t *r, int n)
{
    double s_sf = r->gates[0].s_m;
    /* The run starts start_before_m before S/F; when that wraps behind S/F the first crossing is one
     * lap further along the unwrapped axis. */
    double extra = (r->s_total_start > s_sf) ? r->length_m : 0.0;
    return s_sf + (double)n * r->length_m + extra;
}

double synth_run_time_at_s(const synth_run_t *r, double s_total_m)
{
    double s_tot = clampd(s_total_m, r->s_total_start, r->s_total_end);
    int i = (int)floor(s_tot / r->length_m);
    if (i < 0) i = 0;
    if (i > r->n_tables - 1) i = r->n_tables - 1;
    double s = s_tot - (double)i * r->length_m;
    return r->table_t0[i] + lap_time_at_s(&r->tables[i], s) - r->t_offset_s;
}

void synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out)
{
    double T = clampd(t_s, 0.0, r->duration_s) + r->t_offset_s;
    int i = 0;
    for (int k = 0; k < r->n_tables; k++) { if (r->table_t0[k] <= T) i = k; else break; }
    const synth_lap_t *tb = &r->tables[i];
    int j = piece_by_t(tb, T - r->table_t0[i]);
    const synth_piece_t *p = &tb->pieces[j];
    double tau = clampd(T - r->table_t0[i] - p->t0, 0.0, p->dur);
    double ds = p->v0 * tau + 0.5 * p->a * tau * tau;
    double v  = p->v0 + p->a * tau;

    double e, nn, head;
    piece_point(p, ds, &e, &nn, &head);
    double s_m = p->s0 + ds;
    int table = i;
    if (s_m >= r->length_m && table + 1 < r->n_tables) { s_m -= r->length_m; table++; }
    s_m = clampd(s_m, 0.0, r->length_m);

    out->table        = table;
    out->s_m          = s_m;
    out->s_total_m    = (double)table * r->length_m + s_m;
    out->e_m          = e;
    out->n_m          = nn;
    out->heading_deg  = head_deg_norm(head);
    out->v_mps        = v;
    out->a_lon_mps2   = p->a;
    out->on_arc       = (p->kind == SYNTH_P_ARC);
    if (out->on_arc) {
        double a_lat = v * v / (r->cfg.corner_radius_m * G_MPS2);     /* in g */
        double sign  = (p->turn_rad < 0.0) ? 1.0 : -1.0;              /* + to the right (core/types.h) */
        out->g_lat        = sign * a_lat;
        out->lean_deg     = sign * atan(a_lat) * DEG_PER_RAD;
        out->yaw_rate_dps = p->turn_rad / p->dur * DEG_PER_RAD;       /* + for a left turn */
    } else {
        out->g_lat = 0.0;
        out->lean_deg = 0.0;
        out->yaw_rate_dps = 0.0;
    }
}

int synth_run_lap_crossings(const synth_run_t *r, int lap_no, double *out, size_t out_cap)
{
    if (out == NULL) return -1;
    if (lap_no < 1 || lap_no > r->cfg.laps) return -1;
    if (out_cap < (size_t)(r->n_gates + 1)) return -1;
    double base = synth_run_sf_s_total(r, lap_no - 1);
    double s_sf = r->gates[0].s_m;
    out[0] = synth_run_time_at_s(r, base);
    for (int g = 1; g < r->n_gates; g++)
        out[g] = synth_run_time_at_s(r, base + wrap_s(r->gates[g].s_m - s_sf, r->length_m));
    out[r->n_gates] = synth_run_time_at_s(r, base + r->length_m);
    return r->n_gates + 1;
}

double synth_run_lap_time(const synth_run_t *r, int lap_no)
{
    double t[SYNTH_MAX_GATES + 1];
    int n = synth_run_lap_crossings(r, lap_no, t, sizeof t / sizeof t[0]);
    if (n < 0) return -1.0;
    return t[n - 1] - t[0];
}

void synth_run_venue(const synth_run_t *r, trk_venue_t *v)
{
    memset(v, 0, sizeof *v);
    v->id = r->cfg.venue_id;
    snprintf(v->name, sizeof v->name, "Synthetic");
    v->lat = r->cfg.origin_lat_deg;
    v->lon = r->cfg.origin_lon_deg;
    v->radius_m = VENUE_RADIUS_DEFAULT_M;
    v->flags = TRK_F_UNVERIFIED;                 /* generated lines, never surveyed on site (§10.1) */
    v->n_layouts = 1;
    trk_layout_t *L = &v->layouts[0];
    L->id = 1;
    snprintf(L->name, sizeof L->name, "Full");
    L->dir_sign = 1;                             /* p1/p2 are ordered left/right in the driving sense */
    L->sf = r->gates[0].line;
    L->n_sectors = (uint8_t)(r->n_gates - 1);
    for (int i = 1; i < r->n_gates; i++) L->sectors[i - 1] = r->gates[i].line;
    L->length_m = (uint32_t)llround(r->length_m);
}

void synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg)
{
    double cos_lat0 = cos(lat0_deg * RAD_PER_DEG);
    *lat_deg = lat0_deg + (n_m / GEO_EARTH_R_M) * DEG_PER_RAD;
    *lon_deg = lon0_deg + (e_m / (GEO_EARTH_R_M * cos_lat0)) * DEG_PER_RAD;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1 && cmake --build test/build --parallel 2>&1 | grep -ciE "warning|error" ; ctest --test-dir test/build --output-on-failure 2>&1 | tail -4`
Expected: `0` warnings/errors, then

```
14/14 Test #14: test_synth_track .................   Passed    0.49 sec

100% tests passed, 0 tests failed out of 14
```

`./test/build/tools/replay/test_synth_track` on its own prints `12 Tests 0 Failures 0 Ignored / OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/replay/lib/synth_track.c tools/replay/test/test_synth_track.c
git commit -m "feat(tools): closed-form synthetic circuit and run model

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 3: GPS and fused sampling with noise, latency and dropouts [parallel group 1]

**Files:**
- Create: `tools/replay/lib/synth_gps.c`
- Test: `tools/replay/test/test_synth_gps.c`
- Modify: nothing (`tools/replay/CMakeLists.txt` globs `lib/*.c` and `test/test_*.c`, so no build file is touched)

**Interfaces:**
- Consumes: `replay/synth.h` (`synth_run_t`, `synth_state_t`, `synth_cfg_defaults`, `synth_run_build`, `synth_run_state_at`, `synth_enu_to_ll` — all implemented by Task 2), `core/types.h` (`gps_fix_t`, `fused_sample_t`, `GPS_FLAG_*`, `FUS_*`), `core/tb.h` (`tb_gps_us_from_utc`), `core/consts.h` (`G_MPS2`, `FIX_HACC_MAX_M`, `FIX_MIN_SATS`), `core/geo.h` (`geo_origin_set`, `geo_to_enu`, `GEO_PI` — tests only).
- Produces (every function declared in `replay/synth_gps.h`; the header is Task 1's and MUST NOT be edited): `void synth_rng_seed(synth_rng_t *g, uint64_t seed)`; `uint32_t synth_rng_u32(synth_rng_t *g)`; `double synth_rng_uniform(synth_rng_t *g)`; `double synth_rng_gauss(synth_rng_t *g)`; `void synth_gm_init(synth_gm_t *m, double sigma, double tau_s, synth_rng_t *g)`; `double synth_gm_step(synth_gm_t *m, double dt_s, synth_rng_t *g)`; `void synth_gps_cfg_defaults(synth_gps_cfg_t *c)`; `void synth_gps_init(synth_gps_t *s, const synth_gps_cfg_t *cfg)`; `int synth_gps_next(synth_gps_t *s, const synth_run_t *r, gps_fix_t *fix, double *t_true_s)`; `void synth_fused_at(const synth_run_t *r, double t_s, int64_t t0_gps_us, int64_t t0_mono_us, fused_sample_t *out)`.

**Dependency on Task 2 (read this before starting).** `synth_gps.c` calls `synth_run_state_at` and `synth_enu_to_ll`, and the suite builds its run with `synth_cfg_defaults` / `synth_run_build`. Those four functions live in `tools/replay/lib/synth_track.c`, which Task 2 writes in a sibling worktree of the same parallel group — so this task's branch, cut from the session branch right after Task 1's commit, does **not** contain them and `test_synth_gps` cannot link there on its own. Step 4 below therefore drops a throw-away stand-in into the worktree (exact content given), runs the suite against it, and Step 6 deletes it before the commit. The stand-in is never added to the index; the branch commits exactly two files. The suite's green run on the session branch, against Task 2's real circuit, is the group-1 merge's responsibility.

The tests are written so the stand-in and the real circuit give the same verdict:

- Tests 1–2 and 5 (RNG, Gauss–Markov, the `dt = 0` / long-gap behaviour) call no Task 2 symbol at all.
- Tests 3, 4, 6, 7 (fix epochs, jitter band, dropout bookkeeping, constant fields, noise statistics) build a run but assert only circuit-independent facts. In particular the injected error stream is a function of the sample times `t_k = k / rate_hz` and the seed alone — never of the geometry — and `synth_enu_to_ll` is the exact inverse of `geo_to_enu` about the same origin, so the measured error statistics are the same numbers on a straight line and on a twelve-corner circuit (only `lat_e7`/`lon_e7` quantisation moves them, by ~3 mm RMS against a 1.5 m signal).
- Test 8 is the only one that reads truth back out of `synth_state_t`; its arc branch is dormant under the stand-in (which has no arcs) and becomes live once Task 2 lands.

- [ ] **Step 1: Write the failing test**

`tools/replay/test/test_synth_gps.c`:

```c
#include "unity.h"
#include "replay/synth_gps.h"
#include "replay/synth.h"
#include "core/geo.h"
#include "core/consts.h"
#include "core/tb.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* synth_run_t holds every lap table (a few MB): heap, never the stack. */
static synth_run_t *run_new(void)
{
    synth_cfg_t c;
    synth_cfg_defaults(&c);
    synth_run_t *r = (synth_run_t *)malloc(sizeof *r);
    TEST_ASSERT_NOT_NULL(r);
    char err[128];
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, synth_run_build(r, &c, err, sizeof err), err);
    TEST_ASSERT_TRUE(r->duration_s > 0.0);
    return r;
}

/* signed angular difference folded into [-180, 180) */
static double wrap180(double deg)
{
    double d = fmod(deg + 180.0, 360.0);
    if (d < 0.0) d += 360.0;
    return d - 180.0;
}

/* ---- 1. RNG ---------------------------------------------------------------------------------- */

static void test_rng_is_reproducible_and_seed_separated(void)
{
    synth_rng_t a, b;
    synth_rng_seed(&a, 1);
    synth_rng_seed(&b, 1);
    for (int i = 0; i < 1000; i++) TEST_ASSERT_EQUAL_UINT32(synth_rng_u32(&a), synth_rng_u32(&b));

    synth_rng_t s1, s2;
    synth_rng_seed(&s1, 1);
    synth_rng_seed(&s2, 2);
    /* splitmix64 mixes the whole 64-bit state, so adjacent seeds cannot collide in the first word */
    TEST_ASSERT_NOT_EQUAL_UINT32(synth_rng_u32(&s1), synth_rng_u32(&s2));
}

static void test_rng_uniform_is_uniform_on_the_unit_interval(void)
{
    enum { N = 200000 };
    synth_rng_t g;
    synth_rng_seed(&g, 7);
    double sum = 0.0, lo = 2.0, hi = -1.0;
    for (int i = 0; i < N; i++) {
        double u = synth_rng_uniform(&g);
        sum += u;
        if (u < lo) lo = u;
        if (u > hi) hi = u;
    }
    /* mean 1/2; SE = (1/sqrt(12))/sqrt(N) = 6.5e-4, so 0.005 is ~7.7 SE. This seed gives 0.50054. */
    TEST_ASSERT_DOUBLE_WITHIN(0.005, 0.5, sum / (double)N);
    TEST_ASSERT_TRUE(lo >= 0.0);
    TEST_ASSERT_TRUE(hi < 1.0);
}

static void test_rng_gauss_is_standard_normal(void)
{
    enum { N = 200000 };
    synth_rng_t g;
    synth_rng_seed(&g, 11);
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < N; i++) {
        double x = synth_rng_gauss(&g);
        sum += x;
        sum2 += x * x;
    }
    double mean = sum / (double)N;
    double var = (sum2 - (double)N * mean * mean) / (double)(N - 1);
    /* SE(mean) = 1/sqrt(N) = 2.2e-3 so 0.01 is 4.5 SE; SE(sigma) = 1/sqrt(2N) = 1.6e-3 so 0.01 is
     * 6 SE. This seed gives mean -0.00099 and sigma 1.00170. */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, mean);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, sqrt(var));
}

/* ---- 2. Gauss-Markov ------------------------------------------------------------------------- */

static void test_gm_matches_its_stationary_sigma_and_correlation_time(void)
{
    enum { N = 400000, LAG = 100 };                 /* dt = 0.2 s → LAG = 20 s = tau */
    const double sigma = 1.5, tau = 20.0, dt = 0.2;
    double *x = (double *)malloc(sizeof(double) * (size_t)N);
    TEST_ASSERT_NOT_NULL(x);
    synth_rng_t g;
    synth_rng_seed(&g, 3);
    synth_gm_t m;
    synth_gm_init(&m, sigma, tau, &g);
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        x[i] = synth_gm_step(&m, dt, &g);
        sum += x[i];
    }
    double mean = sum / (double)N;
    double var = 0.0;
    for (int i = 0; i < N; i++) var += (x[i] - mean) * (x[i] - mean);
    var /= (double)(N - 1);
    /* Samples are correlated: n_eff = N*dt/(2*tau) = 2000, so SE(sigma)/sigma = 1/sqrt(2*n_eff) ~ 1.6 %
     * = 0.024 m and 0.05 is ~2 SE. This seed gives 1.47298. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, sigma, sqrt(var));

    double num = 0.0, den = 0.0;
    for (int i = 0; i + LAG < N; i++) num += (x[i] - mean) * (x[i + LAG] - mean);
    for (int i = 0; i < N; i++) den += (x[i] - mean) * (x[i] - mean);
    /* rho(tau) = exp(-1) = 0.36788 for a first-order Gauss-Markov process; 0.05 is ~2 SE at
     * n_eff = 2000. This seed gives 0.36361. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, exp(-1.0), num / den);
    free(x);
}

static void test_gm_zero_dt_holds_and_a_long_gap_decorrelates(void)
{
    synth_rng_t g;
    synth_rng_seed(&g, 5);
    synth_gm_t m;
    synth_gm_init(&m, 1.5, 20.0, &g);
    uint64_t state_before = g.s;
    double x0 = m.x;
    TEST_ASSERT_EQUAL_DOUBLE(x0, synth_gm_step(&m, 0.0, &g));
    TEST_ASSERT_EQUAL_DOUBLE(x0, m.x);
    TEST_ASSERT_EQUAL_UINT64(state_before, g.s);     /* dt = 0 draws nothing */
    TEST_ASSERT_EQUAL_DOUBLE(x0, synth_gm_step(&m, -1.0, &g));

    enum { T = 10000 };
    double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
    for (int i = 0; i < T; i++) {
        synth_gm_t n;
        synth_gm_init(&n, 1.5, 20.0, &g);
        double before = n.x;
        double after = synth_gm_step(&n, 1e6, &g);   /* 50 000 tau: phi underflows to 0 */
        sa += before; sb += after; saa += before * before; sbb += after * after; sab += before * after;
    }
    double n_d = (double)T;
    double cov = sab / n_d - (sa / n_d) * (sb / n_d);
    double corr = cov / sqrt((saa / n_d - (sa / n_d) * (sa / n_d)) * (sbb / n_d - (sb / n_d) * (sb / n_d)));
    /* independent samples: SE of a correlation estimate is 1/sqrt(T) = 0.01, so 0.05 is 5 SE.
     * This seed gives -0.0018. */
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.0, corr);
}

/* ---- 3. Timing ------------------------------------------------------------------------------- */

static void test_fix_epochs_are_exact_and_strictly_increasing(void)
{
    synth_run_t *r = run_new();
    const int rates[2] = { 5, 10 };
    const int64_t step_us[2] = { 200000, 100000 };
    for (int j = 0; j < 2; j++) {
        synth_gps_cfg_t c;
        synth_gps_cfg_defaults(&c);
        c.rate_hz = rates[j];
        synth_gps_t s;
        synth_gps_init(&s, &c);
        int64_t prev = 0;
        for (int k = 0; k < 10; k++) {
            gps_fix_t f;
            double t_true = -1.0;
            TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&s, r, &f, &t_true));
            TEST_ASSERT_EQUAL_INT64(c.t0_gps_us + (int64_t)k * step_us[j], f.gps_us);
            TEST_ASSERT_DOUBLE_WITHIN(1e-12, (double)k / rates[j], t_true);
            if (k > 0) TEST_ASSERT_TRUE(f.gps_us > prev);
            prev = f.gps_us;
        }
    }
    free(r);
}

static void test_arrival_time_is_latency_plus_bounded_jitter(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 5;
    synth_gps_t s;
    synth_gps_init(&s, &c);
    const int64_t lo_us = (int64_t)((c.latency_ms - c.jitter_ms) * 1000.0) - 1;   /* -1: llround slack */
    const int64_t hi_us = (int64_t)((c.latency_ms + c.jitter_ms) * 1000.0) + 1;
    gps_fix_t f;
    double t_true = 0.0;
    int n = 0;
    int64_t seen_lo = INT64_MAX, seen_hi = INT64_MIN;
    while (synth_gps_next(&s, r, &f, &t_true) >= 0) {
        int64_t d = f.mono_us - c.t0_mono_us - (int64_t)llround(t_true * 1e6);
        TEST_ASSERT_TRUE(d >= lo_us);
        TEST_ASSERT_TRUE(d <= hi_us);
        if (d < seen_lo) seen_lo = d;
        if (d > seen_hi) seen_hi = d;
        n++;
    }
    TEST_ASSERT_TRUE(n > 100);
    /* U(-jitter, +jitter) over >100 draws must cover well over half the 40 ms band */
    TEST_ASSERT_TRUE(seen_hi - seen_lo > 20000);
    free(r);
}

/* ---- 4. Dropout ------------------------------------------------------------------------------ */

static void test_dropout_window_skips_fixes_without_moving_the_rng_stream(void)
{
    synth_run_t *r = run_new();
    TEST_ASSERT_TRUE(r->duration_s > 20.0);          /* the window and the fix after it must fit */

    synth_gps_cfg_t base;
    synth_gps_cfg_defaults(&base);
    base.rate_hz = 5;
    synth_gps_cfg_t cut = base;
    cut.dropout_start_s = 10.0;
    cut.dropout_end_s = 12.0;

    synth_gps_t sa, sb;
    synth_gps_init(&sa, &base);
    synth_gps_init(&sb, &cut);

    int zeros = 0, compared = 0;
    int64_t first_after_us = 0;
    for (int k = 0; k < 100; k++) {                  /* 0 .. 19.8 s */
        gps_fix_t fa, fb;
        double ta = 0.0, tb = 0.0;
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&sa, r, &fa, &ta));
        int rb = synth_gps_next(&sb, r, &fb, &tb);
        TEST_ASSERT_EQUAL_UINT32(sa.k, sb.k);        /* k advances through the window too */
        TEST_ASSERT_EQUAL_DOUBLE(ta, tb);
        if (ta >= 10.0 && ta < 12.0) {
            TEST_ASSERT_EQUAL_INT(0, rb);
            zeros++;
        } else {
            TEST_ASSERT_EQUAL_INT(1, rb);
            TEST_ASSERT_EQUAL_INT(0, memcmp(&fa, &fb, sizeof fa));
            if (zeros == 10 && first_after_us == 0) first_after_us = fb.gps_us;
            compared++;
        }
    }
    /* [10, 12) at 5 Hz holds t = 10.0 .. 11.8 inclusive = 10 samples */
    TEST_ASSERT_EQUAL_INT(10, zeros);
    TEST_ASSERT_EQUAL_INT(90, compared);
    TEST_ASSERT_EQUAL_INT64(base.t0_gps_us + 12000000LL, first_after_us);
    free(r);
}

/* ---- 5. End of run --------------------------------------------------------------------------- */

static void test_sampling_stops_one_step_past_the_run_duration(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 5;
    synth_gps_t s;
    synth_gps_init(&s, &c);
    gps_fix_t f;
    uint32_t last_k = 0;
    while (synth_gps_next(&s, r, &f, NULL) >= 0) {
        last_k = s.k;
        TEST_ASSERT_TRUE(last_k < 1000000u);         /* guards against a non-terminating loop */
    }
    /* the last emitted sample is at or before duration, the next one is past it */
    TEST_ASSERT_TRUE((double)(last_k - 1) / c.rate_hz <= r->duration_s);
    TEST_ASSERT_TRUE((double)last_k / c.rate_hz > r->duration_s);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(-1, synth_gps_next(&s, r, &f, NULL));
        TEST_ASSERT_EQUAL_UINT32(last_k, s.k);       /* k frozen after the end */
    }
    free(r);
}

/* ---- 6. Field packing ------------------------------------------------------------------------ */

static void test_every_emitted_fix_carries_the_configured_constant_fields(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    synth_gps_t s;
    synth_gps_init(&s, &c);
    gps_fix_t f;
    int n = 0;
    while (synth_gps_next(&s, r, &f, NULL) >= 0) {
        TEST_ASSERT_EQUAL_UINT8(3, f.fix_type);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE), f.flags);
        TEST_ASSERT_EQUAL_UINT8(c.sats, f.sats);
        TEST_ASSERT_EQUAL_UINT32(1500u, f.hacc_mm);          /* hacc_m 1.5 m -> mm */
        TEST_ASSERT_EQUAL_UINT32(50u, f.sacc_mms);
        TEST_ASSERT_EQUAL_UINT16(c.pdop_e2, f.pdop_e2);
        TEST_ASSERT_EQUAL_INT32(c.alt_mm, f.alt_mm);
        TEST_ASSERT_EQUAL_UINT8(1, f.valid);
        TEST_ASSERT_TRUE(f.head_e5 >= 0 && f.head_e5 < 36000000);
        TEST_ASSERT_TRUE(f.gspeed_mms >= 0);
        /* §6.5 rejects a fix whose hacc or sats fail; a synthetic fix must always pass */
        TEST_ASSERT_TRUE(f.hacc_mm <= (uint32_t)FIX_HACC_MAX_M * 1000u);
        TEST_ASSERT_TRUE(f.sats >= FIX_MIN_SATS);
        n++;
    }
    TEST_ASSERT_TRUE(n > 100);
    free(r);
}

/* ---- 7. Noise statistics --------------------------------------------------------------------- */

/* The position error is a tau = 20 s process, so one 100 s pass holds only ~3 independent samples.
 * Pooling PATHS independent seeds of SAMPLES fixes each gives n_eff ~ PATHS*SAMPLES*dt/(2*tau) = 500
 * while keeping every pass inside the shortest run this suite builds. */
#define NOISE_PATHS   200
#define NOISE_SAMPLES 1000                                   /* at 10 Hz = 100 s per path */

static void test_injected_noise_matches_the_configured_distributions(void)
{
    synth_run_t *r = run_new();
    TEST_ASSERT_TRUE(r->duration_s >= 100.0);
    geo_origin_t o;
    geo_origin_set(&o, r->cfg.origin_lat_deg, r->cfg.origin_lon_deg);

    double se = 0.0, sn = 0.0, se2 = 0.0, sn2 = 0.0;
    double sv = 0.0, sv2 = 0.0, sh = 0.0, sh2 = 0.0;
    long n = 0;
    for (int p = 0; p < NOISE_PATHS; p++) {
        synth_gps_cfg_t c;
        synth_gps_cfg_defaults(&c);
        c.rate_hz = 10;
        c.seed = (uint32_t)(1000 + p);
        synth_gps_t s;
        synth_gps_init(&s, &c);
        for (int i = 0; i < NOISE_SAMPLES; i++) {
            gps_fix_t f;
            double t = 0.0;
            TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&s, r, &f, &t));
            synth_state_t st;
            synth_run_state_at(r, t, &st);
            geo_enu_t e = geo_to_enu(&o, (double)f.lat_e7 * 1e-7, (double)f.lon_e7 * 1e-7);
            double de = e.x - st.e_m, dn = e.y - st.n_m;
            double dv = (double)f.gspeed_mms * 1e-3 - st.v_mps;
            double dh = wrap180((double)f.head_e5 * 1e-5 - st.heading_deg);
            se += de; sn += dn; se2 += de * de; sn2 += dn * dn;
            sv += dv; sv2 += dv * dv; sh += dh; sh2 += dh * dh;
            n++;
        }
    }
    double nd = (double)n;
    TEST_ASSERT_EQUAL_INT32(NOISE_PATHS * NOISE_SAMPLES, (int32_t)n);

    /* Position: RMS about truth is the configured sigma. n_eff ~ 500 -> SE(sigma)/sigma ~ 3.2 %
     * = 0.047 m and SE(mean) = 1.5/sqrt(500) = 0.067 m, so 0.15 m is ~3 SE and ~2 SE. These seeds
     * give rms 1.5242 / 1.4582 and mean -0.0112 / 0.0787. The error stream depends only on the
     * sample times, not on the track, so these numbers do not move with the circuit geometry. */
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 1.5, sqrt(se2 / nd));
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 1.5, sqrt(sn2 / nd));
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 0.0, se / nd);
    TEST_ASSERT_DOUBLE_WITHIN(0.15, 0.0, sn / nd);

    /* Speed and heading noise are white: every one of the 200 000 samples is independent, so
     * SE(sigma_v) = 0.05/sqrt(2n) = 8e-5 and SE(sigma_h) = 0.5/sqrt(2n) = 8e-4. These seeds give
     * 0.049958 m/s and 0.500271 deg. */
    double var_v = (sv2 - nd * (sv / nd) * (sv / nd)) / (nd - 1.0);
    double var_h = (sh2 - nd * (sh / nd) * (sh / nd)) / (nd - 1.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.005, 0.05, sqrt(var_v));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.5, sqrt(var_h));
    free(r);
}

static void test_the_same_seed_reproduces_fixes_byte_for_byte(void)
{
    synth_run_t *r = run_new();
    synth_gps_cfg_t c;
    synth_gps_cfg_defaults(&c);
    c.rate_hz = 10;
    c.seed = 424242;
    synth_gps_t a, b;
    synth_gps_init(&a, &c);
    synth_gps_init(&b, &c);
    for (int i = 0; i < 500; i++) {
        gps_fix_t fa, fb;
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&a, r, &fa, NULL));
        TEST_ASSERT_EQUAL_INT(1, synth_gps_next(&b, r, &fb, NULL));
        TEST_ASSERT_EQUAL_INT(0, memcmp(&fa, &fb, sizeof fa));
    }
    free(r);
}

/* ---- 8. Fused truth -------------------------------------------------------------------------- */

static void test_fused_truth_mirrors_the_state_with_no_noise(void)
{
    synth_run_t *r = run_new();
    const int64_t t0_gps = tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0);
    const int64_t t0_mono = 1000000;
    int straights = 0, arcs = 0;
    for (int i = 0; i <= 1000; i++) {
        double t = r->duration_s * (double)i / 1000.0;
        synth_state_t st;
        synth_run_state_at(r, t, &st);
        fused_sample_t fs;
        synth_fused_at(r, t, t0_gps, t0_mono, &fs);

        TEST_ASSERT_EQUAL_INT64(t0_gps + (int64_t)llround(t * 1e6), fs.gps_us);
        TEST_ASSERT_EQUAL_INT64(t0_mono + (int64_t)llround(t * 1e6), fs.mono_us);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(FUS_LEAN_VALID | FUS_ORIENT_OK), fs.flags);
        /* g_lon is the state's longitudinal acceleration in g (spec §9.3) */
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)(st.a_lon_mps2 / G_MPS2), fs.g_lon);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.g_lat, fs.g_lat);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.lean_deg, fs.lean_deg);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)st.yaw_rate_dps, fs.yaw_dps);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)hypot((double)fs.g_lon, (double)fs.g_lat), fs.g_comb);

        if (!st.on_arc) {
            /* straights are driven in a straight line: no lateral load, no lean, no yaw */
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.g_lat);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.lean_deg);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fs.yaw_dps);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)fabs((double)fs.g_lon), (float)fabs((double)fs.g_comb));
            straights++;
        } else if (fabs((double)fs.g_lat) > 1e-6) {
            /* a balanced two-wheeler leans to atan(g_lat) (spec §9.3) */
            TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(atan((double)fs.g_lat) * 180.0 / GEO_PI), fs.lean_deg);
            /* +lat = right, +yaw = left turn (core/types.h), so a corner loads them oppositely */
            TEST_ASSERT_TRUE((double)fs.g_lat * (double)fs.yaw_dps < 0.0);
            arcs++;
        }
    }
    TEST_ASSERT_TRUE(straights > 0);
    TEST_ASSERT_TRUE(straights + arcs > 0);
    free(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rng_is_reproducible_and_seed_separated);
    RUN_TEST(test_rng_uniform_is_uniform_on_the_unit_interval);
    RUN_TEST(test_rng_gauss_is_standard_normal);
    RUN_TEST(test_gm_matches_its_stationary_sigma_and_correlation_time);
    RUN_TEST(test_gm_zero_dt_holds_and_a_long_gap_decorrelates);
    RUN_TEST(test_fix_epochs_are_exact_and_strictly_increasing);
    RUN_TEST(test_arrival_time_is_latency_plus_bounded_jitter);
    RUN_TEST(test_dropout_window_skips_fixes_without_moving_the_rng_stream);
    RUN_TEST(test_sampling_stops_one_step_past_the_run_duration);
    RUN_TEST(test_every_emitted_fix_carries_the_configured_constant_fields);
    RUN_TEST(test_injected_noise_matches_the_configured_distributions);
    RUN_TEST(test_the_same_seed_reproduces_fixes_byte_for_byte);
    RUN_TEST(test_fused_truth_mirrors_the_state_with_no_noise);
    return UNITY_END();
}
```

No CMake edit: `tools/replay/CMakeLists.txt` (Task 1) globs `test/test_*.c` and registers one `ctest` entry per file.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build test/build --parallel 2>&1 | grep -E '^  "_synth' | sort -u`
Expected: FAIL at the link of `test_synth_gps`, listing both this task's missing symbols and Task 2's:

```
  "_synth_cfg_defaults", referenced from:
  "_synth_fused_at", referenced from:
  "_synth_gm_init", referenced from:
  "_synth_gm_step", referenced from:
  "_synth_gps_cfg_defaults", referenced from:
  "_synth_gps_init", referenced from:
  "_synth_gps_next", referenced from:
  "_synth_rng_gauss", referenced from:
  "_synth_rng_seed", referenced from:
  "_synth_rng_u32", referenced from:
  "_synth_rng_uniform", referenced from:
  "_synth_run_build", referenced from:
  "_synth_run_state_at", referenced from:
```

(On Linux the same symbols appear as `undefined reference to 'synth_…'`.)

- [ ] **Step 3: Implement**

`tools/replay/lib/synth_gps.c`:

```c
#include "replay/synth_gps.h"
#include "replay/synth.h"
#include "core/consts.h"
#include "core/tb.h"
#include <math.h>
#include <string.h>

/* Deterministic GPS/fused sampling of a synth_run_t (spec §22.2).
 *
 * Everything here is reproducible from cfg.seed alone: the RNG is integer-only splitmix64, the
 * noise is applied with IEEE-754 doubles in a fixed order, and the number of RNG draws per sample
 * never depends on whether the sample is emitted (see synth_gps_next). */

/* splitmix64 (Steele, Lea & Flood 2014). The odd increment is 2^64/phi; the two multipliers are the
 * published finalisation constants. Fixed-width integer arithmetic, so the stream is bit-identical
 * on every platform and compiler. */
#define SM64_GAMMA 0x9E3779B97F4A7C15ULL
#define SM64_MIX1  0xBF58476D1CE4E5B9ULL
#define SM64_MIX2  0x94D049BB133111EBULL

/* 2^-53: scales the top 53 bits of a 64-bit word into [0, 1). The largest representable result is
 * (2^53-1)/2^53 < 1, so synth_rng_uniform never returns exactly 1.0. */
#define UNIFORM_SCALE (1.0 / 9007199254740992.0)

/* gps_fix_t.head_e5 is degrees*1e5 over a full turn (spec §12.3), so headings live in [0, 36e6). */
#define HEAD_E5_FULL_TURN 36000000

/* Reported speed accuracy. A u-blox 3D fix with the noise this model injects
 * (cfg.speed_sigma_mps = 0.05 m/s) reports ~50 mm/s; it is constant because the injected speed
 * noise is constant. */
#define SYNTH_GPS_SACC_MMS 50u

/* fix_type for a 3D fix (spec §6.5 requires 3 for a fix to be valid). */
#define SYNTH_GPS_FIX_TYPE_3D 3u

/* Compass heading folded into [0, 360). fmod keeps the sign of its argument, so a negative result
 * is lifted by one turn. */
static double wrap360_deg(double deg)
{
    double d = fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

void synth_rng_seed(synth_rng_t *g, uint64_t seed)
{
    g->s = seed;
}

static uint64_t rng_next(synth_rng_t *g)
{
    g->s += SM64_GAMMA;
    uint64_t z = g->s;
    z = (z ^ (z >> 30)) * SM64_MIX1;
    z = (z ^ (z >> 27)) * SM64_MIX2;
    return z ^ (z >> 31);
}

uint32_t synth_rng_u32(synth_rng_t *g)
{
    /* the high half is the best-mixed part of a splitmix64 word */
    return (uint32_t)(rng_next(g) >> 32);
}

double synth_rng_uniform(synth_rng_t *g)
{
    return (double)(rng_next(g) >> 11) * UNIFORM_SCALE;
}

double synth_rng_gauss(synth_rng_t *g)
{
    /* Marsaglia polar method. The second deviate is deliberately NOT cached: a cached value would
     * make the RNG position depend on how many gauss() calls came before, so adding or removing a
     * draw elsewhere would shift the whole stream in a history-dependent way. */
    double u, v, s2;
    do {
        u = 2.0 * synth_rng_uniform(g) - 1.0;
        v = 2.0 * synth_rng_uniform(g) - 1.0;
        s2 = u * u + v * v;
    } while (s2 >= 1.0 || s2 == 0.0);
    return u * sqrt(-2.0 * log(s2) / s2);
}

void synth_gm_init(synth_gm_t *m, double sigma, double tau_s, synth_rng_t *g)
{
    m->sigma = sigma;
    m->tau_s = tau_s;
    /* Start in the stationary distribution N(0, σ) so there is no warm-up transient in the first
     * seconds of a run. */
    m->x = sigma * synth_rng_gauss(g);
}

double synth_gm_step(synth_gm_t *m, double dt_s, synth_rng_t *g)
{
    /* No time passed: hold the state and draw nothing, so repeated samples at one instant cannot
     * desynchronise the stream. */
    if (dt_s <= 0.0) return m->x;
    /* τ ≤ 0 has no correlation time: degenerate to white noise of the same σ. */
    const double phi = (m->tau_s > 0.0) ? exp(-dt_s / m->tau_s) : 0.0;
    m->x = phi * m->x + m->sigma * sqrt(1.0 - phi * phi) * synth_rng_gauss(g);
    return m->x;
}

void synth_gps_cfg_defaults(synth_gps_cfg_t *c)
{
    c->rate_hz         = 5;
    c->pos_sigma_m     = 1.5;
    c->pos_tau_s       = 20.0;
    c->speed_sigma_mps = 0.05;
    c->head_sigma_deg  = 0.5;
    c->latency_ms      = 80.0;
    c->jitter_ms       = 20.0;
    c->hacc_m          = 1.5;
    c->sats            = 9;
    c->pdop_e2         = 180;
    c->alt_mm          = 100000;
    c->t0_gps_us       = tb_gps_us_from_utc(2026, 9, 15, 10, 0, 0, 0);
    c->t0_mono_us      = 1000000;
    c->dropout_start_s = 0.0;
    c->dropout_end_s   = 0.0;
    c->seed            = 1;
}

void synth_gps_init(synth_gps_t *s, const synth_gps_cfg_t *cfg)
{
    s->cfg = *cfg;
    synth_rng_seed(&s->rng, s->cfg.seed);
    /* east first, then north: the order fixes the stream */
    synth_gm_init(&s->gm_e, s->cfg.pos_sigma_m, s->cfg.pos_tau_s, &s->rng);
    synth_gm_init(&s->gm_n, s->cfg.pos_sigma_m, s->cfg.pos_tau_s, &s->rng);
    s->k = 0;
    s->last_t_s = 0.0;
}

int synth_gps_next(synth_gps_t *s, const synth_run_t *r, gps_fix_t *fix, double *t_true_s)
{
    const double t_k = (double)s->k / (double)s->cfg.rate_hz;
    if (t_k > r->duration_s) return -1;            /* past the end of the run: k does not advance */

    /* Advance the error processes and draw every noise term BEFORE deciding whether this sample is
     * emitted, in this fixed order, so a dropout window changes which fixes appear but never the
     * RNG stream behind them. */
    const double dt = t_k - s->last_t_s;           /* 0 for k = 0 */
    synth_gm_step(&s->gm_e, dt, &s->rng);
    synth_gm_step(&s->gm_n, dt, &s->rng);
    s->last_t_s = t_k;
    const double n_speed = synth_rng_gauss(&s->rng);
    const double n_head  = synth_rng_gauss(&s->rng);
    const double u_jit   = synth_rng_uniform(&s->rng);

    /* Reported for dropped samples too, so a caller can log the gap; the header only promises it
     * for an emitted fix. */
    if (t_true_s) *t_true_s = t_k;

    if (t_k >= s->cfg.dropout_start_s && t_k < s->cfg.dropout_end_s) {
        s->k++;
        return 0;
    }

    synth_state_t st;
    synth_run_state_at(r, t_k, &st);

    double lat_deg, lon_deg;
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg,
                    st.e_m + s->gm_e.x, st.n_m + s->gm_n.x, &lat_deg, &lon_deg);

    memset(fix, 0, sizeof *fix);                   /* zero the padding so identical runs memcmp equal */
    /* 1e6/rate_hz is a whole number of microseconds for 5 and 10 Hz, so integer arithmetic keeps the
     * fix epoch exact for the whole run (no accumulated rounding). */
    fix->gps_us  = s->cfg.t0_gps_us + (int64_t)s->k * 1000000LL / (int64_t)s->cfg.rate_hz;
    fix->mono_us = s->cfg.t0_mono_us + (int64_t)llround(
        t_k * 1e6 + (s->cfg.latency_ms + s->cfg.jitter_ms * (2.0 * u_jit - 1.0)) * 1000.0);
    fix->lat_e7 = (int32_t)llround(lat_deg * 1e7);
    fix->lon_e7 = (int32_t)llround(lon_deg * 1e7);
    fix->alt_mm = s->cfg.alt_mm;

    double v = st.v_mps + s->cfg.speed_sigma_mps * n_speed;
    if (v < 0.0) v = 0.0;                          /* ground speed is an unsigned magnitude */
    fix->gspeed_mms = (int32_t)llround(v * 1000.0);

    /* Wrap in the integer domain as well: llround of a heading a hair under 360° can land on
     * exactly 36e6, which is out of range. */
    int64_t h = llround(wrap360_deg(st.heading_deg + s->cfg.head_sigma_deg * n_head) * 1e5);
    h %= HEAD_E5_FULL_TURN;
    if (h < 0) h += HEAD_E5_FULL_TURN;
    fix->head_e5 = (int32_t)h;

    fix->hacc_mm  = (uint32_t)llround(s->cfg.hacc_m * 1000.0);
    fix->sacc_mms = SYNTH_GPS_SACC_MMS;
    fix->pdop_e2  = s->cfg.pdop_e2;
    fix->fix_type = SYNTH_GPS_FIX_TYPE_3D;
    fix->sats     = s->cfg.sats;
    fix->flags    = (uint8_t)(GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE);
    /* The device logs post-validation fixes; §6.5 invalidity is produced by the dropout options,
     * never by noise, so every emitted synthetic fix is valid. */
    fix->valid    = 1;

    s->k++;
    return 1;
}

void synth_fused_at(const synth_run_t *r, double t_s, int64_t t0_gps_us, int64_t t0_mono_us, fused_sample_t *out)
{
    synth_state_t st;
    synth_run_state_at(r, t_s, &st);

    /* Truth, so the two clocks advance together from the same run-time zero. */
    const int64_t dt_us = (int64_t)llround(t_s * 1e6);
    memset(out, 0, sizeof *out);
    out->mono_us  = t0_mono_us + dt_us;
    out->gps_us   = t0_gps_us + dt_us;
    out->g_lon    = (float)(st.a_lon_mps2 / G_MPS2);
    out->g_lat    = (float)st.g_lat;
    out->g_comb   = (float)hypot((double)out->g_lon, (double)out->g_lat);
    out->lean_deg = (float)st.lean_deg;
    out->yaw_dps  = (float)st.yaw_rate_dps;
    out->flags    = (uint8_t)(FUS_LEAN_VALID | FUS_ORIENT_OK);
}
```

- [ ] **Step 4: Add the throw-away Task 2 stand-in and run the suite**

This file exists only so the suite can link and run before Task 2 merges. It is deleted in Step 6 and never committed.

`tools/replay/lib/synth_track_stub.c` (temporary, NOT part of this task's deliverable):

```c
/* SCRATCH ONLY — stand-in for Task 2's synth_track.c so Task 3's suite can be compiled and run
 * before Task 2 lands. Straight line due east at a constant 30 m/s, duration 120 s. */
#include "replay/synth.h"
#include "core/geo.h"
#include <math.h>
#include <string.h>

#define STUB_V 30.0
#define STUB_DUR 120.0
#define STUB_LEN 3600.0

void synth_cfg_defaults(synth_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->n_vertices = 12;
    c->length_m = 2500.0;
    c->corner_radius_m = 40.0;
    c->irregularity = 0.0;
    c->clockwise = true;
    c->seed = 1;
    c->v_corner_mps = 15.0;
    c->v_max_mps = 50.0;
    c->a_acc_mps2 = 3.0;
    c->a_brk_mps2 = 6.0;
    c->lap_var = 0.03;
    c->laps = 10;
    c->start_before_m = 300.0;
    c->stop_after_m = 200.0;
    c->sf_frac = 0.5 / 12.0;
    c->n_sector_gates = 2;
    c->gate_half_width_m = GATE_HALF_WIDTH_M;
    c->origin_lat_deg = -34.03;
    c->origin_lon_deg = 18.73;
    c->venue_id = 1000;
}

int synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap)
{
    (void)err; (void)err_cap;
    memset(r, 0, sizeof *r);
    r->cfg = *cfg;
    r->n_vertices = cfg->n_vertices;
    r->length_m = STUB_LEN;
    r->n_gates = 1;
    r->n_tables = 1;
    r->duration_s = STUB_DUR;
    return 0;
}

void synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out)
{
    if (t_s < 0.0) t_s = 0.0;
    if (t_s > r->duration_s) t_s = r->duration_s;
    memset(out, 0, sizeof *out);
    out->table = 0;
    out->s_total_m = STUB_V * t_s;
    out->s_m = fmod(out->s_total_m, r->length_m);
    out->e_m = out->s_total_m;
    out->n_m = 0.0;
    out->heading_deg = 90.0;
    out->v_mps = STUB_V;
    out->a_lon_mps2 = 0.0;
    out->on_arc = false;
}

void synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg)
{
    const double r2d = 180.0 / GEO_PI;
    const double lat0 = lat0_deg / r2d;
    *lat_deg = lat0_deg + (n_m / GEO_EARTH_R_M) * r2d;
    *lon_deg = lon0_deg + (e_m / (GEO_EARTH_R_M * cos(lat0))) * r2d;
}
```

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build test/build --parallel 2>&1 | grep -ciE "warning|error" && ctest --test-dir test/build --output-on-failure 2>&1 | tail -4`
Expected: `0` from the grep (nothing warns under `-Wall -Wextra -Werror -Wshadow -Wconversion` and ASan/UBSan are on), then

```
14/14 Test #14: test_synth_gps ...................   Passed    0.61 sec

100% tests passed out of 14
```

14 = the 12 core suites + `test_replay_smoke` (Task 1) + `test_synth_gps`. `./test/build/tools/replay/test_synth_gps` on its own prints `13 Tests 0 Failures 0 Ignored` / `OK`.

- [ ] **Step 5: gcc parity check**

Run: `gcc-16 -std=c11 -c -Wall -Wextra -Werror -Wshadow -Wconversion -I tools/replay/include -I components/core/include tools/replay/lib/synth_gps.c -o /tmp/synth_gps_gcc.o && echo PARITY_OK && rm -f /tmp/synth_gps_gcc.o`
Expected: `PARITY_OK`. Both Apple clang and gcc-16 accept the file under the **unrelaxed** strict set (without the `-Wno-error=conversion` escape hatches) because every narrowing is cast explicitly.

- [ ] **Step 6: Remove the stand-in, hygiene, commit**

Run: `rm -f tools/replay/lib/synth_track_stub.c && rm -rf test/build && git status --short tools/replay`
Expected: exactly two lines, `?? tools/replay/lib/synth_gps.c` and `?? tools/replay/test/test_synth_gps.c`. If `synth_track_stub.c` still appears, delete it — it must never reach the index.

Run: `git diff --check`
Expected: no output.

```bash
git add tools/replay/lib/synth_gps.c tools/replay/test/test_synth_gps.c
git commit -m "feat(tools): synthetic GPS sampling — splitmix64 RNG, Gauss-Markov position error, latency/jitter, dropouts

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

- [ ] **Step 7: Hand-off note for the group-1 merge**

Record in the task's review notes: `test_synth_gps` links only once Task 2's `synth_track.c` is present, so this branch's own `ctest` run is the Step 4 run against the stand-in. The orchestrator re-runs `ctest --test-dir test/build --output-on-failure` on the session branch after merging Tasks 2 and 3; all 13 tests must stay green against the real circuit, and test 8's arc assertions (`lean_deg == atan(g_lat)·180/π`, `g_lat · yaw_dps < 0`) become live there for the first time.

---

### Task 4: `.log` writer/reader, replay summary and the `replay` CLI skeleton [parallel group 1]

**Header change needed:** none. The four Task 1 headers are used exactly as written.

**Files:**
- Create: `tools/replay/lib/logio.c`, `tools/replay/lib/replay_summary.c`, `tools/replay/replay_main.c`
- Test: `tools/replay/test/test_logio.c`, `tools/replay/test/test_replay_summary.c`
- Modify: nothing. `tools/replay/CMakeLists.txt` globs `lib/*.c`, `test/test_*.c` and builds `replay` as soon as `replay_main.c` exists, so no build file changes. This task does not depend on Tasks 2 or 3.

**Interfaces:**

- Consumes (`core/ses.h`, all already implemented in plan 01):
  `int ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap)`;
  `void ses_reader_init(ses_reader_t *r)`;
  `void ses_reader_feed(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx)`;
  `void ses_reader_flush(ses_reader_t *r, ses_frame_cb_t cb, void *ctx)`;
  `void ses_fix_state_init(ses_fix_state_t *st)`;
  `int ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *fix, uint8_t *out, size_t cap)`;
  `int ses_decode_fix(ses_fix_state_t *st, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out)`;
  `void ses_fused_state_init(ses_fused_state_t *st)`; `void ses_fused_state_on_fix(ses_fused_state_t *st, int64_t fix_gps_us)`;
  `int ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap)`;
  `int ses_decode_fused(ses_fused_state_t *st, const uint8_t *payload, uint8_t len, fused_sample_t *out)`;
  and the `ses_encode_*` / `ses_decode_*` pairs for `hdr`, `venue`, `time_map`, `lap`, `sector`,
  `drag_run`, `drag_gate`, `event`, `calib`, `mark`, `power`, `end`.
- Consumes (`core/consts.h`): `SES_MAX_PAYLOAD`, `SES_SYNC`, `FIX_KEYFRAME_S`; (`core/ses.h`) `SES_FRAME_OVERHEAD`, the `SES_T_*` enum.
- Consumes (`core/types.h`): `gps_fix_t`, `fused_sample_t`, `lap_result_t`, `drag_result_t`, `lap_stats_t`, `drag_gate_res_t`.
- Consumes (`core/jw.h`): `jw_init`, `jw_obj_open/close`, `jw_arr_open/close`, `jw_key`, `jw_int`, `jw_uint`, `jw_str`, `jw_null`, `jw_overflow`.
- Consumes (`core/json.h`, tests only): `json_parse`, `json_obj_get`, `json_tok_eq`, `json_tok_int`.
- Consumes (`replay/replay.h`, Task 1): `const char *replay_version(void)`.
- Produces: every function declared in `tools/replay/include/replay/logio.h`
  (`logw_open_file`, `logw_open_mem`, `logw_hdr`, `logw_venue`, `logw_time_map`, `logw_fix`,
  `logw_fused`, `logw_lap`, `logw_sector`, `logw_drag_run`, `logw_drag_gate`, `logw_event`,
  `logw_calib`, `logw_mark`, `logw_power`, `logw_end`, `logw_close`, `logr_init`, `logr_feed`,
  `logr_finish`, `logr_read_file`) plus `int replay_summarize_file(const char *path, replay_summary_t *out)`,
  `void replay_print_text(const replay_summary_t *s, FILE *f)`,
  `void replay_print_json(const replay_summary_t *s, FILE *f)` from `replay/replay.h`, and the
  `replay` executable (`replay <file.log> [--json]`).

**Binding design decisions**

- The record encoders in `components/core/session/ses_records.c` all end in `ses_frame_encode()`, so
  every `ses_encode_*` returns a **complete frame** (sync | type | len | payload | crc16), not a bare
  payload. `logio.c` therefore never frames anything itself: it encodes into a
  `2 + SES_MAX_PAYLOAD + 2 + 1` byte stack buffer and emits the bytes as they are. This is stated in
  the file's header comment.
- `logw_fix` calls `ses_fused_state_on_fix(&w->fused_st, fix->gps_us)` after a successful write and
  `logr`'s frame callback does the same after a successful `ses_decode_fix`, so the two `FUSED`
  references march in step (§12.4).
- `n_frames` counts every frame the framing layer accepted; `n_by_type[type]` counts accepted
  decodes; `n_bad` counts decoder rejections and unknown record types. Framing failures (bad CRC,
  impossible length, a truncated tail at EOF) are counted separately by `rd.frames_bad`;
  `replay_summary_t.n_bad` is the sum of the two.

- [ ] **Step 1: Write the failing writer/reader test**

`tools/replay/test/test_logio.c`:

```c
/* mkstemp/unlink/close are POSIX, not C11; the build is -std=c11 so ask for them explicitly. */
#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "replay/logio.h"
#include "core/consts.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Wire quantisation of FIX_DELTA (spec §12.3/§12.4): altitude in decimetres, speed in cm/s,
 * heading in 0.01 deg (head_e5 / 1000), hacc in decimetres. A KEY frame carries all of them
 * exactly; these are the worst-case reconstruction errors of a DELTA. */
#define FIX_ALT_TOL_MM   100
#define FIX_SPEED_TOL_MMS 10
#define FIX_HEAD_TOL_E5  1000
#define FIX_HACC_TOL_MM  100

static char tmp_path[64];

void setUp(void) { tmp_path[0] = '\0'; }
void tearDown(void) { if (tmp_path[0]) { unlink(tmp_path); tmp_path[0] = '\0'; } }

static void make_tmp(void)
{
    strcpy(tmp_path, "/tmp/laptimer_logio_XXXXXX");
    int fd = mkstemp(tmp_path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
}

/* ---- collector ---- */

typedef struct {
    ses_hdr_t      hdr;    int n_hdr;
    ses_venue_t    venue;  int n_venue;
    ses_time_map_t tm;     int n_tm;
    gps_fix_t      fix[128];   int n_fix;
    fused_sample_t fused[64];  int n_fused;
    lap_result_t   lap;    int n_lap;
    ses_sector_t   sec;    int n_sec;
    drag_result_t  run;    int n_run;
    ses_drag_gate_t dg;    int n_dg;
    ses_event_t    ev;     int n_ev;
    ses_calib_t    cal;    int n_cal;
    ses_mark_t     mk;     int n_mk;
    ses_power_t    pw;     int n_pw;
    ses_end_t      end;    int n_end;
    uint8_t        bad_type, bad_len; int n_bad_cb;
} col_t;

static void c_hdr(const ses_hdr_t *h, void *ctx)        { col_t *c = ctx; c->hdr = *h; c->n_hdr++; }
static void c_venue(const ses_venue_t *v, void *ctx)    { col_t *c = ctx; c->venue = *v; c->n_venue++; }
static void c_tm(const ses_time_map_t *t, void *ctx)    { col_t *c = ctx; c->tm = *t; c->n_tm++; }
static void c_fix(const gps_fix_t *f, void *ctx)
{
    col_t *c = ctx;
    if (c->n_fix < (int)(sizeof c->fix / sizeof c->fix[0])) c->fix[c->n_fix] = *f;
    c->n_fix++;
}
static void c_fused(const fused_sample_t *fs, void *ctx)
{
    col_t *c = ctx;
    if (c->n_fused < (int)(sizeof c->fused / sizeof c->fused[0])) c->fused[c->n_fused] = *fs;
    c->n_fused++;
}
static void c_lap(const lap_result_t *l, void *ctx)     { col_t *c = ctx; c->lap = *l; c->n_lap++; }
static void c_sec(const ses_sector_t *s, void *ctx)     { col_t *c = ctx; c->sec = *s; c->n_sec++; }
static void c_run(const drag_result_t *r, void *ctx)    { col_t *c = ctx; c->run = *r; c->n_run++; }
static void c_dg(const ses_drag_gate_t *g, void *ctx)   { col_t *c = ctx; c->dg = *g; c->n_dg++; }
static void c_ev(const ses_event_t *e, void *ctx)       { col_t *c = ctx; c->ev = *e; c->n_ev++; }
static void c_cal(const ses_calib_t *cb, void *ctx)     { col_t *c = ctx; c->cal = *cb; c->n_cal++; }
static void c_mk(const ses_mark_t *m, void *ctx)        { col_t *c = ctx; c->mk = *m; c->n_mk++; }
static void c_pw(const ses_power_t *p, void *ctx)       { col_t *c = ctx; c->pw = *p; c->n_pw++; }
static void c_end(const ses_end_t *e, void *ctx)        { col_t *c = ctx; c->end = *e; c->n_end++; }
static void c_bad(uint8_t type, uint8_t len, void *ctx)
{
    col_t *c = ctx; c->bad_type = type; c->bad_len = len; c->n_bad_cb++;
}

static const logr_cb_t ALL_CB = {
    .on_hdr = c_hdr, .on_venue = c_venue, .on_time_map = c_tm, .on_fix = c_fix, .on_fused = c_fused,
    .on_lap = c_lap, .on_sector = c_sec, .on_drag_run = c_run, .on_drag_gate = c_dg, .on_event = c_ev,
    .on_calib = c_cal, .on_mark = c_mk, .on_power = c_pw, .on_end = c_end, .on_bad = c_bad,
};

/* ---- fixtures ---- */

#define T0_GPS_US 1789380900000000LL

static gps_fix_t mk_fix(int i, uint8_t valid)
{
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = T0_GPS_US + (int64_t)i * 200000;          /* 5 Hz */
    f.mono_us = 1000000 + (int64_t)i * 200000;
    f.lat_e7 = -338567000 + i * 500;
    f.lon_e7 = 185170000;
    f.alt_mm = 45000;
    f.gspeed_mms = 30000;                                 /* whole cm/s, so DELTA is exact */
    f.head_e5 = 9000000;                                  /* whole 0.01 deg, so DELTA is exact */
    f.hacc_mm = 1500;                                     /* whole dm, so DELTA is exact */
    f.sacc_mms = 300; f.pdop_e2 = 150; f.fix_type = 3; f.sats = 9;
    f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f.valid = valid;
    return f;
}

static void fill_hdr(ses_hdr_t *h)
{
    memset(h, 0, sizeof *h);
    memcpy(h->session_id, "S00042_007", 10);              /* all 10 wire bytes used */
    memcpy(h->fw, "v0.3.1-abcdefghi", 16);                /* all 16 wire bytes used */
    strcpy(h->hwid, "moto_neo6m_epaper_int_bl");
    h->mode = 1; h->variant = 2; h->venue_id = 1000; h->layout_id = 3;
    h->log_profile = 1; h->fused_hz = 10; h->gps_hz = 5; h->start_gps_us = T0_GPS_US;
    for (int i = 0; i < 9; i++) h->r_e4[i] = (int16_t)(i * 1000 - 4000);
    h->gbias[0] = -12; h->gbias[1] = 340; h->gbias[2] = 7;
    h->calib_flags = 0x03;
}

static fused_sample_t mk_fused(int64_t gps_us, float glat)
{
    fused_sample_t s; memset(&s, 0, sizeof s);
    s.gps_us = gps_us; s.mono_us = gps_us;
    s.g_lat = glat; s.g_lon = -0.25f; s.lean_deg = 18.5f; s.yaw_dps = -7.25f;
    s.flags = FUS_LEAN_VALID | FUS_ORIENT_OK;
    return s;
}

static void fill_lap(lap_result_t *l)
{
    memset(l, 0, sizeof *l);
    l->lap_no = 3; l->start_gps_us = T0_GPS_US; l->time_ms = 91234; l->flags = LAP_F_VALID; l->n_sectors = 3;
    l->sector_ms[0] = 30100; l->sector_ms[1] = 30500; l->sector_ms[2] = 30634;
    l->stats.max_speed_cms = 5011; l->stats.min_speed_cms = 1500;
    l->stats.max_lean_l_cdeg = -4200; l->stats.max_lean_r_cdeg = 4500;
    l->stats.max_glat_e3 = 1320; l->stats.max_gacc_e3 = 610; l->stats.max_gbrake_e3 = -1050;
}

static void fill_run(drag_result_t *r)
{
    memset(r, 0, sizeof *r);
    r->run_no = 2; r->t0_gps_us = T0_GPS_US + 1000000; r->flags = DRAG_F_QUARTER; r->trap_cms = 8472; r->n_gates = 4;
    r->gates[0] = (drag_gate_res_t){ 1, 1810, 1000, 300, 1 };
    r->gates[1] = (drag_gate_res_t){ 2, 5660, 2778, 9800, 1 };
    r->gates[2] = (drag_gate_res_t){ 3, 9200, 5000, 25000, 1 };
    r->gates[3] = (drag_gate_res_t){ 4, 12810, 8472, 40234, 1 };
}

static void fill_calib(ses_calib_t *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 9; i++) c->r_e4[i] = (int16_t)(10000 - i * 2500);
    c->gbias[0] = 11; c->gbias[1] = -22; c->gbias[2] = 33;
    c->calib_flags = 0x03;
}

/* ---- tests ---- */

static void test_memory_round_trip_of_every_record_type(void)
{
    uint8_t mem[8192];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);

    ses_hdr_t h; fill_hdr(&h);
    lap_result_t lap; fill_lap(&lap);
    drag_result_t run; fill_run(&run);
    ses_calib_t cal; fill_calib(&cal);
    gps_fix_t f0 = mk_fix(0, 1), f1 = mk_fix(1, 1), f2 = mk_fix(2, 1);
    fused_sample_t u0 = mk_fused(f0.gps_us + 50000, 0.4f);
    fused_sample_t u1 = mk_fused(f0.gps_us + 150000, -0.6f);

    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 3, "Synthetic"));
    TEST_ASSERT_GREATER_THAN(0, logw_time_map(&w, 1000000, T0_GPS_US, 2));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f2));
    TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &lap));
    TEST_ASSERT_GREATER_THAN(0, logw_sector(&w, 3, 1, T0_GPS_US + 30100000, 30100, -210));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_run(&w, &run));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_gate(&w, 2, 3, T0_GPS_US + 9200000, 9200, 5000, 25000));
    TEST_ASSERT_GREATER_THAN(0, logw_event(&w, 1234, T0_GPS_US, 0x0101, 0xDEADBEEF));
    TEST_ASSERT_GREATER_THAN(0, logw_calib(&w, &cal));
    TEST_ASSERT_GREATER_THAN(0, logw_mark(&w, T0_GPS_US + 7, 1));
    TEST_ASSERT_GREATER_THAN(0, logw_power(&w, 4242, 3, 3900));
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 20000000, 2));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(17, w.frames);

    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);

    TEST_ASSERT_EQUAL_UINT32(17, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);

    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_SESSION_HDR]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_VENUE]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_TIME_MAP]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_FIX_KEY]);       /* f0 only */
    TEST_ASSERT_EQUAL_UINT32(2, r.n_by_type[SES_T_FIX_DELTA]);     /* f1, f2 */
    TEST_ASSERT_EQUAL_UINT32(2, r.n_by_type[SES_T_FUSED]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_LAP]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_SECTOR]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_DRAG_RUN]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_DRAG_GATE]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_EVENT]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_CALIB]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_MARK]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_POWER]);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_by_type[SES_T_END]);

    TEST_ASSERT_EQUAL_INT(1, c.n_hdr);
    TEST_ASSERT_EQUAL_MEMORY(&h, &c.hdr, sizeof h);
    TEST_ASSERT_EQUAL_STRING("S00042_007", c.hdr.session_id);
    TEST_ASSERT_EQUAL_STRING("v0.3.1-abcdefghi", c.hdr.fw);

    TEST_ASSERT_EQUAL_INT(1, c.n_venue);
    TEST_ASSERT_EQUAL_UINT16(1000, c.venue.venue_id);
    TEST_ASSERT_EQUAL_UINT16(3, c.venue.layout_id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", c.venue.name);

    TEST_ASSERT_EQUAL_INT(1, c.n_tm);
    TEST_ASSERT_EQUAL_INT64(1000000, c.tm.mono_us);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US, c.tm.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, c.tm.quality);

    TEST_ASSERT_EQUAL_INT(3, c.n_fix);
    const gps_fix_t *src[3] = { &f0, &f1, &f2 };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT64(src[i]->gps_us, c.fix[i].gps_us);
        TEST_ASSERT_EQUAL_INT32(src[i]->lat_e7, c.fix[i].lat_e7);
        TEST_ASSERT_EQUAL_INT32(src[i]->lon_e7, c.fix[i].lon_e7);
        TEST_ASSERT_INT32_WITHIN(FIX_ALT_TOL_MM, src[i]->alt_mm, c.fix[i].alt_mm);
        TEST_ASSERT_INT32_WITHIN(FIX_SPEED_TOL_MMS, src[i]->gspeed_mms, c.fix[i].gspeed_mms);
        TEST_ASSERT_INT32_WITHIN(FIX_HEAD_TOL_E5, src[i]->head_e5, c.fix[i].head_e5);
        TEST_ASSERT_INT32_WITHIN(FIX_HACC_TOL_MM, (int32_t)src[i]->hacc_mm, (int32_t)c.fix[i].hacc_mm);
        TEST_ASSERT_EQUAL_UINT8(src[i]->sats, c.fix[i].sats);
        TEST_ASSERT_EQUAL_UINT8(src[i]->valid, c.fix[i].valid);
        TEST_ASSERT_EQUAL_UINT8(3, c.fix[i].fix_type);
    }

    TEST_ASSERT_EQUAL_INT(2, c.n_fused);
    TEST_ASSERT_EQUAL_INT64(u0.gps_us, c.fused[0].gps_us);
    TEST_ASSERT_EQUAL_INT64(u1.gps_us, c.fused[1].gps_us);
    /* FUSED quantisation (§12.3): g in 1e-3, lean and yaw in 1e-2 deg. */
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u0.g_lat, c.fused[0].g_lat);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u0.g_lon, c.fused[0].g_lon);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, u0.lean_deg, c.fused[0].lean_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, u0.yaw_dps, c.fused[0].yaw_dps);
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID | FUS_ORIENT_OK, c.fused[0].flags);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, u1.g_lat, c.fused[1].g_lat);

    TEST_ASSERT_EQUAL_INT(1, c.n_lap);
    TEST_ASSERT_EQUAL_MEMORY(&lap, &c.lap, sizeof lap);
    TEST_ASSERT_EQUAL_INT(1, c.n_sec);
    TEST_ASSERT_EQUAL_UINT16(3, c.sec.lap_no);
    TEST_ASSERT_EQUAL_UINT8(1, c.sec.idx);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 30100000, c.sec.gps_us);
    TEST_ASSERT_EQUAL_UINT32(30100, c.sec.split_ms);
    TEST_ASSERT_EQUAL_INT32(-210, c.sec.delta_ms);

    TEST_ASSERT_EQUAL_INT(1, c.n_run);
    TEST_ASSERT_EQUAL_MEMORY(&run, &c.run, sizeof run);
    TEST_ASSERT_EQUAL_INT(1, c.n_dg);
    TEST_ASSERT_EQUAL_UINT16(2, c.dg.run_no);
    TEST_ASSERT_EQUAL_UINT8(3, c.dg.gate_id);
    TEST_ASSERT_EQUAL_UINT32(9200, c.dg.time_ms);
    TEST_ASSERT_EQUAL_UINT16(5000, c.dg.speed_cms);
    TEST_ASSERT_EQUAL_UINT32(25000, c.dg.dist_cm);

    TEST_ASSERT_EQUAL_INT(1, c.n_ev);
    TEST_ASSERT_EQUAL_INT64(1234, c.ev.mono_us);
    TEST_ASSERT_EQUAL_HEX16(0x0101, c.ev.code);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, c.ev.arg);

    TEST_ASSERT_EQUAL_INT(1, c.n_cal);
    for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(cal.r_e4[i], c.cal.r_e4[i]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT16(cal.gbias[i], c.cal.gbias[i]);
    TEST_ASSERT_EQUAL_HEX8(cal.calib_flags, c.cal.calib_flags);

    TEST_ASSERT_EQUAL_INT(1, c.n_mk);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 7, c.mk.gps_us);
    TEST_ASSERT_EQUAL_UINT8(1, c.mk.kind);
    TEST_ASSERT_EQUAL_INT(1, c.n_pw);
    TEST_ASSERT_EQUAL_INT64(4242, c.pw.mono_us);
    TEST_ASSERT_EQUAL_UINT8(3, c.pw.state);
    TEST_ASSERT_EQUAL_UINT16(3900, c.pw.batt_mv);
    TEST_ASSERT_EQUAL_INT(1, c.n_end);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 20000000, c.end.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, c.end.reason);
    TEST_ASSERT_EQUAL_INT(0, c.n_bad_cb);
}

static void test_keyframe_cadence_and_forced_key_after_invalid(void)
{
    uint8_t mem[8192];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    for (int i = 0; i < 100; i++) {                       /* 100 fixes at 5 Hz = 19.8 s of span */
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
    }
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(100, r.n_frames);
    /* §12.4: FIX_KEY for the first fix and every FIX_KEYFRAME_S = 5 s. Keys land at t = 0, 5, 10,
     * 15 s (fix 0, 25, 50, 75); t = 20 s would be fix 100, one past the end. */
    TEST_ASSERT_EQUAL_UINT32(4, r.n_by_type[SES_T_FIX_KEY]);
    TEST_ASSERT_EQUAL_UINT32(96, r.n_by_type[SES_T_FIX_DELTA]);
    TEST_ASSERT_EQUAL_INT(100, c.n_fix);

    /* An invalid fix forces the next fix to be a key (§12.4). */
    logw_t w2; logw_open_mem(&w2, mem, sizeof mem);
    gps_fix_t a = mk_fix(0, 1);
    size_t at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &a));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, mem[at + 1]);           /* first fix */
    gps_fix_t b = mk_fix(1, 1);
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &b));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, mem[at + 1]);
    gps_fix_t bad = mk_fix(2, 0);                                  /* invalid, still logged */
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &bad));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, mem[at + 1]);
    gps_fix_t after = mk_fix(3, 1);
    at = w2.len; TEST_ASSERT_GREATER_THAN(0, logw_fix(&w2, &after));
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, mem[at + 1]);
}

static void test_fused_and_fix_states_stay_synchronised(void)
{
    uint8_t mem[4096];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    int64_t want[12]; int n_want = 0;
    for (int i = 0; i < 6; i++) {
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
        for (int k = 0; k < 2; k++) {                    /* 10 Hz fused between 5 Hz fixes */
            int64_t t = f.gps_us + (k + 1) * 50000;
            fused_sample_t u = mk_fused(t, 0.1f * (float)k);
            TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u));
            want[n_want++] = t;
        }
    }
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, w.len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_INT(6, c.n_fix);
    TEST_ASSERT_EQUAL_INT(12, c.n_fused);
    /* Exact only if both sides call ses_fused_state_on_fix on every FIX_* record (§12.4). */
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_INT64(want[i], c.fused[i].gps_us);
}

static void test_memory_cap_makes_writes_fail_stickily(void)
{
    uint8_t mem[64];
    logw_t w; logw_open_mem(&w, mem, sizeof mem);
    gps_fix_t f0 = mk_fix(0, 1);
    /* FIX_KEY is 39 + SES_FRAME_OVERHEAD = 44 bytes; a second key does not fit in 64. */
    TEST_ASSERT_EQUAL_INT(39 + SES_FRAME_OVERHEAD, logw_fix(&w, &f0));
    gps_fix_t f1 = mk_fix(0, 1);
    f1.gps_us = f0.gps_us + 6000000;                     /* > FIX_KEYFRAME_S, so this one is a key too */
    TEST_ASSERT_EQUAL_INT(-1, logw_fix(&w, &f1));
    TEST_ASSERT_EQUAL_INT(-1, w.err);
    TEST_ASSERT_EQUAL_UINT32(1, w.frames);
    size_t len_after_failure = w.len;
    TEST_ASSERT_EQUAL_INT(-1, logw_end(&w, f0.gps_us, 0));
    TEST_ASSERT_EQUAL_INT(-1, logw_mark(&w, f0.gps_us, 1));
    TEST_ASSERT_EQUAL_UINT32(1, w.frames);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)len_after_failure, (uint64_t)w.len);
    TEST_ASSERT_EQUAL_INT(-1, logw_close(&w));
}

static void test_file_round_trip_and_missing_file(void)
{
    make_tmp();
    logw_t w;
    TEST_ASSERT_EQUAL_INT(0, logw_open_file(&w, tmp_path));

    ses_hdr_t h; fill_hdr(&h);
    lap_result_t lap; fill_lap(&lap);
    drag_result_t run; fill_run(&run);
    ses_calib_t cal; fill_calib(&cal);
    gps_fix_t f0 = mk_fix(0, 1), f1 = mk_fix(1, 1), f2 = mk_fix(2, 1);
    fused_sample_t u0 = mk_fused(f0.gps_us + 50000, 0.4f);
    fused_sample_t u1 = mk_fused(f0.gps_us + 150000, -0.6f);
    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 3, "Synthetic"));
    TEST_ASSERT_GREATER_THAN(0, logw_time_map(&w, 1000000, T0_GPS_US, 2));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u0));
    TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f1));
    TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f2));
    TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &lap));
    TEST_ASSERT_GREATER_THAN(0, logw_sector(&w, 3, 1, T0_GPS_US + 30100000, 30100, -210));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_run(&w, &run));
    TEST_ASSERT_GREATER_THAN(0, logw_drag_gate(&w, 2, 3, T0_GPS_US + 9200000, 9200, 5000, 25000));
    TEST_ASSERT_GREATER_THAN(0, logw_event(&w, 1234, T0_GPS_US, 0x0101, 0xDEADBEEF));
    TEST_ASSERT_GREATER_THAN(0, logw_calib(&w, &cal));
    TEST_ASSERT_GREATER_THAN(0, logw_mark(&w, T0_GPS_US + 7, 1));
    TEST_ASSERT_GREATER_THAN(0, logw_power(&w, 4242, 3, 3900));
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 20000000, 2));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(17, w.frames);

    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    TEST_ASSERT_EQUAL_INT(0, logr_read_file(&r, tmp_path));
    TEST_ASSERT_EQUAL_UINT32(17, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_INT(1, c.n_hdr);
    TEST_ASSERT_EQUAL_MEMORY(&h, &c.hdr, sizeof h);
    TEST_ASSERT_EQUAL_INT(3, c.n_fix);
    TEST_ASSERT_EQUAL_INT64(f2.gps_us, c.fix[2].gps_us);
    TEST_ASSERT_EQUAL_INT32(f2.lat_e7, c.fix[2].lat_e7);
    TEST_ASSERT_EQUAL_INT(2, c.n_fused);
    TEST_ASSERT_EQUAL_INT64(u1.gps_us, c.fused[1].gps_us);
    TEST_ASSERT_EQUAL_INT(1, c.n_lap);
    TEST_ASSERT_EQUAL_MEMORY(&lap, &c.lap, sizeof lap);
    TEST_ASSERT_EQUAL_INT(1, c.n_run);
    TEST_ASSERT_EQUAL_MEMORY(&run, &c.run, sizeof run);
    TEST_ASSERT_EQUAL_INT(1, c.n_end);

    col_t c2; memset(&c2, 0, sizeof c2);
    logr_t r2; logr_init(&r2, &ALL_CB, &c2);
    TEST_ASSERT_EQUAL_INT(-1, logr_read_file(&r2, "/tmp/laptimer_logio_does_not_exist"));
}

/* Writes n fixes at 5 Hz into mem, recording each frame's offset. Returns the byte length. */
static size_t write_fix_stream(uint8_t *mem, size_t cap, int n, size_t *off)
{
    logw_t w; logw_open_mem(&w, mem, cap);
    for (int i = 0; i < n; i++) {
        off[i] = w.len;
        gps_fix_t f = mk_fix(i, 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
    }
    return w.len;
}

static void test_truncated_tail_is_one_bad_frame(void)
{
    uint8_t mem[4096]; size_t off[50];
    size_t len = write_fix_stream(mem, sizeof mem, 50, off);
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, len - 3);                          /* cut the last frame's flags byte and CRC */
    logr_finish(&r);
    TEST_ASSERT_EQUAL_INT(49, c.n_fix);
    TEST_ASSERT_EQUAL_UINT32(49, r.n_frames);
    TEST_ASSERT_EQUAL_UINT32(1, r.rd.frames_bad);         /* the flush treats the stub as one bad frame */
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);                 /* no decoder rejected anything */
    TEST_ASSERT_EQUAL_INT(0, c.n_bad_cb);
}

static void test_corrupted_delta_drops_one_frame_until_the_next_key(void)
{
    uint8_t mem[4096]; size_t off[50];
    size_t len = write_fix_stream(mem, sizeof mem, 50, off);
    mem[off[9] + 3] ^= 0xFF;                              /* flip the first payload byte of frame 10 */
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, mem, len);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(49, r.n_frames);             /* the bad CRC costs exactly that frame */
    TEST_ASSERT_EQUAL_UINT32(1, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_bad);
    TEST_ASSERT_EQUAL_INT(49, c.n_fix);
    /* §12.4 guarantees only that a FIX_KEY is written for the first fix and every FIX_KEYFRAME_S
     * seconds, so a lost FIX_DELTA leaves the reconstruction one delta behind until the next KEY.
     * Delivery 9 is the record of fix 10 applied to the state of fix 8, i.e. it reads as fix 9. */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 9 * 200000, c.fix[9].gps_us);
    TEST_ASSERT_EQUAL_INT32(-338567000 + 9 * 500, c.fix[9].lat_e7);
    /* The next key is fix 25 (t = 5 s), delivered at index 24 because one frame was dropped. */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 25 * 200000, c.fix[24].gps_us);
    TEST_ASSERT_EQUAL_INT32(-338567000 + 25 * 500, c.fix[24].lat_e7);
    /* Everything after the key is exact again, up to the last fix. */
    for (int j = 24; j < 49; j++) {
        int i = j + 1;
        TEST_ASSERT_EQUAL_INT64(T0_GPS_US + (int64_t)i * 200000, c.fix[j].gps_us);
        TEST_ASSERT_EQUAL_INT32(-338567000 + i * 500, c.fix[j].lat_e7);
    }
}

static void test_unknown_record_type_is_reported_bad(void)
{
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t frame[32];
    int n = ses_frame_encode(0x5A, payload, (uint8_t)sizeof payload, frame, sizeof frame);
    TEST_ASSERT_EQUAL_INT((int)sizeof payload + SES_FRAME_OVERHEAD, n);
    col_t c; memset(&c, 0, sizeof c);
    logr_t r; logr_init(&r, &ALL_CB, &c);
    logr_feed(&r, frame, (size_t)n);
    logr_finish(&r);
    TEST_ASSERT_EQUAL_UINT32(1, r.n_frames);              /* the framing layer accepted it */
    TEST_ASSERT_EQUAL_UINT32(1, r.n_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.rd.frames_bad);
    TEST_ASSERT_EQUAL_UINT32(0, r.n_by_type[0x5A]);
    TEST_ASSERT_EQUAL_INT(1, c.n_bad_cb);
    TEST_ASSERT_EQUAL_HEX8(0x5A, c.bad_type);
    TEST_ASSERT_EQUAL_UINT8(4, c.bad_len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_memory_round_trip_of_every_record_type);
    RUN_TEST(test_keyframe_cadence_and_forced_key_after_invalid);
    RUN_TEST(test_fused_and_fix_states_stay_synchronised);
    RUN_TEST(test_memory_cap_makes_writes_fail_stickily);
    RUN_TEST(test_file_round_trip_and_missing_file);
    RUN_TEST(test_truncated_tail_is_one_bad_frame);
    RUN_TEST(test_corrupted_delta_drops_one_frame_until_the_next_key);
    RUN_TEST(test_unknown_record_type_is_reported_bad);
    return UNITY_END();
}
```

No CMake edit is needed: `tools/replay/CMakeLists.txt` globs `test/test_*.c` with
`CONFIGURE_DEPENDS`, so the new file is picked up on the next build.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build --parallel 2>&1 | tail -25`
Expected: FAIL at link time —

```
Undefined symbols for architecture arm64:
  "_logr_feed", referenced from:
  "_logr_finish", referenced from:
  "_logr_init", referenced from:
  "_logr_read_file", referenced from:
  "_logw_calib", referenced from:
  ...
  "_logw_venue", referenced from:
```

(on the gcc-16 parity build the same failure reads `undefined reference to 'logw_open_mem'`).

- [ ] **Step 3: Implement the writer and reader**

`tools/replay/lib/logio.c`:

```c
#include "replay/logio.h"
#include "core/consts.h"
#include <string.h>

/* Session .log writer and reader (spec §12.2–§12.5).
 *
 * Every ses_encode_* in components/core/session/ses_records.c ends in ses_frame_encode(), so the
 * record encoders return a COMPLETE frame (sync | type | len | payload | crc16), not a bare
 * payload. logio therefore never frames anything itself: it emits what the encoder produced. */

/* Largest frame the format allows: type + len + payload + crc16, plus one byte for the sync. */
#define LOGW_FRAME_CAP (2 + SES_MAX_PAYLOAD + 2 + 1)
/* Read granularity of logr_read_file; matches the logger's 4 KB buffer (§12.5). */
#define LOGR_CHUNK 4096

/* ---------------- writer ---------------- */

static void logw_reset(logw_t *w)
{
    memset(w, 0, sizeof *w);
    ses_fix_state_init(&w->fix_st);
    ses_fused_state_init(&w->fused_st);
}

int logw_open_file(logw_t *w, const char *path)
{
    logw_reset(w);
    w->f = fopen(path, "wb");
    if (!w->f) { w->err = -1; return -1; }
    return 0;
}

void logw_open_mem(logw_t *w, uint8_t *buf, size_t cap)
{
    logw_reset(w);
    w->mem = buf; w->cap = cap;
}

/* Appends one encoded frame. n is the encoder's return value, so a -1 from the encoder (payload too
 * large, cap too small) becomes a sticky write error here. Returns n, or -1. */
static int emit(logw_t *w, const uint8_t *frame, int n)
{
    if (w->err) return -1;
    if (n <= 0) { w->err = -1; return -1; }
    size_t len = (size_t)n;
    if (w->f) {
        if (fwrite(frame, 1, len, w->f) != len) { w->err = -1; return -1; }
    } else if (w->mem) {
        if (w->len + len > w->cap) { w->err = -1; return -1; }
        memcpy(w->mem + w->len, frame, len);
    } else {
        w->err = -1; return -1;                       /* neither logw_open_file nor logw_open_mem */
    }
    w->len += len;
    w->frames++;
    return n;
}

int logw_hdr(logw_t *w, const ses_hdr_t *h)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_hdr(h, fr, sizeof fr));
}

int logw_venue(logw_t *w, uint16_t venue_id, uint16_t layout_id, const char *name)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_venue(venue_id, layout_id, name, fr, sizeof fr));
}

int logw_time_map(logw_t *w, int64_t mono_us, int64_t gps_us, uint8_t quality)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_time_map(mono_us, gps_us, quality, fr, sizeof fr));
}

int logw_fix(logw_t *w, const gps_fix_t *fix)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    int n = emit(w, fr, ses_encode_fix(&w->fix_st, fix, fr, sizeof fr));
    /* §12.4: FUSED.dt_ms is relative to the last FIX_* when no FUSED followed it, so the writer's
     * fused reference moves to every fix it emits — exactly what the reader does on decode. */
    if (n > 0) ses_fused_state_on_fix(&w->fused_st, fix->gps_us);
    return n;
}

int logw_fused(logw_t *w, const fused_sample_t *fs)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_fused(&w->fused_st, fs, fr, sizeof fr));
}

int logw_lap(logw_t *w, const lap_result_t *lap)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_lap(lap, fr, sizeof fr));
}

int logw_sector(logw_t *w, uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_sector(lap_no, idx, gps_us, split_ms, delta_ms, fr, sizeof fr));
}

int logw_drag_run(logw_t *w, const drag_result_t *run)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_drag_run(run, fr, sizeof fr));
}

int logw_drag_gate(logw_t *w, uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_drag_gate(run_no, gate_id, gps_us, time_ms, speed_cms, dist_cm, fr, sizeof fr));
}

int logw_event(logw_t *w, int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_event(mono_us, gps_us, code, arg, fr, sizeof fr));
}

int logw_calib(logw_t *w, const ses_calib_t *c)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_calib(c, fr, sizeof fr));
}

int logw_mark(logw_t *w, int64_t gps_us, uint8_t kind)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_mark(gps_us, kind, fr, sizeof fr));
}

int logw_power(logw_t *w, int64_t mono_us, uint8_t state, uint16_t batt_mv)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_power(mono_us, state, batt_mv, fr, sizeof fr));
}

int logw_end(logw_t *w, int64_t gps_us, uint8_t reason)
{
    if (w->err) return -1;
    uint8_t fr[LOGW_FRAME_CAP];
    return emit(w, fr, ses_encode_end(gps_us, reason, fr, sizeof fr));
}

int logw_close(logw_t *w)
{
    if (w->f) {
        if (fflush(w->f) != 0) w->err = -1;
        if (fclose(w->f) != 0) w->err = -1;
        w->f = NULL;
    }
    return w->err;
}

/* ---------------- reader ---------------- */

/* One framed record. `payload` is the reader's internal buffer and is valid only here. */
static void logr_frame_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    logr_t *r = ctx;
    const logr_cb_t *cb = r->cb;
    bool ok = false;
    r->n_frames++;

    switch (type) {
    case SES_T_FIX_KEY:
    case SES_T_FIX_DELTA: {
        gps_fix_t f;
        if (ses_decode_fix(&r->fix_st, type, payload, len, &f) == 1) {
            ok = true;
            /* §12.4: keep the fused reference in step with the writer's logw_fix. */
            ses_fused_state_on_fix(&r->fused_st, f.gps_us);
            if (cb && cb->on_fix) cb->on_fix(&f, r->ctx);
        }
        break;
    }
    case SES_T_FUSED: {
        fused_sample_t fs;
        if (ses_decode_fused(&r->fused_st, payload, len, &fs) == 1) {
            ok = true;
            if (cb && cb->on_fused) cb->on_fused(&fs, r->ctx);
        }
        break;
    }
    case SES_T_SESSION_HDR: {
        ses_hdr_t h;
        if (ses_decode_hdr(payload, len, &h) == 1) { ok = true; if (cb && cb->on_hdr) cb->on_hdr(&h, r->ctx); }
        break;
    }
    case SES_T_VENUE: {
        ses_venue_t v;
        if (ses_decode_venue(payload, len, &v) == 1) { ok = true; if (cb && cb->on_venue) cb->on_venue(&v, r->ctx); }
        break;
    }
    case SES_T_TIME_MAP: {
        ses_time_map_t t;
        if (ses_decode_time_map(payload, len, &t) == 1) { ok = true; if (cb && cb->on_time_map) cb->on_time_map(&t, r->ctx); }
        break;
    }
    case SES_T_LAP: {
        lap_result_t l;
        if (ses_decode_lap(payload, len, &l) == 1) { ok = true; if (cb && cb->on_lap) cb->on_lap(&l, r->ctx); }
        break;
    }
    case SES_T_SECTOR: {
        ses_sector_t s;
        if (ses_decode_sector(payload, len, &s) == 1) { ok = true; if (cb && cb->on_sector) cb->on_sector(&s, r->ctx); }
        break;
    }
    case SES_T_DRAG_RUN: {
        drag_result_t d;
        if (ses_decode_drag_run(payload, len, &d) == 1) { ok = true; if (cb && cb->on_drag_run) cb->on_drag_run(&d, r->ctx); }
        break;
    }
    case SES_T_DRAG_GATE: {
        ses_drag_gate_t g;
        if (ses_decode_drag_gate(payload, len, &g) == 1) { ok = true; if (cb && cb->on_drag_gate) cb->on_drag_gate(&g, r->ctx); }
        break;
    }
    case SES_T_EVENT: {
        ses_event_t e;
        if (ses_decode_event(payload, len, &e) == 1) { ok = true; if (cb && cb->on_event) cb->on_event(&e, r->ctx); }
        break;
    }
    case SES_T_CALIB: {
        ses_calib_t c;
        if (ses_decode_calib(payload, len, &c) == 1) { ok = true; if (cb && cb->on_calib) cb->on_calib(&c, r->ctx); }
        break;
    }
    case SES_T_MARK: {
        ses_mark_t m;
        if (ses_decode_mark(payload, len, &m) == 1) { ok = true; if (cb && cb->on_mark) cb->on_mark(&m, r->ctx); }
        break;
    }
    case SES_T_POWER: {
        ses_power_t p;
        if (ses_decode_power(payload, len, &p) == 1) { ok = true; if (cb && cb->on_power) cb->on_power(&p, r->ctx); }
        break;
    }
    case SES_T_END: {
        ses_end_t e;
        if (ses_decode_end(payload, len, &e) == 1) { ok = true; if (cb && cb->on_end) cb->on_end(&e, r->ctx); }
        break;
    }
    default:
        break;                                        /* unknown type: counted as bad below */
    }

    if (ok) {
        /* Every known type is <= SES_T_END (0x7F); the guard keeps a future 8-bit type out of the
         * 128-entry histogram rather than trusting the switch above to stay in range. */
        if (type < 128) r->n_by_type[type]++;
    } else {
        r->n_bad++;
        if (cb && cb->on_bad) cb->on_bad(type, len, r->ctx);
    }
}

void logr_init(logr_t *r, const logr_cb_t *cb, void *ctx)
{
    memset(r, 0, sizeof *r);
    ses_reader_init(&r->rd);
    ses_fix_state_init(&r->fix_st);
    ses_fused_state_init(&r->fused_st);
    r->cb = cb; r->ctx = ctx;
}

void logr_feed(logr_t *r, const uint8_t *buf, size_t n)
{
    ses_reader_feed(&r->rd, buf, n, logr_frame_cb, r);
}

void logr_finish(logr_t *r)
{
    ses_reader_flush(&r->rd, logr_frame_cb, r);
}

int logr_read_file(logr_t *r, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[LOGR_CHUNK];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n) logr_feed(r, buf, n);
        if (n < sizeof buf) break;                    /* short read: EOF or error, ferror decides */
    }
    logr_finish(r);
    int rc = ferror(f) ? -1 : 0;
    fclose(f);
    return rc;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build --parallel 2>&1 | grep -E "error|warning" ; ctest --test-dir test/build --output-on-failure -R test_logio 2>&1 | tail -5`
Expected: no compiler output, then

```
    Start 13: test_logio
1/1 Test #13: test_logio .......................   Passed    0.49 sec

100% tests passed out of 1
```

- [ ] **Step 5: Commit**

```bash
git add tools/replay/lib/logio.c tools/replay/test/test_logio.c
git commit -m "feat(tools): .log writer and reader over the ses_* codecs

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

- [ ] **Step 6: Write the failing summary test**

`tools/replay/test/test_replay_summary.c`:

```c
/* mkstemp/unlink/close/open_memstream are POSIX, not C11; the build is -std=c11. */
#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "replay/replay.h"
#include "replay/logio.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define T0_GPS_US 1789380900000000LL
#define N_FIX     25
#define N_FUSED   10
#define INVALID_I 7                      /* the one fix written with valid = 0 */
#define JSON_MAX_TOKS 1024               /* the summary object is ~200 tokens; 1024 is ample headroom */

static char tmp_path[64];

void setUp(void) { tmp_path[0] = '\0'; }
void tearDown(void) { if (tmp_path[0]) { unlink(tmp_path); tmp_path[0] = '\0'; } }

static void make_tmp(void)
{
    strcpy(tmp_path, "/tmp/laptimer_rsum_XXXXXX");
    int fd = mkstemp(tmp_path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
}

/* Writes the fixture log: hdr, venue, 25 fixes at 5 Hz (one invalid), 10 fused, 2 laps, end. */
static void write_fixture(void)
{
    make_tmp();
    logw_t w;
    TEST_ASSERT_EQUAL_INT(0, logw_open_file(&w, tmp_path));

    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00001_001", 10);
    strcpy(h.fw, "v0.2.0"); strcpy(h.hwid, "moto_neo6m_epaper");
    h.mode = 0; h.variant = 0; h.venue_id = 1000; h.layout_id = 1;
    h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = T0_GPS_US;
    TEST_ASSERT_GREATER_THAN(0, logw_hdr(&w, &h));
    TEST_ASSERT_GREATER_THAN(0, logw_venue(&w, 1000, 1, "Synthetic"));

    for (int i = 0; i < N_FIX; i++) {
        gps_fix_t f; memset(&f, 0, sizeof f);
        f.gps_us = T0_GPS_US + (int64_t)i * 200000;
        f.mono_us = 1000000 + (int64_t)i * 200000;
        f.lat_e7 = -338567000 + i * 500; f.lon_e7 = 185170000; f.alt_mm = 45000;
        f.gspeed_mms = 30000 + i * 100;          /* whole cm/s: the delta field is exact */
        f.head_e5 = 9000000; f.hacc_mm = 1500; f.sacc_mms = 300; f.pdop_e2 = 150;
        f.fix_type = 3; f.sats = 9;
        f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
        f.valid = (uint8_t)((i == INVALID_I) ? 0 : 1);
        TEST_ASSERT_GREATER_THAN(0, logw_fix(&w, &f));
        if (i < N_FUSED) {
            fused_sample_t u; memset(&u, 0, sizeof u);
            u.gps_us = f.gps_us + 100000; u.mono_us = u.gps_us;
            u.g_lat = 0.5f; u.g_lon = -0.2f; u.lean_deg = 20.0f; u.yaw_dps = -8.0f;
            u.flags = FUS_LEAN_VALID | FUS_ORIENT_OK;
            TEST_ASSERT_GREATER_THAN(0, logw_fused(&w, &u));
        }
    }

    for (int k = 0; k < 2; k++) {
        lap_result_t l; memset(&l, 0, sizeof l);
        l.lap_no = (uint16_t)(k + 1);
        l.start_gps_us = T0_GPS_US + (int64_t)k * 91000000;
        l.time_ms = (uint32_t)(91234 - k * 247);
        l.flags = LAP_F_VALID; l.n_sectors = 3;
        l.sector_ms[0] = 30100; l.sector_ms[1] = 30500; l.sector_ms[2] = l.time_ms - 60600;
        TEST_ASSERT_GREATER_THAN(0, logw_lap(&w, &l));
    }
    TEST_ASSERT_GREATER_THAN(0, logw_end(&w, T0_GPS_US + 200000000, 1));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    TEST_ASSERT_EQUAL_UINT32(2 + N_FIX + N_FUSED + 2 + 1, w.frames);
}

/* Renders with `pr` into a heap string the caller frees. */
static char *render(void (*pr)(const replay_summary_t *, FILE *), const replay_summary_t *s)
{
    char *out = NULL; size_t n = 0;
    FILE *ms = open_memstream(&out, &n);
    TEST_ASSERT_NOT_NULL(ms);
    pr(s, ms);
    TEST_ASSERT_EQUAL_INT(0, fclose(ms));
    TEST_ASSERT_NOT_NULL(out);
    return out;
}

static void test_summary_counts_and_spans(void)
{
    write_fixture();
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(0, replay_summarize_file(tmp_path, &s));

    TEST_ASSERT_EQUAL_UINT32(2 + N_FIX + N_FUSED + 2 + 1, s.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0, s.n_bad);
    TEST_ASSERT_EQUAL_INT(1, s.have_hdr);
    TEST_ASSERT_EQUAL_STRING("S00001_001", s.hdr.session_id);
    TEST_ASSERT_EQUAL_UINT8(5, s.hdr.gps_hz);
    TEST_ASSERT_EQUAL_INT(1, s.have_venue);
    TEST_ASSERT_EQUAL_STRING("Synthetic", s.venue.name);

    TEST_ASSERT_EQUAL_UINT32(N_FIX, s.n_fix);
    TEST_ASSERT_EQUAL_UINT32(N_FIX - 1, s.n_fix_valid);              /* one fix written invalid */
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US, s.first_fix_gps_us);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + (int64_t)(N_FIX - 1) * 200000, s.last_fix_gps_us);
    TEST_ASSERT_EQUAL_INT32(30000 + (N_FIX - 1) * 100, s.max_gspeed_mms);
    TEST_ASSERT_EQUAL_UINT32(N_FUSED, s.n_fused);

    TEST_ASSERT_EQUAL_UINT32(2, s.n_lap);
    TEST_ASSERT_EQUAL_UINT16(2, s.n_laps_listed);
    TEST_ASSERT_EQUAL_UINT16(1, s.laps[0].lap_no);
    TEST_ASSERT_EQUAL_UINT32(91234, s.laps[0].time_ms);
    TEST_ASSERT_EQUAL_UINT16(2, s.laps[1].lap_no);
    TEST_ASSERT_EQUAL_UINT32(90987, s.laps[1].time_ms);
    TEST_ASSERT_EQUAL_UINT8(3, s.laps[1].n_sectors);

    TEST_ASSERT_EQUAL_INT(1, s.have_end);
    TEST_ASSERT_EQUAL_UINT8(1, s.end.reason);
    TEST_ASSERT_EQUAL_INT64(T0_GPS_US + 200000000, s.end.gps_us);

    TEST_ASSERT_EQUAL_UINT32(1, s.n_by_type[SES_T_SESSION_HDR]);
    TEST_ASSERT_EQUAL_UINT32(N_FUSED, s.n_by_type[SES_T_FUSED]);
    TEST_ASSERT_EQUAL_UINT32(2, s.n_by_type[SES_T_LAP]);
    TEST_ASSERT_EQUAL_UINT32(N_FIX, s.n_by_type[SES_T_FIX_KEY] + s.n_by_type[SES_T_FIX_DELTA]);
}

static void test_json_and_text_rendering(void)
{
    write_fixture();
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(0, replay_summarize_file(tmp_path, &s));

    char *js = render(replay_print_json, &s);
    TEST_ASSERT_NOT_NULL(strstr(js, "\"bad_frames\":0"));
    char want_frames[32];
    snprintf(want_frames, sizeof want_frames, "\"frames\":%u", (unsigned)s.n_frames);
    TEST_ASSERT_NOT_NULL(strstr(js, want_frames));

    jsmntok_t *toks = malloc(sizeof(jsmntok_t) * JSON_MAX_TOKS);
    TEST_ASSERT_NOT_NULL(toks);
    int ntoks = json_parse(js, strlen(js), toks, JSON_MAX_TOKS);
    TEST_ASSERT_GREATER_THAN(0, ntoks);
    TEST_ASSERT_EQUAL_INT(JSMN_OBJECT, toks[0].type);

    int laps = json_obj_get(js, toks, ntoks, 0, "laps");
    TEST_ASSERT_GREATER_THAN(0, laps);
    TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[laps].type);
    TEST_ASSERT_EQUAL_INT(2, toks[laps].size);

    int hdr = json_obj_get(js, toks, ntoks, 0, "hdr");
    TEST_ASSERT_GREATER_THAN(0, hdr);
    TEST_ASSERT_EQUAL_INT(JSMN_OBJECT, toks[hdr].type);
    int sid = json_obj_get(js, toks, ntoks, hdr, "session_id");
    TEST_ASSERT_GREATER_THAN(0, sid);
    TEST_ASSERT_TRUE(json_tok_eq(js, &toks[sid], "S00001_001"));

    int nlaps = json_obj_get(js, toks, ntoks, 0, "n_laps");
    TEST_ASSERT_GREATER_THAN(0, nlaps);
    int64_t v = 0;
    TEST_ASSERT_TRUE(json_tok_int(js, &toks[nlaps], &v));
    TEST_ASSERT_EQUAL_INT64(2, v);

    int fix = json_obj_get(js, toks, ntoks, 0, "fix");
    TEST_ASSERT_GREATER_THAN(0, fix);
    int nvalid = json_obj_get(js, toks, ntoks, fix, "valid");
    TEST_ASSERT_TRUE(json_tok_int(js, &toks[nvalid], &v));
    TEST_ASSERT_EQUAL_INT64(N_FIX - 1, v);

    free(toks);
    free(js);

    char *txt = render(replay_print_text, &s);
    TEST_ASSERT_NOT_NULL(strstr(txt, "frames"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "lap 1"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "lap 2"));
    TEST_ASSERT_NOT_NULL(strstr(txt, "end"));
    free(txt);
}

static void test_missing_file_fails(void)
{
    replay_summary_t s;
    TEST_ASSERT_EQUAL_INT(-1, replay_summarize_file("/tmp/laptimer_rsum_does_not_exist", &s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_summary_counts_and_spans);
    RUN_TEST(test_json_and_text_rendering);
    RUN_TEST(test_missing_file_fails);
    return UNITY_END();
}
```

- [ ] **Step 7: Run to verify it fails**

Run: `cmake --build test/build --parallel 2>&1 | tail -15`
Expected: FAIL at link time —

```
Undefined symbols for architecture arm64:
  "_replay_print_json", referenced from:
      _test_json_and_text_rendering in test_replay_summary.c.o
  "_replay_print_text", referenced from:
      _test_json_and_text_rendering in test_replay_summary.c.o
  "_replay_summarize_file", referenced from:
      _test_summary_counts_and_spans in test_replay_summary.c.o
```

- [ ] **Step 8: Implement the summary and the CLI**

`tools/replay/lib/replay_summary.c`:

```c
#include "replay/replay.h"
#include "replay/logio.h"
#include "core/jw.h"
#include <stdlib.h>
#include <string.h>

/* Summary of one .log (spec §12, §22.2).
 *
 * JSON contract of replay_print_json — these keys are stable; test_replay_summary.c and, from
 * session 2.7, the test/data/<name>.expected.json fixtures compare against them:
 *
 *   version      string  replay_version() (== core_version())
 *   frames       number  frames the framing layer accepted
 *   bad_frames   number  decoder rejections + unknown record types + framing failures
 *   by_type      object  { "<tt>": count } where <tt> is the record type as two lowercase hex
 *                        digits ("02" = FIX_KEY, "7f" = END); only non-zero types appear
 *   hdr          object  { session_id, mode, variant, venue_id, layout_id, fw, hwid, gps_hz,
 *                          fused_hz, start_gps_us }, or null when the log has no SESSION_HDR
 *   venue        object  { id, layout_id, name }, or null when the log has no VENUE
 *   fix          object  { n, valid, first_gps_us, last_gps_us, max_gspeed_mms }
 *   fused        number  FUSED records decoded
 *   laps         array   the stored laps (the first 64 LAP records), each
 *                        { lap_no, start_gps_us, time_ms, flags, n_sectors, sector_ms: [...] }
 *   n_laps       number  total LAP records, which may exceed the length of "laps"
 *   sectors      number  SECTOR records
 *   drag_runs    number  DRAG_RUN records
 *   drag_gates   number  DRAG_GATE records
 *   events       number  EVENT records
 *   time_maps    number  TIME_MAP records
 *   end          object  { gps_us, reason }, or null when the log has no END
 */

/* One summary object: 64 laps x 9 sector splits plus the scalar fields is well under 16 KB; 64 KB
 * leaves room for the session 2.7 additions without a second pass. */
#define REPLAY_JSON_CAP 65536
/* Capacity of replay_summary_t.laps, taken from the struct so the two can never disagree. */
#define REPLAY_LAPS_CAP ((uint16_t)(sizeof(((replay_summary_t *)0)->laps) / sizeof(lap_result_t)))

/* mm/s -> km/h: x 3600 s/h / 1e6 mm/km. */
static const double MMS_TO_KMH = 0.0036;

static void on_hdr(const ses_hdr_t *h, void *ctx)
{
    replay_summary_t *s = ctx; s->hdr = *h; s->have_hdr = 1;
}

static void on_venue(const ses_venue_t *v, void *ctx)
{
    replay_summary_t *s = ctx; s->venue = *v; s->have_venue = 1;
}

static void on_time_map(const ses_time_map_t *t, void *ctx)
{
    replay_summary_t *s = ctx; (void)t; s->n_time_map++;
}

static void on_fix(const gps_fix_t *f, void *ctx)
{
    replay_summary_t *s = ctx;
    if (s->n_fix == 0) s->first_fix_gps_us = f->gps_us;
    s->last_fix_gps_us = f->gps_us;
    s->n_fix++;
    if (f->valid) s->n_fix_valid++;
    /* max over a set that starts at 0: a log of only negative Doppler speeds reports 0. */
    if (f->gspeed_mms > s->max_gspeed_mms) s->max_gspeed_mms = f->gspeed_mms;
}

static void on_fused(const fused_sample_t *fs, void *ctx)
{
    replay_summary_t *s = ctx; (void)fs; s->n_fused++;
}

static void on_lap(const lap_result_t *l, void *ctx)
{
    replay_summary_t *s = ctx;
    if (s->n_laps_listed < REPLAY_LAPS_CAP) s->laps[s->n_laps_listed++] = *l;
    s->n_lap++;
}

static void on_sector(const ses_sector_t *sec, void *ctx)
{
    replay_summary_t *s = ctx; (void)sec; s->n_sector++;
}

static void on_drag_run(const drag_result_t *run, void *ctx)
{
    replay_summary_t *s = ctx; (void)run; s->n_drag_run++;
}

static void on_drag_gate(const ses_drag_gate_t *g, void *ctx)
{
    replay_summary_t *s = ctx; (void)g; s->n_drag_gate++;
}

static void on_event(const ses_event_t *e, void *ctx)
{
    replay_summary_t *s = ctx; (void)e; s->n_event++;
}

static void on_end(const ses_end_t *e, void *ctx)
{
    replay_summary_t *s = ctx; s->end = *e; s->have_end = 1;
}

int replay_summarize_file(const char *path, replay_summary_t *out)
{
    static const logr_cb_t cb = {
        .on_hdr = on_hdr, .on_venue = on_venue, .on_time_map = on_time_map, .on_fix = on_fix,
        .on_fused = on_fused, .on_lap = on_lap, .on_sector = on_sector, .on_drag_run = on_drag_run,
        .on_drag_gate = on_drag_gate, .on_event = on_event, .on_calib = NULL, .on_mark = NULL,
        .on_power = NULL, .on_end = on_end, .on_bad = NULL,
    };
    memset(out, 0, sizeof *out);
    logr_t r;
    logr_init(&r, &cb, out);
    if (logr_read_file(&r, path) != 0) return -1;
    out->n_frames = r.n_frames;
    /* Both kinds of loss are reported together: a decoder rejection (framed but malformed) and a
     * framing failure (bad CRC, impossible length, truncated tail at EOF). */
    out->n_bad = r.n_bad + r.rd.frames_bad;
    memcpy(out->n_by_type, r.n_by_type, sizeof out->n_by_type);
    return 0;
}

void replay_print_text(const replay_summary_t *s, FILE *f)
{
    fprintf(f, "frames %u (bad %u)\n", (unsigned)s->n_frames, (unsigned)s->n_bad);
    if (s->have_hdr)
        fprintf(f, "hdr %s mode %u venue %u/%u gps %u Hz fused %u Hz\n",
                s->hdr.session_id, (unsigned)s->hdr.mode, (unsigned)s->hdr.venue_id,
                (unsigned)s->hdr.layout_id, (unsigned)s->hdr.gps_hz, (unsigned)s->hdr.fused_hz);
    if (s->have_venue)
        fprintf(f, "venue %u/%u %s\n", (unsigned)s->venue.venue_id, (unsigned)s->venue.layout_id, s->venue.name);
    if (s->n_time_map) fprintf(f, "time_map %u\n", (unsigned)s->n_time_map);
    if (s->n_fix)
        fprintf(f, "fix %u valid %u span %.3f s max %.1f km/h\n", (unsigned)s->n_fix, (unsigned)s->n_fix_valid,
                (double)(s->last_fix_gps_us - s->first_fix_gps_us) / 1e6, (double)s->max_gspeed_mms * MMS_TO_KMH);
    if (s->n_fused) fprintf(f, "fused %u\n", (unsigned)s->n_fused);
    for (uint16_t i = 0; i < s->n_laps_listed; i++) {
        const lap_result_t *l = &s->laps[i];
        fprintf(f, "lap %u %.3f s flags 0x%02X", (unsigned)l->lap_no, (double)l->time_ms / 1000.0, (unsigned)l->flags);
        if (l->n_sectors) {
            fputs(" sectors", f);
            for (uint8_t k = 0; k < l->n_sectors; k++) fprintf(f, " %.1f", (double)l->sector_ms[k] / 1000.0);
        }
        fputc('\n', f);
    }
    if (s->n_lap > s->n_laps_listed) fprintf(f, "laps %u (%u listed)\n", (unsigned)s->n_lap, (unsigned)s->n_laps_listed);
    if (s->n_sector) fprintf(f, "sector %u\n", (unsigned)s->n_sector);
    if (s->n_drag_run) fprintf(f, "drag_run %u\n", (unsigned)s->n_drag_run);
    if (s->n_drag_gate) fprintf(f, "drag_gate %u\n", (unsigned)s->n_drag_gate);
    if (s->n_event) fprintf(f, "event %u\n", (unsigned)s->n_event);
    if (s->have_end) fprintf(f, "end reason %u at %lld\n", (unsigned)s->end.reason, (long long)s->end.gps_us);
}

void replay_print_json(const replay_summary_t *s, FILE *f)
{
    static const char hexdig[] = "0123456789abcdef";
    char *buf = malloc(REPLAY_JSON_CAP);
    if (!buf) { fputs("{\"error\":\"overflow\"}\n", f); return; }
    jw_t w;
    jw_init(&w, buf, REPLAY_JSON_CAP);
    jw_obj_open(&w);

    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "frames"); jw_uint(&w, s->n_frames);
    jw_key(&w, "bad_frames"); jw_uint(&w, s->n_bad);

    jw_key(&w, "by_type"); jw_obj_open(&w);
    for (unsigned t = 0; t < sizeof s->n_by_type / sizeof s->n_by_type[0]; t++) {
        if (s->n_by_type[t] == 0) continue;
        char key[3] = { hexdig[(t >> 4) & 0xFu], hexdig[t & 0xFu], '\0' };
        jw_key(&w, key); jw_uint(&w, s->n_by_type[t]);
    }
    jw_obj_close(&w);

    jw_key(&w, "hdr");
    if (s->have_hdr) {
        jw_obj_open(&w);
        jw_key(&w, "session_id"); jw_str(&w, s->hdr.session_id);
        jw_key(&w, "mode"); jw_uint(&w, s->hdr.mode);
        jw_key(&w, "variant"); jw_uint(&w, s->hdr.variant);
        jw_key(&w, "venue_id"); jw_uint(&w, s->hdr.venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, s->hdr.layout_id);
        jw_key(&w, "fw"); jw_str(&w, s->hdr.fw);
        jw_key(&w, "hwid"); jw_str(&w, s->hdr.hwid);
        jw_key(&w, "gps_hz"); jw_uint(&w, s->hdr.gps_hz);
        jw_key(&w, "fused_hz"); jw_uint(&w, s->hdr.fused_hz);
        jw_key(&w, "start_gps_us"); jw_int(&w, s->hdr.start_gps_us);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_key(&w, "venue");
    if (s->have_venue) {
        jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, s->venue.venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, s->venue.layout_id);
        jw_key(&w, "name"); jw_str(&w, s->venue.name);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_key(&w, "fix"); jw_obj_open(&w);
    jw_key(&w, "n"); jw_uint(&w, s->n_fix);
    jw_key(&w, "valid"); jw_uint(&w, s->n_fix_valid);
    jw_key(&w, "first_gps_us"); jw_int(&w, s->first_fix_gps_us);
    jw_key(&w, "last_gps_us"); jw_int(&w, s->last_fix_gps_us);
    jw_key(&w, "max_gspeed_mms"); jw_int(&w, s->max_gspeed_mms);
    jw_obj_close(&w);

    jw_key(&w, "fused"); jw_uint(&w, s->n_fused);

    jw_key(&w, "laps"); jw_arr_open(&w);
    for (uint16_t i = 0; i < s->n_laps_listed; i++) {
        const lap_result_t *l = &s->laps[i];
        jw_obj_open(&w);
        jw_key(&w, "lap_no"); jw_uint(&w, l->lap_no);
        jw_key(&w, "start_gps_us"); jw_int(&w, l->start_gps_us);
        jw_key(&w, "time_ms"); jw_uint(&w, l->time_ms);
        jw_key(&w, "flags"); jw_uint(&w, l->flags);
        jw_key(&w, "n_sectors"); jw_uint(&w, l->n_sectors);
        jw_key(&w, "sector_ms"); jw_arr_open(&w);
        for (uint8_t k = 0; k < l->n_sectors; k++) jw_uint(&w, l->sector_ms[k]);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_key(&w, "n_laps"); jw_uint(&w, s->n_lap);

    jw_key(&w, "sectors"); jw_uint(&w, s->n_sector);
    jw_key(&w, "drag_runs"); jw_uint(&w, s->n_drag_run);
    jw_key(&w, "drag_gates"); jw_uint(&w, s->n_drag_gate);
    jw_key(&w, "events"); jw_uint(&w, s->n_event);
    jw_key(&w, "time_maps"); jw_uint(&w, s->n_time_map);

    jw_key(&w, "end");
    if (s->have_end) {
        jw_obj_open(&w);
        jw_key(&w, "gps_us"); jw_int(&w, s->end.gps_us);
        jw_key(&w, "reason"); jw_uint(&w, s->end.reason);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_obj_close(&w);
    if (jw_overflow(&w)) fputs("{\"error\":\"overflow\"}\n", f);
    else { fputs(buf, f); fputc('\n', f); }
    free(buf);
}
```

`tools/replay/replay_main.c`:

```c
#include "replay/replay.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* replay <file.log> [--json] — prints a summary of the decoded records (spec §22.2). All logic
 * lives in replaylib; session 2.7 adds the engine run behind the same CLI. */

static int usage(const char *argv0)
{
    fprintf(stderr, "usage: %s <file.log> [--json]\n", argv0);
    return 2;
}

int main(int argc, char **argv)
{
    const char *self = (argc > 0 && argv[0]) ? argv[0] : "replay";
    const char *path = NULL;
    int json = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            if (json) return usage(self);
            json = 1;
        } else if (argv[i][0] == '-' || path) {
            return usage(self);
        } else {
            path = argv[i];
        }
    }
    if (!path) return usage(self);

    replay_summary_t s;
    errno = 0;
    if (replay_summarize_file(path, &s) != 0) {
        fprintf(stderr, "%s: %s: %s\n", self, path, strerror(errno));
        return 1;
    }
    if (json) replay_print_json(&s, stdout);
    else replay_print_text(&s, stdout);
    return 0;
}
```

- [ ] **Step 9: Run to verify it passes**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1 && cmake --build test/build --parallel 2>&1 | grep -E "error|warning" ; ctest --test-dir test/build --output-on-failure 2>&1 | tail -4`

(the re-configure is what makes CMake notice `replay_main.c` and add the `replay` executable.)

Expected: no compiler output, then

```
100% tests passed out of 15
```

(12 core suites + `test_replay_smoke` + `test_logio` + `test_replay_summary`; Tasks 2 and 3 add
their own suites on their own branches.)

Then exercise the CLI on a log written by the new writer:

Run: `./test/build/tools/replay/replay` and `./test/build/tools/replay/replay /tmp/nope.log`
Expected:

```
usage: ./test/build/tools/replay/replay <file.log> [--json]
```
with exit status 2, and
```
./test/build/tools/replay/replay: /tmp/nope.log: No such file or directory
```
with exit status 1.

On a real log (the `synth` output of Task 5, or any `.log` the writer produced) the text form is one
line per item, e.g.

```
frames 1807 (bad 0)
hdr S00001_001 mode 0 venue 1000/1 gps 5 Hz fused 10 Hz
venue 1000/1 Synthetic
time_map 1
fix 600 valid 600 span 119.800 s max 180.4 km/h
fused 1200
lap 1 91.234 s flags 0x40 sectors 30.1 30.5 30.6
end reason 0 at 1789381020000000
```

and `--json` prints one object on one line:

```
{"version":"0.0.1","frames":1807,"bad_frames":0,"by_type":{"01":1,"02":24,"03":576,"04":1200,"05":3,"0c":1,"0d":1,"7f":1},"hdr":{"session_id":"S00001_001","mode":0,"variant":0,"venue_id":1000,"layout_id":1,"fw":"v0.2.0","hwid":"moto","gps_hz":5,"fused_hz":10,"start_gps_us":1789380900000000},"venue":{"id":1000,"layout_id":1,"name":"Synthetic"},"fix":{"n":600,"valid":600,"first_gps_us":1789380900000000,"last_gps_us":1789381019800000,"max_gspeed_mms":50110},"fused":1200,"laps":[...],"n_laps":3,"sectors":0,"drag_runs":0,"drag_gates":0,"events":0,"time_maps":1,"end":{"gps_us":1789381020000000,"reason":0}}
```

- [ ] **Step 10: gcc parity and hygiene**

Run: `cmake -S test -B /tmp/lt-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-16 >/dev/null && cmake --build /tmp/lt-gcc --parallel 2>&1 | grep -E "error|warning" ; ctest --test-dir /tmp/lt-gcc 2>&1 | tail -3`
Expected: no compiler output, `100% tests passed out of 15`.

Run: `git diff --check`
Expected: no output.

- [ ] **Step 11: Commit**

```bash
git add tools/replay/lib/replay_summary.c tools/replay/replay_main.c tools/replay/test/test_replay_summary.c
git commit -m "feat(tools): replay summary, JSON/text rendering and the replay CLI skeleton

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

---

### Task 5: `synth` CLI, truth and venue writers, end-to-end test, ctest smoke (serial)

**Header change needed:** none. The four Task 1 headers are used exactly as written.

> Compile-check note for the implementer: this draft was built and run in a scratch worktree in
> which Tasks 2–4 did not exist yet, against stand-ins (a single-circle `synth_track`, a real
> `synth_gps`, a `logio` over the `ses_*` codecs, and a `replay_summary`/`replay_main` that print
> `"bad_frames":0`). Every file below compiled warning-free under Apple clang and gcc-16 with the
> strict flag set, and `test_synth_e2e` passed all 8 tests under ASan/UBSan. The end-to-end test's
> numeric assertions are written against the **real** Tasks 2–4 and are only meaningful once they
> are merged; run them for the first time at Step 6.

**Files:**
- Create: `tools/replay/include/replay/synth_truth.h`, `tools/replay/lib/synth_truth.c`, `tools/replay/synth_main.c`, `tools/replay/test/test_synth_e2e.c`, `tools/replay/README.md`
- Modify: `tools/replay/CMakeLists.txt` (append the two ctest smoke registrations), `docs/superpowers/plans/2026-09-14-plan-02-core-engines.md` (the "File structure" block and Task 1's `tools/replay/CMakeLists.txt` block), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§4.2 header list)

**Interfaces:**
- Consumes (Task 1 headers, unchanged): `synth_cfg_t`, `synth_gps_cfg_t`, `synth_run_t`, `synth_gate_t`, `synth_cfg_defaults`, `synth_gps_cfg_defaults`, `synth_run_build`, `synth_run_time_at_s`, `synth_run_sf_s_total`, `synth_run_lap_crossings`, `synth_run_lap_time`, `synth_run_venue`, `synth_gps_init`, `synth_gps_next`, `synth_fused_at`, `logw_t`, `logw_open_file`, `logw_open_mem`, `logw_hdr`, `logw_venue`, `logw_time_map`, `logw_fix`, `logw_fused`, `logw_end`, `logw_close`, `logr_t`, `logr_cb_t`, `logr_init`, `logr_feed`, `logr_finish`, `replay_version`. From core: `jw_t` (`core/jw.h`), `trk_to_json`/`trk_from_json`/`trk_validate_venue` (`core/trk.h`), `tb_gps_us_from_utc` (`core/tb.h`), `geo_to_enu`/`geo_segment_cross` (`core/geo.h`), `ses_hdr_t` and the `ses_*` codecs (`core/ses.h`), `json_parse`/`json_obj_get`/`json_tok_double` (`core/json.h`).
- Produces (`tools/replay/include/replay/synth_truth.h`): `SYNTH_LAYOUT_ID`, `SYNTH_FUSED_HZ`, `SYNTH_ARG_QUIET`, `SYNTH_ARG_HELP`; `int synth_truth_write(FILE *f, const synth_run_t *r, const synth_gps_cfg_t *g, uint32_t fixes_written, uint32_t fused_written)`; `int synth_venue_write(FILE *f, const synth_run_t *r)`; `int synth_generate(const synth_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz, logw_t *w, synth_run_t *run, uint32_t *n_fix, uint32_t *n_fused)`; `int synth_parse_args(int argc, char **argv, synth_cfg_t *cfg, synth_gps_cfg_t *gps, int *fused_hz, char *out, size_t out_cap)`. Plus the `synth` executable and the `synth_smoke` / `replay_smoke` ctest entries.

- [ ] **Step 1: Write the failing end-to-end test**

`tools/replay/test/test_synth_e2e.c`:

```c
#include "unity.h"
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/synth_truth.h"
#include "replay/logio.h"
#include "replay/replay.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/json.h"
#include "core/tb.h"
#include "core/trk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_CAP        (4u * 1024u * 1024u)   /* 266 s at 5 Hz plus 10 Hz FUSED is about 30 kB */
#define CAP_MAX_FIX    20000
#define TEST_LAPS      3
#define FIX_US_5HZ     200000                 /* 1e6 / 5 Hz */
#define DROP_START_S   30.0
#define DROP_END_S     40.0
/* FIX_DELTA quantisation (spec 12.3): alt 1 dm, speed 1 cm/s, heading 1e-2 deg, hacc 1 dm. The
 * decoded value is the truth rounded to that step, so it differs by at most one whole step. */
#define Q_ALT_MM       100
#define Q_SPEED_MMS    10
#define Q_HEAD_E5      1000
#define Q_HACC_MM      100
/* Crossing tolerance for straight-line interpolation between raw fixes: one sample interval plus
 * the 1.5 m position noise at the ~30 m/s the default circuit runs. The lap engine's
 * constant-acceleration interpolation (session 2.4) must do far better - spec 22.2 asks for 30 ms
 * at 95 % at 5 Hz - so this only proves the fixture itself is self-consistent. */
#define SF_TOL_US_5HZ  250000
#define SF_TOL_US_10HZ 150000
/* Truth times are written with 6 decimals, so one value carries at most 5e-7 of rounding. */
#define TRUTH_TOL_S    1e-6
/* Summing 3 sector times and comparing with the lap time accumulates 4 such roundings. */
#define SUM_TOL_S      2e-6
#define JSON_MAX_TOKS  4096

typedef struct {
    gps_fix_t   fix[CAP_MAX_FIX];
    uint32_t    n_fix, n_fused, n_time_map;
    ses_hdr_t   hdr;   int have_hdr;
    ses_venue_t venue; int have_venue;
    ses_end_t   end;   int have_end;
} cap_t;

static uint8_t     *g_buf1, *g_buf2;
static synth_run_t *g_run;
static cap_t       *g_cap;

void setUp(void)
{
    g_buf1 = (uint8_t *)malloc(LOG_CAP);
    g_buf2 = (uint8_t *)malloc(LOG_CAP);
    g_run  = (synth_run_t *)malloc(sizeof *g_run);
    g_cap  = (cap_t *)malloc(sizeof *g_cap);
    TEST_ASSERT_NOT_NULL(g_buf1); TEST_ASSERT_NOT_NULL(g_buf2);
    TEST_ASSERT_NOT_NULL(g_run);  TEST_ASSERT_NOT_NULL(g_cap);
}

void tearDown(void)
{
    free(g_buf1); free(g_buf2); free(g_run); free(g_cap);
    g_buf1 = NULL; g_buf2 = NULL; g_run = NULL; g_cap = NULL;
}

/* ---------------- helpers ---------------- */

static void cb_hdr(const ses_hdr_t *h, void *ctx)        { cap_t *c = (cap_t *)ctx; c->hdr = *h; c->have_hdr = 1; }
static void cb_venue(const ses_venue_t *v, void *ctx)    { cap_t *c = (cap_t *)ctx; c->venue = *v; c->have_venue = 1; }
static void cb_time_map(const ses_time_map_t *t, void *ctx) { (void)t; ((cap_t *)ctx)->n_time_map++; }
static void cb_fused(const fused_sample_t *f, void *ctx) { (void)f; ((cap_t *)ctx)->n_fused++; }
static void cb_end(const ses_end_t *e, void *ctx)        { cap_t *c = (cap_t *)ctx; c->end = *e; c->have_end = 1; }
static void cb_fix(const gps_fix_t *f, void *ctx)
{
    cap_t *c = (cap_t *)ctx;
    if (c->n_fix < CAP_MAX_FIX) c->fix[c->n_fix] = *f;
    c->n_fix++;
}

static const logr_cb_t g_cb = {
    cb_hdr, cb_venue, cb_time_map, cb_fix, cb_fused,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, cb_end, NULL
};

/* Generates one whole session into buf and returns the configuration it used. */
static size_t gen(uint8_t *buf, int laps, int rate, int fused_hz, double drop0, double drop1,
                  synth_cfg_t *cfg, synth_gps_cfg_t *gps, uint32_t *n_fix, uint32_t *n_fused)
{
    synth_cfg_defaults(cfg);
    cfg->laps = laps;
    synth_gps_cfg_defaults(gps);
    gps->rate_hz = rate;
    gps->dropout_start_s = drop0;
    gps->dropout_end_s = drop1;
    logw_t w;
    logw_open_mem(&w, buf, LOG_CAP);
    TEST_ASSERT_EQUAL_INT(0, synth_generate(cfg, gps, fused_hz, &w, g_run, n_fix, n_fused));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    return w.len;
}

static void read_back(const uint8_t *buf, size_t len, logr_t *r)
{
    memset(g_cap, 0, sizeof *g_cap);
    logr_init(r, &g_cb, g_cap);
    logr_feed(r, buf, len);
    logr_finish(r);
    TEST_ASSERT_EQUAL_UINT32(0, r->n_bad);
    TEST_ASSERT_TRUE(g_cap->n_fix <= CAP_MAX_FIX);
}

/* S/F crossings recovered from the decoded fixes alone: consecutive fixes to ENU about the venue
 * origin, geo_segment_cross against the S/F line, linear interpolation of gps_us. */
static int sf_crossings_us(const synth_run_t *r, double *out, int out_cap)
{
    geo_origin_t o;
    geo_origin_set(&o, r->cfg.origin_lat_deg, r->cfg.origin_lon_deg);
    geo_enu_t p = geo_to_enu(&o, r->gates[0].line.p1.lat, r->gates[0].line.p1.lon);
    geo_enu_t q = geo_to_enu(&o, r->gates[0].line.p2.lat, r->gates[0].line.p2.lon);
    int n = 0;
    for (uint32_t i = 1; i < g_cap->n_fix; i++) {
        const gps_fix_t *fa = &g_cap->fix[i - 1], *fb = &g_cap->fix[i];
        geo_enu_t a = geo_to_enu(&o, (double)fa->lat_e7 / 1e7, (double)fa->lon_e7 / 1e7);
        geo_enu_t b = geo_to_enu(&o, (double)fb->lat_e7 / 1e7, (double)fb->lon_e7 / 1e7);
        double t = 0.0;
        int dir = 0;
        if (!geo_segment_cross(a, b, p, q, &t, &dir)) continue;
        TEST_ASSERT_EQUAL_INT(1, dir);                  /* p1 is the left end, so dir_sign is +1 */
        TEST_ASSERT_TRUE(n < out_cap);
        out[n++] = (double)fa->gps_us + t * (double)(fb->gps_us - fa->gps_us);
    }
    return n;
}

static void check_sf_crossings(int rate, int64_t tol_us)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t n_fix = 0, n_fused = 0;
    size_t len = gen(g_buf1, TEST_LAPS, rate, 10, 0.0, 0.0, &cfg, &gps, &n_fix, &n_fused);
    logr_t r;
    read_back(g_buf1, len, &r);

    double got[TEST_LAPS + 2];
    int n = sf_crossings_us(g_run, got, (int)(sizeof got / sizeof got[0]));
    TEST_ASSERT_EQUAL_INT(TEST_LAPS + 1, n);
    for (int i = 0; i < n; i++) {
        double truth_s = synth_run_time_at_s(g_run, synth_run_sf_s_total(g_run, i));
        double want = (double)gps.t0_gps_us + truth_s * 1e6;
        TEST_ASSERT_DOUBLE_WITHIN((double)tol_us, want, got[i]);
    }
}

/* Reads a whole tmpfile back into a NUL-terminated heap buffer. */
static char *slurp(FILE *f, size_t *len_out)
{
    TEST_ASSERT_EQUAL_INT(0, fflush(f));
    long n = ftell(f);
    TEST_ASSERT_TRUE(n > 0);
    rewind(f);
    char *b = (char *)malloc((size_t)n + 1);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_size_t((size_t)n, fread(b, 1, (size_t)n, f));
    b[n] = '\0';
    *len_out = (size_t)n;
    return b;
}

/* ---------------- tests ---------------- */

static void test_generate_is_deterministic_and_reads_back(void)
{
    synth_cfg_t cfg, cfg2;
    synth_gps_cfg_t gps, gps2;
    uint32_t nf1 = 0, nu1 = 0, nf2 = 0, nu2 = 0;
    size_t len1 = gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &nf1, &nu1);
    size_t len2 = gen(g_buf2, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg2, &gps2, &nf2, &nu2);
    TEST_ASSERT_EQUAL_size_t(len1, len2);
    TEST_ASSERT_EQUAL_UINT32(nf1, nf2);
    TEST_ASSERT_EQUAL_UINT32(nu1, nu2);
    TEST_ASSERT_EQUAL_INT(0, memcmp(g_buf1, g_buf2, len1));      /* same seeds, same bytes */

    logr_t r;
    read_back(g_buf1, len1, &r);

    char fw[17];
    snprintf(fw, sizeof fw, "%s", replay_version());             /* the wire field is 16 bytes */
    TEST_ASSERT_TRUE(g_cap->have_hdr);
    TEST_ASSERT_EQUAL_STRING("S00001_001", g_cap->hdr.session_id);
    TEST_ASSERT_EQUAL_STRING(fw, g_cap->hdr.fw);
    TEST_ASSERT_EQUAL_STRING("synth", g_cap->hdr.hwid);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.mode);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.variant);
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, g_cap->hdr.venue_id);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, g_cap->hdr.layout_id);
    TEST_ASSERT_EQUAL_UINT8(0, g_cap->hdr.log_profile);
    TEST_ASSERT_EQUAL_UINT8(10, g_cap->hdr.fused_hz);
    TEST_ASSERT_EQUAL_UINT8(5, g_cap->hdr.gps_hz);
    TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us, g_cap->hdr.start_gps_us);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[0]);          /* identity x 1e4 */
    TEST_ASSERT_EQUAL_INT16(0, g_cap->hdr.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[4]);
    TEST_ASSERT_EQUAL_INT16(10000, g_cap->hdr.r_e4[8]);
    TEST_ASSERT_EQUAL_UINT8(0x03, g_cap->hdr.calib_flags);

    TEST_ASSERT_TRUE(g_cap->have_venue);
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, g_cap->venue.venue_id);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, g_cap->venue.layout_id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", g_cap->venue.name);

    TEST_ASSERT_EQUAL_UINT32(nf1, g_cap->n_fix);
    TEST_ASSERT_EQUAL_UINT32(nu1, g_cap->n_fused);
    for (uint32_t i = 0; i < g_cap->n_fix; i++)                  /* no dropout: k = 0, 1, 2, ... */
        TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us + (int64_t)i * FIX_US_5HZ, g_cap->fix[i].gps_us);

    /* The decoded stream must agree with a fresh sampler on the same run and seeds, to within the
     * FIX_DELTA quantisation. */
    synth_gps_t s;
    synth_gps_init(&s, &gps);
    uint32_t idx = 0;
    for (;;) {
        gps_fix_t f;
        int rc = synth_gps_next(&s, g_run, &f, NULL);
        if (rc < 0) break;
        if (rc == 0) continue;
        TEST_ASSERT_TRUE(idx < g_cap->n_fix);
        const gps_fix_t *d = &g_cap->fix[idx];
        TEST_ASSERT_EQUAL_INT64(f.gps_us, d->gps_us);
        TEST_ASSERT_EQUAL_INT32(f.lat_e7, d->lat_e7);
        TEST_ASSERT_EQUAL_INT32(f.lon_e7, d->lon_e7);
        TEST_ASSERT_INT32_WITHIN(Q_ALT_MM, f.alt_mm, d->alt_mm);
        TEST_ASSERT_INT32_WITHIN(Q_SPEED_MMS, f.gspeed_mms, d->gspeed_mms);
        TEST_ASSERT_INT32_WITHIN(Q_HEAD_E5, f.head_e5, d->head_e5);
        TEST_ASSERT_INT32_WITHIN(Q_HACC_MM, (int32_t)f.hacc_mm, (int32_t)d->hacc_mm);
        TEST_ASSERT_EQUAL_UINT8(1, d->valid);
        idx++;
    }
    TEST_ASSERT_EQUAL_UINT32(g_cap->n_fix, idx);

    /* TIME_MAP at run start and every 60 s (spec 12.4). */
    TEST_ASSERT_EQUAL_UINT32(1u + (uint32_t)floor(g_run->duration_s / 60.0), g_cap->n_time_map);

    TEST_ASSERT_TRUE(g_cap->have_end);
    TEST_ASSERT_EQUAL_INT64(gps.t0_gps_us + (int64_t)llround(g_run->duration_s * 1e6), g_cap->end.gps_us);
}

static void test_sf_crossings_at_5hz(void)  { check_sf_crossings(5, SF_TOL_US_5HZ); }
static void test_sf_crossings_at_10hz(void) { check_sf_crossings(10, SF_TOL_US_10HZ); }

/* Records the type of the first FIX_* frame at or after the end of the dropout window. */
typedef struct { ses_fix_state_t st; int64_t hi; uint8_t first_type; int64_t first_us; } scan_t;

static void scan_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    scan_t *s = (scan_t *)ctx;
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return;
    gps_fix_t f;
    if (ses_decode_fix(&s->st, type, payload, len, &f) != 1) return;
    if (s->first_type == 0 && f.gps_us >= s->hi) { s->first_type = type; s->first_us = f.gps_us; }
}

static void test_dropout_window_is_empty_and_resumes_with_a_keyframe(void)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t nf0 = 0, nu0 = 0, nf1 = 0, nu1 = 0;
    size_t len0 = gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &nf0, &nu0);
    (void)len0;
    size_t len1 = gen(g_buf2, TEST_LAPS, 5, 10, DROP_START_S, DROP_END_S, &cfg, &gps, &nf1, &nu1);

    /* 10 s of a 5 Hz stream: exactly 50 slots are skipped. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)((DROP_END_S - DROP_START_S) * 5.0), nf0 - nf1);

    logr_t r;
    read_back(g_buf2, len1, &r);
    TEST_ASSERT_EQUAL_UINT32(nf1, g_cap->n_fix);
    int64_t lo = gps.t0_gps_us + (int64_t)llround(DROP_START_S * 1e6);
    int64_t hi = gps.t0_gps_us + (int64_t)llround(DROP_END_S * 1e6);
    int resumed = 0;
    for (uint32_t i = 0; i < g_cap->n_fix; i++) {
        int64_t t = g_cap->fix[i].gps_us;
        TEST_ASSERT_TRUE(t < lo || t >= hi);
        if (t == hi) resumed = 1;
    }
    TEST_ASSERT_TRUE(resumed);              /* the run continues exactly at the window's end */

    /* The 10 s gap is past FIX_KEYFRAME_S, so the resuming fix must be a keyframe (spec 12.4). */
    scan_t sc;
    memset(&sc, 0, sizeof sc);
    ses_fix_state_init(&sc.st);
    sc.hi = hi;
    ses_reader_t rd;
    ses_reader_init(&rd);
    ses_reader_feed(&rd, g_buf2, len1, scan_cb, &sc);
    ses_reader_flush(&rd, scan_cb, &sc);
    TEST_ASSERT_EQUAL_UINT8(SES_T_FIX_KEY, sc.first_type);
    TEST_ASSERT_EQUAL_INT64(hi, sc.first_us);
}

static void test_truth_json_matches_the_analytic_lap_times(void)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gps;
    uint32_t n_fix = 0, n_fused = 0;
    (void)gen(g_buf1, TEST_LAPS, 5, 10, 0.0, 0.0, &cfg, &gps, &n_fix, &n_fused);

    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, synth_truth_write(f, g_run, &gps, n_fix, n_fused));
    size_t n = 0;
    char *js = slurp(f, &n);
    fclose(f);

    jsmntok_t *toks = (jsmntok_t *)malloc(sizeof(jsmntok_t) * JSON_MAX_TOKS);
    TEST_ASSERT_NOT_NULL(toks);
    int ntoks = json_parse(js, n, toks, JSON_MAX_TOKS);
    TEST_ASSERT_TRUE(ntoks > 0);

    int gates = json_obj_get(js, toks, ntoks, 0, "gates");
    TEST_ASSERT_TRUE(gates > 0);
    TEST_ASSERT_EQUAL_INT(g_run->n_gates, toks[gates].size);
    TEST_ASSERT_EQUAL_INT(3, toks[gates].size);                  /* S/F plus the two default sectors */

    int laps = json_obj_get(js, toks, ntoks, 0, "laps");
    TEST_ASSERT_TRUE(laps > 0);
    TEST_ASSERT_EQUAL_INT(TEST_LAPS, toks[laps].size);

    int e = laps + 1;
    for (int lap = 1; lap <= TEST_LAPS; lap++) {
        int no = json_obj_get(js, toks, ntoks, e, "lap_no");
        int64_t lap_no = 0;
        TEST_ASSERT_TRUE(no > 0 && json_tok_int(js, &toks[no], &lap_no));
        TEST_ASSERT_EQUAL_INT64(lap, lap_no);

        int lt = json_obj_get(js, toks, ntoks, e, "lap_time_s");
        double lap_time = 0.0;
        TEST_ASSERT_TRUE(lt > 0 && json_tok_double(js, &toks[lt], &lap_time));
        TEST_ASSERT_DOUBLE_WITHIN(TRUTH_TOL_S, synth_run_lap_time(g_run, lap), lap_time);

        int st = json_obj_get(js, toks, ntoks, e, "sector_times_s");
        TEST_ASSERT_TRUE(st > 0);
        TEST_ASSERT_EQUAL_INT(g_run->n_gates, toks[st].size);
        double sum = 0.0;
        for (int i = 0; i < toks[st].size; i++) {
            double v = 0.0;
            TEST_ASSERT_TRUE(json_tok_double(js, &toks[st + 1 + i], &v));
            sum += v;
        }
        TEST_ASSERT_DOUBLE_WITHIN(SUM_TOL_S, lap_time, sum);
        e = json_skip(toks, ntoks, e);
    }
    free(toks);
    free(js);
}

static void test_venue_json_loads_and_validates(void)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = TEST_LAPS;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(g_run, &cfg, NULL, 0));

    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, synth_venue_write(f, g_run));
    size_t n = 0;
    char *js = slurp(f, &n);
    fclose(f);

    trk_venue_t v;
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v, js, n, err, sizeof err));
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(&v));
    TEST_ASSERT_EQUAL_UINT16(cfg.venue_id, v.id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", v.name);
    TEST_ASSERT_EQUAL_UINT8(1, v.n_layouts);
    TEST_ASSERT_EQUAL_UINT16(SYNTH_LAYOUT_ID, v.layouts[0].id);
    TEST_ASSERT_EQUAL_INT8(1, v.layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_UINT8(2, v.layouts[0].n_sectors);
    free(js);
}

static void test_parse_args_defaults_and_round_trip(void)
{
    synth_cfg_t c, d;
    synth_gps_cfg_t g, dg;
    int fh = 0;
    char out[64];
    synth_cfg_defaults(&d);
    synth_gps_cfg_defaults(&dg);

    char *a1[] = { "synth", "--out", "x" };
    TEST_ASSERT_EQUAL_INT(0, synth_parse_args(3, a1, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("x", out);
    TEST_ASSERT_EQUAL_INT(SYNTH_FUSED_HZ, fh);
    TEST_ASSERT_EQUAL_INT(d.n_vertices, c.n_vertices);
    TEST_ASSERT_EQUAL_DOUBLE(d.length_m, c.length_m);
    TEST_ASSERT_EQUAL_INT(d.laps, c.laps);
    TEST_ASSERT_TRUE(c.clockwise);
    TEST_ASSERT_EQUAL_INT(dg.rate_hz, g.rate_hz);
    TEST_ASSERT_EQUAL_DOUBLE(dg.pos_sigma_m, g.pos_sigma_m);
    TEST_ASSERT_EQUAL_INT64(dg.t0_gps_us, g.t0_gps_us);

    char *a2[] = { "synth", "--out", "prefix",
                   "--vertices", "8", "--length", "3000", "--radius", "50",
                   "--irregularity", "0.25", "--seed", "7",
                   "--v-corner", "18", "--v-max", "45", "--a-acc", "4", "--a-brk", "7.5",
                   "--lap-var", "0.1", "--laps", "5", "--sectors", "3",
                   "--start-before", "150", "--stop-after", "250", "--sf-frac", "0.25",
                   "--rate", "10", "--pos-sigma", "2.5", "--pos-tau", "30",
                   "--speed-sigma", "0.1", "--head-sigma", "1.25",
                   "--latency", "90", "--jitter", "15", "--fused-hz", "25",
                   "--dropout", "30:40", "--start-utc", "2026-01-02T03:04:05",
                   "--anticlockwise", "--quiet" };
    TEST_ASSERT_EQUAL_INT(SYNTH_ARG_QUIET,
                          synth_parse_args((int)(sizeof a2 / sizeof a2[0]), a2, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("prefix", out);
    TEST_ASSERT_EQUAL_INT(8, c.n_vertices);
    TEST_ASSERT_EQUAL_DOUBLE(3000.0, c.length_m);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, c.corner_radius_m);
    TEST_ASSERT_EQUAL_DOUBLE(0.25, c.irregularity);
    TEST_ASSERT_EQUAL_UINT32(7, c.seed);
    TEST_ASSERT_EQUAL_UINT32(7, g.seed);                      /* one seed drives both models */
    TEST_ASSERT_EQUAL_DOUBLE(18.0, c.v_corner_mps);
    TEST_ASSERT_EQUAL_DOUBLE(45.0, c.v_max_mps);
    TEST_ASSERT_EQUAL_DOUBLE(4.0, c.a_acc_mps2);
    TEST_ASSERT_EQUAL_DOUBLE(7.5, c.a_brk_mps2);
    TEST_ASSERT_EQUAL_DOUBLE(0.1, c.lap_var);
    TEST_ASSERT_EQUAL_INT(5, c.laps);
    TEST_ASSERT_EQUAL_INT(3, c.n_sector_gates);
    TEST_ASSERT_EQUAL_DOUBLE(150.0, c.start_before_m);
    TEST_ASSERT_EQUAL_DOUBLE(250.0, c.stop_after_m);
    TEST_ASSERT_EQUAL_DOUBLE(0.25, c.sf_frac);
    TEST_ASSERT_FALSE(c.clockwise);
    TEST_ASSERT_EQUAL_INT(10, g.rate_hz);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, g.pos_sigma_m);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, g.pos_tau_s);
    TEST_ASSERT_EQUAL_DOUBLE(0.1, g.speed_sigma_mps);
    TEST_ASSERT_EQUAL_DOUBLE(1.25, g.head_sigma_deg);
    TEST_ASSERT_EQUAL_DOUBLE(90.0, g.latency_ms);
    TEST_ASSERT_EQUAL_DOUBLE(15.0, g.jitter_ms);
    TEST_ASSERT_EQUAL_INT(25, fh);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, g.dropout_start_s);
    TEST_ASSERT_EQUAL_DOUBLE(40.0, g.dropout_end_s);
    TEST_ASSERT_EQUAL_INT64(tb_gps_us_from_utc(2026, 1, 2, 3, 4, 5, 0), g.t0_gps_us);

    char *help[] = { "synth", "--help" };
    TEST_ASSERT_EQUAL_INT(SYNTH_ARG_HELP, synth_parse_args(2, help, &c, &g, &fh, out, sizeof out));
}

static void test_parse_args_rejects_bad_input(void)
{
    synth_cfg_t c;
    synth_gps_cfg_t g;
    int fh = 0;
    char out[64];
    char *no_out[]   = { "synth", "--laps", "3" };
    char *bad_laps[] = { "synth", "--out", "p", "--laps", "0" };
    char *bad_rate[] = { "synth", "--out", "p", "--rate", "7" };
    char *bad_num[]  = { "synth", "--out", "p", "--length", "abc" };
    char *trail[]    = { "synth", "--out", "p", "--length", "3000x" };
    char *unknown[]  = { "synth", "--out", "p", "--bogus", "1" };
    char *no_value[] = { "synth", "--out", "p", "--laps" };
    char *bad_utc[]  = { "synth", "--out", "p", "--start-utc", "2026-1-2T3:4:5" };
    char *bad_drop[] = { "synth", "--out", "p", "--dropout", "40:30" };
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(3, no_out,   &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_laps, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_rate, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_num,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, trail,    &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, unknown,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(4, no_value, &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_utc,  &c, &g, &fh, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, synth_parse_args(5, bad_drop, &c, &g, &fh, out, sizeof out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generate_is_deterministic_and_reads_back);
    RUN_TEST(test_sf_crossings_at_5hz);
    RUN_TEST(test_sf_crossings_at_10hz);
    RUN_TEST(test_dropout_window_is_empty_and_resumes_with_a_keyframe);
    RUN_TEST(test_truth_json_matches_the_analytic_lap_times);
    RUN_TEST(test_venue_json_loads_and_validates);
    RUN_TEST(test_parse_args_defaults_and_round_trip);
    RUN_TEST(test_parse_args_rejects_bad_input);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel 2>&1 | tail -5`
Expected: FAIL — `fatal error: 'replay/synth_truth.h' file not found` while compiling
`tools/replay/test/test_synth_e2e.c`.

- [ ] **Step 3: Write the library header**

`tools/replay/include/replay/synth_truth.h`:

```c
#ifndef REPLAY_SYNTH_TRUTH_H
#define REPLAY_SYNTH_TRUTH_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/logio.h"

/* The `synth` tool's library half (spec §22.2): the two side-car writers, the record-generation
 * loop and the command-line parser. Split out of synth_main.c so the end-to-end test drives exactly
 * the code the executable runs. Host only, no global state. */

#define SYNTH_LAYOUT_ID 1        /* the one synthetic layout: header, VENUE record and venue JSON */
#define SYNTH_FUSED_HZ  10       /* default --fused-hz (§12.6 internal profile) */

/* Non-negative return of synth_parse_args: a bit set, 0 when neither flag was given. */
#define SYNTH_ARG_QUIET 0x01
#define SYNTH_ARG_HELP  0x02

/* `<prefix>.truth.json`: generator, configuration, geometry and every analytic crossing time.
 * Returns 0, or -1 on a write error or if the document exceeds the 1 MiB build buffer. */
int synth_truth_write(FILE *f, const synth_run_t *r, const synth_gps_cfg_t *g, uint32_t fixes_written, uint32_t fused_written);
/* `<prefix>.venue.json`: synth_run_venue through trk_to_json (§10.2). 0 / -1. */
int synth_venue_write(FILE *f, const synth_run_t *r);
/* Builds `run` from cfg and writes one whole session into `w` (SESSION_HDR, VENUE, TIME_MAP every
 * 60 s, the time-ordered merge of the FIX_* and FUSED streams, END). fused_hz 0 disables FUSED.
 * Returns 0 and the record counts, or -1 on an invalid configuration or a write error. */
int synth_generate(const synth_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz, logw_t *w, synth_run_t *run, uint32_t *n_fix, uint32_t *n_fused);
/* Parses `--name value` options into defaults-filled cfg/gps/fused_hz and copies --out into `out`.
 * Returns a SYNTH_ARG_* bit set, or -1 on an unknown option, a bad value or a missing --out. */
int synth_parse_args(int argc, char **argv, synth_cfg_t *cfg, synth_gps_cfg_t *gps, int *fused_hz, char *out, size_t out_cap);
#endif
```

- [ ] **Step 4: Write the library implementation**

`tools/replay/lib/synth_truth.c`:

```c
#include "replay/synth_truth.h"
#include "replay/replay.h"
#include "core/jw.h"
#include "core/trk.h"
#include "core/tb.h"
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Truth JSON is built in one pass into this buffer; 200 laps × 9 gates is about 250 kB. */
#define TRUTH_BUF_BYTES   (1024u * 1024u)
/* trk_to_json of one venue with one layout and 8 sectors is under 2 kB. */
#define VENUE_BUF_BYTES   8192
#define DEC_M             6        /* decimals for metres, seconds and headings */
#define DEC_LL            9        /* decimals for latitude/longitude (≈ 0.1 mm) */
#define TIME_MAP_PERIOD_S 60.0     /* §12.4: TIME_MAP at session start and every 60 s */
/* k / rate_hz is exact for 5 and 10 Hz; the epsilon keeps a sample that lands on a 60 s boundary
 * from missing its TIME_MAP if a future rate makes the division inexact. */
#define TIME_EPS_S        1e-9
#define SYNTH_SESSION_ID  "S00001_001"   /* §12.1 S%05u_%03u; synth is always boot 1, sequence 1 */
#define SYNTH_HWID        "synth"
#define SYNTH_VENUE_NAME  "Synthetic"
#define CALIB_IDENT_E4    10000    /* identity rotation × 1e4 (§12.3 calib block) */
#define CALIB_FLAGS_OK    0x03     /* level and forward axis both "learned": truth needs no calibration */
#define TIME_MAP_QUALITY  1        /* min-filter lock; synth has no PPS (§6.2) */
#define END_REASON_NORMAL 0
#define DROPOUT_MAX_S     1.0e7    /* option bound; the run itself is never this long */

static int64_t gps_us_at(int64_t t0_gps_us, double t_s)
{
    return t0_gps_us + (int64_t)llround(t_s * 1e6);
}

/* ---------------- truth JSON ---------------- */

static void put_cfg(jw_t *w, const synth_cfg_t *c)
{
    jw_obj_open(w);
    jw_key(w, "n_vertices");        jw_int(w, c->n_vertices);
    jw_key(w, "length_m");          jw_double(w, c->length_m, DEC_M);
    jw_key(w, "corner_radius_m");   jw_double(w, c->corner_radius_m, DEC_M);
    jw_key(w, "irregularity");      jw_double(w, c->irregularity, DEC_M);
    jw_key(w, "clockwise");         jw_bool(w, c->clockwise);
    jw_key(w, "seed");              jw_uint(w, c->seed);
    jw_key(w, "v_corner_mps");      jw_double(w, c->v_corner_mps, DEC_M);
    jw_key(w, "v_max_mps");         jw_double(w, c->v_max_mps, DEC_M);
    jw_key(w, "a_acc_mps2");        jw_double(w, c->a_acc_mps2, DEC_M);
    jw_key(w, "a_brk_mps2");        jw_double(w, c->a_brk_mps2, DEC_M);
    jw_key(w, "lap_var");           jw_double(w, c->lap_var, DEC_M);
    jw_key(w, "laps");              jw_int(w, c->laps);
    jw_key(w, "start_before_m");    jw_double(w, c->start_before_m, DEC_M);
    jw_key(w, "stop_after_m");      jw_double(w, c->stop_after_m, DEC_M);
    jw_key(w, "sf_frac");           jw_double(w, c->sf_frac, DEC_M);
    jw_key(w, "n_sector_gates");    jw_int(w, c->n_sector_gates);
    jw_key(w, "gate_half_width_m"); jw_double(w, c->gate_half_width_m, DEC_M);
    jw_key(w, "origin_lat_deg");    jw_double(w, c->origin_lat_deg, DEC_LL);
    jw_key(w, "origin_lon_deg");    jw_double(w, c->origin_lon_deg, DEC_LL);
    jw_key(w, "venue_id");          jw_uint(w, c->venue_id);
    jw_obj_close(w);
}

static void put_gps_cfg(jw_t *w, const synth_gps_cfg_t *g)
{
    jw_obj_open(w);
    jw_key(w, "rate_hz");           jw_int(w, g->rate_hz);
    jw_key(w, "pos_sigma_m");       jw_double(w, g->pos_sigma_m, DEC_M);
    jw_key(w, "pos_tau_s");         jw_double(w, g->pos_tau_s, DEC_M);
    jw_key(w, "speed_sigma_mps");   jw_double(w, g->speed_sigma_mps, DEC_M);
    jw_key(w, "head_sigma_deg");    jw_double(w, g->head_sigma_deg, DEC_M);
    jw_key(w, "latency_ms");        jw_double(w, g->latency_ms, DEC_M);
    jw_key(w, "jitter_ms");         jw_double(w, g->jitter_ms, DEC_M);
    jw_key(w, "hacc_m");            jw_double(w, g->hacc_m, DEC_M);
    jw_key(w, "sats");              jw_uint(w, g->sats);
    jw_key(w, "pdop_e2");           jw_uint(w, g->pdop_e2);
    jw_key(w, "alt_mm");            jw_int(w, g->alt_mm);
    jw_key(w, "t0_gps_us");         jw_int(w, g->t0_gps_us);
    jw_key(w, "t0_mono_us");        jw_int(w, g->t0_mono_us);
    jw_key(w, "dropout_start_s");   jw_double(w, g->dropout_start_s, DEC_M);
    jw_key(w, "dropout_end_s");     jw_double(w, g->dropout_end_s, DEC_M);
    jw_key(w, "seed");              jw_uint(w, g->seed);
    jw_obj_close(w);
}

static void put_gates(jw_t *w, const synth_run_t *r)
{
    jw_arr_open(w);
    for (int i = 0; i < r->n_gates; i++) {
        const synth_gate_t *g = &r->gates[i];
        jw_obj_open(w);
        jw_key(w, "idx");         jw_int(w, i);
        jw_key(w, "kind");        jw_str(w, (i == 0) ? "sf" : "sector");
        jw_key(w, "s_m");         jw_double(w, g->s_m, DEC_M);
        jw_key(w, "e_m");         jw_double(w, g->e_m, DEC_M);
        jw_key(w, "n_m");         jw_double(w, g->n_m, DEC_M);
        jw_key(w, "heading_deg"); jw_double(w, g->heading_deg, DEC_M);
        jw_key(w, "p1"); jw_arr_open(w); jw_double(w, g->line.p1.lat, DEC_LL); jw_double(w, g->line.p1.lon, DEC_LL); jw_arr_close(w);
        jw_key(w, "p2"); jw_arr_open(w); jw_double(w, g->line.p2.lat, DEC_LL); jw_double(w, g->line.p2.lon, DEC_LL); jw_arr_close(w);
        jw_obj_close(w);
    }
    jw_arr_close(w);
}

int synth_truth_write(FILE *f, const synth_run_t *r, const synth_gps_cfg_t *g,
                      uint32_t fixes_written, uint32_t fused_written)
{
    if (!f || !r || !g) return -1;
    char *buf = (char *)malloc(TRUTH_BUF_BYTES);
    if (!buf) return -1;

    jw_t w;
    jw_init(&w, buf, TRUTH_BUF_BYTES);
    jw_obj_open(&w);

    jw_key(&w, "generator");
    jw_obj_open(&w);
    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "seed");    jw_uint(&w, r->cfg.seed);
    jw_obj_close(&w);

    jw_key(&w, "cfg"); put_cfg(&w, &r->cfg);
    jw_key(&w, "gps"); put_gps_cfg(&w, g);

    jw_key(&w, "length_m");   jw_double(&w, r->length_m, DEC_M);
    jw_key(&w, "duration_s"); jw_double(&w, r->duration_s, DEC_M);
    jw_key(&w, "t0_gps_us");  jw_int(&w, g->t0_gps_us);
    jw_key(&w, "t0_mono_us"); jw_int(&w, g->t0_mono_us);
    jw_key(&w, "n_gates");    jw_int(&w, r->n_gates);
    jw_key(&w, "gates");      put_gates(&w, r);

    jw_key(&w, "sf_crossings_s");
    jw_arr_open(&w);
    for (int n = 0; n <= r->cfg.laps; n++) jw_double(&w, synth_run_time_at_s(r, synth_run_sf_s_total(r, n)), DEC_M);
    jw_arr_close(&w);
    jw_key(&w, "sf_crossings_gps_us");
    jw_arr_open(&w);
    for (int n = 0; n <= r->cfg.laps; n++) jw_int(&w, gps_us_at(g->t0_gps_us, synth_run_time_at_s(r, synth_run_sf_s_total(r, n))));
    jw_arr_close(&w);

    jw_key(&w, "laps");
    jw_arr_open(&w);
    for (int lap = 1; lap <= r->cfg.laps; lap++) {
        double c[SYNTH_MAX_GATES + 1];
        int n = synth_run_lap_crossings(r, lap, c, sizeof c / sizeof c[0]);
        if (n < 2) { free(buf); return -1; }
        jw_obj_open(&w);
        jw_key(&w, "lap_no"); jw_int(&w, lap);
        jw_key(&w, "crossings_s");
        jw_arr_open(&w);
        for (int i = 0; i < n; i++) jw_double(&w, c[i], DEC_M);
        jw_arr_close(&w);
        jw_key(&w, "crossings_gps_us");
        jw_arr_open(&w);
        for (int i = 0; i < n; i++) jw_int(&w, gps_us_at(g->t0_gps_us, c[i]));
        jw_arr_close(&w);
        jw_key(&w, "lap_time_s"); jw_double(&w, c[n - 1] - c[0], DEC_M);
        jw_key(&w, "sector_times_s");
        jw_arr_open(&w);
        for (int i = 1; i < n; i++) jw_double(&w, c[i] - c[i - 1], DEC_M);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);

    jw_key(&w, "fixes_written"); jw_uint(&w, fixes_written);
    jw_key(&w, "fused_written"); jw_uint(&w, fused_written);
    jw_obj_close(&w);

    int rc = -1;
    if (!jw_overflow(&w) && fputs(buf, f) != EOF && fputc('\n', f) != EOF) rc = 0;
    free(buf);
    return rc;
}

int synth_venue_write(FILE *f, const synth_run_t *r)
{
    if (!f || !r) return -1;
    trk_venue_t v;
    synth_run_venue(r, &v);
    char buf[VENUE_BUF_BYTES];
    if (trk_to_json(&v, buf, sizeof buf) < 0) return -1;
    if (fputs(buf, f) == EOF) return -1;
    if (fputc('\n', f) == EOF) return -1;
    return 0;
}

/* ---------------- generation ---------------- */

/* Emits every TIME_MAP due at or before t_s. Called with the time of a sample that really exists,
 * so the record count is exactly 1 + floor(duration_s / 60). */
static int emit_time_maps(logw_t *w, const synth_gps_cfg_t *g, double *next_s, double t_s)
{
    while (*next_s <= t_s + TIME_EPS_S) {
        int64_t mono = g->t0_mono_us + (int64_t)llround(*next_s * 1e6);
        if (logw_time_map(w, mono, gps_us_at(g->t0_gps_us, *next_s), TIME_MAP_QUALITY) < 0) return -1;
        *next_s += TIME_MAP_PERIOD_S;
    }
    return 0;
}

int synth_generate(const synth_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz,
                   logw_t *w, synth_run_t *run, uint32_t *n_fix, uint32_t *n_fused)
{
    if (!cfg || !gcfg || !w || !run || !n_fix || !n_fused) return -1;
    if (gcfg->rate_hz <= 0 || fused_hz < 0 || fused_hz > 255) return -1;
    *n_fix = 0;
    *n_fused = 0;
    if (synth_run_build(run, cfg, NULL, 0) != 0) return -1;

    ses_hdr_t h;
    memset(&h, 0, sizeof h);
    snprintf(h.session_id, sizeof h.session_id, "%s", SYNTH_SESSION_ID);
    h.mode = 0;                         /* lap mode */
    h.variant = 0;                      /* moto */
    h.venue_id = cfg->venue_id;
    h.layout_id = SYNTH_LAYOUT_ID;
    snprintf(h.fw, sizeof h.fw, "%s", replay_version());   /* char[17] in memory, 16 bytes on the wire */
    snprintf(h.hwid, sizeof h.hwid, "%s", SYNTH_HWID);
    h.log_profile = 0;                  /* internal */
    h.fused_hz = (uint8_t)fused_hz;
    h.gps_hz = (uint8_t)gcfg->rate_hz;
    h.start_gps_us = gcfg->t0_gps_us;
    for (int i = 0; i < 9; i++) h.r_e4[i] = (i % 4 == 0) ? (int16_t)CALIB_IDENT_E4 : (int16_t)0;
    h.calib_flags = CALIB_FLAGS_OK;
    if (logw_hdr(w, &h) < 0) return -1;
    if (logw_venue(w, cfg->venue_id, SYNTH_LAYOUT_ID, SYNTH_VENUE_NAME) < 0) return -1;

    synth_gps_t gs;
    synth_gps_init(&gs, gcfg);
    double next_tm_s = 0.0;
    uint32_t k = 0, j = 0;              /* next fix index and next fused index */
    bool fix_done = false, fused_done = (fused_hz <= 0);

    while (!fix_done || !fused_done) {
        double t_fix   = fix_done   ? 0.0 : (double)k / (double)gcfg->rate_hz;
        double t_fused = fused_done ? 0.0 : (double)j / (double)fused_hz;
        /* Two-stream merge, earliest first; a tie puts the fix first so FUSED always has a
         * FIX_* reference to delta against (§12.4). */
        bool take_fix = !fix_done && (fused_done || t_fix <= t_fused);
        if (take_fix) {
            gps_fix_t fix;
            int rc = synth_gps_next(&gs, run, &fix, NULL);
            if (rc < 0) { fix_done = true; continue; }
            k++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fix) < 0) return -1;
            if (rc == 1) {                       /* rc == 0 is a dropout: the slot exists, the fix does not */
                if (logw_fix(w, &fix) < 0) return -1;
                (*n_fix)++;
            }
        } else {
            if (t_fused > run->duration_s) { fused_done = true; continue; }
            j++;
            if (emit_time_maps(w, gcfg, &next_tm_s, t_fused) < 0) return -1;
            /* FUSED.dt_ms is relative to the previous FIX_* or FUSED record, so a fused sample
             * before the session's first fix has nothing to reference and is skipped. */
            if (*n_fix > 0) {
                fused_sample_t fs;
                synth_fused_at(run, t_fused, gcfg->t0_gps_us, gcfg->t0_mono_us, &fs);
                if (logw_fused(w, &fs) < 0) return -1;
                (*n_fused)++;
            }
        }
    }
    if (logw_end(w, gps_us_at(gcfg->t0_gps_us, run->duration_s), END_REASON_NORMAL) < 0) return -1;
    return w->err;
}

/* ---------------- option parsing ---------------- */

typedef enum { A_DBL, A_INT, A_U32 } argkind_t;
typedef struct { const char *name; argkind_t kind; void *dst; double lo, hi; } argopt_t;

static int parse_dbl(const char *s, double lo, double hi, double *out)
{
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return -1;
    if (!(v >= lo && v <= hi)) return -1;             /* also rejects NaN */
    *out = v;
    return 0;
}

static int parse_i64(const char *s, int64_t lo, int64_t hi, int64_t *out)
{
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) return -1;
    if ((int64_t)v < lo || (int64_t)v > hi) return -1;
    *out = (int64_t)v;
    return 0;
}

static int apply_opt(const argopt_t *o, const char *v)
{
    if (o->kind == A_DBL) {
        double d;
        if (parse_dbl(v, o->lo, o->hi, &d) != 0) return -1;
        *(double *)o->dst = d;
        return 0;
    }
    int64_t n;
    if (parse_i64(v, (int64_t)o->lo, (int64_t)o->hi, &n) != 0) return -1;
    if (o->kind == A_INT) *(int *)o->dst = (int)n;
    else                  *(uint32_t *)o->dst = (uint32_t)n;
    return 0;
}

static int parse_dropout(const char *s, synth_gps_cfg_t *g)
{
    const char *colon = strchr(s, ':');
    if (!colon || colon == s) return -1;
    char head[32];
    size_t n = (size_t)(colon - s);
    if (n + 1 > sizeof head) return -1;
    memcpy(head, s, n);
    head[n] = '\0';
    double a, b;
    if (parse_dbl(head, 0.0, DROPOUT_MAX_S, &a) != 0) return -1;
    if (parse_dbl(colon + 1, 0.0, DROPOUT_MAX_S, &b) != 0) return -1;
    if (b < a) return -1;
    g->dropout_start_s = a;
    g->dropout_end_s = b;
    return 0;
}

static int two(const char *s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

static int parse_utc(const char *s, int64_t *out)
{
    /* Exactly YYYY-MM-DDTHH:MM:SS, so a typo is rejected rather than silently truncated. */
    static const char pat[] = "0000-00-00T00:00:00";
    if (strlen(s) != sizeof pat - 1) return -1;
    for (size_t i = 0; i < sizeof pat - 1; i++) {
        if (pat[i] == '0') { if (s[i] < '0' || s[i] > '9') return -1; }
        else if (s[i] != pat[i]) return -1;
    }
    int y = two(s) * 100 + two(s + 2);
    int mo = two(s + 5), d = two(s + 8), hh = two(s + 11), mi = two(s + 14), ss = two(s + 17);
    if (y < 1970 || y > 2999 || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
    if (hh > 23 || mi > 59 || ss > 60) return -1;                 /* 60 = leap second */
    *out = tb_gps_us_from_utc(y, (unsigned)mo, (unsigned)d, (unsigned)hh, (unsigned)mi, (unsigned)ss, 0);
    return 0;
}

int synth_parse_args(int argc, char **argv, synth_cfg_t *cfg, synth_gps_cfg_t *gps,
                     int *fused_hz, char *out, size_t out_cap)
{
    if (!argv || !cfg || !gps || !fused_hz || !out || out_cap == 0) return -1;
    synth_cfg_defaults(cfg);
    synth_gps_cfg_defaults(gps);
    *fused_hz = SYNTH_FUSED_HZ;
    out[0] = '\0';

    /* One seed drives both the circuit and the GPS noise so a single --seed reproduces a session. */
    uint32_t seed = cfg->seed;
    const argopt_t tab[] = {
        { "--vertices",     A_INT, &cfg->n_vertices,       3.0,   (double)SYNTH_MAX_VERTICES },
        { "--length",       A_DBL, &cfg->length_m,        50.0,   100000.0 },
        { "--radius",       A_DBL, &cfg->corner_radius_m,  1.0,   1000.0 },
        { "--irregularity", A_DBL, &cfg->irregularity,     0.0,   0.9 },
        { "--seed",         A_U32, &seed,                  0.0,   4294967295.0 },
        { "--v-corner",     A_DBL, &cfg->v_corner_mps,     1.0,   150.0 },
        { "--v-max",        A_DBL, &cfg->v_max_mps,        1.0,   150.0 },
        { "--a-acc",        A_DBL, &cfg->a_acc_mps2,       0.1,   30.0 },
        { "--a-brk",        A_DBL, &cfg->a_brk_mps2,       0.1,   30.0 },
        { "--lap-var",      A_DBL, &cfg->lap_var,          0.0,   0.5 },
        { "--laps",         A_INT, &cfg->laps,             1.0,   (double)SYNTH_MAX_LAPS },
        { "--sectors",      A_INT, &cfg->n_sector_gates,   0.0,   (double)LAP_MAX_SECTORS },
        { "--start-before", A_DBL, &cfg->start_before_m,   0.0,   100000.0 },
        { "--stop-after",   A_DBL, &cfg->stop_after_m,     0.0,   100000.0 },
        { "--sf-frac",      A_DBL, &cfg->sf_frac,          0.0,   1.0 },
        { "--rate",         A_INT, &gps->rate_hz,          5.0,   10.0 },
        { "--pos-sigma",    A_DBL, &gps->pos_sigma_m,      0.0,   100.0 },
        { "--pos-tau",      A_DBL, &gps->pos_tau_s,        0.001, 100000.0 },
        { "--speed-sigma",  A_DBL, &gps->speed_sigma_mps,  0.0,   50.0 },
        { "--head-sigma",   A_DBL, &gps->head_sigma_deg,   0.0,   180.0 },
        { "--latency",      A_DBL, &gps->latency_ms,       0.0,   5000.0 },
        { "--jitter",       A_DBL, &gps->jitter_ms,        0.0,   5000.0 },
        { "--fused-hz",     A_INT, fused_hz,               0.0,   100.0 },
    };
    const size_t n_tab = sizeof tab / sizeof tab[0];

    int flags = 0;
    bool have_out = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0)          { flags |= SYNTH_ARG_HELP;  continue; }
        if (strcmp(a, "--quiet") == 0)         { flags |= SYNTH_ARG_QUIET; continue; }
        if (strcmp(a, "--anticlockwise") == 0) { cfg->clockwise = false;   continue; }
        if (i + 1 >= argc) return -1;                       /* every other option takes a value */
        const char *v = argv[++i];
        if (strcmp(a, "--out") == 0) {
            size_t n = strlen(v);
            if (n == 0 || n + 1 > out_cap) return -1;
            memcpy(out, v, n + 1);
            have_out = true;
            continue;
        }
        if (strcmp(a, "--dropout") == 0)   { if (parse_dropout(v, gps) != 0) return -1; continue; }
        if (strcmp(a, "--start-utc") == 0) { if (parse_utc(v, &gps->t0_gps_us) != 0) return -1; continue; }
        size_t t = 0;
        while (t < n_tab && strcmp(a, tab[t].name) != 0) t++;
        if (t == n_tab) return -1;
        if (apply_opt(&tab[t], v) != 0) return -1;
    }
    if (flags & SYNTH_ARG_HELP) return flags;               /* --help never needs --out */
    if (!have_out) return -1;
    if (gps->rate_hz != 5 && gps->rate_hz != 10) return -1; /* §12.6: the only logged GPS rates */
    cfg->seed = seed;
    gps->seed = seed;
    return flags;
}
```

- [ ] **Step 5: Write the CLI**

`tools/replay/synth_main.c`:

```c
#include "replay/synth_truth.h"
#include "replay/replay.h"
#include <stdio.h>
#include <stdlib.h>

#define PREFIX_CAP 480
#define PATH_CAP   512           /* PREFIX_CAP plus the longest suffix, ".truth.json" */
#define ERR_CAP    160           /* synth_run_build messages are one short sentence */

static void usage(FILE *f)
{
    fprintf(f,
        "usage: synth --out PREFIX [options]\n"
        "Writes PREFIX.log, PREFIX.truth.json and PREFIX.venue.json (spec 22.2).\n"
        "\n"
        "circuit:\n"
        "  --vertices N          polygon vertices                       (12)\n"
        "  --length M            lap length along the driven path, m    (2500)\n"
        "  --radius M            corner arc radius, m                   (40)\n"
        "  --irregularity F      vertex radius jitter, 0..0.9           (0)\n"
        "  --anticlockwise       drive anticlockwise                    (clockwise)\n"
        "  --seed S              seed for the circuit and the noise     (1)\n"
        "run:\n"
        "  --v-corner MPS        constant speed through every arc       (15)\n"
        "  --v-max MPS           straight cruise cap                    (50)\n"
        "  --a-acc MPS2          acceleration on straights              (3)\n"
        "  --a-brk MPS2          braking on straights                   (6)\n"
        "  --lap-var F           per-lap v_max scale 1 +/- F            (0.03)\n"
        "  --laps N              full laps after the first S/F          (10)\n"
        "  --sectors N           sector gates, 0..8                     (2)\n"
        "  --start-before M      out-lap length before S/F, m           (300)\n"
        "  --stop-after M        run-out past the last S/F, m           (200)\n"
        "  --sf-frac F           S/F position as a fraction of length   (0.041667)\n"
        "gps:\n"
        "  --rate HZ             fix rate, 5 or 10                      (5)\n"
        "  --pos-sigma M         Gauss-Markov position sigma, m         (1.5)\n"
        "  --pos-tau S           Gauss-Markov correlation time, s       (20)\n"
        "  --speed-sigma MPS     white speed noise sigma                (0.05)\n"
        "  --head-sigma DEG      white heading noise sigma              (0.5)\n"
        "  --latency MS          mean arrival latency                   (80)\n"
        "  --jitter MS           uniform arrival jitter, +/-            (20)\n"
        "  --dropout T0:T1       drop fixes for T0 <= t < T1, s         (none)\n"
        "  --fused-hz N          FUSED rate, 0 disables                 (10)\n"
        "  --start-utc TS        run time 0 as YYYY-MM-DDTHH:MM:SS      (2026-09-15T10:00:00)\n"
        "output:\n"
        "  --out PREFIX          output path prefix                     (required)\n"
        "  --quiet               no summary line on stdout\n"
        "  --help                this text\n");
}

int main(int argc, char **argv)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gcfg;
    int fused_hz = 0;
    char prefix[PREFIX_CAP];

    int flags = synth_parse_args(argc, argv, &cfg, &gcfg, &fused_hz, prefix, sizeof prefix);
    if (flags < 0) { usage(stderr); return 2; }
    if (flags & SYNTH_ARG_HELP) { usage(stdout); return 0; }

    char log_path[PATH_CAP], truth_path[PATH_CAP], venue_path[PATH_CAP];
    if (snprintf(log_path, sizeof log_path, "%s.log", prefix) >= (int)sizeof log_path ||
        snprintf(truth_path, sizeof truth_path, "%s.truth.json", prefix) >= (int)sizeof truth_path ||
        snprintf(venue_path, sizeof venue_path, "%s.venue.json", prefix) >= (int)sizeof venue_path) {
        fprintf(stderr, "synth: --out prefix is too long\n");
        return 2;
    }

    /* synth_run_t holds every lap table (a few megabytes), far past any sane stack. */
    synth_run_t *run = (synth_run_t *)malloc(sizeof *run);
    if (!run) { fprintf(stderr, "synth: out of memory\n"); return 1; }

    /* Validate before creating any file, so an impossible circuit reports its own message and
     * leaves no half-written output behind. synth_generate builds the same run again. */
    char err[ERR_CAP];
    err[0] = '\0';
    if (synth_run_build(run, &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "synth: %s\n", err);
        free(run);
        return 1;
    }

    logw_t w;
    if (logw_open_file(&w, log_path) != 0) {
        fprintf(stderr, "synth: cannot write %s\n", log_path);
        free(run);
        return 1;
    }
    uint32_t n_fix = 0, n_fused = 0;
    int rc = synth_generate(&cfg, &gcfg, fused_hz, &w, run, &n_fix, &n_fused);
    if (logw_close(&w) != 0) rc = -1;
    if (rc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", log_path);
        free(run);
        return 1;
    }

    FILE *tf = fopen(truth_path, "w");
    int trc = tf ? synth_truth_write(tf, run, &gcfg, n_fix, n_fused) : -1;
    if (tf && fclose(tf) != 0) trc = -1;
    if (trc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", truth_path);
        free(run);
        return 1;
    }

    FILE *vf = fopen(venue_path, "w");
    int vrc = vf ? synth_venue_write(vf, run) : -1;
    if (vf && fclose(vf) != 0) vrc = -1;
    if (vrc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", venue_path);
        free(run);
        return 1;
    }

    if (!(flags & SYNTH_ARG_QUIET))
        printf("wrote %s: %u fixes, %u fused, %d laps, %.3f s\n",
               log_path, n_fix, n_fused, cfg.laps, run->duration_s);
    free(run);
    return 0;
}
```

- [ ] **Step 6: Register the ctest smoke tests**

`tools/replay/CMakeLists.txt` (whole file — the last block is the only addition):

```cmake
# Host-only tools (spec §21.4, §22.2). Built by test/CMakeLists.txt through add_subdirectory; never an
# ESP-IDF component. Sources are globbed so parallel tasks add files without touching this list.
file(GLOB REPLAY_LIB_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)
add_library(replaylib STATIC ${REPLAY_LIB_SRCS})
target_include_directories(replaylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(replaylib PUBLIC core m)
target_compile_options(replaylib PRIVATE ${LAPTIMER_STRICT_FLAGS})

foreach(tool synth replay)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_main.c)
    add_executable(${tool} ${tool}_main.c)
    target_link_libraries(${tool} PRIVATE replaylib)
    target_compile_options(${tool} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  endif()
endforeach()

file(GLOB REPLAY_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/test_*.c)
foreach(src ${REPLAY_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE replaylib unity Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

if(TARGET synth AND TARGET replay)
  add_test(NAME synth_smoke COMMAND synth --out ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke --laps 2 --quiet)
  add_test(NAME replay_smoke COMMAND replay ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke.log --json)
  set_tests_properties(replay_smoke PROPERTIES DEPENDS synth_smoke PASS_REGULAR_EXPRESSION "\"bad_frames\":0")
endif()
```

Copy the same content over the `tools/replay/CMakeLists.txt` block in Task 1, Step 3 of this plan, so
the plan stays byte-identical to the file.

- [ ] **Step 7: Run to verify it passes**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel && ctest --test-dir test/build --output-on-failure 2>&1 | tail -12`
Expected: 20 tests, all passing — the 12 core suites, the 6 tool suites (`test_replay_smoke`,
`test_synth_track`, `test_synth_gps`, `test_logio`, `test_replay_summary`, `test_synth_e2e`) and the
two new smoke tests. The tail ends with

```
18/20 Test #18: test_synth_e2e ...................   Passed    0.56 sec
      Start 19: synth_smoke
19/20 Test #19: synth_smoke ......................   Passed    0.49 sec
      Start 20: replay_smoke
20/20 Test #20: replay_smoke .....................   Passed    0.48 sec

100% tests passed out of 20
```

(`test_synth_e2e` prints `8 Tests 0 Failures 0 Ignored` when run on its own.)

Then check the executable by hand:

Run: `./test/build/tools/replay/synth --out /tmp/synth_demo --laps 3 --rate 10 --sectors 3 && ./test/build/tools/replay/replay /tmp/synth_demo.log --json`
Expected: one summary line of the documented shape followed by the JSON summary, for example

```
wrote /tmp/synth_demo.log: 2571 fixes, 2571 fused, 3 laps, 257.083 s
{"frames":...,"bad_frames":0,...}
```

The fix and fused counts and the duration follow from the real circuit model (Task 2), so only the
shape is fixed: `wrote <prefix>.log: <n_fix> fixes, <n_fused> fused, <laps> laps, <duration> s`, and
`bad_frames` must be 0. Also check the failure paths:

Run: `./test/build/tools/replay/synth --out /tmp/x --rate 7 > /dev/null; echo "exit=$?"`
Expected: the usage text on stderr and `exit=2`.
Run: `./test/build/tools/replay/synth --out /tmp/x --v-corner 60 --v-max 50 > /dev/null; echo "exit=$?"`
Expected: `synth: v_corner must be below v_max` (the exact wording is Task 2's `synth_run_build`
message) on stderr and `exit=1`.

- [ ] **Step 8: Write the tool README**

`tools/replay/README.md`:

```markdown
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
```

- [ ] **Step 9: Plan and spec write-back check**

The "File structure produced by this plan" block of this plan already lists `include/replay/synth_truth.h` and `README.md` under Task 5, and Task 1's spec write-back (§4.2) already lists `synth_truth.h` in the header line and `synth_truth.c` in the source line. Verify rather than edit:

Run: `grep -c "synth_truth" docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/superpowers/plans/2026-09-14-plan-02-core-engines.md`
Expected: the spec count is 2 (header list and `replay/lib/*.c` list); the plan count is at least 3. If the spec count is not 2, apply Task 1 Step 6 (a) again before committing.

- [ ] **Step 10: Hygiene and commit**

Run: `git diff --check`
Expected: no output.
Run: `ctest --test-dir test/build --output-on-failure 2>&1 | tail -3`
Expected: `100% tests passed out of 20`.

```bash
git add tools/replay/include/replay/synth_truth.h tools/replay/lib/synth_truth.c \
        tools/replay/synth_main.c tools/replay/test/test_synth_e2e.c \
        tools/replay/README.md tools/replay/CMakeLists.txt \
        docs/superpowers/plans/2026-09-14-plan-02-core-engines.md \
        docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit -m "feat(tools): synth CLI, truth and venue writers, end-to-end fixture test

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

---

## Session 2.2 — fusion part 1: stillness, gyro bias, orientation capture, forward-axis learning

Roadmap exit criterion: `test_fus` calibration cases green (§22.1: rotation with identity and 90° mounts; gyro bias applied; still detection; orientation capture from tilted gravity; forward learning from synthetic straight-line acceleration; lean invalid before forward learned); tag `p02-d2`.

Task graph: **Task 1** (serial) → **Tasks 2, 3, 4** [parallel group 2] → **Task 5** (serial).

Decisions fixed for the session (rulings, spec is the authority; write-backs are in Task 1 Step 6):

- Units inside fusion: accel in g (`raw / IMU_ACC_LSB_PER_G`), gyro in dps (`(raw − gbias) / IMU_GYR_LSB_PER_DPS`), rad/s only where the math needs it; all math `float`, window accumulators `double` (a 2 s window sums 200 squared LSB values, which float32 cannot hold without cancellation).
- Stillness uses tumbling 2 s windows (200 samples): `FUS_STILL` and `fus_is_still` reflect the last completed window; no per-sample sliding statistics (memory and CPU stay trivial, latency ≤ 2 s is acceptable for bias capture and drag arming).
- `fus_calib_t` gains `bias_ok` (the spec's struct had no way to tell a stored zero bias from an uncaptured one); the IMU temperature reaches fusion through a new `fus_set_temp` (the raw sample has no temperature field; the driver's `imu_read_temp_c100` is polled by the pipeline at ~1 Hz).
- `FUS_ORIENT_OK` on a sample means both rotation rows are known (`orient_ok && forward_ok`); before that `g_lon` is the sign-less horizontal magnitude, `g_lat` is 0 and `FUS_LEAN_VALID` is clear. Lateral g in this session is the specific-force formula `−a.y` for both variants; session 2.3 replaces it for the moto with `−v·ψ̇` and adds the lean filter.
- Forward learning counts a run of qualifying fixes as one window the moment it reaches `FWD_LEARN_MIN_S` and keeps accumulating until the run ends; `fus_calib_forward_step` returns 1 exactly once, when the forward row is set.
- A zeroed `fus_still_t` / `fus_fwd_t` is a valid initialised state, so `fus_init` needs no calls into the parallel modules (Task 5 wires their use into `fus_step` and the public calibration calls).
- Host test executables are discovered by a CMake glob from this session on (`test/test_*.c`), so parallel tasks add suites without touching `test/CMakeLists.txt`; `core_selftest` already globs the same files and will run the new suites on the ESP32.

### Task 1: Fusion header, constants, step skeleton, test glob, spec write-backs (serial)

**Files:**
- Create: `components/core/include/core/fus.h`, `components/core/fusion/fus.c`, `test/test_fus.c`
- Modify: `components/core/include/core/consts.h` (six constants), `test/CMakeLists.txt` (glob), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§5.2 fus block, §9.2, Appendix A), `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` (its `consts.h` and `test/CMakeLists.txt` blocks), this plan's Task 1 Step 4 block for `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `core/types.h` (`imu_raw_t`, `fused_sample_t`, `FUS_*` flags), `core/consts.h`, `core/ses.h` (`ses_calib_t`).
- Produces: `core/fus.h` below, verbatim. Tasks 2–4 implement `fus_still_*`, `fus_orient_*`/`fus_rotate` is here, `fus_fwd_*`; Task 5 wires them. Tasks 2–4 MUST NOT change the header.

- [ ] **Step 1: Write the failing tests**

`test/test_fus.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw sample helpers: accel in g and gyro in dps expressed as MPU-6050 LSB (types.h scales). */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    imu_raw_t r;
    r.mono_us = 1000000;
    r.ax = (int16_t)lround(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lround(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lround(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lround(gx * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lround(gy * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lround(gz * IMU_GYR_LSB_PER_DPS);
    return r;
}

static void test_defaults_are_identity_and_valid(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[0]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[4]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[8]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, c.orient_ok); TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok); TEST_ASSERT_EQUAL_UINT8(0, c.bias_ok);
}

static void test_invalid_calibration_is_rejected_and_init_falls_back(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.version = 0;
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[0] = 2.0f;                       /* row x not unit length */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[3] = 1.0f;                       /* row y = (1,0,0) parallel to row x */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.forward_ok = 1;                    /* forward without orientation */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.gbias[1] = 40000.0f;               /* beyond the raw range */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));

    fus_t f; c.version = 0;
    fus_init(&f, &c, 1);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fus_calib(&f)->r[0]);
    fus_init(&f, NULL, 0);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_UINT8(0, f.moto);
}

static void test_identity_mount_gravity_bias_and_yaw_sign(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 5.0f * IMU_GYR_LSB_PER_DPS;                     /* 5 dps bias on X */
    c.bias_ok = 1;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 5.0, 0.0, 10.0);
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_EQUAL_INT64(1000000, o.mono_us);
    /* orientation not learned: sign-less horizontal magnitude, no lateral, no lean, ORIENT_OK clear */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & (FUS_ORIENT_OK | FUS_LEAN_VALID));
    /* yaw: +10 dps about body Z = left turn, bias-free because the bias is on X only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);

    c.orient_ok = 1; c.forward_ok = 1;                            /* identity mount fully known */
    fus_init(&f, &c, 1);
    r = raw_g_dps(0.3, 0.5, 1.0, 5.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    /* accel tolerance 1e-3 g: raw LSB quantisation is 1/2048 g ≈ 4.9e-4 g */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* +X forward: accelerating */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, o.g_lat);             /* +Y is left, so lateral g is -a.y */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(0.34f), o.g_comb);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.yaw_dps);            /* 5 dps on X minus the 5 dps bias */
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_EQUAL_UINT32(1, f.samples);
}

static void test_ninety_degree_mount_rotates_into_the_vehicle_frame(void)
{
    /* IMU mounted with body +X pointing left (vehicle +Y) and body +Y pointing backwards (vehicle -X);
     * body +Z up. Rows are the vehicle axes in body coordinates: x = (0,-1,0), y = (1,0,0), z = (0,0,1). */
    fus_calib_t c; fus_calib_defaults(&c);
    const float R[9] = { 0.0f, -1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.orient_ok = 1; c.forward_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    /* vehicle accelerating at 0.3 g forward appears on body -Y; a 10 dps left turn is body +Z */
    imu_raw_t r = raw_g_dps(0.0, -0.3, 1.0, 0.0, 0.0, 10.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* 1e-3 g: LSB quantisation */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    /* a right-hand lateral specific force (vehicle -Y = body -X) reads as positive g_lat */
    r = raw_g_dps(-0.4, 0.0, 1.0, 0.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lon);

    float v[3]; const float b[3] = { 1.0f, 2.0f, 3.0f };
    fus_rotate(R, b, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, v[2]);
}

static void test_temperature_drift_marks_the_bias_stale(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.bias_ok = 1; c.gbias_temp_c100 = 2500;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; imu_raw_t r = raw_g_dps(0, 0, 1, 0, 0, 0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);        /* temperature unknown: not stale */
    fus_set_temp(&f, 3900);                                      /* 14 °C away: within BIAS_TEMP_STALE_C */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 4100);                                      /* 16 °C away */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 900);                                       /* 16 °C the other way */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    /* no bias captured: temperature can never make it stale */
    fus_calib_defaults(&c); fus_init(&f, &c, 1); fus_set_temp(&f, 9000);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
}

static void test_calibration_round_trips_through_the_calib_record(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float ang = 30.0f * 3.14159265358979f / 180.0f;         /* rotation about Z by 30° (M_PI is not C11) */
    const float R[9] = { cosf(ang), sinf(ang), 0.0f,  -sinf(ang), cosf(ang), 0.0f,  0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.gbias[0] = 12.4f; c.gbias[1] = -7.6f; c.gbias[2] = 0.4f; c.gbias_temp_c100 = 2712;
    c.orient_ok = 1; c.forward_ok = 1; c.bias_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    ses_calib_t w; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(8660, w.r_e4[0]);                     /* cos 30° × 1e4 rounded */
    TEST_ASSERT_EQUAL_INT16(5000, w.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(12, w.gbias[0]); TEST_ASSERT_EQUAL_INT16(-8, w.gbias[1]); TEST_ASSERT_EQUAL_INT16(0, w.gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(0x07, w.calib_flags);
    fus_calib_t d; fus_calib_from_ses(&w, &d);
    for (int i = 0; i < 9; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, c.r[i], d.r[i]);
    TEST_ASSERT_TRUE(fus_calib_valid(&d));                        /* 1e-4 quantisation stays inside FUS_ORTHO_TOL */
    TEST_ASSERT_EQUAL_FLOAT(12.0f, d.gbias[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d.orient_ok); TEST_ASSERT_EQUAL_UINT8(1, d.forward_ok); TEST_ASSERT_EQUAL_UINT8(1, d.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, d.version);
    TEST_ASSERT_EQUAL_INT16(0, d.gbias_temp_c100);                 /* not carried by the record */
    /* a saturating bias clamps instead of wrapping */
    c.gbias[2] = 40000.0f; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(32767, w.gbias[2]);
}

static void test_gps_speed_is_held_with_its_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_identity_and_valid);
    RUN_TEST(test_invalid_calibration_is_rejected_and_init_falls_back);
    RUN_TEST(test_identity_mount_gravity_bias_and_yaw_sign);
    RUN_TEST(test_ninety_degree_mount_rotates_into_the_vehicle_frame);
    RUN_TEST(test_temperature_drift_marks_the_bias_stale);
    RUN_TEST(test_calibration_round_trips_through_the_calib_record);
    RUN_TEST(test_gps_speed_is_held_with_its_validity_and_time);
    return UNITY_END();
}
```

- [ ] **Step 2: Add the constants**

Append to `components/core/include/core/consts.h` immediately after the line `#define MOVING_SPEED_KMH       3` (before `#endif`):

```c
#define IMU_ACC_LSB_PER_G      2048.0f
#define IMU_GYR_LSB_PER_DPS    16.4f
#define FWD_LEARN_MAX_YAW_DPS  2.0f
#define FUS_ORIENT_MIN_G       0.5f
#define FUS_ORIENT_MAX_G       1.5f
#define FUS_REF_MAX_AGE_US     1000000LL
```

Update the `consts.h` block in `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` (search for `#define MOVING_SPEED_KMH`) to match the file byte-for-byte.

- [ ] **Step 3: Write the header**

`components/core/include/core/fus.h`:

```c
#ifndef CORE_FUS_H
#define CORE_FUS_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"
#include "core/ses.h"

/* Sensor fusion and calibration (spec §9.2–9.3). Pure C11, no allocation; all state in fus_t.
 *
 * Frames: body = the IMU's own axes as mounted; vehicle = X forward, Y left, Z up. The rotation R has
 * rows x, y, z = the vehicle axes expressed in body coordinates, so vehicle = R · body.
 * Units: accel in g (raw / IMU_ACC_LSB_PER_G); gyro in dps ((raw − gbias) / IMU_GYR_LSB_PER_DPS),
 * converted to rad/s only where the math needs it. Signs (core/types.h): g_lat and lean + right,
 * yaw + left turn, g_lon + accelerating.
 *
 * A zeroed fus_still_t or fus_fwd_t is a valid initialised state (the *_init functions memset). */

#define FUS_CALIB_VERSION   1
#define FUS_ORTHO_TOL       1e-3f                               /* row norm / dot tolerance for a valid R */
#define FUS_STILL_WINDOW_N  (STILL_WINDOW_S * FUSION_HZ)        /* 200 samples per stillness window */
#define FUS_FWD_MIN_SAMPLES (FWD_LEARN_MIN_S * FUSION_HZ)       /* 100 samples before a run counts */
#define FUS_CALIB_F_ORIENT  0x01                                /* ses_calib_t.calib_flags bits */
#define FUS_CALIB_F_FORWARD 0x02
#define FUS_CALIB_F_BIAS    0x04

typedef struct {
    float   r[9];             /* rows x, y, z (row-major) */
    float   gbias[3];         /* gyro bias, raw LSB */
    int16_t gbias_temp_c100;  /* IMU temperature when gbias was captured */
    uint8_t bias_ok;          /* gbias captured at least once */
    uint8_t orient_ok;        /* z row captured */
    uint8_t forward_ok;       /* x and y rows learned (implies orient_ok) */
    uint8_t version;          /* FUS_CALIB_VERSION */
} fus_calib_t;
void fus_calib_defaults(fus_calib_t *c);                        /* identity R, zero bias, flags 0, current version */
/* version matches, every value finite, |gbias| < 32768, forward_ok implies orient_ok, R orthonormal within FUS_ORTHO_TOL */
bool fus_calib_valid(const fus_calib_t *c);
/* CALIB record (§12.3): r × 1e4 → int16, gbias rounded and clamped to int16, flags FUS_CALIB_F_*. The record
 * carries no temperature: fus_calib_from_ses sets gbias_temp_c100 = 0 and version = FUS_CALIB_VERSION. */
void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out);
void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out);

/* ---- Stillness detector: tumbling windows of FUS_STILL_WINDOW_N raw samples (§9.2) ---- */
typedef struct {
    double   sum_amag, sum_amag2;    /* |accel| in g over the current window */
    double   sum_g[3], sum_g2[3];    /* gyro in dps (bias not removed) */
    double   sum_acc[3];             /* accel in g, for the orientation mean */
    double   sum_graw[3];            /* gyro raw LSB, for the bias mean */
    uint16_t n;                      /* samples in the current window */
    bool     have_window;            /* a window has completed at least once */
    bool     still;                  /* the last completed window was still */
    float    acc_var;                /* last completed window: variance of |a|, g² */
    float    gyr_var_max;            /* last completed window: largest gyro axis variance, dps² */
    float    mean_acc[3];            /* last completed window: mean accel, g (body) */
    float    mean_graw[3];           /* last completed window: mean gyro, raw LSB */
} fus_still_t;
void fus_still_init(fus_still_t *s);
/* Accumulates one raw sample. Returns 1 when this sample completed a window (the last-window fields are
 * then updated and the window restarts), else 0. still = acc_var < STILL_ACC_VAR && gyr_var_max < STILL_GYRO_VAR. */
int  fus_still_push(fus_still_t *s, const imu_raw_t *raw);

/* ---- Orientation rows (§9.2) ---- */
/* z = normalize(mean_acc); rows x, y become a provisional orthonormal completion (the body axis least aligned
 * with z, projected); orient_ok = 1, forward_ok = 0. Returns -1 (calib untouched) unless
 * FUS_ORIENT_MIN_G ≤ |mean_acc| ≤ FUS_ORIENT_MAX_G. */
int  fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3]);
/* x = normalize(sum_ah − (sum_ah·z)z), y = z × x, x = y × z; forward_ok = 1. Returns -1 (calib untouched) if
 * !orient_ok or the horizontal component is shorter than 1e-6. */
int  fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3]);
void fus_rotate(const float r[9], const float b[3], float v[3]);   /* v = R · b */

/* ---- Forward-axis learning window tracker (§9.2) ---- */
typedef struct {
    bool     cond;            /* the latest fix qualifies: |yaw| < FWD_LEARN_MAX_YAW_DPS and gps_acc > FWD_LEARN_ACC_MPS2 */
    uint32_t run_samples;     /* consecutive fus_step samples with cond true */
    double   run_sum[3];      /* a_h accumulated during the current run (g, body) */
    double   sum[3];          /* a_h accumulated over counted runs */
    uint8_t  windows;         /* runs counted so far */
    bool     counted;         /* the current run has been counted (it reached FUS_FWD_MIN_SAMPLES) */
} fus_fwd_t;
void fus_fwd_init(fus_fwd_t *w);
/* Per GPS fix: sets cond. A run ends when cond turns false; its samples count only if it was counted. */
void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps);
/* Per fus_step while orientation is known: accumulates a_h = a − (a·z)z when cond. When a run reaches
 * FUS_FWD_MIN_SAMPLES it is counted (its run_sum so far moves into sum, later samples add to sum directly).
 * Returns 1 exactly once, when the FWD_LEARN_WINDOWS-th run is counted; else 0. */
int  fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3]);
bool fus_fwd_ready(const fus_fwd_t *w);                          /* windows >= FWD_LEARN_WINDOWS */

/* ---- Fusion state ---- */
typedef struct {
    fus_calib_t calib;
    uint8_t     moto;                /* variant: 1 moto (lean filter), 0 car */
    fus_still_t still;
    fus_fwd_t   fwd;
    float       v_mps;               /* latest GPS speed (fus_set_gps_speed) */
    int64_t     v_mono_us;
    bool        v_valid;
    int16_t     temp_c100;           /* latest IMU temperature (fus_set_temp) */
    bool        temp_known;
    bool        bias_stale;          /* |temp − gbias_temp| > BIAS_TEMP_STALE_C since the last bias update */
    bool        fwd_learned_pending; /* set by fus_step when the forward row was just learned; consumed by fus_calib_forward_step */
    float       lean_rad;            /* session 2.3: lean filter state */
    int64_t     last_ref_mono_us;    /* session 2.3: last time a lean reference was applied */
    uint32_t    samples;             /* fus_step calls since init */
} fus_t;

/* calib NULL or invalid → defaults. */
void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto);
void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid);
/* IMU temperature (~1 Hz from the pipeline). Sets bias_stale when a captured bias is more than
 * BIAS_TEMP_STALE_C away from its capture temperature. */
void fus_set_temp(fus_t *f, int16_t temp_c100);
/* Processes one raw sample into out; always produces a sample and returns 1. out->gps_us is left 0 for the
 * pipeline to fill from the time base. */
int  fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out);
bool fus_is_still(const fus_t *f);                               /* last completed stillness window was still */
/* gbias = last still window's mean raw gyro, gbias_temp = current temperature (0 if unknown), bias_ok = 1,
 * bias_stale cleared. No-op unless fus_is_still. */
void fus_gyro_bias_update(fus_t *f);
/* Upright capture (menu action): z row from the last still window's mean accel. 0 ok / -1 not still or
 * fus_orient_from_gravity rejected the mean. */
int  fus_calib_orient_capture(fus_t *f);
/* Per GPS fix: feeds the forward-learning tracker. Returns 1 when the forward row was just learned (caller
 * persists the calibration and emits EV_CALIB_DONE), 0 otherwise, -1 if orientation is not captured. */
int  fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps);
const fus_calib_t *fus_calib(const fus_t *f);
#endif
```

- [ ] **Step 4: Write the step skeleton**

`components/core/fusion/fus.c` (this session's Task 5 replaces the four bottom functions and extends `fus_step`; everything else is final):

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
}

void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid)
{
    f->v_mps = v_mps; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */
        out->g_lat = -a[1];                      /* +Y is left; lateral g is + to the right (§9.3 step 5, car form) */
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;
    }
    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);
    out->lean_deg = 0.0f;                        /* lean filter arrives in session 2.3 */
    out->yaw_dps  = w[2];                        /* session 2.3 applies the lean correction of §9.3 step 3 */
    uint8_t flags = 0;
    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;
    out->flags = flags;
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- wired to the still / orient / fwd modules in Task 5 ---- */

bool fus_is_still(const fus_t *f)
{
    (void)f;
    return false;
}

void fus_gyro_bias_update(fus_t *f)
{
    (void)f;
}

int fus_calib_orient_capture(fus_t *f)
{
    (void)f;
    return -1;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    (void)f; (void)gps_acc_mps2; (void)yaw_dps;
    return -1;
}
```

- [ ] **Step 5: Switch the host test list to a glob**

Replace the twelve `add_core_test(...)` lines in `test/CMakeLists.txt` with:

```cmake
# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no edit here)
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_core_test(${name})
endforeach()
```

so the file reads, in full:

```cmake
cmake_minimum_required(VERSION 3.16)
project(laptimer_host C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# One strict flag set for core, tools and tests (spec §17.9, §21.4).
set(LAPTIMER_STRICT_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/core)
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC ${CORE_DIR}/include)
target_compile_options(core PRIVATE ${LAPTIMER_STRICT_FLAGS})
# vendored third-party sources get relaxed warnings (file added in Task 8)
set_source_files_properties(${CORE_DIR}/util/jsmn.c PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-unused-function")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # -fno-sanitize-recover makes a UBSan finding abort the test run instead of printing and continuing,
  # so a ctest pass really means no undefined behaviour was executed.
  target_compile_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  target_link_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)

enable_testing()
find_package(Threads REQUIRED)
function(add_core_test name)
  add_executable(${name} ${name}.c)
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE core unity m Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no edit here)
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_core_test(${name})
endforeach()

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)
```

Update the two other restatements of this file to the same content: the block in `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` and this plan's Session 2.1 Task 1 Step 4 block.

- [ ] **Step 6: Build and run**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1 && cmake --build test/build --parallel 2>&1 | grep -E "error|warning" | head; ctest --test-dir test/build --output-on-failure 2>&1 | tail -3`
Expected: `100% tests passed out of 21` (`test_fus` added: 7 cases).

- [ ] **Step 7: Spec write-backs**

In `docs/superpowers/specs/2026-09-14-lap-timer-design.md`:

(a) §5.2: replace the block that begins `#### \`core/fus.h\` — fusion (planned, plan 02)` and ends with the closing fence after `const fus_calib_t *fus_calib(const fus_t *f);` with:

````
#### `core/fus.h` — fusion (plan 02)

`fused_sample_t` lives in `core/types.h` above. Calibration and the public entry points (excerpt; the
internal stillness and forward-learning trackers `fus_still_t` / `fus_fwd_t` are documented in the
header):

```c
#define FUS_CALIB_VERSION 1
typedef struct {
    float   r[9];             /* rows x, y, z (row-major): vehicle axes in body coordinates, vehicle = R · body */
    float   gbias[3];         /* gyro bias, raw LSB */
    int16_t gbias_temp_c100;  /* IMU temperature when gbias was captured */
    uint8_t bias_ok;          /* gbias captured at least once */
    uint8_t orient_ok;        /* z row captured */
    uint8_t forward_ok;       /* x and y rows learned (implies orient_ok) */
    uint8_t version;          /* FUS_CALIB_VERSION */
} fus_calib_t;
void fus_calib_defaults(fus_calib_t *c);
bool fus_calib_valid(const fus_calib_t *c);           /* version, finite, R orthonormal within 1e-3 */
void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out);      /* CALIB record (§12.3) */
void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out);

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto);   /* NULL/invalid calib → defaults */
void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid);
void fus_set_temp(fus_t *f, int16_t temp_c100);       /* IMU temperature, ~1 Hz; drives FUS_BIAS_STALE */
int  fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out);   /* one raw sample → one fused sample */
bool fus_is_still(const fus_t *f);
void fus_gyro_bias_update(fus_t *f);                  /* call when still; updates calib */
int  fus_calib_orient_capture(fus_t *f);              /* upright capture; fills the Z row */
int  fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps);   /* per fix; 1 when the X row is learned */
const fus_calib_t *fus_calib(const fus_t *f);
```
````

(b) §9.2: after the paragraph beginning `- Until \`forward_ok\`, lean and lateral g are flagged invalid` append:

```
**Temperature.** The pipeline polls `imu_read_temp_c100` (~1 Hz) and passes it to `fus_set_temp`;
`FUS_BIAS_STALE` is derived from it and `gbias_temp_c100`, and only once a bias has been captured
(`bias_ok`).

**Stillness windows.** The detector uses tumbling windows of `STILL_WINDOW_S · FUSION_HZ` samples;
`fus_is_still` and the `FUS_STILL` flag reflect the last completed window, so stillness is reported
with at most one window of latency.

**Forward-learning windows.** `fus_calib_forward_step` is called once per GPS fix with `Δv/Δt` and
the current yaw rate; while the condition holds, every fusion step accumulates `a_h`. A run counts as
one window the moment it has lasted `FWD_LEARN_MIN_S` and keeps accumulating until it ends; when the
`FWD_LEARN_WINDOWS`-th run is counted the forward row is set and the call returns 1 (the pipeline
persists the calibration and emits `EV_CALIB_DONE`).

**Flags.** `FUS_ORIENT_OK` on a sample means both the Z row and the forward row are known; until
then `g_lon` is the sign-less horizontal specific-force magnitude, `g_lat` is 0 and `FUS_LEAN_VALID`
is clear. `FUS_STILL` mirrors `fus_is_still`; `FUS_BIAS_STALE` is described above.
```

(c) Appendix A: after the row `| \`FWD_LEARN_ACC_MPS2\` / \`FWD_LEARN_MIN_S\` / \`FWD_LEARN_WINDOWS\` | 1.5 / 1 / 3 | §9.2 |` insert:

```
| `FWD_LEARN_MAX_YAW_DPS` | 2 | §9.2 |
| `FUS_ORIENT_MIN_G` / `FUS_ORIENT_MAX_G` | 0.5 / 1.5 | §9.2 |
| `FUS_REF_MAX_AGE_US` | 1000000 | §9.3 |
| `IMU_ACC_LSB_PER_G` / `IMU_GYR_LSB_PER_DPS` | 2048 / 16.4 | §8.2 |
```

Verify: `grep -c "fus_set_temp" docs/superpowers/specs/2026-09-14-lap-timer-design.md` prints 2; `grep -c "FWD_LEARN_MAX_YAW_DPS" docs/superpowers/specs/2026-09-14-lap-timer-design.md` prints 1.

- [ ] **Step 8: Hygiene and commit**

Run: `git diff --check`
Expected: no output.

```bash
git add components/core/include/core/fus.h components/core/include/core/consts.h components/core/fusion/fus.c test/test_fus.c test/CMakeLists.txt docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md docs/superpowers/plans/2026-09-14-plan-02-core-engines.md
git commit -m "feat(core): fusion header, calibration codec, rotation step skeleton, host test glob

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 2: Stillness detector (`components/core/fusion/fus_still.c`) [parallel group 2]

**Files:**
- Create: `components/core/fusion/fus_still.c`, `test/test_fus_still.c`
- Modify: none. The host test list (`test/CMakeLists.txt`, Task 1 Step 5) and the on-target
  `test_apps/core_selftest` wrapper list are both CMake globs over `test/test_*.c`, and `core` globs
  `components/core/*.c` recursively, so neither build file is touched. `test_fus_still.c` therefore
  also has to compile for the ESP32: it includes only `unity.h`, `core/fus.h` and `<math.h>`, keeps
  every helper `static` (the self-test renames `main`/`setUp`/`tearDown` per suite and compiles each
  suite as its own TU) and puts no large arrays on the stack.

**Interfaces:**
- Consumes: `core/fus.h` (`fus_still_t`, `FUS_STILL_WINDOW_N` = `STILL_WINDOW_S · FUSION_HZ` = 200),
  `core/types.h` (`imu_raw_t`), `core/consts.h` (`STILL_ACC_VAR` = 4e-4 g², `STILL_GYRO_VAR` = 4 dps²,
  `IMU_ACC_LSB_PER_G` = 2048, `IMU_GYR_LSB_PER_DPS` = 16.4).
- Produces (declared by Task 1 in `core/fus.h`, which this task MUST NOT change):
  `void fus_still_init(fus_still_t *s);`
  `int  fus_still_push(fus_still_t *s, const imu_raw_t *raw);` — returns 1 on the sample that
  completes a window (last-window fields updated, sums restarted), 0 otherwise.

Design (binding, from the session rulings and spec §9.2):

- `fus_still_init` memsets: a zeroed `fus_still_t` is the initialised state (`core/fus.h`).
- `fus_still_push` converts the raw sample once — accel axes `raw / IMU_ACC_LSB_PER_G` (g) plus the
  magnitude `|a|`, gyro axes `raw / IMU_GYR_LSB_PER_DPS` (dps, bias **not** removed, because the mean
  raw gyro of a still window is what later becomes the bias) — and adds them into `sum_amag`,
  `sum_amag2`, `sum_g[i]`, `sum_g2[i]`, `sum_acc[i]`, `sum_graw[i]`, then `n++`.
- On `n == FUS_STILL_WINDOW_N`: `acc_var = E[|a|²] − E[|a|]²` clamped at 0 against round-off,
  `gyr_var_max = max_i (E[g_i²] − E[g_i]²)`, `mean_acc` and `mean_graw` latched as floats,
  `still = acc_var < STILL_ACC_VAR && gyr_var_max < STILL_GYRO_VAR`, `have_window = true`, sums and
  `n` zeroed, return 1. Otherwise return 0. The last-window fields persist until the next completion,
  which is what gives `fus_is_still` its ≤ 2 s latency.
- The accumulators are `double` because the variance is taken as `E[x²] − E[x]²`, which cancels
  catastrophically in float32 (the file's header comment carries the numbers).

- [ ] **Step 1: Write the failing test**

`test/test_fus_still.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw-LSB sample (mono_us is unused by the detector: it counts samples, not time). */
static imu_raw_t raw_lsb(int ax, int ay, int az, int gx, int gy, int gz)
{
    imu_raw_t r;
    r.mono_us = 0;
    r.ax = (int16_t)ax; r.ay = (int16_t)ay; r.az = (int16_t)az;
    r.gx = (int16_t)gx; r.gy = (int16_t)gy; r.gz = (int16_t)gz;
    return r;
}

/* Physical-unit sample rounded to the MPU-6050 LSB scales of core/types.h. */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    return raw_lsb((int)lround(ax_g * IMU_ACC_LSB_PER_G), (int)lround(ay_g * IMU_ACC_LSB_PER_G),
                   (int)lround(az_g * IMU_ACC_LSB_PER_G), (int)lround(gx * IMU_GYR_LSB_PER_DPS),
                   (int)lround(gy * IMU_GYR_LSB_PER_DPS), (int)lround(gz * IMU_GYR_LSB_PER_DPS));
}

/* Deterministic LCG (Numerical Recipes constants) so the noise is identical on every run and host. */
static uint32_t lcg_next(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return *st;
}

/* Uniform draw on [-half, +half] from the top 16 bits (an LCG's low bits are weakly random).
 * A uniform on [a, b] has variance (b − a)² / 12, so here the variance is half² / 3. */
static double lcg_uniform(uint32_t *st, double half)
{
    const uint32_t u = lcg_next(st) >> 16;                 /* 0 .. 65535 */
    return ((double)u / 65535.0 * 2.0 - 1.0) * half;
}

/* One full window of accel-magnitude noise: |a| = 1 g + U(-h, h) with h = sigma·sqrt(3). */
static void push_accel_noise_window(fus_still_t *s, uint32_t *st, double sigma_g)
{
    const double h = sigma_g * sqrt(3.0);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) {
        const imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0 + lcg_uniform(st, h), 0.0, 0.0, 0.0);
        (void)fus_still_push(s, &r);
    }
}

/* One full window of gyro noise on the Y axis only, accel held at a clean 1 g on Z. */
static void push_gyro_noise_window(fus_still_t *s, uint32_t *st, double sigma_dps)
{
    const double h = sigma_dps * sqrt(3.0);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) {
        const imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 0.0, lcg_uniform(st, h), 0.0);
        (void)fus_still_push(s, &r);
    }
}

static void test_constant_window_completes_on_the_two_hundredth_sample(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 10, -5, 3);          /* exactly 1 g on Z, a tiny gyro offset */
    for (int i = 0; i < FUS_STILL_WINDOW_N - 1; i++) TEST_ASSERT_EQUAL_INT(0, fus_still_push(&s, &r));
    TEST_ASSERT_EQUAL_INT(1, fus_still_push(&s, &r));
    TEST_ASSERT_TRUE(s.have_window);
    TEST_ASSERT_TRUE(s.still);
    /* identical samples: E[x²] − E[x]² is zero up to double round-off, ~1e-16 relative at |a|² = 1 g² */
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.gyr_var_max);
    /* means are exact in float: 2048/2048 = 1 g, and the raw gyro mean is the repeated raw value */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.mean_acc[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.mean_acc[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, s.mean_acc[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, s.mean_graw[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -5.0f, s.mean_graw[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, s.mean_graw[2]);
    TEST_ASSERT_EQUAL_UINT16(0, s.n);                            /* the window restarted */
}

static void test_nothing_is_reported_before_the_first_window_completes(void)
{
    fus_still_t s; fus_still_init(&s);
    TEST_ASSERT_FALSE(s.have_window);
    TEST_ASSERT_FALSE(s.still);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 0, 0, 0);             /* a perfectly still stream */
    for (int i = 0; i < FUS_STILL_WINDOW_N - 1; i++) {
        TEST_ASSERT_EQUAL_INT(0, fus_still_push(&s, &r));
        TEST_ASSERT_FALSE(s.have_window);                        /* still-looking input reports nothing yet */
        TEST_ASSERT_FALSE(s.still);
    }
    TEST_ASSERT_EQUAL_UINT16(FUS_STILL_WINDOW_N - 1, s.n);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.acc_var);                    /* last-window fields untouched since init */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gyr_var_max);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean_acc[2]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mean_graw[0]);
}

static void test_noise_variances_match_the_analytic_value_and_the_thresholds(void)
{
    /* 20 % band: the sample variance of N = 200 uniform draws has relative sd
     * sqrt((mu4/sigma⁴ − (N−3)/(N−1))/N) = sqrt((1.8 − 1)/200) ≈ 6.3 %, so 20 % is about 3 sd.
     * The LSB quantisation adds q²/12 = 2.0e-8 g² and 3.1e-4 dps², both negligible here. */
    uint32_t st = 2026u;
    fus_still_t s; fus_still_init(&s);

    push_accel_noise_window(&s, &st, 0.01);                      /* variance 1e-4 g² < STILL_ACC_VAR = 4e-4 */
    TEST_ASSERT_TRUE(s.have_window);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 1e-4f, 1e-4f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.gyr_var_max);        /* gyro was constant zero */

    fus_still_init(&s);
    push_accel_noise_window(&s, &st, 0.03);                      /* variance 9e-4 g² > 4e-4 */
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 9e-4f, 9e-4f, s.acc_var);

    fus_still_init(&s);
    push_gyro_noise_window(&s, &st, 1.0);                        /* variance 1 dps² < STILL_GYRO_VAR = 4 */
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 1.0f, 1.0f, s.gyr_var_max);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);            /* accel was constant 1 g */

    fus_still_init(&s);
    push_gyro_noise_window(&s, &st, 3.0);                        /* variance 9 dps² > 4 */
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(0.2f * 9.0f, 9.0f, s.gyr_var_max);
}

static void test_tumbling_windows_latch_until_the_next_completion(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t calm = raw_lsb(0, 0, 2048, 0, 0, 0);         /* 1 g */
    const imu_raw_t hi   = raw_lsb(0, 0, 3 * 2048, 0, 0, 0);     /* 3 g */

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);

    /* |a| alternates 1 g / 3 g: mean 2 g, E[x²] = 5 g², so the variance is exactly 1 g² */
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, (i & 1) ? &hi : &calm);
    TEST_ASSERT_FALSE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, s.acc_var);            /* 1e-5: float32 ULP at 1 g² is 6e-8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, s.mean_acc[2]);

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);

    /* 150 moving samples (three quarters of a window) cannot change the latched flag */
    for (int i = 0; i < 150; i++) {
        (void)fus_still_push(&s, (i & 1) ? &hi : &calm);
        TEST_ASSERT_TRUE(s.still);
        TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
    }
    TEST_ASSERT_EQUAL_UINT16(150, s.n);
}

static void test_one_spike_breaks_the_window_and_the_next_one_recovers(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t calm  = raw_lsb(0, 0, 2048, 0, 0, 0);        /* |a| = 1 g */
    const imu_raw_t spike = raw_lsb(0, 0, 9 * 2048, 0, 0, 0);    /* |a| = 9 g, i.e. delta = +8 g */
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, (i == 100) ? &spike : &calm);
    TEST_ASSERT_FALSE(s.still);
    /* one outlier of delta in N identical samples: var = delta²·(1/N)(1 − 1/N) = 64·199/200² = 0.3184 g² */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3184f, s.acc_var);         /* 1e-5: float32 ULP at 0.32 g² is 3e-8 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f + 8.0f / (float)FUS_STILL_WINDOW_N, s.mean_acc[2]);

    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &calm);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, s.acc_var);
}

static void test_full_scale_constant_window_does_not_overflow(void)
{
    fus_still_t s; fus_still_init(&s);
    /* Sigma|a|² reaches 200·768 g² = 1.5e5 g² and Sigma g² reaches 200·(32767/16.4)² = 8.0e8 dps²;
     * a double's ULP there is 3.3e-11 g² and 1.8e-7 dps², so a constant window still reads zero. */
    const imu_raw_t hi = raw_lsb(32767, 32767, 32767, 32767, 32767, 32767);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &hi);
    TEST_ASSERT_TRUE(s.still);                                   /* constant, however large */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.acc_var);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, s.gyr_var_max);
    const float a_g = 32767.0f / IMU_ACC_LSB_PER_G;              /* 15.99951 g, exact in float32 */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, a_g, s.mean_acc[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, 32767.0f, s.mean_graw[i]);   /* float32 ULP at 32767 is 3.9e-3 */
    }

    const imu_raw_t lo = raw_lsb(-32767, -32767, -32767, -32767, -32767, -32767);
    for (int i = 0; i < FUS_STILL_WINDOW_N; i++) (void)fus_still_push(&s, &lo);
    TEST_ASSERT_TRUE(s.still);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, s.acc_var);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a_g, s.mean_acc[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-2f, -32767.0f, s.mean_graw[i]);
    }
}

static void test_window_is_exactly_fus_still_window_n_pushes_long(void)
{
    fus_still_t s; fus_still_init(&s);
    const imu_raw_t r = raw_lsb(0, 0, 2048, 0, 0, 0);
    const int total = 5 * FUS_STILL_WINDOW_N;                    /* 1000 pushes → 5 completions */
    int completions = 0;
    int last_completion = 0;
    for (int i = 1; i <= total; i++) {
        const int rc = fus_still_push(&s, &r);
        if (rc == 1) {
            completions++;
            last_completion = i;
            TEST_ASSERT_EQUAL_INT(0, i % FUS_STILL_WINDOW_N);    /* completes only on multiples of 200 */
        } else {
            TEST_ASSERT_EQUAL_INT(0, rc);
            TEST_ASSERT_NOT_EQUAL_INT(0, i % FUS_STILL_WINDOW_N);
        }
    }
    TEST_ASSERT_EQUAL_INT(5, completions);
    TEST_ASSERT_EQUAL_INT(total, last_completion);
    TEST_ASSERT_EQUAL_UINT16(0, s.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_constant_window_completes_on_the_two_hundredth_sample);
    RUN_TEST(test_nothing_is_reported_before_the_first_window_completes);
    RUN_TEST(test_noise_variances_match_the_analytic_value_and_the_thresholds);
    RUN_TEST(test_tumbling_windows_latch_until_the_next_completion);
    RUN_TEST(test_one_spike_breaks_the_window_and_the_next_one_recovers);
    RUN_TEST(test_full_scale_constant_window_does_not_overflow);
    RUN_TEST(test_window_is_exactly_fus_still_window_n_pushes_long);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug > /dev/null && cmake --build test/build --parallel 2>&1 | grep -A2 "Undefined symbols"`
Expected: FAIL —

```
Undefined symbols for architecture arm64:
  "_fus_still_init", referenced from:
      _test_constant_window_completes_on_the_two_hundredth_sample in test_fus_still.c.o
```

(the CMake glob picked the new suite up on its own; `fus_still_init` / `fus_still_push` are declared
in `core/fus.h` but not yet defined, so the link fails. On Linux/gcc the same failure reads
`undefined reference to 'fus_still_init'`.)

- [ ] **Step 3: Implement**

`components/core/fusion/fus_still.c`:

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* Stillness detector (spec §9.2): tumbling windows of FUS_STILL_WINDOW_N samples
 * (STILL_WINDOW_S · FUSION_HZ = 200 at 100 Hz). A window is still when the variance of the accel
 * magnitude is below STILL_ACC_VAR and every gyro axis variance is below STILL_GYRO_VAR. The
 * last-completed-window fields persist until the next window completes, so stillness is reported
 * with at most one window of latency and no per-sample sliding statistics are needed.
 *
 * Why the accumulators are double (fus.h fixes the type; this is the reason): the variance is taken
 * as E[x²] − E[x]², which cancels catastrophically in float32 when the mean is far from zero. At
 * full scale one raw axis is 32767 LSB, so a squared sample reaches 1.07e9 and a 200-sample sum
 * 2.1e11; in the units accumulated here a window holds Σ|a|² up to 1.5e5 g² and Σg² up to 8.0e8
 * dps². One float32 ULP at 8.0e8 is 64 dps², 16× the 4 dps² gyro threshold, and at 1.5e5 g² it is
 * 0.0156 g², 39× the 4e-4 g² accel threshold: a float32 accumulator could not tell still from
 * moving at all. A double's 53-bit mantissa puts one ULP at 1.8e-7 dps² and 3.3e-11 g², seven
 * orders below either threshold. The per-sample cost stays one sqrt plus ~20 flops at 100 Hz. */

static void window_restart(fus_still_t *s)
{
    s->sum_amag = 0.0;
    s->sum_amag2 = 0.0;
    memset(s->sum_g, 0, sizeof s->sum_g);
    memset(s->sum_g2, 0, sizeof s->sum_g2);
    memset(s->sum_acc, 0, sizeof s->sum_acc);
    memset(s->sum_graw, 0, sizeof s->sum_graw);
    s->n = 0;
}

void fus_still_init(fus_still_t *s)
{
    memset(s, 0, sizeof *s);   /* a zeroed struct is the initialised state (fus.h) */
}

/* Closes the current window: computes the two statistics, latches the means, restarts the sums. */
static void window_close(fus_still_t *s)
{
    const double n = (double)FUS_STILL_WINDOW_N;
    const double mean_amag = s->sum_amag / n;
    double acc_var = s->sum_amag2 / n - mean_amag * mean_amag;
    if (acc_var < 0.0) acc_var = 0.0;   /* a constant window can land microscopically negative */
    double gyr_var_max = 0.0;
    for (int i = 0; i < 3; i++) {
        const double mean_g = s->sum_g[i] / n;
        double v = s->sum_g2[i] / n - mean_g * mean_g;
        if (v < 0.0) v = 0.0;
        if (v > gyr_var_max) gyr_var_max = v;
        s->mean_acc[i] = (float)(s->sum_acc[i] / n);
        s->mean_graw[i] = (float)(s->sum_graw[i] / n);
    }
    s->acc_var = (float)acc_var;
    s->gyr_var_max = (float)gyr_var_max;
    s->still = (acc_var < (double)STILL_ACC_VAR) && (gyr_var_max < (double)STILL_GYRO_VAR);
    s->have_window = true;
    window_restart(s);
}

int fus_still_push(fus_still_t *s, const imu_raw_t *raw)
{
    /* One conversion per sample: accel LSB → g, gyro LSB → dps (the bias is not removed here — the
     * mean raw gyro of a still window is what becomes the bias, §9.2). */
    const double ax = (double)raw->ax / (double)IMU_ACC_LSB_PER_G;
    const double ay = (double)raw->ay / (double)IMU_ACC_LSB_PER_G;
    const double az = (double)raw->az / (double)IMU_ACC_LSB_PER_G;
    const double amag = sqrt(ax * ax + ay * ay + az * az);
    const double graw[3] = { (double)raw->gx, (double)raw->gy, (double)raw->gz };

    s->sum_amag += amag;
    s->sum_amag2 += amag * amag;
    s->sum_acc[0] += ax;
    s->sum_acc[1] += ay;
    s->sum_acc[2] += az;
    for (int i = 0; i < 3; i++) {
        const double gd = graw[i] / (double)IMU_GYR_LSB_PER_DPS;
        s->sum_g[i] += gd;
        s->sum_g2[i] += gd * gd;
        s->sum_graw[i] += graw[i];
    }
    s->n = (uint16_t)(s->n + 1u);

    if (s->n >= FUS_STILL_WINDOW_N) {
        window_close(s);
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build --parallel 2>&1 | grep -E "error|warning"; ctest --test-dir test/build --output-on-failure 2>&1 | tail -3`
Expected: no `error` or `warning` line, and

```
100% tests passed out of 22
```

(`test_fus_still` is the 22nd executable — 21 after session 2.2 Task 1 — and reports `7 Tests 0 Failures 0 Ignored`.)

Parity check (both compilers, strict flags with no `-Wno-error` relaxation, to confirm the explicit
casts are complete):

Run: `for CC in clang gcc-16; do $CC -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Icomponents/core/include -Itest/unity/src -c components/core/fusion/fus_still.c -o /dev/null && $CC -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Icomponents/core/include -Itest/unity/src -c test/test_fus_still.c -o /dev/null && echo "$CC ok"; done`
Expected:

```
clang ok
gcc-16 ok
```

- [ ] **Step 5: Hygiene and commit**

Run: `git diff --check`
Expected: no output.

```bash
git add components/core/fusion/fus_still.c test/test_fus_still.c
git commit -m "feat(core): stillness detector over tumbling 2 s windows (fusion)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

**Header change needed:** none. `core/fus.h` as landed by session 2.2 Task 1 is sufficient and is
used verbatim.

---

### Task 3: Orientation rows (`components/core/fusion/fus_orient.c`) [parallel group 2]

**Files:**
- Create: `components/core/fusion/fus_orient.c`, `test/test_fus_orient.c`
- Modify: none — the host test list is a `test/test_*.c` glob from this session's Task 1 and
  `test_apps/core_selftest` globs the same files, so the suite needs no CMake edit and is also built
  for the ESP32 (nothing in either file is host-only).

**Interfaces:**
- Consumes (Task 1, unchanged): `core/fus.h` — `fus_calib_t`, `void fus_calib_defaults(fus_calib_t *c)`,
  `bool fus_calib_valid(const fus_calib_t *c)`, `void fus_rotate(const float r[9], const float b[3], float v[3])`,
  `FUS_CALIB_VERSION`; `core/consts.h` — `FUS_ORIENT_MIN_G` (0.5), `FUS_ORIENT_MAX_G` (1.5); `<math.h>`.
  `fus_rotate` and `fus_calib_valid` are defined in `fusion/fus.c`; this task does not redefine them.
- Produces (bodies for two declarations that already exist in `core/fus.h`; no new header):
  - `int fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3])` — 0 on success, −1 with the
    calibration byte-untouched when `|mean_acc_g|` is not finite or falls outside
    `[FUS_ORIENT_MIN_G, FUS_ORIENT_MAX_G]`.
  - `int fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3])` — 0 on success, −1 with the
    calibration byte-untouched when `!orient_ok` or the horizontal part of `sum_ah` is shorter than
    `FUS_FWD_MIN_NORM` (1e-6, a `static const float` in this file).

**Geometry (spec §9.2 "Orientation").** `R`'s rows `x, y, z` are the vehicle axes (X forward, Y left,
Z up) expressed in body coordinates, so `vehicle = R · body` and the triad is right-handed
(`y = z × x`, `x = y × z`).

- `fus_orient_from_gravity`: a still upright vehicle reads +1 g along vehicle up, so `z = mean/|mean|`.
  The forward row is not yet known, so the function completes the triad provisionally: it takes the
  body axis `e_k` least aligned with `z` (smallest `|z[k]|`, ties to the lowest index — that projection
  has length `sqrt(1 − z[k]²) ≥ sqrt(2/3)`, so it is never degenerate), projects it,
  `x = normalize(e_k − (e_k·z)z)`, then `y = z × x` and `x = y × z`. It sets `orient_ok = 1` and clears
  `forward_ok` (a forward row learned against the old `z` means nothing against a new one), and leaves
  `gbias`, `gbias_temp_c100`, `bias_ok` and `version` alone.
- `fus_orient_set_forward`: `h = sum_ah − (sum_ah·z)z` drops whatever leaked onto vehicle up,
  `x = h/|h|`, `y = z × x`, `x = y × z`, both renormalised; `forward_ok = 1`. The `z` row is rewritten
  with the value it already had.
- After either call the calibration satisfies `fus_calib_valid` (the tests assert this).

- [ ] **Step 1: Write the failing test**

`test/test_fus_orient.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Tolerance for the analytic row values. Each row is a handful of float32 multiplies, one divide and
 * one sqrtf away from its exact value, i.e. a few ulps of 1.0 (~1e-7); 1e-6 leaves a clear margin. */
#define VEC_TOL 1e-6f

/* <math.h> M_PI is an extension, not C11, and this suite also builds for the ESP32. */
static const float PI_F = 3.14159265358979323846f;

static void cross3(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float det3(const float r[9])
{
    return r[0] * (r[4] * r[8] - r[5] * r[7])
         - r[1] * (r[3] * r[8] - r[5] * r[6])
         + r[2] * (r[3] * r[7] - r[4] * r[6]);
}

static void assert_row(const fus_calib_t *c, int row, float ex, float ey, float ez)
{
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ex, c->r[3 * row + 0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ey, c->r[3 * row + 1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, ez, c->r[3 * row + 2]);
}

/* Unit rows, zero pairwise dots and y = z × x: a right-handed orthonormal triad. */
static void assert_orthonormal(const fus_calib_t *c)
{
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, ri[0] * ri[0] + ri[1] * ri[1] + ri[2] * ri[2]);
        for (int j = i + 1; j < 3; j++) {
            const float *rj = c->r + 3 * j;
            TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, ri[0] * rj[0] + ri[1] * rj[1] + ri[2] * rj[2]);
        }
    }
    float y[3];
    cross3(y, c->r + 6, c->r + 0);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[0], c->r[3]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[1], c->r[4]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[2], c->r[5]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, det3(c->r));
}

static void test_upright_capture_gives_the_identity_triad(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float mean[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    /* body X and Y are equally unaligned with z; the tie goes to the lowest index, so x is body X */
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 1, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
    float v[3];
    fus_rotate(c.r, mean, v);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_tilted_gravity_lands_entirely_on_vehicle_z(void)
{
    const float ang = 20.0f * PI_F / 180.0f;           /* IMU pitched 20° nose-up about body Y */
    const float mags[2] = { 1.0f, 0.98f };             /* unit mean, then a 0.98 g bench reading */
    for (int i = 0; i < 2; i++) {
        const float mean[3] = { mags[i] * sinf(ang), 0.0f, mags[i] * cosf(ang) };
        fus_calib_t c; fus_calib_defaults(&c);
        TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
        assert_row(&c, 2, sinf(ang), 0.0f, cosf(ang)); /* z is the normalised mean */
        TEST_ASSERT_TRUE(fus_calib_valid(&c));
        assert_orthonormal(&c);
        /* body Y is the axis least aligned with z here, so the provisional forward is body Y */
        assert_row(&c, 0, 0.0f, 1.0f, 0.0f);
        float v[3];
        fus_rotate(c.r, mean, v);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
        TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, mags[i], v[2]);
    }
}

static void test_imu_on_its_side_stays_right_handed(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float mean[3] = { 0.0f, 1.0f, 0.0f };        /* vehicle up is body +Y */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    assert_row(&c, 2, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);               /* body X least aligned (tie with Z, lowest index wins) */
    assert_row(&c, 1, 0.0f, 0.0f, -1.0f);              /* y = z × x = (0,1,0) × (1,0,0) */
    float y[3];
    cross3(y, c.r + 6, c.r + 0);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[0], c.r[3]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[1], c.r[4]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, y[2], c.r[5]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, det3(c.r));  /* +1, not -1: a rotation, not a reflection */
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
}

static void test_gravity_outside_the_window_or_not_finite_is_rejected(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 3.0f; c.bias_ok = 1;
    fus_calib_t before; memcpy(&before, &c, sizeof before);   /* memcpy so padding bytes compare too */

    const float low[3] = { 0.0f, 0.0f, 0.3f };         /* 0.3 g < FUS_ORIENT_MIN_G */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, low));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float high[3] = { 0.0f, 0.0f, 1.6f };        /* 1.6 g > FUS_ORIENT_MAX_G */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, high));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float bad[3] = { 0.0f, NAN, 1.0f };          /* NaN makes |mean| NaN, which fails both bounds */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_from_gravity(&c, bad));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
}

static void test_forward_from_a_horizontal_sum_gives_the_ninety_degree_mount(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.0f, -5.0f, 0.0f };        /* accumulated a_h: the vehicle accelerates along body -Y */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 0.0f, -1.0f, 0.0f);              /* the 90° mount of test_fus.c */
    assert_row(&c, 1, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
    const float b[3] = { 0.0f, -0.3f, 1.0f };          /* 0.3 g along body -Y = forward, 1 g up */
    float v[3];
    fus_rotate(c.r, b, v);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.3f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_forward_projects_out_the_vertical_component(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.7f, 0.0f, 0.7f };         /* half of it leaked onto vehicle up */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 1.0f, 0.0f, 0.0f);
    assert_row(&c, 1, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 2, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
}

static void test_forward_on_a_tilted_z_keeps_the_captured_up(void)
{
    const float ang = 20.0f * PI_F / 180.0f;
    const float mean[3] = { sinf(ang), 0.0f, cosf(ang) };
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, mean));
    const float sum[3] = { 0.0f, 1.0f, 0.0f };         /* already perpendicular to z: nothing to project out */
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    assert_row(&c, 0, 0.0f, 1.0f, 0.0f);
    assert_row(&c, 2, sinf(ang), 0.0f, cosf(ang));     /* the captured z row is untouched */
    assert_orthonormal(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    float v[3];
    fus_rotate(c.r, mean, v);                          /* the capture gravity still lands on vehicle Z */
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 0.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(VEC_TOL, 1.0f, v[2]);
}

static void test_forward_is_rejected_without_orientation_or_a_direction(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    fus_calib_t before; memcpy(&before, &c, sizeof before);
    const float sum[3] = { 1.0f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, sum));   /* orient_ok = 0 */
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));

    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    memcpy(&before, &c, sizeof before);
    const float vertical[3] = { 0.0f, 0.0f, 3.0f };    /* parallel to z: the projection is exactly zero */
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, vertical));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(-1, fus_orient_set_forward(&c, zero));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &c, sizeof c));
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);
}

static void test_recapture_clears_forward_and_keeps_the_bias(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 11.0f; c.gbias[1] = -3.0f; c.gbias[2] = 0.5f;
    c.gbias_temp_c100 = 2712; c.bias_ok = 1;
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, up));
    const float sum[3] = { 0.0f, -5.0f, 0.0f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_set_forward(&c, sum));
    TEST_ASSERT_EQUAL_UINT8(1, c.forward_ok);

    /* Re-capture on a 30° roll about body X: |mean| = 1 g, so it is accepted. */
    const float tilt[3] = { 0.0f, 0.5f, 0.8660254f };
    TEST_ASSERT_EQUAL_INT(0, fus_orient_from_gravity(&c, tilt));
    assert_row(&c, 2, 0.0f, 0.5f, 0.8660254f);
    TEST_ASSERT_EQUAL_UINT8(1, c.orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok);          /* the learned forward row no longer applies */
    TEST_ASSERT_EQUAL_FLOAT(11.0f, c.gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, c.gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, c.gbias[2]);
    TEST_ASSERT_EQUAL_INT16(2712, c.gbias_temp_c100);
    TEST_ASSERT_EQUAL_UINT8(1, c.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    assert_orthonormal(&c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_upright_capture_gives_the_identity_triad);
    RUN_TEST(test_tilted_gravity_lands_entirely_on_vehicle_z);
    RUN_TEST(test_imu_on_its_side_stays_right_handed);
    RUN_TEST(test_gravity_outside_the_window_or_not_finite_is_rejected);
    RUN_TEST(test_forward_from_a_horizontal_sum_gives_the_ninety_degree_mount);
    RUN_TEST(test_forward_projects_out_the_vertical_component);
    RUN_TEST(test_forward_on_a_tilted_z_keeps_the_captured_up);
    RUN_TEST(test_forward_is_rejected_without_orientation_or_a_direction);
    RUN_TEST(test_recapture_clears_forward_and_keeps_the_bias);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it — expected FAIL**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build test/build --target test_fus_orient 2>&1 | tail -4`
Expected: a link failure, because `core/fus.h` declares both functions but nothing defines them yet:

```
Undefined symbols for architecture arm64:
  "_fus_orient_from_gravity", referenced from:
      _test_upright_capture_gives_the_identity_triad in test_fus_orient.c.o
  ...
ld: symbol(s) not found for architecture arm64
```

(On the Linux parity build the same failure reads `undefined reference to 'fus_orient_from_gravity'`.)

- [ ] **Step 3: Write the implementation**

`components/core/fusion/fus_orient.c`:

```c
#include "core/fus.h"
#include <math.h>

/* Orientation rows of the calibration matrix (spec §9.2 "Orientation").
 *
 * R's rows x, y, z are the vehicle axes (X forward, Y left, Z up) written in body coordinates, so
 * vehicle = R · body and the triad is right-handed: y = z × x and x = y × z. This file owns the two
 * ways a row is learned: z from a still upright gravity mean, x from the accumulated horizontal
 * acceleration of a straight-line run. fus_rotate and fus_calib_valid live in fusion/fus.c. */

/* Shortest horizontal accumulator (in the caller's units, g·samples) that still points somewhere.
 * Below this the projection is float rounding noise, not a direction, so forward is refused. */
static const float FUS_FWD_MIN_NORM = 1e-6f;

static float v_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float v_norm(const float a[3])
{
    return sqrtf(v_dot(a, a));
}

static void v_cross(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* out = a / |a|. Every caller has already checked that |a| is finite and far enough from zero. */
static void v_normalize(float out[3], const float a[3])
{
    const float n = v_norm(a);
    out[0] = a[0] / n;
    out[1] = a[1] / n;
    out[2] = a[2] / n;
}

/* Given a unit z and an x already perpendicular to it, build the right-handed completion:
 * y = z × x, then x = y × z. Both crosses are renormalised because float rounding leaves them a few
 * ulps off unit length and fus_calib_valid measures the rows against FUS_ORTHO_TOL. */
static void complete_triad(float x[3], float y[3], const float z[3])
{
    float t[3];
    v_cross(t, z, x);
    v_normalize(y, t);
    v_cross(t, y, z);
    v_normalize(x, t);
}

/* Rows of R, row-major: r[0..2] = x, r[3..5] = y, r[6..8] = z. */
static void set_rows(fus_calib_t *c, const float x[3], const float y[3], const float z[3])
{
    c->r[0] = x[0]; c->r[1] = x[1]; c->r[2] = x[2];
    c->r[3] = y[0]; c->r[4] = y[1]; c->r[5] = y[2];
    c->r[6] = z[0]; c->r[7] = z[1]; c->r[8] = z[2];
}

int fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3])
{
    /* A still, upright vehicle reads +1 g along vehicle up. A mean outside the window (or a
     * non-finite one) came from a moving or broken capture, so nothing is written. */
    const float m = v_norm(mean_acc_g);
    if (!isfinite(m) || m < FUS_ORIENT_MIN_G || m > FUS_ORIENT_MAX_G) return -1;

    float z[3];
    v_normalize(z, mean_acc_g);

    /* Provisional forward: the body axis least aligned with z, ties to the lowest index. Its
     * projection has length sqrt(1 - z[k]^2) >= sqrt(2/3), so the normalise below is always safe. */
    int k = 0;
    for (int i = 1; i < 3; i++) {
        if (fabsf(z[i]) < fabsf(z[k])) k = i;
    }
    float x0[3];
    for (int i = 0; i < 3; i++) x0[i] = ((i == k) ? 1.0f : 0.0f) - z[k] * z[i];   /* e_k · z = z[k] */

    float x[3], y[3];
    v_normalize(x, x0);
    complete_triad(x, y, z);

    set_rows(c, x, y, z);
    c->orient_ok = 1;
    c->forward_ok = 0;   /* the old forward row means nothing against a new z (§9.2) */
    return 0;
}

int fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3])
{
    if (!c->orient_ok) return -1;                /* no z row: nothing to project against */

    const float z[3] = { c->r[6], c->r[7], c->r[8] };
    const float d = v_dot(sum_ah, z);
    float h[3];
    for (int i = 0; i < 3; i++) h[i] = sum_ah[i] - d * z[i];   /* drop whatever leaked onto vehicle up */

    const float hn = v_norm(h);
    if (!isfinite(hn) || hn < FUS_FWD_MIN_NORM) return -1;

    float x[3], y[3];
    v_normalize(x, h);
    complete_triad(x, y, z);

    set_rows(c, x, y, z);
    c->forward_ok = 1;
    return 0;
}
```

- [ ] **Step 4: Run — expected PASS**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1 && cmake --build test/build --parallel 2>&1 | grep -E "error|warning" | head; ctest --test-dir test/build --output-on-failure 2>&1 | tail -3`
Expected: no `error`/`warning` line from the build, and

```
100% tests passed out of 22
```

(`test_fus_orient` is the 22nd executable; the new suite on its own prints `9 Tests 0 Failures 0 Ignored / OK`.)

- [ ] **Step 5: Hygiene and commit**

Run: `git diff --check`
Expected: no output.

```bash
git add components/core/fusion/fus_orient.c test/test_fus_orient.c
git commit -m "feat(core): orientation rows from gravity and learned forward axis (fusion)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 4: Forward-axis learning tracker (`components/core/fusion/fus_fwd.c`) [parallel group 2]

**Files:**
- Create: `components/core/fusion/fus_fwd.c`
- Test: `test/test_fus_fwd.c`
- Modify: nothing (`core` glob-compiles `components/core/**/*.c`; `test/CMakeLists.txt` globs
  `test/test_*.c` from this session's Task 1, and `test_apps/core_selftest` globs the same files, so the
  suite also runs on the ESP32)

**Interfaces:**
- Consumes: `core/fus.h` (this session's Task 1, verbatim — **not** edited here): `fus_fwd_t`,
  `FUS_FWD_MIN_SAMPLES` (= `FWD_LEARN_MIN_S * FUSION_HZ` = 100); `core/consts.h`:
  `FWD_LEARN_ACC_MPS2` (1.5), `FWD_LEARN_MAX_YAW_DPS` (2.0), `FWD_LEARN_WINDOWS` (3),
  `FWD_LEARN_MIN_S` (1), `FUSION_HZ` (100). `<math.h>` (`fabsf`) and `<string.h>` (`memset`) only —
  no IDF header, no allocation, no global state, because this file is compiled into the firmware.
- Produces (the four `fus_fwd_*` symbols declared in `core/fus.h`):
  `void fus_fwd_init(fus_fwd_t *w)`;
  `void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps)`;
  `int  fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3])`;
  `bool fus_fwd_ready(const fus_fwd_t *w)`.
- Out of scope: the calibration itself. This file never touches `fus_calib_t`. Task 5 wires
  `fus_calib_forward_step` to call `fus_fwd_on_fix`/`fus_fwd_on_sample` and, on the single `1`,
  finalises the forward row from `w.sum` through `fus_orient_set_forward`.

**Model (binding, spec §9.2).** A *run* is a maximal stretch of fusion samples over which the fix
condition holds. Per fix, `cond = (|yaw_dps| < FWD_LEARN_MAX_YAW_DPS) && (gps_acc_mps2 >
FWD_LEARN_ACC_MPS2)` — both comparisons strict, so braking (negative `gps_acc_mps2`) never qualifies.
When `cond` is false the current run ends (`run_samples = 0`, `run_sum = 0`, `counted = false`); when
it stays true the run simply continues across fixes. Per sample with `cond`: `a_h = a - (a.z)z` (g,
body frame, `z` the calibration's unit Z row), `run_samples++`. While the run is not yet `counted`,
`a_h` lands in `run_sum`; on the sample where `run_samples == FUS_FWD_MIN_SAMPLES` the run is counted
(`counted = true`, `sum += run_sum`, `run_sum = 0`, `windows++` saturating at 255) and the call
returns 1 **iff** `windows == FWD_LEARN_WINDOWS`. Once counted, later samples of the same run add
straight to `sum`. Because `windows` only ever grows, the 1 is returned exactly once per tracker
lifetime: a fourth or later run is still counted and still accumulated, but reports 0. `fus_fwd_ready`
is `windows >= FWD_LEARN_WINDOWS`. Consequence the tests pin down: **every** sample of a counted run
contributes to `sum` (the ones before the count via `run_sum`, the ones after directly), and **no**
sample of a run that ended short of `FUS_FWD_MIN_SAMPLES` contributes at all.

- [ ] **Step 1: Write the failing test**

`test/test_fus_fwd.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include "core/consts.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Vehicle Z row for an upright, axis-aligned mount: a_h is then just the X/Y part of the accel. */
static const float Z_UP[3] = { 0.0f, 0.0f, 1.0f };

/* Pushes n identical samples; returns how many of them reported "forward learned". */
static int push_n(fus_fwd_t *w, const float acc[3], const float z[3], int n)
{
    int ones = 0;
    for (int i = 0; i < n; i++) ones += fus_fwd_on_sample(w, acc, z);
    return ones;
}

/* One complete run of n qualifying samples, bracketed by the fixes that open and close it. */
static void run_of(fus_fwd_t *w, const float acc[3], int n, int *ones)
{
    fus_fwd_on_fix(w, 2.0f, 0.0f);
    *ones += push_n(w, acc, Z_UP, n);
    fus_fwd_on_fix(w, 0.0f, 0.0f);
}

static void test_fix_condition_needs_straight_line_acceleration(void)
{
    /* The boundary cases below are only meaningful at the spec's thresholds. */
    TEST_ASSERT_EQUAL_FLOAT(1.5f, FWD_LEARN_ACC_MPS2);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, FWD_LEARN_MAX_YAW_DPS);

    fus_fwd_t w; fus_fwd_init(&w);
    TEST_ASSERT_FALSE(w.cond);
    fus_fwd_on_fix(&w, 1.6f, 1.9f);    TEST_ASSERT_TRUE(w.cond);    /* 1.6 > 1.5 and |1.9| < 2.0 */
    fus_fwd_on_fix(&w, 1.5f, 0.0f);    TEST_ASSERT_FALSE(w.cond);   /* acceleration is a strict > */
    fus_fwd_on_fix(&w, 1.6f, 2.0f);    TEST_ASSERT_FALSE(w.cond);   /* yaw rate is a strict < */
    fus_fwd_on_fix(&w, 1.6f, -1.9f);   TEST_ASSERT_TRUE(w.cond);    /* the yaw test is on |yaw|, either way */
    fus_fwd_on_fix(&w, -3.0f, 0.0f);   TEST_ASSERT_FALSE(w.cond);   /* braking: §9.2 learns from acceleration only */
}

static void test_samples_outside_the_condition_do_nothing(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.4f, 0.0f, 1.0f };
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 500));      /* 5 s of acceleration with no qualifying fix */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);
    TEST_ASSERT_FALSE(w.counted);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
    }
    TEST_ASSERT_FALSE(fus_fwd_ready(&w));
}

static void test_a_run_shorter_than_the_minimum_is_discarded(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.2f, 0.0f, 1.0f };                 /* a·z = 1 g, so a_h = (0.2, 0, 0) g */
    fus_fwd_on_fix(&w, 2.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, FUS_FWD_MIN_SAMPLES - 1));
    TEST_ASSERT_EQUAL_UINT32(FUS_FWD_MIN_SAMPLES - 1, w.run_samples);
    /* 99 × 0.2 g; 1e-6 covers 99 × (0.2f − 0.2) ≈ 3.0e-7 of float quantisation of the input */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 19.8, w.run_sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[1]);             /* exact: 0 − 1·0 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[2]);             /* exact: 1 − 1·1 */
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);

    fus_fwd_on_fix(&w, 0.0f, 0.0f);                          /* the condition drops: the run is thrown away */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    TEST_ASSERT_FALSE(w.counted);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
        TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
    }
}

static void test_a_run_counts_on_its_minimum_sample_and_keeps_accumulating(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.2f, 0.0f, 1.0f };
    fus_fwd_on_fix(&w, 1.6f, 0.5f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, FUS_FWD_MIN_SAMPLES - 1));
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_INT(0, fus_fwd_on_sample(&w, a, Z_UP));   /* the 100th counts, but 1 < FWD_LEARN_WINDOWS */
    TEST_ASSERT_TRUE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&w));
    /* the whole run_sum moved into sum: 100 × 0.2 g, 1e-6 covers 100 × (0.2f − 0.2) ≈ 3.0e-7 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 20.0, w.sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[1]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[2]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);

    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 50));       /* samples 101..150 add straight to sum */
    TEST_ASSERT_EQUAL_UINT32(150, w.run_samples);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 30.0, w.sum[0]);         /* 150 × 0.2 g; quantisation ≈ 4.5e-7 */
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.run_sum[i]);
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);                   /* one run, counted once */
}

static void test_three_counted_runs_report_exactly_once(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.15f, -0.05f, 1.0f };              /* a_h = (0.15, −0.05, 0) g */
    const int len[3] = { 120, 100, 250 };
    int ones = 0;
    for (int r = 0; r < 3; r++) {
        fus_fwd_on_fix(&w, 1.8f, 0.2f);                      /* run r opens */
        for (int i = 0; i < len[r]; i++) {
            if (fus_fwd_on_sample(&w, a, Z_UP)) {
                ones++;
                TEST_ASSERT_EQUAL_INT(2, r);                             /* only the third run reports */
                TEST_ASSERT_EQUAL_INT(FUS_FWD_MIN_SAMPLES, i + 1);       /* on its 100th sample */
            }
        }
        fus_fwd_on_fix(&w, 0.0f, 0.0f);                      /* run r closes */
    }
    TEST_ASSERT_EQUAL_INT(1, ones);
    TEST_ASSERT_EQUAL_UINT8(3, w.windows);
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    /* Every sample of a counted run contributes, before and after the count: 120 + 100 + 250 = 470.
     * A run that had ended before FUS_FWD_MIN_SAMPLES would have contributed none of its samples. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 470.0 * 0.15, w.sum[0]);   /* 1e-5 covers 470 × (0.15f − 0.15) = 2.8e-6 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 470.0 * (double)a[0], w.sum[0]);  /* against the exact float input the sum is exact */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 470.0 * -0.05, w.sum[1]);  /* 470 × (0.05f − 0.05) = 3.5e-7 */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[2]);                   /* exact: every a_h.z is 1 − 1·1 */
}

static void test_the_gravity_component_is_removed_for_a_tilted_z(void)
{
    /* Mount tilted 20° about body Y: z is the unit vehicle-up row, t a unit vector perpendicular to it. */
    const double ang = 20.0 * 3.14159265358979 / 180.0;   /* M_PI is not C11 */
    const float z[3] = { (float)sin(ang), 0.0f, (float)cos(ang) };
    const float t[3] = { (float)cos(ang), 0.0f, (float)-sin(ang) };
    const float a[3] = { z[0] + 0.2f * t[0], z[1] + 0.2f * t[1], z[2] + 0.2f * t[2] };   /* 1 g down + 0.2 g forward */

    fus_fwd_t w; fus_fwd_init(&w);
    fus_fwd_on_fix(&w, 2.5f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, z, FUS_FWD_MIN_SAMPLES));
    TEST_ASSERT_EQUAL_UINT8(1, w.windows);
    /* sum = 100 × 0.2 × t = 20 t: the 1 g along z is removed whatever the tilt.
     * 1e-5 covers the measured 1.3e-6, which is 100 samples × ~1.3e-8 of float rounding in a·z and a − (a·z)z. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 20.0 * cos(ang), w.sum[0]);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[1]);                 /* exact: a.y and z.y are both 0 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, -20.0 * sin(ang), w.sum[2]);
    /* the residual along z is what "the vertical component is removed" means, to the same bound */
    const double along_z = w.sum[0] * (double)z[0] + w.sum[1] * (double)z[1] + w.sum[2] * (double)z[2];
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 0.0, along_z);
    /* and the full 20 g·samples survive the projection: nothing but gravity was subtracted */
    const double mag = sqrt(w.sum[0] * w.sum[0] + w.sum[1] * w.sum[1] + w.sum[2] * w.sum[2]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 20.0, mag);
}

static void test_a_fourth_run_counts_but_reports_nothing(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.25f, 0.0f, 1.0f };                /* 0.25 is exact in float: no quantisation error */
    int ones = 0;
    for (int r = 0; r < 3; r++) run_of(&w, a, FUS_FWD_MIN_SAMPLES, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));

    run_of(&w, a, 200, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);                          /* the fourth run reports nothing */
    TEST_ASSERT_EQUAL_UINT8(4, w.windows);                   /* but it is counted and accumulated */
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    TEST_ASSERT_EQUAL_DOUBLE(500.0 * 0.25, w.sum[0]);        /* 300 + 200 samples, all exactly representable */
}

static void test_an_interrupted_run_restarts_from_zero(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.3f, 0.0f, 1.0f };
    fus_fwd_on_fix(&w, 2.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 60));
    fus_fwd_on_fix(&w, 1.0f, 0.0f);                          /* below FWD_LEARN_ACC_MPS2: the run ends */
    TEST_ASSERT_EQUAL_UINT32(0, w.run_samples);
    fus_fwd_on_fix(&w, 2.0f, 0.0f);                          /* a new run starts at 0, not at 60 */
    TEST_ASSERT_EQUAL_INT(0, push_n(&w, a, Z_UP, 60));
    TEST_ASSERT_EQUAL_UINT32(60, w.run_samples);
    TEST_ASSERT_FALSE(w.counted);
    TEST_ASSERT_EQUAL_UINT8(0, w.windows);                   /* 120 samples, neither run reached 100 */
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_DOUBLE(0.0, w.sum[i]);
    /* only the live run is held; 1e-6 covers 60 × (0.3f − 0.3) ≈ 7.2e-7 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 60.0 * 0.3, w.run_sum[0]);
}

static void test_the_window_counter_saturates(void)
{
    fus_fwd_t w; fus_fwd_init(&w);
    const float a[3] = { 0.5f, 0.0f, 1.0f };                 /* exact in float */
    int ones = 0;
    for (int r = 0; r < 300; r++) run_of(&w, a, FUS_FWD_MIN_SAMPLES, &ones);
    TEST_ASSERT_EQUAL_INT(1, ones);                          /* still exactly one report, on run 3 */
    TEST_ASSERT_EQUAL_UINT8(255, w.windows);                 /* uint8_t saturates instead of wrapping to 0 */
    TEST_ASSERT_TRUE(fus_fwd_ready(&w));
    TEST_ASSERT_EQUAL_DOUBLE(300.0 * FUS_FWD_MIN_SAMPLES * 0.5, w.sum[0]);   /* every counted sample is in */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fix_condition_needs_straight_line_acceleration);
    RUN_TEST(test_samples_outside_the_condition_do_nothing);
    RUN_TEST(test_a_run_shorter_than_the_minimum_is_discarded);
    RUN_TEST(test_a_run_counts_on_its_minimum_sample_and_keeps_accumulating);
    RUN_TEST(test_three_counted_runs_report_exactly_once);
    RUN_TEST(test_the_gravity_component_is_removed_for_a_tilted_z);
    RUN_TEST(test_a_fourth_run_counts_but_reports_nothing);
    RUN_TEST(test_an_interrupted_run_restarts_from_zero);
    RUN_TEST(test_the_window_counter_saturates);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug > /dev/null && cmake --build test/build --target test_fus_fwd 2>&1 | tail -5`
Expected: a link failure, because the header declares the four functions and nothing defines them:

```
Undefined symbols for architecture arm64:
  "_fus_fwd_init", referenced from:
      _test_fix_condition_needs_straight_line_acceleration in test_fus_fwd.c.o
ld: symbol(s) not found for architecture arm64
```

(`_fus_fwd_on_fix`, `_fus_fwd_on_sample` and `_fus_fwd_ready` are listed as well; on Linux the same
failure reads `undefined reference to 'fus_fwd_init'`.)

- [ ] **Step 3: Write the implementation**

`components/core/fusion/fus_fwd.c`:

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* Forward-axis learning window tracker (spec §9.2).
 *
 * The pipeline calls fus_fwd_on_fix once per GPS fix with the fix-to-fix longitudinal acceleration and
 * the yaw rate, and fus_fwd_on_sample once per fusion step while the Z row is known. A *run* is a maximal
 * stretch of samples over which the fix condition holds; it counts as one learning window the moment it
 * reaches FUS_FWD_MIN_SAMPLES (FWD_LEARN_MIN_S seconds at FUSION_HZ) and keeps accumulating until the
 * condition drops. Only counted runs contribute to sum, so a short burst of acceleration that ends before
 * FWD_LEARN_MIN_S is discarded instead of biasing the forward axis.
 *
 * All state is the caller's fus_fwd_t and there is no allocation: this file is built for the ESP32 too. */

#define FUS_FWD_MAX_WINDOWS 255   /* fus_fwd_t.windows is uint8_t: the count saturates instead of wrapping */

void fus_fwd_init(fus_fwd_t *w)
{
    memset(w, 0, sizeof *w);      /* a zeroed tracker is a valid initialised state (fus.h) */
}

/* Ends the current run: its samples are already in sum if it was counted, and are dropped otherwise. */
static void run_reset(fus_fwd_t *w)
{
    w->run_samples = 0;
    w->run_sum[0] = 0.0; w->run_sum[1] = 0.0; w->run_sum[2] = 0.0;
    w->counted = false;
}

void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps)
{
    /* §9.2 learns only from near-straight acceleration, so that a_h points along the forward axis. Both
     * comparisons are strict, and braking (gps_acc_mps2 <= 0) can never qualify. */
    w->cond = (fabsf(yaw_dps) < FWD_LEARN_MAX_YAW_DPS) && (gps_acc_mps2 > FWD_LEARN_ACC_MPS2);
    if (!w->cond) run_reset(w);
}

int fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3])
{
    if (!w->cond) return 0;

    /* a_h = a − (a·z)z: the horizontal (gravity-free) part of the specific force, in g, body frame. */
    const float az = acc_g[0] * z[0] + acc_g[1] * z[1] + acc_g[2] * z[2];
    const float ah[3] = { acc_g[0] - az * z[0], acc_g[1] - az * z[1], acc_g[2] - az * z[2] };

    w->run_samples++;
    if (w->counted) {                       /* run already counted: later samples go straight to sum */
        for (int i = 0; i < 3; i++) w->sum[i] += (double)ah[i];
        return 0;
    }
    for (int i = 0; i < 3; i++) w->run_sum[i] += (double)ah[i];
    if (w->run_samples != (uint32_t)FUS_FWD_MIN_SAMPLES) return 0;

    w->counted = true;
    for (int i = 0; i < 3; i++) { w->sum[i] += w->run_sum[i]; w->run_sum[i] = 0.0; }
    if (w->windows < FUS_FWD_MAX_WINDOWS) w->windows = (uint8_t)(w->windows + 1);
    /* windows only ever grows, so it equals FWD_LEARN_WINDOWS on exactly one call per tracker: later
     * runs still count (and still accumulate) but report nothing. */
    return (w->windows == FWD_LEARN_WINDOWS) ? 1 : 0;
}

bool fus_fwd_ready(const fus_fwd_t *w)
{
    return w->windows >= FWD_LEARN_WINDOWS;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1 && cmake --build test/build --parallel 2>&1 | grep -ciE "warning|error" ; ctest --test-dir test/build --output-on-failure 2>&1 | grep -E "test_fus_fwd|tests passed"`
Expected: `0` warnings/errors, then

```
      Start  6: test_fus_fwd
 6/22 Test  #6: test_fus_fwd .....................   Passed    0.04 sec
100% tests passed out of 22
```

`./test/build/test_fus_fwd` on its own prints `9 Tests 0 Failures 0 Ignored / OK`. The suite runs
under ASan/UBSan (`-fno-sanitize-recover=undefined`), so a green run also means no undefined
behaviour was executed — the saturation test alone makes 30 000 `fus_fwd_on_sample` calls.

- [ ] **Step 5: Commit**

```bash
git add components/core/fusion/fus_fwd.c test/test_fus_fwd.c
git commit -m "feat(core): forward-axis learning window tracker (fusion)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 5: Wire stillness, gyro bias, orientation capture and forward learning into `fus_step` (serial)

**Files:**
- Create: none.
- Modify: `components/core/fusion/fus.c` (the block in Step 3 replaces Task 1's skeleton in full),
  `test/test_fus.c` (Task 1's seven cases are kept byte-for-byte; five integration cases are
  appended), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§4.8, one table row).
- No CMake edit: the host test list and the `test_apps/core_selftest` wrapper list are both globs
  over `test/test_*.c` (Task 1 Step 5), so `test_fus.c` also has to keep compiling for the ESP32.
  The appended cases therefore include nothing beyond the headers Task 1 already includes
  (`unity.h`, `core/fus.h`, `<math.h>`, `<string.h>`), keep every helper `static`, use no host-only
  header, allocate nothing, and put no array bigger than three floats on the stack.
- No plan edit: this session's Task 1 introduces its `fus.c` block as "this session's Task 5 replaces
  the four bottom functions and extends `fus_step`" and its `test_fus.c` block as the seven cases
  this task appends to. The two blocks below are therefore the plan's byte-identical restatements of
  those files from this commit on; Task 1's blocks stay as the record of what Task 1 landed.

**Interfaces:**
- Consumes (declared by Task 1 in `core/fus.h`, which this task MUST NOT change):
  - Task 2 — `fus_still_t`; `void fus_still_init(fus_still_t *s)`;
    `int fus_still_push(fus_still_t *s, const imu_raw_t *raw)`; the last-window fields
    `still`, `mean_acc[3]`, `mean_graw[3]`.
  - Task 3 — `int fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3])`;
    `int fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3])`.
  - Task 4 — `fus_fwd_t`; `void fus_fwd_init(fus_fwd_t *w)`;
    `void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps)`;
    `int fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3])`;
    `bool fus_fwd_ready(const fus_fwd_t *w)`; the fields `sum[3]` and `windows`.
  - `core/consts.h` — `FUSION_HZ`, `G_MPS2`, `IMU_ACC_LSB_PER_G`, `IMU_GYR_LSB_PER_DPS`,
    `FWD_LEARN_ACC_MPS2`, `FWD_LEARN_MAX_YAW_DPS`, `FWD_LEARN_WINDOWS`, `STILL_ACC_VAR`,
    `STILL_GYRO_VAR`; `core/fus.h` — `FUS_STILL_WINDOW_N`, `FUS_FWD_MIN_SAMPLES`,
    `FUS_CALIB_F_ORIENT`, `FUS_CALIB_F_FORWARD`.
- Produces (bodies only; every signature already exists in `core/fus.h`):
  - `int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)` — extended with the stillness
    push, the `FUS_STILL` flag and the forward-learning block.
  - `bool fus_is_still(const fus_t *f)` — the last completed stillness window's verdict.
  - `void fus_gyro_bias_update(fus_t *f)` — no-op unless still; otherwise `gbias` = that window's
    mean raw gyro, stamped with the current temperature.
  - `int fus_calib_orient_capture(fus_t *f)` — 0 / −1; on success also restarts forward learning.
  - `int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)` — −1 before the upright
    capture, 1 exactly once (the fix after `fus_step` set the forward row), 0 otherwise.

**Design (binding; session rulings and spec §9.2, §9.3 step 8):**

- `fus_step` calls `fus_still_push(&f->still, raw)` on every sample, right after the body vectors are
  rotated, and sets `FUS_STILL` from `f->still.still` (the last completed window, so the flag is at
  most one window behind, which the rulings accept).
- Forward learning runs only while `calib.orient_ok && !calib.forward_ok`, and only after this
  sample's outputs and flags are written: the sample stays consistent with the calibration it was
  rotated with (sign-less `g_lon`, `FUS_ORIENT_OK` clear), and the newly learned rows take effect on
  the next sample, 10 ms later at `FUSION_HZ`. `fus_fwd_on_sample(&f->fwd, a_b, z)` returning 1 means
  the `FWD_LEARN_WINDOWS`-th run was just counted: the accumulated `f->fwd.sum` (double) is narrowed
  into a `float sum[3]` and handed to `fus_orient_set_forward`. On 0 from that call the rows are set
  and `f->fwd_learned_pending = true`; on −1 (a degenerate accumulation, e.g. every run cancelled
  out) `fus_fwd_init(&f->fwd)` throws the accumulation away so learning starts over instead of
  latching a bad row.
- `fus_is_still` is `f->still.still`; everything else in this task goes through it, so "still" has
  exactly one definition.
- `fus_gyro_bias_update`: returns immediately unless `fus_is_still`; then `gbias[i] =
  still.mean_graw[i]` (raw LSB, the unit `fus_step` subtracts in), `gbias_temp_c100 = temp_known ?
  temp_c100 : 0`, `bias_ok = 1`, `bias_stale = false` (the bias was just taken at the current
  temperature, whether or not that temperature is known).
- `fus_calib_orient_capture`: −1 unless `fus_is_still`; otherwise the result of
  `fus_orient_from_gravity(&f->calib, f->still.mean_acc)`. On 0 the forward row is gone (that
  function clears `forward_ok`), so `fus_fwd_init(&f->fwd)` and `fwd_learned_pending = false` drop
  every window accumulated against the old z.
- `fus_calib_forward_step`: −1 unless `calib.orient_ok`; otherwise `fus_fwd_on_fix` sets the run
  condition from this fix, and the pending flag set by `fus_step` is consumed and reported as 1
  exactly once. Learning completes at 100 Hz inside `fus_step`; this 5–10 Hz call is only how the
  pipeline hears about it (it persists the calibration and emits `EV_CALIB_DONE`).
- Spec §4.8 gains one row for `fus_t` (Step 5). Everything else in `fus.c` — the calibration codec,
  `fus_rotate`, `fus_init`, `fus_set_gps_speed`, `fus_set_temp`, the output formulas — is Task 1's
  and is restated unchanged.

**Drafting note.** Tasks 2–4 were drafted in parallel, so their files did not exist when this task
was compile-checked. The check used stand-in implementations of `fus_still.c`, `fus_orient.c` and
`fus_fwd.c` written to the same `core/fus.h` contracts (a real tumbling-window detector with
`E[x²] − E[x]²` variances, gravity/forward rows built with cross products, the run tracker exactly as
the header describes). The header contract is what this task codes against, so the integration cases
below are meaningful against the real modules; the implementer runs them against Tasks 2–4 as merged.

- [ ] **Step 1: Append the integration tests**

`test/test_fus.c` (the first seven cases are Task 1's, unchanged; the helpers and five cases after
the `/* ---- integration cases ... ---- */` banner are new, and `main` gains five `RUN_TEST` lines):

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw sample helpers: accel in g and gyro in dps expressed as MPU-6050 LSB (types.h scales). */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    imu_raw_t r;
    r.mono_us = 1000000;
    r.ax = (int16_t)lround(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lround(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lround(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lround(gx * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lround(gy * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lround(gz * IMU_GYR_LSB_PER_DPS);
    return r;
}

static void test_defaults_are_identity_and_valid(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[0]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[4]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[8]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, c.orient_ok); TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok); TEST_ASSERT_EQUAL_UINT8(0, c.bias_ok);
}

static void test_invalid_calibration_is_rejected_and_init_falls_back(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.version = 0;
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[0] = 2.0f;                       /* row x not unit length */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[3] = 1.0f;                       /* row y = (1,0,0) parallel to row x */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.forward_ok = 1;                    /* forward without orientation */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.gbias[1] = 40000.0f;               /* beyond the raw range */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));

    fus_t f; c.version = 0;
    fus_init(&f, &c, 1);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fus_calib(&f)->r[0]);
    fus_init(&f, NULL, 0);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_UINT8(0, f.moto);
}

static void test_identity_mount_gravity_bias_and_yaw_sign(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 5.0f * IMU_GYR_LSB_PER_DPS;                     /* 5 dps bias on X */
    c.bias_ok = 1;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 5.0, 0.0, 10.0);
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_EQUAL_INT64(1000000, o.mono_us);
    /* orientation not learned: sign-less horizontal magnitude, no lateral, no lean, ORIENT_OK clear */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & (FUS_ORIENT_OK | FUS_LEAN_VALID));
    /* yaw: +10 dps about body Z = left turn, bias-free because the bias is on X only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);

    c.orient_ok = 1; c.forward_ok = 1;                            /* identity mount fully known */
    fus_init(&f, &c, 1);
    r = raw_g_dps(0.3, 0.5, 1.0, 5.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    /* accel tolerance 1e-3 g: raw LSB quantisation is 1/2048 g ≈ 4.9e-4 g */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* +X forward: accelerating */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, o.g_lat);             /* +Y is left, so lateral g is -a.y */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(0.34f), o.g_comb);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.yaw_dps);            /* 5 dps on X minus the 5 dps bias */
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_EQUAL_UINT32(1, f.samples);
}

static void test_ninety_degree_mount_rotates_into_the_vehicle_frame(void)
{
    /* IMU mounted with body +X pointing left (vehicle +Y) and body +Y pointing backwards (vehicle -X);
     * body +Z up. Rows are the vehicle axes in body coordinates: x = (0,-1,0), y = (1,0,0), z = (0,0,1). */
    fus_calib_t c; fus_calib_defaults(&c);
    const float R[9] = { 0.0f, -1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.orient_ok = 1; c.forward_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    /* vehicle accelerating at 0.3 g forward appears on body -Y; a 10 dps left turn is body +Z */
    imu_raw_t r = raw_g_dps(0.0, -0.3, 1.0, 0.0, 0.0, 10.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* 1e-3 g: LSB quantisation */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    /* a right-hand lateral specific force (vehicle -Y = body -X) reads as positive g_lat */
    r = raw_g_dps(-0.4, 0.0, 1.0, 0.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lon);

    float v[3]; const float b[3] = { 1.0f, 2.0f, 3.0f };
    fus_rotate(R, b, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, v[2]);
}

static void test_temperature_drift_marks_the_bias_stale(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.bias_ok = 1; c.gbias_temp_c100 = 2500;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; imu_raw_t r = raw_g_dps(0, 0, 1, 0, 0, 0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);        /* temperature unknown: not stale */
    fus_set_temp(&f, 3900);                                      /* 14 °C away: within BIAS_TEMP_STALE_C */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 4100);                                      /* 16 °C away */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 900);                                       /* 16 °C the other way */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    /* no bias captured: temperature can never make it stale */
    fus_calib_defaults(&c); fus_init(&f, &c, 1); fus_set_temp(&f, 9000);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
}

static void test_calibration_round_trips_through_the_calib_record(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float ang = 30.0f * 3.14159265358979f / 180.0f;         /* rotation about Z by 30° (M_PI is not C11) */
    const float R[9] = { cosf(ang), sinf(ang), 0.0f,  -sinf(ang), cosf(ang), 0.0f,  0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.gbias[0] = 12.4f; c.gbias[1] = -7.6f; c.gbias[2] = 0.4f; c.gbias_temp_c100 = 2712;
    c.orient_ok = 1; c.forward_ok = 1; c.bias_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    ses_calib_t w; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(8660, w.r_e4[0]);                     /* cos 30° × 1e4 rounded */
    TEST_ASSERT_EQUAL_INT16(5000, w.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(12, w.gbias[0]); TEST_ASSERT_EQUAL_INT16(-8, w.gbias[1]); TEST_ASSERT_EQUAL_INT16(0, w.gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(0x07, w.calib_flags);
    fus_calib_t d; fus_calib_from_ses(&w, &d);
    for (int i = 0; i < 9; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, c.r[i], d.r[i]);
    TEST_ASSERT_TRUE(fus_calib_valid(&d));                        /* 1e-4 quantisation stays inside FUS_ORTHO_TOL */
    TEST_ASSERT_EQUAL_FLOAT(12.0f, d.gbias[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d.orient_ok); TEST_ASSERT_EQUAL_UINT8(1, d.forward_ok); TEST_ASSERT_EQUAL_UINT8(1, d.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, d.version);
    TEST_ASSERT_EQUAL_INT16(0, d.gbias_temp_c100);                 /* not carried by the record */
    /* a saturating bias clamps instead of wrapping */
    c.gbias[2] = 40000.0f; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(32767, w.gbias[2]);
}

static void test_gps_speed_is_held_with_its_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

/* ---- integration cases: stillness, bias, orientation capture and forward learning (§22.1) ---- */

#define BURST_SAMPLES   120                       /* 1.2 s of acceleration at FUSION_HZ */
#define COAST_SAMPLES    50                       /* 0.5 s of coasting between bursts */
#define FIX_EVERY        (FUSION_HZ / 5)          /* one GPS fix every 20 samples = 5 Hz */
#define BURST_ACC_MPS2   2.0f                     /* > FWD_LEARN_ACC_MPS2, so the fix qualifies */
#define BURST_YAW_DPS    0.5f                     /* < FWD_LEARN_MAX_YAW_DPS, so the fix qualifies */

/* Deterministic LCG (Numerical Recipes constants): the noise sequence must be identical on every
 * host and on the ESP32, so no rand() and no library state. */
static uint32_t lcg_state;
static void lcg_reset(void) { lcg_state = 22222u; }
static float noise_pm(float amp)                  /* uniform in [-amp, +amp) */
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    const float u = (float)(lcg_state >> 8) / 16777216.0f;   /* top 24 bits → [0,1) */
    return amp * (2.0f * u - 1.0f);
}

/* Raw sample from accel in g and gyro in raw LSB (the gyro bias lives in LSB, §9.2). */
static imu_raw_t raw_at(int64_t mono_us, const float a_g[3], const float g_lsb[3])
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = (int16_t)lroundf(a_g[0] * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lroundf(a_g[1] * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lroundf(a_g[2] * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lroundf(g_lsb[0]);
    r.gy = (int16_t)lroundf(g_lsb[1]);
    r.gz = (int16_t)lroundf(g_lsb[2]);
    return r;
}

/* Feeds n samples at FUSION_HZ, each = (a_g, g_lsb) plus uniform noise, advancing *mono_us. */
static void feed(fus_t *f, fused_sample_t *o, int n, const float a_g[3], const float g_lsb[3],
                 float a_noise_g, float g_noise_lsb, int64_t *mono_us)
{
    for (int i = 0; i < n; i++) {
        /* Sequential declarations are sequenced (unlike the three noise_pm() calls in one brace
         * initializer would be), so each axis draws its LCG value in a fixed, portable order. */
        const float na0 = noise_pm(a_noise_g), na1 = noise_pm(a_noise_g), na2 = noise_pm(a_noise_g);
        const float an[3] = { a_g[0] + na0, a_g[1] + na1, a_g[2] + na2 };
        const float ng0 = noise_pm(g_noise_lsb), ng1 = noise_pm(g_noise_lsb), ng2 = noise_pm(g_noise_lsb);
        const float gn[3] = { g_lsb[0] + ng0, g_lsb[1] + ng1, g_lsb[2] + ng2 };
        imu_raw_t r = raw_at(*mono_us, an, gn);
        fus_step(f, &r, o);
        *mono_us += 1000000 / FUSION_HZ;
    }
}

static const float ZERO3[3] = { 0.0f, 0.0f, 0.0f };
static const float QUIET_ACC_NOISE_G = 0.005f;    /* var 8.3e-6 g² ≪ STILL_ACC_VAR = 4e-4 g² */
static const float QUIET_GYR_NOISE_LSB = 0.5f;    /* var 3.1e-4 dps² ≪ STILL_GYRO_VAR = 4 dps² */
static const float MOVING_GYR_NOISE_LSB = 5.0f * IMU_GYR_LSB_PER_DPS;  /* ±5 dps → var 8.3 dps² > 4 */

static void test_still_detection_and_gyro_bias_update(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_temp(&f, 2500);                       /* the pipeline's ~1 Hz poll, before the capture */
    fused_sample_t o; int64_t t = 1000000;
    const float quiet_a[3] = { 0.0f, 0.0f, 1.0f };
    const float bias_lsb[3] = { 20.0f, -8.0f, 3.0f };   /* the gyro bias the window must recover */

    feed(&f, &o, FUS_STILL_WINDOW_N - 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));           /* 199 samples: no window has completed */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    /* with no bias captured the 3 LSB on Z read as yaw: 3 / 16.4 = 0.183 dps. Tolerance 0.02 dps =
     * 0.33 LSB, more than the ±0.5 LSB noise can survive the int16 rounding of the raw sample. */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.0f / IMU_GYR_LSB_PER_DPS, o.yaw_dps);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));            /* the 200th sample closes the window */
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);
    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);   /* the verdict holds until the next window */

    fus_gyro_bias_update(&f);
    /* Tolerance 0.1 LSB: the ±0.5 LSB noise is zero-mean, so the mean of 200 samples has a standard
     * error of 0.5/sqrt(3·200) ≈ 0.02 LSB, and the int16 rounding of the raw sample removes most of
     * the noise before it is ever averaged. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, fus_calib(&f)->gbias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -8.0f, fus_calib(&f)->gbias[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, fus_calib(&f)->gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->bias_ok);
    TEST_ASSERT_EQUAL_INT16(2500, fus_calib(&f)->gbias_temp_c100);
    TEST_ASSERT_FALSE(f.bias_stale);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, o.yaw_dps);           /* 0.1 dps = 1.6 LSB of headroom */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);      /* captured at the current temperature */

    /* A moving window (±5 dps of gyro) is not still, so the bias must not move. */
    const fus_calib_t before = *fus_calib(&f);
    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    fus_gyro_bias_update(&f);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[0], fus_calib(&f)->gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[1], fus_calib(&f)->gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[2], fus_calib(&f)->gbias[2]);
}

/* Gravity as read by an IMU pitched 30° about body Y: (sin 30°, 0, cos 30°) g. Body -Y stays
 * perpendicular to that z (e_y · z = 0), so it can serve as the vehicle forward direction below. */
static const float TILT_RAD = 30.0f * 3.14159265358979f / 180.0f;   /* M_PI is not C11 */
static void tilted_gravity(float g[3])
{
    g[0] = sinf(TILT_RAD); g[1] = 0.0f; g[2] = cosf(TILT_RAD);
}

/* Fills one still window at the tilted attitude and captures the orientation. */
static void capture_tilted_orientation(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3])
{
    feed(f, o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, t);
    TEST_ASSERT_TRUE(fus_is_still(f));
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(f));
}

static void test_orientation_capture_from_tilted_gravity(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    /* Capture while moving is refused and leaves the calibration alone. */
    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_INT(-1, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->orient_ok);

    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));
    imu_raw_t clean = raw_at(t, grav, ZERO3);      /* noise-free sample, so only LSB rounding is left */
    fus_step(&f, &clean, &o);
    /* identity rows: the whole 30° tilt shows up as sign-less horizontal magnitude, sin 30° = 0.5 g.
     * 1e-3 g covers the 1/2048 g = 4.9e-4 g raw quantisation. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, o.g_lon);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);

    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    fus_step(&f, &clean, &o);                      /* the same sample through the captured rows */
    /* gravity is the z row now, so nothing is left in the horizontal plane. 1e-3 g covers the raw
     * quantisation (4.9e-4 g) plus the mean of the ±0.005 g window noise (0.005/sqrt(3·200) = 2e-4 g
     * per axis), which is all the captured z can be off by. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);            /* zero until forward is learned */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward row still unknown */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);      /* lean invalid before forward learned */
}

/* Three straight-line acceleration bursts along body -Y, with fus_calib_forward_step called at 5 Hz
 * exactly as the pipeline calls it (once per GPS fix). Counts the calls that returned 1 and -1.
 * The samples carry no noise: the forward row is then exact up to the raw quantisation. A constant
 * acceleration has no variance, so the stillness detector also calls these windows still — that is
 * inherent to a variance test and why the assertions below mask FUS_STILL out. */
static int run_forward_bursts(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3], int *neg)
{
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;        /* specific force of the burst, in g */
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    int ones = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        for (int i = 0; i < BURST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, acc, ZERO3, 0.0f, 0.0f, t);
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, grav, ZERO3, 0.0f, 0.0f, t);
        }
    }
    return ones;
}

static void test_forward_learning_from_straight_line_acceleration(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    TEST_ASSERT_EQUAL_INT(-1, fus_calib_forward_step(&f, BURST_ACC_MPS2, 0.0f));   /* before any capture */
    capture_tilted_orientation(&f, &o, &t, grav);

    int neg = 0;
    const int ones = run_forward_bursts(&f, &o, &t, grav, &neg);
    TEST_ASSERT_EQUAL_INT(1, ones);                /* reported exactly once, during the third burst */
    TEST_ASSERT_EQUAL_INT(0, neg);                 /* never -1 once the orientation is captured */
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
    TEST_ASSERT_TRUE(fus_fwd_ready(&f.fwd));

    /* The learned forward row is body -Y, which is perpendicular to the tilted z, so the burst's
     * specific force lands entirely on g_lon: 2 / 9.80665 = 0.2039 g. 2e-3 g covers the raw
     * quantisation of the sample (4.9e-4 g) and of the samples the row was learned from. */
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.2039f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lat);

    /* A left lateral specific force of 0.1 g: the body vector is 0.1 · y_row on top of gravity, and
     * lateral g is + to the right, so the output is -0.1 g. */
    const float *rows = fus_calib(&f)->r;
    const float lat[3] = { grav[0] + 0.1f * rows[3], grav[1] + 0.1f * rows[4], grav[2] + 0.1f * rows[5] };
    r = raw_at(t, lat, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, -0.1f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lon);
}

static void test_calibration_persists_through_the_calib_record_after_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));

    ses_calib_t rec; fus_calib_to_ses(fus_calib(&f), &rec);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_F_ORIENT | FUS_CALIB_F_FORWARD, rec.calib_flags);
    fus_calib_t decoded; fus_calib_from_ses(&rec, &decoded);
    TEST_ASSERT_TRUE(fus_calib_valid(&decoded));
    fus_t g; fus_init(&g, &decoded, 1);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&g)->forward_ok);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fused_sample_t restored;
    fus_step(&f, &r, &o);
    fus_step(&g, &r, &restored);
    /* 1e-3 g: the record stores each row entry as r × 1e4 rounded to int16, so a row entry moves by
     * up to 5e-5 and a 1 g sample by up to ~1e-4 g; the tolerance keeps a comfortable margin. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lon, restored.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lat, restored.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_comb, restored.g_comb);
    TEST_ASSERT_EQUAL_UINT8(o.flags & FUS_ORIENT_OK, restored.flags & FUS_ORIENT_OK);
}

static void test_recapture_resets_forward_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));
    TEST_ASSERT_EQUAL_UINT8(FWD_LEARN_WINDOWS, f.fwd.windows);

    /* The vehicle is re-mounted upright and captured again: the new z row invalidates the forward
     * row and every window accumulated against the old one. */
    const float upright[3] = { 0.0f, 0.0f, 1.0f };
    feed(&f, &o, FUS_STILL_WINDOW_N, upright, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));
    TEST_ASSERT_FALSE(f.fwd_learned_pending);
    imu_raw_t r = raw_at(t, upright, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward must be learned again */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
}

/* Degenerate accumulation (§9.2, fus_orient.c's FUS_FWD_MIN_NORM guard): when the accumulated
 * horizontal sum collapses to ~zero, fus_orient_set_forward refuses to learn a row and fus_step
 * restarts the tracker (fus_fwd_init) instead of leaving stale state behind. Two runs of exactly
 * opposite specific force are built as bit-exact int16 negations of each other, so ah(-a) = -ah(a)
 * to full float precision and the accumulated sum returns to exactly zero, not merely close to it.
 * FWD_LEARN_WINDOWS = 3 needs a third counted run; it alternates sign every sample (an even count),
 * which cancels to exactly zero by the time it is counted too, so the sum stays exactly zero right
 * through the trigger and the test is deterministic on every host. */
static void test_degenerate_forward_sum_restarts_the_tracker(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc_pos[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    const imu_raw_t r_pos = raw_at(t, acc_pos, ZERO3);
    imu_raw_t r_neg = r_pos;
    r_neg.ax = (int16_t)(-(int)r_pos.ax);
    r_neg.ay = (int16_t)(-(int)r_pos.ay);
    r_neg.az = (int16_t)(-(int)r_pos.az);

    int ones = 0, neg = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        const int n = (b < 2) ? BURST_SAMPLES : FUS_FWD_MIN_SAMPLES;
        for (int i = 0; i < n; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            imu_raw_t r;
            if (b == 0) r = r_pos;                     /* run 0: +X in the body plane */
            else if (b == 1) r = r_neg;                 /* run 1: -X, cancelling run 0 exactly */
            else r = (i % 2 == 0) ? r_pos : r_neg;      /* run 2: alternates, cancelling within itself */
            r.mono_us = t;
            fus_step(&f, &r, &o);
            t += 1000000 / FUSION_HZ;
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            feed(&f, &o, 1, grav, ZERO3, 0.0f, 0.0f, &t);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, ones);              /* the degenerate sum never reports a learned row */
    TEST_ASSERT_EQUAL_INT(0, neg);               /* orientation stays captured the whole time */
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);   /* fus_orient_set_forward's -1 restarted the tracker */
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));

    imu_raw_t clean = raw_at(t, grav, ZERO3);
    fus_step(&f, &clean, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);   /* forward still unlearned */

    /* A subsequent clean run still learns the forward row normally. */
    int neg2 = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg2));
    TEST_ASSERT_EQUAL_INT(0, neg2);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_identity_and_valid);
    RUN_TEST(test_invalid_calibration_is_rejected_and_init_falls_back);
    RUN_TEST(test_identity_mount_gravity_bias_and_yaw_sign);
    RUN_TEST(test_ninety_degree_mount_rotates_into_the_vehicle_frame);
    RUN_TEST(test_temperature_drift_marks_the_bias_stale);
    RUN_TEST(test_calibration_round_trips_through_the_calib_record);
    RUN_TEST(test_gps_speed_is_held_with_its_validity_and_time);
    RUN_TEST(test_still_detection_and_gyro_bias_update);
    RUN_TEST(test_orientation_capture_from_tilted_gravity);
    RUN_TEST(test_forward_learning_from_straight_line_acceleration);
    RUN_TEST(test_calibration_persists_through_the_calib_record_after_learning);
    RUN_TEST(test_recapture_resets_forward_learning);
    RUN_TEST(test_degenerate_forward_sum_restarts_the_tracker);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it — expected FAIL**

Run: `cmake --build test/build --parallel && ./test/build/test_fus 2>&1 | tail -8`
Expected: the seven Task 1 cases pass and the five new ones fail, because Task 1's stubs report
"never still" and "no orientation":

```
test/test_fus.c:243:test_still_detection_and_gyro_bias_update:FAIL: Expected TRUE Was FALSE
test/test_fus.c:304:test_orientation_capture_from_tilted_gravity:FAIL: Expected TRUE Was FALSE
test/test_fus.c:286:test_forward_learning_from_straight_line_acceleration:FAIL: Expected TRUE Was FALSE
test/test_fus.c:286:test_calibration_persists_through_the_calib_record_after_learning:FAIL: Expected TRUE Was FALSE
test/test_fus.c:286:test_recapture_resets_forward_learning:FAIL: Expected TRUE Was FALSE

-----------------------
12 Tests 5 Failures 0 Ignored
```

(Line 243 is the `TEST_ASSERT_TRUE(fus_is_still(&f))` on the 200th quiet sample, line 286 the same
assertion inside `capture_tilted_orientation`, line 304 the one before the tilted capture — the
stub `fus_is_still` returns false, so every case that needs a still window stops there.)

- [ ] **Step 3: Wire the modules into the fusion step**

`components/core/fusion/fus.c` (complete file; everything above `fus_step` is Task 1's, unchanged):

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
}

void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid)
{
    f->v_mps = v_mps; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    /* Stillness runs on every raw sample (§9.3 step 8); the flags below report the last completed
     * tumbling window, which is what fus_is_still, the bias capture and drag arming all use. */
    fus_still_push(&f->still, raw);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */
        out->g_lat = -a[1];                      /* +Y is left; lateral g is + to the right (§9.3 step 5, car form) */
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;
    }
    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);
    out->lean_deg = 0.0f;                        /* lean filter arrives in session 2.3 */
    out->yaw_dps  = w[2];                        /* session 2.3 applies the lean correction of §9.3 step 3 */
    uint8_t flags = 0;
    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;
    out->flags = flags;

    /* Forward-axis learning (§9.2): only between the upright capture and the forward row being known.
     * The rows may change in this block, after this sample was already rotated with the old ones —
     * that is deliberate and harmless: the sample stays consistent with the calibration it was
     * computed from (sign-less g_lon, no FUS_ORIENT_OK) and the learned rows take effect from the
     * next sample, 10 ms later at FUSION_HZ. */
    if (f->calib.orient_ok && !f->calib.forward_ok) {
        const float z[3] = { f->calib.r[6], f->calib.r[7], f->calib.r[8] };
        if (fus_fwd_on_sample(&f->fwd, a_b, z) == 1) {
            const float sum[3] = { (float)f->fwd.sum[0], (float)f->fwd.sum[1], (float)f->fwd.sum[2] };
            if (fus_orient_set_forward(&f->calib, sum) == 0) {
                f->fwd_learned_pending = true;   /* fus_calib_forward_step reports it on the next fix */
            } else {
                fus_fwd_init(&f->fwd);           /* degenerate accumulation: drop it and learn again */
            }
        }
    }
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- stillness, bias and calibration entry points (wired to the modules above) ---- */

bool fus_is_still(const fus_t *f)
{
    return f->still.still;                       /* last completed window; false until one completes */
}

void fus_gyro_bias_update(fus_t *f)
{
    if (!fus_is_still(f)) return;                /* §9.2: the bias is only meaningful over a still window */
    for (int i = 0; i < 3; i++) f->calib.gbias[i] = f->still.mean_graw[i];
    /* 0 marks an unknown capture temperature; the bias reads stale once a temperature is known,
     * which is the conservative answer (it only prompts a recapture) and matches the same
     * convention fus_calib_from_ses uses for a restored bias. */
    f->calib.gbias_temp_c100 = f->temp_known ? f->temp_c100 : (int16_t)0;
    f->calib.bias_ok = 1;
    f->bias_stale = false;                       /* freshly captured at the current temperature */
}

int fus_calib_orient_capture(fus_t *f)
{
    if (!fus_is_still(f)) return -1;
    const int rc = fus_orient_from_gravity(&f->calib, f->still.mean_acc);
    if (rc == 0) {
        /* A new z row invalidates the forward row (fus_orient_from_gravity cleared forward_ok), so
         * everything accumulated against the old z is thrown away and learning starts again. */
        fus_fwd_init(&f->fwd);
        f->fwd_learned_pending = false;
    }
    return rc;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    if (!f->calib.orient_ok) return -1;          /* nothing to project against until z is captured */
    fus_fwd_on_fix(&f->fwd, gps_acc_mps2, yaw_dps);
    if (f->fwd_learned_pending) {                /* learning completes in fus_step; reported once here */
        f->fwd_learned_pending = false;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build --parallel 2>&1 | grep -E "error|warning"; ctest --test-dir test/build --output-on-failure 2>&1 | tail -3`
Expected: no `error` or `warning` line, and

```
100% tests passed out of 24
```

(21 executables after session 2.2 Task 1, plus `test_fus_still`, `test_fus_orient` and `test_fus_fwd`
from Tasks 2–4. `test_fus` itself now reports `12 Tests 0 Failures 0 Ignored`.)

Parity check (both compilers, strict flags with no `-Wno-error` relaxation, to confirm the explicit
casts are complete):

Run: `for CC in clang gcc-16; do $CC -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Icomponents/core/include -Itest/unity/src -c components/core/fusion/fus.c -o /dev/null && $CC -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Icomponents/core/include -Itest/unity/src -c test/test_fus.c -o /dev/null && echo "$CC ok"; done`
Expected:

```
clang ok
gcc-16 ok
```

- [ ] **Step 5: Spec write-back (§4.8)**

In `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §4.8, insert one row immediately after
the `ses_reader_t` row:

```
| Fusion state `fus_t` (calibration, stillness window sums, forward tracker) | ~400 B |
```

so the two lines read:

```
| `ses_reader_t` (frame reader, 247 B payload + 502 B rescan buffer) | 772 B |
| Fusion state `fus_t` (calibration, stillness window sums, forward tracker) | ~400 B |
```

The figure carries the `~` the table allows for estimates: `sizeof(fus_t)` is 328 B today
(56 B calibration + 152 B stillness window + 64 B forward tracker + 56 B of speed, temperature,
flags and counters; the `double` accumulators align the same way on xtensa, so the host figure is
the target figure), and session 2.3 adds the lean-filter state to the same struct.

Verify: `grep -c "Fusion state \`fus_t\`" docs/superpowers/specs/2026-09-14-lap-timer-design.md`
Expected: `1`.

- [ ] **Step 6: Hygiene and commit**

Run: `git diff --check`
Expected: no output.

```bash
git add components/core/fusion/fus.c test/test_fus.c docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit -m "feat(core): wire stillness, gyro bias, orientation and forward learning into fusion

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

**Header change needed:** none. `core/fus.h` as landed by session 2.2 Task 1 is used verbatim: every
field this task reads (`still.still`, `still.mean_acc`, `still.mean_graw`, `fwd.sum`, `fwd.windows`,
`fwd_learned_pending`) is already public, and the four stub signatures match what the wiring needs.

---

## Session 2.3 — fusion part 2: lean complementary filter, lateral/longitudinal/combined g, earth-frame yaw, GPS cross-check

Roadmap exit criterion: a synthetic steady turn converges to the known lean within ±1° in 1.5 s; `test_fus` §9.3 cases green; tag `p02-d3`.

Task graph: **Task 1** (serial: contract — GPS course, the turn-rate helper, new `fus_t` state) → **Task 2** (serial: the §9.3 fusion math in `fus_step` and the convergence tests). The §9.3 computation all lives inside `fus_step` and is one coupled algorithm, so this session is serial, not parallel.

Decisions fixed for the session (rulings, spec §9.3 is the authority):

- `fus_set_gps_speed` is widened to carry course over ground (compass degrees) alongside speed, because §9.3's GPS cross-check needs the course derivative and the pipeline reads both from the same NAV-PVT fix. The 2.2 test that called the 4-argument form is updated; the 2.2 plan blocks stay as the historical record of what 2.2 delivered.
- The earth-frame turn rate from two consecutive GPS courses is a pure helper `fus_yaw_rate_gps_dps` (Task 1), so its wrap and sign handling are tested in isolation. Sign: compass heading rises clockwise (a right turn), vehicle yaw is + to the left, so the rate is the negated wrapped course difference over dt.
- `ψ̇` used in the lean reference and the yaw output is computed in rad/s from the rotated gyro and the *previous* step's lean estimate (step 3 of §9.3 says "current lean estimate", which for a causal filter is last step's value); this breaks the φ→ψ̇→φ_ref→φ circularity cleanly (Task 2).
- The "5 s without a reference" that clears `FUS_LEAN_VALID` (§9.3 step 4) uses a new constant `LEAN_REF_TIMEOUT_S = 5` (Task 2 adds it); it is distinct from `LEAN_DISAGREE_S` even though both are 5 s.
- `FUS_DISAGREE` latches a `disagree_latched` bool so the pipeline (plan 03) logs `E_FUSION_DISAGREE` once per session; core only sets the flag and the latch (Task 2).

### Task 1: GPS course, turn-rate helper, fusion state (serial)

**Files:**
- Modify: `components/core/include/core/fus.h`, `components/core/fusion/fus.c`, `test/test_fus.c`, `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§5.2 fus excerpt)

**Interfaces:**
- Consumes: the session 2.2 `core/fus.h` contract and `fus.c`.
- Produces: the widened `void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid)`; the pure helper `float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us)`; the new `fus_t` fields (`v_course_deg`, `have_prev_course`, `prev_course_deg`, `prev_course_mono_us`, `yaw_gps_dps`, `yaw_gps_mono_us`, `disagree_since_mono_us`, `disagree_latched`). Task 2 consumes all of these.

- [ ] **Step 1: Update `test/test_fus.c`** to the full content below (the 2.2 held-speed test becomes the course-carrying form; two new tests cover the helper and the course history). Run `ctest`, expect `test_fus` to fail to compile first if applied before the source, else 15 cases pass after Step 2.

`test/test_fus.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw sample helpers: accel in g and gyro in dps expressed as MPU-6050 LSB (types.h scales). */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    imu_raw_t r;
    r.mono_us = 1000000;
    r.ax = (int16_t)lround(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lround(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lround(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lround(gx * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lround(gy * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lround(gz * IMU_GYR_LSB_PER_DPS);
    return r;
}

static void test_defaults_are_identity_and_valid(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[0]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[4]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[8]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, c.orient_ok); TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok); TEST_ASSERT_EQUAL_UINT8(0, c.bias_ok);
}

static void test_invalid_calibration_is_rejected_and_init_falls_back(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.version = 0;
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[0] = 2.0f;                       /* row x not unit length */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[3] = 1.0f;                       /* row y = (1,0,0) parallel to row x */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.forward_ok = 1;                    /* forward without orientation */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.gbias[1] = 40000.0f;               /* beyond the raw range */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));

    fus_t f; c.version = 0;
    fus_init(&f, &c, 1);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fus_calib(&f)->r[0]);
    fus_init(&f, NULL, 0);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_UINT8(0, f.moto);
}

static void test_identity_mount_gravity_bias_and_yaw_sign(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 5.0f * IMU_GYR_LSB_PER_DPS;                     /* 5 dps bias on X */
    c.bias_ok = 1;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 5.0, 0.0, 10.0);
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_EQUAL_INT64(1000000, o.mono_us);
    /* orientation not learned: sign-less horizontal magnitude, no lateral, no lean, ORIENT_OK clear */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & (FUS_ORIENT_OK | FUS_LEAN_VALID));
    /* yaw: +10 dps about body Z = left turn, bias-free because the bias is on X only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);

    c.orient_ok = 1; c.forward_ok = 1;                            /* identity mount fully known */
    fus_init(&f, &c, 1);
    r = raw_g_dps(0.3, 0.5, 1.0, 5.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    /* accel tolerance 1e-3 g: raw LSB quantisation is 1/2048 g ≈ 4.9e-4 g */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* +X forward: accelerating */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, o.g_lat);             /* +Y is left, so lateral g is -a.y */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(0.34f), o.g_comb);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.yaw_dps);            /* 5 dps on X minus the 5 dps bias */
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_EQUAL_UINT32(1, f.samples);
}

static void test_ninety_degree_mount_rotates_into_the_vehicle_frame(void)
{
    /* IMU mounted with body +X pointing left (vehicle +Y) and body +Y pointing backwards (vehicle -X);
     * body +Z up. Rows are the vehicle axes in body coordinates: x = (0,-1,0), y = (1,0,0), z = (0,0,1). */
    fus_calib_t c; fus_calib_defaults(&c);
    const float R[9] = { 0.0f, -1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.orient_ok = 1; c.forward_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    /* vehicle accelerating at 0.3 g forward appears on body -Y; a 10 dps left turn is body +Z */
    imu_raw_t r = raw_g_dps(0.0, -0.3, 1.0, 0.0, 0.0, 10.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* 1e-3 g: LSB quantisation */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    /* a right-hand lateral specific force (vehicle -Y = body -X) reads as positive g_lat */
    r = raw_g_dps(-0.4, 0.0, 1.0, 0.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lon);

    float v[3]; const float b[3] = { 1.0f, 2.0f, 3.0f };
    fus_rotate(R, b, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, v[2]);
}

static void test_temperature_drift_marks_the_bias_stale(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.bias_ok = 1; c.gbias_temp_c100 = 2500;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; imu_raw_t r = raw_g_dps(0, 0, 1, 0, 0, 0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);        /* temperature unknown: not stale */
    fus_set_temp(&f, 3900);                                      /* 14 °C away: within BIAS_TEMP_STALE_C */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 4100);                                      /* 16 °C away */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 900);                                       /* 16 °C the other way */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    /* no bias captured: temperature can never make it stale */
    fus_calib_defaults(&c); fus_init(&f, &c, 1); fus_set_temp(&f, 9000);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
}

static void test_calibration_round_trips_through_the_calib_record(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float ang = 30.0f * 3.14159265358979f / 180.0f;         /* rotation about Z by 30° (M_PI is not C11) */
    const float R[9] = { cosf(ang), sinf(ang), 0.0f,  -sinf(ang), cosf(ang), 0.0f,  0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.gbias[0] = 12.4f; c.gbias[1] = -7.6f; c.gbias[2] = 0.4f; c.gbias_temp_c100 = 2712;
    c.orient_ok = 1; c.forward_ok = 1; c.bias_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    ses_calib_t w; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(8660, w.r_e4[0]);                     /* cos 30° × 1e4 rounded */
    TEST_ASSERT_EQUAL_INT16(5000, w.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(12, w.gbias[0]); TEST_ASSERT_EQUAL_INT16(-8, w.gbias[1]); TEST_ASSERT_EQUAL_INT16(0, w.gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(0x07, w.calib_flags);
    fus_calib_t d; fus_calib_from_ses(&w, &d);
    for (int i = 0; i < 9; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, c.r[i], d.r[i]);
    TEST_ASSERT_TRUE(fus_calib_valid(&d));                        /* 1e-4 quantisation stays inside FUS_ORTHO_TOL */
    TEST_ASSERT_EQUAL_FLOAT(12.0f, d.gbias[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d.orient_ok); TEST_ASSERT_EQUAL_UINT8(1, d.forward_ok); TEST_ASSERT_EQUAL_UINT8(1, d.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, d.version);
    TEST_ASSERT_EQUAL_INT16(0, d.gbias_temp_c100);                 /* not carried by the record */
    /* a saturating bias clamps instead of wrapping */
    c.gbias[2] = 40000.0f; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(32767, w.gbias[2]);
}

static void test_gps_speed_and_course_are_held_with_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 91.0f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_FLOAT(91.0f, f.v_course_deg);
    TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

static void test_gps_yaw_rate_helper_signs_and_wrap(void)
{
    /* straight: no course change */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fus_yaw_rate_gps_dps(90.0f, 0, 90.0f, 1000000));
    /* right turn: compass rises 10->20 over 1 s -> yaw negative (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -10.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 1000000));
    /* left turn: compass falls 20->10 -> yaw positive (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, fus_yaw_rate_gps_dps(20.0f, 0, 10.0f, 1000000));
    /* wrap, right turn across north: 350 -> 10 is +20 deg clockwise */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(350.0f, 0, 10.0f, 1000000));
    /* wrap, left turn across north: 10 -> 350 is -20 deg */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 350.0f, 1000000));
    /* half a second doubles the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 500000));
    /* non-positive dt -> 0 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 1000000, 20.0f, 1000000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 2000000, 20.0f, 1000000));
}

static void test_gps_course_history_feeds_the_turn_rate(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* NAN until two valid fixes */
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1000000, true);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* one fix: still none */
    fus_set_gps_speed(&f, 30.0f, 106.0f, 1200000, true);  /* +6 deg in 0.2 s -> -30 dps (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    TEST_ASSERT_EQUAL_INT64(1200000, f.yaw_gps_mono_us);
    fus_set_gps_speed(&f, 30.0f, 999.0f, 1300000, false); /* invalid: does not update prev or the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1400000, true);  /* -6 deg from 106 over 0.2 s -> +30 dps (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.0f, f.yaw_gps_dps);
}

/* ---- integration cases: stillness, bias, orientation capture and forward learning (§22.1) ---- */

#define BURST_SAMPLES   120                       /* 1.2 s of acceleration at FUSION_HZ */
#define COAST_SAMPLES    50                       /* 0.5 s of coasting between bursts */
#define FIX_EVERY        (FUSION_HZ / 5)          /* one GPS fix every 20 samples = 5 Hz */
#define BURST_ACC_MPS2   2.0f                     /* > FWD_LEARN_ACC_MPS2, so the fix qualifies */
#define BURST_YAW_DPS    0.5f                     /* < FWD_LEARN_MAX_YAW_DPS, so the fix qualifies */

/* Deterministic LCG (Numerical Recipes constants): the noise sequence must be identical on every
 * host and on the ESP32, so no rand() and no library state. */
static uint32_t lcg_state;
static void lcg_reset(void) { lcg_state = 22222u; }
static float noise_pm(float amp)                  /* uniform in [-amp, +amp) */
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    const float u = (float)(lcg_state >> 8) / 16777216.0f;   /* top 24 bits → [0,1) */
    return amp * (2.0f * u - 1.0f);
}

/* Raw sample from accel in g and gyro in raw LSB (the gyro bias lives in LSB, §9.2). */
static imu_raw_t raw_at(int64_t mono_us, const float a_g[3], const float g_lsb[3])
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = (int16_t)lroundf(a_g[0] * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lroundf(a_g[1] * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lroundf(a_g[2] * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lroundf(g_lsb[0]);
    r.gy = (int16_t)lroundf(g_lsb[1]);
    r.gz = (int16_t)lroundf(g_lsb[2]);
    return r;
}

/* Feeds n samples at FUSION_HZ, each = (a_g, g_lsb) plus uniform noise, advancing *mono_us. */
static void feed(fus_t *f, fused_sample_t *o, int n, const float a_g[3], const float g_lsb[3],
                 float a_noise_g, float g_noise_lsb, int64_t *mono_us)
{
    for (int i = 0; i < n; i++) {
        /* Sequential declarations are sequenced (unlike the three noise_pm() calls in one brace
         * initializer would be), so each axis draws its LCG value in a fixed, portable order. */
        const float na0 = noise_pm(a_noise_g), na1 = noise_pm(a_noise_g), na2 = noise_pm(a_noise_g);
        const float an[3] = { a_g[0] + na0, a_g[1] + na1, a_g[2] + na2 };
        const float ng0 = noise_pm(g_noise_lsb), ng1 = noise_pm(g_noise_lsb), ng2 = noise_pm(g_noise_lsb);
        const float gn[3] = { g_lsb[0] + ng0, g_lsb[1] + ng1, g_lsb[2] + ng2 };
        imu_raw_t r = raw_at(*mono_us, an, gn);
        fus_step(f, &r, o);
        *mono_us += 1000000 / FUSION_HZ;
    }
}

static const float ZERO3[3] = { 0.0f, 0.0f, 0.0f };
static const float QUIET_ACC_NOISE_G = 0.005f;    /* var 8.3e-6 g² ≪ STILL_ACC_VAR = 4e-4 g² */
static const float QUIET_GYR_NOISE_LSB = 0.5f;    /* var 3.1e-4 dps² ≪ STILL_GYRO_VAR = 4 dps² */
static const float MOVING_GYR_NOISE_LSB = 5.0f * IMU_GYR_LSB_PER_DPS;  /* ±5 dps → var 8.3 dps² > 4 */

static void test_still_detection_and_gyro_bias_update(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_temp(&f, 2500);                       /* the pipeline's ~1 Hz poll, before the capture */
    fused_sample_t o; int64_t t = 1000000;
    const float quiet_a[3] = { 0.0f, 0.0f, 1.0f };
    const float bias_lsb[3] = { 20.0f, -8.0f, 3.0f };   /* the gyro bias the window must recover */

    feed(&f, &o, FUS_STILL_WINDOW_N - 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));           /* 199 samples: no window has completed */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    /* with no bias captured the 3 LSB on Z read as yaw: 3 / 16.4 = 0.183 dps. Tolerance 0.02 dps =
     * 0.33 LSB, more than the ±0.5 LSB noise can survive the int16 rounding of the raw sample. */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.0f / IMU_GYR_LSB_PER_DPS, o.yaw_dps);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));            /* the 200th sample closes the window */
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);
    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);   /* the verdict holds until the next window */

    fus_gyro_bias_update(&f);
    /* Tolerance 0.1 LSB: the ±0.5 LSB noise is zero-mean, so the mean of 200 samples has a standard
     * error of 0.5/sqrt(3·200) ≈ 0.02 LSB, and the int16 rounding of the raw sample removes most of
     * the noise before it is ever averaged. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, fus_calib(&f)->gbias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -8.0f, fus_calib(&f)->gbias[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, fus_calib(&f)->gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->bias_ok);
    TEST_ASSERT_EQUAL_INT16(2500, fus_calib(&f)->gbias_temp_c100);
    TEST_ASSERT_FALSE(f.bias_stale);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, o.yaw_dps);           /* 0.1 dps = 1.6 LSB of headroom */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);      /* captured at the current temperature */

    /* A moving window (±5 dps of gyro) is not still, so the bias must not move. */
    const fus_calib_t before = *fus_calib(&f);
    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    fus_gyro_bias_update(&f);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[0], fus_calib(&f)->gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[1], fus_calib(&f)->gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[2], fus_calib(&f)->gbias[2]);
}

/* Gravity as read by an IMU pitched 30° about body Y: (sin 30°, 0, cos 30°) g. Body -Y stays
 * perpendicular to that z (e_y · z = 0), so it can serve as the vehicle forward direction below. */
static const float TILT_RAD = 30.0f * 3.14159265358979f / 180.0f;   /* M_PI is not C11 */
static void tilted_gravity(float g[3])
{
    g[0] = sinf(TILT_RAD); g[1] = 0.0f; g[2] = cosf(TILT_RAD);
}

/* Fills one still window at the tilted attitude and captures the orientation. */
static void capture_tilted_orientation(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3])
{
    feed(f, o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, t);
    TEST_ASSERT_TRUE(fus_is_still(f));
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(f));
}

static void test_orientation_capture_from_tilted_gravity(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    /* Capture while moving is refused and leaves the calibration alone. */
    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_INT(-1, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->orient_ok);

    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));
    imu_raw_t clean = raw_at(t, grav, ZERO3);      /* noise-free sample, so only LSB rounding is left */
    fus_step(&f, &clean, &o);
    /* identity rows: the whole 30° tilt shows up as sign-less horizontal magnitude, sin 30° = 0.5 g.
     * 1e-3 g covers the 1/2048 g = 4.9e-4 g raw quantisation. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, o.g_lon);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);

    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    fus_step(&f, &clean, &o);                      /* the same sample through the captured rows */
    /* gravity is the z row now, so nothing is left in the horizontal plane. 1e-3 g covers the raw
     * quantisation (4.9e-4 g) plus the mean of the ±0.005 g window noise (0.005/sqrt(3·200) = 2e-4 g
     * per axis), which is all the captured z can be off by. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);            /* zero until forward is learned */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward row still unknown */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);      /* lean invalid before forward learned */
}

/* Three straight-line acceleration bursts along body -Y, with fus_calib_forward_step called at 5 Hz
 * exactly as the pipeline calls it (once per GPS fix). Counts the calls that returned 1 and -1.
 * The samples carry no noise: the forward row is then exact up to the raw quantisation. A constant
 * acceleration has no variance, so the stillness detector also calls these windows still — that is
 * inherent to a variance test and why the assertions below mask FUS_STILL out. */
static int run_forward_bursts(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3], int *neg)
{
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;        /* specific force of the burst, in g */
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    int ones = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        for (int i = 0; i < BURST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, acc, ZERO3, 0.0f, 0.0f, t);
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, grav, ZERO3, 0.0f, 0.0f, t);
        }
    }
    return ones;
}

static void test_forward_learning_from_straight_line_acceleration(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    TEST_ASSERT_EQUAL_INT(-1, fus_calib_forward_step(&f, BURST_ACC_MPS2, 0.0f));   /* before any capture */
    capture_tilted_orientation(&f, &o, &t, grav);

    int neg = 0;
    const int ones = run_forward_bursts(&f, &o, &t, grav, &neg);
    TEST_ASSERT_EQUAL_INT(1, ones);                /* reported exactly once, during the third burst */
    TEST_ASSERT_EQUAL_INT(0, neg);                 /* never -1 once the orientation is captured */
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
    TEST_ASSERT_TRUE(fus_fwd_ready(&f.fwd));

    /* The learned forward row is body -Y, which is perpendicular to the tilted z, so the burst's
     * specific force lands entirely on g_lon: 2 / 9.80665 = 0.2039 g. 2e-3 g covers the raw
     * quantisation of the sample (4.9e-4 g) and of the samples the row was learned from. */
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.2039f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lat);

    /* A left lateral specific force of 0.1 g: the body vector is 0.1 · y_row on top of gravity, and
     * lateral g is + to the right, so the output is -0.1 g. */
    const float *rows = fus_calib(&f)->r;
    const float lat[3] = { grav[0] + 0.1f * rows[3], grav[1] + 0.1f * rows[4], grav[2] + 0.1f * rows[5] };
    r = raw_at(t, lat, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, -0.1f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lon);
}

static void test_calibration_persists_through_the_calib_record_after_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));

    ses_calib_t rec; fus_calib_to_ses(fus_calib(&f), &rec);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_F_ORIENT | FUS_CALIB_F_FORWARD, rec.calib_flags);
    fus_calib_t decoded; fus_calib_from_ses(&rec, &decoded);
    TEST_ASSERT_TRUE(fus_calib_valid(&decoded));
    fus_t g; fus_init(&g, &decoded, 1);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&g)->forward_ok);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fused_sample_t restored;
    fus_step(&f, &r, &o);
    fus_step(&g, &r, &restored);
    /* 1e-3 g: the record stores each row entry as r × 1e4 rounded to int16, so a row entry moves by
     * up to 5e-5 and a 1 g sample by up to ~1e-4 g; the tolerance keeps a comfortable margin. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lon, restored.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lat, restored.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_comb, restored.g_comb);
    TEST_ASSERT_EQUAL_UINT8(o.flags & FUS_ORIENT_OK, restored.flags & FUS_ORIENT_OK);
}

static void test_recapture_resets_forward_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));
    TEST_ASSERT_EQUAL_UINT8(FWD_LEARN_WINDOWS, f.fwd.windows);

    /* The vehicle is re-mounted upright and captured again: the new z row invalidates the forward
     * row and every window accumulated against the old one. */
    const float upright[3] = { 0.0f, 0.0f, 1.0f };
    feed(&f, &o, FUS_STILL_WINDOW_N, upright, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));
    TEST_ASSERT_FALSE(f.fwd_learned_pending);
    imu_raw_t r = raw_at(t, upright, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward must be learned again */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
}

/* Degenerate accumulation (§9.2, fus_orient.c's FUS_FWD_MIN_NORM guard): when the accumulated
 * horizontal sum collapses to ~zero, fus_orient_set_forward refuses to learn a row and fus_step
 * restarts the tracker (fus_fwd_init) instead of leaving stale state behind. Two runs of exactly
 * opposite specific force are built as bit-exact int16 negations of each other, so ah(-a) = -ah(a)
 * to full float precision and the accumulated sum returns to exactly zero, not merely close to it.
 * FWD_LEARN_WINDOWS = 3 needs a third counted run; it alternates sign every sample (an even count),
 * which cancels to exactly zero by the time it is counted too, so the sum stays exactly zero right
 * through the trigger and the test is deterministic on every host. */
static void test_degenerate_forward_sum_restarts_the_tracker(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc_pos[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    const imu_raw_t r_pos = raw_at(t, acc_pos, ZERO3);
    imu_raw_t r_neg = r_pos;
    r_neg.ax = (int16_t)(-(int)r_pos.ax);
    r_neg.ay = (int16_t)(-(int)r_pos.ay);
    r_neg.az = (int16_t)(-(int)r_pos.az);

    int ones = 0, neg = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        const int n = (b < 2) ? BURST_SAMPLES : FUS_FWD_MIN_SAMPLES;
        for (int i = 0; i < n; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            imu_raw_t r;
            if (b == 0) r = r_pos;                     /* run 0: +X in the body plane */
            else if (b == 1) r = r_neg;                 /* run 1: -X, cancelling run 0 exactly */
            else r = (i % 2 == 0) ? r_pos : r_neg;      /* run 2: alternates, cancelling within itself */
            r.mono_us = t;
            fus_step(&f, &r, &o);
            t += 1000000 / FUSION_HZ;
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            feed(&f, &o, 1, grav, ZERO3, 0.0f, 0.0f, &t);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, ones);              /* the degenerate sum never reports a learned row */
    TEST_ASSERT_EQUAL_INT(0, neg);               /* orientation stays captured the whole time */
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);   /* fus_orient_set_forward's -1 restarted the tracker */
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));

    imu_raw_t clean = raw_at(t, grav, ZERO3);
    fus_step(&f, &clean, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);   /* forward still unlearned */

    /* A subsequent clean run still learns the forward row normally. */
    int neg2 = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg2));
    TEST_ASSERT_EQUAL_INT(0, neg2);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_identity_and_valid);
    RUN_TEST(test_invalid_calibration_is_rejected_and_init_falls_back);
    RUN_TEST(test_identity_mount_gravity_bias_and_yaw_sign);
    RUN_TEST(test_ninety_degree_mount_rotates_into_the_vehicle_frame);
    RUN_TEST(test_temperature_drift_marks_the_bias_stale);
    RUN_TEST(test_calibration_round_trips_through_the_calib_record);
    RUN_TEST(test_gps_speed_and_course_are_held_with_validity_and_time);
    RUN_TEST(test_gps_yaw_rate_helper_signs_and_wrap);
    RUN_TEST(test_gps_course_history_feeds_the_turn_rate);
    RUN_TEST(test_still_detection_and_gyro_bias_update);
    RUN_TEST(test_orientation_capture_from_tilted_gravity);
    RUN_TEST(test_forward_learning_from_straight_line_acceleration);
    RUN_TEST(test_calibration_persists_through_the_calib_record_after_learning);
    RUN_TEST(test_recapture_resets_forward_learning);
    RUN_TEST(test_degenerate_forward_sum_restarts_the_tracker);
    return UNITY_END();
}
```

- [ ] **Step 2: Update `components/core/include/core/fus.h`** to the full content below (widened setter, the helper declaration, the new `fus_t` fields).

`components/core/include/core/fus.h`:

```c
#ifndef CORE_FUS_H
#define CORE_FUS_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"
#include "core/ses.h"

/* Sensor fusion and calibration (spec §9.2–9.3). Pure C11, no allocation; all state in fus_t.
 *
 * Frames: body = the IMU's own axes as mounted; vehicle = X forward, Y left, Z up. The rotation R has
 * rows x, y, z = the vehicle axes expressed in body coordinates, so vehicle = R · body.
 * Units: accel in g (raw / IMU_ACC_LSB_PER_G); gyro in dps ((raw − gbias) / IMU_GYR_LSB_PER_DPS),
 * converted to rad/s only where the math needs it. Signs (core/types.h): g_lat and lean + right,
 * yaw + left turn, g_lon + accelerating.
 *
 * A zeroed fus_still_t or fus_fwd_t is a valid initialised state (the *_init functions memset). */

#define FUS_CALIB_VERSION   1
#define FUS_ORTHO_TOL       1e-3f                               /* row norm / dot tolerance for a valid R */
#define FUS_STILL_WINDOW_N  (STILL_WINDOW_S * FUSION_HZ)        /* 200 samples per stillness window */
#define FUS_FWD_MIN_SAMPLES (FWD_LEARN_MIN_S * FUSION_HZ)       /* 100 samples before a run counts */
#define FUS_CALIB_F_ORIENT  0x01                                /* ses_calib_t.calib_flags bits */
#define FUS_CALIB_F_FORWARD 0x02
#define FUS_CALIB_F_BIAS    0x04

typedef struct {
    float   r[9];             /* rows x, y, z (row-major) */
    float   gbias[3];         /* gyro bias, raw LSB */
    int16_t gbias_temp_c100;  /* IMU temperature when gbias was captured */
    uint8_t bias_ok;          /* gbias captured at least once */
    uint8_t orient_ok;        /* z row captured */
    uint8_t forward_ok;       /* x and y rows learned (implies orient_ok) */
    uint8_t version;          /* FUS_CALIB_VERSION */
} fus_calib_t;
void fus_calib_defaults(fus_calib_t *c);                        /* identity R, zero bias, flags 0, current version */
/* version matches, every value finite, |gbias| < 32768, forward_ok implies orient_ok, R orthonormal within FUS_ORTHO_TOL */
bool fus_calib_valid(const fus_calib_t *c);
/* CALIB record (§12.3): r × 1e4 → int16, gbias rounded and clamped to int16, flags FUS_CALIB_F_*. The record
 * carries no temperature: fus_calib_from_ses sets gbias_temp_c100 = 0 and version = FUS_CALIB_VERSION. */
void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out);
void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out);

/* ---- Stillness detector: tumbling windows of FUS_STILL_WINDOW_N raw samples (§9.2) ---- */
typedef struct {
    double   sum_amag, sum_amag2;    /* |accel| in g over the current window */
    double   sum_g[3], sum_g2[3];    /* gyro in dps (bias not removed) */
    double   sum_acc[3];             /* accel in g, for the orientation mean */
    double   sum_graw[3];            /* gyro raw LSB, for the bias mean */
    uint16_t n;                      /* samples in the current window */
    bool     have_window;            /* a window has completed at least once */
    bool     still;                  /* the last completed window was still */
    float    acc_var;                /* last completed window: variance of |a|, g² */
    float    gyr_var_max;            /* last completed window: largest gyro axis variance, dps² */
    float    mean_acc[3];            /* last completed window: mean accel, g (body) */
    float    mean_graw[3];           /* last completed window: mean gyro, raw LSB */
} fus_still_t;
void fus_still_init(fus_still_t *s);
/* Accumulates one raw sample. Returns 1 when this sample completed a window (the last-window fields are
 * then updated and the window restarts), else 0. still = acc_var < STILL_ACC_VAR && gyr_var_max < STILL_GYRO_VAR. */
int  fus_still_push(fus_still_t *s, const imu_raw_t *raw);

/* ---- Orientation rows (§9.2) ---- */
/* z = normalize(mean_acc); rows x, y become a provisional orthonormal completion (the body axis least aligned
 * with z, projected); orient_ok = 1, forward_ok = 0. Returns -1 (calib untouched) unless
 * FUS_ORIENT_MIN_G ≤ |mean_acc| ≤ FUS_ORIENT_MAX_G. */
int  fus_orient_from_gravity(fus_calib_t *c, const float mean_acc_g[3]);
/* x = normalize(sum_ah − (sum_ah·z)z), y = z × x, x = y × z; forward_ok = 1. Returns -1 (calib untouched) if
 * !orient_ok or the horizontal component is shorter than 1e-6. */
int  fus_orient_set_forward(fus_calib_t *c, const float sum_ah[3]);
void fus_rotate(const float r[9], const float b[3], float v[3]);   /* v = R · b */

/* ---- Forward-axis learning window tracker (§9.2) ---- */
typedef struct {
    bool     cond;            /* the latest fix qualifies: |yaw| < FWD_LEARN_MAX_YAW_DPS and gps_acc > FWD_LEARN_ACC_MPS2 */
    uint32_t run_samples;     /* consecutive fus_step samples with cond true */
    double   run_sum[3];      /* a_h accumulated during the current run (g, body) */
    double   sum[3];          /* a_h accumulated over counted runs */
    uint8_t  windows;         /* runs counted so far */
    bool     counted;         /* the current run has been counted (it reached FUS_FWD_MIN_SAMPLES) */
} fus_fwd_t;
void fus_fwd_init(fus_fwd_t *w);
/* Per GPS fix: sets cond. A run ends when cond turns false; its samples count only if it was counted. */
void fus_fwd_on_fix(fus_fwd_t *w, float gps_acc_mps2, float yaw_dps);
/* Per fus_step while orientation is known: accumulates a_h = a − (a·z)z when cond. When a run reaches
 * FUS_FWD_MIN_SAMPLES it is counted (its run_sum so far moves into sum, later samples add to sum directly).
 * Returns 1 exactly once, when the FWD_LEARN_WINDOWS-th run is counted; else 0. */
int  fus_fwd_on_sample(fus_fwd_t *w, const float acc_g[3], const float z[3]);
bool fus_fwd_ready(const fus_fwd_t *w);                          /* windows >= FWD_LEARN_WINDOWS */

/* ---- Fusion state ---- */
typedef struct {
    fus_calib_t calib;
    uint8_t     moto;                /* variant: 1 moto (lean filter), 0 car */
    fus_still_t still;
    fus_fwd_t   fwd;
    float       v_mps;               /* latest GPS speed (fus_set_gps_speed) */
    int64_t     v_mono_us;
    bool        v_valid;
    int16_t     temp_c100;           /* latest IMU temperature (fus_set_temp) */
    bool        temp_known;
    bool        bias_stale;          /* |temp − gbias_temp| > BIAS_TEMP_STALE_C since the last bias update */
    bool        fwd_learned_pending; /* set by fus_step when the forward row was just learned; consumed by fus_calib_forward_step */
    float       v_course_deg;        /* latest GPS course over ground (compass degrees) */
    float       lean_rad;            /* complementary-filter lean estimate (rad, + = right) */
    int64_t     last_ref_mono_us;    /* mono time a lean reference (phi_ref) was last applied; <0 = never */
    bool        have_prev_course;    /* a previous valid course is stored for the turn-rate derivative */
    float       prev_course_deg;     /* course of the previous valid fix */
    int64_t     prev_course_mono_us; /* arrival time of the previous valid fix */
    float       yaw_gps_dps;         /* last earth-frame turn rate from GPS courses (+ = left); NAN if none */
    int64_t     yaw_gps_mono_us;     /* arrival time the above was computed at; <0 = none */
    int64_t     disagree_since_mono_us; /* mono time the fused/GPS yaw disagreement began; <0 = agreeing */
    bool        disagree_latched;    /* FUS_DISAGREE has been raised at least once this session (log once) */
    uint32_t    samples;             /* fus_step calls since init */
} fus_t;

/* calib NULL or invalid → defaults. */
void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto);
/* Latest GPS-derived motion, from one NAV-PVT fix: ground speed (m/s), course over ground
 * (compass degrees, 0 = north, clockwise positive), the fix arrival time and its validity. On a
 * valid fix that follows another valid fix, the earth-frame turn rate yaw_gps_dps is recomputed
 * from the two courses (fus_yaw_rate_gps_dps) for the §9.3 cross-check. */
void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid);
/* Earth-frame turn rate implied by two consecutive GPS courses, in degrees/second, + = left turn
 * (compass heading increases clockwise, so a left turn lowers it): -wrap(cur - prev)/dt, the course
 * difference wrapped to (-180, 180]. Returns 0 when dt <= 0. */
float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us);
/* IMU temperature (~1 Hz from the pipeline). Sets bias_stale when a captured bias is more than
 * BIAS_TEMP_STALE_C away from its capture temperature. */
void fus_set_temp(fus_t *f, int16_t temp_c100);
/* Processes one raw sample into out; always produces a sample and returns 1. out->gps_us is left 0 for the
 * pipeline to fill from the time base. */
int  fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out);
bool fus_is_still(const fus_t *f);                               /* last completed stillness window was still */
/* gbias = last still window's mean raw gyro, gbias_temp = current temperature (0 if unknown), bias_ok = 1,
 * bias_stale cleared. No-op unless fus_is_still. */
void fus_gyro_bias_update(fus_t *f);
/* Upright capture (menu action): z row from the last still window's mean accel. 0 ok / -1 not still or
 * fus_orient_from_gravity rejected the mean. */
int  fus_calib_orient_capture(fus_t *f);
/* Per GPS fix: feeds the forward-learning tracker. Returns 1 when the forward row was just learned (caller
 * persists the calibration and emits EV_CALIB_DONE), 0 otherwise, -1 if orientation is not captured. */
int  fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps);
const fus_calib_t *fus_calib(const fus_t *f);
#endif
```

- [ ] **Step 3: Update `components/core/fusion/fus.c`** to the full content below (the `course_delta_deg` and `fus_yaw_rate_gps_dps` helpers, the widened setter that rolls the previous course and recomputes `yaw_gps_dps`, and the `fus_init` sentinels; `fus_step` is unchanged in this task).

`components/core/fusion/fus.c`:

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
    /* sentinels the zero from memset does not express (fus.h) */
    f->last_ref_mono_us = -1;
    f->yaw_gps_dps = NAN;
    f->yaw_gps_mono_us = -1;
    f->disagree_since_mono_us = -1;
}

/* Smallest signed compass difference cur - prev, wrapped to (-180, 180]. */
static float course_delta_deg(float cur_deg, float prev_deg)
{
    float d = fmodf(cur_deg - prev_deg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d <= -180.0f) d += 360.0f;
    return d;
}

float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us)
{
    const double dt_s = (double)(cur_mono_us - prev_mono_us) / 1e6;
    if (dt_s <= 0.0) return 0.0f;
    /* compass heading rises clockwise (a right turn), the vehicle yaw is + to the left, so negate */
    return (float)(-(double)course_delta_deg(cur_course_deg, prev_course_deg) / dt_s);
}

void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid)
{
    if (valid) {
        if (f->have_prev_course && mono_us > f->prev_course_mono_us) {
            f->yaw_gps_dps = fus_yaw_rate_gps_dps(f->prev_course_deg, f->prev_course_mono_us, course_deg, mono_us);
            f->yaw_gps_mono_us = mono_us;
        }
        f->prev_course_deg = course_deg; f->prev_course_mono_us = mono_us; f->have_prev_course = true;
    }
    f->v_mps = v_mps; f->v_course_deg = course_deg; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    /* Stillness runs on every raw sample (§9.3 step 8); the flags below report the last completed
     * tumbling window, which is what fus_is_still, the bias capture and drag arming all use. */
    fus_still_push(&f->still, raw);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;
    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */
        out->g_lat = -a[1];                      /* +Y is left; lateral g is + to the right (§9.3 step 5, car form) */
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;
    }
    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);
    out->lean_deg = 0.0f;                        /* lean filter arrives in session 2.3 */
    out->yaw_dps  = w[2];                        /* session 2.3 applies the lean correction of §9.3 step 3 */
    uint8_t flags = 0;
    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;
    out->flags = flags;

    /* Forward-axis learning (§9.2): only between the upright capture and the forward row being known.
     * The rows may change in this block, after this sample was already rotated with the old ones —
     * that is deliberate and harmless: the sample stays consistent with the calibration it was
     * computed from (sign-less g_lon, no FUS_ORIENT_OK) and the learned rows take effect from the
     * next sample, 10 ms later at FUSION_HZ. */
    if (f->calib.orient_ok && !f->calib.forward_ok) {
        const float z[3] = { f->calib.r[6], f->calib.r[7], f->calib.r[8] };
        if (fus_fwd_on_sample(&f->fwd, a_b, z) == 1) {
            const float sum[3] = { (float)f->fwd.sum[0], (float)f->fwd.sum[1], (float)f->fwd.sum[2] };
            if (fus_orient_set_forward(&f->calib, sum) == 0) {
                f->fwd_learned_pending = true;   /* fus_calib_forward_step reports it on the next fix */
            } else {
                fus_fwd_init(&f->fwd);           /* degenerate accumulation: drop it and learn again */
            }
        }
    }
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- stillness, bias and calibration entry points (wired to the modules above) ---- */

bool fus_is_still(const fus_t *f)
{
    return f->still.still;                       /* last completed window; false until one completes */
}

void fus_gyro_bias_update(fus_t *f)
{
    if (!fus_is_still(f)) return;                /* §9.2: the bias is only meaningful over a still window */
    for (int i = 0; i < 3; i++) f->calib.gbias[i] = f->still.mean_graw[i];
    /* 0 marks an unknown capture temperature; the bias reads stale once a temperature is known,
     * which is the conservative answer (it only prompts a recapture) and matches the same
     * convention fus_calib_from_ses uses for a restored bias. */
    f->calib.gbias_temp_c100 = f->temp_known ? f->temp_c100 : (int16_t)0;
    f->calib.bias_ok = 1;
    f->bias_stale = false;                       /* freshly captured at the current temperature */
}

int fus_calib_orient_capture(fus_t *f)
{
    if (!fus_is_still(f)) return -1;
    const int rc = fus_orient_from_gravity(&f->calib, f->still.mean_acc);
    if (rc == 0) {
        /* A new z row invalidates the forward row (fus_orient_from_gravity cleared forward_ok), so
         * everything accumulated against the old z is thrown away and learning starts again. */
        fus_fwd_init(&f->fwd);
        f->fwd_learned_pending = false;
    }
    return rc;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    if (!f->calib.orient_ok) return -1;          /* nothing to project against until z is captured */
    fus_fwd_on_fix(&f->fwd, gps_acc_mps2, yaw_dps);
    if (f->fwd_learned_pending) {                /* learning completes in fus_step; reported once here */
        f->fwd_learned_pending = false;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Build and run.** `cmake --build test/build --parallel && ctest --test-dir test/build --output-on-failure` → 24/24 (`test_fus` 15 cases). gcc parity likewise. ESP32 `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build` must still succeed.

- [ ] **Step 5: Spec write-back (§5.2).** In the `#### \`core/fus.h\` — fusion (plan 02)` block, replace the line `void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid);` with the widened signature and add, right after it, `float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us);   /* earth-frame turn rate from two GPS courses, + = left */`. Verify: `grep -c "fus_yaw_rate_gps_dps" docs/superpowers/specs/2026-09-14-lap-timer-design.md` prints 1.

- [ ] **Step 6: Hygiene and commit.** `git diff --check` empty.

```bash
git add components/core/include/core/fus.h components/core/fusion/fus.c test/test_fus.c docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit -m "feat(core): GPS course into fusion and the earth-frame turn-rate helper (§9.3 groundwork)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

### Task 2: §9.3 fusion math in `fus_step` — lean complementary filter, earth-frame yaw, lateral g, GPS cross-check (serial)

**Files:**
- Modify: `components/core/include/core/consts.h` (one new constant `LEAN_REF_TIMEOUT_S`), `components/core/fusion/fus.c` (the `fus_step` body; everything else is Task 1's, unchanged), `test/test_fus.c` (append six §9.3 cases — the fifteen from Task 1 are kept byte-for-byte), `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` (its `consts.h` block, to stay byte-identical to the file).
- **Header change needed: none.** `core/fus.h` as landed by Session 2.3 Task 1 is sufficient — every field the §9.3 math writes (`lean_rad`, `last_ref_mono_us`, `yaw_gps_dps`, `yaw_gps_mono_us`, `disagree_since_mono_us`, `disagree_latched`, `v_course_deg`) already exists, as does the widened `fus_set_gps_speed` and the `fus_yaw_rate_gps_dps` helper. This plan's Session 2.3 Task 1 code blocks are the record of what Task 1 landed and are **not** edited here.
- Spec reconciliation (no interface change): §9.3 step 5 is updated to document the moto lateral-g fallback to `−a.y` when no GPS reference qualifies, step 7 to note yaw is emitted every step, and the §9.1 pseudocode to show the widened `fus_set_gps_speed(v, course, mono, valid)`. These record what the causal implementation and the kept Task 1 tests require; no interface changes (**Contract change needed: none**).

**Interfaces:**
- Consumes (Task 1, verbatim): `fus_t` with `moto`, `calib`, `still`, `fwd`, `v_mps`, `v_mono_us`, `v_valid`, `v_course_deg`, `lean_rad`, `last_ref_mono_us`, `yaw_gps_dps`, `yaw_gps_mono_us`, `disagree_since_mono_us`, `disagree_latched`; `fus_set_gps_speed` (widened, rolls `yaw_gps_dps`); `fus_rotate`; `fus_still_push`; `fus_fwd_on_sample`; `fus_orient_set_forward`; `fus_fwd_init`. Constants (`core/consts.h`): `FUSION_HZ`, `G_MPS2`, `LEAN_ALPHA`, `LEAN_MAX_DEG`, `G_MAX`, `LEAN_DISAGREE_DPS`, `LEAN_DISAGREE_S`, `LEAN_REF_MIN_SPEED_MPS`, `FUS_REF_MAX_AGE_US`, and the new `LEAN_REF_TIMEOUT_S`. Flags/fields (`core/types.h`): `FUS_ORIENT_OK`, `FUS_STILL`, `FUS_BIAS_STALE`, `FUS_LEAN_VALID`, `FUS_CLAMPED`, `FUS_DISAGREE`; `fused_sample_t.{g_lon,g_lat,g_comb,lean_deg,yaw_dps,flags}`.
- Produces: `fus_step` now emits the §9.3 outputs — earth-frame `yaw_dps`, moto `lean_deg` (complementary filter), lateral g (moto centripetal / car specific force), `g_comb`, the `FUS_LEAN_VALID` / `FUS_CLAMPED` / `FUS_DISAGREE` flags, and the `disagree_latched` session latch. Signature unchanged.

**Spec §9.3 reconciliation (rulings — no spec or header change; the spec is the authority, these only fix reading ambiguities against the kept Task 1 tests):**

1. **Yaw is computed on every step, including the un-oriented branch.** §9.3 step 7 emits `yaw_dps = ψ̇·180/π`, and ψ̇ (step 3) needs only the rotated gyro and the previous lean. The kept Task 1 test `test_identity_mount_gravity_bias_and_yaw_sign` asserts `yaw_dps ≈ ω.z` on the very first sample *before* orientation is learned, and `test_still_detection_and_gyro_bias_update` asserts the un-oriented Z-gyro reading too. So `yaw_dps` is set unconditionally; when the sample is un-oriented (or the vehicle is a car) φ ≡ 0, so ψ̇ = ω.z and the value reproduces Task 1 exactly. The brief's aside "ensure yaw_dps is 0 [in the oriented-false branch]" is incompatible with those kept tests and is not taken; lean and lateral g **are** still suppressed while un-oriented (`lean_deg = 0`, `g_lat = 0`, `FUS_LEAN_VALID` clear), as the brief requires.

2. **Moto lateral g falls back to specific force when no GPS reference qualifies.** §9.3 step 5 moto form `g_lat = −v·ψ̇/9.80665` is used whenever the shared reference gate holds (valid, fresh `< FUS_REF_MAX_AGE_US`, speed `> LEAN_REF_MIN_SPEED_MPS`). With no usable GPS speed there is no centripetal estimate to report, so `g_lat` falls back to the accelerometer specific force `−a.y` — the Task 1 form and the only lateral estimate available without speed. This keeps the kept moto cases `test_identity_mount_gravity_bias_and_yaw_sign` and `test_ninety_degree_mount_rotates_into_the_vehicle_frame` (both moto, no GPS fed) green; every kept test happens never to feed a qualifying GPS speed, so all fifteen exercise the fallback and pass unchanged. The centripetal `−v·ψ̇/g` path is exercised by the new steady-turn cases.

3. **The G_MAX clamp is applied before `g_comb` is formed.** Appendix A calls G_MAX a "clamp in §9.3"; §9.3 lists `g_comb` (step 6) with no explicit clamp step. We clamp `g_lon` and `g_lat` to ±G_MAX first, then form `g_comb = sqrt(g_lat²+g_lon²)` from the clamped components, so the reported triple is self-consistent (a `g_comb` larger than either reported axis would be incoherent). Clamping any axis sets `FUS_CLAMPED`, as does the lean clamp to ±LEAN_MAX_DEG.

4. **`FUS_DISAGREE` is a level flag; `disagree_latched` is the session latch.** The flag is present on a sample only while the disagreement *currently* holds — a fresh GPS turn rate (`isfinite`, age `< FUS_REF_MAX_AGE_US`) above `LEAN_REF_MIN_SPEED_MPS`, with `|yaw_dps − yaw_gps_dps| > LEAN_DISAGREE_DPS` — and its timer has exceeded `LEAN_DISAGREE_S`. It clears the instant the difference drops back under threshold (or the GPS turn rate goes stale), which resets the timer to −1. `disagree_latched` latches `true` on the first raised flag and stays set for the session, so the plan-03 pipeline logs `E_FUSION_DISAGREE` once.

5. **`LEAN_REF_TIMEOUT_S = 5`** is added to `core/consts.h` (Step 1). It gates `FUS_LEAN_VALID` (set only while a reference was applied within the timeout, never before the first reference) and is deliberately distinct from `LEAN_DISAGREE_S` even though both are 5 s.

---

- [ ] **Step 1: Add `LEAN_REF_TIMEOUT_S` to `components/core/include/core/consts.h`.** Insert this line immediately after `#define LEAN_REF_MIN_SPEED_MPS 3.0f`:

```c
#define LEAN_REF_TIMEOUT_S     5
```

Then sync the plan-block: in `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md`, insert the same line in the same position in its `consts.h` code block (immediately after `#define LEAN_REF_MIN_SPEED_MPS 3.0f`), so that plan stays byte-identical to the file. Verify: `grep -c LEAN_REF_TIMEOUT_S components/core/include/core/consts.h` prints 1, and the same grep on the plan-01 file prints 1.

- [ ] **Step 2: Replace the `fus_step` body in `components/core/fusion/fus.c`.** Replace the placeholder lines (`out->lean_deg = 0.0f;`, `out->yaw_dps = w[2];`) and the two-branch g computation with the §9.3 math below. Two file-local `#define`s for the degree/radian factors are added after the includes (no bare literals in the algebra). Everything above `fus_step` and the trailer functions are Task 1's, unchanged.

`components/core/fusion/fus.c`:

```c
#include "core/fus.h"
#include <math.h>
#include <string.h>

/* Degree/radian conversion factors for the §9.3 lean/yaw algebra (no bare literals below). */
#define RAD_PER_DEG 0.017453292519943295f   /* pi / 180 */
#define DEG_PER_RAD 57.295779513082323f     /* 180 / pi */

/* ---- calibration ---- */

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool all_finite(const float *v, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(v[i])) return false;
    return true;
}

void fus_calib_defaults(fus_calib_t *c)
{
    memset(c, 0, sizeof *c);
    c->r[0] = 1.0f; c->r[4] = 1.0f; c->r[8] = 1.0f;
    c->version = FUS_CALIB_VERSION;
}

bool fus_calib_valid(const fus_calib_t *c)
{
    if (c->version != FUS_CALIB_VERSION) return false;
    if (!all_finite(c->r, 9) || !all_finite(c->gbias, 3)) return false;
    for (int i = 0; i < 3; i++) if (fabsf(c->gbias[i]) >= 32768.0f) return false;
    if (c->forward_ok && !c->orient_ok) return false;
    for (int i = 0; i < 3; i++) {
        const float *ri = c->r + 3 * i;
        if (fabsf(dot3(ri, ri) - 1.0f) > FUS_ORTHO_TOL) return false;
        for (int j = i + 1; j < 3; j++)
            if (fabsf(dot3(ri, c->r + 3 * j)) > FUS_ORTHO_TOL) return false;
    }
    return true;
}

static int16_t clamp_i16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lroundf(v);
}

void fus_calib_to_ses(const fus_calib_t *c, ses_calib_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 9; i++) out->r_e4[i] = clamp_i16(c->r[i] * 1e4f);
    for (int i = 0; i < 3; i++) out->gbias[i] = clamp_i16(c->gbias[i]);
    out->calib_flags = (uint8_t)((c->orient_ok ? FUS_CALIB_F_ORIENT : 0) |
                                 (c->forward_ok ? FUS_CALIB_F_FORWARD : 0) |
                                 (c->bias_ok ? FUS_CALIB_F_BIAS : 0));
}

void fus_calib_from_ses(const ses_calib_t *in, fus_calib_t *out)
{
    fus_calib_defaults(out);
    for (int i = 0; i < 9; i++) out->r[i] = (float)in->r_e4[i] * 1e-4f;
    for (int i = 0; i < 3; i++) out->gbias[i] = (float)in->gbias[i];
    out->orient_ok  = (in->calib_flags & FUS_CALIB_F_ORIENT) ? 1 : 0;
    out->forward_ok = (in->calib_flags & FUS_CALIB_F_FORWARD) ? 1 : 0;
    out->bias_ok    = (in->calib_flags & FUS_CALIB_F_BIAS) ? 1 : 0;
}

void fus_rotate(const float r[9], const float b[3], float v[3])
{
    for (int i = 0; i < 3; i++) v[i] = r[3 * i] * b[0] + r[3 * i + 1] * b[1] + r[3 * i + 2] * b[2];
}

/* ---- state ---- */

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto)
{
    memset(f, 0, sizeof *f);                 /* zeroed still/fwd trackers are initialised (fus.h) */
    if (calib && fus_calib_valid(calib)) f->calib = *calib;
    else fus_calib_defaults(&f->calib);
    f->moto = variant_is_moto ? 1 : 0;
    /* sentinels the zero from memset does not express (fus.h) */
    f->last_ref_mono_us = -1;
    f->yaw_gps_dps = NAN;
    f->yaw_gps_mono_us = -1;
    f->disagree_since_mono_us = -1;
}

/* Smallest signed compass difference cur - prev, wrapped to (-180, 180]. */
static float course_delta_deg(float cur_deg, float prev_deg)
{
    float d = fmodf(cur_deg - prev_deg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d <= -180.0f) d += 360.0f;
    return d;
}

float fus_yaw_rate_gps_dps(float prev_course_deg, int64_t prev_mono_us, float cur_course_deg, int64_t cur_mono_us)
{
    const double dt_s = (double)(cur_mono_us - prev_mono_us) / 1e6;
    if (dt_s <= 0.0) return 0.0f;
    /* compass heading rises clockwise (a right turn), the vehicle yaw is + to the left, so negate */
    return (float)(-(double)course_delta_deg(cur_course_deg, prev_course_deg) / dt_s);
}

void fus_set_gps_speed(fus_t *f, float v_mps, float course_deg, int64_t mono_us, bool valid)
{
    if (valid) {
        if (f->have_prev_course && mono_us > f->prev_course_mono_us) {
            f->yaw_gps_dps = fus_yaw_rate_gps_dps(f->prev_course_deg, f->prev_course_mono_us, course_deg, mono_us);
            f->yaw_gps_mono_us = mono_us;
        }
        f->prev_course_deg = course_deg; f->prev_course_mono_us = mono_us; f->have_prev_course = true;
    }
    f->v_mps = v_mps; f->v_course_deg = course_deg; f->v_mono_us = mono_us; f->v_valid = valid;
}

static void update_bias_stale(fus_t *f)
{
    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
    int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
    if (d < 0) d = -d;
    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
}

void fus_set_temp(fus_t *f, int16_t temp_c100)
{
    f->temp_c100 = temp_c100; f->temp_known = true;
    update_bias_stale(f);
}

int fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out)
{
    const float a_b[3] = { (float)raw->ax / IMU_ACC_LSB_PER_G, (float)raw->ay / IMU_ACC_LSB_PER_G, (float)raw->az / IMU_ACC_LSB_PER_G };
    const float w_b[3] = { ((float)raw->gx - f->calib.gbias[0]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gy - f->calib.gbias[1]) / IMU_GYR_LSB_PER_DPS,
                           ((float)raw->gz - f->calib.gbias[2]) / IMU_GYR_LSB_PER_DPS };
    float a[3], w[3];
    fus_rotate(f->calib.r, a_b, a);
    fus_rotate(f->calib.r, w_b, w);

    /* Stillness runs on every raw sample (§9.3 step 8); the flags below report the last completed
     * tumbling window, which is what fus_is_still, the bias capture and drag arming all use. */
    fus_still_push(&f->still, raw);

    memset(out, 0, sizeof *out);
    out->mono_us = raw->mono_us;
    const bool oriented = f->calib.orient_ok && f->calib.forward_ok;

    /* §9.3 steps 1/3: rotated gyro to rad/s and the earth-frame yaw rate psi-dot. phi is the
     * PREVIOUS step's lean (a causal filter breaks the phi -> psi-dot -> phi_ref -> phi loop); the
     * car variant and any un-oriented moto sample use phi == 0, so psi-dot reduces to omega.z. */
    const float dt       = 1.0f / (float)FUSION_HZ;      /* 0.01 s at FUSION_HZ */
    const float phi_prev = (f->moto && oriented) ? f->lean_rad : 0.0f;
    const float wx = w[0] * RAD_PER_DEG;                 /* body-forward roll rate, rad/s */
    const float wy = w[1] * RAD_PER_DEG;
    const float wz = w[2] * RAD_PER_DEG;
    const float psidot = wz * cosf(phi_prev) + wy * sinf(phi_prev);   /* rad/s, earth frame */

    uint8_t flags = 0;

    if (oriented) {
        out->g_lon = a[0];                       /* specific force along forward, in g (§9.3 step 2) */

        /* GPS reference gate shared by the lean phi_ref and the moto lateral g (§9.3 steps 4/5):
         * a valid, fresh (0 <= age < FUS_REF_MAX_AGE_US) fix above LEAN_REF_MIN_SPEED_MPS. */
        const int64_t v_age = raw->mono_us - f->v_mono_us;
        const bool ref_ok = f->v_valid && f->v_mps > LEAN_REF_MIN_SPEED_MPS &&
                            v_age >= 0 && v_age < FUS_REF_MAX_AGE_US;

        if (f->moto) {
            /* Complementary lean filter (§9.3 step 4): gyro-integrated roll blended toward the
             * GPS-derived reference when one qualifies, else pure gyro (alpha == 1). */
            float phi = f->lean_rad + wx * dt;   /* phi_gyro */
            if (ref_ok) {
                const float phi_ref = atan2f(-f->v_mps * psidot, (float)G_MPS2);
                phi = LEAN_ALPHA * phi + (1.0f - LEAN_ALPHA) * phi_ref;
                f->last_ref_mono_us = raw->mono_us;
            }
            const float lean_max = LEAN_MAX_DEG * RAD_PER_DEG;
            if (phi > lean_max)       { phi =  lean_max; flags |= FUS_CLAMPED; }
            else if (phi < -lean_max) { phi = -lean_max; flags |= FUS_CLAMPED; }
            f->lean_rad = phi;
            out->lean_deg = phi * DEG_PER_RAD;

            /* Lateral g (§9.3 step 5, moto): centripetal from the held speed and the fused turn rate
             * when a reference qualifies; otherwise the accelerometer specific force (the Task 1
             * form), which is all that is available with no usable GPS speed. + = right. */
            out->g_lat = ref_ok ? (-f->v_mps * psidot / (float)G_MPS2) : -a[1];

            /* FUS_LEAN_VALID: a reference was applied within LEAN_REF_TIMEOUT_S; never before the
             * first reference (last_ref_mono_us starts at -1). */
            if (f->last_ref_mono_us >= 0 &&
                raw->mono_us - f->last_ref_mono_us < (int64_t)LEAN_REF_TIMEOUT_S * 1000000)
                flags |= FUS_LEAN_VALID;
        } else {
            /* Car (§9.3 step 5, car): lateral g is the accelerometer specific force. Lean is not
             * meaningful for a car, so lean_deg stays 0 and FUS_LEAN_VALID is never set. */
            out->g_lat = -a[1];
        }

        /* G_MAX clamp (Appendix A, "clamp in §9.3"): bound each in-plane axis to +-G_MAX *before*
         * forming g_comb, so the reported combined magnitude stays consistent with the reported
         * (clamped) components. */
        if (out->g_lon > G_MAX)       { out->g_lon =  G_MAX; flags |= FUS_CLAMPED; }
        else if (out->g_lon < -G_MAX) { out->g_lon = -G_MAX; flags |= FUS_CLAMPED; }
        if (out->g_lat > G_MAX)       { out->g_lat =  G_MAX; flags |= FUS_CLAMPED; }
        else if (out->g_lat < -G_MAX) { out->g_lat = -G_MAX; flags |= FUS_CLAMPED; }
    } else {
        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
        out->g_lat = 0.0f;                       /* no lateral, no lean, FUS_LEAN_VALID clear until oriented */
    }

    out->g_comb  = sqrtf(out->g_lon * out->g_lon + out->g_lat * out->g_lat);   /* §9.3 step 6 */
    out->yaw_dps = psidot * DEG_PER_RAD;         /* §9.3 step 7; computed every step, incl. un-oriented */

    if (oriented) flags |= FUS_ORIENT_OK;
    if (f->still.still) flags |= FUS_STILL;
    if (f->bias_stale) flags |= FUS_BIAS_STALE;

    /* GPS yaw cross-check (§9.3 step 4). While a fresh GPS turn rate exists above the reference
     * speed, a fused/GPS yaw disagreement beyond LEAN_DISAGREE_DPS that holds for LEAN_DISAGREE_S
     * sets FUS_DISAGREE and latches disagree_latched (the plan-03 pipeline logs E_FUSION_DISAGREE
     * once from the latch). Reading of the spec's "for 5 s": the flag is present only while the
     * disagreement currently holds and its timer has exceeded the hold, and clears as soon as the
     * difference drops back under threshold (the timer resets to -1); disagree_latched stays set. */
    const int64_t yaw_age = raw->mono_us - f->yaw_gps_mono_us;
    const bool yaw_ref_ok = f->v_valid && isfinite(f->yaw_gps_dps) &&
                            yaw_age >= 0 && yaw_age < FUS_REF_MAX_AGE_US &&
                            f->v_mps > LEAN_REF_MIN_SPEED_MPS;
    if (yaw_ref_ok && fabsf(out->yaw_dps - f->yaw_gps_dps) > LEAN_DISAGREE_DPS) {
        if (f->disagree_since_mono_us < 0) f->disagree_since_mono_us = raw->mono_us;
        if (raw->mono_us - f->disagree_since_mono_us >= (int64_t)LEAN_DISAGREE_S * 1000000) {
            flags |= FUS_DISAGREE;
            f->disagree_latched = true;
        }
    } else {
        f->disagree_since_mono_us = -1;          /* agreeing, or no fresh GPS turn rate: reset */
    }

    out->flags = flags;

    /* Forward-axis learning (§9.2): only between the upright capture and the forward row being known.
     * The rows may change in this block, after this sample was already rotated with the old ones —
     * that is deliberate and harmless: the sample stays consistent with the calibration it was
     * computed from (sign-less g_lon, no FUS_ORIENT_OK) and the learned rows take effect from the
     * next sample, 10 ms later at FUSION_HZ. */
    if (f->calib.orient_ok && !f->calib.forward_ok) {
        const float z[3] = { f->calib.r[6], f->calib.r[7], f->calib.r[8] };
        if (fus_fwd_on_sample(&f->fwd, a_b, z) == 1) {
            const float sum[3] = { (float)f->fwd.sum[0], (float)f->fwd.sum[1], (float)f->fwd.sum[2] };
            if (fus_orient_set_forward(&f->calib, sum) == 0) {
                f->fwd_learned_pending = true;   /* fus_calib_forward_step reports it on the next fix */
            } else {
                fus_fwd_init(&f->fwd);           /* degenerate accumulation: drop it and learn again */
            }
        }
    }
    f->samples++;
    return 1;
}

const fus_calib_t *fus_calib(const fus_t *f)
{
    return &f->calib;
}

/* ---- stillness, bias and calibration entry points (wired to the modules above) ---- */

bool fus_is_still(const fus_t *f)
{
    return f->still.still;                       /* last completed window; false until one completes */
}

void fus_gyro_bias_update(fus_t *f)
{
    if (!fus_is_still(f)) return;                /* §9.2: the bias is only meaningful over a still window */
    for (int i = 0; i < 3; i++) f->calib.gbias[i] = f->still.mean_graw[i];
    /* 0 marks an unknown capture temperature; the bias reads stale once a temperature is known,
     * which is the conservative answer (it only prompts a recapture) and matches the same
     * convention fus_calib_from_ses uses for a restored bias. */
    f->calib.gbias_temp_c100 = f->temp_known ? f->temp_c100 : (int16_t)0;
    f->calib.bias_ok = 1;
    f->bias_stale = false;                       /* freshly captured at the current temperature */
}

int fus_calib_orient_capture(fus_t *f)
{
    if (!fus_is_still(f)) return -1;
    const int rc = fus_orient_from_gravity(&f->calib, f->still.mean_acc);
    if (rc == 0) {
        /* A new z row invalidates the forward row (fus_orient_from_gravity cleared forward_ok), so
         * everything accumulated against the old z is thrown away and learning starts again. */
        fus_fwd_init(&f->fwd);
        f->fwd_learned_pending = false;
    }
    return rc;
}

int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)
{
    if (!f->calib.orient_ok) return -1;          /* nothing to project against until z is captured */
    fus_fwd_on_fix(&f->fwd, gps_acc_mps2, yaw_dps);
    if (f->fwd_learned_pending) {                /* learning completes in fus_step; reported once here */
        f->fwd_learned_pending = false;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 3: Update `test/test_fus.c`** to the full content below. The first fifteen cases are Task 1's, byte-for-byte; six §9.3 cases and their helpers (`raw_tg`, `raw_turn`, `calibrated_identity`, `drive_turn`, and the `GZ_RIGHT`/`GZ_LEFT` constants) are appended before `main`, and the six new `RUN_TEST` lines are added. Every analytic expectation carries its tolerance's origin in a comment.

`test/test_fus.c`:

```c
#include "unity.h"
#include "core/fus.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Raw sample helpers: accel in g and gyro in dps expressed as MPU-6050 LSB (types.h scales). */
static imu_raw_t raw_g_dps(double ax_g, double ay_g, double az_g, double gx, double gy, double gz)
{
    imu_raw_t r;
    r.mono_us = 1000000;
    r.ax = (int16_t)lround(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lround(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lround(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lround(gx * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lround(gy * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lround(gz * IMU_GYR_LSB_PER_DPS);
    return r;
}

static void test_defaults_are_identity_and_valid(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, c.version);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[0]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[4]); TEST_ASSERT_EQUAL_FLOAT(1.0f, c.r[8]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, c.orient_ok); TEST_ASSERT_EQUAL_UINT8(0, c.forward_ok); TEST_ASSERT_EQUAL_UINT8(0, c.bias_ok);
}

static void test_invalid_calibration_is_rejected_and_init_falls_back(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.version = 0;
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[0] = 2.0f;                       /* row x not unit length */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.r[3] = 1.0f;                       /* row y = (1,0,0) parallel to row x */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.forward_ok = 1;                    /* forward without orientation */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));
    fus_calib_defaults(&c); c.gbias[1] = 40000.0f;               /* beyond the raw range */
    TEST_ASSERT_FALSE(fus_calib_valid(&c));

    fus_t f; c.version = 0;
    fus_init(&f, &c, 1);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fus_calib(&f)->r[0]);
    fus_init(&f, NULL, 0);
    TEST_ASSERT_TRUE(fus_calib_valid(fus_calib(&f)));
    TEST_ASSERT_EQUAL_UINT8(0, f.moto);
}

static void test_identity_mount_gravity_bias_and_yaw_sign(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.gbias[0] = 5.0f * IMU_GYR_LSB_PER_DPS;                     /* 5 dps bias on X */
    c.bias_ok = 1;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    imu_raw_t r = raw_g_dps(0.0, 0.0, 1.0, 5.0, 0.0, 10.0);
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_EQUAL_INT64(1000000, o.mono_us);
    /* orientation not learned: sign-less horizontal magnitude, no lateral, no lean, ORIENT_OK clear */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & (FUS_ORIENT_OK | FUS_LEAN_VALID));
    /* yaw: +10 dps about body Z = left turn, bias-free because the bias is on X only */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);

    c.orient_ok = 1; c.forward_ok = 1;                            /* identity mount fully known */
    fus_init(&f, &c, 1);
    r = raw_g_dps(0.3, 0.5, 1.0, 5.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    /* accel tolerance 1e-3 g: raw LSB quantisation is 1/2048 g ≈ 4.9e-4 g */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* +X forward: accelerating */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, o.g_lat);             /* +Y is left, so lateral g is -a.y */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(0.34f), o.g_comb);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.yaw_dps);            /* 5 dps on X minus the 5 dps bias */
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_EQUAL_UINT32(1, f.samples);
}

static void test_ninety_degree_mount_rotates_into_the_vehicle_frame(void)
{
    /* IMU mounted with body +X pointing left (vehicle +Y) and body +Y pointing backwards (vehicle -X);
     * body +Z up. Rows are the vehicle axes in body coordinates: x = (0,-1,0), y = (1,0,0), z = (0,0,1). */
    fus_calib_t c; fus_calib_defaults(&c);
    const float R[9] = { 0.0f, -1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.orient_ok = 1; c.forward_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o;
    /* vehicle accelerating at 0.3 g forward appears on body -Y; a 10 dps left turn is body +Z */
    imu_raw_t r = raw_g_dps(0.0, -0.3, 1.0, 0.0, 0.0, 10.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, o.g_lon);              /* 1e-3 g: LSB quantisation */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    /* a right-hand lateral specific force (vehicle -Y = body -X) reads as positive g_lat */
    r = raw_g_dps(-0.4, 0.0, 1.0, 0.0, 0.0, 0.0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, o.g_lon);

    float v[3]; const float b[3] = { 1.0f, 2.0f, 3.0f };
    fus_rotate(R, b, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, v[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, v[2]);
}

static void test_temperature_drift_marks_the_bias_stale(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    c.bias_ok = 1; c.gbias_temp_c100 = 2500;
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; imu_raw_t r = raw_g_dps(0, 0, 1, 0, 0, 0);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);        /* temperature unknown: not stale */
    fus_set_temp(&f, 3900);                                      /* 14 °C away: within BIAS_TEMP_STALE_C */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 4100);                                      /* 16 °C away */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    fus_set_temp(&f, 900);                                       /* 16 °C the other way */
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_BIAS_STALE, o.flags & FUS_BIAS_STALE);
    /* no bias captured: temperature can never make it stale */
    fus_calib_defaults(&c); fus_init(&f, &c, 1); fus_set_temp(&f, 9000);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);
}

static void test_calibration_round_trips_through_the_calib_record(void)
{
    fus_calib_t c; fus_calib_defaults(&c);
    const float ang = 30.0f * 3.14159265358979f / 180.0f;         /* rotation about Z by 30° (M_PI is not C11) */
    const float R[9] = { cosf(ang), sinf(ang), 0.0f,  -sinf(ang), cosf(ang), 0.0f,  0.0f, 0.0f, 1.0f };
    memcpy(c.r, R, sizeof R);
    c.gbias[0] = 12.4f; c.gbias[1] = -7.6f; c.gbias[2] = 0.4f; c.gbias_temp_c100 = 2712;
    c.orient_ok = 1; c.forward_ok = 1; c.bias_ok = 1;
    TEST_ASSERT_TRUE(fus_calib_valid(&c));
    ses_calib_t w; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(8660, w.r_e4[0]);                     /* cos 30° × 1e4 rounded */
    TEST_ASSERT_EQUAL_INT16(5000, w.r_e4[1]);
    TEST_ASSERT_EQUAL_INT16(12, w.gbias[0]); TEST_ASSERT_EQUAL_INT16(-8, w.gbias[1]); TEST_ASSERT_EQUAL_INT16(0, w.gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(0x07, w.calib_flags);
    fus_calib_t d; fus_calib_from_ses(&w, &d);
    for (int i = 0; i < 9; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, c.r[i], d.r[i]);
    TEST_ASSERT_TRUE(fus_calib_valid(&d));                        /* 1e-4 quantisation stays inside FUS_ORTHO_TOL */
    TEST_ASSERT_EQUAL_FLOAT(12.0f, d.gbias[0]);
    TEST_ASSERT_EQUAL_UINT8(1, d.orient_ok); TEST_ASSERT_EQUAL_UINT8(1, d.forward_ok); TEST_ASSERT_EQUAL_UINT8(1, d.bias_ok);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_VERSION, d.version);
    TEST_ASSERT_EQUAL_INT16(0, d.gbias_temp_c100);                 /* not carried by the record */
    /* a saturating bias clamps instead of wrapping */
    c.gbias[2] = 40000.0f; fus_calib_to_ses(&c, &w);
    TEST_ASSERT_EQUAL_INT16(32767, w.gbias[2]);
}

static void test_gps_speed_and_course_are_held_with_validity_and_time(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_gps_speed(&f, 27.5f, 91.0f, 5000000, true);
    TEST_ASSERT_EQUAL_FLOAT(27.5f, f.v_mps); TEST_ASSERT_EQUAL_FLOAT(91.0f, f.v_course_deg);
    TEST_ASSERT_EQUAL_INT64(5000000, f.v_mono_us); TEST_ASSERT_TRUE(f.v_valid);
    fus_set_gps_speed(&f, 0.0f, 0.0f, 5200000, false);
    TEST_ASSERT_FALSE(f.v_valid);
}

static void test_gps_yaw_rate_helper_signs_and_wrap(void)
{
    /* straight: no course change */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fus_yaw_rate_gps_dps(90.0f, 0, 90.0f, 1000000));
    /* right turn: compass rises 10->20 over 1 s -> yaw negative (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -10.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 1000000));
    /* left turn: compass falls 20->10 -> yaw positive (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, fus_yaw_rate_gps_dps(20.0f, 0, 10.0f, 1000000));
    /* wrap, right turn across north: 350 -> 10 is +20 deg clockwise */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(350.0f, 0, 10.0f, 1000000));
    /* wrap, left turn across north: 10 -> 350 is -20 deg */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 350.0f, 1000000));
    /* half a second doubles the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, fus_yaw_rate_gps_dps(10.0f, 0, 20.0f, 500000));
    /* non-positive dt -> 0 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 1000000, 20.0f, 1000000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fus_yaw_rate_gps_dps(10.0f, 2000000, 20.0f, 1000000));
}

static void test_gps_course_history_feeds_the_turn_rate(void)
{
    fus_t f; fus_init(&f, NULL, 1);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* NAN until two valid fixes */
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1000000, true);
    TEST_ASSERT_FALSE(isfinite(f.yaw_gps_dps));            /* one fix: still none */
    fus_set_gps_speed(&f, 30.0f, 106.0f, 1200000, true);  /* +6 deg in 0.2 s -> -30 dps (right) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    TEST_ASSERT_EQUAL_INT64(1200000, f.yaw_gps_mono_us);
    fus_set_gps_speed(&f, 30.0f, 999.0f, 1300000, false); /* invalid: does not update prev or the rate */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, f.yaw_gps_dps);
    fus_set_gps_speed(&f, 30.0f, 100.0f, 1400000, true);  /* -6 deg from 106 over 0.2 s -> +30 dps (left) */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.0f, f.yaw_gps_dps);
}

/* ---- integration cases: stillness, bias, orientation capture and forward learning (§22.1) ---- */

#define BURST_SAMPLES   120                       /* 1.2 s of acceleration at FUSION_HZ */
#define COAST_SAMPLES    50                       /* 0.5 s of coasting between bursts */
#define FIX_EVERY        (FUSION_HZ / 5)          /* one GPS fix every 20 samples = 5 Hz */
#define BURST_ACC_MPS2   2.0f                     /* > FWD_LEARN_ACC_MPS2, so the fix qualifies */
#define BURST_YAW_DPS    0.5f                     /* < FWD_LEARN_MAX_YAW_DPS, so the fix qualifies */

/* Deterministic LCG (Numerical Recipes constants): the noise sequence must be identical on every
 * host and on the ESP32, so no rand() and no library state. */
static uint32_t lcg_state;
static void lcg_reset(void) { lcg_state = 22222u; }
static float noise_pm(float amp)                  /* uniform in [-amp, +amp) */
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    const float u = (float)(lcg_state >> 8) / 16777216.0f;   /* top 24 bits → [0,1) */
    return amp * (2.0f * u - 1.0f);
}

/* Raw sample from accel in g and gyro in raw LSB (the gyro bias lives in LSB, §9.2). */
static imu_raw_t raw_at(int64_t mono_us, const float a_g[3], const float g_lsb[3])
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = (int16_t)lroundf(a_g[0] * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lroundf(a_g[1] * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lroundf(a_g[2] * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lroundf(g_lsb[0]);
    r.gy = (int16_t)lroundf(g_lsb[1]);
    r.gz = (int16_t)lroundf(g_lsb[2]);
    return r;
}

/* Feeds n samples at FUSION_HZ, each = (a_g, g_lsb) plus uniform noise, advancing *mono_us. */
static void feed(fus_t *f, fused_sample_t *o, int n, const float a_g[3], const float g_lsb[3],
                 float a_noise_g, float g_noise_lsb, int64_t *mono_us)
{
    for (int i = 0; i < n; i++) {
        /* Sequential declarations are sequenced (unlike the three noise_pm() calls in one brace
         * initializer would be), so each axis draws its LCG value in a fixed, portable order. */
        const float na0 = noise_pm(a_noise_g), na1 = noise_pm(a_noise_g), na2 = noise_pm(a_noise_g);
        const float an[3] = { a_g[0] + na0, a_g[1] + na1, a_g[2] + na2 };
        const float ng0 = noise_pm(g_noise_lsb), ng1 = noise_pm(g_noise_lsb), ng2 = noise_pm(g_noise_lsb);
        const float gn[3] = { g_lsb[0] + ng0, g_lsb[1] + ng1, g_lsb[2] + ng2 };
        imu_raw_t r = raw_at(*mono_us, an, gn);
        fus_step(f, &r, o);
        *mono_us += 1000000 / FUSION_HZ;
    }
}

static const float ZERO3[3] = { 0.0f, 0.0f, 0.0f };
static const float QUIET_ACC_NOISE_G = 0.005f;    /* var 8.3e-6 g² ≪ STILL_ACC_VAR = 4e-4 g² */
static const float QUIET_GYR_NOISE_LSB = 0.5f;    /* var 3.1e-4 dps² ≪ STILL_GYRO_VAR = 4 dps² */
static const float MOVING_GYR_NOISE_LSB = 5.0f * IMU_GYR_LSB_PER_DPS;  /* ±5 dps → var 8.3 dps² > 4 */

static void test_still_detection_and_gyro_bias_update(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_temp(&f, 2500);                       /* the pipeline's ~1 Hz poll, before the capture */
    fused_sample_t o; int64_t t = 1000000;
    const float quiet_a[3] = { 0.0f, 0.0f, 1.0f };
    const float bias_lsb[3] = { 20.0f, -8.0f, 3.0f };   /* the gyro bias the window must recover */

    feed(&f, &o, FUS_STILL_WINDOW_N - 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));           /* 199 samples: no window has completed */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    /* with no bias captured the 3 LSB on Z read as yaw: 3 / 16.4 = 0.183 dps. Tolerance 0.02 dps =
     * 0.33 LSB, more than the ±0.5 LSB noise can survive the int16 rounding of the raw sample. */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.0f / IMU_GYR_LSB_PER_DPS, o.yaw_dps);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));            /* the 200th sample closes the window */
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);
    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_UINT8(FUS_STILL, o.flags & FUS_STILL);   /* the verdict holds until the next window */

    fus_gyro_bias_update(&f);
    /* Tolerance 0.1 LSB: the ±0.5 LSB noise is zero-mean, so the mean of 200 samples has a standard
     * error of 0.5/sqrt(3·200) ≈ 0.02 LSB, and the int16 rounding of the raw sample removes most of
     * the noise before it is ever averaged. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, fus_calib(&f)->gbias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -8.0f, fus_calib(&f)->gbias[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, fus_calib(&f)->gbias[2]);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->bias_ok);
    TEST_ASSERT_EQUAL_INT16(2500, fus_calib(&f)->gbias_temp_c100);
    TEST_ASSERT_FALSE(f.bias_stale);

    feed(&f, &o, 1, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, o.yaw_dps);           /* 0.1 dps = 1.6 LSB of headroom */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_BIAS_STALE);      /* captured at the current temperature */

    /* A moving window (±5 dps of gyro) is not still, so the bias must not move. */
    const fus_calib_t before = *fus_calib(&f);
    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, bias_lsb, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_STILL);
    fus_gyro_bias_update(&f);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[0], fus_calib(&f)->gbias[0]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[1], fus_calib(&f)->gbias[1]);
    TEST_ASSERT_EQUAL_FLOAT(before.gbias[2], fus_calib(&f)->gbias[2]);
}

/* Gravity as read by an IMU pitched 30° about body Y: (sin 30°, 0, cos 30°) g. Body -Y stays
 * perpendicular to that z (e_y · z = 0), so it can serve as the vehicle forward direction below. */
static const float TILT_RAD = 30.0f * 3.14159265358979f / 180.0f;   /* M_PI is not C11 */
static void tilted_gravity(float g[3])
{
    g[0] = sinf(TILT_RAD); g[1] = 0.0f; g[2] = cosf(TILT_RAD);
}

/* Fills one still window at the tilted attitude and captures the orientation. */
static void capture_tilted_orientation(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3])
{
    feed(f, o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, t);
    TEST_ASSERT_TRUE(fus_is_still(f));
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(f));
}

static void test_orientation_capture_from_tilted_gravity(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    /* Capture while moving is refused and leaves the calibration alone. */
    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, MOVING_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(fus_is_still(&f));
    TEST_ASSERT_EQUAL_INT(-1, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->orient_ok);

    feed(&f, &o, FUS_STILL_WINDOW_N, grav, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));
    imu_raw_t clean = raw_at(t, grav, ZERO3);      /* noise-free sample, so only LSB rounding is left */
    fus_step(&f, &clean, &o);
    /* identity rows: the whole 30° tilt shows up as sign-less horizontal magnitude, sin 30° = 0.5 g.
     * 1e-3 g covers the 1/2048 g = 4.9e-4 g raw quantisation. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, o.g_lon);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);

    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    fus_step(&f, &clean, &o);                      /* the same sample through the captured rows */
    /* gravity is the z row now, so nothing is left in the horizontal plane. 1e-3 g covers the raw
     * quantisation (4.9e-4 g) plus the mean of the ±0.005 g window noise (0.005/sqrt(3·200) = 2e-4 g
     * per axis), which is all the captured z can be off by. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.g_lat);            /* zero until forward is learned */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward row still unknown */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);      /* lean invalid before forward learned */
}

/* Three straight-line acceleration bursts along body -Y, with fus_calib_forward_step called at 5 Hz
 * exactly as the pipeline calls it (once per GPS fix). Counts the calls that returned 1 and -1.
 * The samples carry no noise: the forward row is then exact up to the raw quantisation. A constant
 * acceleration has no variance, so the stillness detector also calls these windows still — that is
 * inherent to a variance test and why the assertions below mask FUS_STILL out. */
static int run_forward_bursts(fus_t *f, fused_sample_t *o, int64_t *t, const float grav[3], int *neg)
{
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;        /* specific force of the burst, in g */
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    int ones = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        for (int i = 0; i < BURST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, acc, ZERO3, 0.0f, 0.0f, t);
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) (*neg)++;
            }
            feed(f, o, 1, grav, ZERO3, 0.0f, 0.0f, t);
        }
    }
    return ones;
}

static void test_forward_learning_from_straight_line_acceleration(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);

    TEST_ASSERT_EQUAL_INT(-1, fus_calib_forward_step(&f, BURST_ACC_MPS2, 0.0f));   /* before any capture */
    capture_tilted_orientation(&f, &o, &t, grav);

    int neg = 0;
    const int ones = run_forward_bursts(&f, &o, &t, grav, &neg);
    TEST_ASSERT_EQUAL_INT(1, ones);                /* reported exactly once, during the third burst */
    TEST_ASSERT_EQUAL_INT(0, neg);                 /* never -1 once the orientation is captured */
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
    TEST_ASSERT_TRUE(fus_fwd_ready(&f.fwd));

    /* The learned forward row is body -Y, which is perpendicular to the tilted z, so the burst's
     * specific force lands entirely on g_lon: 2 / 9.80665 = 0.2039 g. 2e-3 g covers the raw
     * quantisation of the sample (4.9e-4 g) and of the samples the row was learned from. */
    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(FUS_ORIENT_OK, o.flags & FUS_ORIENT_OK);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.2039f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lat);

    /* A left lateral specific force of 0.1 g: the body vector is 0.1 · y_row on top of gravity, and
     * lateral g is + to the right, so the output is -0.1 g. */
    const float *rows = fus_calib(&f)->r;
    const float lat[3] = { grav[0] + 0.1f * rows[3], grav[1] + 0.1f * rows[4], grav[2] + 0.1f * rows[5] };
    r = raw_at(t, lat, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, -0.1f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, o.g_lon);
}

static void test_calibration_persists_through_the_calib_record_after_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));

    ses_calib_t rec; fus_calib_to_ses(fus_calib(&f), &rec);
    TEST_ASSERT_EQUAL_UINT8(FUS_CALIB_F_ORIENT | FUS_CALIB_F_FORWARD, rec.calib_flags);
    fus_calib_t decoded; fus_calib_from_ses(&rec, &decoded);
    TEST_ASSERT_TRUE(fus_calib_valid(&decoded));
    fus_t g; fus_init(&g, &decoded, 1);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&g)->forward_ok);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    imu_raw_t r = raw_at(t, acc, ZERO3);
    fused_sample_t restored;
    fus_step(&f, &r, &o);
    fus_step(&g, &r, &restored);
    /* 1e-3 g: the record stores each row entry as r × 1e4 rounded to int16, so a row entry moves by
     * up to 5e-5 and a 1 g sample by up to ~1e-4 g; the tolerance keeps a comfortable margin. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lon, restored.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_lat, restored.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, o.g_comb, restored.g_comb);
    TEST_ASSERT_EQUAL_UINT8(o.flags & FUS_ORIENT_OK, restored.flags & FUS_ORIENT_OK);
}

static void test_recapture_resets_forward_learning(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);
    int neg = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg));
    TEST_ASSERT_EQUAL_UINT8(FWD_LEARN_WINDOWS, f.fwd.windows);

    /* The vehicle is re-mounted upright and captured again: the new z row invalidates the forward
     * row and every window accumulated against the old one. */
    const float upright[3] = { 0.0f, 0.0f, 1.0f };
    feed(&f, &o, FUS_STILL_WINDOW_N, upright, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_EQUAL_INT(0, fus_calib_orient_capture(&f));
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->orient_ok);
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));
    TEST_ASSERT_FALSE(f.fwd_learned_pending);
    imu_raw_t r = raw_at(t, upright, ZERO3);
    fus_step(&f, &r, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);       /* forward must be learned again */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
}

/* Degenerate accumulation (§9.2, fus_orient.c's FUS_FWD_MIN_NORM guard): when the accumulated
 * horizontal sum collapses to ~zero, fus_orient_set_forward refuses to learn a row and fus_step
 * restarts the tracker (fus_fwd_init) instead of leaving stale state behind. Two runs of exactly
 * opposite specific force are built as bit-exact int16 negations of each other, so ah(-a) = -ah(a)
 * to full float precision and the accumulated sum returns to exactly zero, not merely close to it.
 * FWD_LEARN_WINDOWS = 3 needs a third counted run; it alternates sign every sample (an even count),
 * which cancels to exactly zero by the time it is counted too, so the sum stays exactly zero right
 * through the trigger and the test is deterministic on every host. */
static void test_degenerate_forward_sum_restarts_the_tracker(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fused_sample_t o; int64_t t = 1000000;
    float grav[3]; tilted_gravity(grav);
    capture_tilted_orientation(&f, &o, &t, grav);

    const float fwd_g = BURST_ACC_MPS2 / (float)G_MPS2;
    const float acc_pos[3] = { grav[0], grav[1] - fwd_g, grav[2] };
    const imu_raw_t r_pos = raw_at(t, acc_pos, ZERO3);
    imu_raw_t r_neg = r_pos;
    r_neg.ax = (int16_t)(-(int)r_pos.ax);
    r_neg.ay = (int16_t)(-(int)r_pos.ay);
    r_neg.az = (int16_t)(-(int)r_pos.az);

    int ones = 0, neg = 0;
    for (int b = 0; b < FWD_LEARN_WINDOWS; b++) {
        const int n = (b < 2) ? BURST_SAMPLES : FUS_FWD_MIN_SAMPLES;
        for (int i = 0; i < n; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, BURST_ACC_MPS2, BURST_YAW_DPS);
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            imu_raw_t r;
            if (b == 0) r = r_pos;                     /* run 0: +X in the body plane */
            else if (b == 1) r = r_neg;                 /* run 1: -X, cancelling run 0 exactly */
            else r = (i % 2 == 0) ? r_pos : r_neg;      /* run 2: alternates, cancelling within itself */
            r.mono_us = t;
            fus_step(&f, &r, &o);
            t += 1000000 / FUSION_HZ;
        }
        for (int i = 0; i < COAST_SAMPLES; i++) {
            if (i % FIX_EVERY == 0) {
                const int rc = fus_calib_forward_step(&f, 0.0f, 0.0f);   /* run ends: no acceleration */
                if (rc == 1) ones++;
                if (rc < 0) neg++;
            }
            feed(&f, &o, 1, grav, ZERO3, 0.0f, 0.0f, &t);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, ones);              /* the degenerate sum never reports a learned row */
    TEST_ASSERT_EQUAL_INT(0, neg);               /* orientation stays captured the whole time */
    TEST_ASSERT_EQUAL_UINT8(0, fus_calib(&f)->forward_ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.fwd.windows);   /* fus_orient_set_forward's -1 restarted the tracker */
    TEST_ASSERT_FALSE(fus_fwd_ready(&f.fwd));

    imu_raw_t clean = raw_at(t, grav, ZERO3);
    fus_step(&f, &clean, &o);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_ORIENT_OK);   /* forward still unlearned */

    /* A subsequent clean run still learns the forward row normally. */
    int neg2 = 0;
    TEST_ASSERT_EQUAL_INT(1, run_forward_bursts(&f, &o, &t, grav, &neg2));
    TEST_ASSERT_EQUAL_INT(0, neg2);
    TEST_ASSERT_EQUAL_UINT8(1, fus_calib(&f)->forward_ok);
}

/* ---- §9.3 fusion math: lean complementary filter, earth-frame yaw, lateral g, GPS cross-check ---- */

/* Body-Z rate for a steady turn: -383 LSB / 16.4 = -23.35 dps. With the cos(phi) projection of
 * §9.3 step 3, the fused earth-frame rate settles at psi-dot = omega.z * cos(phi*) = -0.3 rad/s once
 * the lean reaches its fixed point phi* = atan(v*0.3/g) = 42.5 deg (yaw ~ -17.2 dps, g_lat ~ 0.918). */
static const int16_t GZ_RIGHT = -383;      /* right turn: yaw < 0 */
static const int16_t GZ_LEFT  =  383;      /* left turn:  yaw > 0 */

/* Raw sample at a given time from accel (g) and gyro (dps). */
static imu_raw_t raw_tg(int64_t mono_us, float ax_g, float ay_g, float az_g,
                        float gx_dps, float gy_dps, float gz_dps)
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = (int16_t)lroundf(ax_g * IMU_ACC_LSB_PER_G);
    r.ay = (int16_t)lroundf(ay_g * IMU_ACC_LSB_PER_G);
    r.az = (int16_t)lroundf(az_g * IMU_ACC_LSB_PER_G);
    r.gx = (int16_t)lroundf(gx_dps * IMU_GYR_LSB_PER_DPS);
    r.gy = (int16_t)lroundf(gy_dps * IMU_GYR_LSB_PER_DPS);
    r.gz = (int16_t)lroundf(gz_dps * IMU_GYR_LSB_PER_DPS);
    return r;
}

/* One steady-turn raw sample: constant body-Z gyro (identity mount, zero pitch, so body Z is the
 * earth yaw axis), upright 1 g on Z, no lateral or forward specific force. */
static imu_raw_t raw_turn(int64_t mono_us, int16_t gz_lsb)
{
    imu_raw_t r;
    r.mono_us = mono_us;
    r.ax = 0; r.ay = 0; r.az = (int16_t)lroundf(IMU_ACC_LSB_PER_G);
    r.gx = 0; r.gy = 0; r.gz = gz_lsb;
    return r;
}

/* Fully-calibrated identity mount: identity rotation, forward learned, zero gyro bias known. Lets the
 * §9.3 cases drive fus_step directly without replaying the 2.2 capture choreography. */
static void calibrated_identity(fus_calib_t *c)
{
    fus_calib_defaults(c);
    c->orient_ok = 1; c->forward_ok = 1; c->bias_ok = 1;
}

/* Drives the moto steady turn: nsamp fus_step samples at FUSION_HZ, feeding a GPS fix at the start of
 * every FIX_EVERY-sample block (a continuous 5 Hz cadence tracked by *phase). Each fix first advances
 * the course by course_step_deg, so the GPS turn rate reads -course_step_deg / 0.2 s. *t carries the
 * running mono time; *course the running compass heading. */
static void drive_turn(fus_t *f, fused_sample_t *o, int nsamp, int16_t gz_lsb, float v_mps,
                       float course_step_deg, int64_t *t, float *course, int *phase)
{
    for (int i = 0; i < nsamp; i++) {
        if (*phase % FIX_EVERY == 0) {
            *course += course_step_deg;
            if (*course >= 360.0f) *course -= 360.0f;
            else if (*course < 0.0f) *course += 360.0f;
            fus_set_gps_speed(f, v_mps, *course, *t, true);
        }
        imu_raw_t r = raw_turn(*t, gz_lsb);
        fus_step(f, &r, o);
        *t += 1000000 / FUSION_HZ;
        (*phase)++;
    }
}

static void test_moto_steady_right_turn_converges(void)
{
    /* Roadmap exit criterion. Steady right turn at 30 m/s, GZ_RIGHT chosen so the fused psi-dot
     * settles at -0.3 rad/s (fixed point 42.56 deg). omega.x = 0, so the gyro roll is flat and the
     * complementary filter is pulled from lean_rad = 0 to phi_ref = atan2(-v*psi-dot, g). tau =
     * dt/(1-alpha) = 0.5 s; the atan2 nonlinearity converges slightly faster, reaching ~41.9 deg at
     * 1.5 s (150 samples), inside +-1 deg. Courses advance +3.438 deg/fix (GPS yaw -17.19 dps), so
     * the cross-check agrees and never trips. */
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 1);                          /* moto */
    fused_sample_t o; int64_t t = 1000000; float course = 90.0f; int phase = 0;
    drive_turn(&f, &o, 150, GZ_RIGHT, 30.0f, 3.4382f, &t, &course, &phase);   /* 1.5 s */

    TEST_ASSERT_FLOAT_WITHIN(1.0f, 42.5f, o.lean_deg);     /* roadmap: within +-1 deg in 1.5 s */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.917f, o.g_lat);      /* -v*psi-dot/g > 0 (right); tol per task */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -17.19f, o.yaw_dps);    /* -0.3 rad/s; 0.5 dps covers 1.5 s residual */
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, o.flags & FUS_LEAN_VALID);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_DISAGREE);    /* GPS turn rate agrees */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_CLAMPED);
}

static void test_moto_steady_left_turn_converges(void)
{
    /* Mirror of the right turn: GZ_LEFT gives psi-dot = +0.3 rad/s, lean -42.5 deg, g_lat < 0 (left),
     * yaw +17.19 dps. Courses advance -3.438 deg/fix (GPS yaw +17.19 dps), so they agree. */
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; int64_t t = 1000000; float course = 90.0f; int phase = 0;
    drive_turn(&f, &o, 150, GZ_LEFT, 30.0f, -3.4382f, &t, &course, &phase);

    TEST_ASSERT_FLOAT_WITHIN(1.0f, -42.5f, o.lean_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, -0.917f, o.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 17.19f, o.yaw_dps);
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, o.flags & FUS_LEAN_VALID);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_DISAGREE);
}

static void test_moto_pure_gyro_and_lean_valid_timeout(void)
{
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; int64_t t = 1000000;

    /* Pure gyro: with no GPS reference, alpha is effectively 1 and the lean is the gyro integral.
     * omega.x = 20 dps for 0.5 s (50 samples) integrates to 20 * 0.5 = 10 deg, exact up to the LSB
     * rounding of the raw sample. FUS_LEAN_VALID stays clear: no reference has ever been applied. */
    for (int i = 0; i < 50; i++) {
        imu_raw_t r = raw_tg(t, 0.0f, 0.0f, 1.0f, 20.0f, 0.0f, 0.0f);
        fus_step(&f, &r, &o);
        t += 1000000 / FUSION_HZ;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);

    /* One fresh, fast, valid fix applies a reference and raises FUS_LEAN_VALID. */
    fus_set_gps_speed(&f, 30.0f, 90.0f, t, true);
    imu_raw_t r = raw_tg(t, 0.0f, 0.0f, 1.0f, 20.0f, 0.0f, 0.0f);
    fus_step(&f, &r, &o);
    const int64_t t_ref = t;
    t += 1000000 / FUSION_HZ;
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, o.flags & FUS_LEAN_VALID);

    /* GPS goes invalid; drive upright (omega.x = 0) with no fresh reference for 7 s. FUS_LEAN_VALID
     * holds for LEAN_REF_TIMEOUT_S after the last reference, then clears. */
    fus_set_gps_speed(&f, 0.0f, 0.0f, t, false);
    uint8_t flags_at_3s = 0xFF;
    for (int i = 0; i < 700; i++) {
        imu_raw_t s = raw_tg(t, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
        fus_step(&f, &s, &o);
        if (t - t_ref >= 3000000 && flags_at_3s == 0xFF) flags_at_3s = o.flags;   /* first sample past 3 s */
        t += 1000000 / FUSION_HZ;
    }
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, flags_at_3s & FUS_LEAN_VALID);   /* < 5 s: still valid */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);                    /* ~7 s: cleared */
}

static void test_lean_and_g_are_clamped(void)
{
    /* Lean clamp: 200 dps of roll for 0.5 s would integrate to 100 deg (no reference, pure
     * integrator), clamped to LEAN_MAX_DEG = 70 with FUS_CLAMPED set. */
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; int64_t t = 1000000;
    for (int i = 0; i < 50; i++) {
        imu_raw_t r = raw_tg(t, 0.0f, 0.0f, 1.0f, 200.0f, 0.0f, 0.0f);
        fus_step(&f, &r, &o);
        t += 1000000 / FUSION_HZ;
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 70.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(FUS_CLAMPED, o.flags & FUS_CLAMPED);

    /* g clamp: 4 g of forward specific force is clamped to G_MAX = 3.0, and g_comb is formed from the
     * clamped components, so it is 3.0 too. */
    fus_t f2; fus_init(&f2, &c, 1);
    imu_raw_t r = raw_tg(t, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    fus_step(&f2, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, o.g_comb);
    TEST_ASSERT_EQUAL_UINT8(FUS_CLAMPED, o.flags & FUS_CLAMPED);
}

static void test_gps_cross_check_disagrees_and_latches(void)
{
    /* Steady right turn (fused yaw ~ -17 dps) but the GPS courses run the wrong way, implying a
     * +5 dps left turn: |-17 - 5| = 22 > LEAN_DISAGREE_DPS. Under LEAN_DISAGREE_S the flag stays
     * clear; once the disagreement has held past it, FUS_DISAGREE sets and disagree_latched latches.
     * course_step = -1 deg/fix -> GPS yaw = -(-1)/0.2 = +5 dps. The disagreement starts at the 2nd
     * fix (sample 20, t0 + 0.2 s), so FUS_DISAGREE sets 5 s later, at sample 520. */
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 1);
    fused_sample_t o; int64_t t = 1000000; float course = 90.0f; int phase = 0;

    drive_turn(&f, &o, 300, GZ_RIGHT, 30.0f, -1.0f, &t, &course, &phase);    /* ~2.8 s past onset */
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_DISAGREE);      /* < LEAN_DISAGREE_S: no flag yet */
    TEST_ASSERT_FALSE(f.disagree_latched);

    drive_turn(&f, &o, 240, GZ_RIGHT, 30.0f, -1.0f, &t, &course, &phase);    /* to ~5.2 s past onset */
    TEST_ASSERT_EQUAL_UINT8(FUS_DISAGREE, o.flags & FUS_DISAGREE);
    TEST_ASSERT_TRUE(f.disagree_latched);

    /* Return the GPS course to agreement (+3.438 deg/fix -> -17.19 dps). The flag clears on the
     * sample, but disagree_latched stays set for the session (the pipeline logs it once). */
    drive_turn(&f, &o, 100, GZ_RIGHT, 30.0f, 3.4382f, &t, &course, &phase);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_DISAGREE);
    TEST_ASSERT_TRUE(f.disagree_latched);
}

static void test_car_variant_lateral_g_and_yaw(void)
{
    fus_calib_t c; calibrated_identity(&c);
    fus_t f; fus_init(&f, &c, 0);                          /* car */
    fused_sample_t o; int64_t t = 1000000;

    /* A right-hand lateral specific force (vehicle -Y) reads as +g_lat directly from -a.y, not from
     * v*psi-dot. Lean is not meaningful for a car: lean_deg stays 0 and FUS_LEAN_VALID never sets,
     * even with a fresh fast GPS fix. */
    fus_set_gps_speed(&f, 30.0f, 90.0f, t, true);
    imu_raw_t r = raw_tg(t, 0.0f, -0.4f, 1.0f, 0.0f, 0.0f, 0.0f);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.4f, o.g_lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
    t += 1000000 / FUSION_HZ;

    /* Yaw uses phi == 0, so psi-dot = omega.z: a +10 dps body-Z rate reads as +10 dps (left). */
    fus_set_gps_speed(&f, 30.0f, 90.0f, t, true);
    r = raw_tg(t, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 10.0f);
    fus_step(&f, &r, &o);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, o.yaw_dps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.lean_deg);
    TEST_ASSERT_EQUAL_UINT8(0, o.flags & FUS_LEAN_VALID);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_identity_and_valid);
    RUN_TEST(test_invalid_calibration_is_rejected_and_init_falls_back);
    RUN_TEST(test_identity_mount_gravity_bias_and_yaw_sign);
    RUN_TEST(test_ninety_degree_mount_rotates_into_the_vehicle_frame);
    RUN_TEST(test_temperature_drift_marks_the_bias_stale);
    RUN_TEST(test_calibration_round_trips_through_the_calib_record);
    RUN_TEST(test_gps_speed_and_course_are_held_with_validity_and_time);
    RUN_TEST(test_gps_yaw_rate_helper_signs_and_wrap);
    RUN_TEST(test_gps_course_history_feeds_the_turn_rate);
    RUN_TEST(test_still_detection_and_gyro_bias_update);
    RUN_TEST(test_orientation_capture_from_tilted_gravity);
    RUN_TEST(test_forward_learning_from_straight_line_acceleration);
    RUN_TEST(test_calibration_persists_through_the_calib_record_after_learning);
    RUN_TEST(test_recapture_resets_forward_learning);
    RUN_TEST(test_degenerate_forward_sum_restarts_the_tracker);
    RUN_TEST(test_moto_steady_right_turn_converges);
    RUN_TEST(test_moto_steady_left_turn_converges);
    RUN_TEST(test_moto_pure_gyro_and_lean_valid_timeout);
    RUN_TEST(test_lean_and_g_are_clamped);
    RUN_TEST(test_gps_cross_check_disagrees_and_latches);
    RUN_TEST(test_car_variant_lateral_g_and_yaw);
    return UNITY_END();
}
```

- [ ] **Step 4: Build and run (clang + gcc parity, both under ASan/UBSan; a Debug build enables the sanitizers unconditionally).**

```bash
cmake --build test/build --parallel && ctest --test-dir test/build --output-on-failure
```

Expected: warning-free build and `100% tests passed out of 24` (`test_fus` now reports `21 Tests 0 Failures 0 Ignored`; the host suite keeps its 24 ctest executables — Task 2 adds cases to `test_fus`, not a new suite). gcc-16 parity (`CC=gcc-16 cmake -S test -B build-gcc -DCMAKE_BUILD_TYPE=Debug && cmake --build build-gcc && ctest --test-dir build-gcc`) likewise builds warning-free and prints `100% tests passed out of 24`. The ESP32 host guard `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build` must still succeed (`test_fus.c` includes nothing beyond Task 1's headers).

Convergence sanity (roadmap exit criterion): the steady right turn reaches `lean_deg = 41.94` deg at 1.5 s (150 samples) — within +-1 deg of the 42.5 deg fixed point — with `yaw_dps = -17.38`, `g_lat = 0.928`, `FUS_LEAN_VALID` set and `FUS_DISAGREE` clear.

- [ ] **Step 5: Hygiene and commit.** `git diff --check` empty.

```bash
git add components/core/include/core/consts.h components/core/fusion/fus.c test/test_fus.c docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md
git commit -m "feat(core): §9.3 lean complementary filter, earth-frame yaw, lateral g and GPS cross-check

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED"
```

---

---
