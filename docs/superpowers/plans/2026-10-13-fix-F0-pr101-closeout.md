# Fix Wave F0 — PR #101 Closeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clear every blocking finding of the PR #101 adversarial review (`pr:H1 H2 M1 M2 L1–L4` + the false PR claim) on the PR branch, so `glyphs-drag-menu-closure` can be squash-merged first in the audit fix wave, and file `pr:M3–M6` as issues.

**Architecture:** Six code/doc tasks on the existing branch, then three process steps (PR body, issues, merge). Nothing new is designed: `pipeline.c` gains one state compare, `finalize_create` reuses the module-static landing slot the PR already added for loads (exposed as `trk_user_scratch()`), and every spec/comment edit is transcribed from code read on the branch. No new files, no new static ≥ 64 B, no heap.

**Tech Stack:** C11 under the Power-of-10 lint, ESP-IDF v5.3.2 (`source tools/idf-env.sh`), host harness (CMake/CTest + Unity, PBM goldens on both canvases), vanilla JS (`devcontroller/web/app.js`), `gh` CLI.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding — this plan is row **F0**, ruling **R32**, and the four PR-#101 rows of §5 "Filed, not fixed"). Evidence: `.superpowers/reviews/2026-10-13/pr-101.md`; audit `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` §7.6. Product spec amended here: `docs/superpowers/specs/2026-09-14-lap-timer-design.md`.

**Worktree:** `/Users/benji/projects/personal/lap-timer-worktrees/gdm`, branch `glyphs-drag-menu-closure`, head `df75497`. Every path below is relative to it; all cited line numbers were verified there. Main (`f4d4db2`) at `/Users/benji/projects/personal/lap-timer` is reference only — never edit it.

## Global Constraints

- Power of 10 on all firmware and `core` code: functions of > 20 code lines carry ≥ 2 `CORE_ASSERT_*`/`LT_ASSERT_*` (never abort); every loop bounded by a constant or table field; no recursion, heap, goto or function pointers; ≤ 60 code lines per function. `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0 and `python3 tools/lint/venue_announce.py` = 0.
- Zero warnings: host suites `-Wall -Wextra -Werror -Wshadow -Wconversion` (lap-timer `test/` **and** dev-kit `devcontroller/test/`); gcc-16 sweep `python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json` → 0 diagnostics.
- Firmware: ccache-disabled clean `./build.sh moto_sim build`, `./build.sh moto_neo6m build`, `PANEL=ws29v2 ./build.sh moto_sim build` each rc 0 with `grep -c -i "warning:"` = 0; **and** `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build` (rc 0, 0 warnings) — CI's required `build (moto_neo6m)` job compiles it and it globs every `test/test_*.c`. This plan adds no new test file, so `test_apps/core_selftest/main/CMakeLists.txt` needs no exclude-regex change.
- Host suite green: 47 executables, 100 % (`ctest --test-dir test/build`) — confirmed on `df75497` before this plan. Dev-kit suite 100 %. Screens goldens stay byte-exact on BOTH canvases; regenerate only by the workflow in `test/test_screens.c`'s header and eyeball, never hand-edit.
- DRAM: `moto_sim` free static DRAM baseline 4192 B (ledger 2026-10-05); the PR reports 4072 B. Report the free-DRAM line of every firmware build; this plan must not move it by more than 8 B. Task 5 reclaims 2752 B of **stack**, not static.
- Storage mutations stay single-owner (only the logger task writes files). The user track table stays pipeline/ui/logger-owned with no lock — Task 4 documents that contract, it does not change it.
- Every comment this plan writes must be true of `df75497` plus this plan's own edits, and must state any premise it rests on — an unstated premise is exactly finding `pr:H2`.
- Never flash, never open a serial port, never run `esptool`. **No bench day in F0** → **no gate tag** (Task 8, Step 4).
- Commit messages end with the branch's two-line trailer, as on `df75497` (`git log -1 --format=%B`), the executing session substituting its own model and session URL:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
  ```
- **Checkpoint protocol** (umbrella plan): after every task's commit, write `Task N complete <sha>` + the resume prompt to `.superpowers/sdd/2026-10-13-fix-F0/progress.md` and push; the operator may `/clear` at any checkpoint. Session blocks: Session 1 = Tasks 1–3 (parallel, then checkpoint with host suite + both linters re-run); Session 2 = Tasks 4–5 (checkpoint with the four firmware builds); Session 3 = Tasks 6–8. Resume prompt: `Resume plan docs/superpowers/plans/2026-10-13-fix-F0-pr101-closeout.md at Task N+1 (ledger .superpowers/sdd/2026-10-13-fix-F0/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/gdm; branch glyphs-drag-menu-closure.`
- Design-doc §4 gates applying to F0: host + dev-kit `ctest` 100 %; both linters 0; all four firmware builds rc 0 / 0 warnings with the DRAM line reported; no flashing by subagents; every finding id F0 owns appears in exactly one task.

---

## File Structure

| File (verified line ranges on `df75497`) | Responsibility | Task |
|---|---|---|
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md:1091-1104` (§5.2 `core/trk.h`), `:1523-1533` (§10.1), `:1631-1635` (§10.9) | the shipped `"LTRK"` format, per-record skip semantics, the real API list and boot-load site | 1 |
| `components/app/pipeline/pipeline.c:177,341-371,673-687`; `test/test_drag.c` | publish the drag snapshot on every `drag_state()` change (closes #102) | 2 |
| `components/app/include/app/lt_ipc.h:53-58,90-98`; `components/core/include/core/event.h:69-71`; `devcontroller/web/app.js:219-221`; `components/app/ui/ui.c:255` | the four false comments/strings + `s_menu_action[UI_MENU_MAX]` | 3 |
| `components/core/include/core/trk.h:44-64`; `components/core/tracks/trk.c:16-51` | ownership comment: the logger as post-boot unlocked reader + the ordering argument | 4 |
| `components/core/include/core/trk.h` (+`trk_user_scratch`), `components/core/tracks/trk.c:134-142`, `components/core/lapengine/lap.c:845-872`, `test/test_lap.c` | `finalize_create` builds in the static landing slot, not a 2752 B local | 5 |
| PR #101 description; 4 GitHub issues | the true goldens list; `pr:M3–M6` filed with owners | 6, 7 |

Tasks 1–3 are **PARALLEL** (disjoint files). Task 4 then Task 5 are **SERIAL**: both own `trk.h`/`trk.c`, and Task 5's API must land after Task 4's comment so the comment it extends is final. Tasks 6–8 are **SERIAL**, last.

---

### Task 1: Spec §5.2/§10.1/§10.9 — the shipped `/tracks/user.bin` format (`pr:H1`, `L1` spec half) — PARALLEL

**Files:** Modify `docs/superpowers/specs/2026-09-14-lap-timer-design.md:1091-1104`, `:1523-1533`, `:1631-1635`.

**Interfaces:** Consumes the code read on `df75497` — `logger.c:62-73` (paths/magic/version), `:483-497` (`tracks_save_record`), `:518-558` (`tracks_save`), `:568-583` (`tracks_load_record`), `:597-615` (`logger_load_tracks`); `core/trk.h:84-130` (payload layout, `TRK_USER_REC_MAX`). Produces a §10.1 a reader can write a byte-compatible writer from. It names `trk_user_scratch()`, which **Task 5** implements — deliberate: both land in this PR.

**Ownership:** R18 gives the rest of §5.2's API blocks to plan F4. F0 edits **only** the `core/trk.h` block of §5.2, plus §10.1 and §10.9.

- [ ] **Step 1: Replace the `core/trk.h` API block** — spec lines 1091-1104 (from `/* The user store is module-static` through `int trk_to_json(...)`) become:

```c
/* The user store is module-static and not protected by a lock: boot writers run on app_main's task
 * before any consumer task exists; the pipeline task is the only post-boot writer; the ui and
 * logger tasks are unlocked readers (core/trk.h and trk.c carry the full ordering argument). */
