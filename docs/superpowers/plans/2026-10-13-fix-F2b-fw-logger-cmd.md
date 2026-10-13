# F2b — Firmware logger + cmd Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the §12/§13.3 session lifecycle and record set real on every build (still-pause, `TIME_MAP`, `CALIB`, `gps_hz`, open/close on venue and mode change), add the missing §18.1/§18.4 ops (`TRACKS_GET/PUT`, `COREDUMP_GET/CLEAR` + console verbs), close the single-producer, cfg-lock, menu-lock and assertion findings in `logger.c`/`cmd.c`/`pipeline.c`/`ui.c`, and delete every false claim in the files this plan owns.

**Architecture:** Two pure, host-tested decision modules carry the new logic — `log_pause_t` (the §12.6 still-pause state machine) and `session_policy()` (the §12.1 open/close/reopen table) — so the IDF-laden tasks only wire them. The pipeline keeps sole ownership of the time base, the GPS profile and the fusion calibration and *hands* the logger records over `result_q` (`LOG_RES_TIMEMAP`, `LOG_RES_CALIB`); the logger keeps sole ownership of storage and the track-table writes (`LOGGER_PUT_TRACK`). `cmd.c` gains no static DRAM: `TRACKS_GET` streams one venue at a time through the existing `s_cw`/JSON buffer and `COREDUMP_GET` through the existing `s_io`, and the JSON buffer is overlaid on `LIST`'s session table (never live at the same time, §18.1 one request in flight), which pays for the bigger `.sum` lap accumulator.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2 (`esp_core_dump`, `esp_partition`, FreeRTOS queues + task notifications + one mutex), Unity host tests (`test/`), `esp_console`.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding; rulings R4, R5, R8, R10, R11, R20, event codes §3, gates §4, PR #101 M3/M4 per §5) and `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §4.4, §4.5, §12.1–12.7, §13.3, §15.2, §18.1, §18.4, §20.7. Evidence: `.superpowers/reviews/2026-10-13/{app-layer,drivers-build-tools,pr-101}.md`, `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` §3.3 #22–25, §7.1–7.3.

**Line numbers in this plan are from the F0 branch** `glyphs-drag-menu-closure` @ `df75497`, worktree `/Users/benji/projects/personal/lap-timer-worktrees/gdm` — **not** from `main` (f4d4db2). F0 merges first and already ships CREATE mode, `LOGGER_SAVE_TRACKS`, `tracks_save()`/`logger_load_tracks()` and the `lt_ipc.h`/`trk.h` comment fixes, so read that tree when following a citation.

**Runs after** F0, F1a and F2a merge. It consumes, and must not re-create: `EV_VENUE_LOST = 20` + `EV_COUNT` (F1a, design §3), `lap_cfg_from_user`/`lap_set_cfg` at `pipeline.c:1061` + the `CMD_CONFIG_RELOAD` arm (F1a owns those two call sites), `pipeline_sensors_ok()`/`pipeline_still_for_ms()` and the R9/R19 changes (F2a).

## Global Constraints

- Power of 10 on all firmware code: a function of > 20 code lines carries ≥ 2 `LT_ASSERT_*`/`CORE_ASSERT_*` (never abort, never side-effecting, never tautological — R20); every loop bounded by a constant or a table field; no recursion, heap, goto or function pointers; ≤ 60 code lines per function; `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0 under F4's stricter linter (statement-based line counting, blocking rule 3, `#if 0` exclusion).
- `python3 tools/lint/venue_announce.py` = 0.
- Zero warnings: `test/` host suite (`-Wall -Wextra -Werror -Wshadow -Wconversion`), gcc-16 sweep of `test/build/compile_commands.json`, and ccache-disabled clean builds of `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 ./build.sh moto_sim build`, `idf.py -C test_apps/core_selftest build` — all rc 0, 0 warnings. Fix at source; no `-Wno-*`, no pragma.
- `app/lt_proto.h` stays IDF-free (stdint/stddef/stdbool only): both host harnesses and the dev-kit compile it.
- Only the logger task mutates `/sessions` and `/tracks` and writes the `core/trk.h` user table after boot; `cmd` lists/reads only.
- The ui and pipeline tasks never block on the logger (never call `logger_request_sync`).
- `g_fix_ring`/`g_fused_ring` have exactly one producer: the pipeline task (R8 restores this).
- Free static DRAM must not fall: baseline `moto_sim` 4192 B, `moto_neo6m` 5616 B (design §4). This plan is net **negative** (Task 9 reports the measured figure).
- Never flash from a subagent (memory `feedback-flash-ready`); the controller runs bench gate `f2b-d1` with the user.
- **Checkpoint protocol — the executing session may `/clear` between tasks and must lose nothing.** Every task ends with a `CHECKPOINT` step that (a) appends one ledger line to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` (create the directory on Task 1; it is git-ignored like the other `.superpowers/` ledgers), (b) pushes the branch, and (c) records the verbatim resume prompt for the next task in that same ledger. The ledger — not this plan's checkboxes, and not conversation memory — is the single source of truth for "where are we". A resumed session's first two actions are: read the ledger, then `git log --oneline -5` to confirm the recorded sha is `HEAD`. Tasks are grouped into three sessions of ≤ 4 tasks; each session ends with a heavier checkpoint that re-runs the full host suite, both linters and one firmware build, so a `/clear` never carries an unverified tree forward.
- Every commit ends with the trailer this branch family uses (`git log -1 --format=%B` on the branch):
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>` then `Claude-Session: <this session's URL>`.

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `components/app/include/app/lt_ipc.h` | `LOGGER_PUT_TRACK`, `LOG_RES_TIMEMAP`/`LOG_RES_CALIB`, `log_request_t.gps_hz`, `CMD_DBG_LOGTEST`, `EV_VENUE_NOT_SAVED`, false-claim fixes | 1 |
| `components/app/include/app/cmd.h` | op codes 0x12/0x13/0x17/0x18, `CMD_JSON_CAP`, `cmd_tracks_payload()`, false-claim fix | 1 |
| `components/app/include/app/lt_proto.h` | the same four op codes mirrored for the dev-kit/host side | 1 |
| `components/app/include/app/pipeline.h` | `pipeline_speed_kmh()` | 1 |
| `components/app/include/app/logger.h` | `logger_submit_time_map()`, `logger_submit_calib()` | 1 |
| `components/app/sys/lt_nvs.c`, `include/app/lt_nvs.h` | **declared exception to F2a's ownership**: `lt_cfg_rmw_begin/end` + one mutex-creation line in `lt_nvs_init()` + two prototypes. Nothing else in either file. | 1 |
| `components/app/logger/log_pause.c`, `include/app/log_pause.h`, `test/test_log_pause.c` | §12.6 still-pause state machine (pure, no IDF) | 2 |
| `components/app/pipeline/session_policy.c`, `include/app/session_policy.h`, `test/test_session_policy.c` | §12.1 session open/close/reopen decision (pure, no IDF) | 3 |
| `components/app/logger/logger.c` | still-pause, `TIME_MAP`, `CALIB`, `gps_hz`, `s_seq` ruling, `s_id` clear, `LAP_ACC_CAP`, reserve guard in `tracks_save`, `LOGGER_PUT_TRACK`, asserts, false comments | 4 |
| `components/app/pipeline/pipeline.c` | session lifecycle on every build, `pipeline_speed_kmh`, TIME_MAP/CALIB producers, `CMD_DBG_LOGTEST`, `ui_post_venue` layout, asserts, false comments | 5 |
| `components/app/cmd/cmd.c` | `TRACKS_GET/PUT`, `COREDUMP_GET/CLEAR`, `op_read` L7, cfg RMW + only-on-change, `s_asm` overlay, asserts, false comments | 6 |
| `components/app/CMakeLists.txt` | `espcoredump` in `REQUIRES` (unclaimed by any other plan) | 6 |
| `components/drivers/export_serial/export_serial.c` | **only** the console verb table, the `dbg` family and the `OTA-*` token framing (L15/L16/L27); F2a owns `ota recv` + the logging-level lines | 7 |
| `components/app/ui/ui.c`, `include/app/ui.h` | **only** R11 speed read, R10 cfg-lock sites, PR M3 `NOT SAVED`, PR M4 layout-from-announce, the listed padding asserts, `ui.h` false claims (`ui.h` is unclaimed by any other plan) | 8 |
| `test/CMakeLists.txt`, `test_apps/core_selftest/main/CMakeLists.txt` | the two new host suites (+ the two pure sources on the target build) — **declared exception to F4's ownership**, two lines | 2, 3, 9 |
| `docs/bench/next-bench-day.md`, spec §12.1/§12.3/§12.4/§12.6/§13.3/§18.1/§18.4 + Appendix A | bench items + the amendments this plan's code makes true | 9 |

Nothing else is touched. In particular: `core/event.h`, `core/consts.h`, `core/trk.h`, `core/ui/*`, `sup.c`, `ota.c`, `status.c`, `lt_rtc.c`, `main/app_main.c` are other plans'.

---

## Session A — Tasks 1–3 (interfaces + the two pure modules)

Worktree: create with `superpowers:using-git-worktrees`, branch `fix-fw-logger-cmd` off merged `main`. Ledger: `.superpowers/sdd/2026-10-13-fix-F2b/progress.md`.

### Task 1 (SERIAL — must land first; everything else compiles against it): shared interfaces, op codes and the cfg RMW lock

**Files:**
- Modify: `components/app/include/app/lt_ipc.h`, `cmd.h`, `lt_proto.h`, `pipeline.h`, `logger.h`
- Modify: `components/app/sys/lt_nvs.c` (two new functions + one line in `lt_nvs_init`), `include/app/lt_nvs.h` (two prototypes)

**Interfaces (produced — every later task consumes these exact names):**

```c
/* app/lt_ipc.h */
#include "core/ses.h"            /* ses_time_map_t, ses_calib_t (added to the existing includes) */

/* log_req_type_t gains (7 stays retired): */
    LOGGER_PUT_TRACK       = 8,   /* R5 §18.1 0x13: install cmd.c's staged venue JSON, then save */

/* log_request_t: the trailing `uint8_t _pad2[1]` becomes a real field -- size stays 32 B. */
    uint8_t      gps_hz;      /* OPEN: gps_profile_t.max_rate_hz (§12.3 SESSION_HDR.gps_hz, R4) */

/* log_result_kind_t gains: */
    LOG_RES_TIMEMAP = 3,   /* u.tmap  -> TIME_MAP (§12.3 0x0C, §12.4), pipeline owns the time base */
    LOG_RES_CALIB   = 4,   /* u.calib -> SESSION_HDR calib block + a CALIB record (§12.3 0x0A) */
/* log_result_t.u gains: */
        ses_time_map_t tmap;
        ses_calib_t    calib;

/* command_type_t gains, and CMD_TYPE_LAST moves to it: */
    CMD_DBG_LOGTEST   = 11,  /* R8: arg16 = synthetic fixes the PIPELINE must push (dev builds) */
#define CMD_TYPE_LAST CMD_DBG_LOGTEST

/* PR #101 M3: a second bit in event_t.flags on EV_VENUE_FOUND, beside core/event.h's
 * EV_VENUE_ANNOUNCE_ONLY (0x01). It lives here and not in core/event.h only because core/event.h
 * is F1a's file in this wave; moving it there later is a pure relocation. Set ONLY by the logger,
 * on a ui-only post (never emit_event()), together with EV_VENUE_ANNOUNCE_ONLY -- so it can never
 * be read as a finished creation and can never replace a persistent one-shot. ui.c already bit-
 * TESTS the announce flag (its own M3 fix), which is exactly what makes a second bit safe. */
#define EV_VENUE_NOT_SAVED 0x02u

/* app/cmd.h */
enum { ..., CMD_TRACKS_GET=0x12, CMD_TRACKS_PUT=0x13, ..., CMD_COREDUMP_GET=0x17, CMD_COREDUMP_CLEAR=0x18, ... };
/* drv:M7: the producer-side cap of cmd.c's JSON/text assembly buffer, exported so a transport can
 * _Static_assert its own reassembly buffer is at least this big instead of silently truncating a
 * frame whose announced size and CRC then agree over cut bytes (export_serial.c's SER_ASM_MAX). */
enum { CMD_JSON_CAP = 3072 };
const char *cmd_tracks_payload(size_t *len_out);   /* LOGGER_PUT_TRACK's view of the staged JSON */

/* app/lt_proto.h -- the same four codes, for the dev-kit/host side (stdint-only header) */
enum { LT_OP_TRACKS_GET = 0x12, LT_OP_TRACKS_PUT = 0x13,
       LT_OP_COREDUMP_GET = 0x17, LT_OP_COREDUMP_CLEAR = 0x18 };

/* app/pipeline.h -- R11 */
/* Latest valid-fix ground speed in km/h (0 while no fix / after EV_FIX_LOST). The §20.7 menu lock
 * and the §20.5 MOVING glyph read THIS, not the 3 km/h EV_MOTION edge they used as a proxy. */
uint16_t pipeline_speed_kmh(void);

/* app/logger.h -- R4 producers, implemented beside logger_submit_lap/drag/set_venue */
void logger_submit_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality);
void logger_submit_calib(const ses_calib_t *c);   /* needs #include "core/ses.h" */

/* app/lt_nvs.h -- R10 */
/* Wrap a cfg load -> mutate -> save sequence (ui.c's menu writers, cmd.c's op_config_set) so a
 * CONFIG_SET landing between a peer's load and save is not silently reverted (app:M11). 200 ms
 * bounded take; false means the lock was not acquired and THE CALLER MUST SKIP THE WRITE (and log
 * it). Separate from the blob lock lt_cfg_load/lt_cfg_save take internally -- a FreeRTOS mutex is
 * not recursive, so reusing that one here would deadlock. */
bool lt_cfg_rmw_begin(void);
void lt_cfg_rmw_end(void);
```

- [ ] **Step 1: edit the five app headers** exactly as the Interfaces block above states. `_Static_assert(sizeof(log_request_t) == 32, ...)` must still hold — verify by reading the struct, not by building (`gps_hz` replaces `_pad2[1]` in place).

- [ ] **Step 2: add the lock to `lt_nvs.c`** (the declared exception; nothing else in the file changes):

```c
/* R10 (app:M11). Static storage, created in lt_nvs_init() beside s_blob_lock -- not lazily on
 * first use, which would itself race the two tasks (ui core 0, cmd core 0) this exists for. */
static SemaphoreHandle_t s_cfg_rmw;
static StaticSemaphore_t s_cfg_rmw_buf;
#define CFG_RMW_WAIT_MS 200

bool lt_cfg_rmw_begin(void)
{
    if (s_cfg_rmw == NULL) return true;   /* pre-init (boot_config, single-threaded): no contention */
    return xSemaphoreTake(s_cfg_rmw, pdMS_TO_TICKS(CFG_RMW_WAIT_MS)) == pdTRUE;
}

void lt_cfg_rmw_end(void)
{
    if (s_cfg_rmw != NULL) (void)xSemaphoreGive(s_cfg_rmw);
}
```

and, in `lt_nvs_init()` immediately after the existing `if (!s_blob_lock) ...` line (`lt_nvs.c:289`):

```c
    if (!s_cfg_rmw) s_cfg_rmw = xSemaphoreCreateMutexStatic(&s_cfg_rmw_buf);
    /* Postcondition: without it lt_cfg_rmw_begin() degrades to "always acquired" and R10's
     * guarantee silently disappears -- report it rather than lose the lock quietly. */
    LT_ASSERT_RET(s_cfg_rmw != NULL, NVS_ASSERT_CODE, -1);
```

- [ ] **Step 3: fix the false claims in the headers this task owns** (exact old → new):

| Site | Old (false) | New |
|---|---|---|
| `lt_ipc.h:4-5` | "The pipeline (3.4) is the single producer for the rings" | keep the sentence (R8 makes it true again) and append: `A dev-only second producer existed until R8 -- 'dbg logtest' pushed synthetic fixes from the console task; it now posts CMD_DBG_LOGTEST and the pipeline pushes them (design 2026-10-13 R8).` |
| `lt_ipc.h` cmd_q block ("The producers (ui/conn/power) land in later sessions; 3.4 creates the queue") | there is no conn or power task; ui and cmd are live producers | `Producers today: the ui task (menu), cmd.c (CMD_CONFIG_RELOAD after a persisted CONFIG_SET) and the console (CMD_DBG_LOGTEST, CMD_SIM_SCENARIO). There is no conn or power task (BLE is sub-project C, power is Plan 6).` **If F2a already rewrote this block, keep theirs and skip.** |
| `cmd.h:36-37` | "the OTA_* ops (§19.4) are the receive-side firmware-update state machine (session 5.4)" — reads as unimplemented | `Every op in this enum is implemented in cmd.c. §18.1's TIME_SYNC (0x30) is the only op still absent -- it needs a provisional-clock consumer and is filed against Plan 8 (design 2026-10-13 R5).` |

- [ ] **Step 4: compile-check the headers** — `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j` (builds `test_proto`, the only host consumer of these headers). Expected: PASS, 0 warnings.

- [ ] **Step 5: commit**

```bash
git add components/app/include/app components/app/sys/lt_nvs.c
git commit -m "f2b(t1): op codes 0x12/0x13/0x17/0x18, LOG_RES_TIMEMAP/CALIB, LOGGER_PUT_TRACK, CMD_DBG_LOGTEST, lt_cfg_rmw lock"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 1 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 2 (PARALLEL — with Task 3; both only need Task 1): `log_pause_t`, the §12.6 still-pause state machine (pure)

**Files:**
- Create: `components/app/logger/log_pause.c`, `components/app/include/app/log_pause.h`, `test/test_log_pause.c`
- Modify: `test/CMakeLists.txt` (one `add_core_test` block), `test_apps/core_selftest/main/CMakeLists.txt` (one `SRCS` entry — declared F4 exception)

`components/app/logger/log_pause.c` is picked up by the firmware automatically: `components/app/CMakeLists.txt` globs `*.c` recursively. No CMake edit there.

**Interfaces:**
- Consumes: nothing (pure C11: `stdbool.h`, `stdint.h`, `core/core.h` for `CORE_ASSERT_*`).
- Produces (`app/log_pause.h`):

```c
/* app/log_pause.h -- §12.6 "Logging pauses (no FIX_*/FUSED) when `still` for 10 s; resumes with a
 * FIX_KEY". Pure: no IDF, no FreeRTOS, no statics of its own, so the whole rule is host-testable
 * (test/test_log_pause.c) instead of living inline in logger.c's task loop (app:M6).
 *
 * LOG_PAUSE_STILL_MS is the Appendix A constant LOG_PAUSE_STILL_S = 10 in milliseconds; it lives
 * here because core/consts.h (F1a) and app/lt_consts.h (F2a) are other plans' files in this wave.
 * F4 adds the Appendix A row; moving the macro there later is a pure relocation. */
