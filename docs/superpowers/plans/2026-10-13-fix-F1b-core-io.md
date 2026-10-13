# F1b — core io + ui core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every `io:*` and pure-`core/ui` finding of the 2026-10-13 audit — two CRITICALs (garbage UTC / truncated export rows; a dropped JSON summary reported as success), two HIGHs (the §17.4 fault-strip bit→icon table; `walk()`'s recursion), M1–M9, L1–L5, every false claim in the slice, and the reviewer's five coverage gaps.

**Architecture:** Four disjoint file lanes — **A** session+export, **B** config+util, **C** pure UI, **D** `ring.h` — then a serial final wave (gates, DRAM, spec). Lanes may run as parallel worktree tasks or straight through 1→16 in one session; they share no file either way. Every fix is TDD: a failing Unity test built from the reviewer's own reproducer inputs, then the C change, then the test, then a commit and a checkpoint.

**Tech Stack:** C11 (Power of 10), Unity host tests in `test/` (CMake globs `test_*.c` — no CMake edit for a new suite), PBM goldens on both canvases (`test_screens` 296×128, `test_screens_213` 250×122), ESP-IDF v5.3.2 for the firmware gates.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding — this is row **F1b**: rulings **R18** icons half, **R20**, **R26**, **R27**; gates §4). Finding ids are from `.superpowers/reviews/2026-10-13/core-io-ui.md` (git-ignored; quoted by the audit's §7.1–7.3 io rows). Behaviour/layout authorities: `2026-09-14-lap-timer-design.md` §12.2–12.4, §14, §15.1, §17.4, §20.2, §20.5 and `2026-09-27-plan-7b-glanceable-ui-design.md`.

**Line numbers:** F0 (PR #101) merges **before** F1b. Citations for `cfg.c`, `cfg.h`, `cfg_json.c`, `screens_moto.c`, `icons.c`, `icons.h`, `model.h`, `units.c`, `test_screens.c`, `test_ui.c`, `test_cfg.c` and the goldens are from the **PR #101 branch** (`/Users/benji/projects/personal/lap-timer-worktrees/gdm`, head `df75497`) — the tree F1b starts from. Everything else is `main` @ `f4d4db2`.

## Global Constraints

- Power of 10: no recursion, heap, goto or function pointers; every loop bounded by a constant or table field; ≤ 60 code lines per function; a function over 20 code lines carries ≥ 2 *meaningful* `CORE_ASSERT_*` (never `abort`); `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0.
- **R20:** no tautological padding assertions, no side effects inside an assertion condition. Where a padding assertion leaves a function that stays over 20 code lines, a real pre/postcondition replaces it so the linter's count stays ≥ 2. No new rows in `docs/power-of-10-deviations.md`.
- Zero warnings: host suite (`-Wall -Wextra -Werror -Wshadow -Wconversion`, ASan+UBSan Debug), the gcc-16 sweep (`.superpowers/bench-helpers/gcc_sweep.py`), and clean ccache-off `moto_sim` / `moto_neo6m` / `PANEL=ws29v2 ./build.sh moto_sim build` / `idf.py -C test_apps/core_selftest build`.
- `components/core` stays pure C11 (no malloc, no IDF, no FreeRTOS); `core/ui` additionally stays free of `<stdio.h>` (`menu_labels.c` is the one documented exception).
- Goldens are regenerated only by the workflow at the top of `test/test_screens.c` (delete the golden, run, eyeball the `.actual.pbm` dump, promote, re-run byte-exact), **always on both canvases**. Never hand-edit a `.pbm`.
- This plan adds **no new test file**, so `test_apps/core_selftest`'s `list(FILTER … EXCLUDE REGEX "/test_(ui|screens|epd_pure[a-z0-9_]*|sim_profile|sim_clock)\\.c$")` is unchanged. If a task is split and a new host-only suite appears, extend that regex in the same commit.
- No flashing, no `esptool`, no serial port from a subagent.
- Every commit message ends with this branch's session trailers exactly as `git log -1 --format=%B` on the working branch shows them, including `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- **Checkpoint protocol.** The executing session may `/clear` between tasks, so no state lives in context. Ledger: `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` (create it in Task 1 if absent). Every task ends with a **CHECKPOINT** step: append `Task N complete <sha>`, push the branch, and record the task's exact resume prompt. Work in a worktree on branch `fix-core-io`. Tasks are grouped into four sessions of ≤ 4 tasks; each session ends with a session checkpoint that re-runs the host suite and the linter.

## Sessions

| Session | Tasks | Lanes touched |
|---|---|---|
| 1 | 1–4 | A (export/session) |
| 2 | 5–8 | A tail + B (config/util) |
| 3 | 9–12 | B tail + C (ui) |
| 4 | 13–16 | C tail, D, final wave |

## File ownership (hard boundary)

Mine: `components/core/{session,export,config,util,sys}/*`, `components/core/ui/*`, `components/core/include/core/{ses,exp,cfg,cfg_blob,json,jsmn,jw,ring,blob,bw}.h`, `components/core/ui/include/core/ui/*`, the matching `test/*.c` + `test/snapshots/**` (both canvases).

Not mine: `components/core/{lapengine,dragengine,fusion,geo,timebase,tracks}` (F1a); `components/app/**`, `components/drivers/**`, `main/**` (F2a/F2b/F3); **spec §5.2's API blocks (F4)** — reviewer false-claim 8: §5.2 declares `ses_frame_cb_t`, `ses_reader_feed`, `ses_reader_flush` and `core_set_assert_hook`, none of which exist in the tree (`ses.h` ships the pull API `ses_reader_push`/`_next`/`_finish`), and its `cfg.h` block omits `cfg_migrate_supported` (`cfg.h:79`, used by `cfg_blob.c:26`). Recorded here, fixed in F4.

**Two declared cross-boundary one-liners** (F1b merges before F2a/F2b, which rebase onto it):
1. `main/app_main.c` `boot_config()` — R26's `cfg_validate` before the no-blob save (Task 8). F2a's task list does not touch that branch.
2. `components/app/ui/ui.c` `render_fb()` `:1498-1514` — the two `s_fb.dirty` assertions must go with the field (Task 11). F2b's ui.c sites are the menu-lock/cfg-lock paths.

**Spec/doc amendments this plan owns** (Task 16, so F4 does not duplicate): main spec §12.3, §14.3, §20.2, §20.5; 7b spec §5/§6/§7. F4 keeps §4.2, §4.6, §5.2, §10.1, §11.4, §18.4, §19.1, §21.3, §21.5, §23.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `session/ses_records.c`, `include/core/exp.h`, `export/exp.c`, `exp_nmea.c`, `exp_vbo.c`, `exp_json.c` | reject `gps_us <= 0`; clamp UTC; skip+count; checked `snprintf`; `"skipped":N` | 1 |
| `export/exp_json.c` | `json_stage` after emit; 512 B header buffer + static assert | 2 |
| `export/exp_nmea.c` | 232 B reserve; `exp_pull` small-cap coverage | 3 |
| `session/ses_records.c` | full 32-byte wire venue name | 4 |
| `export/exp.c`, `session/ses_records.c` | two false comments | 5 |
| `util/json.c`, `include/core/json.h` | `json_tok_bool` type; numeric truncation/range | 6 |
| `config/cfg_json.c` | iterative `walk`; benches split | 7 |
| `config/cfg.c`, `include/core/cfg.h`, `main/app_main.c` | profile validation; bench tail; boot save | 8 |
| `util/jw.c`, `util/json.c` | padding assertions | 9 |
| `ui/icons.c`, `ui/include/core/ui/icons.h`, `ui/screens_moto.c`, `ui/include/core/ui/model.h`, `docs/display-glyphs.md`, goldens | four new glyphs; §17.4 remap; dead-icon deletion | 10 |
| `ui/render.c`, `render.h`, `model.h`, `canvas.h`, `components/app/ui/ui.c` | `fb_t.dirty` removal + R20 in `render.c` | 11 |
| `ui/screens_moto.c` | bounded `put_str`; R20 in `screens_moto.c` | 12 |
| `ui/refresh_policy.c`, `fonts.h`, `screens_moto.c` | L1 + remaining false claims | 13 |
| `include/core/ring.h`, `test/test_ring.c` | bounded `ring_pop`; power-of-two `ring_init` | 14 |
| gates, DRAM, bench checklist | final wave | 15 |
| spec + 7b amendments | final wave | 16 |

Build/run throughout: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure`.

---

## Session 1 — Tasks 1–4 (lane A: export + session)

### Task 1: reject `gps_us <= 0`, clamp UTC, skip-and-count a bad record (io:C1, R26)

**Files:**
- Modify: `components/core/session/ses_records.c:100-133`, `components/core/export/exp.c:12-23,42-57`, `components/core/include/core/exp.h:23-54`, `export/exp_nmea.c:25-56`, `export/exp_vbo.c:12-59`, `export/exp_json.c:72-100,153-198`
- Test: `test/test_ses_records.c`, `test/test_exp_nmea_json.c`, `test/test_exp_vbo.c`

**Interfaces:**
- Consumes: `ses_decode_fix(ses_fix_state_t*, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out)` → 1 decoded / 0 not a fix record / −1 rejected.
- Produces: `exp_t` gains `uint16_t skipped;` (records dropped by a decode failure, saturating at `0xFFFF`); `void exp_skip_record(exp_t *e);` in `core/exp.h` — the single increment site, called by all four format feeds; `exp_json_finish` emits `"skipped":N` as the top-level object's last member; `exp_utc_parts` clamps a negative `gps_us` to 0.

- [ ] **Step 1: Failing tests.** `test/test_ses_records.c` (add `#include "core/exp.h"`; register both in `main`):

```c
static void test_decode_fix_rejects_non_positive_gps_us(void)
{
    /* io:C1 (R26): §12.2's resync accepts any frame whose CRC16 matches, so a damaged log can
     * admit one mis-framed FIX_KEY; a negative gps_us then produced ~4.29e9 UTC fields. */
    uint8_t p[39]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, -1000000000000LL);                       /* the reviewer's reproducer value */
    bw_i32(&w, -338561234); bw_i32(&w, 185123456); bw_i32(&w, 45000);
    bw_i32(&w, 18500); bw_i32(&w, 9012000); bw_u32(&w, 100); bw_u16(&w, 10);
    bw_u16(&w, 120); bw_u8(&w, 3); bw_u8(&w, 8); bw_u8(&w, 0x07);
    ses_fix_state_t st; ses_fix_state_init(&st);
    gps_fix_t f;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&st, SES_T_FIX_KEY, p, 39, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&st, SES_T_FIX_DELTA, p, 15, &f));  /* no anchor set */
    bw_t w0; bw_init(&w0, p, sizeof p); bw_i64(&w0, 0);                /* R26: <= 0, so 0 too */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&st, SES_T_FIX_KEY, p, 39, &f));
}

static void test_utc_parts_clamps_a_negative_timestamp(void)
{
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(-1000000000000LL, &y, &mo, &d, &hh, &mm, &ss, &cs);
    TEST_ASSERT_EQUAL_INT(1970, y);
    TEST_ASSERT_EQUAL_UINT(1u, mo); TEST_ASSERT_EQUAL_UINT(1u, d);
    TEST_ASSERT_EQUAL_UINT(0u, hh); TEST_ASSERT_EQUAL_UINT(0u, mm);
    TEST_ASSERT_EQUAL_UINT(0u, ss); TEST_ASSERT_EQUAL_UINT(0u, cs);
}
```

`test/test_exp_nmea_json.c` — two cases (the file already has `drain`/`feed_frame` helpers):

```c
/* The bad FIX_KEY payload all three exporter cases feed (same bytes as above). */
static void bad_fix_payload(uint8_t *p)
{
    bw_t w; bw_init(&w, p, 39);
    bw_i64(&w, -1000000000000LL); bw_i32(&w, -338561234); bw_i32(&w, 185123456);
    bw_i32(&w, 45000); bw_i32(&w, 18500); bw_i32(&w, 9012000); bw_u32(&w, 100);
    bw_u16(&w, 10); bw_u16(&w, 120); bw_u8(&w, 3); bw_u8(&w, 8); bw_u8(&w, 0x07);
}

static void test_nmea_drops_a_record_with_a_bad_timestamp_and_counts_it(void)
{
    /* io:C1: this used to emit "$GPRMC,4294967283429496725..." with a VALID checksum. */
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    uint8_t p[39]; bad_fix_payload(p);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, SES_T_FIX_KEY, p, 39));   /* skipped, not an abort */
    TEST_ASSERT_EQUAL_UINT16(1, e.skipped);
    TEST_ASSERT_EQUAL_UINT(0, e.win_len);                           /* nothing emitted */
}

static void test_json_summary_reports_skipped_records(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[2048]; size_t at = 0;
    uint8_t lp[30]; memset(lp, 0, sizeof lp);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, SES_T_LAP, lp, 7));       /* wrong len -> decode fails */
    TEST_ASSERT_EQUAL_UINT16(1, e.skipped);
    int fin; while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"skipped\":1}"));
}
```

`test/test_exp_vbo.c` — one case (its `drain` takes a `cap`; follow the file's own signature):

```c
static void test_vbo_drops_a_record_with_a_bad_timestamp(void)
{
    /* io:C1: the old row came out 119 bytes with its "\r\n" cut off, merging two [data] lines. */
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_VBO, &m));
    uint8_t out[2048]; size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, exp_pull(&e, out, sizeof out, &n));    /* drain the header */
    uint8_t p[39]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, -1000000000000LL); bw_i32(&w, -338561234); bw_i32(&w, 185123456);
    bw_i32(&w, 45000); bw_i32(&w, 18500); bw_i32(&w, 9012000); bw_u32(&w, 100);
    bw_u16(&w, 10); bw_u16(&w, 120); bw_u8(&w, 3); bw_u8(&w, 8); bw_u8(&w, 0x07);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, SES_T_FIX_KEY, p, 39));
    TEST_ASSERT_EQUAL_UINT16(1, e.skipped);
    TEST_ASSERT_EQUAL_UINT(0, e.win_len);
}
```

- [ ] **Step 2: Run to verify they fail.** `cmake --build test/build -j8 && ctest --test-dir test/build -R 'test_ses_records|test_exp' --output-on-failure` → compile error on `e.skipped`; then `ses_decode_fix` returns 1, `exp_utc_parts` gives `y == 1969`/garbage, the NMEA feed returns −1 with a non-empty window.

- [ ] **Step 3: Implement.**

`core/exp.h` — after `run_gate_idx`, plus the prototype beside `exp_win_puts`:
```c
    uint16_t  skipped;                 /* io:C1: records dropped because they failed to decode */
...
void exp_skip_record(exp_t *e);        /* counts one dropped record (saturates at 0xFFFF) */
```

`exp.c`:
```c
void exp_skip_record(exp_t *e)
{
    CORE_ASSERT_VOID(e != NULL, EXP_ASSERT_CODE);
    if (e->skipped < 0xFFFFu) e->skipped++;
}
```
and `exp_utc_parts`'s body from `total_cs` onward:
```c
    /* io:C1: a negative wire value must not make sod negative -- hh/mm/ss/cs are unsigned casts. */
    int64_t total_cs = ((gps_us > 0 ? gps_us : 0) + 5000) / 10000;   /* round to centiseconds */
    int64_t secs = total_cs / 100;
    *cs = (unsigned)(total_cs % 100);
    int64_t days = secs / 86400; int64_t sod = secs % 86400;
    exp_civil_from_days(days, y, mo, d);
    *hh = (unsigned)(sod / 3600); *mm = (unsigned)((sod % 3600) / 60); *ss = (unsigned)(sod % 60);
    CORE_ASSERT_VOID(*hh < 24u && *mm < 60u && *ss < 60u && *cs < 100u, EXP_ASSERT_CODE);
    /* postcondition of the clamp: a real civil time, not a cast of a negative second-of-day */
```

`ses_records.c` `ses_decode_fix` — in the `SES_T_FIX_KEY` branch, between `uint8_t fl = br_u8(&r);` and the `out->valid` line:
```c
        if (out->gps_us <= 0) return -1;   /* R26/io:C1: a frame admitted by the §12.2 CRC resync
                                            * must not reach the exporters */
```
and in the `SES_T_FIX_DELTA` branch, after `p->gps_us += (int64_t)dt * 1000;` (before `*out = *p;`):
```c
        if (p->gps_us <= 0) return -1;     /* same rule on the reconstructed timestamp */
```

Decode failure → counted skip, at all four sites (`exp_nmea.c:42`, `exp_vbo.c:44`, `exp_json.c:78`, `exp_json.c:157`):
```c
    if (ses_decode_fix(&e->fix_st, type, p, len, &f) != 1) { exp_skip_record(e); return 0; }
    lap_result_t lap; if (ses_decode_lap(p, len, &lap) != 1) { exp_skip_record(e); return 0; }
    drag_result_t run; if (ses_decode_drag_run(p, len, &run) != 1) { exp_skip_record(e); return 0; }
```

Checked `snprintf`, drop-not-truncate. `exp_nmea.c` `put_sentence` (replace the bare `snprintf`):
```c
    char line[128];
    int n = snprintf(line, sizeof line, "$%s*%02X\r\n", body, x);
    if (n < 0 || (size_t)n >= sizeof line) return -1;   /* never checksum a truncated body */
    return exp_win_puts(e, line);
```
`exp_nmea_feed` — both bodies (format strings unchanged):
```c
    int bn = snprintf(body, sizeof body, "GPRMC,...");
    if (bn < 0 || (size_t)bn >= sizeof body) { exp_skip_record(e); return 0; }
    if (put_sentence(e, body) < 0) return -1;
    bn = snprintf(body, sizeof body, "GPGGA,...");
    if (bn < 0 || (size_t)bn >= sizeof body) { exp_skip_record(e); return 0; }
    return put_sentence(e, body);
```
`exp_vbo_feed`'s row (format unchanged) — a row whose `\r\n` was cut merges two `[data]` lines:
```c
    int ln = snprintf(line, sizeof line, "%03u %02u%02u%02u.%02u ...");
    if (ln < 0 || (size_t)ln >= sizeof line) { exp_skip_record(e); return 0; }
    return exp_win_puts(e, line);
```
`exp_vbo_open` — the same `int ln = snprintf(...); if (ln < 0 || (size_t)ln >= sizeof line) return -1;` pattern for its three formatted writes (`exp_vbo.c:18`, `:21`, `:24`).

`exp_json_finish` — replace the final `exp_win_puts(e, "]}")`:
```c
    char tail[32];
    int tn = snprintf(tail, sizeof tail, "],\"skipped\":%u}", (unsigned)e->skipped);
    CORE_ASSERT_RET(tn > 0 && (size_t)tn < sizeof tail, EXP_ASSERT_CODE, -1);  /* §14.3 tail fits */
    CORE_ASSERT_RET(exp_win_puts(e, tail) == 0, 0x0A01, -1);
    return 0;
```

- [ ] **Step 4: Run to verify they pass.** Target suites green, then full `ctest` green.

- [ ] **Step 5: Commit.**
```bash
git add components/core/session/ses_records.c components/core/export components/core/include/core/exp.h test/test_ses_records.c test/test_exp_nmea_json.c test/test_exp_vbo.c
git commit   # fix(core/export): reject gps_us <= 0, clamp UTC, drop-and-count undecodable records (io:C1, R26)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 1 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 2: `json_stage` after a successful emit; worst-case header buffer (io:C2, R26)

**Files:** Modify `components/core/export/exp_json.c:19-40,67,190`; Test `test/test_exp_nmea_json.c`

**Interfaces:**
- Consumes: `exp_skip_record` (Task 1), `jw_overflow`, `exp_win_free`.
- Produces: `EXP_JSON_HDR_BUF` (512) in `exp_json.c`; `open_hdr` sets `json_stage = 1` only after `emit()` returns 0, so a failed open leaves stage 0 and `exp_json_finish:191`'s retry really re-opens the document.

- [ ] **Step 1: Failing test** — `test/test_exp_nmea_json.c` (inputs from `repro3.c`):

```c
static void test_json_header_with_control_chars_still_emits_every_lap(void)
{
    /* io:C2: session_id (10) + fw (16) + venue (32) all control chars expand 1 -> 6 () in
     * jw_str, so the header is 113 + 6*58 + 34 = 495 bytes -- over the old buf[400]. The old code
     * flipped json_stage BEFORE emit(), so the whole LAP array was dropped and exp_finish returned
     * 0 (SUCCESS) on a 12-byte fragment: [],"runs":[]}]. */
    exp_meta_t m; memset(&m, 0, sizeof m); memset(m.session_id, 0x01, 10);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memset(h.session_id, 0x01, 10); memset(h.fw, 0x01, 16); memset(h.hwid, 'h', 24);
    h.venue_id = 65535; h.layout_id = 65535; h.gps_hz = 25; h.fused_hz = 25;
    h.start_gps_us = 1789640100000000LL;
    uint8_t fr[256]; int n = ses_encode_hdr(&h, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    char nm[33]; memset(nm, 0x01, 32); nm[32] = '\0';          /* 32 control bytes (Task 4) */
    n = ses_encode_venue(65535, 65535, nm, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    lap_result_t lap; memset(&lap, 0, sizeof lap);
    lap.lap_no = 1; lap.time_ms = 112341; lap.flags = LAP_F_VALID;
    lap.n_sectors = 1; lap.sector_ms[0] = 50000;
    n = ses_encode_lap(&lap, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    int fin; while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);                          /* a whole, parseable document */
    int laps = json_obj_get(out, toks, cnt, 0, "laps");
    TEST_ASSERT_GREATER_THAN(0, laps);
    TEST_ASSERT_EQUAL_INT(1, toks[laps].size);                 /* the LAP was NOT dropped */
    TEST_ASSERT_EQUAL_UINT16(0, e.skipped);
}
```
(Run Task 4 first, or build the 32-byte VENUE payload by hand with `bw_bytes` as `repro3.c` does.)

- [ ] **Step 2: Run to verify it fails.** `ctest --test-dir test/build -R test_exp_nmea_json --output-on-failure` → FAIL: `json_parse` on the 12-byte fragment returns −1.

- [ ] **Step 3: Implement** in `exp_json.c`:

```c
/* io:C2 worst case for open_hdr's buffer, proven rather than guessed. Skeleton = 113 bytes (the
 * literal braces/keys/quotes of {"id":"","hdr":{...},"laps":[ with every value removed -- measured
 * with a jw probe). Escapes: jw_str expands a byte < 0x20 to \u00xx (1 -> 6) for all three
 * variable strings (session_id 10 + venue 32 + fw 16 = 58). Numbers: start_gps_us/1000000 is at
 * most 14 chars ("-9223372036854"), "drag" 4 with quotes, venue_id/layout_id 5 each, gps_hz and
 * fused_hz 3 each. Deviation from design R26, which said 480 B: 113 + 348 + 34 + 1 = 496 does not
 * fit 480, so this plan sizes the buffer 512 and asserts the arithmetic at compile time. */
#define EXP_JSON_HDR_SKELETON 113u
#define EXP_JSON_HDR_ESC_MAX  (6u * (10u + 32u + 16u))
#define EXP_JSON_HDR_NUM_MAX  (14u + 4u + 5u + 5u + 3u + 3u)
#define EXP_JSON_HDR_BUF      512u
_Static_assert(EXP_JSON_HDR_SKELETON + EXP_JSON_HDR_ESC_MAX + EXP_JSON_HDR_NUM_MAX + 1u
                   <= EXP_JSON_HDR_BUF,
               "open_hdr's buffer must hold the worst-case escaped header plus its NUL");
```
In `open_hdr`: `char buf[EXP_JSON_HDR_BUF];`, and the stage flip moves below the emit:
```c
    jw_key(&w, "laps"); jw_arr_open(&w);
    /* leave the array open: strip the closing pieces by not calling jw_arr_close/jw_obj_close */
    int r = emit(e, &w);
    if (r != 0) return r;          /* io:C2: a failed emit must leave stage 0, so exp_json_finish
                                    * (and open_hdr's own stage-0 assertion) can retry the open */
    e->json_stage = 1;
    return 0;
```
Both header reserves become the buffer size (the window is `EXP_WINDOW` 1024, so 512 still leaves room for the tail): `feed_stage0:67` → `if (exp_win_free(e) < (int)EXP_JSON_HDR_BUF) { *ret = EXP_FULL; return 1; }`; `exp_json_finish:190` → `if (exp_win_free(e) < (int)EXP_JSON_HDR_BUF) return EXP_FULL;`. `feed_lap`'s own `buf[400]`/400 reserve is unchanged.

- [ ] **Step 4: Run to verify it passes.** Target suite green, then full `ctest`.

- [ ] **Step 5: Commit.**
```bash
git add components/core/export/exp_json.c test/test_exp_nmea_json.c
git commit   # fix(core/export): flip json_stage only after a successful emit; 512 B header buffer with a proven worst case (io:C2)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 2 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 3: NMEA window reserve 232; `exp_pull` with a small cap (io:M5, coverage gaps)

**Files:** Modify `components/core/export/exp_nmea.c:40`; Test `test/test_exp_nmea_json.c`

**Interfaces:** Consumes `exp_win_free`, `exp_win_puts`, `exp_t.win_len` (all public in `core/exp.h`). Produces `EXP_NMEA_RESERVE` (232) in `exp_nmea.c` — two sentences of at most 115 bytes each (`$` + `body[110]`'s 109 chars + `*` + 2 hex + `\r\n`) plus 2 bytes of slack.

- [ ] **Step 1: Failing tests** — `test/test_exp_nmea_json.c`:

```c
static int one_fix_frame(uint8_t *fr, size_t cap)         /* 101500.00, 66.66 kn, 8 sats */
{
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = 1789380900LL * 1000000LL; f.lat_e7 = -338567000; f.lon_e7 = 185170000;
    f.alt_mm = 45000; f.gspeed_mms = 34292; f.head_e5 = 9012000;
    f.pdop_e2 = 120; f.fix_type = 3; f.sats = 8;
    f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    return ses_encode_fix(&fs, &f, fr, cap);
}

/* exp_win_free ignores win_pos, so padding is the only way to hit the boundary deterministically. */
static void pad_window_to_free(exp_t *e, int want)
{
    char pad[EXP_WINDOW];
    size_t n = (size_t)(EXP_WINDOW - want);
    memset(pad, 'x', n); pad[n] = '\0';
    TEST_ASSERT_EQUAL_INT(0, exp_win_puts(e, pad));
    TEST_ASSERT_EQUAL_INT(want, exp_win_free(e));
}

static void test_nmea_reserve_bounds_both_sentences(void)
{
    /* io:M5: the old 200-byte reserve let the first sentence in and then failed the second's
     * exp_win_puts, returning -1 with one orphan sentence already in the window. */
    exp_meta_t m; memset(&m, 0, sizeof m);
    uint8_t fr[64]; int n = one_fix_frame(fr, sizeof fr);
    uint8_t len = (uint8_t)(n - SES_FRAME_OVERHEAD);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    pad_window_to_free(&e, 231);                              /* one below the reserve */
    size_t before = e.win_len;
    TEST_ASSERT_EQUAL_INT(EXP_FULL, exp_feed(&e, fr[1], fr + 3, len));
    TEST_ASSERT_EQUAL_UINT(before, e.win_len);                /* nothing written, no orphan */

    exp_t e2; TEST_ASSERT_EQUAL_INT(0, exp_open(&e2, EXP_NMEA, &m));
    pad_window_to_free(&e2, 232);                             /* exactly the reserve */
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e2, fr[1], fr + 3, len));
    char out[EXP_WINDOW + 1]; size_t at = drain(&e2, out, 0);
    TEST_ASSERT_GREATER_THAN(0, at);
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPRMC,101500.00"));
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPGGA,101500.00"));    /* both sentences made it */
}

static void test_pull_with_a_cap_smaller_than_the_window_streams_then_resets(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    uint8_t fr[64]; int n = one_fix_frame(fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)));
    size_t len = e.win_len, total = 0, got = 0;
    uint8_t buf[16];
    TEST_ASSERT_GREATER_THAN(sizeof buf, len);
    while (total < len) {
        TEST_ASSERT_EQUAL_INT(0, exp_pull(&e, buf, sizeof buf, &got));
        TEST_ASSERT_TRUE(got > 0 && got <= sizeof buf);
        total += got;
    }
    TEST_ASSERT_EQUAL_UINT(len, total);
    TEST_ASSERT_EQUAL_UINT(0, e.win_len);                     /* a full drain resets the window */
    TEST_ASSERT_EQUAL_INT(0, exp_pull(&e, buf, sizeof buf, &got));
    TEST_ASSERT_EQUAL_UINT(0, got);
}
```

- [ ] **Step 2: Run to verify.** `ctest -R test_exp_nmea_json --output-on-failure` → the reserve case FAILs at 231 free (`exp_feed` returns −1, `win_len` grew); the `exp_pull` case PASSES already (missing coverage, not a bug) — record that in the report.

- [ ] **Step 3: Implement** — `exp_nmea.c`:
```c
/* io:M5: put_sentence writes at most 115 bytes ('$' + body[110]'s 109 chars + '*' + 2 hex +
 * "\r\n") and exp_nmea_feed writes TWO sentences per fix, so the reserve must be 230 -- the old
 * 200 let the first through and failed the second. 232 keeps 2 bytes of slack. */
#define EXP_NMEA_RESERVE 232
...
    if (exp_win_free(e) < EXP_NMEA_RESERVE) return EXP_FULL;
```
Keep the reserve check **before** `ses_decode_fix`, so `fix_st` is untouched on `EXP_FULL` and the caller's re-feed of the same frame stays correct.

- [ ] **Step 4: Run to verify it passes.** Target suite green, then full `ctest`.

- [ ] **Step 5: Commit.**
```bash
git add components/core/export/exp_nmea.c test/test_exp_nmea_json.c
git commit   # fix(core/export): NMEA window reserve covers both sentences (232 B); exp_pull small-cap coverage (io:M5)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 3 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 4: full 32-byte wire venue name (io:M9, false claim 9)

**Files:** Modify `components/core/session/ses_records.c:338-345`; Test `test/test_ses_records.c`, `test/test_exp_nmea_json.c:125,139`

**Interfaces:** `ses_encode_venue(uint16_t venue_id, uint16_t layout_id, const char *name, uint8_t *out, size_t cap)` — signature unchanged; it now copies up to **32** bytes of `name` (§12.3: the `char[32]` wire field is exactly 32 bytes, zero-padded, *not* required to be NUL-terminated), matching `ses_encode_hdr`'s `bw_bytes(…, 16)`/`(…, 24)`.

- [ ] **Step 1: Failing test** — `test/test_ses_records.c`:
```c
static void test_encode_venue_carries_all_32_wire_bytes(void)
{
    /* io:M9: strncpy(nm, name, sizeof nm - 1) capped the §12.3 32-byte field at 31, while
     * ses_encode_hdr in the same file uses bw_bytes for the full width of fw/hwid. */
    char full[33]; memset(full, 'X', 32); full[32] = '\0';
    uint8_t out[64];
    int n = ses_encode_venue(7, 1, full, out, sizeof out);
    TEST_ASSERT_EQUAL_INT(41, n);                               /* 36 payload + 5 framing */
    ses_venue_t v;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(out + 3, (uint8_t)(n - SES_FRAME_OVERHEAD), &v));
    TEST_ASSERT_EQUAL_UINT(32u, strlen(v.name));
    TEST_ASSERT_EQUAL_MEMORY(full, v.name, 32);
    n = ses_encode_venue(7, 1, "Killarney", out, sizeof out);   /* short name still zero-pads */
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(out + 3, (uint8_t)(n - SES_FRAME_OVERHEAD), &v));
    TEST_ASSERT_EQUAL_STRING("Killarney", v.name);
}
```
And correct the mis-named existing case (false claim 9: it is called `..._using_every_wire_byte_are_emitted_whole` yet fed "31 chars + NUL"): `test_exp_nmea_json.c:125` → `ses_encode_venue(6, 1, "01234567890123456789012345678901", fr, sizeof fr);   /* 32 chars */` and `:139`'s `json_tok_eq` expectation to the same 32-char string.

- [ ] **Step 2: Run to verify it fails.** `ctest -R 'test_ses_records|test_exp_nmea_json'` → `strlen(v.name)` is 31; the JSON case's `json_tok_eq` fails.

- [ ] **Step 3: Implement** — `ses_records.c`:
```c
    uint8_t p[36]; bw_t w; bw_init(&w, p, sizeof p);
    /* io:M9: §12.3's name field is exactly 32 bytes, zero-padded and NOT required to be
     * NUL-terminated -- so copy up to 32, not strncpy's 31. The length scan is bounded by the
     * destination (rule 2); strnlen is avoided (unused elsewhere in the tree). */
    char nm[32]; memset(nm, 0, sizeof nm);
    size_t ln = 0;
    while (ln < sizeof nm && name[ln] != '\0') ln++;
    memcpy(nm, name, ln);
    bw_u16(&w, venue_id); bw_u16(&w, layout_id); bw_bytes(&w, nm, sizeof nm);
    return finish(SES_T_VENUE, &w, out, cap);
```

- [ ] **Step 4: Run to verify it passes.** Both suites green, then full `ctest`.

- [ ] **Step 5: Commit.**
```bash
git add components/core/session/ses_records.c test/test_ses_records.c test/test_exp_nmea_json.c
git commit   # fix(core/session): ses_encode_venue carries all 32 wire bytes of the §12.3 name (io:M9)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 4 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

- [ ] **SESSION 1 CHECKPOINT** — re-run `ctest --test-dir test/build --output-on-failure` (100 %) and `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` (0); append the two result lines to the ledger under "Session 1 end".

---

## Session 2 — Tasks 5–8 (lane A tail + lane B: config + util)

### Task 5: the two false comments in lane A (reviewer false claims 1 and 2)

**Files:** Modify `components/core/export/exp.c:18-20`, `components/core/session/ses_records.c:91`. No behaviour change, so no new test — the suite must stay green.

- [ ] **Step 1: Replace `exp.c:18-20`.** Old: `/* Buffer/window capacity before a write: every caller pre-checks exp_win_free() against its own worst-case write size before calling this, so this should never trip in practice. */` New:
```c
    /* Buffer/window capacity before a write. Most callers pre-check exp_win_free() against their
     * own worst-case write size (exp_nmea_feed's EXP_NMEA_RESERVE, exp_json's EXP_JSON_HDR_BUF),
     * but NOT all: exp_vbo_open makes five exp_win_puts calls with no exp_win_free check at all --
     * it relies on being called on an empty window, which exp_open guarantees (it memsets *e
     * before dispatching to the per-format open). So this is a real last-resort bound, not a
     * restatement of a universal caller contract (io false claim 1). */
```

- [ ] **Step 2: Replace `ses_records.c:91`.** Old: `/* advance the reconstructed previous exactly as the decoder will */` New:
```c
    /* Advance the reconstructed previous for exactly the fields the delta carries -- the fields the
     * delta math anchors on. ses_decode_fix additionally sets prev.sacc_mms/pdop_e2 to 0,
     * prev.fix_type from the flag byte and prev.flags to a synthesised word; none of those feeds
     * any delta, so the two sides agree on everything that matters, but this is deliberately NOT
     * "exactly as the decoder will" (io false claim 2). */
```

- [ ] **Step 3: Run the suite.** Full `ctest` green.

- [ ] **Step 4: Commit.**
```bash
git add components/core/export/exp.c components/core/session/ses_records.c
git commit   # docs(core): correct the exp_win_puts caller-contract and FIX_DELTA prev-advance comments (io false claims 1-2)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 5 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 6: JSON token types, truncation and range (io:M3, io:L2, false claim 7)

**Files:** Modify `components/core/util/json.c:61-116`, `components/core/include/core/json.h:18-20`; Test `test/test_cfg.c`, `test/test_drag_cfg.c`

**Interfaces:** `json_tok_bool` → `false` unless `t->type == JSMN_PRIMITIVE` **and** the token is `true`/`false`. `json_tok_int`/`json_tok_double` → `false` when the token did not fit the scratch buffer, when `strtoll`/`strtod` set `ERANGE`, or (doubles) when the result is not finite. `tok_copy` now returns the token's **source** length (not the copied length) so a caller can detect truncation; `json_tok_str` keeps its copied-length contract by clamping itself.

- [ ] **Step 1: Failing tests** — `test/test_cfg.c` (inputs from `repro2.c`):
```c
static void test_bool_fields_reject_a_wrong_json_type(void)
{
    /* io:M3: json_tok_bool indexed js[t->start] without checking the type, so any quoted value
     * starting with 't'/'f' set the flag -- and drag.rollout changes reported drag ET by 1 ft. */
    cfg_t c; char err[64];
    const char *j1 = "{\"drag\":{\"rollout\":\"take a hike\"}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j1, strlen(j1), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for drag.rollout", err);
    TEST_ASSERT_FALSE(c.drag.rollout);
    const char *j2 = "{\"display\":{\"invert\":\"frobnicate\"}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j2, strlen(j2), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for display.invert", err);
    const char *j3 = "{\"display\":{\"live_clock\":1}}";          /* 1 is not a bool */
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j3, strlen(j3), err, sizeof err));
    const char *ok = "{\"drag\":{\"rollout\":true},\"display\":{\"invert\":false}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, ok, strlen(ok), err, sizeof err));
    TEST_ASSERT_TRUE(c.drag.rollout);
    TEST_ASSERT_FALSE(c.display.invert);
}

