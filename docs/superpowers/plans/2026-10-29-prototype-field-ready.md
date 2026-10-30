# Prototype: ready for field testing on the track (stream B) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. Block b1's tasks are written in full below; b1 Task 3 writes the tasks of b2-b6 in full (files, tests, acceptance) in this file before any of them is dispatched. Lean path (umbrella "Replan 2026-10-29", DEC-0145). Playbook: `docs/superpowers/playbooks/two-streams.md`. Steps use checkbox (`- [ ]`) syntax.

**Goal:** the lap timer is ready for its first field test at Killarney: real GPS and IMU data in, laps detected and timed, sessions logged, offloaded and exported correctly, on the built hardware powered by a power bank (DEC-0134). The first track date is not fixed; it can be any day from about 2026-11-12 (DEC-0140): plan for field-ready by 2026-11-12, the rest is buffer.

**Architecture:** the existing firmware (core engines in `components/core/`, drivers in `components/drivers/`, app in `main/`) gains real GPS (u-blox on UART2) and IMU (MPU-6050 on I2C) drivers in place of the stubs; the engine and track-data HIGHs from audit 2 are fixed where real data switches them on; the bundled track table is corrected from cited research; sessions are offloaded through the dev-kit (`devcontroller/`) to the laptop, which changes the shape of the DEC-0138 dev-kit storage guard (Scope (in) 7 quotes DEC-0138 and DEC-0321). Host tests and the simulator carry every change; one bench day and a host replay of a road capture close it.

**Tech Stack:** ESP-IDF 5.3.2 (`.idf-version:1@39a3e8a "v5.3.2"`; `source tools/idf-env.sh`), C (Power of 10, zero warnings), Unity host tests (`test/`, `test_apps/core_selftest`), Python host tools (`tools/`), the dev-kit firmware (`devcontroller/`).

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (binding). Roadmap: `docs/superpowers/plans/2026-09-14-roadmap.md`. Audits: `docs/reviews/2026-10-16-audit-post-fix-wave.md`, `docs/reviews/2026-10-17-audit-drift-and-gaps.md`.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34); firmware and harnesses build with zero warnings; every firmware gate list builds `test_apps/core_selftest`.
- Block = PR, at most 3 tasks; the blocks are b1..b6 (DEC-0156), each a `## Block b<N>` heading below with ledger line `CHECKPOINT b<N> <sha>` and branch `block/prototype-b<N>`; a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch, each next block branching from the previous block's tip (DEC-0154), then under grant:prototype-field-ready (DEC-0155) when every check passes, on the owner's sentence "merge when all checks pass"; at the block end one opus final reviewer and one final verifier over the whole block (DEC-0145).
- Owner ruling 3 (P10 full reach): once stream A merges S3 b7 and b8, every file a task touches is clean under the full-reach linter.
- Hardware: never flash without the owner's explicit "ready"; after an OTA push never open the lap-timer port for 35 s; one 5 V source at a time (DEC-0135).
- Files outside stream B's ownership (playbook "Ownership") are raised at the next sync, never edited.
- Every file fact a stream B task writes into a tracked document cites a tracked file as `path:line@sha "quote"`; git-ignored research is imported first. This plan's own line citations have that form.

## Review Focus

1. **Real data the engines never saw.** The audit records that the host suite sees zero fix latency (`docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "the host suite uses zero latency everywhere"`) and that the sim IMU is upright (`docs/reviews/2026-10-16-audit-post-fix-wave.md:393@39a3e8a "imu_sim upright"`): each CE-N2..N5 fix has a host test fed by a recorded or synthesised real-rate stream, not by the simulator's ideal one.
2. **A track table that cannot be found.** Auto-detect must find Killarney from a fix at the circuit (`docs/superpowers/state/program/sources/tracks-research.md:67@b7cb0fa "The current venue sits about 3.8 km SOUTH of the circuit"`; `docs/superpowers/state/program/sources/tracks-research.md:67@b7cb0fa "outside its own 2000 m radius"`; the bundled centre and radius: `components/core/tracks/trk_bundled.c:5@39a3e8a ".lat = -33.8567, .lon = 18.517, .radius_m = 2000"`), and Kyalami must not be confused with it.
3. **An offload that loses a session.** DEC-0138 says "delete on the lap timer only after a verified copy" (`docs/superpowers/state/decisions.yml:1457@39a3e8a "delete on the lap timer only after a verified copy"`); DEC-0321 qualifies it (`docs/superpowers/state/prototype/pending-decisions.yml:207@b7cb0fa "the dev-kit does not store; it streams the session through to the laptop, so the laptop must be connected at offload"`). Task 12 decides where the free-space check and the verify run.

