# Debt sweep A — lap-timer robustness (design)

**Date:** 2026-09-30. **Branch:** `debt-sweep-A`, stacked on `p7c-display-closure` (e37f66f); its PR targets the 7c branch and retargets to `main` when PR #88 merges. **Bench:** verified on the same bench day as `p07c-d1` (gate `dsA-d1`).

Closes #37, #59, #60, #62, #73 by code; #64, #66 by verification (already fixed in 884a108); #63 by a register amendment. Fact sheet behind every statement here: the sweep ledger's `debt-sweep-A-facts.md`.

## 1. Scope and non-goals

In: the six items above plus the small spec amendment §17.5 needs for #62. Out: #53 task stacks (bench-only measurement), #65/#67/#68 dev-controller UX (group B), #11 (M10 hardware), #54/#57 (Plan 6), #61 (sub-project C), #82 (Plan 8), #86/#87 (7c follow-ups). No new subsystems; every change lands inside an existing file's responsibility.

## 2. Logger request/reply (#59, #73)

Today `g_log_req_q` (depth 4, `log_request_t` 16 B, `lt_ipc.h`) is fire-and-forget and nothing sends `LOGGER_CLOSE_SESSION`. Two callers now need a bounded answer.

**Request struct.** `log_request_t` grows to 32 B (`_Static_assert` updated; the queue storage is static and sized from `sizeof`):

```c
typedef struct {
    uint8_t      type, mode, reason, _pad;
    uint16_t     venue_id, layout_id;
    int64_t      gps_us;
    TaskHandle_t requester;   /* NULL = fire-and-forget; else notified with the rc when handled */
    char         id[10];      /* DELETE_SESSION: session id, NUL-terminated (validated by the sender) */
    uint8_t      _pad2[2];
} log_request_t;
```

New type `LOGGER_DELETE_SESSION = 5`.

**Reply.** After `handle_request()` finishes a request whose `requester` is non-NULL, the logger calls `xTaskNotify(req->requester, (uint32_t)(int32_t)rc, eSetValueWithOverwrite)`. The caller waits with `xTaskNotifyWait(0, UINT32_MAX, &val, pdMS_TO_TICKS(timeout))` and reads `rc = (int32_t)val`. Timeout → the caller's own failure path (below). One helper in `lt_ipc.c`, `int logger_request_sync(const log_request_t *req, uint32_t timeout_ms)` (posts with `requester = xTaskGetCurrentTaskHandle()`, waits, returns rc or `-ETIMEDOUT`-style −2 on timeout, −1 when the queue is full), used by both callers. The ui/pipeline tasks never call it (they have no reason to and must not block).

- **END reasons.** §12.3 defines `END.reason` as a bare `u8` with no value table; `core/ses.h` gains `enum { SES_END_NORMAL = 0, SES_END_RESTART = 1, SES_END_STALL = 2 }` (existing callers pass 0 = NORMAL) and §12.3's END row gets that table.
- **#59 planned restart.** `ota_reboot_check()` (sup.c): before `lt_counters_flush(true)` + `esp_restart()`, send `LOGGER_CLOSE_SESSION{ reason = SES_END_RESTART, gps_us = 0 }` synchronously, timeout 2000 ms; a timeout or error is logged (`ESP_LOGW`) and counted (`errlog_add(E_LOG_CLOSE_TIMEOUT)`), the restart proceeds regardless. `check_stalls()`'s stall restart does the same with `reason = SES_END_STALL` and timeout 500 ms (a stalled pipeline must not delay recovery; the logger task is independent of the pipeline). `close_session()` when no session is open returns 0 (nothing to do) — the caller does not care.
- **#73 delete on the logger task.** `op_delete` (cmd.c) validates the id (§4), then `logger_request_sync(LOGGER_DELETE_SESSION{id}, 1000)`. The logger's handler unlinks `.log` then `.sum` via `sto_unlink`, refreshes the sessions/free-kb cache in place (what `LOGGER_RECOUNT` did), and returns 0, `-ENOENT`-style −3 when neither file existed, or the unlink's error. `op_delete` maps rc → the framed reply exactly as today's inline path did (ok / not-found / error), plus a new error for timeout. The `LOGGER_RECOUNT` post from `op_delete` goes away (subsumed). `hal/storage.h`'s contract comment becomes: "mutations (`sto_unlink`, `sto_open` for write, format) run on the logger task only; cmd may list/read." The logger never yields inside `evict_scan_oldest` while another task can mutate the directory, because no other task mutates it any more.