void               trk_init(void);                                   /* clears the user store */
int                trk_validate_venue(const trk_venue_t *v);         /* 0 ok / -1 structurally invalid */
const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out);   /* within radius; user beats bundled on id clash */
const trk_venue_t *trk_get(uint16_t venue_id);
int                trk_user_add(const trk_venue_t *v);               /* replaces same id; -1 if full or invalid */
int                trk_user_add_json(const char *json, size_t n, uint16_t *venue_id_out,
                                     char *err, size_t err_cap);     /* parses into the landing slot */
int                trk_user_count(void);
uint16_t           trk_next_user_id(void);
uint16_t           trk_user_id_at(uint8_t index);   /* live in-RAM order, NOT an id; 0 if out of range */
trk_venue_t       *trk_user_scratch(void);          /* the module-static landing slot every ~2.7 KB venue
                                                     * is built/decoded in instead of on a task stack;
                                                     * NULL when the table is full */
/* Per-venue variable-length record (§10.1). index is 0..trk_user_count()-1, not a venue id.
 * trk_user_load_venue decodes into trk_user_scratch()'s slot, validates, then trk_user_add()s. */
int                trk_user_save_venue(uint8_t index, uint8_t *buf, size_t cap, size_t *n_out);
int                trk_user_load_venue(const uint8_t *buf, size_t n);
#define TRK_USER_REC_MAX /* worst-case record payload (every slot full); the logger's BATCH_CAP must
                          * exceed TRK_USER_REC_MAX + 4 (a _Static_assert there proves it) */
int                trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap);
int                trk_to_json(const trk_venue_t *v, char *out, size_t cap);
```

`trk_user_load`/`trk_user_save` are gone (deleted on this branch, final review M-3, zero production callers). Do not leave them commented out.

- [ ] **Step 2: Replace §10.1's user-venue paragraphs** — spec lines 1523-1525 and the closing sentence at 1533. The bullet list at 1527-1531 (`trk_validate_venue`'s rules) and the `trk_from_json` extra-rules sentence stay verbatim; keep the "`trk_validate_venue` (also applied by …) requires:" lead-in before the bullets. New text:

> User venues live in `/tracks/user.bin`, written and read by the logger **one venue at a time** — never as one whole-table blob (`components/app/logger/logger.c`: `tracks_save()`, `logger_load_tracks()`). File layout: `char magic[4]` = `"LTRK"` (`TRACKS_FILE_MAGIC`) │ `u8 version` = `1` (`TRACKS_FILE_VERSION`) │ `u8 count` │ then `count` records, each `u16 len` (LE) │ `len` payload bytes │ `u16 crc16` (LE).
>
> `crc16` is `ses_crc16` (CRC-16/CCITT-FALSE, §12.2) over **the record's `len` payload bytes only** — not the 6-byte header, not the length prefix. The payload is one `trk_user_save_venue` record (`core/trk.h`), carrying only a venue's actual `n_layouts` layouts and each layout's actual `n_sectors` sectors:
>
> ```
> u16 id | char name[32] | f64 lat | f64 lon | u32 radius_m | u8 flags | u8 n_layouts |
> n_layouts × { u16 id | char name[24] | 4 × f64 sf | i8 dir_sign | u8 n_sectors |
>               n_sectors × (4 × f64 sector line) | u32 length_m }
> ```
>
> so an on-device-created venue (`n_layouts == 2`) costs well under a third of `TRK_USER_REC_MAX`, the worst-case payload the logger's scratch batch is proven to hold. The reserved sim venue id (`TRK_SIM_VENUE_ID`) is never written, is excluded from `count`, and a record carrying it is refused on load.
>
> A save writes `/tracks/user.tmp` and `sto_rename`s it over `/tracks/user.bin` only once every record landed (atomic on LittleFS), so a power cut mid-save leaves the previous file intact; every failure path unlinks the `.tmp`.
>
> A load is **per-record and fail-soft, not all-or-nothing**. A header whose magic or version byte does not match loads nothing and returns `-1`. Otherwise the first `min(count, TRK_MAX_USER)` records are read in turn and:
>
> - a record whose length prefix, payload or CRC trailer cannot be read in full, or whose `len` is `0` or above `TRK_USER_REC_MAX`, **stops** the loop — the rest of the file is unreachable without it;
> - a record whose CRC does not match is **skipped and reading continues**, so one corrupt record never costs the records after it;
> - a record that decodes but has trailing bytes, fails `trk_validate_venue`, carries `TRK_SIM_VENUE_ID`, or arrives at a full table is **dropped** by `trk_user_load_venue` (its landing slot left fully `memset`) and reading continues.
>
> A load therefore degrades to "the venues that survived", never to an emptied store. Max `TRK_MAX_USER` = 4 user venues; the in-RAM store is `4 × sizeof(trk_venue_t)` = 11,008 B of static RAM, while the on-flash cost is only what each venue actually uses. Lookup merges bundled and user; user wins on id clash.

Delete the old "On any failure the store is left empty and `-1` returned" sentence — now false.

- [ ] **Step 3: Correct §10.9's boot-load sentence** — spec lines 1631-1635, from "At boot, before `logger_start()`" to "survives a reboot.", become:

> At boot, `main/app_main.c`'s `boot_subsystems()` clears the table once (`trk_init()`) and then calls `logger_load_tracks()` — declared in `app/logger.h`, implemented in `logger.c` because that file owns the format — **both on app_main's own task**, strictly before `logger_start()` creates the logger task and well before `pipeline_start()` creates the pipeline task. Neither write can race a reader (neither task exists yet), no request/reply is involved, and the table is complete before any venue (sim or scanned) is registered, so every created venue survives a reboot.

- [ ] **Step 4: Verify no stale claim survives**

Run: `grep -n "trk_user_load\b\|trk_user_save\b\|blob version 2\|left empty and\|its own boot init" docs/superpowers/specs/2026-09-14-lap-timer-design.md`
Expected: no output. (The `_venue` forms do not match `\b` after `load`/`save`; if your grep disagrees, confirm each hit is the `_venue` form.)

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit   # message below + the two-line trailer
```
```
docs(spec): §10.1 rewritten to the shipped "LTRK" /tracks/user.bin format; §10.9 names the real boot-load site (pr:H1, pr:L1)
```

