# Plan 7c — Display closure (design)

**Status:** approved design, 2026-09-29. Closes the display follow-ups filed after Plans 7/7b: #79 (best sectors, theoretical lap, session stats), #25 (DRAG gate names), #83 (units), #81 (boot self-test lines), #80 (live clock), #84 (dirty-region rendering). #82 (temperature throttle producer) stays with Plan 8 (needs the MPU6050 temperature). Amends `2026-09-27-plan-7b-glanceable-ui-design.md` §3–§7 where noted; base spec `2026-09-14-lap-timer-design.md` §20 otherwise stands.

## 1. Problem and goal

The Plan 7b pages render fields nothing fills (page 1 sector times and THEO, page 2 stats), DRAG gates show `G<id>`, speeds ignore `cfg.units`, the BOOT screen has no self-test lines, `display.live_clock` does nothing, and every partial refresh repaints the whole frame. All six are software-only and testable on the bench with `moto_sim`. Goal: every riding-screen field shows real data, and a single-cell change costs a single-cell refresh.

## 2. Data path — best sectors, theoretical lap, session stats (#79)

**Producer (pipeline task, the only writer).** Where the pipeline already publishes a completed lap under the F4 seqlock (`s_laps_seq`, `pipeline.c`), it also publishes:

```c
typedef struct {
    uint32_t best_sector_ms[LAP_MAX_SECTORS + 1];   /* from the engine's best-sector table */
    bool     have_best_sector[LAP_MAX_SECTORS + 1];
    uint8_t  n_sectors;                             /* splits in the locked layout (n_sec + 1) */
    uint32_t theo_ms;                               /* lap_theoretical_best_ms(); 0 = not yet */
} pipe_best_t;
int pipeline_best_snapshot(pipe_best_t *out);       /* seqlock reader, same bounded retry as pipeline_laps_snapshot; 0 on success */
```

The record is refreshed on every `EV_LAP_COMPLETE` the engine emits (valid or not — the engine only updates its bests on valid laps, so the copy simply mirrors it) and cleared when the venue/layout changes.

