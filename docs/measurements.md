# Measurements

Bench results recorded per the spec (§22.4) and roadmap session exit criteria. Newest first.

## moto_neo6m storage + logger power-cut test on ESP32 (plan 03, session 3.3)

- Date: 2026-09-17. Branch s3.3-storage-logger (d73396e). ESP-IDF v5.3.2. Board ESP32 DevKit V1. App image 373,872 bytes (71 % free). LittleFS via joltwallet/littlefs 1.16.5.
- **Mount ladder (§13.1):** a genuinely corrupted filesystem (`Corrupted dir pair at {0x0,0x1}`, from an aborted write) was recovered by the ladder — mount fail → retry fail → `esp_littlefs_format` → mount OK (1320/1344 KB free).
- **Power-cut exit test:** `dbg logtest 6000` (opens session, writes+syncs the SESSION_HDR, streams FIX into the ring, 2 LAP → `.sum` rebuild), then a hard reset mid-write, reboot:
  - LittleFS remounted cleanly (no corruption, no format).
  - `dbg sum S00004_001`: HDR ok, VENUE ok, **LAP count = 2**, END absent (session still open — correct), frames ok=4 bad=0 → the atomic `.sum` rewrite is intact.
  - `dbg logck S00004_001`: `.log` ok=1 bad=0 (the fsynced HDR survived; the unsynced batch tail was cleanly lost) → decodable/resyncable.
- Exit criterion met: a power-cut during logging leaves `.sum` intact and `.log` decodable.

## moto_neo6m firmware on ESP32 (plan 03, session 3.2 — board, NVS, boot, supervisor)

- Date: 2026-09-17. Branch s3.2-board-boot (5254894). ESP-IDF v5.3.2. Board ESP32 DevKit V1, /dev/cu.usbserial-0001, console 115200. App image 327,632 bytes (74 % of the 1.25 MB app partition free).
- Boots to `app_main` and completes boot in ~721 ms (`boot #1 complete in 721 ms (safe_mode=0)`), well under the ≤1.5 s target. No reset loop, no stack overflow.
- `dbg status` (exit criterion) over two boots — the NVS boot counter persists and increments, config persists (E_SYS_CFG_RESET only on the first blank-NVS boot), heartbeats tick, no spurious faults:
```
boot count : 2
reset      : power-on (1)
counters   : boots=2 crashes=0 wdt=0 brownout=0
sys_flags  : 0x00000000
hb[1]      : 5     (supervisor, ~1/s; other task slots 0 until their tasks exist)
```

## moto_neo6m firmware banner on ESP32 (plan 03, session 3.1)

- Date: 2026-09-16. Branch s3.1-fw-scaffold. ESP-IDF v5.3.2. Board ESP32 DevKit V1, /dev/cu.usbserial-0001. `./build.sh moto_neo6m flash`.
- App image 218,608 bytes (0x355f0), 83 % of the 1.25 MB app partition free. Boots to `app_main` in ~0.4 s (`I (391) main_task: Calling app_main()`), no reset loop.
- Console: `CONFIG_ESP_CONSOLE_UART_BAUDRATE` lowered 921600 → 115200 (spec §21.2 reconciled); the board's CP2102 adapter is unreliable at 921600 here, and 115200 matches the proven core_selftest tooling. Fast-baud coredump dumps are a minor loss for the prototype.
- Banner (verbatim):
```
I (392) laptimer: LapTimer 41c7f21-dirty (moto_neo6m_epaper)
I (398) laptimer: core 0.0.1 | GPS neo6m | display epaper_ssd1680 | fused-log 10 Hz
I (407) laptimer: reset reason: power-on (1)
```

## core_selftest on ESP32 (plan 02, session 2.6)

