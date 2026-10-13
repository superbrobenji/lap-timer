# F2a — Firmware supervision + OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make OTA work and stick on the production (`moto_neo6m`) build, enforce the hardware-independent half of §19.5's preconditions, give the §17.2 supervisor its missing heap/stack/`EV_FAULT`/task-WDT coverage, and make every supervision/OTA/driver claim in code, comments and the spec true.

**Architecture:** Every decision the supervisor and `ota_begin` make becomes a **pure function** in a new IDF-free TU (`components/app/sys/sup_policy.c`, host-tested with Unity and compiled on the Xtensa toolchain by `core_selftest`); the IDF-bound code in `sup.c`/`ota.c` only gathers inputs and performs the side effects the decision asks for. The OTA-validate gate stops depending on a GPS fix and starts depending on whether the build's GPS driver is a *live receiver* (a new `gps_profile_t.live` field, 0 for every stub and the sim). The Plan-03 GPS/IMU stubs stop reporting success. The board HAL stops letting a floating CHRG pin answer a power question.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2 (`esp_ota_ops`, `esp_task_wdt`, `heap_caps`, `esp_core_dump`, `esp_log`, FreeRTOS static tasks/queues/notifications), Unity host tests (`test/`), ccache-off firmware builds via `source tools/idf-env.sh`.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding; rulings **R1, R2, R6, R7, R9, R19, R20, R28**, §3 `EV_FAULT`, §4 gates, §5 filed items). Fact sheets: `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` §7.1–7.3 and the two reviewer reports under `.superpowers/reviews/2026-10-13/` (`app-layer.md`, `drivers-build-tools.md`). Product spec: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§4.7, §17.1, §17.2, §17.5, §17.6, §17.8, §17.9, §18.4, §19.3–19.6, §20.8, Appendix A).

## Global Constraints

- **Power of 10** on all firmware code: functions > 20 code lines carry ≥ 2 *meaningful* `LT_ASSERT_*`/`CORE_ASSERT_*` (never abort on target); every loop bounded by a constant or table field; no recursion, no heap after init, no goto, no new function pointers; ≤ 60 code lines per function. `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0 (F4 merges first, so this plan is born under the *stricter* linter: rule 3 blocking, statement-based length counting, `#if 0` exclusion, the rule-9 sink list).
- **R20 (own files):** no padding/tautological assertions and no side effects inside an assertion condition. Where a function would otherwise need padding, reduce it below 20 code lines instead. No new rows in `docs/power-of-10-deviations.md`.
- **Zero warnings** (memory rule P10/10): clean ccache-disabled builds of `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 ./build.sh moto_sim build`, `idf.py -C test_apps/core_selftest build`, and the host `test/` suite under `-Wall -Wextra -Werror -Wshadow -Wconversion`, plus a gcc-16 sweep of `test/build/compile_commands.json`. Fix at source — never `-Wno-*`, never a pragma.
- **`app/sup_policy.h` and `app/sup_policy.c` stay IDF-free** (`stdint.h`/`stdbool.h` + `core/core.h` + `app/lt_consts.h` only), like `app/lt_proto.h`: both the host harness and `core_selftest` compile them.
- **Build via** `source tools/idf-env.sh` (pins ESP-IDF to `.idf-version` 5.3.2 and asserts the version). Never fall back to raw 5.5.1.
- **No flashing by any subagent.** Every on-device claim in this plan is a *bench item appended to `docs/bench/next-bench-day.md`*, run by the controller with the user. After an OTA push, never reset or open the lap-timer port for ≥ 35 s (validation lands at 30 s).
- **Console line endings:** host tools send bare `\r` to the ESP32 console; a stray `\n` becomes image byte 0.
- **DRAM budget:** report free static DRAM for both envs (baselines `moto_sim` 4192 B, `moto_neo6m` 5616 B). This plan's budget is **≤ 128 B** net; anything beyond 256 B must name its reclaim.
- **Commit trailer** on every commit in this plan:
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```
- **Checkpoint protocol:** after the last step of every task, stop at the `CHECKPOINT` step and hand control back for review. Do not start the next task until the reviewer says go. A checkpoint reports: the task number, the files touched, the exact commands run with their rc and warning count, the host-test pass counts, the DRAM figure if it moved, and any ruling you had to interpret. If a step cannot be completed as written, stop at the checkpoint and say why — never improvise a different design.

---

## File ownership and the enumerated exceptions

F2a owns, exclusively, for the duration of the wave:

`components/app/{supervisor,ota,link}/*` · `components/app/sys/{lt_nvs,lt_sys,status,lt_rtc,sup_policy}.c` · `components/app/include/app/{lt_sup,ota,lt_err,lt_consts,status,lt_nvs,lt_rtc,link,lt_assert,sup_policy}.h` · `components/drivers/board_devkit_v1/*` · `components/drivers/storage_internal/*` · `components/drivers/{gps_neo6m,imu_mpu6050}/*` · `components/drivers/display_epaper_ssd1680/display_epaper.c` (the one L21 line; the directory is unclaimed by any other plan) · `components/lt_hal/include/hal/{gps,board}.h` · `main/app_main.c` · `main/build_config.h.in` · and in `components/drivers/export_serial/export_serial.c` **only** the logging-level sites (`run_cmd`, `run_stream`, `cmd_ota`), the `SER_ASM_MAX` block, the `dbg` usage `printf`s and the two false-comment blocks at `:14-21` and `:1067-1084`.

Design §1 reserves "the F1a/F2a lines" inside other plans' files. These are **all** of F2a's foreign-file edits — nothing else, and every one is named line-exact in the task that makes it:

| File (owner) | F2a's exact edit | Task |
|---|---|---|
| `components/app/pipeline/pipeline.c` (F2b) | `s_gps_live`/`s_still_since_ms` statics; the `prof->live` read in `pipeline_init_drivers`; the still-edge store in `on_fix`'s motion-edge branch; `pipeline_sensors_ok()` replacing `pipeline_gps_seen()`; new `pipeline_still_for_ms()`; `sup_wdt_subscribe`/`sup_wdt_feed` in `pipeline_task` | 2, 3 |
| `components/app/include/app/pipeline.h` (F2b) | `pipeline_gps_seen` prototype → `pipeline_sensors_ok` + `pipeline_still_for_ms` | 2 |
| `components/app/logger/logger.c` (F2b) | `sup_wdt_subscribe(HB_LOGGER)` at task entry; `sup_wdt_feed()` once per loop and in the three `% 16` yield sites | 3 |
| `components/app/include/app/cmd.h` (F2b) | one added `CMD_JSON_CAP` macro (nothing else in that file) | 8 |
| `test/CMakeLists.txt` (shared) | the `test_sup_policy` and `test_batt_trim` target blocks | 1, 6 |
| `test_apps/core_selftest/main/CMakeLists.txt` (F4) | `sup_policy.c` appended to `SRCS` | 1 |
| `CMakeLists.txt` (F4) | the `CHRG` option + its validation + the `CFG_CHRG_WIRED` derivation (one contiguous block beside the other §4.6 flags) | 4 |
| `main/CMakeLists.txt` (shared) | `espcoredump` appended to `LT_MAIN_REQUIRES` | 9 |
| `docs/bench/next-bench-day.md` (F4) | the appended `### F2a bench items` section (design §4 asks every plan to do this) | 10 |
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (F4) | §15.2, §17.1, §17.2, §18.4, §19.4, §19.5, §19.6, Appendix A — *only* the lines listed in Task 10 | 10 |

F2b merges after F2a and rebases onto these lines. Anything F2a cannot own is **handed over in writing** (Task 10's handoff table), never edited.

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `components/app/include/app/sup_policy.h`, `components/app/sys/sup_policy.c`, `test/test_sup_policy.c` | pure, host-tested supervision + OTA policy: stall episodes, heap hysteresis, stack threshold, changed-flag walk, §19.5 precondition decision, OTA receive staleness | 1 |
| `components/app/include/app/lt_consts.h` | new Appendix-A policy constants (`HEAP_LOW_KB`, `HEAP_CLEAR_KB`, `STACK_LOW_B`, `OTA_STILL_MIN_MS`, `OTA_RECV_TIMEOUT_S`, `OTA_CLOSE_TIMEOUT_MS`, `HEAP_INTEGRITY_PERIOD_S`) | 1 |
| `components/lt_hal/include/hal/gps.h` | `gps_profile_t.live` — "this driver talks to a real receiver" | 2 |
| `components/app/pipeline/pipeline.c`, `include/app/pipeline.h` | `pipeline_sensors_ok()`, `pipeline_still_for_ms()` (F2a's two accessors only) | 2 |
| `components/app/supervisor/sup.c`, `include/app/lt_sup.h` | §17.2 heap/stack/`EV_FAULT`/integrity, stall-episode rate limit, `sup_wdt_*`, R1 validate gate, `ota_poll` drive, `SYS_NVS_DEAD` bit, false comments | 3 |
| `components/app/ota/ota.c`, `include/app/ota.h`, `components/app/sys/status.c`, `include/app/status.h` | §19.5 preconditions + stale-pend clear, `ota_poll` timeout, honest battery precondition, §19.3 self-check, `LT_STATE_OTA` | 4 |
| `components/app/sys/lt_nvs.c`, `include/app/{lt_nvs,lt_err}.h`, `components/app/sys/lt_rtc.c`, `include/app/lt_rtc.h`, `include/app/lt_assert.h` | errlog dedup on code (M1), new error codes, RTC seqlock (L4), four false comments | 5 |
| `components/drivers/board_devkit_v1/{board.c,CMakeLists.txt,include/batt_pure.h,host/batt_trim.c}`, `hal/board.h`, `CMakeLists.txt`, `main/build_config.h.in`, `test/test_batt_trim.c` | ISR IRAM (C1), insertion sort (R19), CHRG contract + `CFG_CHRG_WIRED` (M11), 30 ms per-pin debounce (L6/L17), bring-up rc ladder (L18), pin-map rows (L22) | 6 |
| `components/drivers/storage_internal/storage_internal.c`, `components/drivers/display_epaper_ssd1680/display_epaper.c` | bounded `sto_write` (M10), mounted-but-dead (L19), assertions + checked returns (L20), `disp_sleep` state (L21) | 7 |
| `components/drivers/gps_neo6m/gps_neo6m.c`, `components/drivers/imu_mpu6050/imu_mpu6050.c` | honest stubs: `-ENODEV` (R28/H3) | 7 |
| `components/drivers/export_serial/export_serial.c`, `include/app/cmd.h` | `ESP_LOG_NONE` for transfers (R9), `SER_ASM_MAX` static assert (M7), `dbg` usage (L27), two false comments | 8 |
| `main/app_main.c`, `main/CMakeLists.txt`, `components/app/link/link.c` | NVS-init failure flag (L3), `board_init` rc, §17.6 coredump row, `link_task` §17.1 rationale, GPIO 4 ownership note | 9 |
| spec §§, `docs/bench/next-bench-day.md`, three `gh issue create` calls | final wave: gates, DRAM, amendments, filings, bench items | 10 |

---

# Session 1 — policy core and its two consumers (Tasks 1–4)

### Task 1 — SERIAL — Pure supervision/OTA policy (`sup_policy`) + host tests

**Files:**
- Create: `components/app/include/app/sup_policy.h`, `components/app/sys/sup_policy.c`, `test/test_sup_policy.c`
- Modify: `components/app/include/app/lt_consts.h`, `test/CMakeLists.txt` (new target block), `test_apps/core_selftest/main/CMakeLists.txt` (one `SRCS` entry)

**Interfaces:**
- Consumes: `LT_ASSERT_RET` (`app/lt_assert.h`, IDF-free: `stdint.h` + `core/core.h`), `OTA_MAX_IMG_BYTES` (`app/lt_consts.h`).
- Produces — every name below is used verbatim by Tasks 3 and 4:

```c
/* app/sup_policy.h */
enum { SUP_STALL_LOG = 0x1u, SUP_STALL_RESTART = 0x2u };
typedef struct { uint32_t last_hb; uint32_t stalled_s; bool msg_logged; } sup_stall_t;
uint8_t sup_stall_step(sup_stall_t *st, uint32_t cur_hb, uint32_t stall_s, uint32_t period_s,
                       bool restartable);
enum { SUP_HEAP_NONE = 0u, SUP_HEAP_ENTER = 1u, SUP_HEAP_CLEAR = 2u };
uint8_t sup_heap_eval(uint32_t free_b, bool flagged);
bool    sup_stack_low(uint32_t high_water_b);
uint8_t sup_flag_next_change(uint32_t prev, uint32_t cur, uint8_t from);
enum { OTA_PRE_OK = 0u, OTA_PRE_REFUSE = 1u, OTA_PRE_CLOSE_SESSION = 2u, OTA_PRE_CLEAR_STALE = 3u };
typedef struct {
    uint32_t still_ms;             /* pipeline_still_for_ms() */
    uint32_t size;                 /* declared image size from the OTA_BEGIN payload */
    bool     in_flight;            /* an OTA receive is already open */
    bool     session_open;         /* a logger session is still open (true until a close acks) */
    bool     batt_ok;              /* §19.5 battery/charger gate passed */
    bool     flag_pending;         /* lt_ota_pending_get() */
    bool     slot_pending_verify;  /* the RUNNING slot is ESP_OTA_IMG_PENDING_VERIFY */
} ota_pre_t;
uint8_t ota_precond_eval(const ota_pre_t *in);
bool    ota_recv_stale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_s);
```

- [ ] **Step 1: Add the constants** to `components/app/include/app/lt_consts.h`, after the OTA block:

```c
/* §17.2 supervision thresholds (Appendix A). HEAP_CLEAR_KB is new: §17.2 gives only the 40 KB
 * entry threshold, and a single threshold makes SYS_HEAP_LOW -- and therefore EV_FAULT -- flap
 * once per second on a heap hovering at the line, so the flag releases with 8 KB of hysteresis
 * (audit-fix-wave design R6). The "< 20 KB -> conn_stop()" half of §17.2 is struck: there is no
 * conn task (BLE is sub-project C). */
#define HEAP_LOW_KB          40      /* §17.2/§4.8: free 8-bit heap below this -> SYS_HEAP_LOW */
#define HEAP_CLEAR_KB        48      /* ... and at or above this the flag releases (R6) */
#define STACK_LOW_B          256     /* §17.2: task stack high-water below this -> E_SYS_STACK_LOW */
#define HEAP_INTEGRITY_PERIOD_S 60   /* §17.1: heap_caps_check_integrity_all cadence */

/* §19.5/§19.6 OTA policy that is not a battery/charger figure. */
#define OTA_STILL_MIN_MS     30000u  /* §19.5: no EV_MOTION for 30 s before an OTA_BEGIN is accepted */
#define OTA_RECV_TIMEOUT_S   30      /* §19.6: an OTA_RECV with no OTA_DATA for this long is aborted */
#define OTA_CLOSE_TIMEOUT_MS 2000u   /* §19.5: bounded wait for LOGGER_CLOSE_SESSION's ack */
```

- [ ] **Step 2: Write the failing tests** — `test/test_sup_policy.c`:

```c
#include "unity.h"
#include "app/sup_policy.h"
#include "app/lt_consts.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}

/* ---- §17.2 stall episodes (R9: one log line per episode, not one per second) ---- */
static void test_stall_progressing_is_quiet(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 0, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 8, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT32(8, st.last_hb);
    TEST_ASSERT_EQUAL_UINT32(0, st.stalled_s);
}
static void test_stall_logs_once_per_episode(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 0, .msg_logged = false };
    for (int s = 1; s < 5; s++) TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 7, 5, 1, false));  /* 5 s: fires */
    TEST_ASSERT_EQUAL_UINT32(5, st.stalled_s);
    for (int s = 0; s < 10; s++) TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT32(15, st.stalled_s);                 /* keeps the true duration */
}
static void test_stall_new_episode_logs_again(void)
{
    sup_stall_t st = { .last_hb = 7, .stalled_s = 4, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 7, 5, 1, false));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 9, 5, 1, false));          /* recovered */
    TEST_ASSERT_FALSE(st.msg_logged);
    st.stalled_s = 4;
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG, sup_stall_step(&st, 9, 5, 1, false));
}
static void test_stall_restartable_adds_restart_bit(void)
{
    sup_stall_t st = { .last_hb = 1, .stalled_s = 0, .msg_logged = false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 1, 1, true));           /* 1 s < 1 s? no: == */
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_LOG | SUP_STALL_RESTART, sup_stall_step(&st, 1, 2, 1, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_STALL_RESTART, sup_stall_step(&st, 1, 2, 1, true));
}
static void test_stall_rejects_bad_args(void)
{
    sup_stall_t st = { 0, 0, false };
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(NULL, 1, 5, 1, true));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 0, 1, true));
    TEST_ASSERT_EQUAL_UINT8(0, sup_stall_step(&st, 1, 5, 0, true));
}

/* ---- §17.2 heap hysteresis ---- */
static void test_heap_hysteresis(void)
{
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(60u * 1024u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_ENTER, sup_heap_eval(HEAP_LOW_KB * 1024u - 1u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_LOW_KB * 1024u - 1u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_CLEAR_KB * 1024u - 1u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_CLEAR, sup_heap_eval(HEAP_CLEAR_KB * 1024u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(HEAP_LOW_KB * 1024u, false));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_NONE,  sup_heap_eval(0u, true));
    TEST_ASSERT_EQUAL_UINT8(SUP_HEAP_ENTER, sup_heap_eval(0u, false));
}

/* ---- §17.2 stack ---- */
static void test_stack_low(void)
{
    TEST_ASSERT_TRUE(sup_stack_low(0u));
    TEST_ASSERT_TRUE(sup_stack_low(STACK_LOW_B - 1u));
    TEST_ASSERT_FALSE(sup_stack_low(STACK_LOW_B));
    TEST_ASSERT_FALSE(sup_stack_low(340u));                 /* #53's measured ui headroom: not low */
}

/* ---- §17.2 EV_FAULT: walk the changed bits ---- */
static void test_flag_next_change(void)
{
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x5u, 0x5u, 0));
    TEST_ASSERT_EQUAL_UINT8(0,  sup_flag_next_change(0x0u, 0x1u, 0));
    TEST_ASSERT_EQUAL_UINT8(3,  sup_flag_next_change(0x8u, 0x0u, 0));        /* a CLEARED bit counts */
    TEST_ASSERT_EQUAL_UINT8(10, sup_flag_next_change(0x0u, 0x401u, 1));
    TEST_ASSERT_EQUAL_UINT8(31, sup_flag_next_change(0x0u, 0x80000000u, 0));
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x0u, 0x1u, 1));
    TEST_ASSERT_EQUAL_UINT8(32, sup_flag_next_change(0x0u, 0xFFFFFFFFu, 32));
}

/* ---- §19.5 OTA preconditions ---- */
static ota_pre_t good_pre(void)
{
    ota_pre_t p = { .still_ms = OTA_STILL_MIN_MS, .size = 600000u, .in_flight = false,
                    .session_open = false, .batt_ok = true, .flag_pending = false,
                    .slot_pending_verify = false };
    return p;
}
static void test_precond_ok(void)
{
    ota_pre_t p = good_pre();
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_OK, ota_precond_eval(&p));
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(NULL));
}
static void test_precond_refusals(void)
{
    ota_pre_t p = good_pre(); p.in_flight = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.size = 0u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.size = OTA_MAX_IMG_BYTES + 1u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.size = OTA_MAX_IMG_BYTES;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_OK, ota_precond_eval(&p));           /* inclusive bound */
    p = good_pre(); p.still_ms = OTA_STILL_MIN_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));       /* §19.5 moving */
    p = good_pre(); p.batt_ok = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.flag_pending = true; p.slot_pending_verify = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));       /* a real image awaits */
}
static void test_precond_close_session_runs_last(void)
{
    /* The close is a SIDE EFFECT on a live session, so it must only be asked for once every
     * other precondition already holds -- a refused push must never end a riding session. */
    ota_pre_t p = good_pre(); p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p));
    p.still_ms = 0u;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.session_open = true; p.batt_ok = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
    p = good_pre(); p.session_open = true; p.flag_pending = true; p.slot_pending_verify = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_REFUSE, ota_precond_eval(&p));
}
static void test_precond_stale_flag_is_cleared_not_refused(void)
{
    /* app:C1's dead end: ota_pend set with no PENDING_VERIFY slot refused every later push
     * forever. R1: clear it and proceed. */
    ota_pre_t p = good_pre(); p.flag_pending = true; p.slot_pending_verify = false;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLEAR_STALE, ota_precond_eval(&p));
    p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p));   /* close first */
}
static void test_precond_second_eval_terminates(void)
{
    /* The caller's loop is: eval -> CLOSE_SESSION -> close -> eval. The second eval must never
     * return CLOSE_SESSION again, or the caller would loop. */
    ota_pre_t p = good_pre(); p.session_open = true;
    TEST_ASSERT_EQUAL_UINT8(OTA_PRE_CLOSE_SESSION, ota_precond_eval(&p));
    p.session_open = false;
    uint8_t act = ota_precond_eval(&p);
    TEST_ASSERT_TRUE(act == OTA_PRE_OK || act == OTA_PRE_CLEAR_STALE);
}