static void test_numeric_tokens_too_long_or_out_of_range_are_rejected(void)
{
    /* io:L2: tok_copy's return was discarded (a 40-digit token was silently truncated to 31
     * chars) and strtoll/strtod errno was never checked (1e999 saturated to inf and passed). */
    cfg_t c; char err[64];
    const char *big = "{\"lap\":{\"min_lap_s\":1111111111111111111111111111111111111111}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, big, strlen(big), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);
    jsmntok_t toks[8];
    const char *inf = "1e999";
    TEST_ASSERT_EQUAL_INT(1, json_parse(inf, strlen(inf), toks, 8));
    double d = 0.0;
    TEST_ASSERT_FALSE(json_tok_double(inf, &toks[0], &d));      /* ERANGE -> not a value */
}
```
`test/test_drag_cfg.c` — one case asserting a `{"drag":{"rollout":"yes"}}` merge is refused and `drag_cfg_engine_differs` therefore reports no change.

- [ ] **Step 2: Run to verify they fail.** `ctest -R 'test_cfg|test_drag_cfg' --output-on-failure` → `rollout = 1` with rc 0 and an empty `err`; `json_tok_double("1e999")` returns `true` with `inf`.

- [ ] **Step 3: Implement** — `json.c` (add `#include <errno.h>` and `#include <math.h>`):
```c
/* Returns the token's SOURCE length (not the copied length), so a caller can tell "this token did
 * not fit my scratch buffer" from "this token is 31 chars" (io:L2). */
static size_t tok_copy(const char *js, const jsmntok_t *t, char *tmp, size_t cap)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(tmp != NULL || cap == 0, JSON_ASSERT_CODE, 0);
    size_t src = (size_t)(t->end - t->start);
    if (cap == 0) return src;                 /* a zero-capacity destination is a normal call */
    size_t len = src >= cap ? cap - 1 : src;
    memcpy(tmp, js + t->start, len); tmp[len] = '\0';
    return src;
}

/* Two-line raw comparison: json_tok_eq additionally requires JSMN_STRING, which a primitive
 * literal is not. */
static bool json_tok_eq_raw(const char *js, const jsmntok_t *t, const char *s)
{
    size_t len = (size_t)(t->end - t->start);
    return strlen(s) == len && memcmp(js + t->start, s, len) == 0;
}
```
`json_tok_int` / `json_tok_double` — after the existing `JSMN_PRIMITIVE` test:
```c
    char tmp[32];                                        /* 48 for json_tok_double */
    if (tok_copy(js, t, tmp, sizeof tmp) >= sizeof tmp) return false;   /* io:L2: no truncation */
    errno = 0;
    char *end; long long v = strtoll(tmp, &end, 10);
    if (*end != '\0' || errno == ERANGE) return false;
    *out = v; return true;
```
```c
    errno = 0;
    char *end; double v = strtod(tmp, &end);
    if (*end != '\0' || errno == ERANGE || !isfinite(v)) return false;  /* JSON has no inf/NaN */
    *out = v; return true;
```
`json_tok_bool`, after the three NULL assertions:
```c
    /* io:M3: a JSON string must not set a bool. §15.1 types drag.rollout, display.live_clock and
     * display.invert as bool, so a quoted or numeric value is a type error, not a truthy value. */
    if (t->type != JSMN_PRIMITIVE) return false;
    if (json_tok_eq_raw(js, t, "true"))  { *out = true;  return true; }
    if (json_tok_eq_raw(js, t, "false")) { *out = false; return true; }
    return false;
```
`json_tok_str` — keep its copied-length contract against the new `tok_copy`:
```c
    size_t src = tok_copy(js, t, out, cap);
    size_t r = (cap == 0 || src < cap) ? src : cap - 1;
    CORE_ASSERT_RET(cap == 0 || r < cap, JSON_ASSERT_CODE, r); /* postcondition: room for the NUL */
    return r;
```
`json.h:18-20` (false claim 7 — the iterativeness claim is true, its justification is not):
```c
/* Iterative (no recursion): the subtree of token i is the run of following tokens that start
 * before toks[i].end. jsmn emits tokens in document order, which is what makes that run
 * contiguous -- JSMN_PARENT_LINKS is unrelated (json_parse uses the parent links only for its
 * depth walk). Depth is capped by json_parse, so neither helper can be driven to unbounded
 * stack use. */
```

