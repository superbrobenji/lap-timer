# Fix Wave F5 — Close-Out Wave (skeleton)

> **For agentic workers:** this skeleton is filled into a full plan (tasks with TDD steps, per the writing-plans skill) AFTER the fix wave's bench day, because several items depend on what that bench shows (#106 root cause, #53 measurements, #99/#97 sub-checks). Until then it is the authoritative task list and owner table. Execute with superpowers:subagent-driven-development once filled.

**Goal:** Close every open issue and in-progress plan item that is hardware-independent with the owned bench, leaving only explicitly hardware-gated items under the plan that owns the hardware.

**Architecture:** One branch per item (or small group sharing files), each a reviewable PR; items are disjoint by file so they run in parallel worktrees. Every item ends with its issue closed by the PR and, where behaviour is only visible on hardware, a bench item in `docs/bench/next-bench-day.md`.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` §7 (scope) and §7b (per-issue verdicts); umbrella `docs/superpowers/plans/2026-10-13-audit-fix-wave.md`.

## Global Constraints

Same as the umbrella (design §4 gates, no flashing by subagents, checkpoint protocol with ≤ 4 tasks per session block, ledger `.superpowers/sdd/2026-10-13-fix-F5/progress.md`, commit trailer from `git log -1 --format=%B`). The new linter checks from F4 are blocking by now.

## Task list (to be expanded; owners and files fixed now)

| # | Item | Files | Closes | Mode |
|---|---|---|---|---|
| 1 | `list` memoised per-session summary cache maintained by the logger (open/close/delete/evict update it; `op_list` reads it, no `.sum` scan) | `logger.c`, `cmd.c`, `logger.h`, `status.h` | #91 | PARALLEL |
| 2 | `0xFF` stream-frame CRC16 (`lt_stream_hdr_t` + 2 B; dev-kit demux verifies; `selftest stream` checks; bad-frame counter in `stream stats`) | `link.c`, `lt_proto.h`, `devcontroller/components/linkhost/*`, `cmd_selftest.c`, tests both sides | #61 | PARALLEL |
| 3 | Per-chunk OTA ACK (`OTA-ACK <offset>` after each 4 KB write; `ota_push.py`, `linkhost_flash`, `flash push` wait for it; abort on timeout) + bench: 600 KB push over USB with the logger writing, 0 retries | `export_serial.c` (ota recv), `tools/ota_push.py`, `devcontroller/components/linkhost/linkhost.c`, `cmd_flash.c`, tests | #57 | SERIAL after 2 (shares linkhost.c) |
| 4 | Sim clock epoch kept across resets (`RTC_NOINIT_ATTR` in `sim_common/sim_clock.c`, sim build only) so `dbg crash` mid-lap resumes forward-in-time; bench item 17's resume half executed | `sim_clock.c`, `gps_sim.c`, `test_sim_clock.c`, `next-bench-day.md` | #103 | PARALLEL |
| 5 | Logger append-path reserve guard (`do_write` checks `STO_RESERVE_PCT` via the cached `free_kb`, pauses samples, keeps summaries) + `sto_info` cache invalidation on open; root-cause the `free_kb 0` reading (LittleFS `lfs_fs_size` vs an open file's uncommitted blocks) and document | `logger.c` | #104 | SERIAL after 1 (shares logger.c) |
| 6 | Boot-time reap of `/sessions/*.sum.tmp` (bounded name-only listing) | `logger.c` | #105 | SERIAL after 5 |
| 7 | #106 console INFO silence: confirm on the bench that R9's restore-on-every-exit fixed it; if not, add `dbg loglevel` readback + the `esp_log_level_get("*")` line to `dbg status` and bisect | `export_serial.c` | #106 | PARALLEL (bench first) |
| 8 | Task stacks: with R6's stack-low check live, 1 h soak per env, record per-task high-water in `docs/measurements.md`, set §4.3 sizes to high-water + 25 %, remove the "tight" notes | task `*_STACK_BYTES` sites, `docs/measurements.md`, spec §4.3 | #53 | PARALLEL (bench first) |
| 9 | Calibration wiring (design §7 #93 row): `CMD_CALIB_ORIENT` → still-window capture → `lt_cal/fus` blob → `EV_CALIB_DONE` → CALIBRATE one-shot OK/FAIL; forward learning per fix, persisted on success; `CALIB` record on each persist; sim-tested | `pipeline.c`, `lt_nvs.c`, `ui.c`, `screens_moto.c` (one-shot text), `logger.c` (CALIB record), tests | #93 (amended: validation → Plan 8) | SERIAL after 1/5/6 (logger.c) |
| 10 | Synth `--venue-id` + `gen_sim_capture.sh`; sim table-capacity edge handled (reserved id frees no slot → log + skip, documented) | `tools/replay/*`, `gps_sim/gen_sim_capture.sh`, `sim_capture.h`, `pipeline.c` sim block | #100 | PARALLEL |
| 11 | Rebase `s5.2-conn-ble` onto post-wave `main` once (conflicts resolved, BLE still not activated; note R33 re-enable) | branch only | — | PARALLEL |
| 12 | Bench checklist close: run every remaining `next-bench-day.md` box; move hardware-gated boxes under Plan 6/8 headings; close #92 | `next-bench-day.md`, `docs/measurements.md` | #92 | SERIAL last |
| 13 | Roadmap close-out section updated with the merge commits; `project-audit-2026-10-13` memory updated | roadmap | — | SERIAL last |

Hardware-gated, recorded only: #11 (M10 PPS), #54 pull-up/EXT0 (Plan 6), #82 (Plan 8), #93 real-IMU validation (Plan 8), GPS/IMU ladders (Plan 8), BLE (sub-project C).