- Date: 2026-09-16. Commit: 832e7ab (branch s2.6-drag). ESP-IDF v5.3.2. Same board and port. 19 suites (`drag` added; its analytic fused/GPS streams need no synth). App binary 396,736 bytes.
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3532 B free), free heap 128192, min free 114992 ===`. `drag` 1159 ms on target (the 0.5 g run, gates, trap, braking, false-start, benches). All others under 0.4 s except the ring stress (10,049 ms).

## core_selftest on ESP32 (plan 02, session 2.5)

- Date: 2026-09-16. Commit: e55d1eb (branch s2.5-lap2). ESP-IDF v5.3.2. Same board and port. 18 suites (`lap_gate` added; `lap` now covers sectors, disambiguation, on-device creation, RTC, predictive). App binary 382,992 bytes.
- Two on-hardware crashes were found and fixed before this PASS: (1) `lap_t` had grown to 12.5 KB (embedded predictive table) and overflowed the 24 KB main task stack — fixed by making the predictive table caller-provided (issue #23), `lap_t` → 5,360 B; (2) the predictive test's four `PRED_TABLE_MAX` buffers were file-scope `static` (+7.2 KB .bss) and broke boot-time idle-task allocation — fixed by mallocing them in the tests (.bss back to 123,918).
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3532 B free), free heap 132852, min free 115744 ===`. `lap` 271 ms, `lap_gate` 17 ms on target.

## core_selftest on ESP32 (plan 02, session 2.4)

- Date: 2026-09-16. Commit: aa975bf (branch s2.4-lap). ESP-IDF v5.3.2. Same board and port. 17 suites now (`lap` added; its synth-driven exit test is host-only and excluded on target). App binary 363,392 bytes (65 % of the factory partition free).
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3532 B free), free heap 132852, min free 119732 ===`. `lap` 75 ms on target (venue detect, S/F crossing, completion, pit — the 10-lap synth replay runs on host only).

## core_selftest on ESP32 (plan 02, session 2.3)

- Date: 2026-09-16. Commit: 6cd4693 (branch s2.3-fusion). ESP-IDF v5.3.2. Same board and port. Same 16 suites as session 2.2; the `fus` suite grew from 13 to 21 cases (the §9.3 lean filter, yaw, lateral g and GPS cross-check). App binary 0x56170 bytes (66 % of the factory partition free).
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3532 B free), free heap 132852, min free 119732 ===`. `fus` 278 ms, `ses_records` 1091 ms, `ring` 10,049 ms (unchanged two-thread stress); all others under 0.35 s.

## core_selftest on ESP32 (plan 02, session 2.2)

- Date: 2026-09-16. Commit: c913034 (branch s2.2-fusion). ESP-IDF v5.3.2. Same board and port as earlier runs.
- Build: app binary 0x53660 bytes (67 % of the 1 MB factory partition free). Now 16 suites (the four fusion suites `fus`, `fus_still`, `fus_orient`, `fus_fwd` added).
- Main task stack reduced 40 KB -> 24 KB: the added fusion suites' static test `.bss` shrank the largest contiguous internal-DRAM region below 40 KB, so the 40 KB main task could not be created (`xTaskCreate` assert, boot loop). The 6 KB probe task shows the suites need ~2.6 KB, so 24 KB keeps wide margin and fits.
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3532 B free), free heap 132852, min free 119732 ===` (heap before the run 134,576 bytes).

| Suite | Result | Time on target |
|-------|--------|----------------|
| smoke | OK | 9 ms |
| bw | OK | 19 ms |
| ring | OK | 10,049 ms (two-thread stress, core 1) |
| geo | OK | 81 ms |
| tb | OK | 48 ms |
| ses_frame | OK | 238 ms |
| ses_records | OK | 872 ms |
| jw | OK | 150 ms |
| cfg | OK | 386 ms |
| trk | OK | 102 ms |
| exp_vbo | OK | 12 ms |
| exp_nmea_json | OK | 40 ms |
| fus | OK | 157 ms |
| fus_still | OK | 96 ms |
| fus_orient | OK | 74 ms |
| fus_fwd | OK | 128 ms |

## core_selftest on ESP32 (plan 01, session 1.6 fix wave)

- Date: 2026-09-15. Commit: 8a9fcaf (branch s1.6-plan01-fixes). ESP-IDF v5.3.2. Same board and port as the session 1.5 run below.
- Build: `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build`; app binary 315,056 bytes (70 % of the 1 MB factory partition free).
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, stack6k OK (3524 B free), free heap 135812, min free 122956 ===` (heap before the run 137,536 bytes).
- Stack proof: after the 12 suites, `cfg` and `trk` run again inside a FreeRTOS task with a 6,144-byte stack (spec §4.8 pipeline-task budget). High-water mark 2,620 bytes used, 3,524 bytes never touched.