- [ ] **Step 4: Run to verify they pass.** `ctest -R 'test_cfg|test_drag_cfg|test_trk'` green (`trk_from_json`'s doubles use the same helpers), then full `ctest`.

- [ ] **Step 5: Commit.**
```bash
git add components/core/util/json.c components/core/include/core/json.h test/test_cfg.c test/test_drag_cfg.c
git commit   # fix(core/util): json_tok_bool requires a primitive; reject truncated/out-of-range numeric tokens (io:M3, io:L2)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 6 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 7: `walk()` becomes a two-level loop; split the benches branch (io:H2/R27, io:L3)

**Files:** Modify `components/core/config/cfg_json.c:68-73,107-132,148`; Test `test/test_cfg.c`

**Interfaces:** `static int walk(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, char *err, size_t err_cap)` — no `obj`/`prefix`, no self-call. New `static bool is_section(const char *key)` and `static int apply_benches(cfg_t *c, const char *js, const jsmntok_t *toks, int v, bool kmh)`. Behaviour must not change: §15.1's merge-into-a-copy semantics, unknown keys ignored, a non-object section → `"<key> must be an object"`, a bad value → `"bad value for <path>"`.

- [ ] **Step 1: Equivalence test** — `test/test_cfg.c`. The recursion is not observable, so pin the full shape matrix the loop must still handle:
```c
static void test_walk_handles_every_nesting_shape_sections_can_take(void)
{
    /* io:H2 (R27): walk() was directly recursive (rule 1) with no deviation row. §15.1's grammar
     * is exactly two levels -- top-level scalars, section objects of scalars/arrays -- so a
     * two-level loop is equivalent. These are the shapes it must still handle identically. */
    cfg_t c; char err[80];
    const char *all =
        "{\"units\":\"mph\",\"mode\":\"drag\",\"dist_units\":\"ft\","
        "\"lap\":{\"min_lap_s\":30,\"default_layout\":[{\"venue\":6,\"layout\":2}]},"
        "\"drag\":{\"benches_kmh\":[80,160],\"benches_mph\":[50,100],\"rollout\":true},"
        "\"battery\":{\"cal\":[{\"adc_mv\":3100,\"true_mv\":3000},{\"adc_mv\":4200,\"true_mv\":4100}]},"
        "\"ble\":{\"name\":\"Rig\"},\"log\":{\"fused_hz\":25},\"nonsense\":7,"
        "\"imu\":{\"mot_thr\":9,\"unknown_key\":3}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, all, strlen(all), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_MPH, c.units);
    TEST_ASSERT_EQUAL_UINT8(CFG_MODE_DRAG, c.mode);
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_FT, c.dist_units);
    TEST_ASSERT_EQUAL_UINT16(30, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT8(1, c.lap.n_default_layout);
    TEST_ASSERT_EQUAL_UINT16(6, c.lap.default_layout[0].venue);
    TEST_ASSERT_EQUAL_UINT16(2, c.lap.default_layout[0].layout);
    TEST_ASSERT_EQUAL_UINT8(2, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(160, c.drag.benches_kmh[1]);
    TEST_ASSERT_EQUAL_UINT8(2, c.drag.n_mph);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_mph[1]);
    TEST_ASSERT_TRUE(c.drag.rollout);
    TEST_ASSERT_EQUAL_UINT16(3100, c.battery.adc_mv[0]);
    TEST_ASSERT_EQUAL_UINT16(4100, c.battery.true_mv[1]);
    TEST_ASSERT_EQUAL_STRING("Rig", c.ble.name);
    TEST_ASSERT_EQUAL_UINT8(25, c.log.fused_hz);
    TEST_ASSERT_EQUAL_UINT8(9, c.imu.mot_thr);

    const char *not_obj = "{\"lap\":5}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, not_obj, strlen(not_obj), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("lap must be an object", err);
    const char *bad = "{\"gps\":{\"rate_hz\":\"fast\"}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad, strlen(bad), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for gps.rate_hz", err);
    const char *nested = "{\"lap\":{\"min_lap_s\":{\"deep\":1}}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, nested, strlen(nested), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for lap.min_lap_s", err);
}
```

- [ ] **Step 2: Run it against the recursive version.** `ctest -R test_cfg --output-on-failure` → it **PASSES**. That is the point: it is the equivalence harness for Step 3. Record the pre-rewrite pass in the task report.

- [ ] **Step 3: Implement** — `cfg_json.c`. Benches split (io:L3 — `path[13] == 'k'` re-derives what `strcmp` already decided):
```c
static int apply_benches(cfg_t *c, const char *js, const jsmntok_t *toks, int v, bool kmh)
{
    CORE_ASSERT_RET(c != NULL && js != NULL && toks != NULL, CFG_ASSERT_CODE, -1);
    const jsmntok_t *t = &toks[v];
    if (t->type != JSMN_ARRAY) return -1;
    if (t->size > CFG_MAX_BENCHES) return -1;
    uint16_t *dst = kmh ? c->drag.benches_kmh : c->drag.benches_mph;
    uint8_t n = 0;
    for (int k = 0; k < t->size; k++) { if (!get_u16(js, &toks[v + 1 + k], &dst[n])) return -1; n++; }
    if (kmh) c->drag.n_kmh = n; else c->drag.n_mph = n;
    return 0;
}
```
and in `apply`, replacing the combined branch:
```c
    if (!strcmp(path, "drag.benches_kmh")) return apply_benches(c, js, toks, v, true);
    if (!strcmp(path, "drag.benches_mph")) return apply_benches(c, js, toks, v, false);
