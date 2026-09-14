# ESP32 Lap Timer — Design Specification

**Date:** 2026-09-14
**Status:** Draft for review
**Target:** Motorcycle variant first (e-paper), car variant later (OLED)

---

## 1. Overview

A self-contained, battery-powered GPS + IMU lap timer for track days and drag runs, built on an ESP32 with ESP-IDF. It detects which circuit and layout it is on, times laps and sectors, times 0–100 / 1/4-mile style drag runs, records lean angle and g-forces, stores sessions on-device, and exports them to a phone in RaceChrono-compatible formats over BLE. It must work with no external service, run for many track days on one charge, and keep timing through sensor faults, resets, and power loss.

### 1.1 Goals

- Lap, sector, and drag timing accurate to the limit of the GPS module (±0.03–0.05 s on the 5 Hz prototype, ±0.01–0.02 s on a 10 Hz M10).
- Fully independent operation: no phone, no network, no cloud.
- Battery life measured in track days, not hours.
- Robustness first: watchdogs, escalation ladders, session continuity through resets, safe mode, safe OTA.
- One codebase, compile-time hardware selection, zero dead code per build.
- Pure-C algorithm core with no ESP-IDF dependency, unit-tested and log-replayable on the host.

### 1.2 Non-goals (v1)

- Native phone app.
- Cloud sync or accounts.
- Video overlay generation (done by RaceChrono after import).
- Multi-vehicle CAN-bus data.
- Live predictive delta on e-paper (see optional requirement O5).

---

## 2. Requirements

### 2.1 Functional (must)

| ID | Requirement |
|----|-------------|
| F1 | Lap mode: time laps using GPS line crossing. Show best, previous, current-at-last-gate, and sector delta on the riding screen. |
| F2 | Sector timing with configurable gate lines per layout. Theoretical best lap from best sectors. |
| F3 | Automatic venue detection from GPS position. Automatic layout detection within a venue (full/short, forward/reverse) during lap 1. Manual override. |
| F4 | Drag mode: 0–100 km/h style speed benches and 1/4-mile distance gates with ET and trap speed, using industry conventions (NHRA trap = mean speed over final 66 ft, optional 1 ft rollout). Speed benches shown are those actually reached before the 1/4-mile finish. |
| F5 | Record lean angle (motorcycle), longitudinal g, lateral g, combined g, yaw rate, speed, heading, altitude, fix quality. Per-lap maxima. |
| F6 | Three-button UI. Mode change, pages, menu. Menu locked out while moving. |
| F7 | On-device track creation: mark start/finish and sector gates while riding. |
| F8 | Persist all sessions, laps, and sample logs on-device. Timing summaries survive any log damage. |
| F9 | Export sessions to phone over BLE (Web Bluetooth page) in VBO and NMEA, importable by RaceChrono. USB serial as last-resort fallback. |
| F10 | Automatic power management: active / pit / park / shutdown with motion wake. |
| F11 | Self-healing: per-subsystem recovery ladders, watchdogs, crash-loop safe mode, session continuity through resets. |
| F12 | Firmware update over BLE (later WiFi) with signature check, rollback, and hardware-id check. |

### 2.2 Optional (planned, flagged)

| ID | Requirement |
|----|-------------|
| O1 | WiFi AP with web page: same export, config editing, track JSON upload. |
| O2 | RaceChrono DIY BLE live streaming (GPS + IMU channels) so the phone can record live. |
| O3 | u-blox M10 GPS (SEQURE M10-25Q or similar) at 10 Hz with PPS, as a build-flag driver. |
| O4 | microSD storage as a build-flag driver, with summaries mirrored to internal flash. |
| O5 | Live predictive lap delta on an OLED / memory-LCD motorcycle display variant and on the car OLED variant. |
| O6 | Car variant: OLED display, live 10 Hz screens, no lean angle. |
| O7 | Passkey BLE pairing. |

### 2.3 Constraints

| ID | Constraint |
|----|-----------|
| C1 | Works with no external connection of any kind. |
| C2 | Power efficient; target ≥ 20 track days per charge on the 2×18650 pack. |
| C3 | Lightweight and compact for motorcycle mounting. |
| C4 | Robust: uptime and accuracy over features. Degrade, never stop timing. |
| C5 | Hardware limited to what is affordable and available in South Africa. Prototype uses parts already owned. |
| C6 | ESP-IDF, C11, smallest practical firmware. |
| C7 | Motorcycle and car variants are separate builds from one codebase via build flags. Never both hardware paths in one binary. |

### 2.4 Decisions made during design

- GPS: keep NEO-6M for the prototype; build the driver interface so an M10 is a compile-time swap.
- Board: keep the ESP32 CH340 dev board; fix its idle current with an external regulator and LED removal.
- IMU: keep MPU6050; validate with self-test at boot.
- Display: e-paper for the motorcycle prototype; refresh only at gates and events.
- Battery: 2×18650 3000 mAh INR in parallel (1S2P), TP4056 charging.
- Storage: internal LittleFS for the prototype; SD as a later build flag.
- Track DB: bundled South African circuits + on-device creation; JSON upload later.
- Connectivity: BLE + Web Bluetooth page first; serial fallback; WiFi then RaceChrono live later.
- Framework: ESP-IDF, C11.
- Architecture: dual-core split, real-time pipeline on core 1, system tasks on core 0.
- Riding screens stripped to a single-glance read.

---

## 3. Hardware

### 3.1 Prototype bill of materials

