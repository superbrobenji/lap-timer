# F1a — Core Engines Fix Wave Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every core-engine finding of the 2026-10-13 audit — the critical short-dropout lap-time corruption, the lost sector gate, the leaking best-sector table, the inert `cfg.lap.*` block, the spurious drag DONE, and every MEDIUM/LOW/false-claim item — in `lapengine`, `dragengine`, `fusion`, `geo` and `timebase`, each behind a failing host test written first.

**Architecture:** All five engines stay pure C11 (no IDF, no heap, no recursion, no function pointers) with state in caller-owned structs, so every fix is provable by a Unity host test. Three structural changes carry the weight: (1) `process_segment` finds the S/F crossing *first* and then evaluates gate crossings in two half-open `t_cross` windows around it, so all crossings in one fix segment are processed in time order (R23); (2) a one-bit `seg_has_invalid` latch makes "this chord spans a GPS outage" a property of the chord rather than of its duration (R22); (3) `lap_cfg_t` grows into the real §15.1 `lap.*` block and a new pure `lap_cfg_from_user()` projects the user cfg onto it, so the engine stops reading compile-time macros (R24).

**Tech Stack:** C11 under the Power of 10, ESP-IDF v5.3.2 (firmware builds only — no IDF API is added), Unity host tests in `test/`, the host replay/synth fixture harness in `tools/replay`.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding; rows **F1a**, rulings **R20–R25**, §3 event codes, §4 gates). Findings fact sheet: `.superpowers/reviews/2026-10-13/core-algorithms.md` (git-ignored) and audit §7.5/§7.3 in `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md`. Product spec: `docs/superpowers/specs/2026-09-14-lap-timer-design.md`.

**Line numbers in this plan:** `lap.c`, `lap.h`, `lap_gate.c`, `pipeline.c` and `logger.c` are cited from the **PR #101 branch** worktree `/Users/benji/projects/personal/lap-timer-worktrees/gdm` (head `df75497`), because **F0 (PR #101) merges before F1a** and changes `lap.c` (CREATE mode, `finalize_create`) and `lap.h`. Everything else (`drag.c`, `drag.h`, `fus.c`, `fus.h`, `geo.c`, `tb.c`, `event.h`, `consts.h`, `test/*`) is cited from `main` @ `f4d4db2`, where F0 does not touch it. Branch from `main` **after** #101 lands.

## Global Constraints

