# Fix Wave F3 — Sim Drivers + Dev-Kit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every sim-driver and dev-kit finding of the 2026-10-13 audit that hardware does not gate: the `dbg sim drag` cumulative-origin bug that made every drag fix past ~104 km/h invalid (`drv:H2`), a random NVS-stored AP passphrase (`dk:H1`), the unbounded download window and `portMAX_DELAY` download take (`dk:M1`, `M2`), a `selftest framing` that cannot see a missing CR (`dk:M3`), the 6-verb `lt` whitelist becoming the spec'd relay (`dk:M4`), the bridge-exit drain (`M6`), the SSE cursor off-by-one (`M7`), the `flash stage` abort drain and `push_and_wait`'s swallowed `ERR` lines (`M8`, `M9`), the stale `logging:true` (`M10`), the tight `OTA_READY_TMO_MS` (`M11`), `L1`–`L8`, `drv:L23`, the 35 s OTA-trial rule and port-identity probe in both host tools (`drv:M9`), and every false claim those findings exposed in code comments and the dev-kit spec.

**Architecture:** Nothing new is designed. Each fix lands in the module that already owns the behaviour, and every piece of new *logic* goes into a pure `host/*.c` file so `devcontroller/test` (28 Unity executables, ASan/UBSan, `-Werror`) can fail first: the download-cap arithmetic (`lh_dl_cap_us`), the download-progress counter (`lh_dl_progress`), the framing-marker CR rule, the SSE cursor guard (`sse_cursor_fix`), the static-path guard (`www_path_ok`), the PSK alphabet (`dc_psk_format`), the logstore readiness predicate, and — on the lap-timer side — the drag position step (`sim_drag_lat_e7`, host-tested through `test/test_sim_profile.c` because `gps_sim.c` itself includes `esp_timer.h` and cannot build on the host). The `lt` relay reuses `linkhost_bridge_begin`/`bridge_filter` unchanged rather than inventing a second transport. The unpaced OTA stream (`dk:H2`/`drv:M8`) is **not** fixed here — the per-chunk ACK transport stays filed as #57 — but both pushers gain a 25 ms pause at the lap-timer's own 4096 B flash-write boundary, which the measured numbers justify (Task 3 Step 5, Task 10 Step 6).

**Tech Stack:** C11 (dev-kit: pragmatic Power-of-10 — bounded loops, no heap after init, zero warnings; lap-timer `components/drivers`: full Power-of-10), ESP-IDF v5.3.2 (`source tools/idf-env.sh`), Unity host harnesses (`test/`, `devcontroller/test/`), vanilla JS (`devcontroller/web/`), Python 3 + `unittest` (`tools/test_devkit.py`).

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding — this plan is row **F3**: rulings **R16**, **R17**, **R29**, **R30**, and **R20** on its own files; §4 gates; §5 keeps #57 filed). Supporting evidence, cited per task: `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` §7.2–7.3, `.superpowers/reviews/2026-10-13/dev-controller.md` (findings `dk:*`), `.superpowers/reviews/2026-10-13/drivers-build-tools.md` (findings `drv:*`), and the three reproducers `dl.c`, `fr.c`, `drag.c` in that same directory. Dev-kit specs this plan amends: `docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` §5.2/§6/§7.

