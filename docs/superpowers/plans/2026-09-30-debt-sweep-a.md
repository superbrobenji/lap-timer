# Debt Sweep A — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close #37, #59, #60, #62, #73 in code, #64/#66 by verification and #63 by a register amendment — lap-timer robustness items skipped by earlier plans, host-testable now and bench-verified on the same day as `p07c-d1`.

**Architecture:** One bounded logger request/reply (FreeRTOS task notification carrying the rc) serves both the planned-restart session close and the delete-on-the-logger-task. A pure core blob framer (version byte + payload + CRC16) wraps all four NVS blobs. A pure session-id validator lives in the shared `lt_proto.h`. Safe mode gains a second level, recovery, that starts only the service tasks.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2 (FreeRTOS task notifications, NVS), Unity host tests (lap-timer `test/`, dev-kit `devcontroller/test/`).

**Spec:** `docs/superpowers/specs/2026-09-30-debt-sweep-a-design.md` (binding). Fact sheet: the sweep workspace's `debt-sweep-A-facts.md`.

## Global Constraints

- Power of 10 on all firmware and `core` code: functions of > 20 code lines carry ≥ 2 `CORE_ASSERT_*`/`LT_ASSERT_*` (never abort); every loop bounded by a constant or a table field; no recursion, heap, goto or function pointers; ≤ 60 code lines per function; `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0.
- Zero warnings: host suites (`-Wall -Wextra -Werror -Wshadow -Wconversion`; lap-timer `test/` AND dev-kit `devcontroller/test/`), gcc-16 sweep of `test/build/compile_commands.json` clean, clean ccache-disabled builds of `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 ./build.sh moto_sim build`, and the dev-kit firmware (`devcontroller/`, its own `build.sh`/idf build — see env-notes).
- `app/lt_proto.h` stays IDF-free (stdint/stddef/stdbool only): both host harnesses compile it.
- Only the logger task mutates `/sessions` (unlink, write, format); cmd lists/reads only.
- The ui and pipeline tasks never block on the logger (they never call `logger_request_sync`).
- The cfg blob's on-flash bytes are unchanged; the three small blobs change layout once (one-time reset, logged).
- Never flash from a subagent; the controller runs bench gate `dsA-d1` with the user.
- Commit messages end with the session trailers used on this branch (`git log -1`).

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `components/core/sys/blob.c`, `include/core/blob.h`, `test/test_blob.c` | versioned+CRC blob framing (pure) | 1 |
| `components/core/include/core/ses.h` | `SES_END_*` reason enum | 3 |
| `components/app/sys/lt_nvs.c`, `include/app/lt_nvs.h`, `include/app/lt_err.h`, `test/test_cfg.c` | four blobs on the framer; cfg migrate; `E_NVS_BLOB_RESET` | 2 |
| `components/app/include/app/lt_ipc.h`, `sys/lt_ipc.c`, `logger/logger.c`, `supervisor/sup.c` | request/reply; close on planned restart | 3 |
| `components/app/logger/logger.c`, `cmd/cmd.c`, `lt_hal/include/hal/storage.h` | `LOGGER_DELETE_SESSION`; delete via logger | 4 |
| `components/app/include/app/lt_proto.h`, `test/test_proto.c`, `cmd/cmd.c`, `devcontroller/components/webapi/webapi.c`, `devcontroller/test/test_session_id.c` | session-id validation | 5 |
| `main/app_main.c`, `lt_nvs.[ch]`, `lt_sup.h`, `sup.c`, `export_serial.c`, dev-kit flag names, design spec §17.5 | recovery mode | 6 |
| `docs/power-of-10-deviations.md`, roadmap, spec §9 notes, issue closing comments | docs | 7 |

---

### Task 1: Blob framing (pure core) — `blob_wrap` / `blob_unwrap`

**Files:**
- Create: `components/core/sys/blob.c`, `components/core/include/core/blob.h`, `test/test_blob.c`
- The core CMake globs `*.c` recursively and `test/CMakeLists.txt` globs `test_*.c`: no CMake edits.

**Interfaces (produced):**

```c
/* core/blob.h — on-flash record framing (§15.2/§17.9): [version u8][payload n][crc16 LE] */
#define BLOB_OVERHEAD 3u
size_t blob_wrap(uint8_t ver, const void *payload, size_t n, uint8_t *out, size_t cap);
/* writes n + BLOB_OVERHEAD bytes; returns that size, or 0 when cap is too small / n == 0 */
int blob_unwrap(uint8_t expect_ver, const uint8_t *in, size_t n, void *payload, size_t payload_len,
                uint8_t *ver_out);
/* 0 ok (payload filled); -1 n != payload_len + BLOB_OVERHEAD; -2 CRC mismatch;
 * -3 version mismatch (ver_out = stored version, payload untouched). ver_out may be NULL. */