**Ordering guarantee.** Requests are handled in queue order on the logger task, so a DELETE queued behind an in-flight eviction listing runs after the listing completes.

## 3. NVS blob framing (#37)

New pure core module `components/core/sys/blob.c` + `core/blob.h`:

```c
#define BLOB_OVERHEAD 3                                   /* version byte + CRC16 */
size_t blob_wrap(uint8_t ver, const void *payload, size_t n, uint8_t *out, size_t cap);  /* 0 on cap/size error */
int    blob_unwrap(uint8_t expect_ver, const uint8_t *in, size_t n, void *payload, size_t payload_len, uint8_t *ver_out);
/* 0 ok; -1 size mismatch; -2 CRC mismatch; -3 version mismatch (ver_out filled, payload untouched) */
```

CRC = `ses_crc16` over `version + payload` (the cfg blob's existing shape, so the cfg blob's on-flash bytes are unchanged). Bounded, no heap, ≥ 2 asserts each.

`lt_nvs.c` stores all four blobs through it:

| blob | key | version | on −1/−2 | on −3 |
|---|---|---|---|---|
| counters (`lt_counters_t`, 36 B) | `ctr` | `LT_CTR_VER 1` | zero + `errlog_add(E_NVS_BLOB_RESET, key_tag)` | same (no migration; dev-only firmware) |
| error ring (385 B) | `ring` | `LT_RING_VER 1` | zero + errlog | same |
| crash log (15 B) | `crash_log` | `LT_CRASH_VER 1` | zero + errlog | same |
| cfg (`cfg_t`) | `cfg` | `CFG_VERSION` | defaults (as today) | `cfg_migrate(c, ver_out)` then `cfg_validate`; unknown → defaults |

`E_NVS_BLOB_RESET` is a new `lt_err.h` code with `arg` = a per-blob tag (1 ctr, 2 ring, 3 crash). First boot after this change resets the three small blobs once (layout change: a leading version byte + CRC); the boot log says so. `lt_boot_record_reset`/`lt_crashlog_push` write through `blob_wrap`.

## 4. Session-id validation (#60)

`app/lt_proto.h` (IDF-free, shared with the dev-kit) gains:

```c
#define LT_SESSION_ID_MAX 10
static inline bool lt_session_id_ok(const char *id, size_t len);   /* 1..10 chars of [A-Za-z0-9_] only */
```

Used before any path is built: `op_open` and `op_delete` reject with the framed error `ERR bad id` (existing error-reply mechanism, new text); the dev-kit's `path_tail()` result is checked in `do_session_download` → HTTP 400. Legitimate ids are `S%05u_%03u` (10 chars, alnum + `_`), so nothing real is rejected. Tests: `test/test_proto.c` (accept `S00001_001`, `abc`, `A_1`; reject empty, 11 chars, `..`, `a/b`, `a.b`, `S00001_001 ` with a space, `-`), and `devcontroller/test/test_status_json.c`'s sibling gets one case that the relay rejects `..`.

## 5. Recovery mode (#62) — spec §17.5 amendment

Safe mode (level 1) stays exactly as §17.5 describes: pipeline, engines and drivers run; sample logging, BLE and WiFi off; one SAFE MODE render. A deterministic driver crash therefore still loops. New **recovery mode (level 2)**:

- **Detection** (`boot_safe_mode`, app_main.c): a crash loop (`lt_crashlog_is_loop()`) detected while `boot_cnt <= lt_safe_until_get()` — the previous boot was already safe mode — sets level 2; a plain loop sets level 1 as today. Level persists in NVS (`K_SAFELVL`, u8, `lt_safe_level_get/set`), next to `safe_until`, and `safe_until` is extended by one boot as today. `SYS_RECOVERY_MODE` becomes a new `sys_flags` bit (after `SYS_FUSION_DISAGREE`), and `E_SYS_RECOVERY_MODE 0x0508` is logged.
- **Boot in recovery:** `app_main` runs `boot_rtc_validate`, `boot_config`, `board_init`, `boot_storage`, then a reduced `boot_subsystems`: `sup_start`, `lt_queues_init`, `lt_ipc_init`, `link_start`, `logger_start`, `export_serial_start`. Skipped: `board_gps_power(true)`, `pipeline_start`, `ui_start` (the panel keeps whatever it last showed). The boot log line prints `safe_mode=2`.
- **Visibility:** STATUS carries the flag (existing sys_flags field); `dbg status` prints `RECOVERY`; the dev-kit shows it wherever it shows SAFE today (one string).
- **Clearing:** the supervisor's existing `SAFE_MODE_CLEAR_S` uptime rule clears level 0 and `safe_until` for both levels (one code path). Manual: `dbg safe clear` (new dbg subcommand) does the same immediately for bench use.
- **Supervisor in recovery:** `check_stalls` must not restart for missing pipeline/ui heartbeats when those tasks were never started — the heartbeat check skips unregistered tasks (it already keys off `sup_register_task`; the plan verifies).