## Hardware as built (DEC-0135; written into `docs/hardware/wiring.md` by b1 Task 2)
- Lap timer and dev-kit: two ESP32 boards (owner: `docs/superpowers/state/program/sources/wiring-extract.md:116@39a3e8a "both boards are the same. its esp-wroom-32d"`); the lap timer carries the IMU, GPS, buttons and display (DEC-0311, `docs/superpowers/state/prototype/pending-decisions.yml:117@b7cb0fa "the lap timer wired with IMU, GPS, buttons and display"`); the dev-kit is the second board (`docs/superpowers/state/prototype/ledger.md:20@f97ff2a "I have a second esp32(the dev kit)"`).
- GPS G28U7FTTL-P (`docs/superpowers/state/program/sources/wiring-extract.md:82@39a3e8a "G28U7FTTL-P (owner-identified part)"`; 5 V part, `docs/superpowers/state/program/sources/wiring-extract.md:89@39a3e8a "it's definitley a 5v"`): VCC to VIN, TX to GPIO16, RX to GPIO17, PPS to GPIO36, EN not connected (`docs/superpowers/state/decisions.yml:1430@39a3e8a "GPS VCC to VIN 5 V, TX 16, RX 17, PPS 36, EN open"`). The firmware assigns no GPS UART pins today: the driver is a stub (`components/drivers/gps_neo6m/gps_neo6m.c:4@667e0d7 "This stub exists so moto_neo6m"`).
- GPS power switch: not built. `components/drivers/board_devkit_v1/board.c:48@667e0d7 "drive LOW = GPS on"` defines `PIN_GPS_PWR` as GPIO26, but no MOSFET is owned and nothing is wired to GPIO26; GPS power modes use UBX software commands only (DEC-0147).
- IMU GY-521 (MPU-6050) (`docs/superpowers/state/program/sources/wiring-extract.md:105@39a3e8a "IMU: GY-521 (MPU-6050)"`): VCC 3V3 (`docs/superpowers/state/decisions.yml:1430@39a3e8a "IMU 3V3"`), GND, SDA 21 (`components/drivers/board_devkit_v1/board.c:49@39a3e8a "GPIO_NUM_21"`), SCL 22 (`components/drivers/board_devkit_v1/board.c:50@39a3e8a "GPIO_NUM_22"`; DEC-0311, owner: `docs/superpowers/state/prototype/pending-decisions.yml:117@b7cb0fa "i did find 21 and 22"`), INT 27 (`components/drivers/board_devkit_v1/board.c:51@39a3e8a "GPIO_NUM_27"`); AD0, XDA, XCL not connected (`docs/superpowers/state/program/sources/wiring-extract.md:114@39a3e8a "XDA, XCL, AD0"`).
- Display (2.13" SSD1680, `docs/hardware/wiring.md:43@f97ff2a "e-paper HAT (SSD1680)"`): CLK 18 (`components/drivers/board_devkit_v1/board.c:52@39a3e8a "GPIO_NUM_18"`), DIN 23 (`components/drivers/board_devkit_v1/board.c:53@39a3e8a "GPIO_NUM_23"`), CS 5 (`components/drivers/display_epaper_ssd1680/display_epaper.c:37@39a3e8a "GPIO_NUM_5"`), DC 14 (`components/drivers/display_epaper_ssd1680/display_epaper.c:34@39a3e8a "GPIO_NUM_14"`), RST 13 (`components/drivers/display_epaper_ssd1680/display_epaper.c:35@39a3e8a "GPIO_NUM_13"`), BUSY 35 (`components/drivers/display_epaper_ssd1680/display_epaper.c:36@39a3e8a "GPIO_NUM_35"`). Buttons: 6 mm tactiles (`docs/superpowers/state/program/sources/wiring-extract.md:52@39a3e8a "6 mm 4-leg tactile"`).
- Dev-kit link: dev-kit 17 to lap-timer RX0 (`devcontroller/main/build_config.h.in:12@39a3e8a "DC_LINK_TX       17"`; `docs/superpowers/state/program/sources/wiring-extract.md:65@39a3e8a "Lap-timer GPIO3 (UART0 RX; RX0)"`), dev-kit 16 to lap-timer TX0 (`devcontroller/main/build_config.h.in:13@39a3e8a "DC_LINK_RX       16"`; `docs/superpowers/state/program/sources/wiring-extract.md:66@39a3e8a "Lap-timer GPIO1 (UART0 TX; TX0)"`), GND to GND (`docs/superpowers/state/program/sources/wiring-extract.md:67@39a3e8a "Dev-kit GND"`), lap-timer GPIO4 to dev-kit GND (detect; `components/app/CMakeLists.txt:43@39a3e8a "LINK_DETECT_GPIO=4"`; `docs/superpowers/state/program/sources/wiring-extract.md:68@39a3e8a "Dev-kit GND (a dedicated wire)"`). No 5 V wire today; the owner adds it as needed (`docs/superpowers/state/decisions.yml:1433@39a3e8a "no since I plugged both into the laptop for debugging. I can add/remove it as needed"`).
- Power: field -- power bank into the lap timer's USB port (`docs/superpowers/state/decisions.yml:1433@39a3e8a "the power bank will be plugged into the usb port"`), 5 V wire in (`docs/superpowers/state/decisions.yml:1430@39a3e8a "field: 5 V link in"`), dev-kit powered by the lap timer and off the laptop (`docs/superpowers/state/decisions.yml:1433@39a3e8a "it'll be powered by the laptimer and disconnected from the laptop"`). Between sessions -- unplug the power bank first, then the dev-kit goes on the laptop and powers both boards through the 5 V wire; one 5 V source at a time (`docs/superpowers/state/decisions.yml:1430@39a3e8a "between sessions: power bank out first, laptop powers both via the dev-kit and the 5 V link (one 5 V source at a time)"`). While riding, the dev-kit is held in reset or kept off lap-timer RX0; offload only between sessions (DEC-0312, `docs/superpowers/state/prototype/pending-decisions.yml:123@b7cb0fa "the dev-kit stays in reset or off RX0 while riding; offload only between sessions"`).
- Battery pack (Plan 6) stays in scope after the prototype.

## Scope (in)
1. Real GPS (roadmap 8.1-8.2): u-blox framing and configuration, NAV-PVT at the configured rate, power modes by UBX software commands only (DEC-0147), reset detection, GPS ladder, real UART timing into `tb`, on UART2 GPIO16/17.
2. Real IMU (roadmap 8.3, and 8.4 calibration only, DEC-0147): MPU-6050 init, FIFO, self-test, temperature; on-device calibration (orientation capture, forward learning). PARK wake, the low-power motion mode and the IMU ladder are deferred to Plan 6.
3. Engine defects that switch on with real data: CE-N2 (Doppler re-anchored at arrival, not epoch), CE-N3 (forced layout keeps the old lock), CE-N4 (lap best crosses venues), CE-N5 (un-oriented fusion blocks drag arming), CE-N1 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:399@39a3e8a "A chord of ABSENT (undelivered) fixes shorter than 5 s still evaluates crossings"`; DEC-0303); Scope table rows 10 to 14 carry the audit cites.
4. Track data: IO-N1..IO-N4 (exports on damaged logs, JSON after eviction, VBO laptiming), APP-N1 (rotated sessions' venue blank), DRV-N1 (sign_release signs a stale version); Scope table rows 1 to 6 carry the audit cites.
5. Tracks (DEC-0136): Killarney Full, Full Reverse, Short, Short Reverse, Zwartkops, plus Kyalami as a third venue for the auto-detect test; Killarney and Kyalami coordinates from the track research (`docs/superpowers/state/program/sources/tracks-research.md`, tracked at the replan, DEC-0159), imported into `tools/tracks/research-2026-10-29.md` beside the track table (b2); `verified` stays false until on-track marking confirms them. The current Killarney centre is about 3.8 km south of the circuit (`docs/superpowers/state/program/sources/tracks-research.md:67@b7cb0fa "The current venue sits about 3.8 km SOUTH of the circuit"`), outside its 2000 m radius (`docs/superpowers/state/program/sources/tracks-research.md:67@b7cb0fa "outside its own 2000 m radius"`; bundled: `components/core/tracks/trk_bundled.c:5@39a3e8a ".lat = -33.8567, .lon = 18.517, .radius_m = 2000"`), so auto-detect cannot find Killarney today: corrected first (also the spec section 10.2 example, `docs/superpowers/specs/2026-09-14-lap-timer-design.md:1459@39a3e8a "-33.8567"`). Research answers (DEC-0141): S/F at the white line by the timing box (Killarney) and the painted grid-head line (Kyalami); 30 m gates; Short layouts kept, unverified, length unsourced; derived sector thirds; Kyalami id 1, layout "GP", 4522 m. Killarney Full Reverse is the real reverse layout: anticlockwise (two sources); its chicane ends, lap length and timing line are UNKNOWN until marked on track or supplied by WPMC/ZA Timing (an owner action).
6. On-device gate marking (spec F7) as backup and test (DEC-0137).
7. Storage (DEC-0138): a log-rate measurement (minutes of riding per MB, sessions per filesystem); session offload as DEC-0138 words it (`docs/superpowers/state/decisions.yml:1457@39a3e8a "sessions offloaded to the dev-kit when it is plugged in, with a dev-kit storage guard (free-space check, refuse rather than fill, delete on the lap timer only after a verified copy) and dev-kit to laptop export"`), qualified by DEC-0321 (`docs/superpowers/state/prototype/pending-decisions.yml:207@b7cb0fa "the dev-kit does not store; it streams the session through to the laptop, so the laptop must be connected at offload; changes the shape of DEC-0138 (dev-kit storage guard)"`; the owner's words, `docs/superpowers/state/prototype/pending-decisions.yml:209@b7cb0fa "we can store to laptop when we plug the laptop into the dev kit. but we cannot connect a laptop while doing laps"`). Task 12 decides where the free-space check and the verify run. DC-N3, DC-N4 and DRV-N10 are in only if the b4 offload design puts them on its path, otherwise out and tracked #151 (DEC-0303); Task 3's b4 offload task settles each.
8. Bench doc and bench day: fix the rows audit 2 §5 rates cannot-run (6), false-pass (6) and needs-rewrite (15) (`docs/reviews/2026-10-16-audit-post-fix-wave.md:34@39a3e8a "6 cannot run as written"`; `docs/reviews/2026-10-16-audit-post-fix-wave.md:35@39a3e8a "6 would false-pass (20, 39, 48, 50, 52, 69), 15 need a rewrite"`); helpers safe for the 35 s OTA window; one bench day on the built prototype, flashing only on the owner's "ready".
9. A road capture replayed on the host (the off-track half of roadmap 8.5).
10. P10: once stream A merges S3 b7 and b8, every file a task touches is clean under the full-reach linter (owner ruling 3).
11. Logging rows (DEC-0303): APP-N5 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:408@39a3e8a "one dropped edge keeps FIX/FUSED suppressed while riding"`), APP-N8 with F5-5 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "Append path has no reserve check"`; `docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "evicts 8 sessions"`), APP-N11 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:412@39a3e8a "drops a LAP result with no errlog or counter"`). Task 3 assigns the block.
12. Ride-time crash and reset risk (DEC-0305, `docs/superpowers/state/prototype/pending-decisions.yml:59@b7cb0fa "a 1 h soak per env on the bench day, then size the stacks, about 0.5 day in b5/b6, plus the DRV-N4 fix"`; DEC-0320, `docs/superpowers/state/prototype/pending-decisions.yml:196@b7cb0fa "moto_neo6m only: 1 h"`): F5-8 (task stacks, #53) and DRV-N4 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:415@39a3e8a "SPI NOR is specified ≥ 2.7 V"`, #136). Task 3 assigns the block (b5/b6 hint).

## Scope (out, tracked)
Plan 6 battery pack (after the prototype); PARK wake, the low-power motion mode and the IMU ladder (with Plan 6, DEC-0147); a GPS power switch on GPIO26; Plan 9 field validation; sub-project C (secure boot, BLE); every F5 item and every audit-2 finding that the Scope table below does not rule in. b1 Task 1 lists each F5 skeleton item and each audit-2 section-4 row as in or out with a one-line reason; an "out" row stays on the roadmap or as an issue.

## Scope table (b1 Task 1)

Sources: every task row of the F5 skeleton (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:17@39a3e8a "| # | Item | Files | Closes | Mode |"`, 13 rows, F5-1..F5-13) and every finding row of audit 2 section 4 (`docs/reviews/2026-10-16-audit-post-fix-wave.md:370@39a3e8a "## 4. New findings after verification"`, 79 rows: 13 HIGH, 24 MEDIUM, 42 LOW): 92 sources, 92 rows. Rule (DEC-0134): **in** when the row changes what the device measures, times, logs, offloads or exports on the track; **out** otherwise. **in (conditional)** is in if the b4 offload design puts the row on its path, otherwise out and tracked `#151`; Task 3's b4 offload task settles each (DEC-0303). Every owner question the table raised is answered; no row is left as an owner question. The owner answers DEC-0300..DEC-0314 are recorded in `docs/superpowers/state/prototype/pending-decisions.yml:6@f97ff2a "- id: DEC-0300"` until they move into `decisions.yml`. Totals: in 20, in (conditional) 3, out 69, owner question 0. Crash or reset risk: the rule put to the owner (`docs/superpowers/state/prototype/pending-decisions.yml:57@b7cb0fa "sends ride-time crash risk to the owner and init-only or console-only risk out"`) was confirmed by DEC-0305 (`docs/superpowers/state/prototype/pending-decisions.yml:59@b7cb0fa "confirm the rule; F5-8 and DRV-N4 in"`); the owner ruled rows DRV-N4 and F5-8 in. This plan places rows APP-N13, DRV-N12, DRV-N18 and CE-N7 on the out side; each reason cell gives its basis. An issue in the tracked column was checked against the issue's own title, body and comments. Every out row without another issue is listed in GitHub issue #151 (DEC-0304, umbrella-issue). Every file fact in a reason cell carries a `path:line@sha "quote"` citation or is stated in the audit or F5 row the source column cites.

Owner answers applied (each recorded under its DEC id in the pending file above):
- DEC-0303 (per-row): in APP-N5, APP-N8, F5-5, APP-N11, CE-N1; in (conditional) DC-N3, DC-N4, DRV-N10; out CE-N6, CE-V-2, IO-N5, IO-N8, APP-N3, APP-N4, APP-N6, APP-N7, APP-N10, CE-#118, IO-N17, IO-V1, IO-V4.
- DEC-0304 (umbrella-issue): every out row with no other tracker is tracked `#151`.
- DEC-0305 (rule-and-in): `docs/superpowers/state/prototype/pending-decisions.yml:59@b7cb0fa "confirm the rule; F5-8 and DRV-N4 in"`; Scope (in) 12 carries the two rows.
- DEC-0306 (out): F5-12 is out, tracked `#92`.
- DEC-0307 (no-ota-at-track): APP-N2 and APP-N12 stay out.
- DEC-0308 (no-pps): CE-#11 and H1 stay out.

| # | id | source | verdict | reason | tracked |
|---|---|---|---|---|---|
| 1 | IO-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:381@39a3e8a "One CRC-bad FIX_DELTA is dropped by the"` | in | exports a damaged log as SUCCESS with rows shifted one sample (exports) | Scope (in) 4; b3 |
| 2 | IO-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:382@39a3e8a "A CRC-bad LAP frame is absent from"` | in | JSON summary drops a CRC-bad LAP and reports skipped 0 (exports) | Scope (in) 4; b3 |
| 3 | IO-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:383@39a3e8a "open <id> json"` | in | JSON summary fails once routine eviction removed the .log (exports) | Scope (in) 4; b3 |
| 4 | IO-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:384@39a3e8a "VBO never carries"` | in | VBO export never carries the laptiming section or the layout (exports) | Scope (in) 4; b3 |
| 5 | APP-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:385@39a3e8a "Every session opened by a mode rotation"` | in | rotated or retried sessions log a blank venue for life (logs, exports) | Scope (in) 4; b3 |
| 6 | DRV-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:386@39a3e8a "CFG_FW_VERSION"` | in | named by DEC-0134's field-test-ready option (`docs/superpowers/state/decisions.yml:1421@f97ff2a "DRV-N1"`); the signed image can report a stale fw version | Scope (in) 4; b3 |
| 7 | TCI-T2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:387@39a3e8a "--enforce-fnptr"` | out | Power of 10 linter coverage of esp_console callbacks; no device output changes | #151; stream A owns tools/lint/ (`docs/superpowers/playbooks/two-streams.md:26@f97ff2a "tools/lint/"`) |
| 8 | TCI-T5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:388@39a3e8a "Spec §5.2 says the blocks are"` | out | spec_api_check covers prototypes only; a doc gate, no device output changes | #151; spec §5.2 |
| 9 | DC-FC-1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:389@39a3e8a "The drag scenario is now a permanent"` | out | sim-driver test gap (gps_sim.c); not in the real-GPS image | #151 |
| 10 | CE-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "drag_on_fix"` | in | drag speed gates late by the real fix latency (measures) | Scope (in) 3; b3 |
| 11 | CE-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:391@39a3e8a "lap_force_layout"` | in | a forced layout keeps the old lock and exported layout (times, exports) | Scope (in) 3; b3 |
| 12 | CE-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:392@39a3e8a "Lap best, prev and the predictive reference"` | in | lap best and predictive reference cross venues (times) | Scope (in) 3; b3 |
| 13 | CE-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:393@39a3e8a "Production fusion is never oriented"` | in | un-oriented fusion never arms drag above 4.06 deg pitch (measures) | Scope (in) 3; b3 |
| 14 | CE-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:399@39a3e8a "A chord of ABSENT (undelivered) fixes shorter"` | in | a chord of absent fixes shorter than 5 s still evaluates crossings and gives +159..+165 ms VALID laps and a false best under real frame loss (times); owner answer DEC-0303 | Scope (in) 3; b3 |
| 15 | CE-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:400@39a3e8a "One low Doppler sample within 2 s"` | out | one low Doppler sample within 2 s of t0 aborts a launch; out by owner answer DEC-0303 | #151 |
| 16 | CE-V-2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:401@39a3e8a "A scan-driven venue change resets neither the"` | out | a scan-driven venue change resets neither the pipeline's s_best nor the ui's BEST board; out by owner answer DEC-0303 | #151 |
| 17 | IO-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:402@39a3e8a "gps_us"` | out | a positive garbage KEY exports a fabricated NMEA date; out by owner answer DEC-0303 | #151 |
| 18 | IO-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:403@39a3e8a "ble.name"` | out | ble.name config string round trip; no track data | #151 |
| 19 | IO-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:404@39a3e8a "The logger drains every fix before any"` | out | a stalled drain clamps older FUSED dt to 0 and the .log order lags the VBO; magnitude unverified (audit row); out by owner answer DEC-0303 | #151 |
| 20 | APP-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:405@39a3e8a "A close that times out (-2) still"` | out | needs an OTA push during a session (audit row); no OTA push happens at the track (DEC-0307) | #151 |
| 21 | APP-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:406@39a3e8a "A pending CLOSE plus a mode flip"` | out | a pending CLOSE plus a mode flip plus a same-venue re-find loses the mode rotation; out by owner answer DEC-0303 | #128 |
| 22 | APP-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:407@39a3e8a "The 9th request dequeued in one drain"` | out | the 9th request dequeued in one drain is dropped, so a dropped CLOSE leaves the session open; out by owner answer DEC-0303 | #128 |
| 23 | APP-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:408@39a3e8a "The §12.6 pause is a latch driven"` | in | one dropped EV_MOTION edge keeps FIX and FUSED suppressed while riding (logs); owner answer DEC-0303 | Scope (in) 11; Task 3 assigns |
| 24 | APP-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:409@39a3e8a "LOG_RES_VENUE arriving before its OPEN is discarded"` | out | LOG_RES_VENUE arriving before its OPEN is discarded, so a race blanks the venue name; out by owner answer DEC-0303 | #151 |
| 25 | APP-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:410@39a3e8a "CMD_SET_LAYOUT"` | out | CMD_SET_LAYOUT discards lap_force_layout's -1 and clears s_best unconditionally; out by owner answer DEC-0303 | #114 |
| 26 | APP-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "Append path has no reserve check; one"` | in | one spurious free_kb == 0 reading evicts 8 sessions' .log (logs); same defect as F5-5; owner answer DEC-0303 | Scope (in) 11; #104; Task 3 assigns |
| 27 | APP-N11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:412@39a3e8a "xQueueSend(g_result_q, r, 0)"` | in | a LAP result dropped from the 4-deep result queue with no errlog or counter, the only carrier of the full LAP record into .log and .sum (logs); owner answer DEC-0303 | Scope (in) 11; Task 3 assigns |
| 28 | DRV-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:413@39a3e8a "The panel is in neither the hwid"` | out | panel missing from the hwid and dist name; the prototype has one panel (`docs/hardware/wiring.md:43@f97ff2a "Waveshare 2.13"`) | #151 |
| 29 | DRV-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:414@39a3e8a "A DEVUX=OFF image has no OTA receive"` | out | DEVUX=OFF image has no OTA receive; no env builds DEVUX=OFF | #151 |
| 30 | DRV-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:415@39a3e8a "Brownout level is the IDF default"` | in | brownout level LVL_SEL_0 (about 2.43 V) below the 2.7 V SPI NOR minimum: a reset risk while riding on the power bank (`docs/superpowers/state/decisions.yml:1433@39a3e8a "the power bank will be plugged into the usb port"`); owner answer DEC-0305 | Scope (in) 12; #136 |
| 31 | TCI-T1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:416@39a3e8a "A stale grace triple only prints"` | out | linter GRACE-STALE exit code; CI only | #151 |
| 32 | TCI-T7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:417@39a3e8a "Each required job probes for its input"` | out | required CI jobs pass with their input deleted; CI only | #151; stream A owns ci.yml (`docs/superpowers/playbooks/two-streams.md:26@f97ff2a ".github/workflows/ci.yml"`) |
| 33 | DC-N2 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:418@39a3e8a "The relay refreshes its idle deadline on"` | out | relayed lt verbs run to the 15 s cap, a delay (audit row; release target no, dev-kit) | #151 |
| 34 | DC-N3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:419@39a3e8a "Relay rc is 0 whenever any byte"` | in (conditional) | an ERR reply or a body cut at 512 B returns rc 0 (offloads); in if the b4 offload design relays lt verbs, otherwise out (owner answer DEC-0303); Task 3's b4 offload task settles it | Scope (in) 7 if in; #151 if out |
| 35 | DC-V-3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:420@39a3e8a "devkit.py lt config get"` | out | lt config get truncated at 512 B; config, not track data | #151 |
| 36 | DC-N4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:421@39a3e8a "One 20 s wait for every"` | in (conditional) | one 20 s wait for every lt verb: a .log above about 170 KB raw times out (offloads); in if the b4 offload design uses devkit.py, otherwise out (owner answer DEC-0303); Task 3's b4 offload task settles it | Scope (in) 7 if in; #151 if out |
| 37 | DC-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:422@39a3e8a "Helpers open the lap-timer port with pyserial's"` | in | bench helpers open the port inside the 35 s OTA window (Scope (in) 8 helpers) | Scope (in) 8; b5 |
| 38 | CE-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:428@39a3e8a "lap_init"` | out | lap_init leaves sec_next 0; unreachable from the pipeline call sites (audit row) | #151 |
| 39 | CE-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:429@39a3e8a "tautological / literal-only assertions (rule 5)"` | out | tautological assertions (P10 rule 5); no device output changes | #116 (trk.c, trk_json.c sites); #151 (drag.c, lap.c, lap_cfg.c sites) |
| 40 | CE-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:430@39a3e8a "asserts"` | out | assert on caller input; callers pre-filter (audit row) | #151 |
| 41 | CE-V-3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:431@39a3e8a "every drag test has"` | in | the drag test gap that hides CE-N2; the audit merges it into CE-N2's row (`docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "CE-N2 (+CE-F2, CE-V-3)"`) | Scope (in) 3 (with CE-N2); b3 |
| 42 | CE-#118 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:432@39a3e8a "resume with layout 0 / lap 0"` | out | resume with layout 0 / lap 0 never scores disambiguation (blind tie-break); out by owner answer DEC-0303 | #118 |
| 43 | CE-#119 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:433@39a3e8a "10 two-level"` | out | 10 two-level pred_ dereferences in lap.c with no deviation row; P10 hygiene, covered by Scope (in) 10 only if a task touches lap.c | #119 |
| 44 | CE-#11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:434@39a3e8a "PPS re-acquisition stalls 25 s behind one"` | out | PPS re-acquisition; has_pps = 0 on every shipped target (audit row) and the b2 driver keeps has_pps = 0 (DEC-0308) | #11 |
| 45 | IO-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:435@39a3e8a "strtod"` | out | strtod accepts hex floats in JSON input; no track data | #151 |
| 46 | IO-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:436@39a3e8a "min_lap_s >= max_lap_s"` | out | min_lap_s >= max_lap_s accepted by cfg_validate and replaced by defaults in the engine; config validation, no track data | #151 |
| 47 | IO-N11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:437@39a3e8a "text walk bounded by 32, source field"` | out | render text walk bounded by 32 over a char label[8]; display code only (render.c, screens_moto.c per the audit row) | #151 |
| 48 | IO-N15 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:438@39a3e8a "assertions their own comments call unreachable"` | out | assertions their comments call unreachable; no device output changes | #151 |
| 49 | IO-N17 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:439@39a3e8a "a LAP after runs began is dropped"` | out | a LAP after runs began is dropped uncounted from the JSON summary; out by owner answer DEC-0303 | #151 |
| 50 | IO-N18 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:440@39a3e8a "non-UTF-8 bytes copied raw into JSON"` | out | non-UTF-8 bytes copied raw into JSON; the bundled venue names are ASCII (`tools/tracks/killarney.json:2@39a3e8a "Killarney"`, `tools/tracks/zwartkops.json:2@39a3e8a "Zwartkops"`) | #151 |
| 51 | IO-V1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:441@39a3e8a "LIST never emits a layout name (spec"` | out | LIST never emits a layout name; out by owner answer DEC-0303 | #151 |
| 52 | IO-V4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:442@39a3e8a "missing"` | out | a missing .sum is ignored: export succeeds with a blank header and a 1970 date; out by owner answer DEC-0303 | #151 |
| 53 | APP-N9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:443@39a3e8a "orphan"` | out | orphan .sum.tmp never reaped; a leftover file (audit row: not bench-observable) | #105 |
| 54 | APP-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:444@39a3e8a "dropped SAVE_TRACKS / ui notify leave the"` | out | a dropped SAVE_TRACKS or ui notify leaves the success card, no test of NOT SAVED; out by owner answer DEC-0303 | #108 |
| 55 | APP-N12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:445@39a3e8a "pending rotation of an unconfirmed session +"` | out | needs an OTA close during a pending rotation (audit row); no OTA push happens at the track (DEC-0307) | #128 |
| 56 | APP-N13 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:446@39a3e8a "asserts at task entry return from the"` | out | asserts at task entry return from the task (UB), the in-loop one is a tautology (audit row); the entry asserts check the ring link_start initialises (`components/app/link/link.c:223@39a3e8a "LT_ASSERT_VOID(g_stream_ring.buf != NULL"`) and link_start has one call site (`main/app_main.c:268@39a3e8a "link_start();"`), so they fire only at startup; no device output changes | #151 |
| 57 | APP-V-1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:447@39a3e8a "venue names truncated to 23 chars in"` | out | venue names cut at 23 chars; the bundled names are at most 14 (audit row) | #151 |
| 58 | DRV-N7 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:448@39a3e8a "untracked"` | out | untracked .c compiled past the dirty-tree guard; release tooling | #151 |
| 59 | DRV-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:449@39a3e8a "--dry-run"` | out | sign_release --dry-run checks; release tooling | #151 |
| 60 | DRV-N10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:450@39a3e8a "readdir"` | in (conditional) | readdir NULL never errno-checked, so a listing error ends as success (offloads); in if the b4 offload design lists sessions through it, otherwise out (owner answer DEC-0303); Task 3's b4 offload task settles it | Scope (in) 7 if in; #151 if out |
| 61 | DRV-N12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:451@39a3e8a "ESP_ERROR_CHECK"` | out | ESP_ERROR_CHECK in the ISR enable aborts instead of reporting; its only caller is ui_buttons_init (`components/app/ui/ui_buttons.c:37@39a3e8a "(void)board_buttons_enable_isr(s_btn_q);"`), called from ui_start (`components/app/ui/ui.c:2209@39a3e8a "ui_buttons_init();"`), run once at startup (`main/app_main.c:308@39a3e8a "ui_start();"`), before a ride | #151 |
| 62 | DRV-N16 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:452@39a3e8a "CHRG is not forced per build; a"` | out | CHRG not forced per build; release tooling | #151 |
| 63 | DRV-N17 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:453@39a3e8a "substring match keeps"` | out | dist_prune keeps -dirty images; host tooling | #151 |
| 64 | DRV-N18 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:454@39a3e8a "read_through_ses"` | out | read_through_ses loop without a cap (P10); called only by dbg sum and dbg logck (`components/drivers/export_serial/export_serial.c:768@39a3e8a "read_through_ses(argv[2]"` and `components/drivers/export_serial/export_serial.c:784@39a3e8a "read_through_ses(argv[2]"`), dbg console commands (`components/drivers/export_serial/export_serial.c:1336@39a3e8a "cmd_dbg);"`) | #151 |
| 65 | DRV-V1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:455@39a3e8a "a mid-file"` | out | a mid-file sto_read error ends dbg sum and dbg logck with a partial tally; dev console only (`components/drivers/export_serial/export_serial.c:1336@39a3e8a "cmd_dbg);"`) | #151 |
| 66 | DC-N5 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:456@39a3e8a "dbg sim laps"` | out | sim laps teleport after drag; sim build only | #151 |
| 67 | DC-N1 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:457@39a3e8a "a 0xFF frame glued to the"` | out | an OTA flash fails safe on a glued frame; OTA, not track data | #151 |
| 68 | DC-N6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:458@39a3e8a "dbg sim"` | out | dbg sim on a non-sim build replies posted rc 0; dbg console only (`components/drivers/export_serial/export_serial.c:1336@39a3e8a "cmd_dbg);"`) | #151 |
| 69 | DC-N8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:459@39a3e8a "token-initial quotes stripped from relayed"` | out | relayed tracks put strips token-initial quotes; the bundled table is compiled into the app image and not uploaded through the console (`docs/superpowers/state/decisions.yml:1437@f97ff2a "compiled into the app image"`) | #151 |
| 70 | TCI-T3 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:460@39a3e8a "rules 3/4/5 evadable by spellings with 0"` | out | linter rules 3/4/5 evadable, 0 live instances; tooling | #151 |
| 71 | TCI-T4 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:461@39a3e8a "7 of 9 fn-ptr spellings and mutual"` | out | linter fn-ptr spellings unseen, 0 live instances; tooling | #151 |
| 72 | TCI-T6 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:462@39a3e8a "5 of 6 bypass shapes pass; 0"` | out | venue_announce bypass shapes, 0 live; tooling | #151 |
| 73 | TCI-T8 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:463@39a3e8a "44 host binaries cannot observe an assertion"` | out | host binaries cannot observe an assertion trip; test honesty | #151 |
| 74 | TCI-T9 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:464@39a3e8a "gps_dropout"` | out | replay fixtures pin engine output, not truth; test honesty | #151 |
| 75 | TCI-T10 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:465@39a3e8a "nothing reads it"` | out | sim_capture.expected.json unread; test data | #151 |
| 76 | TCI-T11 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:466@39a3e8a "two tests with no assertion (rely on"` | out | two ui tests with no assertion; test honesty | #151 |
| 77 | TCI-T12 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:467@39a3e8a "coverage job green even if its ctest"` | out | coverage job green on a failed ctest; CI only | #151 |
| 78 | TCI-T13 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:468@39a3e8a "tools-tests list hard-coded"` | out | tools-tests list hard-coded; CI only | #151 |
| 79 | TCI-T15 | `docs/reviews/2026-10-16-audit-post-fix-wave.md:469@39a3e8a "exclusion-justification check accepts any comment naming the"` | out | exclusion-justification check too loose; CI only | #151 |
| 80 | F5-1 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:19@39a3e8a "list"` | out | list summary cache; op_list reads it instead of scanning .sum (F5 row) | #91 |
| 81 | F5-2 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:20@39a3e8a "0xFF"` | out | 0xFF stream-frame CRC16 verified by the dev-kit demux (F5 row), not a session-file change | #61 |
| 82 | F5-3 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:21@39a3e8a "Per-chunk OTA ACK"` | out | per-chunk OTA ACK; OTA, not track data | #57 |
| 83 | F5-4 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:22@39a3e8a "Sim clock epoch kept across resets"` | out | sim clock across resets; sim build only | #103 |
| 84 | F5-5 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:23@39a3e8a "Logger append-path reserve guard"` | in | append-path reserve guard (logs); same defect as APP-N8; owner answer DEC-0303 | Scope (in) 11; #104; Task 3 assigns |
| 85 | F5-6 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:24@39a3e8a "Boot-time reap of"` | out | boot-time reap of /sessions/*.sum.tmp; leftover files, not session content (F5 row) | #105 |
| 86 | F5-7 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:25@39a3e8a "#106 console INFO silence: confirm on the"` | out | console INFO silence; console logging, not session data | #106 |
| 87 | F5-8 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:26@39a3e8a "Task stacks: with R6's stack-low check live"` | in | task stacks: 1 h soak per env, record per-task high-water, size stacks to high-water + 25 % (F5 row); crash risk while riding (owner answer DEC-0305) | Scope (in) 12; #53 |
| 88 | F5-9 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:27@39a3e8a "Calibration wiring (design §7 #93 row)"` | in | calibration wiring (orientation capture, forward learning, persistence) is Scope (in) 2 | Scope (in) 2; b4 |
| 89 | F5-10 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:28@39a3e8a "Synth"` | out | synth venue id and sim table capacity; sim tooling | #100 |
| 90 | F5-11 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:29@39a3e8a "Rebase"` | out | rebase of branch s5.2-conn-ble, BLE not activated (F5 row); sub-project C (Scope (out)) | #135 |
| 91 | F5-12 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:30@39a3e8a "Bench checklist close: run every remaining"` | out | runs every remaining next-bench-day.md box and closes #92 (F5 row); out by owner answer DEC-0306: b6 runs Scope (in) 8 only | #92 |
| 92 | F5-13 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:31@39a3e8a "Roadmap close-out section updated with the merge"` | out | roadmap and memory record; no device output changes | roadmap F5 close-out section (`docs/superpowers/plans/2026-09-14-roadmap.md:285@39a3e8a "## Close-out wave"`) |

The skeleton's closing line records hardware-gated items that are not F5 tasks (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "Hardware-gated, recorded only:"`); they are ruled here as well, outside the 92-row count:

| # | item | source | verdict | reason | tracked |
|---|---|---|---|---|---|
| H1 | #11 M10 PPS | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#11 (M10 PPS)"` | out | PPS re-acquisition; the b2 driver keeps has_pps = 0 (DEC-0308) | #11 |
| H2 | #54 pull-up/EXT0 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#54 pull-up/EXT0 (Plan 6)"` | out | pull-up/EXT0, Plan 6 (skeleton line); Plan 6 battery pack (Scope (out)) | #54 |
| H3 | #82 | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#82 (Plan 8)"` | out | the DEC-0134 rule lists what the device measures, times, logs, offloads or exports, and #82 is the temperature-throttle producer (`docs/superpowers/plans/2026-09-14-roadmap.md:205@39a3e8a "#82 (temperature-throttle producer)"`) for a throttle that limits display refreshes (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:2384@39a3e8a "partial refreshes limited to one per 30 s, full refreshes suppressed"`); Plan 8 (skeleton line) | #82 |
| H4 | #93 real-IMU validation | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "#93 real-IMU validation (Plan 8)"` | in | on-device calibration on the real IMU is Scope (in) 2; the bench day is Scope (in) 8 | Scope (in) 2, 8; b4, b6 |
| H5 | GPS ladder | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "GPS/IMU ladders (Plan 8)"` | in | Scope (in) 1 names the GPS ladder | Scope (in) 1; b2 |
| H6 | IMU ladder | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "GPS/IMU ladders (Plan 8)"` | out | deferred with Plan 6 (DEC-0147, Scope (out)) | Plan 6 |
| H7 | BLE | `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:33@39a3e8a "BLE (sub-project C)"` | out | sub-project C (Scope (out)) | #135; roadmap sub-project C |

## Blocks (estimates; report to the owner at once past 1.5 times)
| block | content | estimate | parallel groups |
|---|---|---|---|
| b1 | scope table (F5 and audit-2 rows in/out), `docs/hardware/wiring.md` and BOM statuses, this plan's b2-b6 tasks written in full | 0.5 day | -- |
| b2 | GPS driver (8.1-8.2, GPS ladder H5) / IMU driver (8.3) / track tables and research import (Tasks 4-6) | 2.5 days (was 2: the GPS task carries the ladder, reset detection and real UART timing across driver, pipeline and supervisor, and first re-verifies the configure sequence on the u-blox 7 module) | three worktrees, disjoint files |
| b3 | CE-N1..N5 / IO-N1..N4, DRV-N1 / APP-N1, APP-N5, APP-N8 with F5-5, APP-N11 (Tasks 7-9) | 2.5 days (was 2: DEC-0303 added CE-N1 and the three logging rows; CE-N1 waits for an owner answer on the chord rule) | Task 8 in its own worktree; Tasks 7 and 9 serial (both change pipeline.c) |
| b4 | calibration (8.4, F5-9) / gate marking (F7) / offload and dev-kit storage guard, with DC-N3, DC-N4, DRV-N10 if on the offload path (Tasks 10-12) | 2.5 days (was 2: the offload design step and up to three conditional rows, DEC-0303; the dev-kit has no session store today) | Tasks 11 and 12 in two worktrees; Task 10 alone (pipeline.c, ui.c) |
| b5 | P10 cleanup of touched files after strict-delivery b7/b8; bench-doc fixes, OTA-safe helpers (DC-N7) and the log-rate measurement procedure; DRV-N4 brownout level (Tasks 13-15) | 1.5 days (unchanged: DRV-N4 is one setting with a build check; Task 13 waits for stream A) | Tasks 14 and 15 in two worktrees; Task 13 alone |
| b6 | bench day on the built prototype with the BOM inventory, a 1 h soak per env and the log-rate measurement; task stacks from the soak (F5-8); road capture and host replay (Tasks 16-18) | 2-3 days with the owner (was 1-2: DEC-0305 adds a 1 h soak per env and a re-soak after the stack change, DEC-0310 the inventory) | -- |

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

## Block b2 -- real GPS driver, real IMU driver, track tables (Tasks 4-6), branch `block/prototype-b2`

Block start: branch `block/prototype-b2` from the b1 tip (DEC-0154); register one planned row per task (prefix `PT-`, owner plan `prototype`, tasks `b2-T4`..`b2-T6`); no audit-2 row is routed to b2 (Scope table rows H5 and Scope (in) 1, 2, 5 are roadmap work, not audit rows); push the branch. The three tasks run in parallel worktrees (playbook "Session kinds" 2) and merge into the block branch one at a time, Task 6 first (it touches no firmware build input other than `trk_bundled.c`).

### Task 4: real GPS driver (roadmap 8.1-8.2)

**Files:**
- Create: `components/core/gps/ubx.c` the pure UBX layer: framing state machine, Fletcher checksum, NAV-PVT decode, the fallback four-message slot, CFG payload builders (no IDF calls, so host tests link it)
- Create: `components/core/include/core/ubx.h` its interface
- Create: `test/test_ubx.c` host suite for ubx.c (picked up by the test glob and by core_selftest)
- Modify: `components/drivers/gps_neo6m/gps_neo6m.c` the stub becomes the driver on UART2 (RX GPIO16, TX GPIO17)
- Modify: `components/drivers/gps_neo6m/CMakeLists.txt` add the UART driver dependency
- Modify: `components/lt_hal/include/hal/gps.h` per-fix serial transmit time and the reset-detected query the pipeline needs
- Modify: `components/app/pipeline/pipeline.c` real serial time into tb_on_fix, reconfigure on reset detection, the ladder command handler
- Modify: `components/app/supervisor/sup.c` the GPS fault ladder timer
- Modify: `components/app/include/app/lt_ipc.h` the ladder-step command
- Modify: `components/app/include/app/lt_err.h` the E_GPS codes the ladder logs
- Modify: `components/drivers/gps_sim/gps_sim.c` the new HAL functions for the sim build (serial time 0, never reset)
- Modify: `sdkconfig.defaults` UART ISR in IRAM (spec 7.3.2)

**Findings:** none -- roadmap 8.1-8.2 (Scope (in) 1) and Scope table row H5; no audit-2 row is routed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** PARALLEL

**Gates:** run_all

Facts this task rests on: the driver is a stub that returns -ENODEV (`components/drivers/gps_neo6m/gps_neo6m.c:34@cd84605 "R28: no receiver is attached and none can be (Plan 08)"`) and advertises `has_pps` 0 (`components/drivers/gps_neo6m/gps_neo6m.c:26@cd84605 ".has_pps     = 0,"`), which stays 0 (DEC-0308, `docs/superpowers/state/prototype/pending-decisions.yml:87@f97ff2a "{label: no-pps, cost: 'driver keeps has_pps = 0; CE-#11 and H1 stay out'}"`); no UBX parser exists in the tree (`git grep -n -i "ubx" -- components` names only comments and the stub); the pipeline passes serial time 0 to tb (`components/app/pipeline/pipeline.c:923@cd84605 "tb_on_fix(&s_tb, fix->gps_us, fix->mono_us, 0);   /* sim: no serial transmit time */"`); the ladders are absent (`components/app/supervisor/sup.c:422@cd84605 "Still absent by design, each with an owner (design §5): the GPS §7.5 and IMU §8.7"`); a live driver must set `live` 1 (`components/lt_hal/include/hal/gps.h:41@cd84605 "A live driver MUST set it to 1"`), which makes the OTA validate gate wait for a real fix (`components/lt_hal/include/hal/gps.h:35@cd84605 "wave (ruling R1): §19.4's OTA-validate gate used to require a GPS fix"`). The module is a u-blox 7-class receiver in the `gps_neo6m` slot (`docs/superpowers/plans/2026-09-14-roadmap.md:244@cd84605 "Hardware: the GPS is a G28U7FTTL-P (u-blox 7-class, UBX-G7020) in the"`); power modes use UBX commands only (DEC-0147, `docs/superpowers/state/decisions.yml:1539@f97ff2a "GPS power modes by UBX software commands only"`).

Input-domain table (the inputs the changed code reads):

| input | values or classes each test covers |
|---|---|
| UART byte stream | one whole frame; a frame split at every byte boundary; two frames in one read; leading garbage; a `$` NMEA sentence at frame start (sets reset detected); a `$` inside a payload (no reset) |
| UBX length field | 84 and 92 (NAV-PVT, spec 7.2); 0; 128; 129 (above the 128-byte buffer: back to SYNC1, frames_bad unchanged per spec 7.1) |
| checksum | correct; CK_A wrong; CK_B wrong (frames_bad + 1 each) |
| NAV-PVT fields | valid bits 0..7 (validDate, validTime, fullyResolved); fixType 0, 2, 3, 5; flags bit0 gnssFixOK 0/1; numSV 0 and 255; lat/lon at +-90/+-180 extremes; gSpeed negative, 0, 70000 mm/s; headMot 0 and 35999999; hAcc and sAcc 0 and 0xFFFFFFFF; nano negative (spec 7.2 i32) and above 999999999 |
| MON-VER swVersion | "7.03 (45969)" (has_navpvt), "6.02 (36023)" (fallback set), an empty or non-numeric string (fallback, never a crash) |
| fallback slot | all four messages with one iTOW (one fix); a new iTOW before completion (frames_partial + 1, no fix); a repeated message within one iTOW |
| ACK replies | ACK-ACK matching; ACK-NAK matching (-ENACK); an ACK for another class/id (ignored); silence (-ETIMEDOUT after 500 ms) |
| configured rate | the profile rate 5 Hz (CFG-RATE measRate 200, spec 7.3.3 step 3) |
| baud | 38400 answer to MON-VER; 9600 only (the CFG-PRT path); neither (gps_configure -EIO after three tries) |
| ladder clock | last valid frame 0, 2.9, 3, 10, 30, 60, 120 s ago; a valid frame at any step (ladder cleared) |
| power mode | GPS_PM_FULL, GPS_PM_CYCLIC_1HZ, GPS_PM_BACKUP, an out-of-range value (refused, -EINVAL) |
| serial time | len 92 and 100 at 38400 baud (len times 10 / baud, spec 7.3.6); bytes still in the ring 0 and 2048 |

- [ ] **Step 1: Re-verify the configure sequence on this module** -- roadmap 8 requires it (`docs/superpowers/plans/2026-09-14-roadmap.md:244@cd84605 "Re-verify the 8.1 configure sequence against this module's actual UBX message set"`): from the u-blox 7 receiver description (protocol 14), list in the task report every message of spec 7.2 (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1116@cd84605 "| 0x01 0x07 | NAV-PVT | 84 (u-blox 6/7) or 92 (8+, M10) | Primary fix. Parser accepts both lengths. |"`) the module supports, with the document section; a message it lacks or a payload that differs is a divergence written in the report with both quotes, and the task stops for the controller.
- [ ] **Step 2: Write the failing ubx tests** -- `test/test_ubx.c` covers every row of the input-domain table for the framing, NAV-PVT, MON-VER, fallback-slot and ACK rows through the real `components/core/gps/ubx.c` (byte-wise feed, as the driver feeds it); run `ctest -R test_ubx` in the host build and watch every case fail because ubx.c does not exist yet.
- [ ] **Step 3: Write ubx.c** -- the minimal code to pass: the state machine and checksum of spec 7.1 (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1108@cd84605 "Parser: byte-wise state machine"`), the NAV-PVT offsets of spec 7.2, the fallback slot keyed by iTOW, the CFG payload builders of spec 7.3.3 and 7.3.4; every loop bounded (Power of 10 rule 2); `ctest -R test_ubx` passes.
- [ ] **Step 4: Driver** -- `gps_neo6m.c` opens UART2 with the spec 7.3.2 settings (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1144@cd84605 "UART_NUM_2"`), runs gps_configure as spec 7.3.3 lists it, reads the UART inside gps_poll (the pipeline calls only gps_poll: `components/app/pipeline/pipeline.c:1629@cd84605 "gps_poll(&fix) == 1"`), stamps `mono_us` as spec 7.3.6 states and returns the serial transmit time with the fix; profile `live` 1, `has_pps` 0; power modes by the UBX messages of spec 7.3.4 only (no `board_gps_power` call, DEC-0147).
- [ ] **Step 5: Timing, reset detection and ladder** -- write a failing test first for each pure part: the ladder step choice from the last-valid-frame age goes into a pure function in ubx.c covered by the ladder-clock rows of `test/test_ubx.c`; then pipeline.c passes the driver's serial time to tb_on_fix, calls gps_configure when the driver reports reset detected (spec 7.3.5), and sup.c posts the ladder command at the spec 7.5 ages (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1211@cd84605 "No valid frame for 3 s"`). The 30 s power-cycle step of spec 7.5 calls `board_gps_power`, and no switch is built on GPIO26 (plan "Hardware as built"); what that step does on this board is an owner question raised by b1 Task 3's report, and this step does not write the 30 s step until the answer is recorded under a DEC id.
- [ ] **Step 6: Build and check** -- `./build.sh moto_neo6m build` and `./build.sh moto_sim build` with zero warnings; `test_apps/core_selftest` builds (it globs `test/test_*.c`); the host suite passes; `python3 tools/gates/run_all.py` only PASS. Acceptance: every input-domain row has a named test case in `test/test_ubx.c` that fails with ubx.c reverted to the empty stub and passes with it restored (transcript in the task report); the console fix line at 5 Hz on hardware (roadmap 8.1 exit) and the ladder reaching its last step with GPS TX unplugged (roadmap 8.2 exit) are b6 bench rows, run only after the owner's "ready". Note for b6: with `live` 1 an OTA image validates only after a real fix, so b6 flashes where the antenna sees the sky.

### Task 5: real IMU driver (roadmap 8.3)

**Files:**
- Create: `components/core/imu/mpu.c` the pure MPU-6050 layer: FIFO byte decode, per-sample timestamps, frozen detection, self-test factory-trim maths, temperature conversion (no IDF calls)
- Create: `components/core/include/core/mpu.h` its interface
- Create: `test/test_mpu.c` host suite for mpu.c
- Modify: `components/drivers/imu_mpu6050/imu_mpu6050.c` the stub becomes the I2C driver
- Modify: `components/drivers/imu_mpu6050/CMakeLists.txt` add the board and I2C dependencies
- Modify: `components/lt_hal/include/hal/board.h` an accessor for the shared I2C master bus
- Modify: `components/drivers/board_devkit_v1/board.c` the accessor's definition

**Findings:** none -- roadmap 8.3 (Scope (in) 2); no audit-2 row is routed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** PARALLEL

**Gates:** run_all

Facts: the driver is a stub (`components/drivers/imu_mpu6050/imu_mpu6050.c:17@cd84605 "int imu_init(void) { return -ENODEV; }"`); the I2C bus on SDA 21 / SCL 22 is created in board.c (`components/drivers/board_devkit_v1/board.c:49@cd84605 "#define PIN_I2C_SDA   GPIO_NUM_21"`, `components/drivers/board_devkit_v1/board.c:50@cd84605 "#define PIN_I2C_SCL   GPIO_NUM_22"`) and held in a file-static handle no other component can reach (`components/drivers/board_devkit_v1/board.c:67@cd84605 "static i2c_master_bus_handle_t   s_i2c_bus;"`); the pipeline already calls imu_init, imu_self_test, imu_read_fifo and imu_read_temp_c100 (`components/app/pipeline/pipeline.c:1470@cd84605 "bool imu_ok = (imu_init() == 0);"`, `components/app/pipeline/pipeline.c:1638@cd84605 "if (imu_read_fifo(raw, IMU_BATCH, &n_read, now) == 0) {"`). PARK wake, the low-power motion mode and the IMU ladder are out (DEC-0147, `docs/superpowers/state/decisions.yml:1539@f97ff2a "PARK wake, low-power motion and the IMU ladder deferred to Plan 6"`): imu_set_mode(IMU_LOWPOWER), imu_set_motion_threshold and imu_recover keep returning -ENODEV and say so in a comment citing DEC-0147.

Input-domain table:

| input | values or classes each test covers |
|---|---|
| WHO_AM_I | 0x68 (ok); 0x00, 0x72, 0xFF (-ENODEV, spec 8.3 step 1) |
| FIFO_COUNT | 0; 11 (partial sample only); 12; 84 times 12; 1000; 1001 (overflow: FIFO reset, 0 samples, spec 8.4 step 1); 1024 |
| FIFO bytes | big-endian extremes 0x7FFF and 0x8000 per axis; order AX AY AZ GX GY GZ |
| read_mono_us | n = 1 and n = 84 samples (mono_us[i] = read_mono_us - (n - 1 - i) times 10000, spec 8.4 step 5); read_mono_us below 840000 (no negative stamp) |
| frozen run | 99 and 100 consecutive equal samples (-EIO at 100, spec 8.4 step 6); one differing sample resets the run |
| self-test codes | test code 0 (FT = 0) and 1 and 31 per gyro axis; accel codes 0, 1, 31; change at -0.14, +0.14 (pass) and +-0.141 (fail) |
| TEMP_OUT raw | -32768, -12412 (0 C), 0, 32767 (T = raw / 340 + 36.53, spec 8.2) |
| I2C transaction result | ESP_OK; ESP_FAIL; ESP_ERR_TIMEOUT (each returns -EIO from the driver; the retry ladder is out with DEC-0147) |
| imu_set_mode | IMU_FULL (ok), IMU_LOWPOWER (-ENODEV, out), any other value (-EINVAL) |

- [ ] **Step 1: Write the failing mpu tests** -- `test/test_mpu.c` covers every row above that mpu.c owns (FIFO decode, timestamps, frozen, self-test maths, temperature) through the real `components/core/imu/mpu.c`; watch them fail because mpu.c does not exist.
- [ ] **Step 2: Write mpu.c** -- the minimal code to pass, with the spec 8.4 and 8.5 rules (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1258@cd84605 "### 8.4 FIFO read"`, `docs/superpowers/specs/2026-09-14-lap-timer-design.md:1266@cd84605 "### 8.5 Self-test"`); every loop bounded.
- [ ] **Step 3: Driver** -- `board_i2c_bus()` returns the handle (NULL before board_init); imu_mpu6050.c adds the 0x68 device at 400 kHz (spec 8.1), runs the spec 8.3 init sequence, reads the FIFO in one burst, runs the self-test, reads TEMP_OUT, and feeds every byte through mpu.c; profile constants match the raw scales in `components/core/include/core/types.h:32@cd84605 "int16_t ax, ay, az;"`.
- [ ] **Step 4: Build and check** -- `./build.sh moto_neo6m build` and `./build.sh moto_sim build` with zero warnings; core_selftest builds; host suite passes; run_all only PASS. Acceptance: each input-domain row has a named case that fails with mpu.c reverted to an empty body and passes restored (transcript in the report); 100 Hz samples on the console and the logged self-test result (roadmap 8.3 exit, `docs/superpowers/plans/2026-09-14-roadmap.md:250@cd84605 "100 Hz samples on console; self-test result logged"`) are b6 bench rows, after the owner's "ready".

### Task 6: track tables and research import (Scope (in) 5)

**Files:**
- Create: `tools/tracks/research-2026-10-29.md` the track research imported beside the table, with its sources and the unknown fields of Killarney Full Reverse named
- Create: `tools/tracks/kyalami.json` Kyalami, id 1, layout GP
- Modify: `tools/tracks/killarney.json` centre and lines from the research
- Modify: `components/core/tracks/trk_bundled.c` regenerated by gen_tracks.py, never hand-edited
- Modify: `tools/tracks/test_gen_tracks.py` generator cases for the new rows
- Modify: `test/test_trk.c` auto-detect cases through trk_find_nearest over the bundled table

**Findings:** none -- Scope (in) 5 (DEC-0136, DEC-0141); no audit-2 row is routed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** PARALLEL

**Gates:** run_all

Facts: the bundled Killarney centre is `components/core/tracks/trk_bundled.c:5@cd84605 ".lat = -33.8567, .lon = 18.517, .radius_m = 2000"` (generated: `components/core/tracks/trk_bundled.c:1@cd84605 "GENERATED by tools/tracks/gen_tracks.py"`); auto-detect is `trk_find_nearest` (`components/core/include/core/trk.h:95@cd84605 "const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out);"`). The research proposes the corrected rows (`docs/superpowers/state/program/sources/tracks-research.md:81@cd84605 "## 5. Proposed contents (generator format; validated in scratch)"`) and records Full Reverse's unknown fields (`docs/superpowers/state/program/sources/tracks-research.md:138@cd84605 "## 8. Killarney Full Reverse (follow-up, 2026-10-29)"`). Owner answers used: DEC-0136 (`docs/superpowers/state/decisions.yml:1439@f97ff2a "add Kyalami as a third venue for the auto-detect test"`) and DEC-0141 (`docs/superpowers/state/decisions.yml:1484@f97ff2a "Kyalami id 1, layout GP, 4522 m; Killarney Full Reverse modelled as the real reverse layout"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| fix position for trk_find_nearest | the Killarney S/F midpoint of research section 5; the old bundled centre -33.8567, 18.517 (about 3.8 km south of the circuit, plan Review Focus 2); the Kyalami S/F midpoint; Zwartkops' centre from `tools/tracks/zwartkops.json`; a point 1999 m and 2001 m from the new Killarney centre; a point in mid-ocean (no venue) |
| venue id | 1 (Kyalami), 2 (Zwartkops), 6 (Killarney); a user venue with id 1 (user beats bundled on an id clash, as the trk_find_nearest declaration cited above states) |
| JSON row shape | sectors list empty (Short layouts); "sf": "same" and "sectors": "reverse" (Full Reverse); a line shorter than 30 m (generator refuses) |
| verified flag | false on every Killarney and Kyalami row |

- [ ] **Step 1: Import the research** -- copy `docs/superpowers/state/program/sources/tracks-research.md` to `tools/tracks/research-2026-10-29.md` with a header naming the source path and commit; in its section 8 list each unknown Full Reverse field (chicane ends, lap length, timing line, sector splits) as UNKNOWN with the research row it comes from; no coordinate is added that the research does not give.
- [ ] **Step 2: Write the failing tests** -- `test/test_trk.c` cases for every fix-position and venue-id row above against the bundled table (a fix at the Killarney S/F finds venue 6 and not 1; a fix at Kyalami finds 1; the old centre finds no venue within 2000 m of the new centre only if the distance says so, computed in the test from the two coordinates); `tools/tracks/test_gen_tracks.py` cases for the JSON-shape rows; watch them fail on the current table.
- [ ] **Step 3: Write the rows** -- `killarney.json` and `kyalami.json` take the research section 5 values verbatim (Full Reverse keeps the research's `"sf": "same"`, `"sectors": "reverse"` and length 3267, each marked an assumption in the imported research, `verified` false); regenerate with `python3 -I tools/tracks/gen_tracks.py tools/tracks/killarney.json tools/tracks/kyalami.json tools/tracks/zwartkops.json -o components/core/tracks/trk_bundled.c`; the tests pass.
- [ ] **Step 4: Check** -- the spec 10.2 example that uses the old Killarney centre is listed in the task report with its line (`git grep -n "33.8567" docs/superpowers/specs/2026-09-14-lap-timer-design.md`) and corrected in the spec (stream B owns it); `./build.sh moto_neo6m build` with zero warnings; host suite and `python3 -m unittest tools.tracks.test_gen_tracks` pass; run_all only PASS. Acceptance: every input-domain row has a named case; each case fails on the old table and passes on the new one (transcript in the report).
- [ ] **Step 5: CHECKPOINT** -- merge the three worktrees into `block/prototype-b2` one at a time with the D34 set after each; the umbrella "Checkpoint protocol for this plan" with one opus final reviewer and one final verifier over the block (DEC-0145); the block PR follows the b1 merge rule (Global Constraints); append `CHECKPOINT b2 <sha>` to `docs/superpowers/state/prototype/ledger.md`; update `docs/superpowers/state/prototype/handoff.md` (next: b3 Task 7) and stream B's board row; stop for `/clear`.

## Block b3 -- engine defects, exports, logging rows (Tasks 7-9), branch `block/prototype-b3`

Block start: branch `block/prototype-b3` from the b2 tip; register these rows (prefix `PT-`, owner plan `prototype`), then route each into its task's Findings line (gate_plan then requires a pin step naming a test of the form test_<module>::test_<name>):
- task b3-T7: `PT-CE-N2` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "writes the Doppler speed of the fix EPOCH into"`; CE-V-3 merged into it, `docs/reviews/2026-10-16-audit-post-fix-wave.md:431@39a3e8a "every drag test has"`), `PT-CE-N3` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:391@39a3e8a "on a RUNNING, locked engine moves"`), `PT-CE-N4` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:392@39a3e8a "Lap best, prev and the predictive reference survive a venue change"`), `PT-CE-N5` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:393@39a3e8a "Production fusion is never oriented"`), `PT-CE-N1` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:399@39a3e8a "A chord of ABSENT (undelivered) fixes shorter"`).
- task b3-T8: `PT-IO-N1` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:382@39a3e8a "A CRC-bad LAP frame is absent from"`), `PT-IO-N2` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:381@39a3e8a "One CRC-bad FIX_DELTA is dropped by the"`), `PT-IO-N3` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:383@39a3e8a "although the never-evicted"`), `PT-IO-N4` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:384@39a3e8a "VBO never carries"`), `PT-DRV-N1` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:386@39a3e8a "sign_release builds incrementally and never reconfigures"`).
- task b3-T9: `PT-APP-N1` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:385@39a3e8a "carries an empty VENUE name in its"`), `PT-APP-N5` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:408@39a3e8a "one dropped edge keeps FIX/FUSED suppressed while riding"`), `PT-APP-N8` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "Append path has no reserve check; one spurious"`; F5-5 is the same defect, `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:23@39a3e8a "Logger append-path reserve guard"`), `PT-APP-N11` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:412@39a3e8a "drops a LAP result with no errlog or counter"`).