#define LOG_PAUSE_STILL_MS 10000u

typedef struct {
    uint32_t still_since_ms;   /* now_ms of the EV_STILL edge; meaningful only while `still` */
    bool     still;
    bool     paused;
} log_pause_t;

void log_pause_init(log_pause_t *p);
/* EV_STILL edge. Idempotent: a repeat edge does NOT restart the 10 s countdown. */
void log_pause_still(log_pause_t *p, uint32_t now_ms);
/* EV_MOTION edge. True when a pause was active, i.e. the caller must re-key the FIX/FUSED delta
 * codecs so logging resumes with a FIX_KEY (§12.4/§12.6). */
bool log_pause_motion(log_pause_t *p);
/* Per loop tick. True exactly once: on the tick the pause engages (so the caller logs it once). */
bool log_pause_tick(log_pause_t *p, uint32_t now_ms);
bool log_pause_active(const log_pause_t *p);
```

- [ ] **Step 1: write the failing test** — `test/test_log_pause.c`:

```c
#include "unity.h"
#include "app/log_pause.h"
void setUp(void) {} void tearDown(void) {}

static void test_init_not_paused(void)
{
    log_pause_t p;
    log_pause_init(&p);
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000));          /* not still: never engages */
    TEST_ASSERT_FALSE(log_pause_active(&p));
}
static void test_engages_at_ten_seconds_exactly_once(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 1000);
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS - 1));
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_TRUE (log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS));
    TEST_ASSERT_TRUE (log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS + 5000));   /* only once */
    TEST_ASSERT_TRUE (log_pause_active(&p));
}
static void test_repeat_still_does_not_restart(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 1000);
    log_pause_still(&p, 1000 + LOG_PAUSE_STILL_MS - 1);    /* a second EV_STILL must not defer it */
    TEST_ASSERT_TRUE(log_pause_tick(&p, 1000 + LOG_PAUSE_STILL_MS));
}
static void test_motion_resumes_and_demands_a_key(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0);
    (void)log_pause_tick(&p, LOG_PAUSE_STILL_MS);
    TEST_ASSERT_TRUE (log_pause_motion(&p));               /* was paused -> re-key */
    TEST_ASSERT_FALSE(log_pause_active(&p));
    TEST_ASSERT_FALSE(log_pause_motion(&p));               /* already moving -> nothing to re-key */
}
static void test_motion_before_the_pause_engages(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0);
    (void)log_pause_tick(&p, LOG_PAUSE_STILL_MS - 1);
    TEST_ASSERT_FALSE(log_pause_motion(&p));               /* never paused: no re-key needed */
    TEST_ASSERT_FALSE(log_pause_tick(&p, LOG_PAUSE_STILL_MS + 1));   /* and the countdown is gone */
}
static void test_wraps_with_the_32_bit_ms_clock(void)
{
    log_pause_t p; log_pause_init(&p);
    log_pause_still(&p, 0xFFFFF000u);
    TEST_ASSERT_FALSE(log_pause_tick(&p, 0x00000000u));    /* delta 4096 ms */
    TEST_ASSERT_TRUE (log_pause_tick(&p, 0xFFFFF000u + LOG_PAUSE_STILL_MS));
}
static void test_null_is_safe(void)
{
    log_pause_init(NULL); log_pause_still(NULL, 0);
    TEST_ASSERT_FALSE(log_pause_motion(NULL));
    TEST_ASSERT_FALSE(log_pause_tick(NULL, 0));
    TEST_ASSERT_FALSE(log_pause_active(NULL));
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_not_paused);
    RUN_TEST(test_engages_at_ten_seconds_exactly_once);
    RUN_TEST(test_repeat_still_does_not_restart);
    RUN_TEST(test_motion_resumes_and_demands_a_key);
    RUN_TEST(test_motion_before_the_pause_engages);
    RUN_TEST(test_wraps_with_the_32_bit_ms_clock);
    RUN_TEST(test_null_is_safe);
    return UNITY_END();
}
```

- [ ] **Step 2: wire the suite** — append to `test/CMakeLists.txt` (after the `test_sim_clock` block):

```cmake
# test_log_pause / test_session_policy (F2b): the §12.6 still-pause state machine and the §12.1
# session-lifecycle decision, both pure (no IDF, no statics) and so host-testable. Same ARGN
# pattern as test_sim_profile: link the app-component source directly and add the app include root.
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/app)
add_core_test(test_log_pause ${APP_DIR}/logger/log_pause.c)
target_include_directories(test_log_pause PRIVATE ${APP_DIR}/include)
```

- [ ] **Step 3: run to fail** — `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j`. Expected: `test_log_pause` fails to compile, `app/log_pause.h: No such file or directory`.

- [ ] **Step 4: implement** `components/app/include/app/log_pause.h` (the Interfaces block verbatim, with `#ifndef APP_LOG_PAUSE_H` guards, `#include <stdbool.h>`, `#include <stdint.h>`) and `components/app/logger/log_pause.c`:

```c
#include "app/log_pause.h"
#include "core/core.h"

#define LOG_PAUSE_ASSERT_CODE 0x0B21   /* P10 rule 5; logger.c's 0x0B20 + 1 (this file's own code) */

void log_pause_init(log_pause_t *p)
{
    CORE_ASSERT_VOID(p != NULL, LOG_PAUSE_ASSERT_CODE);
    p->still_since_ms = 0u;
    p->still  = false;
    p->paused = false;
}

void log_pause_still(log_pause_t *p, uint32_t now_ms)
{
    CORE_ASSERT_VOID(p != NULL, LOG_PAUSE_ASSERT_CODE);
    if (p->still) return;                    /* already counting down: a repeat edge changes nothing */
    p->still = true;
    p->still_since_ms = now_ms;
}

bool log_pause_motion(log_pause_t *p)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    bool was_paused = p->paused;
    p->still  = false;
    p->paused = false;
    return was_paused;                       /* §12.6: the caller re-keys so a FIX_KEY resumes */
}

bool log_pause_tick(log_pause_t *p, uint32_t now_ms)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    if (!p->still || p->paused) return false;
    /* Unsigned difference: correct across the 32-bit ms wrap (~49.7 days of uptime). */
    if ((uint32_t)(now_ms - p->still_since_ms) < LOG_PAUSE_STILL_MS) return false;
    p->paused = true;
    return true;
}

bool log_pause_active(const log_pause_t *p)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    return p->paused;
}
```

- [ ] **Step 5: run to pass** — `cmake --build test/build -j && ctest --test-dir test/build -R test_log_pause --output-on-failure`. Expected: `100% tests passed`, 7 cases.

- [ ] **Step 6: run it on the target toolchain too** — add the source to `test_apps/core_selftest/main/CMakeLists.txt`'s `idf_component_register(SRCS ...)` (declared F4 exception; the app include root is already on its path):

```cmake
                       "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/logger/log_pause.c"
```

then `source tools/idf-env.sh && idf.py -C test_apps/core_selftest build`. Expected: rc 0, 0 warnings (do not flash).

- [ ] **Step 7: commit**

```bash
git add components/app/logger/log_pause.c components/app/include/app/log_pause.h test/test_log_pause.c test/CMakeLists.txt test_apps/core_selftest/main/CMakeLists.txt
git commit -m "f2b(t2): §12.6 still-pause state machine (pure, host + target tested)"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 2 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 3 (PARALLEL — with Task 2): `session_policy()`, the §12.1 lifecycle decision (pure)

**Files:**
- Create: `components/app/pipeline/session_policy.c`, `components/app/include/app/session_policy.h`, `test/test_session_policy.c`
- Modify: `test/CMakeLists.txt`, `test_apps/core_selftest/main/CMakeLists.txt` (one line each, same pattern as Task 2)

**Interfaces:**
- Consumes: nothing (pure C11 + `core/core.h`).
- Produces (`app/session_policy.h`):

```c
/* app/session_policy.h -- §12.1 "A session starts at boot (or resume) and ends at PARK/SHUTDOWN or
 * when the venue changes. A new session also starts on mode change." (R4; audit §3.3 #24/#25.)
 *
 * Pure decision table, host-tested, so the pipeline's engine_cb/handle_set_mode only act on the
 * verdict. `open_now` is the pipeline's own advisory mirror of what it has asked the logger for --
 * the logger stays the authority (it ignores an OPEN while open and a CLOSE while closed), this
 * just stops the pipeline asking for a second session per venue scan or rotating a session that
 * never existed. */
typedef enum {
    SES_ACT_NONE   = 0,
    SES_ACT_OPEN   = 1,
    SES_ACT_CLOSE  = 2,
    SES_ACT_REOPEN = 3,   /* CLOSE{SES_END_NORMAL} then OPEN, in that queue order */
} ses_action_t;

typedef enum {
    SES_IN_VENUE_FOUND    = 0,   /* the ENGINE's own acquisition (event_t.flags has no announce bit) */
    SES_IN_VENUE_ANNOUNCE = 1,   /* the pipeline's ui-only announce (EV_VENUE_ANNOUNCE_ONLY) */
    SES_IN_VENUE_LOST     = 2,   /* EV_VENUE_LOST (F1a, design §3) */
    SES_IN_MODE_CHANGE    = 3,   /* CMD_SET_MODE, or a CMD_CONFIG_RELOAD that flips cfg.mode */
} ses_input_t;

ses_action_t session_policy(bool open_now, ses_input_t in);
```

- [ ] **Step 1: write the failing test** — `test/test_session_policy.c`:

```c
#include "unity.h"
#include "app/session_policy.h"
void setUp(void) {} void tearDown(void) {}

static void test_venue_found_opens_only_when_closed(void)
{
    TEST_ASSERT_EQUAL_INT(SES_ACT_OPEN, session_policy(false, SES_IN_VENUE_FOUND));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true,  SES_IN_VENUE_FOUND));
}
static void test_announce_only_never_touches_the_session(void)
{
    /* The CFG_GPS_SIM boot venue and the §15.3 RTC resume announce a venue the pipeline set
     * itself; the sim boot path opens its session explicitly and a resume continues the one that
     * is already open, so neither may open or close anything here. */
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(false, SES_IN_VENUE_ANNOUNCE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true,  SES_IN_VENUE_ANNOUNCE));
}
static void test_venue_lost_closes_only_when_open(void)
{
    TEST_ASSERT_EQUAL_INT(SES_ACT_CLOSE, session_policy(true,  SES_IN_VENUE_LOST));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE,  session_policy(false, SES_IN_VENUE_LOST));
}
static void test_mode_change_rotates_only_a_live_session(void)
{
    TEST_ASSERT_EQUAL_INT(SES_ACT_REOPEN, session_policy(true,  SES_IN_MODE_CHANGE));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE,   session_policy(false, SES_IN_MODE_CHANGE));
}
static void test_unknown_input_is_inert(void)
{
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(true, (ses_input_t)7));
    TEST_ASSERT_EQUAL_INT(SES_ACT_NONE, session_policy(false, (ses_input_t)200));
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_venue_found_opens_only_when_closed);
    RUN_TEST(test_announce_only_never_touches_the_session);
    RUN_TEST(test_venue_lost_closes_only_when_open);
    RUN_TEST(test_mode_change_rotates_only_a_live_session);
    RUN_TEST(test_unknown_input_is_inert);
    return UNITY_END();
}
```

- [ ] **Step 2: wire the suite** — append to `test/CMakeLists.txt` (reusing Task 2's `APP_DIR`):

```cmake
add_core_test(test_session_policy ${APP_DIR}/pipeline/session_policy.c)
target_include_directories(test_session_policy PRIVATE ${APP_DIR}/include)
```

- [ ] **Step 3: run to fail** — `cmake --build test/build -j`. Expected: `app/session_policy.h: No such file or directory`.

- [ ] **Step 4: implement** the header (Interfaces block verbatim, guards + `<stdbool.h>`) and `components/app/pipeline/session_policy.c`:

```c
#include "app/session_policy.h"
#include "core/core.h"

#define SES_POLICY_ASSERT_CODE 0x0B11   /* P10 rule 5; pipeline.c's 0x0B10 + 1 */

ses_action_t session_policy(bool open_now, ses_input_t in)
{
    switch (in) {
    case SES_IN_VENUE_FOUND:    return open_now ? SES_ACT_NONE   : SES_ACT_OPEN;
    case SES_IN_VENUE_LOST:     return open_now ? SES_ACT_CLOSE  : SES_ACT_NONE;
    case SES_IN_MODE_CHANGE:    return open_now ? SES_ACT_REOPEN : SES_ACT_NONE;
    case SES_IN_VENUE_ANNOUNCE: return SES_ACT_NONE;
    default: break;
    }
    /* An input outside the enum is a caller bug, not data: report it and stay inert. */
    core_assert_fail(SES_POLICY_ASSERT_CODE, __FILE__, __LINE__);
    return SES_ACT_NONE;
}
```

- [ ] **Step 5: run to pass** — `cmake --build test/build -j && ctest --test-dir test/build -R test_session_policy --output-on-failure`. Expected: `100% tests passed`, 5 cases. (`test_unknown_input_is_inert` fires the assert hook twice; `core`'s weak `core_assert_report` is a silent no-op in this binary, so it is not an error.)

- [ ] **Step 6: target toolchain** — add to `test_apps/core_selftest/main/CMakeLists.txt`'s `SRCS`:

```cmake
                       "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/pipeline/session_policy.c"
```

then `idf.py -C test_apps/core_selftest build`. Expected: rc 0, 0 warnings.

- [ ] **Step 7: commit**

```bash
git add components/app/pipeline/session_policy.c components/app/include/app/session_policy.h test/test_session_policy.c test/CMakeLists.txt test_apps/core_selftest/main/CMakeLists.txt
git commit -m "f2b(t3): §12.1 session-lifecycle decision table (pure, host + target tested)"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 3 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

- [ ] **SESSION A END CHECKPOINT** — run all three and record each result line in the ledger before any `/clear`:
  1. `cmake --build test/build -j && ctest --test-dir test/build --output-on-failure` → expect `100% tests passed` (49 suites: the 47 of PR #101 plus `test_log_pause` and `test_session_policy`).
  2. `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation && python3 tools/lint/venue_announce.py` → both rc 0, 0 violations.
  3. `source tools/idf-env.sh && ./build.sh moto_sim build` → rc 0, 0 warnings.

---

## Session B — Tasks 4–6 (the three task files; 4 and 5 are SERIAL against each other, 6 is PARALLEL with both)

Tasks 4 and 5 touch different files but one shared contract (`result_q` kinds, `log_request_t.gps_hz`), already frozen by Task 1 — so they *may* run in parallel worktrees; the simpler order is 4 then 5. Task 6 (`cmd.c`) shares no file with either and is genuinely parallel.

### Task 4 (SERIAL, needs Tasks 1 + 2): `logger.c` — still-pause, TIME_MAP, CALIB, `gps_hz`, `.sum` capacity, `LOGGER_PUT_TRACK`, asserts

**Files:**
- Modify: `components/app/logger/logger.c` (F0 lines: 92–93, 199, 206–227, 233–240, 288–372, 377–402, 518–557, 622–638, 640–651, 703–754, 779, 964–986, 1001–1009, 1033–1042)

**Interfaces:**
- Consumes: `app/log_pause.h` (Task 2); `LOGGER_PUT_TRACK`, `LOG_RES_TIMEMAP`, `LOG_RES_CALIB`, `log_request_t.gps_hz`, `cmd_tracks_payload()` (Task 1); `core/ses.h` `ses_encode_time_map`/`ses_encode_calib`/`SES_END_*`; `core/trk.h` `trk_user_add_json`/`trk_user_count`/`TRK_MAX_USER`.
- Produces: `logger_submit_time_map()`, `logger_submit_calib()` (bodies), and the `LOGGER_PUT_TRACK` rc contract Task 6 maps to error texts: **0** ok, **−5** table full, **−6** bad/empty venue JSON, **−7** installed in RAM but not persisted, **−1** internal anomaly.

- [ ] **Step 1: `.sum` lap capacity (L2) — the arithmetic, then the constant.** §12.5 promises the `.sum` carries the first **100** `LAP` frames. A `LAP` payload is `30 + 4·n_splits` (§12.3) and the frame wrapper is 5 B (`sync|type|len|…|crc16`), so a frame is `35 + 4·n_splits`. A **4-sector** layout has `n_splits = 4 + 1 = 5` → `35 + 20 = 55 B`; `100 × 55 = 5500 B`. Today's `LAP_ACC_CAP` 3072 holds 55 such frames (and the review's 65–87 at 0–3 sectors), so `acc_append` drops from lap 56 on. Replace `logger.c:92`:

```c
/* §12.5 .sum capacity (L2, audit §7.3). The .sum must carry the first 100 LAP frames. Worst case
 * per frame at a 4-sector layout -- the deepest the §20.4 board renders and the deepest any
 * bundled or on-device-created venue uses: payload 30 + 4*n_splits (§12.3) with n_splits = 4+1 = 5
 * -> 50 B, plus the 5 B frame wrapper = 55 B. 100 * 55 = 5500 B. A layout with more than 4 sectors
 * (LAP_MAX_SECTORS is 8 -> 71 B/frame -> 7100 B for 100) is deliberately NOT provisioned: those
 * laps spill past the cap, which §12.5 already allows ("laps beyond the first 100 are summarised
 * only in the .log"), and the spill is now reported once per session instead of silent. */
#define SUM_LAP_FRAME_B   55u
#define SUM_LAP_FRAMES    100u
#define LAP_ACC_CAP       (SUM_LAP_FRAME_B * SUM_LAP_FRAMES)   /* 5500 */
```