- Branch `fix-core-engines` in its own worktree (`superpowers:using-git-worktrees`). Never flash from a subagent.
- **File ownership (hard boundary — this plan touches nothing else):** `components/core/lapengine/*`, `components/core/dragengine/*`, `components/core/fusion/*`, `components/core/geo/*`, `components/core/timebase/*`, `components/core/include/core/{lap,drag,fus,geo,event,consts}.h`, the new `components/core/config/lap_cfg.c` + `components/core/include/core/lap_cfg.h` (an explicit carve-out from F1b's `core/config/` — F1b must not touch these two files, and F1a must not touch `cfg.c`/`cfg_json.c`/`cfg_blob.c`), `test/test_{lap,drag,fus,fus_still,fus_orient,fus_fwd,geo,lap_gate,drag_cfg}.c`, and in `components/app/` **only**: the two lap-cfg call sites in `pipeline.c` (`pipeline_init` ~1057–1078, `pipeline_reload_cfg`) plus the four `ev->type <= EV_FAULT` assertions (`pipeline.c:216,229,420`, `logger.c:644`). Plus `docs/` only where this plan's spec-amendment task says.
- Power of 10 on every line written: every function > 20 code lines carries ≥ 2 **meaningful** `CORE_ASSERT_*` (never `abort`); every loop bounded by a constant or table field with a `/* rule 2 */` note where the bound is not textually obvious; ≤ 60 code lines per function; no recursion, heap, `goto`, or function pointers; one dereference level per expression (rule 9 strict N=1). `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0 and `python3 tools/lint/venue_announce.py` = 0.
- Zero warnings: the host suite compiles with `-Wall -Wextra -Werror -Wshadow -Wconversion`; gcc-16 sweep of `test/build/compile_commands.json` clean; ccache-off clean firmware builds 0 warnings. Never add `-Wno-*` or a pragma.
- **No new test files.** `test/CMakeLists.txt` globs `test_*.c` and `test_apps/core_selftest/main/CMakeLists.txt` globs the same directory and builds every suite on-target; every case in this plan goes into an existing `test_*.c`, so neither CMake file nor the selftest exclude regex changes. No case in this plan does file I/O.
- **Replay fixtures must not drift.** `tools/replay/test/test_replay_fixtures.c` re-runs `lap_on_fix`/`drag_on_fused` over the eight committed fixtures in `test/data` and compares `replay --json` structurally against `<name>.expected.json`. One fixture value changes **by design** (Task 2 — `gps_dropout.expected.json` lap 0 `flags` 8 → 9); that single edit is pre-authorised there and nowhere else. If **any other** fixture field changes at any point in this plan: **stop, do not regenerate, report**.
- Spec amendments are limited to §6.4 step 1, §9.3 steps 4/5, §10.3, §10.10, §10.11, §11.2 and the §15.1 `lap.*` note (Task 14). Every amendment cites `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`.
- Commit trailer on every commit of this branch (the squash commit on `main` carries none — GitHub drops trailers):

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

- Build/test commands used throughout:

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure
```

- **Checkpoint protocol (the executing session must survive a `/clear` between tasks).** All state lives on disk, never in the session: the branch, and the ledger `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` (create it with `mkdir -p .superpowers/sdd/2026-10-13-fix-F1a` in Task 1; `.superpowers/` is git-ignored, so the ledger is never committed). Every task ends with a **CHECKPOINT** step that appends one ledger line, pushes the branch, and records the exact resume prompt for the next task. Tasks are grouped into **sessions of at most four**; each session ends with a session checkpoint that re-runs the host suite and both linters before the context is cleared. To resume after a `/clear`: read the last `Resume:` line of the ledger and follow it — it names the plan, the next task, the ledger, the worktree and the branch, which is everything needed to continue. The ledger's shape:

```
# F1a core engines — progress ledger
worktree: /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines
branch:   fix-core-engines
Task 1 complete 1a2b3c4
Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.
```

A CHECKPOINT step is never "done" until `git push` has reported success and `tail -2` of the ledger shows the new lines. If a checkpoint's host-suite re-run fails, do **not** write the ledger line — fix forward or revert the task's commit first.

---

## Session map

Tasks run in sessions of ≤ 4, each ending with a session checkpoint (host suite + both linters + push). Inside a session the per-task CHECKPOINT steps still run, so a `/clear` at any point is safe.

| Session | Tasks | Theme | Parallelism |
|---|---|---|---|
| **Session 1** | 1, 2, 3, 4 | the lap engine's correctness core: cfg plumbing, the GPS-lost chord, crossing order, venue/layout hygiene | all SERIAL (`lap.c`) |
| **Session 2** | 5, 6, 7, 8 | `EV_VENUE_LOST`, predictive density, `lap_mark_gate`; then the drag abort | 5–7 SERIAL (`lap.c`), 8 PARALLEL (`drag.c`) |
| **Session 3** | 9, 10, 11 | drag's remaining fixes, fusion, geo + tb | 9 SERIAL after 8; 10 and 11 PARALLEL with everything |
| **Session 4** | 12, 13, 14 | the rule-5/rule-9 sweep, the false-claim sweep, the final gate wave | all SERIAL |

---

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `components/core/include/core/lap.h` | `lap_cfg_t` (§15.1 block + default-layout map), `lap_set_cfg`, `lap_force_layout` → `int`, `seg_has_invalid`, `pred_stride`, corrected `LAP_EVT_MAX`/`lap_import_rtc` contracts | 1, 2, 4, 6, 13 |
| `components/core/include/core/lap_cfg.h` *(new)* | `lap_cfg_from_user(const cfg_t *, lap_cfg_t *)` declaration — the only place `core/lap.h` and `core/cfg.h` meet | 1 |
| `components/core/config/lap_cfg.c` *(new)* | that projection, per-field clamped to the §15.1 ranges | 1 |
| `components/core/lapengine/lap.c` | cfg plumbing, R22 chord latch, R23 windowed crossing passes, R25 clears + return codes, `EV_VENUE_LOST`, predictive density, M5, rule-9/rule-5 cleanups | 1–7, 12, 13 |
| `components/core/dragengine/drag.c`, `include/core/drag.h` | R21 abort ordering + M4 `EV_DRAG_ARMED`, M2 backward-step tolerance, range/quarter/faded LOWs, `DRAG_KMH_PER_MPH` rename, rule-5 cleanup | 8, 9 |
| `components/core/dragengine/drag_cfg.c` | the rename's two call sites | 9 |
| `components/core/fusion/fus.c`, `fus_orient.c`, `include/core/fus.h` | M1 un-oriented clamp, M7 stale latch, dead fields, rule-5 cleanup, false claims 6/7 | 10, 12, 13 |
| `components/core/geo/geo.c` | antipodal NaN guard, antimeridian longitude wrap | 11 |
| `components/core/timebase/tb.c` | rule-5 cleanup | 11 |
| `components/core/include/core/event.h` | `EV_VENUE_LOST = 20`, `EV_COUNT = 21` | 5 |
| `components/app/pipeline/pipeline.c`, `logger/logger.c` | the two lap-cfg call sites; four `< EV_COUNT` assertions | 1, 5 |
| `test/test_lap.c` | 13 new cases (C1, H1–H4, M3, M5, R25, predictive density) | 1–7, 12 |
| `test/test_drag.c` | 6 new cases (H5, M2, M4, three LOWs) | 8, 9 |
| `test/test_fus.c`, `test/test_geo.c`, `test/test_lap_gate.c`, `test/test_drag_cfg.c` | M1, M7, geo LOWs, gate-line contract, rename | 7, 9, 10, 11 |
| `test/data/gps_dropout.expected.json` | one flag byte, by design (R22) | 2 |
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `docs/bench/next-bench-day.md` | amendments + bench items | 14 |

**Execution order.** The `lap.c` chain (Tasks 1→2→3→4→5→6→7) is **SERIAL** — every task edits `lap.c`. The `drag.c` pair (8→9) is **SERIAL** but **PARALLEL to the lap chain**. Tasks 10 (fusion) and 11 (geo+tb) are **PARALLEL** to both. Task 12 (rule-5 sweep) is **SERIAL** after 7 and 10. Tasks 13 and 14 are **SERIAL** last.

---

### Task 1 — SERIAL (lap chain head): `cfg.lap.*` reaches the engine (R24, core:H4)

**Files:**
- Create: `components/core/include/core/lap_cfg.h`, `components/core/config/lap_cfg.c`
- Modify: `components/core/include/core/lap.h:54-57,102,177-179`, `components/core/lapengine/lap.c:27-31,81-88,149-154,510-537,648-658,787-797,893,1024-1028`, `components/app/pipeline/pipeline.c:1057-1078` and `pipeline_reload_cfg`
- Test: `test/test_lap.c`

**Interfaces (produced):**

```c
/* core/lap.h */
#define LAP_MAX_DEFAULT_LAYOUTS 8      /* == CFG_MAX_DEFAULT_LAYOUTS (lap_cfg.c _Static_asserts) */
typedef struct {
    uint16_t min_lap_s;        /* §10.4 step 2 debounce + S/F re-arm window (default MIN_LAP_S) */
    uint16_t max_lap_s;        /* §10.4 LAP_F_TOO_LONG threshold           (default MAX_LAP_S) */
    uint16_t gate_rearm_m;     /* §6.4 step 5 re-arm distance              (default GATE_REARM_DIST_M) */
    uint8_t  pit_speed_kmh;    /* §10.6                                    (default PIT_SPEED_KMH) */
    uint8_t  pit_time_s;       /* §10.6                                    (default PIT_TIME_S) */
    uint8_t  n_default_layout; /* entries used in default_layout[] */
    struct { uint16_t venue, layout; } default_layout[LAP_MAX_DEFAULT_LAYOUTS];   /* §10.5 tie-break */
} lap_cfg_t;
void lap_cfg_defaults(lap_cfg_t *c);              /* every field = its consts.h default, map empty */
void lap_init(lap_t *L, const lap_cfg_t *cfg);    /* unchanged signature; NULL -> defaults */
void lap_set_cfg(lap_t *L, const lap_cfg_t *cfg); /* apply to a live engine; no state reset */
/* core/lap_cfg.h */
void lap_cfg_from_user(const cfg_t *cfg, lap_cfg_t *out);
```

- Consumes: `cfg_t` (`core/cfg.h`, F1b's — read only), `CFG_MAX_DEFAULT_LAYOUTS`, `MIN_LAP_S`/`MAX_LAP_S`/`GATE_REARM_DIST_M`/`PIT_SPEED_KMH`/`PIT_TIME_S` (`core/consts.h`).
- Produces: the three functions above; `lap_cfg_t.default_layout_id` **is deleted** (nothing outside `lap.c` read it).

- [ ] **Step 1: Write the failing tests** — append to `test/test_lap.c` (and register in `main`, in this order, after `RUN_TEST(test_reset_mid_session_then_laps_resume)`):

```c
/* H4/R24 (§15.1): cfg.lap.min_lap_s reaches the engine. With min_lap_s = 60 the S/F gate stays
 * disarmed (§6.4 step 5) through a 45 s lap, so nothing completes; the same drive under the
 * defaults (MIN_LAP_S = 20) completes the out-lap. Before R24 both engines behaved identically. */
static void test_cfg_min_lap_s_reaches_the_engine(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);

    lap_cfg_t c; lap_cfg_defaults(&c); c.min_lap_s = 60;
    lap_t L; lap_init(&L, &c);
    evlog_t slow; memset(&slow, 0, sizeof slow);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &slow);
    forward_lap(&L, lat0, lon0, 0, 40000000, &slow);                  /* out-lap opens at t = 1 s */
    feed(&L, lat0, lon0, 0.0, 40.0, 46000000, SPD_MMS, true, &slow);  /* 45 s later: gate still disarmed */
    TEST_ASSERT_EQUAL_INT(0, count_type(&slow, EV_LAP_COMPLETE));
    TEST_ASSERT_EQUAL_UINT16(0, L.lap_no);

    lap_t D; lap_init(&D, NULL);                                      /* control: shipped defaults */
    evlog_t fast; memset(&fast, 0, sizeof fast);
    lap_set_venue(&D, &v);
    arm_at_start(&D, lat0, lon0, 0, &fast);
    forward_lap(&D, lat0, lon0, 0, 40000000, &fast);
    feed(&D, lat0, lon0, 0.0, 40.0, 46000000, SPD_MMS, true, &fast);
    TEST_ASSERT_EQUAL_INT(1, count_type(&fast, EV_LAP_COMPLETE));
    TEST_ASSERT_EQUAL_UINT16(1, D.lap_no);
}

/* H4/R24 (§10.5): with two candidates tied on score, the venue's configured default layout wins.
 * The out-lap below crosses no sector gate of either layout and matches neither length (|1.4 km −
 * 2.5 km| > 15 %), so both score 0 and only the tie-break decides. Wholly untested before R24. */
static void test_default_layout_map_breaks_the_tie(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue_2layout(&v, lat0, lon0);              /* ids 1 "Full", 2 "Short" */

    lap_cfg_t c; lap_cfg_defaults(&c);
    c.n_default_layout = 1;
    c.default_layout[0].venue  = TRK_USER_ID_BASE;
    c.default_layout[0].layout = 2;                                  /* prefer "Short" at this venue */
    lap_t L; lap_init(&L, &c);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 2000000,  SPD_MMS, true, &log);   /* S/F -> out-lap, union */
    feed(&L, lat0, lon0, 400.0,  40.0, 10000000, SPD_MMS, true, &log);   /* east of every gate */
    feed(&L, lat0, lon0, 400.0, 360.0, 18000000, SPD_MMS, true, &log);   /* north, crosses nothing */
    feed(&L, lat0, lon0, 400.0, -40.0, 30000000, SPD_MMS, true, &log);   /* south, re-arms S/F */
    feed(&L, lat0, lon0,   0.0, -40.0, 34000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 36000000, SPD_MMS, true, &log);   /* S/F -> disambiguate */
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAYOUT_LOCKED));
    TEST_ASSERT_EQUAL_UINT16(2, last_locked_id(&log));               /* the configured default */

    lap_t A; lap_init(&A, NULL);                                     /* control: no map -> lowest id */
    evlog_t alog; memset(&alog, 0, sizeof alog);
    lap_set_venue(&A, &v);
    arm_at_start(&A, lat0, lon0, 0, &alog);
    feed(&A, lat0, lon0,   0.0,  40.0, 2000000,  SPD_MMS, true, &alog);
    feed(&A, lat0, lon0, 400.0,  40.0, 10000000, SPD_MMS, true, &alog);
    feed(&A, lat0, lon0, 400.0, 360.0, 18000000, SPD_MMS, true, &alog);
    feed(&A, lat0, lon0, 400.0, -40.0, 30000000, SPD_MMS, true, &alog);
    feed(&A, lat0, lon0,   0.0, -40.0, 34000000, SPD_MMS, true, &alog);
    feed(&A, lat0, lon0,   0.0,  40.0, 36000000, SPD_MMS, true, &alog);
    TEST_ASSERT_EQUAL_UINT16(1, last_locked_id(&alog));
}

/* R24: CMD_CONFIG_RELOAD applies a new lap cfg to a RUNNING engine without resetting it
 * (pipeline_reload_cfg calls lap_set_cfg, never lap_init). */
static void test_set_cfg_applies_without_resetting(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    forward_lap(&L, lat0, lon0, 0,        40000000, NULL);
    forward_lap(&L, lat0, lon0, 40000000, 40000000, NULL);           /* lap 1 in progress */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    TEST_ASSERT_EQUAL_UINT16(1, L.lap_no);

    lap_cfg_t c; lap_cfg_defaults(&c); c.pit_speed_kmh = 30; c.pit_time_s = 3;
    lap_set_cfg(&L, &c);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));          /* state survives the reload */
    TEST_ASSERT_EQUAL_UINT16(1, L.lap_no);
    TEST_ASSERT_EQUAL_UINT8(30, L.cfg.pit_speed_kmh);
    /* and the new pit thresholds are the ones in force: 20 km/h for 4 s is now a pit */
    feed(&L, lat0, lon0, 300.0, -40.0, 85000000, 5555, true, NULL);  /* 20 km/h */
    feed(&L, lat0, lon0, 300.0, -41.0, 89000000, 5555, true, NULL);
    TEST_ASSERT_TRUE(L.lap_flags & LAP_F_PIT);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build test/build -j8` → compile error, `lap_cfg_t` has no member `min_lap_s`. That is the expected failure for this task.

- [ ] **Step 3: Implement — `core/lap.h`**

Replace the `lap_cfg_t` block at `lap.h:54-57` with the Interfaces block above (keep the `/* §10.5 tie-break */` comment on `default_layout`), add `void lap_set_cfg(lap_t *L, const lap_cfg_t *cfg);` next to `lap_init` at `lap.h:177`, and delete nothing else.

- [ ] **Step 4: Implement — `lapengine/lap.c`**

```c
void lap_cfg_defaults(lap_cfg_t *c)
{
    CORE_ASSERT_VOID(c != NULL, LAP_ASSERT_CODE);
    memset(c, 0, sizeof *c);
    c->min_lap_s     = (uint16_t)MIN_LAP_S;
    c->max_lap_s     = (uint16_t)MAX_LAP_S;
    c->gate_rearm_m  = (uint16_t)GATE_REARM_DIST_M;
    c->pit_speed_kmh = (uint8_t)PIT_SPEED_KMH;
    c->pit_time_s    = (uint8_t)PIT_TIME_S;
}

/* §15.2 reload: apply a cfg to a live engine. Never touches lap/venue/best state -- the pipeline
 * calls this on CMD_CONFIG_RELOAD while a lap may be in progress (R24). A zero in any threshold
 * field would disable the guard it controls, so each one falls back to its consts.h default. */
void lap_set_cfg(lap_t *L, const lap_cfg_t *cfg)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    if (cfg == NULL) { lap_cfg_defaults(&L->cfg); return; }
    L->cfg = *cfg;
    if (L->cfg.min_lap_s == 0)    L->cfg.min_lap_s    = (uint16_t)MIN_LAP_S;
    if (L->cfg.max_lap_s == 0)    L->cfg.max_lap_s    = (uint16_t)MAX_LAP_S;
    if (L->cfg.gate_rearm_m == 0) L->cfg.gate_rearm_m = (uint16_t)GATE_REARM_DIST_M;
    if (L->cfg.pit_time_s == 0)   L->cfg.pit_time_s   = (uint8_t)PIT_TIME_S;
    if (L->cfg.n_default_layout > LAP_MAX_DEFAULT_LAYOUTS)
        L->cfg.n_default_layout = LAP_MAX_DEFAULT_LAYOUTS;   /* rule 2: bounds every map scan */
    CORE_ASSERT_VOID(L->cfg.min_lap_s < L->cfg.max_lap_s, LAP_ASSERT_CODE);   /* a lap window exists */
}
```

In `lap_init` (`lap.c:81-88`) replace the `if (cfg) ... else ...` pair with `lap_set_cfg(L, cfg);` (after the `memset`, before `L->state = LAP_ST_NO_VENUE;`).

Replace the `tiebreak_better` body's `uint16_t def = L->cfg.default_layout_id;` (`lap.c:654`) and add the lookup above it:

```c
/* §10.5 tie-break key: the configured default layout id for the current venue, 0 = none. */
static uint16_t default_layout_for_venue(const lap_t *L)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, 0);
    CORE_ASSERT_RET(L->cfg.n_default_layout <= LAP_MAX_DEFAULT_LAYOUTS, LAP_ASSERT_CODE, 0);
    const trk_venue_t *v = L->venue;              /* rule 9: one dereference level per expression */
    if (v == NULL) return 0;
    const uint16_t vid = v->id;
    for (uint8_t i = 0; i < L->cfg.n_default_layout; i++)   /* rule 2: <= LAP_MAX_DEFAULT_LAYOUTS */
        if (L->cfg.default_layout[i].venue == vid) return L->cfg.default_layout[i].layout;
    return 0;
}
```

Then swap the six macro reads for cfg reads (this is the whole of H4):

| Site (gdm `lap.c`) | Was | Now |
|---|---|---|
| `rearm_sf` 519 | `d > GATE_REARM_DIST_M` | `d > (double)L->cfg.gate_rearm_m` |
| `rearm_sf` 521 | `(int64_t)MIN_LAP_S * 1000000` | `(int64_t)L->cfg.min_lap_s * 1000000` |
| `rearm_sectors` 534 | `d > GATE_REARM_DIST_M` | `d > (double)L->cfg.gate_rearm_m` |
| `rearm_ugates` 547 | `d > GATE_REARM_DIST_M` | `d > (double)L->cfg.gate_rearm_m` |
| `complete_lap` 787 | `(uint32_t)MIN_LAP_S * 1000` | `(uint32_t)L->cfg.min_lap_s * 1000u` |
| `complete_lap` 797 | `(uint32_t)MAX_LAP_S * 1000` | `(uint32_t)L->cfg.max_lap_s * 1000u` |
| `handle_create_mode` 893 | `(int64_t)MIN_LAP_S * 1000000` | `(int64_t)L->cfg.min_lap_s * 1000000` |
| `update_running_progress` 1024 | `(double)PIT_SPEED_KMH` | `(double)L->cfg.pit_speed_kmh` |
| `update_running_progress` 1028 | `(int64_t)PIT_TIME_S * 1000000` | `(int64_t)L->cfg.pit_time_s * 1000000` |

`GATE_REARM_MIN_S` stays a macro (it is not a §15.1 key). Update the `consts.h` comment-free macros' *users'* doc only where it reads "compile-time": none in `lap.c`.

- [ ] **Step 5: Implement — `core/lap_cfg.h` and `core/config/lap_cfg.c`**

```c
/* core/lap_cfg.h */
#ifndef CORE_LAP_CFG_H
#define CORE_LAP_CFG_H
#include "core/lap.h"
#include "core/cfg.h"
/* §15.1/§15.2 (audit fix wave R24): project the user cfg's lap.* block onto the engine's
 * lap_cfg_t. Each field is accepted only inside its §15.1 range and otherwise falls back to the
 * consts.h default, so the engine is safe even on a cfg blob that never saw cfg_validate(). The
 * default_layout map is copied verbatim, truncated to LAP_MAX_DEFAULT_LAYOUTS. cfg NULL -> pure
 * defaults. This is the ONLY place core/lap.h and core/cfg.h meet. */
void lap_cfg_from_user(const cfg_t *cfg, lap_cfg_t *out);
#endif
```

```c
/* core/config/lap_cfg.c */
#include "core/lap_cfg.h"
#include "core/core.h"

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define LAPCFG_ASSERT_CODE 0x0A21

_Static_assert(LAP_MAX_DEFAULT_LAYOUTS == CFG_MAX_DEFAULT_LAYOUTS,
               "lap_cfg_t.default_layout[] must mirror cfg_t.lap.default_layout[]");

static uint16_t pick16(uint16_t v, uint16_t lo, uint16_t hi, uint16_t dflt)
{ return (v < lo || v > hi) ? dflt : v; }
static uint8_t pick8(uint8_t v, uint8_t lo, uint8_t hi, uint8_t dflt)
{ return (v < lo || v > hi) ? dflt : v; }

void lap_cfg_from_user(const cfg_t *cfg, lap_cfg_t *out)
{
    CORE_ASSERT_VOID(out != NULL, LAPCFG_ASSERT_CODE);
    lap_cfg_defaults(out);
    if (cfg == NULL) return;
    /* Ranges are cfg_validate()'s (core/config/cfg.c) and spec §15.1's table, field for field. */
    out->min_lap_s     = pick16(cfg->lap.min_lap_s,    5u,  600u,  (uint16_t)MIN_LAP_S);
    out->max_lap_s     = pick16(cfg->lap.max_lap_s,    60u, 3600u, (uint16_t)MAX_LAP_S);
    out->gate_rearm_m  = pick16(cfg->lap.gate_rearm_m, 10u, 500u,  (uint16_t)GATE_REARM_DIST_M);
    out->pit_speed_kmh = pick8(cfg->lap.pit_speed_kmh, 1u,  30u,   (uint8_t)PIT_SPEED_KMH);
    out->pit_time_s    = pick8(cfg->lap.pit_time_s,    3u,  60u,   (uint8_t)PIT_TIME_S);
    uint8_t n = cfg->lap.n_default_layout;
    if (n > LAP_MAX_DEFAULT_LAYOUTS) n = LAP_MAX_DEFAULT_LAYOUTS;   /* rule 2: fixes the loop bound */
    for (uint8_t i = 0; i < n; i++) {
        out->default_layout[i].venue  = cfg->lap.default_layout[i].venue;
        out->default_layout[i].layout = cfg->lap.default_layout[i].layout;
    }
    out->n_default_layout = n;
    CORE_ASSERT_VOID(out->min_lap_s < out->max_lap_s, LAPCFG_ASSERT_CODE);   /* a lap window exists */
}
```

The core CMake globs `*.c` recursively and the host `test/CMakeLists.txt` globs the same tree: **no CMake edit**.

- [ ] **Step 6: Implement — the two `pipeline.c` call sites (the only app edit in this task)**

`pipeline_init` (gdm `pipeline.c:1057`): move the cfg load above the engine construction and feed both engines from it.

```c
 static void pipeline_init(void)
 {
     tb_init(&s_tb);
     fus_init(&s_fus, NULL, (uint8_t)(CFG_VARIANT_MOTO ? 1 : 0));
-    lap_init(&s_lap, NULL);
     /* Plan 7c T2 fix 1: one NVS cfg load feeds both consumers below -- the drag engine's
      * user-derived gate table (benches/units/rollout) and cfg.mode, the single source of truth
      * for the operating mode (T-D). Same load-with-fallback pattern as ui.c/cmd.c; defaults on a
-     * missing/invalid blob (lt_cfg_load leaves cfg untouched on failure). */
+     * missing/invalid blob (lt_cfg_load leaves cfg untouched on failure). Fix wave R24: the lap
+     * engine is the third consumer -- §15.1's lap.* keys were parsed, clamped, persisted and
+     * exported but never reached it (audit core:H4). */
     cfg_t cfg;   /* pipeline task stack: cfg_t is a few hundred bytes; pipeline has ~4.8 KB free */
     cfg_defaults(&cfg);
     (void)lt_cfg_load(&cfg);
+    {
+        lap_cfg_t lc;
+        lap_cfg_from_user(&cfg, &lc);
+        lap_init(&s_lap, &lc);
+    }
     {
         drag_cfg_t dc;
         drag_cfg_from_user(&cfg, &dc);
         drag_init(&s_drag, &dc);
     }
```

`pipeline_reload_cfg`: fix the stale comment and apply the cfg.

```c
 /* #87: re-read the persisted cfg and rebuild what depends on it -- the drag engine's gate table
  * (a units change redefines the mph gates: drag_init() is a fresh engine, session bests are
- * dropped, documented in spec dsB §2) and the riding mode. The lap engine is untouched. */
+ * dropped, documented in spec dsB §2) and the riding mode. The lap engine takes its §15.1 lap.*
+ * block through lap_set_cfg(), which applies in place: a reload mid-lap keeps the running lap,
+ * the candidates and the session bests (fix wave R24). */
 static void pipeline_reload_cfg(void)
 {
     cfg_t      cfg;
     drag_cfg_t dc;
     cfg_defaults(&cfg);
     (void)lt_cfg_load(&cfg);
     drag_cfg_from_user(&cfg, &dc);
     drag_init(&s_drag, &dc);
+    {
+        lap_cfg_t lc;
+        lap_cfg_from_user(&cfg, &lc);
+        lap_set_cfg(&s_lap, &lc);
+    }
```

Add `#include "core/lap_cfg.h"` beside the existing `#include "core/drag.h"`-group includes at the top of `pipeline.c`.

- [ ] **Step 7: Run to verify green**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`
Expected: all suites pass, `test_lap` +3 cases. Fixture suite unchanged (the defaults are byte-identical to the old macro values, and `replay_run.c` passes `lap_init(&c->lap, NULL)` → defaults).

- [ ] **Step 8: Commit**

```bash
git add components/core/include/core/lap.h components/core/include/core/lap_cfg.h \
        components/core/config/lap_cfg.c components/core/lapengine/lap.c \
        components/app/pipeline/pipeline.c test/test_lap.c
git commit   # fix(core): cfg.lap.* reaches the lap engine -- lap_cfg_t, lap_set_cfg, lap_cfg_from_user, per-venue default layout (F1a T1, R24, core:H4)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 1 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 1 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 2**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 2 — SERIAL (lap chain): a chord that spans an invalid fix is a GPS-lost chord (R22, core:C1)

**Files:**
- Modify: `components/core/include/core/lap.h:140-143`, `components/core/lapengine/lap.c:44-79,117-147,287-338,757-841,969-1011,1067-1072,1093-1098`, `test/data/gps_dropout.expected.json` (one value)
- Test: `test/test_lap.c`

**Interfaces:**
- Consumes: Task 1's `lap_cfg_t`/`lap_set_cfg`.
- Produces: `lap_t.seg_has_invalid` (bool); `complete_lap(lap_t*, int64_t t_cross, int64_t mono_us, bool seg_gap, event_t*, int, int*)` and `open_lap(lap_t*, uint16_t, int64_t, bool seg_gap)` keep their signatures — only `complete_lap`'s use of `seg_gap` changes.

- [ ] **Step 1: Write the failing tests** — append to `test/test_lap.c`, register after `test_lap_across_gps_outage_not_clean_valid`:

```c
/* C1/R22 (§6.4 step 1): a GPS dropout SHORTER than LAP_SEG_GAP_US that the S/F chord spans must
 * flag BOTH laps. The chord's t_cross is interpolated straight across the outage -- the reviewer's
 * repro_gap.c measured 227 ms of error on a 2.8 s dropout and the next lap still came out VALID,
 * eligible for best and best_sector. Only the >5 s case was covered before. */
static void test_short_dropout_chord_flags_the_opened_lap(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, &log);   /* out-lap opens */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, &log);   /* out-lap completes; lap 1 runs */
    TEST_ASSERT_EQUAL_UINT16(1, L.lap_no);                   /* prev valid fix = (0,-40) @ 80 s */

    /* a 2 s dropout (< LAP_SEG_GAP_US = 5 s) straddling the S/F, then the first valid fix north */
    feed(&L, lat0, lon0, 0.0, -20.0, 80500000, 0, false, &log);
    feed(&L, lat0, lon0, 0.0,  20.0, 81500000, 0, false, &log);
    feed(&L, lat0, lon0, 0.0,  40.0, 82000000, SPD_MMS, true, &log);

    TEST_ASSERT_EQUAL_UINT16(2, L.lap_no);                   /* lap 1 completed over the gap chord */
    TEST_ASSERT_TRUE(L.lap_flags & LAP_F_GPS_LOST);          /* FIX: the OPENED lap is flagged */

    /* lap 2 then runs on clean fixes and must still not be reported VALID */
    feed(&L, lat0, lon0, 300.0,  40.0, 112000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0, 300.0, -40.0, 117000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0, -40.0, 127000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 129000000, SPD_MMS, true, &log);
    const lap_result_t *l2 = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(2, l2->lap_no);
    TEST_ASSERT_TRUE(l2->flags & LAP_F_GPS_LOST);
    TEST_ASSERT_FALSE(l2->flags & LAP_F_VALID);
}

/* R22: a chord longer than LAP_SEG_GAP_US made of ABSENT fixes (never invalid ones, so the
 * in-lap GPS_LOST path never fires) must flag the COMPLETING lap too -- its end time is the
 * interpolation across the gap. Before R22 only the opened lap was flagged. */
static void test_long_absent_gap_flags_the_completing_lap(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, &log);
    forward_lap(&L, lat0, lon0, 40000000, 40000000, &log);   /* lap 1 runs; prev fix @ 80 s */
    TEST_ASSERT_FALSE(L.lap_flags & LAP_F_GPS_LOST);         /* clean so far */
    feed(&L, lat0, lon0, 0.0, 40.0, 88000000, SPD_MMS, true, &log);   /* 8 s chord, no fix between */
    const lap_result_t *l1 = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(1, l1->lap_no);
    TEST_ASSERT_TRUE(l1->flags & LAP_F_GPS_LOST);            /* FIX: the completing lap too */
    TEST_ASSERT_FALSE(l1->flags & LAP_F_VALID);
    TEST_ASSERT_TRUE(L.lap_flags & LAP_F_GPS_LOST);          /* and the opened one (pre-existing) */
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build test/build -j8 && ./test/build/test_lap`
Expected: `test_short_dropout_chord_flags_the_opened_lap` FAILS at `L.lap_flags & LAP_F_GPS_LOST`; `test_long_absent_gap_flags_the_completing_lap` FAILS at `l1->flags & LAP_F_GPS_LOST`.

- [ ] **Step 3: Implement**

`lap.h`, in the "previous VALID fix" block (`lap.h:140`):

```c
     bool               have_prev_fix;
+    bool               seg_has_invalid;     /* R22/§6.4 step 1: at least one invalid fix has been
+                                             * skipped since the last valid one, so the chord the
+                                             * next valid fix forms spans a GPS outage */
     geo_enu_t          prev_enu;
```

`lap.c` — clear it wherever `have_prev_fix` is cleared (`reset_to_no_venue:65`, `lap_set_venue:138`, `lap_import_rtc:329`): add `L->seg_has_invalid = false;` immediately after each `L->have_prev_fix = false;`.

`lap_on_fix`'s invalid branch (`lap.c:1069`):

```c
     if (!valid) {
+        /* R22: remember that the next chord spans this outage. §6.4 step 1's literal "no crossing
+         * is evaluated" cannot be implemented by dropping the crossing -- that would merge two
+         * laps into one -- so the crossing is still evaluated and both laps it touches carry
+         * LAP_F_GPS_LOST (see the amended §6.4 step 1). */
+        L->seg_has_invalid = true;
         if (L->state == LAP_ST_RUNNING) L->lap_flags |= LAP_F_GPS_LOST;
         return;
     }
```

At the very end of `lap_on_fix` (`lap.c:1093-1098`), after the `prev_*` update:

```c
     L->have_prev_fix = true;
+    L->seg_has_invalid = false;    /* this valid fix starts a clean segment (R22) */
 }
```

`process_segment` (`lap.c:1006-1010`):

```c
-        const bool    seg_gap = (now - L->prev_gps_us) > LAP_SEG_GAP_US;   /* H5: chord spans a GPS gap */
+        /* R22: a chord is unreliable either because it is long (absent fixes) or because it spans
+         * a fix the §6.5 validator rejected -- a 2.8 s dropout is under LAP_SEG_GAP_US and was
+         * silently accepted before (audit core:C1). */
+        const bool    seg_gap = ((now - L->prev_gps_us) > LAP_SEG_GAP_US) || L->seg_has_invalid;
```

`complete_lap` (`lap.c:795-798`) — the completing lap's boundary is just as interpolated as the opened one's:

```c
     uint8_t flags = L->lap_flags;                                  /* GPS_LOST / PIT / INCOMPLETE in-lap */
+    if (seg_gap) flags |= LAP_F_GPS_LOST;    /* R22: this lap's END is interpolated over the gap */
     if (is_out) flags |= LAP_F_OUT_LAP;
```

Also widen the `LAP_SEG_GAP_US` comment at `lap.c:20-23` to name both triggers (the exact replacement text is in Task 13, claim 9).

- [ ] **Step 4: Run to verify green, then check the fixtures**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`
Expected: `test_lap` green; `test_replay_fixtures` FAILS on `gps_dropout` only, at `laps[0].flags` (expected 8, got 9).

- [ ] **Step 5: Apply the one authorised fixture edit**

`test/data/gps_dropout.truth.json` has `gps.dropout_start_s = 120`, `dropout_end_s = 130`, and the out-lap's closing S/F crossing at `t = 126.87 s` — inside the dropout. The chord that closes lap 0 therefore spans a 10 s absent-fix gap, and R22 says that lap's end time is untrustworthy. Edit **exactly one value** in `test/data/gps_dropout.expected.json`:

```diff
-"laps":[{"lap_no":0,"flags":8,
+"laps":[{"lap_no":0,"flags":9,
```

(8 = `LAP_F_OUT_LAP`; 9 = `LAP_F_OUT_LAP | LAP_F_GPS_LOST`.) Do **not** run a regeneration script. Re-run `ctest`: every fixture must now pass with no other diff. **If any other field of any fixture differs, revert this edit, stop, and report** — a second change means the engine drifted, not that the ruling landed.

- [ ] **Step 6: Commit**

```bash
git add components/core/include/core/lap.h components/core/lapengine/lap.c \
        test/test_lap.c test/data/gps_dropout.expected.json
git commit   # fix(core): a crossing chord that spans an invalid or absent fix flags both laps GPS_LOST (F1a T2, R22, core:C1)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 2 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 2 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 3**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 3 — SERIAL (lap chain): all crossings in one segment processed in `t_cross` order (R23, core:H1)

**Files:**
- Modify: `components/core/lapengine/lap.c:577-646,969-1011`
- Test: `test/test_lap.c`

**Interfaces (produced, all file-static):**

```c
typedef struct { uint8_t k; int64_t t; } lap_cross_t;   /* k = 1-based sector gate number */
#define LAP_CROSS_MAX LAP_MAX_SECTORS
static void cross_sort(lap_cross_t *e, uint8_t nf);
static void evaluate_locked_sectors(lap_t *L, geo_enu_t cur, const gps_fix_t *fix,
                                    int64_t t_lo, int64_t t_hi, event_t *out, int cap, int *n);
static void evaluate_union(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, int64_t t_lo, int64_t t_hi);
static void evaluate_gates(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, int64_t t_lo, int64_t t_hi,
                           event_t *out, int cap, int *n);
```

`sec_fire_t` (`lap.c:577`) is renamed `lap_cross_t`; `handle_sector_cross` and `complete_lap` are unchanged — they are simply called in time order now.

**Design.** A segment holds at most **one** accepted S/F crossing (`process_segment` already picks the earliest `t`), so the S/F needs no collector slot: find it first, then run the gate crossings twice — once over `[INT64_MIN, t_sf)` (crossings belonging to the lap that is about to close) and once over `[t_sf, INT64_MAX)` (crossings belonging to the lap the S/F opens), with `process_sf_crossing` between the two. The collector is the existing `fired[]` array generalised: one entry per armed sector gate that fired, ≤ `LAP_CROSS_MAX` = `LAP_MAX_SECTORS` = 8 entries, ordered by a bounded insertion sort, consumed in order by `handle_sector_cross`. The disambiguation union does **not** need collector slots: it emits no event and its per-candidate scoring is order-free, so the same window filter alone is enough (a union gate crossed after the S/F must not score the out-lap that is closing; the second pass picks it up against the just-locked layout instead). Total entries in play ≤ `LAP_MAX_SECTORS + 1` = 9, inside the design's `2·LAP_MAX_SECTORS + 1` bound, and the two passes are sequential so only one 128 B `fired[]` frame exists at a time.

Each gate can fire at most once per segment and the window test is on `t_cross` (not on arm state), so a gate disarmed by pass 1 and re-armed by `open_lap` cannot fire twice. `LAP_EVT_MAX` therefore still bounds the event count: ≤ `LAP_MAX_SECTORS` SECTORs across both passes + `EV_LAYOUT_LOCKED` + `EV_LAP_COMPLETE` = 10 ≤ 11.

- [ ] **Step 1: Write the failing test** — append to `test/test_lap.c`, register after `test_sector_resync_after_skip`:

```c
/* H1/R23 (§6.4): a sector gate in the SAME previous->current segment as the S/F. §6.4's own
 * example is "an S/F line and the first sector line within 11 m at 200 km/h on a 5 Hz module".
 * Geometry lifted from the reviewer's repro_lap.c: gate 1 sits 6 m past the S/F, so the
 * (0,-40) -> (0,40) chord crosses the S/F at t_frac 0.5 and gate 1 at t_frac 0.575 -- the gate
 * belongs to the lap the S/F just opened. Before R23 sectors were evaluated first, the gate was
 * disarmed, dropped by handle_sector_cross (k < sec_next), and re-armed past the car: one lap in
 * three came out INCOMPLETE and was excluded from best and best_sector_ms. */
static void test_sector_just_past_sf_in_the_same_segment(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double north[1] = { 6.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 1, north);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, &log);   /* out-lap opens; gate 1 at +150 ms */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, &log);   /* out-lap completes; lap 1 opens */
    forward_lap(&L, lat0, lon0, 80000000, 40000000, &log);   /* lap 1 completes */

    const lap_result_t *l1 = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(1, l1->lap_no);
    TEST_ASSERT_EQUAL_UINT8(2, l1->n_sectors);               /* split 0 (S/F->gate 1) + split 1 */
    TEST_ASSERT_FALSE(l1->flags & LAP_F_INCOMPLETE);
    TEST_ASSERT_TRUE(l1->flags & LAP_F_VALID);
    TEST_ASSERT_EQUAL_UINT32(l1->time_ms, l1->sector_ms[0] + l1->sector_ms[1]);
    TEST_ASSERT_TRUE(l1->sector_ms[0] <= 1000);              /* 6 m at 40 m/s = 150 ms */

    /* ordering is observable in the event stream: on the fix that carries both crossings the
     * EV_LAP_COMPLETE (t_frac 0.5) precedes the EV_SECTOR (t_frac 0.575). */
    int i_complete = -1, i_sector = -1;
    for (int i = 0; i < log.n && i < 64; i++) {
        if (log.type[i] == EV_LAP_COMPLETE && log.arg16[i] == 0) i_complete = i;
        if (log.type[i] == EV_SECTOR && i_complete >= 0 && i_sector < 0) i_sector = i;
    }
    TEST_ASSERT_TRUE(i_complete >= 0);
    TEST_ASSERT_TRUE(i_sector > i_complete);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `./test/build/test_lap`
Expected: FAIL at `TEST_ASSERT_EQUAL_UINT8(2, l1->n_sectors)` / `LAP_F_INCOMPLETE` — lap 1 comes out INCOMPLETE with no split 0.

- [ ] **Step 3: Implement the collector and the two windowed evaluators**

Replace `lap.c:577` through the end of `evaluate_union` (`lap.c:646`) with:

```c
/* R23 (§6.4): every crossing accepted on ONE previous->current segment is processed in ascending
 * t_cross order, across the sector/S-F boundary. A segment holds at most one accepted S/F
 * crossing, so the S/F needs no collector slot: process_segment finds it first and runs the gate
 * crossings twice, over the half-open windows [INT64_MIN, t_sf) and [t_sf, INT64_MAX), with the
 * S/F handled between the two passes. The gate set in play is either the locked layout's sectors
 * (<= LAP_MAX_SECTORS) or, on an unlocked out-lap, the disambiguation union -- never both. The
 * union emits no event and its scoring is order-free, so it needs only the same window filter. */
typedef struct { uint8_t k; int64_t t; } lap_cross_t;   /* k = 1-based sector gate number */
#define LAP_CROSS_MAX LAP_MAX_SECTORS

/* Ascending insertion sort; both loops are bounded by nf <= LAP_CROSS_MAX (rule 2, no recursion). */
static void cross_sort(lap_cross_t *e, uint8_t nf)
{
    CORE_ASSERT_VOID(e != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(nf <= LAP_CROSS_MAX, LAP_ASSERT_CODE);   /* loop bound */
    for (uint8_t a = 1; a < nf; a++)
        for (uint8_t b = a; b > 0 && e[b - 1].t > e[b].t; b--) {
            const lap_cross_t tmp = e[b - 1];
            e[b - 1] = e[b];
            e[b] = tmp;
        }
}

/* Collect the locked layout's sector-gate crossings whose t_cross lies in [t_lo, t_hi), order
 * them and record each (§10.4 step 5, §10.7). Gates whose crossing falls outside the window are
 * left armed for the other pass. */
static void evaluate_locked_sectors(lap_t *L, geo_enu_t cur, const gps_fix_t *fix,
                                    int64_t t_lo, int64_t t_hi, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->locked_layout != NULL, LAP_ASSERT_CODE);    /* layout pointer valid before use */
    CORE_ASSERT_VOID(L->n_sec <= LAP_CROSS_MAX, LAP_ASSERT_CODE);   /* loop / fired[] bound */
    const int64_t now  = fix->gps_us;
    const double  v1   = (double)fix->gspeed_mms / 1000.0;
    const int8_t  want = L->locked_layout->dir_sign;   /* rule 9: one dereference level per expression */
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);                   /* ground speed non-negative */
    lap_cross_t fired[LAP_CROSS_MAX];
    uint8_t     nf = 0;
    for (uint8_t j = 0; j < L->n_sec; j++) {                        /* rule 2: <= LAP_CROSS_MAX */
        if (!L->sec_armed[j]) continue;
        double t; int dir;
        if (!geo_segment_cross(L->prev_enu, cur, L->sec_p[j], L->sec_q[j], &t, &dir)) continue;
        if (dir != want) continue;
        const int64_t tc = cross_time(L, cur, v1, t, now);
        if (tc < t_lo || tc >= t_hi) continue;                      /* belongs to the other pass */
        fired[nf].k = (uint8_t)(j + 1);
        fired[nf].t = tc;
        nf++;
    }
    cross_sort(fired, nf);
    for (uint8_t f = 0; f < nf; f++) {                              /* rule 2: nf <= LAP_CROSS_MAX */
        const uint8_t j = (uint8_t)(fired[f].k - 1);
        L->sec_armed[j] = false;
        L->sec_last_cross_us[j] = fired[f].t;
        handle_sector_cross(L, fired[f].k, fired[f].t, fix->mono_us, out, cap, n);
    }
}

/* Evaluate the disambiguation union over [t_lo, t_hi): record each gate's crossing time and
 * update every candidate's matching/foreign score (§10.5). No EV_SECTOR while the layout is
 * unknown, and no ordering needed -- the scores are a sum. */
static void evaluate_union(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, int64_t t_lo, int64_t t_hi)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_ugate <= LAP_MAX_UGATES, LAP_ASSERT_CODE);   /* loop bound */
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* inner loop bound */
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);                      /* ground speed non-negative */
    for (uint8_t gi = 0; gi < L->n_ugate; gi++) {
        lap_ugate_t *g = &L->ugate[gi];
        if (!g->armed) continue;
        double t; int dir;
        if (!geo_segment_cross(L->prev_enu, cur, g->p, g->q, &t, &dir)) continue;
        if (dir != g->dir_sign) continue;
        const int64_t tc = cross_time(L, cur, v1, t, now);
        if (tc < t_lo || tc >= t_hi) continue;                         /* belongs to the other pass */
        g->armed = false;
        g->last_cross_us = tc;
        if (g->cross_us == 0) g->cross_us = tc;
        for (uint8_t i = 0; i < L->n_cand; i++) {
            if (g->in_layout & (uint8_t)(1u << i)) L->cand[i].hits_matching++;
            else                                   L->cand[i].hits_foreign++;
        }
    }
}

/* Run whichever gate set is live for the current state over [t_lo, t_hi) (R23). */
static void evaluate_gates(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, int64_t t_lo, int64_t t_hi,
                           event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(t_lo < t_hi, LAP_ASSERT_CODE);   /* a non-empty half-open window */
    if (L->state != LAP_ST_RUNNING) return;
    if (L->locked)            evaluate_locked_sectors(L, cur, fix, t_lo, t_hi, out, cap, n);
    else if (L->n_ugate != 0) evaluate_union(L, cur, fix, t_lo, t_hi);
}
```

The `rearm_sectors` / `rearm_ugates` calls move out of the evaluators (they must happen once, before pass 1, not again between the passes — `open_lap` re-arms everything at the S/F anyway).

- [ ] **Step 4: Implement `process_segment`**

Replace `lap.c:969-1011` with:

```c
/* Evaluate the previous→current segment (§6.4) while ARMED/RUNNING: re-arm, find the S/F crossing,
 * then process every crossing in ascending t_cross order around it (R23). */
static void process_segment(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* matched[]/loop bound */
    if (!((L->state == LAP_ST_ARMED || L->state == LAP_ST_RUNNING) && L->have_prev_fix)) return;
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    rearm_sf(L, cur, now);
    if (L->state == LAP_ST_RUNNING) {
        if (L->locked)            rearm_sectors(L, cur, now);
        else if (L->n_ugate != 0) rearm_ugates(L, cur, now);
    }

    /* The S/F crossing is found FIRST so the gate crossings can be split around its t_cross. */
    bool   matched[TRK_MAX_LAYOUTS];
    bool   hit = false;
    double t_earliest = 2.0;    /* sentinel > 1.0; geo_segment_cross's t is in [0,1] */
    for (uint8_t i = 0; i < L->n_cand; i++) {                          /* rule 2: <= TRK_MAX_LAYOUTS */
        matched[i] = false;
        lap_cand_t *c = &L->cand[i];
        if (!c->sf_armed) continue;
        double t = 0.0;
        int    dir = 0;
        if (!geo_segment_cross(L->prev_enu, cur, c->sf_p, c->sf_q, &t, &dir)) continue;
        const int8_t want = c->layout->dir_sign;   /* rule 9: one dereference level per expression */
        if (dir != want) continue;                 /* only the layout's own crossing direction */
        matched[i] = true;
        hit = true;
        if (t < t_earliest) t_earliest = t;
    }
    const int64_t t_sf = hit ? cross_time(L, cur, v1, t_earliest, now) : INT64_MAX;
    /* R22: a chord is unreliable either because it is long (absent fixes) or because it spans a
     * fix the §6.5 validator rejected (audit core:C1). */
    const bool seg_gap = ((now - L->prev_gps_us) > LAP_SEG_GAP_US) || L->seg_has_invalid;

    evaluate_gates(L, cur, fix, INT64_MIN, t_sf, out, cap, n);     /* crossings before the S/F */
    if (!hit) return;
    process_sf_crossing(L, fix, t_sf, seg_gap, matched, out, cap, n);
    evaluate_gates(L, cur, fix, t_sf, INT64_MAX, out, cap, n);     /* and after it, for the new lap */
}
```

Add `#include <stdint.h>`'s `INT64_MIN`/`INT64_MAX` — `lap.h` already pulls `<stdint.h>`; no new include.

- [ ] **Step 5: Run to verify green**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`
Expected: all green, including `test_replay_fixtures` with **no** diff — in the killarney/zwartkops fixtures (5 Hz, sectors ~1.1 km apart) no sector and S/F ever share a segment, so pass 1's window is `[MIN, MAX)` and the behaviour is byte-identical. If a fixture moves, stop and report.

- [ ] **Step 6: Commit**

```bash
git add components/core/lapengine/lap.c test/test_lap.c
git commit   # fix(core): process every crossing in a fix segment in t_cross order -- a sector gate beside the S/F is no longer lost (F1a T3, R23, core:H1)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 3 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 3 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 4**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 4 — SERIAL (lap chain): per-venue/layout state hygiene and real return codes (R25, core:H2/H3/M3)

**Files:**
- Modify: `components/core/include/core/lap.h:149-154,179,202-205`, `components/core/lapengine/lap.c:44-79,117-154,263-338,440-470,660-689`
- Test: `test/test_lap.c`

**Interfaces (produced):**

```c
int lap_force_layout(lap_t *L, uint16_t layout_id);   /* was void. 0 applied, -1 the venue has no
                                                       * such layout (forced id left untouched) */
int lap_import_rtc(lap_t *L, const lap_rtc_t *s);     /*  0 restored, -1 unknown venue,
                                                       * -2 unknown layout, -3 sector_idx out of range */
static void clear_best_sectors(lap_t *L);
static bool venue_has_layout(const trk_venue_t *v, uint16_t layout_id);
```

`pipeline.c:887`'s `lap_force_layout(&s_lap, cmd->arg16);` still compiles unchanged (the result is discarded, and no `warn_unused_result` attribute is added). Surfacing that −1 to the operator belongs to **F2b** — Task 14 files it.

- [ ] **Step 1: Write the failing tests** — append to `test/test_lap.c`, register after `test_rtc_import_unknown_venue_fails`:

```c
/* H2/R25 (§10.8): best_sector_ms[] / the theoretical best belong to one (venue, layout) pair.
 * Venue A runs 50 s laps, venue B 100 s laps in the same session; the reviewer's repro_bestsec.c
 * showed B reporting A's splits, i.e. an unattainable theoretical best. */
static void test_best_sectors_cleared_on_venue_change(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double north[1] = { 600.0 };                   /* one sector gate, well clear of the S/F */
    trk_venue_t a; build_venue_sec(&a, lat0, lon0, 1, north);
    trk_venue_t b; build_venue_sec(&b, lat0, lon0, 1, north);
    b.id = (uint16_t)(TRK_USER_ID_BASE + 1u);

    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &a);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    int64_t t = 0;
    for (int i = 0; i < 4; i++) { forward_lap(&L, lat0, lon0, t, 50000000, NULL); t += 50000000; }
    const uint32_t best_a = lap_theoretical_best_ms(&L);
    TEST_ASSERT_TRUE(best_a > 0);
    TEST_ASSERT_TRUE(best_a <= 50500);

    lap_set_venue(&L, &b);                               /* venue change inside the session */
    TEST_ASSERT_EQUAL_UINT32(0, lap_theoretical_best_ms(&L));   /* FIX: nothing carried over */
    TEST_ASSERT_EQUAL_UINT8(0, L.best_sector_count);

    arm_at_start(&L, lat0, lon0, t, NULL);
    for (int i = 0; i < 4; i++) { forward_lap(&L, lat0, lon0, t, 100000000, NULL); t += 100000000; }
    const uint32_t best_b = lap_theoretical_best_ms(&L);
    TEST_ASSERT_TRUE(best_b > 90000);                    /* B's own splits, not A's 50 s ones */
}

/* H3/R25: lap_force_layout with an id the venue does not have must be refused. Before the fix
 * arm_candidates() matched nothing, n_cand went to 0, no S/F gate was armed and lap timing was
 * silently dead for the whole session -- reachable from CMD_SET_LAYOUT's arbitrary arg16. */
static void test_force_layout_unknown_id_is_refused(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    TEST_ASSERT_EQUAL_INT(-1, lap_force_layout(&L, 99));
    TEST_ASSERT_EQUAL_UINT16(0, L.forced_layout_id);     /* still Auto */
    TEST_ASSERT_EQUAL_UINT8(1, L.n_cand);                /* the venue's layout is still armed */
    TEST_ASSERT_EQUAL_INT(0, lap_force_layout(&L, 1));   /* the real id is accepted */

    arm_at_start(&L, lat0, lon0, 0, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, &log);
    forward_lap(&L, lat0, lon0, 40000000, 50000000, &log);
    TEST_ASSERT_EQUAL_INT(2, count_type(&log, EV_LAP_COMPLETE));   /* laps still complete */
}

/* H3/M3/R25: lap_import_rtc validates the layout id and the sector index, and a snapshot that
 * claims a lap but no locked layout resumes ARMED -- RUNNING with n_sec == 0 left §10.5/§10.8
 * dead for the rest of the session while laps still completed VALID with n_sectors == 0. */
static void test_import_rtc_rejects_bad_layout_and_sector_idx(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));

    lap_t L; lap_init(&L, NULL);
    lap_rtc_t s; memset(&s, 0, sizeof s);
    s.venue_id = TRK_USER_ID_BASE; s.layout_id = 42; s.lap_no = 3; s.lap_start_gps_us = 1000000;
    TEST_ASSERT_EQUAL_INT(-2, lap_import_rtc(&L, &s));
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));

    memset(&s, 0, sizeof s);
    s.venue_id = TRK_USER_ID_BASE; s.layout_id = 1; s.lap_no = 3; s.sector_idx = 255;
    TEST_ASSERT_EQUAL_INT(-3, lap_import_rtc(&L, &s));

    memset(&s, 0, sizeof s);                              /* lap_no > 0 but no locked layout */
    s.venue_id = TRK_USER_ID_BASE; s.layout_id = 0; s.lap_no = 3; s.lap_start_gps_us = 1000000;
    TEST_ASSERT_EQUAL_INT(0, lap_import_rtc(&L, &s));
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    TEST_ASSERT_EQUAL_UINT16(0, L.lap_no);
    TEST_ASSERT_EQUAL_UINT8(1, L.n_cand);                 /* the S/F gate IS armed */
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `./test/build/test_lap`
Expected: FAIL — compile errors on `TEST_ASSERT_EQUAL_INT(-1, lap_force_layout(...))` (void), and the best-sector case reports A's number after the venue change.

- [ ] **Step 3: Implement the helpers**

```c
/* §10.8: the best-sector table and the theoretical best belong to ONE (venue, layout) pair.
 * Carrying them across a venue or layout change reports a lap nobody drove (audit core:H2). */
static void clear_best_sectors(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    memset(L->best_sector_ms, 0, sizeof L->best_sector_ms);
    memset(L->have_best_sector, 0, sizeof L->have_best_sector);
    L->best_sector_count = 0;
}

/* Does the venue carry this layout id? Forcing or resuming an id it lacks armed ZERO candidates:
 * no S/F gate, no error, lap timing dead for the session (audit core:H3). */
static bool venue_has_layout(const trk_venue_t *v, uint16_t layout_id)
{
    CORE_ASSERT_RET(v != NULL, LAP_ASSERT_CODE, false);
    const uint8_t n = v->n_layouts;                /* rule 9: one dereference level per expression */
    CORE_ASSERT_RET(n <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE, false);   /* loop bound */
    for (uint8_t i = 0; i < n; i++) if (v->layouts[i].id == layout_id) return true;
    return false;
}
```

- [ ] **Step 4: Implement the clears and the return codes**

`reset_to_no_venue` (`lap.c:62`) — replace `L->best_sector_count = 0;` with `clear_best_sectors(L);` and rewrite the trailing comment:

```c
-    L->best_sector_count = 0;
+    clear_best_sectors(L);                  /* R25: a reset drops the venue, so §10.8 goes with it */
...
-    /* best/prev results, best_sector table, the predictive reference table and cfg are preserved
-     * across a reset (§10.3); best_sector_count must still be within the best_sector_ms[]/
-     * have_best_sector[] arrays it indexes elsewhere, or a later read would run past them. */
-    CORE_ASSERT_VOID(L->best_sector_count <= LAP_MAX_SECTORS + 1, LAP_ASSERT_CODE);
+    /* best/prev lap results, the predictive reference table and cfg are preserved across a reset
+     * (§10.3). The §10.8 best-sector table is NOT: it is per (venue, layout) and the reset drops
+     * both, so keeping it would mix two tracks' splits (R25, audit core:H2). */
+    CORE_ASSERT_VOID(L->best_sector_count == 0, LAP_ASSERT_CODE);   /* clear_best_sectors ran */
```

`lap_set_venue` (`lap.c:117-147`) — drop a forced id the new venue lacks, and clear the table:

```c
     CORE_ASSERT_VOID(v->n_layouts <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* venue layout count */
+    if (L->forced_layout_id != 0 && !venue_has_layout(v, L->forced_layout_id))
+        L->forced_layout_id = 0;        /* H3: never arm zero candidates -- fall back to Auto */
     geo_origin_set(&L->origin, v->lat, v->lon);
...
     L->n_ugate = 0;
+    clear_best_sectors(L);              /* R25: a new venue starts a new §10.8 table */
     reset_lap_sectors(L);
     L->have_prev_fix = false;
+    L->seg_has_invalid = false;
```

`lap_force_layout` (`lap.c:149-154`):

```c
int lap_force_layout(lap_t *L, uint16_t layout_id)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, -1);
    if (layout_id != 0 && L->venue != NULL && !venue_has_layout(L->venue, layout_id))
        return -1;                       /* R25: refuse, and leave the forced id where it was */
    if (layout_id == L->forced_layout_id) return 0;          /* no change: keep the lap and §10.8 */
    L->forced_layout_id = layout_id;     /* §10.5: lock to this layout and re-arm its S/F */
    clear_best_sectors(L);               /* R25: a layout change invalidates §10.8 */
    if (L->venue) arm_candidates(L);
    CORE_ASSERT_RET(L->venue == NULL || L->n_cand > 0, LAP_ASSERT_CODE, -1);   /* H3 postcondition */
    return 0;
}
```

(`L->venue != NULL && !venue_has_layout(L->venue, ...)` reads `L->venue` once into the call's parameter — one dereference level in the expression.)

`lock_layout` (`lap.c:460-470`) — a lock starts a layout's accounting:

```c
     L->cand[0] = L->cand[idx];
     L->n_cand = 1;
+    clear_best_sectors(L);              /* R25: the locked layout owns the §10.8 table */
     project_locked_sectors(L);
```

`lap_import_rtc` (`lap.c:287-338`):

```c
     const trk_venue_t *v = trk_get(s->venue_id);
     if (!v) return -1;
+    if (s->layout_id != 0 && !venue_has_layout(v, s->layout_id)) return -2;   /* R25/core:H3 */
+    if (s->sector_idx > LAP_MAX_SECTORS) return -3;                           /* R25/core:M3 */
...
     L->n_sec = 0;
-    L->best_sector_count = 0;
+    clear_best_sectors(L);
...
-    L->state = LAP_ST_RUNNING;
+    /* R25: a snapshot that claims a lap number but no locked layout cannot resume sector timing
+     * or §10.8 -- disambiguate_and_lock is only reachable at lap_no == 0 -- so it resumes ARMED
+     * and the next S/F crossing opens a fresh out-lap. An out-lap snapshot resumes RUNNING. */
+    const bool resume_running = (s->layout_id != 0 || s->lap_no == 0);
+    L->state = resume_running ? LAP_ST_RUNNING : LAP_ST_ARMED;
     L->mode = LAP_MODE_NORMAL;
-    L->lap_no = s->lap_no;
-    L->lap_start_gps_us = s->lap_start_gps_us;
-    L->lap_flags = LAP_F_INTERRUPTED;        /* §10.10 / §10.4 step 3 */
+    L->lap_no = resume_running ? s->lap_no : 0u;
+    L->lap_start_gps_us = resume_running ? s->lap_start_gps_us : 0;
+    L->lap_flags = resume_running ? (uint8_t)LAP_F_INTERRUPTED : 0u;   /* §10.10 / §10.4 step 3 */
     L->lap_dist_m = 0.0;
     memcpy(L->gate_times, s->gate_times, sizeof L->gate_times);
     L->sec_next = (uint8_t)(s->sector_idx + 1);
+    CORE_ASSERT_RET(L->sec_next <= LAP_MAX_SECTORS + 1, LAP_ASSERT_CODE, -3);   /* indexes gate_times[] */
...
     L->have_prev_fix = false;
+    L->seg_has_invalid = false;
```

`lap_export_rtc` (`lap.c:276`) — guard the round trip the other way:

```c
+    CORE_ASSERT_VOID(L->sec_next >= 1 && L->sec_next <= LAP_MAX_SECTORS + 1, LAP_ASSERT_CODE);
     out->sector_idx       = (uint8_t)(L->sec_next - 1);   /* sector gates crossed so far this lap */
```

`lap.h`: change `void lap_force_layout(...)` to `int lap_force_layout(...)` at `lap.h:179` with the return doc; rewrite the `lap_import_rtc` doc block (`lap.h:202-205`) per Task 13 claim 3.

- [ ] **Step 5: Run to verify green**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`
Expected: all green; fixtures unchanged (no fixture changes venue or forces a layout).

- [ ] **Step 6: Commit**

```bash
git add components/core/include/core/lap.h components/core/lapengine/lap.c test/test_lap.c
git commit   # fix(core): clear best sectors on venue/layout change; refuse unknown layout ids; validate RTC snapshots (F1a T4, R25, core:H2/H3/M3)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 4 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 4 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 5**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

- [ ] **SESSION 1 CHECKPOINT (Tasks 1-4)** — before the context is cleared, re-verify the session's whole output, then record it:

```bash
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %, no fixture diff
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                                    # 0
git status --short                                         # clean
printf 'Session 1 (Tasks 1-4) verified: ctest 100%%, both linters 0, tree clean\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push origin fix-core-engines
```

If `ctest` or either linter fails here, the session is **not** checkpointed: fix forward on this branch (or `git revert` the offending task's commit) and re-run before writing the line.

---

### Task 5 — SERIAL (lap chain): `EV_VENUE_LOST` (design §3, R4's lap-engine half)

**Files:**
- Modify: `components/core/include/core/event.h:69-71`, `components/core/lapengine/lap.c:1075-1079`, `components/app/pipeline/pipeline.c:216,229,420`, `components/app/logger/logger.c:644`
- Test: `test/test_lap.c`

**Interfaces (produced):**

```c
/* core/event.h */
    EV_VENUE_LOST = 20,   /* arg16 = the venue id just left                       (lapengine, LOGGED) */
    EV_COUNT      = 21    /* one past the highest code; asserts use `type < EV_COUNT` */
```

Logged codes are append-only, so `EV_VENUE_LOST` takes **20** (after the existing ui-only 16–19) and `EV_FAULT` stays 15. **F2b** consumes it (close the session on a venue change, R4); this task only produces it and makes the four event assertions tolerate it.

- [ ] **Step 1: Write the failing test** — append to `test/test_lap.c`, register after `test_leave_venue_after_factor_radius_for_timeout`:

```c
/* §10.3 + design §3: leaving a venue emits EV_VENUE_LOST carrying the venue id, before the engine
 * drops the venue pointer. F2b closes the open session on it (ruling R4). */
static void test_leave_venue_emits_venue_lost(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    const double lat_s = lat_of(lat0, -300.0);
    const double lon_far = lon_of(lon0, lat0, -4000.0);              /* 4 km out: > radius * 1.5 */

    gps_fix_t f0 = fix_ll(lat_s, lon_of(lon0, lat0, -500.0), 1000000, 20000, true);
    drive_fix(&L, &f0, &log);
    gps_fix_t f1 = fix_ll(lat_s, lon_far, 2000000, 20000, true);     /* leave timer starts */
    drive_fix(&L, &f1, &log);
    TEST_ASSERT_EQUAL_INT(0, count_type(&log, EV_VENUE_LOST));
    gps_fix_t f2 = fix_ll(lat_s, lon_far, 63000000, 0, true);        /* +61 s: leave */
    drive_fix(&L, &f2, &log);

    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_VENUE_LOST));
    for (int i = 0; i < log.n && i < 64; i++)
        if (log.type[i] == EV_VENUE_LOST) TEST_ASSERT_EQUAL_UINT16(TRK_USER_ID_BASE, log.arg16[i]);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build -j8` → compile error, `EV_VENUE_LOST` undeclared.

- [ ] **Step 3: Implement — `core/event.h`**

```c
-    EV_CREATE        = 19   /* ui-only: posted straight to g_ui_evt_q by the pipeline (#97, §10.9).
+    EV_CREATE        = 19,  /* ui-only: posted straight to g_ui_evt_q by the pipeline (#97, §10.9).
                              * flags = EV_CREATE_* phase below; arg16 = gate index (GATE_SET) or
                              ... (unchanged body) ... */
