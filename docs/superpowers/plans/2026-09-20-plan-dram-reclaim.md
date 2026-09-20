# Static DRAM Reclaim (safe tier) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reclaim static internal DRAM (`.bss`) by right-sizing and streaming the firmware's own oversized buffers — behavior-preservingly, keeping the codebase Power-of-10 compliant — so the connectivity work (conn_ble, ~5 KB on neo6m) fits with comfortable roadmap headroom.

**Architecture:** No new subsystems. Each task tightens one existing module: eliminate assemble-then-emit scratch buffers by streaming to the already-open sink; narrow over-wide struct fields to their provable ranges; remove a duplicate snapshot buffer via a small read accessor; and — verify-first — trim two bound-coupled arrays only to their computed worst case plus margin. App-side (`logger`, `cmd`, `export_serial`) and core-side (`trk_json`, `pipeline`) are disjoint and execute in parallel worktrees, then integrate serially.

**Tech Stack:** C, ESP-IDF v5.3.2 (esp-13.2.0). Build via `source tools/idf-env.sh` then `./build.sh <env> build`. Envs: `moto_neo6m` (production target — the DRAM budget that matters) and `moto_sim`.

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (product) + the Power-of-10 compliance design. Detailed per-symbol audit rationale (byte math, classifications): `scratchpad/audit-app.md` and `scratchpad/audit-core.md` (read the relevant rows for the task you are on).

## Global Constraints

- **Power of 10, blocking:** `tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` MUST report 0 violations; the build runs with `-Werror`. Rule 3 (no dynamic allocation after init) — reclaim by right-sizing/streaming, **never by moving to heap**. Rule 5 — every function over 20 code lines keeps ≥2 assertions; a new/rewritten function obeys this. Rule 4 — ≤60-line functions, ≤30-line compound bodies. Any new function pointer needs a `docs/power-of-10-deviations.md` entry (none expected here).
- **Behavior-preserving:** existing host tests and the on-target `core_selftest` must still pass. Output that crosses a wire or lands on storage must be **byte-identical** before/after (the §18.1 command protocol, the §12.x `.log`/`.sum` frame formats, the §14.3 LIST/READ JSON). The §18.1 data-chunk payload cap (496 B) is a protocol constant — never change it.
- **Keep `TRK_MAX_LAYOUTS = 8`** (`core/consts.h`). The 8→4 reduction was explicitly deferred (product ceiling + import-contract + spec amendment). Do not touch it or any `TRK_MAX_USER`/`LAP_MAX_SECTORS` spec constant in this plan.
- **Build both envs green:** `moto_neo6m` and `moto_sim` both `rc=0`; app image within the `build.sh` size gate (≤1.2 MB). Source `tools/idf-env.sh` first (must print `ESP-IDF v5.3.2`).
- **Measure and record:** every task records the `moto_neo6m` DRAM line (`idf.py -C . -B build/moto_neo6m size | grep DRAM`) BEFORE and AFTER its change in its report. neo6m is the budget of record; report sim too.
- **Target (honest):** floor = conn_ble fits with ≥10 KB free on neo6m; stretch = toward ~20 KB where each cut is verifiably safe. Verify-first tasks (B1, B2) reclaim only to proven-bound + margin, and may legitimately yield little or nothing — that is an acceptable outcome, not a failure.

---

## Task 1 (A1): logger.c — stream `.sum` instead of the `buf[SUM_BUILD_CAP]` scratch

**Files:**
- Modify: `components/app/logger/logger.c` (`rebuild_sum`, lines ~155–180; `static uint8_t buf[SUM_BUILD_CAP]` at ~158)
- Test: `components/app/logger/test/` (host test) or the on-target logger test that produces a `.sum`

**Interfaces:**
- Consumes: `ses_encode_hdr/_venue/_end(..., uint8_t *dst, size_t cap)` (return bytes written), `s_lap_acc`/`s_lap_len`, `s_drag_acc`/`s_drag_len`, and the open `.sum` file descriptor used by `rebuild_sum`.
- Produces: no signature change — `rebuild_sum` still produces a byte-identical `.sum` file.

**Reclaim:** −4096 B (eliminate `static uint8_t buf[SUM_BUILD_CAP]`). Class A. This is the largest permanent win.

- [ ] **Step 1: Characterize current behavior (capture a golden `.sum`)**

Read `rebuild_sum` fully. It currently encodes HDR, VENUE, then `memcpy`s `s_lap_acc` and `s_drag_acc`, then END, all into `buf`, then writes `buf[0..off]` to the `.sum` fd in one `sto_write`. Write/extend a test that drives a representative session (HDR + VENUE + several laps in `s_lap_acc` + a drag run in `s_drag_acc` + END) through the existing code path and saves the produced `.sum` bytes as a golden fixture.