`BATCH_CAP` (3840) and `DRAG_ACC_CAP` (512) are unchanged, and so is `_Static_assert(TRK_USER_REC_MAX + 4u <= BATCH_CAP)` — the accumulators never stream through `s_batch`. `sum_write`'s `LT_ASSERT_RET(n <= LAP_ACC_CAP, …)` and `rebuild_sum`'s `LT_ASSERT_RET(s_lap_len <= LAP_ACC_CAP, …)` stay correct at the new value; `rebuild_sum`'s prose proof ("HDR 99 + VENUE 41 + LAP_ACC_CAP 3072 + DRAG_ACC_CAP 512 + END 14 = 3738 < the old 4096 cap") is now stale and is replaced by: `each section is streamed straight to the open fd, so no single buffer has to hold their sum -- only LAP_ACC_CAP/DRAG_ACC_CAP bound their own sections (acc_append).`

- [ ] **Step 2: report the spill instead of dropping silently.** `acc_append` (`logger.c:218`) returns `bool` (`false` = did not fit), and `handle_result`'s `LOG_RES_LAP` arm latches it once per session:

```c
static bool acc_append(uint8_t *acc, size_t *len, size_t cap, const uint8_t *frame, int n)
{
    LT_ASSERT_RET(acc != NULL, LOG_ASSERT_CODE, false);
    LT_ASSERT_RET(len != NULL, LOG_ASSERT_CODE, false);
    LT_ASSERT_RET(*len <= cap, LOG_ASSERT_CODE, false);   /* precondition: accumulator within its cap */
    if (n <= 0 || *len + (size_t)n > cap) return false;   /* beyond cap: kept in .log only (§12.5) */
    LT_ASSERT_RET(frame != NULL, LOG_ASSERT_CODE, false);
    memcpy(acc + *len, frame, (size_t)n);
    *len += (size_t)n;
    return true;
}
```

```c
        /* L2: the 100-frame cap is reached -- say so once, so a short .sum is never a surprise. */
        if (!acc_append(s_lap_acc, &s_lap_len, LAP_ACC_CAP, tmp, n) && !s_acc_full) {
            s_acc_full = true;
            ESP_LOGW(TAG, ".sum lap accumulator full at lap %u: later laps are .log-only (§12.5)",
                     (unsigned)lap->lap_no);
        }
```

with `static bool s_acc_full;` beside `s_sum_dirty` (cleared in `open_session` next to `s_sum_dirty = false;`). The `LOG_RES_DRAG` arm casts its call `(void)acc_append(...)` with the comment `DRAG_ACC_CAP holds 2 worst-case DRAG_RUN frames; a bench session never exceeds it and §12.5 allows the spill`.

- [ ] **Step 3: still-pause (R4, §12.6, app:M6).** Add `#include "app/log_pause.h"`, `static log_pause_t s_pause;`, and `log_pause_init(&s_pause);` on `logger_task`'s existing one-line init (`logger.c:961`). Then:

```c
/* drain_events (logger.c:745): the pause edges are tracked for EVERY event, not only while a
 * session is open -- a bike parked before the session opens must already be counting down. */
    while (xQueueReceive(g_evt_q, &ev, 0) == pdTRUE) {
        n++;
        LT_ASSERT_VOID(n <= EVT_Q_DEPTH, LOG_ASSERT_CODE);   /* rule 2: drain bounded by queue depth */
        if (ev.type == EV_STILL) {
            log_pause_still(&s_pause, now_ms());
        } else if (ev.type == EV_MOTION && log_pause_motion(&s_pause)) {
            /* §12.6 "resumes with a FIX_KEY": re-key both delta codecs so the first record after
             * the pause is a keyframe, not a delta against a sample from minutes ago. */
            ses_fix_state_init(&s_fix_st);
            ses_fused_state_init(&s_fused_st);
        }
        if (s_open) handle_event(&ev);
    }
```

```c
/* drain_rings (logger.c:720): the pause joins the two existing sample-suppression reasons. */
    bool suppress = s_samples_full || log_pause_active(&s_pause) ||
                    (sys_flags_get() & (1u << SYS_SAFE_MODE)) != 0;
```

```c
/* logger_task loop, right after the four drains: */
        if (log_pause_tick(&s_pause, now)) ESP_LOGI(TAG, "logging paused: still %u s (§12.6)",
                                                    (unsigned)(LOG_PAUSE_STILL_MS / 1000u));
```

- [ ] **Step 4: TIME_MAP (R4, §12.4) + CALIB (R4, §12.3) + `gps_hz`.** Add to the statics:

```c
static ses_calib_t s_calib;   /* latest block from the pipeline (LOG_RES_CALIB); zero until Plan 8 */
```