---

### Task 2: Publish the drag snapshot on every `drag_state()` change (`pr:M1`, closes #102) — PARALLEL

**Files:** Modify `components/app/pipeline/pipeline.c` (static at `:177`; `publish_drag_snapshot` `:347-371`; `on_raw` `:673-687`). Test: `test/test_drag.c` (one case + its `RUN_TEST`).

**Interfaces:** Consumes `uint8_t drag_state(const drag_t *)` (`core/drag.h:188`) with `DRAG_ST_IDLE/ARMED/LAUNCHED/DONE = 0/1/2/3` (`core/drag.h:53-58`). Produces no API change: `s_dragsnap.state` now tracks the engine on **every** step, including the two transitions that emit nothing — `abort_to_armed()` (`drag.c:415`, LAUNCHED→ARMED, §11.2 false start) and `go_idle_keep_result()` (`drag.c:93`, called at `drag.c:603`, `DRAG_DONE_SETTLE_S` after DONE).

**TDD note:** the defect is in `pipeline.c`, which has no host harness, so no seam can be made red. The two test changes below are **characterisation tests of the engine contract the pipeline fix depends on** — both transitions change `drag_state()` and emit nothing, so an event-gated publisher is structurally blind to them. They are green before and after the pipeline edit; the pipeline edit's gate is the firmware build plus the bench item in Step 6.

- [ ] **Step 1a: Pin the false start's silence** — `test_false_start_abort` (`test/test_drag.c:382-401`) already drives LAUNCHED→ARMED. Capture the event count before the stall and assert it did not move. Insert after the existing `TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));` line:

```c
    const int ev_before_abort = EV.n;   /* #102/pr:M1: abort_to_armed() emits NOTHING, so the only
                                         * way a consumer can see LAUNCHED -> ARMED is drag_state() */
```

and after the existing `TEST_ASSERT_EQUAL_UINT8(DRAG_ST_ARMED, drag_state(&D));` line:

```c
    TEST_ASSERT_EQUAL_INT(ev_before_abort, EV.n);   /* no event announced the abort */
```

- [ ] **Step 1b: Pin the DONE→IDLE settle's silence** — append a new case after `test_done_then_brake` (ends `:438`):

```c
/* #102 / pr:M1 — the DONE -> IDLE settle (go_idle_keep_result, drag.c:603, DRAG_DONE_SETTLE_S after
 * DONE) is the engine's second SILENT transition: drag_state() changes and no event is emitted, so
 * a publisher that refreshes only on a step that produced events keeps showing DONE until the next
 * arm. That is why pipeline.c's on_raw compares drag_state() against the state it last published.
 * run_quarter_then_brake leaves the engine DONE with the 100-0 gate hit (brake_done), so
 * brake_pending() is false and the settle can fire; the follow-up samples carry flags 0, NOT
 * FUS_STILL, so the engine cannot re-arm and emit EV_DRAG_ARMED while we are counting events. */
static void test_done_to_idle_settle_is_silent(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    run_quarter_then_brake(true);
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_DONE, drag_state(&D));
    const int ev_before = EV.n;
    for (int k = 3001; k <= 3001 + (DRAG_DONE_SETTLE_S + 1) * FUSION_HZ; k++) {
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_IDLE, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(ev_before, EV.n);       /* no event announced the settle */
    TEST_ASSERT_NOT_NULL(drag_current(&D));       /* the frozen result is kept (go_idle_keep_result) */
}
```

Register it in `main()` after `RUN_TEST(test_done_then_brake);` (`:610`): `RUN_TEST(test_done_to_idle_settle_is_silent);`

- [ ] **Step 2: Run it** — `cmake --build test/build -j8 && ./test/build/test_drag`
Expected: PASS, one test more than before. If the new case fails on the event count, the only plausible extra event is `EV_DRAG_ARMED` — re-check the follow-up samples pass `0`, not `FUS_STILL`.

- [ ] **Step 3: Record what was published** — in `pipeline.c`, beside `s_dragsnap` (`:177`):

```c
static pipe_drag_t       s_dragsnap;
/* #102 (pr:M1): drag_state() as of the last publish_drag_snapshot(). on_raw compares against it so
 * the two engine transitions that emit NO event -- abort_to_armed() (false start) and the DONE ->
 * IDLE settle (both core/dragengine/drag.c) -- still refresh the card. 1 B, pipeline task only. */
static uint8_t           s_last_pub_state = DRAG_ST_IDLE;
```

and at the end of `publish_drag_snapshot()`, after `seq_leave();` (`:370`):

```c
    s_last_pub_state = snap.state;   /* #102: every publish site updates the compare base, not just on_raw */
```

(Doing it here, not in `on_raw`, keeps the other two publish sites — `pipeline_reload_cfg()` `:755` and `handle_reset_engine()` `:856` — consistent for free.)

- [ ] **Step 4: Compare state in `on_raw`** — replace `pipeline.c:685` (`if (nev > 0) publish_drag_snapshot();`) with:

```c
        /* #102 (pr:M1): nev > 0 alone misses abort_to_armed() (LAUNCHED -> ARMED on a §11.2 false
         * start) and go_idle_keep_result() (DONE -> IDLE, DRAG_DONE_SETTLE_S later) -- both change
         * drag_state() and emit nothing, so the card used to keep reading LAUNCHED indefinitely
         * after a clutch-dump stall. Publishing on a state change too costs one byte compare per
         * fused sample and leaves the >= 1-event path exactly as it was. */
        if (nev > 0 || drag_state(&s_drag) != s_last_pub_state) publish_drag_snapshot();
```

Leave the existing ordering comment at `:677-684` above it — it still explains why the publish precedes the `engine_cb` loop.

- [ ] **Step 5: Gates**