| Item | Part | Notes |
|------|------|-------|
| MCU | ESP32 DevKit (WROOM-32, CH340) | Assumed 4 MB flash; verify with `esptool.py flash_id`. |
| GPS | GY-NEO6M v2 | 5 Hz max, UART, backup cell on board, no PPS pin. |
| IMU | MPU6050 (GY-521) | I2C, FIFO, motion interrupt. |
| Display | Waveshare-class SSD1680 e-paper, 2.13" (250×122) or 2.9" (296×128) | SPI. Exact panel to be chosen; driver parameterised by size. |
| Battery | 2×18650 3000 mAh INR (15 A), 2-slot holder | Wired in parallel. |
| Charger | TP4056 module | Must be the 6-pad version with DW01 + FS8205 protection. |
| Regulator | XC6220B331 or AP2112K-3.3 | 600 mA+, low quiescent, feeds the 3.3V pin directly. |
| GPS power switch | P-channel MOSFET (AO3401 / SI2301) + 100 kΩ gate pull-up | High-side switch for PARK. |
| Battery sense | 2×470 kΩ divider + 100 nF | Into ADC1. |
| Buttons | 3× glove-friendly tactile or sealed pushbuttons | Active-high with 100 kΩ pull-downs (see wake constraints). |

### 3.2 Dev board power modifications

The stock DevKit draws 5–15 mA in deep sleep because of the AMS1117 LDO (~5 mA quiescent), the power LED, and the CH340. Modifications:

1. Feed regulated 3.3 V from the external regulator directly into the board's 3.3V pin. The AMS1117 has no input and draws nothing; the CH340 is unpowered without USB.
2. Desolder the power LED (or cut its trace).
3. Never connect USB while the battery regulator is driving the 3.3V pin, or fit a Schottky diode on the regulator output. USB is for flashing and serial fallback on the bench only.
4. Result: ~100 µA in deep sleep.

### 3.3 Pin map (ESP32 DevKit V1)

| Function | GPIO | Notes |
|----------|------|-------|
| GPS UART2 RX (from GPS TX) | 16 | |
| GPS UART2 TX (to GPS RX) | 17 | |
| GPS PPS (optional) | 36 | Input-only. Unused on NEO-6M v2. |
| GPS power enable | 26 | RTC GPIO, drives P-MOSFET gate. Held during sleep. |
| I2C SDA | 21 | MPU6050; QMC5883L on M10-25Q later. |
| I2C SCL | 22 | |
| IMU INT | 27 | RTC GPIO, EXT1 wake, active-high. |
| SPI SCK | 18 | Shared e-paper / SD. |
| SPI MOSI | 23 | |
| SPI MISO | 19 | SD only. |
| E-paper CS | 5 | |
| E-paper DC | 14 | |
| E-paper RST | 4 | |
| E-paper BUSY | 35 | Input-only. |
| SD CS (later) | 15 | Idle-high pull-up is boot-safe. |
| Button MODE | 32 | RTC GPIO, EXT1 wake. |
| Button UP | 33 | RTC GPIO, EXT1 wake. |
| Button DOWN | 25 | RTC GPIO, EXT1 wake. |
| Battery ADC | 34 | ADC1_CH6, input-only. ADC2 is unusable with WiFi. |
| Charger CHRG (optional) | 39 | EXT0 wake, active-low from TP4056. |
| Console / serial export | 1, 3 | UART0 via CH340. |

Avoid: 0, 2, 12 (strapping), 6–11 (flash). GPIO 12 must never see a pull-up at boot.

Wake constraint: classic ESP32 EXT1 wake supports either "any high" or "all low" across its pin set. Buttons are therefore wired active-high with external pull-downs and the IMU interrupt is configured active-high, so EXT1 uses `ANY_HIGH`. The charger pin uses EXT0 (single pin, any level).

### 3.4 Upgrade paths (build-flag drivers)

| Component | Prototype | Upgrade | Interface impact |
|-----------|-----------|---------|------------------|
| GPS | NEO-6M, 5 Hz, legacy `UBX-CFG-*` | SEQURE M10-25Q, 10 Hz, `UBX-CFG-VALSET`, optional PPS pad, QMC5883L magnetometer | None. Driver profile carries rate, baud, config backend. |
| Storage | Internal LittleFS ~1.3 MB | microSD over shared SPI | None. `storage.h` interface. |
| Display | SSD1680 e-paper | OLED (SSD1309 SPI) for car or live-delta moto | None. `display.h` interface plus variant UI layouts. |
| Connectivity | BLE + serial | + WiFi AP, + RaceChrono live | None. `conn.h` command handler. |
| Board | DevKit + external regulator | Battery-native board when available | `board.h` pin map and power hooks. |

M10-25Q notes: 5 V input pin, check onboard LDO dropout for direct LiPo feed; no documented backup capacitor, so keep it powered and use UBX power-save instead of cutting power; PPS is not on the 6-pin connector, look for a TIMEPULSE pad.

---

## 4. Software architecture

### 4.1 Repository layout

```
lap-timer/
  CMakeLists.txt                 top-level: variant selection, driver dirs
  sdkconfig.defaults             shared IDF config
  sdkconfig.defaults.moto        variant fragments
  sdkconfig.defaults.car
  partitions.csv                 OTA-capable partition table
  build.sh                       ./build.sh moto_neo6m [flash|monitor]
  .idf-version                   pinned ESP-IDF version
  main/                          app_main: boot sequence, task spawn
  components/
    core/                        pure C11, zero IDF dependency, host-testable
      timebase/                  mono/gps clock mapping, min-filter, week rollover
      geo/                       ENU projection, segment intersection, direction
      fusion/                    calibration, rotation, lean angle, g-forces
      lapengine/                 venue/layout matching, gates, sectors, laps
      dragengine/                launch detection, speed/distance gates, trap speed
      tracks/                    track schema, bundled tables, user tracks
      session/                   record structs, framing, CRC, encode/decode
      export/                    VBO, NMEA, JSON encoders (streaming)
      config/                    config schema, validation, defaults, migration
    hal/                         interface headers only
      gps.h imu.h display.h storage.h board.h conn.h
    drivers/                     exactly one per interface compiled, chosen by CMake
      gps_neo6m/  gps_m10/
      imu_mpu6050/
      display_epaper_ssd1680/  display_oled_ssd1309/
      storage_internal/  storage_sd/
      conn_ble/  conn_wifi/  conn_ble_rc/  export_serial/
      board_devkit_v1/
    app/                         IDF-dependent glue
      pipeline/                  core 1 real-time task
      ui/                        screens, menu, buttons; moto/ and car/ layouts
      power/                     power state machine, sleep, battery
      supervisor/                watchdogs, heartbeats, escalation, error log
      logger/                    storage writer task
      ota/                       OTA handler
  tools/
    web/export.html              Web Bluetooth client
    replay/                      host CLI: run logs through core
    tracks/                      track JSON sources + generator
    gps_sim.py                   replay UBX to device UART
    serial_export.py             serial fallback client
  test/                          host Unity tests + data fixtures
    CMakeLists.txt
    data/
  test_apps/                     on-target driver tests
  docs/superpowers/specs/
```