+
+    /* Logged again from here. Logged codes are append-only, so leaving a venue takes the next free
+     * integer AFTER the ui-only block rather than renumbering it (audit fix wave design §3). */
+    EV_VENUE_LOST    = 20,  /* arg16 = the venue id just left                     (lapengine) */
+    EV_COUNT         = 21   /* one past the highest code. Producers/consumers assert
+                             * `type < EV_COUNT`; there is no contiguous "logged" prefix to
+                             * compare against any more. */
 };
```

- [ ] **Step 4: Implement — the emit in `lap.c`**

```c
     /* Leave-venue watch runs in every venue-bound state (§10.3). */
     if (update_leave_venue(L, lat, lon, now)) {
+        const trk_venue_t *lv = L->venue;    /* rule 9: read before reset_to_no_venue clears it */
+        const uint16_t left = (lv != NULL) ? lv->id : 0u;
+        emit(out, cap, n, EV_VENUE_LOST, 0, left, now, fix->mono_us, 0, 0);
         reset_to_no_venue(L);
         return;
     }
```

`lap.h`'s `LAP_EVT_MAX` comment is corrected in Task 13 (claim 5) — `EV_VENUE_LOST` is a single event followed by an immediate `return`, so the bound does not move.

- [ ] **Step 5: Implement — the four assertions (the only other app edit in this plan)**

```c
/* components/app/pipeline/pipeline.c:216  (emit_event's broadcast guard) */
-    LT_ASSERT_VOID(ev->type <= EV_FAULT, PIPE_ASSERT_CODE);  /* only stable §4.5 codes are broadcast */
+    LT_ASSERT_VOID(ev->type < EV_COUNT, PIPE_ASSERT_CODE);   /* a code core/event.h defines (design §3) */

