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

## Scope table (b1 Task 1)

Sources: every task row of the F5 skeleton (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:17@39a3e8a "| # | Item | Files | Closes | Mode |"`, 13 rows, F5-1..F5-13) and every finding row of audit 2 section 4 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:370@39a3e8a "## 4. New findings after verification"`, 79 rows: 13 HIGH, 24 MEDIUM, 42 LOW): 92 sources, 92 rows. Rule (DEC-0134): **in** when the row changes what the device measures, times, logs, offloads or exports on the track; **out** otherwise. A row the rule puts in that no Scope (in) item names, or that is in only as a sibling of a named finding (a class widening), is an **owner question (next sync)**, not ruled here. Totals: in 13, out 56, owner question 23.

Owner questions for the next sync (each row names one):
- DN-1: the rule puts the row in, but no Scope (in) item names it or it is a sibling of a named finding: CE-N1, CE-N6, CE-V-2, IO-N5, IO-N8, APP-N3, APP-N4, APP-N5, APP-N6, APP-N7, APP-N8, APP-N11, DC-N3, DC-N4, CE-#118, IO-N17, IO-V1, IO-V4, APP-N10, DRV-N10, F5-5 (= APP-N8, #104).
- DN-2: an out row with no issue and no roadmap row stays tracked only in audit 2 §6; the plan requires a roadmap row or an issue.
- DN-3: F5-8 (#53 task stacks): crash risk on the track, not data content; the rule does not settle it.
- DN-4: F5-12 (#92 bench checklist): Scope (in) 8 covers part of it; the rule does not settle the rest.

| # | id | source | verdict | reason | tracked |
|---|---|---|---|---|---|
| 1 | IO-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:381@39a3e8a "One CRC-bad FIX_DELTA is dropped by the"` | in | exports a damaged log as SUCCESS with rows shifted one sample (exports) | Scope (in) 4; b3 |
| 2 | IO-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:382@39a3e8a "A CRC-bad LAP frame is absent from"` | in | JSON summary drops a CRC-bad LAP and reports skipped 0 (exports) | Scope (in) 4; b3 |
| 3 | IO-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:383@39a3e8a "open <id> json"` | in | JSON summary fails once routine eviction removed the .log (exports) | Scope (in) 4; b3 |
| 4 | IO-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:384@39a3e8a "VBO never carries"` | in | VBO export never carries the laptiming section or the layout (exports) | Scope (in) 4; b3 |
| 5 | APP-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:385@39a3e8a "Every session opened by a mode rotation"` | in | rotated or retried sessions log a blank venue for life (logs, exports) | Scope (in) 4; b3 |
| 6 | DRV-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:386@39a3e8a "CFG_FW_VERSION"` | in | named by DEC-0134's field-test-ready option; the signed image can report a stale fw version | Scope (in) 4; b3 |
| 7 | TCI-T2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:387@39a3e8a "--enforce-fnptr"` | out | Power of 10 linter coverage of esp_console callbacks; no device output changes | audit 2 §6 B (text) and F5 (tool), no issue (DN-2); stream A owns tools/lint/ |
| 8 | TCI-T5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:388@39a3e8a "Spec §5.2 says the blocks are"` | out | spec_api_check covers prototypes only; a doc gate, no device output changes | audit 2 §6 B (text) and F5 (tool), no issue (DN-2); spec §5.2 |
| 9 | DC-FC-1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:389@39a3e8a "The drag scenario is now a permanent"` | out | sim-driver test gap (gps_sim.c); not in the real-GPS image | audit 2 §6 A7, no issue (DN-2) |
| 10 | CE-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "drag_on_fix"` | in | drag speed gates late by the real fix latency (measures) | Scope (in) 3; b3 |
| 11 | CE-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:391@39a3e8a "lap_force_layout"` | in | a forced layout keeps the old lock and exported layout (times, exports) | Scope (in) 3; b3 |
| 12 | CE-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:392@39a3e8a "Lap best, prev and the predictive reference"` | in | lap best and predictive reference cross venues (times) | Scope (in) 3; b3 |
| 13 | CE-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:393@39a3e8a "Production fusion is never oriented"` | in | un-oriented fusion never arms drag above 4.06 deg pitch (measures) | Scope (in) 3; b3 |
| 14 | CE-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:399@39a3e8a "A chord of ABSENT (undelivered) fixes shorter"` | owner question (next sync) | the rule says in (false VALID laps under real frame loss: times) but Scope (in) 3 names CE-N2..N5 only (DN-1) | audit 2 §6 D (Plan 8), no issue |
| 15 | CE-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:400@39a3e8a "One low Doppler sample within 2 s"` | owner question (next sync) | the rule says in (one low Doppler sample aborts a launch: measures) but Scope (in) 3 names CE-N2..N5 only (DN-1) | audit 2 §6 D (Plan 8), no issue |
| 16 | CE-V-2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:401@39a3e8a "A scan-driven venue change resets neither the"` | owner question (next sync) | the rule says in (scan-driven venue change keeps the best: times); taking it as CE-N4's sibling widens CE-N4's class (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 17 | IO-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:402@39a3e8a "gps_us"` | owner question (next sync) | the rule says in (a garbage KEY exports a fabricated NMEA date: exports) but Scope (in) 4 names IO-N1..N4 only (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 18 | IO-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:403@39a3e8a "ble.name"` | out | ble.name config string round trip; no track data | audit 2 §6 C only, no issue (DN-2) |
| 19 | IO-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:404@39a3e8a "The logger drains every fix before any"` | owner question (next sync) | the rule says in (drain order lags FUSED rows in the .log and VBO: logs, exports), magnitude unverified (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 20 | APP-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:405@39a3e8a "A close that times out (-2) still"` | out | needs an OTA push during a session; the field procedure (DEC-0135) moves logs between sessions, no OTA | audit 2 §6 C only, no issue (DN-2) |
| 21 | APP-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:406@39a3e8a "A pending CLOSE plus a mode flip"` | owner question (next sync) | the rule says in (session HDR mode differs from the engine mode: logs) but no Scope (in) item names it (DN-1) | #128 |
| 22 | APP-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:407@39a3e8a "The 9th request dequeued in one drain"` | owner question (next sync) | the rule says in (a dropped CLOSE leaves the session open: logs) but no Scope (in) item names it (DN-1) | #128 |
| 23 | APP-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:408@39a3e8a "The §12.6 pause is a latch driven"` | owner question (next sync) | the rule says in (one dropped motion edge suppresses FIX and FUSED while riding: logs) but no Scope (in) item names it (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 24 | APP-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:409@39a3e8a "LOG_RES_VENUE arriving before its OPEN is discarded"` | owner question (next sync) | the rule says in (a race blanks the venue name: logs); taking it as APP-N1's sibling widens APP-N1's class (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 25 | APP-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:410@39a3e8a "CMD_SET_LAYOUT"` | owner question (next sync) | the rule says in (a refused layout command wipes the best: times); taking it as CE-N3's sibling widens CE-N3's class (DN-1) | #114 |
| 26 | APP-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "Append path has no reserve check; one"` | owner question (next sync) | the rule says in (one spurious free_kb 0 evicts 8 sessions' .log: logs) but Scope (in) 7 names measurement and offload only (DN-1) | #104 |
| 27 | APP-N11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:412@39a3e8a "xQueueSend(g_result_q, r, 0)"` | owner question (next sync) | the rule says in (a LAP result dropped from .log and .sum: logs) but no Scope (in) item names it (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 28 | DRV-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:413@39a3e8a "The panel is in neither the hwid"` | out | panel missing from hwid and dist name; the prototype has one panel | audit 2 §6 C only, no issue (DN-2) |
| 29 | DRV-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:414@39a3e8a "A DEVUX=OFF image has no OTA receive"` | out | DEVUX=OFF image has no OTA receive; no env builds DEVUX=OFF | audit 2 §6 B, no issue (DN-2) |
| 30 | DRV-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:415@39a3e8a "Brownout level is the IDF default"` | out | brownout level belongs to the Plan 6 rail decision; no data content changes | #136; Plan 6 (audit 2 §6 D) |
| 31 | TCI-T1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:416@39a3e8a "A stale grace triple only prints"` | out | linter GRACE-STALE exit code; CI only | #139 |
| 32 | TCI-T7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:417@39a3e8a "Each required job probes for its input"` | out | required CI jobs pass with their input deleted; CI only | audit 2 §6 A8, no issue (DN-2); stream A owns ci.yml |
| 33 | DC-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:418@39a3e8a "The relay refreshes its idle deadline on"` | out | relayed lt verbs run to the 15 s cap; slow, the content is unchanged | audit 2 §6 A6, no issue (DN-2) |
| 34 | DC-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:419@39a3e8a "Relay rc is 0 whenever any byte"` | owner question (next sync) | the rule says in if offload or dev-kit export relays lt verbs (an ERR reply or a body cut at 512 B returns rc 0: offloads); b4's offload design is not written (DN-1) | audit 2 §6 A4, no issue |
| 35 | DC-V-3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:420@39a3e8a "devkit.py lt config get"` | out | lt config get truncated at 512 B; config, not track data | audit 2 §6 A5, no issue (DN-2) |
| 36 | DC-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:421@39a3e8a "One 20 s wait for every"` | owner question (next sync) | the rule says in if dev-kit export uses devkit.py (one 20 s wait times out on a .log above about 170 KB: offloads); b4's offload design is not written (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 37 | DC-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:422@39a3e8a "Helpers open the lap-timer port with pyserial's"` | in | bench helpers open the port inside the 35 s OTA window (Scope (in) 8 helpers) | Scope (in) 8; b5 |
| 38 | CE-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:428@39a3e8a "lap_init"` | out | lap_init leaves sec_next 0; unreachable from the pipeline call sites (audit row) | audit 2 §6 C only, no issue (DN-2) |
| 39 | CE-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:429@39a3e8a "tautological / literal-only assertions (rule 5)"` | out | tautological assertions (P10 rule 5); no device output changes | #116 |
| 40 | CE-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:430@39a3e8a "asserts"` | out | assert on caller input; callers pre-filter (audit row) | audit 2 §6 C only, no issue (DN-2) |
| 41 | CE-V-3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:431@39a3e8a "every drag test has"` | in | the drag test gap that hides CE-N2; the audit merges it into CE-N2's row (:390) | Scope (in) 3 (with CE-N2); b3 |
| 42 | CE-#118 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:432@39a3e8a "resume with layout 0 / lap 0"` | owner question (next sync) | the rule says in (resume with layout 0 tie-breaks blind: times); #118 is owned by Plan 8 on the roadmap, not named in Scope (in) (DN-1) | #118 |
| 43 | CE-#119 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:433@39a3e8a "10 two-level"` | out | rule-9 dereferences in lap.c; P10 hygiene, covered by Scope (in) 10 only if a task touches lap.c | #119 |
| 44 | CE-#11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:434@39a3e8a "PPS re-acquisition stalls 25 s behind one"` | out | PPS re-acquisition; has_pps is 0 on every shipped target and Scope (in) 1 names no PPS | #11 |
| 45 | IO-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:435@39a3e8a "strtod"` | out | strtod accepts hex floats in JSON input; no track data | audit 2 §6 C only, no issue (DN-2) |
| 46 | IO-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:436@39a3e8a "min_lap_s >= max_lap_s"` | out | min_lap_s >= max_lap_s accepted by config; only an invalid owner config reaches it | audit 2 §6 C only, no issue (DN-2) |
| 47 | IO-N11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:437@39a3e8a "text walk bounded by 32, source field"` | out | render text walk bound; display only | audit 2 §6 C only, no issue (DN-2) |
| 48 | IO-N15 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:438@39a3e8a "assertions their own comments call unreachable"` | out | assertions their comments call unreachable; no device output changes | audit 2 §6 C only, no issue (DN-2) |
| 49 | IO-N17 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:439@39a3e8a "a LAP after runs began is dropped"` | owner question (next sync) | the rule says in (a LAP after runs began is dropped uncounted from JSON: exports); IO-N1's sibling widens IO-N1's class (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 50 | IO-N18 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:440@39a3e8a "non-UTF-8 bytes copied raw into JSON"` | out | non-UTF-8 bytes copied raw into JSON; the bundled venue names are ASCII | audit 2 §6 C only, no issue (DN-2) |
| 51 | IO-V1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:441@39a3e8a "LIST never emits a layout name (spec"` | owner question (next sync) | the rule says in (LIST never emits a layout name: offloads); audit §6 C groups it with IO-N4, which widens IO-N4's class (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 52 | IO-V4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:442@39a3e8a "missing"` | owner question (next sync) | the rule says in (a missing .sum exports a blank header dated 1970: exports) but Scope (in) 4 names IO-N1..N4 only (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 53 | APP-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:443@39a3e8a "orphan"` | out | orphan .sum.tmp never reaped; a storage leak, the logged content is unchanged | #105 |
| 54 | APP-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:444@39a3e8a "dropped SAVE_TRACKS / ui notify leave the"` | owner question (next sync) | the rule says in if on-device gate marking (Scope (in) 6) saves through this path (a dropped save shows success: tracks the device measures against) (DN-1) | #108 |
| 55 | APP-N12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:445@39a3e8a "pending rotation of an unconfirmed session +"` | out | needs an OTA close during a pending rotation; no OTA in the field procedure (DEC-0135) | #128 |
| 56 | APP-N13 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:446@39a3e8a "asserts at task entry return from the"` | out | link.c task-entry asserts (P10); no device output changes | audit 2 §6 C only, no issue (DN-2) |
| 57 | APP-V-1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:447@39a3e8a "venue names truncated to 23 chars in"` | out | venue names cut at 23 chars; the bundled names are at most 14 (audit row) | audit 2 §6 C only, no issue (DN-2) |
| 58 | DRV-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:448@39a3e8a "untracked"` | out | untracked .c compiled past the dirty-tree guard; release tooling | audit 2 §6 C only, no issue (DN-2) |
| 59 | DRV-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:449@39a3e8a "--dry-run"` | out | sign_release --dry-run checks; release tooling | audit 2 §6 C only, no issue (DN-2) |
| 60 | DRV-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:450@39a3e8a "readdir"` | owner question (next sync) | the rule says in if offload lists sessions through readdir (a listing error ends as success: offloads); b4's offload design is not written (DN-1) | audit 2 §6 C only, no issue (DN-2) |
| 61 | DRV-N12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:451@39a3e8a "ESP_ERROR_CHECK"` | out | ESP_ERROR_CHECK in ISR enable aborts at init; no data content changes | audit 2 §6 C only, no issue (DN-2) |
| 62 | DRV-N16 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:452@39a3e8a "CHRG is not forced per build; a"` | out | CHRG not forced per build; release tooling | audit 2 §6 C only, no issue (DN-2) |
| 63 | DRV-N17 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:453@39a3e8a "substring match keeps"` | out | dist_prune keeps -dirty images; host tooling | audit 2 §6 C only, no issue (DN-2) |
| 64 | DRV-N18 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:454@39a3e8a "read_through_ses"` | out | read_through_ses loop without a cap (P10); dbg console only | audit 2 §6 C only, no issue (DN-2) |
| 65 | DRV-V1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:455@39a3e8a "a mid-file"` | out | dbg sum and dbg logck partial tally; dev console only | audit 2 §6 C only, no issue (DN-2) |
| 66 | DC-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:456@39a3e8a "dbg sim laps"` | out | sim laps teleport after drag; sim build only | audit 2 §6 C only, no issue (DN-2) |
| 67 | DC-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:457@39a3e8a "a 0xFF frame glued to the"` | out | an OTA flash fails safe on a glued frame; OTA, not track data | audit 2 §6 C only, no issue (DN-2) |
| 68 | DC-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:458@39a3e8a "dbg sim"` | out | dbg sim on a non-sim build replies posted; dev console only | audit 2 §6 C only, no issue (DN-2) |
| 69 | DC-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:459@39a3e8a "token-initial quotes stripped from relayed"` | out | relayed tracks put strips quotes; the bundled tables (DEC-0136) do not use tracks put | audit 2 §6 C only, no issue (DN-2) |
| 70 | TCI-T3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:460@39a3e8a "rules 3/4/5 evadable by spellings with 0"` | out | linter rules 3/4/5 evadable, 0 live instances; tooling | audit 2 §6 C only, no issue (DN-2) |
| 71 | TCI-T4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:461@39a3e8a "7 of 9 fn-ptr spellings and mutual"` | out | linter fn-ptr spellings unseen, 0 live instances; tooling | audit 2 §6 C only, no issue (DN-2) |
| 72 | TCI-T6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:462@39a3e8a "5 of 6 bypass shapes pass; 0"` | out | venue_announce bypass shapes, 0 live; tooling | audit 2 §6 C only, no issue (DN-2) |
| 73 | TCI-T8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:463@39a3e8a "44 host binaries cannot observe an assertion"` | out | host binaries cannot observe an assertion trip; test honesty | audit 2 §6 C only, no issue (DN-2) |
| 74 | TCI-T9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:464@39a3e8a "gps_dropout"` | out | replay fixtures pin engine output, not truth; test honesty | audit 2 §6 C only, no issue (DN-2) |
| 75 | TCI-T10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:465@39a3e8a "nothing reads it"` | out | sim_capture.expected.json unread; test data | audit 2 §6 C only, no issue (DN-2) |
| 76 | TCI-T11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:466@39a3e8a "two tests with no assertion (rely on"` | out | two ui tests with no assertion; test honesty | audit 2 §6 C only, no issue (DN-2) |
| 77 | TCI-T12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:467@39a3e8a "coverage job green even if its ctest"` | out | coverage job green on a failed ctest; CI only | audit 2 §6 C only, no issue (DN-2) |
| 78 | TCI-T13 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:468@39a3e8a "tools-tests list hard-coded"` | out | tools-tests list hard-coded; CI only | audit 2 §6 C only, no issue (DN-2) |
| 79 | TCI-T15 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:469@39a3e8a "exclusion-justification check accepts any comment naming the"` | out | exclusion-justification check too loose; CI only | audit 2 §6 C only, no issue (DN-2) |
| 80 | F5-1 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:19@39a3e8a "list"` | out | list summary cache; faster listing, the listed content is unchanged | #91 |
| 81 | F5-2 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:20@39a3e8a "0xFF"` | out | CRC on live-stream frames (#61 title), not on session files | #61 |
| 82 | F5-3 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:21@39a3e8a "Per-chunk OTA ACK"` | out | per-chunk OTA ACK; OTA, not track data | #57 |
| 83 | F5-4 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:22@39a3e8a "Sim clock epoch kept across resets"` | out | sim clock across resets; sim build only | #103 |
| 84 | F5-5 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:23@39a3e8a "Logger append-path reserve guard"` | owner question (next sync) | the rule says in (append-path reserve guard: logs) but Scope (in) 7 names measurement and offload only; same defect as APP-N8 (DN-1) | #104 |
| 85 | F5-6 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:24@39a3e8a "Boot-time reap of"` | out | boot reap of .sum.tmp; a storage leak, the logged content is unchanged | #105 |
| 86 | F5-7 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:25@39a3e8a "#106 console INFO silence: confirm on the"` | out | console INFO silence; console logging, not session data | #106 |
| 87 | F5-8 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:26@39a3e8a "Task stacks: with R6's stack-low check live"` | owner question (next sync) | the rule does not settle it: stack sizing is crash risk on the track (#53 title: before field use), not data content (DN-3) | #53 |
| 88 | F5-9 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:27@39a3e8a "Calibration wiring (design §7 #93 row)"` | in | calibration wiring (orientation capture, forward learning, persistence) is Scope (in) 2 | Scope (in) 2; b4 |
| 89 | F5-10 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:28@39a3e8a "Synth"` | out | synth venue id and sim table capacity; sim tooling | #100 |
| 90 | F5-11 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:29@39a3e8a "Rebase"` | out | BLE branch rebase; sub-project C | #135 |
| 91 | F5-12 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:30@39a3e8a "Bench checklist close: run every remaining"` | owner question (next sync) | the rule does not settle it: Scope (in) 8 fixes the audit 2 §5 rows and runs one bench day, F5 item 12 runs every remaining box and closes #92 (DN-4) | #92 |
| 92 | F5-13 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:31@39a3e8a "Roadmap close-out section updated with the merge"` | out | roadmap and memory record; no device output changes | roadmap F5 close-out section |

The skeleton's closing line records hardware-gated items that are not F5 tasks (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "Hardware-gated, recorded only:"`); they are ruled here as well, outside the 92-row count:

| # | item | source | verdict | reason | tracked |
|---|---|---|---|---|---|
| H1 | #11 M10 PPS | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#11 (M10 PPS)"` | out | PPS re-acquisition; Scope (in) 1 names no PPS | #11 |
| H2 | #54 pull-up/EXT0 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#54 pull-up/EXT0 (Plan 6)"` | out | charger pin and wake; Plan 6 battery pack (Scope (out)) | #54 |
| H3 | #82 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#82 (Plan 8)"` | out | display temperature throttle has no producer (#82 title); no data content changes | #82 |
| H4 | #93 real-IMU validation | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#93 real-IMU validation (Plan 8)"` | in | on-device calibration on the real IMU is Scope (in) 2; the bench day is Scope (in) 8 | Scope (in) 2, 8; b4, b6 |
| H5 | GPS ladder | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "GPS/IMU ladders (Plan 8)"` | in | Scope (in) 1 names the GPS ladder | Scope (in) 1; b2 |
| H6 | IMU ladder | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "GPS/IMU ladders (Plan 8)"` | out | deferred with Plan 6 (DEC-0147, Scope (out)) | Plan 6 |
| H7 | BLE | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "BLE (sub-project C)"` | out | sub-project C (Scope (out)) | #135; roadmap sub-project C |

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