```

CRC = `ses_crc16(in, 1 + payload_len)` (version byte + payload), stored little-endian after the payload — the same bytes today's cfg blob carries (its `cfg_t.version` is the leading byte and the CRC covers all of `cfg_t`).

- [ ] **Step 1: Failing tests** — `test/test_blob.c`:

```c
#include "unity.h"
#include "core/blob.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}
static const uint8_t PAY[4] = { 0x11, 0x22, 0x33, 0x44 };
static void test_round_trip(void)
{
    uint8_t buf[16]; uint8_t out[4]; uint8_t ver = 0;
    TEST_ASSERT_EQUAL_UINT(7, blob_wrap(3, PAY, 4, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(3, buf[0]);
    TEST_ASSERT_EQUAL_MEMORY(PAY, buf + 1, 4);
    TEST_ASSERT_EQUAL_INT(0, blob_unwrap(3, buf, 7, out, 4, &ver));
    TEST_ASSERT_EQUAL_UINT8(3, ver);
    TEST_ASSERT_EQUAL_MEMORY(PAY, out, 4);
}
static void test_cap_too_small_and_empty(void)
{
    uint8_t buf[6];
    TEST_ASSERT_EQUAL_UINT(0, blob_wrap(1, PAY, 4, buf, sizeof buf));   /* needs 7 */
    TEST_ASSERT_EQUAL_UINT(0, blob_wrap(1, PAY, 0, buf, sizeof buf));   /* empty payload rejected */
}
static void test_size_mismatch(void)
{
    uint8_t buf[16]; uint8_t out[4];
    (void)blob_wrap(1, PAY, 4, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, 6, out, 4, NULL));
    TEST_ASSERT_EQUAL_INT(-1, blob_unwrap(1, buf, 8, out, 4, NULL));
}
static void test_crc_mismatch(void)
{
    uint8_t buf[16]; uint8_t out[4] = { 9, 9, 9, 9 };
    (void)blob_wrap(1, PAY, 4, buf, sizeof buf);
    buf[2] ^= 0x80;
    TEST_ASSERT_EQUAL_INT(-2, blob_unwrap(1, buf, 7, out, 4, NULL));
    TEST_ASSERT_EQUAL_UINT8(9, out[0]);                                   /* untouched */
}
static void test_version_mismatch_reports_stored(void)
{
    uint8_t buf[16]; uint8_t out[4] = { 9, 9, 9, 9 }; uint8_t ver = 0;
    (void)blob_wrap(2, PAY, 4, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(-3, blob_unwrap(3, buf, 7, out, 4, &ver));
    TEST_ASSERT_EQUAL_UINT8(2, ver);
    TEST_ASSERT_EQUAL_UINT8(9, out[0]);                                   /* untouched */
    TEST_ASSERT_EQUAL_INT(0, blob_unwrap(2, buf, 7, out, 4, NULL));       /* accepted at its own version */
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_round_trip); RUN_TEST(test_cap_too_small_and_empty); RUN_TEST(test_size_mismatch); RUN_TEST(test_crc_mismatch); RUN_TEST(test_version_mismatch_reports_stored); return UNITY_END(); }
```

- [ ] **Step 2: Run to fail** — `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build` → `test_blob` fails to compile (missing header).

- [ ] **Step 3: Implement** `components/core/sys/blob.c`:

```c
#include "core/blob.h"
#include "core/core.h"
#include "core/ses.h"      /* ses_crc16 */
#include <string.h>
#define BLOB_ASSERT_CODE 0x0AF3
size_t blob_wrap(uint8_t ver, const void *payload, size_t n, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(payload != NULL && out != NULL, BLOB_ASSERT_CODE, 0u);
    if (n == 0u || cap < n + BLOB_OVERHEAD) return 0u;
    out[0] = ver;
    memcpy(out + 1, payload, n);
    uint16_t crc = ses_crc16(out, n + 1u);
    out[n + 1u] = (uint8_t)(crc & 0xFFu);
    out[n + 2u] = (uint8_t)(crc >> 8);
    CORE_ASSERT_RET(n + BLOB_OVERHEAD <= cap, BLOB_ASSERT_CODE, 0u);   /* postcondition: in bounds */
    return n + BLOB_OVERHEAD;
}
int blob_unwrap(uint8_t expect_ver, const uint8_t *in, size_t n, void *payload, size_t payload_len,
                uint8_t *ver_out)
{
    CORE_ASSERT_RET(in != NULL && payload != NULL, BLOB_ASSERT_CODE, -1);
    CORE_ASSERT_RET(payload_len > 0u, BLOB_ASSERT_CODE, -1);
    if (n != payload_len + BLOB_OVERHEAD) return -1;
    uint16_t want = ses_crc16(in, payload_len + 1u);
    uint16_t got  = (uint16_t)(in[payload_len + 1u] | (in[payload_len + 2u] << 8));
    if (want != got) return -2;
    if (ver_out != NULL) *ver_out = in[0];
    if (in[0] != expect_ver) return -3;
    memcpy(payload, in + 1, payload_len);
    return 0;
}
```

Header `core/blob.h`: include guard, `<stdint.h>`/`<stddef.h>`, the two prototypes and `BLOB_OVERHEAD` with the doc comment above. Confirm `ses_crc16(const uint8_t*, size_t)` is in `core/ses.h` (it is; `lt_nvs.c` already calls it).

- [ ] **Step 4: Run to pass** — `./test/build/test_blob` 5/5; full `ctest` green.

- [ ] **Step 5: Gate + commit** — lint 0; host 0 warnings; gcc-16 sweep clean; clean `moto_sim` build 0 warnings (the new core file compiles into the firmware).

```
feat(core): blob_wrap/blob_unwrap — versioned + CRC16 framing for on-flash records (debt sweep A T1, #37)
```

---

### Task 2: All four NVS blobs on the framer; cfg migrate (#37)

**Files:**
- Modify: `components/app/sys/lt_nvs.c` (`load_blob`, `lt_nvs_init`, `persist_counters`, `errlog_persist`, `lt_errlog_clear`, `lt_crashlog_push`, `lt_cfg_load`, `lt_cfg_save`), `components/app/include/app/lt_err.h` (+ `E_NVS_BLOB_RESET = 0x050B`), `test/test_cfg.c` (one case)

**Interfaces (consumed):** `blob_wrap`/`blob_unwrap` (Task 1); `cfg_migrate(cfg_t*, uint8_t from_version)` (exists, `core/config/cfg.c:92`).

Design (spec §3): RAM mirrors stay aligned and unchanged (`s_counters`, `s_ring`, `s_crash[]`); the framed bytes are built in a scratch buffer at save/load. Per-blob versions `LT_CTR_VER 1`, `LT_RING_VER 1`, `LT_CRASH_VER 1`. For the cfg blob the payload is `cfg_t` minus its own leading version byte (`(const uint8_t *)c + 1`, `sizeof(cfg_t) - 1`) with `ver = c->version`, so the on-flash bytes are exactly today's.

- [ ] **Step 1: Failing host test** — `test/test_cfg.c`, add (next to `test_migrate_v1_is_noop`) and register in `main`:

```c
#include "core/blob.h"
static void test_cfg_blob_older_version_path(void)
{
    /* lt_cfg_load's decision (Plan debt-sweep-A §3): unwrap at CFG_VERSION -> -3 with the stored
     * version; re-unwrap at that version, then cfg_migrate(); unknown -> defaults. Version 0 is
     * unknown today, so the migrate step must say -1. */
    cfg_t c; cfg_defaults(&c);
    uint8_t buf[sizeof(cfg_t) + BLOB_OVERHEAD]; uint8_t ver = 0xFF;
    TEST_ASSERT_EQUAL_UINT(sizeof buf - 1, blob_wrap(0, (const uint8_t *)&c + 1, sizeof(cfg_t) - 1, buf, sizeof buf));
    cfg_t d; memset(&d, 0, sizeof d);
    TEST_ASSERT_EQUAL_INT(-3, blob_unwrap(CFG_VERSION, buf, sizeof buf - 1, (uint8_t *)&d + 1, sizeof(cfg_t) - 1, &ver));
    TEST_ASSERT_EQUAL_UINT8(0, ver);
    TEST_ASSERT_EQUAL_INT(0, blob_unwrap(ver, buf, sizeof buf - 1, (uint8_t *)&d + 1, sizeof(cfg_t) - 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, cfg_migrate(&d, ver));
}
```

(`sizeof buf - 1` because the payload is one byte shorter than `cfg_t`.) Run → passes only once `core/blob.h` exists (Task 1) — this case documents the load decision; it must be green before Step 3.

- [ ] **Step 2: Implement** in `lt_nvs.c`:

```c
#include "core/blob.h"
#define LT_CTR_VER   1u
#define LT_RING_VER  1u
#define LT_CRASH_VER 1u
#define BLOB_TAG_CTR 1u
#define BLOB_TAG_RING 2u
#define BLOB_TAG_CRASH 3u
_Static_assert(sizeof(lt_counters_t) == 36, "counters blob payload (§15.2)");
_Static_assert(sizeof(err_ring_t) == 385, "error ring blob payload (§15.2)");
_Static_assert(sizeof(crash_entry_t) * CRASH_LOG_LEN == 15, "crash log blob payload (§15.2)");
#define BLOB_SCRATCH_MAX (sizeof(err_ring_t) + BLOB_OVERHEAD)   /* largest framed blob: 388 B */

/* Load key into dst (expect bytes) through the framer. 0 ok; -1 absent/size; -2 CRC; -3 version. */
static int load_framed(nvs_handle_t h, const char *key, uint8_t ver, void *dst, size_t expect)
{
    LT_ASSERT_RET(key != NULL && dst != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(expect > 0 && expect + BLOB_OVERHEAD <= BLOB_SCRATCH_MAX, NVS_ASSERT_CODE, -1);
    static uint8_t scratch[BLOB_SCRATCH_MAX];           /* boot-time only; lt_nvs_init runs before the tasks */
    size_t sz = 0;
    if (nvs_get_blob(h, key, NULL, &sz) != ESP_OK || sz != expect + BLOB_OVERHEAD) return -1;
    if (nvs_get_blob(h, key, scratch, &sz) != ESP_OK) return -1;
    return blob_unwrap(ver, scratch, sz, dst, expect, NULL);
}

/* Save src (n bytes) under key through the framer. 0 ok. */
static int save_framed(nvs_handle_t h, const char *key, uint8_t ver, const void *src, size_t n)
{
    LT_ASSERT_RET(key != NULL && src != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(n > 0 && n + BLOB_OVERHEAD <= BLOB_SCRATCH_MAX, NVS_ASSERT_CODE, -1);
    uint8_t buf[BLOB_SCRATCH_MAX];                       /* 388 B on the caller's stack: sup/logger/cmd/boot only */
    size_t len = blob_wrap(ver, src, n, buf, sizeof buf);
    if (len == 0 || nvs_set_blob(h, key, buf, len) != ESP_OK) return -1;
    (void)nvs_commit(h);
    return 0;
}

/* Boot: a blob that fails size/CRC/version is reset to zero once and reported (dev-only firmware,
 * no migration for these three). Returns the load rc for the log line. */
static int load_or_reset(nvs_handle_t h, const char *key, uint8_t ver, void *dst, size_t n, uint32_t tag)
{
    LT_ASSERT_RET(dst != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(tag >= BLOB_TAG_CTR && tag <= BLOB_TAG_CRASH, NVS_ASSERT_CODE, -1);
    int rc = load_framed(h, key, ver, dst, n);
    if (rc != 0) {
        memset(dst, 0, n);
        ESP_LOGW(TAG, "nvs blob %s reset (rc %d)", key, rc);
        s_blob_reset_mask |= (uint8_t)(1u << tag);     /* errlog after the ring itself is loaded */
    }
    return rc;
}
```

`lt_nvs_init` replaces the three `load_blob` lines with `load_or_reset(s_h_err, K_CTR, LT_CTR_VER, &s_counters, sizeof s_counters, BLOB_TAG_CTR)`, the ring (`BLOB_TAG_RING`, keep the H2 head clamp after it) and the crash log (`BLOB_TAG_CRASH`), then after `s_ready = true` walks `s_blob_reset_mask` (bounded 3) calling `errlog_add(E_NVS_BLOB_RESET, tag)` per set bit (the ring is loaded by then; errlog_add persists through `errlog_persist`). Remove `load_blob` (no longer used). `persist_counters` → `save_framed(s_h_err, K_CTR, LT_CTR_VER, &s_counters, sizeof s_counters)`; `errlog_persist` and `lt_errlog_clear` → `save_framed(s_h_err, K_RING, LT_RING_VER, &s_ring, sizeof s_ring)`; `lt_crashlog_push` → `save_framed(s_h_sys, K_CRASH, LT_CRASH_VER, s_crash, sizeof s_crash)`.

`lt_cfg_load`:

```c
int lt_cfg_load(cfg_t *c)
{
    LT_ASSERT_RET(c != NULL, NVS_ASSERT_CODE, -1);
    uint8_t buf[sizeof(cfg_t) + 2];                    /* [version][cfg_t minus its version][crc16]: today's bytes */
    size_t sz = 0;
    if (nvs_get_blob(s_h_cfg, K_CFG, NULL, &sz) != ESP_OK || sz != sizeof(buf)) return -1;
    if (nvs_get_blob(s_h_cfg, K_CFG, buf, &sz) != ESP_OK) return -1;
    uint8_t stored = 0;
    int rc = blob_unwrap(CFG_VERSION, buf, sz, (uint8_t *)c + 1, sizeof(cfg_t) - 1, &stored);
    if (rc == -3) {                                    /* older/unknown version: migrate or defaults */
        if (blob_unwrap(stored, buf, sz, (uint8_t *)c + 1, sizeof(cfg_t) - 1, NULL) != 0) return -1;
        if (cfg_migrate(c, stored) != 0) { (void)errlog_add(E_SYS_CFG_RESET, stored); return -1; }
    } else if (rc != 0) {
        return -1;
    }
    c->version = CFG_VERSION;
    LT_ASSERT_RET(c->version == CFG_VERSION, NVS_ASSERT_CODE, -1);   /* postcondition after migrate */
    return cfg_validate(c);
}
int lt_cfg_save(const cfg_t *c)
{
    LT_ASSERT_RET(c != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(c->version == CFG_VERSION, NVS_ASSERT_CODE, -1);
    uint8_t buf[sizeof(cfg_t) + 2];
    size_t len = blob_wrap(c->version, (const uint8_t *)c + 1, sizeof(cfg_t) - 1, buf, sizeof buf);
    if (len != sizeof buf || nvs_set_blob(s_h_cfg, K_CFG, buf, len) != ESP_OK) return -1;
    (void)nvs_commit(s_h_cfg);
    return 0;
}
```

(`E_SYS_CFG_RESET 0x0508` already exists — reuse it for "unknown cfg version → defaults".) Add `E_NVS_BLOB_RESET = 0x050B` to `lt_err.h` after `E_SYS_STACK_LOW` with a comment "arg = blob tag 1 ctr / 2 ring / 3 crash". A migration that lands on a same-size older layout is the only kind `cfg_migrate` supports today; a size change still returns −1 at the `sz != sizeof(buf)` check (defaults), which is the documented behaviour.

- [ ] **Step 3: Verify** — full `ctest` green (`test_cfg` incl. the new case); lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings. The pipeline/sup/cmd stacks gain a 388 B transient in `save_framed` — note the `moto_sim` stack headroom for the logger and cmd tasks is unchanged in principle (they already hold larger frames); report `.bss` (+1 B mask, +388 B static scratch).

- [ ] **Step 4: Commit**

```
feat(nvs): counters/error-ring/crash-log blobs versioned + CRC16 via blob_wrap; cfg load migrates known older versions (debt sweep A T2, #37)
```

---

### Task 3: Logger request/reply + session close on planned restart (#59)

**Files:**
- Modify: `components/app/include/app/lt_ipc.h` (`log_request_t` 32 B, `LOGGER_DELETE_SESSION`, `logger_request_sync` prototype), `components/app/sys/lt_ipc.c` (`logger_request_sync`), `components/app/logger/logger.c` (notify the requester after `handle_request`), `components/core/include/core/ses.h` (`SES_END_*`), `components/app/supervisor/sup.c` (`ota_reboot_check`, `check_stalls`), `components/app/include/app/lt_err.h` (`E_LOG_CLOSE_TIMEOUT = 0x050C`)

**Interfaces (produced):**

```c
/* core/ses.h — END.reason values (§12.3 gets this table) */
enum { SES_END_NORMAL = 0, SES_END_RESTART = 1, SES_END_STALL = 2 };

/* app/lt_ipc.h */
typedef struct {
    uint8_t      type, mode, reason, _pad;
    uint16_t     venue_id, layout_id;
    int64_t      gps_us;
    TaskHandle_t requester;   /* NULL = fire-and-forget; else xTaskNotify(requester, rc) after handling */
    char         id[10];      /* DELETE_SESSION: NUL-terminated id, validated by the sender */
    uint8_t      _pad2[2];
} log_request_t;
_Static_assert(sizeof(log_request_t) == 32, "log_request_t must be 32 B (§4.4, debt sweep A)");
enum { /* append */ LOGGER_DELETE_SESSION = 5 };
/* Post req (requester = the calling task), wake the logger, wait up to timeout_ms for its rc.
 * Returns the logger's rc (0 ok, <0 its error), -1 when the queue is full, -2 on timeout.
 * Never call from the ui or pipeline task. */
int logger_request_sync(const log_request_t *req, uint32_t timeout_ms);
```

- [ ] **Step 1: Types + helper.** `lt_ipc.h`: add `#include "freertos/task.h"`, the struct/enum above (keep field order so existing designated initialisers compile). `lt_ipc.c`:

```c
#include "freertos/task.h"
#include "app/logger.h"     /* logger_notify */
int logger_request_sync(const log_request_t *req, uint32_t timeout_ms)
{
    LT_ASSERT_RET(req != NULL, IPC_ASSERT_CODE, -1);
    LT_ASSERT_RET(g_log_req_q != NULL, IPC_ASSERT_CODE, -1);
    log_request_t r = *req;
    r.requester = xTaskGetCurrentTaskHandle();
    (void)xTaskNotifyStateClear(NULL);                       /* drop a stale notification from an earlier timeout */
    if (xQueueSend(g_log_req_q, &r, 0) != pdTRUE) return -1;
    logger_notify();
    uint32_t val = 0;
    if (xTaskNotifyWait(0, UINT32_MAX, &val, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return -2;
    return (int)(int32_t)val;
}
```

`logger.c`: `handle_request()` returns `int` (each case's rc: `close_session` → 0, or 0 when nothing was open; others 0), and the request drain becomes:

```c
    while (xQueueReceive(g_log_req_q, &req, 0) == pdTRUE) {
        LT_ASSERT_VOID(n++ < LOG_REQ_Q_DEPTH, LOG_ASSERT_CODE);
        int rc = handle_request(&req);
        if (req.requester != NULL) (void)xTaskNotify(req.requester, (uint32_t)(int32_t)rc, eSetValueWithOverwrite);
    }
```

The logger task must never wait on anything the requester holds (it does not). `close_session` returns `int` (0; keep the `if (!s_open) return 0;`). Existing senders (`pipeline.c` OPEN, `export_serial.c` `dbg logtest`, `cmd.c` RECOUNT — RECOUNT is removed in Task 4) leave `requester` zero via designated initialisers; verify each compiles unchanged.

- [ ] **Step 2: Callers in `sup.c`.**

```c
/* Close the open session before a supervisor-owned restart (§17.2/§19.4, #59). Bounded: a
 * logger that cannot answer in time must not block the restart. */
static void close_session_before_restart(uint8_t reason, uint32_t timeout_ms)
{
    LT_ASSERT_VOID(reason == SES_END_RESTART || reason == SES_END_STALL, SUP_ASSERT_CODE);
    LT_ASSERT_VOID(timeout_ms > 0 && timeout_ms <= 5000, SUP_ASSERT_CODE);
    log_request_t req = { .type = LOGGER_CLOSE_SESSION, .reason = reason, .gps_us = 0 };
    int rc = logger_request_sync(&req, timeout_ms);
    if (rc != 0) {
        ESP_LOGW(TAG, "session close before restart rc %d (reason %u)", rc, (unsigned)reason);
        (void)errlog_add(E_LOG_CLOSE_TIMEOUT, (uint32_t)reason);
    }
}
```

`ota_reboot_check()`: `close_session_before_restart(SES_END_RESTART, 2000);` before `lt_counters_flush(true)`. `check_stalls()` pipeline branch: `close_session_before_restart(SES_END_STALL, 500);` before `lt_counters_flush(true)`. Includes: `core/ses.h`, `app/lt_ipc.h`. `E_LOG_CLOSE_TIMEOUT = 0x050C` in `lt_err.h`.

- [ ] **Step 3: Verify** — `ctest` green (no host change beyond compiling `ses.h`); lint 0 (the drain loop stays ≤ 20 lines or gains asserts); gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings; `.bss` delta = +64 B (queue store 4 × 16 B). Report the supervisor task's stack headroom is unaffected (the request is 32 B on stack).

- [ ] **Step 4: Commit**

```
feat(logger): bounded request/reply via task notification; supervisor closes the session before OTA and stall restarts (debt sweep A T3, #59)
```

---

### Task 4: Delete on the logger task (#73)

**Files:**
- Modify: `components/app/logger/logger.c` (`delete_session` handler), `components/app/cmd/cmd.c` (`op_delete`), `components/lt_hal/include/hal/storage.h` (contract comment)

**Interfaces (consumed):** `LOGGER_DELETE_SESSION`, `logger_request_sync` (Task 3); `logger_open_session_id()` (exists).

- [ ] **Step 1: Logger handler** (`logger.c`):

```c
/* DELETE_SESSION (#73): the only unlink path -- runs on this task, so the eviction listing's
 * iterator is never crossed by a foreign mutation. 0 ok; -3 neither file existed; else the
 * unlink's error. Refuses the open session (its fd would be orphaned). */
static int delete_session(const log_request_t *req)
{
    LT_ASSERT_RET(req != NULL, LOG_ASSERT_CODE, -1);
    LT_ASSERT_RET(req->id[sizeof req->id - 1] == '\0' || strnlen(req->id, sizeof req->id) < sizeof req->id, LOG_ASSERT_CODE, -1);
    if (s_open && strncmp(s_id, req->id, sizeof req->id) == 0) return -4;   /* session is open */
    char path[48];
    int rc_log, rc_sum;
    (void)snprintf(path, sizeof path, "/sessions/%.10s.log", req->id);
    rc_log = sto_unlink(path);
    (void)snprintf(path, sizeof path, "/sessions/%.10s.sum", req->id);
    rc_sum = sto_unlink(path);
    if (rc_log != 0 && rc_sum != 0) return -3;
    status_cache_prime();                            /* recount + fresh free_kb, on this task */
    return 0;
}
```

`handle_request`: `case LOGGER_DELETE_SESSION: return delete_session(req);` and drop the `LOGGER_RECOUNT` comment block's cmd.c reference (RECOUNT itself stays for other users; if none remain, delete the enum value and note it in the report).

- [ ] **Step 2: `op_delete`** (`cmd.c`) becomes:

```c
static int op_delete(const uint8_t *payload, size_t len,
                     cmd_emit_fn emit, void *ctx, uint8_t tag, uint16_t *seq)
{
    LT_ASSERT_RET(emit != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(seq != NULL, CMD_ASSERT_CODE, -1);
    LT_ASSERT_RET(payload != NULL || len == 0, CMD_ASSERT_CODE, -1);
    char id[11];
    size_t idl = (len < 10) ? len : 10;
    memcpy(id, payload, idl);
    id[idl] = '\0';
    id[strcspn(id, " ")] = '\0';
    /* Task 5 inserts the lt_session_id_ok() check here. */
    log_request_t req = { .type = LOGGER_DELETE_SESSION };
    (void)snprintf(req.id, sizeof req.id, "%s", id);
    int rc = logger_request_sync(&req, 1000);
    if (rc == -4) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "session is open; close it first");
    if (rc == -3) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "no such session");
    if (rc == -2) return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "delete timed out");
    if (rc != 0)  return emit_error(emit, ctx, tag, seq, E_CONN_PROTO, "delete failed");
    return emit_bytes(emit, ctx, tag, seq, NULL, 0, true);
}
```

Remove the now-unused `logger_open_session_id` include/usage only if nothing else in cmd.c uses it (check). `hal/storage.h` header comment: replace "each is called from one task only (logger, plus cmd for read-only listing/export in 3.5)" with "mutations (sto_unlink, writes, format) are called from the logger task only; cmd may list and read (debt sweep A, #73)". Note the reply contract change in `lt_proto.h`'s DELETE comment if one exists (search `DELETE`), else in cmd.c's op comment: new error texts `no such session` / `delete timed out`.

- [ ] **Step 3: Verify** — lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings; `grep -rn "sto_unlink" components main` shows only `logger.c` (and the storage driver).

- [ ] **Step 4: Commit**

```
fix(cmd/logger): session delete runs on the logger task with a bounded reply; storage mutations single-owner (debt sweep A T4, #73)
```

---

### Task 5: Session-id validation (#60) — lap-timer + dev-kit relay

**Files:**
- Modify: `components/app/include/app/lt_proto.h`, `test/test_proto.c`, `components/app/cmd/cmd.c` (`op_open`, `op_delete`), `devcontroller/components/webapi/webapi.c` (`do_session_download`)
- Create: `devcontroller/test/test_session_id.c` (the dev-kit harness globs `test_*.c`; confirm in `devcontroller/test/CMakeLists.txt`)

**Interfaces (produced):**

```c
/* app/lt_proto.h — IDF-free; both firmwares and both host harnesses include it */
#define LT_SESSION_ID_MAX 10
static inline bool lt_session_id_ok(const char *id, size_t len)
{
    if (id == NULL || len == 0u || len > LT_SESSION_ID_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        if (!ok) return false;
    }
    return true;
}
```

(`#include <stdbool.h>` / `<stddef.h>` at the top of `lt_proto.h`; static inline, ≤ 20 lines, no assert needed by the lint rule.)

- [ ] **Step 1: Failing tests** — `test/test_proto.c`:

```c
void test_session_id_accepts_real_ids(void) {
    TEST_ASSERT_TRUE(lt_session_id_ok("S00001_001", 10));
    TEST_ASSERT_TRUE(lt_session_id_ok("abc", 3));
    TEST_ASSERT_TRUE(lt_session_id_ok("A_1", 3));
}
void test_session_id_rejects_traversal_and_shape(void) {
    TEST_ASSERT_FALSE(lt_session_id_ok("", 0));
    TEST_ASSERT_FALSE(lt_session_id_ok("S00001_0011", 11));
    TEST_ASSERT_FALSE(lt_session_id_ok("..", 2));
    TEST_ASSERT_FALSE(lt_session_id_ok("a/b", 3));
    TEST_ASSERT_FALSE(lt_session_id_ok("a.b", 3));
    TEST_ASSERT_FALSE(lt_session_id_ok("S00001_00 ", 10));
    TEST_ASSERT_FALSE(lt_session_id_ok("-", 1));
    TEST_ASSERT_FALSE(lt_session_id_ok(NULL, 3));
}
```

Register both in `main`. `devcontroller/test/test_session_id.c`: the same two tests against `app/lt_proto.h` (Unity shape copied from `devcontroller/test/test_status_json.c`).

- [ ] **Step 2: Run to fail** (both harnesses: `cmake -S test -B test/build ... && ./test/build/test_proto`; `cmake -S devcontroller/test -B devcontroller/test/build && cmake --build devcontroller/test/build && ctest --test-dir devcontroller/test/build`).

- [ ] **Step 3: Implement.** `lt_proto.h` as above. `cmd.c` `op_open`: after the trim, `if (!lt_session_id_ok(id, strlen(id))) return emit_error(emit, ctx, tag, &seq, E_CONN_PROTO, "bad id");` before touching `s_open`/`stream_by_fmt`. `op_delete`: same line at the marked spot, using `seq`. `webapi.c` `do_session_download`: after `path_tail(...) <= 0` handling, `if (!lt_session_id_ok(id, strlen(id))) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad session id"); return; }` (`webapi.c` already reaches `app/lt_proto.h` through the linkhost component's INCLUDE_DIRS; add the include).

- [ ] **Step 4: Run to pass** — both harnesses green.

- [ ] **Step 5: Gate + commit** — lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m`; dev-kit firmware clean build 0 warnings (env-notes gives the command).

```
fix(proto): session ids validated (alnum/_ ≤ 10) before any path is built, on the lap-timer and the dev-kit relay (debt sweep A T5, #60)
```

---

### Task 6: Recovery mode (#62) — spec §17.5 amendment

**Files:**
- Modify: `components/app/sys/lt_nvs.c` + `include/app/lt_nvs.h` (`lt_safe_level_get/set`, `K_SAFELVL "safe_lvl"`), `components/app/include/app/lt_sup.h` (`SYS_RECOVERY_MODE`), `components/app/include/app/lt_err.h` (`E_SYS_RECOVERY_MODE = 0x050A`), `main/app_main.c` (`boot_safe_mode`, `boot_subsystems`, `app_main`), `components/app/supervisor/sup.c` (clear block), `components/drivers/export_serial/export_serial.c` (`dbg safe clear`, status annotation), `devcontroller/components/linkhost/host/linkhost_proto.c` (flag name table, if it names bits — check `test_status_json.c`), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §17.5

- [ ] **Step 1: NVS level.**

```c
/* lt_nvs.h */
uint8_t lt_safe_level_get(void);        /* 0 normal, 1 safe, 2 recovery (§17.5) */
int     lt_safe_level_set(uint8_t lvl); /* persisted */
/* lt_nvs.c */
#define K_SAFELVL "safe_lvl"
uint8_t lt_safe_level_get(void) { uint8_t v = 0; (void)nvs_get_u8(s_h_sys, K_SAFELVL, &v); return v > 2u ? 0u : v; }
int lt_safe_level_set(uint8_t lvl)
{
    LT_ASSERT_RET(lvl <= 2u, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(s_ready, NVS_ASSERT_CODE, -1);
    if (nvs_set_u8(s_h_sys, K_SAFELVL, lvl) != ESP_OK) return -1;
    (void)nvs_commit(s_h_sys);
    return 0;
}
```

`lt_safe_clear()` also sets level 0.

- [ ] **Step 2: Detection + reduced boot** (`app_main.c`):

```c
static uint32_t boot_safe_mode(uint8_t *level_out)
{
    uint32_t boot_cnt = lt_nvs_boot_inc();
    lt_counters_inc(LT_CTR_BOOTS, false);
    uint8_t level = 0;
    bool in_window = boot_cnt <= lt_safe_until_get();
    if (lt_crashlog_is_loop()) {
        level = in_window ? 2u : 1u;                 /* a loop inside a safe-mode window escalates (§17.5) */
        lt_safe_until_set(boot_cnt + 1);
        (void)lt_safe_level_set(level);
        ESP_LOGE(TAG, "crash loop: 3 abnormal resets < 60 s -> %s", level == 2u ? "RECOVERY MODE" : "SAFE MODE");
    } else if (in_window) {
        level = lt_safe_level_get();                 /* still inside a prior window: same level */
        if (level == 0u) level = 1u;
    }
    if (level >= 1u) { sys_flags_set(SYS_SAFE_MODE); errlog_add(E_SYS_SAFE_MODE, boot_cnt); }
    if (level == 2u) { sys_flags_set(SYS_RECOVERY_MODE); errlog_add(E_SYS_RECOVERY_MODE, boot_cnt); }
    CORE_ASSERT_RET(level <= 2u, MAIN_ASSERT_CODE, boot_cnt);
    *level_out = level;
    return boot_cnt;
}

static void boot_subsystems(uint8_t level)
{
    sup_start();
    lt_queues_init();
    lt_ipc_init();
    link_start();
    logger_start();
    if (level == 2u) {                               /* recovery (§17.5): service tasks only */
        ESP_LOGE(TAG, "RECOVERY MODE: pipeline, GPS power and ui not started");
        return;
    }
    pipeline_start();
    ui_start();
}
```

`app_main`: `uint8_t level = 0; uint32_t boot_cnt = boot_safe_mode(&level);` … `if (level != 2u) board_gps_power(true);` … `boot_subsystems(level);` … the final log prints `safe_mode=%u` with `level`. `SYS_RECOVERY_MODE` appended to the `sys_flags` enum (bit 14) with the §17.4 table in the spec extended.

- [ ] **Step 3: Supervisor clear + console.** `sup.c` clear block: condition `(sys_flags_get() & ((1u << SYS_SAFE_MODE) | (1u << SYS_RECOVERY_MODE)))`; body also `sys_flags_clear(SYS_RECOVERY_MODE)` (and `lt_safe_clear()` now zeroes the level). `export_serial.c`: `dbg status` prints ` [SAFE_MODE]`/` [RECOVERY]` from the two bits; new subcommand `safe` with `argv[2] == "clear"`: `lt_safe_clear(); sys_flags_clear(SYS_SAFE_MODE); sys_flags_clear(SYS_RECOVERY_MODE); printf("dbg: safe/recovery gate cleared (next boot normal)\n");` — usage line `usage: dbg safe clear`. Dev-kit: if `linkhost_proto.c` names sys_flag bits in the STATUS JSON, add `"recovery"` for bit 14 and extend `test_status_json.c`'s expectation; if it prints the raw mask, no change (say which in the report).

- [ ] **Step 4: Spec §17.5.** Append to the design spec's §17.5 list: "- A crash loop detected while `boot_cnt <= safe_until` (the previous boot was already safe mode) escalates to **recovery mode** (`safe_lvl` = 2, `SYS_RECOVERY_MODE`): supervisor, link, logger and the console/OTA export start; pipeline, GPS power and ui do not. The same `SAFE_MODE_CLEAR_S` uptime rule clears both levels; `dbg safe clear` clears them immediately." and add bit 14 `SYS_RECOVERY_MODE` (icon —) to the §17.4 table.

- [ ] **Step 5: Verify** — lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m` + ws29v2 0 warnings; dev-kit build if touched; `check_stalls` needs no change (unregistered tasks are skipped — cite the `w->used` guard in the report).

- [ ] **Step 6: Commit**

```
feat(boot): recovery mode — a crash loop inside a safe-mode window starts service tasks only (debt sweep A T6, #62, §17.5)
```

---

### Task 7: Docs, register, closing comments

**Files:**
- Modify: `docs/power-of-10-deviations.md` (PD-7 row), `docs/superpowers/plans/2026-09-14-roadmap.md` (entry after Plan 7c), `docs/superpowers/specs/2026-09-30-debt-sweep-a-design.md` (§9 "Implementation notes": rulings, measured `.bss`, the lint run for #63 pasted, #64/#66 verification pointers), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §12.3 END row (reason table)

- [ ] **Step 1:** PD-7: file/symbol `components/app/cmd/cmd.c:cw_t.emit` (was attributed to export_serial), rationale "stored in the static `s_cw` for the duration of one synchronous streaming op on the calling task; set by `cw_init`, never crosses a task; the typedef (`cmd.h:cmd_emit_fn`) is the lint's registered site". Record `python3 tools/lint/power_of_10.py --paths components/app/supervisor/sup.c components/app/sys/lt_nvs.c --json` output (0 findings) in the spec's §9. Roadmap entry in the Plan 7c entry's style with the bench line "pending". §12.3 END row: `reason u8: 0 NORMAL, 1 RESTART (planned/OTA), 2 STALL`.
- [ ] **Step 2:** `git diff --check`; commit `docs(debt-sweep-a): PD-7 amended, roadmap entry, END reason table, implementation notes`.
- [ ] **Step 3 (controller):** bench `dsA-d1` per spec §7 with the user (same day as `p07c-d1`); then close #37 #59 #60 #62 #73 via the PR, close #64/#66 with comments naming 884a108 + the bench spot check, close #63 with the register commit; PR against `p7c-display-closure`.

---

## Self-review

- **Spec coverage:** §2 → T3 (reply, END reasons, #59) + T4 (#73); §3 → T1 + T2; §4 → T5; §5 → T6; §6 → T7 (#63 register; #64/#66 closing) + bench; §7 → each task's tests + T7 Step 3; §8 out of scope untouched.
- **Placeholders:** none — every code step carries code; "check"/"confirm" items name the exact file and the fallback.
- **Type consistency:** `blob_wrap(uint8_t, const void*, size_t, uint8_t*, size_t) → size_t` and `blob_unwrap(uint8_t, const uint8_t*, size_t, void*, size_t, uint8_t*) → int` used identically in T1/T2; `log_request_t` fields `requester`/`id` (T3) used in T4; `logger_request_sync(const log_request_t*, uint32_t) → int` rc conventions (0 / −1 full / −2 timeout / −3 not found / −4 open) used in T3/T4; `lt_session_id_ok(const char*, size_t)` (T5) used in cmd.c and webapi.c; `lt_safe_level_get/set` (T6) used in app_main/sup/export_serial; error codes `E_NVS_BLOB_RESET 0x050B` (T2), `E_LOG_CLOSE_TIMEOUT 0x050C` (T3), `E_SYS_RECOVERY_MODE 0x050A` (T6) do not collide with `0x0508`/`0x0509`.
- **Spec correction folded in:** the spec's §5 named `E_SYS_RECOVERY_MODE 0x0508`; `0x0508` is already `E_SYS_CFG_RESET`, so the code is `0x050A` (T7 amends the spec line).