```bash
cmake --build test/build -j8 && ctest --test-dir test/build           # 47/47, 100%
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation # 0 violations, rc 0
python3 tools/lint/venue_announce.py                                  # 0 violations, rc 0
python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json  # 0
source tools/idf-env.sh && ./build.sh moto_sim build                  # rc 0, 0 warnings
```
Expected DRAM delta: 0 or −1 B (one `uint8_t` static; padding may absorb it). Worse than −8 B means something else changed — investigate before committing.

- [ ] **Step 6: Bench item + commit** — append to `docs/bench/next-bench-day.md` under the open items:

> **F0/#102 — DRAG card on a silent transition.** In DRAG mode with the sim: `dbg sim drag`, then stall the run within 2 s of launch so the engine false-starts. The card must fall back to `NOT READY` within one refresh instead of staying on `LAUNCHED`. Then let a full run reach `DONE` and wait > 5 s stationary: the card must read `NOT READY`, not `DONE`.

```bash
git add components/app/pipeline/pipeline.c test/test_drag.c docs/bench/next-bench-day.md
git commit   # message below + the two-line trailer
```
```
fix(pipeline): publish the drag snapshot on every drag_state() change, not only on events — a false start no longer leaves the card reading LAUNCHED (#102, pr:M1)
```

---

### Task 3: The four false comments/strings and `s_menu_action[UI_MENU_MAX]` (`pr:L1` code half, `L2`, `L3`, `L4`) — PARALLEL

**Files:** Modify `components/app/include/app/lt_ipc.h:53-58` and `:90-98`; `components/core/include/core/event.h:69-71`; `devcontroller/web/app.js:219-221`; `components/app/ui/ui.c:255`.

**Interfaces:** Comments, one help string, one array bound. No signature, behaviour or wire-format change. `app.js` gains `rerenderOnChange: true` on the `dist_units` field, matching `units` at `:212`. `UI_MENU_MAX` is `12` (`ui.c:114`) and already `_Static_assert`ed against `screen_model_t.menu_items[]` (`ui.c:120-122`).

- [ ] **Step 1: `lt_ipc.h`'s retired-request comment** — `:53-58` becomes:

```c
    /* 7 was LOGGER_LOAD_TRACKS. Retired in Task 5's review fix round 1 (#97, T5-R2) and the final
     * review (F-4/I-4): the table is loaded by logger_load_tracks() (declared in app/logger.h,
     * implemented in logger.c because that file owns the format) called ONCE from app_main's own
     * task, in main/app_main.c's boot_subsystems(), before this task exists -- not from
     * logger_task()'s boot init and not via a request the pipeline sends. See
     * logger_request_sync()'s doc comment below for why that cross-task request existed only
     * briefly and why removing it was the fix, not a workaround. Value retired, not reused, same
     * as 4 above. */
```

- [ ] **Step 2: `lt_ipc.h`'s second site** — the tail of `:95-98` becomes:

```c
 * PIPE_STALL_S with no benefit over the fix actually shipped. The table is now loaded on app_main's
 * own task, inside main/app_main.c's boot_subsystems(), before the logger or pipeline task exists --
 * see core/trk.h/.c's ownership comment for the ordering argument that makes this safe with no lock
 * and no request at all.)
```

Both removed claims: the symbol is `logger_load_tracks()` (not `tracks_load()`), and it runs on app_main's task (`main/app_main.c:235`), not "inside its own boot init".

- [ ] **Step 3: `event.h`'s EV_CREATE comment** — `:69-71` becomes:

```c
    EV_CREATE        = 19   /* ui-only: posted straight to g_ui_evt_q by the pipeline (#97, §10.9).
                             * flags = EV_CREATE_* phase below; arg16 = gate index (GATE_SET) or an
                             * EV_CREATE_FAIL_* reason (FAILED -- NOFIX = 0 or FULL = 1, both sent) */
```

(`handle_create_cmd()` sends `EV_CREATE_FAIL_FULL = 1`, so "always 0 today" was false.)

- [ ] **Step 4: `app.js`'s `dist_units` field** — `:219-221` becomes:

```js
      { path: ["dist_units"], label: "Distance units", control: "segmented", rerenderOnChange: true,
        options: [["m", "m"], ["ft", "ft"]],
        help: "Unit for DRAG distance gates and for every distance the list, card and footer show (60ft/330ft/1000ft <-> 18m/101m/305m); 1/8 and 1/4 mile keep their name either way." },
```

- [ ] **Step 5: `ui.c:255`** becomes:

```c
static uint8_t s_menu_action[UI_MENU_MAX];   /* pr:L4: tied to the same capacity every bound assert
                                              * (menu_add, menu_select, the long-press paths) checks,
                                              * so bumping UI_MENU_MAX alone can no longer overrun it */
```

- [ ] **Step 6: Verify every claim is now true**

```bash
grep -rn "tracks_load()\|own boot init\|always 0 today\|labels only" components/ devcontroller/web/   # no output
grep -n "logger_load_tracks" main/app_main.c                                                          # one hit, ~:235
grep -n "s_menu_action\[" components/app/ui/ui.c    # 5 hits (:255 decl, :302 write, :590/:692/:734 reads),
                                                    # each read guarded by an LT_ASSERT_VOID(... < UI_MENU_MAX)
node --check devcontroller/web/app.js                                                                 # silent, rc 0
```

- [ ] **Step 7: Gates + commit** — `www.bin` needs no separate step: `devcontroller/CMakeLists.txt:53` packs `web/` via `littlefs_create_partition_image(www web FLASH_IN_PROJECT)`, so the dev-kit build repacks it and CI uploads `devcontroller/build/www.bin` (`.github/workflows/firmware.yml:74`). Nothing named `www.bin` is tracked (`git ls-files | grep www` is empty) — nothing to commit for it.

```bash
cmake --build test/build -j8 && ctest --test-dir test/build                                     # 47/47
cmake -S devcontroller/test -B devcontroller/test/build && cmake --build devcontroller/test/build \
  && ctest --test-dir devcontroller/test/build --output-on-failure                              # 100%
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation                           # 0
source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build             # rc 0, 0 warnings
cd devcontroller && idf.py build                                                                # rc 0, www.bin repacked
```

```bash
git add components/app/include/app/lt_ipc.h components/core/include/core/event.h \
        devcontroller/web/app.js components/app/ui/ui.c
git commit   # message below + the two-line trailer
```
```
fix(docs,ui,devkit): three comments and one help string that asserted what the code does not do; size s_menu_action[] by UI_MENU_MAX (pr:L1..L4)
```

---

### Task 4: `trk` ownership comment — the logger as a post-boot unlocked reader (`pr:H2`) — SERIAL (after Tasks 1–3)

**Files:** Modify `components/core/include/core/trk.h:44-64` (the `Readers:`/`Writers:` block), `components/core/tracks/trk.c:16-51` (the `Not reentrant:` block). Comments only; this produces the invariant Task 5 must not break and the one the next reviewer will judge changes against.