### 4.2 Task map

| Task | Core | Priority | Responsibility |
|------|------|----------|----------------|
| pipeline | 1 | 20 | GPS + IMU ingest, timestamping, fusion, lap/drag engines. Never blocks on anything but its own queue set. |
| supervisor | 0 | 22 | Heartbeat checks, escalation ladders, error log. |
| logger | 0 | 8 | Drains sample rings to storage in 4 KB batches. |
| ui | 0 | 6 | Buttons, screens, display refresh. |
| conn | 0 | 5 | BLE GATT server, command handler, file transfer, OTA. |
| power | 0 | 4 | Battery ADC, idle timers, state transitions, sleep entry. |

The BLE/WiFi stacks pin to core 0 by IDF default. Core 1 belongs to the pipeline.

### 4.3 Inter-task communication

- **Sample rings** (pipeline → logger, ui): lock-free single-producer single-consumer ring buffers. One for raw fixes at GPS rate, one for fused samples at 25 Hz.
- **Event queue** (pipeline → ui, logger, power): `LAP_COMPLETE`, `SECTOR`, `VENUE_FOUND`, `LAYOUT_LOCKED`, `FIX_LOST`, `FIX_OK`, `DRAG_ARMED`, `DRAG_GATE`, `DRAG_DONE`, `MOTION`, `STILL`.
- **Command queue** (any → pipeline): `SET_MODE`, `SET_LAYOUT`, `MARK_GATE`, `CALIBRATE`, `RESET_ENGINE`, `SET_CONFIG`.
- No other shared mutable state. All buffers statically allocated at init.

### 4.4 Build flags

`VARIANT` ∈ {moto, car}, `GPS` ∈ {neo6m, m10}, `IMU` ∈ {mpu6050}, `DISPLAY` ∈ {epaper_ssd1680, oled_ssd1309}, `STORAGE` ∈ {internal, sd}, `CONN` ∈ {ble, wifi, ble+wifi}, plus booleans `CONN_BLE_RC`, `EXPORT_SERIAL` (default on).

CMake validates combinations (moto requires epaper or oled; car requires oled) and adds only the chosen `components/drivers/<x>` directories. A generated `build_config.h` exposes the variant to the app layer for UI selection. Drivers contain no `#ifdef` on flags; selection is by directory.

Named environments: `moto_neo6m`, `moto_m10`, `car_neo6m`, `car_m10`, each with `_sd` and `_wifi` suffix variants as needed.

---

## 5. Time base and timing math

### 5.1 Clocks

- `mono_us`: int64 microseconds from `esp_timer_get_time()`. Survives light sleep (IDF compensates). Reset by deep sleep; the session epoch is preserved in RTC memory.
- `gps_us`: int64 microseconds UTC derived from UBX-NAV-PVT (`iTOW` + `nano`, week rollover handled in the driver). All lap, sector, and drag timestamps live in this domain.

Lap time is the difference of two crossing timestamps in `gps_us`. UART latency and crystal drift (~20 ppm, ~2.4 ms per two-minute lap) cancel. Accuracy comes from position interpolation quality, not from clock discipline.

### 5.2 Clock mapping

A mapping `mono_us → gps_us` is needed only to align IMU samples with GPS fixes.

- With PPS (M10 TIMEPULSE pad wired to GPIO 36): an ISR captures `mono_us` on the rising edge, which marks the top of a GPS second. Mapping accurate to microseconds.
- Without PPS (NEO-6M): the offset `arrival_mono_us − fix_gps_us` is dominated by one-sided positive latency jitter. A running minimum filter over a 30 s window estimates the true offset within ~5–10 ms. Adequate for IMU alignment.

### 5.3 Gate crossing

Implemented in `core/geo` and `core/lapengine`.

1. The venue centre is the ENU origin. Lat/lon converts to metres by equirectangular projection (error negligible under 10 km).
2. A gate is a segment P1–P2. Motion is the segment from fix k−1 to fix k. Proper segment intersection is tested with cross products.
3. Direction check: the sign of `cross(gate_dir, motion_dir)` must match the layout's expected sign. A reverse layout has the opposite sign. Wrong-way crossings are ignored.
4. Crossing time uses constant-acceleration interpolation: with `d` the distance along the motion segment to the intersection and Doppler speeds `v0`, `v1` from NAV-PVT `gSpeed`, solve `d = v0·τ + ½·a·τ²` with `a = (v1 − v0)/Δt`.
5. Debounce: the same gate is ignored until the vehicle is 50 m away or `min_lap_s` (default 20 s) has elapsed.

### 5.4 Fix validity

A fix is valid when fix type is 3D, `hAcc` ≤ 15 m, and satellites ≥ 5. Invalid fixes are dropped; the current lap is flagged invalid; timing continues on the last good mapping until fixes return.

### 5.5 Drag distance and speed gates

Distance is integrated from Doppler speed (trapezoid), never from position deltas. Speed gates (e.g. 100 km/h) and distance gates (60 ft = 18.288 m, 1/8 mi = 201.168 m, 1000 ft = 304.8 m, 1/4 mi = 402.336 m) are interpolated inside the enclosing segment with the same constant-acceleration model. Trap speed is the mean speed over the final 20.117 m (66 ft) before the 1/4-mile line.