**Consumer (ui task).** In `handle_lap_result()` (non-out-lap completions) the ui calls `pipeline_best_snapshot()` and copies `best_sector_ms[]`, `n_sectors` → `best_n_sectors`, `theo_ms` → `theo_best_ms`/`have_theo` (`have_theo = theo_ms != 0`) into `screen_model_t`; a sector with `have_best_sector[i] == false` keeps showing `--.--` (the renderer treats a zero as missing — page 1's value row uses `have_best_sector` too: add `bool have_best_sector[LAP_MAX_SECTORS + 1]` to the model, replacing the "value row shows `--.--` until n" rule of 7b §5). It then calls `pipeline_lap_at(pipeline_lap_count() - 1, &lr)` for the newest lap and folds `lr.stats` into session maxima with a pure function:

```c
/* core/ui/stats_fold.h (pure, host-tested) */
typedef struct { uint16_t max_speed_cms; int16_t max_lean_l_cdeg, max_lean_r_cdeg; int16_t max_glat_e3, max_gacc_e3, max_gbrake_e3; } session_max_t;
void session_max_fold(session_max_t *acc, const lap_stats_t *lap);   /* element-wise max; asserts non-NULL */
```

The model's page-2 fields are derived at fold time: `max_speed_cms` (raw; converted to the display unit at render, see §3), `lean_l_deg`/`lean_r_deg` = cdeg/100, `lat_g_e2`/`acc_g_e2`/`brk_g_e2` = e3/10 (rounded). Session maxima reset at boot (they already zero-initialise) — never mid-session.

Rendering is unchanged for both pages except the `have_best_sector` gate; 7b §5's sentence "the value row keeps showing `--.--` until #58/#79" is replaced by "until that sector has a best time".

## 3. DRAG gate names (#25) and units (#83)

**Gate table, one source of truth.** A pure builder in `core/dragengine`:

```c
/* Fill *out from the §11.1 default gate table and the user config: the bench list for the configured unit
 * (cfg->drag.benches_kmh / benches_mph) replaces the default SPEED_FROM0 benches; ids stay stable. */
void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out);
```

`pipeline.c` passes it to `drag_init(&s_drag, &dc)` instead of `NULL` (today the engine runs on defaults and ignores the config); the ui builds the same table from its own config copy at start and after a units change, so both sides agree on ids and thresholds.

**Labels.** A pure, host-tested function:

```c
/* Writes the §6.6 name of one gate into buf (cap >= 8): SPEED_FROM0 "0-<a>", SPEED_RANGE "<a>-<b>",
 * BRAKE "<a>-0" (values as configured, in the configured unit); DIST by centimetre value: 1829 "60ft",
 * 10058 "330ft", 20117 "1/8", 30480 "1000ft", 40234 "1/4"; any other DIST "<m>m". Returns strlen or -1. */
int drag_gate_label(const drag_gate_def_t *g, char *buf, size_t cap);
```

`ui.c` `handle_drag_gate()` looks the event's `arg16` id up in its table and fills `label`, `t_ms = arg32`, `trap_kmh`/`has_trap` for the 1/4 gate (`arg32b` = speed cm/s → display unit). `EV_DRAG_GATE` carries the speed, not the stopping distance, so BRAKE gates and the session-best page come from a second seqlock record published by the pipeline task on every drag event (same pattern as §2):

```c
typedef struct {
    drag_result_t current;                       /* copy of drag_current(): the run in progress or the last frozen run */
    uint32_t      best_time_ms[DRAG_MAX_GATES];  /* per gate id-1: drag_best() time (or dist_cm for BRAKE); 0 = never hit */
    bool          have_best[DRAG_MAX_GATES];
} pipe_drag_t;
int pipeline_drag_snapshot(pipe_drag_t *out);
```

The model has one `drag[]`/`drag_n` array shared by the three DRAG pages, so the ui fills it for the page being shown, from the snapshot, on every drag event and on every page change: page 0 = the current run's hit gates in hit order (as today), page 1 = all gates of the last run in table order with `present` flags, page 2 = the session best per gate (`have_best`). BRAKE rows take `is_distance = true`, `dist_m = dist_cm / 100` from the snapshot (the event carries only the speed). This replaces today's append-only handling, which only ever fed page 0. The renderer is unchanged.

**Units.** `screen_model_t` gains `uint8_t units` (`CFG_UNITS_KMH`/`CFG_UNITS_MPH`), set from `s_cfg.units` at boot and when the menu toggles it (`s_dirty = true`). One pure helper `uint16_t speed_display(uint16_t cms, uint8_t units)` (cm/s → whole km/h or mph, integer maths, rounded) is used by every speed on screen: page 2 `MAX SPD` and the DRAG trap row (gate times and distances are not speeds and never convert). The unit string (`km/h`/`mph`) is drawn in `FONT_SMALL` once per place: page 2 label becomes `MAX SPD km/h`; the DRAG trap row `@173` gains a small suffix at `DCARD_LABEL_X + FONT_SMALL.w + DCARD_AT_GAP + 3*FONT_MED.w + DCARD_UNIT_GAP` on the label baseline. Times are never converted. The model keeps `max_speed_cms` (raw) instead of `max_speed_kmh`.

## 4. Live clock (#80)

- `ui.c` keeps `int64_t s_lap_start_mono_us` (0 = no lap running), set from `e->mono_us` on **every** `EV_LAP_COMPLETE`, including the out-lap (its crossing starts lap 1); cleared at boot.
- `screen_model_t` gains `uint32_t cur_ms` (running lap time) and `bool cur_running`; the reserved `cur_ms_at_gate` is removed.
- Tick: in `ui_loop_iter()`, when `s_cfg.display.live_clock && s_lap_start_mono_us != 0 && screen == SCR_RIDING && page == 0 && mode == LAP` and at least 1000 ms passed since the last tick: `cur_ms = (now - s_lap_start_mono_us) / 1000`, `s_dirty = true`, and a flag `s_clock_tick = true` for the refresh bookkeeping.
- Card rendering (amends 7b §4): while `cur_running` is set (the ui sets it only when the live clock is on and a lap is running) the LAST cell shows label `CUR` and value `m:ss` in `FONT_MED` (`fmt_time_s()`, whole seconds, ≤ 5 glyphs); LAST is not shown while the clock runs; BEST unchanged. With the clock off the card is exactly the 7b card.
- Policy: a render triggered only by a clock tick is a partial that does **not** increment `partial_count` and never promotes to a full; it obeys `throttled` and `dead` like any refresh. Event-driven renders in the same iteration keep their normal accounting. Implementation: `ui.c` passes `dirty = true, wants_full = false` and, when `s_clock_tick` was the only cause, skips the counter/timestamp updates after a successful partial.

## 5. Dirty-region rendering (#84)

- `ui.c` keeps a second buffer `s_fb_prev_bits[FB_STRIDE * FB_H]` (3904 B on the 2.13", 4736 B on the 2.9") holding the frame the panel last accepted.
- After each `screens_render()`:

```c
/* core/ui/render.h (pure, host-tested) */
bool fb_diff_rect(const fb_t *prev, const fb_t *cur, fb_rect_t *out);   /* bounding box of differing bytes, x0/x1 on byte boundaries; false + out->valid=false when identical */
```

- `render_and_refresh()`: `dirty = fb_diff_rect(...)`; if false → no refresh (`RF_NONE` path, no bookkeeping); else the existing policy decides P/F; a partial uses `disp_set_window(rect clamped to CANVAS_VISIBLE_W/CANVAS_H)`; after a **successful** partial or full, `memcpy(prev, cur)`; after a failed one `prev` is left as is, so the next diff covers the union. `wants_full` forces a full regardless of the rect. The boot render copies too (after `disp_init`'s full).
- Renderers keep clear-and-redraw; no per-field invalidation. Renders with no pixel change (duplicate events) now cost nothing.
- The driver's `-EINVAL` on an empty/oversize window cannot occur: an empty rect never reaches it and the clamp keeps it inside the visible area.

## 6. Boot self-test lines (#81)

- `app/lt_sup.h` gains a four-slot boot status: `enum { BOOT_STORAGE = 0, BOOT_DISPLAY, BOOT_GPS, BOOT_IMU }`, `enum { BOOT_UNKNOWN = 0, BOOT_OK, BOOT_FAIL, BOOT_SIM }`, `void sup_boot_report(uint8_t slot, uint8_t st)` / `uint8_t sup_boot_status(uint8_t slot)` (one atomic byte per slot, relaxed; any task).
- Reporters: `app_main.c` after `sto_mount()` (OK/FAIL); `ui.c` after `disp_init()` (OK/FAIL); `pipeline.c` after `gps_init()` and `imu_init()` (OK/FAIL; the sim drivers report SIM — the pipeline knows the driver kind from the build flags `CFG_GPS_SIM`/`CFG_IMU_SIM`, — `CFG_GPS_SIM` exists in `build_config.h`; add `CFG_IMU_SIM` the same way if it is missing).
- The ui formats `boot_line[0..3]` = `STORAGE OK|FAIL|--`, `DISPLAY OK|FAIL|--`, `GPS OK|FAIL|SIM|--`, `IMU OK|FAIL|SIM|--` (`--` = unknown) right before the BOOT render that follows `disp_init()`, and re-formats once more if the BOOT screen is still showing 1 s later (GPS/IMU init in the pipeline can land after the first render); `boot_n_lines = 4`. `ONESHOT_BOOT_MS` becomes 3000.

## 7. Model and API summary

`screen_model_t` (core/ui/model.h): add `bool have_best_sector[LAP_MAX_SECTORS + 1]`, `uint8_t units`, `uint16_t max_speed_cms` (replaces `max_speed_kmh`), `uint32_t cur_ms`, `bool cur_running`; remove `cur_ms_at_gate`. `ui.c` statics: `s_fb_prev_bits`, `s_lap_start_mono_us`, `s_last_clock_tick_us`, `s_clock_tick`, `session_max_t s_session_max`, `drag_cfg_t s_drag_cfg`. New pure code: `session_max_fold`, `drag_cfg_from_user`, `drag_gate_label`, `speed_display`, `fmt_time_s`, `fb_diff_rect`. New target glue: `pipeline_best_snapshot`, `pipeline_drag_snapshot`, `sup_boot_report/status`.

DRAM: +3904 B (2.13") / +4736 B (2.9") for the previous frame, + ~140 B of statics (drag cfg copy ~100 B, session max 14 B, clock stamps). Expected free after: ≈ 4.2 KB on `moto_sim`, ≈ 4.7 KB on `moto_neo6m` (sub-project C recovers ~11 KB later).

## 8. Tests (host)

- `test_stats_fold`: element-wise max, zero start, mixed signs on lean.
- `test_drag_cfg`: defaults + km/h benches; mph benches replace the SPEED_FROM0 set; ids stable; `drag_gate_label` for every default gate, a custom DIST (`"120m"`), a RANGE, cap too small → -1.
- `test_units`: `speed_display` rounding at 0, 3600 cm/s → 130 km/h / 81 mph, 65535 cm/s.
- `test_fb_diff`: identical → false; one pixel at (13, 7) → rect x 8..16, y 7..8; two far corners → full bbox; a change in the padding columns is reported (documented); both canvases.
- `test_screens` (both golden sets): `lap_p1_filled` (best sectors + THEO + deltas), `lap_p2_stats_kmh`, `lap_p2_stats_mph`, `lap_p0_cur_clock` (`CUR 1:23`, LAST absent, BEST present), `drag_p0_named_kmh` (`1/4 12.84 @173 km/h`), `drag_p1_named_mph`, `boot_four_lines`.

## 9. Bench acceptance (`p07c-d1`, `moto_sim`, 2.13" panel)

After two sim laps: page 1 shows sector times and THEO (photo); page 2 shows a non-zero MAX SPD, lean, G with the unit (photo); menu → Units → mph re-renders speeds in mph (photo); Display → live clock on → `CUR` counts once per second on the card and the log's refresh lines show cell-sized windows (`dirty` rect ≈ 98×24) for the ticks; BOOT screen lists four lines after a reset (photo); DRAG mode (config `mode: drag`) shows `READY`/`ARMED` and, via `dbg`-driven or real gates when available, named gates (photo of the lists after a sim run is not possible — the sim never launches — so the DRAG naming is host-golden-verified plus one on-panel photo of the list headers). No task WDT over a 10-minute soak with the clock on. Tag `p07c-d1`; closes #79, #25, #83, #81, #80, #84.

## 10. Out of scope

Real GPS/IMU health flags (Plan 8; #82 with them), mph-defined gate thresholds beyond the bench list, predictive lap time, sector-level ghosting tuning, the 2.9" on hardware.

## 11. Implementation notes (2026-09-29/30, `p7c-display-closure` T1–T8)

Full task-by-task detail lives in `.superpowers/sdd/2026-09-29-plan-7c-display-closure/` (`progress.md` ledger, `task-1-report.md`…`task-8-report.md`). This section records what a future reader needs without opening that directory.

### Rulings

- **R-1 (pre-flight, carried by T6):** `fmt_time_s` clamps at 99:59 (minutes ≥ 100 show `99:59`) — keeps the CUR cell ≤ 5 glyphs so it never collides with its label. Cost if wrong: a > 100-minute "lap" shows a frozen clock (irrelevant on track).
- **R-2 (pre-flight, carried by T3):** T3 factors one static seqlock reader, `snap_read(kind, dst, size)`, selecting the source record by enum (laps ring / best / drag) with no function pointers; `pipeline_lap_at`/`pipeline_laps_snapshot` keep their existing loops unchanged. Avoids three verbatim copies without violating `--enforce-fnptr`. Cost if wrong: a slightly larger diff in `pipeline.c`.
- **Task 3 ruling (coverage gap, accepted):** no host unit test exists for `publish_drag_snapshot`/`snap_read` — `pipeline.c` is FreeRTOS-bound and has no host harness. Coverage comes from the `p07c-d1` bench gate (a DRAG run on the sim → page 2 bests). Cost if wrong: a snapshot-mapping bug surfaces at the bench, not in CI.
- **R-3 (T4):** the plan's assumption that "km/h goldens stay byte-identical" is wrong for any case that renders a speed — the page-2 label gains " km/h" and the DRAG trap row gains a "km/h" suffix, so `lap_p2_stats*` and the drag cases with a trap legitimately change on both canvases (re-dumped, eyeballed, promoted); every other golden stayed byte-identical. Cost if wrong: a silently changed unrelated golden would hide a regression — the implementer had to list every changed golden explicitly.
- **R-4 (T4 fix 1):** reverses the plan's `trap_speed` deviation. `drag_row_t` carries the raw `trap_cms`; the renderer converts with `speed_display(trap_cms, m->units)` at render time. `handle_drag_gate` (and T5's row-fill helper) store the raw cm/s. This is spec-compliant (§3's "convert at render time only") and toggle-safe (a units change mid-session no longer mislabels an already-drawn row). Cost if wrong: none foreseen — the km/h goldens staying byte-identical verifies the digit mapping.
- **R-5 (T7 pre-review fix):** size **both** framebuffers (`s_fb_bits` and the new `s_fb_prev_bits`) by the compile-time canvas (`FB_STRIDE = CANVAS_W/8`, `FB_H = CANVAS_H`: 3904 B on `ws213v4`, 4736 B on `ws29v2`) instead of the fixed 296×128 worst case. The "one build holds either panel" argument that motivated the worst-case sizing is stale since Plan 7 T3 made the canvas a `PANEL`-time constant, and `fb_init` already only ever touches the canvas-sized prefix. Recovers 1664 B on the 213 build (measured DRAM before/after below). Cost if wrong: a `ws29v2` build is unaffected either way; a 213 build with a mismatched driver stride would show at the bench gate (`disp_blit` uses the panel's own `ram_w`).
- **Task 3 fix round 1 (ordering, both publish paths):** `s_laps[]`/`s_best` and `s_dragsnap` must be published *before* the event that announces them is enqueued, or a same-task consumer reacting to the event it just dequeued has no structural guarantee of seeing that data. Fixed on both paths: in `engine_cb()`, `on_lap_complete(ev->gps_us)` now runs before `emit_event(ev)` for `EV_LAP_COMPLETE` only (`EV_SECTOR`/`EV_DRAG_DONE` publish nothing a same-event consumer reads back, so their order is unchanged); in `on_raw()`'s `MODE_DRAG` block, `publish_drag_snapshot()` was hoisted to run immediately after `drag_on_fused()` returns, before the `engine_cb()` forwarding loop. Both confirmed safe by tracing `drag_on_fused()`/`update_best()`'s synchronous settlement before `emit()`.
- **Task 6/7 tick-accounting rules — "a render that resolves something owed is never only a tick":** `render_and_refresh()` computes `tick_only = s_clock_tick && !s_refresh_pending` at function entry, before anything else can change `s_refresh_pending`, and folds it back into `s_clock_tick` so `do_refresh()`'s existing `if (!s_clock_tick)` partial-accounting gate reads the corrected value. T6 fix 1 closed a related gap: `ui_loop_iter()`'s throttle-pending branch (fires independent of `s_dirty` once the 30 s window reopens) now clears `s_clock_tick = false` before calling `render_and_refresh()`, so a throttled-backlog render always gets real `DISP_PARTIAL` accounting (`s_partial_count++`, `s_last_partial_us = now`) even if a clock tick happened to tag it moments earlier in the same iteration — otherwise the stamp never advances and the 30 s thermal rate limit is bypassed indefinitely. T7 added the unchanged-frame decision table for `render_and_refresh()` (an unchanged frame is `fb_diff_rect() == false`, i.e. `s_diff.valid == false`):

  | `changed` | `wants_full` | policy `kind` | outcome |
  |---|---|---|---|
  | true | any | any | normal path (diff/refresh/accounting unaffected by this task) |
  | false | false | (policy not asked) | early skip — no refresh, `s_refresh_pending = false`, `s_clock_tick = false` |
  | false | true | `RF_FULL` | acted on normally (a full never calls `partial_window_or_full()`, so an invalid `s_diff` is never dereferenced) |
  | false | true | `RF_PARTIAL` | skip — nothing to redraw on an unchanged frame; `s_refresh_pending = false` |
  | false | true | `RF_NONE`, throttled | skip — owed; `s_refresh_pending = true` (the 30 s retry still fires) |
  | false | true | `RF_NONE`, dead | skip — `s_refresh_pending = false` (dead re-arms on its own via `dead_retry()`) |

  Without the middle rows, an unchanged frame with `s_wants_full` set (e.g. the UP+DOWN ghost-clear combo while riding) fell through to `partial_window_or_full()`'s `LT_ASSERT_RET(s_diff.valid, ...)` on ordinary, reachable control flow (fix round 1), and a first attempt at the guard over-cleared `s_refresh_pending` on a throttled `RF_NONE`, dropping the "owed, not dropped" guarantee (fix round 2, corrected to `s_refresh_pending = (kind == RF_NONE) ? in.throttled : false`).
- **Task 8 ruling (accepted deviation):** extracting `pipeline_init_drivers()` (the GPS/IMU bring-up block, moved out of `pipeline_init()` verbatim) was not in the original brief — adding the two `sup_boot_report()` calls inline pushed `pipeline_init()` from 59 to 61 code lines, past the lint tool's 60-line cap. The extraction is a verbatim relocation of an already-self-contained block (it touches none of `pipeline_init()`'s other locals) and was accepted on review as the same pattern the codebase already uses elsewhere (e.g. `sup.c`'s `ota_lifecycle()` grouping).

### Measured DRAM / `.bss`

`moto_sim` (`xtensa-esp32-elf-size`), `.bss` by task, one build at a time, ccache disabled:

| Point | `.bss` | Delta |
|---|---|---|
| After T1+T2 (base `8a730b1`, before T3) | 101009 B | — (pre-T1 baseline not separately measured) |
| After T3 | 101513 B | +504 B |
| After T4 | 101513 B | +0 B |
| After T5 | 101625 B | +112 B |
| After T6 | 101649 B | +24 B |
| After T7, before ruling R-5 | 106409 B | +4760 B (worst-case 296×128 buffer sizing) |
| After T7, ruling R-5 fix | 104745 B | −1664 B (canvas-sized buffers) |
| After T8 (branch head, `e5f92fc`) | 104769 B | +24 B |

Free static DRAM (`idf.py -B build/<env> size`, "Remain"), measured after T7's R-5 fix; T8's own `.bss` delta (+24 B, uniform across envs) was not re-measured per environment:

| Env | Free DRAM | ≥ 4 KB floor? |
|---|---|---|
| `moto_sim` (ws213v4) | 4440 B | yes (was 2776 B before R-5) |
| `moto_neo6m` | 5768 B | yes |
| `moto_sim_ws29v2` (`PANEL=ws29v2`, not owned hardware) | 2776 B | not gated |

### Known, deferred (verbatim from the ledger)

- "stats_fold.c uses two CORE_ASSERT_VOID null checks; render.c fb_diff_rect combines three in one CORE_ASSERT_RET — house style only" (T1).
- "render_and_refresh clears s_clock_tick on both exit paths — harmless duplication" (T6).
- "with SYS_DISP_DEAD and SYS_DISP_TEMP_THROTTLE both set, RF_NONE is attributed to 'throttled' and s_refresh_pending retries every 30 s until dead_retry clears — benign" (T7).

### Test names that differ from §8's list

§8 above lists the tests as planned; the implemented names in `test/test_screens.c` and `test/test_drag_cfg.c` are `lap_p1_filled`, `lap_p2_stats_filled`, `lap_p2_stats_mph`, `drag_p0_trap_mph`, `lap_p0_cur_clock`, `boot_four_lines`. Of these, `lap_p1_filled`, `lap_p2_stats_mph`, `lap_p0_cur_clock`, and `boot_four_lines` match §8 verbatim. Two differ: `lap_p2_stats_filled` replaces the planned `lap_p2_stats_kmh` (same coverage — the model defaults to `units = 0`/km/h via `memset`, exercised alongside the new `lap_p2_stats_mph`). The planned `drag_p0_named_kmh`/`drag_p1_named_mph` were not implemented as separate named tests: names reach the renderer only as strings, so their coverage is `drag_p0_trap_mph` (the mph-unit-suffix case) plus `test_drag_cfg.c`'s `test_labels_for_every_default_gate` (every default gate's label string, at the pure-function level) — the pre-existing `test_drag_p0_gate_speed`/`test_drag_p0_distance`/`test_drag_p1_gates`/`test_drag_p2_best` (from Plan 7b) now also exercise real names end to end via T5's rewiring, unrenamed.