- [ ] **Step 2: Run the test to confirm the golden capture passes on current code**

Run the logger test. Expected: PASS (golden matches current output). This locks the byte contract before refactor.

- [ ] **Step 3: Rewrite `rebuild_sum` to stream section-by-section to the fd**

Replace the single scratch buffer with sequential writes to the open `.sum` fd, preserving order and bytes exactly: encode HDR into a small local (reuse a ≤256 B stack buffer sized to the max of the HDR/VENUE/END encodings — confirm from `ses_encode_*` max output), `sto_write` it; encode VENUE, `sto_write`; `sto_write(s_lap_acc, s_lap_len)`; `sto_write(s_drag_acc, s_drag_len)`; encode END, `sto_write`. Keep every existing `LT_ASSERT_VOID` bound check (cap guards become guards on the small local). Keep the function ≤60 lines / bodies ≤30 (split a `write_section` helper if needed — it needs ≥2 assertions if >20 lines). Note: the write cadence to storage changes (5 small writes vs 1), but the `.sum` file content is identical; if `.sum` must be one atomic write for a spec reason, instead stream into `s_batch`-style reuse — but prefer direct fd writes.

- [ ] **Step 4: Run the test — `.sum` bytes must equal the golden fixture**

Run the logger test. Expected: PASS (byte-identical `.sum`).

- [ ] **Step 5: Build both envs, lint, measure**

```bash
source tools/idf-env.sh
tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
./build.sh moto_neo6m build && ./build.sh moto_sim build
idf.py -C . -B build/moto_neo6m size | grep -i DRAM
```
Expected: lint 0, both `rc=0`, neo6m DRAM free up ~4 KB.

- [ ] **Step 6: Commit** — `refactor(logger): stream .sum to fd, drop 4KB SUM_BUILD_CAP scratch`

---

## Task 2 (A2): logger.c + cmd.c — small safe trims (`s_batch`, `s_sess` narrowing, `s_err` dedup)

**Files:**
- Modify: `components/app/logger/logger.c` (`#define BATCH_CAP 4096` → `3840`, line 54)
- Modify: `components/app/cmd/cmd.c` (`sess_ent_t` fields ~508–517; `static lt_err_entry_t s_err[32]` line 46 usage at ~207, ~239)
- Modify: `components/app/include/app/lt_nvs.h` + `components/app/lt_nvs.c` (add a per-entry errlog accessor)
- Test: existing cmd host tests for LIST / ERRLOG_GET / DIAG_GET; logger batch test

**Interfaces:**
- Produces: `int lt_errlog_at(int index, lt_err_entry_t *out);` (or `lt_errlog_count(void)` + `lt_errlog_at`) in `lt_nvs.h` — returns one error-ring entry by index with the same ordering `lt_errlog_snapshot` used, 0 on success / <0 out-of-range. Preserves whatever ring-copy semantics `lt_errlog_snapshot` had.

**Reclaim:** `s_batch` −256 B (B); `s_sess` narrowing −1024 B (B); `s_err` dedup −512 B (D). ≈ −1.8 KB, all permanent.

- [ ] **Step 1: `s_batch` cap 4096→3840.** Justify in a comment: max real content before a flush = `BATCH_FLUSH_B` (3584) + one max real frame (DRAG_RUN 16 gates = 211 B, §12.3) = 3795 ≤ 3840. Keep all `<= BATCH_CAP` asserts (they now compare to 3840). Run the logger batch/flush test — must pass.
- [ ] **Step 2: Narrow `sess_ent_t`.** `log_kb`/`sum_kb` `uint32_t`→`uint16_t` (max KB for the ~1.34 MiB storage partition = 1375 < 65535); `start_utc` `int64_t`→`uint32_t` (UTC epoch seconds, fits until 2106); `laps` `int`→`int16_t`. Add a one-line comment on each bound. Adjust the `printf`/format specifiers at the LIST render site accordingly. Run the LIST host test — output values must be identical for a large-partition fixture.
- [ ] **Step 3: Write the failing test for `lt_errlog_at`** — assert it returns the same entries, in the same order, as `lt_errlog_snapshot` did (drive a few errlog_add calls, compare index-by-index).
- [ ] **Step 4: Implement `lt_errlog_at`/`lt_errlog_count`** in `lt_nvs.c`, declared in `lt_nvs.h`. Run the test — PASS.
- [ ] **Step 5: Replace `s_err[32]` snapshot usage in `cmd.c`** (`op_errlog_get`, `op_diag_get`): format each entry via `lt_errlog_at(i, &e)` into the JSON directly, removing the `static lt_err_entry_t s_err[32]` array. Keep loop bounds asserted (rule 2/5). Run the ERRLOG_GET + DIAG_GET host tests — output byte-identical.
- [ ] **Step 6: Build both envs, lint, measure** (as Task 1 Step 5). Expect neo6m free up ~1.8 KB.
- [ ] **Step 7: Commit** — `refactor(cmd,logger): narrow s_sess fields, drop s_err snapshot via lt_errlog_at, trim s_batch`