### 5.6 Expected accuracy

| GPS | Rate | Lap/gate accuracy |
|-----|------|-------------------|
| NEO-6M | 5 Hz | ±0.03–0.05 s absolute; lap-to-lap consistency better |
| M10 | 10 Hz | ±0.01–0.02 s |

### 5.7 NEO-6M driver notes

- Prefers `UBX-NAV-PVT` (firmware 7.03+). Falls back to assembling a fix from `NAV-SOL` + `NAV-POSLLH` + `NAV-VELNED` matched on `iTOW`. The core only sees `gps_fix_t`.
- Configuration applied at every boot and after every GPS power cycle: baud 38400, `CFG-RATE` 200 ms, `CFG-NAV5` dynamic model automotive, NMEA output off, SBAS off (no coverage in South Africa), `CFG-MSG` for the chosen navigation messages.
- NEO-6M has no flash; any NMEA sentence arriving means the module reset, and the driver reconfigures.
- M10 driver uses `UBX-CFG-VALSET` keys (`CFG-RATE-MEAS`, `CFG-NAVSPG-DYNMODEL`, `CFG-MSGOUT-*`, `CFG-PM-*`) and defaults to 115200 baud.

---

## 6. Pipeline and sensor fusion

### 6.1 Pipeline loop

The pipeline task blocks on a FreeRTOS queue set containing the UART RX event queue, a 50 ms IMU timer, and the command queue. It performs no other blocking call and no heap allocation after init.

### 6.2 GPS ingest

Byte-wise UBX state machine with checksum verification, dispatching to the driver's message assembler. Output: `gps_fix_t { gps_us, lat, lon, alt, gSpeed, headMot, hAcc, sAcc, fix_type, sats, valid }`. Speed and heading are Doppler-derived, never position-derived.

### 6.3 IMU ingest

MPU6050 configured at 100 Hz sample rate, DLPF 20 Hz (motorcycle vibration), ±16 g, ±2000 dps, FIFO enabled for accel + gyro (12 bytes per sample). Every 50 ms the pipeline reads `FIFO_COUNT` and pulls all samples in one I2C burst. Each sample is stamped `mono_us` by back-dating from the read time at 10 ms spacing. FIFO overflow resets the FIFO and increments an error counter.

### 6.4 Calibration

Stored in NVS, versioned.

- **Gyro bias**: estimated automatically whenever the vehicle is stationary for 2 s (low accelerometer and gyro variance). Tracked against MPU6050 temperature.
- **Mounting orientation**: user performs "vehicle upright, press button", capturing the gravity vector in the body frame. The forward axis is auto-learned from the first straight-line accelerations (yaw rate ≈ 0) as the horizontal acceleration direction. Together these compose rotation `R` from body frame to vehicle frame (X forward, Y left, Z up). Re-run on remount.

### 6.5 Fusion (100 Hz, `core/fusion`)

- Rotate accelerometer and gyro into the vehicle frame.
- **Longitudinal g**: vehicle-X specific force with bias removed. Pitch is ignored (flat-track assumption).
- **Lean angle φ (motorcycle)**: complementary filter. Fast path integrates body roll rate. Slow reference `φ_ref = atan2(v · ψ̇, g)` where `v` is GPS Doppler speed and `ψ̇` is earth-frame yaw rate from the gyro rotated by the current φ estimate. `φ = 0.98·(φ + ω_x·dt) + 0.02·φ_ref` at 100 Hz. Converges within ~1 s, drift-free. Positive is right lean. The GPS heading rate cross-checks; sustained disagreement over 10° raises a fusion warning flag.
- **Lateral g (motorcycle)**: `v · ψ̇ / g`. The accelerometer's lateral axis reads near zero while leaned and is not used directly.
- **Lateral g (car)**: vehicle-Y specific force directly. Lean is not computed.
- Combined g, yaw rate, and per-lap maxima (max/min speed, max lean left/right, max lateral g, max acceleration g, max braking g).

### 6.6 Outputs

- Raw fix ring at GPS rate.
- Fused sample ring at 25 Hz (every fourth fused sample).
- Events per §4.3.
- Motion flag derived from GPS speed (> 3 km/h) for the power manager; IMU variance is used only as a wake source.

---

## 7. Lap engine, track model, drag engine

### 7.1 Track model

```
venue   { id, name, centre_lat, centre_lon, radius_m, layouts[] }
layout  { id, name, sf_line { p1, p2 }, direction_sign, sectors[] { p1, p2 }, length_m }
```

A reverse layout is the same S/F line with the opposite `direction_sign`. Short and full layouts differ in sector gates and length. Killarney, for example, is one venue with layouts Full, Full Reverse, Short, Short Reverse.

Bundled venues are generated from `tools/tracks/*.json` into C tables at build time. Initial set: Kyalami, Zwartkops, Red Star Raceway, Phakisa, Aldo Scribante, Killarney, Dezzi Raceway, East London Grand Prix Circuit, Midvaal, plus regional kart tracks as data becomes available. Start/finish lines are approximate until verified on site. User-created tracks live in `tracks/user.bin` in the same struct; user entries win on id clash.

### 7.2 Lap engine states

```
NO_VENUE → VENUE_FOUND → ARMED → (out-lap) → LAP_RUNNING → LAP_RUNNING …
```

- `VENUE_FOUND`: position within `radius_m` of a venue. Candidate layouts loaded.
- `ARMED`: waiting for the first S/F crossing. That crossing starts the out-lap and emits no lap time.
- Lap 1 disambiguates the layout by which sector gates were crossed and the lap distance, then locks it. The user may override at any time from the menu; the default layout per venue is configurable.
- Each S/F crossing emits `LAP_COMPLETE { gps_us, lap_time, sector_times[], flags, stats }`. Each sector gate emits `SECTOR`.
- Pit detection: speed < 5 km/h for 10 s inside a lap marks the lap invalid.
- Plausibility: laps shorter than `min_lap_s` are ignored; laps longer than 30 min are invalid; a sector gate out of order resyncs the expected-gate index.
- Best lap and best sectors are tracked per session; theoretical best is the sum of best sectors.