```
Iterative `walk` (R27):
```c
static bool is_section(const char *key)
{
    CORE_ASSERT_RET(key != NULL, CFG_ASSERT_CODE, false);
    for (size_t s = 0; s < sizeof SECTIONS / sizeof SECTIONS[0]; s++)
        if (!strcmp(key, SECTIONS[s])) return true;
    return false;
}

/* R27/io:H2: two nested loops, not recursion. §15.1's grammar is exactly two levels -- top-level
 * scalars and section objects of scalars/arrays -- which is why the recursive version could only
 * ever reach depth 2 (is_section was tested only when prefix[0] == 0). The outer loop walks the
 * root object's members, the inner one a section's; both are bounded by the token count. */
static int walk(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, char *err, size_t err_cap)
{
    CORE_ASSERT_RET(c != NULL, CFG_ASSERT_CODE, -1);
    CORE_ASSERT_RET(js != NULL, CFG_ASSERT_CODE, -1);
    CORE_ASSERT_RET(toks != NULL, CFG_ASSERT_CODE, -1);   /* err is optional: set_err checks it */
    CORE_ASSERT_RET(ntoks > 0, CFG_ASSERT_CODE, -1);
    int i = 1;                                            /* first key of the root object */
    for (int k = 0; k < toks[0].size && i + 1 < ntoks; k++) {
        char key[32];
        json_tok_str(js, &toks[i], key, sizeof key);
        int v = i + 1;
        if (is_section(key)) {
            if (toks[v].type != JSMN_OBJECT) {
                char msg[64]; snprintf(msg, sizeof msg, "%s must be an object", key);
                return set_err(err, err_cap, msg);
            }
            int j = v + 1;                                /* first key of the section object */
            for (int s = 0; s < toks[v].size && j + 1 < ntoks; s++) {
                char sub[32], path[64];
                json_tok_str(js, &toks[j], sub, sizeof sub);
                snprintf(path, sizeof path, "%s.%s", key, sub);
                if (apply(c, js, toks, ntoks, path, j + 1) < 0) {
                    char msg[96]; snprintf(msg, sizeof msg, "bad value for %s", path);
                    return set_err(err, err_cap, msg);
                }
                j = json_skip(toks, ntoks, j + 1);        /* j is the key; step past its value */
            }
        } else if (apply(c, js, toks, ntoks, key, v) < 0) {
            char msg[96]; snprintf(msg, sizeof msg, "bad value for %s", key);
            return set_err(err, err_cap, msg);
        }
        i = json_skip(toks, ntoks, i + 1);
    }
    return 0;
}
```
Call site: `if (walk(&tmp, json, toks, cnt, err, err_cap) < 0) return -1;`

- [ ] **Step 4: Run to verify it still passes.** `ctest -R 'test_cfg|test_drag_cfg'` green; full `ctest` green; lint 0 (no rule-1 check exists today, so the linter is the rule-4/5 guard on the two new functions).

- [ ] **Step 5: Commit.**
```bash
git add components/core/config/cfg_json.c test/test_cfg.c
git commit   # refactor(core/config): walk() becomes a two-level loop (Power of 10 rule 1); split the benches branch (io:H2, io:L3)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 7 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 8: `cfg_apply_profile` validation, unclamped bench tail, the no-blob boot save (io:M6, io:L5, R26)

**Files:** Modify `components/core/config/cfg.c:42-91,121-131`, `components/core/include/core/cfg.h:76`, `main/app_main.c:150-165` (cross-boundary edit 1); Test `test/test_cfg.c`

**Interfaces:** `cfg_apply_profile(cfg_t *c, const cfg_profile_t *p)` → 0 ok, −1 on a BLE name over 15 chars **or** a `log_fused_hz` outside §15.1's `{5,10,25}`; on −1 `*c` is untouched. `cfg_validate` additionally **zeroes** `benches_kmh[n_kmh..CFG_MAX_BENCHES)` / `benches_mph[n_mph..)` without counting a correction, so `cfg_validate(cfg_defaults(&c))` stays 0.

- [ ] **Step 1: Failing tests** — `test/test_cfg.c`:
```c
static void test_profile_rejects_a_log_rate_outside_the_spec_set(void)
{
    /* io:M6: cfg_apply_profile wrote p->log_fused_hz straight into c->log.fused_hz with no check
     * against §15.1's {5,10,25}; verified: 77 was accepted and stored, and the no-blob boot path
     * then persisted it. */
    cfg_t c; cfg_defaults(&c);
    cfg_t before = c;
    cfg_profile_t p = { true, 77, "x" };
    TEST_ASSERT_EQUAL_INT(-1, cfg_apply_profile(&c, &p));
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
    for (uint8_t hz = 0; hz < 30; hz++) {
        cfg_defaults(&c);
        cfg_profile_t q = { false, hz, "LapTimer" };
        int rc = cfg_apply_profile(&c, &q);
        if (hz == 5u || hz == 10u || hz == 25u) {
            TEST_ASSERT_EQUAL_INT(0, rc);
            TEST_ASSERT_EQUAL_UINT8(hz, c.log.fused_hz);
        } else {
            TEST_ASSERT_EQUAL_INT(-1, rc);
            TEST_ASSERT_EQUAL_UINT8(10, c.log.fused_hz);        /* the default is untouched */
        }
    }
}

static void test_validate_clears_the_unused_bench_tail_without_counting_it(void)
{
    /* io:L5: entries past n_kmh/n_mph were never touched, so a blob's stale tail survived a later
     * count increase. Zeroing (not clamping) keeps cfg_validate(defaults) == 0. */
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    c.drag.n_kmh = 2; c.drag.benches_kmh[2] = 999; c.drag.benches_kmh[3] = 7;
    c.drag.n_mph = 1; c.drag.benches_mph[3] = 1;
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));                 /* not counted as a correction */
    TEST_ASSERT_EQUAL_UINT16(0, c.drag.benches_kmh[2]);
    TEST_ASSERT_EQUAL_UINT16(0, c.drag.benches_kmh[3]);
    TEST_ASSERT_EQUAL_UINT16(0, c.drag.benches_mph[3]);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);       /* live entries untouched */
}
```

- [ ] **Step 2: Run to verify they fail.** `ctest -R test_cfg --output-on-failure` → `cfg_apply_profile` returns 0 with `fused_hz == 77`; `benches_kmh[2]` stays 999.

- [ ] **Step 3: Implement.** `cfg.c` `cfg_apply_profile`, after the name check:
```c
    /* io:M6: log_fused_hz is a §15.1 enum {5,10,25}, not a free integer -- cfg_validate would
     * clamp it later, but the no-blob boot path persists this config, so an out-of-range profile
     * would reach flash. Validated before anything is written: a rejected profile changes nothing. */
    if (p->log_fused_hz != 5u && p->log_fused_hz != 10u && p->log_fused_hz != 25u) return -1;
```
`cfg_validate`, right after the two bench clamp loops:
```c
    /* io:L5: entries past the count are neither displayed nor exported, so a stale tail from an
     * older blob is invisible until n_kmh/n_mph grows. Zero them -- deliberately NOT clamped into
     * [10,400]/[10,250], which would count a correction on every fresh default config and fire
     * E_SYS_CFG_RESET at every boot. Not counted in n. */
    for (int i = c->drag.n_kmh; i < CFG_MAX_BENCHES; i++) c->drag.benches_kmh[i] = 0;
    for (int i = c->drag.n_mph; i < CFG_MAX_BENCHES; i++) c->drag.benches_mph[i] = 0;
```
`cfg.h:76`:
```c
int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p);   /* 0 ok / -1 bad ble_name length (> 15)
                                                            * or log_fused_hz outside §15.1's
                                                            * {5,10,25}; on -1 *c is untouched */
```
`main/app_main.c` `boot_config` (cross-boundary edit 1):
```c
    int corr = lt_cfg_load(&cfg);
    if (corr < 0) {
        (void)cfg_validate(&cfg);              /* io:M6/R26: the only path that stores a config
                                                * cfg_validate never saw */
        lt_cfg_save(&cfg);                     /* no valid blob -> persist the validated defaults */
        errlog_add(E_SYS_CFG_RESET, 0);
```

- [ ] **Step 4: Run to verify they pass.** `ctest -R test_cfg` green, full `ctest` green, clean `moto_sim` build 0 warnings (the `app_main.c` edit compiles only there).

- [ ] **Step 5: Commit.**
```bash
git add components/core/config/cfg.c components/core/include/core/cfg.h main/app_main.c test/test_cfg.c
git commit   # fix(core/config): validate log_fused_hz in cfg_apply_profile, clear the bench tail, validate before the no-blob save (io:M6, io:L5)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 8 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

- [ ] **SESSION 2 CHECKPOINT** — re-run the full `ctest` (100 %) and the linter (0); append both result lines to the ledger under "Session 2 end".

---

## Session 3 — Tasks 9–12 (lane B tail + lane C: pure UI)

### Task 9: padding assertions in `jw.c` and `json.c` (io:M8, R20)

**Files:** Modify `components/core/util/jw.c:45-68`, `components/core/util/json.c:54-58`; Test `test/test_jw.c`

**Interfaces:** `jw_obj_open`/`jw_arr_open` unchanged externally; past `JW_MAX_DEPTH` they set `overflow` and do not nest (as today). `json_skip` unchanged.

| Site | Old (tautology) | New |
|---|---|---|
| `jw.c:50` | `CORE_ASSERT_VOID(w->depth < JW_MAX_DEPTH, …)` right after the `if (depth+1 < MAX) depth++; else overflow = true;` that guarantees it | the cap becomes the control flow (below); no assertion |
| `jw.c:59`, `:66` | `CORE_ASSERT_VOID(w->overflow \|\| w->depth > 0, …)` restating `open_`'s own effect | deleted (both functions are 3 code lines) |
| `json.c:56` | `CORE_ASSERT_RET(j >= i, …)` after `j = i + 1; j++` | deleted |
| `json.c:57` | `CORE_ASSERT_RET(j <= ntoks, …)` restating the `while` condition | deleted |

- [ ] **Step 1: Regression test** — `test/test_jw.c`:
```c
static void test_nesting_past_the_depth_cap_sets_overflow_and_writes_nothing_deeper(void)
{
    /* R20: the depth guard that used to be a tautological assertion after the fact is now the
     * control flow. Past JW_MAX_DEPTH an open sets overflow and does not nest. */
    char buf[256]; jw_t w; jw_init(&w, buf, sizeof buf);
    for (int i = 0; i < JW_MAX_DEPTH + 4; i++) jw_arr_open(&w);
    TEST_ASSERT_TRUE(jw_overflow(&w));
    TEST_ASSERT_TRUE(w.depth < JW_MAX_DEPTH);        /* first[] was never indexed out of range */
}
```

- [ ] **Step 2: Run it.** `ctest -R test_jw --output-on-failure` → PASSES today (the behaviour is already correct); it is the guard for Step 3's restructure. Record that.

- [ ] **Step 3: Implement** — `jw.c`:
```c
static void open_(jw_t *w, char c)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    sep(w); putc_(w, c);
    /* R20: the nesting cap is the control flow, not an assertion restating the branch above it --
     * first[] is only indexed once the increment is known to be in range. */
    if (w->depth + 1u >= JW_MAX_DEPTH) { w->overflow = true; return; }
    w->depth++;
    w->first[w->depth] = true;
}

void jw_obj_open(jw_t *w) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); open_(w, '{'); }
void jw_arr_open(jw_t *w) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); open_(w, '['); }
```
(`sep`'s `depth < JW_MAX_DEPTH` assertion at `:39` stays — it guards a read of `first[depth]` that nothing in `sep` establishes.) `json_skip` loses lines 56–57, leaving `int j = i + 1; while (j < ntoks && toks[j].start < toks[i].end) j++; return j;`.

- [ ] **Step 4: Run to verify.** Full `ctest` green; lint 0 (confirms nothing dropped below 2 assertions while over 20 code lines).

- [ ] **Step 5: Commit.**
```bash
git add components/core/util/jw.c components/core/util/json.c test/test_jw.c
git commit   # refactor(core/util): replace the jw/json padding assertions with real control flow (io:M8, R20)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 9 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 10: four new 12×12 glyphs, the §17.4 bit→icon remap, dead-icon deletion (io:H1, io:L4, R18)

**Files:**
- Modify: `components/core/ui/include/core/ui/icons.h:12-31`, `ui/icons.c:1-137`, `ui/screens_moto.c:201-231`, `ui/include/core/ui/model.h` (declare the accessor), `docs/display-glyphs.md`
- Test: `test/test_screens.c`, `test/test_ui.c:97,184,211`
- Goldens (regenerate; both canvases unless noted): `lap_p0_fault.pbm`, `213/lap_p0_fault.pbm`, `drag_p0_fault.pbm`, `213/drag_p0_fault.pbm`, **new** `strip_imu_disk_lean.pbm` + `213/strip_imu_disk_lean.pbm`, and single-canvas (`test_ui`) `icons_and_bar.pbm`, `lap_screen.pbm`, `clip_edge.pbm`

**Interfaces:**
- Produces: `icon_id_t` = 14 glyphs (below); `int ui_fault_icon_for_bit(int bit);` declared in `core/ui/model.h` — the one lookup both `fault_strip` and `fault_strip_left_x` use, now directly testable.
- Deleted: `ICON_GPS` (filled pin), `ICON_BLE` (Bluetooth rune), `ICON_DISK` (healthy floppy). **Extension of R18, justified:** R18 names only `ICON_GPS`/`ICON_BLE`, because bit 5 then referenced `ICON_DISK`; remapping bit 5 to `ICON_DISK_STRIKE` leaves `ICON_DISK` referenced by nothing, so §1.1's "zero dead code per build" — R18's own argument — deletes it too. Verified before deleting: tree-wide `grep -rn 'ICON_GPS\b\|ICON_BLE\b\|ICON_DISK\b'` hits only `icons.c`, `icons.h`, `screens_moto.c`, `test/test_ui.c:97,184,211`.
- Bit→icon map (§17.4 Icon column): 0 `GPS_DEAD`→`ICON_GPS_STRIKE`, 1 `GPS_NOFIX`→`ICON_GPS_NOFIX` (new), 2 `IMU_DEAD`→`ICON_IMU_STRIKE` (new), 3 `IMU_SUSPECT`→`ICON_IMU_Q`, 4 `DISP_DEAD`→−1, 5 `STORAGE_DEAD`→`ICON_DISK_STRIKE` (new), 6 `STORAGE_FULL`→`ICON_DISK_FULL`, 7 `STORAGE_DEGRADED`→`ICON_DISK_WARN`, 8 `BATT_LOW`→`ICON_BATT_LOW`, 9 `SAFE_MODE`→`ICON_SAFE`, 10 `HEAP_LOW`→−1, 11 `DISP_TEMP_THROTTLE`→`ICON_THERMOMETER`, 12 `OTA_PENDING`→−1, 13 `FUSION_DISAGREE`→`ICON_LEAN_Q` (new), 14/15/16 → `ICON_SIM`/`ICON_MOVING`/`ICON_LINK`.

