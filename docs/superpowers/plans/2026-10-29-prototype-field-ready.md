# Prototype: ready for field testing on the track (stream B) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. Block b1's tasks are written in full below; b1 Task 3 writes the tasks of b2-b6 in full (files, tests, acceptance) in this file before any of them is dispatched. Lean path (umbrella "Replan 2026-10-29", DEC-0145). Playbook: `docs/superpowers/playbooks/two-streams.md`. Steps use checkbox (`- [ ]`) syntax.

**Goal:** the lap timer is ready for its first field test at Killarney: real GPS and IMU data in, laps detected and timed, sessions logged, offloaded and exported correctly, on the built hardware powered by a power bank (DEC-0134). The first track date is not fixed; it can be any day from about 2026-11-12 (DEC-0140): plan for field-ready by 2026-11-12, the rest is buffer.

**Architecture:** the existing firmware (core engines in `components/core/`, drivers in `components/drivers/`, app in `main/`) gains real GPS (u-blox on UART2) and IMU (MPU-6050 on I2C) drivers in place of the stubs; the engine and track-data HIGHs from audit 2 are fixed where real data switches them on; the bundled track table is corrected from cited research; sessions are offloaded to the dev-kit (`devcontroller/`) with a storage guard. Host tests and the simulator carry every change; one bench day and a host replay of a road capture close it.

**Tech Stack:** ESP-IDF 5.3.2 (`source tools/idf-env.sh`), C (Power of 10, zero warnings), Unity host tests (`test/`, `test_apps/core_selftest`), Python host tools (`tools/`), the dev-kit firmware (`devcontroller/`).

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (binding). Roadmap: `docs/superpowers/plans/2026-09-14-roadmap.md`. Audits: `docs/reviews/2026-10-16-audit-post-fix-wave.md`, `docs/reviews/2026-10-17-audit-drift-and-gaps.md`.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34); firmware and harnesses build with zero warnings; every firmware gate list builds `test_apps/core_selftest`.
- Block = PR, at most 3 tasks; the blocks are b1..b6 (DEC-0156), each a `## Block b<N>` heading below with ledger line `CHECKPOINT b<N> <sha>` and branch `block/prototype-b<N>`; a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch, each next block branching from the previous block's tip (DEC-0154), then under grant:prototype-field-ready (DEC-0155) when every check passes, on the owner's sentence "merge when all checks pass"; at the block end one opus final reviewer and one final verifier over the whole block (DEC-0145).
- Owner ruling 3 (P10 full reach): once stream A merges S3 b7 and b8, every file a task touches is clean under the full-reach linter.
- Hardware: never flash without the owner's explicit "ready"; after an OTA push never open the lap-timer port for 35 s; one 5 V source at a time (DEC-0135).
- Files outside stream B's ownership (playbook "Ownership") are raised at the next sync, never edited.
- Every file fact a stream B task writes into a tracked document cites a tracked file as `path:line@sha "quote"`; git-ignored research is imported first. This plan's own line citations have that form.

## Review Focus

1. **Real data the engines never saw.** CE-N2..N5 switch on only with real GPS and IMU input: each fix has a host test fed by a recorded or synthesised real-rate stream, not by the simulator's ideal one.
2. **A track table that cannot be found.** Auto-detect must find Killarney from a fix at the circuit (the current centre is about 3.8 km south, outside its 2000 m radius), and Kyalami must not be confused with it.
3. **An offload that loses a session.** The lap timer deletes a session only after the dev-kit holds a verified copy; the dev-kit refuses an offload rather than fill its storage.

