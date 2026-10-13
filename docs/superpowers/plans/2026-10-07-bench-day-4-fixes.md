# Bench Day 4 Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the four defects and two display/naming problems that bench day 4 (2026-10-07, image `v0.1.0-37-g4153025`) found in the work of the glyphs/drag/menu-closure plan, so PR #101 can merge: the DRAG card's ready text must follow the engine's real state; a display-only setting must never reset the drag engine (which today also corrupts the run numbering in the session log); the ui must learn the venue on every path that sets one (today `Layout:` is dead and the venue name is wrong after a sim boot or a mid-session resume, and each dead press wipes the best-lap snapshot); the gate list must make each value's owner unambiguous and honour the distance unit everywhere; and the two unit items must be named `Speed:` and `Distance:`.

**Architecture:** Four tasks, each one defect with its investigated root cause and the test that pins it. Task 1 replaces the ui's one-bit `drag_armed` mirror with the engine's four-state value carried in the existing drag snapshot. Task 2 adds a pure "does this config change the engine?" comparison and uses it to make a config reload idempotent, and stops the display-only menu items from posting one. Task 3 gives the pipeline a direct ui venue announcement used by the two paths that set a venue without the engine's scan, plus a structural linter so a future path cannot forget, and guards the Layout item from posting a no-op command. Task 4 is pure rendering and labels: one baseline per list row with a column divider and a header rule, the distance unit applied to list values as well as labels, and the menu renamed.