- [ ] **Step 1: `trk.h`'s `Readers:` paragraph** — replace lines 46-48 with:

```c
 * Readers: the pipeline task (core 1) and -- since #98 -- the ui task (core 0), both only through
 * trk_get(); the ui calls it only after receiving the EV_VENUE_FOUND/EV_LAYOUT_LOCKED event that
 * names the id. Since #97 the LOGGER task (core 0) is a third reader, and the only one no event
 * orders: on every LOGGER_SAVE_TRACKS it serialises the store through trk_user_count(),
 * trk_user_id_at() and trk_user_save_venue() (logger.c's tracks_save()), unlocked, while the one
 * post-boot writer -- trk_user_add() from lap.c's finalize_create(), pipeline task, core 1 -- could
 * in principle run. Three facts, not a lock, make that safe; a change breaking any one of them must
 * add a lock or move the work:
 *   (1) the save captures trk_user_count() ONCE, up front, and only touches indices < that count;
 *   (2) the writer is append-only inside that range -- trk_user_add() fills user[user_n] and only
 *       then bumps user_n -- so it never writes a byte the save can be reading (its same-id
 *       "replace" branch is the exception, and the invariant closing this comment is what keeps
 *       that branch off any id a reader may hold);
 *   (3) the xQueueSend(g_log_req_q, ...) asking for the save is issued by the pipeline task AFTER
 *       the add returns (pipeline.c's create_finish_if_active()), so the queue send/receive pair
 *       orders the add before the save it triggers -- no other barrier needed.
 * The window a future change could open is a second creation landing inside one save; today two
 * creations cannot be closer than MIN_LAP_S apart, so it is unreachable, not merely narrow.
```

- [ ] **Step 2: `trk.h`'s closing invariant** — line 63's clause becomes:

```c
 * itself does not enforce: no writer may rewrite or remove an id the ui may already hold, or that
 * the logger may be mid-serialising (trk.c has the full reasoning and every live call site's
 * compliance). */
```

