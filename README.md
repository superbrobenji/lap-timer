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

(The `test/` tree arrives with plan 01.)

    cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build && ctest --test-dir test/build --output-on-failure

CI compiles with gcc; run the same with `CC=$(ls /opt/homebrew/bin/gcc-* | head -1)` and `-B test/build-gcc` before opening a PR (`brew install gcc` once).

## Firmware

(`build.sh` arrives with plan 03; until then use the ESP-IDF hello-world flow in plan 00 task 7.)

Prerequisites: `brew install cmake ninja python@3.12`, ESP-IDF v5.3.2 at `~/esp/esp-idf-v5.3.2` (see `tools/idf-env.sh`; run `source tools/idf-env.sh` first).

    ./build.sh moto_neo6m build                    # see build.sh for environments
    ./build.sh moto_neo6m flash-monitor --yes --port /dev/cu.usbserial-0001

Disconnect the battery pack before connecting USB (spec §3.2).

## Branching

`main` is always releasable. One branch per roadmap session, named `s<plan>.<day>-<topic>`, merged by squash PR once CI is green. Tags `pNN-dD` mark finished sessions, `plan-NN-done` finished plans, `vX.Y.Z` releases.

`main` is protected: pull requests only, all five CI checks required and up to date, linear history, no force pushes, enforced for admins.

## License

Source-available under the [PolyForm Strict License 1.0.0](LICENSE): noncommercial use only, no distribution. Copyright (c) 2026 Bennie Swanepoel.