---

## Task 3 (A3): export_serial.c — stream STATUS/CONFIG_GET/ERRLOG_GET/DIAG_GET; trim `s_setbuf`; drop duplicate `laps[24]`

**Files:**
- Modify: `components/drivers/export_serial/export_serial.c` (`s_ser.buf`/`SER_ASM_MAX`; the four ops; `s_setbuf`; `dbg_laps`'s `static lap_result_t laps[24]`)
- Modify: `components/app/pipeline/pipeline.c` + `components/app/include/app/pipeline.h` (add `pipeline_lap_at`)
- Test: existing export_serial op tests (STATUS/CONFIG_GET/ERRLOG_GET/DIAG_GET output over the serial transport); `dbg laps` test

**Interfaces:**
- Produces: `int pipeline_lap_at(int index, lap_result_t *out);` in `pipeline.h` — returns one completed lap by index using the SAME seqlock retry semantics as `pipeline_laps_snapshot` (retry while `s_laps_seq` is odd / changed), 0 on success / <0 out-of-range. Consumes: the file already has a two-pass `run_stream`/`sframe_t` streaming pattern (used for LIST/OPEN/READ) — reuse it, do not invent a new one.

**Reclaim:** `s_ser.buf` streaming −2816 B (A, interim — this module leaves in sub-project C); `s_setbuf` 704→256 −448 B (B); `laps[24]` dedup −1920 B (D). Note `s_setbuf`: the console's own `max_cmdline_length` is 256 and the file already asserts against it — confirm before trimming.

- [ ] **Step 1: Capture golden output** for STATUS, CONFIG_GET, ERRLOG_GET, DIAG_GET over the serial transport (existing tests likely already assert these — extend to byte-compare if not).
- [ ] **Step 2:** Convert those four ops to the file's existing two-pass streaming (`run_stream`/`sframe_t`) so they emit in ≤496 B frames instead of composing the whole response in `s_ser.buf`. Shrink `SER_ASM_MAX` to the remaining true max (only whatever op still assembles). Run the tests — byte-identical.
- [ ] **Step 3:** `s_setbuf` 704→256 after confirming `max_cmdline_length == 256` and the existing assert. Run the console-input test.
- [ ] **Step 4: Add `pipeline_lap_at`** (test first: same laps, same order, as `pipeline_laps_snapshot`; then implement with seqlock retry). Replace `dbg_laps`'s local `laps[24]` with a per-iteration `pipeline_lap_at(i,&lap)` call. Run the `dbg laps` test — identical output.
- [ ] **Step 5: Build both envs, lint, measure.** Expect neo6m free up ~5 KB.
- [ ] **Step 6: Commit** — `refactor(export_serial,pipeline): stream 4 ops, add pipeline_lap_at, drop dup laps[24], trim s_setbuf`

---

## Task 4 (B1): trk_json.c — verify-first `toks[]` sizing

**Files:**
- Modify: `components/core/tracks/trk_json.c` (`toks[512]`, ~10,240 B — likely `jsmntok_t toks[N]`)
- Test: `components/core/tracks/test/` — track-JSON parse tests

**Reclaim:** up to −5120 B (B) — **only if verified**. The audit's own range (147–615 tokens) means 512 may be marginal; do not blind-cut.

- [ ] **Step 1: Compute the true worst-case token count.** Read `trk_json.c`'s parse. Construct (in a test) the maximal legal venue JSON under the current spec: `TRK_MAX_LAYOUTS`(8) layouts, each `LAP_MAX_SECTORS`(8) sectors, all optional fields present, longest legal names. Count jsmn tokens for it (run jsmn in the test, or compute from the grammar). Record the number.
- [ ] **Step 2: Decide the size from evidence.** If worst-case + small margin (say +16) < 512, resize `toks[]` down to that and add a `_Static_assert`/comment tying the size to the computed bound. If worst-case ≥ 512, DO NOT shrink — instead flag in the report that `toks[512]` is at/над its real bound (a pre-existing risk worth a separate ticket), and reclaim 0 here.
- [ ] **Step 3: Test both boundaries** — a max-venue JSON parses successfully at the new size; a JSON one token over the new cap fails cleanly (the existing `> TRK_MAX_LAYOUTS` / token-exhaustion error path). Run parse tests.
- [ ] **Step 4: Build both envs, lint, measure.** Record the actual delta (may be 0).
- [ ] **Step 5: Commit** — `refactor(trk_json): size toks[] to verified worst-case (N tokens)` (or a report-only note if 0).

---

## Task 5 (B2): pipeline.c — verify-first `ugate[]` sizing (+ report drift notes)

**Files:**
- Modify: `components/app/pipeline/pipeline.c` (`lap_t.ugate[LAP_MAX_UGATES]`, where `LAP_MAX_UGATES = TRK_MAX_LAYOUTS * LAP_MAX_SECTORS = 64`)
- Test: lap-engine tests exercising a multi-layout venue's unique-gate set

**Reclaim:** up to −1792 B (B) — **only if verified**. Halving `ugate` bets that unique gates across a venue's layouts ≤ 32; this is the same "do all venues fit" ceiling we deferred with `TRK_MAX_LAYOUTS`. Treat with equal caution.

- [ ] **Step 1: Determine the real upper bound on unique gates** for a venue with `TRK_MAX_LAYOUTS=8` layouts. Read how `ugate[]` is filled (the deduplicated union of all layouts' sector gates). If the code/spec guarantees ≤32 unique gates (e.g. gates are shared across layouts by construction), that bound is safe. If a legal venue can have up to 64 distinct gates, `ugate` MUST stay 64 — reclaim 0.
- [ ] **Step 2:** Only if a ≤32 bound is provable from the spec/data model (not just from today's bundled venues), resize with a `_Static_assert` and comment citing the guarantee. Otherwise leave it and note the finding.
- [ ] **Step 3: Report the two audit drift notes (no code change):** `lt_ipc.c` `fused_sample_t` is 40 B in code vs 24 B in the §4.4 spec table; `s_fb_bits` sim-only drop was considered and rejected (loses render test coverage). File these as observations in the task report for a future spec-reconciliation ticket.
- [ ] **Step 4: Build both envs, lint, measure.** Record actual delta (may be 0).
- [ ] **Step 5: Commit** — `refactor(pipeline): size ugate[] to verified bound` (or report-only note if 0).

---

## Task 6: Integration + wired-conn_ble fit proof

**Files:** none (integration + measurement). Depends on all prior tasks merged.

- [ ] **Step 1:** Merge app-side (Tasks 1–3) and core-side (Tasks 4–5) branches into the reclaim integration branch. Resolve any trivial conflicts (the tasks touch disjoint files except `pipeline.c`/`pipeline.h`, edited by A3 and B2 — integrate those two serially).
- [ ] **Step 2:** Rebuild both envs, lint 0. Record the aggregate neo6m DRAM free.
- [ ] **Step 3: Prove conn_ble fits.** Cherry-pick/merge `conn_ble` (`s5.2-conn-ble`, commit 6322039) and re-apply its `app_main` wiring (`scratchpad/app_main_wiring.patch`). `rm -rf build/moto_neo6m build/moto_sim`, rebuild both. Expected: both `rc=0` (no `dram0_0_seg overflowed`), neo6m DRAM free ≥ 10 KB. This is the gate that this whole plan existed to pass.
- [ ] **Step 4:** If free < 10 KB, report the shortfall and the remaining levers (deferred `TRK_MAX_LAYOUTS` 8→4, deferred `s_stack` HWM shrink, sub-project C dev-tooling relocation) — do not blind-cut further.
- [ ] **Step 5: Commit / report** the final DRAM budget.

---

## Self-Review

- **Spec coverage:** the plan tightens existing buffers only; it changes no spec requirement (explicitly holds `TRK_MAX_LAYOUTS=8`, §18.1 chunk 496, §12.x/§14.3 formats). Behavior-preservation is gated by byte-identical tests on every wire/storage output.
- **Type consistency:** `pipeline_lap_at`/`lt_errlog_at`/`lt_errlog_count` signatures are defined in Tasks 2–3 and consumed in the same tasks; no forward references across owners except `pipeline.h` (A3 adds `pipeline_lap_at`, B2 reads `pipeline.c` — integrate serially per Task 6 Step 1).
- **Placeholder scan:** verify-first tasks (B1, B2) intentionally have evidence-gated outcomes; a 0-byte reclaim there is a valid, documented result, not a placeholder.