/* components/app/pipeline/pipeline.c:229  (emit_event's constructor) */
-    LT_ASSERT_VOID(type != EV_NONE && type <= EV_FAULT, PIPE_ASSERT_CODE);   /* a real §4.5 event code */
+    LT_ASSERT_VOID(type != EV_NONE && type < EV_COUNT, PIPE_ASSERT_CODE);    /* a real §4.5 event code */

/* components/app/pipeline/pipeline.c:420  (engine_cb) */
-    LT_ASSERT_VOID(ev->type <= EV_FAULT, PIPE_ASSERT_CODE);  /* stable §4.5 code, drives the switch */
+    LT_ASSERT_VOID(ev->type < EV_COUNT, PIPE_ASSERT_CODE);   /* stable §4.5 code, drives the switch */

/* components/app/logger/logger.c:644  (handle_event) */
-    LT_ASSERT_VOID(ev->type <= EV_FAULT, LOG_ASSERT_CODE);   /* the EV_* set is closed and stable (core/event.h) */
+    LT_ASSERT_VOID(ev->type < EV_COUNT, LOG_ASSERT_CODE);    /* the EV_* set is closed and stable (core/event.h) */
```

No other `logger.c` change: it already writes an EVENT record for any `type` it receives on `g_evt_q`, so `EV_VENUE_LOST` is logged with no switch edit.

- [ ] **Step 6: Run to verify green, then build the firmware once**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
source tools/idf-env.sh && ./build.sh moto_sim build        # 0 warnings; the asserts compile
```

- [ ] **Step 7: Commit**

```bash
git add components/core/include/core/event.h components/core/lapengine/lap.c \
        components/app/pipeline/pipeline.c components/app/logger/logger.c test/test_lap.c
git commit   # feat(core): EV_VENUE_LOST (20) + EV_COUNT; leaving a venue is now an event F2b can close a session on (F1a T5, design §3, R4)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 5 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 5 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 6**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 6 — SERIAL (lap chain): predictive table really halves density (§10.11, core LOW)

**Files:**
- Modify: `components/core/include/core/lap.h:165-174`, `components/core/lapengine/lap.c:44-79,117-147,287-338,342-385,757-773`
- Test: `test/test_lap.c`

**Interfaces (produced):** `lap_t.pred_stride` (`uint16_t`, 1 = every fix); `PRED_STRIDE_MAX` (file-static macro in `lap.c`). `lap_t.pred_rec_fixes` keeps its name and meaning (fixes **offered** to the recorder this lap) and becomes live.

- [ ] **Step 1: Write the failing assertions** — extend the existing `test_predictive_table_cap` in `test/test_lap.c` (it already drives 700 fixes into a 600-entry table):

```c
     TEST_ASSERT_TRUE(L.pred_rec_fixes >= 700);
     TEST_ASSERT_TRUE(L.pred_rec_n > 0);
     TEST_ASSERT_TRUE(L.pred_rec_n <= PRED_TABLE_MAX);              /* never overflows the fixed table */
+    /* §10.11 "max 600 entries; beyond that every 2nd fix": one halving has happened, so the
+     * stride is 2 and the table holds the 300 survivors plus ~half of the remaining ~100 fixes.
+     * Before the fix pred_rec_fixes was incremented and never read -- recording continued at FULL
+     * density and the halving comment was false (audit core LOW / false claim 2). */
+    TEST_ASSERT_EQUAL_UINT16(2, L.pred_stride);
+    TEST_ASSERT_TRUE(L.pred_rec_n >= 300);
+    TEST_ASSERT_TRUE(L.pred_rec_n < 420);
```

- [ ] **Step 2: Run to verify it fails**

Run: `./test/build/test_lap` → compile error (`pred_stride` undeclared); after declaring it, FAIL with `pred_stride == 0`.

- [ ] **Step 3: Implement**

`lap.h` (`lap.h:173-174`):

```c
-    uint32_t           pred_rec_fixes;
+    uint32_t           pred_rec_fixes;     /* fixes OFFERED to the recorder this lap (stride counter) */
+    uint16_t           pred_stride;        /* §10.11 recording density: 1 = every fix, doubled on
+                                            * each halving up to PRED_STRIDE_MAX */
     uint16_t           pred_cap;           /* entries each buffer holds; 0 = predictive disabled */