## Hardware as built (DEC-0135; written into `docs/hardware/wiring.md` by b1 Task 2)
- Lap timer and dev-kit: two ESP32 DevKit boards (owner: "both boards are the same. its esp-wroom-32d").
- GPS G28U7FTTL-P (5 V part): VCC to VIN, TX to GPIO16, RX to GPIO17, PPS to GPIO36, EN not connected. The firmware assigns no GPS UART pins today: the driver is a stub (`components/drivers/gps_neo6m/gps_neo6m.c:4@667e0d7 "This stub exists so moto_neo6m"`).
- GPS power switch: not built. `components/drivers/board_devkit_v1/board.c:48@667e0d7 "drive LOW = GPS on"` defines `PIN_GPS_PWR` as GPIO26, but no MOSFET is owned and nothing is wired to GPIO26; GPS power modes use UBX software commands only (DEC-0147).
- IMU GY-521 (MPU-6050): VCC 3V3, GND, SDA 21, SCL 22, INT 27; AD0, XDA, XCL not connected.
- Display (2.13" SSD1680): CLK 18, DIN 23, CS 5, DC 14, RST 13, BUSY 35. Buttons: 6 mm tactiles.
- Dev-kit link: dev-kit 17 to lap-timer RX0, dev-kit 16 to lap-timer TX0, GND to GND, lap-timer GPIO4 to dev-kit GND (detect). No 5 V wire today; the owner adds it as needed.
- Power: field -- power bank into the lap timer's USB port, 5 V wire in, dev-kit powered by the lap timer and off the laptop. Between sessions -- unplug the power bank first, then the dev-kit goes on the laptop and powers both boards through the 5 V wire; one 5 V source at a time.
- Battery pack (Plan 6) stays in scope after the prototype.

## Scope (in)
1. Real GPS (roadmap 8.1-8.2): u-blox framing and configuration, NAV-PVT at the configured rate, power modes by UBX software commands only (DEC-0147), reset detection, GPS ladder, real UART timing into `tb`, on UART2 GPIO16/17.
2. Real IMU (roadmap 8.3, and 8.4 calibration only, DEC-0147): MPU-6050 init, FIFO, self-test, temperature; on-device calibration (orientation capture, forward learning). PARK wake, the low-power motion mode and the IMU ladder are deferred to Plan 6.
3. Engine defects that switch on with real data: CE-N2 (Doppler re-anchored at arrival, not epoch), CE-N3 (forced layout keeps the old lock), CE-N4 (lap best crosses venues), CE-N5 (un-oriented fusion blocks drag arming).
4. Track data: IO-N1..IO-N4 (exports on damaged logs, JSON after eviction, VBO laptiming), APP-N1 (rotated sessions' venue blank), DRV-N1 (sign_release signs a stale version).
5. Tracks (DEC-0136): Killarney Full, Full Reverse, Short, Short Reverse, Zwartkops, plus Kyalami as a third venue for the auto-detect test; Killarney and Kyalami coordinates from the track research (`docs/superpowers/state/program/sources/tracks-research.md`, tracked at the replan, DEC-0159), imported into `tools/tracks/research-2026-10-29.md` beside the track table (b2); `verified` stays false until on-track marking confirms them. The current Killarney centre is about 3.8 km south of the circuit, outside its 2000 m radius, so auto-detect cannot find Killarney today: corrected first (also the spec §10.2 example). Research answers (DEC-0141): S/F at the white line by the timing box (Killarney) and the painted grid-head line (Kyalami); 30 m gates; Short layouts kept, unverified, length unsourced; derived sector thirds; Kyalami id 1, layout "GP", 4522 m. Killarney Full Reverse is the real reverse layout: anticlockwise (two sources); its chicane ends, lap length and timing line are UNKNOWN until marked on track or supplied by WPMC/ZA Timing (an owner action).
6. On-device gate marking (spec F7) as backup and test (DEC-0137).
7. Storage (DEC-0138): a log-rate measurement (minutes of riding per MB, sessions per filesystem); session offload to the dev-kit when it is plugged in, with a dev-kit storage guard (free-space check first, refuse rather than fill, delete on the lap timer only after a verified copy) and dev-kit to laptop export.
8. Bench doc and bench day: fix the rows audit 2 §5 rates cannot-run (6), false-pass (6) and needs-rewrite (15); helpers safe for the 35 s OTA window; one bench day on the built prototype, flashing only on the owner's "ready".
9. A road capture replayed on the host (the off-track half of roadmap 8.5).
10. P10: once stream A merges S3 b7 and b8, every file a task touches is clean under the full-reach linter (owner ruling 3).

## Scope (out, tracked)
Plan 6 battery pack (after the prototype); PARK wake, the low-power motion mode and the IMU ladder (with Plan 6, DEC-0147); a GPS power switch on GPIO26; Plan 9 field validation; sub-project C (secure boot, BLE); every F5 item and every audit-2 finding that does not affect track data. b1 Task 1 lists each F5 skeleton item and each audit-2 section-4 row as in or out with a one-line reason; an "out" row stays on the roadmap or as an issue.

## Blocks (estimates; report to the owner at once past 1.5 times)
| block | content | estimate | parallel groups |
|---|---|---|---|
| b1 | scope table (F5 and audit-2 rows in/out), `docs/hardware/wiring.md` and BOM statuses, this plan's b2-b6 tasks written in full | 0.5 day | -- |
| b2 | GPS driver (8.1-8.2) / IMU driver (8.3) / track tables and research import | 2 days | three worktrees, disjoint files |
| b3 | CE-N2..N5 / IO-N1..N4, APP-N1, DRV-N1 | 2 days | two worktrees |
| b4 | calibration (8.4) / gate marking (F7) / offload and dev-kit storage guard | 2 days | three worktrees |
| b5 | P10 cleanup of touched files after strict-delivery b7/b8; bench-doc fixes; log-rate measurement procedure | 1.5 days | two worktrees |
| b6 | bench day on the built prototype; road capture and host replay | 1-2 days with the owner | -- |

## Block b1 -- scope table, wiring doc, tasks in full (Tasks 1-3), branch `block/prototype-b1`

Block start: register one planned row per task (prefix `PT-`, owner plan `prototype`, task `b1-T1`..`b1-T3`) so each implementer dispatch has a row, and push the branch.

### Task 1: scope table (F5 skeleton and audit-2 section 4, in or out)

**Files:**
- Modify: `docs/superpowers/plans/2026-10-29-prototype-field-ready.md` (a "Scope table" section after "Scope (out, tracked)")

**Findings:** none -- planning text (DEC-0134); the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: List the sources** -- every F5 skeleton item (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md`) and every row of audit 2 section 4 (`docs/reviews/2026-10-16-audit-post-fix-wave.md`, "## 4. New findings after verification"), each with its id and a `path:line@sha "quote"` citation.
- [ ] **Step 2: Rule each row in or out** -- in when it changes what the device measures, times, logs, offloads or exports on the track (DEC-0134); out otherwise, with a one-line reason and where it stays tracked (roadmap row or issue number). A row the rule does not settle is an owner question for the next sync.
- [ ] **Step 3: Check** -- the count of table rows equals the count of source items; `python3 tools/gates/gate_plan.py --plan docs/superpowers/plans/2026-10-29-prototype-field-ready.md` only PASS; `run_all` only PASS; the gate suite green on both interpreters.

### Task 2: wiring doc and BOM statuses

**Files:**
- Create: `docs/hardware/wiring.md`
- Modify: `docs/hardware/bom.md` (status column: owned, built, not built)

**Findings:** none -- documentation of the as-built hardware (DEC-0135); the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: Import the wiring** -- write every line of "Hardware as built" above into `docs/hardware/wiring.md` with its source: the owner's DEC-0135 answer, the pin defines in `components/drivers/board_devkit_v1/board.c` and the display driver, each cited `path:line@sha "quote"`. The wiring extract `docs/superpowers/state/program/sources/wiring-extract.md` (tracked at the replan, DEC-0159) is a source too; every fact taken from it is cited there as `path:line@sha "quote"` or restated with the DEC that records it.
- [ ] **Step 2: BOM statuses** -- each BOM row gets owned, built or not built; the GPS power MOSFET on GPIO26 is not built.
- [ ] **Step 3: Check** -- every pin in `wiring.md` matches the board defines it cites (one grep per pin, recorded in the task report); `run_all` only PASS; the gate suite green on both interpreters.

### Task 3: write b2-b6 tasks in full

**Files:**
- Modify: `docs/superpowers/plans/2026-10-29-prototype-field-ready.md` (the Block b2 to Block b6 headings with their tasks)

**Findings:** none -- planning text (DEC-0134); each later task routes the audit ids it fixes (CE-N2..N5, IO-N1..N4, APP-N1, DRV-N1) once they are registered with the `PT-` prefix at that block's start

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: Write each block** -- one `## Block b<N>` heading per row of the blocks table, at most three `### Task N` per block, each task with Files, Findings, Siblings, Mode (PARALLEL only for tasks on disjoint files), Gates and test-first steps naming the production-path test and its acceptance; b5 and b6 steps that need hardware say so and wait for the owner's "ready".
- [ ] **Step 2: Check** -- `python3 tools/gates/gate_plan.py --plan docs/superpowers/plans/2026-10-29-prototype-field-ready.md` only PASS (overlap between PARALLEL tasks included); `run_all` only PASS; the gate suite green on both interpreters.
- [ ] **Step 3: CHECKPOINT** -- the manual checkpoint of the umbrella "Checkpoint protocol for this plan" with one opus final reviewer and one final verifier over the block (DEC-0145); open the PR against `main`: it merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch, each next block branching from the previous block's tip (DEC-0154), then under grant:prototype-field-ready (DEC-0155) when every check passes, on the owner's sentence "merge when all checks pass"; append `CHECKPOINT b1 <sha>` to `docs/superpowers/state/prototype/ledger.md`; update `docs/superpowers/state/prototype/handoff.md` (next: b2 Task 1) and stream B's board row; stop for `/clear`.