Task 8 runs in its own worktree beside Task 7; Task 9 starts after Task 7 has merged (both change pipeline.c).

### Task 7: engine defects CE-N1..CE-N5 (Scope (in) 3)

**Files:**
- Modify: `components/core/dragengine/drag.c` CE-N2 re-anchor at the fix epoch
- Modify: `components/core/lapengine/lap.c` CE-N3 forced layout, CE-N4 venue change, CE-N1 chord rule
- Modify: `components/app/pipeline/pipeline.c` CE-N5 the CMD_CALIB_ORIENT call to the orientation capture
- Modify: `test/test_drag.c` CE-N2 latency sweep and CE-N5 pitched-mount cases
- Modify: `test/test_lap.c` CE-N1, CE-N3, CE-N4 cases
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` section 6.4 if the CE-N1 owner answer changes the chord rule

**Findings:** none -- routes CE-N1, CE-N2, CE-N3, CE-N4, CE-N5 once registered as PT- rows at the block start

**Siblings:** none -- the rows are registered at the block start; their siblings are added here then

**Mode:** SERIAL

**Gates:** run_all

Defects (audit rows, 39a3e8a): CE-N2 `docs/reviews/2026-10-16-audit-post-fix-wave.md:390@39a3e8a "every speed gate is late by the fix latency"` at `components/core/dragengine/drag.c:222@cd84605 "D->v_est  = v;"`; CE-N3 `docs/reviews/2026-10-16-audit-post-fix-wave.md:391@39a3e8a "on a RUNNING, locked engine moves"` at `components/core/lapengine/lap.c:219@cd84605 "L->forced_layout_id = layout_id;"`; CE-N4 `docs/reviews/2026-10-16-audit-post-fix-wave.md:392@39a3e8a "Lap best, prev and the predictive reference survive a venue change"`; CE-N5 `docs/reviews/2026-10-16-audit-post-fix-wave.md:393@39a3e8a "Production fusion is never oriented"`, with the pipeline dropping the command (`components/app/pipeline/pipeline.c:1441@cd84605 "break;                              /* CALIB_ORIENT: later sessions */"`) that the menu sends (`components/app/ui/ui.c:629@cd84605 "case MA_CALIBRATE: ui_send_cmd(CMD_CALIB_ORIENT, 0, 0);"`); CE-N1 `docs/reviews/2026-10-16-audit-post-fix-wave.md:399@39a3e8a "A chord of ABSENT (undelivered) fixes shorter than 5 s still evaluates crossings"` at `components/core/lapengine/lap.c:1172@cd84605 "const bool seg_gap = ((now - L->prev_gps_us) > LAP_SEG_GAP_US) || L->seg_has_invalid;"`, which spec 6.4 allows today (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1045@cd84605 "(5 s) made of absent fixes"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| fix latency (arrival minus epoch) | 0, 30, 60, 90, 120 ms (the audit's CE-N2 sweep) and 200 ms (one 5 Hz period) |
| fix rate | 5 Hz with +-20 ms jitter (Review Focus 1: a real-rate stream, not the ideal sim one) |
| drag speed at the fix | below, at and above each speed gate; 0 m/s; negative gSpeed (clamped to 0: `components/core/dragengine/drag.c:221@cd84605 "if (v < 0.0) v = 0.0;"`) |
| forced layout | 0 (auto); the locked layout; another layout of the venue; a layout id the venue lacks (-1, unchanged); forced while LAP_ST_RUNNING and while not running |
| venue change | scan finds venue B after VALID laps at venue A; the same venue re-found; venue lost then A re-found |
| mount pitch | 0, 3.5, 4.0, 4.06, 5.0, 15 deg, before and after the orientation capture |
| absent-fix chord length | 0.2, 0.4, 1.0, 2.0, 4.9, 5.0, 5.1 s; a chord holding one invalid fix |

- [ ] **Step 1: Write the failing tests** -- through the real engines: `test/test_drag.c` feeds a 5 Hz jittered fix stream with every latency row to `drag_on_fix` and the fused stream to `drag_on_fused`, asserting each gate time within the spec 6.6 bound of +-5 ms; it also captures orientation with `fus_calib_orient_capture` on each pitched-mount row and asserts drag arms; `test/test_lap.c` covers the forced-layout, venue-change and chord rows through `lap_on_fix`. Watch each fail for the audit's stated reason.
- [ ] **Step 2: CE-N1 rule** -- the threshold below 5 s at which an absent-fix chord flags LAP_F_GPS_LOST is a spec 6.4 change no owner answer gives yet; b1 Task 3's report raises it, and this step waits for the answer recorded under a DEC id, then amends spec 6.4 with that DEC id and writes the minimal lap.c change.
- [ ] **Step 3: Minimal code for CE-N2..N5** -- drag.c re-anchors `v_est` at the fix epoch and integrates fused samples from the epoch to arrival; lap.c `lap_force_layout` on a locked running engine moves the lock, sector set and exported layout id (spec 10.5 "locks immediately"); lap.c clears best, prev and the predictive reference on every venue change; pipeline.c handles CMD_CALIB_ORIENT by calling `fus_calib_orient_capture` and posting the result (the persistence, forward learning and the ui one-shot are b4 Task 10). The tests pass.
- [ ] **Step 4: Pin each finding** -- for each of PT-CE-N1..N5 name the pinning test (the form `test_drag::test_<name>` or `test_lap::test_<name>`) and write its mutation transcript at `docs/superpowers/state/prototype/mutations/PT-CE-N<k>.txt` (fix present PASS, fix reverted FAIL, fix restored PASS); the pipeline.c call site of CE-N5 has no host build, so its check is the b6 bench row "Calibrate from the menu, then arm drag on a pitched mount".
- [ ] **Step 5: Check** -- `./build.sh moto_neo6m build` and `./build.sh moto_sim build` with zero warnings; core_selftest builds; the replay fixtures under `test/data/` still match (`ctest -R test_lap`); run_all only PASS. Acceptance: every input-domain row has a named case; the five transcripts exist.

### Task 8: exports IO-N1..IO-N4 and DRV-N1 (Scope (in) 4)

**Files:**
- Modify: `components/core/session/ses_records.c` IO-N2 no delta applied across a dropped frame
- Modify: `components/core/session/ses_frame.c` IO-N2 the bad-frame count the reader exposes
- Modify: `components/core/export/exp_json.c` IO-N1 skipped counts CRC-bad frames; IO-N3 summary from the .sum when the .log is gone
- Modify: `components/core/export/exp_vbo.c` IO-N4 laptiming section and layout
- Modify: `components/core/include/core/exp.h` the export metadata the fixes need
- Modify: `components/app/cmd/cmd.c` IO-N2 export reports a damaged log; IO-N3 json from the .sum; IO-N4 S/F and layout from the tracks table
- Modify: `test/test_exp_nmea_json.c` IO-N1, IO-N2, IO-N3 cases
- Modify: `test/test_exp_vbo.c` IO-N4 cases
- Modify: `test/test_ses_records.c` IO-N2 reader cases
- Modify: `CMakeLists.txt` DRV-N1 version taken at build, not only at configure
- Modify: `tools/sign_release.sh` DRV-N1 reconfigure before a signed build
- Modify: `tools/test_sign_release.sh` DRV-N1 stale-version case

**Findings:** none -- routes IO-N1, IO-N2, IO-N3, IO-N4, DRV-N1 once registered as PT- rows at the block start

**Siblings:** none -- the rows are registered at the block start; their siblings are added here then

**Mode:** PARALLEL

**Gates:** run_all

Defects (audit rows, 39a3e8a): IO-N2 `docs/reviews/2026-10-16-audit-post-fix-wave.md:381@39a3e8a "every later delta is applied to the un-advanced anchor"` (reader at `components/core/session/ses_records.c:125@cd84605 "if (!st->have_prev) return -1;"`); IO-N1 `docs/reviews/2026-10-16-audit-post-fix-wave.md:382@39a3e8a "A CRC-bad LAP frame is absent from the JSON summary"` (tail at `components/core/export/exp_json.c:233@cd84605 "(unsigned)e->skipped)"`); IO-N3 `docs/reviews/2026-10-16-audit-post-fix-wave.md:383@39a3e8a "although the never-evicted"` (`components/app/cmd/cmd.c:1007@cd84605 "open: no .log"`); IO-N4 `docs/reviews/2026-10-16-audit-post-fix-wave.md:384@39a3e8a "VBO never carries"` (`components/app/cmd/cmd.c:998@cd84605 "s_meta.has_sf = 0;"`); DRV-N1 `docs/reviews/2026-10-16-audit-post-fix-wave.md:386@39a3e8a "sign_release builds incrementally and never reconfigures"` (`CMakeLists.txt:174@cd84605 "COMMAND git describe --tags"`, `tools/sign_release.sh:94@cd84605 "./build.sh"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| .log frame sequence | all good; one CRC-bad FIX_DELTA mid-run; a CRC-bad FIX_KEY; a CRC-bad LAP; two adjacent bad frames; a bad final frame; a truncated final frame |
| .sum state | present and good; present with a bad LAP frame; absent (IO-V4 is out, DEC-0303: the case only asserts no crash) |
| .log state | present; evicted (only the .sum left, the logger's eviction deletes the .log only); both absent |
| export format | vbo, nmea, json |
| layout | locked layout with S/F known; venue with no layout locked; a user venue; a venue id no table holds |
| build state for DRV-N1 | clean tree after a new commit; a new tag; a dirty tree; an unchanged tree (no rebuild of the version) |

- [ ] **Step 1: Write the failing tests** -- through the real core reader and exporters on fixture .log and .sum bytes built in the test (`test/test_ses_records.c`, `test/test_exp_nmea_json.c`, `test/test_exp_vbo.c`), one case per row above; `tools/test_sign_release.sh` gains the stale-version case run through the real `tools/sign_release.sh` in a scratch clone; watch each fail for the audit's reason.
- [ ] **Step 2: Minimal code** -- the reader never applies a delta to an anchor a dropped frame left behind, and exposes the bad-frame count; the export reports a damaged log instead of SUCCESS (the reply text the spec 14.3 export section states; a reply shape the spec does not state is a question to the controller); the json summary counts every CRC-bad frame in `skipped` and reads the .sum when the .log is gone; cmd.c fills the S/F and layout from `trk_get` for the locked layout; CMakeLists.txt and sign_release.sh make a signed build carry the `git describe` of the tree it signs.
- [ ] **Step 3: Pin each finding** -- for each of PT-IO-N1..N4 and PT-DRV-N1 name the pinning test (`test_exp_nmea_json::test_<name>`, `test_exp_vbo::test_<name>`, `test_ses_records::test_<name>`, or the sign_release case) and write `docs/superpowers/state/prototype/mutations/<id>.txt`; the cmd.c call sites have no host build, so their check is the b6 bench rows that export an evicted session as json and a locked-layout session as VBO.
- [ ] **Step 4: Check** -- builds with zero warnings; core_selftest builds; host suite and `bash tools/test_sign_release.sh` pass; run_all only PASS. Acceptance: every input-domain row has a named case; the five transcripts exist.

### Task 9: logging rows APP-N1, APP-N5, APP-N8 with F5-5, APP-N11 (Scope (in) 4, 11)

**Files:**
- Modify: `components/app/pipeline/pipeline.c` APP-N1 venue name on every session open; APP-N5 motion state re-sent with the fix stream
- Modify: `components/app/logger/logger.c` APP-N1 venue name kept across rotation; APP-N8 reserve check on the append path; APP-N11 a dropped LAP result is retried and counted
- Modify: `components/app/logger/log_pause.c` APP-N5 a moving fix ends the pause even when the edge was dropped
- Modify: `components/app/include/app/log_pause.h` its interface
- Create: `components/app/logger/log_reserve.c` APP-N8 the pure append-reserve decision logger.c calls
- Create: `components/app/include/app/log_reserve.h` its interface
- Create: `test/test_log_reserve.c` host suite linking the real log_reserve.c
- Modify: `test/test_log_pause.c` APP-N5 cases
- Modify: `test/CMakeLists.txt` the test_log_reserve link line (log_pause's pattern)
- Modify: `test_apps/core_selftest/main/CMakeLists.txt` the selftest link for the new app source

**Findings:** none -- routes APP-N1, APP-N5, APP-N8, APP-N11 once registered as PT- rows at the block start (F5-5 is the same defect as APP-N8, Scope table row 84)

**Siblings:** none -- the rows are registered at the block start; their siblings are added here then

**Mode:** SERIAL

**Gates:** run_all

Defects (audit rows, 39a3e8a): APP-N1 `docs/reviews/2026-10-16-audit-post-fix-wave.md:385@39a3e8a "carries an empty VENUE name in its"` (`components/app/logger/logger.c:401@cd84605 "s_venue_name[0] = 0;"`); APP-N5 `docs/reviews/2026-10-16-audit-post-fix-wave.md:408@39a3e8a "one dropped edge keeps FIX/FUSED suppressed while riding"` (`components/app/pipeline/pipeline.c:249@cd84605 "if (g_evt_q) { (void)xQueueSend(g_evt_q, ev, 0); logger_notify(); }"`); APP-N8 `docs/reviews/2026-10-16-audit-post-fix-wave.md:411@39a3e8a "Append path has no reserve check; one spurious"` (`components/app/logger/logger.c:223@cd84605 "if (sto_write(s_log_fd, s_batch, s_batch_len) != 0)"`, `components/app/logger/logger.c:1074@cd84605 "while (si.free_kb < (si.total_kb * STO_TARGET_PCT) / 100u && pass < EVICT_MAX_PER_PASS) {"`); APP-N11 `docs/reviews/2026-10-16-audit-post-fix-wave.md:412@39a3e8a "drops a LAP result with no errlog or counter"` (`components/app/logger/logger.c:1278@cd84605 "if (xQueueSend(g_result_q, r, 0) == pdTRUE) logger_notify();"`, depth `components/app/include/app/lt_ipc.h:122@cd84605 "#define RESULT_Q_DEPTH 4"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| session open cause | first venue find; mode rotation; deferred rotation; the 60 s RETRY (the four APP-N1 paths) |
| motion edges | EV_MOTION delivered; EV_MOTION dropped (queue full) while a moving fix arrives; EV_STILL dropped; still held 9.9 s and 10 s (LOG_PAUSE_STILL_S) |
| sto_info free_kb | 0 once then normal (spurious); 0 steadily; exactly the reserve; one below; total_kb 0 (read failure) |
| result queue | 0 to 3 queued (send ok); 4 queued (full) with a LAP; full with a DRAG result |

- [ ] **Step 1: Write the failing tests** -- `test/test_log_pause.c` drives the real log_pause.c with each motion-edge row and asserts the pause ends on a moving fix even after a dropped EV_MOTION; `test/test_log_reserve.c` drives the real log_reserve.c with each free_kb row and asserts one spurious 0 evicts nothing; watch them fail.
- [ ] **Step 2: Minimal code** -- pipeline.c sets the venue name on every one of the four open paths and gives the logger the moving state with each fix; logger.c calls log_reserve before evicting and log_pause on each fix; a LAP result that finds the queue full is retried within the logger's bounded wait and, if still refused, logged with an errlog code and counted (the code and its value are appended to `components/app/include/app/lt_err.h` only if no existing code fits; the file is append-only, `components/app/include/app/lt_err.h:6@cd84605 "are APPEND-ONLY -- never renumber one."`).
- [ ] **Step 3: Pin each finding** -- PT-APP-N5 by `test_log_pause::test_<name>` and PT-APP-N8 by `test_log_reserve::test_<name>`, each with its transcript under `docs/superpowers/state/prototype/mutations/`; PT-APP-N1 and PT-APP-N11 change pipeline.c and logger.c only, which have no host build: their checks are b6 bench rows (a rotated session's `list` shows the venue name; a forced full result queue leaves an errlog entry through the dev console), recorded with the bench transcript as the mutation evidence.
- [ ] **Step 4: Check** -- builds with zero warnings; core_selftest builds; host suite passes; run_all only PASS. Acceptance: every input-domain row has a named case or a named b6 bench row.
- [ ] **Step 5: CHECKPOINT** -- merge Task 8's worktree, then the SERIAL tasks in order, with the D34 set after each; the umbrella checkpoint protocol with one opus final reviewer and one final verifier (DEC-0145); append `CHECKPOINT b3 <sha>` to the ledger; update the handoff (next: b4 Task 10) and stream B's board row; stop for `/clear`.

## Block b4 -- calibration, gate marking, offload and dev-kit storage guard (Tasks 10-12), branch `block/prototype-b4`

Block start: branch `block/prototype-b4` from the b3 tip; register `PT-F5-9` (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:27@39a3e8a "Calibration wiring (design §7 #93 row)"`, task b4-T10) and, if Task 12 Step 1 puts them on the offload path, `PT-DC-N3` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:419@39a3e8a "Relay rc is 0 whenever any byte arrived"`), `PT-DC-N4` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:421@39a3e8a "One 20 s wait for every"`) and `PT-DRV-N10` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:450@39a3e8a "NULL never errno-checked"`), task b4-T12; push the branch. Tasks 11 and 12 run in parallel worktrees; Task 10 runs alone.

### Task 10: on-device calibration (roadmap 8.4 calibration only, F5-9, H4)

**Files:**
- Modify: `components/app/pipeline/pipeline.c` forward learning per fix and the persist call on success
- Modify: `components/app/sys/lt_nvs.c` the calibration blob read and write
- Modify: `components/app/include/app/lt_nvs.h` its interface
- Modify: `components/app/ui/ui.c` the CALIBRATE one-shot OK or FAIL
- Modify: `test/test_fus_orient.c` capture and persistence round trip through the real fusion
- Modify: `test/test_fus_fwd.c` forward learning on a real-rate stream

**Findings:** none -- routes F5-9 once registered as a PT- row at the block start; H4 (Scope table) is validated on the b6 bench

**Siblings:** none -- the row is registered at the block start; its siblings are added here then

**Mode:** SERIAL

**Gates:** run_all

Facts: the F5 row's wiring (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:27@39a3e8a "forward learning per fix, persisted on success"`); `fus_calib_forward_step` has no app caller (`git grep -n "fus_calib_forward_step" -- components/app main` prints nothing; its definition is `components/core/fusion/fus.c:417@cd84605 "int fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps)"`); the session header carries the live calibration (`components/app/pipeline/pipeline.c:541@cd84605 "Values are fus_calib_defaults() until Plan 8 captures a real orientation (#93)"`). Scope is calibration only (DEC-0147, `docs/superpowers/state/decisions.yml:1539@f97ff2a "roadmap 8.4 calibration only (orientation capture, forward learning)"`). The orientation capture call itself lands in b3 Task 7 (CE-N5).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| still window at capture | still for the full window; motion inside it (capture refused, FAIL card); IMU absent (FAIL) |
| GPS acceleration for forward learning | below and above the learning threshold; yaw rate high (window rejected); 5 Hz with jitter |
| NVS blob | absent (defaults); valid; wrong version; CRC-bad; short read |
| reboot | calibration persisted then reboot (loaded, SESSION_HDR carries it) |