- [ ] **Step 1: Failing tests** — `test/test_screens.c`, both registered in `main`:
```c
/* io:H1: §17.4's Icon column, asserted directly rather than inferred from a golden's geometry.
 * Before this fix bit 1 drew the same strike as bit 0 (a dead GPS and a searching GPS were
 * indistinguishable), bits 2 and 13 drew nothing, and bit 5 drew the HEALTHY disk. */
static void test_fault_icon_table_matches_spec_17_4(void)
{
    TEST_ASSERT_EQUAL_INT((int)ICON_GPS_STRIKE,  ui_fault_icon_for_bit(SCR_SYS_GPS_DEAD));
    TEST_ASSERT_EQUAL_INT((int)ICON_GPS_NOFIX,   ui_fault_icon_for_bit(SCR_SYS_GPS_NOFIX));
    TEST_ASSERT_EQUAL_INT((int)ICON_IMU_STRIKE,  ui_fault_icon_for_bit(SCR_SYS_IMU_DEAD));
    TEST_ASSERT_EQUAL_INT((int)ICON_IMU_Q,       ui_fault_icon_for_bit(SCR_SYS_IMU_SUSPECT));
    TEST_ASSERT_EQUAL_INT(-1,                    ui_fault_icon_for_bit(SCR_SYS_DISP_DEAD));
    TEST_ASSERT_EQUAL_INT((int)ICON_DISK_STRIKE, ui_fault_icon_for_bit(SCR_SYS_STORAGE_DEAD));
    TEST_ASSERT_EQUAL_INT((int)ICON_DISK_FULL,   ui_fault_icon_for_bit(SCR_SYS_STORAGE_FULL));
    TEST_ASSERT_EQUAL_INT((int)ICON_DISK_WARN,   ui_fault_icon_for_bit(SCR_SYS_STORAGE_DEGRADED));
    TEST_ASSERT_EQUAL_INT((int)ICON_BATT_LOW,    ui_fault_icon_for_bit(SCR_SYS_BATT_LOW));
    TEST_ASSERT_EQUAL_INT((int)ICON_SAFE,        ui_fault_icon_for_bit(SCR_SYS_SAFE_MODE));
    TEST_ASSERT_EQUAL_INT(-1,                    ui_fault_icon_for_bit(SCR_SYS_HEAP_LOW));
    TEST_ASSERT_EQUAL_INT((int)ICON_THERMOMETER, ui_fault_icon_for_bit(SCR_SYS_DISP_TEMP_THROTTLE));
    TEST_ASSERT_EQUAL_INT(-1,                    ui_fault_icon_for_bit(SCR_SYS_OTA_PENDING));
    TEST_ASSERT_EQUAL_INT((int)ICON_LEAN_Q,      ui_fault_icon_for_bit(SCR_SYS_FUSION_DISAGREE));
    TEST_ASSERT_EQUAL_INT((int)ICON_SIM,         ui_fault_icon_for_bit(SCR_UI_SIM));
    TEST_ASSERT_EQUAL_INT((int)ICON_MOVING,      ui_fault_icon_for_bit(SCR_UI_MOVING));
    TEST_ASSERT_EQUAL_INT((int)ICON_LINK,        ui_fault_icon_for_bit(SCR_UI_LINK));
    TEST_ASSERT_EQUAL_INT(-1, ui_fault_icon_for_bit(-1));              /* out of range */
    TEST_ASSERT_EQUAL_INT(-1, ui_fault_icon_for_bit(SCR_STRIP_BITS));
    for (int a = 0; a < SCR_STRIP_BITS; a++)                           /* no shared glyphs */
        for (int b = a + 1; b < SCR_STRIP_BITS; b++) {
            int ia = ui_fault_icon_for_bit(a), ib = ui_fault_icon_for_bit(b);
            if (ia >= 0 && ib >= 0) TEST_ASSERT_NOT_EQUAL_INT(ia, ib);
        }
}

/* The three bits that used to draw nothing / the wrong glyph now each draw their own. */
static void test_strip_imu_disk_lean(void)
{
    screen_model_t m = {0};
    m.screen = SCR_RIDING; m.mode = SCR_MODE_LAP; m.page = 0;
    m.flags = (1u << SCR_SYS_IMU_DEAD) | (1u << SCR_SYS_STORAGE_DEAD) | (1u << SCR_SYS_FUSION_DISAGREE);
    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("strip_imu_disk_lean.pbm"), &s_fb));
    /* three slots, ascending bit order leftwards from the anchor: IMU_DEAD(2), STORAGE_DEAD(5),
     * FUSION_DISAGREE(13). Probe one ink pixel inside each (fault_strip_left_x is static). */
    const int pitch = ICON_W + 2;
    TEST_ASSERT_TRUE(px(&s_fb, FAULT_STRIP_X0 + 1, FAULT_STRIP_Y + 1));              /* IMU "?" bowl */
    TEST_ASSERT_TRUE(px(&s_fb, FAULT_STRIP_X0 - pitch + 1, FAULT_STRIP_Y + 1));      /* disk outline */
    TEST_ASSERT_TRUE(px(&s_fb, FAULT_STRIP_X0 - 2 * pitch + 5, FAULT_STRIP_Y + 11)); /* lean ground */
}
```
`test/test_ui.c`: retarget the deleted icons — `:97` and `:184` `ICON_GPS` → `ICON_GPS_NOFIX`, `:211` `ICON_BLE` → `ICON_LINK`.

- [ ] **Step 2: Run to verify they fail.** `cmake --build test/build -j8` → compile errors (`ICON_GPS_NOFIX`, `ui_fault_icon_for_bit` undeclared). With the header in place but before the remap, the table test fails on bits 1, 2, 5 and 13.

- [ ] **Step 3: Implement.** `icons.h` — the enum and the corrected byte-layout comment (false claim 6: `icons.h:30` said the **low** nibble pads, `icons.c:4-5` said the **top** nibble; verified — every row satisfies `byte[1] & 0x0F == 0`, so the low nibble is the padding):
```c
typedef enum {
    ICON_GPS_STRIKE = 0, /* SYS_GPS_DEAD: hollow GPS ring with a diagonal strike */
    ICON_GPS_NOFIX,      /* SYS_GPS_NOFIX: hollow GPS pin -- searching, not dead (§20.5) */
    ICON_IMU_STRIKE,     /* SYS_IMU_DEAD: the IMU "?" with a diagonal strike */
    ICON_IMU_Q,          /* SYS_IMU_SUSPECT: IMU/orientation quality disagreement */
    ICON_DISK_STRIKE,    /* SYS_STORAGE_DEAD: floppy outline with a diagonal strike */
    ICON_DISK_FULL,      /* SYS_STORAGE_FULL */
    ICON_DISK_WARN,      /* SYS_STORAGE_DEGRADED (near full / write error) */
    ICON_BATT_LOW,       /* SYS_BATT_LOW */
    ICON_SAFE,           /* SYS_SAFE_MODE */
    ICON_THERMOMETER,    /* SYS_DISP_TEMP_THROTTLE */
    ICON_LEAN_Q,         /* SYS_FUSION_DISAGREE: lean "?" */
    ICON_SIM,            /* SCR_UI_SIM */
    ICON_MOVING,         /* SCR_UI_MOVING */
    ICON_LINK,           /* SCR_UI_LINK */
    ICON_COUNT
} icon_id_t;

/* icon_bitmaps[id][row] is ICON_STRIDE bytes covering the icon's ICON_W columns: byte 0 holds
 * columns 0-7 (MSB = column 0) and byte 1's TOP four bits hold columns 8-11, so byte 1's LOW
 * nibble is the unused padding and is always 0 (io false claim 6: icons.h and icons.c used to
 * contradict each other about which nibble pads). */
extern const uint8_t icon_bitmaps[ICON_COUNT][ICON_H][ICON_STRIDE];
```
`icons.c` — fix the same contradiction in the file header (`top 4 bits of the low byte's padding half unused` → `the low 4 bits of the second byte are the unused padding, always 0`), retarget `ICON_GPS_STRIKE`'s comment to bit 0 (`SYS_GPS_DEAD`, not `SYS_GPS_NOFIX`), delete the `ICON_GPS`, `ICON_BLE` and `ICON_DISK` entries, and add these four (12 rows of `{ byte0, byte1 }` in the layout above):
```c
    /* ICON_GPS_NOFIX — hollow location pin: a fix is being searched for (SYS_GPS_NOFIX)
     * ....####....   ...##..##...   ..##....##..   ..#......#..
     * ..#......#..   ..##....##..   ...##..##...   ....####....
     * ....#..#....   .....##.....   ............   ............ */
    [ICON_GPS_NOFIX] = {
        { 0x0f, 0x00 }, { 0x19, 0x80 }, { 0x30, 0xc0 }, { 0x20, 0x40 },
        { 0x20, 0x40 }, { 0x30, 0xc0 }, { 0x19, 0x80 }, { 0x0f, 0x00 },
        { 0x09, 0x00 }, { 0x06, 0x00 }, { 0x00, 0x00 }, { 0x00, 0x00 },
    },
    /* ICON_IMU_STRIKE — the IMU "?" with an anti-diagonal strike (SYS_IMU_DEAD)
     * ..######...#   .##....##.#.   .#......##..   ........#...
     * ......##....   .....##.....   ....##......   ....#.......
     * ...#........   ..#.##......   .#..##......   #........... */
    [ICON_IMU_STRIKE] = {
        { 0x3f, 0x10 }, { 0x61, 0xa0 }, { 0x40, 0xc0 }, { 0x00, 0x80 },
        { 0x03, 0x00 }, { 0x06, 0x00 }, { 0x0c, 0x00 }, { 0x08, 0x00 },
        { 0x10, 0x00 }, { 0x2c, 0x00 }, { 0x4c, 0x00 }, { 0x80, 0x00 },
    },
    /* ICON_DISK_STRIKE — floppy outline with an anti-diagonal strike (SYS_STORAGE_DEAD)
     * ...........#   .##########.   .#.......##.   .#......#.#.
     * .#.....#..#.   .#....#...#.   .#...#....#.   .#..#.....#.
     * .#.#......#.   .##.......#.   .##########.   #........... */
    [ICON_DISK_STRIKE] = {
        { 0x00, 0x10 }, { 0x7f, 0xe0 }, { 0x40, 0x60 }, { 0x40, 0xa0 },
        { 0x41, 0x20 }, { 0x42, 0x20 }, { 0x44, 0x20 }, { 0x48, 0x20 },
        { 0x50, 0x20 }, { 0x60, 0x20 }, { 0x7f, 0xe0 }, { 0x80, 0x00 },
    },
    /* ICON_LEAN_Q — a "?" beside a leaning mast on a ground line (SYS_FUSION_DISAGREE: the lean
     * angle and lateral g on screen are not trustworthy)
     * .###........   #...#.......   ....#.......   ...##.......
     * ..#........#   ..........#.   ..#.......#.   .........#..
     * .........#..   ........#...   ........#...   .....######. */
    [ICON_LEAN_Q] = {
        { 0x70, 0x00 }, { 0x88, 0x00 }, { 0x08, 0x00 }, { 0x18, 0x00 },
        { 0x20, 0x10 }, { 0x00, 0x20 }, { 0x20, 0x20 }, { 0x00, 0x40 },
        { 0x00, 0x40 }, { 0x00, 0x80 }, { 0x00, 0x80 }, { 0x07, 0xe0 },
    },
```
`screens_moto.c:201-231` — replace the table comment, the table and the static accessor:
```c
/* Bit position (SCR_SYS_*, model.h) -> icon (icons.h), per spec §17.4's Icon column and §20.5.
 * -1 is a bit §17.4 marks "--": SYS_DISP_DEAD (the panel is gone -- there is nowhere to draw it),
 * SYS_HEAP_LOW and SYS_OTA_PENDING. Bits 14/15/16 are ui-level (SCR_UI_SIM/SCR_UI_MOVING/
 * SCR_UI_LINK, model.h), not §17.4 sys flags -- they always draw. io:H1: bits 1, 2, 5 and 13 used
 * to map to the wrong glyph or to nothing; every §17.4 bit with an Icon entry now has its own. */
static const int8_t FAULT_ICON_FOR_BIT[SCR_STRIP_BITS] = {
    (int8_t)ICON_GPS_STRIKE,    /* 0  SYS_GPS_DEAD */
    (int8_t)ICON_GPS_NOFIX,     /* 1  SYS_GPS_NOFIX */
    (int8_t)ICON_IMU_STRIKE,    /* 2  SYS_IMU_DEAD */
    (int8_t)ICON_IMU_Q,         /* 3  SYS_IMU_SUSPECT */
    -1,                         /* 4  SYS_DISP_DEAD */
    (int8_t)ICON_DISK_STRIKE,   /* 5  SYS_STORAGE_DEAD */
    (int8_t)ICON_DISK_FULL,     /* 6  SYS_STORAGE_FULL */
    (int8_t)ICON_DISK_WARN,     /* 7  SYS_STORAGE_DEGRADED */
    (int8_t)ICON_BATT_LOW,      /* 8  SYS_BATT_LOW (special-cased below for the "%<pct>" label) */
    (int8_t)ICON_SAFE,          /* 9  SYS_SAFE_MODE */
    -1,                         /* 10 SYS_HEAP_LOW */
    (int8_t)ICON_THERMOMETER,   /* 11 SYS_DISP_TEMP_THROTTLE */
    -1,                         /* 12 SYS_OTA_PENDING */
    (int8_t)ICON_LEAN_Q,        /* 13 SYS_FUSION_DISAGREE */
    (int8_t)ICON_SIM,           /* 14 SCR_UI_SIM (ui-level, model.h) */
    (int8_t)ICON_MOVING,        /* 15 SCR_UI_MOVING (ui-level, model.h) */
    (int8_t)ICON_LINK,          /* 16 SCR_UI_LINK (ui-level, model.h) */
};

int ui_fault_icon_for_bit(int bit)
{
    CORE_ASSERT_RET(bit >= 0, UI_ASSERT_CODE, -1);
    CORE_ASSERT_RET(bit < SCR_STRIP_BITS, UI_ASSERT_CODE, -1);
    return (int)FAULT_ICON_FOR_BIT[bit];
}
```
Replace the two internal `fault_icon_for_bit(...)` calls (`fault_strip:258`, `fault_strip_left_x:310`) with `ui_fault_icon_for_bit(...)` and delete the old static. Declare it in `core/ui/model.h` beside `fault_strip`:
```c
/* The icon (core/ui/icons.h) spec §17.4/§20.5 assigns to strip bit `bit` (SCR_SYS_*/SCR_UI_*
 * above), or -1 when that bit draws none. One table, shared by fault_strip() and its width
 * helper, and asserted against §17.4 by test/test_screens.c (io:H1). */
int ui_fault_icon_for_bit(int bit);
```

