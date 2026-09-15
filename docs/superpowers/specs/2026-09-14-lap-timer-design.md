# ESP32 Lap Timer — Design Specification

**Date:** 2026-09-14
**Status:** Source of truth for all implementation work. Changes to behaviour require a change to this document first.
**Target:** Motorcycle variant first (e-paper), car variant later (OLED)

---

## 0. Document conventions

- **Units.** Time in int64 microseconds unless a field name says otherwise (`_ms`, `_s`). Distance in metres, speed in m/s internally; display and export convert. Angles in degrees for display, radians in the fusion math. Acceleration in g (9.80665 m/s²) at the API boundary, m/s² inside fusion.
- **Naming.** Core C symbols are prefixed by component: `geo_`, `tb_` (timebase), `fus_`, `lap_`, `drag_`, `trk_`, `ses_`, `exp_`, `cfg_`. HAL functions are prefixed by interface: `gps_`, `imu_`, `disp_`, `sto_`, `board_`, `conn_`. App-layer tasks: `pipeline_`, `logger_`, `ui_`, `power_`, `sup_`, `ota_`.
- **Byte order.** All binary formats little-endian. All on-flash structs are `__attribute__((packed))` with explicit-width integers.
- **Constants.** Every threshold named in this document appears in Appendix A with its value. Code references the constant, never the literal.
- **MUST / SHOULD / MAY** carry their RFC 2119 meanings.
- **Verify-on-hardware** items are marked **[VERIFY]**. They are assumptions that must be confirmed on the bench before the dependent code is considered done.

---

## 1. Overview

A self-contained, battery-powered GPS + IMU lap timer for track days and drag runs, built on an ESP32 with ESP-IDF in C11. It detects which circuit and layout it is on, times laps and sectors, times 0–100 / 1/4-mile style drag runs, records lean angle and g-forces, stores sessions on-device, and exports them to a phone in RaceChrono-compatible formats over BLE. It works with no external service, runs for many track days on one charge, and keeps timing through sensor faults, resets, and power loss.

### 1.1 Goals

- Lap, sector, and drag timing accurate to the limit of the GPS module: ±0.03–0.05 s on the 5 Hz NEO-6M prototype, ±0.01–0.02 s on a 10 Hz M10.
- Fully independent operation: no phone, no network, no cloud.
- Battery life measured in track days: ≥ 20 track days per charge on the 2×18650 pack.
- Robustness first: watchdogs, per-subsystem recovery ladders, session continuity through resets, crash-loop safe mode, signed OTA with rollback.
- One codebase, compile-time hardware selection, zero dead code per build.
- Pure-C algorithm core with no ESP-IDF dependency, unit-tested and log-replayable on the host.

### 1.2 Non-goals (v1)

- Native phone app.
- Cloud sync or accounts.
- Video overlay generation (RaceChrono does this after import).
- CAN-bus vehicle data.
- Live predictive delta on e-paper (see O5).
- Secure Boot with eFuse burn (signed-update verification only).

---

## 2. Requirements

### 2.1 Functional (MUST)

| ID | Requirement | Acceptance |
|----|-------------|-----------|
| F1 | Lap mode times laps by GPS line crossing. Riding screen shows best, previous, current-at-last-gate, and sector delta. | Replay of a recorded 10-lap session yields 10 lap times within ±50 ms of reference at 5 Hz. Screen refreshes at each gate only. |
| F2 | Sector timing with configurable gate lines per layout; theoretical best lap from best sectors. | Sector splits sum to lap time within 1 ms. |
| F3 | Automatic venue detection by proximity; automatic layout detection (full/short, forward/reverse) during lap 1; manual override. | Killarney fixtures (four layouts) each lock the correct layout by end of lap 1. |
| F4 | Drag mode: speed benches and distance gates with ET and trap speed using NHRA conventions; optional 1 ft rollout; only benches actually reached are shown. | Synthetic 0–300 km/h run reports every bench within ±20 ms of analytic truth; a run peaking at 180 km/h shows only 0–100 and 1/4. |
| F5 | Record lean angle (moto), longitudinal g, lateral g, combined g, yaw rate, speed, heading, altitude, fix quality, per-lap maxima. | Synthetic steady 30° turn converges to 30° ± 1° within 1.5 s. |
| F6 | Three-button UI: mode change, pages, menu. Menu locked out above 10 km/h. | Manual test script in §22.6. |
| F7 | On-device track creation: mark start/finish and sector gates while riding. | A created venue produces laps on the next pass and persists across power cycles. |
| F8 | Persist sessions, laps, sample logs on-device; summaries survive any log damage. | Truncate a log file mid-frame: summary intact, replay resyncs, ≤ 5 s samples lost. |
| F9 | Export over BLE to a phone (Web Bluetooth page) as VBO and NMEA; RaceChrono imports them. USB serial fallback. | RaceChrono imports the exported VBO and shows the same laps ± 0.1 s. |
| F10 | Automatic power management: ACTIVE / PIT / PARK / SHUTDOWN, motion wake. | Measured currents within 25 % of §16.5 budget. |
| F11 | Self-healing: watchdogs, recovery ladders, crash-loop safe mode, session continuity through resets. | `dbg crash` mid-lap: device reboots, lap continues, flagged `interrupted`, time within 50 ms of an uninterrupted control. |
| F12 | Firmware update over BLE (later WiFi): signed image, hardware-id check, rollback on failed validation. | Corrupt image refused; wrong hwid refused; image that panics at boot rolls back automatically. |

### 2.2 Optional (planned, build-flagged)

| ID | Requirement |
|----|-------------|
| O1 | WiFi AP with web page over HTTP: same export, config editing, track JSON upload, OTA. |
| O2 | RaceChrono DIY BLE live streaming (GPS + IMU channels). |
| O3 | u-blox M10 GPS driver (SEQURE M10-25Q) at 10 Hz with PPS and QMC5883L magnetometer. |
| O4 | microSD storage driver with summaries mirrored to internal flash. |
| O5 | Live predictive lap delta on an OLED/memory-LCD moto variant and the car variant. |
| O6 | Car variant: OLED display, 10 Hz live screens, no lean angle. |
| O7 | Passkey BLE pairing. |

### 2.3 Constraints

| ID | Constraint |
|----|-----------|
| C1 | No external connection of any kind required to operate. |
| C2 | Power efficient; ≥ 20 track days per charge. |
| C3 | Lightweight and compact for motorcycle mounting. |
| C4 | Uptime and accuracy over features. Degrade, never stop timing. |
| C5 | Hardware limited to what is affordable and available in South Africa; prototype uses owned parts. |
| C6 | ESP-IDF, C11, smallest practical firmware. |
| C7 | Moto and car are separate builds from one codebase via build flags. Never both hardware paths in one binary. |
| C8 | All algorithm code compiles and runs on the host without ESP-IDF. |

### 2.4 Decisions made during design

| Topic | Decision | Reason |
|-------|----------|--------|
| GPS | Keep NEO-6M for prototype; M10 as compile-time driver swap | Budget, availability |
| Board | Keep ESP32 CH340 dev board; external regulator + LED removal | Availability |
| IMU | Keep MPU6050; boot self-test guards clones | Adequate specs |
| Display | SSD1680 e-paper, event-driven refresh | Sunlight readability, zero idle power, SA stock |
| Battery | 2×18650 3000 mAh INR parallel, TP4056 | Owned |
| Storage | Internal LittleFS first, SD as flag | Owned |
| Track DB | Bundled SA circuits + on-device creation; JSON upload later | No open global DB |
| Connectivity | BLE + Web Bluetooth page, serial fallback, WiFi then RaceChrono live later | No app needed |
| Framework | ESP-IDF, C11 | Size, efficiency |
| Architecture | Dual-core: pipeline on core 1, system on core 0 | Timing isolation |
| Riding screens | Single-glance content only | Rider safety |
| OTA | Two OTA slots, signed update, rollback | F12 |

---

## 3. Hardware

### 3.1 Prototype bill of materials

| Item | Part | Interface | Notes |
|------|------|-----------|-------|
| MCU | ESP32 DevKit V1 (ESP32-WROOM-32, CH340) | — | Flash 4 MB, verified 2026-09-15 with esptool flash_id (ESP32-D0WD-V3 rev 3.1, 40 MHz crystal, VRef calibration in eFuse). Dual core 240 MHz, 520 KB SRAM, 8 KB slow RTC RAM. |
| GPS | GY-NEO6M v2 (u-blox NEO-6M) | UART, 3.3 V logic | 5 Hz max nav rate, ~40 mA, onboard LDO + backup cell (MS621FE) + EEPROM, no PPS pin. Firmware **[VERIFY]** via `UBX-MON-VER` (NAV-PVT needs 7.03). |
| IMU | GY-521 (MPU6050) | I2C 400 kHz, addr 0x68 (AD0 low) | 16-bit accel ±16 g, gyro ±2000 dps, 1 KB FIFO, motion interrupt. Authenticity **[VERIFY]** via self-test. |
| Display | Waveshare-class SSD1680 e-paper 2.13" (250×122) or 2.9" (296×128) | SPI mode 0, ≤ 20 MHz | Exact panel **[VERIFY]**; driver parameterised by panel table. |
| Battery | 2×18650 3000 mAh INR 15 A, 2-slot holder | — | Wired in parallel (1S2P) = 6000 mAh nominal 3.7 V. |
| Charger | TP4056 module, 6-pad version | micro-USB / USB-C in | **[VERIFY]** has DW01A + FS8205A protection (pads B+, B−, OUT+, OUT−, IN+, IN−). |
| Regulator | XC6220B331MR or AP2112K-3.3 | — | ≥ 600 mA, ≤ 60 µA quiescent, ≤ 0.2 V dropout at 500 mA. Input from OUT+ of TP4056. 10 µF in, 10 µF out. |
| GPS power switch | P-MOSFET AO3401A or SI2301, 100 kΩ gate pull-up to battery | GPIO 26 | Gate low = GPS on. |
| Battery sense | 470 kΩ + 470 kΩ divider, 100 nF to GND at tap | GPIO 34 | ~4 µA continuous. |
| Buttons | 3× sealed tactile or 12 mm momentary, glove-friendly | GPIO 32/33/25 | Active-high: 3.3 V through switch to pin, 100 kΩ pull-down to GND at pin. |
| Bulk cap | 470 µF electrolytic on 3.3 V rail | — | Absorbs ESP32 radio bursts. |

### 3.2 Dev board power modifications

Stock DevKit deep-sleep draw is 5–15 mA (AMS1117 quiescent ~5 mA, power LED ~2 mA, CH340 on the 5 V rail). Required modifications:

1. Feed the external regulator's 3.3 V output into the board's `3V3` pin. With nothing on `VIN`/USB, the AMS1117 and CH340 are unpowered.
2. Remove the red power LED (desolder, or cut its series resistor trace).
3. **Never** connect USB while the battery regulator is driving `3V3` unless a Schottky diode (e.g. SS14) is fitted in series on the regulator output. Bench flashing: disconnect the battery pack first.
4. Expected deep-sleep draw after modification: ≤ 150 µA total (ESP32 ~10 µA, regulator ≤ 60 µA, divider 4 µA, MPU6050 cycle mode ~10 µA, TP4056 protection ~3 µA, GPS off).

### 3.3 Pin map (ESP32 DevKit V1)

| Function | GPIO | Direction | Electrical | Notes |
|----------|------|-----------|-----------|-------|
| GPS UART2 RX (from GPS TX) | 16 | in | 3.3 V | `UART_NUM_2` |
| GPS UART2 TX (to GPS RX) | 17 | out | 3.3 V | |
| GPS PPS / TIMEPULSE | 36 | in | rising edge | Input-only (SENSOR_VP). Unused on NEO-6M v2. |
| GPS power enable | 26 | out | drive low = on | RTC GPIO; `rtc_gpio_hold_en` keeps level in deep sleep (held high = off). |
| I2C SDA | 21 | bidir | 4.7 kΩ pull-up (on GY-521) | MPU6050; QMC5883L later at 0x0D |
| I2C SCL | 22 | out | 4.7 kΩ pull-up | 400 kHz |
| IMU INT | 27 | in | active-high push-pull | RTC GPIO, EXT1 wake |
| SPI SCK | 18 | out | | VSPI, shared e-paper / SD |
| SPI MOSI | 23 | out | | |
| SPI MISO | 19 | in | | SD only; unused by e-paper |
| E-paper CS | 5 | out | idle high | Boot strapping pin; idle-high is safe |
| E-paper DC | 14 | out | | MTMS; emits a short clock at boot, harmless |
| E-paper RST | 4 | out | active low | |
| E-paper BUSY | 35 | in | high = busy | Input-only |
| SD CS (O4) | 15 | out | idle high | MTDO strapping; idle-high pull-up is boot-safe |
| Button MODE | 32 | in | active-high, 100 kΩ pull-down | RTC GPIO, EXT1 wake |
| Button UP | 33 | in | active-high, 100 kΩ pull-down | RTC GPIO, EXT1 wake |
| Button DOWN | 25 | in | active-high, 100 kΩ pull-down | RTC GPIO, EXT1 wake |
| Battery ADC | 34 | analog | 0–2.1 V at tap | `ADC1_CHANNEL_6`, 11 dB attenuation. ADC2 is unusable with WiFi. |
| Charger CHRG (optional) | 39 | in | open-drain active-low | EXT0 wake on low. 100 kΩ pull-up. |
| Console / serial export | 1 (TX), 3 (RX) | | | `UART_NUM_0` via CH340 |

Forbidden: GPIO 0, 2, 12 (strapping), 6–11 (flash). GPIO 12 MUST never see a pull-up at boot (selects 1.8 V flash and bricks the boot).

**Wake polarity constraint.** Classic ESP32 EXT1 wake is either `ESP_EXT1_WAKEUP_ANY_HIGH` or `ESP_EXT1_WAKEUP_ALL_LOW` across its whole pin mask. Buttons are therefore active-high and the MPU6050 interrupt is configured active-high (`INT_PIN_CFG.INT_LEVEL = 0`), so EXT1 uses `ANY_HIGH` over mask {27, 32, 33, 25}. The charger pin uses EXT0 (single pin, level 0). RTC pull-downs on 32/33/25 are enabled in addition to the external resistors before entering deep sleep.

### 3.4 Wiring diagram (textual)

```
18650 x2 (parallel) ── B+/B− ── TP4056 ── OUT+ ──┬── XC6220 IN ── OUT ── 3V3 pin (ESP32) ── 470µF
                                                 │                        ├── MPU6050 VCC
                                                 │                        ├── e-paper VCC
                                                 └── P-MOSFET S ── D ── GY-NEO6M VCC
                                                        G ── 100k ── OUT+   G ── GPIO26
Battery tap: OUT+ ── 470k ──┬── 470k ── GND
                            └── 100nF ── GND
                            └── GPIO34
GPS TX ── GPIO16, GPS RX ── GPIO17
MPU SDA ── GPIO21, SCL ── GPIO22, INT ── GPIO27
E-paper: SCK 18, DIN 23, CS 5, DC 14, RST 4, BUSY 35
Buttons: 3V3 ── switch ── GPIO32/33/25 ──100k── GND
```

### 3.5 Upgrade paths (compile-time drivers)

| Component | Prototype driver | Upgrade driver | Interface impact |
|-----------|------------------|----------------|------------------|
| GPS | `gps_neo6m` — 5 Hz, 38400 baud, legacy `UBX-CFG-*` | `gps_m10` — 10 Hz, 115200 baud, `UBX-CFG-VALSET`, PPS on GPIO 36, QMC5883L on I2C | None; driver profile struct carries rate/baud/backend |
| Storage | `storage_internal` — LittleFS | `storage_sd` — FAT on shared VSPI, CS 15 | None; `storage.h` |
| Display | `display_epaper_ssd1680` | `display_oled_ssd1309` | None; `display.h` + variant UI layouts |
| Connectivity | `conn_ble` + `export_serial` | + `conn_wifi`, + `conn_ble_rc` | None; `conn.h` command handler |
| Board | `board_devkit_v1` | `board_<new>` | `board.h` pin map + power hooks |

**SEQURE M10-25Q notes.** u-blox M10 chipset, 1–10 Hz configurable (10 Hz default), UART 115200, NMEA + UBX, 5 V input pin, −40 to +85 °C, 12.2 g, 25×25×8 mm, QMC5883L on the same SH1.0-6P connector (5V, GND, TX, RX, SDA, SCL). **[VERIFY]** onboard LDO dropout for direct 3.5–4.2 V LiPo feed; **[VERIFY]** TIMEPULSE pad on the PCB; **[VERIFY]** backup capacitor presence. Without a backup capacitor the driver keeps the module powered and uses UBX power-save rather than the MOSFET cut.

---

## 4. Software architecture

### 4.1 Layering

```
┌───────────────────────────────────────────────────────────────┐
│ app/  (IDF-dependent glue: tasks, queues, sleep, NVS, OTA)     │
├───────────────────────────────────────────────────────────────┤
│ drivers/  (one per HAL interface, selected by CMake)           │
├───────────────────────────────────────────────────────────────┤
│ hal/  (interface headers only, no code)                        │
├───────────────────────────────────────────────────────────────┤
│ core/  (pure C11, no IDF, no FreeRTOS, no malloc, host-tested) │
└───────────────────────────────────────────────────────────────┘
```

Rules:
- `core/` MUST NOT include any ESP-IDF, FreeRTOS, or driver header. It receives time as arguments and returns results through out-parameters and callbacks. It MUST NOT call `malloc`. All state lives in caller-provided structs.
- `drivers/` implement exactly one `hal/*.h` each. They MAY use IDF. They MUST NOT include each other or `app/`.
- `app/` owns tasks, queues, and the mapping from HAL events to core calls.
- `hal/` headers are the only coupling point between `app/` and `drivers/`.

### 4.2 Repository layout

```
lap-timer/
  CMakeLists.txt                 top-level: variant selection, driver dirs, build_config.h generation
  sdkconfig.defaults             shared IDF options
  sdkconfig.defaults.moto        variant fragment
  sdkconfig.defaults.car         variant fragment
  partitions.csv                 OTA-capable partition table (§19.1)
  build.sh                       ./build.sh <env> [build|flash|monitor|clean|size]
  .idf-version                   pinned ESP-IDF tag, e.g. v5.3.2
  .gitignore
  main/
    CMakeLists.txt
    app_main.c                   boot sequence (§4.7), task spawn
    build_config.h.in            template for generated header
  components/
    core/
      CMakeLists.txt             idf_component_register(SRCS ... INCLUDE_DIRS include)
      include/core/*.h           public API (§5.2); core/types.h holds gps_fix_t, imu_raw_t, fused_sample_t, lap_result_t, drag_result_t (HAL headers include it)
      timebase/tb.c
      geo/geo.c
      fusion/fus.c  fusion/fus_calib.c
      lapengine/lap.c  lapengine/lap_gate.c
      dragengine/drag.c
      tracks/trk.c  tracks/trk_bundled.c (generated)
      session/ses_frame.c  ses_records.c  ses_crc.c
      export/exp_vbo.c  exp_nmea.c  exp_json.c
      config/cfg.c  cfg_json.c (jsmn vendored in include/core/jsmn.h; jw.c minimal JSON writer)
      util/ring.h  util/jw.c  util/bw.h (byte writer/reader for packed records)
      ui/render.c  ui/fonts.c (generated)  ui/screens_moto.c  ui/screens_car.c  ui/model.h   pure-C framebuffer renderer and screens; host tests snapshot to PBM
    hal/
      CMakeLists.txt             INTERFACE component
      include/hal/gps.h imu.h display.h storage.h board.h conn.h
    drivers/
      gps_neo6m/     gps_ubx_common/ (shared UBX framing used by both GPS drivers)
      gps_m10/
      gps_sim/       bench driver: replays /sim/gps.ubx or synthesises a circuit
      imu_mpu6050/
      imu_sim/       bench driver: replays /sim/imu.bin or synthesises samples
      display_epaper_ssd1680/
      display_oled_ssd1309/
      storage_internal/
      storage_sd/
      conn_ble/
      conn_wifi/
      conn_ble_rc/
      export_serial/
      board_devkit_v1/
    app/
      pipeline/pipeline.c
      logger/logger.c
      ui/ui.c ui_buttons.c        task, event handling, display glue (renderer and screens live in core/ui)
      power/power.c power_battery.c
      supervisor/sup.c sup_errlog.c
      ota/ota.c
      cmd/cmd.c                  transport-agnostic command handler (§18.1)
  tools/
    web/export.html
    replay/CMakeLists.txt replay.c
    tracks/*.json  tracks/gen_tracks.py
    fonts/gen_fonts.py
    gps_sim.py
    serial_export.py
    sign_release.sh
  test/
    CMakeLists.txt               host build of core + Unity
    unity/                       vendored Unity
    test_geo.c test_tb.c test_fus.c test_lap.c test_drag.c test_ses.c test_exp.c test_trk.c test_cfg.c
    data/                        fixtures (§22.3)
  test_apps/                     on-target driver tests
  docs/superpowers/specs/
```

### 4.3 Task map

| Task | Core | Priority | Stack (bytes) | Period / trigger | Responsibility |
|------|------|----------|---------------|------------------|----------------|
| `pipeline` | 1 | 20 | 8192 | queue set: UART events, 50 ms IMU timer, command queue | GPS + IMU ingest, timestamping, fusion, lap/drag engines, event emission |
| `supervisor` | 0 | 22 | 3072 | 1000 ms | heartbeats, ladders, error log, safe mode |
| `logger` | 0 | 8 | 4096 | ring notify or 1000 ms | drain rings to storage in 4 KB batches, fsync every 2 s, summaries |
| `ui` | 0 | 6 | 6144 | event queue, button queue | screens, menu, display refresh |
| `conn` | 0 | 5 | 6144 | BLE callbacks, command queue | command handler, file streaming, OTA |
| `power` | 0 | 4 | 3072 | 1000 ms | battery ADC, idle timers, state transitions, sleep entry |
| NimBLE host | 0 | 21 (IDF default) | 4096 | stack | BLE stack |
| IDF timer / ipc / idle | — | — | — | — | IDF-owned |