### 7.3 On-device track creation

From the menu: "New track". The rider crosses the intended start/finish and presses MODE; the engine drops a 30 m line perpendicular to the current heading at the crossing point. Each subsequent MODE press on lap 1 adds a sector gate the same way. The venue is saved as `Track_YYYYMMDD` with the current position as centre and a 2 km radius. Reverse is derived automatically.

### 7.4 Drag engine states

```
IDLE → ARMED → LAUNCHED → DONE
```

- `ARMED`: speed < 0.5 km/h and IMU still for 2 s.
- Launch: longitudinal acceleration > 0.15 g sustained for 100 ms. `t0` is back-computed to zero speed with the constant-acceleration model, which detects launch earlier than a GPS speed threshold. Optional 1 ft rollout subtracts 0.3048 m of distance.
- Gates evaluated per fix: speed benches (km/h {100, 200, 300}; mph {60, 120, 180}; configurable list), 100–200 km/h, distance gates (60 ft, 1/8 mi, 1000 ft, 1/4 mi with trap speed), and 100–0 braking distance after the run.
- The run ends after the 1/4-mile line, or when speed falls with no new gate for 60 s. `DRAG_GATE` is emitted per gate, `DRAG_DONE` at the end. Best run per bench is kept per session.

### 7.5 Mode switching

Only the active mode's engine runs. Switching mode resets both engines. A switch during a running lap requires confirmation.

---

## 8. Data model, storage, export

### 8.1 Records (`core/session`)

Packed little-endian structs, each type versioned.

- `session_hdr`: id (start `gps_us`, or boot counter if no fix yet), mode, venue/layout ids, variant, firmware version, calibration snapshot, log rate profile.
- `lap`: number, start `gps_us`, time ms, sector ms[], flags {gps_lost, pit, incomplete, out_lap, interrupted}, stats.
- `drag_run`: `t0`, rollout flag, gate results [{gate id, ms, speed}], trap speed.
- `fix`: delta-encoded against the previous fix (dt u16, dlat/dlon i16 at 1e-7°, speed u16 cm/s, heading u16, hAcc u8, sats/flags u8), ~14 bytes. An absolute keyframe every 5 s.
- `fused`: dt u16, lateral g i16, longitudinal g i16, lean i16, yaw i16, flags, ~12 bytes.

### 8.2 Log file format

One append-only file per session. Frame: `sync u8 | type u8 | len u8 | payload | crc16`. A reader resynchronises on the sync byte after corruption; keyframes bound the loss to 5 s.

### 8.3 Summary file

One per session: header plus all laps and runs. Rewritten at each lap or run end by writing a temp file and renaming (atomic on LittleFS). Summaries are never auto-evicted and survive any log damage.

### 8.4 Storage profiles

- `STORAGE_INTERNAL`: LittleFS on the ~1.3 MB storage partition. Fused samples logged at 10 Hz. Logging pauses when stationary. Roughly 0.6 MB per hour of riding, ~2 h retained. When free space falls below 10 %, the oldest sample log is evicted (summaries kept).
- `STORAGE_SD`: FAT over shared SPI. Fused at 25 Hz. Summaries mirrored to internal LittleFS. Write error → remount, retry twice, then degrade to internal summaries-only with a display flag. Boot self-test writes and reads a probe file.

### 8.5 Logger task

Drains rings into a 4 KB buffer; writes on full or on a 1 s tick; `fsync` every 2 s. Power loss costs at most 2 s of samples and never metadata.

### 8.6 NVS

Config (units, mode, thresholds, default layouts, drag bench list, `live_clock`), calibration, error ring (last 32 entries: code, uptime, detail), counters (boots, crashes, WDT resets, GPS resets, I2C recoveries).

### 8.7 Export encoders (`core/export`)

Pull-based streaming with a 512-byte window; no whole file in RAM.

- **VBO** (Racelogic): `[header]`, `[channel units]`, `[column names]`, `[data]`. Standard columns `sats time lat long velocity heading height`; custom columns `lat_g lon_g lean yaw`. Latitude and longitude in decimal minutes, west longitude positive. Fused values held to the nearest fix row. Imported by RaceChrono, Harry's LapTimer, RaceRender, Circuit Tools.
- **NMEA**: `GPRMC` + `GPGGA` per fix. Universal fallback.
- **JSON**: session list with lap summaries for the web page.

---

## 9. Power management

### 9.1 States

```
BOOT → ACTIVE ⇄ PIT ⇄ PARK
              ↘ CONNECTED (entered from menu when stationary)
   any → SHUTDOWN (battery ≤ 3.3 V filtered, or user hold)
```

| State | Entry | Behaviour |
|-------|-------|-----------|
| ACTIVE | speed > 3 km/h within last 30 s | DFS 80→40 MHz, `ESP_PM_NO_LIGHT_SLEEP` lock held (classic ESP32 corrupts UART bytes on light-sleep wake). UART clock `REF_TICK`. GPS full rate, IMU 100 Hz FIFO, radio off. |
| PIT | no speed for 30 s | GPS cyclic power-save at 1 Hz with messages off (`CFG-RXM`/`CFG-PM2` on NEO-6M, `CFG-PM-*` on M10). Gyro bias recalibrated, then IMU to accel-only cycle mode 40 Hz with motion interrupt. PM lock released, auto light sleep. IMU motion or button returns to ACTIVE in < 100 ms. E-paper holds its image at zero power. |
| PARK | no speed for 10 min, or long-press | Flush, summary write, deep sleep. GPS power cut via MOSFET (`board_gps_power(false)`); fallback `UBX-RXM-PMREQ`. Wake on EXT1 (IMU INT, buttons) or EXT0 (charger). |
| CONNECTED | menu, stationary only | BLE advertising 60 s; connected transfer; 5 min idle auto-off. |
| SHUTDOWN | battery < 3.3 V for 30 s under load | Flush, "LOW BATT" screen, deep sleep. Wake only on charge or button; refuses to run below 3.4 V. |

