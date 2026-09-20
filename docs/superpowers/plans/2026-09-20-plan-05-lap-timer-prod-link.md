# Plan 5 (sub-project A): Lap-timer Production Link — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the lap-timer its production connectivity surface — BLE RaceChrono live telemetry, a formalized `app/cmd` responder + fused-log/event stream over UART0, and signed OTA-receive with rollback — while keeping the prod image lean (dev/tooling UX moves to the dev controller in sub-project B).

**Architecture:** The lap-timer answers, it does not host an operator UX. `app/cmd` (already built) is the request/response core, reached over two transports: the existing serial console (kept for bootstrapping) and a new `conn_ble` NimBLE service. A fused-log/event stream is emitted to an attached peer. OTA is *receive-side only*: the peer (dev controller in B, or a serial/BLE client now) pushes a signed image via `OTA_*` ops; the supervisor validates-or-rolls-back on the next boot. Signed images only this sub-project (secure boot is sub-project C).

**Tech Stack:** ESP-IDF v5.3.2, C11, NimBLE (`CONFIG_BT_NIMBLE_ENABLED`), `esp_ota_ops`/`esp_app_format`, `espsecure.py` (ECDSA image signing, no secure boot). Power-of-10 rules are in force (`plan-4.5-done`) — all new code is gated by the blocking CI.

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §18 (connectivity), §19 (OTA); architecture `docs/superpowers/specs/2026-09-20-connectivity-dev-controller-design.md` (the two-device split; this is sub-project A).

> **⚠ 2026-09-20 UPDATE — BLE (Session 5.2) deferred to sub-project C.** `conn_ble` is written and fits the neo6m target (5836 B free after the DRAM reclaim), but its ~15 KB NimBLE host stack leaves no comfortable headroom while the prod image still carries ~20 KB of dev tooling. That tooling is offloaded to the dev controller in sub-project C, so **BLE activation + flash-validation move to C** (into the roomy post-offload firmware). The code is parked on `s5.2-conn-ble`. This plan's other sessions do **not** need BLE: the dev board links over the UART `cmd`/stream transport (which exists), 5.3 is validated over the serial peer, 5.4 (OTA) and 5.5 (prod-slim) are BLE-independent. Prerequisite reclaim: `docs/superpowers/plans/2026-09-20-plan-dram-reclaim.md` (+10.9 KB, landed). Where this plan says "BLE" as a live transport below, read it as "activates in C".

## Global Constraints