`handle_result` gains two arms (and `submit`'s bound assert at `logger.c:1006` becomes `r->kind <= LOG_RES_CALIB`):

```c
    } else if (res->kind == LOG_RES_TIMEMAP) {
        /* §12.4: the pipeline owns the time base, so it produces every (mono, gps, quality)
         * triple -- at session start, on each tb_quality() change and every 60 s -- and this task
         * only writes it. .log-only (§12.5 lists the .sum's sections). */
        n = ses_encode_time_map(res->u.tmap.mono_us, res->u.tmap.gps_us, res->u.tmap.quality,
                                tmp, sizeof tmp);
        batch_append(tmp, n);
    } else if (res->kind == LOG_RES_CALIB) {
        /* §12.3: the header's calib block AND the CALIB record (0x0A). The pipeline posts this
         * right after each OPEN, so the first session of a boot has a zero block in its .log HDR
         * frame (already flushed) with the authoritative CALIB record immediately after it; the
         * .sum HDR is rebuilt from s_hdr below, and every later session's HDR is right from byte
         * one (s_calib persists across sessions). Values are fusion defaults until Plan 8 (#93). */
        s_calib = res->u.calib;
        memcpy(s_hdr.r_e4,  s_calib.r_e4,  sizeof s_hdr.r_e4);
        memcpy(s_hdr.gbias, s_calib.gbias, sizeof s_hdr.gbias);
        s_hdr.calib_flags = s_calib.calib_flags;
        n = ses_encode_calib(&s_calib, tmp, sizeof tmp);
        batch_append(tmp, n);
        s_sum_dirty = true;
    }
```

In `open_session`, replace `logger.c:329-330` (**both false comments die here**):

```c
    s_hdr.gps_hz       = req->gps_hz;          /* §12.3: the real GPS rate, from gps_profile_t.max_rate_hz */
    s_hdr.start_gps_us = req->gps_us;
    memcpy(s_hdr.r_e4,  s_calib.r_e4,  sizeof s_hdr.r_e4);     /* §12.3 calib block: the last block the
    memcpy(s_hdr.gbias, s_calib.gbias, sizeof s_hdr.gbias);     * pipeline posted (LOG_RES_CALIB), zeros
    s_hdr.calib_flags = s_calib.calib_flags;                    * before the first one of a boot */
```

Add the two producers beside `logger_set_venue` (`logger.c:1033`):

```c
void logger_submit_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality)
{
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_TIMEMAP;
    r.u.tmap.mono_us = mono_us;
    r.u.tmap.gps_us  = gps_us;
    r.u.tmap.quality = quality;
    submit(&r);
}

void logger_submit_calib(const ses_calib_t *c)
{
    LT_ASSERT_VOID(c != NULL, LOG_ASSERT_CODE);
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_CALIB;
    r.u.calib = *c;
    submit(&r);
}
```

- [ ] **Step 5: TIME_MAP must land directly after the HDR — reorder the drains.** In `logger_task`'s loop swap two lines so the order is `drain_requests(); drain_results(); drain_rings(); drain_events();` with:

```c
        /* R4/§12.4: drain_results() runs BEFORE the sample rings so the TIME_MAP the pipeline
         * posts right after an OPEN is the first record after the SESSION_HDR, as §12.4 requires
         * ("at session start"). Records are individually timestamped, so moving LAP/SECTOR/
         * DRAG/VENUE/TIME_MAP/CALIB ahead of FIX/FUSED inside one batch changes interleaving only
         * -- never a value, never the .sum, never a decode. */
```

- [ ] **Step 6: `s_seq` saturation (M13) — refuse, do not append.** Replace `logger.c:314`:

```c
    /* M13 (audit §7.3): at the 256th open of one boot s_id would stop advancing and this open
     * would re-open the PREVIOUS session's .log with STO_APPEND, concatenating two sessions into
     * one file while rebuild_sum overwrote the shared .sum from empty accumulators -- losing the
     * first session's laps from the summary §12.6 promises is never evicted. R4 makes mode changes
     * and venue changes rotate sessions, so this is now reachable: refuse the open instead.
     * The boot counter advances on the next reset, which is the only recovery and the honest one. */
    if (s_seq >= 0xFFu) {
        (void)errlog_add(E_STO_WRITE, 0xFFu);
        ESP_LOGE(TAG, "session sequence exhausted (255 opens this boot): not opening");
        return;
    }
    s_seq++;
```

- [ ] **Step 7: clear `s_id` on close (L1).** At the end of `close_session`, after the existing `ESP_LOGI(TAG, "session %s closed …")` line and before `return 0;`:

```c
    /* L1: evict_scan_oldest() excludes "%s.log" built from s_id, so a populated s_id after the
     * close kept the last closed session's .log permanently out of eviction -- with one log left,
     * eviction reported nothing_left and latched SYS_STORAGE_FULL instead of reclaiming it.
     * Logged above first: this must not blank the id in that line. */
    s_id[0] = '\0';
```

`eviction_check`'s `LT_ASSERT_VOID(!s_open || s_id[0] != '\0', …)` still holds (`s_open` is already false here), and `rebuild_sum`'s `if (s_id[0] == 0) return false;` guard now correctly refuses a `LOGGER_REBUILD_SUMMARY` for a closed session (that arm is already gated on `s_open`).

- [ ] **Step 8: honour the reserve in `tracks_save` (PR #101 M3, second half).** Factor the reserve test out of `open_session` (`logger.c:298-302`, which also brings that function back under RULE-4) and use it in both:

```c
/* §12.7's hard floor. open_session() refuses below it (after one eviction pass); tracks_save()
 * refuses outright (PR #101 M3: it was the one writer in this file that ignored the reserve and
 * could add ~10 KB of .tmp on a filesystem the project keeps above 5 % -- memory
 * `project-lt-storage-listing-cost`: never let the fs reach 0 free). */
static bool below_reserve(sto_info_t *si)
{
    LT_ASSERT_RET(si != NULL, LOG_ASSERT_CODE, false);
    if (sto_info(si) != 0 || si->total_kb == 0) return false;   /* unknown: do not block the write */
    return si->free_kb < (si->total_kb * STO_RESERVE_PCT) / 100u;
}
```

and at the top of `tracks_save()`, before the `do_write()` flush:

```c
    sto_info_t si;
    if (below_reserve(&si)) {
        (void)errlog_add(E_STO_FULL, 1u);   /* arg 1 = the tracks writer (0 = open_session) */
        ESP_LOGE(TAG, "tracks save refused: %u/%u KB free is below the %u%% reserve",
                 (unsigned)si.free_kb, (unsigned)si.total_kb, (unsigned)STO_RESERVE_PCT);
        return -2;
    }
```

- [ ] **Step 9: `LOGGER_PUT_TRACK` (R5).** Add `#include "app/cmd.h"` and, beside `tracks_save()`:

```c
/* R5 (§18.1 0x13 TRACKS_PUT). The install runs HERE because core/trk.h's ownership comment makes
 * the user table writer-exclusive to boot, the pipeline and (since #97) this task -- a
 * trk_user_add() on the console task would be a fourth, unlocked writer.
 *
 * Ownership hand-off: op_tracks_put() (cmd.c) copies the venue JSON into its own buffer and then
 * BLOCKS in logger_request_sync() until this returns, so cmd_tracks_payload() points at bytes that
 * cannot change for exactly this call (§18.1 one request in flight; the console task is
 * cmd_dispatch's only caller) and ownership returns to cmd.c with the rc below. Nothing is
 * retained after the return.
 *
 * rc: 0 ok; -5 the table was already full (trk_user_add_json's own TRK_MAX_USER bound -- note a
 * same-id replace is refused too when full, which is the honest answer while no delete op exists);
 * -6 the JSON did not parse or nothing was staged; -7 installed in RAM but not persisted (the
 * caller tells the operator so, rather than reporting a success that a reboot erases). */
static int put_track(void)
{
    size_t n = 0;
    const char *json = cmd_tracks_payload(&n);
    LT_ASSERT_RET(json != NULL, LOG_ASSERT_CODE, -1);
    if (n == 0) return -6;
    bool was_full = (trk_user_count() >= TRK_MAX_USER);
    uint16_t vid = 0;
    char err[96];
    err[0] = '\0';
    if (trk_user_add_json(json, n, &vid, err, sizeof err) != 0) {
        ESP_LOGW(TAG, "tracks put: %s", err[0] ? err : "parse failed");
        return was_full ? -5 : -6;
    }
    if (tracks_save() != 0) return -7;
    ESP_LOGI(TAG, "tracks put: venue %u installed and persisted", (unsigned)vid);
    return 0;
}
```

and one arm in `handle_request` (`logger.c:632`): `case LOGGER_PUT_TRACK: return put_track();`.

- [ ] **Step 10: `LOGGER_SAVE_TRACKS` failure reaches the rider (PR #101 M3, first half).** `create_finish_if_active()` cannot block (lt_ipc.h forbids the pipeline calling `logger_request_sync`), so the logger reports the failure itself, to the ui, directly:

```c
/* PR #101 M3: a creation that finished while storage was dead or full showed the rider the VENUE
 * one-shot with the new name and no hint that the venue lives in RAM only until the next reboot.
 * The requester is NULL (fire-and-forget, by design: the pipeline must never block on this task),
 * so the rc goes nowhere -- post the bad news to the ui instead. EV_VENUE_FOUND carrying BOTH
 * EV_VENUE_ANNOUNCE_ONLY (core/event.h: never a finished creation, never replaces a persistent
 * one-shot) and EV_VENUE_NOT_SAVED (app/lt_ipc.h). Straight to g_ui_evt_q, never emit_event():
 * ui-only, exactly like EV_CFG_CHANGED/EV_LAP_RESET. arg16 = the venue id the save was for. */
static void notify_tracks_not_saved(uint16_t venue_id)
{
    if (g_ui_evt_q == NULL) return;
    event_t ev = { .type = EV_VENUE_FOUND,
                   .flags = (uint8_t)(EV_VENUE_ANNOUNCE_ONLY | EV_VENUE_NOT_SAVED),
                   .arg16 = venue_id, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "tracks not-saved: ui queue full");
}
```

and `handle_request`'s save arm becomes:

```c
    case LOGGER_SAVE_TRACKS: {
        int rc = tracks_save();
        if (rc != 0) notify_tracks_not_saved(req->venue_id);
        return rc;
    }
```

(The pipeline now fills `req.venue_id` — Task 5, Step 6.)

- [ ] **Step 11: the `EV_COUNT` assert (design §3) and the side-effect/padding asserts (M3/M4, R20).** Exact sites in `logger.c`:

| Site | Old | New |
|---|---|---|
| `:644` `handle_event` | `LT_ASSERT_VOID(ev->type <= EV_FAULT, …)` | `LT_ASSERT_VOID(ev->type < EV_COUNT, LOG_ASSERT_CODE);` with `/* design 2026-10-13 §3: the logged EV_* set is append-only and EV_COUNT (core/event.h, F1a) is its single bound -- EV_FAULT stopped being the last code when EV_VENUE_LOST was added */` |
| `:643` `handle_event` | `LT_ASSERT_VOID(s_open, …)` — tautology, the sole caller already gated on `s_open` (M4) | delete; replace with `LT_ASSERT_VOID(ev->gps_us >= 0 && ev->mono_us >= 0, LOG_ASSERT_CODE);   /* both are written into the EVENT record (§12.3) as i64 epoch/monotonic values */` |
| `:709` `drain_results` | `LT_ASSERT_VOID(n++ < RESULT_Q_DEPTH, …)` — the increment IS the bound (M3) | `n++;` then `LT_ASSERT_VOID(n <= RESULT_Q_DEPTH, LOG_ASSERT_CODE);` |
| `:725` `drain_rings` fix loop | `LT_ASSERT_VOID(fix_n++ < FIX_RING_CAP, …)` | `fix_n++;` then `LT_ASSERT_VOID(fix_n <= FIX_RING_CAP, LOG_ASSERT_CODE);` |
| `:736` `drain_rings` fused loop | `LT_ASSERT_VOID(fused_n++ < FUSED_RING_CAP, …)` | `fused_n++;` then `LT_ASSERT_VOID(fused_n <= FUSED_RING_CAP, LOG_ASSERT_CODE);` |
| `:751` `drain_events` | `LT_ASSERT_VOID(n++ < EVT_Q_DEPTH, …)` | already rewritten in Step 3 (`n++;` then `LT_ASSERT_VOID(n <= EVT_Q_DEPTH, …)`) |
| `:477` `handle_result` | `LT_ASSERT_VOID(s_open, …)` is at `:657` as a plain `if (!s_open) return;` — fine, no change | — |
| `:485`, `:570` | `LT_ASSERT_RET(sizeof s_batch == BATCH_CAP, …)` — tautology over a compile-time fact (M4) | replace both with `_Static_assert(sizeof(s_batch) == BATCH_CAP, "tracks_save/load stream through the whole scratch batch");` at file scope, and give each function a real precondition instead: `LT_ASSERT_RET(s_batch_len == 0, LOG_ASSERT_CODE, false);   /* the scratch batch must be free: a session batch would be clobbered */` |
| `:798` `evict_scan_oldest` | `LT_ASSERT_RET(strlen(e->oldest) < STO_NAME_MAX, …)` — tautology over `snprintf`'s own bound | `LT_ASSERT_RET(e->curlog[0] == '\0' || strcmp(e->oldest, e->curlog) != 0, LOG_ASSERT_CODE, false);   /* postcondition: the OPEN session's .log is never a candidate */` |

- [ ] **Step 12: build and lint** — `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build && python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation`. Expected: rc 0, 0 warnings, 0 violations. (`logger_submit_time_map`/`logger_submit_calib` are unreferenced until Task 5 — they are non-static with prototypes in `logger.h`, so `-Wmissing-prototypes`/`-Wunused` stay quiet.)

- [ ] **Step 13: commit**

```bash
git add components/app/logger/logger.c
git commit -m "f2b(t4): logger -- §12.6 still-pause, §12.4 TIME_MAP, §12.3 CALIB + gps_hz, 100-lap .sum, s_seq refuse, s_id clear, LOGGER_PUT_TRACK, reserve in tracks_save"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 4 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 5 (SERIAL, needs Tasks 1 + 3 + 4): `pipeline.c` — session lifecycle on every build, `pipeline_speed_kmh`, TIME_MAP/CALIB producers, `CMD_DBG_LOGTEST`

**Files:**
- Modify: `components/app/pipeline/pipeline.c` (F0 lines: 150–154, 213–231, 402–437, 478–523, 590–631, 738–759, 782–823, 896–905, 994–1028, 1032–1055, 1109–1168, 1170–1210, 1339–1343)

**Interfaces:**
- Consumes: `session_policy()`/`ses_action_t`/`ses_input_t` (Task 3); `LOGGER_PUT_TRACK`-free but `log_request_t.gps_hz`, `CMD_DBG_LOGTEST` (Task 1); `logger_submit_time_map()`, `logger_submit_calib()` (Task 4); `EV_VENUE_LOST`, `EV_COUNT` (F1a); `SES_END_NORMAL` (`core/ses.h`); `fus_calib()` + `fus_calib_to_ses()` (`core/fus.h`); `tb_quality()` + `tb_mono_to_gps()` (`core/tb.h`).
- Produces: `uint16_t pipeline_speed_kmh(void)` (body — Task 8 reads it); `ui_post_venue(uint16_t venue_id, uint16_t layout_id, int64_t gps_us)` with `layout_id` carried in `event_t.arg32` (Task 8 reads it); `LOGGER_SAVE_TRACKS` requests now carry `req.venue_id` (Task 4 reads it).

- [ ] **Step 1: R11 — publish the real speed.** Add beside `s_gps_seen` (`pipeline.c:152`):

```c
/* R11 (orchestrator §3.2 #15): the §20.7 menu lock is specified at 10 km/h, but the ui had no
 * per-fix speed and used the EV_MOTION edge (MOVING_SPEED_KMH = 3) as a proxy -- locking the menu
 * from 3 km/h and lighting the §20.5 MOVING glyph there too. Written on every VALID fix by this
 * task and read by the ui task through pipeline_speed_kmh(); a single aligned 16-bit load/store, so
 * volatile is the whole synchronisation it needs (same shape as s_gps_seen). Zeroed on the
 * EV_FIX_LOST edge: a stale speed must never keep the menu locked while the GPS is gone. */
static volatile uint16_t s_speed_kmh;
```

in `on_fix`'s `if (valid)` arm (`pipeline.c:598`), after `s_cur_speed_cms`:

```c
        double kmh = (double)fix->gspeed_mms * MMS_TO_KMH;
        s_speed_kmh = (kmh <= 0.0) ? 0u : ((kmh >= 65535.0) ? 65535u : (uint16_t)kmh);
```

in the invalid arm, on the fix-lost edge (`pipeline.c:617`): `if (s_invalid_run == FIX_LOST_COUNT) { s_fix_lost = true; s_speed_kmh = 0u; emit_simple(EV_FIX_LOST, fix->gps_us, fix->mono_us); }`

and beside `pipeline_gps_seen` (`pipeline.c:1339`):

```c
uint16_t pipeline_speed_kmh(void)
{
    return s_speed_kmh;
}
```

- [ ] **Step 2: the §12.1 session lifecycle, on every build (R4; audit §3.3 #24/#25).** Add the statics and helpers above `engine_cb` (after `create_finish_if_active`):

```c
/* R4 (§12.1/§13.3; audit §3.3 #25: on moto_neo6m NO session was ever opened, so F8 was unmet on
 * the production build -- LOGGER_OPEN_SESSION was sent only from the #if CFG_GPS_SIM block and by
 * `dbg logtest`). THIS task owns the lifecycle on every build now. §13.3's "commanded by the power
 * task" is amended (there is no power task; Plan 6).
 *
 * s_ses_* mirror what this task has ASKED the logger for. The logger stays the authority -- it
 * ignores an OPEN while open and a CLOSE while closed -- so this mirror is advisory only; its job
 * is to stop a venue rescan asking for a second session and a mode flip rotating a session that
 * never existed (session_policy(), app/session_policy.h). */
static bool     s_ses_open;
static uint16_t s_ses_venue_id, s_ses_layout_id;
static uint8_t  s_gps_hz;              /* gps_profile_t.max_rate_hz; §12.3 SESSION_HDR.gps_hz */

static void session_send(uint8_t type, uint8_t reason, uint16_t venue_id, uint16_t layout_id,
                         int64_t gps_us)
{
    if (g_log_req_q == NULL) return;
    log_request_t req = { .type = type, .mode = s_mode, .reason = reason, .gps_us = gps_us,
                          .venue_id = venue_id, .layout_id = layout_id, .gps_hz = s_gps_hz };
    /* Fire-and-forget with the same bounded 100 ms send every other request from this task uses
     * (create_finish_if_active): this task must never block on the logger (app/lt_ipc.h). A drop
     * leaves a trace on the device, not just in the log. */
    if (xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        (void)errlog_add(E_STO_WRITE, 0);
        ESP_LOGW(TAG, "session request %u dropped (log queue full)", (unsigned)type);
        return;
    }
    logger_notify();
}

/* §12.4 "TIME_MAP ... at session start, whenever tb quality changes, and every 60 s". This task
 * owns the time base, so it is the only producer that can make a (mono, gps, quality) triple; the
 * logger just writes what it is handed. gps_us > 0 pins the pair to a real fix; 0 asks tb for it. */
#define TIMEMAP_INTERVAL_US (60LL * 1000000)
static int64_t s_last_timemap_us;
static uint8_t s_tb_q_last = 0xFFu;      /* 0xFF = no TIME_MAP posted yet this boot */

static void post_time_map(int64_t gps_us)
{
    int64_t mono = esp_timer_get_time();
    LT_ASSERT_VOID(mono >= 0, PIPE_ASSERT_CODE);           /* esp_timer is monotonic/non-negative */
    uint8_t q = tb_quality(&s_tb);
    LT_ASSERT_VOID(q <= 2u, PIPE_ASSERT_CODE);             /* core/tb.h: 0 unlocked, 1 min-filter, 2 PPS */
    logger_submit_time_map(mono, (gps_us > 0) ? gps_us : tb_mono_to_gps(&s_tb, mono), q);
    s_tb_q_last = q;
    s_last_timemap_us = mono;
}

/* §12.3's SESSION_HDR calib block and the CALIB record (0x0A), from the live fusion calibration.
 * Values are fus_calib_defaults() until Plan 8 captures a real orientation (#93) -- the point of
 * writing them now is that the block and the record exist and are decodable, instead of the
 * permanently-zero field logger.c used to ship with a "lands in 3.4" comment on it. */
static void post_calib(void)
{
    const fus_calib_t *c = fus_calib(&s_fus);
    LT_ASSERT_VOID(c != NULL, PIPE_ASSERT_CODE);
    ses_calib_t out;
    fus_calib_to_ses(c, &out);
    logger_submit_calib(&out);
}

static void session_open(uint16_t venue_id, uint16_t layout_id, int64_t gps_us)
{
    s_ses_venue_id  = venue_id;
    s_ses_layout_id = layout_id;
    session_send(LOGGER_OPEN_SESSION, SES_END_NORMAL, venue_id, layout_id, gps_us);
    s_ses_open = true;
    /* Both ride on result_q, which the logger drains AFTER log_req_q in the same loop pass (Task
     * 4 Step 5), so the HDR is already written when these two records are encoded -- §12.4's "at
     * session start" is exact, not approximate. */
    post_time_map(gps_us);
    post_calib();
}

static void session_close(uint8_t reason, int64_t gps_us)
{
    session_send(LOGGER_CLOSE_SESSION, reason, 0, 0, gps_us);
    s_ses_open = false;
}

/* §12.1 "A new session also starts on mode change". Queue order does the work: log_req_q is FIFO
 * and the logger handles both requests in one drain, so CLOSE is finalised (END + .sum) before
 * OPEN allocates the next sequence number -- no synchronous wait, no window where both are open. */
static void session_rotate(void)
{
    if (session_policy(s_ses_open, SES_IN_MODE_CHANGE) != SES_ACT_REOPEN) return;
    int64_t gps_us = (s_last_fix.valid != 0u) ? s_last_fix.gps_us : 0;
    uint16_t venue = s_ses_venue_id, layout = s_ses_layout_id;
    session_close(SES_END_NORMAL, gps_us);
    session_open(venue, layout, gps_us);
}
```

- [ ] **Step 3: wire the lifecycle into `engine_cb`** (`pipeline.c:426-436`):

```c
    switch (ev->type) {
    case EV_SECTOR:
        LT_ASSERT_VOID(ev->arg16 <= LAP_MAX_SECTORS, PIPE_ASSERT_CODE);   /* engine sector idx in range */
        ESP_LOGI(TAG, "  sector %u  split %lu ms  delta %ld ms", (unsigned)ev->arg16,
                 (unsigned long)ev->arg32, (long)(int32_t)ev->arg32b);
        s_rtc_save_due = true;     /* §15.3: the crossed sector is now the resume point */
        break;
    case EV_DRAG_DONE:     on_drag_done(); break;
    case EV_VENUE_FOUND:
        /* R4: the engine just acquired (or finished creating) a venue -- open the session §12.1
         * promises. The ANNOUNCE-only kind never reaches engine_cb (it goes straight to
         * g_ui_evt_q, ui_post_venue()), so every event here is a real acquisition. */
        if (session_policy(s_ses_open, SES_IN_VENUE_FOUND) == SES_ACT_OPEN)
            session_open(ev->arg16, 0u, ev->gps_us);
        else
            s_ses_venue_id = ev->arg16;          /* same session, new venue: keep the mirror honest */
        create_finish_if_active(ev->arg16);      /* #97: persist a just-created venue */
        break;
    case EV_LAYOUT_LOCKED: {
        /* The layout is only known one event later than the venue, so this is where the .sum's
         * VENUE record and the HDR's layout_id get their real values -- previously only the
         * CFG_GPS_SIM boot path ever called logger_set_venue(), so a real-GPS .sum had none. */
        s_ses_layout_id = ev->arg16;
        const trk_venue_t *v = trk_get(s_ses_venue_id);
        logger_set_venue(s_ses_venue_id, ev->arg16, (v != NULL) ? v->name : NULL);
        break;
    }
    case EV_VENUE_LOST:
        /* §12.1 "ends ... when the venue changes" (F1a emits this, design §3). */
        if (session_policy(s_ses_open, SES_IN_VENUE_LOST) == SES_ACT_CLOSE)
            session_close(SES_END_NORMAL, ev->gps_us);
        break;
    default: break;               /* EV_LAP_COMPLETE handled above, before emit_event() */
    }
```

- [ ] **Step 4: rotate on a mode change.** In `handle_set_mode` (`pipeline.c:899`), after `stats_reset();`: `session_rotate();   /* §12.1: a mode change starts a new session */`. In `pipeline_reload_cfg` (`pipeline.c:753`), extend the mode-changed arm: `if (new_mode != s_mode) { stats_reset(); cancel_active_create(); s_mode = new_mode; session_rotate(); } else { s_mode = new_mode; }` — `s_mode` must be assigned **before** `session_rotate()` so the reopened session's `SESSION_HDR.mode` is the new mode (the existing `s_mode = new_mode;` line below the `if` is removed). Keep the existing comment and append: `R4: and rotates the session, so the .log/.sum never mix two modes.`

- [ ] **Step 5: periodic TIME_MAP.** In `pipeline_task`'s loop, after the IMU block and before `g_hb[HB_PIPELINE]++`:

```c
        /* §12.4: on a tb quality change, and otherwise every 60 s, while a session is open. */
        if (s_ses_open) {
            int64_t mono = esp_timer_get_time();
            if (tb_quality(&s_tb) != s_tb_q_last || (mono - s_last_timemap_us) >= TIMEMAP_INTERVAL_US)
                post_time_map(0);
        }
```

- [ ] **Step 6: `create_finish_if_active` carries the venue id (PR #101 M3).** Change its signature to `static void create_finish_if_active(uint16_t venue_id)` and its request to `log_request_t req = { .type = LOGGER_SAVE_TRACKS, .venue_id = venue_id };` with:

```c
 * PR #101 M3: the request now names the venue it is saving, so a save the logger cannot land
 * reaches the rider as a "NOT SAVED" card instead of a silent success that a reboot erases
 * (logger.c's notify_tracks_not_saved()). Still fire-and-forget (requester NULL): this task must
 * never block on the logger.
```

- [ ] **Step 7: `gps_hz` from the profile (R4).** In `pipeline_init_drivers` (`pipeline.c:1036`), inside the `if (gps_ok)` arm after `gps_configure`:

```c
        s_gps_hz = prof->max_rate_hz;      /* R4/§12.3: the real rate every SESSION_HDR carries */
        LT_ASSERT_VOID(s_gps_hz <= 25u, PIPE_ASSERT_CODE);   /* §12.6's profiles top out at 25 Hz */
```

- [ ] **Step 8: the sim boot block uses the shared helpers** (`pipeline.c:1148-1160`) — the inline `log_request_t` disappears:

```c
            uint16_t layout_id = (v->n_layouts > 0) ? v->layouts[0].id : 0;
            session_open(v->id, layout_id, 0);          /* R4: the one lifecycle owner, every build */
            logger_set_venue(v->id, layout_id, v->name);
            ESP_LOGI(TAG, "venue \"%s\" (id %u) armed from sim capture", v->name, (unsigned)v->id);
            ui_post_venue(v->id, layout_id, 0);
```

`session_open` here runs before `pipeline_init_drivers()`, so `s_gps_hz` is still 0 for the sim's *boot* session; move `pipeline_init_drivers();` (`pipeline.c:1167`) to the **top** of `pipeline_init()`, immediately after `tb_init(&s_tb);`, with: `/* R4: hoisted above the venue/session block so s_gps_hz (gps_profile_t.max_rate_hz) is known before the first LOGGER_OPEN_SESSION -- §12.3's SESSION_HDR.gps_hz was permanently 0. gps_init/imu_init touch no engine state, so the order is otherwise immaterial. */`

- [ ] **Step 9: PR #101 M4 — the announce carries the locked layout.** Change the forward declaration (`pipeline.c:267`) and definition (`pipeline.c:813`) to `static void ui_post_venue(uint16_t venue_id, uint16_t layout_id, int64_t gps_us)` with:

```c
    event_t ev = { .type = EV_VENUE_FOUND, .flags = EV_VENUE_ANNOUNCE_ONLY, .arg16 = venue_id,
                   .arg32 = layout_id, .gps_us = gps_us, .mono_us = esp_timer_get_time() };
```

and append to its doc comment:

```c
 * PR #101 M4: arg32 carries the LOCKED layout id (0 = none/Auto). lap_import_rtc() locks
 * forced_layout_id from the §15.3 snapshot and emits no EV_LAYOUT_LOCKED (it has no event
 * buffer), so handle_venue_found() (ui.c) used to reset the menu to "Layout: Auto" on a lap that
 * was locked to layout N -- and the first MODE press then advanced Auto -> layouts[0], posting
 * CMD_SET_LAYOUT for a possibly DIFFERENT layout mid-lap and wiping s_best. The announce now
 * states the truth and the ui adopts it.
```

The resume call site (`pipeline.c:512`) becomes `ui_post_venue(lr.venue_id, lr.layout_id, fix->gps_us);`. The resume does **not** touch the session (it continues the one already open — `session_policy(…, SES_IN_VENUE_ANNOUNCE)` is `SES_ACT_NONE` by construction, which is the row `test_session_policy` pins), but it must adopt the mirror: add `s_ses_venue_id = lr.venue_id; s_ses_layout_id = lr.layout_id;` next to `s_resume_active = true;`.

- [ ] **Step 10: R8 — `dbg logtest` through the pipeline.** Add (DEVUX-only, so a release image carries none of it):

```c
#if CFG_HAS_DEVUX
/* R8 (app:M2). `dbg logtest n` used to ring_push() synthetic fixes from the CONSOLE task, making
 * g_fix_ring (core/ring.h: a relaxed-head SPSC ring whose push loads head, memcpys the slot, then
 * stores head+1) two-producer: two producers reading the same head both write the SAME slot and
 * both store head+1, so the slot holds a byte-level mix of two gps_fix_t and the logger encodes
 * that mix into the live session's .log as one FIX record. The console now posts
 * CMD_DBG_LOGTEST{arg16 = n} and THIS task -- the ring's one documented producer (app/lt_ipc.h) --
 * synthesises them, at most PIPE_LOGTEST_PER_STEP per 50 ms pass so the heartbeat, the 100 Hz IMU
 * path and the task WDT all keep running. FIX_RING_CAP is 32, so a larger burst would only be
 * dropped by the ring anyway. Body moved verbatim from export_serial.c's synth_fix(). */
#define PIPE_LOGTEST_PER_STEP 16u
static uint32_t s_logtest_left, s_logtest_i;

static void logtest_synth_fix(gps_fix_t *f, uint32_t i)
{
    LT_ASSERT_VOID(f != NULL, PIPE_ASSERT_CODE);
    memset(f, 0, sizeof *f);
    f->gps_us     = (int64_t)1700000000000000LL + (int64_t)i * 200000;   /* 5 Hz */
    f->mono_us    = esp_timer_get_time();
    f->lat_e7     = -338900000 + (int32_t)(i % 2000);
    f->lon_e7     =  184000000 + (int32_t)(i % 2000);
    f->alt_mm     = 45000 + (int32_t)(i % 50);
    f->gspeed_mms = 20000 + (int32_t)(i % 1000);
    f->head_e5    = (int32_t)((i * 137) % 36000000);
    f->hacc_mm    = 2000;
    f->sacc_mms   = 300;
    f->pdop_e2    = 120;
    f->fix_type   = 3;
    f->sats       = 10;
    f->flags      = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f->valid      = 1;
}

static void logtest_step(void)
{
    if (s_logtest_left == 0u) return;
    uint32_t n = (s_logtest_left < PIPE_LOGTEST_PER_STEP) ? s_logtest_left : PIPE_LOGTEST_PER_STEP;
    for (uint32_t k = 0; k < n; k++) {
        gps_fix_t f;
        logtest_synth_fix(&f, s_logtest_i);
        s_logtest_i++;
        (void)ring_push(&g_fix_ring, &f);   /* drop-newest on full, exactly like a real fix */
    }
    s_logtest_left -= n;
    LT_ASSERT_VOID(s_logtest_left <= 0xFFFFu, PIPE_ASSERT_CODE);   /* arg16-sourced: never underflows */
    logger_notify();
}
#endif
```

one arm in `handle_cmd`'s switch:

```c
#if CFG_HAS_DEVUX
    case CMD_DBG_LOGTEST:
        s_logtest_left = cmd->arg16;
        s_logtest_i    = 0u;
        ESP_LOGI(TAG, "logtest: %u synthetic fixes queued on this task", (unsigned)cmd->arg16);
        break;
#endif
```

and one call in `pipeline_task`'s loop, right after the GPS drain line (`pipeline.c:1192`):

```c
#if CFG_HAS_DEVUX
        logtest_step();                    /* R8: the console's synthetic fixes, from THIS task */
#endif
```

- [ ] **Step 11: asserts and false comments in `pipeline.c`.**

| Site | Old | New |
|---|---|---|
| `:216` `emit_event` | `LT_ASSERT_VOID(ev->type <= EV_FAULT, …)` | `LT_ASSERT_VOID(ev->type < EV_COUNT, PIPE_ASSERT_CODE);` + `/* design 2026-10-13 §3: EV_COUNT is the single bound; EV_FAULT stopped being the last code when EV_VENUE_LOST (20) landed */` |
| `:229` `emit_simple` | `LT_ASSERT_VOID(type != EV_NONE && type <= EV_FAULT, …)` | `LT_ASSERT_VOID(type != EV_NONE && type < EV_COUNT, PIPE_ASSERT_CODE);` |
| `:420` `engine_cb` | `LT_ASSERT_VOID(ev->type <= EV_FAULT, …)` | `LT_ASSERT_VOID(ev->type < EV_COUNT, PIPE_ASSERT_CODE);` |
| `:599` `stats_finalise` (`:202`) | `LT_ASSERT_VOID(out->min_speed_cms != 0xFFFFu, …)` — asserts the line above it just assigned (M4) | `LT_ASSERT_VOID(out->max_speed_cms >= out->min_speed_cms, PIPE_ASSERT_CODE);   /* §9.4: the window's max can never be below its min */` |
| `:465` | `/* power_state: no owner in plan 03 */` and `/* partial_count */ 0` | `/* power_state: 0 -- there is no power task; Plan 6 (#54) owns §15.3's power_state and the ui owns s_partial_count (ui.c), which is not handed here either: issue filed by F4. */` |
| `:1192` | `for (int i = 0; i < PIPE_FIX_DRAIN_MAX && gps_poll(&fix) == 1; i++) { on_fix(&fix); s_gps_seen = true; }` | unchanged logic, but the `s_gps_seen` comment at `:150-151` gains `F2a's pipeline_sensors_ok() is what the supervisor actually gates OTA validation on now (R1); this flag is one of its inputs.` **Skip this row if F2a already rewrote the comment.** |

- [ ] **Step 12: build + lint + host** — `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build && python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation && python3 tools/lint/venue_announce.py`. Expected: rc 0, 0 warnings, 0 violations. The venue linter must still see `ui_post_venue` in both acquisition functions (its signature changed, not its name) — if it reports 0 sites, the regex needs no change but the call sites do.

- [ ] **Step 13: commit**

```bash
git add components/app/pipeline/pipeline.c
git commit -m "f2b(t5): pipeline -- §12.1 session lifecycle on every build, pipeline_speed_kmh (R11), TIME_MAP/CALIB/gps_hz producers, CMD_DBG_LOGTEST (R8), announce carries the locked layout"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 5 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 6 (PARALLEL with Tasks 4–5; needs Task 1): `cmd.c` — `TRACKS_GET/PUT`, `COREDUMP_GET/CLEAR`, `op_read` L7, cfg RMW + only-on-change, the `s_asm` overlay

**Files:**
- Modify: `components/app/cmd/cmd.c` (F0 lines: 1–10, 51, 88, 116–120, 162–179, 347, 393, 414, 476, 493–518, 569–601, 661, 718, 737, 813–824, 856–886)
- Modify: `components/app/CMakeLists.txt` (one `REQUIRES` token)

**Interfaces:**
- Consumes: `CMD_TRACKS_GET/PUT`, `CMD_COREDUMP_GET/CLEAR`, `CMD_JSON_CAP`, `LOGGER_PUT_TRACK` (Task 1); `logger_request_sync()` (`app/lt_ipc.h`); `trk_to_json`, `trk_bundled`/`trk_bundled_count`, `trk_user_count`, `trk_user_id_at`, `trk_get`, `TRK_MAX_USER` (`core/trk.h`); `lt_cfg_rmw_begin/end` (Task 1); `esp_core_dump_image_get`/`esp_core_dump_image_erase` (`esp_core_dump.h`), `esp_partition_find_first`/`esp_partition_read` (`esp_partition.h`).
- Produces: `const char *cmd_tracks_payload(size_t *len_out)` (body — Task 4's `put_track()` is its only caller); the four op codes answered by `cmd_dispatch` (Task 7's console verbs are their only on-device client).

- [ ] **Step 1: pay for Task 4's DRAM — overlay the JSON buffer on `LIST`'s session table.** Replace `cmd.c:51` and `cmd.c:517`:

```c
/* §18.1 is explicit that exactly ONE request is in flight, and cmd_dispatch is not reentrant (the
 * console task is its only caller). The two largest assembly buffers in this file are never live
 * at the same time: `json` serves CONFIG_GET/SET, ERRLOG_GET, DIAG_GET and TRACKS_GET/PUT; `sess`
 * serves LIST alone. Neither survives a dispatch -- every op rebuilds its own member from scratch
 * at the start of each pass (op_list's measuring pass refills sess[]; stream_export/stream_raw
 * re-run exp_open/ses_reader_init from s_open's id and fmt, so an OPEN -> LIST -> READ sequence
 * regenerates everything it needs), and the two-pass memo that DOES have to survive between a
 * measuring and a printing pass (s_memo, s_open) is deliberately outside this union. Overlaying
 * them reclaims 3072 B of static DRAM, which is what pays for LAP_ACC_CAP's rise to the 100 LAP
 * frames §12.5 promises (logger.c). */
static union {
    char       json[CMD_JSON_CAP];        /* 3072 (app/cmd.h, exported for the transport's own assert) */
    sess_ent_t sess[LIST_MAX_SESSIONS];   /* 64 x ~72 B */
} s_asm;
```

`sess_ent_t` and `LIST_MAX_SESSIONS` must move above this declaration (they are currently at `cmd.c:503-517`, below `s_json`). Then rename mechanically — `s_json` → `s_asm.json`, `s_sess` → `s_asm.sess` — and fix `sizeof s_json` → `sizeof s_asm.json` and `sizeof s_sess[i].venue` → `sizeof s_asm.sess[i].venue`:

```bash
sed -i '' -e 's/\bs_json\b/s_asm.json/g' -e 's/\bs_sess\b/s_asm.sess/g' components/app/cmd/cmd.c
```

then re-read the 27 touched lines: the `static char s_json[3072];` and `static sess_ent_t s_sess[...]` declarations are gone (replaced by the union), and nothing else about the bodies changes.

- [ ] **Step 2: R10 + §15.2 "only on change" in `op_config_set`** (`cmd.c:163-179`):

```c
static int op_config_set(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    /* R10 (app:M11): load -> merge -> validate -> save must be atomic against ui.c's four menu
     * writers, or a menu edit landing between this load and this save is silently reverted (and
     * vice versa). On a lock timeout the write is SKIPPED and reported -- never forced. */
    if (!lt_cfg_rmw_begin())
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config busy; retry");
    load_cfg();
    cfg_t before = s_cfg;                                  /* §15.2 "config ... only on change" */
    char err[96];
    err[0] = '\0';
    if (cfg_from_json(&s_cfg, (const char *)payload, len, err, sizeof err) != 0) {
        lt_cfg_rmw_end();
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, err[0] ? err : "config json error");
    }
    (void)cfg_validate(&s_cfg);                            /* clamps in place; returns corrections */
    bool changed = memcmp(&before, &s_cfg, sizeof s_cfg) != 0;
    int  rc = changed ? lt_cfg_save(&s_cfg) : 0;
    lt_cfg_rmw_end();
    if (rc != 0) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "config save failed");
    /* A no-op CONFIG_SET is still acked, and still does NOT wake the pipeline or the ui: #87's
     * notify exists to propagate a change, and firing it for an unchanged blob is what made every
     * cosmetic re-POST re-init the drag engine (bench B4-F2). */
    if (changed) cfg_change_notify();
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);  /* ack */
}
```

`cfg_t` is 110 B, so `before` on the 6 KB console stack is free. `memcmp` over `cfg_t` is exact here because `load_cfg()` always starts from `cfg_defaults()` (no uninitialised padding in either copy) — state that in a one-line comment on the `memcmp`.

- [ ] **Step 3: L7 — `op_read`'s malformed payload** (`cmd.c:819-822`):

```c
    /* L7: §18.1 requires E_CONN_PROTO for a malformed payload. A short READ used to be silently
     * read as offset 0, i.e. a full restream the client never asked for. */
    if (len < 4) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "read: short payload");
    uint32_t offset = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                      ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
```

- [ ] **Step 4: `TRACKS_GET` (0x12, R5).** Add `#include "core/trk.h"` and, after `op_config_set`:

```c
/* R5 (§18.1 0x12): a JSON array of every venue -- bundled first, then the user table -- each
 * object being trk_to_json()'s venue document with a "src" key spliced in front. Streamed one
 * venue at a time through s_cw, so there is no whole-table buffer: trk_to_json's output goes into
 * the SAME s_asm.json every other text op uses (a maximal venue -- 8 layouts x 8 sectors -- is
 * ~2.6 KB, inside CMD_JSON_CAP). Read-only and side-effect free, so export_serial's two-pass
 * framing may run it twice.
 *
 * This makes the cmd/console task a THIRD unlocked reader of core/trk.h's module-static user
 * table, beside the pipeline and the ui. The ordering argument (the same shape as the logger's,
 * #97/PR #101 H2): the loop reads trk_user_count() first and only indices below it; the only
 * post-boot writers are the pipeline's finalize_create() -- which always allocates a fresh id via
 * trk_next_user_id(), so it only ever APPENDS at user[user_n] and then bumps user_n, never
 * touching a byte below the captured count -- and the logger's LOGGER_PUT_TRACK, which can
 * replace a same-id slot but runs only while THIS task is blocked inside logger_request_sync()
 * (op_tracks_put below), so it can never overlap this read. trk.h's ownership comment still names
 * two readers: F4 files the doc-debt issue (trk.h is F0's file in this wave).
 *
 * Returns 1 when a venue was written, 0 when it was skipped, -1 on a transport error. */
static int tracks_emit_one(const trk_venue_t *v, const char *src, bool first)
{
    LT_ASSERT_RET(v != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(src != NULL, CMD_ASSERT_CODE, -1);
    int n = trk_to_json(v, s_asm.json, sizeof s_asm.json);
    /* n <= 2 covers both an encode failure and a bare "{}" -- splicing into the latter would emit
     * {"src":"user",} and break the whole document, so skip it rather than corrupt the array. */
    if (n <= 2 || s_asm.json[0] != '{') return 0;
    if (!first && cw_write(&s_cw, (const uint8_t *)",", 1) != 0) return -1;
    char pre[24];
    int pw = snprintf(pre, sizeof pre, "{\"src\":\"%s\",", src);
    if (pw <= 0 || (size_t)pw >= sizeof pre) return -1;
    if (cw_write(&s_cw, (const uint8_t *)pre, (size_t)pw) != 0) return -1;
    /* +1 / -1: drop trk_to_json's own opening brace, keep everything from its first key on. */
    if (cw_write(&s_cw, (const uint8_t *)(s_asm.json + 1), (size_t)n - 1) != 0) return -1;
    return 1;
}

static int op_tracks_get(cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/true);
    if (cw_write(&s_cw, (const uint8_t *)"[", 1) != 0) return -1;
    bool first = true;
    for (uint16_t i = 0; i < trk_bundled_count; i++) {     /* rule 2: bounded by the table itself */
        int r = tracks_emit_one(&trk_bundled[i], "bundled", first);
        if (r < 0) return -1;
        if (r == 1) first = false;
    }
    int un = trk_user_count();
    LT_ASSERT_RET(un >= 0 && un <= TRK_MAX_USER, CMD_ASSERT_CODE, -1);   /* bounds the loop below */
    for (int i = 0; i < un; i++) {
        const trk_venue_t *v = trk_get(trk_user_id_at((uint8_t)i));
        if (v == NULL) continue;
        int r = tracks_emit_one(v, "user", first);
        if (r < 0) return -1;
        if (r == 1) first = false;
    }
    if (cw_write(&s_cw, (const uint8_t *)"]", 1) != 0) return -1;
    return cw_finish(&s_cw);
}
```

- [ ] **Step 5: `TRACKS_PUT` (0x13, R5) + the staging accessor.**

```c
static size_t s_track_len;   /* bytes of s_asm.json staged for LOGGER_PUT_TRACK; 0 outside the call */

/* app/cmd.h's contract: valid for exactly the span of op_tracks_put's logger_request_sync(). */
const char *cmd_tracks_payload(size_t *len_out)
{
    if (len_out != NULL) *len_out = s_track_len;
    return s_asm.json;
}

/* R5 (§18.1 0x13): parse-and-install is forbidden here. core/trk.h's ownership comment makes the
 * user table writer-exclusive to boot, the pipeline and the logger, and trk_from_json needs a
 * 2752 B trk_venue_t landing slot that must not be a local on this 6 KB console stack (PR #101
 * M2's twin). So: stage the JSON text, hand it to the logger (LOGGER_PUT_TRACK), and block until
 * it answers -- the logger parses into the trk module's own static landing slot via
 * trk_user_add_json() and then persists the table (logger.c's put_track()). The block is what
 * makes the hand-off safe: the staged buffer cannot change while the logger reads it, and
 * ownership returns here with the rc. 2000 ms, not op_delete's 1000: put_track() writes up to
 * TRK_MAX_USER records plus a rename. */
static int op_tracks_put(const uint8_t *payload, size_t len,
                         cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    if (len == 0 || len >= sizeof s_asm.json)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put: bad length");
    memcpy(s_asm.json, payload, len);
    s_asm.json[len] = '\0';
    s_track_len = len;
    log_request_t req = { .type = LOGGER_PUT_TRACK };
    int rc = logger_request_sync(&req, 2000);
    s_track_len = 0;                                       /* the staging window is closed */
    if (rc == -5) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "table full");
    if (rc == -6) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "bad venue json");
    if (rc == -7) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "installed but not saved");
    if (rc == -2) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put timed out");
    if (rc != 0)  return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "tracks put failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}
```

- [ ] **Step 6: `COREDUMP_GET`/`COREDUMP_CLEAR` (0x17/0x18, R5).** Add `#include "esp_core_dump.h"` and `#include "esp_partition.h"`:

```c
/* R5 (§18.1 0x17, §17.10, §18.4's `coredump`). Streams the stored image out of the `coredump`
 * partition (partitions.csv: 0x2A0000, 0x10000 = the 64 KB §18.1 caps the response at) in 256 B
 * reads through the SAME s_io buffer LIST/OPEN/READ already use -- no new static DRAM. The console
 * path Base64-encodes the body between the frame markers (§18.4), exactly like `open <id> log`.
 * Read-only, so the two-pass framing may run it twice. */
static int op_coredump_get(cmd_emit_fn emit, void *ctx, uint8_t tag)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    uint16_t seq = 0;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0)
        return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: none stored");
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (p == NULL) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: no partition");
    /* esp_core_dump_image_get reports a FLASH address; esp_partition_read takes an offset within
     * the partition. The image is written at the partition start, so rel is 0 in practice -- it is
     * derived rather than assumed so a future IDF layout change cannot read the wrong bytes. */
    size_t rel = (addr >= p->address) ? (addr - p->address) : 0u;
    if (rel >= p->size) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "coredump: bad offset");
    if (size > p->size - rel) size = p->size - rel;
    cw_init(&s_cw, emit, ctx, tag, 0, /*tail*/true);
    size_t done = 0;
    int chunks = 0;   /* rule 2: bounded by CMD_STREAM_MAX_CHUNKS (storage-partition-sized cap) */
    while (done < size) {
        LT_ASSERT_RET(chunks++ < CMD_STREAM_MAX_CHUNKS, CMD_ASSERT_CODE, -1);
        size_t n = size - done;
        if (n > sizeof s_io) n = sizeof s_io;
        if (esp_partition_read(p, rel + done, s_io, n) != ESP_OK) break;   /* short body, real CRC */
        if (cw_write(&s_cw, s_io, n) != 0) return -1;
        done += n;
    }
    return cw_finish(&s_cw);
}

/* R5 (§18.1 0x18): erase the stored image so the next crash has somewhere to land. */
static int op_coredump_clear(cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    if (esp_core_dump_image_erase() != ESP_OK)
        return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "coredump erase failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}
```

four arms in `cmd_dispatch`:

```c
    case CMD_TRACKS_GET:     return op_tracks_get(emit, ctx, tag);
    case CMD_TRACKS_PUT:     return op_tracks_put(payload, len, emit, ctx, tag, &seq);
    case CMD_COREDUMP_GET:   return op_coredump_get(emit, ctx, tag);
    case CMD_COREDUMP_CLEAR: return op_coredump_clear(emit, ctx, tag, &seq);
```

and `espcoredump` added to `components/app/CMakeLists.txt`'s `REQUIRES` list (same line as `esp_partition`), with `# cmd.c's COREDUMP_GET/CLEAR (R5, §18.1 0x17/0x18): esp_core_dump_image_get/erase.`

- [ ] **Step 7: side-effect asserts (M3) and the file-header false claim (app false-claims #5).** Every `LT_ASSERT_*` in `cmd.c` whose condition carries the loop's only increment — `:88` (`steps++`), `:347` (`steps++`), `:393` (`steps++`), `:414` (`retries++`), `:476` (`chunks++`), `:661` (`chunks++`), `:718` (`chunks++`), `:737` (`retries++`) — becomes two statements, increment first. Pattern, applied verbatim at each site:

```c
        steps++;
        LT_ASSERT_RET(steps <= EMIT_MAX_STEPS, CMD_ASSERT_CODE, -1);
```

(`chunks`/`retries` likewise, each against its own existing bound; `op_coredump_get`'s new `chunks++` from Step 6 is written the same way.) Then replace `cmd.c:3-5`:

```c
 * Implements every op of §18.1 except TIME_SYNC (0x30): STATUS, LIST, OPEN, READ, CLOSE, DELETE,
 * CONFIG_GET/SET, TRACKS_GET/PUT, ERRLOG_GET/CLEAR, DIAG_GET, COREDUMP_GET/CLEAR and the four
 * OTA_* ops. TIME_SYNC needs a provisional-clock consumer and is filed against Plan 8 (design
 * 2026-10-13 R5). (The previous text claimed LIST/OPEN/READ "are Task 5 and return an E_CONN_PROTO
 * ERROR chunk for now" and that "the TRACKS, COREDUMP, OTA and TIME_SYNC ops are later sessions" --
 * false for all but TIME_SYNC since 3.5/5.4.)
```

Also delete `cmd.c:752`'s claim that "a concurrent-request abort (E_CONN_XFER_ABORT) is a BLE/WiFi-transport concern" in favour of: `§18.1's "a new request while streaming aborts the stream" is unimplemented: the serial transport is synchronous (one dispatch at a time, UART mutex held), so there is never a stream in progress to abort -- but every streaming buffer here is a file static, so the first asynchronous transport must add the abort before it ships. Filed by F4.`

- [ ] **Step 8: build, lint, host** — `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build && python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation && cmake --build test/build -j && ctest --test-dir test/build --output-on-failure`. Expected: rc 0 / 0 warnings / 0 violations / 100% tests passed.

- [ ] **Step 9: commit**

```bash
git add components/app/cmd/cmd.c components/app/CMakeLists.txt
git commit -m "f2b(t6): cmd -- TRACKS_GET/PUT + COREDUMP_GET/CLEAR (R5), cfg RMW lock + only-on-change (R10/§15.2), op_read E_CONN_PROTO (L7), JSON/LIST buffer overlay (-3072 B)"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 6 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

- [ ] **SESSION B END CHECKPOINT** — run all four and record each result line in the ledger before any `/clear`:
  1. `cmake --build test/build -j && ctest --test-dir test/build --output-on-failure` → `100% tests passed` (49 suites).
  2. `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation && python3 tools/lint/venue_announce.py` → both rc 0.
  3. `source tools/idf-env.sh && IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build` → rc 0, 0 warnings; record the free-static-DRAM line.
  4. `idf.py -C test_apps/core_selftest build` → rc 0, 0 warnings.

---

## Session C — Tasks 7–9 (console surface, the four ui sites, final wave)

Tasks 7 and 8 are PARALLEL (different files, no shared symbol beyond Task 1's headers). Task 9 is SERIAL after both.

### Task 7 (PARALLEL with Task 8; needs Tasks 1 + 6): `export_serial.c` — console verbs, the `dbg` family, the `OTA-*` tokens

**Files:**
- Modify: `components/drivers/export_serial/export_serial.c` (F0 lines: 440–462, 520–597, 601–613, 636–641, 643–660, 703–716, 940–980, 1118–1158, 1228–1240)

**Scope fence:** this task owns the console **verb table**, the `dbg` family and the `OTA-*` **token framing**. F2a owns `ota recv`'s body (`ota_recv_stream`, the raw UART reads) and every `esp_log_level_set` line in this file (R9: `ESP_LOG_ERROR` → `ESP_LOG_NONE`). If a line is both (the three `printf`s inside `cmd_ota`), this task changes only the format string, never the level calls around it.

**Interfaces:**
- Consumes: `CMD_TRACKS_GET/PUT`, `CMD_COREDUMP_GET/CLEAR` (Task 1); `CMD_DBG_LOGTEST` + `CMD_TYPE_LAST` (Task 1); `run_cmd`/`run_stream`/`s_setbuf` (existing in this file).
- Produces: nothing other tasks consume.

- [ ] **Step 1: factor the argv rejoin out of `cmd_config_c`** (`cmd.c`'s console twin needs it too, and both verbs must stay inside RULE-4's line budget):

```c
/* Rejoin argv[from..argc-1] into s_setbuf with single spaces, NUL-terminated, truncated to fit.
 * Shared by `config set <json>` and `tracks put <json>` -- esp_console strips bare quotes, so a
 * JSON body must be escaped on the command line (config set {\"units\":1}) and arrives split on
 * spaces. Returns the byte length written. */
static size_t join_args(int argc, char **argv, int from)
{
    CORE_ASSERT_RET(argv != NULL, EXP_SERIAL_ASSERT_CODE, 0);
    CORE_ASSERT_RET(from >= 0, EXP_SERIAL_ASSERT_CODE, 0);
    size_t w = 0;
    for (int i = from; i < argc && w + 1 < sizeof s_setbuf; i++) {
        if (i > from) s_setbuf[w++] = ' ';
        size_t al = strlen(argv[i]);
        if (w + al >= sizeof s_setbuf) al = sizeof s_setbuf - 1 - w;
        memcpy(s_setbuf + w, argv[i], al);
        w += al;
    }
    s_setbuf[w] = '\0';
    return w;
}
```

`cmd_config_c`'s `set` arm becomes `size_t w = join_args(argc, argv, 2); run_cmd(CMD_CONFIG_SET, "config", (const uint8_t *)s_setbuf, w, false); return 0;`.

- [ ] **Step 2: the two new verbs (R5, §18.4's `tracks get` / `tracks put <json>` / `coredump`).**

```c
/* R5 (§18.4, drv:H4). `tracks get` is read-only and may be large (every bundled + user venue), so
 * it takes the two-pass streaming frame with the §18.1 CRC32 tail, exactly like `list`. `tracks
 * put` has side effects, so it takes the single-dispatch buffered frame (run_cmd) -- running it
 * twice would install the venue twice.
 *
 * Console size limit: repl_cfg.max_cmdline_length is 256 B, so a venue pasted here must be small
 * (one layout, few sectors). The framed CMD_TRACKS_PUT op over the dev-kit link carries the full
 * CMD_JSON_CAP (3072 B); the usage line says so rather than letting a truncated paste fail
 * mysteriously. */
static int cmd_tracks_c(int argc, char **argv)
{
    CORE_ASSERT_RET(argv != NULL, EXP_SERIAL_ASSERT_CODE, 1);
    if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        run_stream(CMD_TRACKS_GET, "tracks", NULL, 0, /*binary*/false, /*has_tail*/true);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "put") == 0) {
        size_t w = join_args(argc, argv, 2);
        run_cmd(CMD_TRACKS_PUT, "tracks", (const uint8_t *)s_setbuf, w, false);
        return 0;
    }
    printf("usage: tracks get | tracks put <json>   (console line cap 256 B; the framed\n");
    printf("       CMD_TRACKS_PUT op over the dev-kit link takes up to 3072 B)\n");
    return 1;
}