| Suite | Cases | Result | Time on target |
|-------|-------|--------|----------------|
| smoke | 2 | OK | 19 ms |
| bw | 2 | OK | 11 ms |
| ring | 5 | OK | 10,049 ms (two-thread stress, both threads on core 1, 20 k items each) |
| geo | 12 | OK | 81 ms |
| tb | 7 | OK | 48 ms |
| ses_frame | 9 | OK | 217 ms |
| ses_records | 13 | OK | 753 ms (includes the 10,000-fix random-walk round trip) |
| jw | 8 | OK | 141 ms |
| cfg | 14 | OK | 261 ms |
| trk | 10 | OK | 82 ms |
| exp_vbo | 2 | OK | 12 ms |
| exp_nmea_json | 5 | OK | 40 ms |
| stack6k (cfg + trk again, 6 KB task) | 24 | OK | — |

Free heap at start is ~104 KB lower than in session 1.5. `idf.py size-components` attributes it to
the test suites themselves (`libmain.a` .bss 104,576 bytes: the 16 KB user-track blobs and other
static test buffers added by the fix wave), not to the core (`libcore.a` .bss 25,093 bytes, which
includes the 4-slot user-track store). Test-only cost; nothing to act on.

## core_selftest on ESP32 (plan 01, session 1.5)

- Date: 2026-09-15. Commit: 2d9f8a2 (branch s1.5-target-bom). ESP-IDF v5.3.2. Board: ESP32 DevKit V1, ESP32-D0WD-V3 rev 3.1, 4 MB flash, port /dev/cu.usbserial-0001.
- Build: `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build`; app binary 293,360 bytes (72 % of the 1 MB factory partition free); ring stress iterations reduced to 20,000 on target (`RING_STRESS_N`).
- Result: `=== core_selftest RESULT: PASS, 0 failing suites, free heap 240124, min free 228636 ===` (heap before the run 241,848 bytes).

| Suite | Cases | Result | Time on target |
|-------|-------|--------|----------------|
| smoke | 1 | OK | 10 ms |
| bw | 2 | OK | 11 ms |
| ring | 5 | OK | 10,049 ms (two-thread stress tests, both threads pinned to core 1, 20 k items each) |
| geo | 9 | OK | 57 ms |
| tb | 7 | OK | 48 ms |
| ses_frame | 8 | OK | 60 ms |
| ses_records | 7 | OK | 50 ms |
| jw | 5 | OK | 32 ms |
| cfg | 9 | OK | 57 ms |
| trk | 6 | OK | 39 ms |
| exp_vbo | 2 | OK | 12 ms |
| exp_nmea_json | 3 | OK | 21 ms |

The suite times are dominated by one test: the two-thread ring stress runs 10,049 ms of the
10,446 ms total, ~96 % of the whole on-target run. Every other suite together is under 0.4 s, so
this table says almost nothing about the cost of the rest of the core — read it as "the ring stress
still passes on hardware", not as a profile.

Notes learned on the way (all fixed before this result):
1. ESP-IDF builds its own `unity` component by default; it linked ahead of the vendored Unity and its ABI differs (no 64-bit support), which corrupted assert arguments and crashed. `EXCLUDE_COMPONENTS "unity" "cmock"` in the self-test project fixes it.
2. Unity auto-enables 64-bit asserts only on 64-bit hosts; `UNITY_SUPPORT_64` must be defined on the target (now in the shared Unity config).
3. A test thread that spins must not share the main task's core: `app_main` is pinned to core 0, so the self-test pins its pthreads to core 1.