- [ ] **Step 3: `trk.c`'s `Readers:` paragraph** — insert after line 40 (`* above is guaranteed visible by the time the ui's read runs. No lock is taken.`):

```c
 *
 * Third reader, post-boot and unlocked (#97, review finding pr:H2): the logger task (core 0)
 * serialises the whole store on every LOGGER_SAVE_TRACKS -- trk_user_count() once, then
 * trk_user_id_at() and trk_user_save_venue() per index (logger.c's tracks_save()) -- with no lock,
 * and with the pipeline task's trk_user_add() able in principle to run. Unlike the ui, no event
 * orders this read: the argument is the captured count, the append-only writer (user[user_n] filled
 * before user_n is bumped, so reader and writer never touch the same byte), and the xQueueSend()
 * requesting the save being issued after the add returns. core/trk.h states all three in full; all
 * three must hold for any future change, and the replace branch below is the one that would break
 * fact (2) if it were ever aimed at a live id.
```

- [ ] **Step 4: `trk.c`'s closing invariant** — line 42's clause becomes:

```c
 * This rests on one invariant the API itself does NOT enforce: no writer may rewrite or remove an
 * id the ui may already hold, or that the logger may be mid-serialising.
```

- [ ] **Step 5: Gates + commit** — the lint counts comments separately from code; confirm nothing shifted a function past a threshold.

Run: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` (0); `cmake --build test/build -j8 && ctest --test-dir test/build` (47/47); `source tools/idf-env.sh && ./build.sh moto_sim build` (rc 0, 0 warnings, DRAM unchanged).

```bash
git add components/core/include/core/trk.h components/core/tracks/trk.c
git commit   # message below + the two-line trailer
```
```
docs(trk): the ownership comment names the logger task as a post-boot unlocked reader and states the three facts that make it safe (pr:H2)
```

---

### Task 5: `finalize_create` builds in the module-static landing slot (`pr:M2`) — SERIAL (after Task 4)

**Files:** Modify `components/core/include/core/trk.h` (declare `trk_user_scratch` after `trk_user_id_at`, `:82`), `components/core/tracks/trk.c:134-142`, `components/core/lapengine/lap.c:845-872`. Test: `test/test_lap.c` (one case + its `RUN_TEST`).

**Interfaces:**
- Produces `trk_venue_t *trk_user_scratch(void);` — returns `&user[user_n]`, the same not-yet-active slot `user_slot_for_parse()` already hands `trk_user_add_json()` and `trk_user_load_venue()`; `NULL` when `user_n == TRK_MAX_USER`. The caller `memset`s it before building and on every failure path.
- Consumes `trk_user_add(const trk_venue_t *)`, `trk_get(uint16_t)`, `exp_utc_parts`, `lap_layout_reverse` — all already used by `finalize_create`.

Reclaims 2752 B of pipeline-task stack (`sizeof(trk_venue_t)`, measured) out of `PIPE_STACK_BYTES 8192`. No new static: the slot already exists.

- [ ] **Step 1: Write the failing test** — `test_create_track_saves_valid_venue` (`test/test_lap.c:630-676`) is already the "creation still produces the same venue" guard: it asserts id, flags, `n_layouts`, both `dir_sign`s, sector counts and a positive `length_m`, and it must stay green unchanged. The new case covers only what the change adds — the slot being `NULL`, which is the branch that replaces the old `trk_user_add() != 0` rejection. Append after `:676` (`setUp()` calls `trk_init()` at `:21`, so the store starts empty):

```c
/* pr:M2 — finalize_create() now builds the venue in trk_user_scratch()'s module-static slot instead
 * of a 2752 B trk_venue_t on the 8 KB pipeline-task stack, so it has a new failure branch: the slot
 * is NULL once the user table is full. That must fail exactly as the old trk_user_add() rejection
 * did -- back to NO_VENUE, nothing saved, no EV_VENUE_FOUND, and the table it could not join left
 * untouched. (test_create_track_saves_valid_venue above is the unchanged-venue guard.) */
static void test_create_refuses_when_user_table_full(void)
{
    const double lat0 = -34.0, lon0 = 18.7;
    for (uint8_t i = 0; i < TRK_MAX_USER; i++) {
        trk_venue_t filler; build_venue_2layout(&filler, lat0 + 1.0 + (double)i, lon0);
        filler.id = (uint16_t)(TRK_USER_ID_BASE + i);   /* build_venue_2layout always uses the base id */
        TEST_ASSERT_EQUAL_INT(0, trk_user_add(&filler));
    }
    TEST_ASSERT_EQUAL_INT(TRK_MAX_USER, trk_user_count());
    TEST_ASSERT_NULL(trk_user_scratch());

    lap_t L; lap_init(&L, NULL);
    lap_create_begin(&L);
    gps_fix_t sf = fix_ll(lat_of(lat0, 0.0), lon_of(lon0, lat0, 0.0), 1000000, SPD_MMS, true);
    sf.head_e5 = 0;
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 0, &sf, NULL));
    evlog_t log; memset(&log, 0, sizeof log);
    feed_hdg(&L, lat0, lon0,   0.0, 300.0,  8000000,  0.0,   NULL);
    feed_hdg(&L, lat0, lon0, 400.0, 300.0,  15000000, 90.0,  NULL);
    feed_hdg(&L, lat0, lon0, 400.0,-100.0,  25000000, 180.0, NULL);   /* > MIN_LAP_S now */
    feed_hdg(&L, lat0, lon0,   0.0,-100.0,  30000000, 270.0, NULL);
    feed_hdg(&L, lat0, lon0,   0.0,  50.0,  35000000, 0.0,   &log);   /* S/F crossing -> finalise */

    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(0, count_type(&log, EV_VENUE_FOUND));
    TEST_ASSERT_EQUAL_INT(TRK_MAX_USER, trk_user_count());
}
```

Register it in `main()` after `RUN_TEST(test_create_track_saves_valid_venue);` (`:1282`): `RUN_TEST(test_create_refuses_when_user_table_full);`

- [ ] **Step 2: Run it to fail** — `cmake --build test/build -j8`
Expected: compile error `implicit declaration of function 'trk_user_scratch'` (hard failure under `-Werror`).

- [ ] **Step 3: Expose the slot** — in `core/trk.h`, after `trk_user_id_at`'s declaration (`:82`):

```c
/* The module-static landing slot (user[user_n]) that trk_user_add_json() and trk_user_load_venue()
 * already build/decode into, so a ~2.7 KB trk_venue_t never passes through a task stack (final
 * review I-1; pr:M2 for the pipeline-side twin, lap.c's finalize_create()). NULL once the table is
 * full. The caller memsets it before building and on every failure path, then hands it to
 * trk_user_add(), whose "new entry" branch copies the slot onto itself (harmless) before bumping
 * user_n. Not reentrant, like the rest of this module, and -- like every other entry point -- only
 * legitimate on the task that owns writing (the pipeline task, after boot). */
trk_venue_t       *trk_user_scratch(void);
```

In `components/core/tracks/trk.c`, directly after `user_slot_for_parse()` (`:142`):

```c
trk_venue_t *trk_user_scratch(void) { return user_slot_for_parse(); }
```

- [ ] **Step 4: Build the venue in the slot** — replace `lap.c:851-871` (from `trk_venue_t nv;` through the `emit(...)` line):

```c
    /* pr:M2: build the new venue in the track module's static landing slot, not in a 2752 B
     * trk_venue_t on this task's 8 KB stack (PIPE_STACK_BYTES) -- the rule core/trk.h states for
     * trk_user_load_venue()/trk_user_add_json(), now applied to the pipeline-side twin. */
    trk_venue_t *nv = trk_user_scratch();
    if (nv == NULL) {                            /* store full: abort creation, same as before */
        reset_to_no_venue(L);
        return;
    }
    memset(nv, 0, sizeof *nv);
    nv->id = L->create_venue_id;
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(L->create_gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    snprintf(nv->name, sizeof nv->name, "Track_%04d%02u%02u", y, mo, d);
    nv->lat = L->create_lat;
    nv->lon = L->create_lon;
    nv->radius_m = VENUE_RADIUS_DEFAULT_M;
    nv->flags = 0;                               /* on-device creation is the most-confirmed case, not
                                                  * TRK_F_UNVERIFIED (§10.1/§10.9) */
    nv->n_layouts = 2;
    nv->layouts[0] = L->create_layout;                          /* id 1, "Layout 1" */
    lap_layout_reverse(&L->create_layout, &nv->layouts[1]);     /* id 2, "Layout 1 Reverse" (§10.9 step 4) */

    const uint16_t new_id = nv->id;              /* read before the add, which may copy nv elsewhere */
    if (trk_user_add(nv) != 0) {                 /* invalid venue: abort creation */
        memset(nv, 0, sizeof *nv);               /* leave the slot clean, as trk_user_load_venue does */
        reset_to_no_venue(L);
        return;
    }
    CORE_ASSERT_VOID(trk_get(new_id) != NULL, LAP_ASSERT_CODE);   /* postcondition: the add is visible */
    lap_set_venue(L, trk_get(new_id));           /* → VENUE_FOUND on the saved venue (mode = NORMAL) */
    emit(out, cap, n, EV_VENUE_FOUND, 0, new_id, t_cross, mono, 0, 0);
```

- [ ] **Step 5: Run to pass** — `cmake --build test/build -j8 && ./test/build/test_lap`
Expected: PASS, with `test_create_track_saves_valid_venue` and `test_create_cancel` unchanged.

- [ ] **Step 6: Gates**

```bash
ctest --test-dir test/build                                            # 47/47, 100%
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation  # 0 violations
python3 tools/lint/venue_announce.py                                   # 0 violations
python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json  # 0
source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build \
  && PANEL=ws29v2 ./build.sh moto_sim build \
  && idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build    # all rc 0, 0 warnings
```
`finalize_create` now carries 3 `CORE_ASSERT_VOID`s (≥ 2 required above 20 code lines) and stays far under 60. Report each build's free-DRAM line: `moto_sim` unchanged — the slot is pre-existing static, the 2752 B saved is stack.

- [ ] **Step 7: Commit**

```bash
git add components/core/include/core/trk.h components/core/tracks/trk.c \
        components/core/lapengine/lap.c test/test_lap.c
git commit   # message below + the two-line trailer
```
```
fix(lap): finalize_create builds the new venue in trk_user_scratch()'s static slot — 2752 B off the pipeline task's 8 KB stack (pr:M2)
```

---

### Task 6: Correct the PR #101 description — SERIAL

**Files:** PR #101 body on GitHub (no tracked file; no commit).

- [ ] **Step 1: Re-derive the true list** (never trust the old claim)

```bash
git diff --name-status main..HEAD -- test/snapshots | awk '$1=="M"'
git diff --name-status main..HEAD -- test/snapshots | awk '$1=="M"' | wc -l
```
Expected (verified on `df75497`): **13** — `test/snapshots/{drag_p0_armed_gates, drag_p1_gates, drag_p1_overflow, drag_p2_best, drag_p2_overflow, menu_scrolled, icons_and_bar}.pbm` plus `test/snapshots/213/{drag_p0_armed_gates, drag_p1_gates, drag_p1_overflow, drag_p2_best, drag_p2_overflow, menu_scrolled}.pbm`. A different count means an unexplained golden moved — stop and investigate before editing the body.

- [ ] **Step 2: Fetch the body** — `gh pr view 101 --json body -q .body > /tmp/pr101-body.md` then `grep -n "byte-identical" /tmp/pr101-body.md` (one hit, in the `Verification:` paragraph).

- [ ] **Step 3: Replace the false sentence** — delete

> Every pre-existing gate-row golden is byte-identical, proving the card's main path did not move.

and put in its place:

> Thirteen pre-existing goldens changed content, every one by design and each eyeballed: `drag_p0_armed_gates`, `drag_p1_gates`, `drag_p1_overflow`, `drag_p2_best`, `drag_p2_overflow` and `menu_scrolled` on **both** canvases (12), plus `icons_and_bar` on the 2.9" set (the 2.13" set has no such golden). Causes: B4-F4 moved `DLIST_LABEL_DY` 6 → 12 (the label's cell is now bottom-aligned to its value's) and added the 1 px header rule (`DLIST_RULE_Y`) and the inter-column divider (`DLIST_DIVIDER_X`), which redraws every DRAG gate-list page; B4-F6 renamed the menu rows (`Units:` → `Speed:`, a new `Distance:` row, `Layout` → a dynamic `Layout: <name>`), which redraws `menu_scrolled`; #99 added `ICON_LINK`, and `test_icons_and_bar` renders every entry of `icon_bitmaps[ICON_COUNT]`. `git diff --name-status main..HEAD -- test/snapshots` lists exactly these 13 `M` rows and no unexplained one — the regression guard the old claim was reaching for.

- [ ] **Step 4: Fix the two weaker claims the code now contradicts** — reword any sentence saying the table is loaded "inside its own boot init"/"by the logger task at boot" to "loaded at boot by `logger_load_tracks()` on app_main's task, before the logger and pipeline tasks exist", and any sentence describing `/tracks/user.bin` as a whole-table blob to the per-record `"LTRK"` form. Then `grep -n "own boot init\|blob version 2\|byte-identical" /tmp/pr101-body.md` → no output.

- [ ] **Step 5: Push the body**

```bash
gh pr edit 101 --body-file /tmp/pr101-body.md
gh pr view 101 --json body -q .body | grep -c "Thirteen pre-existing goldens"   # 1
grep -rn "byte-identical" docs/bench/    # no output — verified: the claim lives only in the PR body
```

---

### Task 7: File `pr:M3`–`M6` as issues — SERIAL

Owners per the design doc's "Filed, not fixed" table: M3/M4 → plan **F2b**; M5/M6 → plan **F4**.

- [ ] **Step 1: Create all four**

```bash
gh issue create --label bug --title "Track creation while storage is dead or full reports success — the venue lives only in RAM, and tracks_save() ignores the 5% reserve" --body 'PR #101 review (`.superpowers/reviews/2026-10-13/pr-101.md`, M3). Owner: audit fix wave plan **F2b**.

`create_finish_if_active()` (`components/app/pipeline/pipeline.c:401-414`) sends `LOGGER_SAVE_TRACKS` with `requester == NULL`, so nothing consumes the rc. `tracks_save()` (`components/app/logger/logger.c:518`) returns -1 on an `sto_open` failure and only `errlog_add(E_STO_WRITE, 0)`. The rider sees the VENUE one-shot with the new `Track_YYYYMMDD` and has no indication the venue is gone at the next reboot. Related: `tracks_save()` is the only writer in `logger.c` that does not honour `STO_RESERVE_PCT` (`logger.c:87`, enforced only in `open_session()` at `:299`) — it can write ~10 KB of `.tmp` (`TRK_MAX_USER * (TRK_USER_REC_MAX + 4)`) beside the existing file on a filesystem kept above a 5% floor. It self-heals (`sto_unlink` on every error path), so that half is headroom consumption, not a deadlock.

Wanted: make the save rc reach the rider (an EV_CREATE FAILED phase or a distinct one-shot), and gate `tracks_save()` on the same reserve check `open_session()` uses.'

gh issue create --label bug --title "After an RTC resume the Layout menu reads Auto on a layout-locked lap, and the first MODE press changes the layout mid-lap" --body 'PR #101 review (`.superpowers/reviews/2026-10-13/pr-101.md`, M4). Owner: audit fix wave plan **F2b**.

`on_fix_try_resume()` posts `ui_post_venue(lr.venue_id, fix->gps_us)`. `handle_venue_found()` (`components/app/ui/ui.c:1237-1240`) then unconditionally sets `s_layout_choice = 0` ("Auto") and clears `s_model.layout_name` — but `lap_import_rtc()` (`components/core/lapengine/lap.c:291-318`) has just set `forced_layout_id = s->layout_id` and locked it, and emits no `EV_LAYOUT_LOCKED` (it has no event buffer). So the menu reads `Layout: Auto` on a lap locked to layout N, and the first MODE press advances Auto -> `layouts[0]`, posting `CMD_SET_LAYOUT` for a possibly different layout and wiping `s_best` under the seqlock (`handle_set_layout`).

Wanted: carry the resumed `layout_id` to the ui (an explicit post, or let `lap_import_rtc` report it) so `s_layout_choice` starts on the locked layout. Note #103: the resume path is unreachable on a sim build, so this needs a host test or a real-GPS bench.'

gh issue create --label bug --title "venue_announce.py is satisfied by an announce call anywhere in the enclosing function, not on the acquisition path" --body 'PR #101 review (`.superpowers/reviews/2026-10-13/pr-101.md`, M5). Owner: audit fix wave plan **F4** (linter hardening).

`tools/lint/venue_announce.py`'"'"'s `check_file()` passes a site as soon as `ANNOUNCE_RE.search(stripped, open_idx, close_idx)` matches anywhere between the enclosing function'"'"'s braces. So a `lap_set_venue()` in one branch plus an unrelated `ui_post_venue()` in another branch passes, and so does announcing a *different* venue id; an acquisition whose enclosing function `find_top_level_functions()` cannot parse is skipped silently (`body is None -> continue`). What does work and should be kept: the zero-sites floor (`total_sites == 0 -> rc 1`) really does close the API-rename hole it was written for.

Wanted: require the announce to follow the acquisition in the same basic block (or at least the same if/else arm), fail loudly instead of skipping an unparsable function, and add a probe file per hole to the linter'"'"'s own tests.'

gh issue create --label enhancement --title "No host test for /tracks/user.bin file-level framing (bad magic, wrong version, count overrun, mid-file bad CRC, len 0 / len > max)" --body 'PR #101 review (`.superpowers/reviews/2026-10-13/pr-101.md`, M6). Owner: audit fix wave plan **F4**.

`test/test_trk.c` covers the record codec well (truncated/empty/one-byte-short records, same-id replace, mid-record decode-failure cleanup, the exact `TRK_USER_REC_MAX` worst case). Nothing exercises `logger.c`'"'"'s file-level paths against a hostile file: bad magic, wrong version byte, a `count` larger than the records present, a bad-CRC record in the middle followed by a good one, `len == 0`, `len > TRK_USER_REC_MAX`. The reviewer read all of them and found them correct — they are simply unverified, on a flash file the code is told to treat as untrusted, in a module with no host harness. Spec §10.1 (rewritten in fix wave F0) now states these semantics byte-exactly, so the tests have something to assert against.

Wanted: a host harness for the `tracks_save`/`logger_load_tracks` framing (a seam over `hal/storage.h`, or the framing extracted into a pure helper), one case per bullet above — including the key one: a bad-CRC record in the middle must be skipped and the record after it still loaded.'
```

- [ ] **Step 2: Record the numbers** — note the four issue numbers `gh` prints; plans F2b and F4 and Task 8's report reference them. `gh issue list --limit 6` → the four new issues above #107.

---

### Task 8: Merge — SERIAL, user-driven

- [ ] **Step 1: Full gate run on the branch**

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8 \
  && ctest --test-dir test/build                                                 # 47/47, 100%
cmake --build devcontroller/test/build && ctest --test-dir devcontroller/test/build --output-on-failure  # 100%
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation            # 0, rc 0
python3 tools/lint/venue_announce.py                                             # 0, rc 0
python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json  # 0
source tools/idf-env.sh
./build.sh moto_sim build && ./build.sh moto_neo6m build && PANEL=ws29v2 ./build.sh moto_sim build \
  && idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build    # all rc 0, 0 warnings
node --check devcontroller/web/app.js && (cd devcontroller && idf.py build)       # rc 0, 0 warnings
```
Report each firmware build's free static DRAM line in the PR thread (baseline `moto_sim` 4192 B; the PR reported 4072 B; this plan should move it by ≤ 8 B).

- [ ] **Step 2: Push and watch CI** — `git push`; `gh pr checks 101 --watch`.
Expected: every required check green — hygiene, host-tests, dc-host-tests, tools-tests, tracks-generated, build (moto_neo6m), build (moto_sim), build (devcontroller).

- [ ] **Step 3: Hand the merge to the user.** The agent never merges. Report: the five commits, the corrected PR body, the four issue numbers, the DRAM lines, and the two bench questions still open from bench day 5 — the #99 LINK-glyph check (needs the operator to unplug the connector) and the `Track_20260915` reading, which the reviewer resolved: the sim capture's first fix is `1789466520000000 µs` = 2026-09-15 10:02 UTC, so that name is correct, not a defect. Then ask the user to squash-merge #101 into `main`.

- [ ] **Step 4: No gate tag.** Repo convention tags a plan only for a bench gate it actually ran (`git tag` shows `p07-d1`…`p07d-d1`, `p5.6-d1`…, `dsA-d1`, `dsB-d1`). **F0 runs no bench** — every change is host-tested or documentation, and the bench item Task 2 appended belongs to the one bench day scheduled after F2b merges. Create **no** `p07e-d1` or any other gate tag here; the merge commit on `main` is F0's only marker.

---

## Self-Review

**1. R32 + §5 coverage** — every item maps to exactly one task:

| R32 / design item | Task |
|---|---|
| spec §10.1 → the shipped `"LTRK"` format, per-record skip semantics, deleted `trk_user_load`/`trk_user_save` | 1 (1–2) |
| spec §10.9 app-side paragraph → the real boot-load site | 1 (3) |
| `trk.h`/`trk.c` ownership comment: logger reader + ordering argument | 4 |
| drag snapshot on every `drag_state()` change (`s_last_pub_state` compared in `on_raw`); silence pinned by `test_false_start_abort` (+2 asserts) and `test_done_to_idle_settle_is_silent` | 2 |
| `s_menu_action[UI_MENU_MAX]` | 3 (5) |
| `finalize_create` off the 2752 B local → static landing slot; creation unchanged (existing `test_create_track_saves_valid_venue`) + the new `NULL`-slot branch (`test_create_refuses_when_user_table_full`) | 5 |
| the three false comments (`lt_ipc.h` ×2 sites, `event.h`, `app.js` + `rerenderOnChange`), `www.bin` | 3 (1–4, 7) |
| PR body: the 13 changed goldens and why | 6 |
| `pr:M3–M6` filed, four `gh issue create` commands with full bodies and owners | 7 |
| merge: CI green, user squash-merges, tag policy | 8 |

**2. Placeholder scan** — no `TBD`, no "add error handling", no "similar to Task N", no "write tests for the above"; every code, test, comment, spec and issue-body step carries its literal text. Two deliberate deviations, both labelled with their reason: Task 2's characterisation test (no host seam exists for a `pipeline.c` defect) and Task 1's forward reference to `trk_user_scratch()` (spec'd in Task 1, implemented in Task 5 — both land in this PR).

**3. Name consistency** — `trk_user_scratch(void)` returning `trk_venue_t *` is spelled identically in Task 1's spec block, Task 5's `trk.h` declaration, `trk.c` definition, `lap.c` call and `test_lap.c` assertion. `s_last_pub_state` (`uint8_t`) is declared, updated in `publish_drag_snapshot()` and read in `on_raw`, all in Task 2. `DRAG_ST_*`, `DRAG_DONE_SETTLE_S`, `FUSION_HZ`, `TRK_MAX_USER`, `TRK_USER_REC_MAX`, `TRACKS_FILE_MAGIC`, `TRACKS_FILE_VERSION`, `STO_RESERVE_PCT`, `VENUE_RADIUS_DEFAULT_M`, `UI_MENU_MAX`, `PIPE_STACK_BYTES` all match the branch's headers verbatim. Test helpers used and verified present on `df75497`: `fix_ll`, `lat_of`, `lon_of`, `feed_hdg`, `build_venue_2layout`, `count_type`, `evlog_t`, `SPD_MMS` (`test_lap.c`); `arm_engine`, `drive_fused`, `run_quarter_then_brake`, `fused`, `gfix`, `EV`, `DT_US`, `LAUNCH_K`, `D` (`test_drag.c`).

**4. Deliberately out of F0's scope** — `pr:L5–L11` (unbounded `drag_run_state` assign, same-day `Track_YYYYMMDD` collisions, `lap_mark_gate`'s 1 mm/s "moving", the mixed-unit custom DIST label, the dead "New track" item in DRAG mode, the missing step-overflow golden, `cfg.h`'s partial `_Static_assert`) are neither in R32 nor in the filed table; they sit in the audit's LOW tier, carried by F1a/F1b/F4. Do not fold them in — F0 must merge first and small.