/* R5 (§18.4 `coredump`, §17.10). Binary body -> Base64 between the markers, like `open <id> log`. */
static int cmd_coredump_c(int argc, char **argv)
{
    CORE_ASSERT_RET(argv != NULL, EXP_SERIAL_ASSERT_CODE, 1);
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        run_cmd(CMD_COREDUMP_CLEAR, "coredump", NULL, 0, false);
        return 0;
    }
    run_stream(CMD_COREDUMP_GET, "coredump", NULL, 0, /*binary*/true, /*has_tail*/true);
    return 0;
}
```

registered beside the others (`export_serial.c:1236`, before the `#if CFG_HAS_DEVUX` block — both are production ops, not dev UX):

```c
    register_cmd("tracks",   "tracks get | tracks put <json>  (§18.1 0x12/0x13)", cmd_tracks_c);
    register_cmd("coredump", "coredump | coredump clear  (base64 image, §18.1 0x17/0x18)", cmd_coredump_c);
```

- [ ] **Step 3: R8 — `dbg logtest` posts a command instead of pushing fixes.** Delete `synth_fix` (`export_serial.c:521-538`, now in `pipeline.c`) and rewrite the fix loop of `dbg_logtest` (`export_serial.c:570-593`):

```c
    long n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 1000;
    if (n < 1) n = 1;
    if (n > 65535) n = 65535;      /* R8: carried in command_t.arg16 (was 100000) */
    ...                             /* the OPEN_SESSION request + the two synthetic laps are unchanged */
    /* R8 (app:M2): the fixes themselves are synthesised and pushed by the PIPELINE -- g_fix_ring's
     * one documented producer (app/lt_ipc.h). Pushing them from this task made the relaxed-head
     * SPSC ring two-producer and spliced two gps_fix_t into one FIX record in the live .log. */
    command_t c = { .type = CMD_DBG_LOGTEST, .arg16 = (uint16_t)n };
    CORE_ASSERT_RET(g_cmd_q != NULL, EXP_SERIAL_ASSERT_CODE, 1);
    if (xQueueSend(g_cmd_q, &c, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("logtest: cmd queue full; fixes not queued\n");
        return 1;
    }
    printf("logtest: opened a session, queued 2 laps, asked the pipeline for %ld fixes; NOT closed.\n", n);
    printf("  -> the pipeline pushes up to 16 per 50 ms pass, so %ld fixes take ~%ld ms.\n",
           n, (n + 15) / 16 * 50);
    printf("  -> watch for \"session Sxxxxx_yyy open\", then pull power to test the cut.\n");
    printf("  -> after reboot: dbg fs ; dbg sum <id> ; dbg logck <id>\n");
```