### 9.2 RTC memory

8 KB slow RTC memory, CRC-guarded and versioned: session id, mode, venue/layout lock, lap count, best lap, previous lap, in-progress lap start, calibration. On wake within 4 h the session resumes. Older state starts a new session. Unknown version is discarded.

### 9.3 Battery sensing

2×470 kΩ divider with 100 nF on ADC1 (GPIO 34), 11 dB attenuation, eFuse Vref calibration, 64-sample average, two-point user calibration in config. State of charge from a Li-ion OCV table, trusted only in PIT/PARK. Display shows % and a warning icon at ≤ 20 %.

### 9.4 Budget

| State | ESP32 | GPS | IMU | Misc | Total |
|-------|-------|-----|-----|------|-------|
| ACTIVE | 30 | 40 | 4 | 1 | ~75 mA |
| PIT | 1.5 | 11 | 0.01 | 0.5 | ~13 mA |
| PARK | 0.01 | 0 | 0.01 | 0.1 | ~0.1 mA |
| CONNECTED | 60 | 11 | 0.01 | 1 | ~70 mA |

A track day of ~2 h ACTIVE, ~6 h PIT, and a few transfers is roughly 260 mAh. The 6000 mAh pack yields ~20 track days. An M10 swap cuts ACTIVE to ~45 mA.

E-paper: panel deep sleep (~1 µA) after every refresh.

---

## 10. Robustness and self-healing

### 10.1 Watchdogs

Task WDT at 5 s with every task subscribed; interrupt WDT on; brownout detector at 2.8 V. A stuck task panics, the device resets, and RTC state resumes the session.

### 10.2 Supervisor

Runs at 1 Hz on core 0 at top priority. Reads per-task heartbeat counters, GPS frame age, IMU sample age, logger queue depth, free and minimum-ever heap, and stack high-water marks. Anomalies run the subsystem's escalation ladder. Every step is logged to the NVS error ring.

### 10.3 Escalation ladders

- **GPS**: 3 s without a valid frame → resend configuration. 10 s → reinit UART and autobaud (9600/38400/115200). 30 s → power-cycle via MOSFET or `PMREQ`. Still dead → `GPS_DEAD` flag, warning icon, IMU logging continues, retry every 60 s. Fix sanity: reject 0/0, speed > 500 km/h, time going backwards, position jumps implying > 250 m/s.
- **IMU**: I2C NACK → retry 3×. Then bus recovery (9 SCL pulses + STOP, driver reinit), `WHO_AM_I`, re-apply registers. Still dead → `IMU_DEAD`, lean and g channels flagged invalid, lap timing continues GPS-only. Identical raw samples for 1 s count as a fault. FIFO overflow resets the FIFO.
- **Display**: BUSY timeout → re-init panel. Repeated → `DISPLAY_DEAD`; timing and logging unaffected.
- **Storage**: write error → remount. Mount fails twice → format and log (uptime over data; summaries are expected to have been exported). SD ladder per §8.4.
- **Heap**: below threshold → BLE off, log. The pipeline never allocates after init.

### 10.4 Crash handling

Coredump to the flash partition, reboot, reset reason logged. A fresh coredump at boot creates an error ring entry and is kept for export. Three crashes within 60 s enter safe mode: GPS, timing, and summaries only; one "SAFE MODE" screen; no further display refresh, no BLE, no sample logging. Safe mode clears after 10 min of stable uptime.

### 10.5 Session continuity

Lap start timestamps in `gps_us` are kept in RTC memory. After a reset mid-lap (boot ~1.5 s, GPS hot start ~1 s) the lap resumes with its original start and is flagged `interrupted`; its time remains valid if a fix returns before the next crossing.

### 10.6 Boot self-test

IMU `WHO_AM_I` and self-test registers (catches clones), GPS answers `UBX-MON-VER`, display responds, storage probe file, battery voltage sane, RTC state CRC, config range-validated with fallback to defaults. Results shown for 2 s and logged.

### 10.7 Environment

MPU6050 temperature stands in for ambient. Below 0 °C or above 45 °C, e-paper refresh is throttled to protect the panel and the condition is logged.

### 10.8 Flash-write stalls

Classic ESP32 disables cache during flash writes, stalling both cores for 2–20 ms. Mitigation in every build: `CONFIG_UART_ISR_IN_IRAM=y`, IRAM-safe ISRs, 2 KB GPS UART ring buffer, IMU FIFO absorbing 50 ms, logger writes batched at 4 KB. Timing is unaffected because timestamps live in the GPS domain.

### 10.9 Code rules

C11, `-Wall -Wextra -Werror`. Stack sizes from measured high-water plus 25 %. Core assertions never abort on target; they log and degrade. All time values are int64 microseconds. All records carry CRC16. No dynamic allocation after init.

### 10.10 Diagnostics screen

Error ring, counters, heap, uptime, GPS/IMU state, storage free. Exportable over BLE and serial.

---

## 11. Connectivity, export, and OTA

### 11.1 Command handler (`conn.h`)

Transport-agnostic. Commands: `LIST`, `OPEN {id, fmt}`, `READ {offset}`, `DELETE {id}`, `CONFIG_GET`, `CONFIG_SET`, `TRACKS_GET`, `TRACKS_PUT`, `ERRLOG_GET`, `STATUS`, `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_ABORT`. Transports implement send and receive only. Export encoders stream behind `OPEN`/`READ`.

### 11.2 BLE (`CONN_BLE`, prototype)

NimBLE on classic ESP32. Custom 128-bit service with three characteristics:

- `cmd` (write): binary command frames.
- `data` (notify): `seq u16 | payload ≤ 500 B`, MTU 512 negotiated.
- `status` (read/notify): state, battery %, storage free, firmware version, fault flags.

