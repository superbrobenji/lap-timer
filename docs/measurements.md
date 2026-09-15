# Measurements

Bench results recorded per the spec (§22.4) and roadmap session exit criteria. Newest first.

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