- [ ] **Step 1: Write the failing tests** -- through the real fusion (`test/test_fus_orient.c`, `test/test_fus_fwd.c`): capture, forward learning to `fus_fwd_ready`, then `fus_calib_to_ses` and `fus_calib_from_ses` round trip, for each row above that core owns; watch them fail.
- [ ] **Step 2: Minimal code** -- pipeline.c calls `fus_calib_forward_step` on each valid fix and persists through lt_nvs on success; boot loads the blob (defaults when absent or bad, with an errlog entry for bad); ui.c shows OK or FAIL from the result the pipeline posts; each persist writes a CALIB record (logger_submit_calib, already present).
- [ ] **Step 3: Check** -- builds with zero warnings; core_selftest builds; host suite passes; run_all only PASS. Acceptance: every row has a named host case or a named b6 bench row (capture on the real IMU, reboot, `status` shows the stored calibration; H4).

### Task 11: on-device gate marking with real GPS (spec F7, DEC-0137)

**Files:**
- Create: `test/test_lap_mark_real.c` lap_mark_gate and the lap engine on a 5 Hz jittered stream
- Create: `docs/bench/gate-marking.md` the on-track marking procedure and its read-back check
- Modify: `components/core/lapengine/lap.c` only if the new test shows a defect; the defect is then listed in the report first