**Tech Stack:** ESP-IDF v5.3.2 (`source tools/idf-env.sh`), C11 under the Power-of-10 lint, host harness (CMake/CTest + Unity, PBM goldens on both canvases), Python for the repo's linters.

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` — §6.6 (gate names/units), §10.3 (venue acquisition), §11.2 (drag states), §11.4 / `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` §7 (DRAG card and gate list), §15.3 (RTC resume), §20.7 (menu). The approved in-chat design (2026-10-07): four card states, `Speed:`/`Distance:` naming, same-baseline list rows with a divider.

**Investigations (binding — read the one named in your task before you start; they contain the root cause, the exact minimal change and the test code):**
- `.superpowers/sdd/2026-10-06-glyphs-drag-menu-closure/investigation-ready-state.md` (Task 1)
- `.superpowers/sdd/2026-10-06-glyphs-drag-menu-closure/investigation-cfg-reload.md` (Task 2)
- `.superpowers/sdd/2026-10-06-glyphs-drag-menu-closure/investigation-venue-id.md` (Task 3)

## Global Constraints

- Zero warnings: host `-Wall -Wextra -Werror -Wshadow -Wconversion`; clean ccache-disabled `./build.sh moto_sim build`, `./build.sh moto_neo6m build`, `PANEL=ws29v2 ./build.sh moto_sim build` print 0 for `grep -c -i "warning:"` (rc 0); **and** `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build` (rc 0, 0 warnings) — CI's required `build (moto_neo6m)` job builds it and it globs every `test/test_*.c`; a host-only test goes into the exclude regex in `test_apps/core_selftest/main/CMakeLists.txt` in the same commit. Builds run in the FOREGROUND, one at a time, long timeout.
- Power-of-10 lint 0 violations: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` (functions > 20 code lines need ≥ 2 `CORE_ASSERT_*`/`LT_ASSERT_*`; no function pointers; no goto; functions ≤ 60 code lines; compound statements ≤ 30). gcc-16 sweep: `python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json` → 0 diagnostics.
- Host suite green (47 executables at this plan's base; screens goldens byte-exact on BOTH canvases, regenerated only by the documented workflow at the top of `test/test_screens.c` and eyeballed — never hand-edited; `/private/tmp/claude-501/-Users-benji-projects-personal-lap-timer/7ee58960-7fd0-452e-b7b7-3ab602786208/scratchpad/pbm_ascii.py <file.pbm> [y0 y1 xstep]` renders a golden as ASCII for inspection).
- DRAM: `moto_sim` free static DRAM 4080 B at this plan's base (measured on the same machine; report the delta of every firmware build). No new static ≥ 64 B.
- ui-only events (`EV_CFG_CHANGED 16`, `EV_LAP_RESET 17`, `EV_OTA 18`, `EV_CREATE 19`, and Task 3's new code) are posted ONLY with a non-blocking `xQueueSend(g_ui_evt_q, &ev, 0)`, never through `emit_event()` and never through `engine_cb()`.
- Storage mutations stay single-owner (only the logger task writes files). The track-table ownership rules in `components/core/tracks/trk.c` / `trk.h` are binding: the ui reads through `trk_get()`; a writer completes before the event that surfaces the id; no writer rewrites an id the ui may hold.
- Entering or leaving a one-shot sets `s_wants_full = true; s_screen_changed = true;`; in-place value updates are partials.
- Commit messages end with the trailer:
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
  ```
- Never flash, never open a serial port; the bench re-check is the next flash day (`docs/bench/next-bench-day.md`).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `components/app/include/app/pipeline.h` | `pipe_drag_t.state` (the engine's `drag_state()`) | 1 |
| `components/app/pipeline/pipeline.c` | publish the state; idempotent `pipeline_reload_cfg()`; `ui_post_venue()` at the two direct venue sites | 1, 2, 3 |
| `components/core/ui/include/core/ui/model.h` | `drag_armed` → `drag_run_state` (+ the `DRAG_RUN_*` values) | 1 |
| `components/core/ui/screens_moto.c` | the card's four-state text; list rows on one baseline + divider + header rule; the list's distance unit | 1, 4 |
| `components/core/ui/include/core/ui/canvas.h` | list layout constants (both canvases) | 4 |
| `components/app/ui/ui.c` | state from the snapshot; drop the reload post for display-only items; the Layout no-op guard; `Speed:`/`Distance:` labels | 1, 2, 3, 4 |
| `components/core/include/core/drag.h`, `components/core/dragengine/drag_cfg.c` | `drag_cfg_engine_differs()`; the list value's unit helper if one is needed | 2, 4 |
| `tools/lint/venue_announce.py`, `.github/workflows/ci.yml` | structural check: every direct venue set announces to the ui | 3 |
| `test/test_screens.c` + goldens (both canvases), `test/test_drag.c`, `test/test_drag_cfg.c`, `test/test_lap.c`, `test/test_ui.c` | the four regression tests | 1-4 |
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md`, `docs/bench/next-bench-day.md` | spec/bench text | 1-4 |

---

### Task 1: DRAG card follows the engine's real state (B4-F1)

**Read first:** `investigation-ready-state.md` — its reachable-state table, the proposed mapping and the test code are this task's requirements. The log evidence is `/private/tmp/claude-501/-Users-benji-projects-personal-lap-timer/7ee58960-7fd0-452e-b7b7-3ab602786208/scratchpad/day4-items-95-96-lt.log`.

**Files:** Modify `components/app/include/app/pipeline.h` (`pipe_drag_t`), `components/app/pipeline/pipeline.c` (`publish_drag_snapshot`), `components/core/ui/include/core/ui/model.h`, `components/core/ui/screens_moto.c` (`render_drag_page0`), `components/app/ui/ui.c` (`drag_rows_refill`, the four `EV_DRAG_*` cases, `ui_apply_mode`), `test/test_screens.c` + goldens, the 7b design spec §7.

**Interfaces:**
- Produces: `pipe_drag_t.state` (uint8_t, = `drag_state()`: `DRAG_ST_IDLE/ARMED/LAUNCHED/DONE` from `core/drag.h`); `screen_model_t.drag_run_state` (uint8_t, same values) replacing `bool drag_armed`; the card's text mapping for `drag_n == 0`.
- Consumes: `drag_state(&s_drag)`, the existing seqlock snapshot plumbing.

- [ ] **Step 1: failing goldens first.** In `test/test_screens.c` replace the two `drag_armed` cases with four, one per state (`dragcard_idle`, `dragcard_ready`, `dragcard_launched`, `dragcard_done`), each asserting its golden plus the visible-width bound like its siblings; keep every `drag_n > 0` case unchanged (they must stay byte-identical — that is the regression guard that the n>0 path did not move). Run `./test/build/test_screens` → compile error on `drag_run_state` (red).
- [ ] **Step 2: model + snapshot.** `model.h`: `uint8_t drag_run_state;` with a comment naming the four values and that it mirrors `drag_state()` via `pipe_drag_t.state`; delete `bool drag_armed`. `pipeline.h`: add `uint8_t state;` to `pipe_drag_t` with the same note. `pipeline.c` `publish_drag_snapshot()`: set it from `drag_state(&s_drag)` inside the existing seqlock write.
- [ ] **Step 3: render.** `render_drag_page0()`'s `drag_n == 0` branch: pick the text from `m->drag_run_state` — `DRAG_ST_IDLE` → `"NOT READY"`, `ARMED` → `"READY"`, `LAUNCHED` → `"LAUNCHED"`, `DONE` → `"DONE"`, any other value → `"NOT READY"` (defensive). Keep FONT_MED unless a string overflows the big slot on the 213 canvas (measure from the dump; report it). Add the words to the 7b spec §7 card description.
- [ ] **Step 4: ui.** `drag_rows_refill()` copies `snap.state` into `s_model.drag_run_state` on every refill; the four `EV_DRAG_*` cases in `handle_event()` call `drag_rows_refill()` (ARMED and GATE/DONE already do — add LAUNCH, which today only cleared the bit); `ui_apply_mode()`/the two places that cleared `drag_armed` set `drag_run_state = DRAG_ST_IDLE` instead. Nothing else may write the field.
- [ ] **Step 5: promote the goldens** (documented workflow; eyeball each with `pbm_ascii.py` and describe what you saw in the report) and re-run both screens binaries byte-exact.
- [ ] **Step 6: gates + commit.** `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` build 0 warnings + DRAM line, the selftest app build. Commit: `fix(ui): DRAG card text follows the engine's state (idle/armed/launched/done), not a one-bit armed mirror (bench B4-F1, #95)` + trailer.

---

### Task 2: a display-only config change must not reset the drag engine (B4-F2)

**Read first:** `investigation-cfg-reload.md` — the destroyed-state table, the `drag_cfg_engine_differs()` shape and both test bodies are this task's requirements.

**Files:** Modify `components/core/include/core/drag.h` + `components/core/dragengine/drag_cfg.c` (the new comparison), `components/app/pipeline/pipeline.c` (`pipeline_reload_cfg`), `components/app/ui/ui.c` (`menu_do_dist`, and `menu_do_units` only if the investigation says the units path needs the reload — it does when the mph bench list rebuilds the table, so keep that post), `test/test_drag_cfg.c`, `test/test_drag.c`, spec §15.2 (a note that a display-only key never resets the engines).

**Interfaces:**
- Produces: `bool drag_cfg_engine_differs(const drag_cfg_t *a, const drag_cfg_t *b)` — true iff the engine's behaviour would change: `n_gates`, `rollout`, or any of the first `n_gates` gate definitions (`id`, `kind`, `a`, `b`) differ. Never `units`, never `benches_*` (display/selection only). Both pointers non-NULL (assert).
- Consumes: `drag_cfg_from_user()`, `s_drag.cfg`.

- [ ] **Step 1: failing tests first.** `test/test_drag_cfg.c`: the byte-identity case (two cfgs differing only in `dist_units` produce identical `drag_cfg_t`) and the differs-matrix from the investigation (false for a `dist_units` change, for a units flip whose mph bench list is empty, and for a bench-list edit that does not reach the table; true for a populated mph flip, a rollout flip, an `n_gates` change). `test/test_drag.c`: complete a run, then apply a display-only cfg swap the way the fixed pipeline will (`D.cfg = new_cfg`) and assert the current run, the per-gate session bests and the run number all survive; and that a real table change still clears them. Run → compile error on `drag_cfg_engine_differs` (red).
- [ ] **Step 2: the comparison.** Implement it in `drag_cfg.c` exactly as the investigation specifies, with its own asserts; document in `drag.h` which fields are engine-affecting and why `units` is (it selects the SPEED_FROM0 bench list that builds the gates) while `dist_units` never is.
- [ ] **Step 3: pipeline.** `pipeline_reload_cfg()`: build `dc` as today, then `if (drag_cfg_engine_differs(&dc, &s_drag.cfg)) { drag_init(&s_drag, &dc); } else { s_drag.cfg = dc; }` — with a comment that `drag_init` zeroes the engine (run, session bests, run number, armed state, history ring) and must therefore only run when the engine's own behaviour changed; the run number corrupting the session log (`logger.c` DRAG_GATE records, `exp_json.c` `"n"`) is the reason this is not merely cosmetic. Keep `publish_drag_snapshot()` and the mode-gated `stats_reset()` as they are.
- [ ] **Step 4: ui.** `menu_do_dist()` no longer posts `CMD_CONFIG_RELOAD` (the pipeline never reads `dist_units`; `menu_do_display` is the precedent) — leave `menu_do_units()`'s post in place. State in the report which remote path still triggers a reload (`cmd.c`'s `cfg_change_notify` posts for every key, which is why Step 3 is the mandatory half).
- [ ] **Step 5: gates + commit.** `test_drag_cfg`, `test_drag`, `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` + `moto_neo6m` builds 0 warnings + DRAM, the selftest app build. Commit: `fix(drag,pipeline): a config reload only re-inits the engine when the gate table changed; a display-only unit no longer wipes the run, the session bests or the run number (bench B4-F2, #96)` + trailer.

---

### Task 3: every path that sets a venue tells the ui (B4-F3)

**Read first:** `investigation-venue-id.md` — the acquisition-path table, the recommended `ui_post_venue()` shape, the create-hook trap analysis and the proposed linter are this task's requirements.

**Files:** Modify `components/app/pipeline/pipeline.c` (the sim-capture boot venue site ~line 1069, the RTC resume site ~line 490, and a `ui_post_venue()` helper beside `ui_post_lap_reset`/`ui_post_create`), `components/app/ui/ui.c` (`menu_do_layout`'s no-op guard), create `tools/lint/venue_announce.py`, modify `.github/workflows/ci.yml` (one blocking step beside the Power-of-10 one), `test/test_lap.c`, spec §10.3/§15.3 (one sentence: a venue set without the scan is announced to the ui).

**Interfaces:**
- Produces: `static void ui_post_venue(uint16_t venue_id, int64_t mono_us)` in pipeline.c — posts `EV_VENUE_FOUND` straight to `g_ui_evt_q` with `arg16 = venue_id` (never through `emit_event()`/`engine_cb()`, so the create-finish hook, the logger and the stream are untouched); `tools/lint/venue_announce.py` (exit 1 with the file:line of any `lap_set_venue(`/`lap_import_rtc(` call in `components/app/` not followed by a `ui_post_venue(` within the same function, excluding the engine's own sources).
- Consumes: `g_ui_evt_q`, `EV_VENUE_FOUND`, `esp_timer_get_time()`.

- [ ] **Step 1: the linter first (red).** Write `tools/lint/venue_announce.py` with a `--fail-on-violation` flag and run it: it must report exactly the two violations the investigation names (the sim boot site and the resume site). Keep it small and dependency-free, matching `power_of_10.py`'s style and output shape.
- [ ] **Step 2: a failing engine-contract test.** In `test/test_lap.c`, add the case the investigation specifies: for each venue-acquisition path reachable in pure code, assert whether an `EV_VENUE_FOUND` is emitted (the scan emits; `lap_import_rtc` cannot — pin that as the documented contract so the app layer is responsible for announcing). This test documents the split that the linter enforces at the app layer.
- [ ] **Step 3: the helper and the two call sites.** Add `ui_post_venue()` and call it right after each direct `lap_set_venue()` (sim boot) and after a successful `lap_import_rtc()` (resume), passing the venue id the site already has. Comment each site with why the post is direct (the create-finish hook in `engine_cb` treats `EV_VENUE_FOUND` as "creation finished", and `s_create_active` can be true at the resume site). Verify the boot post cannot render a one-shot over BOOT (the investigation proves `show_venue_oneshot` returns early while BOOT is up — restate the ordering in the report).
- [ ] **Step 4: the Layout no-op guard.** `menu_do_layout()`: when the venue is unknown or has no layouts, do not post `CMD_SET_LAYOUT` at all (today a dead press posts id 0, which clears `s_best` under the seqlock) — keep the label at `Layout: Auto` and log once at debug level. Covered by a `test_ui` case on `ui_layout_label` only; state in the report that the guard itself is firmware-only.
- [ ] **Step 5: linter green + CI.** Re-run the linter → 0 violations; add the blocking step to `.github/workflows/ci.yml` next to the Power-of-10 step with the same wording style.
- [ ] **Step 6: gates + commit.** `test_lap`, `test_ui`, `ctest`, lint (both linters), gcc-16, host 0 warnings, clean `moto_sim` + `moto_neo6m` builds 0 warnings + DRAM, the selftest app build. Commit: `fix(pipeline,ui): announce the venue to the ui on the sim-boot and RTC-resume paths; Layout no longer posts a no-op that wipes the best snapshot; structural linter (bench B4-F3, #98)` + trailer.

---

### Task 4: gate list pairing, the list's distance unit, and the `Speed:`/`Distance:` menu names (B4-F4, B4-F5, B4-F6)

**Evidence:** render `test/snapshots/drag_p1_dist_m.pbm` with `pbm_ascii.py` (rows 20..70) before you start: each row draws a FONT_SMALL label at the column's left edge and the FONT_MED value right-aligned ~60 px away, and column 1's value ends exactly where column 2's label begins — which is why a value reads as belonging to the neighbouring label.

**Files:** Modify `components/core/ui/screens_moto.c` (`render_drag_gate_list`), `components/core/ui/include/core/ui/canvas.h` (list constants, both canvases), `components/app/ui/ui.c` (the two menu labels; `drag_rows_refill`'s `dist_m` population if the unit conversion belongs there), `components/core/dragengine/drag_cfg.c` / `drag.h` only if a shared ft/m value helper is the cleanest home, `test/test_screens.c` + goldens (both canvases: the list cases and the two menu cases), the 7b design spec §7, spec §20.7, `docs/bench/next-bench-day.md` (the #95/#96 expectations and the new names).

**Interfaces:**
- Produces: the row layout (label and value on one baseline, label left-aligned, value right-aligned in the same column, a 1 px vertical divider between the columns, a 1 px rule under the header); the list's distance values honouring `dist_units` (metres today, feet when `Distance: ft`, suffix `m`/`ft`); menu labels `Speed: km/h|mph` and `Distance: m|ft`.
- Consumes: `screen_model_t.dist_units`, `drag_row_t.dist_m`/`is_distance`, `DLIST_*` constants, `fb_rect(fb, x, y, w, h, black, fill)` (render.h:48) for the 1 px rules — there is no `fb_hline`.

- [ ] **Step 1: failing goldens first.** Rename/extend the list cases so each unit has one (`drag_p1_dist_m`, `drag_p1_dist_ft` already exist — keep the names), add a brake-distance row to at least one case so the value's unit is exercised, and update the two menu goldens for the new labels. Run both screens binaries → golden mismatches (red).
- [ ] **Step 2: the row layout.** One baseline per row: label at the column's left edge, value right-aligned at the column's right edge, both on the FONT_MED baseline (the label keeps FONT_SMALL — align its baseline to the value's, so the pairing is visually one line); a 1 px vertical divider at the midpoint between the two columns spanning the rows' vertical extent; a 1 px horizontal rule under the header. Add the needed constants to BOTH canvas blocks (`DLIST_DIVIDER_X`, `DLIST_RULE_Y`, and a label-baseline constant if `DLIST_LABEL_DY` no longer fits) and keep the existing 2 px column gap and right-edge limits — nothing may draw at `x >= CANVAS_VISIBLE_W`.
- [ ] **Step 3: the distance unit in values.** A distance row's value must follow `dist_units`: metres as today, or feet (`dist_m` → feet, rounded) with the suffix `ft`, reserving the same `DLIST_UNIT_W` room (check that the widest feet value still fits the column; widen the reserve in both canvas blocks if not). Put the conversion in one place and name it; `drag_gate_label` already owns the label side.
- [ ] **Step 4: the menu names.** `Units: ` → `Speed: ` and `Dist: ` → `Distance: ` in `build_menu()` and the two `menu_do_*` label updates; check the longest label still fits the menu row width on the 213 canvas (`Distance: km/h` is not a thing — the longest are `Speed: km/h` and `Distance: m`), and update `UI_MENU_*` only if a width constant exists. Spec §20.7 and the bench doc use the new names.
- [ ] **Step 5: promote the goldens** (documented workflow; eyeball every one with `pbm_ascii.py` and describe the pairing in the report: label and value on one line, divider between columns, rule under the header, correct unit suffix).
- [ ] **Step 6: gates + commit.** Both screens binaries, `test_ui`, `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` + `PANEL=ws29v2` builds 0 warnings + DRAM, the selftest app build. Commit: `feat(ui): gate-list rows pair label and value on one baseline with a column divider; list distances honour the distance unit; menu reads Speed:/Distance: (bench B4-F4/F5/F6, #96)` + trailer.