§17.5 text amendment (design spec): add the level-2 paragraph above and the sentence "A crash loop inside a safe-mode window escalates to recovery mode".

## 6. Verify-and-close (#64, #66) and the register (#63)

- **#64** is fixed by `frame_tx_begin/end` (export_serial.c, LF window) and **#66** by `LT_FUSED_OFF_*`/`LT_EVENT_OFF_*` + `_Static_assert`s in link.c + the dev-kit decoder + `test_stream_json.c` (all in 884a108). Bench spot check on the gate day (§7), then close both with a comment naming the commit and the tests.
- **#63**: the rule-5 claim no longer reproduces (`tools/lint/power_of_10.py --paths sup.c lt_nvs.c` → 0 findings; recorded). The `cw_t` deviation lives in `cmd.c`, not `export_serial.c`; PD-7's rationale ("never stored") is wrong: `cw_init()` stores `cmd_emit_fn` in the static `s_cw` for one streaming op. Amend PD-7 (file/symbol `cmd.c:cw_t.emit`, rationale "stored for the duration of one synchronous streaming op on the calling task; never crosses a task; set and cleared by `cw_init`/`cw_finish`") and close.

## 7. Tests and gates

**Host:** `test_blob.c` (round trip; each failure code; cap too small; version mismatch leaves the payload untouched and reports the stored version), `test_proto.c` session-id cases, `test_cfg.c` gains one case: a `cfg_t` wrapped with version `CFG_VERSION - 1` unwraps with −3 and `ver_out == CFG_VERSION - 1`, and `cfg_migrate(c, CFG_VERSION - 1)` on it returns −1 (unknown) today — the exact decision `lt_cfg_load` now makes. Existing suites unchanged. Lint 0, gcc-16 sweep clean, three clean firmware builds 0 warnings, DRAM floor ≥ 4 KB on `moto_sim` (the request struct +64 B; recovery adds < 16 B).

**Bench `dsA-d1`** (same day as `p07c-d1`, moto_sim, dev-kit wired):
1. #59: open a session (sim boot opens one), OTA-push a build → after the reboot the previous session's `.sum` exists and its END reason is `SES_END_RESTART` (read via `list`/`open`).
2. #73: `delete <id>` while `dbg evict` (or the periodic eviction) is listing → reply `OK` within 1 s; a second `delete` of the same id → not-found; `list` sane; no `E (` lines.
3. #60: `delete ../x` and `open ../x raw` → `ERR bad id`; dev-kit `GET /api/session/../x` → 400.
4. #62: `dbg crash` ×3 within 60 s → SAFE MODE boot (level 1); `dbg crash` ×3 again → RECOVERY (no ui/pipeline tasks in `dbg status`, console alive, OTA push works); `dbg safe clear` → normal boot. The uptime clear is unchanged code and is not re-timed at the bench.
5. #37: after the first boot on the new image the log shows the three one-time blob resets; a second boot shows none; `errlog` lists three `E_NVS_BLOB_RESET`.
6. #64/#66: one `status` round-trip decodes at the dev-kit; the live stream JSON shows sane fused/event fields.

Tag `dsA-d1`; close the seven issues.

## 8. Out of scope / follow-ups

Migration of the three small blobs (reset is enough for dev-only flash); a cfg-change notification for the ui (#87); the power task that will send `LOGGER_CLOSE_SESSION` on shutdown (Plan 6); recovery-mode UI rendering (a "RECOVERY" screen needs the display driver, the very thing recovery avoids).