**Findings:** none -- spec F7 (Scope (in) 6); no audit row is routed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** PARALLEL

**Gates:** run_all

Facts: F7 is built and sim-tested: the pipeline marks the gate it expects next from the last fresh valid fix (`components/app/pipeline/pipeline.c:1339@cd84605 "if (fresh && lap_mark_gate(&s_lap, idx, &s_last_fix, NULL) == 0) {"`); the save is fire-and-forget (`components/app/pipeline/pipeline.c:445@cd84605 "if (g_log_req_q != NULL && xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100)) == pdTRUE) {"`). A dropped save or notify leaves the success card (APP-N10), and APP-N10 is out (DEC-0303, `docs/superpowers/state/prototype/pending-decisions.yml:43@f97ff2a "out: the rest)"`), so this task does not rely on the card: the procedure confirms a save by reading the table back after a reboot. Owner answer DEC-0137 (`docs/superpowers/state/decisions.yml:1448@f97ff2a "F7 built and bench-tested as backup and as the on-track confirmation of the researched lines"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| fix stream while marking | 5 Hz with +-20 ms jitter; a 1 s dropout just before the press (stale fix refused); speed 0, 5, 30 m/s |
| press order | S/F, then sectors 1..n; a press beyond LAP_MAX_SECTORS (refused, full) |
| heading at the press | 0, 90, 179, 359 deg (the 30 m line is perpendicular to heading, spec 6.4 step 6) |
| laps after creation | three laps over the marked lines; a lap in the reverse direction (no crossing) |
| save read-back | table read back after reboot holds the venue; table read back lacks it (procedure reports NOT SAVED) |

- [ ] **Step 1: Write the test** -- `test/test_lap_mark_real.c` drives the real lap.c with each stream row, marks S/F and two sectors, then times three laps over the marked lines and asserts the lap times against the synthesised truth within one fix period; run it. A failing case is a defect: list it in the report with file:line, then fix lap.c test-first.
- [ ] **Step 2: Procedure** -- `docs/bench/gate-marking.md`: the on-track steps (menu New track, press at S/F and each sector at speed), then the read-back (reboot, `tracks get` through the dev-kit, the new venue present with its lines); every flash or console step on hardware waits for the owner's "ready"; never open the lap-timer port within 35 s of an OTA push.
- [ ] **Step 3: Check** -- host suite passes; core_selftest builds; run_all only PASS. Acceptance: every input-domain row has a named case or a named procedure step.

### Task 12: session offload and dev-kit storage guard (Scope (in) 7, DEC-0138, DEC-0312)

**Files:**
- Create: `devcontroller/components/offload/offload.c` the offload job: free-space check, download into the dev-kit store, verify, then delete on the lap timer
- Create: `devcontroller/components/offload/include/offload.h` its interface
- Create: `devcontroller/components/offload/host/offload_guard.c` the pure guard and verify decisions offload.c calls
- Create: `devcontroller/components/offload/CMakeLists.txt` the component
- Create: `devcontroller/test/test_offload_guard.c` host suite linking the real offload_guard.c
- Modify: `devcontroller/test/CMakeLists.txt` the test's link line
- Modify: `devcontroller/components/devconsole/cmd_lt.c` changed only if Step 1 puts DC-N3 on the offload path
- Modify: `tools/devkit.py` the dev-kit to laptop export verb; the per-verb wait only if Step 1 puts DC-N4 on the path
- Modify: `tools/test_devkit.py` cases for the export verb and, if on the path, the DC-N4 wait
- Modify: `components/drivers/storage_internal/storage_internal.c` changed only if Step 1 puts DRV-N10 on the path

**Findings:** none -- routes DC-N3, DC-N4, DRV-N10 only if Step 1 puts them on the offload path (registered then at the block start); otherwise they are out, tracked #151 (DEC-0303)

**Siblings:** none -- no register row is routed to this task until Step 1 settles the conditional rows

**Mode:** PARALLEL

**Gates:** run_all

Facts: the dev-kit already streams a session download (`devcontroller/components/linkhost/linkhost.c:424@cd84605 "int linkhost_download_cmd(const char *cmd, bool is_binary, lh_dl_chunk_cb chunk_cb, void *ctx,"`); its only store is the rotating black-box log, which deletes its oldest files to fit (`devcontroller/components/logstore/logstore.c:206@cd84605 "static void enforce_cap(uint32_t incoming_bytes)"`) on a 0xF0000-byte partition (`devcontroller/partitions.csv:9@cd84605 "logs,       data, spiffs,   0x310000, 0xF0000,"`); the lap timer lists sessions through `sto_list_open` (`components/app/cmd/cmd.c:646@cd84605 "if (sto_list_open(&it,"`), whose loop never checks errno after readdir (`components/drivers/storage_internal/storage_internal.c:265@cd84605 "while (it->iters++ < STO_LIST_MAX_ENTRIES && (ent = readdir(it->d)) != NULL) {"`); the relay's rc is 0 on any byte (`devcontroller/components/devconsole/cmd_lt.c:530@cd84605 "int rc = (total > 0) ? 0 : LINKHOST_E_TIMEOUT;"`); devkit.py waits 20 s per `lt` verb (`tools/devkit.py:84@cd84605 "LT_TIMEOUT_S = 20.0"`). Owner answers: DEC-0138 (`docs/superpowers/state/decisions.yml:1457@f97ff2a "with a dev-kit storage guard (free-space check, refuse rather than fill, delete on the lap timer only after a verified copy) and dev-kit to laptop export"`); DEC-0312 (`docs/superpowers/state/prototype/pending-decisions.yml:123@f97ff2a "the dev-kit stays in reset or off RX0 while riding; offload only between sessions"`); DEC-0303 (`docs/superpowers/state/prototype/pending-decisions.yml:41@f97ff2a "DC-N3, DC-N4, DRV-N10 once the b4 offload design shows them on its path"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| dev-kit free space vs session size | free above size plus the guard margin; exactly at it; one byte below (refused, nothing written, nothing deleted); free-space read error (refused) |
| lap-timer session list | 0, 1, many sessions; a listing error (DRV-N10 class, if on the path); a session open now (never offloaded) |
| download result | SUCCESS with a valid CRC32; CRC32 mismatch; truncated body; timeout; an ERR reply |
| verify | dev-kit copy re-read CRC32 equals the lap timer's; differs (no delete) |
| delete on the lap timer | ok; refused (copy kept, session kept, reported) |
| link state | dev-kit link detect wire present (lap-timer GPIO4 to dev-kit GND, plan "Hardware as built"); absent (offload does not start) |
| laptop export | one session; all sessions; dev-kit store empty |

- [ ] **Step 1: Design with citations** -- write in the task report which transport the offload uses, with a citation for each choice: (a) does the offload relay `lt` verbs through cmd_lt.c (DC-N3 on the path) or call linkhost_download_cmd directly (DC-N3 off); (b) does the dev-kit to laptop export use devkit.py's `lt` relay with its single 20 s wait (DC-N4 on) or a dedicated verb with its own bound (DC-N4 off); (c) does the offload list sessions through LIST, which reads the sessions directory through `sto_list_open` (DRV-N10 on) or not. Each row on the path is fixed in this task, test-first, with its PT- row registered and pinned (`test_offload_guard::test_<name>` or a `tools/test_devkit.py` case) and its transcript under `docs/superpowers/state/prototype/mutations/`; each row off the path is written in the report as out, tracked #151. The offload runs only between sessions with the dev-kit off lap-timer RX0 while riding (DEC-0312); the dev-kit never deletes black-box files to make room for a session.
- [ ] **Step 2: Write the failing tests** -- `devcontroller/test/test_offload_guard.c` drives the real offload_guard.c with each free-space, download, verify and delete row; `tools/test_devkit.py` covers the laptop export rows; watch them fail.
- [ ] **Step 3: Minimal code** -- offload.c: free-space check first, refuse rather than fill, download into a session store separate from the black-box rotation, verify by CRC32, delete on the lap timer only after the verified copy (plan Review Focus 3); devkit.py export verb.
- [ ] **Step 4: Check** -- `devcontroller` builds with zero warnings; devcontroller host tests and `python3 -m unittest tools.test_devkit` pass; run_all only PASS. Acceptance: every input-domain row has a named case; the offload of a real session and a refused offload on a full dev-kit store are b6 bench rows, after the owner's "ready".
- [ ] **Step 5: CHECKPOINT** -- merge the worktrees one at a time with the D34 set after each; the umbrella checkpoint protocol with one opus final reviewer and one final verifier (DEC-0145); append `CHECKPOINT b4 <sha>` to the ledger; update the handoff (next: b5 Task 13) and stream B's board row; stop for `/clear`.

## Block b5 -- P10 cleanup, bench doc and helpers, brownout level (Tasks 13-15), branch `block/prototype-b5`

Block start: branch `block/prototype-b5` from the b4 tip; register `PT-DC-N7` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:422@39a3e8a "Helpers open the lap-timer port with pyserial's default DTR/RTS asserted"`, task b5-T14) and `PT-DRV-N4` (`docs/reviews/2026-10-16-audit-post-fix-wave.md:415@39a3e8a "Brownout level is the IDF default"`, task b5-T15); push the branch. Task 13 starts only when stream A has merged S3 b7 and b8 (Global Constraints, owner ruling 3); if they are not merged at block start, Tasks 14 and 15 run and Task 13 waits, and the block does not checkpoint until it has run.

### Task 13: P10 cleanup of every file b2-b4 touched (Scope (in) 10)

**Files:**
- Modify: `components/core/gps/ubx.c` full-reach linter clean
- Modify: `components/drivers/gps_neo6m/gps_neo6m.c` full-reach linter clean
- Modify: `components/core/imu/mpu.c` full-reach linter clean
- Modify: `components/drivers/imu_mpu6050/imu_mpu6050.c` full-reach linter clean
- Modify: `components/drivers/board_devkit_v1/board.c` full-reach linter clean
- Modify: `components/core/dragengine/drag.c` full-reach linter clean
- Modify: `components/core/lapengine/lap.c` full-reach linter clean
- Modify: `components/core/session/ses_records.c` full-reach linter clean
- Modify: `components/core/session/ses_frame.c` full-reach linter clean
- Modify: `components/core/export/exp_json.c` full-reach linter clean
- Modify: `components/core/export/exp_vbo.c` full-reach linter clean
- Modify: `components/app/cmd/cmd.c` full-reach linter clean
- Modify: `components/app/pipeline/pipeline.c` full-reach linter clean
- Modify: `components/app/supervisor/sup.c` full-reach linter clean
- Modify: `components/app/logger/logger.c` full-reach linter clean
- Modify: `components/app/logger/log_pause.c` full-reach linter clean
- Modify: `components/app/logger/log_reserve.c` full-reach linter clean
- Modify: `components/app/sys/lt_nvs.c` full-reach linter clean
- Modify: `components/app/ui/ui.c` full-reach linter clean
- Modify: `devcontroller/components/offload/offload.c` full-reach linter clean
- Modify: `devcontroller/components/offload/host/offload_guard.c` full-reach linter clean

**Findings:** none -- owner ruling 3 (Scope (in) 10); each linter finding the run reports is listed in the report and fixed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: Wait and list** -- confirm stream A's S3 b7 and b8 merges on `main` (`git log --oneline origin/main` names them) and rebase per the playbook; list in the report every C file the b2-b4 merges changed (`git diff --name-only <b1 tip> <b4 tip> -- '*.c' '*.h'`); a file in that list and not in Files above is added to Files by a plan fix before Step 2.
- [ ] **Step 2: Run the linter** -- the full-reach linter of stream A's S3 b7/b8 on each listed file (its command as the merged S3 plan states it); record every finding with file:line in the report.
- [ ] **Step 3: Fix** -- each finding fixed at source (no grace entry; an exception goes only through the deviation register, owner ruling); the host suite and both builds after each file.
- [ ] **Step 4: Check** -- the linter prints no finding for any listed file; run_all only PASS; zero warnings. Acceptance: the before and after linter output is in the report.

### Task 14: bench doc, OTA-safe helpers, log-rate procedure (Scope (in) 7, 8)

**Files:**
- Modify: `docs/bench/next-bench-day.md` every audit 2 section 5 row rated CANNOT RUN, WOULD FALSE-PASS or NEEDS REWRITE rewritten; the log-rate measurement procedure; the b2-b4 bench rows this plan names
- Create: `tools/bench/lt_port.py` the one way bench helpers open the lap-timer port: DTR and RTS deasserted, refuses within 35 s of an OTA push
- Create: `tools/bench/test_lt_port.py` cases through lt_port.main on a fake port

**Findings:** none -- routes DC-N7 once registered as a PT- row at the block start

**Siblings:** none -- the row is registered at the block start; its siblings are added here then

**Mode:** PARALLEL

**Gates:** run_all

Facts: the section 5 table (`docs/reviews/2026-10-16-audit-post-fix-wave.md:471@39a3e8a "## 5. Bench-day truth table (docs/bench/next-bench-day.md, open items F0/#102 and 18–71; F4-2 = item 71)"`); the helpers DC-N7 names are git-ignored (`docs/reviews/2026-10-16-audit-post-fix-wave.md:422@39a3e8a ".superpowers/bench-helpers/gate_dsA.py:25"`), so this task writes one tracked opener under `tools/bench/` (stream B owns `tools/` except gates, lint and program, `docs/superpowers/playbooks/two-streams.md:27@cd84605 "except gates, lint and program"`) and the bench doc calls only it; the log-rate figures to compare against are spec 12.6 (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:1692@cd84605 "| internal (NEO-6M) | 5 Hz × 20 B | 10 Hz × 16 B | ~270 | ~0.95 |"`). F5-12 (#92) stays out (DEC-0306, `docs/superpowers/state/prototype/pending-decisions.yml:70@f97ff2a "{label: out, cost: 'b6 runs Scope (in) 8 only; #92 stays with F5'}"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| time since the last OTA push (stamp file the push writes) | none recorded; 0, 34.9, 35.0, 120 s |
| port open settings | DTR and RTS deasserted before open (asserted is refused by the test's fake port) |
| port name | a present port; an absent port (named error, exit 2) |

- [ ] **Step 1: Write the failing tests** -- `tools/bench/test_lt_port.py` drives `lt_port.main` on a fake serial class with every row above; watch them fail.
- [ ] **Step 2: Minimal code** -- lt_port.py; `tools/ota_push.py` gains nothing in this task unless the stamp needs it: if it does, the report names the change and the file is added to Files by a plan fix first.
- [ ] **Step 3: Pin** -- PT-DC-N7 by `test_lt_port::test_refuses_inside_ota_window` with its transcript at `docs/superpowers/state/prototype/mutations/PT-DC-N7.txt`.
- [ ] **Step 4: Bench doc** -- list in the report every section 5 row by verdict with its audit line, compare the counts with Scope (in) 8 and report a difference; rewrite each row so it can run on the built prototype with real GPS and IMU, cannot false-pass (each criterion reads a value the defect would change), and opens the port only through lt_port.py; add the log-rate procedure (minutes of riding per MB and sessions per filesystem from `.log` sizes after a timed ride, compared with spec 12.6); add the bench rows b2-b4 name; every flash step says "wait for the owner's ready", every OTA step says "35 s before the port opens", and the power steps say one 5 V source at a time (DEC-0135).
- [ ] **Step 5: Check** -- `python3 -m unittest tools.bench.test_lt_port` passes; run_all only PASS. Acceptance: the report's row table (row, old verdict, new text line) covers every section 5 row of the three verdicts.

### Task 15: brownout level DRV-N4 (Scope (in) 12)

**Files:**
- Modify: `sdkconfig.defaults` brownout level 2.8 V (spec 17.1)
- Create: `tools/test_sdkconfig_defaults.py` checks the built sdkconfig of each env carries the level

**Findings:** none -- routes DRV-N4 once registered as a PT- row at the block start

**Siblings:** none -- the row is registered at the block start; its siblings are added here then

**Mode:** PARALLEL

**Gates:** run_all

Facts: `sdkconfig.defaults:17@cd84605 "CONFIG_ESP_BROWNOUT_DET=y"` sets no level, so the IDF default applies (`docs/reviews/2026-10-16-audit-post-fix-wave.md:415@39a3e8a "not spec §17.1's 2.8 V"`); owner answer DEC-0305 (`docs/superpowers/state/prototype/pending-decisions.yml:59@f97ff2a "plus the DRV-N4 fix"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| env | each env of `build.sh:23@cd84605 "environments: moto_neo6m moto_sim moto_neo6m_wifi moto_m10 moto_m10_sd"` |
| built level line | LVL_SEL_7 set (pass); LVL_SEL_0 or absent (fail) |

- [ ] **Step 1: Write the failing test** -- `tools/test_sdkconfig_defaults.py` builds nothing itself: it reads `build/<env>/sdkconfig` for each env built by Step 3 and fails on any env not at LVL_SEL_7; run it after `./build.sh moto_neo6m build` and watch it fail.
- [ ] **Step 2: Minimal change** -- set the level in sdkconfig.defaults.
- [ ] **Step 3: Pin and check** -- PT-DRV-N4 by `test_sdkconfig_defaults::test_brownout_level_every_env` with its transcript at `docs/superpowers/state/prototype/mutations/PT-DRV-N4.txt`; build every env with zero warnings; run_all only PASS.
- [ ] **Step 4: CHECKPOINT** -- after Task 13 has run (it may wait for stream A): merge the worktrees with the D34 set after each; the umbrella checkpoint protocol with one opus final reviewer and one final verifier (DEC-0145); append `CHECKPOINT b5 <sha>` to the ledger; update the handoff (next: b6 Task 16) and stream B's board row; stop for `/clear`.

## Block b6 -- bench day, stack sizing, road capture and host replay (Tasks 16-18), branch `block/prototype-b6`, with the owner

Block start: branch `block/prototype-b6` from the b5 tip; register `PT-F5-8` (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:26@39a3e8a "Task stacks: with R6's stack-low check live"`, task b6-T17); push the branch. Every step that flashes, powers or opens a port on hardware waits for the owner's explicit "ready"; after an OTA push the lap-timer port stays closed for 35 s; one 5 V source at a time (DEC-0135); the dev-kit stays in reset or off lap-timer RX0 while riding (DEC-0312).

### Task 16: bench day on the built prototype (Scope (in) 8, 7, H4, DEC-0310)

**Files:**
- Modify: `docs/bench/next-bench-day.md` results of every row b5 Task 14 rewrote and of the b2-b4 bench rows
- Modify: `docs/hardware/bom.md` the in-hand inventory of every row it marks unknown
- Modify: `docs/measurements.md` log rate, 1 h soak per env and per-task stack high-water

**Findings:** none -- hardware validation; findings the day raises are registered as PT- rows at that time; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

Facts: BOM rows stay unknown until this inventory (DEC-0310, `docs/superpowers/state/prototype/pending-decisions.yml:106@f97ff2a "{label: bench-day, cost: 'rows stay unknown until the b6 bench-day inventory'}"`; legend `docs/hardware/bom.md:3@cd84605 "**unknown** = no tracked source says whether it is in hand"`); the soak (DEC-0305, `docs/superpowers/state/prototype/pending-decisions.yml:59@f97ff2a "a 1 h soak per env on the bench day, then size the stacks"`); the log-rate measurement on the bench day (DEC-0138, `docs/superpowers/state/decisions.yml:1457@f97ff2a "a log-rate measurement (minutes of riding per MB, sessions per filesystem) on the bench day"`). Which envs the soak covers is an owner question raised by b1 Task 3's report; Step 3 waits for that answer.

- [ ] **Step 1: Inventory (no power)** -- with the owner, each BOM row marked unknown gets built, owned or not built from what is on the bench; the field power bank's row is written only as the owner states it.
- [ ] **Step 2: Flash and run the rows (hardware: wait for "ready")** -- flash the b5 tip to the lap timer only after the owner's "ready" (USB with the dev-kit held in reset, or a dev-kit OTA push with the 35 s window), the antenna where it sees the sky; run every bench row in doc order through `tools/bench/lt_port.py`; record each result with its transcript line.
- [ ] **Step 3: Soak (hardware)** -- for each env the owner's answer names: flash on "ready", run 1 h with GPS fixes and IMU samples flowing, then read the per-task stack high-water (R6's stack-low check) into `docs/measurements.md`.
- [ ] **Step 4: Log rate (hardware)** -- run the b5 log-rate procedure; record minutes of riding per MB and sessions per filesystem in `docs/measurements.md`.
- [ ] **Step 5: Check** -- every row has a result or a registered PT- finding; run_all only PASS. Acceptance: no row left unrun without a stated reason.

### Task 17: task stacks from the soak (F5-8, DEC-0305)

**Files:**
- Modify: `components/app/pipeline/pipeline.c` stack size from the soak
- Modify: `components/app/logger/logger.c` stack size from the soak
- Modify: `components/app/ui/ui.c` stack size from the soak
- Modify: `components/app/supervisor/sup.c` stack size from the soak
- Modify: `components/app/link/link.c` stack size from the soak
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` section 4.3 sizes

**Findings:** none -- routes F5-8 once registered as a PT- row at the block start

**Siblings:** none -- the row is registered at the block start; its siblings are added here then

**Mode:** SERIAL

**Gates:** run_all

Facts: the F5 rule (`docs/superpowers/plans/2026-10-13-fix-F5-close-out.md:26@39a3e8a "set §4.3 sizes to high-water + 25 %"`); the sizes today (`components/app/pipeline/pipeline.c:71@cd84605 "#define PIPE_STACK_BYTES 8192"`, `components/app/logger/logger.c:54@cd84605 "#define LOG_STACK_BYTES  4096"`, `components/app/ui/ui.c:81@cd84605 "#define UI_STACK_BYTES 6144"`, `components/app/supervisor/sup.c:40@cd84605 "#define SUP_STACK_BYTES 3072"`, `components/app/link/link.c:66@cd84605 "#define LINK_STACK_BYTES  3072"`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| per-task high-water from Task 16 | each task's measured bytes per env; the maximum over envs is the input |
| resulting size | high-water times 1.25 rounded up to 16 bytes; DRAM headroom after the change (the map file's free DRAM, both builds) |

- [ ] **Step 1: Compute** -- for each task, size = the maximum high-water over the soaked envs plus 25 %, in a table in the report with the Task 16 measurement line it comes from.
- [ ] **Step 2: Change and pin** -- set each define; PT-F5-8 is pinned by the re-run soak, not a host test: the soak of Task 16 Step 3 is repeated for 1 h on the moto_neo6m image (hardware: wait for "ready") and no stack-low errlog entry appears; the before and after high-water lines are the transcript at `docs/superpowers/state/prototype/mutations/PT-F5-8.txt`.
- [ ] **Step 3: Check** -- builds with zero warnings and DRAM headroom recorded; spec 4.3 updated; run_all only PASS.

### Task 18: road capture and host replay (Scope (in) 9, roadmap 8.5 off-track half)

**Files:**
- Create: `test/data/road_2026.log` the captured session (binary .log as the logger wrote it)
- Create: `test/data/road_2026.expected.json` the device's own lap and summary results for it
- Create: `test/data/road_2026.venue.json` the venue the capture was timed against
- Modify: `tools/replay/gen_fixtures.sh` the new fixture
- Modify: `test/test_lap.c` the replay case that compares engine output with the device's results

**Findings:** none -- roadmap 8.5 off-track half; no audit row is routed here; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

Facts: the roadmap exit (`docs/superpowers/plans/2026-09-14-roadmap.md:252@cd84605 "first road/track capture, convert to"`); existing fixtures follow the `.log`, `.expected.json`, `.venue.json` triple (`test/data/gps_dropout.venue.json` beside `test/data/gps_dropout.log`).

Input-domain table:

| input | values or classes each test covers |
|---|---|
| capture | one road loop of at least three laps over a user venue marked with F7 (b4 Task 11) or a road venue file |
| frames in the capture | as logged, including any CRC-bad or absent fix the ride produced (the replay must give the device's result, not an ideal one) |
| replay output | lap count, each lap time, each sector split, flags |

- [ ] **Step 1: Capture (hardware: wait for "ready")** -- the owner rides or drives the loop with the power bank as the only 5 V source; between sessions offload through the b4 path and export to the laptop.
- [ ] **Step 2: Write the failing replay test** -- `test/test_lap.c` replays the capture through the real lap engine (the replay library test_lap already links) and compares with `road_2026.expected.json`, which holds the device's results from its `.sum`; it fails until the fixture is registered.
- [ ] **Step 3: Register and pass** -- gen_fixtures.sh entry; the replay matches the device's results exactly (any difference is a finding, registered as a PT- row and fixed test-first, not tolerated).
- [ ] **Step 4: Check** -- host suite passes; run_all only PASS.
- [ ] **Step 5: CHECKPOINT** -- the umbrella checkpoint protocol with one opus final reviewer and one final verifier (DEC-0145); append `CHECKPOINT b6 <sha>` to the ledger; update the handoff (next: field test) and stream B's board row; stop for `/clear`.