Priorities are FreeRTOS numeric (higher = more urgent). `configMAX_PRIORITIES` = 25. Stack sizes are initial values; §22.4 measures high-water and the final value is high-water + 25 %.

### 4.4 Inter-task communication

| Channel | Type | Producer → Consumer | Depth | Item |
|---------|------|---------------------|-------|------|
| `fix_ring` | SPSC ring | pipeline → logger | 32 | `gps_fix_t` (56 B) |
| `fused_ring` | SPSC ring | pipeline → logger | 64 | `fused_sample_t` (24 B) |
| `evt_q` | FreeRTOS queue | pipeline → ui, logger, power (broadcast via `xQueueSend` to three queues, one per consumer) | 16 each | `event_t` (32 B) |
| `cmd_q` | FreeRTOS queue | ui, conn, power → pipeline | 8 | `command_t` (24 B) |
| `btn_q` | FreeRTOS queue | button ISR → ui | 8 | `button_evt_t` (4 B) |
| `uart_q` | IDF UART event queue | UART ISR → pipeline | 16 | `uart_event_t` |
| `ui_req_q` | FreeRTOS queue | conn, power, supervisor → ui | 8 | `ui_request_t` (16 B) |
| `log_req_q` | FreeRTOS queue | power, conn → logger | 4 | `log_request_t` (16 B): open/close session, rebuild summary, evict |
| `hb[]` | volatile uint32 array | every task → supervisor | 6 | heartbeat counters |
| `sys_flags` | atomic uint32 | supervisor, drivers → all | — | fault flags (§17.4) |

SPSC rings are implemented in `components/core/include/core/ring.h` as a header-only lock-free ring with `head`/`tail` `_Atomic uint32_t`, power-of-two capacity, overwrite-oldest policy for `fused_ring` (sample loss is tolerable) and drop-newest with a counter for `fix_ring` (fix loss is logged); the consumer publishes its pop with a compare-and-swap on `tail` so an eviction that races a read is detected and retried, never returned torn.

### 4.5 Events and commands

`event_t { uint8_t type; uint8_t flags; uint16_t arg16; int64_t gps_us; int64_t mono_us; uint32_t arg32; uint32_t arg32b; }`

| Event | Payload | Emitter |
|-------|---------|---------|
| `EV_VENUE_FOUND` | arg16 = venue id | lapengine |
| `EV_LAYOUT_LOCKED` | arg16 = layout id | lapengine |
| `EV_ARMED` | — | lapengine / dragengine |
| `EV_SECTOR` | arg16 = sector idx, arg32 = split ms, arg32b = delta ms vs best (int32) | lapengine |
| `EV_LAP_COMPLETE` | arg16 = lap no, arg32 = lap ms, flags = lap flags | lapengine |
| `EV_FIX_LOST` / `EV_FIX_OK` | — | pipeline |
| `EV_DRAG_ARMED` / `EV_DRAG_LAUNCH` | — | dragengine |
| `EV_DRAG_GATE` | arg16 = gate id, arg32 = ms, arg32b = speed cm/s | dragengine |
| `EV_DRAG_DONE` | arg16 = run no | dragengine |
| `EV_MOTION` / `EV_STILL` | — | pipeline (speed-based) |
| `EV_CALIB_DONE` | arg16 = stage | fusion |
| `EV_FAULT` | arg16 = error code | supervisor |

`command_t { uint8_t type; uint8_t arg8; uint16_t arg16; int32_t arg32; double lat; double lon; }`

| Command | Payload |
|---------|---------|
| `CMD_SET_MODE` | arg8 = MODE_LAP / MODE_DRAG |
| `CMD_SET_LAYOUT` | arg16 = layout id |
| `CMD_MARK_GATE` | arg8 = 0 for S/F, n for sector n |
| `CMD_CALIB_ORIENT` | — |
| `CMD_RESET_ENGINE` | — |
| `CMD_CONFIG_RELOAD` | — |
| `CMD_GPS_POWER` | arg8 = 0/1 (from power task) |
| `CMD_IMU_MODE` | arg8 = IMU_FULL / IMU_LOWPOWER |

### 4.6 Build flags

| Variable | Values | Default |
|----------|--------|---------|
| `VARIANT` | `moto`, `car` | required |
| `GPS` | `neo6m`, `m10`, `sim` | required |
| `IMU` | `mpu6050`, `sim` | `mpu6050` |
| `DISPLAY` | `epaper_ssd1680`, `oled_ssd1309` | required |
| `STORAGE` | `internal`, `sd` | `internal` |
| `CONN` | `ble`, `wifi`, `ble_wifi` | `ble` |
| `CONN_BLE_RC` | `ON`/`OFF` | `OFF` |
| `EXPORT_SERIAL` | `ON`/`OFF` | `ON` |
| `PANEL` | `ws213v4`, `ws29v2` | `ws29v2` |

`GPS=sim` and `IMU=sim` select bench drivers (`gps_sim`, `imu_sim`) that implement the HAL by replaying a capture file from LittleFS (`/sim/gps.ubx`, `/sim/imu.bin`) at real-time rate, or generating a synthetic circuit when no file exists. They exist so the whole firmware can be developed and bench-tested before the physical sensors are available. They are never part of a release build (`sign_release.sh` refuses them).

Validation in CMake: `moto` requires `DISPLAY` ∈ {epaper_ssd1680, oled_ssd1309}; `car` requires `oled_ssd1309`; `CONN_BLE_RC=ON` requires `CONN` containing `ble`; `GPS=m10` enables PPS handling. Invalid combinations fail configuration with a message.

Generated `build_config.h`:
```c
#define CFG_VARIANT_MOTO 1        /* or CFG_VARIANT_CAR */
#define CFG_GPS_NAME "neo6m"
#define CFG_DISPLAY_NAME "epaper_ssd1680"
#define CFG_PANEL_WS29V2 1
#define CFG_HWID "moto_neo6m_epaper"   /* used by OTA hardware-id check */
#define CFG_FW_VERSION "v0.3.1-7-gabc123"
#define CFG_HAS_PPS 0
#define CFG_HAS_SD 0
#define CFG_HAS_WIFI 0
#define CFG_HAS_BLE 1
#define CFG_HAS_BLE_RC 0
#define CFG_FUSED_LOG_HZ 10
```

Named environments: `moto_neo6m` (= moto, neo6m, epaper, internal, ble), `moto_sim` (= moto, sim, sim, epaper_ssd1680, internal, ble; bench builds until sensors arrive), `moto_neo6m_wifi`, `moto_m10`, `moto_m10_sd`, `car_neo6m`, `car_m10`. `build.sh` maps names to flag sets and uses `-B build/<env>`.

### 4.7 Boot sequence (`app_main`)

1. `esp_reset_reason()` recorded. Brownout, WDT, and panic reasons increment NVS counters.
2. NVS init (`nvs_flash_init`; on `ESP_ERR_NVS_NO_FREE_PAGES` or version mismatch: erase and re-init, log `E_SYS_CFG_RESET`).
3. Boot counter incremented. Crash-loop check (§17.5): if three abnormal resets within 60 s of uptime each, set `SAFE_MODE`.
4. RTC memory state validated (CRC + version). Valid and younger than `RTC_RESUME_MAX_S` → resume flags set.
5. Config loaded from NVS; validated; defaults on failure.
6. Board init: GPIO directions, GPS power on, I2C bus, SPI bus, ADC calibration.
7. Storage mount (ladder §17.3). Sessions directory ensured.
8. Display init; boot screen "LapTimer vX.Y.Z" + self-test line placeholders.
9. Self-test (§17.6). Results rendered and logged.
10. Task WDT init (5 s, panic on timeout). Supervisor task started first.
11. Queues and rings created (static allocation via `xQueueCreateStatic`).
12. Pipeline task started on core 1. Pipeline initialises GPS driver (config push) and IMU driver (register config, calibration load).
13. Logger, ui, power tasks started on core 0. Conn task started only if `CONN` enabled; BLE stack initialised lazily on first CONNECTED entry to save RAM at boot.
14. OTA: if the running image is `ESP_OTA_IMG_PENDING_VERIFY`, the validation timer starts (§19.4).
15. Power task enters ACTIVE if resume flags say the session was active and motion is present, else PIT.

Boot-to-pipeline-running target: ≤ 1.5 s from reset (excluding e-paper boot screen, which renders asynchronously).

### 4.8 Memory budget (classic ESP32, 320 KB usable DRAM)

| Consumer | Estimate |
|----------|----------|
| IDF + FreeRTOS + drivers baseline | ~60 KB |
| NimBLE host + controller (when active) | ~70 KB |
| Task stacks (§4.3) | ~35 KB |
| Rings and queues | ~5 KB |
| Framebuffer (296×128/8) | 4.7 KB |
| Logger batch buffer | 4 KB |
| UART RX ring | 2 KB |
| LittleFS cache/lookahead | ~2 KB |
| Export streaming window | 1 KB |
| Lap engine (venue + 8 layouts × 16 gates) | ~3 KB |
| Predictive delta table (O5) | 2.4 KB |
| Headroom | > 100 KB |

Hard rule: `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` MUST stay above 40 KB during BLE transfer; supervisor logs `E_SYS_HEAP_LOW` below that and disables BLE below 20 KB.

---

## 5. Interfaces

### 5.1 HAL headers

All HAL functions return `int` (0 = OK, negative = `-errno`-style code from `hal/hal_err.h`) unless noted. All are called from one task only, as noted, and are not reentrant.

#### `hal/gps.h` (called from pipeline)

```c
typedef struct {
    int64_t  gps_us;      /* UTC microseconds since Unix epoch; 0 if time invalid */
    int64_t  mono_us;     /* arrival time of the last byte of the message */
    int32_t  lat_e7;      /* degrees * 1e7 */
    int32_t  lon_e7;
    int32_t  alt_mm;      /* height above MSL */
    int32_t  gspeed_mms;  /* ground speed, mm/s (Doppler) */
    int32_t  head_e5;     /* heading of motion, degrees * 1e5, 0..36e6 */
    uint32_t hacc_mm;
    uint32_t sacc_mms;
    uint16_t pdop_e2;
    uint8_t  fix_type;    /* 0 none, 2 2D, 3 3D */
    uint8_t  sats;
    uint8_t  flags;       /* bit0 gnssFixOK, bit1 time_valid, bit2 date_valid */
    uint8_t  valid;       /* result of core validity rule, filled by pipeline */
} gps_fix_t;

typedef struct {
    uint8_t  max_rate_hz;      /* 5 for NEO-6M, 10 for M10 */
    uint32_t baud;             /* 38400 / 115200 */
    uint8_t  has_pps;
    const char *name;
} gps_profile_t;

int  gps_init(const gps_profile_t **out_profile);   /* UART setup, no config push */
int  gps_configure(uint8_t rate_hz);                 /* push full config (§7.3 / §7.4); blocks ≤ 2 s */
int  gps_poll(gps_fix_t *out);                       /* non-blocking; returns 1 if a new fix was assembled, 0 if none, <0 error */
int  gps_feed_bytes(const uint8_t *buf, size_t n);   /* pipeline pushes UART bytes; parser runs here */
int  gps_set_power_mode(uint8_t mode);               /* GPS_PM_FULL, GPS_PM_CYCLIC_1HZ, GPS_PM_BACKUP */
int  gps_wake(void);                                 /* from BACKUP */
int  gps_get_version(char *buf, size_t n);           /* UBX-MON-VER swVersion; used by self-test */
int  gps_reinit_uart(uint32_t baud);                 /* ladder step */
uint32_t gps_stats_frames_ok(void);
uint32_t gps_stats_frames_bad(void);
int64_t  gps_last_frame_mono_us(void);
```

#### `hal/imu.h` (called from pipeline)

```c
typedef struct {
    int64_t mono_us;
    int16_t ax, ay, az;   /* raw LSB, ±16 g range → 2048 LSB/g */
    int16_t gx, gy, gz;   /* raw LSB, ±2000 dps → 16.4 LSB/dps */
} imu_raw_t;

int  imu_init(void);                                  /* bus + WHO_AM_I + register config (§8.3) */
int  imu_self_test(uint8_t *pass_mask);               /* §8.5; bit per axis */
int  imu_read_fifo(imu_raw_t *out, size_t max, size_t *n_read, int64_t read_mono_us);
int  imu_read_temp_c100(int16_t *out);                /* °C * 100 */
int  imu_set_mode(uint8_t mode);                      /* IMU_FULL (100 Hz FIFO) / IMU_LOWPOWER (40 Hz accel, motion INT) */
int  imu_set_motion_threshold(uint8_t thr_lsb, uint8_t dur_ms);
int  imu_recover(void);                               /* bus recovery + reinit; ladder step */
int  imu_int_pending(void);                           /* reads INT_STATUS; clears */
```

#### `hal/display.h` (called from ui)

```c
typedef struct {
    uint16_t width, height;      /* logical, after rotation */
    uint8_t  partial_ok;
    int8_t   temp_min_c, temp_max_c;
    uint16_t full_refresh_ms, partial_refresh_ms;   /* nominal */
} disp_caps_t;

int  disp_init(const disp_caps_t **caps);
int  disp_blit(const uint8_t *fb);                   /* full framebuffer, 1 bpp, row-major, MSB = leftmost */
int  disp_refresh(uint8_t mode);                     /* DISP_PARTIAL / DISP_FULL; blocks until BUSY clears or timeout (returns -ETIMEDOUT) */
int  disp_sleep(void);
int  disp_wake(void);
int  disp_reinit(void);                              /* ladder step */
```

#### `hal/storage.h` (called from logger and cmd)

```c
typedef struct { uint32_t total_kb, free_kb; uint8_t degraded; } sto_info_t;
typedef int sto_file_t;

int  sto_mount(void);                 /* ladder inside driver: mount → retry → format (internal) / degrade (sd) */
int  sto_info(sto_info_t *out);
int  sto_open(const char *path, int flags, sto_file_t *out);   /* flags: STO_RD, STO_WR|STO_APPEND|STO_CREATE */
int  sto_write(sto_file_t f, const void *buf, size_t n);
int  sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read);
int  sto_seek(sto_file_t f, uint32_t offset);
int  sto_sync(sto_file_t f);
int  sto_close(sto_file_t f);
int  sto_rename(const char *from, const char *to);              /* atomic on LittleFS */
int  sto_unlink(const char *path);
int  sto_list(const char *dir, void (*cb)(const char *name, uint32_t size, void *ctx), void *ctx);
int  sto_probe(void);                 /* self-test: write+read+unlink probe file */
```

Paths are `/sessions/<id>.log`, `/sessions/<id>.sum`, `/tracks/user.bin`. On `storage_sd`, the same paths resolve on the SD card and `.sum` files are additionally mirrored to internal LittleFS at the same path.

#### `hal/board.h` (called from app tasks)

```c
int  board_init(void);
int  board_gps_power(bool on);
int  board_battery_read_mv(uint16_t *mv);          /* 64-sample average, calibrated */
int  board_charger_present(bool *out);             /* CHRG pin, or false if not wired */
int  board_buttons_read(uint8_t *mask);            /* bit0 MODE, bit1 UP, bit2 DOWN */
int  board_buttons_enable_isr(void (*cb)(uint8_t mask, int64_t mono_us));
int  board_prepare_deep_sleep(void);               /* holds, EXT0/EXT1 masks, RTC pulls */
int  board_pps_enable(void (*cb)(int64_t mono_us));   /* no-op when CFG_HAS_PPS == 0 */
const char *board_name(void);
```

#### `hal/conn.h` (called from conn task; transports implement)

```c
typedef int (*conn_rx_cb_t)(const uint8_t *frame, size_t n);   /* cmd handler entry, runs in conn task */
int  conn_init(conn_rx_cb_t rx);
int  conn_start(void);                 /* advertise / AP up */
int  conn_stop(void);
int  conn_send(uint8_t tag, uint16_t chunk_seq, uint8_t flags, const uint8_t *data, size_t n);  /* blocks until queued; -EAGAIN if not connected */
int  conn_status_update(const uint8_t *status20);
bool conn_is_connected(void);
int64_t conn_last_activity_mono_us(void);
```

### 5.2 Core API (pure C)

#### `core/tb.h` — timebase

```c
typedef struct { int64_t offset_us; int64_t window_min_us; int64_t window_start_mono_us; uint8_t locked; uint8_t quality; } tb_t;
void    tb_init(tb_t *t);
void    tb_on_fix(tb_t *t, int64_t fix_gps_us, int64_t arrival_mono_us);   /* min-filter update (§6.2) */
void    tb_on_pps(tb_t *t, int64_t edge_mono_us, int64_t top_of_second_gps_us);
int64_t tb_mono_to_gps(const tb_t *t, int64_t mono_us);
bool    tb_locked(const tb_t *t);
```

#### `core/geo.h`

```c
typedef struct { double x, y; } geo_enu_t;                  /* metres */
typedef struct { double lat0_rad, lon0_rad, cos_lat0; } geo_origin_t;
void    geo_origin_set(geo_origin_t *o, double lat_deg, double lon_deg);
geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg);
double  geo_dist_m(double lat1, double lon1, double lat2, double lon2);   /* haversine */
/* segment intersection: returns 1 and fills t (fraction along AB) and side sign if AB crosses gate PQ properly */
int     geo_segment_cross(geo_enu_t a, geo_enu_t b, geo_enu_t p, geo_enu_t q, double *t_out, int *dir_sign_out);
double  geo_dist_point_segment(geo_enu_t x, geo_enu_t p, geo_enu_t q);
/* constant-acceleration interpolation: distance d along a segment traversed from speed v0 to v1 over dt */
double  geo_interp_time(double d, double v0, double v1, double dt);   /* returns τ in [0, dt] */
```

#### `core/fus.h` — fusion

```c
typedef struct { float r[9]; float gbias[3]; int16_t gbias_temp_c100; uint8_t orient_ok; uint8_t forward_ok; uint8_t version; } fus_calib_t;
typedef struct {
    int64_t mono_us;
    float   g_lon, g_lat, g_comb;   /* g, +lat = right */
    float   lean_deg;               /* + = right */
    float   yaw_dps;                /* earth-frame, + = left turn */
    uint8_t flags;                  /* FUS_LEAN_VALID, FUS_ORIENT_OK, FUS_STILL, FUS_DISAGREE */
} fused_sample_t;
typedef struct { /* internal state */ float lean_rad; float still_acc_var, still_gyr_var; ... } fus_t;

void fus_init(fus_t *f, const fus_calib_t *calib, uint8_t variant_is_moto);
void fus_set_gps_speed(fus_t *f, float v_mps, int64_t mono_us, bool valid);
/* process one raw sample; out is filled; returns 1 if a sample was produced */
int  fus_step(fus_t *f, const imu_raw_t *raw, fused_sample_t *out);
bool fus_is_still(const fus_t *f);
void fus_gyro_bias_update(fus_t *f);                 /* call when still ≥ 2 s; updates calib */
int  fus_calib_orient_capture(fus_t *f);             /* upright capture; fills r[] Z row */
int  fus_calib_forward_step(fus_t *f, float gps_acc_mps2, float yaw_dps);   /* learns X row */
const fus_calib_t *fus_calib(const fus_t *f);
```

#### `core/lap.h` — lap engine

```c
typedef struct { double lat, lon; } trk_pt_t;
typedef struct { trk_pt_t p1, p2; } trk_line_t;
typedef struct { uint16_t id; char name[24]; trk_line_t sf; int8_t dir_sign; uint8_t n_sectors; trk_line_t sectors[LAP_MAX_SECTORS]; uint32_t length_m; } trk_layout_t;
typedef struct { uint16_t id; char name[32]; double lat, lon; uint32_t radius_m; uint8_t n_layouts; trk_layout_t layouts[TRK_MAX_LAYOUTS]; } trk_venue_t;

typedef struct { uint16_t lap_no; int64_t start_gps_us; uint32_t time_ms; uint8_t flags; uint8_t n_sectors; uint32_t sector_ms[LAP_MAX_SECTORS]; lap_stats_t stats; } lap_result_t;

void lap_init(lap_t *L, const lap_cfg_t *cfg);
void lap_set_venue(lap_t *L, const trk_venue_t *v);          /* enters VENUE_FOUND */
void lap_force_layout(lap_t *L, uint16_t layout_id);
void lap_reset(lap_t *L);
/* feed a fix (valid or not) and current fused stats; may emit events via cb */
void lap_on_fix(lap_t *L, const gps_fix_t *fix, const fused_sample_t *fs, lap_evt_cb_t cb, void *ctx);
uint8_t lap_state(const lap_t *L);
const lap_result_t *lap_best(const lap_t *L);
const lap_result_t *lap_prev(const lap_t *L);
uint32_t lap_current_elapsed_ms(const lap_t *L, int64_t now_gps_us);
uint32_t lap_theoretical_best_ms(const lap_t *L);
int  lap_mark_gate(lap_t *L, uint8_t gate_idx, const gps_fix_t *fix, trk_layout_t *out_layout);  /* on-device creation */
```

#### `core/drag.h`

```c
typedef struct { uint8_t id; uint8_t kind; /* DRAG_SPEED_FROM0, DRAG_SPEED_RANGE, DRAG_DIST, DRAG_BRAKE */ uint16_t a, b; /* km/h or cm */ } drag_gate_def_t;
typedef struct { uint8_t gate_id; uint32_t time_ms; uint16_t speed_cms; uint32_t dist_cm; uint8_t hit; } drag_gate_res_t;
typedef struct { uint16_t run_no; int64_t t0_gps_us; uint8_t flags; uint8_t n_gates; drag_gate_res_t gates[DRAG_MAX_GATES]; uint16_t trap_cms; } drag_result_t;

void drag_init(drag_t *D, const drag_cfg_t *cfg);
void drag_reset(drag_t *D);
void drag_on_fused(drag_t *D, const fused_sample_t *fs, drag_evt_cb_t cb, void *ctx);   /* 100 Hz */
void drag_on_fix(drag_t *D, const gps_fix_t *fix);                                       /* re-anchors speed */
uint8_t drag_state(const drag_t *D);
const drag_result_t *drag_current(const drag_t *D);
const drag_result_t *drag_best(const drag_t *D, uint8_t gate_id);
```

