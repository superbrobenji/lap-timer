# lap-timer

ESP32 GPS + IMU lap timer for track days and drag runs. Battery powered, fully offline. Sessions
are exported over the serial console today (VBO / NMEA / JSON / raw log); BLE export to RaceChrono
is designed (spec §18.2) but not built — it is sub-project C.

- Design spec (source of truth): `docs/superpowers/specs/2026-09-14-lap-timer-design.md`
- Roadmap and session plans: `docs/superpowers/plans/`
- Bench results and resource measurements: `docs/measurements.md`

## Layout

| Path | What |
|------|------|
| `components/core/` | pure C11 algorithm core (timebase, geodesy, fusion, lap/drag engines, tracks, session codec, exporters, config, renderer), no ESP-IDF, host-tested |
| `components/lt_hal/` | hardware interface headers (`hal/gps.h`, `imu.h`, `display.h`, `storage.h`, `board.h`, `conn.h`); named `lt_hal` because ESP-IDF ships a built-in component called `hal` |
| `components/drivers/` | one driver per HAL interface, selected at build time (`gps_neo6m`, `gps_sim`, `imu_mpu6050`, `imu_sim`, `sim_common`, `display_epaper_ssd1680`, `storage_internal`, `export_serial`, `board_devkit_v1`) |
| `components/app/` | ESP-IDF tasks and glue (`pipeline`, `logger`, `ui`, `supervisor`, `cmd`, `ota`, `link`, `sys`) |
| `devcontroller/` | the removable dev-kit firmware (sub-project B): WiFi AP, web UI, OTA push, session offload |
| `test/` | host unit tests (CMake + Unity) and the byte-exact PBM screen goldens |
| `test_apps/core_selftest/` | the same host tests run on the ESP32 |
| `tools/` | linters, track/font generators, replay + synth, dev-kit and OTA host CLIs, release signing |

## Host tests

Unity lives in `test/unity` as a submodule, so after cloning:

    git submodule update --init --recursive

Then:

    cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build && ctest --test-dir test/build --output-on-failure

CI compiles with gcc; run the same with `CC=$(ls /opt/homebrew/bin/gcc-* | head -1)` and
`-B test/build-gcc` before opening a PR (`brew install gcc` once).

Before a PR, also run what CI gates:

    git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin' ':!components/core/include/core/jsmn.h'
    python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
    python3 tools/lint/venue_announce.py --fail-on-violation
    python3 tools/spec_api_check.py
    python3 -m unittest tools/test_devkit.py tools/lint/test_power_of_10.py tools/lint/test_venue_announce.py tools/test_spec_api_check.py tools/tracks/test_gen_tracks.py

## Firmware

Prerequisites: `brew install cmake ninja python@3.12`, ESP-IDF v5.3.2 at `~/esp/esp-idf-v5.3.2`
(see `tools/idf-env.sh`; run `source tools/idf-env.sh` first — never a raw 5.5.x toolchain).

    ./build.sh moto_sim build                      # bench build (sim GPS + IMU); see build.sh for all environments
    ./build.sh moto_neo6m build                    # production environment (real-sensor drivers are Plan 8 stubs today)
    ./build.sh moto_neo6m flash-monitor --yes --port /dev/cu.usbserial-0001

`PANEL=ws29v2 ./build.sh moto_sim build` builds the 2.9" panel variant; the default `ws213v4` is
the owned hardware. With the dev-kit wired, USB flashing fails (shared UART0 RX) — push over the
dev-kit instead (`tools/ota_push.py`, `tools/devkit.py`), and never open the lap-timer's port
within 35 s of an OTA push (the image is still in its validation trial window).

Disconnect the battery pack before connecting USB (spec §3.2).

## Hardware

Prototype parts, quantities, order status and South African sources:
[docs/hardware/bom.md](docs/hardware/bom.md). Pin map and electrical details: spec §3.

## Branching

`main` is always releasable. One branch per roadmap session, named `s<plan>.<day>-<topic>`, merged
by squash PR once CI is green. Tags `pNN-dD` mark finished sessions, `plan-NN-done` finished plans,
`vX.Y.Z` releases.

`main` is protected: pull requests only, linear history, no force pushes, enforced for admins. The
required status checks today are `hygiene`, `host-tests`, `tracks-generated`, `build (moto_neo6m)`
and `build (moto_sim)`. CI itself runs nine jobs on every PR — those five plus `dc-host-tests`,
`tools-tests`, `static-analysis` (the Power-of-10 gate) and `build (devcontroller)`; the audit fix
wave's final task raises the required set to all nine.

## License

Source-available under the [PolyForm Strict License 1.0.0](LICENSE): noncommercial use only, no
distribution. Copyright (c) 2026 Bennie Swanepoel.