- **Power of 10 compliance (blocking):** every new `.c` in `components/**`/`main/**` passes `tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` and the `-Werror` set (incl. `-Wmissing-prototypes`/`-Wmissing-declarations`). Functions ≤60 code lines, compound bodies ≤30, ≥2 genuine assertions per >20-line function, no new unregistered function pointer (NimBLE GATT callbacks are an IDF-boundary function-pointer site → register in `docs/power-of-10-deviations.md` with the §2 burden of proof, same class as PD-1..6).
- **Prod/dev split:** the lap-timer only *answers* `cmd` + emits the stream + receives OTA. No web UI, no interactive tuning UX on the lap-timer. The serial console stays (flag-gated, `CFG_HAS_EXPORT_SERIAL`) through sub-projects A and B for bootstrapping; sub-project C removes open access.
- **BLE is live telemetry + `cmd` transport**, not a control-only channel: RaceChrono reads the session export via `cmd` OPEN/READ; the `status` characteristic and `data` notify carry live state.
- **Signed images only** (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, ECDSA V1). Secure boot / flash encryption are sub-project C.
- **Behavior-preserving where it touches existing code:** the current serial `cmd`/export path and the lap-timing pipeline stay green (host tests 31/31, replay fixtures byte-exact); each firmware session flashes to confirm (wait for the user's "ready").
- **App image ≤ 1.2 MB** (`build.sh size` hard-fails above it, §19.1) — BLE + OTA must stay within it (headroom analysis: BLE-only image ≈0.5 MB today, ample).
- **Every commit** ends with the two trailers (`Co-Authored-By: Claude Opus 4.8 …` / `Claude-Session: …`).
- Working directory: repo root; ESP-IDF via `source tools/idf-env.sh`; envs `moto_sim` (BLE + sim) and `moto_neo6m` (BLE + real GPS/IMU).

## Current state (what exists vs. what this plan builds)

- **Exists:** `components/app/cmd` (`cmd_dispatch` + ops STATUS/LIST/OPEN/READ/CLOSE/DELETE/CONFIG_GET/CONFIG_SET/ERRLOG_GET/ERRLOG_CLEAR/DIAG_GET; PD-7 `cmd_emit`), `components/drivers/export_serial` (serial console + REPL + `run_stream` framing + a `g_fix_ring` fused-sample stream), the supervisor, NVS, storage.
- **Builds new:** `conn_ble` (NimBLE service — none today), the `OTA_*` ops + `esp_ota` receive path + signing + supervisor rollback (none today), a formalized peer fused-log/event stream + hot-swap link/detect, and the prod-slim flag-gating.

## File structure

```
components/app/conn_ble/           NEW component: NimBLE RaceChrono service + cmd/data/status
  conn_ble.c                       gap/gatt setup, advertising, characteristics, cmd bridge, status notify
  include/app/conn_ble.h           conn_ble_start(), conn_ble_status_notify(), conn_ble_stream_push()
components/app/cmd/cmd.c           MODIFY: add OTA_* op handling (delegates to app/ota)
components/app/include/app/cmd.h   MODIFY: add CMD_OTA_BEGIN/DATA/END/ABORT op codes
components/app/ota/                NEW: OTA receive-side
  ota.c                            esp_ota_begin/write/end, hwid+SHA+precondition checks, set_boot
  include/app/ota.h                ota_begin()/ota_data()/ota_end()/ota_abort(), ota_pending state
components/app/supervisor/sup.c    MODIFY: pending-verify → mark_app_valid_cancel_rollback / rollback log
components/app/pipeline/pipeline.c MODIFY (small): expose the fused-log/event stream to attached peers
components/drivers/export_serial/  MODIFY: flag-gate the interactive console; keep the cmd+stream transport
keys/laptimer_pub.pem              NEW (committed); laptimer_priv.pem is gitignored (offline)
tools/sign_release.sh              NEW: build + sign + verify + emit dist/laptimer-<ver>-<hwid>.bin
sdkconfig.defaults / .moto         MODIFY: NimBLE MTU/DLE/conns, SECURE_SIGNED_APPS_*, rollback enable
```

---

## Session 5.1 — `cmd` transport confirm + OTA op codes + peer-stream framing (foundation)

Roadmap exit: `cmd` serial path verified end-to-end; `OTA_*` op codes defined; the fused-log/event peer-stream framing is specified and emitted over serial; tag `p05-d1`. Mostly formalization — `cmd` already works.

**Files:** `components/app/include/app/cmd.h` (add op codes), `components/app/cmd/cmd.c` (route OTA ops to `app/ota`, stubbed until 5.4), `components/drivers/export_serial/export_serial.c` (peer-stream framing), a host test if the framing is host-reachable.

**Interfaces produced:**
- `enum { …, CMD_OTA_BEGIN=0x20, CMD_OTA_DATA=0x21, CMD_OTA_END=0x22, CMD_OTA_ABORT=0x23 }` (cmd.h).
- A `STREAM` frame shape on the `data` channel: `tag=0xFF (stream) | seq u16 | flags u8 | payload` where payload = a fused-sample or event record (§14 record types), so the same framing serves BLE `data` notify and the serial stream.

- [ ] **Step 1:** add the `CMD_OTA_*` op codes to `cmd.h`; in `cmd_dispatch`, route them to `app/ota` functions behind a weak/stub `ota_*` (returns `E_OTA_PRECOND` "not built") so 5.1 links before 5.4.
- [ ] **Step 2:** confirm the existing serial `cmd` path (STATUS/LIST/OPEN/READ) still round-trips: build `moto_sim`, flash, run the console `list`/`open`/`read` verbs, confirm a session file exports (as today).
- [ ] **Step 3:** define the `0xFF` STREAM tag framing; make `export_serial` emit fused-log records to the peer with it (formalize the existing `g_fix_ring` path). No behavior change to file ops.
- [ ] **Step 4:** linter clean (`--enforce-fnptr --fail-on-violation`), host tests 31/31, both envs build. Commit; tag `p05-d1`.

---

## Session 5.2 — `conn_ble`: NimBLE RaceChrono service — CODE-COMPLETE, ACTIVATION DEFERRED TO SUB-PROJECT C

> Status 2026-09-20: the module (`conn_ble.c`/`.h`, GAP advertising, GATT `cmd`/`data`/`status`, flow control) is written, builds, and fits neo6m — parked on `s5.2-conn-ble`. It is **not wired into `app_main` on the prod line** until sub-project C frees DRAM by offloading dev tooling. The flash gate below runs in C.

Roadmap exit (§18.2), **run in C**: a phone (nRF Connect / `bleak`) sees the service, connects, and pulls a session via `cmd` OPEN/READ over BLE; `status` reads back live state; tag deferred. Flash-gated (BLE is on-device only).

**Files:** new `components/app/conn_ble/conn_ble.c` + `include/app/conn_ble.h`; `main/app_main.c` (start conn_ble when `CONN` has `ble`); `sdkconfig.defaults` (NimBLE MTU 512, DLE on, 1 conn, role central/observer off); `docs/power-of-10-deviations.md` (register the GATT/GAP callback function-pointer sites — IDF API contract, §2 burden of proof).

**Interfaces produced:**
- `void conn_ble_start(void);` — init NimBLE, register the service, begin advertising for `cfg.ble.adv_s`.
- `int conn_ble_status_notify(void);` — push the 20-byte `status` (§18.2 layout) on change, ≤1 Hz.
- `int conn_ble_stream_push(const uint8_t *rec, uint8_t len);` — notify a `data` STREAM frame (flow-controlled).
- Service `7a3f0001-…`; chars `cmd 7a3f0002` (WRITE), `data 7a3f0003` (NOTIFY), `status 7a3f0004` (READ/NOTIFY).

- [ ] **Step 1:** NimBLE host init + GAP: device name `cfg.ble.name`, connectable-undirected advertising at 100 ms for `cfg.ble.adv_s`, then stop. Register the GAP/GATT callbacks and add a `docs/power-of-10-deviations.md` row for them (IDF function-pointer boundary).
- [ ] **Step 2:** GATT service + 3 characteristics with the §18.2 UUIDs/properties. `cmd` WRITE handler assembles the request and calls `cmd_dispatch(op, tag, payload, len, ble_emit, &conn)`; `ble_emit` notifies `data` chunks (reuse the CMD_CHUNK_MAX=496 framing).
- [ ] **Step 3:** `status` characteristic — build the 20-byte struct (proto_ver, state, sys_flags low16, batt_pct/mv, storage_free_kb, session_count, fw[7]); READ + NOTIFY on change ≤1 Hz.
- [ ] **Step 4:** connection-parameter request (15–30 ms during transfer; relax after 5 s idle) + flow control: `ble_gatts_notify_custom`, on `BLE_HS_ENOMEM` wait on the `BLE_GAP_EVENT_NOTIFY_TX` semaphore (≤500 ms) and retry; 10 s no progress → `E_CONN_XFER_ABORT`.
- [ ] **Step 5 (verify):** build both envs; flash `moto_neo6m`; **wait for "ready"**. With nRF Connect: service visible, connect, read `status`, write a `LIST` then `OPEN`/`READ`, confirm a VBO downloads that RaceChrono imports; throughput sanity (≥20 KB/s target). Linter/host-tests green. Commit; tag `p05-d2`.

---

## Session 5.3 — fused-log/event peer stream + hot-swap link state machine + peer detect

Roadmap exit: with a peer attached (serial or BLE), the lap-timer streams fused-log + events live; attach/detach is clean and the lap-timer runs normally with no peer; tag `p05-d3`.

**Files:** `components/app/pipeline/pipeline.c` (tap the event/fused path to the stream), `conn_ble.c` + `export_serial.c` (stream sinks), a small link-state module (peer present/absent, detect line).

- [ ] **Step 1:** a `stream_push(rec, len)` seam the pipeline calls for each fused sample (at `cfg.log_fused_hz`) + each event; fan out to whichever transport(s) have an attached peer. Bounded, non-blocking (drop-on-full, like the button queue) — must never stall the pipeline.
- [ ] **Step 2:** peer-detect: a GPIO detect line (design §6 connector) + a heartbeat/handshake over `cmd` (a `STATUS` poll marks the peer present); tolerate the peer absent/floating (no hang, no error spam).
- [ ] **Step 3:** hot-swap: attach mid-run starts streaming; detach stops cleanly (release buffers, no leak). Verify the UART lines idle safely with no peer.
- [ ] **Step 4 (verify):** flash `moto_sim`; **wait for "ready"**. Confirm: no peer → normal laps (28.071/28.044/28.028), no stall; attach a serial peer mid-run → fused stream flows; detach → clean. Linter/host/build green. Commit; tag `p05-d3`.

---

## Session 5.4 — OTA receive-side: signed image, hwid, SHA, rollback (§19)

Roadmap exit (§19.6): good / corrupt / wrong-hwid / crashing images each behave correctly (apply / reject / rollback); tag `p05-d4`. Flash-gated with deliberately-bad images.

**Files:** new `components/app/ota/ota.c` + `include/app/ota.h`; `cmd.c` (wire the OTA ops to real `ota_*`); `sup.c` (pending-verify → validate/rollback); `keys/laptimer_pub.pem` (+ gitignore rule); `tools/sign_release.sh`; `sdkconfig.defaults` (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`, `SECURE_SIGNED_APPS_ECDSA_SCHEME`, `SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, `SECURE_BOOT_VERIFICATION_KEY`, `BOOTLOADER_APP_ROLLBACK_ENABLE`).

**OTA flow (§19.4):** `OTA_BEGIN{size,sha,ver,hwid}` → precondition check (§19.5: batt ≥50% or charging, not already pending) → `esp_ota_begin(next slot)` → ack; `OTA_DATA{offset,bytes}×N` → `esp_ota_write`, after first 4 KB read the target's `hwid`@0x120 + `project_name`@0x30 and compare (mismatch → `E_OTA_HWID`, abort, erase), running SHA-256; `OTA_END` → SHA match check → `esp_ota_end` (ECDSA signature verify) → `esp_ota_set_boot_partition` → `nvs ota_pending=1` → ack → 2 s → `esp_restart`. Boot: supervisor marks valid iff self-test OK ∧ GPS frame seen ∧ storage mounted ∧ uptime ≥ `OTA_VALID_UPTIME_S`, else the bootloader rolls back.

- [ ] **Step 1:** signing setup — generate the offline key (documented, not run in CI), commit `keys/laptimer_pub.pem` (gitignore `*.pem` with `!keys/laptimer_pub.pem`), enable the `SECURE_SIGNED_*` sdkconfig, add `tools/sign_release.sh`. Confirm a signed `moto_neo6m` image builds + `espsecure.py verify_signature` passes.
- [ ] **Step 2:** `ota_begin/data/end/abort` in `ota.c` with the precondition, hwid, SHA, and `esp_ota_*` calls; `cmd.c` routes `CMD_OTA_*` to them (replacing the 5.1 stub). Rule-5 assertions on the state machine (offset monotonic, size bound, slot handle non-NULL).
- [ ] **Step 3:** supervisor pending-verify: on boot, if `ota_pending`, gate `esp_ota_mark_app_valid_cancel_rollback()` on the §19.4 conditions; log `E_OTA_VALIDATED`; on failure path log `E_OTA_ROLLBACK`.
- [ ] **Step 4 (verify):** build + sign; flash `moto_neo6m`; **wait for "ready"**. Push a **good** signed image over serial `OTA_*` → applies + validates. Push a **corrupt** (bad SHA) → `E_OTA_WRITE`, running image untouched. **Wrong-hwid** → `E_OTA_HWID`, abort. A **crashing** image → bootloader rolls back, `E_OTA_ROLLBACK` next boot. Linter/host/build green. Commit; tag `p05-d4`.

---

## Session 5.5 — prod-slim: flag-gate the dev console

Roadmap exit: prod builds can exclude the interactive console/`dbg` UX (kept for A/B bootstrapping via the flag); the prod interface is `cmd` + stream + OTA-receive + BLE; the image is no larger (ideally smaller) than before; tag `p05-d5` = sub-project-A done.

**Files:** `components/drivers/export_serial/export_serial.c` (split the interactive REPL/`dbg` verbs behind a sub-flag from the `cmd`/stream transport), `main/app_main.c`, `build.sh`/CMake (a `DEVUX` flag, default ON for now).

- [ ] **Step 1:** separate the transport (cmd request read + response emit + stream) from the operator UX (the REPL, `dbg` verbs, help text). Gate the UX behind a new flag (default ON so nothing changes yet); the transport stays unconditional.
- [ ] **Step 2:** build a `DEVUX=OFF` variant → confirm it links (the transport alone), the interactive verbs are gone, and the image is smaller. Build the default (`DEVUX=ON`) → unchanged behavior.
- [ ] **Step 3 (verify):** both envs build; flash `moto_sim` default variant; **wait for "ready"**; confirm console + laps unchanged. Linter/host/build green. Commit; tag `p05-d5`.

---

## Self-review notes

- Spec coverage: §18.2 BLE (5.2), §18.1 cmd (5.1, exists), §18 stream (5.3), §19 OTA+signing+rollback (5.4), prod/dev split (5.5). The web pages (§18.3/18.4) are intentionally NOT here — they move to sub-project B (dev controller).
- Function-pointer note: NimBLE GAP/GATT callbacks and any `esp_timer`/task callbacks introduced are IDF-boundary sites → register in `docs/power-of-10-deviations.md` (like PD-1..6), do not fight the SDK.
- Testability: BLE + OTA-apply are on-device only (flash gates per session, per the spec's §19.6 image-behavior matrix and §18.2 phone check). `cmd`/OTA-verify pure logic gets host tests where reachable.
- Interface consistency: the `data` STREAM framing (5.1) is shared by BLE notify (5.2) and the serial stream (5.3); the `OTA_*` ops (5.1 codes) are implemented in 5.4.