/* ---- §19.6 receive staleness (wrap-safe u32 ms) ---- */
static void test_recv_stale(void)
{
    TEST_ASSERT_FALSE(ota_recv_stale(1000u, 1000u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(1000u + 29999u, 1000u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_TRUE(ota_recv_stale(1000u + 30000u, 1000u, OTA_RECV_TIMEOUT_S));
    /* the 49-day millisecond wrap must not read as "fresh for 49 days" */
    TEST_ASSERT_TRUE(ota_recv_stale(30000u, 0xFFFFFFFFu, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(10u, 0xFFFFFFF0u, OTA_RECV_TIMEOUT_S));
    TEST_ASSERT_FALSE(ota_recv_stale(1000u, 1000u, 0u));          /* a 0 s timeout never fires */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stall_progressing_is_quiet);
    RUN_TEST(test_stall_logs_once_per_episode);
    RUN_TEST(test_stall_new_episode_logs_again);
    RUN_TEST(test_stall_restartable_adds_restart_bit);
    RUN_TEST(test_stall_rejects_bad_args);
    RUN_TEST(test_heap_hysteresis);
    RUN_TEST(test_stack_low);
    RUN_TEST(test_flag_next_change);
    RUN_TEST(test_precond_ok);
    RUN_TEST(test_precond_refusals);
    RUN_TEST(test_precond_close_session_runs_last);
    RUN_TEST(test_precond_stale_flag_is_cleared_not_refused);
    RUN_TEST(test_precond_second_eval_terminates);
    RUN_TEST(test_recv_stale);
    return UNITY_END();
}
```

- [ ] **Step 3: Wire the host target** — append to `test/CMakeLists.txt`, and add `test_sup_policy` to the hold-back list in the generic `foreach` (the `if(NOT name STREQUAL ...)` chain) so it is not added twice:

```cmake
# test_sup_policy (audit fix wave F2a Task 1): the pure supervision/OTA policy decisions
# (app/sup_policy.h -- IDF-free: stdint/stdbool + core/core.h + app/lt_consts.h). Linked like
# test_epd_pure links its driver's host/*.c: the one source directly, plus the app component's
# include root for "app/sup_policy.h"/"app/lt_consts.h"/"app/lt_assert.h". `core` is still linked
# for core_assert_fail (LT_ASSERT_RET's reporter).
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/app)
add_core_test(test_sup_policy ${APP_DIR}/sys/sup_policy.c)
target_include_directories(test_sup_policy PRIVATE ${APP_DIR}/include)
```

- [ ] **Step 4: Run to fail** —

```bash
cd /Users/benji/projects/personal/lap-timer
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
```
Expected: `test_sup_policy` fails to compile — `app/sup_policy.h: No such file or directory`.

- [ ] **Step 5: Implement** `components/app/include/app/sup_policy.h` with an include guard, `<stdbool.h>`/`<stdint.h>`, the Produces block above verbatim, and this file comment:

```c
/* app/sup_policy.h -- the pure, host-testable decisions behind the §17.2 supervisor loop and the
 * §19.5 OTA preconditions (audit fix wave 2026-10-13, rulings R1/R2/R6/R9).
 *
 * IDF-free on purpose (stdint.h/stdbool.h + core/core.h via app/lt_assert.h + app/lt_consts.h),
 * exactly like app/lt_proto.h: this TU compiles into the firmware (components/app globs it), into
 * the host harness (test/test_sup_policy.c) and onto the Xtensa toolchain
 * (test_apps/core_selftest), so every threshold and every precondition ordering rule is proven by
 * a real test instead of by a bench observation that cannot be scripted. The IDF-bound callers
 * (supervisor/sup.c, ota/ota.c) only gather inputs and perform the side effects a decision asks
 * for; they contain no policy of their own. No statics live here -- each decision is a function of
 * its arguments plus the caller's own state struct (sup_stall_t), so the same function can be
 * driven for HB_COUNT tasks without cross-talk. */
```

and `components/app/sys/sup_policy.c`:

```c
#include "app/sup_policy.h"

#include "app/lt_assert.h"
#include "app/lt_consts.h"

#define SUP_POLICY_ASSERT_CODE 0x0BC0   /* Power of 10 rule 5 (app/lt_assert.h); this file's code */

/* §17.2 line 1, one watched task, one supervisor tick. Returns the actions the caller must take:
 * SUP_STALL_LOG exactly ONCE per stall episode (R9 -- the old code re-armed every stall_s and so
 * printed an ESP_LOGE at 1 Hz straight into framed transfers and the raw OTA stream, app:M12),
 * and SUP_STALL_RESTART while a restartable task (the pipeline) is over its window. stalled_s
 * keeps accumulating across the episode so the logged figure is the true stall duration. */
uint8_t sup_stall_step(sup_stall_t *st, uint32_t cur_hb, uint32_t stall_s, uint32_t period_s,
                       bool restartable)
{
    LT_ASSERT_RET(st != NULL, SUP_POLICY_ASSERT_CODE, 0u);              /* caller passes &s_watch[i].stall */
    LT_ASSERT_RET(stall_s > 0u && period_s > 0u, SUP_POLICY_ASSERT_CODE, 0u);  /* a 0 window/period would never fire */
    if (cur_hb != st->last_hb) {                 /* progressing: the episode (if any) is over */
        st->last_hb = cur_hb;
        st->stalled_s = 0u;
        st->msg_logged = false;
        return 0u;
    }
    st->stalled_s += period_s;
    if (st->stalled_s < stall_s) return 0u;
    uint8_t act = 0u;
    if (!st->msg_logged) { st->msg_logged = true; act = (uint8_t)(act | SUP_STALL_LOG); }
    if (restartable) act = (uint8_t)(act | SUP_STALL_RESTART);
    return act;
}

/* §17.2 heap line + §4.8's "min-free heap stays above 40 KB" hard rule, which had no enforcement
 * at all (app:M5: SYS_HEAP_LOW and E_SYS_HEAP_LOW have zero writers). free_b is the CURRENT free
 * 8-bit heap, not heap_caps_get_minimum_free_size(): the min-ever figure is monotonically
 * non-increasing, so a flag driven by it could never clear and R6's recovery threshold would be
 * dead code. The caller logs the min-ever figure as the errlog arg instead. */
uint8_t sup_heap_eval(uint32_t free_b, bool flagged)
{
    if (!flagged && free_b < (uint32_t)HEAP_LOW_KB * 1024u)   return SUP_HEAP_ENTER;
    if (flagged  && free_b >= (uint32_t)HEAP_CLEAR_KB * 1024u) return SUP_HEAP_CLEAR;
    return SUP_HEAP_NONE;
}

/* §17.2 stack line (app:M5: E_SYS_STACK_LOW had no writer -- this is the check that would have
 * surfaced #53's 340 B ui headroom automatically). high_water_b is
 * uxTaskGetStackHighWaterMark()'s value: on ESP-IDF Xtensa StackType_t is 1 byte, so the count
 * IS bytes. */
bool sup_stack_low(uint32_t high_water_b) { return high_water_b < (uint32_t)STACK_LOW_B; }

/* §17.2's "EV_FAULT on changes": the index of the lowest sys_flags bit at or above `from` that
 * differs between prev and cur, or 32 when there is none. Bounded by 32 iterations (rule 2);
 * a SET and a CLEARED bit both count as a change, so the ui's fault strip learns about recovery
 * as well as onset. */
uint8_t sup_flag_next_change(uint32_t prev, uint32_t cur, uint8_t from)
{
    uint32_t diff = prev ^ cur;
    for (uint8_t b = from; b < 32u; b++) { if ((diff >> b) & 1u) return b; }
    return 32u;
}

/* §19.5, the hardware-independent half (ruling R2; app:H1 found ota_begin checking almost none of
 * it). Ordering is the design: every cheap, side-effect-free refusal is evaluated BEFORE
 * OTA_PRE_CLOSE_SESSION, because closing the logger session ends a rider's session and must never
 * happen for a push that was going to be refused anyway. OTA_PRE_CLEAR_STALE is R1's escape from
 * the app:C1 dead end (an ota_pend flag with no PENDING_VERIFY slot refused every later push
 * until a USB flash). Struck from §19.5 by R2: power state CONNECTED (there is no power task --
 * Plan 6) and fus_is_still (fusion is never calibrated -- Plan 8/#93); the 30 s no-motion window
 * is honoured through still_ms. */
uint8_t ota_precond_eval(const ota_pre_t *in)
{
    LT_ASSERT_RET(in != NULL, SUP_POLICY_ASSERT_CODE, OTA_PRE_REFUSE);   /* caller passes its own local */
    if (in->in_flight) return OTA_PRE_REFUSE;
    if (in->size == 0u || in->size > OTA_MAX_IMG_BYTES) return OTA_PRE_REFUSE;
    if (in->still_ms < OTA_STILL_MIN_MS) return OTA_PRE_REFUSE;
    if (!in->batt_ok) return OTA_PRE_REFUSE;
    if (in->flag_pending && in->slot_pending_verify) return OTA_PRE_REFUSE;
    if (in->session_open) return OTA_PRE_CLOSE_SESSION;      /* side effect: only once all of the above hold */
    return in->flag_pending ? OTA_PRE_CLEAR_STALE : OTA_PRE_OK;
}

/* §19.6: an OTA_RECV that has seen no OTA_DATA for timeout_s is dead (app:M8 -- a vanished peer
 * used to wedge the state machine until a reboot, and every later push then returned
 * E_OTA_PRECOND). Milliseconds in a u32 so the stamp is a single naturally-atomic word across the
 * console/cmd writer and the supervisor reader; the subtraction is modular, so the 49-day wrap
 * reads correctly rather than as 49 days of freshness. */
bool ota_recv_stale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_s)
{
    if (timeout_s == 0u) return false;
    LT_ASSERT_RET(timeout_s <= 4000000u, SUP_POLICY_ASSERT_CODE, false);   /* * 1000 must not overflow u32 */
    return (uint32_t)(now_ms - last_ms) >= timeout_s * 1000u;
}
```

- [ ] **Step 6: Run to pass** —

```bash
cmake --build test/build -j8 && ctest --test-dir test/build --output-on-failure
```
Expected: `test_sup_policy` 14/14; every pre-existing suite still green; 0 warnings.

- [ ] **Step 7: Compile the policy on the target toolchain** — append `sup_policy.c` to `test_apps/core_selftest/main/CMakeLists.txt`'s `idf_component_register(SRCS ...)` list, next to `assert_support.c`, with this comment above the `set(APP_INCLUDE_DIR ...)` line:

```cmake
# APP_POLICY_SRC: test/test_sup_policy.c (audit fix wave F2a) exercises app/sup_policy.c, which is
# IDF-free by design (see app/sup_policy.h) -- so the suite runs on the Xtensa toolchain too, per
# §22.3, by compiling that ONE app source here rather than by REQUIRES app (which would drag
# nvs_flash/gps/imu/display into this core-only selftest). It needs no include dir of its own:
# APP_INCLUDE_DIR below already resolves "app/sup_policy.h", "app/lt_consts.h" and
# "app/lt_assert.h".
set(APP_POLICY_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/sys/sup_policy.c")
```
and change the register call to `idf_component_register(SRCS "main.c" "${TEST_DIR}/assert_support.c" "${APP_POLICY_SRC}" ${WRAPPERS} ...)`.

- [ ] **Step 8: Gate + commit** —

```bash
source tools/idf-env.sh
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # rc 0
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build 2>&1 | grep -ciE "warning|error"   # 0
idf.py -C test_apps/core_selftest build                                 # rc 0, 0 warnings
git add components/app/include/app/sup_policy.h components/app/sys/sup_policy.c \
        components/app/include/app/lt_consts.h test/test_sup_policy.c test/CMakeLists.txt \
        test_apps/core_selftest/main/CMakeLists.txt
git commit   # feat(app): sup_policy -- pure supervision/OTA policy decisions, host + target tested (F2a T1, R2/R6/R9)
```

- [ ] **CHECKPOINT** — report per the Checkpoint protocol. Stop.

---

### Task 2 — SERIAL (after 1) — `gps_profile_t.live` and the two pipeline accessors

**Files:**
- Modify: `components/lt_hal/include/hal/gps.h`, `components/app/pipeline/pipeline.c`, `components/app/include/app/pipeline.h`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
```c
/* hal/gps.h */
typedef struct { uint8_t max_rate_hz; uint32_t baud; uint8_t has_pps; uint8_t live; const char *name; } gps_profile_t;
/* app/pipeline.h */
bool     pipeline_sensors_ok(void);     /* REPLACES pipeline_gps_seen(): a fix was seen, OR this build has no live receiver */
uint32_t pipeline_still_for_ms(void);   /* ms since the last EV_STILL edge; 0 while moving */
```

- [ ] **Step 1: Add the `live` field** to `gps_profile_t` in `components/lt_hal/include/hal/gps.h`, between `has_pps` and `name`:

```c
/* GPS receiver profile advertised by gps_init (§5.1). */
typedef struct {
    uint8_t  max_rate_hz;      /* 5 for NEO-6M, 10 for M10 */
    uint32_t baud;             /* 38400 / 115200 */
    uint8_t  has_pps;
    /* 1 only when this driver talks to a real receiver that can produce a real fix; 0 for the
     * Plan-03 stubs (gps_neo6m until Plan 08) and for gps_sim. Added by the 2026-10-13 audit fix
     * wave (ruling R1): §19.4's OTA-validate gate used to require a GPS fix, which a stub can
     * never deliver -- so every OTA to moto_neo6m stayed PENDING_VERIFY and the bootloader
     * reverted it at the next reset, permanently (audit §7.1, app:C1, drv:H3). The supervisor now
     * asks pipeline_sensors_ok(), which treats "this build has no live receiver" as healthy. A
     * driver that omits the field in its designated initialiser gets 0, i.e. "not live" -- the
     * fail-safe direction for this gate, which is why gps_sim needs no edit. */
    uint8_t  live;
    const char *name;
} gps_profile_t;
```

- [ ] **Step 2: Replace the pipeline's gate state.** In `components/app/pipeline/pipeline.c`, replace the `s_gps_seen` comment block (`:144-146`) with:

```c
/* §19.4 OTA-validate gate + §19.5 stillness precondition (audit fix wave F2a, R1/R2).
 * s_gps_seen: set once the pipeline has ingested at least one GPS fix (set-once, monotonic).
 * s_gps_live: this build's GPS driver advertised gps_profile_t.live (latched once in
 *   pipeline_init_drivers; 0 for the Plan-03 stubs and the sim).
 * s_still_since_ms: esp_timer milliseconds at the last EV_STILL edge, STILL_MOVING while moving,
 *   and 0 before any motion edge at all -- i.e. "still since boot", which is the honest reading
 *   for a build whose GPS never produces a fix, and is what lets an OTA proceed there.
 * All three are read cross-task by the supervisor / ota_begin through the two accessors at the
 * bottom of this file. Each is a single naturally-atomic word, written only by this task, so a
 * volatile load/store needs no lock (the same contract s_gps_seen already had). */
static volatile bool     s_gps_seen;
static volatile bool     s_gps_live;
#define STILL_MOVING 0xFFFFFFFFu
static volatile uint32_t s_still_since_ms;
```

- [ ] **Step 3: Stamp the still edge.** In `on_fix`'s §9.1 motion-edge branch (`:502-508`), add one line inside the `if (!s_have_moving || moving != s_moving)` body, after `s_moving = moving;`:

```c
        s_moving = moving;
        /* R2: the §19.5 "EV_MOTION not seen for 30 s" precondition reads this stamp through
         * pipeline_still_for_ms(). Monotonic esp_timer ms, not fix->mono_us, so the window is
         * measured against the same clock the supervisor and ota_begin use. */
        s_still_since_ms = moving ? STILL_MOVING : (uint32_t)(esp_timer_get_time() / 1000);
        emit_simple(moving ? (uint8_t)EV_MOTION : (uint8_t)EV_STILL, fix->gps_us, fix->mono_us);
```

- [ ] **Step 4: Latch `live`.** In `pipeline_init_drivers` (`:717-731`), after the existing `sup_boot_report(BOOT_GPS, ...)` line, add:

```c
    /* R1: latch the driver's own claim about itself. A failed gps_init is, by definition, not a
     * live receiver, so `gps_ok &&` is part of the latch rather than a separate case. */
    s_gps_live = (gps_ok && prof->live != 0u);
```

- [ ] **Step 5: Replace the accessor.** Replace `pipeline_gps_seen` (`:995-1000`) with:

```c
bool pipeline_sensors_ok(void)
{
    /* §19.4's OTA-validate condition (R1, replacing pipeline_gps_seen): true once a GPS fix has
     * been ingested, OR immediately and permanently on a build whose GPS driver is not a live
     * receiver. On moto_sim this is the fix condition as before; on moto_neo6m it is the second
     * arm, which is the whole point -- without it the stub's gps_poll()==0 made every OTA to the
     * only release env revert at the next reset (audit §7.1). Once the Plan-08 UBX driver sets
     * live = 1, the gate tightens back to "a real fix was seen" with no code change here. */
    return s_gps_seen || !s_gps_live;
}

uint32_t pipeline_still_for_ms(void)
{
    /* §19.5 (R2): milliseconds since the last EV_STILL edge; 0 while moving. 0 is also what a
     * caller sees for the first millisecond of stillness, which is correct -- the caller's test
     * is `>= OTA_STILL_MIN_MS`. Before any motion edge the stamp is 0, so this returns the whole
     * uptime: a device that has never reported motion has been still for its entire life. */
    uint32_t since = s_still_since_ms;
    if (since == STILL_MOVING) return 0u;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (uint32_t)(now - since);       /* modular: correct across the 49-day ms wrap */
}
```

- [ ] **Step 6: Replace the prototype** in `components/app/include/app/pipeline.h:29`:

```c
/* §19.4 OTA-validate gate (audit fix wave F2a, R1). True once a GPS fix has been ingested, or
 * unconditionally on a build whose GPS driver is not a live receiver (gps_profile_t.live == 0:
 * the Plan-03 stubs and gps_sim) -- see pipeline.c. Read by the supervisor. */
bool pipeline_sensors_ok(void);
/* §19.5 OTA stillness precondition (R2): ms since the last EV_STILL edge, 0 while moving, the
 * whole uptime before any motion edge. Read by ota_begin. */
uint32_t pipeline_still_for_ms(void);
```

- [ ] **Step 7: Verify it fails to link** — `source tools/idf-env.sh && IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build`. Expected: FAIL with `undefined reference to 'pipeline_gps_seen'` from `sup.c.o` (Task 3 fixes the one call site). This is the *only* consumer — a tree-wide grep confirmed `sup.c:233` is the sole caller.

- [ ] **Step 8: Commit** (the build is intentionally red between Tasks 2 and 3; the two tasks are split because they live in different plans' files and must be reviewable apart):

```bash
git add components/lt_hal/include/hal/gps.h components/app/pipeline/pipeline.c \
        components/app/include/app/pipeline.h
git commit   # feat(hal,pipeline): gps_profile_t.live + pipeline_sensors_ok/still_for_ms (F2a T2, R1/R2)
```

- [ ] **CHECKPOINT** — report. Note explicitly that `moto_sim` does not link yet and why. Stop.

---

### Task 3 — SERIAL (after 2) — `sup.c`: §17.2 heap/stack/`EV_FAULT`/integrity, R1 gate, R7 task WDT

**Files:**
- Modify: `components/app/supervisor/sup.c`, `components/app/include/app/lt_sup.h`, `components/app/include/app/lt_err.h` (two codes), `components/app/logger/logger.c` (5 lines, enumerated), `components/app/pipeline/pipeline.c` (2 lines)

**Interfaces:**
- Consumes: `sup_stall_t`, `sup_stall_step`, `sup_heap_eval`, `sup_stack_low`, `sup_flag_next_change`, `SUP_STALL_*`, `SUP_HEAP_*` (Task 1); `pipeline_sensors_ok()` (Task 2); existing `g_evt_q`, `g_ui_evt_q` (`app/lt_ipc.h`), `logger_notify()` (`app/logger.h`), `EV_FAULT` (`core/event.h`), `errlog_add`, `sys_flags_get/set/clear`.
- Produces:
```c
/* app/lt_sup.h */
void sup_wdt_subscribe(uint8_t hb_id);   /* esp_task_wdt_add(NULL) + a boot line + an errlog on failure */
void sup_wdt_feed(void);                 /* esp_task_wdt_reset(); safe from any subscribed task */
enum { ..., SYS_RECOVERY_MODE = 14, SYS_NVS_DEAD = 15 };
/* app/lt_err.h */
E_SYS_WDT_SUBSCRIBE = 0x050D, E_SYS_HEAP_CORRUPT = 0x050E, E_NVS_INIT = 0x050F
```

- [ ] **Step 1: Add the two sys_flags/error-code entries.** In `components/app/include/app/lt_sup.h`, replace the `/* M11: bit 15 ... */` comment with a real bit:

```c
    SYS_RECOVERY_MODE,   /* bit 14 (§17.5 recovery-mode amendment): level-2 safe mode */
    SYS_NVS_DEAD,        /* bit 15 (audit fix wave F2a, app:L3): lt_nvs_init() failed, so every
                          * NVS accessor runs on handle 0 -- boot_cnt stays 0, crash-loop
                          * detection and the safe-mode gate are dead, and open_session reuses
                          * S00000_001 every boot. app_main used to boot on with one ESP_LOGE.
                          * Bit 15 is the last STATUS-visible flag (status_build masks to u16). */
```
In `components/app/include/app/lt_err.h`, inside the `E_SYS_*` block after `E_LOG_CLOSE_TIMEOUT`:

```c
    E_SYS_WDT_SUBSCRIBE = 0x050D, /* arg = hb_id: esp_task_wdt_add() failed for that task (§17.1) */
    E_SYS_HEAP_CORRUPT  = 0x050E, /* arg = uptime_s: heap_caps_check_integrity_all() failed (§17.1) */
    E_NVS_INIT          = 0x050F, /* arg = 0: lt_nvs_init() failed; SYS_NVS_DEAD set (app:L3) */
```

- [ ] **Step 2: Rework `watch_t` + `check_stalls`.** Replace `watch_t` and `check_stalls` in `sup.c`:

```c
typedef struct {
    TaskHandle_t task;
    uint8_t      hb_id;
    uint32_t     stall_s;
    sup_stall_t  stall;          /* R9: episode state owned by sup_policy.c (last_hb/stalled_s/msg_logged) */
    bool         stack_logged;   /* §17.2: E_SYS_STACK_LOW fires once per task per boot */
    bool         used;
} watch_t;
```
and in `sup_register_task`, replace the `.last_hb = g_hb[hb_id], .stalled_s = 0` initialiser fields with:
```c
    s_watch[hb_id] = (watch_t){ .task = task, .hb_id = hb_id, .stall_s = stall_s,
                                .stall = { .last_hb = g_hb[hb_id], .stalled_s = 0, .msg_logged = false },
                                .stack_logged = false, .used = true };
```
and replace `check_stalls`'s body with:

```c
static void check_stalls(void)
{
    for (int i = 0; i < HB_COUNT; i++) {
        watch_t *w = &s_watch[i];
        if (!w->used || w->stall_s == 0) continue;
        /* w->hb_id is stored state (bounded once at registration); re-checking it here before it
         * indexes g_hb[] guards against future slot corruption reading past g_hb[HB_COUNT]. */
        LT_ASSERT_VOID(w->hb_id < HB_COUNT, SUP_ASSERT_CODE);
        uint8_t act = sup_stall_step(&w->stall, g_hb[w->hb_id], w->stall_s,
                                     (uint32_t)(SUP_PERIOD_MS / 1000), w->hb_id == HB_PIPELINE);
        if (act & SUP_STALL_LOG) {
            /* R9: ONCE per stall episode. This line used to print every second for as long as a
             * task was wedged, landing inside ---BEGIN/---END bodies and the raw OTA byte stream
             * (app:M12) -- exactly the degraded conditions where the operator needs the log. */
            ESP_LOGE(TAG, "task %d stalled %us", w->hb_id, (unsigned)w->stall.stalled_s);
            (void)errlog_add(E_SYS_TASK_STALL, w->hb_id);
        }
        if (act & SUP_STALL_RESTART) {
            /* §17.2: a stalled pipeline restarts. R6 strikes §17.2's "after saving RTC state":
             * the pipeline OWNS the RTC snapshot (lt_rtc_save runs on its task on every S/F and
             * sector event), and a wedged pipeline cannot be asked to save -- so the resume point
             * is the last gate, by design, not by omission. F3: the HW reset reason will be
             * ESP_RST_SW (§17.5 normal), so leave a marker the next boot folds in as abnormal. */
            close_session_before_restart(SES_END_STALL, 500);
            (void)lt_counters_flush(true);
            lt_stall_flag_set();
            esp_restart();
        }
    }
}
```

- [ ] **Step 3: Add the three missing §17.2 checks + the WDT wrapper** to `sup.c`. New statics next to `s_safe_mode_cleared`:

```c
static uint32_t s_flags_prev;        /* §17.2 "EV_FAULT on changes": last published sys_flags value */
static uint8_t  s_stack_scan;        /* §17.2 stack check: round-robin cursor over s_watch[] */
static uint32_t s_integrity_at_s;    /* §17.1 heap_caps_check_integrity_all: last run (uptime s) */
```
and these functions (placed above `sup_task`):

```c
/* §17.1 (app:H2 / R7): the task-WDT subscription, wrapped so every app task subscribes the same
 * way and the result is VISIBLE at boot. §17.1 claimed "all six app tasks and both idle tasks
 * subscribed"; only the supervisor and the ui actually were, so a wedged logger -- the Plan-7-T9
 * LittleFS-compaction failure mode -- was detected by check_stalls alone, which logs
 * E_SYS_TASK_STALL and restarts nothing but the pipeline. Call once from a task entry, then
 * sup_wdt_feed() once per loop iteration and inside any long operation (§17.1).
 * link_task deliberately does NOT subscribe -- see the §17.1 amendment: it is a best-effort
 * telemetry drain and a stalled or absent peer must never reset the lap-timer. */
void sup_wdt_subscribe(uint8_t hb_id)
{
    LT_ASSERT_VOID(hb_id < HB_COUNT, SUP_ASSERT_CODE);
    esp_err_t e = esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "wdt subscribe hb=%u rc=%d", (unsigned)hb_id, (int)e);
    if (e != ESP_OK) (void)errlog_add(E_SYS_WDT_SUBSCRIBE, hb_id);
}

void sup_wdt_feed(void) { (void)esp_task_wdt_reset(); }

/* §17.2 heap line + §4.8's "min-free heap stays above 40 KB" hard rule (app:M5: SYS_HEAP_LOW and
 * E_SYS_HEAP_LOW had zero writers firmware-wide, so the hard rule had no enforcement at all).
 * The flag tracks the CURRENT free size so it can clear with R6's hysteresis; the errlog arg
 * carries the min-ever figure the hard rule is written against. */
static void check_heap(void)
{
    uint32_t cur  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t minf = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    bool flagged = (sys_flags_get() & (1u << SYS_HEAP_LOW)) != 0;
    uint8_t act = sup_heap_eval(cur, flagged);
    if (act == SUP_HEAP_ENTER) {
        sys_flags_set(SYS_HEAP_LOW);
        (void)errlog_add(E_SYS_HEAP_LOW, minf);
        ESP_LOGE(TAG, "heap low: free %u B (min ever %u B)", (unsigned)cur, (unsigned)minf);
    } else if (act == SUP_HEAP_CLEAR) {
        sys_flags_clear(SYS_HEAP_LOW);
        ESP_LOGW(TAG, "heap recovered: free %u B", (unsigned)cur);
    }
}

/* §17.2 stack line (app:M5). One registered task per tick, round-robin, so the loop stays short
 * and every task is sampled once per HB_COUNT seconds; once per task per boot (stack_logged),
 * because a task that dipped below the line will keep reading below it -- the high-water mark
 * never recovers. arg packs the task id and the headroom: (hb_id << 16) | high_water. */
static void check_stack(void)
{
    uint8_t id = s_stack_scan;
    s_stack_scan = (uint8_t)((s_stack_scan + 1u) % HB_COUNT);
    watch_t *w = &s_watch[id];
    if (!w->used || w->stack_logged || w->task == NULL) return;
    uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(w->task);
    if (!sup_stack_low(hw)) return;
    w->stack_logged = true;
    (void)errlog_add(E_SYS_STACK_LOW, ((uint32_t)id << 16) | (hw & 0xFFFFu));
    ESP_LOGE(TAG, "stack low task %u free %u", (unsigned)id, (unsigned)hw);
}

/* §17.2's last line, "sys_flags -> ui (fault icons); EV_FAULT on changes" (app:M5: never
 * emitted -- the EV_FAULT constant was used only as an upper bound in assertions). One EV_FAULT
 * per CHANGED bit onto BOTH event queues: g_evt_q is the logger's copy (EV_FAULT is a logged
 * §12.3 EVENT record, so a session file finally carries when each fault appeared and cleared) and
 * g_ui_evt_q is the ui's. A second producer onto a FreeRTOS queue is legal -- xQueueSend is
 * multi-producer safe -- whereas g_stream_ring is a relaxed-head SPSC ring with the pipeline as
 * its single documented producer, so stream_push() is NEVER called from here (design §2 R6).
 * arg16 = the sys_flags bit index; flags = 1 when the bit was SET, 0 when it was CLEARED.
 * Bounded by 32 iterations. The early return keeps s_flags_prev un-advanced until both queues
 * exist (sup_start() runs before app_main's lt_ipc_init(), so the first tick has neither), which
 * is what makes the boot-time flags -- SAFE_MODE, STORAGE_DEAD, NVS_DEAD -- reach the ui at all. */
static void emit_faults(void)
{
    if (g_evt_q == NULL || g_ui_evt_q == NULL) return;
    uint32_t cur = sys_flags_get();
    if (cur == s_flags_prev) return;
    int64_t now = esp_timer_get_time();
    for (uint8_t b = sup_flag_next_change(s_flags_prev, cur, 0);
         b < 32u; b = sup_flag_next_change(s_flags_prev, cur, (uint8_t)(b + 1u))) {
        event_t ev = { .type = EV_FAULT, .flags = (uint8_t)((cur >> b) & 1u), .arg16 = b,
                       .gps_us = 0, .mono_us = now, .arg32 = 0, .arg32b = 0 };
        (void)xQueueSend(g_evt_q, &ev, 0);        /* drop-newest: the next change re-publishes */
        (void)xQueueSend(g_ui_evt_q, &ev, 0);
    }
    s_flags_prev = cur;
    logger_notify();                              /* drain the EVENT record promptly */
}

/* §17.1's last protection: heap_caps_check_integrity_all() every HEAP_INTEGRITY_PERIOD_S. §17.1
 * says "in debug builds only"; this project ships one build flavour (no poisoning is configured
 * in sdkconfig.defaults, so a #if CONFIG_HEAP_POISONING_* guard would compile the line out on
 * every env and leave §17.1 claiming a check that never runs). It therefore runs unconditionally:
 * without poisoning the walk still validates every allocator block header and canary, it is a
 * header walk over a few hundred blocks once a minute, and the supervisor has the budget. §17.1
 * is amended accordingly (Task 10). */
static void check_heap_integrity(uint32_t uptime_s)
{
    if (uptime_s - s_integrity_at_s < (uint32_t)HEAP_INTEGRITY_PERIOD_S) return;
    s_integrity_at_s = uptime_s;
    if (!heap_caps_check_integrity_all(true)) {
        (void)errlog_add(E_SYS_HEAP_CORRUPT, uptime_s);
        ESP_LOGE(TAG, "heap integrity check FAILED");
    }
}
```
Add the includes `#include "esp_heap_caps.h"` and `#include "app/logger.h"` and `#include "app/sup_policy.h"` to `sup.c`'s include block (`core/event.h` arrives transitively through `app/lt_ipc.h`).

- [ ] **Step 4: Fix the R1 gate + drive the new checks** in `sup.c`. In `ota_try_validate`, replace the `healthy` expression and its comment:

```c
/* §19.4: mark a pending image valid (cancel rollback) once it is not in safe mode, storage is
 * mounted, the sensor gate is satisfied and it has run for OTA_VALID_UPTIME_S. Until then a
 * panic/WDT/brownout lets the bootloader roll back (logged as E_OTA_ROLLBACK on the next boot).
 *
 * R1 (audit §7.1, app:C1, drv:H3): this gate used to require pipeline_gps_seen(), which the
 * Plan-03 gps_neo6m stub can never satisfy (gps_poll always returns 0) -- so on moto_neo6m, the
 * only release env, every applied image stayed PENDING_VERIFY, the bootloader reverted it at the
 * next reset, and the un-cleared ota_pend flag then refused every later push until a USB flash.
 * pipeline_sensors_ok() is the same fix condition on moto_sim and is true on any build whose GPS
 * driver is not a live receiver (gps_profile_t.live == 0). */
    bool healthy = !(f & (1u << SYS_SAFE_MODE)) && !(f & (1u << SYS_STORAGE_DEAD)) &&
                   pipeline_sensors_ok() && uptime_s >= OTA_VALID_UPTIME_S;
```
Also fix the stale reference inside `ota_reboot_check`'s long comment (`sup.c:162`): `ota_try_validate's \`!SAFE_MODE && pipeline_gps_seen()\` gate` → `` ota_try_validate's `!SAFE_MODE && pipeline_sensors_ok()` gate ``.
Then in `sup_task`'s loop, replace the stub-ladder comment with the real calls:

```c
        ota_lifecycle(uptime_s);                  /* §19.4: reboot / boot-decide / validate / recv timeout */
        safe_recovery_clear_check(uptime_s);      /* §17.5: uptime auto-clear (see helper) */
        check_heap();                             /* §17.2 heap line (+ §4.8's hard rule) */
        check_stack();                            /* §17.2 stack line, one task per tick */
        check_heap_integrity(uptime_s);           /* §17.1, every 60 s */
        emit_faults();                            /* §17.2 "sys_flags -> ui; EV_FAULT on changes" */

        /* Still absent by design, each with an owner (design §5): the GPS §7.5 and IMU §8.7
         * ladders and their CMD_GPS_LADDER_STEP / CMD_IMU_RECOVER commands, and the storage §13
         * ladder, need the real drivers -- filed against Plan 8. The §17.2 temperature throttle
         * needs a calibrated IMU temperature producer -- Plan 8/#93. The "< 20 KB -> conn_stop()"
         * heap step needs a conn task -- sub-project C. */

        g_hb[HB_SUPERVISOR]++;
```
and replace `sup_task`'s own `(void)esp_task_wdt_add(NULL);` with `sup_wdt_subscribe(HB_SUPERVISOR);` and the loop's `(void)esp_task_wdt_reset();` with `sup_wdt_feed();`.

- [ ] **Step 5: Subscribe the pipeline and the logger (R7).** Exactly these edits, and nothing else, in the two F2b files:

`components/app/pipeline/pipeline.c`, in `pipeline_task` after `sup_register_task(HB_PIPELINE, ...)`:
```c
    sup_wdt_subscribe(HB_PIPELINE);   /* §17.1 (R7): a wedged pipeline must panic, not just stall */
```
and as the first statement of its `for (;;)` body:
```c
        sup_wdt_feed();
```
(`app/lt_sup.h` is already included by `pipeline.c`.)

`components/app/logger/logger.c`, in `logger_task` after `sup_register_task(HB_LOGGER, ...)`:
```c
    sup_wdt_subscribe(HB_LOGGER);     /* §17.1 (R7): a logger wedged in LittleFS compaction -- the
                                       * Plan-7-T9 bench failure -- must panic, not hang silently */
```
as the first statement after the loop's `ulTaskNotifyTake`:
```c
        sup_wdt_feed();
```
and in each of the three yielding listing/eviction sites, alongside the existing `vTaskDelay(1)`:
```c
            if (++n % 16 == 0) { sup_wdt_feed(); vTaskDelay(1); }   /* §17.1: a long listing resets the WDT inside */
```
(`session_count()` ~`:150` and `evict_scan_oldest()` ~`:623`), plus in `eviction_check`'s inter-pass delay (~`:688`):
```c
        sup_wdt_feed();
        vTaskDelay(1);                                        /* one tick between passes */
```

- [ ] **Step 6: Fix `sup.c`'s three false comments.** Exact old → new:

| Site | Old | New |
|---|---|---|
| `sup.c:3-7` | `3.2 runs the reduced §17.2 loop: … The GPS/IMU/ storage ladders and heap/stack/temperature/OTA checks are stubbed until their subsystems land (3.3/3.4). Only the supervisor itself is live now; other tasks call sup_register_task as they are created.` | `Runs the §17.2 loop: heartbeat-stall watch with a once-per-episode report, the task-WDT subscribe/feed wrappers every app task uses, the crash-loop uptime tick, the batched counter flush, the §19.4 OTA lifecycle (boot decide / validate / reboot / receive timeout), the §17.5 safe-mode auto-clear, the §17.2 heap and stack checks, §17.1's 60 s heap-integrity walk, and EV_FAULT on every sys_flags change. Still absent, each with an owner: the GPS/IMU/storage ladders and the temperature throttle (Plan 8), the "< 20 KB -> conn_stop()" heap step (no conn task; sub-project C).` |
| `sup.c:144` | `/* §17.2: a stalled pipeline restarts (RTC snapshot save lands in 3.5). F3: …` | the R6 comment written in Step 2 (the save never lands: the pipeline owns the snapshot and a wedged pipeline cannot be asked to save; §17.2 amended) |
| `sup.c:293-294` | `/* Ladders (GPS §7.5 / IMU §8.7 / storage §13) and heap/stack/temp checks: their subsystems arrive in 3.3/3.4; wired here then. */` | the "Still absent by design, each with an owner" block written in Step 4 |

- [ ] **Step 7: Build + lint** —

```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build     # rc 0, 0 warnings -- the Task-2 link error is gone
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build   # rc 0, 0 warnings
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # rc 0
ctest --test-dir test/build --output-on-failure    # unchanged, all green
```
`check_stalls` is now 22 code lines with 1 assertion → add the second meaningful one if the linter says so: `LT_ASSERT_VOID(w->stall_s <= 60u, SUP_ASSERT_CODE);` is padding and is **not** acceptable (R20). If rule 5 trips, split the `SUP_STALL_RESTART` arm into `static void restart_stalled_pipeline(void)` instead, bringing `check_stalls` under 20 lines.

- [ ] **Step 8: Commit** —

```bash
git add components/app/supervisor/sup.c components/app/include/app/lt_sup.h \
        components/app/include/app/lt_err.h components/app/logger/logger.c \
        components/app/pipeline/pipeline.c
git commit   # feat(sup): §17.2 heap/stack/EV_FAULT + §17.1 WDT for pipeline/logger, R1 validate gate (F2a T3, R1/R6/R7/R9)
```

- [ ] **CHECKPOINT** — report; include the two build DRAM figures. Stop.

---

### Task 4 — SERIAL (after 3) — `ota.c` §19.5 preconditions, receive timeout, honest battery gate, §19.3 self-check; `status.c` `LT_STATE_OTA`

**Files:**
- Modify: `components/app/ota/ota.c`, `components/app/include/app/ota.h`, `components/app/sys/status.c`, `components/app/include/app/status.h`, `components/app/supervisor/sup.c` (one call in `ota_lifecycle`), `components/app/include/app/lt_err.h` (one code)

**Interfaces:**
- Consumes: `ota_pre_t`, `ota_precond_eval`, `OTA_PRE_*`, `ota_recv_stale` (Task 1); `pipeline_still_for_ms()` (Task 2); `logger_request_sync`, `log_request_t`, `LOGGER_CLOSE_SESSION` (`app/lt_ipc.h`), `SES_END_RESTART` (`core/ses.h`), `OTA_STILL_MIN_MS`/`OTA_RECV_TIMEOUT_S`/`OTA_CLOSE_TIMEOUT_MS` (Task 1).
- Produces:
```c
/* app/ota.h */
void ota_poll(void);     /* called once per supervisor tick: §19.6 receive timeout */
/* app/status.h */
enum { LT_STATE_IDLE = 0, LT_STATE_OTA = 1 };
/* app/lt_err.h */
E_OTA_BATT_UNWIRED = 0x0807
```

- [ ] **Step 1: Add the error code** to `lt_err.h`'s OTA block (codes are append-only):

```c
    E_OTA_BATT_UNWIRED = 0x0807,  /* arg = 0: §19.5's battery/charger precondition was SKIPPED
                                   * because this build has no power hardware (CFG_CHRG_WIRED == 0,
                                   * #54/Plan 6). Logged once per boot so the deviation is visible
                                   * in the errlog instead of hidden in a floating pin (drv:M11). */
```
and, per ruling R3, add the deviation note the ruling asks this header to carry, above `E_OTA_PRECOND`:

```c
    /* OTA (§17.7, §19.6). NOTE (audit fix wave ruling R3, 2026-10-13): these six values disagree
     * with §17.7's table on five of six codes. The FIRMWARE values below are authoritative -- they
     * are the ones in the field, in the project's operating notes and in every errlog already
     * captured; §17.7 and Appendix A are amended to match (owner: plan F4). Any host-side decoder
     * must be written from this enum, not from §17.7's historical table. */
```

- [ ] **Step 2: Introduce the `CFG_CHRG_WIRED` build flag.** `ota.c` is its first consumer (Task 6 is the second), so it is declared here. Three contiguous lines in the top-level `CMakeLists.txt`, placed with the other §4.6 flags (after the `DEVUX` block at `:119-125`) — this is F2a's **only** edit to that file, so F4 must not touch these lines:

```cmake
# §3.3 CHRG (GPIO 39) wiring, audit fix wave F2a (drv:M11, #54). GPIO 34-39 have no internal
# pull resistors on classic ESP32, so an unwired CHRG pin floats -- and a float reading low used
# to make board_charger_present() answer "charging", which bypassed §19.5's battery precondition
# outright. OFF (the prototype, and the default) makes board_charger_present() honour
# hal/board.h's documented "false if not wired" contract and makes §19.5's battery check an
# explicitly-logged skip instead of a silent pass. Plan 6 adds the external 100k pull-up and
# flips this ON.
set(CHRG "OFF" CACHE STRING "CHRG sense (GPIO39) is wired with its external pull-up: ON | OFF")
if(NOT CHRG MATCHES "^(ON|OFF)$")
    message(FATAL_ERROR "CHRG must be 'ON' or 'OFF' (got '${CHRG}')")
endif()
if(CHRG)
    set(CFG_CHRG_WIRED 1)
else()
    set(CFG_CHRG_WIRED 0)
endif()
```
and the matching line in `main/build_config.h.in`, after `CFG_HAS_DEVUX`:

```c
/* CHRG (§3.3 GPIO 39) is wired with its external 100k pull-up (audit fix wave F2a, drv:M11/#54).
 * 0 on this prototype: board_charger_present() then reports false unconditionally rather than
 * letting a floating input-only pin answer a power question, and §19.5's battery precondition is
 * skipped-and-logged (E_OTA_BATT_UNWIRED) rather than silently passed. */
#define CFG_CHRG_WIRED     @CFG_CHRG_WIRED@
```

- [ ] **Step 3: Replace `ota_batt_ok` with an honest one** (drv:M11). Add near `s_ui_pct_step`:

```c
static bool     s_batt_skip_logged;    /* E_OTA_BATT_UNWIRED is logged once per boot */
static volatile uint32_t s_last_data_ms;  /* §19.6: esp_timer ms of the last accepted OTA_DATA.
                                           * A single u32 word, so the console/cmd writer and the
                                           * supervisor reader need no lock (an int64 on 32-bit
                                           * Xtensa would not be a naturally-atomic load/store). */
```
and replace the function:

```c
/* §19.5 battery precondition.
 *
 * drv:M11 / #54: on this prototype there is no power hardware. CHRG is GPIO 39 -- input-only with
 * no internal pull on classic ESP32 -- so an unwired pin floats, a float reading low used to make
 * board_charger_present() answer "charging", and that answer bypassed this whole check: an OTA
 * (and its 1.25 MB slot erase) could proceed on a nearly empty pack. board_charger_present() now
 * returns false unless CFG_CHRG_WIRED == 1 (hal/board.h's documented contract), and the battery
 * ADC tap is unpopulated on the same board, so on such a build NEITHER input is trustworthy.
 *
 * Rather than let a float decide, the check is SKIPPED when the power hardware is absent and the
 * skip is recorded once per boot as E_OTA_BATT_UNWIRED. That keeps the bench OTA path working
 * (the only path by which this device is flashed) and makes the deviation auditable, which is
 * strictly better than the status quo where the same outcome was produced invisibly by a floating
 * pin. With CFG_CHRG_WIRED == 1 both inputs are real and §19.5 is enforced as written. */
static int ota_batt_ok(void)
{
#if !CFG_CHRG_WIRED
    if (!s_batt_skip_logged) { s_batt_skip_logged = true; (void)errlog_add(E_OTA_BATT_UNWIRED, 0); }
    return 0;
#else
    bool chg = false;
    if (board_charger_present(&chg) == 0 && chg) return 0;
    uint16_t mv = 0;
    if (board_battery_read_mv(&mv) == 0 && mv >= BATT_OTA_MIN_MV) return 0;
    return E_OTA_PRECOND;
#endif
}
```

- [ ] **Step 4: Rewrite `ota_begin`'s precondition half (R2).** Add two helpers above it and replace the head of `ota_begin`:

```c
/* §19.5's "Logger session closed (LOGGER_CLOSE_SESSION acked)" (app:H1: nothing ever closed the
 * session for an OTA, so ~500 KB of esp_ota_write interleaved with the logger's 4 KB batches and
 * .sum rewrites on the same flash, each erase stalling BOTH cores' icache mid-lap, §17.8).
 * Bounded by OTA_CLOSE_TIMEOUT_MS: a logger that cannot ack in time means the precondition is NOT
 * met, so the push is refused rather than racing it. */
static int ota_close_session(void)
{
    log_request_t req = { .type = LOGGER_CLOSE_SESSION, .reason = SES_END_RESTART, .gps_us = 0 };
    int rc = logger_request_sync(&req, OTA_CLOSE_TIMEOUT_MS);
    if (rc != 0) {
        ESP_LOGE(TAG, "session close for OTA rc %d", rc);
        (void)errlog_add(E_LOG_CLOSE_TIMEOUT, SES_END_RESTART);
    }
    return rc;
}

/* R1: the RUNNING slot's own state, which is the authority on whether an image really awaits
 * validation. A set ota_pend flag with a non-pending running slot is a STALE flag (an NVS GC, a
 * power-cut between ota_end's persist and set_boot, or a completed rollback), and refusing on it
 * alone was the app:C1 dead end that made the device un-OTA-able until a USB flash. */
static bool ota_slot_pending_verify(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    return run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

int ota_begin(const uint8_t *payload, size_t len)
{
    LT_ASSERT_RET(payload != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (len < OTA_BEGIN_LEN) return E_OTA_PRECOND;
    if (sys_flags_get() & (1u << SYS_OTA_PENDING)) return E_OTA_PRECOND;
    uint32_t size = rd_u32le(payload + OTA_OFF_SIZE);
    if (memcmp(payload + OTA_OFF_HWID, hwid, sizeof hwid) != 0) return E_OTA_HWID;   /* early reject */
    /* §19.5 (R2): the decision is pure and host-tested (app/sup_policy.h ota_precond_eval); this
     * function only gathers the inputs and performs the side effects it asks for. session_open
     * starts true -- assume a session is open until a close actually acks -- so the evaluator can
     * enforce the ordering rule that the close runs LAST, after every cheap refusal. */
    ota_pre_t pre = {
        .still_ms            = pipeline_still_for_ms(),
        .size                = size,
        .in_flight           = (s_state != OTA_IDLE),
        .session_open        = true,
        .batt_ok             = (ota_batt_ok() == 0),
        .flag_pending        = lt_ota_pending_get(),
        .slot_pending_verify = ota_slot_pending_verify(),
    };
    uint8_t act = ota_precond_eval(&pre);
    if (act == OTA_PRE_REFUSE) return E_OTA_PRECOND;
    if (act == OTA_PRE_CLOSE_SESSION) {
        if (ota_close_session() != 0) return E_OTA_PRECOND;
        pre.session_open = false;
        act = ota_precond_eval(&pre);          /* terminates: only OK / CLEAR_STALE can remain */
    }
    if (act == OTA_PRE_CLEAR_STALE) {
        lt_ota_pending_clear();
        (void)errlog_add(E_OTA_ROLLBACK, 1);   /* arg 1 == attributed at ota_begin, not at boot */
        ESP_LOGW(TAG, "stale ota_pending cleared (no pending-verify slot)");
    }
    /* Postcondition of the two evaluations above: nothing but a go-ahead reaches the flash. */
    LT_ASSERT_RET(act == OTA_PRE_OK || act == OTA_PRE_CLEAR_STALE, OTA_ASSERT_CODE, E_OTA_PRECOND);
    s_target = esp_ota_get_next_update_partition(NULL);
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (esp_ota_begin(s_target, size, &s_handle) != ESP_OK) { s_handle = 0; s_target = NULL; return E_OTA_WRITE; }
    mbedtls_sha256_init(&s_sha);
    (void)mbedtls_sha256_starts(&s_sha, 0);
    memcpy(s_sha_want, payload + OTA_OFF_SHA, OTA_SHA_LEN);
    s_size = size;
    s_recv = 0;
    s_hwid_ok = false;
    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);   /* §19.6: arm the receive timeout */
    s_state = OTA_RECV;
    sys_flags_set(SYS_OTA_PENDING);
    s_ui_pct_step = 0;
    ota_ui_post(OTA_PHASE_RECEIVING, 0);
    ESP_LOGI(TAG, "begin %u B -> %s", (unsigned)size, s_target->label);
    return 0;
}
```
`ota_begin` is now ~38 code lines with 3 meaningful assertions — within rule 4 (≤ 60) and rule 5 (≥ 2). Add `#include "app/lt_ipc.h"` (already present for `g_ui_evt_q`), `#include "app/pipeline.h"`, `#include "app/sup_policy.h"`, `#include "core/ses.h"` to `ota.c`.
Also delete the now-duplicated `if (size == 0 || size > OTA_MAX_IMG_BYTES)` and `if (lt_ota_pending_get())` lines and the old `if (ota_batt_ok() != 0)` call — all three moved into `ota_precond_eval`.

- [ ] **Step 5: Refresh the stamp + add `ota_poll` (M8).** In `ota_data`, after `s_recv += (uint32_t)n;`:

```c
    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);   /* §19.6: the peer is alive */
```
and add, next to `ota_reboot_due`:

```c
/* §19.6, called once per supervisor tick (app:M8: nothing ever timed out OTA_RECV, so a peer that
 * vanished mid-push left s_state == OTA_RECV and SYS_OTA_PENDING set until a reboot, and every
 * later ota_begin returned E_OTA_PRECOND). Covers BOTH transports: the framed CMD_OTA_* path had
 * no timeout at all, and `ota recv`'s own chunk-read timeout only covers the window its task is
 * blocked in -- a host that stops mid-stream after a successful chunk leaves nobody watching.
 *
 * Cross-task safety: this runs on the supervisor while the console/cmd task owns the state
 * machine. It can only act after OTA_RECV_TIMEOUT_S with no OTA_DATA, and a producing task
 * anywhere inside ota_data() refreshes the stamp -- so the only way both could be inside the
 * machine at once is a task blocked for > 30 s in a single esp_ota_write (a 4 KB program is
 * ~10-40 ms). §19.6's "abort on disconnect" is satisfied by THIS timeout and deliberately not by
 * the link DETECT edge: a detect flap during a healthy push would abort a good transfer, and the
 * console `ota recv` path does not use DETECT at all (see the §19.6 amendment, Task 10). */
void ota_poll(void)
{
    if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != OTA_RECV) return;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!ota_recv_stale(now_ms, s_last_data_ms, (uint32_t)OTA_RECV_TIMEOUT_S)) return;
    ESP_LOGE(TAG, "recv timeout after %d s at %u/%u B", OTA_RECV_TIMEOUT_S,
             (unsigned)s_recv, (unsigned)s_size);
    (void)errlog_add(E_CONN_XFER_ABORT, s_recv);
    (void)ota_fail(E_OTA_WRITE);          /* esp_ota_abort + SHA free + reset to IDLE (§19.6) */
}
```
Declare it in `ota.h`:
```c
void ota_poll(void);   /* §19.6: abort an OTA_RECV idle for OTA_RECV_TIMEOUT_S. Supervisor, 1 Hz. */
```
and call it from `sup.c`'s `ota_lifecycle`, as its first line:
```c
static void ota_lifecycle(uint32_t uptime_s)
{
    ota_poll();                 /* §19.6: a dead peer must not wedge the state machine (app:M8) */
    ota_reboot_check();
```

- [ ] **Step 6: §19.3 self-check.** `ota.c` relies on the linker placing `.rodata_custom_desc` after a 256 B `esp_app_desc_t` so `hwid` lands at image offset 0x120; a toolchain change would break every `ota_verify_hwid` with no signal until the first push (audit "Spec items not implemented", §19.3). Add next to the `hwid` definition:

```c
/* §19.3 is an image-LAYOUT contract, so assert both halves of it at build time and prove the
 * running half once at boot. There was no check of either kind: a changed esp_app_desc_t size
 * would silently move hwid off 0x120 and make every ota_verify_hwid compare the wrong bytes. */
_Static_assert(sizeof(esp_app_desc_t) == 256, "§19.3: hwid lands at image offset 0x120 only if esp_app_desc_t is 256 B");
_Static_assert(IMG_HWID_OFF == IMG_DESC_OFF + 256, "§19.3: hwid offset must follow the app descriptor");
```
and verify the running image at the start of `ota_verify_hwid`, replacing its first assertion:

```c
static int ota_verify_hwid(void)
{
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_WRITE);
    /* §19.3: read OUR OWN hwid back out of OUR OWN running slot at 0x120 before trusting the same
     * offset in the target. If the linker ever stops placing it there, this fails on the first
     * push with E_OTA_WRITE and an explicit log line, instead of silently comparing the target's
     * padding bytes against our hwid and rejecting every legitimate image as E_OTA_HWID. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    char selfhwid[OTA_HWID_LEN];
    if (run == NULL || esp_partition_read(run, IMG_HWID_OFF, selfhwid, sizeof selfhwid) != ESP_OK ||
        memcmp(selfhwid, hwid, sizeof selfhwid) != 0) {
        ESP_LOGE(TAG, "§19.3 layout check failed: hwid is not at image offset 0x%03x", IMG_HWID_OFF);
        return E_OTA_WRITE;
    }
    esp_app_desc_t desc;
    char thwid[OTA_HWID_LEN];
    ...                                 /* unchanged from here */
```

- [ ] **Step 7: `LT_STATE_OTA` (M7).** In `components/app/include/app/status.h`, above the `status_build` prototype:

```c
/* §19.4's "status.state = STATE_OTA while any of this is in flight". STATUS byte
 * LT_ST_OFF_STATE was hardcoded 0 (app:M7), so a peer polling STATUS during a push could not
 * tell the device was mid-OTA. Only these two states exist today; §16's full power state machine
 * lands with Plan 6 and will extend this enum. The values are WIRE-VISIBLE: append only. */
enum { LT_STATE_IDLE = 0, LT_STATE_OTA = 1 };
```
In `components/app/sys/status.c`, add `#include "app/ota.h"` and replace the hardcoded byte:

```c
    /* §19.4 (M7): ota_in_progress() is a single volatile read of ota.c's state word -- no I/O, no
     * lock -- so status_build() stays safe to call from any task (see this file's header). */
    out[LT_ST_OFF_STATE] = ota_in_progress() ? (uint8_t)LT_STATE_OTA : (uint8_t)LT_STATE_IDLE;
```

- [ ] **Step 8: Fix `ota.c`'s two false comments.**

| Site | Old | New |
|---|---|---|
| `ota.c:94-99` | `… both of which quiet logging to ESP_LOG_ERROR for the duration (export_serial.c) — an unsuppressed line mid-transfer would inject bytes …` | `… both of which quiet logging to ESP_LOG_NONE for the duration (export_serial.c, ruling R9 -- it was ESP_LOG_ERROR, which still let the assert sink, the supervisor's stall line and storage errors inject bytes into the OTA token/frame stream on the same UART, app:M12/drv:M6) …` |
| `ota.c:232-239` | `… so a flag lost to a power-cut in the tiny window below still resolves correctly; keeping the flag ahead of set_boot only tightens the fail-closed guarantee.` | `… so a flag lost to a power-cut in the tiny window below still resolves correctly. The OTHER direction of that window is NOT free, and is accepted: a cut BETWEEN lt_ota_pending_set() and esp_ota_set_boot_partition() leaves flag = 1 with the old, non-pending image running, which ota_boot_decide attributes as a bootloader rollback -- one spurious E_OTA_ROLLBACK entry and one LT_CTR_OTA_ROLLBACK bump for an update that was never activated. R1's stale-flag clear in ota_begin means this no longer blocks the next push, which is what actually mattered (app:C1, false-claim 13).` |

- [ ] **Step 9: Build, lint, commit** —

```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build          # rc 0, 0 warnings
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build        # rc 0, 0 warnings
CHRG=ON IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build  # the CFG_CHRG_WIRED==1 arm compiles too
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
ctest --test-dir test/build --output-on-failure
git add components/app/ota/ota.c components/app/include/app/ota.h components/app/sys/status.c \
        components/app/include/app/status.h components/app/supervisor/sup.c \
        components/app/include/app/lt_err.h CMakeLists.txt main/build_config.h.in
git commit   # feat(ota): §19.5 preconditions, stale-pend clear, §19.6 recv timeout, §19.3 self-check, STATE_OTA (F2a T4, R1/R2)
```
The `CHRG=ON` build proves the `#else` arm of `ota_batt_ok` still compiles — it is the arm Plan 6 will ship, and nothing else in CI builds it.

- [ ] **CHECKPOINT** — report. Stop.

---

# Session 2 — NVS/RTC, board, storage/stubs, console (Tasks 5–8)

Tasks 5, 6, 7 and 8 touch **disjoint files** and may run in parallel in separate worktrees
(`feedback-parallel-tasks`): T5 `app/sys/{lt_nvs,lt_rtc}.c` + three headers · T6
`drivers/board_devkit_v1/*` + `hal/board.h` · T7 `drivers/{storage_internal,gps_neo6m,imu_mpu6050,display_epaper_ssd1680}` ·
T8 `drivers/export_serial/export_serial.c` + `include/app/cmd.h`. Only T5 and T4 share
`lt_err.h`; T4 lands first, so T5 appends below its codes. Each still ends at its own
CHECKPOINT and its own commit.

### Task 5 — PARALLEL — errlog dedup on code (M1), RTC seqlock (L4), four false comments

**Files:**
- Modify: `components/app/sys/lt_nvs.c` (`errlog_add`, `lt_errlog_clear`), `components/app/include/app/lt_nvs.h` (header comment), `components/app/sys/lt_rtc.c` (`lt_rtc_save`, `lt_rtc_validate`), `components/app/include/app/lt_rtc.h` (comment), `components/app/include/app/lt_assert.h` (comment), `components/app/include/app/lt_err.h` (comment), `components/app/include/app/lt_ipc.h` (lines 119-122 only), `components/app/include/app/status.h` (comment)

**Interfaces:**
- Consumes: `blob_lock`/`blob_unlock`, `ERRLOG_MIN_PERSIST_US`, `s_ring`, `ses_crc16` — all already in `lt_nvs.c`.
- Produces: no new symbols. The rate-limit contract that `logger.c` and every assert site depend on changes, and is documented in `lt_nvs.h`.

- [ ] **Step 1: Rewrite `errlog_add`'s dedup (app:M1).** The bug: the rate limit dedups on `(code, arg)` against the *immediately previous* report, and `logger.c:187` reports `errlog_add(E_STO_WRITE, (uint32_t)s_batch_len)` whose arg **varies per write** — so on a dead or full filesystem (issue #104's observed `free_kb == 0` with an open session) consecutive failures never dedup and every logger loop writes a fresh 388 B `nvs_set_blob` + `nvs_commit` into the 24 KB `nvs` partition, at ≥ 1 Hz, forever. `core_assert_report` has the same shape (it passes `__LINE__` as `arg`, so two asserts failing alternately on a hot path never dedup either).