Transfer: the client sends `OPEN`; the device streams the whole file by notifications with a CRC32 trailer. The link layer is reliable; `seq` exists only for resume via `READ {offset}` after a disconnect. Connection interval 15–30 ms during transfer, 500 ms idle. Expected 20–40 KB/s; a 20-minute session on the internal profile (~0.2 MB) transfers in ~10 s.

Security v1: BLE advertises only when enabled from the menu, for 60 s; the first connection is accepted; no bonding. Passkey pairing is optional requirement O7.

### 11.3 Web Bluetooth page

`tools/web/export.html`: vanilla JavaScript, single file. Connect, list sessions with lap summaries, download `.vbo` / `.nmea` (assembled into a Blob and saved), delete, edit config, view error log and diagnostics, update firmware. Hosted on GitHub Pages (HTTPS required by Web Bluetooth) or opened locally in Chrome. Works on Android Chrome and desktop Chrome; iOS via the Bluefy browser.

### 11.4 Serial fallback (`EXPORT_SERIAL`, always compiled)

UART0 via CH340 at 921600 baud. Same commands as text lines. Files framed by `---BEGIN <name>---` / `---END <crc32>---`. Firmware logging is suspended during a transfer. `tools/serial_export.py` wraps the protocol; a plain terminal also works.

### 11.5 WiFi AP (`CONN_WIFI`, optional O1)

`esp_http_server` serving the same HTML from flash with an HTTP transport, endpoints `/api/sessions`, `/api/session/{id}.vbo`, `/api/config`, `/api/tracks`, `/api/ota`. `laptimer.local` via mDNS. Menu-enabled, 5 min idle off. Adds track JSON upload.

### 11.6 RaceChrono live (`CONN_BLE_RC`, optional O2)

Second GATT service `0x1FF8` implementing the RaceChrono DIY protocol: GPS main and GPS time characteristics at fix rate; lean, lateral g, longitudinal g, and yaw rate sent on the CAN-Bus characteristic as PIDs `0x100–0x103`, mapped by the user with equations in RaceChrono. Runs during ACTIVE, ~+15 mA. Can coexist with `CONN_BLE`.

### 11.7 Config

Versioned JSON schema, single `config_get_json` / `config_set_json`, range-validated, shared by every transport. Migration or defaults on version change.

### 11.8 OTA firmware update (F12)

**Partition table (4 MB flash):**

| Partition | Size |
|-----------|------|
| nvs | 24 KB |
| otadata | 8 KB |
| phy_init | 4 KB |
| ota_0 | 1.25 MB |
| ota_1 | 1.25 MB |
| coredump | 64 KB |
| storage (LittleFS) | ~1.3 MB |

The app must stay under 1.25 MB: NimBLE + LittleFS builds are ~0.9 MB, WiFi + HTTP builds ~1.2 MB. A larger flash module enlarges storage automatically.

**Safety chain:**

