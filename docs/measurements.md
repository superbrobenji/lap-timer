# Measurements

Bench results recorded per the spec (§22.4) and roadmap session exit criteria. Newest first.

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