Replace the body from `if (code == 0) return -1;` to the closing `return 0;` with:

```c
    if (code == 0) return -1;
    blob_lock();                                /* F1: RMW of s_ring + its NVS write is not atomic */
    if (s_ring.head >= ERR_RING_LEN) s_ring.head = 0;
    /* M1: the dedup and the flash-write rate limit both key on `code` ALONE, not on (code, arg).
     * They used to key on both, which made a varying arg -- E_STO_WRITE's batch length, or an
     * assert's __LINE__ -- defeat the limit entirely and storm NVS at the loop rate.
     *
     * Consecutive reports of the same code COLLAPSE onto the newest slot (latest arg and uptime
     * win) rather than appending: appending would let a 1 Hz storm fill all 32 slots in 32 s and
     * evict every other diagnostic, which is exactly the evidence an operator needs when storage
     * has died. A DIFFERENT code always appends and always persists at once, so genuine
     * crash/stall/mount records stay immediate and reliable. The only thing lost is the flash
     * copy of a repeat's newest arg if power is cut inside the ERRLOG_MIN_PERSIST_US window --
     * strictly less than what the old code lost (it did not even keep the repeat in RAM). */
    bool same_code = s_last_valid && code == s_last_code;
    if (same_code) {
        uint8_t newest = (uint8_t)((s_ring.head + ERR_RING_LEN - 1) % ERR_RING_LEN);
        s_ring.entry[newest].arg      = arg;
        s_ring.entry[newest].uptime_s = uptime_s_now();
    } else {
        err_entry_t *e = &s_ring.entry[s_ring.head];
        e->code = code; e->uptime_s = uptime_s_now(); e->boot = (uint16_t)s_boot_cnt; e->arg = arg;
        s_ring.head = (uint8_t)((s_ring.head + 1) % ERR_RING_LEN);
    }
    bool due = !same_code || (esp_timer_get_time() - s_ring_persist_us) >= ERRLOG_MIN_PERSIST_US;
    s_last_code = code; s_last_arg = arg; s_last_valid = true;
    if (due) errlog_persist();
    blob_unlock();
    if (!same_code) ESP_LOGW(TAG, "errlog 0x%04x arg=%u", code, (unsigned)arg);   /* no log storm either */
    return 0;
```
`s_last_arg` stays (it is still the value a future consumer would want and costs nothing); it simply no longer participates in the decision. Keep the function assert-FREE — it IS the assertion sink (`core_assert_report` → here), so an `LT_ASSERT_*` would recurse to a boot-time stack overflow.