#### `core/ses.h` — session records and framing

```c
int  ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t out_cap);  /* returns bytes written */
int  ses_frame_decode(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx);   /* streaming, resyncs */
int  ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *fix, uint8_t *out, size_t cap);          /* chooses KEY vs DELTA */
int  ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap);
int  ses_encode_lap(const lap_result_t *lap, uint8_t *out, size_t cap);
int  ses_encode_drag(const drag_result_t *run, uint8_t *out, size_t cap);
uint16_t ses_crc16(const uint8_t *buf, size_t n);
```

#### `core/exp.h` — exporters

```c
typedef struct { /* opaque; holds decoder state, pending output, current row */ } exp_t;
int  exp_open(exp_t *e, uint8_t format, const ses_session_meta_t *meta);
/* feed decoded frames in; pull text out. Producer calls exp_feed until it returns EXP_FULL, then exp_pull until empty. */
int  exp_feed(exp_t *e, uint8_t type, const void *payload, uint8_t len);
int  exp_pull(exp_t *e, uint8_t *out, size_t cap, size_t *n_out);
int  exp_finish(exp_t *e);
```

#### `core/cfg.h`

```c
int  cfg_defaults(cfg_t *c);
int  cfg_validate(cfg_t *c);                         /* clamps out-of-range fields, returns count of corrections */
int  cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap);
int  cfg_to_json(const cfg_t *c, char *out, size_t cap);
int  cfg_migrate(cfg_t *c, uint8_t from_version);
```

#### `core/trk.h`

```c
const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out);   /* bundled + user; NULL if none within radius */
int  trk_user_add(const trk_venue_t *v);
int  trk_user_load(const uint8_t *blob, size_t n);
int  trk_user_save(uint8_t *blob, size_t cap, size_t *n_out);
int  trk_from_json(trk_venue_t *out, const char *json, size_t n);
int  trk_to_json(const trk_venue_t *v, char *out, size_t cap);
```

---

## 6. Time base and timing math

### 6.1 Clocks

| Clock | Source | Resolution | Properties |
|-------|--------|-----------|------------|
| `mono_us` | `esp_timer_get_time()` | 1 µs | Monotonic; continues through light sleep (IDF advances it on wake from the RTC clock); resets on deep sleep and reset. |
| `gps_us` | UBX-NAV-PVT UTC fields + `nano`, or NAV-TIMEUTC in fallback mode | 1 µs (receiver accuracy ~100 ns) | Absolute UTC microseconds since 1970-01-01T00:00:00Z. Only present when `valid.validTime && valid.validDate && valid.fullyResolved`. |

All lap, sector, and drag results are differences of `gps_us` values. The ESP32 crystal (±20 ppm typical on dev boards) contributes only to IMU-to-GPS alignment, never to lap times.

