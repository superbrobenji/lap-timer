# lap-timer

ESP32 GPS + IMU lap timer for track days and drag runs. Battery powered, fully offline, exports to RaceChrono over BLE.

- Design spec (source of truth): `docs/superpowers/specs/2026-09-14-lap-timer-design.md`
- Roadmap and session plans: `docs/superpowers/plans/`

## Layout

| Path | What |
|------|------|
| `components/core/` | pure C11 algorithm core, no ESP-IDF, host-tested |
| `components/hal/` | hardware interface headers |
| `components/drivers/` | one driver per HAL interface, selected at build time |
| `components/app/` | ESP-IDF tasks and glue |
| `test/` | host unit tests (CMake + Unity) |
| `tools/` | web client, replay, generators, scripts |

## Host tests

    cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure

## Firmware

Prerequisites: `brew install cmake ninja python@3.12`, ESP-IDF v5.3.2 at `~/esp/esp-idf-v5.3.2` (see `tools/idf-env.sh`; run `source tools/idf-env.sh` first).

    ./build.sh moto_neo6m build        # see build.sh for environments
    ./build.sh moto_neo6m flash monitor --port /dev/cu.usbserial-XXXX

Disconnect the battery pack before connecting USB (spec §3.2).

## Branching

`main` is always releasable. One branch per roadmap session, named `s<plan>.<day>-<topic>`, merged by squash PR once CI is green. Tags `pNN-dD` mark finished sessions, `plan-NN-done` finished plans, `vX.Y.Z` releases.

Branch protection is not enforced (private repository on a free plan); the PR-and-CI protocol above is followed by convention.

## Status

Session 0.1 complete.