- [ ] **Step 4: Regenerate and eyeball the goldens.** Write the ASCII renderer into the scratchpad once (earlier plans used a `pbm_ascii.py` from a previous session's scratchpad; it is not in the tree):
```python
#!/usr/bin/env python3
"""Render a P4 PBM as ASCII ('#' = black ink). usage: pbm_ascii.py <file.pbm> [y0 y1 xstep]"""
import sys
d = open(sys.argv[1], "rb").read()
assert d[:2] == b"P4", "not a P4 PBM"
i = 2
def tok():
    global i
    while d[i:i + 1].isspace(): i += 1
    j = i
    while not d[j:j + 1].isspace(): j += 1
    t = d[i:j]; i = j
    return int(t)
w, h = tok(), tok(); i += 1
stride = (w + 7) // 8
y0 = int(sys.argv[2]) if len(sys.argv) > 2 else 0
y1 = int(sys.argv[3]) if len(sys.argv) > 3 else h
xs = int(sys.argv[4]) if len(sys.argv) > 4 else 1
for y in range(y0, min(y1, h)):
    row = d[i + y * stride: i + (y + 1) * stride]
    print("".join("#" if (row[x // 8] >> (7 - x % 8)) & 1 else "." for x in range(0, w, xs)))
```
Per golden, following `test/test_screens.c`'s documented workflow: delete it, run the suite (`pbm_eq_file` dumps `<name>.pbm.actual.pbm`), render the dump over the strip rows (110–122 on the 213 canvas, 116–128 on the 296; `icons_and_bar.pbm` rows 4–16 and 30–84), **describe in the task report what each glyph looks like** — the hollow pin must read as an outline not a blob, the IMU strike as a "?" with a slash, the disk strike as a floppy outline with a slash, the lean "?" as a question mark beside a leaning mast on a ground line — then promote and re-run byte-exact. Repeat for `test_screens_213`. `ctest -R 'test_screens|test_screens_213|test_ui' --output-on-failure` → byte-exact on all three.

- [ ] **Step 5: `docs/display-glyphs.md`, gates, commit.** Replace the single "GPS strike" row and the "Disk" row with five rows:

| Glyph | Looks like | Means | What to do |
|---|---|---|---|
| GPS strike | hollow GPS ring with a diagonal strike | The GPS module is not answering at all (`SYS_GPS_DEAD`). Nothing is being timed. | Check the module/wiring (BOOT shows `GPS FAIL`). |
| GPS hollow | hollow location pin | No fix yet (`SYS_GPS_NOFIX`): the receiver is searching. Nothing is timed until it clears. | Wait outdoors with a clear sky view. |
| IMU strike | `?` with a diagonal strike | The IMU is not answering (`SYS_IMU_DEAD`): no lean, no lateral g, drag launches fall back to GPS only. | Check the I²C wiring (BOOT shows `IMU FAIL`). |
| Disk strike | floppy outline with a diagonal strike | Storage is dead / not mounted (`SYS_STORAGE_DEAD`): nothing is being logged. | Reboot; if it persists the filesystem needs reformatting. |
| Lean `?` | `?` beside a leaning mast on a ground line | Fusion disagrees with itself (`SYS_FUSION_DISAGREE`): the lean angle shown is not trustworthy. | Re-run Calibrate (#93 until wired); check the mount. |

…and the closing paragraph:
```
Conditions with no icon (they appear in the errlog / dev-kit instead): `SYS_DISP_DEAD` (the display
itself is gone, so there is nowhere to draw it), `SYS_HEAP_LOW` and `SYS_OTA_PENDING` (an update is
on trial for its first 30 s). Every other §17.4 bit now has its own glyph. The healthy-storage and
Bluetooth bitmaps were deleted: no condition ever drew them (`sys_flags` has no "storage OK" bit,
and BLE is sub-project C), and a bitmap nothing draws is dead flash.
```
Gates: full `ctest`; lint 0; gcc-16 sweep clean; clean `moto_sim` build 0 warnings (report the flash delta: four new const tables −3 deleted = +24 B rodata, 0 DRAM).
```bash
git add components/core/ui/icons.c components/core/ui/include/core/ui/icons.h \
        components/core/ui/screens_moto.c components/core/ui/include/core/ui/model.h \
        docs/display-glyphs.md test/test_screens.c test/test_ui.c test/snapshots
git commit   # feat(core/ui): four new fault glyphs and the real §17.4 bit->icon map; delete the three dead icons (io:H1, io:L4, R18)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 10 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 11: remove `fb_t.dirty` and the "changed region" claims (io:M4, R27, io:M8, false claim 4)

**Files:**
- Modify: `components/core/ui/render.c:37-59,72-88,107-110,134,149-150,154,178-181,198`, `ui/include/core/ui/render.h:24-70`, `ui/include/core/ui/model.h:208-217`, `ui/include/core/ui/canvas.h:35-38`, `components/app/ui/ui.c:1498-1514` (cross-boundary edit 2)
- Test: `test/test_ui.c:49-101,221-223`, `test/test_screens.c` (the ~47 `s_fb.dirty.valid` lines and `fb_max_ink_col`'s comment)

**Interfaces:** `fb_t` = `{ uint8_t *bits; uint16_t w, h; uint16_t stride; }` — no `dirty`. `fb_rect_t` **stays** (it is `fb_diff_rect`'s out-parameter, `render.h:87`, and the real partial-refresh window since 7c §5). All primitive signatures unchanged; `fb_extend_dirty` is deleted. R20 replacements in `render.c`:

| Site | Old | New |
|---|---|---|
| `:107-110` (`fb_rect`, > 20 code lines) | `cx0 >= 0`, `cx1 <= fb->w`, `cy0 >= 0`, `cy1 <= fb->h`, each restating the clamp two lines above | at the top: `CORE_ASSERT_VOID(fb->bits != NULL, …)` and `CORE_ASSERT_VOID((int)fb->stride * 8 == (int)fb->w, …)` — fb_init's invariant, which is what makes `fb_set_px`'s stride indexing in bounds |
| `:149-150` (`fb_hline`, 13 lines) | `cx0 >= 0`, `cx1 <= fb->w` | the same two preconditions at the top |
| `:178-181` (`fb_blit_1bpp`, > 20 lines) | the same four | `CORE_ASSERT_VOID(fb->bits != NULL, …)` plus `CORE_ASSERT_VOID(stride * 8 >= cw, …)` — the bound that actually keeps `bitmap[row * stride + col / 8]` inside its row |

- [ ] **Step 1: Replacement tests** — `test/test_ui.c`. Delete `test_fb_init_starts_with_no_dirty_region`, `test_fb_clear_marks_the_whole_frame_dirty`, `test_dirty_box_accumulates_bounding_box_of_draws` (and their `RUN_TEST` lines) and the two trailing `s_fb.dirty.*` assertions in `test_clipping_...`; add, registering both:
```c
static void test_fb_init_sets_the_geometry(void)
{
    /* io:M4/R27: fb_t no longer carries a dirty box (ui.c's partial window comes from
     * fb_diff_rect against the panel's last accepted frame, 7c §5), so fb_init's whole
     * observable contract is the geometry it derives. */
    memset(&s_fb, 0xAA, sizeof s_fb);
    fb_init(&s_fb, s_bits, FB_W, FB_H);
    TEST_ASSERT_EQUAL_PTR(s_bits, s_fb.bits);
    TEST_ASSERT_EQUAL_UINT16(FB_W, s_fb.w);
    TEST_ASSERT_EQUAL_UINT16(FB_H, s_fb.h);
    TEST_ASSERT_EQUAL_UINT16(FB_W / 8, s_fb.stride);
}

static void test_off_frame_draws_touch_no_bits(void)
{
    /* The "a fully off-frame draw is a no-op" invariant the deleted dirty-box test carried,
     * checked against the bits themselves -- a stronger check than the box ever was. */
    static uint8_t copy[sizeof s_bits];
    fb_clear(&s_fb, 0);
    memcpy(copy, s_bits, sizeof copy);
    fb_icon(&s_fb, ICON_GPS_NOFIX, 1000, 1000);
    fb_rect(&s_fb, -50, -50, 10, 10, 1, true);
    fb_hline(&s_fb, 4, -3, 40, 1);
    fb_text(&s_fb, &FONT_SMALL, 400, 4, "OFF");
    TEST_ASSERT_EQUAL_MEMORY(copy, s_bits, sizeof copy);
    fb_clear(&s_fb, 1);                                  /* and a clear really touches every byte */
    for (size_t i = 0; i < sizeof s_bits; i++) TEST_ASSERT_EQUAL_UINT8(0x00u, s_bits[i]);
}
```
`test/test_screens.c`: delete every `TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H));` line (one per golden case — `grep -c` first and report the count), and end `fb_max_ink_col`'s comment at `:48-51` with:
```c
 * (CANVAS_VISIBLE_W, core/ui/canvas.h). This content scan is the whole check: fb_t carries no
 * dirty box any more (io:M4), and fb_clear() touched every addressable byte anyway, padding
 * columns included, so a box could never have said anything narrower. Plan 7 T3 fix 1, T3-R1. */
```

- [ ] **Step 2: Run them.** `cmake --build test/build -j8 && ctest -R 'test_ui|test_screens'` → green (the field still exists). This is the "before" state; Step 3 makes the deletion compile-enforced.

- [ ] **Step 3: Implement.** `render.h`: drop `dirty` from `fb_t`; re-comment `fb_rect_t` as `/* fb_diff_rect's out-parameter -- the partial-refresh window (7c §5) */` with `valid` as `/* false = empty rect (fb_diff_rect found no difference) */`; strike every "dirty" clause from the primitive docs (`fb_init` → "…call fb_clear to establish a known starting colour."; `fb_clear` → "Fills the whole framebuffer with `black` (nonzero = all ink/black, 0 = all background/white)."; `fb_rect` → "…a fully off-frame rect is a no-op."; `fb_hline`/`fb_text_inv` → "Same clipping rules as …"). `render.c`: delete `fb_extend_dirty` (`:37-59`) and every call (`grep -n fb_extend_dirty` → `:134`, `:154`, `:198`), the five `fb->dirty.* = …` lines in `fb_init` and the five in `fb_clear`, and apply the R20 table above. `canvas.h:35-38`: drop the `fb->dirty`/`render_now()` clause, keeping the sentence about `test_screens.c`'s content scan. `model.h:208` and `:217`:
```c
/* Renders the moto riding screens: dispatches on m->mode + m->page, clears the fb and draws the
 * screen. SCR_MODE_DRAG renders pages 0 (the engine-state/latest-gate card)/1 (all gates of the
 * last run)/2 (best per gate this session); see the SCR_MODE_DRAG case in screens_moto.c. Unlike
 * screens_render() below, this does not look at m->screen -- it always renders the riding layout,
 * which is what the host tests exercise directly. */
...
/* Top-level render entry point (spec §20.6-20.7): dispatches on m->screen -- SCR_RIDING to
 * screens_moto_render(), SCR_MENU to the menu list renderer, SCR_ONESHOT to the one-shot screen
 * renderer (selected by m->oneshot). Every path clears the fb first. This is the entry point the
 * app-side ui task calls every render. */
```
(this also deletes the "pages 0 (benches, §11.4)" half of false claim 3). `components/app/ui/ui.c` — `render_fb` becomes:
```c
static void render_fb(void)
{
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE);            /* fb_init ran before any render */
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE); /* screens_render dispatches on it */
    LT_ASSERT_VOID(s_model.page < 3, UI_APP_ASSERT_CODE);              /* riding renderer dispatches on it */
    screens_render(&s_fb, &s_model);
    /* No dirty-box bounds check here any more (io:M4/R27): fb_t carries no box, and the renderer's
     * per-pixel clipping is what keeps a draw inside the buffer. The content-based check that
     * "nothing draws at x >= CANVAS_VISIBLE_W" lives in test/test_screens.c's fb_max_ink_col(). */
}
```
Verify: `grep -rn 'fb->dirty\|\.dirty\.valid\|fb_extend_dirty' components/ main/ test/` comes back empty (the surviving `dirty` hits are `ui.c`'s `s_dirty`, `logger.c`'s `s_sum_dirty`, `lt_nvs.c`'s `s_counters_dirty` and `rf_in_t.dirty` — none is `fb_t.dirty`).

- [ ] **Step 4: Run to verify.** Full `ctest` green with **no golden changes** — removing bookkeeping changes no pixels, so any golden diff here means a primitive was edited by mistake. Lint 0; gcc-16 sweep clean; clean `moto_sim` build 0 warnings.

- [ ] **Step 5: Commit.**
```bash
git add components/core/ui components/app/ui/ui.c test/test_ui.c test/test_screens.c
git commit   # refactor(core/ui): drop fb_t.dirty and the false "changed region" claims; real preconditions in render.c (io:M4, io:M8, R27)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 11 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 12: bounded `put_str`; the after-the-fact buffer assertions (io:M2, io:M8, R20)

**Files:** Modify `components/core/ui/screens_moto.c` (helpers `:39-47`, all 20 `put_str(` call sites, and the assertion sites `:115`, `:143`, `:196`, `:273`, `:375`, `:397`, `:946`, `:1019`, `:1038`); Test `test/test_screens.c`

**Interfaces:** `static char *put_str(char *p, const char *end, const char *s)` — copies until `*s == '\0'` **or** `p == end`; `end` is the last writable text position, so the caller's trailing `*p = '\0'` is always in bounds. `put_char`, `put_uint`, `put_g_e2` keep their signatures (each writes a bounded, hand-derived number of bytes). New `#define SBUF_END(b) ((b) + sizeof (b) - 1u)`.

- [ ] **Step 1: Failing test** — `test/test_screens.c`:
```c
static void test_card_footer_survives_an_unterminated_model_label(void)
{
    /* io:M2: put_str walked the caller's string with no destination bound. Every model string is
     * NUL-terminated today (drag_gate_label and its "G%u" fallback both use snprintf, and the
     * model is memset), so this is the latent case: a drag row whose label[8] uses all eight bytes
     * with no NUL. It must render truncated, never walk off buf[32]. */
    screen_model_t m;
    memset(&m, 0, sizeof m);
    m.mode = SCR_MODE_DRAG; m.page = 0; m.drag_n = 2;
    memset(m.drag[0].label, 'W', sizeof m.drag[0].label);   /* 8 bytes, no NUL */
    m.drag[0].time_ms = 1284;
    memset(m.drag[1].label, 'W', sizeof m.drag[1].label);
    m.drag[1].time_ms = 2345;
    screens_moto_render(&s_fb, &m);
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W);   /* nothing ran off the frame */
}
```

- [ ] **Step 2: Run to verify it fails.** `ctest -R test_screens --output-on-failure` → ASan reports a stack-buffer-overflow in `render_dcard_footer`/`render_drag_page0` (`r->label`, `char label[8]`, copied into `buf[32]`). The suite's ASan+UBSan Debug build is what makes this visible.

- [ ] **Step 3: Implement** — `screens_moto.c`:
```c
/* Last writable text position of a local char array: the caller's trailing *p = '\0' always fits. */
#define SBUF_END(b) ((b) + sizeof (b) - 1u)

static char *put_str(char *p, const char *end, const char *s)
{
    CORE_ASSERT_RET(p != NULL, UI_ASSERT_CODE, p);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, p);
    CORE_ASSERT_RET(end >= p, UI_ASSERT_CODE, p);
    /* io:M2: bounded by the destination, not by the caller's string -- the only unbounded write in
     * the renderer. Rule 2: the loop is bounded by end - p. */
    while (p < end && *s != '\0') {
        *p++ = *s++;
    }
    return p;
}
```
Then update **every** `put_str(` call site — the compiler enforces completeness (`grep -n 'put_str(' components/core/ui/screens_moto.c` → 20 calls plus the definition). Each passes the end of the buffer the cursor walks: `SBUF_END(buf)` for a local array (`:373` `p = put_str(p, SBUF_END(buf), "LAP ");`, `:433`/`:460` `char *p = put_str(buf, SBUF_END(buf), EMPTY_TIME);`, `:597` `char *lp = put_str(label, SBUF_END(label), "MAX SPD ");`, …), and `buf + cap - 1u` in `newtrack_subline` (`:944`), whose buffer arrives as a `(char *buf, size_t cap)` pair.

Assertion replacements (R20):

| Site | Old | New |
|---|---|---|
| `:115` `fmt_time_ms` | `(p - buf) < TIME_BUF_LEN` | delete; beside `#define TIME_BUF_LEN 20` add `_Static_assert(TIME_BUF_LEN >= 15, "fmt_time_ms: 8 minute digits + ':' + 2 + '.' + 2 + NUL");` |
| `:143` `fmt_delta_ms` | `(p - buf) < DELTA_BUF_LEN` | delete; beside `#define DELTA_BUF_LEN 20` add `_Static_assert(DELTA_BUF_LEN >= 12, "fmt_delta_ms: sign + 7 second digits + '.' + 2 + NUL");` |
| `:196` `fmt_time_s` (12 lines) | `(p - buf) <= 5u` | delete; extend the clamp's comment with "…so the result is structurally at most 5 glyphs; `buf` must be >= 6 bytes" |
| `:273` `fault_strip` (> 20 lines) | `(p - pct_buf) < sizeof pct_buf` | delete, and add at the top of `fault_strip`: `CORE_ASSERT_VOID(FAULT_STRIP_X0 + ICON_W <= CANVAS_VISIBLE_W, UI_ASSERT_CODE);` — the real precondition `fault_strip_left_x` already carries, keeping this function at 2 assertions |
| `:375` `render_card_big` (13 lines) | `(p - buf) < sizeof buf` | delete (two NULL-param assertions remain) |
| `:397` `render_card_marker` (11 lines) | `(p - buf) < sizeof buf` | delete |
| `:946` `newtrack_subline` (16 lines) | `(p - buf) < cap` | delete — `cap >= NEWTRACK_SUB_BUF_CAP` at `:933` and `_Static_assert(LAP_MAX_SECTORS < 10, …)` at `:924` already prove the fit, and `put_str`'s bound enforces it |
| `:1019` `render_oneshot_lowbatt` (10 lines) | `(p - buf) < sizeof buf` | delete |
| `:1038` `render_oneshot_ota` (14 lines) | `(p - buf) < sizeof buf` | delete |