- [ ] **Step 2: Reset the dedup state on clear.** In `lt_errlog_clear`, inside the existing `blob_lock()` region, add:

```c
    s_last_valid = false;   /* M1: a cleared ring has no "newest slot" to collapse onto */
```
If `lt_errlog_clear` already clears it, leave it and note so at the checkpoint.

- [ ] **Step 3: Make `lt_rtc_validate` tear-proof (app:L4).** `export_serial.c:745` calls `lt_rtc_validate(&st)` on the console task while `pipeline.c:464` rewrites the same `RTC_NOINIT` struct unlocked; a torn read fails the CRC and `dbg rtc` reports `RTC_INVALID` for a perfectly good snapshot. Add to `components/app/sys/lt_rtc.c`:

```c
/* L4: a seqlock over the RTC_NOINIT snapshot. The writer is always the pipeline task
 * (lt_rtc_save, on every S/F and sector event); the readers are the pipeline's own resume path
 * at boot and `dbg rtc` on the console task. An odd generation means a write is in progress.
 * This counter is NOT in RTC memory on purpose: it only has to order readers against a writer
 * WITHIN one boot. After a reset it starts even, which is correct -- no write is in progress --
 * and the CRC in the snapshot itself remains the cross-boot integrity check. */
static volatile uint32_t s_rtc_gen;
#define RTC_READ_RETRIES 8      /* rule 2: bounded; a writer takes microseconds */
```
Wrap `lt_rtc_save`'s store (its first and last statements):

```c
void lt_rtc_save(const lap_rtc_t *lr, const char *session_id, int64_t saved_gps_us,
                 uint8_t mode, uint8_t power_state, uint32_t partial_count)
{
    __atomic_store_n(&s_rtc_gen, s_rtc_gen + 1u, __ATOMIC_RELEASE);   /* odd: write in progress */
    ...                                     /* unchanged body, including the CRC computation */
    __atomic_store_n(&s_rtc_gen, s_rtc_gen + 1u, __ATOMIC_RELEASE);   /* even: snapshot coherent */
}
```
and make `lt_rtc_validate` retry:

```c
rtc_validity_t lt_rtc_validate(rtc_state_t *out)
{
    /* L4: read the snapshot under the seqlock -- an odd generation, or one that moved across the
     * copy, means lt_rtc_save was mid-write and the bytes we read are a mix of two snapshots
     * whose CRC would (correctly) fail. Retry a bounded number of times; falling through after
     * RTC_READ_RETRIES returns the CRC verdict on the last copy, which is the pre-L4 behaviour
     * and still safe (a torn read fails the CRC, it never returns VALID garbage). */
    for (int i = 0; i < RTC_READ_RETRIES; i++) {
        uint32_t g0 = __atomic_load_n(&s_rtc_gen, __ATOMIC_ACQUIRE);
        if ((g0 & 1u) != 0u) continue;                       /* writer in progress */
        rtc_validity_t v = rtc_validate_copy(out);           /* the pre-L4 body, extracted verbatim */
        if (__atomic_load_n(&s_rtc_gen, __ATOMIC_ACQUIRE) == g0) return v;
    }
    return rtc_validate_copy(out);
}
```
Extract the existing magic/version/CRC body of `lt_rtc_validate` verbatim into `static rtc_validity_t rtc_validate_copy(rtc_state_t *out)` — no logic change, which also keeps both functions under 20 code lines (rule 5 exempt, R20: no padding).

- [ ] **Step 4: Fix the five false comments.** Exact old → new:

| Site | Old text | New text |
|---|---|---|
| `lt_rtc.h:48-49` | `Called by the pipeline on every S/F and sector event and before a supervised restart.` | `Called by the pipeline on every S/F and sector event. NOT called before a supervised restart: §17.2's "restarts after saving RTC state" is struck by the 2026-10-13 audit-fix-wave ruling R6 -- the pipeline owns this snapshot and a WEDGED pipeline (the only task the supervisor restarts) cannot be asked to save, so the resume point is the last gate crossing by design. power_state has no owner until Plan 6 (pass 0); partial_count is owned by the ui and is not yet handed here (§15.3, filed).` |
| `lt_nvs.h:4-7` | `Write policy per §15.2: a DISTINCT error-ring entry persists immediately; an immediate repeat of the same (code,arg) is deduped and its flash write rate-limited (H1, ...); counters are batched (>=60 s) except on crash paths; cfg only on change.` | `Write policy per §15.2: a DISTINCT error code persists immediately; CONSECUTIVE reports of the SAME code collapse onto the newest ring slot (latest arg and uptime win) with their flash write rate-limited to ERRLOG_MIN_PERSIST_US -- keyed on the code ALONE, because keying on (code,arg) let E_STO_WRITE's varying batch length storm NVS at 1 Hz on a dead filesystem (audit fix wave app:M1). Counters are batched (>= 60 s) except on crash paths. cfg is written on EVERY lt_cfg_save() call: §15.2's "only on change" compare and the cross-task read-modify-write lock land with ruling R10 (plan F2b).` |
| `lt_assert.h:13-15` | `Session 4.5.1 is tooling-only: this header is new and nothing calls it yet, so there is no behavior change. Rule 5 retrofit ... lands in later Plan 4.5 sessions.` | `These macros are now the app/driver layer's primary assertion sink -- roughly 300 call sites across components/app, components/drivers and main. The strong core_assert_report they reach lives in components/app/sys/lt_nvs.c (next to errlog_add, in a TU the firmware always links) and records the code into the §17.7 error ring.` |
| `lt_err.h:1-3` | `3.2 needs the E_SYS_* subset used by the boot sequence, crash-loop detection and supervisor; the full code table lands with the serial console (3.5).` | `The table below is still a SUBSET of §17.7: E_GPS_*, E_IMU_*, E_STO_SD_DEGRADED, E_PWR_*, E_LAP_PLAUSIBILITY and E_FUSION_* are absent because their producers are (GPS/IMU/fusion ladders -> Plan 8; power/SD -> Plan 6). Every code defined here has at least one writer. Codes are stable u16 values logged into the error ring (§15.2) and are APPEND-ONLY -- never renumber one.` |
| `lt_ipc.h:119-122` | `The producers (ui/conn/power) land in later sessions; 3.4 creates the queue and the pipeline drains it (CMD_SET_MODE / CMD_SET_LAYOUT / CMD_RESET_ENGINE at least).` | `The live producers are the ui task (menu actions) and app/cmd (remote CONFIG/engine ops); there is no power task (Plan 6) and no conn task (sub-project C). lt_ipc_init() creates the queue at boot step 11 and the pipeline drains it (CMD_SET_MODE / CMD_SET_LAYOUT / CMD_RESET_ENGINE / CMD_CONFIG_RELOAD / CMD_SIM_SCENARIO).` |
| `status.h:14-16` | `Session open/close is commanded by the power task` (also in `logger.h:3-7`, which is F2b's — hand it over) | `Session open/close is commanded over log_req_q by the pipeline (sim builds), by the supervisor (planned restarts and the §19.5 OTA close) and by dbg logtest; there is no power task (Plan 6).` |

- [ ] **Step 5: Gate + commit** —

```bash
source tools/idf-env.sh
ctest --test-dir test/build --output-on-failure                         # test_cfg etc. unchanged
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build && IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
git add components/app/sys/lt_nvs.c components/app/sys/lt_rtc.c components/app/include/app/
git commit   # fix(nvs,rtc): errlog rate-limit on code alone, RTC seqlock, five false comments (F2a T5, M1/L4)
```

- [ ] **CHECKPOINT** — report. Stop.

---

### Task 6 — PARALLEL — `board.c`: ISR IRAM (C1), insertion sort (R19), CHRG contract (M11), 30 ms per-pin debounce (L6/L17), bring-up ladder (L18), pin-map rows (L22)

**Files:**
- Create: `components/drivers/board_devkit_v1/include/batt_pure.h`, `components/drivers/board_devkit_v1/host/batt_trim.c`, `test/test_batt_trim.c`
- Modify: `components/drivers/board_devkit_v1/board.c`, `components/drivers/board_devkit_v1/CMakeLists.txt`, `components/lt_hal/include/hal/board.h`, `test/CMakeLists.txt` (one target block)

**Interfaces:**
- Consumes: `CFG_CHRG_WIRED` (`build_config.h`, Task 4).
- Produces:
```c
/* batt_pure.h -- IDF-free, host-testable */
#define BATT_SAMPLES 64
#define BATT_TRIM    8
int  batt_trimmed_mean(int *s, int n, int trim);   /* sorts s in place; -1 on bad args */
/* hal/board.h */
#define BTN_DEBOUNCE_MS 30
```

- [ ] **Step 1: Write the failing test** — `test/test_batt_trim.c`:

```c
#include "unity.h"
#include "batt_pure.h"
#include <stdlib.h>
#include <string.h>
void setUp(void) {} void tearDown(void) {}

static void test_sorted_input(void)
{
    int s[8]; for (int i = 0; i < 8; i++) s[i] = i * 10;       /* 0..70 */
    TEST_ASSERT_EQUAL_INT(35, batt_trimmed_mean(s, 8, 2));     /* mean of 20,30,40,50 */
}
static void test_reverse_input_is_sorted_in_place(void)
{
    int s[8]; for (int i = 0; i < 8; i++) s[i] = 70 - i * 10;
    TEST_ASSERT_EQUAL_INT(35, batt_trimmed_mean(s, 8, 2));
    for (int i = 1; i < 8; i++) TEST_ASSERT_TRUE(s[i - 1] <= s[i]);
}
static void test_outliers_are_discarded(void)
{
    /* the §16.4 case the trim exists for: ADC spikes at both ends must not move the mean */
    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) s[i] = 2000;
    for (int i = 0; i < BATT_TRIM; i++) { s[i] = 0; s[BATT_SAMPLES - 1 - i] = 4095; }
    TEST_ASSERT_EQUAL_INT(2000, batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM));
}
static void test_matches_a_reference_sort_on_random_input(void)
{
    /* R19 replaces qsort(cmp_int) with a bounded insertion sort; prove the two agree. */
    int s[BATT_SAMPLES], ref[BATT_SAMPLES];
    unsigned seed = 12345u;
    for (int trial = 0; trial < 200; trial++) {
        for (int i = 0; i < BATT_SAMPLES; i++) {
            seed = seed * 1103515245u + 12345u;
            s[i] = (int)((seed >> 16) % 4096u);
        }
        memcpy(ref, s, sizeof ref);
        int got = batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM);
        /* reference: selection sort, independent implementation */
        for (int i = 0; i < BATT_SAMPLES - 1; i++) {
            int m = i;
            for (int j = i + 1; j < BATT_SAMPLES; j++) if (ref[j] < ref[m]) m = j;
            int t = ref[i]; ref[i] = ref[m]; ref[m] = t;
        }
        long long sum = 0;
        for (int i = BATT_TRIM; i < BATT_SAMPLES - BATT_TRIM; i++) sum += ref[i];
        TEST_ASSERT_EQUAL_INT((int)(sum / (BATT_SAMPLES - 2 * BATT_TRIM)), got);
    }
}
static void test_bad_args(void)
{
    int s[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(NULL, 4, 1));
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 4, 2));     /* n - 2*trim == 0 */
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 4, 3));     /* negative window */
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 0, 0));
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, BATT_SAMPLES + 1, 0));   /* above the rule-2 cap */
    TEST_ASSERT_EQUAL_INT(2, batt_trimmed_mean(s, 4, 0));      /* no trim: mean(1,2,3,4) == 2 */
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sorted_input);
    RUN_TEST(test_reverse_input_is_sorted_in_place);
    RUN_TEST(test_outliers_are_discarded);
    RUN_TEST(test_matches_a_reference_sort_on_random_input);
    RUN_TEST(test_bad_args);
    return UNITY_END();
}
```

- [ ] **Step 2: Wire the host target** — append to `test/CMakeLists.txt` and add `test_batt_trim` to the generic loop's hold-back chain:

```cmake
# test_batt_trim (audit fix wave F2a Task 6, drv:M12/R19): the board HAL's pure §16.4 trimmed-mean
# over the battery ADC samples, extracted so the bounded insertion sort that replaces
# qsort(cmp_int) is proven by a test rather than by a bench voltage reading (there is none -- the
# STATUS record's batt_mv is still 0 until Plan 6). Same pattern as test_epd_pure: link the
# driver's one host/*.c source plus its include dir.
set(BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/drivers/board_devkit_v1)
add_core_test(test_batt_trim ${BOARD_DIR}/host/batt_trim.c)
target_include_directories(test_batt_trim PRIVATE ${BOARD_DIR}/include)
```

- [ ] **Step 3: Run to fail** — `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8` → `batt_pure.h: No such file or directory`.

- [ ] **Step 4: Implement** `components/drivers/board_devkit_v1/include/batt_pure.h`:

```c
/* batt_pure.h -- the pure, host-testable part of the board HAL's §16.4 battery reading: the
 * trimmed mean over BATT_SAMPLES ADC samples (audit fix wave F2a, drv:M12 / ruling R19).
 *
 * Extracted from board.c for two reasons. (1) board.c:234 called qsort(s, 64, sizeof(int),
 * cmp_int) -- an unregistered rule-9 function pointer the linter's FNPTR_RE structurally cannot
 * see, and newlib's qsort is itself a recursive implementation, which rule 1 forbids in shipped
 * code. A trimmed mean over 64 entries needs no general-purpose sort. (2) There is no way to
 * observe battery millivolts on the bench (the §18.2 STATUS record's batt_mv is hardcoded 0 until
 * Plan 6), so a host test is the only honest verification this arithmetic can have.
 *
 * IDF-free (stddef.h only) so it links into test/test_batt_trim.c as well as the firmware. */
#ifndef BATT_PURE_H
#define BATT_PURE_H
#include <stddef.h>

#define BATT_SAMPLES 64   /* §16.4: 64 samples ... */
#define BATT_TRIM    8    /* ... discard the 8 highest and the 8 lowest, mean the middle 48 */

/* Sorts s[0..n-1] ascending IN PLACE with a bounded insertion sort (rule 1: no recursion; rule 2:
 * the two loops are bounded by n <= BATT_SAMPLES) and returns the integer mean of the middle
 * n - 2*trim entries. -1 on a bad argument (NULL, n outside 1..BATT_SAMPLES, or an empty/negative
 * middle window), which the caller reports as an ADC read failure. */
int batt_trimmed_mean(int *s, int n, int trim);

#endif /* BATT_PURE_H */
```
and `components/drivers/board_devkit_v1/host/batt_trim.c`:

```c
#include "batt_pure.h"

int batt_trimmed_mean(int *s, int n, int trim)
{
    if (s == NULL || n <= 0 || n > BATT_SAMPLES) return -1;
    if (trim < 0 || n - 2 * trim <= 0) return -1;
    /* insertion sort, ascending. Outer loop <= BATT_SAMPLES - 1 iterations, inner loop <= i, so
     * the whole sort is bounded by BATT_SAMPLES^2 / 2 == 2048 comparisons -- about 20 us on the
     * ESP32 and run at most once per battery read. */
    for (int i = 1; i < n; i++) {
        int v = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    long long sum = 0;
    for (int i = trim; i < n - trim; i++) sum += s[i];
    return (int)(sum / (n - 2 * trim));
}
```

- [ ] **Step 5: Run to pass** — `cmake --build test/build -j8 && ctest --test-dir test/build -R test_batt_trim --output-on-failure` → 5/5.

- [ ] **Step 6: `board.c` — the ISR (drv:C1).** The reviewer proved in the committed ELF that `IRAM_ATTR btn_isr` (IRAM, `0x40083b30`) calls flash-resident `read_button_mask` (`0x400d999c`, XIP window, not inlined) — so any edge on GPIO 32/33/25 while the icache is off (the logger's 4 KB batch every ≤ 1 s with a session open, `esp_ota_write`, any NVS commit) hits unreachable code. The orchestrator's correction: `gpio_install_isr_service(0)` passes no `ESP_INTR_FLAG_IRAM`, so IDF masks the interrupt across cache-off windows and the effect today is a **deferred edge**, not a panic — plus a §17.9 deviation and an `IRAM_ATTR` that lies. Make the §17.9 claim true on both halves:

```c
/* drv:C1 + §17.9 ("ISRs ... are IRAM_ATTR"): btn_isr is IRAM_ATTR and calls this, so this must be
 * too -- the committed moto_sim ELF had it at 0x400d999c, i.e. in the flash XIP window, not
 * inlined. s_btn_override and the three PIN_* constants it reads are already DRAM/immediate, and
 * gpio_get_level is IRAM via CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y (§17.8), so this is the only piece
 * that had to move. */
static uint8_t IRAM_ATTR read_button_mask(void)
```
and register the service with the matching flag:

```c
    /* drv:C1: ESP_INTR_FLAG_IRAM is the OTHER half of the §17.9 claim. Without it IDF masks the
     * GPIO interrupt for the duration of every cache-off flash write and replays the edge after,
     * so a press during the logger's 4 KB batch arrives late (up to ~40 ms) instead of on time;
     * with it -- and with every function the handler reaches now in IRAM (see read_button_mask)
     * -- the handler runs during the write, as §17.8 requires of a button path shared with a
     * 1 Hz flash writer. */
    esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;   /* INVALID_STATE = already installed */
```

- [ ] **Step 7: `board.c` — per-pin 30 ms debounce (app:L6, drv:L17).** Appendix A says `BTN_DEBOUNCE_MS` = 30 and §20.8 says "level sampled 30 ms after the edge"; the ISR used a 25 ms guard, and one shared `s_btn_last_us` for all three buttons meant a MODE edge within 25 ms of an UP edge was dropped entirely — which also weakens the UP+DOWN combo path (§20.8's "UP+DOWN held 2 s"). Add the constant to `components/lt_hal/include/hal/board.h` (the contract both the board driver and the ui read), next to `board_buttons_enable_isr`:

```c
/* §20.8 / Appendix A: contact-bounce guard for the three button GPIOs, in milliseconds. Lives
 * here rather than inside the board driver so the ISR's guard and the ui's "level sampled 30 ms
 * after the edge" are one number (audit fix wave app:L6 -- the ISR used 25 ms and the ui a
 * different mechanism at a different time constant). */
#define BTN_DEBOUNCE_MS 30
```
and in `board.c` replace the constant and the single stamp:

```c
#define BTN_DEBOUNCE_US (BTN_DEBOUNCE_MS * 1000)   /* §20.8 / Appendix A, via hal/board.h */
...
/* drv:L17: one debounce stamp PER PIN. A single shared stamp dropped any edge within the guard of
 * an edge on a DIFFERENT button -- so MODE pressed just after UP was lost, and the UP+DOWN combo
 * (§20.8) was unreliable by construction. Index order matches s_btn_pins/the mask bits below. */
static volatile int64_t s_btn_last_us[3];
static const gpio_num_t s_btn_pins[3] = { PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
```
and rewrite `btn_isr` to debounce the pin that fired (the handler argument carries it):

```c
static void IRAM_ATTR btn_isr(void *arg)
{
    /* arg is the pin index this handler was registered with (gpio_isr_handler_add's void* -- not
     * a function pointer, so rule 9 is unaffected). Bounded by 3 by construction. */
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    if (idx >= 3u) return;
    int64_t now = esp_timer_get_time();
    if (now - s_btn_last_us[idx] < BTN_DEBOUNCE_US) return;   /* per-pin debounce (§20.8, 30 ms) */
    s_btn_last_us[idx] = now;
    if (s_btn_q != NULL) {
        btn_raw_t ev = { .mask = read_button_mask(), .mono_us = now };
        BaseType_t hpw = pdFALSE;
        (void)xQueueSendFromISR(s_btn_q, &ev, &hpw);   /* drop-on-full: a stale edge is harmless */
        portYIELD_FROM_ISR(hpw);
    }
}
```
and in `board_buttons_enable_isr` register each pin with its index, replacing the local `pins[]`:

```c
    for (uint32_t i = 0; i < 3u; i++) {
        ESP_ERROR_CHECK(gpio_set_intr_type(s_btn_pins[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_btn_pins[i], btn_isr, (void *)(uintptr_t)i));
    }
```

- [ ] **Step 8: `board.c` — the trimmed mean (R19).** In `board_battery_read_mv`, delete `cmp_int` entirely and the `#include <stdlib.h>` that only `qsort` needed, add `#include "batt_pure.h"`, and replace the sort + mean:

```c
    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_BATT_CHANNEL, &raw) != ESP_OK) return -EIO;
        s[i] = raw;
    }
    /* §16.4 via batt_pure.h: bounded insertion sort + mean of the middle 48 (R19 -- this was
     * qsort(cmp_int), an unregistered rule-9 function pointer into a recursive newlib sort). */
    int mean_raw = batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM);
    if (mean_raw < 0) return -EIO;
```
and delete the now-redundant `CORE_ASSERT_RET(BATT_SAMPLES > 2 * BATT_TRIM, ...)` — `batt_trimmed_mean` validates it and the `mean_raw < 0` check handles it (R20: no assertion whose job another line already does). Keep the remaining `CORE_ASSERT_RET(mv != NULL, ...)` and add one real postcondition before the store:
```c
    CORE_ASSERT_RET(batt >= 0 && batt <= 65535, BOARD_ASSERT_CODE, -EIO);   /* fits the u16 out */
```
(the two clamps above it make this a genuine postcondition of the clamping, not a tautology).

- [ ] **Step 9: `board.c` + `hal/board.h` — the CHRG contract (drv:M11, #54).** `hal/board.h:28` promises "CHRG pin, or false if not wired", which was unimplementable: GPIO 34-39 have no internal pulls, so an unwired pin floats and a float reading low answered *true* — which `ota.c:108` then used to bypass §19.5's whole battery precondition. Make the contract honest:

`hal/board.h`:
```c
/* Charger-present sense (§3.3 CHRG, GPIO 39, open-drain active-low with an EXTERNAL 100k
 * pull-up). Returns false UNCONDITIONALLY on a build with CFG_CHRG_WIRED == 0 -- the prototype,
 * which has neither the pull-up nor a battery. GPIO 34-39 have no internal pull resistor on
 * classic ESP32, so an unwired pin floats and a float that happens to read low is
 * indistinguishable from a charger; answering "false if not wired" is the only implementable
 * reading of this contract, and it is the fail-safe direction for §19.5. Plan 6 (#54) adds the
 * pull-up and builds with CHRG=ON. */
int board_charger_present(bool *out);
```
`board.c`:
```c
int board_charger_present(bool *out)
{
    if (!out) return -EINVAL;
#if CFG_CHRG_WIRED
    /* CHRG is open-drain active-low (§3.3): present/charging => pin low. */
    *out = (gpio_get_level(PIN_CHRG) == 0);
#else
    /* drv:M11 / #54: no external pull-up and no battery on this board -- GPIO 39 floats, and a
     * floating input-only pin must never be allowed to answer a power question. hal/board.h's
     * contract ("false if not wired") is honoured literally. */
    *out = false;
#endif
    return 0;
}
```
and fix `board_prepare_deep_sleep`'s EXT0 line and its false comment (`board.c:305`):
```c
#if CFG_CHRG_WIRED
    /* §3.3: the charger is the single EXT0 wake pin (level 0). Only armed when CHRG is really
     * wired: on the prototype GPIO 39 floats, and a float reading low is an IMMEDIATE EXT0 wake
     * -- it would defeat PARK/SHUTDOWN entirely, the opposite of the "harmless if CHRG unwired"
     * this line used to claim (drv:M11, false-claim 2). */
    if (esp_sleep_enable_ext0_wakeup(PIN_CHRG, 0) != ESP_OK) ESP_LOGW(TAG, "ext0 (CHRG) wake not armed");
#endif
```

- [ ] **Step 10: `board.c` — bring-up ladder + checked returns (drv:L18, L14-board-half).** `board_init` used `ESP_ERROR_CHECK` on every I2C/SPI/ADC/RTC call, so any bring-up failure aborts into a panic and feeds the §17.5 crash loop instead of §17.6's self-test ladder; and the deep-sleep wake config ignored five return values. Replace each `ESP_ERROR_CHECK(X);` in `board_init` and `board_init_battery_adc` with the logged form, and make the function report:

```c
/* drv:L18: a failed peripheral bring-up is a §17.6 self-test result, not a reason to panic --
 * ESP_ERROR_CHECK would abort() here and feed §17.5's crash-loop detector, turning a dud I2C
 * pull-up into a safe-mode device. Each failure is logged and accumulated; board_init returns
 * -EIO so app_main can record it (and so the §17.6 rows that depend on a bus -- IMU, display --
 * fail honestly rather than mysteriously). */
#define BOARD_TRY(call, what) do { \
        esp_err_t _e = (call); \
        if (_e != ESP_OK) { ESP_LOGE(TAG, "%s: %s", (what), esp_err_to_name(_e)); rc = -EIO; } \
    } while (0)
```
with `int rc = 0;` at the top of `board_init`, `BOARD_TRY(rtc_gpio_init(PIN_GPS_PWR), "rtc_gpio_init")` and so on for all nine calls, `board_init_battery_adc` changed to `static int board_init_battery_adc(void)` returning the same way, and the tail:
```c
    s_inited = (rc == 0);
    ESP_LOGI(TAG, "board_devkit_v1 init rc %d: I2C0(21/22) VSPI(18/23/19) ADC1_CH6(34) buttons(32/33/25)", rc);
    return rc;
```
Keep the two existing `CORE_ASSERT_RET` postconditions (`s_i2c_bus != NULL`, `s_adc != NULL`) — both are real: ESP_OK must imply a usable handle. In `board_prepare_deep_sleep`, wrap `board_gps_power`, `rtc_gpio_pulldown_en`, `rtc_gpio_pullup_dis` and `esp_sleep_enable_ext1_wakeup` the same way (`BOARD_TRY` where they return `esp_err_t`, an `if (... != 0) ESP_LOGW` for `board_gps_power`) and return the accumulated rc.
If `BOARD_TRY` pushes `board_init` past rule 4's 60 code lines, split the I2C+SPI bus bring-up into `static int board_init_buses(void)` the same way `board_init_battery_adc` was split.

- [ ] **Step 11: `board.c` — the §3.3 pin-map rows (drv:L22).** GPIO 4 (the dev-kit DETECT line) is configured by `components/app/link/link.c`, behind `hal/board.h`'s back, and appears in neither `board.c`'s pin-map block nor §3.3's table. Add to the `---- §3.3 pin map ----` block:

```c
/* GPIO 4 (peer-connector DETECT, active-low with an internal pull-up) is NOT owned here: it is
 * configured and polled by components/app/link/link.c, which receives it as LINK_DETECT_GPIO from
 * components/app/CMakeLists.txt. Recorded in this block so the board's pin inventory is complete
 * (drv:L22). The §3.3 table gains the same row -- see this plan's spec amendments. Moving the
 * line behind hal/board.h would mean a new HAL entry point for a single app-owned GPIO; it is
 * documented instead, deliberately. */
```

- [ ] **Step 12: Register the include dir** — `components/drivers/board_devkit_v1/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "board.c" "host/batt_trim.c"
    INCLUDE_DIRS "include"
    REQUIRES lt_hal driver esp_adc esp_timer esp_hw_support core)
```

- [ ] **Step 13: Gate + commit** —

```bash
source tools/idf-env.sh
ctest --test-dir test/build --output-on-failure                     # + test_batt_trim 5/5
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build
CHRG=ON IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build               # the wired arm compiles
# prove drv:C1 in the ELF, the same way the reviewer disproved it:
xtensa-esp32-elf-nm build/moto_sim/laptimer.elf | grep -E 'btn_isr|read_button_mask'
#   -> both symbols must be in the IRAM range (0x4008xxxx), neither in 0x400Dxxxx
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
git add components/drivers/board_devkit_v1 components/lt_hal/include/hal/board.h \
        test/test_batt_trim.c test/CMakeLists.txt
git commit   # fix(board): ISR fully in IRAM, insertion-sort trimmed mean, honest CHRG, 30 ms per-pin debounce, bring-up ladder (F2a T6, C1/R19/M11/L6/L17/L18/L22)
```

- [ ] **CHECKPOINT** — report, quoting the `nm` output for both symbols. Stop.

---

### Task 7 — PARALLEL — storage bounds (M10/L19/L20), `disp_sleep` (L21), honest stubs (R28/H3)

**Files:**
- Modify: `components/drivers/storage_internal/storage_internal.c`, `components/drivers/display_epaper_ssd1680/display_epaper.c`, `components/drivers/gps_neo6m/gps_neo6m.c`, `components/drivers/imu_mpu6050/imu_mpu6050.c`

**Interfaces:**
- Consumes: `gps_profile_t.live` (Task 2) — the stub's designated initialiser leaves it 0, which is the value R1 needs; no edit to `gps_sim.c` is required for the same reason.
- Produces: `gps_init`/`imu_init`/`imu_self_test` now return `-ENODEV` on the stub builds. `pipeline_init_drivers` already tolerates failure (it reports `BOOT_FAIL`), so no caller changes.

- [ ] **Step 1: Bound `sto_write`'s drain loop (drv:M10).** A `write()` returning 0 makes no progress and the loop never exits — on the logger task that stops `hb[HB_LOGGER]`, which §17.2 turns into a stall report and (now, after Task 3) a task-WDT panic. Every other loop in this driver carries an explicit rule-2 cap; this one did not:

```c
int sto_write(sto_file_t f, const void *buf, size_t n)
{
    CORE_ASSERT_RET(buf != NULL || n == 0u, STO_ASSERT_CODE, -1);   /* a non-empty write needs a buffer */
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    /* rule 2 (drv:M10): a short write is legal and retried, but a write() that returns 0 makes no
     * progress -- without this cap the logger task spins here forever. One iteration per byte is
     * the worst case a legal short-write sequence can need, so the cap can never reject a
     * transfer that would have completed. */
    size_t guard = n + 1u;
    while (off < n && guard-- > 0u) {
        ssize_t w = write(f, p + off, n - off);
        if (w < 0) { ESP_LOGE(TAG, "write: %s", strerror(errno)); return -1; }
        if (w == 0) { ESP_LOGE(TAG, "write: no progress at %u/%u", (unsigned)off, (unsigned)n); return -1; }
        off += (size_t)w;
    }
    CORE_ASSERT_RET(off == n, STO_ASSERT_CODE, -1);   /* postcondition: a 0 return means all n landed */
    return 0;
}
```

- [ ] **Step 2: Stop declaring a mounted filesystem dead (drv:L19).** `sto_mount` returned −1 — which makes the caller set `SYS_STORAGE_DEAD` — on the `used <= total` assertion, even though the filesystem had registered successfully. Replace the assertion with a reported-but-non-fatal path:

```c
    /* drv:L19: a successful mount whose info query is implausible is a REPORTING fault, not a
     * dead filesystem -- returning -1 here made app_main set SYS_STORAGE_DEAD and disable
     * logging on a perfectly working fs. Report it and carry on; sto_info() independently returns
     * -1 to any caller that asks for the numbers, so nobody consumes the underflowed figure. */
    bool info_ok = (ie == ESP_OK && used <= total);
    if (!info_ok) ESP_LOGW(TAG, "littlefs info implausible (used %u, total %u): sizes unreported",
                           (unsigned)used, (unsigned)total);
    ESP_LOGI(TAG, "mounted %s at %s: %u KB total, %u KB free%s", LFS_PART, LFS_BASE,
             (unsigned)(total / 1024u), info_ok ? (unsigned)((total - used) / 1024u) : 0u,
             formatted ? " (formatted)" : "");
    return formatted ? 1 : 0;
```
and make `sto_info` enforce the same invariant rather than trusting it:
```c
int sto_info(sto_info_t *out)
{
    if (!out) return -1;
    size_t total = 0, used = 0;
    if (esp_littlefs_info(LFS_PART, &total, &used) != ESP_OK) return -1;
    if (used > total) return -1;                /* drv:L19: never return an underflowed free_kb */
    ...
```

- [ ] **Step 3: Assertions and checked returns on the bare primitives (drv:L20).** `sto_probe`, `sto_open`, `sto_read`, `sto_close` and `sto_unlink` carry zero assertions (all ≤ 20 code lines, so rule-5-exempt) and `sto_probe` discards `sto_unlink`'s and `sto_close`'s returns. Add exactly one *meaningful* precondition to each (R20: a pointer the function dereferences, or a descriptor it passes to the C library) and make `sto_probe` report:

```c
int sto_open(const char *path, int flags, sto_file_t *out)
{
    CORE_ASSERT_RET(path != NULL && out != NULL, STO_ASSERT_CODE, -1);   /* both are dereferenced below */
    ...
int sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read)
{
    CORE_ASSERT_RET(buf != NULL || n == 0u, STO_ASSERT_CODE, -1);        /* read() writes into buf */
    ...
int sto_close(sto_file_t f)
{
    CORE_ASSERT_RET(f >= 0, STO_ASSERT_CODE, -1);      /* closing a never-opened descriptor is a caller bug */
    ...
int sto_unlink(const char *path)
{
    CORE_ASSERT_RET(path != NULL, STO_ASSERT_CODE, -1);
    ...
```
and in `sto_probe`, replace each discarded return with a checked one so a §17.6 probe failure is visible:
```c
    if (sto_write(f, "ok", 2) != 0)  { (void)sto_close(f); (void)sto_unlink(PROBE_PATH); return -1; }
    if (sto_close(f) != 0)           { (void)sto_unlink(PROBE_PATH); return -1; }
    if (sto_unlink(PROBE_PATH) != 0) return -1;         /* drv:L20: the cleanup half is part of the probe */
```
(adapt the exact call sequence to the function as it stands; the rule is that no `sto_*` return inside `sto_probe` is discarded, and every early exit still removes the probe file.)

- [ ] **Step 4: `disp_sleep` leaves no lie behind (drv:L21).** `display_epaper.c:516` left `s_inited` true, so a `disp_refresh` without an intervening `disp_wake` talks to a sleeping panel (safe today only because `ui.c` always pairs them):

```c
/* drv:L21: deep sleep invalidates the panel's RAM/LUT state, so the driver is no longer
 * initialised -- clearing the flag makes an unpaired disp_refresh() fail its own s_inited guard
 * (and log) instead of clocking commands into a sleeping SSD1680. disp_wake() re-inits and sets
 * it again; today ui.c always pairs them, so this changes no current behaviour and removes a
 * latent one. */
    s_inited = false;
```
Confirm `disp_wake()` sets `s_inited` back (it calls the init path); if it does not, add the assignment there and say so at the checkpoint.

- [ ] **Step 5: Honest GPS stub (R28, drv:H3).** `components/drivers/gps_neo6m/gps_neo6m.c` — add `#include <errno.h>`, rewrite the file comment, and make every entry point admit there is no receiver:

```c
/* gps_neo6m.c -- PLAN-03 STUB of hal/gps.h for the NEO-6M (spec §4.6 GPS=neo6m, §7.3).
 *
 * The real UBX driver (UART setup, config push, gps_ubx_common parser, §7.5 fault ladder) lands
 * with the physical sensor in Plan 08. This stub exists so moto_neo6m -- a REQUIRED CI build --
 * links and the pipeline task starts.
 *
 * Ruling R28 (2026-10-13 audit fix wave, drv:H3): it reports FAILURE, not success. It used to
 * return 0 from gps_init, so §17.6's boot self-test and the BOOT screen read "GPS OK" on a board
 * with no receiver attached. On moto_neo6m the BOOT screen now reads GPS FAIL until Plan 08 --
 * which is the truth. The profile is still published (with live = 0: see gps_profile_t.live),
 * because the §19.4 OTA-validate gate needs to know this is not a live receiver even when init
 * fails, and because the pipeline logs the advertised rate either way. */
static const gps_profile_t s_profile = {
    .max_rate_hz = 5,
    .baud        = 38400,
    .has_pps     = 0,
    .live        = 0,            /* R1: not a real receiver until the Plan-08 driver replaces this */
    .name        = "neo6m-stub",
};

int gps_init(const gps_profile_t **out_profile)
{
    if (out_profile) *out_profile = &s_profile;
    return -ENODEV;              /* R28: no receiver is attached and none can be (Plan 08) */
}
int gps_configure(uint8_t rate_hz) { (void)rate_hz; return -ENODEV; }
int gps_poll(gps_fix_t *out)       { (void)out; return 0; }    /* 0 == "no new fix", not an error */
int gps_feed_bytes(const uint8_t *buf, size_t n) { (void)buf; (void)n; return -ENODEV; }
int gps_set_power_mode(uint8_t mode) { (void)mode; return -ENODEV; }
int gps_wake(void)                 { return -ENODEV; }
int gps_reinit_uart(uint32_t baud) { (void)baud; return -ENODEV; }

int gps_get_version(char *buf, size_t n)
{
    CORE_ASSERT_RET(buf != NULL, GPS_NEO_ASSERT_CODE, -1);   /* written by snprintf below */
    CORE_ASSERT_RET(n > 0, GPS_NEO_ASSERT_CODE, -1);         /* destination has room */
    (void)snprintf(buf, n, "neo6m-stub (no receiver)");
    return -ENODEV;              /* §17.6's "GPS present: MON-VER reply" row: no reply exists */
}
```
`gps_poll` keeps returning 0 rather than a negative: 0 is the documented "no new fix" value the pipeline's bounded drain loop tests for, and a negative would change that loop's shape in F2b's file.

- [ ] **Step 6: Honest IMU stub (R28, drv:H3).** `components/drivers/imu_mpu6050/imu_mpu6050.c` — the worse half: it reported `*pass_mask = 0x3F` ("all six axes pass") with no hardware present, so §17.6's self-test and the BOOT screen's IMU slot read **OK** on `moto_neo6m`:

```c
/* imu_mpu6050.c -- PLAN-03 STUB of hal/imu.h for the MPU-6050 (spec §4.6 IMU=mpu6050, §8).
 *
 * The real I2C driver (WHO_AM_I, register config, FIFO, self-test, motion INT) lands with the
 * physical sensor in Plan 08. Ruling R28 (drv:H3): this stub reports FAILURE. It used to return
 * a full 0x3F self-test pass mask with no chip on the bus, so §17.6's "IMU present"/"IMU genuine"
 * rows and the BOOT screen both read OK on a board with no IMU -- while imu_read_fifo returned
 * zero samples, leaving fusion, lean/g and every per-lap stat silently at zero. */
#include <errno.h>

int imu_init(void) { return -ENODEV; }

int imu_self_test(uint8_t *pass_mask)
{
    if (pass_mask) *pass_mask = 0x00;   /* R28: no axis passes, because no axis was tested */
    return -ENODEV;
}

int imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us)
{
    (void)out; (void)max; (void)read_mono_us;
    CORE_ASSERT_RET(n_read != NULL, IMU_MPU_ASSERT_CODE, -1);   /* written just below */
    *n_read = 0;
    return -ENODEV;
}

int imu_read_temp_c100(int16_t *out) { (void)out; return -ENODEV; }   /* no fake 25.00 C */
int imu_set_mode(uint8_t mode) { (void)mode; return -ENODEV; }
int imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms) { (void)thr_lsb; (void)dur_ms; return -ENODEV; }
int imu_recover(void)   { return -ENODEV; }
int imu_int_pending(void) { return 0; }    /* "no interrupt pending" is the honest answer */
```
`pipeline.c` already guards every one of these: `imu_ok = (imu_init() == 0)` → `BOOT_FAIL`; `if (imu_read_fifo(...) == 0)` → no samples consumed; `if (imu_read_temp_c100(&tc) == 0) fus_set_temp(...)` → temperature simply not fed. **No pipeline edit is needed or permitted** — confirm this by reading those three call sites before committing.

- [ ] **Step 7: Gate + commit** —

```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build        # sim drivers unaffected
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build      # the stubs' env
PANEL=ws29v2 IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
ctest --test-dir test/build --output-on-failure
git add components/drivers/storage_internal components/drivers/display_epaper_ssd1680 \
        components/drivers/gps_neo6m components/drivers/imu_mpu6050
git commit   # fix(drivers): bound sto_write, mounted-is-not-dead, probe/primitive guards, disp_sleep state, honest GPS/IMU stubs (F2a T7, M10/L19/L20/L21/R28)
```

- [ ] **CHECKPOINT** — report; state explicitly that the `moto_neo6m` BOOT screen now reads `GPS FAIL` / `IMU FAIL` **by design**, and that this is bench item **F2a-8**. Stop.

---

### Task 8 — PARALLEL — `export_serial.c`: `ESP_LOG_NONE` for transfers (R9), `SER_ASM_MAX` assert (M7), `dbg` usage (L27), two false comments

**Files:**
- Modify: `components/drivers/export_serial/export_serial.c` (only the sites listed), `components/app/include/app/cmd.h` (one macro)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `CMD_JSON_CAP` in `app/cmd.h` — F2b's `cmd.c` must keep `s_json`'s size and this macro in step; the static assert in `export_serial.c` is what enforces it.

- [ ] **Step 1: `ESP_LOG_NONE` at the three sites (R9, app:M12 / drv:M6).** `ESP_LOGE` still prints during a framed transfer and an OTA push, from any task, outside `s_uart_mtx` — the assert sink (`lt_nvs.c:406`, reachable from every task), the supervisor's stall line, `logger.c:296`'s "storage below reserve" at ~1 Hz with a full fs, `ota.c`'s own errors and `storage_internal.c`'s open/write errors all land inside the `---BEGIN`/body/`---END` bytes or the raw OTA stream, where the announced size and CRC32 do not cover them. Errors are in the error ring regardless, so the transfer window takes priority.

In `run_cmd` (`:234-235`), `run_stream` (`:326-327`) and `cmd_ota` (`:1136-1137`), replace `ESP_LOG_ERROR` with `ESP_LOG_NONE` and the comment:

```c
    esp_log_level_t saved = esp_log_level_get("*");
    /* §18.4 as amended by ruling R9: NONE, not ERROR. An ERROR line from ANY task -- the
     * core_assert_report sink, the supervisor's stall report, logger.c's below-reserve message at
     * 1 Hz on a full fs, storage_internal's open/write errors -- prints through stdout outside
     * s_uart_mtx and so lands inside this transfer's body, which its announced size and CRC32 do
     * not cover. Nothing is lost: every one of those sites also writes the §17.7 error ring, which
     * `errlog` dumps. Restored verbatim at every exit from this function. */
    esp_log_level_set("*", ESP_LOG_NONE);
```
Audit every exit path of all three functions for a matching `esp_log_level_set("*", saved);` — `run_stream`'s early `if (m.err)` return and `cmd_ota`'s three early returns already have one; confirm, do not assume.

- [ ] **Step 2: Couple `SER_ASM_MAX` to its producer (drv:M7).** `ser_emit` silently clamps to `SER_ASM_MAX` (2560) and `ser_flush` then computes the announced size and CRC32 over the **truncated** bytes, so the host receives a self-consistent frame containing cut-off JSON. The producer buffer is `cmd.c`'s `static char s_json[3072]`; today's worst case (32 × ~72 B errlog entries = 2306 B) fits with 254 B to spare, but nothing couples the two numbers. Export the cap from the producer's header — `components/app/include/app/cmd.h`, after the `CMD_CHUNK_MAX` enum (F2a's only line in that file):

```c
/* Capacity of cmd.c's JSON/text response assembly buffer (s_json). Exported so a transport can
 * assert its own frame-assembly buffer is at least this big: export_serial's ser_emit clamps
 * silently and then CRCs the truncated body, which would make a widened field or one extra key
 * turn every CONFIG_GET/ERRLOG_GET dump into a self-consistent lie (drv:M7). Keep in step with
 * s_json's declaration in cmd.c. */
#define CMD_JSON_CAP 3072u
```
and in `export_serial.c`, immediately after `#define SER_ASM_MAX 2560`:

```c
/* drv:M7: couple this buffer to its producer at compile time.
 *
 * The two numbers are deliberately NOT made equal. Raising SER_ASM_MAX to CMD_JSON_CAP (3072)
 * costs 512 B of static DRAM for a body size cmd.c cannot currently produce through this path --
 * the largest is ERRLOG_GET's 32 x ~72 B = 2306 B worst case -- and the audit-fix-wave design §4
 * caps this plan at 256 B of DRAM growth unless it names a reclaim, which it cannot. So the
 * SILENT half of the bug is removed instead (ser_emit's clamp becomes an explicit error frame
 * below) and these two asserts pin the headroom that keeps that error path unreachable today:
 * one extra key or one widened field in op_errlog/op_config_get trips the BUILD, not the wire. */
#define SER_ERRLOG_WORST 2306u   /* ERR_RING_LEN(32) x {"code":65535,"arg":4294967295,"uptime_s":4294967295,"boot":4294967295}, + 2 */
_Static_assert(SER_ASM_MAX >= SER_ERRLOG_WORST, "ERRLOG_GET's worst case must fit without the overflow path");
_Static_assert(CMD_JSON_CAP >= SER_ASM_MAX, "cmd.c's producer buffer must not be the smaller of the two");
```
and replace `ser_emit`'s silent clamp (`:195-198`) with a reported overflow:

```c
    } else if (n) {
        size_t room = sizeof c->buf - c->len;
        if (n > room) {
            /* drv:M7: this used to clamp, after which ser_flush computed the announced size AND
             * the CRC32 over the TRUNCATED bytes -- a frame the host accepts as whole while the
             * JSON is cut mid-object. Unreachable today (see the asserts above); if it is ever
             * reached, it is an ERROR frame, not a lie. */
            c->error = true;
            c->err_code = E_CONN_XFER_ABORT;
            c->len = 0;
            return 0;
        }
        memcpy(c->buf + c->len, p, n);
        c->len += n;
    }
```
(`E_CONN_XFER_ABORT` comes from `app/lt_err.h`, already included; if it is not, add the include.) Update `SER_ASM_MAX`'s own comment's last sentence to: `2560 B holds the ERRLOG_GET worst case (SER_ERRLOG_WORST, asserted below) with 254 B of margin; an overflow is an ERROR frame, never a truncated body.` Net DRAM change from this step: **0 B**. Flag the reasoning at the checkpoint — if the orchestrator prefers the straight raise to 3072, it is a two-line change plus a named 512 B reclaim.

- [ ] **Step 3: `dbg` usage text (drv:L27).** The usage block omits `dbg reset` entirely. Replace the first usage line:

```c
    printf("usage: dbg status | logtest [n] | fs | sum <id> | logck <id> | laps | rtc | mem | reset | crash | hang\n");
```
Leave the `register_cmd("dbg", ...)` help string alone — the console verb table is F2b's file region, and F2b adds `tracks`/`coredump` rows to it under ruling R5; the help-string fix is handed over in Task 10.

- [ ] **Step 4: Two false comments.** Exact old → new:

| Site | Old | New |
|---|---|---|
| `export_serial.c:18-21` | `` `dbg` carries the diagnostics verbs migrated from the 3.2-3.4 console (status/logtest/fs/sum/logck/laps) plus `rtc` …, `crash` … and `hang` …. `gps raw`/`imu raw`/`power`/`sim` are stubbed until their drivers land. `` | `` `dbg` carries the diagnostics verbs: status, logtest, fs, sum, logck, laps, rtc, mem, btn, flag, safe, reset, sim (bench scenarios: drag / laps <n> / park), crash and hang. `gps raw`/`imu raw` are stubbed until the real sensor drivers land (Plan 8) and `power` until the power task does (Plan 6); `sim` is NOT stubbed -- it dispatches to dbg_sim. `` |
| `export_serial.c:1079-1084` | `… only a worst-case ~40 ms write can still overrun it -> dropped bytes -> a byte-offset mismatch in ota_data() -> ota_abort() + "OTA-ERR". This fails SAFE …` | `… only a worst-case ~40 ms write can still overrun it -> dropped bytes. The failure MECHANISM claimed here was wrong (drv, false-claim 4): ota_data()'s offset check can never fire on this path, because ota_recv_stream supplies the DEVICE's own recvd counter, which always equals ota.c's s_recv. What actually happens is either ota_fill_chunk stalling out ("OTA-ERR timeout"), or -- if the stream resynchronises -- a corrupt image reaching ota_end and failing its SHA-256 there (E_OTA_WRITE). Both fail SAFE …` |

- [ ] **Step 5: Gate + commit** —

```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build        # the _Static_assert must PASS after the raise
IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build
DEVUX=OFF IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build   # cmd_ota/dbg are #if'd out: still clean
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
git add components/drivers/export_serial/export_serial.c components/app/include/app/cmd.h
git commit   # fix(console): ESP_LOG_NONE for transfers, SER_ASM_MAX coupled to CMD_JSON_CAP, dbg usage, two false comments (F2a T8, R9/M7/L27)
```

- [ ] **CHECKPOINT** — report; state the DRAM delta for this task (expected 0 B) and the `SER_ASM_MAX` reasoning. Stop.

---

# Session 3 — boot sequence, and the final wave (Tasks 9–10)

### Task 9 — SERIAL (after 5–8) — `app_main.c` boot sequence; `link.c`'s §17.1 rationale

**Files:**
- Modify: `main/app_main.c`, `main/CMakeLists.txt` (one `REQUIRES` entry), `components/app/link/link.c` (two comments)

**Interfaces:**
- Consumes: `SYS_NVS_DEAD`, `E_NVS_INIT` (Task 3); `board_init`'s new `int` rc (Task 6); `sup_wdt_*` (Task 3).
- Produces: nothing new. Boot-time behaviour changes: an NVS-init failure is flagged, a board bring-up failure is recorded, and a stored coredump is reported once per crash boot.

- [ ] **Step 1: Flag an NVS-init failure (app:L3).** `boot_nvs` ignored `lt_nvs_init()`'s return and booted on, after which every NVS accessor runs on handle 0: `boot_cnt` stays 0 so `open_session` regenerates `S00000_001` every boot and appends into the same `.log`, and crash-loop detection plus the safe-mode gate are dead. Replace that line:

```c
    /* app:L3: handle-0 operation is degraded-but-safe (IDF validates handles), but it silently
     * disables crash-loop detection, the safe-mode gate and unique session ids -- so it gets a
     * sys_flag a peer and the fault strip can see, not one ESP_LOGE. */
    if (lt_nvs_init() != 0) {
        sys_flags_set(SYS_NVS_DEAD);
        ESP_LOGE(TAG, "lt_nvs_init failed: boot counter, crash-loop detection and the safe-mode "
                      "gate are INOPERATIVE this boot");
    }
    /* core asserts -> error ring via the linked core_assert_report (§17.9) */
    if (nvs_erased) errlog_add(E_SYS_CFG_RESET, 0);
    if (sys_flags_get() & (1u << SYS_NVS_DEAD)) errlog_add(E_NVS_INIT, 0);   /* RAM ring at least */
```
(The `errlog_add` goes after the flag so the entry exists in the RAM ring even though its NVS persist will fail; `errlog_add` is already failure-tolerant.)

- [ ] **Step 2: Record the board bring-up result (drv:L18 caller half).** `app_main` called `board_init();` and discarded the rc, which Task 6 now sets to −EIO on any peripheral failure:

```c
    /* §4.7 step 6 (partial) + §17.6: board bring-up. Task 6 made board_init() report instead of
     * ESP_ERROR_CHECK-aborting, so a dud bus is a recorded self-test result rather than a panic
     * feeding §17.5's crash loop. The battery ADC, I2C (IMU/mag) and VSPI (panel) all hang off
     * this, so a failure here is what makes the IMU/DISPLAY BOOT slots fail honestly. */
    int brc = board_init();
    if (brc != 0) {
        ESP_LOGE(TAG, "board_init rc %d: some peripherals are unavailable this boot", brc);
        (void)errlog_add(E_SYS_PANIC, 0xB0A2Du);     /* 'BOARD' marker; §17.7 has no E_BOARD_* code */
    }
    if (level != 2u) board_gps_power(true);
```
If an `E_BOARD_INIT` code reads better than reusing `E_SYS_PANIC` with a marker arg, add `E_BOARD_INIT = 0x0510` to `lt_err.h` instead and use it — decide one way, do not leave both, and say which at the checkpoint.

- [ ] **Step 3: §17.6's coredump row.** §17.6 lists `Coredump | esp_core_dump_image_check | E_SYS_PANIC entry with summary; image kept` and nothing implemented it (`esp_core_dump_image_check` appears nowhere). Add to `app_main.c`, above `boot_subsystems`:

```c
/* §4.7 step 9 / §17.6's coredump row. Reported ONLY on a boot whose reset reason was a crash:
 * the image is deliberately KEPT (§17.10 exports it, and ruling R5's `coredump` console verb --
 * plan F2b -- reads it), so an unconditional report would add one errlog entry per boot forever
 * until someone erased it. CRC32 is the checksum §17.1 configures
 * (CONFIG_ESP_COREDUMP_CHECKSUM_CRC32), so a failed check means the dump itself is damaged and
 * the entry says so rather than promising a readable dump. */
static void boot_coredump_check(esp_reset_reason_t reason)
{
    if (reason != ESP_RST_PANIC && reason != ESP_RST_TASK_WDT && reason != ESP_RST_INT_WDT) return;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        ESP_LOGW(TAG, "crash reset but no coredump stored");
        return;
    }
    esp_err_t chk = esp_core_dump_image_check();
    ESP_LOGW(TAG, "coredump: %u B at 0x%06x, check %s (kept)", (unsigned)size, (unsigned)addr,
             chk == ESP_OK ? "ok" : esp_err_to_name(chk));
    (void)errlog_add(E_SYS_PANIC, (uint32_t)size);
}
```
with `#include "esp_core_dump.h"`, called from `app_main` right after `boot_reset_record(reason);`, and `espcoredump` appended to `main/CMakeLists.txt`'s `LT_MAIN_REQUIRES` list (F2a's only line in that file):

```cmake
set(LT_MAIN_REQUIRES core esp_system app lt_hal nvs_flash esp_timer esp_hw_support espcoredump
    board_devkit_v1 storage_internal gps_$ENV{LT_GPS} imu_$ENV{LT_IMU} display_$ENV{LT_DISPLAY})
```

- [ ] **Step 4: `app_main.c`'s three false comments.** Exact old → new:

| Site | Old | New |
|---|---|---|
| `app_main.c:3-7` | `Session 3.2 implements boot steps 1-5 …, step 6 in part …, and steps 10-11 …. Steps 7-9 … and 12-15 … land in 3.3-3.5.` | `Implements the §4.7 boot sequence: step 1 reset reason + crash counters, step 2 NVS, step 3 boot counter + crash-loop/safe-mode/recovery gate, step 4 RTC validate, step 5 config load, step 6 board + GPS power, step 7 internal storage mount, step 9 the coredump self-test row, steps 10-11 supervisor + static queues/rings + the peer link, steps 12-13 logger/pipeline/ui, and the §18.4 console. Steps 14-15 (power task, conn) have no owner until Plan 6 / sub-project C; §17.6's IMU-genuine, GPS MON-VER and battery-range ROWS are logged here but are not yet rendered as BOOT-screen lines (the screen has four slots: storage, display, GPS, IMU) -- filed.` |
| `app_main.c:239` | `Plan 04 ships no display driver, so it logs the dirty box instead of refreshing a panel; the menu (§20.7) + buttons (§20.8) are live.` | `It drives the SSD1680 through hal/display.h and runs the full §20.3 refresh ladder; the menu (§20.7) + buttons (§20.8) are live.` |
| `app_main.c:291` | `The main task returns: the supervisor and console tasks run on, and the idle tasks on both cores feed the task WDT. Pipeline/logger/ui/power start here in 3.4.` | `The main task returns: the supervisor, logger, pipeline, ui, link and console tasks (all started above) run on, and the idle tasks on both cores feed the task WDT. There is no power task (Plan 6).` |

- [ ] **Step 5: `link.c`'s §17.1 rationale and the GPIO 4 note (drv:L22, §17.1 amendment).** `link.c:52-54` already says the task is unsupervised; make it cite the amendment so a future reader does not "fix" it, and record the GPIO ownership:

```c
/* §4.3-style task: core 0 (off the pipeline's core 1), low priority (best-effort telemetry, must
 * not preempt logging/ui), and deliberately NEITHER supervised NOR task-WDT-subscribed. §17.1's
 * "all six app tasks subscribed" is amended by the 2026-10-13 audit fix wave (ruling R7) to
 * exclude this one BY DESIGN: it blocks on a UART write to a peer that may be slow, wedged or
 * absent, and a stalled or missing peer must never panic or restart the lap-timer. Every other
 * app task (pipeline, logger, ui, supervisor) subscribes via sup_wdt_subscribe(). */
```
and above the `LINK_DETECT_GPIO` block:
```c
/* GPIO detect line (§6 connector, GPIO 4). NOTE (drv:L22): this GPIO is owned HERE, not by
 * hal/board.h -- an app module configuring a pin behind the board HAL's back. Deliberate: it is a
 * single app-protocol pin whose polarity and debounce (LINK_DETECT_STABLE) are link semantics,
 * not board semantics, and routing it through the HAL would add an entry point for one caller.
 * Recorded in board.c's §3.3 pin-map block and in §3.3's own table so the inventory is complete. */
```

- [ ] **Step 6: Gate + commit** —

```bash
source tools/idf-env.sh
IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build && IDF_CCACHE_ENABLE=0 ./build.sh moto_neo6m build
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
ctest --test-dir test/build --output-on-failure
git add main/app_main.c main/CMakeLists.txt components/app/link/link.c \
        components/app/include/app/lt_err.h
git commit   # fix(boot): flag a failed lt_nvs_init, record board_init rc, §17.6 coredump row, three false comments (F2a T9, L3/L18/§17.6)
```

- [ ] **CHECKPOINT** — report. Stop.

---

### Task 10 — SERIAL (last) — gates, DRAM, spec amendments, filed issues, bench items, handoffs

**Files:**
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (only the lines below), `docs/bench/next-bench-day.md` (append one section)
- Run: three `gh issue create` calls and one `gh issue comment`

- [ ] **Step 1: Run the design §4 gate list in full.** Record every figure in the checkpoint report; a non-zero rc or a non-zero warning count is a stop.

```bash
cd /Users/benji/projects/personal/lap-timer
source tools/idf-env.sh

# -- host --
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build -j8
ctest --test-dir test/build --output-on-failure            # 100%, including test_sup_policy + test_batt_trim
# gcc-16 sweep over the compile DB (0 diagnostics)
python3 - <<'PY'
import json,subprocess,shlex,sys
db=json.load(open('test/build/compile_commands.json')); bad=0
for e in db:
    cmd=shlex.split(e['command']); cmd[0]='gcc-16'
    r=subprocess.run(cmd,cwd=e['directory'],capture_output=True,text=True)
    if r.stderr.strip(): bad+=1; print(e['file']); print(r.stderr)
print('files with diagnostics:',bad); sys.exit(1 if bad else 0)
PY

# -- linters --
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation   # 0
python3 tools/lint/venue_announce.py                                     # 0

# -- firmware, ccache OFF, clean --
for env in moto_sim moto_neo6m; do
  rm -rf build/$env
  IDF_CCACHE_ENABLE=0 ./build.sh $env build 2>&1 | tee /tmp/$env.log | tail -3
  grep -ciE "warning:|error:" /tmp/$env.log      # must be 0
done
rm -rf build/moto_sim && PANEL=ws29v2 IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
CHRG=ON IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
DEVUX=OFF IDF_CCACHE_ENABLE=0 ./build.sh moto_sim build
idf.py -C test_apps/core_selftest build                                  # rc 0, 0 warnings

# -- DRAM: free static internal DRAM per env --
for env in moto_sim moto_neo6m; do ./build.sh $env size | grep -iE "dram|free"; done
```

- [ ] **Step 2: Report the DRAM delta** in the checkpoint and in the PR body, as a table against the design §4 baselines (`moto_sim` 4192 B, `moto_neo6m` 5616 B). Expected contributors, all static:

| Site | Δ bytes | Why |
|---|---|---|
| `sup.c` `watch_t` (× `HB_COUNT` = 6) | +6 | `sup_stall_t` adds `msg_logged`, `stack_logged` replaces nothing (the two `uint32_t`s it replaces are the same size) |
| `sup.c` `s_flags_prev`, `s_stack_scan`, `s_integrity_at_s` | +9 | §17.2 heap/stack/EV_FAULT state |
| `ota.c` `s_last_data_ms`, `s_batt_skip_logged` | +5 | §19.6 timeout + the logged battery skip |
| `lt_rtc.c` `s_rtc_gen` | +4 | L4 seqlock |
| `board.c` `s_btn_last_us[3]` (was one `int64_t`) + `s_btn_pins[3]` | +22 | per-pin debounce (L17) |
| `export_serial.c` | 0 | `SER_ASM_MAX` deliberately unchanged (Task 8 Step 2) |
| **total** | **≈ +46** | inside design §4's 256 B allowance; no reclaim required |
If the measured figure exceeds 256 B, **stop at the checkpoint** and report rather than hunting a reclaim — the orchestrator decides.

- [ ] **Step 3: Amend the spec.** Exactly these edits to `docs/superpowers/specs/2026-09-14-lap-timer-design.md`; every one cites `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`.

**§15.2** — add the two live `lt_sys` keys the table omits, after the `ota_pending` row:
```
| `lt_sys` | `safe_lvl` | u8 | safe-mode level 0 normal / 1 safe / 2 recovery (§17.5 recovery amendment) |
| `lt_sys` | `stall_rst` | u8 | 1 while the previous boot ended in a supervisor-forced pipeline-stall restart (§17.2) |
```
and append to the write-policy paragraph:
> Error-ring writes are deduped and rate-limited on the error **code** alone: consecutive reports of the same code collapse onto the newest slot (latest `arg` and `uptime_s` win) and touch flash at most once per `ERRLOG_MIN_PERSIST_US`; a different code persists immediately. Keying on `(code, arg)` let a varying `arg` — `E_STO_WRITE`'s batch length — write a 388 B blob at ~1 Hz forever on a full filesystem (audit fix wave `app:M1`). The "config and calibration only on change" half is not yet honoured: `lt_cfg_save()` writes unconditionally (ruling R10, plan F2b).

**§17.1** — replace the Task-WDT bullet and the heap bullet:
> - Task WDT: `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`, `CONFIG_ESP_TASK_WDT_PANIC=y`, both idle tasks and every app task **except `link_task`** subscribed — pipeline, logger, ui and the supervisor, each through `sup_wdt_subscribe()`. Each calls `sup_wdt_feed()` once per loop iteration, and inside long operations (the logger's yielding session listings and eviction passes every 16 entries; e-paper full refresh ≤ 2 s; OTA write). `link_task` is **deliberately unsupervised and unsubscribed**: it blocks on a UART write to a peer that may be slow, wedged or absent, and a stalled or missing peer must never panic or restart the lap-timer (ruling R7; before that ruling only the supervisor and the ui were subscribed at all, so a logger wedged in LittleFS compaction hung silently).
> - Heap: `heap_caps_check_integrity_all` in the supervisor every `HEAP_INTEGRITY_PERIOD_S` (60 s) on **every** build. The original "in debug builds only" is struck: this project ships one build flavour and configures no heap poisoning, so a debug-gated check would never run (ruling R6 / F2a). Without poisoning the walk still validates every allocator block header and canary; a failure logs `E_SYS_HEAP_CORRUPT`.

**§17.2** — replace the pseudo-code block's stall, heap, stack and `EV_FAULT` lines, and add a closing paragraph:
```
for each task t: if hb[t] == last_hb[t] for > HB_STALL_S(t): log E_SYS_TASK_STALL(t) ONCE per stall episode; if t == PIPELINE: close the session, flush counters, set the stall marker, esp_restart()
heap: free (MALLOC_CAP_8BIT) < HEAP_LOW_KB (40) → SYS_HEAP_LOW + E_SYS_HEAP_LOW(min_ever); releases at ≥ HEAP_CLEAR_KB (48)
stack: uxTaskGetStackHighWaterMark per registered task < STACK_LOW_B (256) → E_SYS_STACK_LOW((t << 16) | headroom), once per task per boot
sys_flags → ui (fault icons); EV_FAULT{arg16 = changed bit, flags = 1 set / 0 cleared} to g_evt_q AND g_ui_evt_q on every change
```
> **Amendments (audit fix wave 2026-10-13).** "after saving RTC state" is **struck** from the pipeline-stall restart (ruling R6): the pipeline owns the RTC snapshot and writes it on every S/F and sector event, and a *wedged* pipeline — the only task the supervisor restarts — cannot be asked to save, so the last gate crossing is the resume point by design. The stall report is **once per episode**, not once per second, because an `ESP_LOGE` at 1 Hz lands inside framed transfers and the raw OTA stream (ruling R9). `EV_FAULT` is posted onto both FreeRTOS event queues (legal: `xQueueSend` is multi-producer safe) and **never** onto `g_stream_ring`, whose single documented producer is the pipeline. The heap flag keys on the *current* free size so it can clear with hysteresis; the min-ever figure is the errlog argument. The GPS §7.5 / IMU §8.7 / storage §13 ladders, their `CMD_GPS_LADDER_STEP` / `CMD_IMU_RECOVER` commands and the temperature throttle remain **unimplemented — Plan 8** (filed). The "< 20 KB → `conn_stop()`" heap step is struck until a conn task exists (sub-project C).

**§18.4** — in the logging bullet, `ERROR` → `NONE`:
> - ESP-IDF logging is set to level **`NONE`** for the duration of an `open`/`list`/`read` transfer and of an `ota recv` push, and restored at `close`/END. (Ruling R9: `ERROR` still let the `core_assert_report` sink, the supervisor's stall line and the logger's below-reserve message print through `stdout` outside the UART mutex, straight into the framed body or the raw OTA byte stream, where the announced size and CRC32 do not cover them. Errors reach the §17.7 error ring regardless, which `errlog` dumps.)

**§19.4** — in the supervisor line of the flow block, replace `self-test OK ∧ GPS frame received ∧ storage mounted ∧ uptime ≥ OTA_VALID_UPTIME_S` with:
> `not SYS_SAFE_MODE ∧ not SYS_STORAGE_DEAD ∧ pipeline_sensors_ok() ∧ uptime ≥ OTA_VALID_UPTIME_S`
and append to the paragraph after it:
> `pipeline_sensors_ok()` is true once a GPS fix has been ingested **or** whenever the build's GPS driver is not a live receiver (`gps_profile_t.live == 0`: the Plan-03 stubs and `gps_sim`). The previous "GPS frame received" wording made every OTA to `moto_neo6m` — the only release env — stay `PENDING_VERIFY` and be reverted by the bootloader at the next reset, after which the un-cleared `ota_pending` flag refused every later push until a USB flash (ruling R1, audit §7.1). `ota_begin` therefore also clears a `ota_pending` flag whose running slot is **not** `PENDING_VERIFY`, logging `E_OTA_ROLLBACK` once, instead of refusing on it.

**§19.5** — rewrite the precondition list:
> - `EV_MOTION` not seen for 30 s (`pipeline_still_for_ms() ≥ OTA_STILL_MIN_MS`).
> - `batt_pct ≥ BATT_OTA_MIN_PCT` (50) or charger present — **skipped and logged as `E_OTA_BATT_UNWIRED` on a build with `CFG_CHRG_WIRED == 0`**, where neither input exists (GPIO 39 floats and the battery tap is unpopulated; #54/Plan 6).
> - Logger session closed (`LOGGER_CLOSE_SESSION` acked within `OTA_CLOSE_TIMEOUT_MS`; a logger that cannot ack means the precondition is unmet and the push is refused). The close is attempted **only after** every other precondition holds, so a refused push never ends a riding session.
> - `size ≤ 0x140000 − 4096`, and not already `SYS_OTA_PENDING` or mid-transfer.
>
> **Struck (ruling R2):** power state `CONNECTED` (there is no power task — Plan 6) and `fus_is_still` (fusion is never calibrated — Plan 8/#93). §17.8's "OTA writes happen only when stationary" is now actually enforced by the 30 s stillness window. The decision is a pure, host-tested function (`app/sup_policy.h` `ota_precond_eval`).

**§19.6** — append:
> "Disconnect mid-transfer" is detected by a **receive timeout**: an `OTA_RECV` that has seen no `OTA_DATA` for `OTA_RECV_TIMEOUT_S` (30) is aborted by the supervisor's `ota_poll()` (`esp_ota_abort`, running image untouched, `E_CONN_XFER_ABORT`). It is deliberately **not** detected from the link DETECT edge: a detect flap during a healthy push would abort a good transfer, and the console `ota recv` path does not use DETECT at all. Before this (audit `app:M8`) nothing timed out at all on either transport, so a vanished peer left `SYS_OTA_PENDING` set and every later `ota_begin` returning `E_OTA_PRECOND` until a reboot.

**§3.3 pin map** — add the row:
```
| 4 | peer-connector DETECT | input, internal pull-up, active low | owned by `app/link` (not `hal/board.h`) — see `link.c` |
```

**Appendix A** — add the four new constants and annotate one:
```
| `HEAP_CLEAR_KB` | 48 | §17.2 (hysteresis release for `HEAP_LOW_KB`) |
| `OTA_STILL_MIN_MS` | 30000 | §19.5 |
| `OTA_RECV_TIMEOUT_S` | 30 | §19.6 |
| `OTA_CLOSE_TIMEOUT_MS` | 2000 | §19.5 |
| `HEAP_INTEGRITY_PERIOD_S` | 60 | §17.1 |
```
`BTN_DEBOUNCE_MS` (30) is unchanged but now has a single definition (`hal/board.h`) that the board ISR actually uses — the ISR previously used 25 ms (`app:L6`). **`OTA_VALID_UPTIME_S` (60 → 30) and `HB_STALL_S` (pipeline 1 → 5) are ruling R3 and belong to plan F4** — do not touch those rows here; F4's spec-reconciliation task runs last.

- [ ] **Step 4: File the three issues** (design §5). Run these verbatim:

```bash
gh issue create \
  --title "Plan 8: GPS/IMU/storage fault ladders + CMD_GPS_LADDER_STEP / CMD_IMU_RECOVER" \
  --body "$(cat <<'BODY'
Spec §17.2 lines 2-4, §17.3, §7.5 (GPS), §8.7 (IMU), §13 (storage). None of the three ladders
exists, and neither do the two commands the spec says the supervisor posts to the pipeline:
`CMD_GPS_LADDER_STEP(n)` and `CMD_IMU_RECOVER` are absent from `command_type_t`.

Consequence today: `SYS_GPS_DEAD`, `SYS_IMU_DEAD`, `SYS_IMU_SUSPECT` and `SYS_FUSION_DISAGREE`
have **no writer**, so their §20.5 fault-strip icons are unreachable code, and a GPS or IMU that
goes quiet is never reconfigured, re-initialised or power-cycled.

Deferred deliberately, not dropped: every ladder step calls into the real sensor driver
(`gps_configure`, `gps_reinit_uart`, `gps_set_power_mode`, `imu_recover`), and both drivers are
Plan-03 stubs until Plan 8. The 2026-10-13 audit fix wave (plan F2a) implemented the rest of the
§17.2 loop — heap, stack, EV_FAULT, task-WDT coverage — and amended §17.2 to mark these lines
"Plan 8" with this issue as the owner, so the spec no longer claims them.

Scope when Plan 8 lands:
- `CMD_GPS_LADDER_STEP` / `CMD_IMU_RECOVER` in `command_type_t` + their pipeline handlers (the
  owning task executes the step, never the supervisor — §17.2's last paragraph).
- Supervisor side: `gps_age = now - gps_last_frame_mono_us` drives the GPS ladder;
  pipeline-published `imu_fault_level` drives the IMU ladder; logger-published `sto_state` drives
  the storage ladder.
- Writers for the four `SYS_*` bits above, and the §17.2 temperature throttle
  (`SYS_DISP_TEMP_THROTTLE`) once a calibrated IMU temperature producer exists.
- Host tests for each ladder's step/retry/DEAD state machine as a pure function, the way
  `app/sup_policy.h` does for the heap/stack/stall decisions.

Source: docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md §7.3 (app:M5);
docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §5.
BODY
)" --label "plan-8"

gh issue create \
  --title "Plan 8: TIME_SYNC op (0x30) — needs a provisional-clock consumer" \
  --body "$(cat <<'BODY'
Spec §18.1 lists op `0x30 TIME_SYNC`; it is absent from `cmd.h`'s op enum and `cmd_dispatch`
answers "unknown op". Ruling R5 of the 2026-10-13 audit fix wave assigned `TRACKS_GET/PUT` and
`COREDUMP_GET/CLEAR` to plan F2b and ruled TIME_SYNC a **spec-side deferral** to Plan 8, because
unlike the other four it has no consumer yet: the timebase (`core/tb`) derives everything from
GPS time, so there is nothing for a host-supplied wall clock to feed until there is a
provisional-clock path (session ids, `Track_YYYYMMDD` names and the live clock all currently wait
for the first fix).

Scope: define what a peer-supplied time is allowed to do before the first fix (and what must be
re-stamped after it), then add the op, the `tb` entry point, and host tests for the
provisional → GPS handover.

Source: docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md §7.2 (drv:H4);
docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §2 R5 and §5.
BODY
)" --label "plan-8"

gh issue create \
  --title "Plan 8: lt_cal NVS namespace — calibration is never persisted or loaded" \
  --body "$(cat <<'BODY'
Spec §15.2 defines `lt_cal/fus` (`fus_calib_t`) and `lt_cal/mag` (QMC hard-iron offsets, M10
only). Neither key, nor the namespace, exists anywhere in `lt_nvs.c`, and `pipeline.c` calls
`fus_init(&s_fus, NULL, ...)` — always defaults. So §4.7 step 12's "calibration load" and §19.7's
"Calibration blob version mismatch → recalibration prompt" have no implementation, and the §12.3
`SESSION_HDR` calib block (`r i16[9]`, `gbias i16[3]`, `calib_flags`) plus the `CALIB` (0x0A)
record ship permanently zero.

This is the **persistence** half of #93 (whose body covers `CMD_CALIB_ORIENT` being a no-op) and
is filed separately because #93 does not name it.

Scope: `lt_cal_fus_load/save` + `lt_cal_mag_load/save` on the existing `core/blob.h` framer
(versioned + CRC16, like the other four blobs); `fus_init` fed from the loaded blob; the
`SESSION_HDR` calib block filled from `fus_calib()`; the `CALIB` record written at session open;
`FUS_LEAN_VALID` reachable in a production build; a version mismatch raising the §20.6
`CALIBRATE` one-shot (`ONESHOT_CALIBRATE` exists in `model.h` and no code path sets it).

Source: docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md §7.3 (app:M14 and the
"Spec items not implemented" list); docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §5.
BODY
)" --label "plan-8"
```
and append the §5 amendment to #93:
```bash
gh issue comment 93 --body "$(cat <<'BODY'
Amendment from the 2026-10-13 audit fix wave (design §5, plan F2a).

This issue covers `CMD_CALIB_ORIENT` being a no-op. The audit found three adjacent gaps that this
issue did **not** name, so the wave's design records them against it explicitly:

1. **Calibration persistence** — §15.2's `lt_cal/fus` and `lt_cal/mag` namespace does not exist;
   `pipeline.c` calls `fus_init(&s_fus, NULL, ...)` on every boot, so any calibration is lost at
   reset. Filed as its own Plan-8 issue (see the issue list) because it is a distinct deliverable.
2. **The `CALIB` record and the `SESSION_HDR` calib block** (§12.3) are never written:
   `ses_encode_calib` exists with no caller, and the header's `r i16[9]` / `gbias i16[3]` /
   `calib_flags` fields ship zero in every session file. `logger.c:318`'s
   `/* calib block left zero until 3.4 */` comment was corrected to say so.
3. **`FUS_LEAN_VALID` is unreachable in production** — fusion is never oriented, so
   `fus.c:274`'s un-oriented `|g_lon|` branch is the only one that runs. The practical bench
   consequence is already noted in `docs/bench/next-bench-day.md` item 1: the drag 100-0 brake
   gate depends on the GPS Doppler re-anchor rather than on integrated longitudinal g, so a
   missing 100-0 row is expected today, not a defect.

Also recorded: §20.6's `ONESHOT_CALIBRATE` exists in `core/ui/model.h` and no code path ever sets
it — the "CALIBRATE" one-shot is unreachable until this issue lands.

Nothing here changes this issue's scope; it records what else must land with it so none of it is
lost. Source: docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md §7.3,
docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §5.
BODY
)"
```

- [ ] **Step 5: Append the bench items** to `docs/bench/next-bench-day.md`, under the existing `## Open items` list, as a new section. These are the **only** on-device claims this plan makes; everything else it changes is proven by a host test or a build.

```markdown
### F2a bench items (audit fix wave — firmware supervision + OTA, 2026-10-13)

Standing rules unchanged: flash by dev-kit OTA push, never open the lap-timer's USB port for 35 s
after a push, sign in place with `keys/laptimer_priv.pem`. Identify the two ports first.

- [ ] **F2a-1. OTA validates on the production build (R1 — the audit's only CRITICAL in this slice).**
  Prerequisite: a signed `moto_neo6m` image, flashed once over USB (hold the dev-kit in reset via
  RTS on its adapter, `tools/sign_release.sh moto_neo6m`). Then push a SECOND signed `moto_neo6m`
  image with `tools/devkit.py --port <dk> flash dist/<signed>.bin` and wait 35 s untouched.
  Expect: `OTA-END 0x0000`, the new version in the banner after the reboot, and at ~30 s
  `sup: OTA image validated (uptime 30s)` **with no GPS fix and no receiver attached** — plus
  `errlog` carrying `0x0805` (`E_OTA_VALIDATED`) and NOT `0x0806`. Then `dbg reset`: the NEW
  version must still be running (before this fix the bootloader reverted here, every time).
- [ ] **F2a-2. §19.5 stillness + session close (R2).** On `moto_sim`: `dbg sim laps 20`, and while
  it is lapping push a signed image. Expect the push to be refused immediately with
  `OTA-ERR 0x0801` and `errlog` 0x0801, with the session still open and still logging
  (`dbg fs` shows the `.log` growing). Then `dbg sim park`, wait ≥ 30 s, push again. Expect:
  accepted, and `logger: session <id> closed (reason 1)` printed **before** `OTA-READY` — i.e. the
  logger stopped writing flash before `esp_ota_write` started. `dbg sum <id>` afterwards shows a
  finalised summary with END reason 1.
- [ ] **F2a-3. EV_FAULT reaches both consumers (R6).** With a session open, `dbg flag set 10`
  (SYS_HEAP_LOW), wait 2 s, then `dbg flag clear 10`. Expect: the fault strip gains and loses its
  glyph within ~2 s each way, and `dbg sum <id>` lists **two** EVENT records of type 15
  (`EV_FAULT`) with `arg16 = 10` — the first `flags = 1`, the second `flags = 0`. Repeat with
  bit 5 (`SYS_STORAGE_DEAD`) to confirm it is not bit-specific.
- [ ] **F2a-4. Task-WDT coverage is real (R7).** Read the boot log. Expect exactly three
  `sup: wdt subscribe hb=N rc=0` lines — `hb=1` (supervisor), `hb=0` (pipeline), `hb=2` (logger) —
  and **no** line for the link task (deliberate, §17.1 amendment). The ui subscribes directly and
  prints no line until plan F2b converts it (handed over). Then `dbg hang` and confirm the task
  WDT still panics in ~5 s as before.
- [ ] **F2a-5. Stack check is live and quiet (R6).** `dbg mem` twice — once with the menu open
  (the `handle_event → drag_rows_refill` branch) and once on a plain riding render (the
  `render_fb → disp_refresh` branch), per #53's note that the two deepest ui branches are mutually
  exclusive. Record all four high-water figures. Expect every one > 256 B and `errlog` carrying no
  `0x0509` (`E_SYS_STACK_LOW`). There is **no injection verb** for a low stack, so this item
  records that the detector is live and silent; the threshold arithmetic itself is proven by
  `test_sup_policy`'s `test_stack_low`. If any figure IS under 256 B, the errlog entry must be
  present — that is the pass condition in that case.
- [ ] **F2a-6. Transfers survive an ERROR storm (R9).** Reuse bench item 7's setup: leave
  `dbg sim laps 200` running until `E_STO_EVICT`/`E_STO_FULL` appear, which puts `logger.c`'s
  "storage below reserve" `ESP_LOGE` on the wire at ~1 Hz from the logger task. With that running,
  download a ≥ 200 KB session: `lt open <id> log` then `lt read` to completion. Expect: the
  received body's CRC32 matches the `---END` value, `dbg logck <id>` passes on the device side,
  and **no log line of any level appears between `---BEGIN` and `---END`**. Immediately after
  `---END`, normal `I (...)` lines must resume — that is the level-restore check. Before this fix
  the ERROR lines landed inside the body and the CRC failed.
- [ ] **F2a-7. OTA receive timeout on both transports (M8 / §19.6).** (a) Dev-kit/framed path:
  start `tools/devkit.py --port <dk> flash dist/<signed>.bin`, kill the host with Ctrl-C after
  ~20 % and leave the lap-timer alone. Expect within 30 s: `ota: recv timeout after 30 s at
  <n>/<size> B`, `errlog` 0x0702 (`E_CONN_XFER_ABORT`), and — without any reboot — a second push
  succeeding. (b) `lt status` immediately after the kill must show byte 1 (`state`) = 1
  (`LT_STATE_OTA`, M7), and = 0 after the timeout fires.
- [ ] **F2a-8. The neo6m BOOT screen tells the truth (R28).** On the USB-flashed `moto_neo6m`
  image: the BOOT screen reads `GPS FAIL` and `IMU FAIL`. **This is the expected result, not a
  regression** — the Plan-03 stubs now return `-ENODEV` instead of reporting a 0x3F self-test pass
  with no chip present. `dbg status` shows no fix. The slots flip to OK when Plan 8 lands the real
  drivers.
- [ ] **F2a-9. CHRG no longer spoofs the power precondition (M11 / #54).** `errlog` after any OTA
  push on the default build carries exactly one `0x0807` (`E_OTA_BATT_UNWIRED`) per boot — the
  §19.5 battery check is now an explicitly-logged skip rather than a floating pin silently
  answering "charging". Nothing else about the push changes.
- [ ] **F2a-10. Buttons: 30 ms per-pin debounce (L6/L17).** With the 6 mm tactiles (#78): press
  MODE and UP in quick succession (< 100 ms apart) ten times. Expect both edges registered every
  time (ui log shows two distinct `btn` masks) — a single shared 25 ms stamp used to drop the
  second. Then hold UP+DOWN for 2 s and confirm the §20.8 full-refresh still fires (`kind=F`).
- [ ] **F2a-11. RTC snapshot reads are not torn (L4).** While `dbg sim laps 20` is running, issue
  `dbg rtc` twenty times. Expect 20/20 `RTC_VALID` with a monotonically sensible `lap_no` — zero
  `RTC_INVALID`. The unlocked read used to fail the CRC on a torn copy.
- [ ] **F2a-12. The §17.6 coredump row (and the 60 s heap walk).** `dbg crash`; on the next boot
  expect `coredump: <n> B at 0x<addr>, check ok (kept)` and `errlog` 0x0502 (`E_SYS_PANIC`) with
  `arg` = that byte count. Then a 10-minute read-only capture: `hb[1]` must advance once per
  second throughout (the new 60 s `heap_caps_check_integrity_all` walk must not perturb the
  supervisor's cadence) and no `0x050E` (`E_SYS_HEAP_CORRUPT`) may appear.
```

- [ ] **Step 6: Record the handoffs.** Add this table to the PR body (it is the plan's contract with F2b and F4 — nothing in it is an F2a edit):

| Item | Hand to | What |
|---|---|---|
| `app:L5` | **F2b** | §17.5's "one full-screen SAFE MODE render then display idle" is not enforced: `btn_long` (`ui.c:535-538`) dismisses the persistent SAFE one-shot and the ui resumes normal riding screens, pages and menu in safe mode. F2b owns `ui.c`. |
| `app:L8` | **F4** | `cmd.c`'s statics total ~10.5 KB DRAM (`s_sess[64]` ≈ 4.6 KB, `s_json` 3072, `s_exp` ~1.5 KB, `s_sr` ~0.8 KB, `s_cw` ~0.5 KB) and none appears in §4.8's budget table, which matters against the 124 KB `.bss` ceiling. Add the rows. |
| `drv:L14` (console half) | **F2b** | `export_serial.c:1196` ignores `esp_console_cmd_register()`'s return (rule 7) — a failed registration silently yields a missing command — and `:550`'s `xQueueSend(g_log_req_q, …)` is unchecked and not even `(void)`-cast. Both are in the verb-table region F2b owns (it is adding `tracks`/`coredump` rows there under R5). F2a fixed the `board.c` half. |
| `drv:L27` (help string) | **F2b** | `register_cmd("dbg", …)`'s help string omits `safe` and `sim`. Same region. F2a fixed the two `usage:` `printf`s. |
| `ui.c` WDT wrapper | **F2b** | `ui.c:1696` calls `esp_task_wdt_add(NULL)` directly; convert it to `sup_wdt_subscribe(HB_UI)` / `sup_wdt_feed()` so all four app tasks report identically at boot (bench item F2a-4 expects three lines until this lands). |
| `core/event.h` `EV_FAULT` comment | **F4** | The comment reads `arg16 = error code`; the supervisor now sends `arg16 = the sys_flags bit index, flags = 1 set / 0 cleared`. `event.h` belongs to F1a/F0, both merged before F2a, so the one-line comment correction goes with F4's reconciliation task. New text: `EV_FAULT = 15, /* arg16 = sys_flags bit index, flags = 1 set / 0 cleared (supervisor) */`. |
| `logger.h:3-7` | **F2b** | "Session open/close is commanded by the power task" — there is no power task. F2a corrected the identical claim in `status.h`; `logger.h` is F2b's. |
| `OTA_VALID_UPTIME_S` / `HB_STALL_S` spec rows (R3) | **F4** | Appendix A and §17.2/§17.7 still read 60 s and 1 s; the firmware's 30 s and 5 s are authoritative. F2a added the pointer comment in `lt_err.h`; the spec rows are F4's. |

- [ ] **Step 7: Final gate re-run and the PR.** Re-run Step 1 in full on the final tree (every earlier task's gate was run against an intermediate tree), then open the PR with: the task list, the DRAM table from Step 2, the gate output, the bench-item list from Step 5 marked "not yet run — bench day after F2b merges" per design §4, the handoff table from Step 6, and the three issue numbers from Step 4.

```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/bench/next-bench-day.md
git commit   # docs: §15.2/§17.1/§17.2/§18.4/§19.4/§19.5/§19.6/§3.3/Appendix A amendments + F2a bench items (F2a T10)
gh pr create --fill
```

- [ ] **CHECKPOINT** — report the full gate output, the DRAM table, the three issue URLs and the PR URL. Stop.

---

## Cleanup done

Every item below is something this plan **removes or corrects**, not something it adds. A reviewer
should be able to tick each one off against the diff.

| # | Removed / corrected | Was | Task |
|---|---|---|---|
| 1 | `pipeline_gps_seen()` | the OTA-validate gate no stub build could ever satisfy | 2, 3 |
| 2 | `qsort(s, 64, sizeof(int), cmp_int)` + `static int cmp_int(...)` + `#include <stdlib.h>` in `board.c` | an unregistered rule-9 function pointer into a recursive newlib sort | 6 |
| 3 | `BTN_DEBOUNCE_US 25000` and the single shared `s_btn_last_us` | 25 ms against Appendix A's 30 ms, and one stamp for three buttons | 6 |
| 4 | `*pass_mask = 0x3F` in `imu_self_test` and `*out = 2500` in `imu_read_temp_c100` | a self-test pass and a temperature with no chip on the bus | 7 |
| 5 | `gps_init`/`imu_init` returning 0 | "GPS OK"/"IMU OK" on the BOOT screen of a board with no sensors | 7 |
| 6 | `*out = (gpio_get_level(PIN_CHRG) == 0)` on an unwired build | a floating input-only pin answering a power question and bypassing §19.5 | 6 |
| 7 | the unconditional `esp_sleep_enable_ext0_wakeup(PIN_CHRG, 0)` | an immediate EXT0 wake that defeats PARK/SHUTDOWN, labelled "harmless if CHRG unwired" | 6 |
| 8 | `ser_emit`'s silent clamp | a frame whose announced size and CRC32 agreed with its own truncation | 8 |
| 9 | `esp_log_level_set("*", ESP_LOG_ERROR)` at three sites | the level that still let ERROR lines into framed bodies and the raw OTA stream | 8 |
| 10 | the `(code, arg)` key in `errlog_add`'s rate limit | a varying `arg` writing a 388 B NVS blob at 1 Hz forever on a full fs | 5 |
| 11 | the unguarded `while (off < n)` in `sto_write` | an unbounded loop that hangs the logger on `write() == 0` | 7 |
| 12 | `return -1` on `sto_mount`'s `used <= total` assertion | a successfully-mounted filesystem declared `SYS_STORAGE_DEAD` | 7 |
| 13 | nine `ESP_ERROR_CHECK(...)` calls in `board_init`/`board_init_battery_adc` | a dud bus aborting into §17.5's crash loop instead of §17.6's ladder | 6 |
| 14 | `watch_t.last_hb` / `watch_t.stalled_s` and the `stalled_s = 0` re-arm | a 1 Hz `ESP_LOGE` for the whole duration of any stall | 3 |
| 15 | `sup.c`'s "wired here then" ladder comment and `check_stalls`' "RTC snapshot save lands in 3.5" | two promises three plans stale | 3 |
| 16 | 14 false comment blocks across `sup.c` (3), `app_main.c` (3), `ota.c` (2), `lt_assert.h`, `lt_err.h`, `lt_nvs.h`, `lt_rtc.h`, `status.h`, `lt_ipc.h`, `export_serial.c` (2), `board.c` (1) | claims the audit's false-claim register contradicted | 3,5,6,8,9 |
| 17 | `out[LT_ST_OFF_STATE] = 0; /* device state machine lands later */` | §19.4's `STATE_OTA` unreportable | 4 |
| 18 | `lt_errlog_clear`'s stale `s_last_valid` | a collapse onto a slot that no longer exists | 5 |
| 19 | `disp_sleep` leaving `s_inited` true | a `disp_refresh` able to clock commands into a sleeping panel | 7 |
| 20 | the discarded returns in `sto_probe` and `board_prepare_deep_sleep` | a §17.6 probe and a deep-sleep wake config that could not fail visibly | 6, 7 |

## Self-Review

**1. Spec coverage** — every ruling and finding assigned to F2a maps to a task:

| Assigned | Task | Assigned | Task |
|---|---|---|---|
| R1 (OTA validate gate, stale pend) | 2, 3, 4 | R2 (§19.5 preconditions) | 1, 2, 4 |
| R6 (heap/stack/`EV_FAULT`, §17.2 RTC strike) | 1, 3, 10 | R7 (task WDT) | 3, 10 |
| R9 (`ESP_LOG_NONE`, stall rate limit) | 1, 3, 8, 10 | R19 (insertion sort) | 6 |
| R20 (padding/side-effect assertions, own files) | Global Constraints + 3, 4, 6, 7 | R28 (honest stubs) | 7 |
| §3 `EV_FAULT` emission | 3 | §4 gates | 10 |
| §5 filings (ladders, TIME_SYNC, #93 text) | 10 | app:C1 | 2, 3, 4 |
| app:H1 | 1, 4 | app:H2 | 3 |
| app:M1 | 5 | app:M5 | 1, 3 |
| app:M7 | 4 | app:M8 | 1, 4 |
| app:M9/M10 (`lt_err.h` pointer only; rows are F4's) | 4, 10 | app:M12 | 8 |
| app:M14 (file-only: #93 comment) | 10 | app:L3 | 3, 9 |
| app:L4 | 5 | app:L5 (→ F2b) | 10 handoff |
| app:L6 | 6 | app:L8 (→ F4) | 10 handoff |
| drv:C1 (IRAM + `ESP_INTR_FLAG_IRAM`) | 6 | drv:H3 | 7 |
| drv:M6 | 8 | drv:M7 | 8 |
| drv:M10 | 7 | drv:M11 | 4, 6 |
| drv:M12 | 6 | drv:L14 (board half; console half → F2b) | 6, 10 |
| drv:L17 | 6 | drv:L18 | 6, 9 |
| drv:L19 | 7 | drv:L20 | 7 |
| drv:L21 | 7 | drv:L22 | 6, 9, 10 |
| drv:L27 (usage; help string → F2b) | 8, 10 | false claims: `board.c:305`, `export_serial.c:18-21`, `:1079-1084` | 6, 8 |
| §17.1 heap integrity | 3, 10 | §17.6 coredump row | 9 |
| §19.3 layout self-check | 4 | §15.2 `lt_sys` keys | 10 |
| §19.4 `STATE_OTA` | 4 | §19.6 abort-on-disconnect | 4, 10 |

Deliberate non-coverage, each named with its owner: §17.2's three ladders + temperature throttle
and the four `SYS_*` bits they would write (Plan 8, filed in Task 10 Step 4); the `< 20 KB →
conn_stop()` heap step (sub-project C, §17.2 amended); §17.6's IMU-genuine / GPS MON-VER /
battery-range rows as *rendered* BOOT lines (the screen has four slots — `ui.c` is F2b's, noted
in the `app_main.c:3-7` comment); `status.batt_mv`/`batt_pct` (Plan 6, #54); `OTA_VALID_UPTIME_S`
and `HB_STALL_S` spec rows (R3 → F4).

**2. Placeholder scan** — no "TBD", "implement later", "add error handling", "similar to Task N",
or "write tests for the above". Every code step carries the actual C or CMake. Three steps
deliberately hand the implementer a *decision* rather than a placeholder, each with both options
spelled out and an instruction to report which was taken: Task 3 Step 7 (split
`restart_stalled_pipeline` out if rule 5 trips), Task 7 Step 4 (`disp_wake` may already restore
`s_inited`), Task 9 Step 2 (`E_BOARD_INIT 0x0510` versus an `E_SYS_PANIC` marker arg).

**3. Type consistency** — checked across tasks: `sup_stall_t`/`sup_stall_step`/`SUP_STALL_LOG`/
`SUP_STALL_RESTART` (defined T1, used T3); `sup_heap_eval`/`SUP_HEAP_ENTER`/`SUP_HEAP_CLEAR`
(T1 → T3); `sup_stack_low`, `sup_flag_next_change` (T1 → T3); `ota_pre_t` field names
`still_ms`/`size`/`in_flight`/`session_open`/`batt_ok`/`flag_pending`/`slot_pending_verify` and
`OTA_PRE_OK`/`REFUSE`/`CLOSE_SESSION`/`CLEAR_STALE` (T1 → T4); `ota_recv_stale(now_ms, last_ms,
timeout_s)` (T1 → T4); `pipeline_sensors_ok`/`pipeline_still_for_ms` (T2 → T3, T4);
`sup_wdt_subscribe(uint8_t)`/`sup_wdt_feed(void)` (T3 → T3's foreign-file edits, T9's comment);
`gps_profile_t.live` (T2 → T3's accessor, T7's stub); `batt_trimmed_mean(int*, int, int)` +
`BATT_SAMPLES`/`BATT_TRIM` (T6, and `board.c` stops defining the latter two locally — they move
to `batt_pure.h`, so the old `#define BATT_SAMPLES 64` / `#define BATT_TRIM 8` in `board.c` must
be **deleted**, not duplicated); `CMD_JSON_CAP` (T8, `cmd.h` → `export_serial.c`);
`LT_STATE_IDLE`/`LT_STATE_OTA` (T4, `status.h` → `status.c`); `CFG_CHRG_WIRED` (T4's CMake +
`build_config.h.in` → T4's `ota.c`, T6's `board.c`); new error codes `E_SYS_WDT_SUBSCRIBE 0x050D`,
`E_SYS_HEAP_CORRUPT 0x050E`, `E_NVS_INIT 0x050F` (T3) and `E_OTA_BATT_UNWIRED 0x0807` (T4) — all
distinct, all append-only, and `0x0510` is left free for T9's optional `E_BOARD_INIT`;
`SYS_NVS_DEAD` = bit 15 (T3 → T9), the last STATUS-visible bit.