**Worktree:** create `/Users/benji/projects/personal/lap-timer-worktrees/f3` on a new branch `fix-sim-devkit` off `main` **after F0 (PR #101) is merged** (`git worktree add ../lap-timer-worktrees/f3 -b fix-sim-devkit main`). F0's only overlap with this plan's files is one help string in `devcontroller/web/app.js:219-221`; branching after it merges removes the conflict. Every path below is relative to that worktree.

## Global Constraints

- **Dev-kit code (`devcontroller/**`) is pragmatic Power-of-10** (Plan 5.5/5.6 Global Constraints): every loop bounded by a constant, a deadline, or a byte count that the caller supplied; no heap allocation after init (no `malloc`/`calloc`/`strdup` anywhere in this plan); no recursion; function pointers only where one already exists (`lh_dl_chunk_cb`, `console_register`, `httpd` handlers); `assert()` for preconditions in pure `host/*.c`. The linter scans `devcontroller/` **report-only** (R12, F4) — do not introduce a violation it would print.
- **Lap-timer code (`components/drivers/{gps_sim,sim_common}`) is full Power-of-10**: > 20 code lines ⇒ ≥ 2 `CORE_ASSERT_*` (never abort); ≤ 60 code lines per function; no recursion, heap, goto or function pointers. `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0 and `python3 tools/lint/venue_announce.py` = 0 at the final wave (F4's stricter linter may already have merged — re-run whichever is on the branch).
- **R20 on this plan's own files:** no padding assertions. Every assertion added must be a real pre/postcondition; no side effect may live inside an assertion (there are none in these files today — do not add any).
- **Zero warnings everywhere:** lap-timer host suite and dev-kit host suite both build with `-Wall -Wextra -Werror -Wshadow -Wconversion` (the dev-kit harness adds ASan+UBSan in Debug); `./build.sh moto_sim build`, `./build.sh moto_neo6m build`, `PANEL=ws29v2 ./build.sh moto_sim build`, `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build`, and `cd devcontroller && idf.py build` each rc 0 with `grep -c -i "warning:"` = 0. Fix at source — never `-Wno-*`, never a pragma.
- **No new `test/test_*.c` file on the lap-timer side.** CI's required `build (moto_neo6m)` job compiles `test_apps/core_selftest`, which globs `test/test_*.c`, and its exclude regex (`test_apps/core_selftest/main/CMakeLists.txt:15`) is **F4's file, not ours**. Task 1 therefore extends the already-excluded `test/test_sim_profile.c` instead of adding a suite. New **dev-kit** test files are free: `devcontroller/test/CMakeLists.txt` globs `test_*.c` with `CONFIGURE_DEPENDS` and needs no edit.
- **DRAM:** baseline free static DRAM `moto_sim` 4192 B, `moto_neo6m` 5616 B (ledger 2026-10-05). This plan adds 12 B of `.bss` on the lap-timer (`s_drag_origin`) plus `sim_drag_pos.o`; report the free-DRAM line of every firmware build. Dev-kit `.bss` grows ~2.6 KB (2048 B `s_log_entries`, 513 B relay body, 13 B PSK) — the dev-kit has no DRAM budget; report `heap_free` from `dc status` at the bench instead.
- **The dev-kit's on-disk log format (`logstore_rec_hdr_t`, `.bin`) and `app/lt_proto.h` are unchanged.** `lt_proto.h` stays IDF-free. No wire-format change reaches the lap-timer: every lap-timer-facing byte this plan writes is an existing console command.
- **Every comment this plan writes must be true of the resulting tree.** Seven of this plan's findings ARE false comments; do not create new ones. Where a comment states a bound, the bound must be in the code next to it.
- **Never flash, never run `esptool`, never open a serial port from a subagent.** `tools/devkit.py`/`tools/ota_push.py` changes are verified by `tools/test_devkit.py` against `FakeSerial` only. The bench items Task 11 appends are run by the user on the single bench day after F2b merges.
- **`www.bin` needs no separate regeneration step or commit:** `devcontroller/CMakeLists.txt:53` packs `web/` into the `www` partition image with `littlefs_create_partition_image(www web FLASH_IN_PROJECT)`, so `cd devcontroller && idf.py build` repacks `devcontroller/build/www.bin` on every SPA edit and nothing named `www.bin` is tracked by git. The bench consequence is real and belongs in the bench item: an **app-only OTA does not refresh the SPA** — a `web/` change reaches the dev-kit only through `idf.py -p <port> flash` over USB.
- **Commit messages** end with the executing session's own two-line trailer, exactly the shape F0 used on its branch, e.g.
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_<this session's id>
  ```
  (substitute the executing session's model name and session URL). The squash-merge message on `main` carries no trailer — the user writes it.
- **Checkpoint protocol — the executing session must be able to `/clear` between tasks and lose nothing.** All state lives in three places and nowhere else: the branch `fix-sim-devkit` (pushed after every task), this plan file (the only source of what to do next), and the ledger `.superpowers/sdd/2026-10-13-fix-F3/progress.md` (git-ignored via `.gitignore:18`, so it is never committed — create it with `mkdir -p` on the first checkpoint). **Every** task ends with a `CHECKPOINT` step that writes `Task N complete <short sha>`, pushes the branch, and records the verbatim resume prompt for the next task. Tasks are grouped into **three sessions of four tasks** (Session 1 = Tasks 1–4, Session 2 = Tasks 5–8, Session 3 = Tasks 9–12); each session ends with a `SESSION N END` step that re-runs both host suites plus `tools/test_devkit.py` and appends the counts to the ledger, so no red test is ever carried across a `/clear`. Nothing may be held only in conversation context: if a task discovers something the next task needs (a measured number, a deviation, an issue number), it goes in the ledger line *and* in the commit message. On resume, the first actions are `cd` to the worktree, `git log --oneline -5`, `tail -5 <ledger>`, and re-reading this plan from the named task — never re-run a completed task's edits.
- **Design §4 gates that apply to F3** (full list in Task 12): lap-timer `ctest` 100 %, dev-kit `ctest` 100 %, `python3 -m unittest tools/test_devkit.py` 100 %, both linters 0, all five firmware builds rc 0 / 0 warnings with the DRAM line reported, `node --check devcontroller/web/app.js`, no flashing by subagents, every `dk:*`/`drv:*` id this plan owns in exactly one task.

---

## File Structure

| File (line ranges verified on `f4d4db2`) | Responsibility | Task |
|---|---|---|
| `components/drivers/sim_common/include/sim_drag_pos.h`, `host/sim_drag_pos.c` (**new**), `CMakeLists.txt:12` | pure drag-run position step from a latched origin | 1 |
| `components/drivers/gps_sim/gps_sim.c:124-139` (`gps_configure`), `:152-167` (`gps_sim_rearm`), `:310-355` (`deliver_drag`) | latch the origin once per run; re-anchor the sim clock on reconfigure | 1 |
| `test/test_sim_profile.c` (+2 cases, +2 `RUN_TEST`), `test/CMakeLists.txt:171` | host proof of the step bound and the total run distance | 1 |
| `devcontroller/components/linkhost/include/linkhost_proto.h:160-230`, `host/linkhost_proto.c:360-395`, `:450-576`, `:890-899`, `:746-760`; `host/bridge_filter.c:53` | body-size cap, cap arithmetic, progress counter, demux `len` sanity, STATUS plausibility, bridge `len` bound, demux resync | 2 |
| `devcontroller/test/test_linkhost_dl_cap.c` (**new**), `test_linkhost_demux.c`, `test_bridge_filter.c` | host proof of all of Task 2 | 2 |
| `devcontroller/components/linkhost/linkhost.c:36-70`, `:73`, `:375-470`, `:530-600`; `include/linkhost.h:76-77` | idle timer fed by progress only, bounded download take, 15 s OTA-READY, OTA pacing, bridge-exit drain, tear-free activity stamp, stale heartbeat claims | 3 |
| `devcontroller/components/webapi/include/sse_cursor.h`, `host/sse_cursor.c`, `include/www_path.h`, `host/www_path.c` (**new ×4**), `host/config_diff.c`, `include/config_diff.h`, `CMakeLists.txt` | pure SSE cursor guard, static-path guard, distinct parse-vs-size diff codes | 4 |
| `devcontroller/components/webapi/webapi.c:292-295`, `:340-360`, `:735-745`, `:1073-1090` | 400 vs 413, `/api/logs` full list + `created_unix`, `>= BC_CAP`, path guard | 4 |
| `devcontroller/test/test_sse_cursor.c`, `test_www_path.c` (**new ×2**), `test_config_diff.c` | host proof of Task 4 | 4 |
| `devcontroller/web/app.js` (`saveConfig`, `renderLogsTable`), `web/index.html:88-96`, `web/app.css` | busy-aware save message, Created column, honest `rx_us` note | 4 |
| `devcontroller/components/devconsole/include/dc_psk.h`, `host/dc_psk_gen.c`, `dc_psk.c` (**new ×3**), `CMakeLists.txt`, `devcontroller/main/main.c:84-101`, `cmd_dc.c:36-39`, `:54-145`, `:202-219`; `devcontroller/test/test_dc_psk.c` (**new**) | random NVS AP passphrase, `dc status` field, `dc psk reset` | 5 |
| `devcontroller/components/devconsole/cmd_lt.c:1-30`, `:412-459` | `lt <any verb>` pass-through relay over the bridge filter | 6 |
| `devcontroller/components/devconsole/host/selftest_eval.c:84-128`, `include/selftest_eval.h:62-72`; `devcontroller/test/test_selftest_eval.c` | marker lines must carry exactly one CR | 7 |
| `devcontroller/components/devconsole/cmd_flash.c:25-45`, `:177-195`, `flash push` result print | honest drain claim + drain-to-quiet, 35 s rule on the console | 8 |
| `devcontroller/components/logstore/include/logstore_rot.h`, `host/logstore_state.c` (**new**), `logstore.c:81-100`, `:340-365`, `include/logstore.h:24-29`, `include/logstore_rec.h:26`; `devcontroller/test/test_logstore_state.c` (**new**) | `logging:false` on a failed append, honest header contracts | 9 |
| `tools/devkit.py`, `tools/ota_push.py`, `tools/test_devkit.py` | port-identity probe, DTR/RTS-safe open, stage abort stop, `ERR` classifier, 35 s rule, OTA pacing | 10 |
| `docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` §5.2/§6/§7, `docs/bench/next-bench-day.md`, GitHub issues | spec amendments, bench items, #57 comment, one new issue | 11 |

**Parallelism (design §1: "SERIAL/PARALLEL by file ownership"):** seven disjoint file groups.

| Group | Tasks | Mode |
|---|---|---|
| A sim driver | 1 | PARALLEL with every other group |
| B linkhost | 2 → 3 | **SERIAL** (Task 3 consumes Task 2's two new functions) |
| C webapi + SPA | 4 | PARALLEL |
| D devconsole + flashcore | 5, 6, 7, 8 | PARALLEL with each other (disjoint files; only 5 touches the component `CMakeLists.txt`) |
| E logstore | 9 | PARALLEL |
| F python tools | 10 | PARALLEL |
| G docs/specs/process | 11 → 12 | **SERIAL, last** (after 1–10 merge into the branch) |

The file groups are disjoint, so a dispatcher may run A/C/D/E/F concurrently in sub-agents; **every group lands on the one branch `fix-sim-devkit` in the one worktree**, so the default and simplest execution is serial in the session order below, with a `/clear` at any CHECKPOINT.

## Session plan (≤ 4 tasks per session; `/clear` at any CHECKPOINT)

| Session | Tasks | Ends with |
|---|---|---|
| **1** | 1 sim driver · 2 linkhost pure · 3 linkhost glue · 4 webapi + SPA | `SESSION 1 END`: host 47/47, dc-host 31/31, tools 17 |
| **2** | 5 AP PSK · 6 `lt` relay · 7 framing CR · 8 flash drain | `SESSION 2 END`: host 47/47, dc-host 33/33, tools 28 |
| **3** | 9 logstore · 10 host tools · 11 specs/bench/issues · 12 gates + PR | `SESSION 3 END`: host 47/47, dc-host 34/34, tools 28 + the PR number |

---

## Session 1 — Tasks 1–4 (sim driver, linkhost, webapi + SPA)

---

### Task 1: `dbg sim drag` latches its origin once per run (`drv:H2`, `drv:L23`, `drv` false claim 1) — GROUP A, PARALLEL

**Evidence:** `.superpowers/reviews/2026-10-13/drivers-build-tools.md` H2 + L23 + false claim 1; audit §7.2; reproducer `.superpowers/reviews/2026-10-13/drag.c`. Re-run on this machine 2026-10-13: final north offset **49496 m** against a true run length of **690 m**; every frame from t ≈ 7.8 s on jumps > 70 m and is rejected by the §6.5 filter (`FIX_MAX_JUMP_MPS·dt + 20` = `250·0.2 + 20`).

**Files:**
- Create: `components/drivers/sim_common/include/sim_drag_pos.h`, `components/drivers/sim_common/host/sim_drag_pos.c`
- Modify: `components/drivers/sim_common/CMakeLists.txt:12` (add `"host/sim_drag_pos.c"` to `SRCS`), `components/drivers/gps_sim/gps_sim.c`, `test/test_sim_profile.c`, `test/CMakeLists.txt:171`

**Interfaces:**
- Consumes: `sim_drag_at(int64_t t_us, sim_drag_state_t *out)` and `SIM_DRAG_*` from `sim_profile.h`; `sim_clock_rearm(sim_clock_t *, int64_t last_us, int64_t period_us)` from `sim_clock.h`; `gps_fix_t` from `hal/gps.h`.
- Produces: `int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m)` and `#define SIM_DEG_LAT_M 111320.0` in `sim_drag_pos.h`. Nothing outside `gps_sim.c` and the host test uses them.

- [ ] **Step 1: Write the failing test** — append to `test/test_sim_profile.c` (above `int main`), then add both `RUN_TEST` lines inside `main`:

```c
#include "sim_drag_pos.h"      /* with the existing includes at the top of the file */

/* drv:H2 -- the delivered position must be origin + f(cumulative distance), with the origin
 * LATCHED for the whole run: the per-frame step is then dist(t) - dist(t-200ms), never dist(t).
 * Bound: the pipeline's §6.5 jump filter allows FIX_MAX_JUMP_MPS*dt + 20 = 250*0.2 + 20 = 70 m
 * per 5 Hz step (core/consts.h, pipeline.c's compute_validity). The reviewer's reproducer
 * (.superpowers/reviews/2026-10-13/drag.c) measured 690 m single-frame steps and a 49496 m total
 * with the origin moving every frame. */
static void test_drag_position_steps_stay_inside_the_jump_filter(void)
{
    const int32_t origin = -338900000;             /* the capture's own latitude, 1e-7 deg */
    int32_t prev = origin;
    double  max_step_m = 0.0;
    for (int i = 0; i < 140; i++) {                      /* 28 s at 5 Hz: past the profile's park */
        sim_drag_state_t s;
        sim_drag_at((int64_t)i * 200000, &s);
        int32_t lat = sim_drag_lat_e7(origin, s.dist_m);
        double step_m = (double)(lat - prev) / 1e7 * SIM_DEG_LAT_M;
        TEST_ASSERT_TRUE(step_m >= 0.0);                 /* due north, monotonic */
        if (step_m > max_step_m) max_step_m = step_m;
        prev = lat;
    }
    TEST_ASSERT_TRUE(max_step_m < 70.0);
    TEST_ASSERT_TRUE(max_step_m > 12.0 && max_step_m < 13.0);   /* VMAX 62 m/s * 0.2 s = 12.4 m */
    double total_m = (double)(prev - origin) / 1e7 * SIM_DEG_LAT_M;
    TEST_ASSERT_TRUE(total_m > 680.0 && total_m < 700.0);       /* the profile's 690.25 m run */
}

/* The origin is a pure offset: the same distance from two different origins yields the same
 * displacement, and dist_m == 0 (sim_profile.h's t=0 contract) yields the origin byte-identically
 * -- which is what makes a LAPS -> DRAG switch continuous in position. */
static void test_drag_position_is_a_pure_offset(void)
{
    sim_drag_state_t s;
    sim_drag_at(0, &s);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, s.dist_m);
    TEST_ASSERT_EQUAL_INT32(-338900000, sim_drag_lat_e7(-338900000, s.dist_m));
    sim_drag_at(SIM_DRAG_HOLD_US + 5000000, &s);
    int32_t d1 = sim_drag_lat_e7(0, s.dist_m);
    int32_t d2 = sim_drag_lat_e7(1000000, s.dist_m) - 1000000;
    TEST_ASSERT_EQUAL_INT32(d1, d2);
}
```

- [ ] **Step 2: Add the source to both builds and run to fail**

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
# test/CMakeLists.txt:171 -- one line, append the new pure source:
#   add_core_test(test_sim_profile ${SIM_COMMON_DIR}/host/sim_profile.c ${SIM_COMMON_DIR}/host/sim_drag_pos.c)
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8 2>&1 | tail -5
```
Expected: FAIL — `sim_drag_pos.h: No such file or directory` (the header does not exist yet).

- [ ] **Step 3: Write the pure helper**

`components/drivers/sim_common/include/sim_drag_pos.h`:
```c
#ifndef SIM_DRAG_POS_H
#define SIM_DRAG_POS_H
#include <stdint.h>
/* Pure position step for SIM_SCENARIO_DRAG (drv:H2). The drag profile (sim_profile.h) reports the
 * CUMULATIVE distance from the launch point, so a delivered fix must be origin + f(dist_m) with
 * the origin latched once for the whole run (gps_sim.c's s_drag_origin, latched in
 * gps_sim_rearm()). Integrating against the previous frame's position instead makes every
 * per-frame step equal the whole cumulative distance -- beyond ~104 km/h that exceeds the §6.5
 * jump filter (FIX_MAX_JUMP_MPS*dt + 20 = 70 m at 5 Hz) and every fix of the run is dropped with
 * EV_FIX_LOST. No state, no allocation; host-tested by test/test_sim_profile.c. */
#define SIM_DEG_LAT_M 111320.0   /* metres per degree of latitude (due-north runs only) */
/* Latitude in 1e-7 deg for a point `dist_m` (>= 0) due north of `origin_lat_e7`. */
int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m);
#endif /* SIM_DRAG_POS_H */
```

`components/drivers/sim_common/host/sim_drag_pos.c`:
```c
/* sim_drag_pos.c -- see include/sim_drag_pos.h. Pure, IDF-free (built into the sim_common
 * component for the target and linked directly by test/test_sim_profile.c on the host). */
#include "sim_drag_pos.h"
#include <assert.h>

int32_t sim_drag_lat_e7(int32_t origin_lat_e7, double dist_m)
{
    assert(dist_m >= 0.0);                 /* sim_profile.h's own contract on sim_drag_state_t */
    return origin_lat_e7 + (int32_t)(dist_m * 1e7 / SIM_DEG_LAT_M);
}
```
Add `"host/sim_drag_pos.c"` to `SRCS` in `components/drivers/sim_common/CMakeLists.txt:12` and extend that file's header comment with one sentence naming the new source (the file documents each of its sources).

- [ ] **Step 4: Run the test to pass**

```bash
cmake --build test/build -j8 && ./test/build/test_sim_profile
```
Expected: 4 tests, 0 failures (the two existing cases plus the two new ones).

- [ ] **Step 5: Fix the driver (R29 + `drv:L23`)** — in `components/drivers/gps_sim/gps_sim.c`:

1. After the `s_last_delivered_gps_us` declaration, add the latched origin:
```c
/* R29 (drv:H2): the DRAG run's position origin, latched ONCE per run by gps_sim_rearm() (and by
 * gps_init/gps_configure) -- never s_last_fix, which deliver_drag() itself advances every frame.
 * 12 B: only the three fields a due-north straight line needs. */
static struct { int32_t lat_e7; int32_t lon_e7; int32_t alt_mm; } s_drag_origin;
```
2. Add, next to `remember_last_fix`:
```c
/* Latch the current position as the next DRAG run's origin (R29). Called from gps_init(),
 * gps_configure() and gps_sim_rearm() -- every path that starts a scenario -- so a `dbg sim drag`
 * run's origin is exactly where the vehicle was when the command landed, and stays there for the
 * whole run. */
static void latch_drag_origin(void)
{
    s_drag_origin.lat_e7 = s_last_fix.lat_e7;
    s_drag_origin.lon_e7 = s_last_fix.lon_e7;
    s_drag_origin.alt_mm = s_last_fix.alt_mm;
}
```
3. `gps_init()`: call `latch_drag_origin();` immediately after `reset_last_fix_to_start();`.
4. `gps_configure()`: add, before the counter resets, the two lines `drv:L23` asks for — and extend its `(void)rate_hz` comment to say why:
```c
    /* drv:L23: resetting s_idx/s_started without re-anchoring the clock reintroduces the exact
     * gps_us rewind C1/F-1 fixed for gps_sim_rearm() -- the pipeline's §6.5 "last valid"
     * watermark is never reset, so a rewound fix is invalid for as long as the rewind lasts.
     * Re-anchor the same way gps_sim_rearm() does, and latch the drag origin with it (R29). */
    sim_clock_rearm(&s_clock, s_last_delivered_gps_us, SIM_DRAG_PERIOD_US);
    latch_drag_origin();
```
5. `gps_sim_rearm()`: add `latch_drag_origin();` directly after the `sim_clock_rearm(...)` call, and extend the function's doc comment with: *"and latches the DRAG run's position origin (R29): `deliver_drag()` reads `s_drag_origin`, not `s_last_fix`, so the origin cannot move under the run."*
6. `deliver_drag()`: replace the three `s_last_fix` reads and the latitude arithmetic:
```c
    const int32_t lat0 = s_drag_origin.lat_e7, lon0 = s_drag_origin.lon_e7,
                  alt0 = s_drag_origin.alt_mm;
    ...
    out->lat_e7     = sim_drag_lat_e7(lat0, s.dist_m);
```
   Keep the `remember_last_fix(out)` call at the end exactly as it is (`dbg sim park` after a drag run must still freeze at the run's end position — fix round 1, I2).
7. Rewrite the false claim (`drv` false claim 1) in `deliver_drag()`'s doc comment — the last paragraph of it, `gps_sim.c:317-322` — to:
```c
 * Final review C1/F-1 + R29 (drv:H2): the position origin is s_drag_origin -- s_last_fix as it
 * stood when gps_sim_rearm() latched it, i.e. the last fix any scenario had delivered when the
 * switch landed -- and it does NOT move for the rest of the run. At t=0 (dist_m == 0.0,
 * sim_profile.h's contract) the first drag fix is byte-identical in position to the last delivered
 * one (zero jump), and every later frame steps by dist(t) - dist(t-200ms) <= 12.4 m, inside the
 * §6.5 jump filter's 70 m. Reading s_last_fix here instead made the origin move every frame via
 * remember_last_fix() below, so each step equalled the whole cumulative distance. gps_us is
 * sim_clock_drag_us() (base_us + t), so the switch is continuous in time too.
```
   Also add one sentence to the file header's "Fix round 1 / Final review" block: *"R29: `deliver_drag()` reads the latched `s_drag_origin`; the file header's claim that a switch is 'continuous in position' is true of the first frame AND of every later step only because of that latch."*

- [ ] **Step 6: Verify** — the host test already passes; now prove the firmware side:

```bash
source tools/idf-env.sh
./build.sh moto_sim build && ./build.sh moto_neo6m build        # rc 0, 0 warnings; report free DRAM
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
ctest --test-dir test/build                                     # 47/47 (same count: no new suite)
idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build   # rc 0, 0 warnings
grep -n "s_last_fix" components/drivers/gps_sim/gps_sim.c       # no hit inside deliver_drag's body
```
Expected: `moto_sim` free DRAM within 32 B of 4192 B (the 12 B struct plus alignment).

- [ ] **Step 7: Commit**

```bash
git add components/drivers/sim_common components/drivers/gps_sim/gps_sim.c test/test_sim_profile.c test/CMakeLists.txt
git commit   # message below + the two-line trailer
```
```
fix(gps_sim): latch the drag run's position origin once per run; re-anchor the sim clock on gps_configure (F3 T1, drv:H2/L23)

Every `dbg sim drag` frame past ~104 km/h failed the §6.5 jump filter because the
origin was s_last_fix, which deliver_drag() itself advanced each frame: the step
equalled the cumulative distance (reproducer: 690 m steps, 49496 m total vs 690 m).
The origin is now latched in gps_sim_rearm()/gps_init()/gps_configure(); the step
arithmetic moved to sim_common/host/sim_drag_pos.c and is host-tested (max step
12.4 m < 70 m, total 690 m). gps_configure() now re-anchors s_clock too (L23).
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 1 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 2: linkhost pure protocol — download caps, progress, demux `len` sanity (`dk:M1` arithmetic, `dk:L3`, `dk:L8`) — GROUP B, SERIAL (first)

**Evidence:** `dev-controller.md` M1, L3, L8; reproducer `dl.c`, re-run 2026-10-13: a corrupted header `---BEGIN sessions 999999999---` parses, `state=LH_DL_BODY`, scaled cap **36.2 h**, and `lh_dl_gap_ms` returns the 3 s steady-state gap that *every received byte* then refreshes.

**Files:**
- Create: `devcontroller/test/test_linkhost_dl_cap.c`
- Modify: `devcontroller/components/linkhost/include/linkhost_proto.h`, `host/linkhost_proto.c`, `host/bridge_filter.c`, `devcontroller/test/test_linkhost_demux.c`, `devcontroller/test/test_bridge_filter.c`

**Interfaces:**
- Consumes: `lh_dl_ctx_t` (its `state`/`body_size`/`body_got` fields), `LT_REC_MAX`, `LT_REC_STATUS`, `LT_STATUS_LEN` (all `linkhost_proto.h` / `app/lt_proto.h`).
- Produces, all in `linkhost_proto.h`:
  - `#define LH_DL_BODY_MAX (2u * 1024u * 1024u)` — largest announced body accepted.
  - `#define LH_DL_HARD_MAX_MS (20 * 60 * 1000)` — absolute ceiling on one download.
  - `#define LH_DL_MARGIN_US (5 * 1000 * 1000)` — slack on top of the size-scaled transfer time (moved here from `linkhost.c`'s `DL_HARD_MARGIN_US`, which Task 3 deletes).
  - `int64_t lh_dl_cap_us(uint32_t body_size, int64_t t_header_us, uint32_t baud);`
  - `uint32_t lh_dl_progress(const lh_dl_ctx_t *c);`
  - `void linkhost_demux_resync(void);` (Task 3's `linkhost_bridge_end` calls it)

  Unit note: the design sketches this as `lh_dl_cap_ms(size, t_header)`. It is written in **microseconds** (`_us`) so the call site can compare it directly against `esp_timer_get_time()` with no conversion — the mixed-unit version was where `dk:M1`'s arithmetic went wrong in the first place.

- [ ] **Step 1: Write the failing tests** — `devcontroller/test/test_linkhost_dl_cap.c` (new file; the CMake glob picks it up automatically):

```c
/* devcontroller/test/test_linkhost_dl_cap.c -- host tests for the streaming download's BOUNDS
 * (dk:M1): the announced-size cap, the absolute cap arithmetic, and the progress counter the IDF
 * loop's idle timer is allowed to trust. Pure: no UART, no IDF. */
#include "unity.h"
#include "linkhost_proto.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static int sink_count;
static int sink_cb(void *ctx, const uint8_t *d, size_t n) { (void)ctx; (void)d; (void)n; sink_count++; return 0; }

/* dk:M1 -- the reviewer's dl.c header: a 9-digit size parses today and scales the cap to 36 h.
 * It must now be rejected outright, in the same feed call, as a protocol error. */
static void test_oversize_header_is_rejected(void)
{
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    const char *hdr = "---BEGIN sessions 999999999---\n";
    lh_dl_state_t st = lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr));
    TEST_ASSERT_EQUAL_INT(LH_DL_ERR, st);
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_PROTO, lh_dl_result(&c));
}

/* Exactly LH_DL_BODY_MAX is accepted (the boundary is inclusive); one byte more is not. */
static void test_body_size_boundary(void)
{
    char hdr[64];
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    snprintf(hdr, sizeof hdr, "---BEGIN log %u---\n", (unsigned)LH_DL_BODY_MAX);
    TEST_ASSERT_EQUAL_INT(LH_DL_BODY, lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr)));
    lh_dl_init(&c, false, sink_cb, NULL);
    snprintf(hdr, sizeof hdr, "---BEGIN log %u---\n", (unsigned)LH_DL_BODY_MAX + 1u);
    TEST_ASSERT_EQUAL_INT(LH_DL_ERR, lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr)));
}

/* The cap: t_header + 1.5 * size*10/baud + LH_DL_MARGIN_US, never more than
 * t_header + LH_DL_HARD_MAX_MS, and never less than t_header + the margin. */
static void test_cap_arithmetic(void)
{
    TEST_ASSERT_EQUAL_INT64(1000 + LH_DL_MARGIN_US, lh_dl_cap_us(0, 1000, 115200));
    /* a 46 KB .log: 46*1024*10/115200 = 40.9 s; *1.5 = 61.3 s; + 5 s = 66.3 s */
    int64_t cap = lh_dl_cap_us(46u * 1024u, 0, 115200);
    TEST_ASSERT_TRUE(cap > 65000000 && cap < 68000000);
    /* 2 MiB at 115200 = 182 s; *1.5 + 5 s = 278 s -- comfortably under the 20 min ceiling */
    cap = lh_dl_cap_us(LH_DL_BODY_MAX, 0, 115200);
    TEST_ASSERT_TRUE(cap > 275000000 && cap < 285000000);
    /* the ceiling binds at a low baud: 2 MiB at 9600 would be 36 min */
    TEST_ASSERT_EQUAL_INT64((int64_t)LH_DL_HARD_MAX_MS * 1000, lh_dl_cap_us(LH_DL_BODY_MAX, 0, 9600));
    /* an impossible size can never widen the window either (belt and braces to the header check) */
    TEST_ASSERT_EQUAL_INT64((int64_t)LH_DL_HARD_MAX_MS * 1000, lh_dl_cap_us(0xFFFFFFFFu, 0, 115200));
}

/* dk:M1's second half -- the idle timer must be fed ONLY by body bytes before ---END. Noise in
 * the header phase (a prompt, a log line, the lap-timer's resumed 0xFF stream) leaves the counter
 * at 0; the trailer freezes it. */
static void test_progress_counts_only_body_bytes(void)
{
    lh_dl_ctx_t c;
    lh_dl_init(&c, false, sink_cb, NULL);
    const char *noise = "laptimer> \r\nI (12) x: hi\r\n";
    lh_dl_feed(&c, (const uint8_t *)noise, strlen(noise));
    TEST_ASSERT_EQUAL_UINT32(0, lh_dl_progress(&c));
    const char *hdr = "---BEGIN status 4---\r\n";
    lh_dl_feed(&c, (const uint8_t *)hdr, strlen(hdr));
    TEST_ASSERT_EQUAL_UINT32(0, lh_dl_progress(&c));
    lh_dl_feed(&c, (const uint8_t *)"ab", 2);
    TEST_ASSERT_EQUAL_UINT32(2, lh_dl_progress(&c));
    lh_dl_feed(&c, (const uint8_t *)"cd", 2);                  /* body complete -> LH_DL_TAIL */
    uint32_t frozen = lh_dl_progress(&c);
    TEST_ASSERT_EQUAL_UINT32(4, frozen);
    lh_dl_feed(&c, (const uint8_t *)"\r\n", 2);                /* trailer bytes: no progress */
    TEST_ASSERT_EQUAL_UINT32(frozen, lh_dl_progress(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_oversize_header_is_rejected);
    RUN_TEST(test_body_size_boundary);
    RUN_TEST(test_cap_arithmetic);
    RUN_TEST(test_progress_counts_only_body_bytes);
    return UNITY_END();
}
```

Add to `devcontroller/test/test_linkhost_demux.c` (mirror the file's existing helper style — it feeds bytes through `linkhost_feed` and pops with `linkhost_stream_pop`; call `linkhost_reset()` in each case as the existing ones do):

```c
/* dk:L3 -- the demux applied NO len sanity check, so a corrupt len = 255 swallowed 255 following
 * bytes (possibly a whole genuine frame) before resyncing. The largest legal len is
 * LT_REC_MAX + 1 (type byte + LT_REC_MAX payload, stream_emit's own datalen = len - 1 bound). */
static void test_impossible_stream_len_does_not_swallow_the_next_frame(void)
{
    linkhost_reset();
    uint8_t bad[5] = { 0xFF, 1, 0, 0, 255 };                /* len 255: impossible */
    linkhost_feed(bad, sizeof bad);
    uint8_t good[5 + 2] = { 0xFF, 2, 0, 0, 2, LT_SES_T_EVENT, 7 };
    linkhost_feed(good, sizeof good);
    lt_stream_rec_t r;
    TEST_ASSERT_EQUAL_INT(0, linkhost_stream_pop(&r));        /* the real frame survived */
    TEST_ASSERT_EQUAL_UINT8(LT_SES_T_EVENT, r.type);
    TEST_ASSERT_EQUAL_UINT16(2, r.seq);
}

/* dk:L8 -- a single corrupted byte inside a base64 body can synthesize a 0xFF "frame" whose type
 * byte is a legal base64 character ('@' == 0x40 == LT_REC_STATUS); nothing checked its length, so
 * linkstats cached 20 bytes of body garbage as a fresh STATUS and /api/status reported fabricated
 * battery/free-space/fw with connected:true. A STATUS record must now carry exactly
 * LT_STATUS_LEN payload bytes or be dropped. */
static void test_status_record_of_the_wrong_length_is_dropped(void)
{
    linkhost_reset();
    uint8_t rec[5 + 1 + 8] = { 0xFF, 1, 0, 0, 9, LT_REC_STATUS };   /* 8 payload bytes, not 20 */
    linkhost_feed(rec, sizeof rec);
    lt_stream_rec_t r;
    TEST_ASSERT_NOT_EQUAL(0, linkhost_stream_pop(&r));               /* nothing emitted */
}
```

Add to `devcontroller/test/test_bridge_filter.c`:
```c
/* dk:L3 -- the bridge rejected len == LT_REC_MAX + 1 as "bad" while the demux accepts it, so a
 * future 64-byte-payload record would have its 65 payload bytes forwarded RAW into the operator's
 * terminal -- the exact injection this filter exists to prevent. */
static void test_max_length_frame_is_filtered_not_forwarded(void)
{
    bridge_filter_t f;
    bridge_filter_reset(&f);
    uint8_t in[5 + LT_REC_MAX + 1];
    memset(in, 'X', sizeof in);
    in[0] = 0xFF; in[1] = 1; in[2] = 0; in[3] = 0; in[4] = (uint8_t)(LT_REC_MAX + 1);
    uint8_t out[sizeof in];
    size_t w = bridge_filter_run(&f, in, sizeof in, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(0, w);
    TEST_ASSERT_EQUAL_UINT32(1, f.frames);
    TEST_ASSERT_EQUAL_UINT32(0, f.bad);
    /* one byte longer is still impossible and still counted bad */
    bridge_filter_reset(&f);
    in[4] = (uint8_t)(LT_REC_MAX + 2);
    (void)bridge_filter_run(&f, in, 5, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(1, f.bad);
}
```
Register all three new cases with `RUN_TEST` in their files' `main`.

- [ ] **Step 2: Run to fail**

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
cmake -S devcontroller/test -B devcontroller/test/build -DCMAKE_BUILD_TYPE=Debug \
  && cmake --build devcontroller/test/build -j8 2>&1 | tail -5
```
Expected: FAIL to compile — `LH_DL_BODY_MAX`/`lh_dl_cap_us`/`lh_dl_progress` undeclared. After Step 3's header lands but before Step 4, `test_linkhost_dl_cap` links and the three behavioural cases fail.

- [ ] **Step 3: Declare the new interface** — add the three macros and three prototypes listed above to `linkhost_proto.h`, immediately after `lh_dl_gap_ms`'s declaration, each with the doc comment the header's style requires:

```c
/* Largest announced body this parser accepts (dk:M1). A ---BEGIN whose size exceeds it cannot be
 * a real session file on a 4 MB device and is far more likely a digit inserted into the size by a
 * glitchy link -- the exact corruption this module exists to tolerate. The header is rejected
 * (LH_DL_ERR / LINKHOST_E_PROTO) rather than streamed, because accepting it hands the caller's
 * timeout loop a window scaled off a number the wire made up (measured: 36.2 h). */
#define LH_DL_BODY_MAX     (2u * 1024u * 1024u)
/* Absolute ceiling on one download, from the moment the header was seen (dk:M1). Independent of
 * the announced size, so it still holds if the size cap above is ever widened. */
#define LH_DL_HARD_MAX_MS  (20 * 60 * 1000)
/* Slack added on top of the size-scaled transfer time (was linkhost.c's DL_HARD_MARGIN_US). */
#define LH_DL_MARGIN_US    (5 * 1000 * 1000)
/* Absolute deadline (same clock as t_header_us, microseconds) for a body of `body_size` on-wire
 * bytes at `baud`, measured from when the header was SEEN (not from the request): a slow `list`
 * header can arrive seconds into the call, and measuring from the request would clip the window by
 * however long the header took. = t_header + 1.5*size*10/baud + LH_DL_MARGIN_US, clamped to
 * t_header + LH_DL_HARD_MAX_MS; `body_size` is itself clamped to LH_DL_BODY_MAX first. Pure, so
 * the policy is host-testable without the UART loop. */
int64_t lh_dl_cap_us(uint32_t body_size, int64_t t_header_us, uint32_t baud);
/* On-wire BODY bytes consumed so far: 0 while the header is still being scanned, rising through
 * LH_DL_BODY, frozen at body_size once the trailer starts. The IDF loop's idle deadline may be
 * refreshed only when this ADVANCES (dk:M1): bytes the parser discards -- prompt/echo/log noise,
 * or the lap-timer's resumed 10 Hz 0xFF stream after a mis-parsed header -- must not keep a
 * download (and the link mutex) alive, which is exactly what "reset the deadline on every received
 * byte" did. */
uint32_t lh_dl_progress(const lh_dl_ctx_t *c);
/* Resets the demux state machine to DX_SCAN with its line/frame/stream-header accumulators
 * cleared, WITHOUT touching the record ring, the ring high-water, or the raw capture (unlike
 * linkhost_reset(), which clears everything). Called by linkhost_bridge_end (dk:M6): after a raw
 * bridge session the demux would otherwise resume mid-byte-stream in whatever state the bridge's
 * own reads left it. */
void linkhost_demux_resync(void);
```

- [ ] **Step 4: Implement** — in `host/linkhost_proto.c`:

1. `dl_feed_hdr`, inside the `dl_parse_begin` success branch, before `c->body_size = size`:
```c
        if (size > LH_DL_BODY_MAX) { dl_fail(c, LINKHOST_E_PROTO); return; }   /* dk:M1 */
```
2. New functions beside `lh_dl_gap_ms`:
```c
uint32_t lh_dl_progress(const lh_dl_ctx_t *c)
{
    assert(c != NULL);
    return c->body_got;            /* 0 in LH_DL_HDR; frozen at body_size from LH_DL_TAIL on */
}

int64_t lh_dl_cap_us(uint32_t body_size, int64_t t_header_us, uint32_t baud)
{
    assert(baud > 0);
    if (t_header_us < 0) t_header_us = 0;
    uint32_t sz = (body_size > LH_DL_BODY_MAX) ? LH_DL_BODY_MAX : body_size;
    int64_t xfer_us = (int64_t)sz * 10 * 1000000 / (int64_t)baud;   /* 8N1: 10 bits per byte */
    int64_t cap  = t_header_us + xfer_us + xfer_us / 2 + LH_DL_MARGIN_US;
    int64_t hard = t_header_us + (int64_t)LH_DL_HARD_MAX_MS * 1000;
    return (cap > hard) ? hard : cap;
}
```
3. `linkhost_feed`'s `DX_STREAM_HDR` case, replacing the body of the `sh_got >= 5` branch (`dk:L3`):
```c
            if (s_dx.sh_got >= 5) {
                /* dk:L3: a len that cannot be a real record (stream_emit's datalen = len - 1 is
                 * bounded by LT_REC_MAX) means those five bytes were not a frame header at all --
                 * drop them and resync on the next byte instead of swallowing up to 255 more. */
                if (s_dx.sh[4] > (uint8_t)(LT_REC_MAX + 1u)) { s_dx.state = DX_SCAN; break; }
                s_dx.sp_need = s_dx.sh[4];
                s_dx.sp_got = 0;
                if (s_dx.sp_need == 0) { stream_emit(); s_dx.state = DX_SCAN; }
                else s_dx.state = DX_STREAM_BODY;
            }
```
4. `stream_emit`, after the existing `stream_type_known`/`datalen` guard (`dk:L8`):
```c
    /* dk:L8: STATUS is a FIXED-length record (§18.2). A 0xFF byte corrupting a base64 body can
     * synthesize a header whose type byte is a legal base64 char ('@' == LT_REC_STATUS); without a
     * length check, 20 bytes of body garbage reached linkstats' STATUS cache and /api/status
     * reported fabricated battery/free-space/fw as connected:true. */
    if (type == LT_REC_STATUS && datalen != LT_STATUS_LEN) return;
```
5. New function beside `linkhost_reset` (which keeps its `memset` of everything):
```c
/* Every PARSER field of s_dx (its struct is at linkhost_proto.c:650-680), and nothing else: the
 * line/frame/sh/sp byte arrays need no clearing because their *_len/*_got counters gate every read
 * of them, and the ring (ring/ring_head/ring_tail/ring_hw) plus the rawcap are deliberately
 * untouched -- a bridge session must not lose the stream records already queued for
 * stream_consumer, nor the high-water figure `stream stats` reports. resp_ready is cleared because
 * a half-parsed or unpopped response from before the bridge is stale by definition (the bridge
 * held s_req_mtx, so no caller is waiting on one). */
void linkhost_demux_resync(void)
{
    s_dx.state       = DX_SCAN;
    s_dx.line_len    = 0;
    s_dx.frame_len   = 0;
    s_dx.body_need   = 0;
    s_dx.body_got    = 0;
    s_dx.tail_off    = 0;
    s_dx.sh_got      = 0;
    s_dx.sp_need     = 0;
    s_dx.sp_got      = 0;
    s_dx.resp_status = 0;
    s_dx.resp_ready  = false;
}
```

6. `host/bridge_filter.c:53`, the `BF_HDR` len check (`dk:L3`):
```c
                if (b > (uint8_t)(LT_REC_MAX + 1u)) {
                    f->bad++;                    /* not a real frame -- resync on the next byte */
                    f->st = BF_TEXT;
```
   and update `bridge_filter.h`'s doc sentence "A header whose len byte exceeds LT_REC_MAX" to "exceeds `LT_REC_MAX + 1` (the demux's own largest legal len: one type byte plus `LT_REC_MAX` payload)".

- [ ] **Step 5: Run to pass + no regressions**

```bash
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build --output-on-failure
```
Expected: 29/29 (28 existing + `test_linkhost_dl_cap`), 100 %. Then re-run the reviewer's reproducer against the fixed parser:
```bash
cc -I devcontroller/components/linkhost/include -I components/app/include -o /tmp/dl \
  .superpowers/reviews/2026-10-13/dl.c devcontroller/components/linkhost/host/linkhost_proto.c && /tmp/dl
```
Expected: `state=4` (`LH_DL_ERR`) and `result=-4` (`LINKHOST_E_PROTO`) on the very first feed, instead of `state=1 body_size=999999999` and a 36.2 h cap.

- [ ] **Step 6: Commit**

```bash
git add devcontroller/components/linkhost devcontroller/test/test_linkhost_dl_cap.c \
        devcontroller/test/test_linkhost_demux.c devcontroller/test/test_bridge_filter.c
git commit
```
```
fix(devcontroller/linkhost): bound the announced download size and the absolute cap, count only body bytes as progress, sanity-check stream len (F3 T2, dk:M1/L3/L8)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 2 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 3: linkhost IDF glue — bounded takes, drains, timeouts, OTA pacing (`dk:M1` wiring, `M2`, `M6`, `M11`, `L6`, `H2` mitigation, false claims 7–8) — GROUP B, SERIAL (after Task 2)

**Evidence:** `dev-controller.md` M1, M2, M6, M11, L6, H2, false claims 7 and 8; `drivers-build-tools.md` M8; design R30 and §5 (#57 stays filed).

**Files:**
- Modify: `devcontroller/components/linkhost/linkhost.c`, `devcontroller/components/linkhost/include/linkhost.h`, `devcontroller/components/linkhost/include/linkhost_proto.h` (one comment)

**Interfaces:**
- Consumes: `lh_dl_cap_us`, `lh_dl_progress`, `linkhost_demux_resync`, `LH_DL_MARGIN_US` (Task 2); existing `rx_park_and_drain()`, `LINK_REQ_MTX_MS`, `RX_DRAIN_CAP`, `DC_LINK_BAUD`.
- Produces: no new public API. `linkhost_download_cmd` may now return `LINKHOST_E_BUSY` — `webapi.c`'s `send_stream_error` (`:168-181`) and `cmd_lt.c`'s `lt_run_download` already handle that code (503 `{"error":"link busy, retry"}` / `rc -6`), so no caller changes.

- [ ] **Step 1: No host harness exists for this file — state the verification up front.** `linkhost.c` is IDF glue (UART, FreeRTOS, `esp_partition`); the dev-kit harness deliberately covers only `host/*.c`. Every behaviour below is therefore proven by (a) Task 2's pure tests for the arithmetic it now calls, (b) the compile + `grep` checks in Step 7, and (c) the bench items Task 11 appends. Do not add a fake-UART harness for this one file.

- [ ] **Step 2: `dk:M1` wiring — the idle timer may only be refreshed by progress.** In `linkhost_download_cmd`, delete the `#define DL_HARD_MARGIN_US` line from the tunables block (now `LH_DL_MARGIN_US` in `linkhost_proto.h`) and replace the loop's bookkeeping:

```c
    uint32_t      prog = 0;              /* lh_dl_progress at the last idle-deadline refresh */
    lh_dl_state_t seen = dl.state;       /* parser state at the last idle-deadline refresh */
    lh_dl_state_t st   = dl.state;
    while (st < LH_DL_DONE) {                     /* bounded by hard_dl / idle_dl */
        int64_t t = esp_timer_get_time();
        if (t >= hard_dl || t >= idle_dl) break;                    /* timeout -> incomplete */
        int n = uart_read_bytes(DC_LINK_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            st = lh_dl_feed(&dl, buf, (size_t)n);
            /* dk:M1: refresh the no-bytes deadline only on REAL progress -- the parser advancing a
             * state, or consuming body bytes before ---END. Refreshing it on every received byte
             * (as this did) means the lap-timer's own resumed 10 Hz 0xFF stream keeps the deadline
             * alive forever while s_req_mtx is held: the reproducer's garbled BEGIN held the link
             * for the full scaled cap with /api/config, `lt *` and `flash push` all answering BUSY.
             * Evaluated AFTER the feed so a read that completes the header gets the shorter
             * steady-state gap immediately, not one more DL_FIRST_TMO_MS window. */
            uint32_t p = lh_dl_progress(&dl);
            if (p != prog || dl.state != seen) {
                prog = p;
                seen = dl.state;
                idle_dl = esp_timer_get_time() +
                          (int64_t)lh_dl_gap_ms(dl.state, DL_FIRST_TMO_MS, DL_IDLE_TMO_MS) * 1000;
            }
        }
        ...
```
and replace the size-scaling block with the pure call:
```c
        if (!scaled && dl.state >= LH_DL_BODY && dl.body_size > 0) {
            int64_t cap = lh_dl_cap_us(dl.body_size, t_header, DC_LINK_BAUD);   /* dk:M1 */
            if (cap > hard_dl) hard_dl = cap;
            scaled = true;
        }
```
Keep the `DL_FIRST_TMO_MS`/`DL_HARD_MIN_MS` floor exactly as it is, and shorten its comment to point at `lh_dl_cap_us` for the scaled half. The header-phase consequence is intended and must be written down next to `DL_FIRST_TMO_MS`: *"with dk:M1's progress rule, the 12 s first-byte allowance is now measured from the request and is not extended by noise — which is what this constant always claimed ('no bytes yet')."*

- [ ] **Step 3: `dk:M2` — bounded take on the download path.** Replace `xSemaphoreTake(s_req_mtx, portMAX_DELAY);` in `linkhost_download_cmd` with:
```c
    /* dk:M2: spec §7 says a busy link answers BUSY via the BOUNDED take -> 503. The old
     * portMAX_DELAY here (justified as "worker task: OK to wait") parked the single async_worker
     * behind a `lt shell` bridge for up to its own 10-minute cap with the browser socket open and
     * the SPA's spinner never resolving, and then answered later jobs "download queue full" --
     * the wrong diagnosis. Same bound as linkhost_cmd_timed/linkhost_bridge_begin. */
    if (xSemaphoreTake(s_req_mtx, pdMS_TO_TICKS(LINK_REQ_MTX_MS)) != pdTRUE) return LINKHOST_E_BUSY;
```

- [ ] **Step 4: `dk:M6` — drain and resync on bridge exit.**
```c
void linkhost_bridge_end(void)
{
    /* dk:M6 + spec §5.2/§6/§7 ("resyncs on exit", "drains-to-quiet on exit"): this used to only
     * clear s_rx_paused and give the mutex back, so the demux resumed mid-byte-stream in whatever
     * state the bridge's own reads left behind (it recovered on the next '\n' or 0xFF, but the
     * specified handshake was simply absent). s_rx_paused is still true here and rx_task is still
     * parked, so this is the only UART1 reader -- rx_park_and_drain's documented precondition. */
    rx_park_and_drain();            /* bounded: RX_PARK_TMO_MS + RX_DRAIN_CAP * RX_DRAIN_TMO_MS */
    linkhost_demux_resync();        /* parser state only; the record ring and linkstats survive */
    s_rx_paused = false;
    xSemaphoreGive(s_req_mtx);
}
```

- [ ] **Step 5: `dk:M11` + the `dk:H2`/`drv:M8` pacing mitigation.** In the tunables block:
```c
#define OTA_READY_TMO_MS   15000     /* dk:M11: OTA-READY is printed only AFTER the lap-timer's
                                      * ota_begin -> esp_ota_begin(s_target, size, ...), which
                                      * erases ceil(size/4096) sectors of the target slot: ~147
                                      * sectors for a 600 KB image at ~25-40 ms worst case each =
                                      * up to ~5.9 s, before any of ota_begin's own checks. The old
                                      * 3000 ms left no margin at all -- a slow erase returned
                                      * LINKHOST_E_TIMEOUT ("flash result link -1") while the
                                      * lap-timer then sat waiting for image bytes and aborted 9 s
                                      * later. 15 s is ~2.5x the measured worst case and still far
                                      * below the operator-visible OTA_DONE_TMO_MS path. */
#define OTA_PACE_BYTES     4096u     /* the lap-timer's own OTA_RECV_CHUNK (export_serial.c:995) */
#define OTA_PACE_MS        25        /* dk:H2/drv:M8 MITIGATION, not the fix (#57 is). The
                                      * lap-timer's console RX ring is a fixed 256 B (IDF
                                      * esp_console, no Kconfig) = ~22 ms at 115200, and it blocks
                                      * in esp_ota_write for ~10-40 ms once per 4096 B chunk; a
                                      * worst-case write therefore overruns the ring by ~18 ms of
                                      * wire time (~200 B) and the image is corrupted (it fails
                                      * SAFE: SHA-256 + ECDSA gate esp_ota_end). Host and device
                                      * count the same bytes from 0, so pausing 25 ms at each
                                      * OTA_PACE_BYTES boundary -- the moment the device is in that
                                      * write -- covers the deficit with 7 ms of margin. Cost: 147
                                      * pauses on a 600 KB image = 3.7 s on a ~55 s push (+6.7 %).
                                      * Far below the lap-timer's own 3 s per-read stall window
                                      * (OTA_RECV_READ_MS), so it can never look like a stall. No
                                      * feedback, no retry: #57 (per-chunk ACK) remains the fix. */
```
and in `linkhost_flash`'s streaming loop, directly after `sent += want;`:
```c
            if ((sent % OTA_PACE_BYTES) == 0u && sent < size) vTaskDelay(pdMS_TO_TICKS(OTA_PACE_MS));
```
Then correct the lap-timer-side false claim's dev-kit half by name: `linkhost_flash`'s own doc comment gains *"Paced at the lap-timer's 4096 B chunk boundary (OTA_PACE_MS) — a mitigation for its fixed 256 B console RX ring; the per-chunk ACK handshake `export_serial.c` assigns to 'the dev-controller integration step' is issue #57 and is NOT implemented here."* (The matching comment in `components/drivers/export_serial/export_serial.c:1080-1084` belongs to **F2a** — Task 11 Step 4 hands it the exact replacement sentence.)

- [ ] **Step 6: `dk:L6` + false claim 8 — a tear-free activity stamp and honest heartbeat wording.** Replace `static volatile int64_t s_last_activity_us;` with:
```c
/* dk:L6: a 64-bit read on a 32-bit core is not atomic -- rx_task writes this while the httpd task
 * reads it in linkhost_peer_present, so the old int64 µs stamp could tear (worst case: one wrong
 * fast-fail/retry decision). A 32-bit millisecond stamp is read and written atomically; the
 * unsigned difference below is wrap-correct, so the 49.7-day rollover needs no special case. */
static volatile uint32_t s_last_activity_ms;
static volatile bool     s_activity_seen;
```
`rx_task`, on `n > 0`: `s_last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000); s_activity_seen = true;`. `linkhost_peer_present()`:
```c
bool linkhost_peer_present(void)
{
    if (!s_activity_seen) return false;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (uint32_t)(now_ms - s_last_activity_ms) < LINK_PRESENT_MS;
}
```
with `#define LINK_PRESENT_MS 3000` replacing `LINK_PRESENT_US` (update the one other reference, the absent-path check in `linkhost_cmd_timed`, to use `linkhost_peer_present()` or the ms form — read the site and keep its logic identical). Fix the two stale claims (false claim 8): `linkhost.h:76-77` "link activity (a `status` heartbeat / stream frame)" → *"True when ANY UART1 byte has been seen within LINK_PRESENT_MS. Plan 5.6 §4.2 deleted the status heartbeat (it never touched UART1) — the signal is raw link traffic, not a heartbeat."*; `linkhost_proto.h:33` `LINKHOST_E_NOTCONN` "no heartbeat / not yet wired" → *"no UART1 traffic within LINK_PRESENT_MS / not yet wired"*.

- [ ] **Step 7: Verify**

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..   # rc 0, 0 warnings
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build   # 29/29
grep -n "portMAX_DELAY" devcontroller/components/linkhost/linkhost.c                      # no hits
grep -n "s_last_activity_us\|DL_HARD_MARGIN_US\|LINK_PRESENT_US" devcontroller/components  # no hits
grep -rn "heartbeat" devcontroller/components/linkhost/include                             # only the corrected sentences
```

- [ ] **Step 8: Commit**

```bash
git add devcontroller/components/linkhost
git commit
```
```
fix(devcontroller/linkhost): bounded download take, progress-fed idle timer, bridge-exit drain+resync, 15 s OTA-READY, 25 ms OTA pacing, atomic activity stamp (F3 T3, dk:M1/M2/M6/M11/L6 + H2 mitigation)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 3 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 4: webapi + SPA — SSE guard, path guard, 400 vs 413, honest `/api/logs` (`dk:M7`, `L1`, `L2`, `L4`, `L5`, false claims 1 and 9) — GROUP C, PARALLEL

**Evidence:** `dev-controller.md` M7, L1, L2, L4, L5, false claims 1 and 9.

**Files:**
- Create: `devcontroller/components/webapi/include/sse_cursor.h`, `host/sse_cursor.c`, `include/www_path.h`, `host/www_path.c`, `devcontroller/test/test_sse_cursor.c`, `devcontroller/test/test_www_path.c`
- Modify: `devcontroller/components/webapi/CMakeLists.txt` (add both `host/*.c` to `SRCS`), `host/config_diff.c`, `include/config_diff.h`, `webapi.c`, `devcontroller/test/test_config_diff.c`, `devcontroller/web/app.js`, `devcontroller/web/index.html`

**Interfaces:**
- Produces: `uint32_t sse_cursor_fix(uint32_t head, uint32_t cursor, uint32_t cap);`, `bool www_path_ok(const char *uri);`, and in `config_diff.h` `#define CFG_DIFF_E_SIZE (-1)` / `#define CFG_DIFF_E_PARSE (-2)`.
- Consumes: `BC_CAP` (64, `webapi.c:84`), `logstore_list`/`logstore_entry_t`/`LOGSTORE_ROT_MAX_FILES`.

- [ ] **Step 1: Write the failing tests**

`devcontroller/test/test_sse_cursor.c`:
```c
/* dk:M7 -- the SSE overrun guard was `> BC_CAP`, so a cursor at distance EXACTLY cap (whose slot
 * index is congruent to the head's: the slot the producer writes next) was not skipped, which is
 * precisely the "guaranteed torn read" its own comment described. */
#include "unity.h"
#include "sse_cursor.h"
void setUp(void) {}
void tearDown(void) {}
static void test_cursor_at_exactly_cap_is_skipped(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 64u + 2u, sse_cursor_fix(1000u, 1000u - 64u, 64u));
}
static void test_cursor_inside_the_window_is_untouched(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 63u, sse_cursor_fix(1000u, 1000u - 63u, 64u));
    TEST_ASSERT_EQUAL_UINT32(1000u, sse_cursor_fix(1000u, 1000u, 64u));
}
static void test_far_overrun_and_wraparound(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u - 64u + 2u, sse_cursor_fix(1000u, 100u, 64u));
    /* unsigned wrap: head has rolled past 2^32, cursor has not */
    TEST_ASSERT_EQUAL_UINT32(5u - 64u + 2u, sse_cursor_fix(5u, 0xFFFFFF00u, 64u));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_cursor_at_exactly_cap_is_skipped);
    RUN_TEST(test_cursor_inside_the_window_is_untouched); RUN_TEST(test_far_overrun_and_wraparound);
    return UNITY_END(); }
```

`devcontroller/test/test_www_path.c`:
```c
/* dk:L4 -- static_file built "/www" + req->uri with no normalization and handed `..` segments to
 * LittleFS. Containment was incidental (lfs clamps at the mount root), not asserted. */
#include "unity.h"
#include "www_path.h"
void setUp(void) {}
void tearDown(void) {}
static void test_normal_assets_pass(void)
{
    TEST_ASSERT_TRUE(www_path_ok("/index.html"));
    TEST_ASSERT_TRUE(www_path_ok("/app.js"));
    TEST_ASSERT_TRUE(www_path_ok("/assets/app.css"));
    TEST_ASSERT_TRUE(www_path_ok("/a-b_c.1.png"));
}
static void test_traversal_and_junk_are_rejected(void)
{
    TEST_ASSERT_FALSE(www_path_ok("/../nvs.bin"));
    TEST_ASSERT_FALSE(www_path_ok("/assets/../../logs/1.bin"));
    TEST_ASSERT_FALSE(www_path_ok("/.."));
    TEST_ASSERT_FALSE(www_path_ok("//double"));
    TEST_ASSERT_FALSE(www_path_ok("no-leading-slash"));
    TEST_ASSERT_FALSE(www_path_ok("/with space.html"));
    TEST_ASSERT_FALSE(www_path_ok("/ctrl\x01.html"));
    TEST_ASSERT_FALSE(www_path_ok(""));
    TEST_ASSERT_FALSE(www_path_ok(NULL));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_normal_assets_pass);
    RUN_TEST(test_traversal_and_junk_are_rejected); return UNITY_END(); }
```

In `devcontroller/test/test_config_diff.c` (`dk:L1`):
```c
/* dk:L1 -- malformed JSON, a non-object, and an oversize fragment all returned -1, so webapi
 * answered a malformed POST body "413 a changed value exceeds the console line limit". */
static void test_malformed_body_is_distinguishable_from_oversize(void)
{
    char out[CFG_SET_OBJ_MAX + 1];
    TEST_ASSERT_EQUAL_INT(CFG_DIFF_E_PARSE, config_diff_minify("{\"a\":1}", "{not json", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(CFG_DIFF_E_PARSE, config_diff_minify("{\"a\":1}", "[1,2]", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(CFG_DIFF_E_PARSE, config_diff_minify("{not json", "{\"a\":1}", out, sizeof out));
    /* an oversize single fragment stays CFG_DIFF_E_SIZE (-1), the 413 path */
    char big[CFG_SET_OBJ_MAX * 2];
    int n = snprintf(big, sizeof big, "{\"name\":\"%0*d\"}", CFG_SET_OBJ_MAX, 7);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT(CFG_DIFF_E_SIZE, config_diff_minify("{\"name\":\"x\"}", big, out, sizeof out));
}
```
Register it with `RUN_TEST`.

- [ ] **Step 2: Run to fail**

```bash
cmake --build devcontroller/test/build -j8 2>&1 | tail -5
```
Expected: FAIL — `sse_cursor.h`/`www_path.h` missing, `CFG_DIFF_E_PARSE` undeclared.

- [ ] **Step 3: Implement the two new pure files**

`host/sse_cursor.c` (+ its header, documenting the aliasing argument):
```c
uint32_t sse_cursor_fix(uint32_t head, uint32_t cursor, uint32_t cap)
{
    assert(cap > 0);
    /* Distance >= cap means the producer has lapped this reader: at distance EXACTLY cap the
     * cursor's slot (cursor & (cap-1)) is the head's own slot -- the next one the producer writes
     * -- so reading it is a guaranteed torn read. Skip to cap-2 records behind the head: past the
     * producer's slot plus one record of margin (dk:M7). */
    return ((uint32_t)(head - cursor) >= cap) ? (uint32_t)(head - cap + 2u) : cursor;
}
```
`host/www_path.c`:
```c
bool www_path_ok(const char *uri)
{
    if (uri == NULL || uri[0] != '/') return false;
    /* Whitelist, not a blacklist: the `www` partition holds SPA assets whose names this device
     * generates, so anything outside [A-Za-z0-9._/-] (and any empty or dot-only segment) is not a
     * real asset path and never reaches the filesystem (dk:L4). A query string is stripped by the
     * caller before this runs. */
    size_t seg = 0, dots = 0;
    for (size_t i = 0; uri[i] != '\0'; i++) {          /* bounded by the NUL (caller's URI) */
        char c = uri[i];
        if (i >= WWW_URI_MAX) return false;
        if (c == '/') {
            if (i > 0 && (seg == 0 || seg == dots)) return false;   /* "//", "/.", "/.." */
            seg = 0; dots = 0;
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok) return false;
        if (c == '.') dots++;
        seg++;
    }
    if (seg == 0 || seg == dots) return false;          /* trailing "/", "/." or "/.." */
    return true;
}
```
with `#define WWW_URI_MAX 128u` in `www_path.h` (`static_file`'s `path[160]` minus `"/www"` and a margin). Add both sources to `devcontroller/components/webapi/CMakeLists.txt`'s `SRCS` and name them in its header comment.

- [ ] **Step 4: Implement the `config_diff` codes** — in `config_diff.h` add the two macros with a doc line each, and change `config_diff_minify`'s contract sentence to name them. In `host/config_diff.c` return `CFG_DIFF_E_PARSE` from every jsmn-parse/non-object failure and `CFG_DIFF_E_SIZE` from every capacity/fragment failure (read the function and classify each `return -1`).

- [ ] **Step 5: Wire `webapi.c`**

1. `api_config_post` (`:292-295`): split the two causes —
```c
    int dlen = config_diff_minify(s_cur_cfg, s_post_body, s_diff, sizeof s_diff);
    if (dlen == CFG_DIFF_E_PARSE)
        return send_json(req, "400 Bad Request", "{\"error\":\"malformed JSON body\"}");   /* dk:L1 */
    if (dlen < 0)
        return send_json(req, "413 Payload Too Large",
                         "{\"error\":\"a changed value exceeds the console line limit\"}");
```
2. `do_stream` (`:737-741`): replace the inline guard with `cursor = sse_cursor_fix(s_bc_head, cursor, BC_CAP);` and replace the comment with a one-liner pointing at `sse_cursor.h` (this also retires false claim 1 — the comment described behaviour the old `>` did not have).
3. `api_logs` (`:340-360`): list every file and emit `created_unix` (`dk:L2`, false claim 9) —
```c
/* dk:L2: the 16-entry cap silently truncated the list and dropped created_unix, which
 * logstore_list pays one open+read+close per file to produce and logstore.h documents as part of
 * THIS endpoint's contract. Static, not stack: LOGSTORE_ROT_MAX_FILES entries is 2 KB, and
 * logstore.c's own scan array is static for the same reason. Single-flight: one httpd request task
 * reaches this handler at a time (webapi_register's config). */
static logstore_entry_t s_log_entries[LOGSTORE_ROT_MAX_FILES];
...
    int n = logstore_list(s_log_entries, (int)(sizeof s_log_entries / sizeof s_log_entries[0]));
...
        int m = snprintf(item, sizeof item,
                         "%s{\"id\":\"%s\",\"bytes\":%lu,\"created_unix\":%lu}",
                         (i ? "," : ""), s_log_entries[i].id,
                         (unsigned long)s_log_entries[i].size,
                         (unsigned long)s_log_entries[i].created_unix);
```
   (`item[96]` grows to `item[128]` for the extra field.)
4. `static_file` (`:1073-1090`): after the `/api/` check and before building `path`, strip the query string from a local copy of the URI, then
```c
    if (!www_path_ok(uri)) {                                   /* dk:L4 */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
        return ESP_FAIL;
    }
```
   Order matters: the existing `?`-stripping happens after `snprintf` today — move it before the guard so a legal `/app.js?v=2` still passes.
5. Update `logstore.h:24-29`'s `logstore_entry_t` doc only if its wording still misstates the endpoint after this change (it should now be true as written — verify, do not rewrite).

- [ ] **Step 6: SPA + markup**

1. `app.js` `saveConfig`'s 503 branch (`dk:M2`'s user-visible half): `showMsg(msg, "err", link503Msg(res, "Lap-timer not connected — save failed."));` — with the bounded download take, a Save racing a `list`/bridge now gets `{"error":"link busy, retry"}`, and "not connected" was the wrong diagnosis. Add a `res.status === 400` branch above it: `showMsg(msg, "err", "Save rejected: malformed request body.");`
2. `renderLogsTable`: add a third cell before the download cell —
```js
        el("td", { text: l.created_unix ? new Date(l.created_unix * 1000)
                   .toISOString().replace("T", " ").slice(0, 19) + "Z" : "—" }),
```
   and change the empty-row `colSpan: 3` to `4`.
3. `index.html:100` logs `<thead>`: `<tr><th>ID</th><th>Size</th><th>Created (UTC)</th><th>Download</th></tr>`.
4. `index.html:92-95` (`dk:L5`): replace the `rx_us` sentence with *"`rx_us` is the dev-kit's own receive time in microseconds **since the dev-kit last booted** (it restarts at 0 on every dev-kit reboot, so records from two boots cannot be ordered by it); `gps_us` is the lap-timer's GPS time."* Mirror the same clarification in `logstore_rec.h:26`'s field comment and in `logstore.h`'s `logstore_append` doc (already says "since boot" — confirm, do not duplicate).

- [ ] **Step 7: Verify**

```bash
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build --output-on-failure  # 31/31
node --check devcontroller/web/app.js
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..    # rc 0, 0 warnings, www.bin repacked
grep -n "> BC_CAP" devcontroller/components/webapi/webapi.c             # no hits
```

- [ ] **Step 8: Commit**

```bash
git add devcontroller/components/webapi devcontroller/test/test_sse_cursor.c \
        devcontroller/test/test_www_path.c devcontroller/test/test_config_diff.c devcontroller/web
git commit
```
```
fix(devcontroller/webapi): SSE guard at >= BC_CAP, whitelisted static paths, 400 vs 413 on config POST, /api/logs lists every file with created_unix (F3 T4, dk:M7/L1/L2/L4/L5)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 4 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

- [ ] **SESSION 1 END** — before `/clear`, re-run the three suites in full so the next session starts from a known-green tree (no firmware build needed yet — Task 12 does the clean sweep):

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
cmake --build test/build -j8 && ctest --test-dir test/build                                 # 47/47
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build --output-on-failure   # 31/31
python3 -m unittest tools/test_devkit.py                                                    # 17 tests, OK
printf 'SESSION 1 END: host %s, dc-host %s, tools %s, head %s\n' "47/47" "31/31" "17 tests, OK" \
  "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Any red here is fixed in THIS session, never carried across a `/clear`.

---

## Session 2 — Tasks 5–8 (devconsole: PSK, relay, framing, flash)

Resume prompt for this session: *Resume plan `docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md` at Task 5 (ledger `.superpowers/sdd/2026-10-13-fix-F3/progress.md`); worktree `/Users/benji/projects/personal/lap-timer-worktrees/f3`; branch `fix-sim-devkit`.* All four tasks own disjoint files inside `devcontroller/components/devconsole/`; only Task 5 edits that component's `CMakeLists.txt`.

---

### Task 5: Random NVS AP passphrase (`dk:H1`, ruling **R16**) — GROUP D, PARALLEL

**Evidence:** `dev-controller.md` H1: `main.c:89-94` derives the PSK from the eFuse MAC, and the SoftAP BSSID in every beacon is a public ESP-IDF function of that same base MAC — anyone in radio range inverts it. Design R16: *12-character random PSK from `esp_random()` generated once into NVS (`dc/psk`), printed at boot and by `dc status`; `dc psk reset` regenerates.*

**Files:**
- Create: `devcontroller/components/devconsole/include/dc_psk.h`, `devcontroller/components/devconsole/host/dc_psk_gen.c`, `devcontroller/components/devconsole/dc_psk.c`, `devcontroller/test/test_dc_psk.c`
- Modify: `devcontroller/components/devconsole/CMakeLists.txt`, `devcontroller/main/main.c`, `devcontroller/components/devconsole/cmd_dc.c`

**Interfaces:**
- Produces, in `dc_psk.h`:
  - `#define DC_PSK_LEN 12u`, `#define DC_PSK_ALPHABET "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"` (32 unambiguous characters: no `0`/`O`/`1`/`I`/`l`, no lowercase — 12 × 5 bits = 60 bits of entropy, and every character is equally likely because 32 divides a byte's low 5 bits exactly).
  - `void dc_psk_format(const uint8_t *rnd, size_t n, char *out, size_t out_cap);` (pure)
  - `bool dc_psk_valid(const char *s);` (pure)
  - `int dc_psk_get(char *out, size_t out_cap);` — 0 = loaded/created and persisted, 1 = generated but NOT persisted (NVS error; the AP still comes up with it), <0 = could not produce one.
  - `int dc_psk_reset(char *out, size_t out_cap);` — same returns; regenerates with RF already up.
- Consumes: `nvs_open`/`nvs_get_str`/`nvs_set_str`/`nvs_commit`, `esp_fill_random`, `bootloader_random_enable`/`_disable`, `esp_wifi_get_config`/`_set_config`.
- **Placement rationale (put it in the header):** `devconsole` is the lowest component that both `app_main` (which brings the AP up) and `dc status` already depend on (`main/CMakeLists.txt` REQUIRES `devconsole`), so the PSK module lives here rather than in `main` (which `devconsole` cannot see) or in a new component.

- [ ] **Step 1: Write the failing test** — `devcontroller/test/test_dc_psk.c`:

```c
/* dk:H1 / R16 -- the AP passphrase must be random, not a public function of the beacon BSSID.
 * Pure half: the alphabet mapping and the stored-value validator. */
#include "unity.h"
#include "dc_psk.h"
#include <string.h>
void setUp(void) {}
void tearDown(void) {}

static void test_format_length_charset_and_nul(void)
{
    uint8_t rnd[DC_PSK_LEN];
    for (size_t i = 0; i < sizeof rnd; i++) rnd[i] = (uint8_t)(i * 17u + 3u);
    char out[DC_PSK_LEN + 1];
    dc_psk_format(rnd, sizeof rnd, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(DC_PSK_LEN, strlen(out));
    for (size_t i = 0; i < DC_PSK_LEN; i++)
        TEST_ASSERT_NOT_NULL(strchr(DC_PSK_ALPHABET, out[i]));
    TEST_ASSERT_TRUE(dc_psk_valid(out));
}

static void test_format_uses_only_the_low_five_bits_so_every_byte_maps(void)
{
    uint8_t zeros[DC_PSK_LEN] = { 0 };
    char out[DC_PSK_LEN + 1];
    dc_psk_format(zeros, sizeof zeros, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("222222222222", out);
    uint8_t ones[DC_PSK_LEN];
    memset(ones, 0xFF, sizeof ones);
    dc_psk_format(ones, sizeof ones, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ZZZZZZZZZZZZ", out);
}

static void test_distinct_input_gives_distinct_output(void)
{
    uint8_t a[DC_PSK_LEN] = { 1 }, b[DC_PSK_LEN] = { 2 };
    char oa[DC_PSK_LEN + 1], ob[DC_PSK_LEN + 1];
    dc_psk_format(a, sizeof a, oa, sizeof oa);
    dc_psk_format(b, sizeof b, ob, sizeof ob);
    TEST_ASSERT_TRUE(strcmp(oa, ob) != 0);
}

static void test_valid_rejects_anything_not_from_this_generator(void)
{
    TEST_ASSERT_FALSE(dc_psk_valid(NULL));
    TEST_ASSERT_FALSE(dc_psk_valid(""));
    TEST_ASSERT_FALSE(dc_psk_valid("22222222222"));          /* 11 chars */
    TEST_ASSERT_FALSE(dc_psk_valid("2222222222222"));        /* 13 chars */
    TEST_ASSERT_FALSE(dc_psk_valid("ltdev-aabbcc"));         /* the old MAC-derived form */
    TEST_ASSERT_FALSE(dc_psk_valid("22222222222O"));         /* excluded character */
}

static void test_format_never_overflows_a_short_buffer(void)
{
    uint8_t rnd[DC_PSK_LEN] = { 5 };
    char small[4] = { 'x', 'x', 'x', 'x' };
    dc_psk_format(rnd, sizeof rnd, small, sizeof small);
    TEST_ASSERT_EQUAL_UINT(3, strlen(small));                /* 3 chars + NUL, no overrun */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_format_length_charset_and_nul);
    RUN_TEST(test_format_uses_only_the_low_five_bits_so_every_byte_maps);
    RUN_TEST(test_distinct_input_gives_distinct_output);
    RUN_TEST(test_valid_rejects_anything_not_from_this_generator);
    RUN_TEST(test_format_never_overflows_a_short_buffer);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to fail** — `cmake --build devcontroller/test/build -j8` → `dc_psk.h: No such file or directory`.

- [ ] **Step 3: Implement the pure half** — `host/dc_psk_gen.c`:
```c
void dc_psk_format(const uint8_t *rnd, size_t n, char *out, size_t out_cap)
{
    assert(rnd != NULL || n == 0);
    assert(out != NULL);
    assert(out_cap > 0);
    static const char A[] = DC_PSK_ALPHABET;
    size_t want = (n < DC_PSK_LEN) ? n : DC_PSK_LEN;
    if (want + 1u > out_cap) want = out_cap - 1u;
    for (size_t i = 0; i < want; i++) out[i] = A[rnd[i] & 0x1Fu];   /* bounded: want <= DC_PSK_LEN */
    out[want] = '\0';
}

bool dc_psk_valid(const char *s)
{
    if (s == NULL) return false;
    size_t i = 0;
    for (; i < DC_PSK_LEN; i++) {                            /* bounded: DC_PSK_LEN */
        if (s[i] == '\0' || strchr(DC_PSK_ALPHABET, s[i]) == NULL) return false;
    }
    return s[i] == '\0';
}
```
(`_Static_assert(sizeof(DC_PSK_ALPHABET) - 1 == 32, "the 5-bit mapping needs exactly 32 characters");` at file scope.)

- [ ] **Step 4: Run to pass** — `./devcontroller/test/build/test_dc_psk` → 5 tests, 0 failures.

- [ ] **Step 5: Implement the NVS half** — `dc_psk.c`:

```c
/* dc_psk.c -- R16 (dk:H1): the dev-kit's WPA2 passphrase. NVS namespace "dc", key "psk": a
 * DC_PSK_LEN-character value generated ONCE on the first boot and reused forever after, so it is
 * not a function of anything an attacker can read off the air (the old PSK was "ltdev-" + the
 * eFuse MAC, and the SoftAP BSSID in every beacon is a published ESP-IDF function of that same
 * base MAC). A stored value that does not pass dc_psk_valid (absent, truncated, or the old
 * MAC-derived form) is replaced. Entropy: esp_random()'s HWRNG is only fully seeded once RF is
 * running (esp_random.h), and the first-boot generation happens BEFORE esp_wifi_start(), so that
 * path brackets the fill with bootloader_random_enable()/_disable() (bootloader_random.h: the SAR
 * ADC entropy source, which MUST be disabled again before RF/ADC init -- it is, inside this
 * function, before wifi_ap_start() goes on to esp_wifi_init()). dc_psk_reset() runs with the AP
 * already up and uses esp_fill_random() directly. */
#define DC_PSK_NS  "dc"
#define DC_PSK_KEY "psk"

static void psk_fill(char *out, size_t out_cap, bool rf_running)
{
    uint8_t rnd[DC_PSK_LEN];
    if (!rf_running) bootloader_random_enable();
    esp_fill_random(rnd, sizeof rnd);
    if (!rf_running) bootloader_random_disable();
    dc_psk_format(rnd, sizeof rnd, out, out_cap);
}

/* Persists `psk` under dc/psk. 0 once the commit has landed, <0 on any NVS failure (the caller
 * then keeps using the value in RAM for this boot and says so). Same open/set/commit/close shape
 * the lap-timer's lt_nvs.c uses, except the handle is not cached: this runs twice in the life of a
 * device (first boot, `dc psk reset`), so holding a handle open forever buys nothing. */
static int psk_store(const char *psk)
{
    assert(psk != NULL);
    nvs_handle_t h;
    if (nvs_open(DC_PSK_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_set_str(h, DC_PSK_KEY, psk);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "psk store: %s", esp_err_to_name(e));
        return -1;
    }
    return 0;
}

int dc_psk_get(char *out, size_t out_cap)
{
    assert(out != NULL);
    if (out_cap < DC_PSK_LEN + 1u) return -1;
    nvs_handle_t h;
    if (nvs_open(DC_PSK_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = out_cap;
        esp_err_t e = nvs_get_str(h, DC_PSK_KEY, out, &len);
        nvs_close(h);
        if (e == ESP_OK && dc_psk_valid(out)) return 0;
    }
    psk_fill(out, out_cap, /*rf_running*/false);
    return (psk_store(out) == 0) ? 0 : 1;       /* 1: usable this boot, not persisted */
}

int dc_psk_reset(char *out, size_t out_cap)
{
    assert(out != NULL);
    if (out_cap < DC_PSK_LEN + 1u) return -1;
    psk_fill(out, out_cap, /*rf_running*/true);
    return (psk_store(out) == 0) ? 0 : 1;
}
```
`dc_psk.c`'s includes and tag, in full: `#include "dc_psk.h"`, `<assert.h>`, `<stdio.h>`, `<string.h>`, `"bootloader_random.h"` (the enable/disable pair), `"esp_log.h"`, `"esp_random.h"` (`esp_fill_random`), `"nvs.h"`, plus `static const char *TAG = "dc_psk";`. No loops and no allocation anywhere in the file. CMake: add `"dc_psk.c" "host/dc_psk_gen.c"` to `SRCS`, add `nvs_flash esp_hw_support bootloader_support` to `PRIV_REQUIRES`, and extend the component comment with a paragraph on the module and its placement.

- [ ] **Step 6: Bring the AP up with it** — `devcontroller/main/main.c` `wifi_ap_start()`: replace the MAC derivation (`:84-94`, including `esp_mac.h` if nothing else needs it) with
```c
    /* R16 (dk:H1): a random per-device passphrase from NVS, not a function of the beacon BSSID.
     * Generated before esp_wifi_init() below, which is also what lets dc_psk_get() use the
     * bootloader SAR-ADC entropy source and turn it off again before RF comes up. */
    char psk[DC_PSK_LEN + 1];
    int pr = dc_psk_get(psk, sizeof psk);
    if (pr < 0) { ESP_LOGE(TAG, "AP psk generation failed"); return; }
    snprintf((char *)ap_config.ap.password, sizeof ap_config.ap.password, "%s", psk);
```
and the boot print becomes
```c
    ESP_LOGI(TAG, "SoftAP up: ssid=%s psk=%s%s channel=%d ip=192.168.4.1",
             DC_WIFI_SSID, psk, (pr == 1) ? " (NOT persisted: NVS error -- changes on reboot)" : "",
             DC_WIFI_CHANNEL);
```
Move the `dc_psk_get` call to the top of `wifi_ap_start()` (before `esp_netif_init`) so the entropy bracket really is pre-RF, and update the file-header step list ("2. SoftAP up") to mention the NVS passphrase.

- [ ] **Step 7: `dc status` + `dc psk reset`** — `cmd_dc.c`:
- `dc_status`: read the PSK once (`char psk[DC_PSK_LEN + 1]; (void)dc_psk_get(psk, sizeof psk);`) and emit `jsonw_str(&w, "ap_psk", psk);` after `ap_clients`, plus `printf("ap_psk: %s\n", psk);` in the human form.
- New subcommand, dispatched from `cmd_dc_main` before the unknown-subcommand error:
```c
static int dc_psk_cmd(int argc, char **argv, bool json)
{
    if (argc != 3 || strcmp(argv[2], "reset") != 0) { dc_err(json, "usage: dc psk reset [--json]"); return 1; }
    char psk[DC_PSK_LEN + 1];
    int rc = dc_psk_reset(psk, sizeof psk);
    if (rc < 0) { dc_err(json, "psk generation failed"); return 1; }
    /* Apply live so the rotation takes effect without a reboot: the AP restarts and every joined
     * client must rejoin with the new passphrase (this command is USB-console only, so no caller
     * is relying on the WiFi link at this moment). Persisted BEFORE the live apply, so a failed
     * apply still leaves the next boot using the printed value. */
    wifi_config_t c;
    bool applied = (esp_wifi_get_config(WIFI_IF_AP, &c) == ESP_OK);
    if (applied) {
        snprintf((char *)c.ap.password, sizeof c.ap.password, "%s", psk);
        applied = (esp_wifi_set_config(WIFI_IF_AP, &c) == ESP_OK);
    }
    if (json) {
        char buf[96];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "ap_psk", psk);
        jsonw_bool(&w, "persisted", rc == 0);
        jsonw_bool(&w, "applied", applied);
        if (!jsonw_end(&w)) { dc_err(true, "reply too large"); return 1; }
        printf("%s\n", buf);
        return 0;
    }
    printf("ap_psk: %s\npersisted: %s\napplied: %s (clients must rejoin)\n",
           psk, (rc == 0) ? "true" : "false", applied ? "true" : "false");
    return 0;
}
```
- Update both usage strings and `console_register`'s help to `dc status [--json] (~1s) | dc log <level> | dc baud <rate> | dc psk reset`.
- Fix the now-false comment at `cmd_dc.c:36-39` (Task 4 makes `/api/logs` list every file): *"`dc status` summarises the newest DC_LOGSTORE_MAX files only — enough for an at-a-glance total on a console line; `GET /api/logs` lists all of them (LOGSTORE_ROT_MAX_FILES), and logstore_list bounds its own scan either way."*

- [ ] **Step 8: Verify + commit**

```bash
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build   # test_dc_psk green
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..                       # rc 0, 0 warnings
grep -rn "esp_efuse_mac_get_default\|ltdev-" devcontroller/                                # no hits
```
```bash
git add devcontroller/components/devconsole devcontroller/main/main.c devcontroller/test/test_dc_psk.c
git commit
```
```
feat(devcontroller): random 12-char AP passphrase in NVS (dc/psk), printed at boot and by `dc status`, rotatable with `dc psk reset` (F3 T5, dk:H1/R16)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 5 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 6: `lt <any verb>` pass-through relay (`dk:M4`, ruling **R17**) — GROUP D, PARALLEL

**Evidence:** `dev-controller.md` M4: spec §5.2 defines `lt <console cmd…>` as "relay **any** lap-timer console command", `cmd_lt.c:420-426` dispatches six; `errlog`, `diag`, `dbg`, `close`, `read` are unreachable, which is why `2026-10-03-debt-sweep-b-design.md` §8.1 had to route `dbg reset` "via `lt shell`". R17: *pass-through of any verb through the bridge filter; the dev-kit's own verbs keep precedence.*

**Files:**
- Modify: `devcontroller/components/devconsole/cmd_lt.c` (header comment, new `lt_relay`, `cmd_lt_main`, `cmd_lt_register`)

**Interfaces:**
- Consumes: `linkhost_bridge_begin()`/`linkhost_bridge_end()` (exclusive UART1 + parked demux + drain, Task 3's version), `bridge_filter_reset`/`bridge_filter_run` (**unchanged** — R17 requires no change there; Task 2's `len` bound is a separate finding), `linkhost_now_us()`, `uart_read_bytes`/`uart_write_bytes` on `DC_LINK_UART` (`build_config.h`), the existing `lt_err`/`console_wants_json`.
- Produces: no new public API. New file-scope constants `LT_RELAY_IDLE_MS 1500`, `LT_RELAY_CAP_MS 15000`, `LT_RELAY_LINE_MAX 250u`, and one module static `static char s_relay_body[LT_JSON_BODY_MAX + 1];`.

- [ ] **Step 1: There is no host harness for this path — state what proves it.** `cmd_lt.c` is IDF glue (UART + console). The verb-classification half is three `strcmp`s against a table, verified by the `grep` checks in Step 4; the relay itself is proven by the bench item Task 11 appends (`lt errlog` returns the error ring; `lt dbg mem` prints its lines). Do not add a fake-UART harness.

- [ ] **Step 2: Implement the relay** — add to `cmd_lt.c`:

```c
/* R17 (dk:M4): every verb this file does NOT implement locally is relayed verbatim to the
 * lap-timer's console. The six locally-implemented verbs keep precedence (they add id validation,
 * wire escaping, STATUS decoding and the --json shapes `tools/devkit.py` parses); the list below
 * is the DENY-list of the dev-kit's OWN console verbs, which are never sent over the link -- typed
 * after `lt` they can only be a mistake ("lt dc status"), and relaying them would come back as the
 * lap-timer's "Unrecognized command" with no hint why. */
static bool lt_verb_is_local(const char *v)
{
    static const char *const DENY[] = { "dc", "link", "flash", "stream", "selftest", "help" };
    for (size_t i = 0; i < sizeof DENY / sizeof DENY[0]; i++)      /* bounded: 6 */
        if (strcmp(v, DENY[i]) == 0) return true;
    return false;
}

/* Joins argv[1..argc) with single spaces into out (bounded by out_cap, which the caller sizes to
 * the lap-timer console's 250 B line budget) and rejects any byte outside printable ASCII -- a
 * control byte would split one relayed line into several commands on the lap-timer, the same
 * injection POST /api/config's control-char guard exists to stop. Same shape as lt_join_json
 * above, from argv[1] instead of argv[3]. Returns the joined length, or -1 if it does not fit or
 * is not printable. */
static int lt_relay_join(int argc, char **argv, char *out, size_t out_cap)
{
    assert(out != NULL);
    assert(out_cap > 0);
    size_t w = 0;
    for (int i = 1; i < argc; i++) {                            /* bounded by argc */
        size_t al = strlen(argv[i]);
        size_t need = al + ((i > 1) ? 1u : 0u);
        if (w + need >= out_cap) return -1;
        if (i > 1) out[w++] = ' ';
        for (size_t k = 0; k < al; k++) {                       /* bounded by al */
            unsigned char c = (unsigned char)argv[i][k];
            if (c < 0x20u || c > 0x7Eu) return -1;
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
    return (w > 0) ? (int)w : -1;
}

/* Relays one arbitrary console line and prints the lap-timer's reply. Transport: the raw bridge
 * (linkhost_bridge_begin/_end -- exclusive UART1, demux parked, drained on entry AND exit) with
 * every byte passed through bridge_filter_run, exactly as `lt shell` does, because an arbitrary
 * verb's reply may be framed (errlog/diag/close/read all go through export_serial's run_cmd) OR
 * plain text (every `dbg` subcommand) -- the framed-download path would simply time out on the
 * latter with nothing printed. Bounded twice: LT_RELAY_IDLE_MS of silence ends the reply,
 * LT_RELAY_CAP_MS ends the call regardless. --json reports {cmd,rc,rt_ms,bytes,body} with the body
 * truncated to LT_JSON_BODY_MAX (same shape as lt_report_cmd, so devkit.py's `lt` parser is
 * unchanged); the human form streams the bytes as they arrive. */
static int lt_relay(int argc, char **argv, bool json)
{
    char line[LT_RELAY_LINE_MAX + 1];
    if (lt_relay_join(argc, argv, line, sizeof line) < 0) { lt_err(json, "relay line too long or not printable"); return 1; }
    if (linkhost_bridge_begin() != 0) { lt_err(json, "busy"); return 1; }

    bridge_filter_t filt;
    bridge_filter_reset(&filt);
    uart_write_bytes(DC_LINK_UART, line, strlen(line));
    uart_write_bytes(DC_LINK_UART, "\r", 1);

    int64_t t0 = linkhost_now_us();
    int64_t cap = t0 + (int64_t)LT_RELAY_CAP_MS * 1000;
    int64_t idle = t0 + (int64_t)LT_RELAY_IDLE_MS * 1000;
    size_t blen = 0;
    uint32_t total = 0;
    bool last_nl = true;              /* did the last forwarded byte end a line? (prompt tails) */
    while (linkhost_now_us() < cap && linkhost_now_us() < idle) {   /* bounded by both deadlines */
        uint8_t rb[64], filtered[64];
        int n = uart_read_bytes(DC_LINK_UART, rb, sizeof rb, pdMS_TO_TICKS(20));
        if (n <= 0) continue;
        size_t w = bridge_filter_run(&filt, rb, (size_t)n, filtered, sizeof filtered);
        idle = linkhost_now_us() + (int64_t)LT_RELAY_IDLE_MS * 1000;
        total += (uint32_t)w;
        if (w > 0) last_nl = (filtered[w - 1] == '\n');
        if (!json) { if (w > 0) fwrite(filtered, 1, w, stdout); continue; }
        for (size_t i = 0; i < w && blen < LT_JSON_BODY_MAX; i++) s_relay_body[blen++] = (char)filtered[i];
    }
    linkhost_bridge_end();
    s_relay_body[blen] = '\0';
    long long rt_ms = (long long)((linkhost_now_us() - t0) / 1000);
    /* rc is 0 when the lap-timer answered at all (any bytes), LINKHOST_E_TIMEOUT when it did not:
     * an arbitrary verb has no framed status to report, so "did the console reply" is the only
     * honest result this path can produce. */
    int rc = (total > 0) ? 0 : LINKHOST_E_TIMEOUT;

    if (json) {
        char buf[LT_JSON_BODY_MAX + 256u];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "cmd", line);
        jsonw_int(&w, "rc", rc);
        jsonw_int(&w, "attempts", 1);          /* the relay never retries, same as lt_run_download */
        jsonw_int(&w, "rt_ms", rt_ms);
        jsonw_uint(&w, "bytes", total);
        jsonw_str(&w, "body", s_relay_body);   /* jsonw_str escapes and drops control bytes */
        if (!jsonw_end(&w)) { lt_err(true, "reply too large"); return 1; }
        printf("%s\n", buf);
        return (rc == 0) ? 0 : 1;
    }

    if (!last_nl) printf("\n");               /* the lap-timer's prompt leaves no trailing LF */
    printf("-- %u bytes, %lu stream frames filtered\n", (unsigned)total, (unsigned long)filt.frames);
    printf("-- rt %lld ms, attempts 1, rc %d\n", rt_ms, rc);
    return (rc == 0) ? 0 : 1;
}
```
`cmd_lt_main`'s tail becomes:
```c
    if (strcmp(argv[1], "shell")  == 0) return lt_shell(argc, json);
    if (lt_verb_is_local(argv[1])) { lt_err(json, "that is a dev-kit verb -- type it without `lt`"); return 1; }
    return lt_relay(argc, argv, json);          /* R17: any other verb goes to the lap-timer */
```
Add `#include "bridge_filter.h"`, `#include "build_config.h"`, `#include "driver/uart.h"` and `#include "freertos/FreeRTOS.h"` (the `pdMS_TO_TICKS` + uart pattern `cmd_shell.c` already uses in this component), and update the file's header command table plus `console_register`'s help string to `lt <console cmd…>`.

- [ ] **Step 3: Update the help text to match the spec** — `cmd_lt_register`: `"lt status|list|config get|config set <json>|delete <id>|open <id> <fmt>|shell|<any lap-timer console cmd> [--json]"`. `tools/devkit.py`'s own help string (`:573-575`) already advertises the generic relay and is now true — Task 10 Step 5 adds the one-line note about the deny-list.

- [ ] **Step 4: Verify + commit**

```bash
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..   # rc 0, 0 warnings
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build   # unchanged, green
grep -n "unknown subcommand" devcontroller/components/devconsole/cmd_lt.c                  # no hits
```
```bash
git add devcontroller/components/devconsole/cmd_lt.c
git commit
```
```
feat(devcontroller): `lt <verb>` relays any lap-timer console command over the bridge filter; the six local verbs keep precedence (F3 T6, dk:M4/R17)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 6 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 7: `selftest framing` requires one CR on marker lines (`dk:M3`, false claim 6) — GROUP D, PARALLEL

**Evidence:** `dev-controller.md` M3 + false claim 6; reproducer `fr.c`, re-run 2026-10-13: `"---BEGIN status 28---\nQUJD\n---END deadbeef---\n"` → `pass=1 markers=2 bad=0`. A lap-timer that lost its CR translation passes the check that exists to catch exactly that.

**Files:**
- Modify: `devcontroller/components/devconsole/host/selftest_eval.c`, `include/selftest_eval.h`, `devcontroller/test/test_selftest_eval.c`

**Interfaces:** unchanged — `selftest_framing_eval(const uint8_t *raw, size_t n, st_framing_t *out)` keeps its signature, `st_framing_t` keeps `pass/markers/bad/reason`, and the reason precedence stays `"markers"` then `"cr"`. `cmd_selftest.c` needs no change.

- [ ] **Step 1: Write the failing tests** — add to `devcontroller/test/test_selftest_eval.c`:

```c
/* dk:M3 -- spec §5.2: "PASS if every ---BEGIN/---END marker is followed by exactly one CR before
 * LF". The evaluator only flagged lines with MORE than one CR, so a bare-LF marker line (a
 * lap-timer that lost its \n -> \r\n translation) passed. Reviewer's reproducer, verbatim. */
static void test_bare_lf_marker_lines_fail(void)
{
    st_framing_t r;
    const char *bad = "---BEGIN status 28---\nQUJD\n---END deadbeef---\n";
    selftest_framing_eval((const uint8_t *)bad, strlen(bad), &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_INT(2, r.markers);
    TEST_ASSERT_EQUAL_INT(2, r.bad);
    TEST_ASSERT_EQUAL_STRING("cr", r.reason);
}

/* The spec-correct reply still passes, and a doubled CR on a marker line still fails. */
static void test_one_cr_passes_and_double_cr_fails(void)
{
    st_framing_t r;
    const char *good = "---BEGIN status 28---\r\nQUJD\r\n---END deadbeef---\r\n";
    selftest_framing_eval((const uint8_t *)good, strlen(good), &r);
    TEST_ASSERT_TRUE(r.pass);
    TEST_ASSERT_EQUAL_INT(0, r.bad);
    const char *dbl = "---BEGIN status 28---\r\r\nQUJD\r\n---END deadbeef---\r\n";
    selftest_framing_eval((const uint8_t *)dbl, strlen(dbl), &r);
    TEST_ASSERT_FALSE(r.pass);
    TEST_ASSERT_EQUAL_STRING("cr", r.reason);
}

/* rawcap is a 256 B truncated capture, so the LAST line is routinely incomplete: a marker line with
 * no terminating LF must not be judged (it would fail every real `selftest framing` run). */
static void test_truncated_trailing_marker_line_is_not_judged(void)
{
    st_framing_t r;
    const char *cut = "---BEGIN status 28---\r\nQUJD\r\n---END deadbe";
    selftest_framing_eval((const uint8_t *)cut, strlen(cut), &r);
    TEST_ASSERT_EQUAL_INT(0, r.bad);
    TEST_ASSERT_TRUE(r.pass);
}
```
Register all three; keep the file's existing framing cases unchanged (the no-marker `\r\r\n` case must still report `markers`).

- [ ] **Step 2: Run to fail** — `./devcontroller/test/build/test_selftest_eval` → the bare-LF case fails (`pass=1, bad=0`).

- [ ] **Step 3: Implement** — in `selftest_eval.c`, replace the CR-run loop in `selftest_framing_eval` with a line-wise scan, keeping `count_occurrences` for `markers`:

```c
/* True if raw[0..n) contains `needle`. Bounded by n. */
static bool mem_has(const uint8_t *hay, size_t n, const char *needle)
{
    size_t nn = strlen(needle);
    if (nn == 0 || n < nn) return false;
    for (size_t i = 0; i + nn <= n; i++)                 /* bounded by n */
        if (memcmp(hay + i, needle, nn) == 0) return true;
    return false;
}

/* Counts CR defects line by line over the COMPLETE lines of raw[0..n) (a line is complete only
 * when terminated by '\n' -- rawcap truncates at 256 B, so the trailing partial line is never
 * judged). A line carrying a ---BEGIN/---END marker must end in EXACTLY one CR before its LF
 * (spec §5.2); any other line is flagged only for the doubled-CR wire quirk
 * linkhost_parse_frame documents ("\r\r\n"). */
static int framing_bad_lines(const uint8_t *raw, size_t n)
{
    int bad = 0;
    size_t start = 0, cr = 0;
    for (size_t i = 0; i < n; i++) {                     /* bounded by n */
        if (raw[i] == '\r') { cr++; continue; }
        if (raw[i] != '\n') { cr = 0; continue; }
        size_t text = (i >= start + cr) ? (i - cr - start) : 0;   /* the line without its CR run */
        bool marker = mem_has(raw + start, text, "---BEGIN") || mem_has(raw + start, text, "---END");
        if (marker ? (cr != 1) : (cr > 1)) bad++;
        start = i + 1;
        cr = 0;
    }
    return bad;
}
```
and `out->bad = framing_bad_lines(raw, n);`. Update `selftest_eval.h`'s `selftest_framing_eval` doc block: *"`bad` counts COMPLETE lines (terminated by '\n') whose CR run before the LF is wrong: a line carrying a `---BEGIN`/`---END` marker must have exactly one CR (spec §5.2 — a bare-LF marker line is the failure this check exists to catch, dk:M3); any other line is counted only when it carries more than one (the `\r\r\n` wire quirk). A truncated trailing line is not judged."* Keep the module's thresholds comment block accurate.

- [ ] **Step 4: Run to pass + re-run the reproducer**

```bash
cmake --build devcontroller/test/build -j8 && ./devcontroller/test/build/test_selftest_eval
cc -I devcontroller/components/devconsole/include -o /tmp/fr .superpowers/reviews/2026-10-13/fr.c \
   devcontroller/components/devconsole/host/selftest_eval.c && /tmp/fr
```
Expected: `bare-LF reply: pass=0 markers=2 bad=2 reason='cr'`; `no-marker reply: pass=0 ... reason='markers'`; `one-CR reply: pass=1 ... bad=0`.

- [ ] **Step 5: Commit**

```bash
git add devcontroller/components/devconsole/host/selftest_eval.c \
        devcontroller/components/devconsole/include/selftest_eval.h devcontroller/test/test_selftest_eval.c
git commit
```
```
fix(devcontroller): `selftest framing` fails a marker line that is not followed by exactly one CR (F3 T7, dk:M3)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 7 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 8: `flash stage` drain claim + the 35 s rule on the console (`dk:M8` device half, `L7`, false claim 2) — GROUP D, PARALLEL

**Evidence:** `dev-controller.md` M8 (`cmd_flash.c:177-184`: 200 ms ≈ 2.3 KB against ~55 s of in-flight image), false claim 2 ("they **never** get interpreted as REPL command characters"), L7 (the 35 s OTA-trial rule appears nowhere in `cmd_flash.c`).

**Files:**
- Modify: `devcontroller/components/devconsole/cmd_flash.c`

**Interfaces:** unchanged. `FLASH_DRAIN_ITERS`/`FLASH_DRAIN_MS` keep their names; `flash_drain_rx` keeps its signature.

- [ ] **Step 1: Make the drain drain** (and bound it honestly):
```c
#define FLASH_DRAIN_ITERS 60     /* was 10: with the host fixed (tools/devkit.py stops on the
                                  * first STAGE-ERR, F3 T10), the worst case still in flight is one
                                  * CHUNK plus the host OS buffer -- ~4-8 KB, ~0.7 s at 115200 --
                                  * so 60 * FLASH_DRAIN_MS = 1.2 s covers it. The loop now stops
                                  * EARLY on the first empty read window, so a clean abort costs
                                  * one window, not the whole budget. */
static void flash_drain_rx(void)
{
    uint8_t junk[64];
    for (int i = 0; i < FLASH_DRAIN_ITERS; i++) {        /* bounded: FLASH_DRAIN_ITERS windows */
        int n = uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, junk, sizeof junk,
                                pdMS_TO_TICKS(FLASH_DRAIN_MS));
        if (n <= 0) break;                               /* wire quiet: nothing left in flight */
    }
}
```

- [ ] **Step 2: Correct the false claim** — `cmd_flash.c:32-35`, replace "are drained off UART0 for a bounded window so they **never** get interpreted as REPL command characters" with: *"are drained off UART0 until the wire goes quiet, bounded by `FLASH_DRAIN_ITERS` windows (~1.2 s). That covers a host that stops sending when it sees `STAGE-ERR` — which the reference host, `tools/devkit.py`, now does (it polls between chunks, dk:M8). A host that keeps streaming the whole image regardless will still outrun this drain and have its remainder land on the REPL line editor; the bound here is deliberate (a drain that waited out an arbitrary sender would be an unbounded loop)."*

- [ ] **Step 3: Print the 35 s rule after a successful push** (`dk:L7`) — in the `flash push` result path, after the `flash result 0x0000` line and only for that code:
```c
    if (code == 0x0000u) {
        /* dk:L7: the lap-timer is now inside its OTA trial window -- it validates the new image at
         * OTA_VALID_UPTIME_S (30 s) plus a sensors-ok check, and anything that resets it before
         * then (a USB port open asserting DTR/RTS on its auto-reset circuit included) makes the
         * bootloader roll the image back with 0x0806. The rule lived only in a memory note and the
         * bench checklist; it belongs next to the event that starts the clock. */
        printf("note: do not reset the lap-timer or open its USB port for 35 s (OTA trial window; "
               "an early reset rolls back, 0x0806)\r\n");
    }
```
Mirror the same line in `flash status`'s `done_ok` row? No — print it exactly once, at the push, so a polled `flash status` cannot spam it.

- [ ] **Step 4: Verify + commit**

```bash
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..   # rc 0, 0 warnings
grep -n "never get interpreted" devcontroller/components/devconsole/cmd_flash.c   # no hits
```
```bash
git add devcontroller/components/devconsole/cmd_flash.c
git commit
```
```
fix(devcontroller): `flash stage` drains to quiet within a stated bound, honest drain comment, 35 s OTA-trial note after a push (F3 T8, dk:M8/L7)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 8 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

- [ ] **SESSION 2 END** — before `/clear`, re-run the three suites in full so the next session starts from a known-green tree (no firmware build needed yet — Task 12 does the clean sweep):

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
cmake --build test/build -j8 && ctest --test-dir test/build                                 # 47/47
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build --output-on-failure   # 33/33
python3 -m unittest tools/test_devkit.py                                                    # 28 tests, OK
printf 'SESSION 2 END: host %s, dc-host %s, tools %s, head %s\n' "47/47" "33/33" "28 tests, OK" \
  "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Any red here is fixed in THIS session, never carried across a `/clear`.

---

## Session 3 — Tasks 9–12 (logstore, host tools, specs/bench, gates + PR)

Resume prompt for this session: *Resume plan `docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md` at Task 9 (ledger `.superpowers/sdd/2026-10-13-fix-F3/progress.md`); worktree `/Users/benji/projects/personal/lap-timer-worktrees/f3`; branch `fix-sim-devkit`.* Tasks 11 and 12 are the serial tail: 11 needs every code task committed (its spec text describes the merged behaviour), and 12 needs 11.

---

### Task 9: `logging:false` after a failed append (`dk:M10`, false claim 9 remainder) — GROUP E, PARALLEL

**Evidence:** `dev-controller.md` M10: `logstore.c:344-345` returns −1 on a `write_all` failure but leaves `s_ready` true, so `/api/status` and `dc status` keep reporting `logging:true`/`ready:true` on a steady write error (ENOSPC from LittleFS metadata growth, a bad block) while nothing is persisted. R30: *`logging:false` after a failed append until the next successful one.*

**Files:**
- Create: `devcontroller/components/logstore/host/logstore_state.c`, `devcontroller/test/test_logstore_state.c`
- Modify: `devcontroller/components/logstore/include/logstore_rot.h` (declaration), `CMakeLists.txt` (`SRCS`), `logstore.c`, `include/logstore.h`

**Interfaces:**
- Produces: `bool logstore_accepting(bool open_ok, bool write_ok);` declared in `logstore_rot.h` (the component's existing pure-logic header) — the predicate behind `logstore_ready()`.
- Consumes: nothing new.

- [ ] **Step 1: Write the failing test** — `devcontroller/test/test_logstore_state.c`:
```c
/* dk:M10 -- what GET /api/status's `logging` (and `dc status`'s logstore.ready) must report. The
 * two inputs are deliberately separate: `open_ok` is "a log file is open" (cleared only when
 * open_new_file fails, which needs a re-init), `write_ok` is "the last append actually wrote". A
 * persistent write error must read false, and must return to true on the next successful append --
 * which is why it cannot be folded into open_ok: logstore_append() early-returns on !open_ok, so a
 * single latch could never recover. */
#include "unity.h"
#include "logstore_rot.h"
void setUp(void) {}
void tearDown(void) {}
static void test_accepting_is_open_and_writing(void)
{
    TEST_ASSERT_TRUE(logstore_accepting(true, true));
    TEST_ASSERT_FALSE(logstore_accepting(true, false));
    TEST_ASSERT_FALSE(logstore_accepting(false, true));
    TEST_ASSERT_FALSE(logstore_accepting(false, false));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_accepting_is_open_and_writing); return UNITY_END(); }
```

- [ ] **Step 2: Run to fail** — `cmake --build devcontroller/test/build -j8` → `logstore_accepting` undeclared.

- [ ] **Step 3: Implement** — `host/logstore_state.c` (`return open_ok && write_ok;`) with the header doc above; add to `SRCS` and to the component comment's source list.

- [ ] **Step 4: Wire the glue** — `logstore.c`:
```c
static volatile bool s_write_ok = true;   /* dk:M10: the LAST append's write outcome. Separate from
                                            * s_ready (a file is open) so a transient/steady write
                                            * error is visible in /api/status and clears itself on
                                            * the next successful append. */
```
- `logstore_append`: on either `write_all` failure, `s_write_ok = false; return -1;`; at the end of the success path (after the fsync gate), `s_write_ok = true; return 0;`. The function's top gate stays `if (!s_ready || !rec) return -1;` — unchanged, so recovery is reachable.
- `logstore_init`'s success path sets `s_write_ok = true` alongside `s_ready = true`.
- `logstore_ready()` becomes `return logstore_accepting(s_ready, s_write_ok);`.
- `logstore.h`'s `logstore_ready` doc gains: *"False when the store is not open **or** the last append failed to write (dk:M10) — i.e. exactly 'the black-box store is accepting appends', which is what spec §4.3 defines `logging` as. A write error clears on the next successful append."*
- `main.c`'s one-shot WARN in `stream_consumer` (`:179-182`) stays as it is — add half a sentence to its comment: *"`/api/status`'s `logging` now reports this too (dk:M10), so the one WARN is no longer the only indicator."* (That line is in `main.c`, owned by Task 5 — make this edit in Task 9 and note it in the commit; the two tasks touch different lines of the file, and Step 5's build proves the merge.)

- [ ] **Step 5: Verify + commit**

```bash
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build   # green
source tools/idf-env.sh && cd devcontroller && idf.py build && cd ..                       # rc 0, 0 warnings
```
```bash
git add devcontroller/components/logstore devcontroller/test/test_logstore_state.c devcontroller/main/main.c
git commit
```
```
fix(devcontroller/logstore): report logging:false while appends are failing, true again on the next success (F3 T9, dk:M10)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 9 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 10: host tools — port identity, 35 s rule, stage abort, `ERR` lines, OTA pacing (`drv:M9`, `dk:M8`/`M9`/`L7`, `drv:M8` mitigation) — GROUP F, PARALLEL

**Evidence:** `drivers-build-tools.md` M9 (both tools auto-pick the first `usbserial` port; opening the lap-timer's port is exactly what the 35 s window forbids, and `devkit.py flash` would stream a raw image into the lap-timer's line reader) and M8 (`ota_push.py` streams 4096 B blocks with no handshake); `dev-controller.md` M8 (`stage_image` never polls for an early `STAGE-ERR`), M9 (`push_and_wait` discards every `ERR` line and reports a 120 s timeout), L7.

**Files:**
- Modify: `tools/devkit.py`, `tools/ota_push.py`, `tools/test_devkit.py`

**Interfaces (produced; all pure or `ser`-in, so `FakeSerial` can drive them):**
- `devkit.py`: `TRIAL_WINDOW_S = 35`, `trial_window_note() -> str`, `classify_push_line(line) -> (kind, payload)` with `kind ∈ {"progress","result","err",None}`, `_poll_err_line(ser) -> str|None`, `probe_is_devkit(ser, timeout=PROBE_TIMEOUT_S) -> bool`, `PROBE_TIMEOUT_S = 2.0`.
- `ota_push.py`: `OTA_PACE_MS = 25`, `TRIAL_WINDOW_S = 35`, `trial_window_note() -> str`, `probe_laptimer(ser, deadline) -> bool`, `stream_image(ser, data, pace_ms=OTA_PACE_MS)`.
- Both: `open_serial`-style opens with DTR and RTS deasserted **before** `open()`.

- [ ] **Step 1: Write the failing tests** — in `tools/test_devkit.py`, give `FakeSerial` the attribute the new polling needs, then add the cases:

```python
    @property
    def in_waiting(self):
        """pyserial's bytes-ready count. Modelled as "everything still queued", which is what lets
        the stage loop's non-blocking poll (devkit.py _poll_err_line) see a device line that has
        already arrived without ever adding a blocking read per chunk."""
        return len(self._rx) if self._chunks is None else (len(self._chunks[0]) if self._chunks else 0)
```

```python
class ClassifyPushLineTest(unittest.TestCase):
    """dk:M9 -- cmd_flash.c answers `ERR nothing staged`, `ERR nothing staged (claim expired or
    taken by /api/flash)`, `ERR push (image discarded, re-stage)` and `ERR push timeout` through
    flash_err(). push_and_wait skipped every one of them as noise and blocked the full
    PUSH_TIMEOUT_S, then reported "timeout waiting for flash result" -- the wrong diagnosis, and
    the recovery guidance ("re-stage", not "retry push") was lost."""

    def test_classifies_each_shape(self):
        self.assertEqual(classify_push_line("pushing 42%"), ("progress", "pushing 42%"))
        self.assertEqual(classify_push_line("flash result 0x0000"), ("result", "0x0000"))
        self.assertEqual(classify_push_line("flash result link -4"), ("result", "link -4"))
        self.assertEqual(classify_push_line("ERR push (image discarded, re-stage)"),
                         ("err", "push (image discarded, re-stage)"))
        self.assertEqual(classify_push_line("I (123) tag: noise"), (None, None))

class PushErrTest(unittest.TestCase):
    def test_err_line_raises_at_once_with_the_device_message(self):
        ser = FakeSerial(rx=b"ERR nothing staged\r\n")
        with self.assertRaises(DevkitError) as ctx:
            push_and_wait(ser, timeout=5.0)
        self.assertIn("nothing staged", str(ctx.exception))
        self.assertNotIn("timeout", str(ctx.exception))

class StageAbortTest(unittest.TestCase):
    """dk:M8 -- on an early STAGE-ERR the host kept pushing the remaining ~55 s (600 KB) of image,
    whose tail then landed on the REPL line editor as command characters."""

    def test_stops_sending_on_an_early_stage_err(self):
        fd, path = tempfile.mkstemp(prefix="devkit_test_")
        os.close(fd)
        try:
            with open(path, "wb") as f:
                f.write(b"A" * (4 * 4096))                 # four CHUNKs
            ser = FakeSerial(rx=b"STAGE-READY\r\n" b"STAGE-ERR write\r\n")
            with self.assertRaises(DevkitError) as ctx:
                stage_image(ser, path)
            self.assertIn("STAGE-ERR write", str(ctx.exception))
            self.assertLess(len(ser.written), 2 * 4096)     # the command line + at most one chunk
        finally:
            os.remove(path)

class ProbeTest(unittest.TestCase):
    """drv:M9 -- auto-picking the first /dev/cu.usbserial-* can select the LAP-TIMER (the
    -0001/-3 assignment swaps between re-plugs), and opening that port inside the 35 s OTA trial
    window rolls the image back."""

    def test_accepts_a_devkit_status_object(self):
        ser = FakeSerial(rx=b'{"version":"1.2","uptime_s":5,"link":{"connected":true}}\r\n')
        self.assertTrue(probe_is_devkit(ser, timeout=1.0))
        self.assertEqual(bytes(ser.written), b"dc status --json\r")

    def test_rejects_the_lap_timer_console(self):
        ser = FakeSerial(rx=b"Unrecognized command\r\nlaptimer> ")
        self.assertFalse(probe_is_devkit(ser, timeout=0.2))

    def test_rejects_silence_and_a_foreign_json_object(self):
        self.assertFalse(probe_is_devkit(FakeSerial(rx=b""), timeout=0.05))
        self.assertFalse(probe_is_devkit(FakeSerial(rx=b'{"proto":1,"state":0}\r\n'), timeout=1.0))

class TrialWindowNoteTest(unittest.TestCase):
    def test_note_states_the_window_and_the_consequence(self):
        for note in (trial_window_note(), ota_trial_window_note()):
            self.assertIn("35", note)
            self.assertIn("0x0806", note)

class OtaPushTest(unittest.TestCase):
    """drv:M8/M9 -- ota_push.py's own helpers, driven by the same FakeSerial."""

    def test_probe_laptimer_sees_the_prompt_and_refuses_a_devkit(self):
        self.assertTrue(probe_laptimer(FakeSerial(rx=b"\r\nlaptimer> "), time.monotonic() + 1.0))
        self.assertFalse(probe_laptimer(FakeSerial(rx=b"\r\ndevkit> "), time.monotonic() + 0.2))

    def test_stream_image_writes_exactly_the_image_with_and_without_pacing(self):
        data = b"B" * (2 * 4096 + 7)
        ser = FakeSerial()
        stream_image(ser, data, pace_ms=0)
        self.assertEqual(bytes(ser.written), data)
        ser = FakeSerial()
        stream_image(ser, data, pace_ms=1)                  # 1 ms: two pauses, still exact bytes
        self.assertEqual(bytes(ser.written), data)
```
Extend the import block with `classify_push_line, probe_is_devkit, trial_window_note, _poll_err_line` from `devkit` and add `from ota_push import probe_laptimer, stream_image, trial_window_note as ota_trial_window_note` plus `import time` (both tool modules import `serial` only inside `main()`, so they import cleanly without pyserial).

- [ ] **Step 2: Run to fail** — `python3 -m unittest tools/test_devkit.py` → `ImportError: cannot import name 'classify_push_line'`.

- [ ] **Step 3: `devkit.py` — the three reply-handling fixes**

```python
PROBE_TIMEOUT_S = 2.0           # `dc status --json` spends ~1.05 s measuring its rate window
TRIAL_WINDOW_S = 35             # OTA trial window: validation at 30 s + a sensors-ok check


def trial_window_note():
    """The rule that must be printed after every successful push (dk:L7, drv:M9). It is not
    advice: resetting the lap-timer -- or merely OPENING its USB port, which asserts DTR/RTS on
    the auto-reset circuit -- before it validates makes the bootloader roll the image back."""
    return ("note: the lap-timer is now in its OTA trial window -- do NOT reset it or open its USB "
            "port for %d s; an early reset rolls the image back (0x0806)." % TRIAL_WINDOW_S)


def classify_push_line(line):
    """Classify one line of `flash push` output: ("progress", line) | ("result", "0x0000"|"link -4")
    | ("err", reason) | (None, None) for console/log noise. The `ERR ` arm is dk:M9."""
    if line.startswith("pushing "):
        return ("progress", line)
    if line.startswith("flash result "):
        return ("result", line[len("flash result "):].strip())
    if line.startswith("ERR "):
        return ("err", line[len("ERR "):].strip())
    return (None, None)


def _poll_err_line(ser):
    """Non-blocking peek for one device line: returns it (ANSI-stripped, trimmed) when bytes are
    already waiting, else None. Never blocks -- uses `in_waiting` when the serial-like object has
    it -- so `flash stage`'s upload loop can poll between chunks without paying a read timeout per
    chunk (which on a 600 KB image would be 147 s of pure waiting)."""
    if getattr(ser, "in_waiting", 0) <= 0:
        return None
    raw = ser.readline()
    if not raw:
        return None
    return _strip_ansi(raw.decode("latin-1", errors="replace")).strip()
```
`stage_image`'s upload loop, after each `sent = end`:
```python
        early = _poll_err_line(ser)
        if early is not None:
            if early.startswith("STAGE-ERR"):
                raise DevkitError(early)          # dk:M8: stop NOW; do not push the rest
            if early.startswith("STAGE-END"):     # the device finished early (a short image)
                parts = early.split()
                return parts[1] if len(parts) > 1 else early
```
`push_and_wait`'s loop body becomes:
```python
        kind, payload = classify_push_line(clean)
        if kind == "progress":
            print(payload)
            sys.stdout.flush()
        elif kind == "err":
            raise DevkitError(payload)            # dk:M9
        elif kind == "result":
            if payload != "0x0000":
                raise DevkitError("flash result %s" % payload)
            return payload
```
`_cmd_flash`, after `print("result: %s" % result)`: `print(trial_window_note())`.

- [ ] **Step 4: `devkit.py` — port identity (`drv:M9`)**

```python
def probe_is_devkit(ser, timeout=PROBE_TIMEOUT_S):
    """True when `ser` answers `dc status --json` with a dev-kit status object inside `timeout` s.
    `version` + `link` together are unique to cmd_dc.c's reply: the lap-timer console answers an
    unknown verb with plain text, and its own `status` JSON has neither key."""
    try:
        ser.write(b"dc status --json\r")
        ser.flush()
        obj = read_json(ser, timeout)
    except DevkitError:
        return False
    return isinstance(obj, dict) and "version" in obj and "link" in obj
```
`open_serial`: build the port unopened so neither control line is ever asserted:
```python
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 1.0
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False            # set BEFORE open(): pyserial applies both at open, so the
    ser.rts = False            # auto-reset circuit is never pulsed on purpose (drv:M9)
    ser.open()
```
(keep the existing docstring's honesty — opening a port still cannot be *guaranteed* not to reset a board — and the prompt-settle wait.)

`main()`: probe whenever the port was auto-picked, and always for `flash`:
```python
    probe_needed = (args.port is None) or (args.cmd == "flash")
    if probe_needed and not args.force_port:
        if not probe_is_devkit(ser):
            print("FAIL: %s did not answer `dc status --json` within %.0f s -- this is probably the "
                  "LAP-TIMER's port, not the dev-kit's (the usbserial-0001/-3 assignment swaps "
                  "between re-plugs). Pass --port explicitly, or --force-port to proceed anyway.\n"
                  "%s" % (port, PROBE_TIMEOUT_S, trial_window_note()), file=sys.stderr)
            ser.close()
            return 2
```
with `ap.add_argument("--force-port", action="store_true", help="skip the `dc status` identity probe (recovery only)")`. Auto-pick stays `_default_port()`; add one sentence to its docstring: *"the first match is only a candidate — `main()` probes it before any command runs."*

- [ ] **Step 5: `devkit.py` — help text** — in the module docstring's command table and `_cmd_lt`'s usage string, note the relay is now generic (Task 6): *"`lt <cmd...>`: any lap-timer console verb (`errlog`, `diag`, `dbg …`, `close`, `read <offset>` included). The dev-kit's own verbs (`dc`, `link`, `flash`, `stream`, `selftest`, `help`) are refused rather than relayed — type those without the `lt` prefix."*

- [ ] **Step 6: `ota_push.py` — pacing, banner probe, trial note, safe open**

```python
OTA_PACE_MS = 25      # drv:M8 MITIGATION (not the fix -- #57 is the per-chunk ACK transport). The
                      # lap-timer's console RX ring is a fixed 256 B (IDF esp_console, no Kconfig)
                      # = ~22 ms at 115200, and it blocks in esp_ota_write for ~10-40 ms once per
                      # 4096 B chunk (export_serial.c's own numbers): a worst-case write overruns
                      # the ring by ~18 ms of wire time. Host and device count the same bytes from
                      # 0, so pausing 25 ms at each CHUNK boundary -- the moment the device is in
                      # that write -- covers it with 7 ms of margin. Cost: 147 pauses on a 600 KB
                      # image = 3.7 s on a ~55 s push. Far under the device's 3 s per-read stall
                      # window, so it can never look like a stall. Use --pace 0 to disable.
TRIAL_WINDOW_S = 35
LT_PROMPT = b"laptimer>"
```
- `stream_image(ser, data, pace_ms=OTA_PACE_MS)`: after each flushed block, `if pace_ms > 0 and sent < total: time.sleep(pace_ms / 1000.0)`.
- `probe_laptimer(ser, deadline)`: write a bare `\r`, then read in bounded iterations until `LT_PROMPT` appears in the last 256 bytes or the deadline passes; return the bool. (A bare CR on the lap-timer console just reprints the prompt — it is the one inert probe byte.)
- `main()`: `--pace` (`type=int, default=OTA_PACE_MS`) and `--force-port`; open the port with the same unopened-then-`open()` dance and `dtr`/`rts` false; then, unless `--force-port`:
```python
        if not probe_laptimer(ser, time.monotonic() + 2.0):
            print("FAIL: %s did not print the lap-timer prompt within 2 s -- wrong port? "
                  "(--force-port to proceed anyway)" % args.port, file=sys.stderr)
            return 2
```
- Replace the success line with the rule: `print("PASS: image applied + verified (§19.4).")` followed by `print(trial_window_note())`, and delete the old "the device reboots into the new slot" sentence (it invited exactly the reset the rule forbids). Add the same `trial_window_note()` helper (3 lines, deliberately duplicated — `ota_push.py` is a standalone single-file tool with no shared module; say so in its docstring).

- [ ] **Step 7: Run to pass**

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
python3 -m unittest tools/test_devkit.py -v      # 17 existing + 11 new, all pass
python3 -c "import ast,sys; [ast.parse(open(f).read()) for f in ('tools/devkit.py','tools/ota_push.py')]"
```

- [ ] **Step 8: Commit**

```bash
git add tools/devkit.py tools/ota_push.py tools/test_devkit.py
git commit
```
```
fix(tools): probe the port's identity before writing, stop staging on STAGE-ERR, surface console ERR lines, print the 35 s OTA rule, pace the USB OTA stream (F3 T10, drv:M9 + dk:M8/M9/L7, drv:M8 mitigation)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 10 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 11: Spec amendments, bench items, issues (`dk:M5` filed, `dk:H2`/`drv:M8` → #57, false claims 3–7) — GROUP G, SERIAL (after Tasks 1–10)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` (§5.2 rows for `lt`/`lt shell`/`selftest framing`, §6 Bridge bullet, §7 Link-busy + Bridge-safety bullets), `docs/bench/next-bench-day.md`
- GitHub: one new issue; one comment on #57

- [ ] **Step 1: §5.2 — three rows** (`:192`, `:193`, `:199`)
- `lt <console cmd…>`: append *"Any verb not implemented locally is relayed raw through the bridge filter (`lt_relay`, F3 T6): framed replies and plain-text `dbg` output both work, bounded by 1.5 s of silence or a 15 s cap. The dev-kit's own verbs (`dc`, `link`, `flash`, `stream`, `selftest`, `help`) are refused, not relayed."*
- `lt shell`: keep "resyncs on exit" — it is now true (`linkhost_bridge_end` drains to quiet and calls `linkhost_demux_resync`, F3 T3) — and append *"(drain-to-quiet + demux resync, both bounded)"* so the claim names its mechanism.
- `selftest framing`: *"PASS if every `---BEGIN`/`---END` marker line is terminated by exactly one CR before its LF (a bare-LF or `\r\r\n` marker line FAILS); other lines fail only on a doubled CR, and a truncated trailing line is not judged (the capture is 256 B)."*

- [ ] **Step 2: §6 and §7**
- §6 Bridge bullet (`:317-318`): *"**Bridge:** `lt shell` takes the link mutex, pauses the demux, pumps bytes both ways, and on exit drains UART1 to quiet and resyncs the demux parser state (the record ring and `linkstats` counters survive) before the RX task resumes."*
- §7 Link-busy bullet (`:328-329`): *"Link busy (download, flash push, or bridge holds the mutex): `/api/*`, `lt` **and the session/log download worker** get BUSY via the bounded mutex take → 503 / `busy`. `dc status` never blocks (no mutex)."*
- §7 Bridge-safety bullet (`:331`): unchanged in meaning, now accurate — append *"(`rx_park_and_drain` + `linkhost_demux_resync`)"*.
- Add one new §7 bullet, directly after Link busy: *"**Download bounds:** an announced body over 2 MiB (`LH_DL_BODY_MAX`) is rejected as a protocol error rather than streamed, the absolute window is capped at 20 min (`LH_DL_HARD_MAX_MS`) from the header, and the no-bytes idle deadline is refreshed only by body bytes before `---END` — so neither a corrupted size nor the lap-timer's resumed 10 Hz stream can hold the link mutex (dk:M1)."*
- Add one new §7 bullet: *"**AP passphrase:** 12 random characters from NVS `dc/psk`, generated once, printed at boot and by `dc status`, rotated by `dc psk reset` (R16). Never derived from the MAC — the SoftAP BSSID in every beacon is a published function of it."*

- [ ] **Step 3: Text handed to F4** (files F3 does not own — put this verbatim block in the PR body and in the handoff report, do **not** edit those files here):
- `docs/superpowers/specs/2026-09-14-lap-timer-design.md:384` (§4.6), replace the first sentence with: *"`GPS=sim` and `IMU=sim` select bench drivers (`gps_sim`, `imu_sim`) that implement the HAL from compiled-in data, not from the filesystem: `gps_sim` replays the committed `SIM_FIXES[]` capture (`components/drivers/gps_sim/sim_capture.h`, generated by `tools/sim/gen_sim_capture.sh`) at its real-time rate and can also synthesise the shared drag profile or a parked bike (`dbg sim drag|laps <n>|park`); `imu_sim` synthesises a static upright 1 g plus a body-X component during a DRAG run. There is no `/sim/*` path anywhere in the tree."*
- Same file `:260` (the tree listing), replace `bench driver: replays /sim/gps.ubx or synthesises a circuit` with `bench driver: replays the compiled-in sim_capture.h; dbg sim drag|laps|park`.
- §4.6's drag sentence ("for `drag`, continues from that same position too"): *"for `drag`, the run starts at the position the sim was last at — latched once for the whole run (`s_drag_origin`), so the run is continuous in position at the switch and every later frame steps by one 200 ms interval's distance."*
- `docs/superpowers/specs/2026-09-20-dev-controller-B-design.md:94-95` ("Download semantics"): *"`open <id> <fmt>` emits the whole framed file. `read <offset>` is specified as a resume of an interrupted transfer, but **no dev-kit code implements it** (`LT_CMD_READ` is declared in `lt_proto.h` and referenced nowhere in `devcontroller/`): a CRC/transport failure mid-download aborts the HTTP socket with no resume path — see issue <N> from the 2026-10-13 audit (dk:M5). The CRC arrives in `---END` after the body has already streamed to the browser, so a mismatch is handled by aborting the socket rather than a pre-validated body."* (substitute the issue number from Step 5).

- [ ] **Step 4: Text handed to F2a** (`components/drivers/export_serial/export_serial.c:1079-1084`, that plan's file): the comment's last sentence must become: *"The robust fix is a per-chunk OTA-ACK handshake on the host side — issue #57, not implemented. Both pushers (`tools/ota_push.py`'s `--pace`, the dev-kit's `OTA_PACE_MS`) instead pause 25 ms at each `OTA_RECV_CHUNK` boundary, which covers the ~18 ms worst-case ring deficit but provides no feedback and no retry."* Also note for F2a that the same comment's claim about "a byte-offset mismatch in `ota_data()`" is false (`drv` false claim 4) — `ota_data` is handed the device's own `recvd` counter, so the real outcome is `OTA-ERR timeout` or the SHA mismatch at `ota_end`.

- [ ] **Step 5: File `dk:M5`, comment on #57**

```bash
gh issue create --label enhancement \
  --title "Dev-kit: `read <offset>` download resume is specified but not implemented" \
  --body 'From the 2026-10-13 audit (`.superpowers/reviews/2026-10-13/dev-controller.md`, finding M5).

`2026-09-20-dev-controller-B-design.md` §"Download semantics" and §4.4 keep `open`/`read` on the framed path with `read <offset>` as "a resume of an interrupted transfer". `LT_CMD_READ` is declared in `components/app/include/app/lt_proto.h:136` and referenced by nothing in `devcontroller/`. A CRC or transport failure mid-download aborts the HTTP socket (`webapi.c:454-461`) and the whole file must be fetched again — on a multi-MB `.log` over a 115200 baud link that is minutes.

Fix wave F3 amended the spec text to say so (it was a false claim). Implementing it needs: the lap-timer side (`cmd_read_c` exists and resumes the LAST open transfer — confirm it survives a dev-kit-side `open` + partial read), a byte counter in the `lh_dl_*` sink so the dev-kit knows the resume offset, and an answer for the browser half (a chunked HTTP response cannot be resumed mid-stream, so the resume has to happen below the HTTP layer and be invisible to the client). Owner: unassigned; not bench-gated.'

gh issue comment 57 --body 'Audit 2026-10-13 re-confirmed this on both pushers and quantified it (`.superpowers/reviews/2026-10-13/dev-controller.md` H2, `drivers-build-tools.md` M8):

- The lap-timer'"'"'s `ota recv` console RX ring is a fixed 256 B (IDF `esp_console_repl_chip.c`; no Kconfig) ≈ 22 ms at 115200, and it blocks in `esp_ota_write` for ~10–40 ms once per 4096 B chunk. A worst-case write therefore overruns the ring by ~18 ms of wire time (~200 B).
- `devcontroller/.../linkhost.c` streamed 512 B chunks back-to-back with `LINK_TX_RING = 0`; `tools/ota_push.py` streamed 4096 B blocks. Neither waited for anything. It fails safe (SHA-256 + ECDSA gate `esp_ota_end`), so the symptom is an unreliable push, not a wrong image.

Fix wave F3 added a **mitigation only**: both pushers now pause 25 ms at each 4096 B boundary (`OTA_PACE_MS` / `--pace`), which covers the deficit with 7 ms of margin at a cost of 3.7 s on a ~55 s 600 KB push. There is still no ACK, no offset feedback and no retry — this issue remains the real fix, and the `export_serial.c` comment that claimed the handshake "lands with the dev-controller integration step" has been corrected.'
```

- [ ] **Step 6: Bench items** — append to `docs/bench/next-bench-day.md`'s "Open items" (continuing the numbering; items 12–17):

```markdown
- [ ] **12. Drag run shows no `EV_FIX_LOST`** (F3 T1, drv:H2). `config set {"mode":"drag"}`, wait for
  READY/ARMED, then `dbg sim drag` via `lt shell`. Expect: NO `EV_FIX_LOST` and no `pipe: fix invalid`
  line for the whole run (before the fix, every frame past ~104 km/h was rejected and the bench-day-3
  drag pass was effectively IMU-only); the 100-0 brake gate hits, which it can only do with the 5 Hz
  Doppler re-anchor working. Also: `dbg sim laps 1` → `dbg sim drag` → `dbg sim park` in sequence, and
  check `lt status`'s free_kb/sessions stay sane across the switches (no gps_us rewind).
- [ ] **13. AP PSK printed and rotatable** (F3 T5, dk:H1). Cold boot the dev-kit on USB: the banner
  prints `SoftAP up: ssid=laptimer-dev psk=<12 chars>`. Join the AP with it; `devkit.py status` shows
  the same `ap_psk`. Reboot: the SAME passphrase (persisted in NVS `dc/psk`). `dc psk reset` prints a
  NEW one, `applied: true`, and the joined client is dropped and must rejoin with the new value.
  Then reboot again: the new one persists. **Redact `ap_psk` from anything pasted into a PR or docs.**
- [ ] **14. `lt errlog` and friends relay** (F3 T6, dk:M4). On the dev-kit console: `lt errlog` prints
  the lap-timer's error ring, `lt diag` its diagnostics JSON, `lt dbg mem` its heap lines, `lt close`
  acks — each ending with the `-- rt N ms, attempts 1, rc 0` summary. `lt dc status` is REFUSED with
  "that is a dev-kit verb". `devkit.py lt errlog` returns the same body as JSON with `rc: 0`.
- [ ] **15. Logs tab lists every file with its creation time** (F3 T4, dk:L2). With ≥ 20 rotated
  dev-kit log files, the Logs tab lists them all (not 16) with a Created (UTC) column; note the load
  time (one open+read+close per file). The `rx_us` note reads "since the dev-kit last booted".
- [ ] **16. `logging` goes false while appends fail** (F3 T9, dk:M10). Fill the `logs` partition (leave
  the link streaming until rotation has run several times), or mount it read-only, and confirm
  `/api/status` and `dc status` report `logging:false`/`logstore.ready: false` while nothing is being
  written, then `true` again once appends succeed.
- [ ] **17. A paced USB OTA push completes** (F3 T10, drv:M8 mitigation). `tools/ota_push.py --port <lt>
  --image dist/<signed>.bin` with the dev-kit held in reset: expect `OTA-END 0x0000` and a total time
  ~3-4 s longer than the un-paced push, then the printed 35 s rule — and honour it. Repeat with
  `--pace 0` and record whether it also completes (the mitigation's value is the comparison).
  Note: a `devcontroller/web/*` change does NOT reach the dev-kit over OTA — the SPA lives in the
  `www` LittleFS partition and needs `cd devcontroller && idf.py -p <dk> flash` over USB.
```
Also add one line under "Standing rules": *"`tools/devkit.py` now refuses an auto-picked port that does not answer `dc status --json` within 2 s and opens every port with DTR/RTS deasserted — if it refuses, you have the lap-timer's port (identify, don't `--force-port`)."*

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md docs/bench/next-bench-day.md
git commit
```
```
docs(devkit): spec §5.2/§6/§7 match the shipped relay, bridge drain, download bounds and NVS AP passphrase; bench items 12-17 (F3 T11, dk:M3/M4/M5/M6/M1/H1)
```

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 11 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> Resume plan docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3; branch fix-sim-devkit.

---

### Task 12: Final gate run + PR (design §4) — GROUP G, SERIAL (last)

- [ ] **Step 1: Full gate run on the branch**

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8 \
  && ctest --test-dir test/build                                                  # 47/47, 100%
cmake -S devcontroller/test -B devcontroller/test/build -DCMAKE_BUILD_TYPE=Debug \
  && cmake --build devcontroller/test/build -j8 \
  && ctest --test-dir devcontroller/test/build --output-on-failure                # 34/34, 100%
python3 -m unittest tools/test_devkit.py                                          # 28 tests, OK
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation              # 0, rc 0
python3 tools/lint/venue_announce.py                                              # 0, rc 0
python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py \
        test/build/compile_commands.json                                          # 0 diagnostics
node --check devcontroller/web/app.js
source tools/idf-env.sh                                                           # must print v5.3.2
./build.sh moto_sim build && ./build.sh moto_neo6m build && PANEL=ws29v2 ./build.sh moto_sim build \
  && idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build      # all rc 0, 0 warnings
(cd devcontroller && idf.py fullclean && idf.py build)                             # rc 0, 0 warnings, www.bin repacked
```
Expected test counts: dev-kit 34 = 28 existing + `test_linkhost_dl_cap`, `test_sse_cursor`, `test_www_path`, `test_dc_psk`, `test_logstore_state` (+1 margin if a task splits a file); `tools/test_devkit.py` 28 = 17 existing + 11 new. Report the free static DRAM line from `moto_sim` (baseline 4192 B; this plan should move it by ≤ 32 B) and `moto_neo6m` (baseline 5616 B).

- [ ] **Step 2: Re-run all three reproducers against the fixed tree** and paste the three outputs into the PR body:

```bash
SC=$(mktemp -d)
cc -I devcontroller/components/linkhost/include -I components/app/include -o $SC/dl \
   .superpowers/reviews/2026-10-13/dl.c devcontroller/components/linkhost/host/linkhost_proto.c && $SC/dl
cc -I devcontroller/components/devconsole/include -o $SC/fr .superpowers/reviews/2026-10-13/fr.c \
   devcontroller/components/devconsole/host/selftest_eval.c && $SC/fr
./test/build/test_sim_profile                   # the drag.c scenario, now as a permanent test
```
Expected: `dl` → `LH_DL_ERR`/`LINKHOST_E_PROTO` on the corrupted header; `fr` → the bare-LF reply fails with `reason='cr'`; `test_sim_profile` → 4/4 with max step 12.4 m.

- [ ] **Step 3: Open the PR**

```bash
git push -u origin fix-sim-devkit
gh pr create --title "Fix wave F3 — sim drivers + dev-kit: drag origin, AP PSK, download bounds, relay, host tools" --body "<body>"
```
The body must contain: the finding-id → task table from the Self-Review below; the three reproducer outputs; the DRAM lines; the dev-kit `.bss` delta (~2.6 KB) and `heap_free` note; the exact spec text handed to **F4** (Task 11 Step 3) and to **F2a** (Step 4); the new issue number and the #57 comment link; and the explicit statement that `dk:H2`/`drv:M8` are **mitigated, not fixed** (#57).

- [ ] **Step 4: CI** — `gh pr checks --watch`. Expected green: hygiene, host-tests, dc-host-tests, tools-tests, tracks-generated, build (moto_neo6m), build (moto_sim), build (devcontroller) — plus static-analysis if F4 has already merged (R13).

- [ ] **Step 5: Hand over.** The agent never merges and never flashes. Report: the ten commits, the test counts, the DRAM lines, the reproducer outputs, the two spec hand-offs, the issue number, and the six bench items (12–17) awaiting the bench day after F2b. **Create no gate tag** — repo convention tags a plan only for a bench gate it actually ran, and F3 runs no bench of its own (same rule F0 followed).

- [ ] **CHECKPOINT** — write the ledger line, push the branch, and record the resume prompt, so the session can `/clear` here and lose nothing:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
mkdir -p .superpowers/sdd/2026-10-13-fix-F3
printf 'Task 12 complete %s\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
printf 'RESUME: F3 complete: PR open on branch fix-sim-devkit, awaiting the merge call from the user (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3. Nothing to resume.\n' >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
git push -u origin fix-sim-devkit
tail -2 .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Resume prompt recorded verbatim (the next session is started with exactly this line):
> F3 complete: PR open on branch fix-sim-devkit, awaiting the merge call from the user (ledger .superpowers/sdd/2026-10-13-fix-F3/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/f3. Nothing to resume.

- [ ] **SESSION 3 END** — the plan's last checkpoint: Task 12 Step 1 already ran the full firmware + linter sweep, so this records the three suite counts next to the PR number and closes the ledger:

```bash
cd /Users/benji/projects/personal/lap-timer-worktrees/f3
cmake --build test/build -j8 && ctest --test-dir test/build                                 # 47/47
cmake --build devcontroller/test/build -j8 && ctest --test-dir devcontroller/test/build --output-on-failure   # 34/34
python3 -m unittest tools/test_devkit.py                                                    # 28 tests, OK
printf 'SESSION 3 END: host %s, dc-host %s, tools %s, head %s\n' "47/47" "34/34" "28 tests, OK" \
  "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F3/progress.md
```
Any red here is fixed in THIS session, never carried across a `/clear`.

---

## Self-Review

**1. Coverage — every finding id this plan owns, in exactly one task.**

| Finding | Task | Finding | Task |
|---|---|---|---|
| `dk:H1` (AP PSK, R16) | 5 | `dk:L1` (413 vs 400) | 4 |
| `dk:H2` (unpaced OTA) | **filed #57** + mitigation 3, 10, 11 | `dk:L2` (/api/logs) | 4 |
| `dk:M1` (download cap + idle) | 2 (pure) + 3 (wiring) | `dk:L3` (len bounds) | 2 |
| `dk:M2` (portMAX_DELAY) | 3 (+SPA copy 4) | `dk:L4` (static path) | 4 |
| `dk:M3` (framing CR) | 7 | `dk:L5` (`rx_us` since boot) | 4 |
| `dk:M4` (relay, R17) | 6 | `dk:L6` (torn 64-bit stamp) | 3 |
| `dk:M5` (`read <offset>`) | **filed** 11 + spec text | `dk:L7` (35 s rule) | 8, 10 |
| `dk:M6` (bridge drain) | 3 | `dk:L8` (fake STATUS) | 2 |
| `dk:M7` (SSE guard) | 4 | `drv:H2` (drag origin, R29) | 1 |
| `dk:M8` (stage abort) | 10 (host) + 8 (device) | `drv:L23` (`gps_configure`) | 1 |
| `dk:M9` (`ERR` lines) | 10 | `drv:M8` (ota_push pacing) | 10 + #57 |
| `dk:M10` (`logging` stale) | 9 | `drv:M9` (port identity) | 10 |
| `dk:M11` (OTA_READY_TMO) | 3 | | |
| False claims 1 (SSE) 2 (drain) 3 (`lt shell` spec) 4 (relay spec) 5 (bounded take spec) 6 (framing spec) 7 (ACK comment) 8 (heartbeat) 9 (`logstore_entry_t`) | 4, 8, 11, 11, 11, 11, 3+11, 3, 4+9 | `drv` false claims 1 (gps_sim) 3 (§4.6 `/sim/*`) | 1, 11 (text for F4) |

"Not findings" (the reviewer's verified-correct list: id validation, SPA escaping, `multipart.c`, `stage_bounds`/`otastage`, `flashctl`'s claim model, `stream_log_jsonl`, logstore rotation) need nothing and are touched by no task — except `/api/logs`, whose id handling Task 4 leaves exactly as it is.

**2. Ruling coverage.** R16 → Task 5 (namespace `dc`, key `psk`, 12 chars, `esp_random` via `esp_fill_random`, unambiguous 32-char alphabet, generated once, printed at boot and in `dc status`, `dc psk reset`). R17 → Task 6 (deny-list of dev-kit-local verbs; `bridge_filter` unchanged by that task). R29 → Task 1 (`s_drag_origin` latched in `gps_sim_rearm`; `gps_configure` re-anchors `s_clock`; host-tested through the extracted pure `sim_drag_lat_e7`, because `gps_sim.c` includes `esp_timer.h` and `test_sim_profile.c`/`test_sim_clock.c` link only `sim_common/host/*.c`). R30 → Tasks 2, 3, 4, 8, 9, 10, item by item (`DL_BODY_MAX` 2 MiB, `DL_HARD_MAX_MS` 20 min, progress-fed idle timer, bounded `linkhost_download_cmd` take → 503, framing CR rule, `linkhost_bridge_end` drain + demux reset, `>= BC_CAP`, `stage_image` STAGE-ERR poll, `push_and_wait` `ERR` parse, `s_ready`/`s_write_ok`, `OTA_READY_TMO_MS` 3000 → 15000, both tools' port probe + 35 s rule). R20 → no padding assertions added; the only new assertions are real preconditions in pure functions (`dist_m >= 0`, `baud > 0`, `cap > 0`, `out_cap > 0`, non-NULL). Design §4 gates → Task 12. Design §5 → #57 stays filed (Task 11 Step 5), `dk:M5` newly filed.

**3. Placeholder scan.** No "TBD"/"TODO"/"similar to Task N", and no elided function body: every function this plan adds is written out in full, including `psk_store` (open/set_str/commit/close with the error log), `lt_relay_join` (the printable-ASCII join from `argv[1]`), both `jsonw` report blocks (`lt_relay`, `dc_psk_cmd`), and `linkhost_demux_resync`'s complete field list (transcribed from `s_dx`'s struct, with the reason the ring and rawcap are excluded). Test code carries literals, not symbols — the drag origin is `-338900000`. The one thing deliberately left to the implementer is which existing lines a *rewritten comment* replaces, and each of those is pinned by file:line in the Cleanup table. One cross-file edit is called out explicitly (Task 9 touching `main.c`'s comment while Task 5 owns the file).

**4. Checkpointing.** Every one of the 12 tasks ends with a `CHECKPOINT` step (ledger line + push + verbatim resume prompt for Task N+1), and Tasks 4, 8 and 12 additionally end with a `SESSION N END` step that re-runs both host suites and `tools/test_devkit.py`. The session plan above groups the work 4 + 4 + 4. The ledger path `.superpowers/sdd/2026-10-13-fix-F3/progress.md` is git-ignored (`.gitignore:18`), so no checkpoint ever dirties the tree a gate then measures.

**5. Type consistency.** `lh_dl_cap_us(uint32_t, int64_t, uint32_t) -> int64_t`, `lh_dl_progress(const lh_dl_ctx_t *) -> uint32_t`, `linkhost_demux_resync(void)` — declared in Task 2, used in Task 3 with those exact types. `sim_drag_lat_e7(int32_t, double) -> int32_t` + `SIM_DEG_LAT_M` — Task 1 only. `sse_cursor_fix(uint32_t, uint32_t, uint32_t) -> uint32_t`, `www_path_ok(const char *) -> bool`, `CFG_DIFF_E_PARSE`/`CFG_DIFF_E_SIZE` — Task 4 only. `dc_psk_format(const uint8_t *, size_t, char *, size_t)`, `dc_psk_valid(const char *) -> bool`, `dc_psk_get/dc_psk_reset(char *, size_t) -> int` with the same 0/1/<0 contract in both — Task 5, used by `main.c` and `cmd_dc.c`. `logstore_accepting(bool, bool) -> bool` — Task 9. `classify_push_line(line) -> (kind, payload)`, `probe_is_devkit(ser, timeout) -> bool`, `trial_window_note() -> str`, `probe_laptimer(ser, deadline) -> bool`, `stream_image(ser, data, pace_ms)` — Task 10, imported by `test_devkit.py` under exactly those names. `OTA_PACE_MS` is 25 in both the dev-kit (`linkhost.c`) and `ota_push.py`, and both pace at the lap-timer's 4096 B `OTA_RECV_CHUNK` boundary.

**6. Cleanup.** The "Cleanup done" table below enumerates every line this plan deletes or rewrites — 7 of the 9 dev-controller false claims live in code comments and 5 in the dev-kit spec, and all 12 appear there with their file:line. Anything in its left column that survives into the merged tree is a review failure. The four items it marks as handed to F4/F2a are the only false claims this plan identifies but does not itself remove, because those files belong to other rows of design §1.

---

## Cleanup done

Every line this plan **deletes or rewrites** because it is now false, superseded, or duplicated. A reviewer can use this as the diff's negative space: anything in the left column must not survive in the merged tree.

| What goes / changes | Where | Replaced by | Task |
|---|---|---|---|
| `deliver_drag()`'s "the position origin is `s_last_fix` … zero jump, a switch is continuous in position as well as time" (true of the first frame only) | `components/drivers/gps_sim/gps_sim.c:317-322` | the `s_drag_origin` paragraph, with the 12.4 m-per-step bound stated | 1 |
| the three `s_last_fix` reads + the inline `lat0 + (int32_t)(s.dist_m * 1e7 / 111320.0)` arithmetic | `gps_sim.c` `deliver_drag()` | `s_drag_origin` + `sim_drag_lat_e7()` | 1 |
| `gps_configure()`'s silent counter reset (no clock re-anchor) | `gps_sim.c:124-139` | `sim_clock_rearm` + `latch_drag_origin` + the `drv:L23` comment | 1 |
| `#define DL_HARD_MARGIN_US` | `devcontroller/.../linkhost.c:63` | `LH_DL_MARGIN_US` in `linkhost_proto.h` | 2, 3 |
| the inline `xfer_us`/`cap` size-scaling block (no ceiling) | `linkhost.c:440-452` | `lh_dl_cap_us(size, t_header, baud)` | 3 |
| `idle_dl` refreshed on **every** received byte | `linkhost.c:433-434` | refreshed only when `lh_dl_progress()` or the parser state advances | 3 |
| `xSemaphoreTake(s_req_mtx, portMAX_DELAY)` + its comment "worker task: OK to wait for the link (not httpd)" | `linkhost.c:405` | bounded `LINK_REQ_MTX_MS` take → `LINKHOST_E_BUSY` → 503 | 3 |
| `OTA_READY_TMO_MS 3000` (bare, unexplained) | `linkhost.c:50` | `15000` with the erase arithmetic in the comment | 3 |
| `static volatile int64_t s_last_activity_us` + `LINK_PRESENT_US` | `linkhost.c:73`, `:47` | `s_last_activity_ms` (32-bit, atomic) + `s_activity_seen` + `LINK_PRESENT_MS` | 3 |
| `linkhost_bridge_end()`'s two-line body (clear flag, give mutex) | `linkhost.c:598-604` | `rx_park_and_drain()` + `linkhost_demux_resync()` first | 3 |
| "link activity (a **`status` heartbeat** / stream frame)" | `linkhost.h:76-77` | "any UART1 byte within `LINK_PRESENT_MS`; Plan 5.6 §4.2 deleted the heartbeat" | 3 |
| `LINKHOST_E_NOTCONN` "no **heartbeat** / not yet wired" | `linkhost_proto.h:33` | "no UART1 traffic within `LINK_PRESENT_MS` / not yet wired" | 3 |
| the `DX_STREAM_HDR` branch with no `len` sanity check | `linkhost_proto.c:891-899` | `len > LT_REC_MAX + 1` → drop the 5 bytes, resync | 2 |
| "A header whose len byte exceeds `LT_REC_MAX` cannot be a real stream frame" | `bridge_filter.h:40-47` | "exceeds `LT_REC_MAX + 1` (the demux's own largest legal len)" | 2 |
| the `> BC_CAP` SSE guard **and** its comment claiming it skips "the exact slot the producer writes next" | `webapi.c:737-741` | `sse_cursor_fix(head, cursor, BC_CAP)` (`>= cap`) + a pointer to `sse_cursor.h` | 4 |
| `logstore_entry_t entries[16]` local + the `{id,bytes}`-only item format | `webapi.c:340-360` | `static s_log_entries[LOGSTORE_ROT_MAX_FILES]` + `created_unix` | 4 |
| the single `413` answer for a malformed POST body | `webapi.c:292-295` | `400 {"error":"malformed JSON body"}` via `CFG_DIFF_E_PARSE` | 4 |
| `"/www" + req->uri` with no normalization | `webapi.c:1080-1085` | `www_path_ok(uri)` whitelist before the path is built | 4 |
| "`rx_us` is the dev-kit receive time" (no mention that it restarts at 0 each boot) | `web/index.html:92-95`, `logstore_rec.h:26` | "microseconds since the dev-kit last booted; records from two boots cannot be ordered by it" | 4, 9 |
| `saveConfig`'s unconditional "Lap-timer not connected — save failed." on 503 | `web/app.js` | `link503Msg(...)` + a `400` branch | 4 |
| PSK = `"ltdev-" + esp_efuse_mac_get_default()` hex, its comment, and the now-unused `esp_mac.h` include | `devcontroller/main/main.c:84-94` | `dc_psk_get()` (NVS `dc/psk`, 12 random chars) | 5 |
| "GET /api/logs (webapi.c) uses the same 16-entry scratch array for the same reason" | `cmd_dc.c:36-39` | "`dc status` summarises the newest 16; `/api/logs` lists all of them" | 5 |
| `lt_err(json, "unknown subcommand (want status\|list\|config\|delete\|open\|shell)")` and its `return 1` | `cmd_lt.c:425-426` | `lt_verb_is_local()` deny-list + `lt_relay()` | 6 |
| the 6-verb command table in the file header and `console_register`'s `lt` help string | `cmd_lt.c:1-30`, `:455-457` | both name `<any lap-timer console cmd>` and the deny-list | 6 |
| the whole-capture CR-run loop (`cr_run > 1` only) | `selftest_eval.c:114-126` | `framing_bad_lines()` (marker lines need exactly one CR) + `mem_has()` | 7 |
| `st_framing_t.bad`'s doc ("lines preceded by MORE than one '\r'") | `selftest_eval.h:62-72` | the marker-line rule, incl. "a truncated trailing line is not judged" | 7 |
| "…are drained off UART0 for a bounded window so they **never** get interpreted as REPL command characters" | `cmd_flash.c:32-35` | the honest bound + "a host that keeps streaming will still outrun this drain" | 8 |
| `FLASH_DRAIN_ITERS 10` (200 ms, no early exit) | `cmd_flash.c:177-184` | `60` with an early exit on the first empty window | 8 |
| `logstore_ready()`'s `return s_ready;` | `logstore.c:361-364` | `logstore_accepting(s_ready, s_write_ok)` | 9 |
| `push_and_wait`'s "other console/log noise: skip" fallthrough (swallowed every `ERR` line) | `tools/devkit.py:275-289` | `classify_push_line()` with an `"err"` arm | 10 |
| `stage_image`'s write-the-whole-image loop with no reply poll | `tools/devkit.py:246-251` | `_poll_err_line()` between chunks; stop on `STAGE-ERR` | 10 |
| `serial.Serial(port, baud, timeout=1.0, dsrdtr=False, rtscts=False)` then dropping DTR/RTS *after* open | `tools/devkit.py:325-331`, `tools/ota_push.py:195` | unopened `serial.Serial()`, `dtr`/`rts` false **before** `open()` | 10 |
| "`PASS: image applied + verified; the device reboots into the new slot (§19.4).`" (invited the very reset the trial window forbids) | `tools/ota_push.py:230` | `PASS:` + `trial_window_note()` (35 s, 0x0806) | 10 |
| `_default_port()`'s docstring implying the first match is used | `tools/devkit.py:305-318` | "the first match is only a candidate — `main()` probes it" | 10 |
| §5.2 `selftest framing` row: "PASS if every marker is followed by exactly one CR before LF" (the evaluator never required a CR) | dev-kit spec `:199` | the marker-line rule as implemented, incl. the truncated-line exemption | 11 |
| §5.2 `lt` row (silently false: a 6-verb whitelist) and `lt shell` row's bare "resyncs on exit" | dev-kit spec `:192`, `:193` | the relay + deny-list sentence; "resyncs on exit (drain-to-quiet + demux resync, both bounded)" | 11 |
| §6 "restores and **drains-to-quiet on exit**" (nothing drained) | dev-kit spec `:317-318` | the same promise with `rx_park_and_drain` + `linkhost_demux_resync` named | 11 |
| §7 "…get BUSY via the bounded mutex take → 503 / `busy`, **as today**" (the download path used `portMAX_DELAY`) | dev-kit spec `:328-329` | "`/api/*`, `lt` **and the session/log download worker**", "as today" struck | 11 |
| §7 "on exit the demux is drained and resynced" (unmechanised) | dev-kit spec `:331` | the two function names appended | 11 |

**Handed to other plans, deliberately NOT edited here** (the exact replacement text is in Task 11 Steps 3–4 and must be pasted into the PR body): §4.6's `/sim/*` capture-file claim and its `PANEL`/flag-table rows, the §4.6 tree line for `gps_sim`, §4.6's drag "continues from that same position" sentence, and `2026-09-20-dev-controller-B-design.md`'s `read <offset>` resume bullet → **F4**; `export_serial.c:1079-1084`'s "lands with the dev-controller integration step" ACK claim and its false `ota_data` offset-mismatch mechanism → **F2a**.