```

`lap.c`:

```c
+/* §10.11: the recording table is halved in place when it fills and the density halves with it, so
+ * the stride doubles. Capped so the stride stays a bounded, statically evident divisor (rule 2). */
+#define PRED_STRIDE_MAX 64u
```

```c
-/* Append the current (distance, elapsed) sample of the lap in progress to the recording table. Beyond
- * PRED_TABLE_MAX entries the table is halved in place and recording continues at half density ("every
- * 2nd fix"), so it always holds <= PRED_TABLE_MAX monotonic-distance samples spanning the whole lap. */
+/* Append the current (distance, elapsed) sample of the lap in progress. When the table fills it is
+ * halved in place AND the recording stride doubles (§10.11 "beyond that every 2nd fix"), so the
+ * table always holds <= pred_cap monotonic-distance samples spanning the whole lap at an even
+ * density. Samples the stride skips are counted but not stored. */
 static void pred_record(lap_t *L, int64_t now)
 {
     CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
     if (L->pred_cap == 0) return;                        /* predictive disabled: no buffers to fill */
     CORE_ASSERT_VOID(L->pred_rec_dist_m != NULL, LAP_ASSERT_CODE);   /* enabled => buffers set */
     CORE_ASSERT_VOID(L->pred_rec_t_ms != NULL, LAP_ASSERT_CODE);
+    CORE_ASSERT_VOID(L->pred_stride >= 1u, LAP_ASSERT_CODE);         /* used as a divisor below */
     if (now <= L->lap_start_gps_us) return;
+    const uint32_t seq = L->pred_rec_fixes++;
+    if (L->pred_stride > 1u && (seq % (uint32_t)L->pred_stride) != 0u) return;   /* §10.11 density */
     uint32_t t_ms = (uint32_t)((now - L->lap_start_gps_us + 500) / 1000);
     double d = L->lap_dist_m;
     if (d < 0.0) d = 0.0; else if (d > (double)UINT16_MAX) d = (double)UINT16_MAX;
     uint16_t d16 = (uint16_t)llround(d);
-    L->pred_rec_fixes++;

     if (L->pred_rec_n >= L->pred_cap) {                  /* full: keep every 2nd entry, then continue */
         uint16_t m = 0;
-        for (uint16_t i = 0; i < L->pred_rec_n; i += 2) {
+        for (uint16_t i = 0; i < L->pred_rec_n; i += 2) {    /* rule 2: pred_rec_n <= pred_cap */
             L->pred_rec_dist_m[m] = L->pred_rec_dist_m[i];
             L->pred_rec_t_ms[m]   = L->pred_rec_t_ms[i];
             m++;
         }
         L->pred_rec_n = m;
+        if (L->pred_stride < PRED_STRIDE_MAX) L->pred_stride = (uint16_t)(L->pred_stride * 2u);
     }
```

Set `L->pred_stride = 1;` wherever `pred_rec_fixes` is zeroed — `lap_set_predictive` (`lap.c:353`), `open_lap` (`lap.c:772`), `lap_set_venue` (`lap.c:145`), `reset_to_no_venue` (`lap.c:74`), `lap_import_rtc` (`lap.c:336`). `lap_init`'s `memset` leaves stride 0, which is safe: `pred_cap == 0` returns before the assertion.

- [ ] **Step 4: Run to verify green**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure` → all green (the fixtures do not enable predictive: `replay_run.c` never calls `lap_set_predictive`).

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/lap.h components/core/lapengine/lap.c test/test_lap.c
git commit   # fix(core): the predictive table's halving really halves density (§10.11); pred_rec_fixes is no longer dead (F1a T6)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 6 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 6 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 7**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 7 — SERIAL (lap chain): `lap_mark_gate` must not confirm a degenerate gate (core:M5)

**Files:**
- Modify: `components/core/lapengine/lap.c:216-261`
- Test: `test/test_lap.c`, `test/test_lap_gate.c`

**Interfaces:** `lap_mark_gate`'s signature is unchanged; its contract gains "−1 when the fix's position or heading is out of range, or the derived line is degenerate".

- [ ] **Step 1: Write the failing tests**

`test/test_lap_gate.c` — pin `lap_gate_line`'s rejection contract (register in its `main`):

```c
/* core:M5 (§10.9): lap_gate_line returns a ZEROED line when its lat/lon/heading preconditions
 * fail. Callers must test for that -- lap_mark_gate used to store the (0,0) line and report
 * success, so the UI confirmed a gate that trk_validate_venue's line_ok later rejected, aborting
 * the whole creation at finalize_create. */
static void test_gate_line_rejects_out_of_range_inputs(void)
{
    const trk_line_t bad_head = lap_gate_line(-34.0, 18.0, 400.0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bad_head.p1.lat);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bad_head.p2.lon);
    const trk_line_t bad_lat = lap_gate_line(-91.0, 18.0, 0.0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bad_lat.p1.lat);
    const trk_line_t ok = lap_gate_line(-34.0, 18.0, 90.0);
    TEST_ASSERT_TRUE(ok.p1.lat != ok.p2.lat || ok.p1.lon != ok.p2.lon);
}
```

`test/test_lap.c` — register after `test_create_cancel`:

```c
/* core:M5: head_e5 is a signed int32 straight off NAV-PVT's headMot, so an out-of-range heading is
 * reachable from the wire. lap_mark_gate must refuse it instead of storing a (0,0) line and
 * returning 0. */
static void test_mark_gate_refuses_an_out_of_range_heading(void)
{
    lap_t L; lap_init(&L, NULL);
    lap_create_begin(&L);
    gps_fix_t f = fix_ll(-34.0, 18.0, 1000000, 20000, true);
    f.head_e5 = 40000000;                                   /* 400 deg: out of range */
    trk_layout_t ly;
    TEST_ASSERT_EQUAL_INT(-1, lap_mark_gate(&L, 0, &f, &ly));
    TEST_ASSERT_FALSE(L.create_have_sf);
    f.head_e5 = -9000000;                                   /* -90 deg: out of range */
    TEST_ASSERT_EQUAL_INT(-1, lap_mark_gate(&L, 0, &f, &ly));
    f.head_e5 = 9000000;                                    /* 90 deg: accepted */
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 0, &f, &ly));
    TEST_ASSERT_TRUE(L.create_have_sf);
    TEST_ASSERT_TRUE(ly.sf.p1.lat != ly.sf.p2.lat || ly.sf.p1.lon != ly.sf.p2.lon);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `./test/build/test_lap` → FAIL, `lap_mark_gate` returns 0 for `head_e5 = 40000000`.

- [ ] **Step 3: Implement**

```c
     if (L->mode != LAP_MODE_CREATE) return -1;
     if (!fix || fix->valid == 0 || fix->gspeed_mms <= 0) return -1;   /* need a heading of motion */

     const double lat = (double)fix->lat_e7 / 1e7;
     const double lon = (double)fix->lon_e7 / 1e7;
     const double heading_deg = (double)fix->head_e5 / 1e5;
+    /* core:M5: lap_gate_line answers an out-of-range lat/lon/heading with a ZEROED line (its own
+     * CORE_ASSERT_RETs). head_e5 is a signed int32 straight off NAV-PVT's headMot, so that is
+     * reachable from the wire, not just from a buggy caller -- range-check here, and verify the
+     * line the geometry actually returned, so a degenerate gate can never be confirmed to the UI
+     * only for trk_validate_venue to reject the venue at finalize_create. */
+    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return -1;
+    if (heading_deg < 0.0 || heading_deg > 360.0) return -1;
     const trk_line_t line = lap_gate_line(lat, lon, heading_deg);
+    if (line.p1.lat == line.p2.lat && line.p1.lon == line.p2.lon) return -1;   /* degenerate line */
```

- [ ] **Step 4: Run to verify green; Step 5: Commit**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
git add components/core/lapengine/lap.c test/test_lap.c test/test_lap_gate.c
git commit   # fix(core): lap_mark_gate checks lap_gate_line's result and the fix's heading range (F1a T7, core:M5)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 7 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 7 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 8**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 8 — PARALLEL with the lap chain, SERIAL before Task 9: the false-start abort (R21, core:H5/M4)

**Files:**
- Modify: `components/core/dragengine/drag.c:403-435,541-591`
- Test: `test/test_drag.c`

**Interfaces (produced):**

```c
/* drag.c, file-static */
static void abort_to_armed(drag_t *D, event_t *out, int cap, int *n, int64_t gps_us, int64_t mono_us);
```

- [ ] **Step 1: Write the failing test** — append to `test/test_drag.c`, register after `test_low_g_launch_no_false_start`:

```c
/* H5/R21 (§11.2 "Abort"): a marginal-g trigger that then stalls. do_launch can enter LAUNCHED with
 * v_peak as low as DRAG_LAUNCH_G x DRAG_LAUNCH_HOLD_MS = 0.1471 m/s = 0.53 km/h -- BELOW the old
 * abort guard's DRAG_FALSE_START_KMH (1 km/h) but ABOVE `stopped`'s 0.5 km/h. So the stall fell
 * through to enter_done: a consumed run_no, an EV_DRAG_DONE and a gate-less DRAG_RUN log frame.
 * Geometry from the reviewer's repro_drag.c case A (0.16 g for 11 samples, then Doppler 0). */
static void test_marginal_g_launch_then_stall_aborts(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_ARMED));
    for (int k = LAUNCH_K; k <= 211; k++) {                 /* 11 samples just over DRAG_LAUNCH_G */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.16f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_LAUNCH));

    gps_fix_t stall = gfix((int64_t)211 * DT_US, 0, true);  /* Doppler: the bike never moved */
    drag_on_fix(&D, &stall);
    fused_sample_t s212 = fused((int64_t)212 * DT_US, 0.0f, 0);
    drive_fused(&s212, true);

    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_ARMED, drag_state(&D));     /* §11.2: discarded, re-armed */
    TEST_ASSERT_EQUAL_INT(0, ev_count(EV_DRAG_DONE));           /* no spurious run */
    TEST_ASSERT_EQUAL_INT(2, ev_count(EV_DRAG_ARMED));          /* M4: the re-arm is announced */
    TEST_ASSERT_EQUAL_UINT16(0, drag_current(&D)->run_no);      /* the run was wiped */

    /* and the next, real launch takes run number 1 -- the discarded one consumed nothing */
    for (int k = 300; k <= 320; k++) {
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.5f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    TEST_ASSERT_EQUAL_UINT16(1, drag_current(&D)->run_no);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build -j8 && ./test/build/test_drag`
Expected: FAIL — `drag_state` is `DRAG_ST_DONE` (3), `ev_count(EV_DRAG_DONE)` is 1.

- [ ] **Step 3: Implement**

```c
-/* §11.2 false start: discard the run and return to ARMED (no event, no run_no consumed). */
-static void abort_to_armed(drag_t *D)
-{
-    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
-    if (D->run_no > 0) D->run_no--;      /* the discarded launch does not consume a run number */
-    D->state = DRAG_ST_ARMED;
-    D->v_est = D->dist_m = 0.0;
-    D->v_prev = D->dist_prev = 0.0;
-    D->t0_gps_us = 0;
-    D->launch_run = false;
-    reset_run(D);
-}
+/* §11.2 false start: discard the run and return to ARMED. R21/M4: §11.2's ARMED row specifies
+ * EV_DRAG_ARMED on entry, so the re-arm is announced -- without it the ui's engine-state card read
+ * LAUNCHED for ever after a stall and the logger never learned the engine re-armed. run_no is NOT
+ * consumed: the discarded launch's EV_DRAG_LAUNCH carries no run number (arg16 = 0), so no record
+ * ever referenced it, and the next real launch reuses the number cleanly. */
+static void abort_to_armed(drag_t *D, event_t *out, int cap, int *n, int64_t gps_us, int64_t mono_us)
+{
+    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
+    if (D->run_no > 0) D->run_no--;
+    D->t0_gps_us = 0;
+    enter_armed(D, out, cap, n, gps_us, mono_us);   /* zeroes v_est/dist/launch_run, resets the run */
+}
```

`enter_armed` is defined above `abort_to_armed` already, so no forward declaration is needed.

In `step_launched`, hoist `stopped` and make the abort unconditional on `v_peak`, checked **before** the DONE transitions:

```c
     if (D->v_est > D->v_peak) D->v_peak = D->v_est;
     gate_step(D, out, cap, n, D->prev_gps_us, D->v_prev, D->dist_prev, now, D->v_est, D->dist_m);
     brake_step(D, out, cap, n, now, fs->mono_us, d_inc);   /* may record a braking result before DONE */

-    /* §11.2 false start: v_est < 1 km/h within DRAG_FALSE_START_S of launch → discard, re-arm.
-     * Guarded on v_peak having already cleared the threshold once: a sustained low-g launch is
-     * itself briefly under 1 km/h just after t0 (v_est is still climbing from 0), and that is not
-     * a stall — only a drop-below after the run has actually moved past the threshold counts. */
-    if (now - D->t0_gps_us <= (int64_t)DRAG_FALSE_START_S * 1000000 &&
-        D->v_peak > kmh_to_mps((double)DRAG_FALSE_START_KMH) &&
-        D->v_est < kmh_to_mps((double)DRAG_FALSE_START_KMH)) {
-        abort_to_armed(D);
-        return;
-    }
+    const bool stopped = D->v_est < kmh_to_mps((double)DRAG_ARM_SPEED_KMH);
+    /* §11.2 abort (R21): a stall inside DRAG_FALSE_START_S of t0 discards the run, whatever v_peak
+     * reached, and it is tested BEFORE the DONE transitions so a stall can never be mistaken for a
+     * finished run (audit core:H5). The threshold is `stopped` (DRAG_ARM_SPEED_KMH, 0.5 km/h)
+     * rather than DRAG_FALSE_START_KMH (1 km/h): do_launch cannot enter LAUNCHED below
+     * DRAG_LAUNCH_G x DRAG_LAUNCH_HOLD_MS = 0.53 km/h, so a genuine launch -- whose v_est is still
+     * climbing out of 0 -- never trips 0.5 km/h, while a clutch-dump stall (Doppler 0) always
+     * does. The old v_peak guard existed to protect that climbing launch and is now unnecessary. */
+    if (stopped && now - D->t0_gps_us <= (int64_t)DRAG_FALSE_START_S * 1000000) {
+        abort_to_armed(D, out, cap, n, now, fs->mono_us);
+        return;
+    }

     /* DONE (§11.2): the 1/4 gate is hit; a full stop; or v below half-peak with no gate for the
      * timeout. */
     bool quarter = (D->quarter_idx != DRAG_NO_GATE && D->cur.gates[D->quarter_idx].hit);
-    bool stopped = D->v_est < kmh_to_mps((double)DRAG_ARM_SPEED_KMH);
     bool faded   = ...
```

- [ ] **Step 4: Run to verify green**

Run: `cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`
Expected: all green, including the two pre-existing guards this must not break — `test_false_start_abort` (0.3 g then Doppler 0: still aborts, now with an extra `EV_DRAG_ARMED` it does not assert on) and `test_low_g_launch_no_false_start` (0.2 g sustained: `v_est` is 0.196 m/s at LAUNCHED entry and 0.216 m/s at the first `step_launched`, both above `stopped`'s 0.139 m/s, so it still launches exactly once). The `drag_0_180`/`drag_0_320` fixtures are unaffected (0.5 g runs, never stopped inside 2 s of t0).

- [ ] **Step 5: Commit**

```bash
git add components/core/dragengine/drag.c test/test_drag.c
git commit   # fix(core): a marginal-g launch that stalls aborts to ARMED and announces it, instead of emitting a gate-less DONE (F1a T8, R21, core:H5/M4)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 8 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 8 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 9**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

- [ ] **SESSION 2 CHECKPOINT (Tasks 5-8)** — before the context is cleared, re-verify the session's whole output, then record it:

```bash
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %, no fixture diff
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                                    # 0
git status --short                                         # clean
printf 'Session 2 (Tasks 5-8) verified: ctest 100%%, both linters 0, tree clean\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push origin fix-core-engines
```

If `ctest` or either linter fails here, the session is **not** checkpointed: fix forward on this branch (or `git revert` the offending task's commit) and re-run before writing the line.

---

### Task 9 — SERIAL after Task 8 (drag.c): non-monotonic time, range/quarter/faded LOWs, the mph constant's name

**Files:**
- Modify: `components/core/dragengine/drag.c:17-21,54-69,256-352,427-490,504-604`, `components/core/include/core/drag.h:61,89,97`, `components/core/dragengine/drag_cfg.c:11-12`
- Test: `test/test_drag.c`, `test/test_drag_cfg.c`

**Interfaces (produced):** `DRAG_KMH_PER_MPH` replaces `DRAG_MPH_PER_KMH` (same value `1.609344`; the old name inverted the ratio its own comment stated). `gate_step`'s signature is unchanged.

- [ ] **Step 1: Write the failing tests** — append to `test/test_drag.c`, register at the end of `main`:

```c
/* M2: a fused sample whose gps_us steps BACKWARD. fused.gps_us = tb_mono_to_gps(mono), and tb's
 * half-window rollover or a PPS acquire/drop can move the filtered offset by up to
 * TB_PPS_DISAGREE_US = 50 ms (§6.2), so the stream is not strictly monotonic. gate_step's
 * CORE_ASSERT_VOID(tc >= tp) RETURNED from the middle of the gate loop, so if the backward step
 * landed on the crossing sample the gate was lost for ever (the next pair has vp >= V already) --
 * and it write-stormed the persisted error ring. Here the step is engineered onto the 100 km/h
 * crossing: gate 2 must still be recorded. */
static void test_backward_gps_us_step_does_not_lose_a_gate(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    const double a = 0.5 * G_MPS2;
    bool kicked = false;
    int64_t t = T_LAUNCH_US;
    for (int k = LAUNCH_K; k <= LAUNCH_K + 900; k++) {
        t = (int64_t)k * DT_US;
        if (!kicked && D.v_est > 27.0) { t -= 50000; kicked = true; }   /* 100 km/h = 27.78 m/s */
        fused_sample_t fs = fused(t, 0.5f, 0);
        drive_fused(&fs, true);
        if (k % 20 == 0) {
            double tl = (double)((int64_t)k * DT_US - T_LAUNCH_US) / 1e6;
            if (tl < 0.0) tl = 0.0;
            gps_fix_t f = gfix((int64_t)k * DT_US, (int32_t)(a * tl * 1000.0), true);
            drag_on_fix(&D, &f);
        }
        if (drag_state(&D) == DRAG_ST_DONE) break;
    }
    TEST_ASSERT_TRUE(kicked);
    TEST_ASSERT_TRUE(gate_by_id(drag_current(&D), 2)->hit);     /* 0-100 survived the step */
    TEST_ASSERT_TRUE(gate_by_id(drag_current(&D), 1)->hit);     /* 0-60 too */
}

/* LOW (§11.1): the trap gate is "the DIST gate with the largest distance a". find_quarter used
 * `>= best_a`, so on a tie the LAST listed DIST gate silently took the trap. */
static void test_quarter_gate_tie_keeps_the_first_listed(void)
{
    drag_cfg_t c; drag_cfg_defaults(&c);
    c.n_gates = 3;
    c.gates[0] = (drag_gate_def_t){ 1, DRAG_SPEED_FROM0, 100, 0 };
    c.gates[1] = (drag_gate_def_t){ 9, DRAG_DIST, 40234, 0 };
    c.gates[2] = (drag_gate_def_t){ 10, DRAG_DIST, 40234, 0 };    /* same a: a tie */
    drag_init(&D, &c);
    TEST_ASSERT_EQUAL_UINT8(1, D.quarter_idx);                    /* the first of the two */
}

/* LOW (§11.1 gate 5): one sample pair must not both ARM a and RECORD b. A large Doppler re-anchor
 * after a gap crosses 100 and 200 km/h in one step; the recorded interval would be a fraction of
 * one sample, i.e. a fictitious ~0 s 100-200. */
static void test_range_gate_not_armed_and_recorded_in_one_step(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    for (int k = LAUNCH_K; k <= LAUNCH_K + 30; k++) {             /* launch at 1 g */
        fused_sample_t fs = fused((int64_t)k * DT_US, 1.0f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    gps_fix_t jump = gfix((int64_t)(LAUNCH_K + 30) * DT_US, 70000, true);   /* 252 km/h re-anchor */
    drag_on_fix(&D, &jump);
    fused_sample_t nxt = fused((int64_t)(LAUNCH_K + 31) * DT_US, 1.0f, 0);
    drive_fused(&nxt, true);
    TEST_ASSERT_FALSE(gate_by_id(drag_current(&D), 5)->hit);      /* no 0 s 100-200 */
}

/* LOW (§11.2 DONE): "v below half peak with no gate for DRAG_TIMEOUT_S". With no gate yet
 * recorded, last_gate_gps_us is 0 and `faded` could never be true, so a run that never reaches its
 * first gate stayed LAUNCHED for ever (reachable with a custom gate list). */
static void test_faded_done_when_no_gate_was_ever_hit(void)
{
    drag_cfg_t c; drag_cfg_defaults(&c);
    c.n_gates = 1;
    c.gates[0] = (drag_gate_def_t){ 4, DRAG_SPEED_FROM0, 300, 0 };   /* unreachable in this run */
    drag_init(&D, &c);
    arm_engine(true);
    int64_t t = T_LAUNCH_US;
    for (int k = LAUNCH_K; k <= LAUNCH_K + 120; k++) {                /* ~40 km/h at 0.3 g */
        t = (int64_t)k * DT_US;
        fused_sample_t fs = fused(t, 0.3f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    for (int k = LAUNCH_K + 121; k <= LAUNCH_K + 7500; k++) {         /* coast 15 km/h for ~74 s */
        t = (int64_t)k * DT_US;
        fused_sample_t fs = fused(t, 0.0f, 0);
        drive_fused(&fs, true);
        if (k % 20 == 0) { gps_fix_t f = gfix(t, 4166, true); drag_on_fix(&D, &f); }
        if (drag_state(&D) != DRAG_ST_LAUNCHED) break;
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_DONE, drag_state(&D));
}
```

`test/test_drag_cfg.c:60` — rename the constant in the one place it is used:

```c
-        drag_gate_def_t g = { 1, DRAG_SPEED_FROM0, (uint16_t)lround((double)m * DRAG_MPH_PER_KMH), 0 };
+        drag_gate_def_t g = { 1, DRAG_SPEED_FROM0, (uint16_t)lround((double)m * DRAG_KMH_PER_MPH), 0 };
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build test/build -j8` → `test_drag_cfg` fails to compile (`DRAG_KMH_PER_MPH` undeclared); after the rename, `./test/build/test_drag` FAILS all four new cases.

- [ ] **Step 3: Implement — `drag.c`**

```c
 static void gate_step(drag_t *D, event_t *out, int cap, int *n,
                       int64_t tp, double vp, double dp, int64_t tc, double vc, double dc)
 {
     CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
     CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
-    CORE_ASSERT_VOID(tc >= tp, DRAG_ASSERT_CODE);                           /* sample time is monotonic */
-    CORE_ASSERT_VOID(vp >= 0.0 && vc >= 0.0, DRAG_ASSERT_CODE);             /* speeds non-negative */
-    CORE_ASSERT_VOID(dp >= 0.0 && dc >= 0.0, DRAG_ASSERT_CODE);             /* distances non-negative */
+    CORE_ASSERT_VOID(D->quarter_idx == DRAG_NO_GATE || D->quarter_idx < DRAG_MAX_GATES,
+                     DRAG_ASSERT_CODE);        /* the trap block below indexes cfg.gates[quarter_idx] */
+    /* M2: fused.gps_us is tb_mono_to_gps(mono), and tb's half-window rollover or a PPS
+     * acquire/drop can step the filtered offset backward by up to TB_PPS_DISAGREE_US (§6.2). Skip
+     * this sample's interpolation and trap accumulation rather than assert: the old assertion did
+     * the same skip but also write-stormed the persisted error ring, and drag_on_fused's own
+     * `dt <= 0` guard already keeps v_est/dist sane. */
+    if (tc < tp) return;
```

```c
 static void find_quarter(drag_t *D)
 ...
-        if (D->cfg.gates[i].kind == DRAG_DIST && D->cfg.gates[i].a >= best_a) {
+        /* §11.1 names ONE trap gate ("the DIST gate with the largest a"); `>=` let the LAST of two
+         * equal gates take it. On a tie the first listed wins. A DIST gate with a == 0 is not a
+         * gate at all and is never picked. */
+        if (D->cfg.gates[i].kind == DRAG_DIST && D->cfg.gates[i].a > best_a) {
```

```c
 static void gate_step_range(...)
 ...
+    bool armed_now = false;
     if (!D->range_started[i]) {
         if (vp < Va && vc >= Va && vc > vp) {
             ...
             D->range_started[i]  = true;
+            armed_now = true;
         }
     }
-    if (D->range_started[i] && !D->cur.gates[i].hit) {
+    /* A single sample pair must not both arm `a` and record `b`: a large Doppler re-anchor after a
+     * gap can cross both thresholds in one step, and the interval would be a fraction of one
+     * sample. The b-crossing is looked for from the NEXT pair onwards. */
+    if (D->range_started[i] && !armed_now && !D->cur.gates[i].hit) {
```

```c
     bool quarter = (D->quarter_idx != DRAG_NO_GATE && D->cur.gates[D->quarter_idx].hit);
-    bool faded   = D->v_peak > 0.0 && D->v_est < 0.5 * D->v_peak &&
-                   D->last_gate_gps_us != 0 &&
-                   now - D->last_gate_gps_us >= (int64_t)DRAG_TIMEOUT_S * 1000000;
+    /* §11.2's DONE row times the fade from the last gate; with no gate yet recorded the reference
+     * is t0, not 0 -- `last_gate_gps_us != 0` alone left a run that never reaches its first gate
+     * stuck in LAUNCHED for ever (reachable with a custom gate list). */
+    const int64_t since = (D->last_gate_gps_us != 0) ? D->last_gate_gps_us : D->t0_gps_us;
+    bool faded   = D->v_peak > 0.0 && D->v_est < 0.5 * D->v_peak && since != 0 &&
+                   now - since >= (int64_t)DRAG_TIMEOUT_S * 1000000;
```

Rule-5 padding in `drag.c` (R20, drag's share) — delete these assertions, which cannot fail given their only call sites:

| Site | Assertion | After |
|---|---|---|
| `drag.c:19` `kmh_to_mps` | `kmh >= 0.0` | deleted; the function is 2 code lines (no rule-5 minimum) |
| `drag.c:349` `finalize_trap` | `trap_sum >= 0.0` | deleted; `D != NULL` remains, 4 code lines |
| `drag.c:300-301` `gate_step` | `vp/vc >= 0`, `dp/dc >= 0` | replaced above by the `quarter_idx` bound; 3 meaningful assertions remain |
| `drag.c:511-512` `step_idle` | `state == DRAG_ST_IDLE`, `v_est >= 0.0` | deleted; 14 code lines, `D`/`fs` remain |
| `drag.c:530` `step_armed` | `state == DRAG_ST_ARMED` | deleted; 12 code lines |
| `drag.c:547` `step_launched` | `state == DRAG_ST_LAUNCHED` | deleted; `D`, `fs`, `d_inc >= 0`, `quarter_idx` bound remain (4 ≥ 2) |
| `drag.c:599` `step_done` | `state == DRAG_ST_DONE` | deleted; 8 code lines |

`drag_on_fused`'s `switch` is the sole dispatcher to all four `step_*`, so the state is structurally guaranteed; `isfinite(fs->g_lon)` and `D->state <= DRAG_ST_DONE` there stay (both are real input validation).

- [ ] **Step 4: Implement — the rename**

`drag.h:61`:

```c
-#define DRAG_MPH_PER_KMH 1.609344   /* km/h per mph; mph bench values convert with lround(mph * DRAG_MPH_PER_KMH) */
+#define DRAG_KMH_PER_MPH 1.609344   /* km/h per mph; mph bench values convert with lround(mph * DRAG_KMH_PER_MPH) */
```

and the two doc references at `drag.h:89` and `drag.h:97`, and `drag_cfg.c:11-12`:

```c
-static uint16_t mph_to_kmh(uint16_t mph) { return (uint16_t)lround((double)mph * DRAG_MPH_PER_KMH); }
-static unsigned kmh_to_mph(uint16_t kmh) { return (unsigned)lround((double)kmh / DRAG_MPH_PER_KMH); }
+static uint16_t mph_to_kmh(uint16_t mph) { return (uint16_t)lround((double)mph * DRAG_KMH_PER_MPH); }
+static unsigned kmh_to_mph(uint16_t kmh) { return (unsigned)lround((double)kmh / DRAG_KMH_PER_MPH); }
```

Verify no site is missed: `grep -rn DRAG_MPH_PER_KMH components tools test main` must print nothing (the two hits in `docs/superpowers/plans/2026-10-03-debt-sweep-b.md` are a historical plan and stay).

- [ ] **Step 5: Run to verify green; Step 6: Commit**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
git add components/core/dragengine/drag.c components/core/dragengine/drag_cfg.c \
        components/core/include/core/drag.h test/test_drag.c test/test_drag_cfg.c
git commit   # fix(core): tolerate a backward fused gps_us; trap-gate ties, one-step range gates, gate-less fade; DRAG_KMH_PER_MPH (F1a T9, core:M2 + LOWs)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 9 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 9 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 10**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 10 — PARALLEL: fusion — the un-oriented clamp, the stale-bias latch, dead state (core:M1/M7, false claims 6/7)

**Files:**
- Modify: `components/core/fusion/fus.c:140-161,210-277,355-366`, `components/core/include/core/fus.h:104,114`
- Test: `test/test_fus.c`

**Interfaces:** `fus_t.samples` and `fus_t.v_course_deg` are **deleted** (write-only; `grep -rn "\.samples\|->samples\|v_course_deg" components test tools` must show no reader outside the deleted lines). No public function changes.

- [ ] **Step 1: Write the failing tests** — append to `test/test_fus.c`, register in `main`:

```c
/* M1 (§9.3 step 5, "Both g_lon and g_lat are clamped to ±G_MAX, and clamping sets FUS_CLAMPED"):
 * the clamp lived inside the `if (oriented)` branch only, so before an upright capture g_lon was
 * the raw in-plane magnitude. The reviewer's repro_misc.c reported 22.627 g with flags 0x00, and
 * drag_on_fused integrates g_lon x G_MPS2 straight into v_est. */
static void test_unoriented_g_lon_is_clamped_to_gmax(void)
{
    fus_t f; fus_init(&f, NULL, 1);                      /* identity R, orient_ok = 0 */
    imu_raw_t r; memset(&r, 0, sizeof r);
    r.mono_us = 0; r.ax = 32767; r.ay = 32767; r.az = 0; /* ~16 g on each in-plane axis */
    fused_sample_t o;
    TEST_ASSERT_EQUAL_INT(1, fus_step(&f, &r, &o));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, G_MAX, o.g_lon);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, o.g_lat);
    TEST_ASSERT_TRUE(o.flags & FUS_CLAMPED);
    TEST_ASSERT_FALSE(o.flags & FUS_ORIENT_OK);
}

/* M7 (§9.2, "the sample flag FUS_BIAS_STALE is set until the next still period"): update_bias_stale
 * recomputed the flag on every fus_set_temp, so a temperature excursion that came back cleared it
 * without any recapture. */
static void test_bias_stale_latches_until_a_recapture(void)
{
    lcg_reset();
    fus_t f; fus_init(&f, NULL, 1);
    fus_set_temp(&f, 2500);
    fused_sample_t o; int64_t t = 1000000;
    const float quiet_a[3] = { 0.0f, 0.0f, 1.0f };
    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(fus_is_still(&f));
    fus_gyro_bias_update(&f);                            /* bias captured at 25.00 C */
    feed(&f, &o, 1, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(o.flags & FUS_BIAS_STALE);

    fus_set_temp(&f, 4500);                              /* +20 C > BIAS_TEMP_STALE_C */
    feed(&f, &o, 1, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(o.flags & FUS_BIAS_STALE);

    fus_set_temp(&f, 2500);                              /* back again: still stale, no recapture */
    feed(&f, &o, 1, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_TRUE(o.flags & FUS_BIAS_STALE);          /* FIX: latched */

    feed(&f, &o, FUS_STILL_WINDOW_N, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    fus_gyro_bias_update(&f);                            /* the next still period clears it */
    feed(&f, &o, 1, quiet_a, ZERO3, QUIET_ACC_NOISE_G, QUIET_GYR_NOISE_LSB, &t);
    TEST_ASSERT_FALSE(o.flags & FUS_BIAS_STALE);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build test/build -j8 && ./test/build/test_fus`
Expected: FAIL — `o.g_lon` is 22.627 with `flags == 0`; `FUS_BIAS_STALE` clears on the return to 25 °C.

- [ ] **Step 3: Implement — M1 and the two false claims**

```c
     } else {
-        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);   /* sign-less horizontal magnitude until forward is learned (§9.2) */
-        out->g_lat = 0.0f;                       /* no lateral, no lean, FUS_LEAN_VALID clear until oriented */
+        /* No forward axis yet (§9.2): g_lon is the sign-less magnitude of the two IN-PLANE axes of
+         * the CURRENT calibration frame. Before an upright capture R is the identity, so that is
+         * the body x/y magnitude -- on a non-flat mount it therefore includes a component of
+         * gravity and reads ~1 g while stationary. It is NOT the horizontal magnitude. */
+        out->g_lon = sqrtf(a[0] * a[0] + a[1] * a[1]);
+        out->g_lat = 0.0f;                       /* no lateral, no lean, FUS_LEAN_VALID clear until oriented */
+        /* M1 (§9.3 step 5): the G_MAX clamp applies to this branch too -- the magnitude is
+         * non-negative, so only the upper bound can bite. Without it drag_on_fused integrated an
+         * unbounded g_lon into v_est. */
+        if (out->g_lon > G_MAX) { out->g_lon = G_MAX; *flags |= FUS_CLAMPED; }
     }
```

and the two claims about "before `g_comb` is formed" (`fus.c:210-213` header comment and `fus.c:266-268`) — see Task 13 claim 6 for the exact replacement text.

- [ ] **Step 4: Implement — M7**

```c
 static void update_bias_stale(fus_t *f)
 {
     CORE_ASSERT_VOID(f != NULL, FUS_ASSERT_CODE);
-    if (!f->calib.bias_ok || !f->temp_known) { f->bias_stale = false; return; }
+    /* §9.2: FUS_BIAS_STALE is set "until the next still period", i.e. it LATCHES -- only a
+     * recapture (fus_gyro_bias_update) clears it. Recomputing it on every fus_set_temp let a
+     * temperature excursion that came back clear it with no recapture (audit core:M7). */
+    if (!f->calib.bias_ok || !f->temp_known) return;   /* nothing to compare: the latch is unchanged */
     int32_t d = (int32_t)f->temp_c100 - (int32_t)f->calib.gbias_temp_c100;
     if (d < 0) d = -d;
-    f->bias_stale = d > (int32_t)BIAS_TEMP_STALE_C * 100;
+    if (d > (int32_t)BIAS_TEMP_STALE_C * 100) f->bias_stale = true;
 }
```

`fus_gyro_bias_update` already ends with `f->bias_stale = false;` — the only clear. `fus.h:102`'s comment becomes:

```c
-    bool        bias_stale;          /* |temp − gbias_temp| > BIAS_TEMP_STALE_C since the last bias update */
+    bool        bias_stale;          /* §9.2 latch: set once |temp − gbias_temp| exceeds
+                                      * BIAS_TEMP_STALE_C, cleared only by fus_gyro_bias_update */
```

- [ ] **Step 5: Implement — remove the dead fields**

`fus.h:104` delete `float v_course_deg;` and `fus.h:114` delete `uint32_t samples;`. In `fus.c:151` drop `f->v_course_deg = course_deg;` from the assignment chain, and delete `f->samples++;` at `fus.c:364`. 8 B off `fus_t`.

- [ ] **Step 6: Run to verify green; Step 7: Commit**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
git add components/core/fusion/fus.c components/core/include/core/fus.h test/test_fus.c
git commit   # fix(core): clamp the un-oriented g_lon to G_MAX; latch FUS_BIAS_STALE until a recapture; drop two write-only fields (F1a T10, core:M1/M7)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 10 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 10 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 11**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 11 — PARALLEL: geo NaN/antimeridian guards and the `tb` padding assertions (core LOW, R20)

**Files:**
- Modify: `components/core/geo/geo.c:22-44`, `components/core/timebase/tb.c:47,63`
- Test: `test/test_geo.c`

**Interfaces:** no signature changes. `geo_dist_m` becomes total (never NaN); `geo_to_enu` wraps the longitude difference into (−180°, 180°].

- [ ] **Step 1: Write the failing tests** — append to `test/test_geo.c`, register in `main`:

```c
/* LOW: the haversine `a` reaches 1.0000000000000002 for antipodal inputs, so sqrt(1.0 - a) is
 * sqrt(-2e-16) = NaN and geo_dist_m returned NaN. Benign today only because every caller's
 * comparison happens to be false on NaN -- in a file that isfinite-checks all four inputs. */
static void test_dist_antipodal_is_finite(void)
{
    const double d = geo_dist_m(0.0, 0.0, 0.0, 180.0);
    TEST_ASSERT_TRUE(isfinite(d));
    TEST_ASSERT_DOUBLE_WITHIN(1.0, GEO_PI * EARTH_R_M, d);
    const double d2 = geo_dist_m(45.0, 10.0, -45.0, -170.0);
    TEST_ASSERT_TRUE(isfinite(d2));
}

/* LOW: no ±180° wrap in geo_to_enu, so a venue at +179.99 and a fix at −179.99 projected ~40 000 km
 * apart -- every gate test and the leave-venue timer would be nonsense at the antimeridian. */
static void test_enu_wraps_the_antimeridian(void)
{
    geo_origin_t o;
    geo_origin_set(&o, 0.0, 179.99);
    const geo_enu_t e = geo_to_enu(&o, 0.0, -179.99);
    TEST_ASSERT_DOUBLE_WITHIN(5.0, 0.02 * (GEO_PI / 180.0) * EARTH_R_M, e.x);   /* ~2.2 km east */
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 0.0, e.y);
    geo_origin_set(&o, 0.0, -179.99);
    const geo_enu_t w = geo_to_enu(&o, 0.0, 179.99);
    TEST_ASSERT_DOUBLE_WITHIN(5.0, -0.02 * (GEO_PI / 180.0) * EARTH_R_M, w.x);  /* ~2.2 km west */
}
```

(`test_geo.c` already includes `<math.h>` for `isfinite`; add it if not.)

- [ ] **Step 2: Run to verify they fail**

Run: `./test/build/test_geo` → FAIL: `d` is NaN; `e.x` is ≈ −40 000 km.

- [ ] **Step 3: Implement — `geo.c`**

```c
 geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg)
 ...
     geo_enu_t e;
-    e.x = (lon_deg * DEG2RAD - o->lon0_rad) * o->cos_lat0 * GEO_EARTH_R_M;
+    /* Wrap the longitude difference into (-pi, pi]: without it a venue at +179.99 and a fix at
+     * -179.99 (2.2 km apart) project ~40 000 km apart and every crossing test is nonsense. */
+    double dlon = lon_deg * DEG2RAD - o->lon0_rad;
+    if (dlon > GEO_PI)       dlon -= 2.0 * GEO_PI;
+    else if (dlon < -GEO_PI) dlon += 2.0 * GEO_PI;
+    e.x = dlon * o->cos_lat0 * GEO_EARTH_R_M;
     e.y = (lat_deg * DEG2RAD - o->lat0_rad) * GEO_EARTH_R_M;
     return e;
 }
```

```c
     double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
+    /* `a` is a sin²-sum in [0,1] mathematically, but rounding pushes it to 1 + 2e-16 for antipodal
+     * inputs, and sqrt(1.0 - a) is then sqrt(-2e-16) = NaN. Clamp so the function is total. */
+    if (a < 0.0) a = 0.0;
+    else if (a > 1.0) a = 1.0;
     return 2.0 * GEO_EARTH_R_M * atan2(sqrt(a), sqrt(1.0 - a));
```

- [ ] **Step 4: Implement — the `tb.c` padding assertions (R20, tb's share)**

`t->cur` and the local `c` are only ever assigned `0` or `1 - c`, so both checks are tautologies:

```c
 static void recompute(tb_t *t)
 {
     CORE_ASSERT_VOID(t != NULL, TB_ASSERT_CODE);
-    CORE_ASSERT_VOID(t->cur == 0 || t->cur == 1, TB_ASSERT_CODE);   /* half index is one of two */
     int64_t m = 0; bool any = false;
```

```c
     int c = t->cur;
-    CORE_ASSERT_VOID(c == 0 || c == 1, TB_ASSERT_CODE);       /* half index is one of two */
     if (!t->half_valid[c]) {
```

`recompute` is 10 code lines (no rule-5 minimum); `tb_on_fix` keeps three meaningful assertions (`t != NULL`, `arrival_mono_us >= 0`, `serial_time_us >= 0`) — all real input validation on values the pipeline supplies.

- [ ] **Step 5: Run to verify green; Step 6: Commit**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
git add components/core/geo/geo.c components/core/timebase/tb.c test/test_geo.c
git commit   # fix(core): geo_dist_m is total at antipodes, geo_to_enu wraps the antimeridian; drop tb's tautological half-index asserts (F1a T11)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 11 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 11 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 12**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

- [ ] **SESSION 3 CHECKPOINT (Tasks 9-11)** — before the context is cleared, re-verify the session's whole output, then record it:

```bash
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %, no fixture diff
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                                    # 0
git status --short                                         # clean
printf 'Session 3 (Tasks 9-11) verified: ctest 100%%, both linters 0, tree clean\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push origin fix-core-engines
```

If `ctest` or either linter fails here, the session is **not** checkpointed: fix forward on this branch (or `git revert` the offending task's commit) and re-run before writing the line.

---

### Task 12 — SERIAL after Tasks 7 and 10: the remaining padding assertions and rule-9 dereferences (R20, core LOW)

**Files:**
- Modify: `components/core/lapengine/lap.c`, `components/core/fusion/fus.c`, `components/core/fusion/fus_orient.c`
- Test: none new — this task must not change behaviour; the whole suite is the guard.

**Interfaces:** none. **Boundary note:** the reviewer's padding list also names `trk.c:34,39,49`, `trk_json.c:53-56,62-65,134-135,140-141` and `json.c:56-57`. `core/tracks/*` and `core/util/json.c` are **not** F1a files (F0 owns the `trk.*` comment, F1b owns `json.c`); Task 14 files them instead of reaching across the boundary.

**Rule 5 contract:** every function still over 20 code lines keeps **≥ 2 meaningful** assertions. Where deleting the padding would leave fewer, the table below names the replacement — a real pre- or postcondition, never a restatement.

- [ ] **Step 1: Remove the `lap.c` padding, with replacements**

| Site (gdm) | Delete | Keep / add |
|---|---|---|
| `update_running_progress:1019-1020` | `state == LAP_ST_RUNNING` (the caller just tested it at `lap.c:1091`), `lap_dist_m >= 0.0` (a precondition that is also the postcondition) | keep `L`/`fix` NULL; **add** after the integration: `CORE_ASSERT_VOID(isfinite(L->lap_dist_m) && L->lap_dist_m >= 0.0, LAP_ASSERT_CODE); /* postcondition: a NaN speed would poison §10.5 length scoring and §10.11 for the rest of the lap */` |
| `finalize_create:848` | `lap_dist_m >= 0.0` | keep `L` NULL; **add** `CORE_ASSERT_VOID(L->create_layout.n_sectors <= LAP_MAX_SECTORS, LAP_ASSERT_CODE); /* copied into nv.layouts[0].sectors[] below */` |
| `handle_create_mode:880` | `mode == LAP_MODE_CREATE` (the caller branches on it at `lap.c:1056`) | keep `L`/`fix` NULL; **add** `CORE_ASSERT_VOID(isfinite(lat) && isfinite(lon), LAP_ASSERT_CODE); /* fed to geo_to_enu, which would poison the ENU frame */` |
| `scan_for_venue:912` | `state == LAP_ST_NO_VENUE` (the caller branches on it at `lap.c:1062`) | keep `L`/`fix` NULL; 15 code lines, no minimum |

- [ ] **Step 2: Remove the fusion NULL padding on `&`-of-local-only static helpers**

These file-static helpers are only ever called with the address of a caller local, so the pointer cannot be NULL:

- `fus.c:18-19` (`dot3`), `fus.c:95-97` (`fus_rotate` — called only from `fus_step_rotate_inputs` with locals; its own 3-iteration loop is already bounded), `fus.c:180-184` (`fus_step_rotate_inputs`, 5 NULL checks, 9 code lines).
- `fus.c:29` keep `v != NULL` **deleted** but **keep** `n >= 0 && n <= FUS_ALL_FINITE_MAX_N` — that one is the loop bound (rule 2), not padding.
- `fus_orient.c:21-22` (`v_cross`), `:27` (`v_normalize` — keep `n > 0.0f`, which is a real guard against a zero-length input), `:34-36`, `:45-46` (`complete_triad`), `:59-61` (`set_rows`). Each of these functions is under 20 code lines after the deletions.
- `fus_step_planar` is over 20 code lines: delete the five NULL checks (`fus.c:217-222`), keep `isfinite(psidot)`, and **add** as the last statement `CORE_ASSERT_VOID(isfinite(out->g_lon) && isfinite(out->g_lat), FUS_ASSERT_CODE); /* postcondition: both components are integrated by the drag engine and logged */`.

- [ ] **Step 3: Fix the eleven rule-9 two-level dereferences in `lap.c`**

The project declares strict N = 1 (`docs/power-of-10-deviations.md`, design §3 rule 9) and the linter cannot see these. Introduce a named local at each site; no behaviour changes.

| Site (gdm `lap.c`) | Was | Now |
|---|---|---|
| 102–103 `arm_candidates` | `L->venue->n_layouts`, `&L->venue->layouts[i]` | `const trk_venue_t *v = L->venue;` then `v->n_layouts`, `&v->layouts[i]` |
| 273–274 `lap_export_rtc` | `L->venue->id`, `L->locked_layout->id` | `const trk_venue_t *v = L->venue; const trk_layout_t *ly = L->locked_layout;` then `v->id`, `ly->id` |
| 306 `lap_import_rtc` | `L->cand[i].layout->id` | `const trk_layout_t *cl = L->cand[i].layout;` then `cl->id` |
| 469 `lock_layout` | `L->locked_layout->id` | `const trk_layout_t *ly = L->locked_layout;` then `ly->id` |
| 598 `evaluate_locked_sectors` | `L->locked_layout->dir_sign` | already a local `want` (Task 3) |
| 672 `disambiguate_and_lock` | `L->cand[i].layout->length_m` | `const trk_layout_t *cl = L->cand[i].layout;` then `cl->length_m` |
| 741–742 `update_leave_venue` | `L->venue->lat/lon`, `L->venue->radius_m` | `const trk_venue_t *v = L->venue;` then `v->lat`, `v->lon`, `v->radius_m` |
| 1000 `process_segment` | `c->layout->dir_sign` | already a local `want` (Task 3) |

- [ ] **Step 4: Add the missing static assertion**

`lap_ugate_t.in_layout` is a `uint8_t` bitmask built with `1u << i` over the candidate array, so it exactly fits `TRK_MAX_LAYOUTS == 8` with nothing stopping a future bump. At the top of `lap.c`, below `LAP_ASSERT_CODE`:

```c
/* lap_ugate_t.in_layout is a uint8_t bitmask over cand[] (`1u << i`), so raising TRK_MAX_LAYOUTS
 * past 8 would silently shift bits off the end of the mask (audit core LOW). */
_Static_assert(TRK_MAX_LAYOUTS <= 8, "lap_ugate_t.in_layout is a uint8_t bitmask over cand[]");
```

- [ ] **Step 5: Run the full suite and the linter**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
```
Expected: 100 % pass, lint 0, fixtures unchanged. If `ctest` moves at all, a deletion changed behaviour — revert that one site.

- [ ] **Step 6: Commit**

```bash
git add components/core/lapengine/lap.c components/core/fusion/fus.c components/core/fusion/fus_orient.c
git commit   # refactor(core): replace padding assertions with real pre/postconditions; one dereference level per expression in lap.c (F1a T12, R20)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 12 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 13 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 12 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 13 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 13**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 13 — SERIAL: every false claim in the comments (core reviewer "False claims in code/comments")

**Files:** `components/core/lapengine/lap.c`, `components/core/include/core/lap.h`, `components/core/fusion/fus.c`. No behaviour change; no new test.

**Interfaces:** none. Claims 1, 2, 6 and 7 were applied by Tasks 3, 6 and 10 — **verify** them here with the greps given; claims 3, 4, 5 and 9 are applied here. Claim 8 is outside F1a's files and is filed in Task 14.

- [ ] **Step 1: Verify the claims already applied**

```bash
grep -n "a sector gate precedes the S/F" components/core/lapengine/lap.c   # claim 1: no output
grep -n "recording continues at half density" components/core/lapengine/lap.c   # claim 2: no output
grep -n "sign-less horizontal magnitude" components/core/fusion/fus.c      # claim 7: no output
grep -n "before\* g_comb is formed\|\*before\* g_comb" components/core/fusion/fus.c
```

Claim 6's two comments must now read (apply if the greps still show the old text) — `fus.c:210-213`:

```c
-/* §9.3 steps 2/4/5 + the Appendix A clamp: the in-plane g components (and, on a moto, the
- * complementary lean filter and lateral g), then each in-plane axis bounded to +-G_MAX *before*
- * g_comb is formed so the reported combined magnitude stays consistent with the reported
- * components. *flags accumulates FUS_CLAMPED / FUS_LEAN_VALID. */
+/* §9.3 steps 2/4/5 + the Appendix A clamp: the in-plane g components (and, on a moto, the
+ * complementary lean filter and lateral g), each bounded to +-G_MAX before g_comb is formed by the
+ * caller, so the reported combined magnitude stays consistent with the reported components. The
+ * clamp applies in BOTH branches -- oriented (g_lon and g_lat) and un-oriented (the single
+ * magnitude in g_lon); it used to apply only in the oriented one (audit core:M1). *flags
+ * accumulates FUS_CLAMPED / FUS_LEAN_VALID. */
```

and `fus.c:266-268`:

```c
-        /* G_MAX clamp (Appendix A, "clamp in §9.3"): bound each in-plane axis to +-G_MAX *before*
-         * forming g_comb, so the reported combined magnitude stays consistent with the reported
-         * (clamped) components. */
+        /* G_MAX clamp (Appendix A, "clamp in §9.3"), oriented branch: bound each in-plane axis to
+         * +-G_MAX before g_comb is formed, so the reported combined magnitude stays consistent
+         * with the reported (clamped) components. The un-oriented branch clamps too, below. */
```

- [ ] **Step 2: Claim 3 — `lap_import_rtc`'s contract (`lap.h:202-205`)**

```c
-/* §10.10 RTC continuity. Export mirrors the resumable state into a POD; import restores it into
- * LAP_RUNNING with LAP_F_INTERRUPTED and returns 0, or -1 if the venue id is unknown. */
+/* §10.10 RTC continuity. Export mirrors the resumable state into a POD. Import validates the
+ * snapshot and returns 0, -1 (the venue id is unknown to the track store), -2 (the venue has no
+ * such layout_id -- restoring it would arm zero candidates and kill lap timing for the session) or
+ * -3 (sector_idx > LAP_MAX_SECTORS, which would put sec_next outside gate_times[]). On success a
+ * snapshot with a locked layout_id, or an out-lap (lap_no == 0), restores into LAP_RUNNING with
+ * LAP_F_INTERRUPTED; a snapshot with lap_no > 0 and NO locked layout restores into LAP_ST_ARMED
+ * with lap_no 0 instead -- sector timing and §10.8 cannot be resumed without a layout, and
+ * disambiguation is only reachable at lap_no == 0 (audit fix wave R25). */
```

- [ ] **Step 3: Claim 4 — the `(void)fs` comment (`lap.c:1044`)**

```c
-    (void)fs;                                    /* fused stats feed sector stats in a later session */
+    (void)fs;                                    /* The lap engine does not read the fused sample.
+                                                  * §9.1 step 5's per-sector fused statistics are the
+                                                  * PIPELINE's: pipeline.c's stats_step() folds them
+                                                  * and the logger writes them. The parameter stays
+                                                  * in the signature so the caller's contract (one
+                                                  * fix + its latest fused sample) is explicit. */
```

- [ ] **Step 4: Claim 5 — `LAP_EVT_MAX`'s accounting (`lap.h:33-35`)**

```c
-/* Upper bound on the events lap_on_fix can append in a single call: at most one ARMED/VENUE_FOUND,
- * one LAYOUT_LOCKED, LAP_MAX_SECTORS SECTORs, and one LAP_COMPLETE. Callers pass a buffer this large. */
+/* Upper bound on the events lap_on_fix can append in a single call. Two cases bound it, and they
+ * are mutually exclusive:
+ *   - the fix that finds a venue emits EV_VENUE_FOUND AND EV_ARMED (both fire on one fix), but
+ *     lap_set_venue clears have_prev_fix, so no crossing can be evaluated on that same fix: 2;
+ *   - a crossing fix emits at most LAP_MAX_SECTORS EV_SECTORs (each gate fires at most once per
+ *     segment, whichever side of the S/F it falls on), one EV_LAYOUT_LOCKED and one
+ *     EV_LAP_COMPLETE: LAP_MAX_SECTORS + 2.
+ * EV_VENUE_LOST is a single event followed by an immediate return. Callers pass a buffer this
+ * large. (The old comment said "at most one ARMED/VENUE_FOUND", which is wrong -- both fire.) */
```

- [ ] **Step 5: Claim 9 — the `LAP_SEG_GAP_US` comment now covers both triggers (`lap.c:20-23`)**

```c
-/* H5 (§6.4): a crossing found on a chord longer than this spans a GPS gap (invalid or absent fixes
- * skip prev_*), so cross_time interpolates an unreliable t_cross; a lap opened at it carries
- * LAP_F_GPS_LOST. Well above the worst normal fix spacing (a lap timer runs GPS at >= 1 Hz). */
+/* §6.4 step 1: a chord longer than this spans absent fixes, so cross_time interpolates an
+ * unreliable t_cross. Well above the worst normal fix spacing (a lap timer runs GPS at >= 1 Hz).
+ * This duration test is only HALF the condition: lap_t.seg_has_invalid covers the other half -- a
+ * chord that spans one or more fixes the §6.5 validator rejected, however short (R22, audit
+ * core:C1). Either way both the lap that closes at the crossing and the lap it opens carry
+ * LAP_F_GPS_LOST. */
```

- [ ] **Step 6: Run the suite and commit**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
git add components/core/lapengine/lap.c components/core/include/core/lap.h components/core/fusion/fus.c
git commit   # docs(core): correct every false claim the audit found in the engine comments (F1a T13)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 13 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 13 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

Next task: **Task 14**. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

---

### Task 14 — SERIAL (final wave): gates, spec amendments, bench items, filed follow-ups

**Files:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (the seven amendments below only), `docs/bench/next-bench-day.md`. No code.

- [ ] **Step 1: Run every gate from design §4 and record the output**

```bash
# host
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %
# zero-warning sweep (no ccache, both compilers)
rm -rf test/build && CC=clang cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8 2>&1 | grep -i warning   # empty
gcc-16 -fsyntax-only $(python3 -c "import json;print(' '.join(set('-I'+i for e in json.load(open('test/build/compile_commands.json')) for i in [] )))") # see tools/README: run the project's gcc-16 sweep over compile_commands.json; 0 diagnostics
# linters
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
python3 tools/lint/venue_announce.py
# firmware: ccache off, clean, 0 warnings, rc 0
source tools/idf-env.sh
CCACHE_DISABLE=1 ./build.sh moto_sim build
CCACHE_DISABLE=1 ./build.sh moto_neo6m build
CCACHE_DISABLE=1 PANEL=ws29v2 ./build.sh moto_sim build
CCACHE_DISABLE=1 idf.py -C test_apps/core_selftest build
```

`test_apps/core_selftest` must build **and** pick up every new case: its CMake globs `test/test_*.c` and this plan adds no new file and no host-only (file-I/O) case, so its exclude regex `/test_(ui|screens|epd_pure[a-z0-9_]*|sim_profile|sim_clock)\.c$` is unchanged. Confirm the generated `suites_gen.h` still lists `lap`, `drag`, `fus`, `geo`, `tb`, `lap_gate`, `drag_cfg`.

- [ ] **Step 2: Report the static DRAM delta in the PR body**

```bash
CCACHE_DISABLE=1 ./build.sh moto_sim build   | tee /tmp/f1a_sim.log
CCACHE_DISABLE=1 ./build.sh moto_neo6m build | tee /tmp/f1a_neo.log
```

Record free static DRAM against the design §4 baselines (`moto_sim` 4192 B, `moto_neo6m` 5616 B). Expected net change, to be confirmed by the numbers: **+34 B** on `lap_t` (`lap_cfg_t` 2 B → 40 B) **+1 B** `seg_has_invalid` **+2 B** `pred_stride` **−8 B** on `fus_t` (`samples`, `v_course_deg`), i.e. ≈ **+32 B** including padding — well under the 256 B threshold that would require naming a reclaim. If either build loses more than 256 B, stop and report rather than hunting for a reclaim inside F1a's files.

- [ ] **Step 3: Amend the spec (seven edits, each citing the fix-wave design)**

1. **§6.4 step 1** (line 1149) — R22:

```diff
-1. Both fixes MUST be valid (§6.5). If either is invalid, no crossing is evaluated and `lap.flags |= LAP_GPS_LOST` if the engine is running.
+1. Both fixes MUST be valid (§6.5). An invalid fix updates no segment state: it sets `lap.flags |= LAP_GPS_LOST` if the engine is running and is otherwise skipped, so the next valid fix forms a chord across the whole outage. A crossing found on such a chord **is** evaluated — dropping it would merge two laps into one — but the chord's `t_cross` is an interpolation across missing data, so `LAP_GPS_LOST` is set on **both** the lap that closes at the crossing and the lap it opens. The same applies to a chord longer than `LAP_SEG_GAP_US` (5 s) made of absent fixes. (Amended by `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` ruling R22; the previous "no crossing is evaluated" was never implementable.)
```

2. **§9.3 step 4, cross-check bullet** (line 1471) — core:M6, documenting the extra gate the code has always had:

```diff
-   - Cross-check: `ψ̇_gps` = derivative of `headMot` across fixes; if `|ψ̇ − ψ̇_gps| > 10 °/s` for 5 s set `FUS_DISAGREE`, log `E_FUSION_DISAGREE` once per session.
+   - Cross-check: `ψ̇_gps` = derivative of `headMot` across fixes; if `|ψ̇ − ψ̇_gps| > 10 °/s` for 5 s set `FUS_DISAGREE`, log `E_FUSION_DISAGREE` once per session. The cross-check runs only while a GPS yaw reference qualifies — a valid fix, age < 1 s, and `v > LEAN_REF_MIN_SPEED_MPS` — because `headMot` is meaningless at a standstill; below that speed the timer resets and the flag clears. (Recorded by ruling R-list item core:M6 of `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`; the firmware has always had this gate.)
```

3. **§9.3 step 5, last sentence** (line 1472) — M1:

```diff
-Both `g_lon` and `g_lat` are clamped to ±`G_MAX`, and clamping sets `FUS_CLAMPED`.
+Both `g_lon` and `g_lat` are clamped to ±`G_MAX`, and clamping sets `FUS_CLAMPED`. Before the forward axis is known (§9.2) there is no signed longitudinal axis: `g_lon` carries the sign-less magnitude of the two in-plane axes of the current calibration frame (with the shipped identity `R`, the body x/y magnitude, which on a non-flat mount includes a component of gravity) and `g_lat` is 0. That magnitude is clamped to `G_MAX` on the same terms. (Ruling R-list item core:M1.)
```

4. **§10.3 table** — the leave-venue exit now emits an event; add the row to the §4.5 event table (after `EV_LAP_COMPLETE`) and amend the `LAP_RUNNING` exit cell:

```diff
+| `EV_VENUE_LOST` | arg16 = venue id left | lapengine |
```
```diff
-| `LAP_RUNNING` | | ... | stays; → `NO_VENUE` if distance from venue centre > `radius_m · 1.5` for 60 s |
+| `LAP_RUNNING` | | ... | stays; → `NO_VENUE` if distance from venue centre > `radius_m · 1.5` for 60 s, emitting `EV_VENUE_LOST` (arg16 = the venue id) before the venue is dropped — the logger closes the session on it (§12.1) |
```

Also update the §4.5 code list at line 754 to end `..., EV_FAULT, EV_CFG_CHANGED, EV_LAP_RESET, EV_OTA, EV_CREATE, EV_VENUE_LOST = 20, EV_COUNT }` with the note that logged codes are append-only, so `EV_VENUE_LOST` follows the ui-only block rather than renumbering it (design §3).

5. **§10.10** (line 818) — R25:

```diff
-int  lap_import_rtc(lap_t *L, const lap_rtc_t *s);           /* restore → LAP_RUNNING | LAP_F_INTERRUPTED */
+int  lap_import_rtc(lap_t *L, const lap_rtc_t *s);           /* 0 / -1 unknown venue / -2 unknown layout / -3 bad sector_idx */
```
plus one paragraph: a validated snapshot with a layout, or an out-lap, resumes `LAP_RUNNING | LAP_F_INTERRUPTED`; `lap_no > 0` with no layout resumes `ARMED` at `lap_no` 0 (ruling R25).

6. **§10.11** (line 1612) — make the recorded behaviour match: after "beyond that every 2nd fix", add "(the table is halved in place and the recording stride doubles, so it holds ≤ 600 monotonic-distance samples at an even density spanning the whole lap)".

7. **§11.2 Abort** (line 1652) — R21:

```diff
-Abort: if `v_est` falls below 1 km/h within 2 s of launch (false start, e.g. clutch dump stall) the run is discarded and state returns to `ARMED`.
+Abort: if `v_est` falls below `DRAG_ARM_SPEED_KMH` (0.5 km/h) within `DRAG_FALSE_START_S` (2 s) of `t0` (false start, e.g. clutch dump stall) the run is discarded, `run_no` is released, and the state returns to `ARMED` with `EV_DRAG_ARMED`. The test runs **before** the DONE transitions, so a stall can never be reported as a finished run. The threshold is 0.5 km/h, not 1 km/h: `do_launch` cannot enter `LAUNCHED` below `DRAG_LAUNCH_G × DRAG_LAUNCH_HOLD_MS` = 0.53 km/h, so a genuine launch whose `v_est` is still climbing out of zero never trips it. (Amended by ruling R21; a 1 km/h test could not distinguish the two.)
```

8. **§15.1**, after the six `lap.*` rows (lines 1869–1874) — add: "All six `lap.*` keys reach the lap engine through `lap_cfg_from_user()` → `lap_set_cfg()` (`core/lap_cfg.h`); the `consts.h` macros `MIN_LAP_S`/`MAX_LAP_S`/`GATE_REARM_DIST_M`/`PIT_SPEED_KMH`/`PIT_TIME_S` are now only the defaults and the per-field fallbacks. `lap.default_layout[]` is the §10.5 tie-break map, keyed by venue id. (Ruling R24; before this they were parsed, clamped, persisted and exported but inert.)"

- [ ] **Step 4: Append the bench items**

Only hardware can show these; append to the open list in `docs/bench/next-bench-day.md`:

```markdown
- [ ] **12. `cfg.lap.*` really reaches the engine (F1a, R24).** On the sim build with `dbg sim laps 10`
  running: `lt config set {"lap":{"min_lap_s":120}}` then `lt config get` (confirm 120 persisted) and
  watch the lap card — the ~28 s sim laps must stop completing while the one in progress keeps its
  clock (the reload applies in place, it does not reset the engine). Then
  `lt config set {"lap":{"min_lap_s":20}}` and confirm laps complete again. Second half:
  `lt config set {"lap":{"pit_speed_kmh":30,"pit_time_s":3}}`, `dbg sim park`, and confirm the next
  completed lap carries PIT. Before F1a every one of these keys was inert.
- [ ] **13. Leaving the venue closes the session (F1a emits `EV_VENUE_LOST`, F2b consumes it).** Needs
  F2b merged. `dbg sim laps 2`, then drive the sim origin > 3 km out for 60 s (or power-cycle with a
  venue-less capture): expect one `EV_VENUE_LOST` EVENT record with the venue id in the session, the
  session closed, and a new one opened on the next `EV_VENUE_FOUND`.
- [ ] **14. A layout override the venue does not have (F1a, R25).** `lt` → `CMD_SET_LAYOUT` with an id
  the sim venue lacks (e.g. 99): lap timing must keep working (the override is refused and the engine
  stays on Auto), not go silently dead for the rest of the session. The console gives no error yet —
  surfacing the −1 is F2b's follow-up below.
- [ ] **15. Best sectors do not cross venues (F1a, R25).** With the sim venue locked and a few laps
  banked, create a second venue on-device (NEW TRACK, item 5) and let it lock: the theoretical-best
  line must blank and then rebuild from the new venue's own splits, never show the old venue's number.
```

- [ ] **Step 5: File the follow-ups F1a cannot close inside its own files**

Open one issue each (title, body = the quoted finding and the owning plan); list the numbers in the PR body:

| Item | Why not here | Owner |
|---|---|---|
| `lap_force_layout` now returns −1; `pipeline.c:887` discards it, so `CMD_SET_LAYOUT` still answers OK for an unknown layout id | `pipeline.c`'s cmd path is F2b's | **F2b** |
| `trk.c:194-201` canonicalisation comment is only partly true — padding is zeroed but `name[]`/`layouts[].name[]` bytes after the NUL are copied verbatim (`trk.c:209,222`); not reachable today (every producer memsets first) | `core/tracks/*` is not an F1a file (F0 owns the `trk.*` comment) | **F4** (comment) or a 2-line `memset` of the name tails in whichever plan next owns `trk.c` |
| Padding assertions at `trk.c:34,39,49`, `trk_json.c:53-56,62-65,134-135,140-141` | not F1a files | **F4** |
| Padding assertions at `json.c:56-57` | `core/util/json.c` is F1b's | **F1b** |
| `lap_import_rtc` with `lap_no == 0` and no layout resumes RUNNING with an empty union, so §10.5 disambiguation is skipped for that out-lap | pre-existing, not a reviewer finding; out of R25's scope | new issue, Plan 8 |

- [ ] **Step 6: Self-review against design §4's last gate**

Walk the reviewer report top to bottom and tick every id against a task: C1→T2, H1→T3, H2/H3→T4, H4→T1, H5→T8, M1→T10, M2→T9, M3→T4, M4→T8, M5→T7, M6→T14 (spec), M7→T10, LOW `geo.c:43`→T11, LOW `geo.c:29`→T11, LOW predictive/`pred_rec_fixes`→T6, LOW dead `fus_t` fields→T10, LOW `faded`→T9, LOW `find_quarter`→T9, LOW `gate_step_range`→T9, LOW `DRAG_MPH_PER_KMH`→T9, LOW `in_layout` static assert→T12, LOW rule-9 ×11→T12, LOW rule-2 `do_launch`→T3's `/* rule 2 */` convention applied in T12's sweep (the `while (cur != newest)` loop gets the explicit `CORE_ASSERT_VOID(D->hist_count <= DRAG_HIST_N, ...)`-backed `/* rule 2: at most DRAG_HIST_N - 1 iterations; cur walks forward to newest */` note in T9), LOW rule-5 padding→T9/T11/T12, false claims 1–7+9→T13, false claim 8→filed. Test gaps: all nine closed by T1 (×2), T2 (×2), T3, T4 (×3), T6, T8, T9 (M2), T10 (M1), T11 (antipodal).

- [ ] **Step 7: Commit and open the PR**

```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/bench/next-bench-day.md
git commit   # docs: amend §6.4/§9.3/§10.3/§10.10/§10.11/§11.2/§15.1 for the F1a rulings; bench items 12-15 (F1a T14)
gh pr create --base main --title "F1a — core engines: lap/drag/fusion/geo/tb audit fixes (R20-R25)"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1a/progress.md` ("Task 14 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines."

```bash
mkdir -p .superpowers/sdd/2026-10-13-fix-F1a
SHA=$(git rev-parse --short HEAD)
printf 'Task 14 complete %s\n' "$SHA" >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
printf 'Resume: Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-core-engines; branch fix-core-engines.\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push -u origin fix-core-engines
tail -2 .superpowers/sdd/2026-10-13-fix-F1a/progress.md        # both lines present, and the push reported success
```

No Task 15: the plan is finished. The resume prompt stays pointed at Task 14 so a cleared session re-reads the final wave rather than inventing new work; the next action is the PR review, not another task. A `/clear` here is safe: the ledger's last `Resume:` line is the whole handoff.

- [ ] **SESSION 4 CHECKPOINT (Tasks 12-14)** — before the context is cleared, re-verify the session's whole output, then record it:

```bash
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %, no fixture diff
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                                    # 0
git status --short                                         # clean
printf 'Session 4 (Tasks 12-14) verified: ctest 100%%, both linters 0, tree clean\n' >> .superpowers/sdd/2026-10-13-fix-F1a/progress.md
git push origin fix-core-engines
```

If `ctest` or either linter fails here, the session is **not** checkpointed: fix forward on this branch (or `git revert` the offending task's commit) and re-run before writing the line.

- [ ] **Step 8: Write the PR body**

The PR body is the wave's evidence, so it carries all of it and nothing is left to the reader's trust: the fourteen-task list with each task's commit sha; a finding-to-task table copying the Self-Review table at the end of this plan, so every id in the core-algorithms report is visibly accounted for; the verbatim gate output from Step 1 (ctest summary line, both linter exit codes, the four firmware build result lines and their warning counts); the static DRAM delta from Step 2 against the design §4 baselines; the seven spec amendments with their section numbers; the **one** replay-fixture value changed, quoted as the diff from Task 2 Step 5 together with the `gps_dropout.truth.json` evidence that the out-lap's closing crossing falls inside the 120–130 s dropout, and the explicit statement that no other fixture field moved; the Cleanup-done table at the end of this plan; the follow-up issue numbers filed in Step 5 with their owning plans; and the bench items 12–15 appended to `docs/bench/next-bench-day.md`, flagged as the hardware half that this PR cannot prove. Do not claim a gate passed without pasting its output — design §4's last gate is that every finding id appears in exactly one plan's task list, and the PR body is where that is checked.

---

## Cleanup done

Everything this plan deletes or rewrites as cleanup, so a reviewer can see that nothing was removed silently and nothing stale was left behind. "Dead" means written and never read; the greps are the proof obligations.

| # | What | Where | Why it goes | Task | Proof |
|---|---|---|---|---|---|
| 1 | `lap_cfg_t.default_layout_id` (field) | `core/lap.h:55` | replaced by the §10.5 `default_layout[]` map; it was the only cfg field the engine had, read at one site | 1 | `grep -rn "default_layout_id" components test tools main` → no output |
| 2 | `lap_t.pred_rec_fixes` left dead | `core/lap.h:173`, `lap.c:370` | incremented and never read (4 B of dead state); now the stride counter that implements §10.11's "every 2nd fix", so the field is live rather than deleted | 6 | `test_predictive_table_cap` asserts `pred_stride == 2` |
| 3 | `fus_t.samples` (field + `f->samples++`) | `core/fus.h:114`, `fus.c:364` | write-only; nothing ever read it | 10 | `grep -rn "->samples\|\.samples" components test tools main` → no output |
| 4 | `fus_t.v_course_deg` (field + its assignment) | `core/fus.h:104`, `fus.c:151` | write-only; the yaw cross-check uses `yaw_gps_dps`, not the raw course | 10 | `grep -rn "v_course_deg" components test tools main` → no output |
| 5 | `#define DRAG_MPH_PER_KMH` | `core/drag.h:61` (+ 2 doc refs, 2 uses, 1 test use) | the name inverted the ratio its own comment stated; renamed `DRAG_KMH_PER_MPH`, same value | 9 | `grep -rn "DRAG_MPH_PER_KMH" components test tools main` → no output (the two hits in `docs/superpowers/plans/2026-10-03-debt-sweep-b.md` are a historical plan and stay) |
| 6 | `sec_fire_t` (typedef) | `lap.c:577` | generalised into `lap_cross_t` with the window-filtered collector | 3 | `grep -n "sec_fire_t" components/core/lapengine/lap.c` → no output |
| 7 | the `v_peak > DRAG_FALSE_START_KMH` abort guard | `drag.c:572` | the guard existed to protect a climbing low-g launch; the `stopped` threshold (0.5 km/h, below the 0.53 km/h launch floor) does that structurally, and the guard was what let a stall reach `enter_done` | 8 | `test_marginal_g_launch_then_stall_aborts`, `test_low_g_launch_no_false_start` |
| 8 | `CORE_ASSERT_VOID(tc >= tp, ...)` in `gate_step` | `drag.c:299` | a legitimate backward `gps_us` step is not a bug; replaced by an early `return` that skips only that sample | 9 | `test_backward_gps_us_step_does_not_lose_a_gate` |
| 9 | 18 padding assertions in `drag.c`, `tb.c` | `drag.c:19,300,301,349,511,512,530,547,599`; `tb.c:47,63` | tautologies given their only call sites (state checked right after the dispatching `switch`, `kmh >= 0` on `uint16_t` input, `cur == 0 \|\| cur == 1` on a value only ever assigned `0`/`1-c`) | 9, 11 | linter rule 5 still 0; each > 20-line function keeps ≥ 2 meaningful assertions (tables in those tasks) |
| 10 | 19 NULL-pointer assertions on `&`-of-local-only static helpers | `fus.c:18,19,95,96,97,180-184`; `fus_orient.c:21,22,34,35,36,45,46,59,60,61` | every caller passes the address of its own local, so the pointer cannot be NULL; `all_finite`'s `n` bound and `v_normalize`'s `n > 0.0f` are real and stay | 12 | linter rule 5 still 0; `fus_step_planar` gains an `isfinite` postcondition |
| 11 | 6 padding assertions in `lap.c` | `lap.c:848,880,912,1019,1020` | state/mode the immediate caller just branched on, and a precondition that restated the postcondition; each replaced by a real pre/postcondition | 12 | linter rule 5 still 0, `ctest` unchanged |
| 12 | stale comment "fused stats feed sector stats in a later session" | `lap.c:1044` | there is no later session: §9.1 step 5's stats are the pipeline's and `stats_step()` already does it | 13 | `grep -n "in a later session" components/core/lapengine/lap.c` → no output |
| 13 | stale comment "a sector gate precedes the S/F ... for any realistic gate spacing" + "crossings in a segment are processed in `t_cross` order" | `lap.c:981-983` | both false; §6.4's own worked example is the counter-case. Deleted with the code it described; the ordering claim is now true and lives on `evaluate_gates` | 3, 13 | `grep -n "a sector gate precedes the S/F" components/core/lapengine/lap.c` → no output |
| 14 | stale comment "recording continues at half density (\"every 2nd fix\")" | `lap.c:356-358` | it continued at full density; rewritten to describe the stride that now exists | 6, 13 | `grep -n "recording continues at half density" components/core/lapengine/lap.c` → no output |
| 15 | stale comment "sign-less **horizontal** magnitude" | `fus.c:274` | with the shipped identity `R` it is the body x/y magnitude, which on a non-flat mount includes gravity — not the horizontal one | 10, 13 | `grep -n "sign-less horizontal magnitude" components/core/fusion/fus.c` → no output |
| 16 | over-promising `lap_import_rtc` doc ("Returns -1 only if the venue id is unknown", "restores it into LAP_RUNNING") | `core/lap.h:202-205` | it returned 0 for an unknown layout id and accepted any `sector_idx`; the contract is now four return codes and two resume states | 4, 13 | the three-case `test_import_rtc_rejects_bad_layout_and_sector_idx` |
| 17 | wrong `LAP_EVT_MAX` accounting ("at most one ARMED/VENUE_FOUND") | `core/lap.h:33-35` | both fire on one fix; the real bound is re-derived in the comment | 13 | `grep -n "at most one ARMED/VENUE_FOUND" components/core/include/core/lap.h` → no output |
| 18 | `reset_to_no_venue`'s claim that the best-sector table is preserved | `lap.c:75-78` | it is now cleared, because the table is per (venue, layout) and a reset drops both | 4 | `test_best_sectors_cleared_on_venue_change` |
| 19 | `pipeline_reload_cfg`'s comment "The lap engine is untouched." | `pipeline.c` | it is touched now, through `lap_set_cfg` | 1 | `grep -n "The lap engine is untouched" components/app/pipeline/pipeline.c` → no output |

Nothing in this table is a behaviour change on its own except rows 1, 2, 5, 7, 8, 18 — each of which is covered by a test named in its Proof column. Rows 9–11 must leave `ctest` bit-identical; if any of them moves a test, revert that one site (Task 12 Step 5 says so).

---

## Self-Review

Run as a checklist against the core-algorithms reviewer report and design §4, not as a subagent dispatch.

**1. Spec coverage — every finding id in `.superpowers/reviews/2026-10-13/core-algorithms.md` maps to exactly one task.**

| Finding | Task | What closes it |
|---|---|---|
| **C1** short dropout → wrong lap VALID | 2 | `seg_has_invalid` makes the chord, not its duration, the test; both laps flagged |
| **H1** sector gate lost beside the S/F | 3 | S/F found first, gate crossings split into `[MIN,t_sf)` / `[t_sf,MAX)` windows |
| **H2** best sectors leak across venues/layouts | 4 | `clear_best_sectors` on reset / venue set / forced-layout change / lock / import |
| **H3** unknown layout id arms nothing | 4 | `venue_has_layout`; `lap_force_layout` → −1; `lap_import_rtc` → −2; `lap_set_venue` drops a stale forced id |
| **H4** `cfg.lap.*` inert | 1 | `lap_cfg_t` + `lap_cfg_from_user` + `lap_set_cfg` + the two `pipeline.c` sites + the nine macro→cfg reads |
| **H5** marginal-g stall emits DONE | 8 | abort on `stopped` inside `DRAG_FALSE_START_S`, tested before the DONE transitions |
| **M1** un-oriented `g_lon` unclamped | 10 | the `G_MAX` clamp in the else branch |
| **M2** `gate_step` asserts on a backward step | 9 | `if (tc < tp) return;` replacing the assertion |
| **M3** `lap_import_rtc` validates only the venue | 4 | `sector_idx > LAP_MAX_SECTORS` → −3; `sec_next` postcondition; export guard |
| **M4** `abort_to_armed` emits no `EV_DRAG_ARMED` | 8 | `abort_to_armed` routes through `enter_armed`; `run_no` release documented |
| **M5** `lap_mark_gate` ignores a degenerate line | 7 | lat/lon/heading range checks + the degenerate-line test |
| **M6** §9.3's undocumented speed gate on the yaw cross-check | 14 | §9.3 step 4 amended to state it (code is already right) |
| **M7** `FUS_BIAS_STALE` clears without a recapture | 10 | `update_bias_stale` latches; only `fus_gyro_bias_update` clears |
| LOW `geo.c:43` antipodal NaN | 11 | `a` clamped to [0,1] |
| LOW `geo.c:29` no ±180° wrap | 11 | `dlon` wrapped into (−π, π] |
| LOW predictive "half density" false, `pred_rec_fixes` dead | 6 | `pred_stride` doubling at each halving |
| LOW dead `fus_t.samples` / `v_course_deg` | 10 | deleted (Cleanup rows 3, 4) |
| LOW `faded` requires `last_gate_gps_us != 0` | 9 | falls back to `t0_gps_us` |
| LOW `find_quarter` `>=` lets the last tie win | 9 | `>` |
| LOW `gate_step_range` arms `a` and records `b` in one pair | 9 | `armed_now` |
| LOW `DRAG_MPH_PER_KMH` name inverts the ratio | 9 | renamed `DRAG_KMH_PER_MPH` |
| LOW `ugate.in_layout` fits `TRK_MAX_LAYOUTS == 8` with no static assert | 12 | `_Static_assert(TRK_MAX_LAYOUTS <= 8, ...)` |
| LOW rule 9 — 11 two-level dereferences in `lap.c` | 12 | a named local at each of the 11 sites (two already removed by Task 3) |
| LOW rule 2 — `do_launch`'s unbounded `while` | 9 | explicit `/* rule 2 */` bound note backed by the `hist_count <= DRAG_HIST_N` assertion |
| LOW rule 5 — ~30 padding assertions | 9, 11, 12 | Cleanup rows 9–11, with the replacement assertion named per site |
| False claim 1 (`lap.c:981-983`) | 3, 13 | Cleanup row 13 |
| False claim 2 (`lap.c:356-358`) | 6, 13 | Cleanup row 14 |
| False claim 3 (`lap.h:203` / `lap.c:284-286`) | 4, 13 | Cleanup row 16 |
| False claim 4 (`lap.c:1044`) | 13 | Cleanup row 12 |
| False claim 5 (`lap.h:33-35`) | 13 | Cleanup row 17 |
| False claim 6 (`fus.c:210-213`, `266-268`) | 10, 13 | both comments rewritten to name both branches |
| False claim 7 (`fus.c:274`, reviewer cites `fus.h:116`) | 10, 13 | Cleanup row 15 + the §9.3 step 5 amendment |
| False claim 8 (`trk.c:194-201` canonicalisation) | **filed, not fixed** | `core/tracks/*` is not an F1a file — Task 14 Step 5 files it to F4 with the exact text and the two-line `memset` alternative |

**Test gaps (the reviewer's nine) — each becomes a case in the task that fixes it:** sector + S/F in one segment → Task 3; venue change inside a session → Task 4; unknown *layout* id on force and on import → Task 4; a non-zero default-layout map / §10.5 tie-break → Task 1; marginal-g launch that stalls → Task 8; non-monotonic fused `gps_us` → Task 9; un-oriented `g_lon` magnitude + clamping → Task 10; dropout shorter than `LAP_SEG_GAP_US` → Task 2; antipodal `geo_dist_m` → Task 11. Plus five not on the reviewer's list but needed by the rulings: `min_lap_s` reaching the engine and `lap_set_cfg` applying without a reset (Task 1), the absent-fix gap flagging the completing lap (Task 2), `EV_VENUE_LOST` (Task 5), predictive stride (Task 6), `lap_gate_line`'s rejection contract and `lap_mark_gate`'s heading range (Task 7), and three drag LOWs (Task 9).

**Design §4 gates:** host `ctest` 100 % — every task's run step plus four session checkpoints; clang + gcc-16 0 diagnostics — Task 14 Step 1; both linters 0 — Tasks 9, 11, 12 and all four session checkpoints; the three firmware builds + `test_apps/core_selftest` — Task 14 Step 1 (and one `moto_sim` build in Task 5, where the app-side assertions change); DRAM delta reported — Task 14 Step 2; no flashing by a subagent — stated in Global Constraints, and the hardware-only behaviour is deferred to bench items 12–15 (Task 14 Step 4); every finding id in exactly one task list or filed — the table above.

**2. Placeholder scan.** No "TBD", "TODO", "implement later", "add appropriate error handling", "write tests for the above", or "similar to Task N" appears: every code step carries the actual C or the actual diff hunk, every test step the actual Unity case, and the three places where a value must be *measured* rather than predicted (the DRAM delta, the gate output, the fixture re-run) say exactly which command produces it and what to do when the number is wrong — Task 14 Steps 1–2 and Task 2 Step 5, which both end in "stop and report" rather than in a guess. The one forward reference, Task 12's note that `trk.c`/`trk_json.c`/`json.c` padding is out of bounds, resolves to a filed row in Task 14 Step 5.

**3. Name consistency across tasks.** `lap_cfg_t` / `lap_cfg_defaults` / `lap_set_cfg` / `lap_cfg_from_user` / `LAP_MAX_DEFAULT_LAYOUTS` (Task 1) are spelled identically in Tasks 4, 14 and the Interfaces blocks that consume them. `lap_cross_t` / `LAP_CROSS_MAX` / `cross_sort` / `evaluate_gates` / `evaluate_locked_sectors(…, t_lo, t_hi, …)` / `evaluate_union(…, t_lo, t_hi)` (Task 3) are used with those exact signatures in Task 12's dereference table. `seg_has_invalid` (Task 2) is cleared by name in Tasks 4 and 6's edits to `lap_set_venue` / `lap_import_rtc`. `clear_best_sectors` / `venue_has_layout` (Task 4) are referenced by `lap_set_venue` and `lap_force_layout` only, both in Task 4. `pred_stride` / `PRED_STRIDE_MAX` (Task 6) appear in five reset sites, all listed. `abort_to_armed(D, out, cap, n, gps_us, mono_us)` (Task 8) has one caller, in Task 8. `EV_VENUE_LOST` / `EV_COUNT` (Task 5) are used by the four assertions in Task 5 and the spec rows in Task 14. `DRAG_KMH_PER_MPH` (Task 9) replaces every occurrence of the old name in the same task, verified by grep. `lap_force_layout` returns `int` from Task 4 onward and no earlier task calls it.