(`fmt_secs_ms`'s `strlen(buf) <= 5u` at `:173` stays: a postcondition on the `s > 999` clamp, not a restated assignment, and not flagged by the reviewer.)

- [ ] **Step 4: Run to verify.** `ctest -R 'test_screens|test_screens_213|test_ui'` byte-exact green (no golden changes: every production model string is NUL-terminated, so the bound never truncates); full `ctest`; lint 0 — if it reports a function over 20 code lines with fewer than 2 assertions, add the real precondition that function lacks (never re-add a tautology, never a deviation row).

- [ ] **Step 5: Commit.**
```bash
git add components/core/ui/screens_moto.c test/test_screens.c
git commit   # fix(core/ui): bound put_str against its destination; replace the after-the-fact buffer assertions (io:M2, io:M8, R20)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 12 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 13 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

- [ ] **SESSION 3 CHECKPOINT** — re-run the full `ctest` (100 %) and the linter (0); append both result lines to the ledger under "Session 3 end".

---

## Session 4 — Tasks 13–16 (lane C tail, lane D, final wave)

### Task 13: `refresh_policy` upper bound and the remaining false claims (io:L1, false claims 3 and 5)

**Files:** Modify `components/core/ui/refresh_policy.c:25`, `ui/include/core/ui/fonts.h:8`, `ui/screens_moto.c:1-2`; Test `test/test_refresh_policy.c`

**Interfaces:** `ui_refresh_decide(const rf_in_t *in)` additionally returns `RF_NONE` for `in->full_every > 50` — the other half of §15.1's cfg-enforced 1..50 range its comment already claims to backstop.

- [ ] **Step 1: Failing test** — `test/test_refresh_policy.c` (the file's own initialiser helper is `base()` at `:7`, which sets `dirty = true`, `full_every = 10`):
```c
static void test_full_every_above_the_cfg_range_is_none(void)
{
    /* io:L1: the assertion checked full_every >= 1 while its comment claimed it backstopped the
     * "cfg-enforced 1..50 range" -- the upper half was unchecked, so a caller bug past 50 silently
     * disabled the counter-based full-refresh trigger instead of being reported. */
    rf_in_t i = base();
    i.full_every = 51;
    TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
    i.full_every = 50;
    TEST_ASSERT_NOT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i));
}
```

- [ ] **Step 2: Run to verify it fails.** `ctest -R test_refresh_policy --output-on-failure` → `RF_PARTIAL` at `full_every = 51`.

- [ ] **Step 3: Implement.** `refresh_policy.c:25` → `CORE_ASSERT_RET(in->full_every >= 1 && in->full_every <= 50, RF_ASSERT_CODE, RF_NONE);` (its module comment at `:11` already reads "a full_every below its cfg-enforced 1..50 range" — change "below" to "outside").

`fonts.h:8` (false claim 5 — `FONT_BIG` was removed in Plan 7b §2; only three fonts exist):
```c
 * HUGE and MED store a non-contiguous character subset densely (sorted by code point), so a
```
`screens_moto.c:1-2` (false claim 3 — no part of §11.4's benches rule is implemented here: no ascending-by-speed ordering, no max-4-rows/drop-lowest-first, no `1/4 --` row):
```c
/* Moto riding screens: LAP pages 0/1/2 and DRAG pages 0/1/2 (spec §20.4-20.5, as amended by
 * 2026-09-27-plan-7b-glanceable-ui-design.md §4-§7) + the shared fault-icon strip (§20.5 + §17.4);
 * plus the one-shot screens (§20.6), the menu list (§20.7) and the top-level screens_render()
 * dispatch (session 4.3). NOTE: spec §11.4's benches rule (ascending by speed, at most four rows,
 * drop-lowest-first, a "1/4 --" row for a run that ends before the 1/4) is NOT implemented here --
 * 7b §7 superseded it with the newest-gate card plus a scrolling footer (io false claim 3). Pure
 * C11, no malloc/float/libm -- ...
```

- [ ] **Step 4: Run to verify.** `ctest` green throughout; lint 0; gcc-16 sweep clean.

- [ ] **Step 5: Commit.**
```bash
git add components/core/ui/refresh_policy.c components/core/ui/include/core/ui/fonts.h components/core/ui/screens_moto.c test/test_refresh_policy.c
git commit   # fix(core/ui): bound full_every at both ends; correct the FONT_BIG and §11.4 comments (io:L1, false claims 3/5)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 13 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 14: bounded `ring_pop`, power-of-two `ring_init` (io:M1, R27)

**Files:** Modify `components/core/include/core/ring.h:1-75`; Test `test/test_ring.c`

**Interfaces:** `RING_POP_RETRIES` (16) and `RING_ASSERT_CODE` (`0x0AD0` — the next free slot after `refresh_policy`'s `0x0AC0`; `0x0AF0`–`0x0AF3` are taken by stats_fold/units/drag_cfg/menu_labels). `ring_pop` returns `false` after 16 lost CAS attempts instead of spinning forever. `ring_init` with a non-power-of-two capacity reports `RING_ASSERT_CODE` and degrades to a **single** slot (`mask = 0`) rather than aliasing two indices onto one slot. `ring.h` now includes `core/core.h`.

- [ ] **Step 1: Failing tests** — `test/test_ring.c`, both registered in `main`:
```c
static void test_ring_init_rejects_a_capacity_that_is_not_a_power_of_two(void)
{
    /* R27/io:M1: mask = cap - 1 indexes correctly only for a power of two -- a cap of 3 gave
     * mask 2 and silently aliased slot 3 onto slot 0, and ring.h had no assertions at all.
     * Degrade to one slot instead of aliasing. */
    item_t storage[4]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 3, false);
    TEST_ASSERT_EQUAL_UINT32(0u, r.mask);
    item_t it = { 1, 2 };
    TEST_ASSERT_TRUE(ring_push(&r, &it));
    TEST_ASSERT_FALSE(ring_push(&r, &it));            /* capacity 1: the second push is a drop */
    TEST_ASSERT_EQUAL_UINT32(1u, ring_dropped(&r));
    item_t out;
    TEST_ASSERT_TRUE(ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_INT(1, out.a);
    TEST_ASSERT_FALSE(ring_pop(&r, &out));
}

static void test_ring_pop_on_an_uncontended_ring_is_unchanged_by_the_retry_cap(void)
{
    /* The cap is only reachable by losing the CAS, which a single thread cannot do -- so pin the
     * uncontended behaviour it must not disturb. The contended path stays covered by the two
     * stress cases, whose invariants (no torn item, nothing lost or miscounted) hold whether a pop
     * returns false early or retries: the consumer simply polls again. */
    item_t storage[4]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 4, true);
    for (int i = 0; i < 4; i++) { item_t it = { i, i * 10 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }
    for (int i = 0; i < 4; i++) {
        item_t out;
        TEST_ASSERT_TRUE(ring_pop(&r, &out));
        TEST_ASSERT_EQUAL_INT(i, out.a);
    }
    item_t out;
    TEST_ASSERT_FALSE(ring_pop(&r, &out));
}
```
(`test_ring.c` does not link `assert_support.c`, so the reported code is not observed; the test pins the *behaviour* — mask 0, one usable slot — which needs no CMake change.)

- [ ] **Step 2: Run to verify it fails.** `ctest -R test_ring --output-on-failure` → `r.mask == 2` and a second successful push.

- [ ] **Step 3: Implement** — `ring.h` (add `#include "core/core.h"`):
```c
/* R27/io:M1: every other for(;;) in core carries an explicit bound (ses_frame.c's NEXT_MAX_STEPS);
 * this one did not, so with overwrite_oldest and a producer on the other core a consumer could be
 * starved indefinitely. 16 attempts is far past any realistic CAS contention on two cores. */
#define RING_POP_RETRIES 16u
#define RING_ASSERT_CODE 0x0AD0
...
static inline void ring_init(ring_t *r, void *storage, uint32_t item_size, uint32_t cap_pow2, bool overwrite_oldest)
{
    CORE_ASSERT_VOID(r != NULL, RING_ASSERT_CODE);
    CORE_ASSERT_VOID(storage != NULL, RING_ASSERT_CODE);
    CORE_ASSERT_VOID(item_size > 0u, RING_ASSERT_CODE);
    /* mask = cap - 1 indexes correctly only for a power of two; any other capacity would alias two
     * indices onto one slot. Initialise to a safe single slot FIRST, then report -- CORE_ASSERT_VOID
     * returns, and a ring_t left untouched would hand ring_push a NULL buf. */
    uint32_t cap = cap_pow2;
    if (cap == 0u || (cap & (cap - 1u)) != 0u) { cap = 1u; }
    r->buf = (uint8_t *)storage; r->item_size = item_size; r->mask = cap - 1u;
    atomic_store(&r->head, 0u); atomic_store(&r->tail, 0u);
    r->overwrite = overwrite_oldest; atomic_store(&r->dropped, 0u);
    CORE_ASSERT_VOID(cap == cap_pow2, RING_ASSERT_CODE);   /* the caller's capacity was usable */
}
...
static inline bool ring_pop(ring_t *r, void *out)
{
    CORE_ASSERT_RET(r != NULL, RING_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, RING_ASSERT_CODE, false);
    for (uint32_t tries = 0; tries < RING_POP_RETRIES; tries++) {   /* rule 2 */
        uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
        uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
        if (t == h) return false;
        memcpy(out, r->buf + (size_t)(t & r->mask) * r->item_size, r->item_size);
        /* Publish only if the producer did not evict this slot while we copied it. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &t, t + 1u, memory_order_acq_rel, memory_order_acquire))
            return true;
        /* Lost the race: the copy may be torn. Retry from the new tail. */
    }
    return false;   /* contended out after RING_POP_RETRIES: the caller's next poll retries */
}
```
Extend the header's policy comment: "…and retries when it lost the race, at most `RING_POP_RETRIES` times before returning false (the caller polls again), so a torn copy is never returned and the consumer is never unboundedly starved."

- [ ] **Step 4: Run to verify.** `ctest -R test_ring --output-on-failure` green including both stress cases; full `ctest`; `idf.py -C test_apps/core_selftest build` rc 0 / 0 warnings (that build runs `test_ring` with `-DRING_STRESS_N=20000ULL`); clean `moto_sim` build 0 warnings (`lt_ipc.h` and `link.c` include `ring.h`).

- [ ] **Step 5: Commit.**
```bash
git add components/core/include/core/ring.h test/test_ring.c
git commit   # fix(core): bound ring_pop's CAS retry at 16 and refuse a non-power-of-two ring capacity (io:M1, R27)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 14 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 15 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 15: design §4 gates, DRAM delta, bench checklist

**Files:** Modify `docs/bench/next-bench-day.md` (append one section)

- [ ] **Step 1: Host gates.**
```bash
rm -rf test/build
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100 %, 0 failures
python3 .superpowers/bench-helpers/gcc_sweep.py            # gcc-16 over compile_commands.json: 0
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0 violations
python3 tools/lint/venue_announce.py                       # 0
```
Record the executable count and the pass lines verbatim in the task report.

- [ ] **Step 2: Firmware gates** (ccache off; `source tools/idf-env.sh` first — it pins IDF 5.3.2 from `.idf-version`; never fall back to 5.5.1). No flashing.
```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build
IDF_CCACHE_ENABLE=0 PANEL=ws29v2 ./build.sh moto_sim build
IDF_CCACHE_ENABLE=0 idf.py -C test_apps/core_selftest build
```
All four rc 0 with **0** compiler warnings.

- [ ] **Step 3: DRAM delta.** Report free static DRAM for `moto_sim` and `moto_neo6m` against design §4's baselines (4192 B / 5616 B). Expected net ≈ **−16 B of .bss** (a gain): `ui.c`'s two static `fb_t` each lose the 10-byte `fb_rect_t dirty` (−20 B); `cmd.c:375`'s static `exp_t` gains `uint16_t skipped` plus alignment (+2…4 B). The four new icon tables are `const` (flash, +96 B) against three deleted (−72 B) → +24 B flash, 0 DRAM. If the measured DRAM is worse than baseline by more than 256 B, stop and report — design §4 requires a named reclaim.

- [ ] **Step 4: Bench checklist** — append to `docs/bench/next-bench-day.md`:
```markdown
## F1b (audit fix wave — core io + ui core)

1. **Fault glyphs on the panel** (io:H1). `dbg flag set 1` → the strip shows a **hollow** GPS pin,
   visibly different from `dbg flag set 0`'s struck-through ring. Then `dbg flag set 2` (IMU "?"
   with a slash), `set 5` (floppy outline with a slash), `set 13` ("?" beside a leaning mast);
   `dbg flag clear <n>` after each. All four were invisible or wrong before this plan, so each is a
   first-light check at 12x12 on real e-paper: photograph the strip and confirm each glyph is
   distinguishable at arm's length.
2. **Partial refresh after the dirty-box removal** (io:M4). The partial window has come from
   `fb_diff_rect` since 7c T7, so nothing should change — confirm on the panel that a page change
   still refreshes in full (ruling B-9) and a gate crossing still refreshes partially, with no
   stale rows. One lap of sim run is enough.
3. **Session export after the exporter changes** (io:C1/C2/M5). `lt get <session> vbo`, `… nmea`
   and `… json` on a sim session: every VBO `[data]` row ends with CRLF, every NMEA checksum
   verifies, the JSON parses whole and its `"skipped"` reads 0.