`gps_us` from NAV-PVT: `days = days_from_civil(year, month, day)` (Howard Hinnant's algorithm, no floating point); `gps_us = ((days*86400 + hour*3600 + min*60 + sec) * 1000000) + nano/1000`. `nano` may be negative (−1e9 < nano < 1e9) and the subtraction is applied as-is; the result is exact.

### 6.2 `mono_us` → `gps_us` mapping (`tb_t`)

Purpose: give every IMU sample a `gps_us` for logging alignment and give the drag engine 100 Hz timestamps in the GPS domain.

Without PPS:
- On every valid fix: `o = arrival_mono_us − fix_gps_us`. The true offset is `o_true = o − latency`, where latency (receiver compute + UART transmit, ~30–120 ms on NEO-6M at 38400 baud) is always positive and jittery.
- Keep `window_min = min(o)` over a sliding 30 s window (implemented as two alternating half-window minima). `offset_us = window_min`. Because latency is one-sided, the minimum converges to `o_true + latency_min` where `latency_min` is the fixed transmit time (~25 ms for 100 bytes at 38400 baud). The driver subtracts the computed serial transmit time of the message length at the configured baud, leaving ≤ 10 ms residual.
- `locked` becomes true after 10 fixes. `quality` = 0 (unlocked), 1 (min-filter), 2 (PPS).
- Crystal drift over 30 s at 20 ppm is 0.6 ms, ignored.

With PPS (M10):
- ISR captures `mono_us` on the rising edge. The next NAV-PVT after the edge carries the `iTOW` of that second; `gps_us_top = floor(fix_gps_us / 1e6) * 1e6` of that fix. `offset_us = edge_mono_us − gps_us_top`. Quality 2. A new edge is checked against the current PPS offset (or, when no PPS lock exists yet and the filter is locked, against the min-filter); disagreement > 50 ms rejects the edge, drops the lock, and logs `E_GPS_BAD_FIX`. A PPS lock also expires when no edge has arrived for 5 s (`TB_PPS_STALE_US`), so the mapping falls back to the min-filter rather than tracking a stale offset; the min-filter never demotes an active PPS lock.

`tb_mono_to_gps(t, m) = m − offset_us`.

### 6.3 Geodesy

- Venue origin `(lat0, lon0)`. ENU: `x = (lon − lon0) · cos(lat0) · R`, `y = (lat − lat0) · R`, `R = 6371008.8 m`. Error at 5 km from origin is < 5 cm, irrelevant.
- Haversine only for venue proximity (`trk_find_nearest`).

### 6.4 Gate crossing

Inputs: previous fix `A` (ENU `a`, speed `v0`, time `tA`), current fix `B` (`b`, `v1`, `tB`), gate `P–Q`, `dir_sign` ∈ {+1, −1}.

1. Both fixes MUST be valid (§6.5). If either is invalid, no crossing is evaluated and `lap.flags |= LAP_GPS_LOST` if the engine is running.
2. Segment intersection: `r = b − a`, `s = q − p`, `den = r × s`. If `|den| < 1e-9` (parallel), no crossing. `t = ((p − a) × s) / den`, `u = ((p − a) × r) / den`. Crossing iff `0 ≤ t ≤ 1` and `0 ≤ u ≤ 1`.
3. Direction: `side = sign(s × r)`. Crossing accepted iff `side == dir_sign`. (Reverse layouts are the same line with `dir_sign` negated.)
4. Crossing time: `d = t · |r|`, `dt = tB − tA`, `acc = (v1 − v0)/dt`. If `|acc| < 0.01` m/s²: `τ = d / v0`. Else `τ = (−v0 + sqrt(v0² + 2·acc·d)) / acc`, clamped to `[0, dt]`. `t_cross = tA + τ`.
5. Debounce: a gate that fired is disabled until `geo_dist_point_segment(current, P, Q) > GATE_REARM_DIST_M` **and** `now − t_cross > GATE_REARM_MIN_S`; the S/F gate additionally requires `now − t_cross > MIN_LAP_S`.
6. Gate half-width: bundled and user gates are stored as two endpoints; on-device creation builds a 30 m line (`GATE_HALF_WIDTH_M` = 15 each side of the crossing point, perpendicular to heading).

Two fixes may straddle more than one gate (e.g. an S/F line and the first sector line within 11 m at 200 km/h on a 5 Hz module). Crossings are evaluated for all armed gates and processed in order of `t_cross`.

### 6.5 Fix validity rule (`pipeline`)

A fix is valid iff all hold:
- `fix_type == 3`
- `flags & gnssFixOK`
- `hacc_mm ≤ FIX_HACC_MAX_M · 1000`
- `sats ≥ FIX_MIN_SATS`
- time and date valid, `gps_us > 0`
- `gspeed_mms ≤ FIX_MAX_SPEED_MPS · 1000`
- `gps_us > last_valid_gps_us` (monotonic)
- distance from last valid fix `≤ FIX_MAX_JUMP_MPS · (gps_us − last_valid_gps_us)/1e6 + 20 m`

Invalid fixes are still logged (flagged) but not fed to the engines. Three consecutive invalid fixes emit `EV_FIX_LOST`; the next valid fix emits `EV_FIX_OK`.

### 6.6 Drag distance, speed, and gate interpolation

The drag engine runs at 100 Hz on fused samples, re-anchored by GPS fixes:

- `v_est(t) = v_gps_last + ∫_{t_fix_last}^{t} a_lon(τ) dτ`, integrated per fused sample (10 ms, trapezoid). At each valid fix `v_est` is reset to the Doppler `gSpeed` (no blending; Doppler is authoritative, the IMU only bridges the 100–200 ms between fixes).
- `dist(t) = ∫ v_est dt` from `t0`, trapezoid at 100 Hz.
- Speed gate `V`: the first sample with `v_est ≥ V`; crossing time linearly interpolated between the previous and this sample: `t = t_prev + (V − v_prev)/(v − v_prev) · 10 ms`.
- Distance gate `D`: same with `dist`.
- Trap speed at `D`: mean of `v_est` over samples where `dist ∈ [D − TRAP_DIST_M, D]`.
- Braking gate (100–0): starts when `v_est` falls through 100 km/h after the run peak; ends when `v_est < 0.5 km/h`; result is `dist` accumulated in between.

Expected error: IMU bias over 200 ms at 0.01 g contributes 2 cm/s, negligible; timing resolution 10 ms with linear interpolation, ±5 ms.

### 6.7 Expected accuracy

| GPS | Rate | Lap/gate accuracy | Notes |
|-----|------|-------------------|-------|
| NEO-6M | 5 Hz | ±0.03–0.05 s absolute | Lap-to-lap consistency ~±0.02 s because position bias is correlated over minutes |
| M10 | 10 Hz | ±0.01–0.02 s | With PPS, IMU alignment ~0.1 ms |

---

## 7. GPS drivers

### 7.1 UBX framing (`gps_ubx_common`)

Frame: `0xB5 0x62 | class u8 | id u8 | len u16 LE | payload[len] | CK_A | CK_B`. Checksum is 8-bit Fletcher over class, id, len, payload:

```c
ck_a = 0; ck_b = 0;
for each byte b in (class, id, len_lo, len_hi, payload...) { ck_a += b; ck_b += ck_a; }   /* uint8_t arithmetic */
```

Parser: byte-wise state machine `SYNC1 → SYNC2 → CLASS → ID → LEN1 → LEN2 → PAYLOAD → CK_A → CK_B`, payload buffer 128 bytes (longest message used is NAV-PVT at 92). Any checksum failure increments `frames_bad` and returns to `SYNC1`. `len > 128` returns to `SYNC1`. Bytes `$` while in `SYNC1` start an NMEA sentence skip (consume to `\n`) and set `nmea_seen = 1`, which the driver treats as "module lost its configuration" (§7.3.5).

Send helper: `ubx_send(class, id, payload, len)` builds and writes the frame; `ubx_send_ack(class, id, payload, len, timeout_ms)` additionally waits for `ACK-ACK (0x05 0x01)` or `ACK-NAK (0x05 0x00)` whose payload matches `(class, id)`; returns `-ENACK` on NAK, `-ETIMEDOUT` on silence.

### 7.2 Messages used

| Class/ID | Name | Length | Use |
|----------|------|--------|-----|
| 0x01 0x07 | NAV-PVT | 84 (u-blox 6/7) or 92 (8+, M10) | Primary fix. Parser accepts both lengths. |
| 0x01 0x06 | NAV-SOL | 52 | Fallback set: fix type, numSV, pAcc |
| 0x01 0x02 | NAV-POSLLH | 28 | Fallback: lat/lon/height/hAcc |
| 0x01 0x12 | NAV-VELNED | 36 | Fallback: gSpeed/heading/sAcc |
| 0x01 0x21 | NAV-TIMEUTC | 20 | Fallback: UTC + nano + valid |
| 0x0A 0x04 | MON-VER | var | Self-test, firmware detection |
| 0x05 0x01 / 0x05 0x00 | ACK-ACK / ACK-NAK | 2 | Config confirmation |
| 0x06 0x00 | CFG-PRT | 20 | Baud, protocols |
| 0x06 0x01 | CFG-MSG | 3 | Per-message output rate |
| 0x06 0x08 | CFG-RATE | 6 | Measurement rate |
| 0x06 0x24 | CFG-NAV5 | 36 | Dynamic model |
| 0x06 0x16 | CFG-SBAS | 8 | SBAS off |
| 0x06 0x11 | CFG-RXM | 2 | Power mode (u-blox 6) |
| 0x06 0x3B | CFG-PM2 | 44 | Power-save parameters |
| 0x06 0x09 | CFG-CFG | 13 | Save to BBR |
| 0x02 0x41 | RXM-PMREQ | 8 | Backup mode request |
| 0x06 0x8A | CFG-VALSET | var | M10 configuration |

NAV-PVT field offsets (both lengths share the first 84 bytes): `iTOW u32 @0, year u16 @4, month u8 @6, day @7, hour @8, min @9, sec @10, valid u8 @11 (bit0 validDate, bit1 validTime, bit2 fullyResolved), tAcc u32 @12, nano i32 @16, fixType u8 @20, flags u8 @21 (bit0 gnssFixOK), flags2 @22, numSV @23, lon i32 @24 (1e-7°), lat i32 @28, height i32 @32 (mm), hMSL i32 @36 (mm), hAcc u32 @40 (mm), vAcc u32 @44, velN i32 @48 (mm/s), velE @52, velD @56, gSpeed i32 @60 (mm/s), headMot i32 @64 (1e-5°), sAcc u32 @68 (mm/s), headAcc u32 @72 (1e-5°), pDOP u16 @76 (0.01)`. Bytes 84–91 (u-blox 8+): `headVeh i32, magDec i16, magAcc u16` — ignored.

Fallback assembly (NEO-6M firmware < 7.03): NAV-SOL, NAV-POSLLH, NAV-VELNED, NAV-TIMEUTC are each enabled at rate 1. The driver keeps a slot keyed by `iTOW`; when all four have arrived with the same `iTOW` the fix is emitted. A new `iTOW` before completion discards the partial slot and counts `frames_partial`.

### 7.3 NEO-6M driver (`gps_neo6m`)

#### 7.3.1 Profile
`{ max_rate_hz = 5, baud = 38400, has_pps = 0, name = "neo6m" }`.

#### 7.3.2 UART
`UART_NUM_2`, RX GPIO 16, TX GPIO 17, 8N1, RX ring buffer 2048 B, event queue depth 16, `rx_timeout` 2 symbol times (so a burst delivers one `UART_DATA` event), `UART_SCLK_REF_TICK` clock source (baud stable under DFS), ISR in IRAM (`CONFIG_UART_ISR_IN_IRAM=y`).

#### 7.3.3 Configuration sequence (`gps_configure`)
Executed at boot, after every GPS power-cycle, and whenever `nmea_seen` becomes set. Each step uses `ubx_send_ack` with 500 ms timeout unless noted.

1. Open UART at 38400 and poll `MON-VER` (500 ms). If a reply arrives the module already runs the saved configuration; skip to step 2. Otherwise open at 9600 (module default) and send `CFG-PRT`: `portID=1, mode=0x000008D0 (8N1), baudRate=38400, inProtoMask=0x0001 (UBX), outProtoMask=0x0001 (UBX)`. No ACK is awaited (the module switches baud immediately). Wait 100 ms, reopen UART at 38400.
2. Poll `MON-VER`; store `swVersion` (e.g. `7.03 (45969)`); parse the leading float to decide `has_navpvt = (version ≥ 7.03)`.
3. `CFG-RATE`: `measRate=200, navRate=1, timeRef=0 (UTC)`.
4. `CFG-NAV5`: `mask=0x0005 (dyn | fixMode), dynModel=4 (automotive), fixMode=2 (3D only)`, all other fields zero. Rationale: 3D-only avoids 2D altitude-hold fixes with poor velocity.
5. `CFG-SBAS`: `mode=0` (disabled). No SBAS coverage over South Africa; saves search time.
6. `CFG-MSG` for each: if `has_navpvt`: `NAV-PVT rate 1`; else `NAV-SOL 1, NAV-POSLLH 1, NAV-VELNED 1, NAV-TIMEUTC 1`. All NMEA messages are already off because `outProtoMask` excludes NMEA.
7. `CFG-CFG` save: `clearMask=0, saveMask=0x0000061F (ioPort, msgConf, infMsg, navConf, rxmConf, antConf), loadMask=0, deviceMask=0x01 (BBR)`. Keeps config across a PARK if the backup cell holds; the driver still reapplies at every boot because the cell is small.

Total configure time ≤ 1.5 s. Any step failing three times returns `-EIO`; the ladder (§17.3) takes over.

#### 7.3.4 Power modes
- `GPS_PM_FULL`: `CFG-RXM lpMode=0` (continuous) and message rates as configured.
- `GPS_PM_CYCLIC_1HZ`: `CFG-RATE measRate=1000`; `CFG-PM2 version=1, flags=0x00029000 (cyclic tracking, doNotEnterOff, updateEPH), updatePeriod=1000, searchPeriod=10000, onTime=0, minAcqTime=0`; `CFG-RXM lpMode=1`; `CFG-MSG` all nav messages rate 0 (the module keeps tracking, sends nothing). ~11 mA.
- `GPS_PM_BACKUP`: `RXM-PMREQ duration=0, flags=0x00000002 (backup)`. Wake by sending 0xFF on the UART and waiting 100 ms **[VERIFY]** wake behaviour on u-blox 6 (if the module does not wake on RX, the MOSFET cut is mandatory and `GPS_PM_BACKUP` is unused).

#### 7.3.5 Reset detection
The NEO-6M has no flash; a power glitch returns it to 9600 baud NMEA. The parser sets `nmea_seen` on any `$` at frame start. The pipeline, seeing `nmea_seen`, calls `gps_configure()` again (which starts by reopening at 9600). Logged as `E_GPS_RESET_DETECTED`.

#### 7.3.6 Timing
Message arrival `mono_us` is taken from the `UART_DATA` event timestamp captured in the pipeline when the event is dequeued, minus the serial time of the bytes still in the ring (`bytes_in_ring · 10 / baud`). The driver records the serial transmit time of the message itself (`len · 10 / baud`) so `tb_on_fix` can subtract it.

### 7.4 M10 driver (`gps_m10`)

#### 7.4.1 Profile
`{ max_rate_hz = 10, baud = 115200, has_pps = 1, name = "m10" }`.

#### 7.4.2 Configuration (`CFG-VALSET`)
Frame payload: `version=0x00, layers=0x01 (RAM) [second call with 0x02 (BBR)], reserved u16, then (key u32, value)*`. Key IDs per the u-blox M10 interface description **[VERIFY]** against the module's protocol version via `MON-VER`:

| Key | ID | Value |
|-----|----|-------|
| `CFG-UART1-BAUDRATE` | `0x40520001` | 115200 (u32) |
| `CFG-UART1OUTPROT-UBX` | `0x10740001` | 1 |
| `CFG-UART1OUTPROT-NMEA` | `0x10740002` | 0 |
| `CFG-RATE-MEAS` | `0x30210001` | 100 (u16, ms) |
| `CFG-RATE-NAV` | `0x30210002` | 1 |
| `CFG-NAVSPG-DYNMODEL` | `0x20110021` | 4 (automotive) |
| `CFG-NAVSPG-FIXMODE` | `0x20110011` | 2 (3D only) |
| `CFG-MSGOUT-UBX_NAV_PVT_UART1` | `0x20910007` | 1 |
| `CFG-SIGNAL-SBAS_ENA` | `0x10310020` | 0 |
| `CFG-TP-TP1_ENA` | `0x10050007` | 1 |
| `CFG-TP-PULSE_DEF` | `0x20050023` | 0 (period) |
| `CFG-TP-PERIOD_TP1` / `_LOCK_TP1` | `0x40050002` / `0x40050003` | 1000000 µs |
| `CFG-TP-LEN_TP1` / `_LOCK_TP1` | `0x40050004` / `0x40050005` | 100000 µs |
| `CFG-TP-USE_LOCKED_TP1` | `0x10050009` | 1 |
| `CFG-TP-ALIGN_TO_TOW_TP1` | `0x1005000a` | 1 |
| `CFG-TP-POL_TP1` | `0x1005000b` | 1 (rising edge at top of second) |
| `CFG-PM-OPERATEMODE` | `0x20d00001` | 0 FULL / 2 PSMCT for PIT |
| `CFG-PM-POSUPDATEPERIOD` | `0x40d00002` | 1 (s) in PIT |

The default M10 baud is 9600 with NMEA on; the driver opens at 9600, sets the baud key first, reopens at 115200, then sends the rest. VALSET is acknowledged with ACK-ACK.

#### 7.4.3 Magnetometer
QMC5883L at I2C 0x0D, continuous mode 50 Hz, ±2 G, OSR 512. Read by the pipeline every 50 ms alongside the IMU FIFO. Used only for standstill heading and a slow yaw-drift check in fusion; not required for lean or timing. Hard-iron calibration by min/max tracking over the first 60 s of motion, stored in NVS.

#### 7.4.4 PPS
GPIO 36 rising-edge interrupt in IRAM, captures `esp_timer_get_time()` into a volatile and sets a flag; the pipeline drains it on its next wake and calls `tb_on_pps`.

### 7.5 GPS fault ladder (executed by supervisor via commands to pipeline)

| Condition | Action | Log |
|-----------|--------|-----|
| No valid frame for 3 s | `gps_configure()` | `E_GPS_SILENT`, `E_GPS_RECONFIG` |
| 10 s | `gps_reinit_uart` at 9600, 38400, 115200 in turn, 500 ms listen each, then `gps_configure()` | `E_GPS_UART_REINIT` |
| 30 s | `board_gps_power(false)`, 500 ms, `board_gps_power(true)`, 1 s, `gps_configure()` | `E_GPS_POWER_CYCLE` |
| 60 s | set `SYS_GPS_DEAD`; retry the power-cycle step every 60 s | `E_GPS_DEAD` |
| Any valid frame | clear ladder and `SYS_GPS_DEAD` | — |

---

## 8. IMU driver (`imu_mpu6050`)

### 8.1 Bus
I2C port 0, SDA 21, SCL 22, 400 kHz, 7-bit address 0x68, clock stretching timeout 1 ms, transaction timeout 20 ms. Every transaction result is checked; `ESP_FAIL`/`ESP_ERR_TIMEOUT` increments `nack_count` and feeds the ladder (§17.3).

### 8.2 Register map used

| Reg | Name | Value / use |
|-----|------|-------------|
| 0x0D–0x10 | SELF_TEST_X/Y/Z/A | Factory trim for self-test |
| 0x19 | SMPLRT_DIV | 9 → 1 kHz / (1+9) = 100 Hz |
| 0x1A | CONFIG | DLPF_CFG = 4 (accel 21 Hz, gyro 20 Hz; 1 kHz internal) |
| 0x1B | GYRO_CONFIG | FS_SEL = 3 (±2000 dps, 16.4 LSB/dps); ST bits for self-test |
| 0x1C | ACCEL_CONFIG | AFS_SEL = 3 (±16 g, 2048 LSB/g); ACCEL_HPF bits [2:0] |
| 0x1F | MOT_THR | Motion threshold, 1 LSB ≈ 2 mg **[VERIFY]** empirically |
| 0x20 | MOT_DUR | Motion duration, 1 LSB = 1 ms |
| 0x23 | FIFO_EN | 0x78 (XG, YG, ZG, ACCEL); TEMP off |
| 0x37 | INT_PIN_CFG | 0x30 (LATCH_INT_EN, INT_RD_CLEAR); INT_LEVEL = 0 (active-high), INT_OPEN = 0 (push-pull) |
| 0x38 | INT_ENABLE | FULL: 0x10 (FIFO_OFLOW_EN); LOWPOWER: 0x40 (MOT_EN) |
| 0x3A | INT_STATUS | Read to clear |
| 0x3B–0x40 | ACCEL_XOUT_H..ZOUT_L | Direct read in self-test |
| 0x41–0x42 | TEMP_OUT | `T = raw/340 + 36.53` |
| 0x43–0x48 | GYRO_XOUT..ZOUT | Direct read in self-test |
| 0x69 | MOT_DETECT_CTRL | ACCEL_ON_DELAY = 1 |
| 0x6A | USER_CTRL | FIFO_EN (0x40); FIFO_RESET (0x04) |
| 0x6B | PWR_MGMT_1 | 0x80 reset; then 0x01 (CLKSEL = PLL X gyro); LOWPOWER: 0x28 (CYCLE | TEMP_DIS) |
| 0x6C | PWR_MGMT_2 | FULL: 0x00; LOWPOWER: 0xC7 (LP_WAKE_CTRL = 3 → 40 Hz, STBY all gyro) |
| 0x72–0x73 | FIFO_COUNTH/L | Bytes in FIFO |
| 0x74 | FIFO_R_W | Burst read |
| 0x75 | WHO_AM_I | 0x68 |

### 8.3 Init sequence (`imu_init`)
1. `WHO_AM_I` == 0x68 else `-ENODEV` (`E_IMU_WHOAMI`).
2. `PWR_MGMT_1 = 0x80`; wait 100 ms. `PWR_MGMT_1 = 0x01`; wait 10 ms.
3. `SMPLRT_DIV = 9`, `CONFIG = 0x04`, `GYRO_CONFIG = 0x18`, `ACCEL_CONFIG = 0x18`.
4. `USER_CTRL = 0x04` (FIFO reset), then `USER_CTRL = 0x40`, `FIFO_EN = 0x78`.
5. `INT_PIN_CFG = 0x30`, `INT_ENABLE = 0x10`.
6. Read and discard FIFO. `mode = IMU_FULL`.

### 8.4 FIFO read (`imu_read_fifo`)
1. Read `FIFO_COUNT` (2 bytes). If `count > 1000` → overflow: `USER_CTRL |= 0x04`, log `E_IMU_FIFO_OVF`, return 0 samples.
2. `n = count / 12` (drop any partial sample remainder by reading and discarding `count % 12` bytes at the end).
3. Burst-read `n·12` bytes from `FIFO_R_W` in one transaction (≤ 84 samples per 50 ms is never exceeded; typical 5).
4. Samples are big-endian int16 pairs in order `AX AY AZ GX GY GZ`.
5. Timestamps: `mono_us[i] = read_mono_us − (n − 1 − i) · 10000`.
6. Frozen detection: if all six raw values equal the previous sample's for 100 consecutive samples, set `frozen` and return `-EIO` (`E_IMU_FROZEN`).

### 8.5 Self-test (`imu_self_test`), per MPU-6050 Register Map rev 4.2 §4
1. Save config. Set `GYRO_CONFIG = 0xE0 | (FS_SEL=0)`, `ACCEL_CONFIG = 0xF0 | (AFS_SEL=2)`; wait 250 ms; read 100 samples of accel and gyro, average → `ST_on`.
2. Clear ST bits (same ranges); wait 250 ms; average 100 samples → `ST_off`.
3. `STR = ST_on − ST_off` per axis.
4. Read `SELF_TEST_X/Y/Z/A`; extract 5-bit `XG_TEST` (bits [4:0] of 0x0D) and `XA_TEST` (bits [7:5] of 0x0D as high 3 bits, bits [5:4] of 0x10 as low 2 bits), likewise Y, Z.
5. Factory trim: gyro `FT = 25 · 131 · 1.046^(G_TEST − 1)` (Y axis negated); accel `FT = 4096 · 0.34 · (0.92/0.34)^((A_TEST − 1)/30)`. `FT = 0` if the test code is 0.
6. `change = (STR − FT) / FT`. Pass iff `|change| ≤ 0.14` per axis. `pass_mask` bit set per passing axis; a clone typically fails all gyro axes.
7. Restore config.

Self-test failure sets `SYS_IMU_SUSPECT` (fusion still runs, results flagged `FUS_SUSPECT` in logs).

### 8.6 Low-power motion mode (`imu_set_mode(IMU_LOWPOWER)`)
1. `ACCEL_CONFIG = 0x18 | 0x01` (HPF 5 Hz), `MOT_THR = IMU_MOT_THR_LSB` (default 20 ≈ 40 mg), `MOT_DUR = IMU_MOT_DUR_MS` (default 40).
2. `MOT_DETECT_CTRL = 0x15`, `INT_ENABLE = 0x40`.
3. `PWR_MGMT_2 = 0xC7`, `PWR_MGMT_1 = 0x28`. FIFO disabled (`USER_CTRL = 0x00`).
Return to `IMU_FULL` re-runs steps 2–6 of §8.3 (no reset). Gyro needs ~35 ms to restart; the first 5 samples after mode change are discarded.

### 8.7 Fault ladder

| Condition | Action | Log |
|-----------|--------|-----|
| I2C error | retry same transaction up to 3× with 1 ms gap | `E_IMU_NACK` (once per burst) |
| 3 failures, or `frozen` | `imu_recover`: `i2c_driver_delete`; bit-bang SCL 9 pulses at 100 kHz with SDA released; generate STOP; reinstall driver; `imu_init` | `E_IMU_BUS_RECOVERY` |
| recovery fails 3× in 60 s | set `SYS_IMU_DEAD`; fusion outputs flagged invalid; retry recovery every 60 s | `E_IMU_DEAD` |
| success | clear `SYS_IMU_DEAD` | — |

---

## 9. Pipeline and sensor fusion

### 9.1 Pipeline loop

```
init: gps_init, gps_configure, imu_init, calib load, engines init
loop:
  xQueueSelectFromSet(set, 100 ms)
  case UART event:   drain bytes → gps_feed_bytes; while gps_poll(fix): on_fix(fix)
  case IMU timer:    imu_read_fifo → for each raw: on_raw(raw); [m10: qmc read]
  case cmd_q:        handle command
  case timeout:      nothing (heartbeat only)
  hb[PIPELINE]++
  supervisor requests (via flags): ladder steps executed here, never inside supervisor
```

The pipeline never blocks on the display, storage, or radio. Its worst-case loop iteration (5 IMU samples through fusion + one fix through both engines) is budgeted at 2 ms at 80 MHz; measured in §22.4.

`on_fix(fix)`:
1. Validity (§6.5) → `fix.valid`.
2. `tb_on_fix` if valid.
3. `fus_set_gps_speed(v, mono, valid)`.
4. Active engine: `lap_on_fix` or `drag_on_fix`.
5. Motion state: `moving = valid && gspeed > MOVING_SPEED_KMH`; edge → `EV_MOTION` / `EV_STILL`.
6. Push to `fix_ring`.

`on_raw(raw)`:
1. `fus_step(raw) → fused`.
2. `fused.gps_us = tb_mono_to_gps(mono)`.
3. If drag mode: `drag_on_fused`.
4. Every `FUSION_HZ / FUSED_RING_HZ` samples (4): push to `fused_ring`.
5. Lap stats accumulate max/min per lap (done inside `lap_on_fix` using the latest fused sample; the pipeline keeps a running per-lap maxima struct updated at 100 Hz and passes it to the lap engine).

### 9.2 Calibration

`fus_calib_t` in NVS namespace `lt_cal`, key `fus`, version 1.

**Gyro bias.** Stillness: over a 2 s window, accel magnitude variance < `STILL_ACC_VAR` and every gyro axis variance < `STILL_GYRO_VAR`. When still, `gbias = mean(gyro_raw)` (LSB), stored with `gbias_temp_c100`. Applied as `ω = (raw − gbias) / 16.4` dps. When `|temp − gbias_temp| > 15 °C` the sample flag `FUS_BIAS_STALE` is set until the next still period.

**Orientation.**
- Capture (menu action, vehicle upright and still, rider seated): 2 s mean of accel → `z = normalize(mean)`. Row 2 of `r` = `z`. `orient_ok = 1`. Forward row invalidated.
- Forward learning (automatic): condition `|yaw_dps| < 2` and GPS longitudinal acceleration (`Δv/Δt` from consecutive fixes) > 1.5 m/s² for ≥ 1 s. During such windows accumulate `a_h = a − (a·z)z` (horizontal component of accel, bias-removed). After ≥ 3 windows, `x = normalize(Σ a_h)`. `y = z × x`, re-orthogonalise `x = y × z`. `forward_ok = 1`. Saved to NVS. `EV_CALIB_DONE` emitted.
- Rotation: `v = R · b` where `R` rows are `x, y, z`. Vehicle frame: X forward, Y left, Z up.
- Until `forward_ok`, lean and lateral g are flagged invalid (`FUS_LEAN_VALID = 0`); longitudinal g uses `a·z`-removed magnitude sign-less (flagged).

### 9.3 Fusion math (100 Hz)

Inputs per step: `a_b` (m/s², bias-free accel, body), `ω_b` (rad/s, bias-removed gyro, body), `v` (GPS speed m/s, age), `dt = 0.01`.

1. `a = R·a_b`, `ω = R·ω_b`.
2. **Longitudinal g**: `g_lon = a.x / 9.80665`. (Specific force along forward axis; pitch neglected.)
3. **Earth-frame yaw rate**: `ψ̇ = ω.z·cos φ + ω.y·sin φ` (φ = current lean estimate; for the car variant φ ≡ 0). Sign: positive = left turn (right-hand about Z-up).
4. **Lean (moto)**: sign convention positive = right lean = positive rotation about +X (forward) by the right-hand rule.
   - `φ_gyro = φ + ω.x · dt`
   - `φ_ref = atan2(−v · ψ̇, 9.80665)` (right turn has ψ̇ < 0 ⇒ φ_ref > 0). Only when `v > 3 m/s` and GPS age < 1 s; otherwise `φ_ref` is not applied and α is 1 for that step (pure gyro), with `FUS_LEAN_VALID` cleared after 5 s without a reference.
   - `φ = α·φ_gyro + (1−α)·φ_ref`, `α = LEAN_ALPHA = 0.98` (τ ≈ 0.5 s).
   - Clamp `|φ| ≤ LEAN_MAX_DEG`; clamping sets `FUS_CLAMPED`.
   - Cross-check: `ψ̇_gps` = derivative of `headMot` across fixes; if `|ψ̇ − ψ̇_gps| > 10 °/s` for 5 s set `FUS_DISAGREE`, log `E_FUSION_DISAGREE` once per session.
5. **Lateral g**: moto `g_lat = −v·ψ̇ / 9.80665` (positive = right). Car `g_lat = −a.y / 9.80665`.
6. **Combined**: `g_comb = sqrt(g_lat² + g_lon²)`.
7. **Yaw rate out**: `yaw_dps = ψ̇ · 180/π`.
8. **Stillness** updated for bias calibration and drag arming.

All float32. No trig tables needed; `sinf/cosf/atan2f` at 100 Hz cost < 1 % CPU.

### 9.4 Per-lap statistics

Maintained by the pipeline at 100 Hz, reset at each S/F crossing, passed to the lap engine at lap completion:

`lap_stats_t { uint16_t max_speed_cms, min_speed_cms; int16_t max_lean_l_cdeg, max_lean_r_cdeg; int16_t max_glat_e3, max_gacc_e3, max_gbrake_e3; }`

`min_speed` ignores samples during `LAP_GPS_LOST`. Lean maxima ignore samples with `FUS_LEAN_VALID = 0`.

---

## 10. Track model and lap engine

### 10.1 Track data model

```c
#define TRK_MAX_LAYOUTS 8
#define LAP_MAX_SECTORS 8

typedef struct { double lat, lon; } trk_pt_t;                 /* degrees */
typedef struct { trk_pt_t p1, p2; } trk_line_t;
typedef struct {
    uint16_t   id;                 /* unique within venue */
    char       name[24];           /* "Full", "Full Reverse", "Short" */
    trk_line_t sf;                 /* start/finish */
    int8_t     dir_sign;           /* +1 / −1, see §6.4 */
    uint8_t    n_sectors;          /* number of sector gates excluding S/F; sectors = n_sectors + 1 */
    trk_line_t sectors[LAP_MAX_SECTORS];   /* ordered in driving direction */
    uint32_t   length_m;           /* approximate lap length */
} trk_layout_t;
typedef struct {
    uint16_t   id;                 /* global unique; bundled 1..999, user 1000+ */
    char       name[32];
    double     lat, lon;           /* centre */
    uint32_t   radius_m;           /* detection radius, default 2000 */
    uint8_t    n_layouts;
    trk_layout_t layouts[TRK_MAX_LAYOUTS];
} trk_venue_t;
```

Bundled venues are generated from `tools/tracks/*.json` (schema §10.2) by `gen_tracks.py` into `trk_bundled.c` as a `const` array in flash. Initial set (start/finish coordinates approximate until verified on site **[VERIFY]**): Kyalami (Full), Zwartkops (Full, Short), Red Star Raceway (Full, Short), Phakisa (Full), Aldo Scribante (Full), Killarney (Full, Full Reverse, Short, Short Reverse), Dezzi Raceway (Full), East London Grand Prix Circuit (Full), Midvaal (Full). Kart venues are added as data arrives.

User venues: `/tracks/user.bin` = `u8 version | u8 count | trk_venue_t[count]` packed. Max 16 user venues. Loaded at boot; lookup merges bundled and user, user wins on id clash.

### 10.2 Track JSON schema (tools and upload)

```json
{
  "id": 6,
  "name": "Killarney",
  "lat": -33.8567, "lon": 18.5169, "radius_m": 2000,
  "layouts": [
    { "id": 1, "name": "Full", "dir": 1, "length_m": 3267,
      "sf": [[-33.8580, 18.5150], [-33.8582, 18.5153]],
      "sectors": [ [[lat,lon],[lat,lon]], [[lat,lon],[lat,lon]] ] },
    { "id": 2, "name": "Full Reverse", "dir": -1, "length_m": 3267, "sf": "same", "sectors": "reverse" }
  ]
}
```

Line endpoint convention: `p1` is the **left** end and `p2` the **right** end of the line as seen in the driving direction of the layout; with that convention `dir` is `+1`. A reverse layout keeps the same endpoints and sets `dir` to `-1`. Formally `dir = sign(cross(p2 − p1, motion))` in ENU, the same expression the engine evaluates at every crossing and that on-device creation uses to set `dir_sign`.

`"sf": "same"` copies the S/F line of the first layout; `"sectors": "reverse"` reverses the sector order of the first layout. The generator expands these before emitting C.

### 10.3 Lap engine state machine

| State | Entry | On valid fix | Exit |
|-------|-------|--------------|------|
| `NO_VENUE` | init, reset | every 5 s: `trk_find_nearest`; found within `radius_m` → `lap_set_venue`, `EV_VENUE_FOUND` | → `VENUE_FOUND` |
| `VENUE_FOUND` | venue set | load candidate layouts = all layouts of venue (or the forced one); ENU origin set to venue centre; arm S/F gates of all candidates; `EV_ARMED` | → `ARMED` immediately |
| `ARMED` | | evaluate S/F crossings for every candidate layout (direction filters reverse vs forward); first accepted crossing: `lap_start = t_cross`, `lap_no = 0` (out-lap), candidates narrowed to those whose S/F matched (dir_sign) | → `LAP_RUNNING` |
| `LAP_RUNNING` | | evaluate sector gates of remaining candidates and S/F; on sector hit record split; on S/F hit → complete lap (§10.4); if `lap_no == 0` (out-lap) run layout disambiguation (§10.5) | stays; → `NO_VENUE` if distance from venue centre > `radius_m · 1.5` for 60 s |

`lap_reset` returns to `NO_VENUE` but keeps best/prev results of the session unless `CMD_RESET_ENGINE` with `arg8 = 1` (clear session).

### 10.4 Lap completion

On accepted S/F crossing at `t_cross`:
1. `time_ms = (t_cross − lap_start) / 1000` (rounded to nearest ms).
2. If `time_ms < MIN_LAP_S·1000` → ignore crossing (debounce guard, should not happen after §6.4 step 5).
3. Flags: `LAP_OUT_LAP` if `lap_no == 0` (no time reported, but the crossing starts lap 1). `LAP_GPS_LOST` if any fix invalid during the lap. `LAP_PIT` if pit condition triggered (§10.6). `LAP_INCOMPLETE` if not all sectors were hit in order. `LAP_INTERRUPTED` if the lap was resumed from RTC memory after a reset. `LAP_TOO_LONG` if `time_ms > MAX_LAP_S·1000`.
4. `valid = !(LAP_GPS_LOST | LAP_PIT | LAP_INCOMPLETE | LAP_TOO_LONG | LAP_OUT_LAP)`.
5. Final sector split = `t_cross − last_gate_time`. Sector count = `n_sectors + 1`.
6. If `valid` and (`best == NULL` or `time_ms < best.time_ms`) → new best; best sectors updated individually from any valid lap.
7. `prev = this`. `EV_LAP_COMPLETE`. `lap_start = t_cross`, `lap_no++`, stats reset, sector index reset to 0.
8. The result is passed to the logger via the event; the logger writes `LAP` frame to `.log` and rewrites `.sum`.

### 10.5 Layout disambiguation (during out-lap and lap 1)

Candidates after the first S/F crossing share an S/F line and direction (e.g. Full and Short). The engine arms the union of their sector gates, tagged by layout. On each sector hit it records which layouts contain that gate. At the next S/F crossing:
- Score each candidate: `hits_matching − hits_foreign` plus `1` if `|lap_distance − length_m| < 0.15 · length_m` (lap distance integrated from Doppler speed).
- The candidate with the highest score locks; ties resolved by the venue's default layout (config `lap.default_layout[venue_id]`), then the lowest layout id.
- `EV_LAYOUT_LOCKED`. Foreign gates disarmed. The out-lap and lap 1 sector splits are re-derived against the locked layout (splits recorded per gate, so the mapping is exact).

`lap_force_layout` at any time locks immediately and re-arms.

### 10.6 Pit detection
Inside `LAP_RUNNING`, if `gspeed < PIT_SPEED_KMH` for `PIT_TIME_S` continuous seconds → `LAP_PIT` flag on the current lap. The lap still completes at the next S/F crossing (a rider re-entering the track through the pit exit crosses S/F normally).

### 10.7 Sector delta

At each sector hit in lap N: `delta_ms = split_ms − best_lap.sector_ms[idx]` if a best lap exists, else no delta. At lap completion: `delta_ms = time_ms − best_prev.time_ms` (the best before this lap). Delivered in `EV_SECTOR.arg32b` and `EV_LAP_COMPLETE.arg32b` as int32 ms.

### 10.8 Theoretical best
`Σ best_sector_ms[i]` over all sector indices, only when every index has a value.

### 10.9 On-device track creation (`lap_mark_gate`)

Entered from menu "New track" → engine in `CREATE` sub-mode (state `NO_VENUE` retained):
1. First MODE press while moving with a valid fix: S/F line = perpendicular to `headMot` through the fix position, endpoints `±GATE_HALF_WIDTH_M`. `dir_sign` computed so the current motion is accepted. Venue centre = this position, `radius_m = 2000`, id = next free user id, name `Track_YYYYMMDD` (from `gps_us`). Layout id 1 name `Layout 1`.
2. Each further MODE press appends a sector gate the same way (max `LAP_MAX_SECTORS`).
3. The next S/F crossing ends creation, computes `length_m` from integrated distance, saves via `trk_user_add` and the logger writes `/tracks/user.bin`, and the engine enters `VENUE_FOUND` on the new venue.
4. A reverse layout (id 2, name `Layout 1 Reverse`, `dir_sign` negated, sectors reversed) is generated automatically.
5. Long MODE cancels creation.

### 10.10 RTC continuity

The pipeline mirrors `{ venue_id, layout_id (0 if not locked), lap_no, lap_start_gps_us, sector_idx, gate_times[], best, prev, mode }` into `rtc_state_t` (CRC32, version) after every S/F or sector event. On boot with valid RTC state younger than `RTC_RESUME_MAX_S` and matching venue (first fix within `radius_m`), the engine restores directly into `LAP_RUNNING` with the flag `LAP_INTERRUPTED` set on the current lap.

### 10.11 Predictive delta (O5)

When a best lap exists, the engine records the best lap as a table `(dist_m u16, t_ms u32)` at every fix (max 600 entries; beyond that every 2nd fix). During the current lap, `dist` (integrated Doppler distance since S/F) is looked up by binary search + linear interpolation to give `t_ref(dist)`; `delta_live = elapsed − t_ref`. Exposed via `lap_live_delta_ms()`. The e-paper UI ignores it; OLED UIs render it at 10 Hz. Table memory 2.4 KB.

---

## 11. Drag engine

### 11.1 Gate definitions

Default gate list (`drag_cfg_t.gates`, up to `DRAG_MAX_GATES = 16`), km/h units (mph list in §15.2):

| id | kind | a | b | Display name |
|----|------|---|---|--------------|
| 1 | SPEED_FROM0 | 60 | — | 0-60 |
| 2 | SPEED_FROM0 | 100 | — | 0-100 |
| 3 | SPEED_FROM0 | 200 | — | 0-200 |
| 4 | SPEED_FROM0 | 300 | — | 0-300 |
| 5 | SPEED_RANGE | 100 | 200 | 100-200 |
| 6 | DIST | 1829 (cm) | — | 60ft |
| 7 | DIST | 10058 | — | 330ft |
| 8 | DIST | 20117 | — | 1/8 |
| 9 | DIST | 30480 | — | 1000ft |
| 10 | DIST | 40234 | — | 1/4 (trap) |
| 11 | BRAKE | 100 | 0 | 100-0 |

`benches` for the riding screen = the `SPEED_FROM0` gates whose `a` is in `cfg.drag.benches_kmh` (default {100, 200, 300}); 0-60 is logged but not a headline bench by default.

### 11.2 State machine

| State | Condition to enter | Behaviour |
|-------|--------------------|-----------|
| `IDLE` | init, after `DONE` + 5 s, or moving without arming | nothing |
| `ARMED` | `v_est < DRAG_ARM_SPEED_KMH` and `fus_is_still` for `DRAG_ARM_STILL_S` | `EV_DRAG_ARMED`; 1 s history ring of fused samples maintained; `dist = 0`, `v_est = 0` |
| `LAUNCHED` | `g_lon > DRAG_LAUNCH_G` for `DRAG_LAUNCH_HOLD_MS` continuous | `t0` = time of the first sample in the contiguous run with `g_lon > 0.05` ending at detection (scan back through history); if rollout enabled, `t0` := time when `dist` reaches `DRAG_ROLLOUT_M` and `dist` is re-zeroed there. `EV_DRAG_LAUNCH`. Gates evaluated every sample (§6.6). |
| `DONE` | 1/4-mile gate hit; or `v_est < 0.5·v_peak` after a peak with no gate for `DRAG_TIMEOUT_S`; or `v_est < DRAG_ARM_SPEED_KMH` | braking gate may still complete after DONE if armed; `EV_DRAG_DONE`; result frozen; logger writes `DRAG_RUN` |

Abort: if `v_est` falls below 1 km/h within 2 s of launch (false start, e.g. clutch dump stall) the run is discarded and state returns to `ARMED`.

### 11.3 Result

`drag_result_t` (§5.2). `hit` per gate. `trap_cms` per §6.6. Best per gate across the session = lowest `time_ms` among hit gates (braking = shortest distance).

### 11.4 Screen benches rule

Rows shown on the riding screen: `SPEED_FROM0` gates in `benches` that were hit, ascending by `a`, followed by the 1/4 row. Maximum 4 rows; if more than 3 benches were hit, the lowest are dropped first. Rows appear as gates are hit (each hit triggers a partial refresh). A run ending without the 1/4 shows `1/4 --`.

---

## 12. Session data model and log format

### 12.1 Identifiers and files

- Session id: `S%05u_%03u` = boot counter (u16, NVS `lt_sys/boot_cnt`) and per-boot sequence (u8). Example `S00042_001`.
- Files: `/sessions/S00042_001.log` (append-only stream) and `/sessions/S00042_001.sum` (summary, rewritten atomically).
- A session starts at boot (or resume) and ends at PARK/SHUTDOWN or when the venue changes. A new session also starts on mode change.

### 12.2 Frame format

```
offset 0   sync   u8   0xA5
offset 1   type   u8
offset 2   len    u8   payload length 0..247
offset 3   payload[len]
offset 3+len  crc  u16 LE  CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout) over bytes 1..(2+len)
```

Reader: scan for `0xA5`; read type/len; if `len > 247` resync; read payload+crc; verify; on mismatch advance one byte and rescan. A valid frame is never ambiguous with an in-payload `0xA5` because the CRC is checked before acceptance.

### 12.3 Record types

| Type | Name | Payload (packed, LE) | Size |
|------|------|----------------------|------|
| 0x01 | `SESSION_HDR` | `ver u8=1, session_id char[10], mode u8, variant u8, venue_id u16, layout_id u16, fw char[16], hwid char[24], log_profile u8, fused_hz u8, gps_hz u8, start_gps_us i64 (0 if unknown), calib: r i16[9] (×1e-4), gbias i16[3], calib_flags u8` | 94 |
| 0x02 | `FIX_KEY` | `gps_us i64, lat_e7 i32, lon_e7 i32, alt_mm i32, gspeed_mms i32, head_e5 i32, hacc_mm u32, sacc_mms u16, pdop_e2 u16, fix_type u8, sats u8, flags u8` | 39 |
| 0x03 | `FIX_DELTA` | `dt_ms u16, dlat_e7 i16, dlon_e7 i16, dalt_dm i16, gspeed_cms u16, head_e2 u16, hacc_dm u8, sats u8, flags u8` | 15 |
| 0x04 | `FUSED` | `dt_ms u16, glat_e3 i16, glon_e3 i16, lean_cdeg i16, yaw_cdps i16, flags u8` | 11 |
| 0x05 | `LAP` | `lap_no u16, start_gps_us i64, time_ms u32, flags u8, n_sectors u8, sector_ms u32[n_sectors] (≤ 9), stats (14 B per §9.4)` | 30 + 4·n |
| 0x06 | `SECTOR` | `lap_no u16, idx u8, gps_us i64, split_ms u32, delta_ms i32` | 19 |
| 0x07 | `DRAG_RUN` | `run_no u16, t0_gps_us i64, flags u8, trap_cms u16, n_gates u8, gates[n]: {id u8, time_ms u32, speed_cms u16, dist_cm u32, hit u8}` | 14 + 12·n |
| 0x08 | `DRAG_GATE` | `run_no u16, gate_id u8, gps_us i64, time_ms u32, speed_cms u16, dist_cm u32` | 21 |
| 0x09 | `EVENT` | `mono_us i64, gps_us i64, code u16, arg u32` | 22 |
| 0x0A | `CALIB` | same calib block as header | 25 |
| 0x0B | `MARK` | `gps_us i64, kind u8` (user button mark) | 9 |
| 0x0C | `TIME_MAP` | `mono_us i64, gps_us i64, quality u8` | 17 |
| 0x0D | `VENUE` | `venue_id u16, layout_id u16, name char[32]` | 36 |
| 0x0E | `POWER` | `mono_us i64, state u8, batt_mv u16` | 11 |
| 0x7F | `END` | `gps_us i64, reason u8` | 9 |

`FIX_DELTA` flags: bit0 valid, bit1 gnssFixOK, bit2 3D. `FUSED` flags = `fused_sample_t.flags`. `LAP` flags: bit0 GPS_LOST, bit1 PIT, bit2 INCOMPLETE, bit3 OUT_LAP, bit4 INTERRUPTED, bit5 TOO_LONG, bit6 VALID.

### 12.4 Delta encoding rules

- `FIX_KEY` is written for the first fix, every `FIX_KEYFRAME_S` seconds, after any invalid fix, and whenever a delta field would overflow (`|dlat| > 32767`, `dt > 65535 ms`, speed > 655 m/s, alt delta > 3276 m).
- `FIX_DELTA.dt_ms` is relative to the previous `FIX_*` record; `hacc_dm` saturates at 255 (25.5 m).
- `FUSED.dt_ms` is relative to the previous `FUSED` record, or to the last `FIX_*` record if none since (the reader tracks both).
- `TIME_MAP` is written at session start, whenever `tb` quality changes, and every 60 s.

### 12.5 Summary file

`.sum` = `SESSION_HDR` frame + `VENUE` frame + every `LAP` frame + every `DRAG_RUN` frame + `END` frame (when session closes). Written by: build the whole content in the logger's 4 KB buffer (a 60-lap session is ~3 KB; if larger, laps beyond the first 100 are summarised only in the `.log`), write to `.sum.tmp`, `sto_sync`, `sto_rename` to `.sum`. Rewritten at every `LAP`/`DRAG_RUN` and at session end. The reader of a `.sum` uses the same frame decoder.

### 12.6 Logging rates

Framed sizes: `FIX_DELTA` 15 + 5 framing = 20 B; `FUSED` 11 + 5 = 16 B; `FIX_KEY` 44 B every 5 s.

| Profile | `fix` | `fused` | Bytes/s moving | MB/h | Riding hours retained (1.3 MB) |
|---------|-------|---------|----------------|------|--------------------------------|
| internal (NEO-6M) | 5 Hz × 20 B | 10 Hz × 16 B | ~270 | ~0.95 | ~1.4 (≈ 2.7 at `log.fused_hz = 5`) |
| internal (M10) | 10 Hz × 20 B | 10 Hz × 16 B | ~370 | ~1.3 | ~1.0 |
| sd | GPS rate × 20 B | 25 Hz × 16 B | ~500–600 | ~2.0 | card-limited |

Summaries are never evicted, so timing results outlive sample logs.

Logging pauses (no `FIX_*`/`FUSED`) when `still` for 10 s; resumes with a `FIX_KEY`.

### 12.7 Eviction (internal)

At session start and every 60 s, if `free_kb < 10 % of total`: delete the oldest `.log` (by session id order) that is not the current session. `.sum` files are never auto-deleted. If no `.log` remains to delete and free < 5 %, logging of samples stops (`SYS_STORAGE_FULL`), summaries continue (they need < 4 KB). Logged `E_STO_EVICT` / `E_STO_FULL`.

---

## 13. Storage drivers

### 13.1 `storage_internal` (LittleFS)

- Partition `storage`, type `data`, subtype `spiffs` label (LittleFS uses the same subtype id 0x82), mounted at `/lfs` via `esp_littlefs` (joltwallet component, pinned version).
- Config: `format_if_mount_failed = false` (the ladder decides), `read_size 128, prog_size 128, block_size 4096, cache_size 512, lookahead_size 128, block_cycles 512`.
- Mount ladder: mount → fail → mount again → fail → `esp_littlefs_format` → mount → fail → `SYS_STORAGE_DEAD` (summaries kept in RTC/NVS best-effort: last lap and best lap only).
- `sto_rename` is atomic. `sto_sync` maps to `fsync`.
- Directory `/lfs/sessions` and `/lfs/tracks` created at mount if missing.

### 13.2 `storage_sd` (FAT over SPI)

- `sdspi_host` on VSPI (shared with e-paper; the SPI bus is shared via IDF's bus arbitration; e-paper transfers use CS 5 and SD uses CS 15), 20 MHz, `max_files 4`, `allocation_unit_size 16 KB`.
- Mount ladder: mount → fail → re-init host → mount → fail → `degraded = 1` (internal LittleFS takes over `.sum`, `.log` disabled, `E_STO_SD_DEGRADED`). Every 60 s a remount is attempted while degraded.
- Write error at runtime: `sto_close`, unmount, remount, reopen in append, retry the write; twice → degrade as above.
- `.sum` mirror: after every successful `.sum` rename on SD, the same bytes are written to internal `/lfs/sessions/<id>.sum` (atomic). Export prefers the SD copy when present.
- `sto_probe` at boot: write 512 B, read back, compare, unlink.
- Card removal is detected by a write error (no card-detect pin).

### 13.3 Logger task

```
loop:
  wait on ring notify or 1000 ms
  while fix_ring / fused_ring not empty: encode into batch (4096 B)
  drain evt_q copy: LAP/SECTOR/DRAG/EVENT/VENUE/POWER → encode into batch; LAP/DRAG_RUN also → summary rebuild flag
  if batch ≥ 3584 B or ≥ 1000 ms since last write: sto_write(batch)
  if ≥ 2000 ms since last sync: sto_sync
  if summary flag: rebuild .sum (§12.5)
  eviction check every 60 s
  hb[LOGGER]++
```

The logger owns the open `.log` handle. Session open/close is commanded by the power task (`LOGGER_OPEN_SESSION`, `LOGGER_CLOSE_SESSION`) via `ui_req_q`-style request queue `log_req_q` (depth 4).

---

## 14. Export formats

### 14.1 VBO (Racelogic)

Text, CRLF line endings, ASCII. Generated by streaming `.log` frames through `exp_t` with format `EXP_VBO`.

```
File created on 14/09/2026 at 10:15:00

[header]
satellites
time
latitude
longitude
velocity kmh
heading
height
lat_g
lon_g
lean
yaw

[channel units]

[comments]
LapTimer v0.3.1 (moto_neo6m_epaper)
Session S00042_001
Venue Killarney / Full

[laptiming]
Start -1110.9000 -2031.4020 -1110.9180 -2031.4092

[column names]
sats time lat long velocity heading height lat_g lon_g lean yaw

[data]
008 101500.00 -2031.40200 -1110.90000 123.45 090.12 0045.00 +0.120 -0.050 +12.30 +05.20
```

Rules:
- `time` = UTC `HHMMSS.SS` from `gps_us`.
- `lat` = decimal minutes, `deg·60 + min`, signed (south negative), 5 decimals.
- `long` = decimal minutes, **west positive** (Racelogic convention): `long = −(lon_deg · 60)`. East longitudes therefore appear negative (as in the example for 18.5°E → −1110.9).
- `velocity` km/h, 2 decimals. `heading` degrees, 2 decimals, zero-padded to 3 integer digits. `height` metres MSL, 2 decimals.
- `lat_g`, `lon_g` in g with sign, 3 decimals. `lean` degrees signed, 2 decimals. `yaw` deg/s signed, 2 decimals.
- One row per `FIX_*` record. `FUSED` values are held from the latest `FUSED` record before the fix (sample-and-hold); rows before any fused sample carry `0`.
- Invalid fixes (`flags` bit0 = 0) are still emitted with `sats` as recorded; consumers use sats/velocity to judge.
- `[laptiming]` `Start` line = S/F line endpoints in VBO `long lat long lat` order (minutes, west-positive), only when a layout was locked. RaceChrono and Circuit Tools use it to split laps; if a consumer ignores it, laps are re-detected from its own track database.

### 14.2 NMEA

Per `FIX_*` record, two sentences, CRLF:

```
$GPRMC,101500.00,A,3351.40200,S,01830.90000,E,66.66,090.12,140926,,,A*hh
$GPGGA,101500.00,3351.40200,S,01830.90000,E,1,08,1.2,45.0,M,0.0,M,,*hh
```

- Lat `DDMM.MMMMM`, lon `DDDMM.MMMMM`, hemispheres from sign.
- RMC speed in knots (`m/s · 1.943844`), course degrees, date `DDMMYY`, status `A` if valid else `V`.
- GGA fix quality 1 (valid) or 0, sats 2-digit, HDOP from `pdop_e2/100` (VBO consumers accept PDOP here), altitude MSL, geoid separation 0.
- Checksum: XOR of all bytes between `$` and `*`, two uppercase hex digits.

### 14.3 JSON (session list and summary)

`LIST` response:
```json
{"proto":1,"sessions":[
  {"id":"S00042_001","start_utc":1789640100,"mode":"lap","venue":"Killarney","layout":"Full",
   "laps":12,"best_ms":111900,"log_kb":180,"sum_kb":3,"has_log":true}
]}
```
`OPEN fmt=json` (summary) response: `{"id":..., "hdr":{...}, "laps":[{"n":1,"ms":112340,"valid":true,"flags":0,"sectors":[32100,41000,39240],"stats":{...}}], "runs":[...]}`.

Written by `exp_json.c` with a minimal writer (no library); numbers only, strings escaped.

---

## 15. Configuration and NVS

### 15.1 Config struct and JSON schema (version 1)

| JSON path | C field | Type | Range | Default | Notes |
|-----------|---------|------|-------|---------|-------|
| `version` | `version` | u8 | 1 | 1 | schema version |
| `units` | `units` | enum | `kmh`,`mph` | `kmh` | display + bench list selection |
| `mode` | `mode` | enum | `lap`,`drag` | `lap` | persisted last mode |
| `lap.min_lap_s` | `lap.min_lap_s` | u16 | 5–600 | 20 | |
| `lap.max_lap_s` | `lap.max_lap_s` | u16 | 60–3600 | 1800 | |
| `lap.gate_rearm_m` | `lap.gate_rearm_m` | u16 | 10–500 | 50 | |
| `lap.pit_speed_kmh` | `lap.pit_speed_kmh` | u8 | 1–30 | 5 | |
| `lap.pit_time_s` | `lap.pit_time_s` | u8 | 3–60 | 10 | |
| `lap.default_layout` | `lap.default_layout[8]` | array of `{venue:u16, layout:u16}` | ≤ 8 | empty | |
| `drag.benches_kmh` | `drag.benches_kmh[4]` | u16[] | 10–400 | [100,200,300] | headline benches |
| `drag.benches_mph` | `drag.benches_mph[4]` | u16[] | 10–250 | [60,120,180] | |
| `drag.rollout` | `drag.rollout` | bool | | false | 1 ft rollout |
| `drag.launch_g` | `drag.launch_g_e2` | u8 | 5–50 (×0.01 g) | 15 | |
| `power.pit_after_s` | `power.pit_after_s` | u16 | 10–600 | 30 | |
| `power.park_after_s` | `power.park_after_s` | u16 | 60–7200 | 600 | |
| `power.shutdown_mv` | `power.shutdown_mv` | u16 | 3000–3600 | 3300 | |
| `power.conn_idle_s` | `power.conn_idle_s` | u16 | 30–1800 | 300 | |
| `display.live_clock` | `display.live_clock` | bool | | false (epaper) / true (oled) | |
| `display.full_refresh_every` | `display.full_every` | u8 | 1–50 | 10 | partials between full refreshes |
| `display.rotation` | `display.rotation` | u8 | 0,180 | 0 | |
| `display.invert` | `display.invert` | bool | | false | |
| `battery.cal` | `battery.cal[2]` | `{adc_mv:u16, true_mv:u16}`×2 | | identity | two-point |
| `ble.name` | `ble.name[16]` | string | ≤ 15 chars | `LapTimer-XXXX` (last 2 MAC bytes) | |
| `ble.adv_s` | `ble.adv_s` | u16 | 15–600 | 60 | |
| `log.fused_hz` | `log.fused_hz` | u8 | 5,10,25 | profile default | |
| `gps.dyn_model` | `gps.dyn_model` | u8 | 0–8 | 4 | |
| `gps.rate_hz` | `gps.rate_hz` | u8 | 1–profile max | profile max | |
| `imu.mot_thr` | `imu.mot_thr` | u8 | 2–255 | 20 | motion wake threshold LSB |
| `imu.mot_dur_ms` | `imu.mot_dur_ms` | u8 | 1–255 | 40 | |

`cfg_validate` clamps every field into range and returns the number of corrections; the supervisor logs `E_SYS_CFG_RESET` with that count if > 0. Unknown JSON keys are ignored; missing keys keep current values (`CONFIG_SET` is a merge).

### 15.2 NVS layout

| Namespace | Key | Type | Content |
|-----------|-----|------|---------|
| `lt_cfg` | `cfg` | blob | packed `cfg_t` with leading `version u8`, trailing CRC16 |
| `lt_cal` | `fus` | blob | `fus_calib_t` |
| `lt_cal` | `mag` | blob | QMC hard-iron offsets (M10 only) |
| `lt_err` | `ring` | blob | 32 × `{code u16, uptime_s u32, boot u16, arg u32}` = 384 B, plus `head u8` |
| `lt_err` | `ctr` | blob | counters `{boots, crashes, wdt, brownout, gps_reset, i2c_recover, sto_format, ota_ok, ota_rollback}` u32 each |
| `lt_sys` | `boot_cnt` | u32 | |
| `lt_sys` | `crash_log` | blob | last 3 `{reset_reason u8, uptime_s u32}` for crash-loop detection |
| `lt_sys` | `safe_until` | u32 | boot counter until which safe mode applies |
| `lt_sys` | `ota_pending` | u8 | 1 while a new image awaits validation (belt-and-braces beside otadata) |

NVS writes are batched: config and calibration only on change; error ring entries immediately (they are rare); counters at most once per 60 s except crash paths.

### 15.3 RTC memory (`RTC_DATA_ATTR rtc_state_t`)

```c
typedef struct {
    uint32_t magic;            /* 0x4C505452 'LPTR' */
    uint8_t  version;          /* 1 */
    uint8_t  mode, power_state, _pad;
    int64_t  saved_gps_us;     /* when saved; resume only if now − saved < RTC_RESUME_MAX_S */
    int64_t  session_epoch_mono_us;
    char     session_id[10];
    uint16_t venue_id, layout_id;
    uint16_t lap_no; uint8_t sector_idx; uint8_t _pad2;
    int64_t  lap_start_gps_us;
    int64_t  gate_times[LAP_MAX_SECTORS + 1];
    lap_result_t best, prev;   /* trimmed copies */
    uint32_t partial_count;    /* e-paper partial refreshes since last full */
    uint32_t crc32;
} rtc_state_t;
```

Updated by the pipeline on every S/F and sector event and by the power task on every state change. CRC32 over all bytes except `crc32`.

---

## 16. Power management

### 16.1 States and transitions

```
BOOT ──► ACTIVE ◄──► PIT ◄──► PARK
           │  ▲        │
           │  └────────┘ (motion / button)
           ▼
       CONNECTED (menu, stationary only) ──► back to previous
any ──► SHUTDOWN
```

| From | To | Guard |
|------|----|-------|
| BOOT | ACTIVE | RTC resume says ACTIVE and `EV_MOTION` within 5 s, else PIT |
| ACTIVE | PIT | no `EV_MOTION` for `power.pit_after_s` |
| PIT | ACTIVE | `EV_MOTION`, or any button, or IMU motion interrupt (wakes light sleep) |
| PIT | PARK | no `EV_MOTION` for `power.park_after_s` (counted from ACTIVE exit) |
| PARK | (BOOT) | EXT1 any-high (IMU INT, buttons) or EXT0 (charger) → deep-sleep wake → boot sequence with RTC resume |
| ACTIVE/PIT | CONNECTED | menu "Export" and `still` |
| CONNECTED | previous | disconnect + `power.conn_idle_s`, or menu exit, or `EV_MOTION` (transfer aborted with `E_CONN_XFER_ABORT`) |
| any | SHUTDOWN | `batt_mv < power.shutdown_mv` for `BATT_SHUTDOWN_HOLD_S` while ACTIVE/PIT, or menu "Sleep now" long-hold |
| SHUTDOWN | (BOOT) | EXT0 charger low or EXT1 button; boot refuses to leave BOOT unless `batt_mv ≥ BATT_RESTART_MIN_V` (shows LOW BATT, sleeps again) |

### 16.2 Per-state configuration

| Item | ACTIVE | PIT | PARK | CONNECTED |
|------|--------|-----|------|-----------|
| CPU | `esp_pm_configure{max 80, min 40, light_sleep false}` + `ESP_PM_NO_LIGHT_SLEEP` lock held | lock released; `light_sleep_enable = true`; tickless idle | deep sleep | max 80, min 80, no light sleep (BLE) |
| GPS | `GPS_PM_FULL`, configured rate | `GPS_PM_CYCLIC_1HZ`, messages off | `board_gps_power(false)` (or BACKUP) | as previous state |
| IMU | `IMU_FULL` 100 Hz FIFO | gyro bias update, then `IMU_LOWPOWER`, INT enabled | `IMU_LOWPOWER`, INT = EXT1 wake source | as previous |
| Pipeline | running | running (UART idle; IMU timer stopped; wakes on INT via GPIO ISR → `cmd_q`) | — | running |
| Logger | logging | session kept open, no samples | closed | closed for transfer consistency |
| Display | event refresh | event refresh | last image retained (zero power) | progress/status screens |
| Radio | off | off | off | BLE on |
| Supervisor | 1 Hz | 1 Hz (wakes from light sleep; acceptable) | — | 1 Hz |

UART wake from light sleep is not used (classic ESP32 loses the first bytes). In PIT the GPS sends nothing, so nothing is lost; the transition to ACTIVE re-enables messages.

### 16.3 Deep sleep entry (`PARK`, `SHUTDOWN`)

1. Power task sends `LOGGER_CLOSE_SESSION`; waits for ack (≤ 3 s).
2. `rtc_state_t` updated with `power_state`, `saved_gps_us`.
3. `disp_sleep()`.
4. `imu_set_mode(IMU_LOWPOWER)`; `imu_int_pending()` cleared.
5. `board_prepare_deep_sleep()`: `rtc_gpio_hold_en(26)` with level high (GPS off), RTC pull-downs on 32/33/25, `esp_sleep_enable_ext1_wakeup(mask{27,32,33,25}, ESP_EXT1_WAKEUP_ANY_HIGH)`, `esp_sleep_enable_ext0_wakeup(39, 0)` if wired.
6. SHUTDOWN additionally: IMU INT excluded from EXT1 (only buttons/charger wake), and `lt_sys/low_batt = 1` so boot checks voltage first.
7. `esp_deep_sleep_start()`.

### 16.4 Battery measurement

- `adc1_config_width(ADC_WIDTH_BIT_12)`, `adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11)`, `esp_adc_cal_characterize` with eFuse Vref/Two-Point when present. This board has VRef calibration in eFuse (verified 2026-09-15).
- Read: 64 samples, discard 8 highest and 8 lowest, mean → `esp_adc_cal_raw_to_voltage` → `v_tap_mv`. `batt_mv = v_tap_mv · 2` then two-point correction `batt_mv = a·batt_mv + b` from `battery.cal`.
- Filtering: EMA α = 0.2 at 1 Hz. Shutdown decision on the filtered value.
- SoC: piecewise-linear OCV table for INR cells `{4200:100, 4100:90, 4000:78, 3900:64, 3800:48, 3700:30, 3600:16, 3500:8, 3400:3, 3300:0}` (mV → %). Under load (ACTIVE) add +60 mV IR compensation before lookup.
- `POWER` records logged every 60 s and on state change.

### 16.5 Budget

| State | ESP32 | GPS | IMU | Regulator + misc | Total |
|-------|-------|-----|-----|------------------|-------|
| ACTIVE | 30 mA (80 MHz, DFS, radio off) | 40 mA | 4 mA | 1 mA | ~75 mA |
| PIT | 1.5 mA (light sleep, 1 Hz wakes) | 11 mA | 0.01 mA | 0.5 mA | ~13 mA |
| PARK | 0.01 mA | 0 | 0.01 mA | 0.1 mA | ~0.15 mA |
| CONNECTED (BLE transfer) | 60 mA | 11 mA | 0.01 mA | 1 mA | ~70 mA |
| E-paper refresh | +25 mA for 0.3–2 s per refresh | | | | |

Track day ≈ 2 h ACTIVE (150 mAh) + 6 h PIT (78 mAh) + 0.2 h CONNECTED (14 mAh) + 16 h PARK (2.4 mAh) ≈ 245 mAh → ~24 days on 6000 mAh. Acceptance: measured ACTIVE ≤ 95 mA, PIT ≤ 18 mA, PARK ≤ 0.3 mA.

---

## 17. Robustness and supervision

### 17.1 Watchdogs and protections

- Task WDT: `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`, `CONFIG_ESP_TASK_WDT_PANIC=y`, all six app tasks and both idle tasks subscribed. Each task calls `esp_task_wdt_reset()` once per loop iteration; long operations (e-paper full refresh ≤ 2 s, OTA write) reset it explicitly inside.
- Interrupt WDT: `CONFIG_ESP_INT_WDT=y`, 300 ms.
- Brownout: `CONFIG_ESP_BROWNOUT_DET=y`, level 2.8 V (`CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7` on classic ESP32 ≈ 2.8 V **[VERIFY]** mapping in menuconfig).
- Panic handler: `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`, `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF`, `CONFIG_ESP_COREDUMP_CHECKSUM_CRC32`.
- Stack overflow: `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY`.
- Heap: `CONFIG_HEAP_POISONING_LIGHT` in debug builds; `heap_caps_check_integrity_all` in the supervisor every 60 s in debug builds only.

### 17.2 Supervisor loop (1 Hz)

```
for each task t: if hb[t] == last_hb[t] for > HB_STALL_S(t): log E_SYS_TASK_STALL(t); if t == PIPELINE: esp_restart() after saving RTC state
gps_age = now − gps_last_frame_mono_us; run GPS ladder (§7.5) by posting CMD_GPS_LADDER_STEP(n) to pipeline
imu: consult pipeline-published imu_fault_level; run IMU ladder (§8.7) via CMD_IMU_RECOVER
storage: consult logger-published sto_state; ladder §13
heap: min_free < 40 KB → E_SYS_HEAP_LOW; < 20 KB → request conn_stop()
stack: uxTaskGetStackHighWaterMark per task < 256 B → E_SYS_STACK_LOW(t)
temperature: imu temp outside [EPAPER_TEMP_MIN, EPAPER_TEMP_MAX] → set SYS_DISP_TEMP_THROTTLE (ui reads)
ota: if pending-verify and validation criteria met → esp_ota_mark_app_valid_cancel_rollback()
sys_flags → ui (fault icons); EV_FAULT on changes
persist counters if dirty and ≥ 60 s
hb[SUPERVISOR]++
```

`HB_STALL_S`: pipeline 1 s, logger 5 s, ui 10 s (a full refresh may take 2 s), conn 10 s, power 5 s. Ladder steps are executed by the owning task (pipeline for GPS/IMU, logger for storage, ui for display), never by the supervisor itself, so driver calls stay single-threaded.

### 17.3 Ladders summary

| Subsystem | Steps | Detail |
|-----------|-------|--------|
| GPS | reconfigure → UART reinit + autobaud → power-cycle → DEAD (retry 60 s) | §7.5 |
| IMU | retry → bus recovery + reinit → DEAD (retry 60 s) | §8.7 |
| Display | reinit on BUSY timeout (5 s) → after 3 consecutive failures DEAD (retry 300 s) | §20.3 |
| Storage internal | mount → mount → format → DEAD | §13.1 |
| Storage SD | mount → re-init → degraded (retry 60 s) | §13.2 |
| BLE | `conn_stop`; `conn_start` on next menu entry; stack deinit/reinit on `E_CONN_BLE_INIT` twice | §18.2 |
| Heap | BLE off < 20 KB; sample logging off < 12 KB | |

### 17.4 System flags (`sys_flags`, atomic u32)

| Bit | Name | Icon |
|-----|------|------|
| 0 | `SYS_GPS_DEAD` | GPS strike-through |
| 1 | `SYS_GPS_NOFIX` | GPS hollow |
| 2 | `SYS_IMU_DEAD` | IMU strike-through |
| 3 | `SYS_IMU_SUSPECT` | IMU "?" |
| 4 | `SYS_DISP_DEAD` | — |
| 5 | `SYS_STORAGE_DEAD` | disk strike-through |
| 6 | `SYS_STORAGE_FULL` | disk full |
| 7 | `SYS_STORAGE_DEGRADED` | disk "!" |
| 8 | `SYS_BATT_LOW` | battery |
| 9 | `SYS_SAFE_MODE` | "SAFE" |
| 10 | `SYS_HEAP_LOW` | — |
| 11 | `SYS_DISP_TEMP_THROTTLE` | thermometer |
| 12 | `SYS_OTA_PENDING` | — |
| 13 | `SYS_FUSION_DISAGREE` | lean "?" |

### 17.5 Crash-loop protection and safe mode

At boot, `crash_log` (last 3 entries) is shifted with `{reset_reason, previous uptime}`; if all three are abnormal (`ESP_RST_PANIC`, `ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT`, `ESP_RST_WDT`, `ESP_RST_BROWNOUT`) with uptime < 60 s, `safe_until = boot_cnt + 1` and `SYS_SAFE_MODE` is set. Safe mode:
- Pipeline runs; lap and drag engines run; summaries are written.
- No sample logging, no BLE, no WiFi, one full-screen "SAFE MODE" render then display idle.
- Supervisor clears safe mode after `SAFE_MODE_CLEAR_S` of uptime (persisted), next boot is normal.

### 17.6 Boot self-test

| Check | Method | On failure |
|-------|--------|-----------|
| IMU present | `WHO_AM_I` | `SYS_IMU_DEAD`, `E_IMU_WHOAMI` |
| IMU genuine | self-test §8.5 | `SYS_IMU_SUSPECT`, `E_IMU_SELFTEST_FAIL` |
| GPS present | `MON-VER` reply within 1.5 s after configure | GPS ladder starts |
| Display | `disp_init` returns 0 and BUSY clears after reset | `SYS_DISP_DEAD` |
| Storage | `sto_probe` | ladder |
| Battery | `2900 ≤ batt_mv ≤ 4350` | out of range → `E_PWR_LOW_BATT` or sensing fault flagged |
| RTC state | magic/version/CRC | discarded, `E_SYS_RTC_INVALID` |
| Config | `cfg_validate` corrections == 0 | `E_SYS_CFG_RESET` |
| Coredump | `esp_core_dump_image_check` | `E_SYS_PANIC` entry with summary; image kept |

Boot screen lists each as `OK` / `FAIL` for 2 s (skipped when `SYS_SAFE_MODE`).

### 17.7 Error codes

| Code | Name | Code | Name |
|------|------|------|------|
| 0x0101 | `E_GPS_SILENT` | 0x0401 | `E_STO_WRITE` |
| 0x0102 | `E_GPS_RECONFIG` | 0x0402 | `E_STO_MOUNT` |
| 0x0103 | `E_GPS_UART_REINIT` | 0x0403 | `E_STO_FORMAT` |
| 0x0104 | `E_GPS_POWER_CYCLE` | 0x0404 | `E_STO_FULL` |
| 0x0105 | `E_GPS_DEAD` | 0x0405 | `E_STO_EVICT` |
| 0x0106 | `E_GPS_BAD_FIX` | 0x0406 | `E_STO_SD_DEGRADED` |
| 0x0107 | `E_GPS_RESET_DETECTED` | 0x0501 | `E_SYS_WDT_RESET` |
| 0x0108 | `E_GPS_CFG_NAK` | 0x0502 | `E_SYS_PANIC` |
| 0x0201 | `E_IMU_NACK` | 0x0503 | `E_SYS_BROWNOUT` |
| 0x0202 | `E_IMU_BUS_RECOVERY` | 0x0504 | `E_SYS_HEAP_LOW` |
| 0x0203 | `E_IMU_DEAD` | 0x0505 | `E_SYS_SAFE_MODE` |
| 0x0204 | `E_IMU_FROZEN` | 0x0506 | `E_SYS_TASK_STALL` |
| 0x0205 | `E_IMU_FIFO_OVF` | 0x0507 | `E_SYS_RTC_INVALID` |
| 0x0206 | `E_IMU_SELFTEST_FAIL` | 0x0508 | `E_SYS_CFG_RESET` |
| 0x0207 | `E_IMU_WHOAMI` | 0x0509 | `E_SYS_STACK_LOW` |
| 0x0301 | `E_DISP_BUSY_TIMEOUT` | 0x0601 | `E_PWR_LOW_BATT` |
| 0x0302 | `E_DISP_DEAD` | 0x0602 | `E_PWR_SHUTDOWN` |
| 0x0303 | `E_DISP_TEMP` | 0x0701 | `E_CONN_BLE_INIT` |
| 0x0801 | `E_OTA_SIG` | 0x0702 | `E_CONN_XFER_ABORT` |
| 0x0802 | `E_OTA_HWID` | 0x0703 | `E_CONN_PROTO` |
| 0x0803 | `E_OTA_ROLLBACK` | 0x0901 | `E_LAP_PLAUSIBILITY` |
| 0x0804 | `E_OTA_PRECOND` | 0x0902 | `E_FUSION_DISAGREE` |
| 0x0805 | `E_OTA_WRITE` | 0x0903 | `E_FUSION_CALIB_LOST` |
| 0x0806 | `E_OTA_VALIDATED` (info) | | |

### 17.8 Flash-write stalls

Classic ESP32 disables the instruction cache during SPI flash erase/write; code not in IRAM stalls on both cores for the duration (4 KB erase ≈ 20–40 ms, 256 B program ≈ 0.3 ms). Consequences and mitigations:
- `CONFIG_UART_ISR_IN_IRAM=y`, `CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y`, `CONFIG_SPI_MASTER_ISR_IN_IRAM=y`, PPS ISR `IRAM_ATTR`; UART RX ring 2 KB absorbs 500 ms at 38400 baud.
- IMU FIFO (1 KB = 85 samples = 850 ms) absorbs stalls.
- Logger writes in 4 KB batches at most once per second; OTA writes happen only when stationary.
- Timestamps in the GPS domain are unaffected; `mono_us` stamps for IMU samples are back-dated from FIFO order, so a delayed read does not shift them.

### 17.9 Code rules

- C11, `-Wall -Wextra -Werror -Wshadow` plus `-Wconversion` as a non-fatal warning (core), `-Os` on target. Vendored third-party files (Unity, jsmn) are compiled with warnings relaxed.
- No `malloc` after init; all buffers static or in task-owned structs. `CONFIG_COMPILER_STACK_CHECK_MODE_STRONG` in debug builds.
- Core assertions: `CORE_ASSERT(cond, code)` logs `code` and returns an error; never aborts on target. Host tests map it to Unity `TEST_FAIL`.
- All time int64 µs; no floating-point time.
- All on-flash records versioned and CRC'd.
- ISRs only set flags / push to queues from ISR-safe APIs; they are `IRAM_ATTR`.

### 17.10 Diagnostics screen and export
Menu → Diagnostics shows: fw version, hwid, uptime, boots, crashes, WDT resets, GPS resets, I2C recoveries, GPS state (fix, sats, hAcc, rate), IMU state (temp, bias age), storage free, heap min-free, battery mV/%, last 5 error codes. `ERRLOG_GET`/`DIAG_GET` export the full ring and counters as JSON.

---

## 18. Connectivity

### 18.1 Command protocol (transport-agnostic, `app/cmd`)

Request frame (from client): `op u8 | tag u8 | payload[]`. Response stream (to client): zero or more `data` chunks `tag u8 | chunk_seq u16 | flags u8 | payload[≤ 496]`; `flags` bit0 `LAST`, bit1 `ERROR` (payload = `code u16 | msg utf8`). A request with tag T is answered only by chunks with tag T. One request in flight at a time; a new request while streaming aborts the stream (`E_CONN_XFER_ABORT`).

| op | Name | Request payload | Response |
|----|------|-----------------|----------|
| 0x01 | `STATUS` | — | 20-byte status (§18.2) |
| 0x02 | `LIST` | — | JSON §14.3 (may span chunks) |
| 0x03 | `OPEN` | `id char[10], fmt u8` (0 json summary, 1 vbo, 2 nmea, 3 raw log, 4 raw sum) | file bytes streamed from offset 0, `LAST` on final chunk, followed by a 4-byte CRC32 in the final chunk's tail |
| 0x04 | `READ` | `offset u32` | resume the open stream from offset |
| 0x05 | `CLOSE` | — | ack (empty LAST) |
| 0x06 | `DELETE` | `id char[10]` | ack |
| 0x10 | `CONFIG_GET` | — | JSON |
| 0x11 | `CONFIG_SET` | JSON (merge) | ack or ERROR with validation message |
| 0x12 | `TRACKS_GET` | — | JSON array of venues (bundled + user, `"src"` field) |
| 0x13 | `TRACKS_PUT` | JSON venue | ack; persisted to `user.bin` |
| 0x14 | `ERRLOG_GET` | — | JSON |
| 0x15 | `ERRLOG_CLEAR` | — | ack |
| 0x16 | `DIAG_GET` | — | JSON |
| 0x17 | `COREDUMP_GET` | — | raw partition bytes (≤ 64 KB) |
| 0x18 | `COREDUMP_CLEAR` | — | ack |
| 0x20 | `OTA_BEGIN` | `size u32, sha256[32], version char[16], hwid char[24]` | ack or ERROR (`E_OTA_PRECOND`, `E_OTA_HWID`) |
| 0x21 | `OTA_DATA` | `offset u32, bytes[]` | ack per chunk (empty LAST) |
| 0x22 | `OTA_END` | — | ack after signature + hwid verification, then reboot in 2 s |
| 0x23 | `OTA_ABORT` | — | ack |
| 0x30 | `TIME_SYNC` | `unix_s u32` | ack (used only when GPS has no fix; sets a provisional clock for file naming; never used for timing) |

Errors: `E_CONN_PROTO` for unknown op / malformed payload.

### 18.2 BLE (`conn_ble`)

- Stack: NimBLE (`CONFIG_BT_NIMBLE_ENABLED=y`, Bluedroid off), `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`, `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512`, `CONFIG_BT_NIMBLE_ROLE_CENTRAL=n`, `OBSERVER=n`.
- Device name `cfg.ble.name`. Advertising: connectable undirected, interval 100 ms, for `cfg.ble.adv_s` seconds; then stop and return to previous power state.
- Service UUID `7a3f0001-1b2c-4d5e-8f90-a1b2c3d4e5f6`.
  - `cmd` `7a3f0002-…` WRITE / WRITE_NO_RSP, max 512.
  - `data` `7a3f0003-…` NOTIFY, value length 500 = 4-byte chunk header (tag, seq u16, flags) + ≤ 496 payload (fits MTU 512 minus the 3-byte ATT header).
  - `status` `7a3f0004-…` READ / NOTIFY, 20 bytes: `proto_ver u8=1 | state u8 | sys_flags u16 (low 16 bits) | batt_pct u8 | batt_mv u16 | storage_free_kb u32 | session_count u16 | fw char[7] (e.g. "0.3.1\0")`. Notified on change, at most 1 Hz.
- Connection parameters requested on connect: interval 15–30 ms, latency 0, timeout 4 s during transfer; after 5 s idle: 200–500 ms, latency 4.
- Flow control: `conn_send` calls `ble_gatts_notify_custom`; on `BLE_HS_ENOMEM` it waits on a semaphore given by the `BLE_GAP_EVENT_NOTIFY_TX` callback (≤ 500 ms) and retries; after 10 s of no progress → `E_CONN_XFER_ABORT`.
- Security: no bonding, no encryption in v1 (O7 adds LE Secure Connections passkey displayed on the e-paper). Attack surface is limited by advertising only on user action for ≤ `adv_s` seconds.
- Throughput target ≥ 20 KB/s measured with 496-byte chunks at 15 ms interval and DLE enabled (`CONFIG_BT_NIMBLE_...` data length extension on).

### 18.3 Web Bluetooth page (`tools/web/export.html`)

Single HTML file, no build step, vanilla ES2020, ≤ 60 KB.

Sections:
1. **Connect**: `navigator.bluetooth.requestDevice({ filters:[{ services:[SERVICE_UUID] }] })`; on connect subscribe to `data` and `status`; show name, fw, battery, storage, flags.
2. **Sessions**: table from `LIST` (id, start local time, venue/layout, laps, best); per row: `VBO`, `NMEA`, `Summary JSON`, `Delete`. Download assembles chunks into a `Uint8Array`, verifies CRC32, wraps in a `Blob`, triggers `<a download>` (`S00042_001.vbo`). Progress bar from bytes/size (size from LIST `log_kb`).
3. **Config**: form generated from `CONFIG_GET` JSON with the schema table (§15.1) embedded for ranges; `Save` sends `CONFIG_SET` with only changed keys.
4. **Tracks**: list from `TRACKS_GET`; textarea to paste venue JSON → `TRACKS_PUT`; map link to Google Maps for S/F coordinates.
5. **Diagnostics**: `DIAG_GET`, `ERRLOG_GET`, `Clear`, `Download coredump`.
6. **Firmware**: file picker for `.bin` + `.sig` produced by `sign_release.sh`; reads `esp_app_desc_t` (image offset 0x20) and the custom hwid (offset 0x120) from the binary to display version and hwid, computes SHA-256 in JS, runs `OTA_BEGIN`/`OTA_DATA` (496-byte chunks)/`OTA_END`, shows progress, waits for reconnect and reads `status.fw`.

Platform notes displayed on the page: Android Chrome and desktop Chrome/Edge supported; iOS requires Bluefy. The page is served from GitHub Pages (HTTPS) and also works when opened as a local file in Chrome.

### 18.4 Serial fallback (`export_serial`)

- UART0, 921600 baud (CH340 supports up to 2 Mbaud), IDF `esp_console` REPL with line editing disabled (`CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600`).
- Commands (text): `status`, `list`, `open <id> <vbo|nmea|json|log|sum>`, `read <offset>`, `close`, `delete <id>`, `config get`, `config set <json>`, `tracks get`, `tracks put <json>`, `errlog`, `errlog clear`, `diag`, `coredump`, `dbg <hang|crash|gps raw on|gps raw off|imu raw on|imu raw off|power <active|pit|park|shutdown>|sim on|sim off>`.
- File output framing: `---BEGIN <name> <size>---\r\n` … raw text … `---END <crc32 hex>---\r\n`. Binary formats (log/sum/coredump) are Base64-encoded between the markers.
- ESP-IDF logging is redirected to level `ERROR` for the duration of an `open` transfer, restored at `close`/END.
- `tools/serial_export.py <port> <command...>` wraps the protocol and writes files; plain `screen`/`minicom` also works.
- OTA over serial is not implemented; use `idf.py flash` / `esptool.py`.

### 18.5 WiFi AP (`conn_wifi`, O1)

- `esp_wifi` AP mode, SSID `cfg.ble.name`, WPA2 with a passphrase shown on the e-paper (random 8 digits regenerated when changed in config), channel 6, max 2 stations, `192.168.4.1/24`, DHCP.
- `esp_http_server`: `GET /` (export.html from flash, gzip), `GET /api/status`, `GET /api/sessions`, `GET /api/session/{id}.{vbo|nmea|json|log|sum}`, `DELETE /api/session/{id}`, `GET|PUT /api/config`, `GET|POST /api/tracks`, `GET /api/errlog`, `DELETE /api/errlog`, `GET /api/diag`, `GET /api/coredump`, `POST /api/ota` (raw body, headers `X-Size`, `X-SHA256`, `X-Version`, `X-HWID`).
- mDNS `laptimer.local`. Captive portal not implemented.
- Same `cmd` handler; the HTTP layer maps routes to ops and streams chunked responses.
- Power: ~150 mA while up; same enter/exit rules as CONNECTED; BLE and WiFi are never up simultaneously.

### 18.6 RaceChrono DIY live (`conn_ble_rc`, O2)

- Second GATT service UUID `0x1FF8` alongside the export service.
- Characteristic `0x0003` (GPS main, NOTIFY, 20 bytes): per the RaceChrono DIY specification: `sync/time (3+21 bits: time since hour start in ms)`, fix quality, sats, `lat/lon ×1e7`, altitude, speed, bearing, HDOP/VDOP with the specification's dual-range encoding. Sent per fix.
- Characteristic `0x0004` (GPS time, NOTIFY): `sync bits + (year−2000)·8928 + (month−1)·744 + (day−1)·24 + hour`. Sent when the hour changes and on connect.
- Characteristic `0x0001` (CAN-Bus, NOTIFY): `pid u32 | data[≤ 8]`. PIDs: `0x100` lean (int16 ×0.01°), `0x101` lat g (int16 ×0.001), `0x102` lon g, `0x103` yaw (int16 ×0.01 °/s), `0x104` combined g. Sent at 10 Hz.
- Characteristic `0x0002` (CAN-Bus filter, WRITE): honoured (deny-all / allow-all / allow-pid).
- Runs during ACTIVE when enabled from the menu ("Live to phone"); the connection is kept while riding; ~+15 mA. If both export and live are enabled, the single connection carries both services.

---

## 19. OTA firmware update

### 19.1 Partition table (`partitions.csv`, 4 MB flash)

```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
otadata,    data, ota,      0xF000,   0x2000,
phy_init,   data, phy,      0x11000,  0x1000,
ota_0,      app,  ota_0,    0x20000,  0x140000,
ota_1,      app,  ota_1,    0x160000, 0x140000,
coredump,   data, coredump, 0x2A0000, 0x10000,
storage,    data, spiffs,   0x2B0000, 0x150000,
```

`ota_0`/`ota_1` = 1.25 MB each. `storage` = 1.3125 MB. Sizes are fixed; a bigger flash module enlarges `storage` only (generated variant of the CSV via `build.sh --flash-size 8MB`). The app image MUST be ≤ 1.25 MB including the signature block; `build.sh size` fails the build above 1.2 MB to keep margin.

### 19.2 Image signing

- `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`, `CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME=y` (ESP32 Secure Boot V1 signature scheme, works on all ESP32 revisions), `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y`, `CONFIG_SECURE_BOOT_VERIFICATION_KEY="keys/laptimer_pub.pem"` (public key embedded in the app).
- Key generation (once, offline): `espsecure.py generate_signing_key --version 1 keys/laptimer_priv.pem`; public key extracted with `espsecure.py extract_public_key --version 1 --keyfile keys/laptimer_priv.pem keys/laptimer_pub.pem`. The private key is stored outside the repository (`.gitignore` excludes `*.pem`; only `laptimer_pub.pem` is committed via `git add -f`).
- `tools/sign_release.sh <env>`: builds, signs (`espsecure.py sign_data --version 1 --keyfile ... build/<env>/laptimer.bin`), verifies (`espsecure.py verify_signature`), writes `dist/laptimer-<version>-<hwid>.bin` and `dist/laptimer-<version>-<hwid>.sha256`.
- `esp_ota_end()` verifies the signature before the slot is marked bootable; the bootloader verifies again on boot. Unsigned or wrongly signed images fail with `E_OTA_SIG`.

### 19.3 Hardware id and version

`esp_app_desc_t.project_name` = `laptimer`, `.version` = `CFG_FW_VERSION`. The hardware id is embedded in a custom section: `const char __attribute__((section(".rodata_custom_desc"))) hwid[24] = CFG_HWID;` placed right after the app descriptor (IDF `CONFIG_APP_PROJECT_VER_FROM_CONFIG=n`; custom description via `esp_app_desc` custom area). Image layout: `esp_image_header_t` (24 B) + first `esp_image_segment_header_t` (8 B) + `esp_app_desc_t` (256 B) + custom description. So `esp_app_desc_t` starts at image offset 0x20 and `hwid` at offset 0x120. The OTA handler reads bytes 0x000–0x13F from the target partition after the first 4 KB are written and compares `hwid` (and `project_name == "laptimer"`) with the running image; mismatch → `E_OTA_HWID`, abort, partition erased.

### 19.4 Update flow

```
client                          device
OTA_BEGIN{size,sha,ver,hwid} → precondition check (§19.5) → esp_ota_begin(next slot, size) → ack
OTA_DATA{offset,bytes} ×N     → esp_ota_write; after first 4 KB: hwid check; running SHA-256 → ack each
OTA_END                       → verify SHA-256 == sha; esp_ota_end (signature check) → esp_ota_set_boot_partition → nvs ota_pending=1 → ack → 2 s → esp_restart
[boot]                          bootloader verifies signature, boots new slot as PENDING_VERIFY
                                supervisor: self-test OK ∧ GPS frame received ∧ storage mounted ∧ uptime ≥ OTA_VALID_UPTIME_S → esp_ota_mark_app_valid_cancel_rollback(); ota_pending=0; E_OTA_VALIDATED
                                any panic/WDT/brownout before that → bootloader rolls back (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) → E_OTA_ROLLBACK logged on next boot
```

Progress is shown on the display every 10 % (partial refresh) and in `status.state = STATE_OTA`.

### 19.5 Preconditions (checked at `OTA_BEGIN`, re-checked at `OTA_END`)
- Power state `CONNECTED`, `fus_is_still`, `EV_MOTION` not seen for 30 s.
- `batt_pct ≥ BATT_OTA_MIN_PCT` (50) or charger present.
- Logger session closed (`LOGGER_CLOSE_SESSION` acked).
- `size ≤ 0x140000 − 4096`.
- Not already `SYS_OTA_PENDING`.
Failure → `E_OTA_PRECOND` with a reason string.

### 19.6 Abort and failure handling
- Disconnect or `OTA_ABORT` mid-transfer: `esp_ota_abort`, slot left invalid, running image untouched, `E_CONN_XFER_ABORT`.
- `OTA_END` SHA mismatch: `esp_ota_abort`, `E_OTA_WRITE`.
- Signature failure: `E_OTA_SIG`.
- Rollback: the reverted image sees `ota_pending=1` at boot with itself running → logs `E_OTA_ROLLBACK`, clears the flag, and shows "UPDATE FAILED, REVERTED" for 3 s.

### 19.7 Compatibility rules
- Config (`lt_cfg`) version bump requires `cfg_migrate` from every prior version still in the field (initially just v1).
- `rtc_state_t.version` mismatch → discard state.
- Log/summary frame types are append-only; a new firmware MUST decode all older frame types; unknown frame types are skipped by the reader.
- Calibration blob version mismatch → recalibration prompt on the display.
- Bootloader is never updated OTA.

---

## 20. Display and UI

### 20.1 Display driver (`display_epaper_ssd1680`)

SPI: `SPI3_HOST` (VSPI), mode 0, 10 MHz, DC on GPIO 14 via pre-transfer callback, CS 5 hardware, queue size 4, DMA enabled, ISR in IRAM. RST 4, BUSY 35 polled at 1 ms with a timeout of 5 s (`-ETIMEDOUT` → ladder).

Panel table (`PANEL` build flag) **[VERIFY]** against the Waveshare reference driver of the actual panel:

| Panel | Res (w×h, portrait native) | Landscape logical | Full LUT | Partial LUT | Border | Notes |
|-------|----------------------------|-------------------|----------|-------------|--------|-------|
| `ws213v4` (2.13" V4, SSD1680) | 122×250 | 250×122 | on-chip (0x22: 0xF7) | on-chip (0x22: 0xFF after 0x3C 0x80) | 0x05 | RAM width 128 (16 bytes/row) |
| `ws29v2` (2.9" V2, SSD1680) | 128×296 | 296×128 | on-chip (0xF7) | on-chip (0xFF) | 0x05 | RAM width 128 |

Command sequence (SSD1680):
- **Reset**: RST low 10 ms, high 10 ms; wait BUSY; `0x12` (SW reset); wait BUSY.
- **Init**: `0x01` driver output control `(h−1) & 0xFF, (h−1) >> 8, 0x00`; `0x11` data entry `0x03` (x inc, y inc); `0x44` RAM X range `0x00, (w/8 − 1)`; `0x45` RAM Y range `0x00, 0x00, (h−1)&0xFF, (h−1)>>8`; `0x3C` border `0x05`; `0x18` temperature sensor `0x80` (internal); `0x21` display update control 1 `0x00, 0x80`; `0x4E 0x00`, `0x4F 0x00 0x00`; wait BUSY.
- **Full refresh**: write framebuffer to `0x24` (B/W RAM) and identical bytes to `0x26` (previous RAM, so the next partial diff is correct); `0x22 0xF7`; `0x20`; wait BUSY (≈ 2 s).
- **Partial refresh**: `0x3C 0x80` (border hold); set RAM window (`0x44`/`0x45`/`0x4E`/`0x4F`) to the dirty rectangle rounded to 8-px columns; write rectangle bytes to `0x24`; `0x22 0xFF`; `0x20`; wait BUSY (≈ 0.3–0.5 s). After the update, copy the rectangle into `0x26`.
- **Sleep**: `0x10 0x01`. **Wake**: hardware reset + init.
- Rotation: the driver rotates the logical landscape framebuffer into the panel's portrait RAM order during the write (nibble/bit transposition in a 128-byte line buffer; ≈ 1 ms at 80 MHz).
- Temperature: the panel's internal sensor is used by the on-chip LUT selection (`0x18 0x80`); the MPU6050 temperature only gates refresh permission (§17.2).

### 20.2 Framebuffer and renderer (`core/ui/render.c`, pure C, host-testable; screens in `core/ui/screens_moto.c`)

- 1 bpp, `width/8 × height` bytes, `0 = black` (e-paper convention inverted at blit if `display.invert`). Logical origin top-left, landscape.
- Primitives: `fb_clear`, `fb_rect`, `fb_hline`, `fb_text(font, x, y, str)`, `fb_text_right(...)`, `fb_icon(id, x, y)`, `fb_bar(x, y, w, h, pct)`.
- Dirty tracking: renderer accumulates a bounding box; `ui` passes it to `disp_refresh(PARTIAL)` via the driver's window API (`disp_set_window` extension in the e-paper driver; other drivers ignore).
- Host tests render every screen from a fixed model and write `test/snapshots/<screen>.pbm`; a golden comparison guards against layout regressions, and the PBMs are the review artefact before a panel exists.
- Fonts generated by `tools/fonts/gen_fonts.py` from DejaVu Sans Mono Bold: `FONT_BIG` 40 px (glyphs `0-9 : . - + S`), `FONT_MED` 24 px (`0-9 : . - + A-Z`), `FONT_SMALL` 12 px (ASCII 32–126). Stored as 1-bpp glyph bitmaps with a fixed advance per font.
- Icons 12×12: GPS, GPS-strike, IMU-q, disk, disk-full, disk-warn, battery-low, thermometer, BLE, SAFE.

### 20.3 Refresh policy and display ladder

- Partial refresh on: `EV_LAP_COMPLETE`, `EV_SECTOR`, `EV_DRAG_GATE`, `EV_DRAG_DONE`, `EV_VENUE_FOUND`, `EV_LAYOUT_LOCKED`, `EV_FIX_LOST`/`EV_FIX_OK`, `EV_FAULT` (flag change), battery crossing 20 %, page change, menu navigation, OTA progress.
- Full refresh when: `partial_count ≥ display.full_every`, or 30 min since last full, or on wake from PARK, or on page/menu entry when `still`. If the trigger occurs while moving and `partial_count < 2·full_every`, the full refresh is deferred until `still` (a partial is used instead).
- Coalescing: `ui` drains its event queue completely, updates the screen model, renders once, refreshes once.
- Never periodic while riding unless `display.live_clock`.
- Temperature throttle (`SYS_DISP_TEMP_THROTTLE`): partial refreshes limited to one per 30 s, full refreshes suppressed.
- Ladder: BUSY timeout → `disp_reinit` → retry the refresh once; three consecutive failures → `SYS_DISP_DEAD`, `E_DISP_DEAD`, retry `disp_reinit` every 300 s.

### 20.4 Screen model

`ui` keeps a `screen_model_t` updated from events: `best_ms, prev_ms, cur_ms_at_gate, cur_sector_idx, sector_delta_ms, lap_delta_ms, new_best, venue_name, layout_name, drag rows[4], flags, batt_pct, page, mode, menu state`. Rendering is a pure function of the model.

### 20.5 Riding screens (2.9", 296×128; 2.13" scales fonts to MED/SMALL)

**LAP** (page 0):
```
y=4   BEST  [FONT_BIG right-aligned "1:51.90"]         (row height 40)
y=46  PREV  [FONT_BIG "1:52.34"]
y=88  CUR   [FONT_MED "1:12.30"]  [FONT_MED "S2"]
y=112 ΔS    [FONT_MED signed "-0.21"]      fault icons bottom-right (x ≥ 260)
```
Labels in `FONT_SMALL` at x=4. Times right-aligned at x=200. New best: the ΔS row shows `BEST` instead of the delta for that lap. Before any lap: BEST/PREV show `--:--.--`. CUR resets to `0:00.00 S0` at S/F.

**DRAG** (page 0): four rows of `FONT_MED`: label left (x=4), time right-aligned at x=180, optional `@ 305` in `FONT_SMALL` at x=190 on the 1/4 row. Rows fill top-down as gates hit; empty rows blank. Armed indicator: small `ARMED` in `FONT_SMALL` top-right until launch.

**LAP page 1**: `BEST` lap with per-sector splits (`S1 32.10  S2 41.00  S3 39.24`) and `THEO 1:51.20`.
**LAP page 2**: session stats: `MAX SPD 214`, `LEAN L 52 R 55`, `LAT G 1.32`, `ACC 0.61 BRK 1.05`, `LAPS 12 (10 valid)`.
**DRAG page 1**: all gates of the last run (60ft, 330ft, 1/8, 1000ft, 1/4, 100-200, 100-0).
**DRAG page 2**: best per gate this session.

Fault icons: drawn only when the corresponding `sys_flags` bit is set, in a strip at bottom-right; `SYS_BATT_LOW` shows a battery icon with `%`. `SYS_GPS_NOFIX` shows the hollow GPS icon.

### 20.6 One-shot screens
`BOOT` (name, version, self-test lines), `VENUE` ("KILLARNEY" then "FULL" once locked, 2 s each, then back to page 0), `SAFE MODE`, `LOW BATT`, `OTA` (progress bar), `UPDATE FAILED, REVERTED`, `CALIBRATE` ("Hold upright, press MODE"), `NEW TRACK` ("Cross S/F, press MODE").

### 20.7 Menu

Entered by MODE long-press when `gspeed < MENU_LOCK_SPEED_KMH`; otherwise ignored (short flash of a lock icon). Items (UP/DOWN move, MODE select, long MODE back/exit, auto-exit after 30 s idle):

1. Mode: Lap / Drag
2. Layout: list of the current venue's layouts + `Auto`
3. New track (§10.9)
4. Calibrate (orientation capture; shows result)
5. Units: km/h / mph
6. Export (BLE) — enters CONNECTED, shows name + countdown
7. Live to phone (only with `CFG_HAS_BLE_RC`)
8. Diagnostics (§17.10)
9. Sessions: list, per-session delete, delete all logs (keep summaries)
10. Display: live clock on/off, full refresh now
11. Sleep now (long-hold MODE 3 s to confirm)

### 20.8 Buttons (`app/ui/ui_buttons.c`)

- GPIO ISR on any edge for 32/33/25 (IRAM), pushes `{mask, mono_us}` to `btn_q`.
- `ui` debounces in software: level sampled 30 ms after the edge; a press is registered when stable high; release when stable low. Short press < 500 ms, long press ≥ 1000 ms (fires once while held), very long ≥ 3000 ms (Sleep now confirm).
- In PIT/PARK, the same pins are wake sources; the press that woke the device is consumed as "wake" and not as a UI action.
- Combined UP+DOWN held 2 s → full refresh now (ghost clearing) from any screen.

### 20.9 Car variant (`display_oled_ssd1309`, O6)
SPI, 128×64 mono, 10 Hz timer-driven render when the model is dirty or `live_clock`; same primitives; layouts in `screens_car.c`: running lap clock, live predictive delta bar (±2 s scale), lateral/longitudinal g dot (friction circle), speed. Pages and menu identical in structure.

---

## 21. Build system

### 21.1 Top-level `CMakeLists.txt` (behaviour)

```cmake
cmake_minimum_required(VERSION 3.16)
set(VARIANT "" CACHE STRING "moto|car") ; set(GPS "" CACHE STRING "neo6m|m10") ; ... (all §4.6 variables)
# validate (fatal_error on bad combos)
# map choices to driver dirs
set(EXTRA_COMPONENT_DIRS
    components/core components/hal components/app
    components/drivers/gps_ubx_common components/drivers/gps_${GPS}
    components/drivers/imu_${IMU} components/drivers/display_${DISPLAY}
    components/drivers/storage_${STORAGE} components/drivers/board_devkit_v1)
if(CONN MATCHES "ble") list(APPEND EXTRA_COMPONENT_DIRS components/drivers/conn_ble) endif()
if(CONN MATCHES "wifi") list(APPEND EXTRA_COMPONENT_DIRS components/drivers/conn_wifi) endif()
if(CONN_BLE_RC) list(APPEND EXTRA_COMPONENT_DIRS components/drivers/conn_ble_rc) endif()
if(EXPORT_SERIAL) list(APPEND EXTRA_COMPONENT_DIRS components/drivers/export_serial) endif()
# git describe --tags --match 'v*' → CFG_FW_VERSION ; configure_file(main/build_config.h.in ${CMAKE_BINARY_DIR}/build_config.h)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.${VARIANT}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(laptimer)
```

Driver components register with the same interface name (e.g. `gps_neo6m` provides `hal_gps_impl`), so `app` links against `hal_gps_impl` without knowing which one.

### 21.2 `sdkconfig.defaults` (shared)

```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_COMPILER_CXX_EXCEPTIONS=n
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_INT_WDT=y
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y
CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME=y
CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y
CONFIG_SECURE_BOOT_VERIFICATION_KEY="keys/laptimer_pub.pem"
CONFIG_UART_ISR_IN_IRAM=y
CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y
CONFIG_SPI_MASTER_ISR_IN_IRAM=y
CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y
CONFIG_BT_CTRL_PINNED_TO_CORE_0=y
CONFIG_ESP_WIFI_ENABLED=n          # overridden in *_wifi envs
CONFIG_LITTLEFS_MAX_PARTITIONS=1
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
```

The three `CONFIG_SECURE_*` lines and `CONFIG_SECURE_BOOT_VERIFICATION_KEY` are added in roadmap session 5.5 together with the generated public key `keys/laptimer_pub.pem` (committed with `git add -f`; `.gitignore` carries a `!keys/laptimer_pub.pem` exception). Until then `sdkconfig.defaults` omits them so dev builds are unsigned.

`sdkconfig.defaults.moto` / `.car` currently only differ in `CONFIG_LAPTIMER_VARIANT_*` Kconfig symbols used by `app/ui` (kept for menuconfig visibility; the authoritative switch is `build_config.h`).

### 21.3 `build.sh`

```
./build.sh <env> [build|flash|monitor|flash-monitor|clean|size|menuconfig] [--port /dev/ttyUSB0] [--flash-size 4MB|8MB|16MB]
```
Maps `<env>` to flags (§4.6), runs `idf.py -B build/<env> -D<flags> <cmd>`, `size` runs `idf.py size` and fails if the app exceeds 0x130000 bytes. `flash` prints the reminder "Disconnect the battery pack before USB" and requires `--yes` to proceed.

### 21.4 Host build (`test/CMakeLists.txt`)

Plain CMake ≥ 3.16, C11, `-Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined` (Debug). Adds `components/core/**/*.c` with `include/`, vendored Unity, one executable per `test_*.c`, `add_test` for each; `tools/replay` built as `replay`. Works on macOS (Apple clang) and Linux (gcc). No IDF, no network.

### 21.5 Versioning
`git describe --tags --match 'v*' --dirty --always` → `CFG_FW_VERSION` (session tags `pNN-dD` and `plan-NN-done` are excluded by the `--match` filter). Tags `vMAJOR.MINOR.PATCH`. Release builds require a clean tree (`sign_release.sh` refuses `-dirty`).

### 21.6 CI (GitHub Actions)
- `host-tests`: ubuntu, cmake + ctest with sanitizers.
- `firmware`: matrix over the environments that exist at the time (initially `moto_neo6m` and `moto_sim`; adding a matrix entry requires updating the branch-protection required checks in the same change) in container `espressif/idf:<pinned>`; runs `build.sh <env> build` and `size`; uploads `.bin` artifacts (unsigned).
- `web`: HTML validity check and a size cap for `export.html`.

---

## 22. Testing and acceptance

### 22.1 Unit tests (host, Unity, written test-first)

| File | Cases (minimum) |
|------|-----------------|
| `test_geo.c` | ENU round-trip at −34° lat; segment crossing true/false/parallel/touching-endpoint; direction sign both ways; `geo_interp_time` constant speed = linear; decelerating case matches closed form; `dist_point_segment` on/off segment |
| `test_tb.c` | min-filter converges within 10 fixes; jitter 30–120 ms rejected to ≤ 10 ms; window rollover; PPS lock wins; PPS disagreement fallback; `days_from_civil` known dates incl. leap years; negative `nano` |
| `test_fus.c` | rotation with identity and 90° mounts; gyro bias applied; still detection; orientation capture from tilted gravity; forward learning from synthetic straight-line acceleration; steady-turn synthetic (v=30 m/s, ψ̇=−0.3 rad/s) converges to lean ≈ atan(0.917) = 42.5° ± 1°; lateral g = 0.917 ± 0.02; lean invalid before forward learned; disagreement flag |
| `test_lap.c` | venue detect at 1.9 km yes / 2.1 km no; S/F crossing forward accepted, reverse rejected, and vice versa on the reverse layout; out-lap has no time; 10 synthetic laps produce 10 results with sector sums = lap time; debounce prevents double fire on jitter; full vs short disambiguation on Killarney fixture; pit flag; incomplete flag when a sector is skipped; sector resync; `min_lap` rejection; `max_lap` invalid; theoretical best; RTC restore mid-lap yields `INTERRUPTED` with correct time; `lap_mark_gate` builds a 30 m perpendicular line and derived reverse |
| `test_drag.c` | synthetic constant 0.5 g run: 0-100 at 5.66 s ± 20 ms, 1/4 at 12.81 s ± 20 ms, trap ≈ 226 km/h ± 2; rollout shifts t0 as expected; bench visibility for peak 180 vs 320 km/h; false-start abort; GPS re-anchor correctness with lagging fixes; braking distance from 100 km/h at −1 g = 39.3 m ± 0.5 |
| `test_ses.c` | frame encode/decode all types; CRC mismatch skipped and resync finds the next frame; delta encoder chooses KEY on overflow and every 5 s; reader reconstructs absolute fixes exactly; truncated file decodes all complete frames |
| `test_exp.c` | VBO golden file byte-equal for a 3-fix fixture (checks west-positive longitude, minute conversion, formatting); NMEA checksums verified by independent computation; JSON parses (using a tiny reference parser in the test) |
| `test_trk.c` | nearest lookup; user override on id clash; JSON parse/emit round-trip; `"sf":"same"` / `"sectors":"reverse"` expansion |
| `test_cfg.c` | defaults valid; each field clamped; JSON merge semantics; unknown keys ignored; migration v1→v1 no-op |
| `test_ui.c` | every screen renders from a fixed model without out-of-bounds writes; PBM snapshots match goldens; bench rows rule (§11.4); fault icon strip only when flags set |
| `test_ring.c` | SPSC ring overwrite-oldest vs drop-newest policies, wraparound |

Coverage target: ≥ 90 % lines in `core/` (gcov in CI).

### 22.2 Replay and synthetic generator (host)

- `replay <session.log|capture.ubx> [--imu capture.csv] [--venue id] [--mode lap|drag] [--json]` runs the pipeline core (same call sequence as `app/pipeline` minus IDF) and prints laps/sectors/runs. `--json` emits a machine-readable result compared against `test/data/<name>.expected.json` by `ctest`.
- `tools/replay/synth.c` generates a polygonal circuit (default 12 vertices, 2.5 km) driven at a speed profile with configurable GPS rate (5/10 Hz), position noise (σ = 1.5 m, correlated with τ = 20 s), speed noise (σ = 0.05 m/s), and latency jitter; ground-truth crossing times are analytic. Acceptance: at 5 Hz, |error| ≤ 30 ms for 95 % of crossings; at 10 Hz ≤ 15 ms.
- Fixtures in `test/data/`: `killarney_full.log`, `killarney_short.log`, `killarney_full_rev.log`, `zwartkops.log`, `drag_0_180.log`, `drag_0_320.log`, `gps_dropout.log`, `truncated.log`. Initially synthetic; replaced by real device captures as they are recorded (first real captures are the Phase 1 exit criterion).

### 22.3 Bench tests (target)

| Test | Procedure | Pass |
|------|-----------|------|
| GPS simulation | `tools/gps_sim.py --port /dev/ttyUSB1 --file capture.ubx --rate 5` into GPIO 16 through a USB-UART adapter | laps appear on the display matching replay |
| Power per state | multimeter in series with battery; `dbg power active|pit|park` | ≤ 95 / 18 / 0.3 mA |
| Sleep/wake | PARK, tap the IMU | ACTIVE within 3 s of tap, GPS fix within 5 s (warm) |
| WDT path | `dbg hang` | reset within 5 s, session resumed, `E_SYS_WDT_RESET` logged |
| Crash path | `dbg crash` ×3 within 60 s | safe mode entered; clears after 10 min |
| I2C recovery | short SDA to GND for 2 s during ACTIVE | `E_IMU_BUS_RECOVERY` then IMU samples resume within 5 s |
| GPS power cycle | unplug GPS TX for 40 s | ladder reaches power-cycle; fixes resume within 10 s of reconnection |
| Brownout | bench supply ramp 3.3 → 2.6 V | clean reset, no storage corruption (`.sum` intact) |
| Storage truncation | power off during logging (10 trials) | mount OK, summaries intact, replay resyncs |
| BLE export | web page on Android | 200 KB session ≤ 15 s, CRC OK, RaceChrono imports |
| OTA | good image; corrupted image; wrong hwid; image with `dbg crash` in `app_main` | applied / `E_OTA_SIG` / `E_OTA_HWID` / rolled back |
| E-paper | 50 partials, temp 5 °C and 40 °C | legible, full refresh clears ghosting |

### 22.4 Resource measurement
After Phase 1: `uxTaskGetStackHighWaterMark` per task, pipeline worst-case iteration time (GPIO toggle + scope, or `esp_timer` delta logged), heap min-free after 1 h with BLE cycling. Values recorded in `docs/measurements.md` and stack sizes updated to high-water + 25 %.

### 22.5 Field validation
Three track sessions with RaceChrono on a phone as reference (phone GPS 1 Hz is worse than ours; use a RaceBox or a second lap timer if available; else compare lap counts and relative deltas). Acceptance: every lap detected, no false laps, lap times agree with the reference within ±0.1 s (5 Hz) on ≥ 95 % of laps.

### 22.6 Manual UI test script
1. Boot → boot screen lists self-test OK. 2. Stationary → PIT within 30 s (icon), tap → ACTIVE. 3. MODE long → menu; UP/DOWN cycle; long MODE exits. 4. Menu locked when `gps_sim` reports 50 km/h. 5. Mode switch Lap→Drag resets screen. 6. Export → BLE name shown, page connects. 7. Diagnostics shows counters. 8. Sleep now → PARK; button wakes.

---

## 23. Tools

| Tool | Language | Purpose | Interface |
|------|----------|---------|-----------|
| `tools/web/export.html` | JS | phone client (§18.3) | Web Bluetooth / HTTP |
| `tools/replay/` | C | offline pipeline (§22.2) | CLI |
| `tools/tracks/gen_tracks.py` | Python 3 | `*.json` → `trk_bundled.c`; validates schema, lengths, line lengths (10–60 m) | `python gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c` |
| `tools/fonts/gen_fonts.py` | Python 3 (Pillow) | TTF → 1-bpp glyph arrays | `python gen_fonts.py DejaVuSansMono-Bold.ttf -o components/app/ui/ui_fonts.c` |
| `tools/gps_sim.py` | Python 3 (pyserial) | replay `.ubx` or `.log` fixes as UBX NAV-PVT at real-time rate to a serial port | `--port --file --rate --loop` |
| `tools/serial_export.py` | Python 3 (pyserial) | serial protocol client (§18.4) | `serial_export.py /dev/ttyUSB0 list` |
| `tools/sign_release.sh` | bash | build + sign + verify + dist (§19.2) | `sign_release.sh moto_neo6m` |
| `tools/log2csv.py` | Python 3 | decode `.log` to CSV for analysis (same frame spec) | |

Python tools pin dependencies in `tools/requirements.txt` (`pyserial`, `Pillow`).

---

## 24. Delivery phases

| Phase | Scope | Env | Exit criteria |
|-------|-------|-----|---------------|
| 0 | Repo skeleton, `build.sh`, host test harness, `core/util/ring`, `core/geo`, `core/tb`, `core/ses`, `core/cfg`, CI | host | all unit tests green; firmware skeleton boots and prints version |
| 1a | Drivers: `board_devkit_v1`, `gps_ubx_common` + `gps_neo6m`, `imu_mpu6050`, `display_epaper_ssd1680`, `storage_internal`; pipeline with fusion; logger; supervisor; power states | `moto_neo6m` | bench tests power/sleep/WDT/I2C/GPS-cycle pass; fixes and fused samples logged |
| 1b | `core/lap`, `core/drag`, `core/trk` + bundled SA tracks, UI screens/menu, on-device track creation, RTC continuity | `moto_neo6m` | replay fixtures pass; first real track session recorded and replayed |
| 1c | `core/exp`, `app/cmd`, `conn_ble`, `export_serial`, web page, OTA | `moto_neo6m` | BLE export into RaceChrono; OTA good/bad/rollback bench tests pass |
| 2 | `conn_wifi`, HTTP routes, track upload, OTA over WiFi | `moto_neo6m_wifi` | web page works over HTTP; upload creates a venue |
| 3 | `conn_ble_rc` | `+CONN_BLE_RC` | RaceChrono shows live GPS + lean channel |
| 4 | `gps_m10`, PPS, QMC5883L | `moto_m10` | accuracy ≤ 15 ms on synthetic 10 Hz; PPS lock quality 2 |
| 5 | `storage_sd` | `*_sd` | degrade/recover bench test |
| 6 | `display_oled_ssd1309`, `screens_car.c`, predictive delta | `car_*` | live delta within ±50 ms of replayed truth |

Phase 1 (a–c) is the subject of the first implementation plan.

---

## 25. Items to verify on hardware before or during Phase 1

1. ~~Flash size~~ — verified 4 MB on 2026-09-15 (ESP32-D0WD-V3 rev 3.1); partition table §19.1 stands.
2. NEO-6M firmware via `MON-VER` (NAV-PVT ≥ 7.03 else fallback set).
3. TP4056 module protection (6 pads).
4. Dev board accepts 3.3 V injection on `3V3` without back-feed issues.
5. MPU6050 self-test passes (genuine) and `MOT_THR` scaling.
6. Exact e-paper panel/controller and its update-control constants.
7. NEO-6M `RXM-PMREQ` wake-on-UART behaviour (else MOSFET only).
8. Brownout level menuconfig mapping to ≈ 2.8 V.
9. M10-25Q: TIMEPULSE pad, LDO dropout, backup capacitor.
10. Measured currents per state versus §16.5.

---

## Appendix A. Constants

| Name | Value | Used in |
|------|-------|---------|
| `FUSION_HZ` | 100 | §9 |
| `FUSED_RING_HZ` | 25 | §9.1 |
| `FUSED_LOG_HZ_INTERNAL` / `_SD` | 10 / 25 | §12.6 |
| `IMU_POLL_MS` | 50 | §8.4 |
| `IMU_MOT_THR_LSB` / `IMU_MOT_DUR_MS` | 20 / 40 | §8.6 |
| `LEAN_ALPHA` | 0.98 | §9.3 |
| `LEAN_MAX_DEG` | 70 | §9.3 |
| `G_MAX` | 3.0 | clamp in §9.3 |
| `LEAN_DISAGREE_DPS` / `LEAN_DISAGREE_S` | 10 / 5 | §9.3 |
| `LEAN_REF_MIN_SPEED_MPS` | 3 | §9.3 |
| `STILL_ACC_VAR` | (0.02 g)² | §9.2 |
| `STILL_GYRO_VAR` | (2 dps)² | §9.2 |
| `STILL_WINDOW_S` | 2 | §9.2 |
| `BIAS_TEMP_STALE_C` | 15 | §9.2 |
| `FWD_LEARN_ACC_MPS2` / `FWD_LEARN_MIN_S` / `FWD_LEARN_WINDOWS` | 1.5 / 1 / 3 | §9.2 |
| `TB_WINDOW_S` | 30 | §6.2 |
| `TB_LOCK_FIXES` | 10 | §6.2 |
| `TB_PPS_DISAGREE_US` | 50000 | §6.2 |
| `TB_PPS_STALE_US` | 5000000 | §6.2 |
| `EARTH_R_M` | 6371008.8 | §6.3 |
| `FIX_HACC_MAX_M` | 15 | §6.5 |
| `FIX_MIN_SATS` | 5 | §6.5 |
| `FIX_MAX_SPEED_MPS` | 139 | §6.5 |
| `FIX_MAX_JUMP_MPS` | 250 | §6.5 |
| `FIX_LOST_COUNT` | 3 | §6.5 |
| `GATE_REARM_DIST_M` | 50 | §6.4 |
| `GATE_REARM_MIN_S` | 2 | §6.4 |
| `GATE_HALF_WIDTH_M` | 15 | §6.4, §10.9 |
| `MIN_LAP_S` / `MAX_LAP_S` | 20 / 1800 | §10.4 |
| `VENUE_RADIUS_DEFAULT_M` | 2000 | §10.1 |
| `VENUE_LEAVE_FACTOR` / `VENUE_LEAVE_S` | 1.5 / 60 | §10.3 |
| `VENUE_SCAN_S` | 5 | §10.3 |
| `LAYOUT_LEN_TOL` | 0.15 | §10.5 |
| `PIT_SPEED_KMH` / `PIT_TIME_S` | 5 / 10 | §10.6 |
| `PRED_TABLE_MAX` | 600 | §10.11 |
| `DRAG_MAX_GATES` | 16 | §11 |
| `DRAG_ARM_SPEED_KMH` / `DRAG_ARM_STILL_S` | 0.5 / 2 | §11.2 |
| `DRAG_LAUNCH_G` / `DRAG_LAUNCH_HOLD_MS` / `DRAG_LAUNCH_SCAN_G` | 0.15 / 100 / 0.05 | §11.2 |
| `DRAG_ROLLOUT_M` | 0.3048 | §11.2 |
| `DRAG_TIMEOUT_S` | 60 | §11.2 |
| `DRAG_FALSE_START_S` | 2 | §11.2 |
| `TRAP_DIST_M` | 20.117 | §6.6 |
| `FIX_KEYFRAME_S` | 5 | §12.4 |
| `LOG_BATCH_BYTES` / `LOG_FLUSH_BYTES` | 4096 / 3584 | §13.3 |
| `LOG_WRITE_S` / `LOG_FSYNC_S` | 1 / 2 | §13.3 |
| `LOG_PAUSE_STILL_S` | 10 | §12.6 |
| `STO_EVICT_FREE_PCT` / `STO_STOP_FREE_PCT` | 10 / 5 | §12.7 |
| `MOVING_SPEED_KMH` | 3 | §9.1 |
| `MENU_LOCK_SPEED_KMH` | 10 | §20.7 |
| `IDLE_TO_PIT_S` / `IDLE_TO_PARK_S` | 30 / 600 | §16.1 |
| `RTC_RESUME_MAX_S` | 14400 | §15.3 |
| `BATT_SHUTDOWN_MV` / `BATT_SHUTDOWN_HOLD_S` / `BATT_RESTART_MIN_MV` | 3300 / 30 / 3400 | §16.1 |
| `BATT_WARN_PCT` / `BATT_OTA_MIN_PCT` | 20 / 50 | §16.4, §19.5 |
| `BATT_EMA_ALPHA` | 0.2 | §16.4 |
| `BATT_LOAD_COMP_MV` | 60 | §16.4 |
| `BLE_ADV_S` / `CONN_IDLE_S` | 60 / 300 | §16.1 |
| `BLE_CHUNK_MAX` | 496 | §18.1 |
| `BLE_STALL_S` | 10 | §18.2 |
| `WDT_S` | 5 | §17.1 |
| `HB_STALL_S` (pipeline/logger/ui/conn/power) | 1 / 5 / 10 / 10 / 5 | §17.2 |
| `HEAP_LOW_KB` / `HEAP_BLE_OFF_KB` / `HEAP_LOG_OFF_KB` | 40 / 20 / 12 | §17.2 |
| `STACK_LOW_B` | 256 | §17.2 |
| `GPS_LADDER_S` | 3 / 10 / 30 / 60 | §7.5 |
| `GPS_CFG_TIMEOUT_MS` | 500 | §7.3 |
| `IMU_RETRY` / `IMU_RECOVER_MAX_PER_60S` | 3 / 3 | §8.7 |
| `IMU_FROZEN_SAMPLES` | 100 | §8.4 |
| `DISP_BUSY_TIMEOUT_S` / `DISP_FAIL_MAX` / `DISP_RETRY_S` | 5 / 3 / 300 | §20.3 |
| `EPAPER_FULL_EVERY_N` / `EPAPER_FULL_EVERY_S` | 10 / 1800 | §20.3 |
| `EPAPER_TEMP_MIN_C` / `EPAPER_TEMP_MAX_C` | 0 / 45 | §17.2 |
| `EPAPER_THROTTLE_S` | 30 | §20.3 |
| `BTN_DEBOUNCE_MS` / `BTN_SHORT_MS` / `BTN_LONG_MS` / `BTN_VLONG_MS` | 30 / 500 / 1000 / 3000 | §20.8 |
| `MENU_IDLE_S` | 30 | §20.7 |
| `CRASH_LOOP_N` / `CRASH_LOOP_WINDOW_S` / `SAFE_MODE_CLEAR_S` | 3 / 60 / 600 | §17.5 |
| `OTA_VALID_UPTIME_S` | 60 | §19.4 |
| `OTA_MAX_IMAGE` | 0x140000 − 4096 | §19.5 |
| `RTC_MAGIC` | 0x4C505452 | §15.3 |
| `SES_SYNC` | 0xA5 | §12.2 |
| `SES_MAX_PAYLOAD` | 247 | §12.2 |
| `TRK_MAX_LAYOUTS` / `LAP_MAX_SECTORS` / `TRK_MAX_USER` | 8 / 8 / 16 | §10.1 |

## Appendix B. Glossary

| Term | Meaning |
|------|---------|
| Doppler speed | Ground speed computed by the receiver from carrier frequency shift; far less noisy than differentiating positions. |
| ENU | East-North-Up local tangent plane in metres around a venue origin. |
| Gate | A line segment on the track (S/F or sector) whose crossing produces a timing event. |
| Layout | A specific configuration of a venue (full, short, reverse) with its own S/F direction and sector gates. |
| Venue | A physical circuit location with one or more layouts. |
| Out-lap | The partial lap from the first S/F crossing of a session; timed from the next crossing. |
| Trap speed | Mean speed over the last 66 ft before the 1/4-mile line (NHRA). |
| Rollout | Drag-strip convention: the clock starts after the vehicle moves 1 ft out of the staging beams. |
| PPS / TIMEPULSE | GPS receiver output pulse aligned to the top of each second. |
| UBX | u-blox binary protocol. `CFG-VALSET` is the M9/M10 configuration interface; `CFG-*` legacy messages apply to u-blox 6–8. |
| PSM / cyclic tracking | u-blox power-save mode where the receiver duty-cycles while keeping tracking. |
| DLPF | Digital low-pass filter inside the MPU6050. |
| Ladder | An ordered list of escalating recovery actions for one subsystem. |
| Safe mode | Reduced-function mode after a crash loop: timing and summaries only. |
| Summary file | `.sum`: header + laps + runs, rewritten atomically, never evicted. |
| VBO | Racelogic VBOX text format, widely imported by lap-timing software. |