1. Images are signed at build (ECDSA) with `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`. The bootloader and `esp_ota` verify the signature before switching slots. No eFuse burn, no secure-boot lock-in. The private key stays out of the repository.
2. `esp_app_desc_t` carries the version and a hardware id string (e.g. `moto_neo6m_epaper`). The handler reads it after the write and refuses a mismatch.
3. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`: the new image boots as `PENDING_VERIFY` and is marked valid only after the boot self-test passes, a GPS frame is received, storage mounts, and 60 s of uptime elapse. Any crash, WDT, or failed self-test before that reverts to the previous slot on the next reset.
4. Preconditions: `CONNECTED` state, stationary, battery ≥ 50 %, session flushed. Otherwise refused with a reason.
5. Progress bar on the display; WDT fed throughout; abort or disconnect leaves the running slot untouched.

**Transport:** the `OTA_*` commands over BLE (~40 s per image), WiFi POST `/api/ota`, or serial fallback. The web page provides a firmware file picker. Releases are published as GitHub release `.bin` files with signatures.

**Compatibility:** log format, summary, NVS config, and RTC memory struct are versioned. Config migrates or falls back to defaults; unknown RTC state is discarded; calibration in NVS survives OTA untouched.

---

## 12. Display and UI

### 12.1 Display interface

`display.h`: `init`, `blit(framebuffer)`, `refresh(PARTIAL | FULL)`, `sleep`, `caps { width, height, partial_ok, temp_min, temp_max }`. The prototype driver `display_epaper_ssd1680` supports the 2.13" (250×122) and 2.9" (296×128) Waveshare-class panels with size as a config value. Framebuffer ~5 KB, static. A minimal renderer provides two bitmap fonts (large digits ~40 px, small text ~12 px), icons, and a bar. No LVGL.

### 12.2 Refresh policy (e-paper)

Draw only on events; never periodically while riding.

- `LAP_COMPLETE`: partial full-screen refresh.
- `SECTOR`: partial refresh of the affected rows.
- State changes (venue found, fix lost/back, battery ≤ 20 %, faults): partial.
- Full refresh every 10 partials or 30 min, preferably when stationary.
- Events arriving mid-refresh are coalesced; the latest state is rendered once.
- Config `live_clock` (default off on e-paper, on for OLED) enables a 1 Hz current-lap refresh at the cost of ghosting, panel wear, and ~10 mA.

### 12.3 Riding screens

```
LAP                             DRAG
┌────────────────────────┐     ┌────────────────────────┐
│BEST   1:51.90          │     │0-100     5.91          │
│PREV   1:52.34          │     │0-200    12.40          │
│CUR    1:12.30   S2     │     │0-300    19.87          │
│ΔS     -0.21            │     │1/4     14.20 @ 305     │
└────────────────────────┘     └────────────────────────┘
```

- **LAP**: BEST and PREV lap times; CUR is the elapsed time at the last gate crossed with its sector label; ΔS is the just-completed sector versus the same sector of the best lap (negative is good). At a lap crossing PREV and BEST update, CUR resets to `0:00.00 S0`, and ΔS shows the full-lap delta.
- **DRAG**: rows appear as gates pass. Speed benches shown are those reached before the 1/4-mile finish (km/h {100, 200, 300}; mph {60, 120, 180}; configurable). At most four rows; the lowest benches drop first if the list is longer. The 1/4 row shows ET and trap speed. A run that ends before the 1/4 mile shows the benches reached and `1/4 --`.
- No permanent status bar. A small icon appears only on a fault (fix lost, battery ≤ 20 %, IMU dead, storage full) and disappears when clear. The venue name is shown once on detection.

### 12.4 Pages and menu

UP/DOWN cycle pages, available riding or stationary. LAP pages: riding screen; best lap with all sectors; session stats (max lean L/R, max g, max speed, theoretical best). DRAG pages: riding screen; all gates including 60 ft, 1/8 mi, 100–200, braking; best runs.

MODE long-press opens the menu: mode, layout override, new track, calibrate orientation, units, export via BLE, diagnostics, sessions (delete), sleep now. UP/DOWN navigate, MODE selects, long MODE goes back. The menu is locked out above 10 km/h.

### 12.5 Buttons

Three GPIOs, active-high with external pull-downs, edge ISR to queue, 30 ms software debounce, short press < 500 ms, long press ≥ 1 s. All on RTC-capable pins for wake.

### 12.6 One-shot screens

Boot self-test result, safe mode, low battery, venue detected, OTA progress.

### 12.7 Car variant (O6) and live delta (O5)

`display_oled_ssd1309` (SPI) with the same widgets and screen state machine, layouts in `app/ui/car/`, 10 Hz live updates, predictive delta bar. The lap engine stores the best lap as a distance-indexed time table (5 Hz, ~600 entries × 4 bytes) and computes the predictive delta continuously; e-paper shows it only at gates, OLED shows it live.

---

## 13. Build system and testing

### 13.1 Toolchain

ESP-IDF 5.x, exact version pinned in `.idf-version`, target `esp32`, C11.

### 13.2 Variant build

The top-level `CMakeLists.txt` reads the cache variables from §4.4, validates the combination, adds only the selected driver directories, and generates `build_config.h`. `sdkconfig.defaults` plus per-variant fragments set PM, tickless idle, task WDT, coredump-to-flash, NimBLE, the partition CSV, signed-update options, and log level. `./build.sh <env> [flash|monitor]` expands to `idf.py -B build/<env> -D… <cmd>`. The firmware version comes from `git describe`.

### 13.3 Host build

`test/CMakeLists.txt` is plain CMake for macOS and Linux. It compiles `components/core/*` with Unity and runs under `ctest`. No ESP-IDF is required. The same CMake builds `tools/replay`.

### 13.4 Test layers

1. **Unit tests, host, Unity, test-first.** `geo` (ENU, intersection, direction sign), `timebase` (mapping, min-filter, week rollover), `fusion` (synthetic steady turn converges to a known lean, bias calibration), `lapengine` (crossing sequences, debounce, reverse, full-vs-short disambiguation, invalid laps, sector resync), `dragengine` (synthetic acceleration profiles give known gate times, rollout, benches), `session` (round-trip and corruption resync), `export` (golden files), `tracks` (lookup), `config` (validation, migration).
2. **Replay, host.** `replay` feeds a device session log or a UBX + IMU capture through the core deterministically and prints laps, sectors, and drag gates. `test/data/` holds recorded sessions with expected results as regression fixtures. A synthetic track generator drives a polygon at speed with noise at 5 Hz and 10 Hz and asserts a timing error bound (≤ 30 ms at 5 Hz).
3. **Bench, target.** `tools/gps_sim.py` replays UBX to the GPS UART in real time through the real driver. Console `dbg` commands: `status`, `errlog`, `hang` (WDT path), `crash` (coredump and safe mode), `gps raw`, `imu raw`, `power <state>`. Power is measured per state with a meter in series. I2C recovery is exercised by shorting SDA; brownout by ramping a bench supply.
4. **Driver tests, target.** IDF `test_apps/`: IMU self-test, storage probe, display init, GPS `MON-VER`.
5. **Field.** First sessions run side-by-side with RaceChrono on a phone; expected agreement ≤ 0.1 s at 5 Hz.

### 13.5 CI

GitHub Actions: host tests, then the `espressif/idf` container builds the full environment matrix on every push.

---

## 14. Delivery phases

| Phase | Scope | Build env |
|-------|-------|-----------|
| 0 | Repo, build system, host test harness, `core/geo`, `core/timebase`, `core/session` | host |
| 1 | Prototype: NEO-6M, MPU6050, e-paper, internal storage, BLE export, serial fallback, power states, supervisor, OTA over BLE | `moto_neo6m` |
| 2 | WiFi AP, web page over HTTP, track JSON upload, OTA over WiFi | `moto_neo6m_wifi` |
| 3 | RaceChrono DIY live streaming | `+CONN_BLE_RC` |
| 4 | M10 driver, PPS, magnetometer | `moto_m10` |
| 5 | SD storage driver | `*_sd` |
| 6 | Car variant (OLED), predictive delta, optional OLED moto variant | `car_*` |

Phase 1 is the subject of the first implementation plan.

---

## 15. Items to verify on hardware before Phase 1

1. Flash size of the dev board module (`esptool.py flash_id`). Partition table assumes 4 MB.
2. NEO-6M firmware version via `UBX-MON-VER` (NAV-PVT needs 7.03+).
3. TP4056 module has the DW01 + FS8205 protection circuit (6 pads).
4. The dev board accepts 3.3 V injected on its 3.3V pin without back-feeding (no series diode issues).
5. MPU6050 authenticity via self-test registers.
6. Exact e-paper panel and controller (SSD1680 assumed).
7. For the M10-25Q later: presence of a TIMEPULSE pad, onboard LDO dropout for direct LiPo feed, and whether a backup capacitor exists.