```

- [ ] **Step 5: Commit.**
```bash
git add docs/bench/next-bench-day.md
git commit   # docs(bench): F1b bench items — fault glyphs, partial refresh, session export
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 15 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 16 (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

---

### Task 16: the spec and 7b amendments this plan owns

**Files:** Modify `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§12.3, §14.3, §20.2, §20.5), `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` (§5, §6, §7)

- [ ] **Step 1: §20.2** — replace the icon bullet:
```
- Icons 12×12 (`core/ui/icons.c`, 14 glyphs): GPS-strike, GPS-hollow, IMU-strike, IMU-q,
  disk-strike, disk-full, disk-warn, battery-low, SAFE, thermometer, lean-q, SIM, MOVING, LINK.
  Every glyph is drawn by some condition — the §17.4 bit→icon map is `FAULT_ICON_FOR_BIT` in
  `core/ui/screens_moto.c`, reachable as `ui_fault_icon_for_bit()` and asserted against the §17.4
  table by `test/test_screens.c`. The Plan 4 list also named a healthy-disk and a BLE glyph; both
  bitmaps were deleted in the 2026-10-13 audit fix wave (ruling R18 and its "zero dead code"
  argument — no condition drew either; BLE returns with sub-project C).
```

- [ ] **Step 2: §20.5** — replace the first two sentences of the "Fault icons" paragraph (line 2405):
```
Fault icons: drawn only when the corresponding `sys_flags` bit is set, in a strip at bottom-right;
`SYS_BATT_LOW` shows a battery icon with `%`. `SYS_GPS_NOFIX` shows the hollow GPS pin and
`SYS_GPS_DEAD` the struck-through ring — two different glyphs, as §17.4's Icon column requires;
`SYS_IMU_DEAD`, `SYS_STORAGE_DEAD` and `SYS_FUSION_DISAGREE` likewise each have their own glyph
(IMU-strike, disk-strike, lean-q) since the 2026-10-13 audit fix wave (ruling R18). Three ui-level
glyphs share the strip, drawn after the §17.4 icons: …
```
(the SIM/MOVING/LINK remainder is unchanged).

- [ ] **Step 3: §12.3 and §14.3.** §12.3, on the `VENUE` record's `name` row, append: "`ses_encode_venue` writes all 32 bytes (zero-padded, not required to be NUL-terminated), matching `SESSION_HDR`'s `fw`/`hwid`; it capped at 31 before the 2026-10-13 audit fix wave (io:M9)." §14.3, after the document shape, add:
```
The top-level object's last member is `"skipped":N` — the number of records the exporter dropped
because they failed to decode (a wrong payload length, or a `gps_us <= 0` admitted by §12.2's CRC
resync). `N` saturates at 65535. A whole, well-formed export reads `"skipped":0`; a non-zero count
means the log was damaged and the export is partial but still structurally valid (added by the
2026-10-13 audit fix wave, ruling R26).
```

- [ ] **Step 4: 7b §5/§6/§7 — the M7 ruling.** Add as the last paragraph of **§5**:
```
**Fault strip (ruling M7, 2026-10-13 audit fix wave):** pages 1 and 2 deliberately carry **no**
fault strip — it is drawn only by `render_lap_page0` and `render_drag_page0`. The strip's row band
(`FAULT_STRIP_Y` 110 on the 2.13") collides with these pages' own bottom row (`GRID_FOOTER_Y` 106 +
12 = 118), and the glanceable pages are secondary screens the rider reaches by pressing MODE, not
the screen they ride on. §20.5 states the strip for the riding screens as a family; this clause is
the exception. The cost is that `MOVING` — the operator's cue that a sim run has finished — is
invisible on two of three pages; the operator presses MODE back to page 0 to see it.
```
Add one sentence to **§6** and to **§7**'s "Pages 1 … and 2 … share one list layout" paragraph: "No fault strip on this page — see §5's ruling M7."

- [ ] **Step 5: Verify and commit.** Re-read the amended sections against the merged tree — every claim must be true of the code as committed, which is the whole point of the audit. `grep -n 'ICON_GPS\b\|ICON_BLE\b\|ICON_DISK\b' docs/superpowers/specs/*.md docs/display-glyphs.md` must come back empty.
```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md
git commit   # docs(spec): §12.3/§14.3/§20.2/§20.5 and 7b §5-§7 match the F1b tree (R18, R26, ruling M7)
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F1b/progress.md` ("Task 16 complete \<sha\>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md at Task 16 (complete — open the PR) (ledger .superpowers/sdd/2026-10-13-fix-F1b/progress.md); worktree \<path\>; branch fix-core-io."

- [ ] **SESSION 4 CHECKPOINT** — re-run the full `ctest` (100 %) and the linter (0); append both result lines plus the Task 15 DRAM figures to the ledger under "Session 4 end / plan complete".

---

## Cleanup done (every deletion this plan makes)

| What is deleted | Where | Why | Task |
|---|---|---|---|
| `ICON_GPS` bitmap + enumerator | `icons.c`, `icons.h` | referenced by no production code (io:L4, R18) | 10 |
| `ICON_BLE` bitmap + enumerator | `icons.c`, `icons.h` | same; BLE returns with sub-project C | 10 |
| `ICON_DISK` bitmap + enumerator | `icons.c`, `icons.h` | dead once bit 5 maps to `ICON_DISK_STRIKE` (R18's own "zero dead code" argument) | 10 |
| `static fault_icon_for_bit` | `screens_moto.c` | replaced by the public, testable `ui_fault_icon_for_bit` | 10 |
| `fb_t.dirty` member | `render.h` | no production reader since 7c §5 (io:M4, R27) | 11 |
| `fb_extend_dirty` + its 3 call sites | `render.c` | bookkeeping for the deleted field | 11 |
| the 10 `fb->dirty.* = …` lines in `fb_init`/`fb_clear` | `render.c` | same | 11 |
| 3 dirty-box tests + 2 trailing dirty assertions | `test_ui.c` | replaced by geometry and bits-level checks | 11 |
| ~47 `s_fb.dirty.valid` assertions | `test_screens.c` | `fb_max_ink_col` is the real check | 11 |
| 2 `s_fb.dirty` assertions in `render_fb` | `components/app/ui/ui.c` | cross-boundary edit 2 | 11 |
| 4 clip tautologies in `fb_rect`, 2 in `fb_hline`, 4 in `fb_blit_1bpp` | `render.c` | R20; replaced by real preconditions | 11 |
| `depth < JW_MAX_DEPTH` after the branch that guarantees it; 2 `overflow \|\| depth > 0` postconditions | `jw.c` | R20; the cap becomes control flow | 9 |
| 2 scan postconditions in `json_skip` | `json.c` | R20 | 9 |
| 9 after-the-fact `(p - buf) < sizeof buf` assertions | `screens_moto.c` | R20; `put_str`'s bound + 2 static asserts replace them | 12 |
| false claim: "every caller pre-checks `exp_win_free()`" | `exp.c:18-20` | `exp_vbo_open` does not | 5 |
| false claim: "exactly as the decoder will" | `ses_records.c:91` | 5 decoder-set fields are not advanced | 5 |
| false claim: "leaves `fb->dirty` as the changed region" ×2 | `model.h:208,217` (+ `render.h`, `canvas.h` clauses) | nothing reads it; it was always the whole frame | 11 |
| false claim: "+ the §11.4 benches rule" | `screens_moto.c:1-2`, `model.h:209` | no part of §11.4 is implemented there (7b §7 superseded it) | 13, 11 |
| false claim: "HUGE, **BIG** and MED" | `fonts.h:8` | `FONT_BIG` was removed in Plan 7b §2 | 13 |
| false claim: the padding-nibble contradiction | `icons.h:30` vs `icons.c:4-5` | verified: the **low** nibble of byte 1 pads | 10 |
| false claim: "which `JSMN_PARENT_LINKS` guarantees is contiguous" | `json.h:19` | contiguity comes from document order | 6 |
| recursive `walk` + its `obj`/`prefix` parameters | `cfg_json.c` | Power of 10 rule 1 (io:H2, R27) | 7 |
| `bool kmh = path[13] == 'k'` | `cfg_json.c:71` | `strcmp` already decided (io:L3) | 7 |
| unbounded `for (;;)` in `ring_pop` | `ring.h:64` | rule 2 (io:M1, R27) | 14 |

**Not deleted here — F4 owns it:** spec §5.2's `ses_frame_cb_t`/`ses_reader_feed`/`ses_reader_flush`/`core_set_assert_hook` declarations (reviewer false claim 8), and its `cfg.h` block's missing `cfg_migrate_supported`.

## Finding coverage

| id | Task |
|---|---|
| C1 non-positive `gps_us`, unchecked `snprintf` | 1 |
| C2 `json_stage` before `emit`, `buf[400]` | 2 |
| H1 §17.4/§20.5 bit→icon table, dead icon data | 10 |
| H2 `walk()` recursion | 7 |
| M1 `ring_pop` unbounded, no power-of-two check | 14 |
| M2 `put_str` unbounded | 12 |
| M3 `json_tok_bool` token type | 6 |
| M4 `fb->dirty` dead + false "changed region" | 11 |
| M5 NMEA 200 B reserve | 3 |
| M6 `cfg_apply_profile` unvalidated; no-blob boot save | 8 |
| M7 no strip on pages 1/2, undocumented | 16 |
| M8 padding assertions | 9 (`jw`/`json`), 11 (`render.c`), 12 (`screens_moto.c`) |
| M9 `ses_encode_venue` 31-byte name | 4 |
| L1 `refresh_policy` upper bound | 13 |
| L2 `tok_copy` return, `strtoll`/`strtod` range, `inf` | 6 |
| L3 `path[13] == 'k'` | 7 |
| L4 `ICON_GPS`/`ICON_BLE` dead flash | 10 |
| L5 unclamped bench tail | 8 |
| False claims 1, 2 | 5 |
| False claim 3 | 13 (`screens_moto.c`), 11 (`model.h`) |
| False claim 4 | 11 |
| False claim 5 | 13 |
| False claim 6 | 10 |
| False claim 7 | 6 |
| False claim 8 | **F4, not F1b** |
| False claim 9 | 4 |
| Gap: negative/absurd `gps_us` through an exporter | 1 |
| Gap: control characters through `jw_str` via `exp_json` | 2 |
| Gap: wrong JSON type for the three bool fields | 6 |
| Gap: `fault_strip`'s bit→icon table untested | 10 |
| Gap: NMEA reserve boundary; `exp_pull` with `cap < win_len` | 3 |

## Self-Review (run against the design doc and the reviewer report after the plan was written)

**1. Spec coverage.** Design `2026-10-13-audit-fix-wave-design.md` row F1b and every ruling it names:

| Requirement | Where it lands | Note |
|---|---|---|
| **R26** `ses_decode_fix` rejects `gps_us <= 0` | Task 1 | both the `FIX_KEY` and the reconstructed `FIX_DELTA` branch |
| **R26** `exp_utc_parts` clamps at 0 and asserts | Task 1 | clamp + an `*hh/*mm/*ss/*cs` range postcondition (an *early-return* assertion would have left the out-params unwritten) |
| **R26** `snprintf` returns checked | Task 1 | `put_sentence`, both NMEA bodies, the VBO row, `exp_vbo_open`'s three writes, `exp_json_finish`'s tail — a row/sentence that would not fit is **dropped and counted**, never truncated |
| **R26** `json_stage` set after a successful emit | Task 2 | |
| **R26** header buffer | Task 2 | **512**, not the design's 480: the worst case is 113 + 348 + 34 + 1 = 496, proved by a `_Static_assert` on named terms. Deviation declared in the task body |
| **R26** NMEA reserve 232 | Task 3 | |
| **R26** `ses_encode_venue` copies 32 via `bw_bytes` | Task 4 | with a destination-bounded length scan, since the parameter is a C string |
| **R26** `json_tok_bool` requires `JSMN_PRIMITIVE` | Task 6 | |
| **R26** `cfg_apply_profile` validates `log_fused_hz ∈ {5,10,25}` | Task 8 | |
| **R26** `boot_config` runs `cfg_validate` before the no-blob save | Task 8 | declared cross-boundary edit 1 (`main/app_main.c`, F2a's file; F1b merges first) |
| **R27** iterative `walk` | Task 7 | two-level loop shown in full, with an equivalence test that passes before *and* after |
| **R27** `ring_pop` retry cap 16 with a `/* rule 2 */` note | Task 14 | plus a `ring_init` power-of-two check that degrades to one slot instead of aliasing |
| **R27** `fb_t.dirty` removed with the `render.h`/`model.h` claims | Task 11 | every use grepped, tests included; declared cross-boundary edit 2 (`ui.c` `render_fb`) |
| **R18** (icons half) 4 new 12×12 bitmaps | Task 10 | actual hex rows in `icons.c`'s byte layout, with ASCII art per row |
| **R18** `FAULT_ICON_FOR_BIT` remap for bits 1/2/5/13 | Task 10 | + a test asserting the whole §17.4 column and that no two bits share a glyph |
| **R18** `ICON_GPS`/`ICON_BLE` deleted | Task 10 | references checked first; `ICON_DISK` goes too (declared extension, justified by R18's own argument) |
| **R20** padding assertions, rule-5 counts preserved | Tasks 9, 11, 12 | per-site old→new tables; the linter is the stated check after each |
| **§4 gates** host/lint/firmware/DRAM | Task 15 | all four firmware builds incl. `test_apps/core_selftest`, DRAM delta with the arithmetic |
| **§4** `core_selftest` exclusion regex | Global Constraints | no new test file is added, so the regex is unchanged — stated explicitly, with the instruction to extend it if a task is split |
| **§4** bench checklist appended | Task 15 | three items |
| Spec/7b amendments | Task 16 | §12.3, §14.3, §20.2, §20.5, 7b §5/§6/§7 (ruling M7) |

Reviewer finding ids: **C1** T1 · **C2** T2 · **H1** T10 · **H2** T7 · **M1** T14 · **M2** T12 · **M3** T6 · **M4** T11 · **M5** T3 · **M6** T8 · **M7** T16 · **M8** T9/T11/T12 · **M9** T4 · **L1** T13 · **L2** T6 · **L3** T7 · **L4** T10 · **L5** T8. False claims **1,2** T5 · **3** T13+T11 · **4** T11 · **5** T13 · **6** T10 · **7** T6 · **8** *F4, not F1b* (recorded under File ownership) · **9** T4. Coverage gaps: negative `gps_us` through an exporter T1 · control characters through `jw_str` via `exp_json` T2 · wrong JSON type for the three bool fields T6 · `fault_strip`'s bit→icon table T10 · NMEA reserve boundary and `exp_pull` with `cap < win_len` T3. **No gaps.**

**2. Placeholder scan.** No "TBD", "TODO", "implement later", "add appropriate error handling", "write tests for the above" or "similar to Task N" anywhere. Every code step carries real C or real Python. Three places deliberately defer to a command rather than a literal, each with the command given: the ~47 `s_fb.dirty.valid` deletions in `test_screens.c` (`grep -c` first, report the count), the 20 `put_str(` call sites (`grep -n`; the compiler refuses to build until all are updated, so none can be missed), and the goldens' pixel content (regenerated by the documented workflow and eyeballed with the `pbm_ascii.py` given in Task 10 — a plan cannot contain a golden's bytes). Facts that could have been guesses were measured instead: the 113-byte JSON skeleton (jw probe), the 496-byte worst case, the 115-byte NMEA sentence, the 41-byte VENUE frame, and all three reviewer reproducers were compiled and run against `f4d4db2`.

**3. Name and type consistency.** Checked across tasks: `exp_skip_record(exp_t *)` (defined T1, consumed T2) · `exp_t.skipped` as `uint16_t`, saturating, read by `exp_json_finish`'s `"skipped":N` (T1) and asserted in T1/T2's tests · `EXP_JSON_HDR_BUF`/`_SKELETON`/`_ESC_MAX`/`_NUM_MAX` (T2 only) · `EXP_NMEA_RESERVE` (T3 only) · `json_tok_eq_raw`, `tok_copy`'s changed **source-length** return and `json_tok_str`'s compensating clamp (all T6) · `is_section`, `apply_benches`, `walk`'s 6-parameter signature and its matching call site (all T7) · `SBUF_END` and `put_str(p, end, s)`'s 3-parameter form at every site (T12) · `ui_fault_icon_for_bit` declared in `model.h`, defined in `screens_moto.c`, used by `fault_strip`/`fault_strip_left_x` and both T10 tests · `RING_POP_RETRIES`/`RING_ASSERT_CODE 0x0AD0` (T14; checked free — `0x0AC0` is `refresh_policy`, `0x0AF0`–`0x0AF3` are stats_fold/units/drag_cfg/menu_labels, and `blob.c` is assertion-free by design) · `fb_rect_t` deliberately survives `fb_t.dirty` because `fb_diff_rect` returns it (T11). Test-helper names were read from the files they are added to: `drain`/`feed_frame` (`test_exp_nmea_json.c`), `px`/`fb_max_ink_col`/`SNAP` (`test_screens.c`), `base()` (`test_refresh_policy.c`), `item_t` (`test_ring.c`), `s_bits`/`s_fb`/`FB_W`/`FB_H` (`test_ui.c`).

**One open risk flagged for the executor, not a gap:** Task 1 turns a *decode* failure from an abort (`-1`) into a counted skip (`0`). No existing test asserts `-1` for that case (checked), and emit/window failures still return `-1`, but the change widens `exp_feed`'s contract — which is why §14.3's `"skipped"` member (Task 16) is the plan's own documentation of it.