`#include "app/pipeline.h"` is already present (for `pipeline_lap_at`); `g_cmd_q`/`command_t` come from `app/lt_ipc.h`, also already included. The now-unused `ring_push`/`g_fix_ring` references leave this file entirely — if `#include "core/ring.h"` becomes unused, drop it.

- [ ] **Step 4: L27 + the dead `dbg` verbs (design §2's "remove + amend" ruling).** Delete the stub arm at `export_serial.c:968-971` entirely:

```c
        if (strcmp(s, "gps") == 0 || strcmp(s, "imu") == 0 || strcmp(s, "power") == 0) {
            printf("dbg %s: not in plan 03 (GPS/IMU raw and power states land later)\n", s);
            return 0;
        }
```

An unknown sub-verb then falls through to the usage block, which is the honest answer: `dbg gps raw on` has never existed and `dbg power` cannot exist before Plan 6 owns the power states. Replace the usage block (`:973-978`) and the help string (`:1238`) so both list exactly what the code implements:

```c
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id> | laps | rtc | mem\n");
    printf("       dbg btn <mode|up|down|up+down> [hold_ms]  (bench button injection)\n");
    printf("       dbg flag set|clear <bit 0..15>            (bench sys_flags injection)\n");
    printf("       dbg safe clear                            (clear the safe/recovery gate, §17.5)\n");
    printf("       dbg sim drag | laps <n> | park            (bench scenarios; sim build only)\n");
    printf("       dbg reset | crash | hang                  (reset / panic / task-WDT trip)\n");
    return 1;
```

```c
    register_cmd("dbg", "status|logtest [n]|fs|sum <id>|logck <id>|laps|rtc|mem|btn|flag|safe|sim|reset|crash|hang", cmd_dbg);
```

(`reset` was missing from the usage text and `safe`/`sim` from the help string — L27.)

- [ ] **Step 5: `dbg logck`/`dbg sum` count the two new record types.** `tally_t` (`:636`) gains `int tmaps, calibs;`, `tally_cb` gains two arms, and `dbg_logck`'s second `printf` gains the two fields:

```c
    case SES_T_TIME_MAP:    t->tmaps++;  break;
    case SES_T_CALIB:       t->calibs++; break;
```

```c
    printf("  HDR=%d FIX=%d FUSED=%d LAP=%d SECTOR=%d DRAG_RUN=%d DRAG_GATE=%d EVENT=%d"
           " TIME_MAP=%d CALIB=%d END=%d other=%d\n",
           t.hdr, t.fixes, t.fused, t.laps, t.sectors, t.drags, t.gates, t.events,
           t.tmaps, t.calibs, t.end, t.others);
```

This is what the bench item checks: a session with the R4 work in it reports `TIME_MAP >= 1` and `CALIB >= 1` instead of folding both into `other`.

- [ ] **Step 6: L16 — `print_sessions` must not report an error as "(empty)"** (`:601-613`):

```c
static void print_sessions(void)
{
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") != 0) { printf("  (cannot list)\n"); return; }
    sto_entry_t ent;
    int n = 0;
    int rc;
    while ((rc = sto_list_next(&it, &ent)) == 1) {
        printf("  %-24s %8u B\n", ent.name, (unsigned)ent.size);
        n++;
    }
    sto_list_close(&it);
    /* L16: rc < 0 is a storage error mid-listing; reporting it as "(empty)" hid a degraded fs
     * behind a normal-looking answer. rc == 0 is a genuine end of directory. */
    if (rc < 0) printf("  (listing error %d after %d entries)\n", rc, n);
    else if (n == 0) printf("  (empty)\n");
}
```

- [ ] **Step 7: L15 — the `OTA-*` tokens reach the wire as `\r\r\n`.** `cmd_ota` does not open the LF window (`frame_tx_begin`), so the console's `ESP_LINE_ENDINGS_CRLF` translation turns each format string's own `\n` into `\r\n` — emitting `OTA-READY\r\r\n`. Change the three token `printf`s in `cmd_ota` (`:1139`, `:1145`, `:1153`) and any `OTA-ERR` token inside `ota_recv_stream` to a **bare** `\n`, so the translation produces exactly one `\r\n`:

```c
        printf("OTA-ERR 0x%04x\n", (unsigned)brc);
...
    printf("OTA-READY\n");            /* L15: bare \n -- the console's CRLF translation adds the \r;
                                       * a literal \r\n here reached the wire as \r\r\n */
...
    printf("OTA-END 0x%04x\n", (unsigned)erc);
```

`tools/ota_push.py` already `.strip()`s, so no tool change is needed (and `tools/` is F3's) — but a stricter parser, including the dev-kit's `push_and_wait`, now sees clean tokens. Note in the commit body that the dev-kit's framing selftest (F3, R30) is the consumer that benefits.

- [ ] **Step 8: build, lint** — `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build && PANEL=ws29v2 ./build.sh moto_sim build && python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation`. Expected: rc 0, 0 warnings, 0 violations. Grep-verify the dead verbs are gone: `grep -n "not in plan 03" components/drivers/export_serial/export_serial.c` → no output.

- [ ] **Step 9: commit**

```bash
git add components/drivers/export_serial/export_serial.c
git commit -m "f2b(t7): console -- tracks get/put + coredump verbs (R5), dbg logtest via CMD_DBG_LOGTEST (R8), dead dbg gps/imu/power removed, dbg usage/help corrected (L27), OTA token CRLF (L15), listing error not '(empty)' (L16)"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 7 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 8 (PARALLEL with Task 7; needs Tasks 1 + 5): `ui.c` — the four owned sites

**Files:**
- Modify: `components/app/ui/ui.c` (F0 lines: 112, 205, 356–376, 374, 457–459, 471–473, 522–524, 538–540, 723, 1103–1104, 1235–1249, 1442–1443, 1475, 1526)
- Modify: `components/app/include/app/ui.h` (lines 1–8)

**Scope fence:** nothing else in `ui.c`. F1b owns `components/core/ui/*` (so no renderer, model or golden changes — every site below works through the existing `screen_model_t` fields), and `ui_reload_cfg`/`menu_select`/`handle_create` keep their current bodies except where listed.

**Interfaces:**
- Consumes: `pipeline_speed_kmh()` (Task 5), `lt_cfg_rmw_begin/end` (Task 1), `EV_VENUE_NOT_SAVED` (Task 1), `event_t.arg32` = locked layout id on an announce (Task 5).
- Produces: nothing.

- [ ] **Step 1: R11 — read the real speed.** `s_gspeed_kmh` (`ui.c:205`) stops being an `EV_MOTION` proxy:

```c
/* R11 (orchestrator §3.2 #15): the real per-fix ground speed, refreshed once per ui pass from
 * pipeline_speed_kmh() (app/pipeline.h) so the §20.5 MOVING glyph and the §20.3 refresh policy
 * see one consistent value within a tick. §20.7's menu gate reads the live value at menu-entry
 * time instead. Was MENU_LOCK_SPEED_KMH-or-0 from the EV_MOTION/EV_STILL edges, i.e. a 3 km/h
 * (MOVING_SPEED_KMH) proxy for a 10 km/h rule. */
static uint16_t s_gspeed_kmh;
```

`ui_open_menu` (`ui.c:358`) reads live, and says what it actually compared:

```c
static void ui_open_menu(void)
{
    uint16_t kmh = pipeline_speed_kmh();      /* R11: live, not the once-per-pass cache */
    if (kmh < MENU_LOCK_SPEED_KMH) {
        ...unchanged...
    } else {
        ESP_LOGI(TAG, "menu locked (%u >= %u km/h, §20.7)", (unsigned)kmh,
                 (unsigned)MENU_LOCK_SPEED_KMH);
    }
}
```

In `ui_loop_iter`, immediately before the existing `update_flags();` call, add `s_gspeed_kmh = pipeline_speed_kmh();   /* R11: one read per pass feeds the glyph + refresh policy */`. The `EV_MOTION`/`EV_STILL` arms (`ui.c:1442-1443`) lose their assignments and keep the events (the logger's still-pause and F2a's OTA stillness precondition are their real consumers now):

```c
    /* R11: no longer a speed proxy -- s_gspeed_kmh comes from pipeline_speed_kmh(). These edges
     * stay the §9.1 still/moving boundary (3 km/h) that the logger's §12.6 pause and the §19.5
     * OTA precondition use; the ui needs nothing from them. */
    case EV_MOTION:
    case EV_STILL:
        break;
```

`update_flags` (`:1475`) and the refresh-policy input (`:1526`) are unchanged — they already read `s_gspeed_kmh`.

- [ ] **Step 2: R10 — lock the four cfg read-modify-write sites.** Each of `menu_do_mode` (`:457-459`), `menu_do_units` (`:471-473`), `menu_do_dist` (`:522-524`) and `menu_do_display` (`:538-540`) takes the lock around exactly its load→mutate→save, and **skips the write** on a timeout. `menu_do_units` in full; the other three are the identical three-line wrap around their own mutation:

```c
    /* R10 (app:M11): cmd.c's op_config_set does the same load -> mutate -> save on this core; the
     * lock is what makes the pair atomic. On a 200 ms timeout the write is skipped and logged --
     * never forced, which would be the silent revert this exists to prevent. The in-RAM model is
     * left untouched too, so the screen keeps showing the persisted truth. */
    if (!lt_cfg_rmw_begin()) { ESP_LOGW(TAG, "units: cfg busy, not saved"); return; }
    (void)lt_cfg_load(&s_cfg);
    s_cfg.units = (s_cfg.units == CFG_UNITS_MPH) ? (uint8_t)CFG_UNITS_KMH : (uint8_t)CFG_UNITS_MPH;
    (void)lt_cfg_save(&s_cfg);
    lt_cfg_rmw_end();
```

The post-save work in each function (`ui_send_cmd`, label `snprintf`, `s_model.*`, `drag_rows_refill`, `s_dirty`) stays **outside** the lock: it touches no NVS and must not run while the mutex is held. `menu_do_mode`'s `ui_send_cmd(CMD_SET_MODE, …)` must stay after `lt_cfg_rmw_end()` for the same reason. Add `#include "app/lt_nvs.h"` if it is not already there (it is — `lt_cfg_load`/`lt_cfg_save` come from it).

- [ ] **Step 3: PR #101 M3 — the `NOT SAVED` card.** `handle_venue_found` (`:1235`) gains an early branch, before it touches `s_venue_id`/`s_layout_choice`:

```c
static void handle_venue_found(const event_t *e, int64_t now)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);
    /* PR #101 M3: the logger could not persist the venue a creation just added (storage dead, full,
     * or below the §12.7 reserve), so it lives in RAM only until the next reboot. Say so on the
     * VENUE card's value row -- the created name was already shown by the engine's own
     * EV_VENUE_FOUND a moment earlier, and a silent success here is what the finding is about.
     * s_venue_id / s_layout_choice are deliberately NOT touched: the venue IS armed in the engine,
     * only unpersisted, so the Layout item must keep working. */
    if ((e->flags & EV_VENUE_NOT_SAVED) != 0u) {
        snprintf(s_model.venue_name, sizeof s_model.venue_name, "NOT SAVED");
        s_model.layout_name[0] = '\0';        /* value row = venue_name, label row = "VENUE" */
        show_venue_oneshot(now, /*announce_only*/true);
        s_dirty = true;
        return;
    }
    s_venue_id = e->arg16;
    ...
```

- [ ] **Step 4: PR #101 M4 — adopt the locked layout from the announce.** Replace `ui.c:1238`'s unconditional `s_layout_choice = 0;` and the `layout_name` clear with:

```c
    s_venue_id = e->arg16;
    const trk_venue_t *v = trk_get(e->arg16);
    /* PR #101 M4: an announce (the §15.3 RTC resume) carries the LOCKED layout id in arg32 --
     * lap_import_rtc() locks forced_layout_id and emits no EV_LAYOUT_LOCKED, so this used to reset
     * the menu to "Auto" on a locked lap and the first MODE press then changed the layout mid-lap.
     * Adopt it: s_layout_choice is the 1-based index of that layout in v->layouts[], or 0 (Auto)
     * when the event names none or the id is not in this venue (a stale snapshot). */
    s_layout_choice = 0;
    if (v != NULL && e->arg32 != 0u) {
        for (uint8_t i = 0; i < v->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
            if (v->layouts[i].id == (uint16_t)e->arg32) { s_layout_choice = (uint8_t)(i + 1u); break; }
        }
    }
    snprintf(s_model.venue_name, sizeof s_model.venue_name, "%s",
             (v != NULL && v->name[0] != '\0') ? v->name : "VENUE");
    /* The venue card shows the venue name (value row); the Layout menu item is built from
     * s_venue_id/s_layout_choice the next time the menu opens (build_menu/ui_layout_label). */
    s_model.layout_name[0] = '\0';
    show_venue_oneshot(now, (e->flags & EV_VENUE_ANNOUNCE_ONLY) != 0u);
    s_dirty = true;
```

- [ ] **Step 5: the padding asserts listed for `ui.c` (M4/R20).**

| Site | Old (tautology) | New |
|---|---|---|
| `:374` | — (the `ESP_LOGI` arm) | no assert needed; Step 1 rewrote it |
| `:723` | asserts the slot the preceding loop just cleared | delete; replace with `LT_ASSERT_VOID(idx < LAP_MAX_SECTORS + 1u, UI_APP_ASSERT_CODE);   /* indexes last_sector_delta[]/have_last_sector_delta[] */` placed BEFORE the loop |
| `:1103-1104` | `LT_ASSERT_VOID(s_model.lap_no == 1 && s_model.big_kind == BIG_NONE, …)` right after assigning both | delete both; replace with `LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* a lap reset must leave a valid screen for the next render */` |
| `:374` (`s_model.mode == s_mode` at `ui.c:374` in the review's main-tree numbering → `ui_apply_mode`'s tail on this branch) | asserts the field three lines after assigning it | delete; replace with `LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, UI_APP_ASSERT_CODE);   /* the only two modes CMD_SET_MODE and cfg.mode can carry */` |

Each function must still end with ≥ 2 assertions if it is over 20 code lines — check with the linter in Step 7, and if a replacement drops a function below 2, add the second as a genuine precondition on its parameters (never a restatement of an assignment).

- [ ] **Step 6: `ui.h`'s false claims (app false-claims #6).** Replace `ui.h:3-7` wholesale:

```c
 * The ui task (core 0, prio 6, stack 6144 on every build) owns a static screen_model_t and a
 * static framebuffer, drains the ui's own copy of the event broadcast (g_ui_evt_q -- g_evt_q is
 * the LOGGER's copy, app/lt_ipc.h) and the button queue, updates the model, renders once per
 * change via core/ui's screens_render() and drives the SSD1680 e-paper through hal/display.h with
 * the full §20.3 refresh ladder. Menu navigation, the button debounce state machine and the §20.7
 * menu-lock gate (pipeline_speed_kmh() >= MENU_LOCK_SPEED_KMH, design 2026-10-13 R11) live in
 * ui.c; ui_buttons.c is only the ISR->queue glue.
```

(The old text claimed a 2560-byte `moto_sim` stack — it is 6144 on every build, `ui.c:70-78`; that it drains `g_evt_q`; and that it "logs the dirty box instead of refreshing a panel" because "plan 04 has no driver".)

- [ ] **Step 7: build, lint, host** — `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build && python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation && python3 tools/lint/venue_announce.py && cmake --build test/build -j && ctest --test-dir test/build --output-on-failure`. Expected: rc 0 / 0 warnings / 0 violations / `100% tests passed`. The `test_screens`/`test_ui` goldens must be **byte-identical** — this task adds no renderer or model-layout change; if a golden moves, the change went outside the fence and must be reverted.

- [ ] **Step 8: commit**

```bash
git add components/app/ui/ui.c components/app/include/app/ui.h
git commit -m "f2b(t8): ui -- menu lock reads pipeline_speed_kmh (R11), cfg RMW locked (R10), NOT SAVED card + layout from the announce (PR #101 M3/M4), padding asserts, ui.h header claims"
```

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 8 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

---

### Task 9 (SERIAL, final wave): gates, DRAM, spec amendments, bench items, PR

**Files:**
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§12.1, §12.3, §12.4, §12.6, §13.3, §18.1, §18.4 + Appendix A — **only** these; F4 owns the rest of §18.4 and every other section)
- Modify: `docs/bench/next-bench-day.md` (append this plan's items)
- Create: the PR

**Interfaces:** none produced.

- [ ] **Step 1: the full design §4 gate list.** Run every line, record each result in the ledger, and paste the set into the PR body:

```bash
source tools/idf-env.sh
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j
ctest --test-dir test/build --output-on-failure          # expect 100%, 49 suites
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                      # 0
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim    build          # rc 0, 0 warnings
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m  build          # rc 0, 0 warnings
IDF_CCACHE_ENABLE=0 PANEL=ws29v2 ./build.sh moto_sim build
idf.py -C test_apps/core_selftest build                   # memory: core_selftest is a required gate
```

plus the gcc-16 sweep of `test/build/compile_commands.json` (0 diagnostics) exactly as the previous plans ran it. No flashing by a subagent.

- [ ] **Step 2: report the DRAM delta** (design §4: a plan needing > 256 B must say where it reclaims it). Take the free-static-DRAM figure from each clean build and check it against this account:

| Change | Δ bytes |
|---|---|
| `logger.c` `LAP_ACC_CAP` 3072 → 5500 (100 LAP frames, §12.5) | **+2428** |
| `logger.c` `s_calib` (`ses_calib_t`), `s_pause` (`log_pause_t`), `s_acc_full` | +36 |
| `pipeline.c` `s_speed_kmh`, `s_gps_hz`, `s_ses_open`, `s_ses_venue_id/layout_id`, `s_tb_q_last`, `s_last_timemap_us` | +20 |
| `pipeline.c` `s_logtest_left`/`s_logtest_i` (`CFG_HAS_DEVUX` only) | +8 |
| `lt_nvs.c` `s_cfg_rmw` + `s_cfg_rmw_buf` | +84 |
| `cmd.c` `s_track_len` | +4 |
| `cmd.c` `s_asm` union of `json[3072]` and `sess[64]` (was 3072 + 4608 = 7680, now 4608) | **−3072** |
| `TRACKS_GET` / `COREDUMP_GET` / `TRACKS_PUT` buffers | 0 — they reuse `s_asm.json`, `s_io`, `s_cw` |
| `log_request_t` `gps_hz` (replaces `_pad2[1]`), `log_result_t` `tmap`/`calib` (both smaller than `lap_result_t`) | 0 |
| **net** | **≈ −492** |

If the measured figure disagrees by more than ~64 B (alignment/padding), reconcile before opening the PR rather than explaining it away. Note for the wave: F2a raising `SER_ASM_MAX` 2560 → `CMD_JSON_CAP` (3072) for drv:M7 costs +512 B, which this plan's −3072 B already covers.

- [ ] **Step 3: spec amendments — the exact text.** Each one cites `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`.

**§12.1**, last bullet — replace:
> A session starts at boot (or resume) and ends at PARK/SHUTDOWN or when the venue changes. A new session also starts on mode change.

with:
> A session starts when the lap engine acquires a venue (`EV_VENUE_FOUND`, §10.3) and ends when it leaves one (`EV_VENUE_LOST`) or on a planned restart/OTA (`END.reason = RESTART`). A mode change — `CMD_SET_MODE`, or a `CMD_CONFIG_RELOAD` that flips `cfg.mode` — closes the open session (`NORMAL`) and opens the next one, so a `.log`/`.sum` pair never mixes two modes. The owner of the lifecycle is the **pipeline** task (`session_policy()`, `app/session_policy.h`): it issues `LOGGER_OPEN_SESSION`/`LOGGER_CLOSE_SESSION` fire-and-forget on `log_req_q`, whose FIFO order makes a close-then-open rotation atomic without a synchronous wait; the logger is the authority and ignores an OPEN while open or a CLOSE while closed. PARK/SHUTDOWN closes belong to Plan 6 (there is no power task). The per-boot sequence byte saturates at 255: the 256th open of one boot is **refused** (`E_STO_WRITE`, arg `0xFF`) rather than appended to the previous session's file. Ruling R4, design 2026-10-13.

**§12.3**, after the record table — append:
> `TIME_MAP` (0x0C), `CALIB` (0x0A) and the `SESSION_HDR` calib block are written by the logger from records the pipeline posts on `result_q` (it owns the time base and the fusion state); the calib values are `fus_calib_defaults()` until Plan 8 captures a real orientation (#93), but the block and the record are present and decodable. `MARK` (0x0B) is **reserved and never written**: there is no button-mark feature. `POWER` (0x0E) is **reserved and never written** until Plan 6 owns the power states. Ruling R4, design 2026-10-13.

**§12.4**, last bullet — replace:
> `TIME_MAP` is written at session start, whenever `tb` quality changes, and every 60 s.

with:
> `TIME_MAP` is written at session start, whenever `tb_quality()` changes, and otherwise every 60 s while a session is open. The pipeline produces every `(mono_us, gps_us, quality)` triple (`LOG_RES_TIMEMAP`) because it owns the time base; the logger only encodes it, and drains `result_q` ahead of the sample rings so the session-start record lands immediately after `SESSION_HDR`.

**§12.6**, last line — replace:
> Logging pauses (no `FIX_*`/`FUSED`) when `still` for 10 s; resumes with a `FIX_KEY`.

with:
> Logging pauses (no `FIX_*`/`FUSED`) once `still` (`EV_STILL`, below `MOVING_SPEED_KMH`) has held for `LOG_PAUSE_STILL_S` = 10 s, and resumes on `EV_MOTION` with a `FIX_KEY` (both delta codecs are re-keyed). Summaries, events and the `.sum` keep flowing throughout, exactly as under `SYS_STORAGE_FULL` and safe mode. The rule is the pure `log_pause_t` state machine (`app/log_pause.h`, host-tested in `test/test_log_pause.c`). The `.sum` carries the first 100 `LAP` frames of a 4-sector layout (5500 B); laps past that cap are `.log`-only and the logger reports the spill once per session.

**§13.3**, last paragraph — replace "Session open/close is commanded by the power task (`LOGGER_OPEN_SESSION`, `LOGGER_CLOSE_SESSION`) via `ui_req_q`-style request queue `log_req_q` (depth 4)." with:
> Session open/close is commanded by the **pipeline** task over the request queue `log_req_q` (depth 4): `LOGGER_OPEN_SESSION`, `LOGGER_CLOSE_SESSION`, plus `LOGGER_REBUILD_SUMMARY`, `LOGGER_EVICT`, `LOGGER_DELETE_SESSION`, `LOGGER_SAVE_TRACKS` and `LOGGER_PUT_TRACK`. There is no power task (Plan 6). A request may carry a `requester` for a bounded reply (`logger_request_sync`, debt sweep A #59/#73) — used by the supervisor, the console and `TRACKS_PUT`, never by the ui or the pipeline.

**§18.1**, the op table — mark 0x12/0x13/0x17/0x18 implemented and annotate the last row:
> | 0x30 | `TIME_SYNC` | `unix_s u32` | **Not implemented.** It needs a provisional-clock consumer for file naming; filed against Plan 8 (design 2026-10-13 R5). |

and append after "Errors: `E_CONN_PROTO` …":
> `TRACKS_PUT` answers `E_CONN_PROTO` with `table full`, `bad venue json`, `installed but not saved` (the venue is armed in RAM but the table could not be persisted — storage dead, full, or below the §12.7 5 % reserve) or `tracks put timed out`. "A new request while streaming aborts the stream (`E_CONN_XFER_ABORT`)" is unimplemented: the serial transport is synchronous, so no stream is ever in progress; the first asynchronous transport must add it.

**§18.4** (this plan's parts only — F4 owns the rest of the section, including R9's "ERROR" → "NONE"):
- First bullet: strike "921600 baud (CH340 supports up to 2 Mbaud)" → `UART0 at 115200 baud (CONFIG_ESP_CONSOLE_UART_BAUDRATE), IDF esp_console REPL with line editing disabled (linenoise dumb mode)`.
- Command list: `status`, `list`, `open <id> <vbo|nmea|json|log|sum>`, `read <offset>`, `close`, `delete <id>`, `config get`, `config set <json>`, `tracks get`, `tracks put <json>`, `errlog`, `errlog clear`, `diag`, `coredump`, `coredump clear`, and — on `CFG_HAS_DEVUX` builds only — `ota recv <size> <sha256_hex> <ver> <hwid>` and `dbg <status|logtest [n]|fs|sum <id>|logck <id>|laps|rtc|mem|btn|flag|safe|sim|reset|crash|hang>`.
- Append: `There is no `dbg gps raw`, `dbg imu raw` or `dbg power`: GPS/IMU raw dumps need the real drivers (Plan 8, #93) and the power states need Plan 6, so the stub verbs that printed "not in plan 03" were removed rather than left as a promise (ruling R5/§18.4 amendment, design 2026-10-13). `tracks put <json>` over the console is bounded by the 256-byte command line; the framed `TRACKS_PUT` op carries up to 3072 bytes.`
- Strike the last bullet ("OTA over serial is not implemented; use `idf.py flash` / `esptool.py`") → `OTA over serial IS the primary bench flash path: `ota recv` plus `tools/ota_push.py`, or the dev-kit's `flash push`.`

**Appendix A** — add one row: `| LOG_PAUSE_STILL_S | 10 | §12.6 still-pause (app/log_pause.h's LOG_PAUSE_STILL_MS) |`. If F4 has already rebuilt Appendix A, add the row to its table rather than reintroducing the old one.

- [ ] **Step 4: bench items** — append to `docs/bench/next-bench-day.md` under `## Open items`, numbered after the existing ones. Exact procedures and expected lines:

```markdown
- [ ] **F2b-1. §12.6 still-pause.** With a session open (`moto_sim` boots one), note `dbg logck <id>`'s
  `FIX=`/`FUSED=` counts, then `dbg sim park` and wait 15 s. Expect: the log line
  `log: logging paused: still 10 s (§12.6)` ~10 s after the EV_STILL, and a second `dbg logck <id>`
  showing `FIX=`/`FUSED=` UNCHANGED from a reading taken 2 s after the pause line (other counts may
  grow). Then `dbg sim laps 2`: the next `dbg logck` shows FIX growing again, and `dbg sum <id>` is
  intact. Evidence: the two logck lines plus the pause log line.
- [ ] **F2b-2. §12.4 TIME_MAP + §12.3 CALIB.** `dbg logck <id>` on any session opened by this build.
  Expect `TIME_MAP=1` (or more) and `CALIB=1` immediately — the two new columns; previously both fell
  into `other=`. Leave the device idle 70 s with the session open and re-run: `TIME_MAP` has grown by
  at least 1 (the 60 s cadence). Evidence: both logck lines.
- [ ] **F2b-3. §12.1 session rotation on a mode change.** With a session open, toggle Mode in the menu
  (or `config set {"mode":"drag"}`). Expect: `log: session Sxxxxx_NNN closed (reason 0)` then
  `log: session Sxxxxx_NNN+1 open`, `dbg fs` listing both `.log`/`.sum` pairs, and `dbg sum` on the
  new one reporting `mode` drag. Evidence: the two log lines + `dbg fs`.
- [ ] **F2b-4. §12.1 session open on a real-GPS build (closes audit §3.3 #25).** Flash `moto_neo6m`
  and confirm — with the stub GPS, no session can open (no venue is ever acquired), so the honest
  observable is the ABSENCE of a session plus no crash: `dbg fs` shows `/sessions: (empty)`, no
  `E_STO_*` in `errlog`. The positive half needs the real driver (Plan 8, #93) and stays open there.
- [ ] **F2b-5. R5 tracks round trip.** `tracks get` → a `---BEGIN tracks <n>---` frame whose body is a
  JSON array with one object per bundled venue carrying `"src":"bundled"` (and the sim venue as
  `"src":"user"` on `moto_sim`), ending `---END <crc32>---`. Then `tracks put {\"id\":1001,...}` with a
  minimal one-layout venue (the 256 B console cap). Expect an empty-body ack frame, then `tracks get`
  lists it with `"src":"user"`, and after a reset it is STILL listed (it went through
  `/tracks/user.bin`). Negative: `tracks put {}` → `ERR 0x0703: bad venue json`; repeat the put until
  the table is full (TRK_MAX_USER = 4) → `ERR 0x0703: table full`.
- [ ] **F2b-6. R5 coredump.** `coredump` with no stored image → `ERR 0x0703: coredump: none stored`.
  Then `dbg crash`, let it reboot, and `coredump` → a `---BEGIN coredump <n>---` frame with a Base64
  body (n is the Base64 length) and `---END <crc32>---`; decode it host-side and check the ELF magic
  (`espcoredump.py info_corefile` is the full check). Then `coredump clear` → empty ack, and `coredump`
  → `none stored` again. Evidence: the three frames/errors.
- [ ] **F2b-7. R11 menu lock at 10 km/h.** `dbg sim laps 5`, then long-MODE while the sim is between
  3 and 10 km/h (the capture's slowest corner) and again above 10. Expect: the low-speed press OPENS
  the menu (previously refused) with no `menu locked` line, the high-speed press logs
  `ui: menu locked (<n> >= 10 km/h, §20.7)` with the real speed, and the MOVING glyph is on only above
  10 km/h. Evidence: the two log lines plus a photo of the strip.
- [ ] **F2b-8. R10 cfg lock.** `config set {"units":1}` from the console while repeatedly toggling Units
  in the menu. Expect: no silently reverted write — every change ends up in `config get`, and any
  contention shows as `ui: units: cfg busy, not saved` or `ERR 0x0703: config busy; retry` rather than
  a lost value. Also: a `config set` with the SAME values logs no `pipe: config reloaded` line at all
  (§15.2 only-on-change). Evidence: the `config get` before/after plus the absence of the reload line.
- [ ] **F2b-9. PR #101 M3 NOT SAVED.** Fill the filesystem below the 5 % reserve (`dbg logtest 60000`
  repeatedly until `dbg fs` shows `FULL`), then create a track from the menu. Expect: the VENUE card
  reads `VENUE / NOT SAVED`, `errlog` carries `E_STO_FULL` arg 1, and the venue is still armed (the
  Layout item offers it) but gone after a reset. Evidence: the card photo + the errlog line.
- [ ] **F2b-10. PR #101 M4 resumed layout.** Lock a layout, let a lap run, `dbg crash` mid-lap, and on
  the next boot open the menu. Expect: `Layout: <the locked layout's name>`, NOT `Auto`, and the first
  MODE press advances to the NEXT layout rather than silently switching away from the running one.
  Evidence: the two menu photos.
- [ ] **F2b-11. L9 ui stack high-water on BOTH deep paths** (context for #53; reviewer's frame math).
  `dbg mem` right after a `drag_rows_refill` burst (`dbg sim drag`) and again right after a full
  refresh (UP+DOWN 2 s). Record both `ui` high-water figures: the event branch
  (`handle_event -> drag_rows_refill`, ~424 B of locals) and the render branch
  (`render_and_refresh -> disp_refresh`) are mutually exclusive, so the 340 B figure in #53 may have
  been taken on only one of them. Evidence: both `dbg mem` lines, pasted into #53.
```

- [ ] **Step 5: issues to file (note only — F4 owns the filing).** Hand F4: (a) `core/trk.h`'s ownership comment must name the **cmd/console task** as a third unlocked reader (`TRACKS_GET`) with the captured-count argument — `trk.h` is F0's file; (b) PR #101 **M5** (the venue linter passes a call anywhere in the function) and **M6** (no host test for `logger.c`'s file-level corrupt-`/tracks/user.bin` paths); (c) §18.1's missing `E_CONN_XFER_ABORT`; (d) §15.3's `power_state`/`partial_count` never populated (Plan 6 / the ui hand-off). This plan files none of them itself.

- [ ] **Step 6: self-check the fence** — `git diff --stat main...HEAD` must list **only**: `components/app/include/app/{lt_ipc,cmd,lt_proto,pipeline,logger,log_pause,session_policy,lt_nvs,ui}.h`, `components/app/sys/lt_nvs.c`, `components/app/{logger/logger.c,logger/log_pause.c,pipeline/pipeline.c,pipeline/session_policy.c,cmd/cmd.c,ui/ui.c,CMakeLists.txt}`, `components/drivers/export_serial/export_serial.c`, `test/{CMakeLists.txt,test_log_pause.c,test_session_policy.c}`, `test_apps/core_selftest/main/CMakeLists.txt`, `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `docs/bench/next-bench-day.md`, this plan. Anything else is a fence breach — revert it.

- [ ] **Step 7: commit and open the PR**

```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/bench/next-bench-day.md
git commit -m "f2b(t9): spec §12.1/§12.3/§12.4/§12.6/§13.3/§18.1/§18.4 + Appendix A amendments, bench items F2b-1..F2b-11"
gh pr create --base main --head fix-fw-logger-cmd \
  --title "F2b — firmware logger + cmd: §12 session lifecycle, TRACKS/COREDUMP ops, cfg lock, 10 km/h menu lock" \
  --body-file .superpowers/sdd/2026-10-13-fix-F2b/pr-body.md
```

The PR body must carry: the gate results from Step 1 verbatim, the DRAM table from Step 2 with the **measured** figures, the finding ids closed (app:M2 M3 M4 M6 M11 M13 L1 L2 L7 L9-noted, drv:H4 M7 L15 L16 L27, pr:M3 (#108) M4, audit §3.3 #20–#25), the rulings applied (R4 R5 R8 R10 R11 R20), the spec amendments, the bench checklist items added, and the four issues handed to F4. No claim without a pasted command result (`superpowers:verification-before-completion`).

- [ ] **CHECKPOINT** — ledger line written to `.superpowers/sdd/2026-10-13-fix-F2b/progress.md` ("Task 9 complete <sha>"), branch pushed, and this exact resume prompt recorded in the ledger: "Resume plan docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md at Task 10 — none remain; the plan is complete and PR <url> is open awaiting review + bench gate f2b-d1 (ledger .superpowers/sdd/2026-10-13-fix-F2b/progress.md); worktree <path>; branch fix-fw-logger-cmd."

- [ ] **SESSION C END CHECKPOINT** — the PR URL, the gate block and the measured DRAM figures are all in the ledger; `git status` is clean; the branch is pushed. Bench gate `f2b-d1` is the user's, not a subagent's.

---

## Cleanup done

Every deletion and rewrite this plan performs, so a reviewer can check that nothing was left as a promise.

| What | Where | Action | Task |
|---|---|---|---|
| `dbg gps` / `dbg imu` / `dbg power` stub arm printing "not in plan 03" | `export_serial.c:968-971` | **deleted** (ruling R5: remove + amend §18.4) | 7 |
| `dbg gps raw <on\|off> \| imu raw <on\|off> \| power <..>` usage line | `export_serial.c:977` | **deleted** | 7 |
| `dbg` usage text missing `reset`; help string missing `safe`/`sim` (L27) | `export_serial.c:973-978`, `:1238` | rewritten to match the code exactly | 7 |
| `synth_fix()` + the `ring_push` retry loop (second producer on `g_fix_ring`) | `export_serial.c:521-538`, `:570-591` | **deleted**, body moved to `pipeline.c`'s `logtest_synth_fix()` (R8) | 7, 5 |
| `printf("OTA-READY\r\n")` and the two `OTA-ERR`/`OTA-END` `\r\n` tokens (L15) | `export_serial.c:1139,1145,1153` | rewritten to a bare `\n` | 7 |
| `while (sto_list_next(...) == 1)` reporting `-1` as `(empty)` (L16) | `export_serial.c:607` | rewritten to capture and report `rc` | 7 |
| `s_hdr.gps_hz = 0; /* real rate filled by the pipeline (3.4) */` | `logger.c:329` | **deleted**; the field is filled from `req->gps_hz` | 4 |
| `/* calib block left zero until 3.4 */` | `logger.c:330` | **deleted**; the block is filled from `s_calib` | 4 |
| `rebuild_sum`'s "HDR 99 + VENUE 41 + LAP_ACC_CAP 3072 + … < the old 4096 cap" proof | `logger.c:246-247` | rewritten (the sections stream; only each accumulator's own cap binds) | 4 |
| `LT_ASSERT_RET(sizeof s_batch == BATCH_CAP, …)` ×2 (tautology) | `logger.c:485`, `:570` | **deleted**, replaced by one file-scope `_Static_assert` + a real precondition each | 4 |
| `LT_ASSERT_VOID(s_open, …)` in `handle_event` (tautology; caller gates it) | `logger.c:643` | **deleted**, replaced by a timestamp precondition | 4 |
| `LT_ASSERT_RET(strlen(e->oldest) < STO_NAME_MAX, …)` (tautology over `snprintf`) | `logger.c:798` | replaced by the "never the open session" postcondition | 4 |
| `n++`/`fix_n++`/`fused_n++` inside `LT_ASSERT_*` conditions (M3) | `logger.c:709,725,736,751` | increment hoisted out of the assertion | 4 |
| `ev->type <= EV_FAULT` bounds (stale since `EV_VENUE_LOST`) | `logger.c:644`, `pipeline.c:216,229,420` | rewritten to `< EV_COUNT` (design §3) | 4, 5 |
| `/* power_state: no owner in plan 03 */` + `/* partial_count */ 0` | `pipeline.c:465` | rewritten to name Plan 6 and the filed issue | 5 |
| `LT_ASSERT_VOID(out->min_speed_cms != 0xFFFFu, …)` (restates the line above) | `pipeline.c:202` | replaced by `max >= min` | 5 |
| the inline `LOGGER_OPEN_SESSION` request in the `CFG_GPS_SIM` block | `pipeline.c:1150-1152` | **deleted**, folded into `session_open()` | 5 |
| `steps++`/`retries++`/`chunks++` inside `LT_ASSERT_*` conditions (M3, 8 sites) | `cmd.c:88,347,393,414,476,661,718,737` | increment hoisted out of the assertion | 6 |
| `cmd.c`'s header claim that LIST/OPEN/READ "are Task 5" and TRACKS/COREDUMP/OTA/TIME_SYNC "are later sessions" | `cmd.c:3-5` | rewritten; only `TIME_SYNC` is still absent | 6 |
| `cmd.c`'s "a concurrent-request abort is a BLE/WiFi-transport concern" | `cmd.c:752` | rewritten to state the gap and who must close it | 6 |
| `static char s_json[3072]` + `static sess_ent_t s_sess[64]` as separate statics | `cmd.c:51`, `:517` | **deleted**, replaced by the `s_asm` union (−3072 B) | 6 |
| `op_read`'s silent `offset = 0` on a short payload (L7) | `cmd.c:819-822` | replaced by an `E_CONN_PROTO` error chunk | 6 |
| `cmd.h`'s "the OTA_* ops … (session 5.4)" | `cmd.h:36-37` | rewritten | 1 |
| `lt_ipc.h`'s "producers (ui/conn/power) land in later sessions" | `lt_ipc.h` cmd_q block | rewritten (no conn/power task exists) | 1 |
| `s_gspeed_kmh = MENU_LOCK_SPEED_KMH` / `= 0` proxy assignments (R11) | `ui.c:1442-1443` | **deleted**; the cases keep the events and do nothing | 8 |
| `LT_ASSERT_VOID(s_model.lap_no == 1 && s_model.big_kind == BIG_NONE, …)` and the two sibling tautologies | `ui.c:1103-1104`, `:723`, `ui_apply_mode`'s tail | **deleted**, each replaced by a real pre/postcondition | 8 |
| `ui.h`'s "stack 2560 moto_sim", "drains g_evt_q", "no display panel exists yet … logs the dirty box" | `ui.h:3-7` | rewritten wholesale | 8 |
| spec §12.1/§12.3/§12.4/§12.6/§13.3/§18.1/§18.4 stale text (incl. "commanded by the power task", "921600 baud", "OTA over serial is not implemented") | design spec | rewritten per Task 9 Step 3 | 9 |

---

## Self-Review

**1. Spec coverage.** R4 → Tasks 2 (still-pause), 4 (TIME_MAP/CALIB/`gps_hz`/`.sum`), 3+5 (lifecycle, mode change, venue found/lost, every build). R5 → Tasks 1 (op codes), 6 (`TRACKS_GET/PUT`, `COREDUMP_GET/CLEAR`), 7 (console verbs), 9 (§18.1/§18.4 text). R8 → Tasks 1, 5, 7. R10 → Tasks 1 (lock), 6 (`op_config_set` + only-on-change), 8 (four ui sites). R11 → Tasks 1, 5, 8. R20 → Tasks 4, 5, 6, 8 (own files only). Design §3 event bounds → Tasks 4, 5. Design §4 gates → Task 9 Steps 1–2. Design §5 PR #101 M3/M4 → Tasks 4 Step 10, 5 Steps 6/9, 8 Steps 3/4; M5/M6 handed to F4 (Task 9 Step 5). app:M2 M3 M4 M6 M11 M13, L1 L2 L7 (L9 → bench F2b-11), drv:H4 M7 L15 L16 L27 — each in the Cleanup table with a task. Audit §3.3 #20–#25 all covered; #22's `MARK`/`POWER` are annotated reserved rather than implemented (no feature exists), which §12.3's amendment states. Deliberately **not** in scope and named as such: `TIME_SYNC` (Plan 8), `status.state` (F2a), the errlog dedup M1 (F2a), `lt_cal` persistence M14 (Plan 8).

**2. Placeholder scan.** No "TBD"/"TODO"/"handle errors appropriately"/"similar to Task N". Every code step carries compilable C; every rename carries the `sed` line and the count of sites to re-read; every bench item carries the command and the expected line. The two deliberate "if another plan already did this, keep theirs" notes (`lt_ipc.h`'s cmd_q block, `pipeline.c:150`'s `s_gps_seen` comment) are coordination facts, not placeholders.

**3. Type consistency.** `log_pause_t` / `log_pause_init/still/motion/tick/active` + `LOG_PAUSE_STILL_MS` are spelled identically in Tasks 2, 4 and the §12.6 amendment. `session_policy` / `ses_action_t` (`SES_ACT_NONE/OPEN/CLOSE/REOPEN`) / `ses_input_t` (`SES_IN_VENUE_FOUND/VENUE_ANNOUNCE/VENUE_LOST/MODE_CHANGE`) match across Tasks 3, 5 and the §12.1 amendment. `LOG_RES_TIMEMAP`/`LOG_RES_CALIB` + `u.tmap`/`u.calib` match Tasks 1, 4, 5. `logger_submit_time_map`/`logger_submit_calib` are declared in Task 1, defined in Task 4, called in Task 5. `LOGGER_PUT_TRACK` + `cmd_tracks_payload()` + the rc set {0, −2, −5, −6, −7} match Task 4's `put_track()` and Task 6's `op_tracks_put()` exactly. `pipeline_speed_kmh()` is declared in Task 1, defined in Task 5, read in Task 8. `EV_VENUE_NOT_SAVED` is defined in Task 1, set in Task 4, tested in Task 8. `ui_post_venue(venue_id, layout_id, gps_us)` with the layout in `arg32` matches Task 5's two call sites and Task 8's reader. `s_asm.json`/`s_asm.sess` and `CMD_JSON_CAP` match Tasks 1 and 6. `below_reserve()` is defined once (Task 4) and used by both `open_session` and `tracks_save`.
