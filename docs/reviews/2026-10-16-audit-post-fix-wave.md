# Audit 2026-10-16 — main after the fix wave (F0 F3 F1a F1b F2a F2b F4), re-audited against code, spec and bench doc

Scope: `main` @ 7265059 (`git rev-parse HEAD` → `7265059abf97d0eaa2a209fc35c29263d6859046`). Seven disjoint reviewer areas, each
followed by an adversarial verifier; this report is the synthesis. Where a verifier's verdict differs from its reviewer's, the
verifier's stands; where two areas found the same defect it is one row here (merged ids named in the row).

Evidence legend (all reports are git-ignored, under `.superpowers/reviews/2026-10-15/`; paths in the Appendix):
`ce` core-engines, `io` core-io-ui, `app` app-layer, `drv` drivers-build, `tci` tools-ci-tests, `dc` devcontroller-link-sim,
`doc` docs-claims; a trailing `-v` means the verifier's report (e.g. `app-v §1 N1` = app-layer-verified.md §1 row N1).
`gates` = gates-main.txt. File:line citations are on 7265059. "Spec" = docs/superpowers/specs/2026-09-14-lap-timer-design.md;
"bench doc" = docs/bench/next-bench-day.md; "FW" = docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md.

## 0. Verdict

- **Every advertised gate is green on 7265059 and reproduces:** host 51/51, dev-kit 33/33, tools 71 + 12 + 5, P10 CI form rc 0,
  four firmware builds rc 0 with 0 warnings, nine required checks enforced (`gates`; tci-v §2). No fix-wave verdict is BROKEN and no
  finding was REFUTED in any area (each `-v` §5).
- **The wave closed what it targeted:** all four 2026-10-13 CRITICALs and 12 of its 13 HIGHs are FIXED or PARTIAL (code fixed,
  unpinned); none REGRESSED; the one NOT FIXED HIGH is the dev-kit OTA pacing, mitigated as PR #113 says (#57) (§2.1–2.2).
- **DRAM is no longer a constraint:** 66,472 B free on moto_sim, 66,640 B on moto_neo6m (from 4,192 / 5,616), BT off (drv-v §0).
- **moto_neo6m still exercises almost nothing:** no GPS fix, no IMU, no session (gps_neo6m.c:34-38, imu_mpu6050.c:16); only bench items
  38, 40, 49, 62 run on it, and none of the F1a engine or session-lifecycle items can (doc §5; §5).
- **Exports can still succeed on a damaged log:** one CRC-bad FIX_DELTA shifts rows with SUCCESS + valid CRC32, a CRC-bad LAP vanishes
  from the JSON summary with `"skipped":0`, the summary dies with the `.log` after eviction, and VBO never carries `[laptiming]` —
  all executed through the real cmd.c (IO-N2, IO-N1, IO-N3, IO-N4; io-v cases A–D).
- **PR #127 merged its own "fix before merge" item:** every rotated session's `.sum` VENUE name is blank and items 48/64/69 pass anyway
  (APP-N1; app-v harness A T1b).
- **Release tooling can sign a wrong version:** `CFG_FW_VERSION` is fixed at configure time and sign_release never reconfigures
  (DRV-N1; drv-v repro `v1.0.0-dirty <- embedded`).
- **Three "gates" are weaker than their documents say:** the R29 "permanent test" does not exist (DC-FC-1, mutation passes 4/4),
  rule 9 cannot see the 11 esp_console callbacks (TCI-T2), and spec_api_check ignores macros while §5.2 says it checks them (TCI-T5).
- **Four new HIGH engine defects are latent until Plan 8:** Doppler re-anchored at arrival, not epoch (CE-N2); forced layout keeps the
  old lock (CE-N3); lap best crosses venues (CE-N4); un-oriented fusion blocks drag arming above 4° (CE-N5) (ce-v §1).
- **The bench doc is not ready:** of the 51 rows in §5, 6 cannot run as written (28, 33, 54, 55, 57, 58) plus item 26's PIT half,
  6 would false-pass (20, 39, 48, 50, 52, 69), 15 need a rewrite, 25 run as written, and the helpers are an OTA-rollback hazard
  (§5; DC-N7).
- **Severity totals after verification, de-duplicated:** CRITICAL 0 · HIGH 14 · MEDIUM 44 · LOW 111 (169 findings: 79 in §4, 90
  standalone false claims and bench-doc rows in §3/§5). Raw per-area sums before de-duplication: ce 4/3/4 + 8 FC, io 4/3/16,
  app 2/8/7 + 8 FC, drv 1/4/15, tci 2/2/12, dc 1/5/5 + 5 FC, docs 0/27/52 (H/M/L). Merged: APP-N1 = DOC D-8 = doc rows 161/263 = #127
  M-1; APP-N21 = DOC D-6 (rows 164/170); DRV-N5 = DOC D-1 (row 152); DRV-N3 = row 65; DRV-N4 = row 53; DRV-N11 = row 20; IO-N7 =
  DOC D-5 (row 148); IO-N12 = row 40; IO-N16 = rows 44/61/94; APP-N14 = row 33; APP-N16 = row 52 = doc-v V-6; APP-N19 = row 275;
  DRV-N15 = row 68; TCI-T14 = row 86; DC-V-1 = doc-v V-3; APP-N22 = doc-v V-5 (fill half); DC-N5 = DRV-V2; APP-N7 = #114 = CE-H3
  residual; DC-FC-1 = drv:H2 PARTIAL; D-2 = rows 146/269.
- **Triage (§6): A 8 · B 66 · C 89 · D 6.** Before bench day: fix the helpers' DTR/RTS (A1), sign_release's version (A2), the blank
  VENUE (A3), the dev-kit relay rc/truncation/idle (A4–A6), commit the gps_sim test (A7) and the CI "nothing to do" scaffold (A8); and
  rewrite the 29 bench rows (B).

## 1. What is proven today (7265059)

### 1.1 Gates (orchestrator clean runs, `gates`, plus verifier re-runs)

| Gate | Result | Evidence |
|---|---|---|
| hygiene job (stale-comment, `git diff --check`, idf tag) | rc 0 | `gates` "== hygiene rc=0"; re-run by tci-v (extracted step rc 0) |
| `spec_api_check` | `0 drift(s) across 13 §5.2 blocks` | `gates`; but the tool does not check macros, fn-ptr typedefs or 9 of 22 headers — TCI-T5 (§4) |
| Host suite (clang, Debug, ASan+UBSan) | `100% tests passed out of 51` | `gates`; re-run fresh by ce-v, io-v, app-v, tci-v (0 warnings) |
| Dev-kit host suite | `100% tests passed out of 33` | `gates`; dc-v §0 |
| tools tests | `OK` (71 unittests), `test_sign_release: 12 passed, 0 failed`, `test_build_refusals: 5 passed, 0 failed` | `gates`; doc row 257; drv-v header; tci-v header |
| Power-of-10 linter, bare | `BLOCKING: 5 violation(s)` — the five #131 sites (lt_nvs.c:376, cfg.c:7, cfg_json.c:184, export_serial.c:418, :717) | `gates`; tci-v §2 row 3 |
| Power-of-10 linter, CI flags (`--enforce-fnptr --fail-on-violation --report-only-sites <5>`) | rc 0, 0 GRACE-STALE, 88 devcontroller report-only lines | tci-v §2 row 3; the `gates` "unrecognized arguments" line is the orchestrator's own grep extraction, not the README/CI form (doc row 198) |
| venue-announce linter | `0 violation(s)` | `gates`; tci-v header |
| tracks-generated | `tracks-identical` | `gates`; tci-v header (`cmp` identical) |
| Firmware builds, IDF v5.3.2, ccache off | moto_sim, moto_neo6m, moto_sim_ws29v2, core_selftest: each `rc=0 warnings=0`; moto_sim rebuild `rc=0 warnings=0` | `gates` |
| Coverage (advisory, job-level `continue-on-error`) | `lines: 98.5% (4395 out of 4461)`, branches 68.1 % | tci §1 M13c (run 38722857364); tci-v §1 T12 |
| core_selftest on an ESP32 | **not run** — CI builds it only | tci-v §3; drv-v §3 |

### 1.2 DRAM and image size (verified map readings; BT controller off)

`dram0_0_seg` = 0x3ffb0000 / 0x2c200 = 180,736 B on all three maps; `# CONFIG_BT_ENABLED is not set` (build/moto_neo6m/sdkconfig:459) (drv-v §0, §2).

| Image | .dram0.data | .dram0.bss | used (`_heap_low_start` − origin) | free static DRAM | app image | Evidence |
|---|---|---|---|---|---|---|
| moto_sim (ws213v4) | 10,944 | 103,320 | 114,264 | **66,472** | 655,280 B | build/moto_sim/laptimer.map:37760 `_heap_low_start` 0x3ffcbe58 (drv-v §0); doc row 206 |
| moto_neo6m | 10,928 | 103,168 | 114,096 | **66,640** | 589,744 B | map lines 20725/21135/37516 (drv §DRAM, drv-v §0); doc row 97 |
| moto_sim_ws29v2 | 10,944 | 104,984 | 115,928 | 64,808 | 655,280 B | drv-v §0 |
| core_selftest | 8,992 | 145,200 | 154,192 | 26,544 | — | drv §DRAM; doc row 213 |

All app images are below the 0x130000 (1,245,184 B) size gate (drv-v §2, `wc -c`). The spec §4.8 / PR #138 figures 66,472 / 66,640
reproduce exactly (drv-v §0; doc rows 206, 259). The §4.8 table itself is wrong in two rows (one 4.7 KB framebuffer vs two 3,904 B
buffers; export_serial.c's 7,989 B of statics, incl. the 7th-largest static `s_ota_buf` 4,100 B, has no row) — DRV-N11 (§3).
Change since 2026-10-13: free DRAM 4,192 / 5,616 B → 66,472 / 66,640 B, from R33 (BT off) plus right-sizing (doc row 211:
4,072 + 60,952 = 65,024 → +1,448 on moto_sim).

### 1.3 Branch protection and merge discipline

- Required checks on `main` = `hygiene, host-tests, tracks-generated, build (moto_neo6m), build (moto_sim), dc-host-tests,
  tools-tests, static-analysis, build (devcontroller)`; strict true, enforce_admins true, linear history (`gates` "branch
  protection"; tci-v §2 row 1). `required_pull_request_reviews` is null (tci-v §2 row 1), so README:76 "pull requests only" is not a
  protection setting (doc row 202).
- Every fix-wave PR's final head ran all nine checks green, was `behind 0` against the previous main, and is tree-identical to its
  squash commit: #121 5f956eb→430b4bc, #122 5ae2284→9395a2f, #126 85e01bc→f38d8a6, #127 6436f81→e0defd3, #138 30888b1→7265059
  (tci-v §2 row 2).

### 1.4 Open issues

36 open (`gates` "open issues: 36"; `gh issue list --state open` re-run by this synthesis → 36). The roadmap owner table says 37,
still lists #110/#120 (closed 2026-10-15T10:48:23Z) and omits #139 (doc row 181).

### 1.5 Not proven by anything in the tree

- Any behaviour of a real GPS or IMU: `gps_neo6m.c:34` and `imu_mpu6050.c:16` return `-ENODEV`; `gps_poll` never yields a fix
  (`gps_neo6m.c:38`) (drv-v §2 H3). On moto_neo6m no lap, drag, fusion or session-lifecycle path runs (ce §5; app-v §5 item 6).
- core_selftest execution on target; panel legibility; LINK timing; tactile debounce; heap headroom under load; LittleFS free-space
  accounting for an open file (#104) — hardware only (tci-v §3, drv-v §3, app-v §3, doc §4).

## 2. Fix-wave scorecard

Verdict rule (BRIEF §1.3): FIXED = the old reproducer cannot reproduce AND a committed test pins it; PARTIAL = code fixed, no
pinning test (or a residual class remains); NOT FIXED; REGRESSED. No item REGRESSED: every verifier reports "BROKEN fixes: none"
(ce-v §5, io-v §5, app-v §5, drv-v §5, tci-v §5, dc-v §5).

### 2.1 2026-10-13 audit §7.1 CRITICAL

| Finding (2026-10-13) | Plan / PR | Verdict | Pin or gap | Evidence |
|---|---|---|---|---|
| OTA can never stick on a real-GPS build (gate on `pipeline_gps_seen`) | F2a #126 | PARTIAL | gate `pipeline_sensors_ok()` has no host test; only bench item 40; stale-flag half pinned by `test_precond_stale_flag_is_cleared_not_refused` | app-v §2 C1 (gps_neo6m.c:27 `.live = 0`, pipeline.c:1458-1476, :1793; sup.c:247-251) |
| Export: negative/absurd `gps_us` → garbage UTC, truncated rows | F1b #122 | PARTIAL | negative case pinned by `test_vbo_drops_a_record_with_a_bad_timestamp`, `test_nmea_drops_a_record_with_a_bad_timestamp_and_counts_it`; positive-absurd and INT64_MAX open (IO-N5) | io-v §1 C1, §2 C1 (2026-10-13 repro1 → `len=0` ×3) |
| JSON summary truncated, success reported | F1b #122 | FIXED | `test_json_header_with_control_chars_still_emits_every_lap` | io-v §2 C2 (repro3 + json_ctrl: laps present, rc 0) |
| Short (<5 s) GPS dropout near S/F → wrong lap VALID | F1a #121 | FIXED for delivered-invalid fixes; absent-fix class open (CE-N1) | `test_short_dropout_chord_flags_the_opened_lap`, `test_long_absent_gap_flags_the_completing_lap` | ce-v §2 C1 (repro_gap → `LAP 4 … 0x01 invalid`) |

### 2.2 2026-10-13 audit §7.2 / §7.5 HIGH

| Finding | Plan | Verdict | Pin or gap | Evidence |
|---|---|---|---|---|
| §19.5 OTA preconditions absent | F2a | FIXED (residual APP-N2) | `test_precond_close_session_runs_last`, `test_precond_refusals` | app-v §2 H1 |
| Task WDT: pipeline/logger not subscribed | F2a/F2b | PARTIAL | IDF-bound, no host test; bench 42/60 | app-v §2 H2 (pipeline.c:1611, logger.c:1205, ui.c:2088) |
| Fault-strip bit→icon table wrong | F1b | FIXED | `test_fault_icon_table_matches_spec_17_4` | io-v §2 H1 |
| `cfg_json.c` `walk()` recursive | F1b + F4 | FIXED | iterative loops cfg_json.c:133-167; linter `rule1(recursion)` fires on a probe (direct recursion only, TCI-T4) | io-v §2 H2 |
| `dbg sim drag` integrates position cumulatively | F3 #113 | PARTIAL | no pinning test: a mutation restoring the bug passes `test_sim_profile` 4/4 (DC-FC-1) | dc-v §1 FC-1; drv-v §2 H2 (`max_step_drag=12.40 m`; mutant `690.24 m`) |
| `tracks get/put`, `coredump`, `TIME_SYNC` absent | F2b #127 | PARTIAL | tracks/coredump code present (cmd.h:47-49; export_serial.c:542-563, :1333-1334); no report names a pinning host test; TIME_SYNC → Plan 8 (#124) | app-v §2 §3 claim 11; drv-v §2 H4; doc §1 3.3#21 |
| Dev-kit AP PSK derivable from BSSID | F3 | FIXED (pure half) | `test_dc_psk`; RNG/NVS half on target only | dc-v §2 dk:H1 |
| Dev-kit OTA stream unpaced into a 256 B ring | F3 | NOT FIXED (mitigated, as PR #113 states) | 25 ms/4096 B pacing; no per-chunk ACK; #57 OPEN | dc-v §2 dk:H2 |
| Sector gate in the S/F segment lost → INCOMPLETE | F1a | FIXED | `test_sector_just_past_sf_in_the_same_segment` | ce-v §2 H1 (repro_lap laps 1–10 VALID) |
| Best sectors / theoretical best leak across venues | F1a | FIXED for §10.8; the lap best/prev/predictive reference still leak (CE-N4) | `test_best_sectors_cleared_on_venue_change` | ce-v §2 H2 |
| Unknown layout id arms zero candidates | F1a + F2b | engine FIXED; pipeline NOT FIXED (#114 = APP-N7) | `test_force_layout_unknown_id_is_refused`, `test_import_rtc_rejects_bad_layout_and_sector_idx` | ce-v §2 H3; app-v §1 N7 (harness T7 `rc=-1 … s_best.theo_ms=0`) |
| `cfg.lap.*` inert | F1a | FIXED | `test_cfg_min_lap_s_reaches_the_engine`, `test_default_layout_map_breaks_the_tie`, `test_set_cfg_applies_without_resetting`, `test_bad_min_max_window_falls_back_to_defaults` | ce-v §2 H4 (+ ce verify-repro-item26) |
| Marginal-g launch then stall → spurious DONE | F1a | FIXED | `test_marginal_g_launch_then_stall_aborts` | ce-v §2 H5 |

### 2.3 2026-10-13 audit §7.3 / §7.5 MEDIUM

| Finding | Verdict | Pin or gap | Evidence |
|---|---|---|---|
| Button ISR calls flash-resident code | PARTIAL | ELF shows the chain in IRAM on both builds; no CI/ELF check pins it | drv-v §2 C1 |
| `g_fix_ring` two producers | FIXED (by construction) | only pipeline.c:962 and :1389 push, both on the pipeline task (grep census) | app-v §2 M2 |
| errlog dedup on (code,arg) | PARTIAL | lt_nvs.c:400-410; NVS-bound, no test | app-v §2 M1 |
| Side effects inside asserts | FIXED | regex census: 0 code hits | app-v §2 M3 |
| Tautological padding asserts | PARTIAL | remnants link.c:234, ui.c:371, pipeline.c:1056, units.c:14/:36, lap.c:64/136/411, lap_cfg.c:38, trk.c:350/:417, trk_json.c:127, drag.c:29 | app-v §2 M4; io-v §1 N15; ce-v §1 N8 |
| §17.2 heap/stack/EV_FAULT/ladders | FIXED for heap/stack/EV_FAULT; ladders deferred (#123) | `test_heap_hysteresis`, `test_stack_low`, `test_flag_next_change` | app-v §2 M5 |
| §12.6 still-pause | FIXED (residual APP-N5) | `test_log_pause` (7 cases) | app-v §2 M6 |
| §12.1 session lifecycle (mode/venue change) | FIXED with residuals APP-N1, APP-N3 | 17 `test_session_policy` cases | app-v §1 N1, N3; doc §1 3.3#24 |
| `s_seq` saturation merges sessions | PARTIAL | executed in app-v harness B (`256th open is refused … 0x0401 arg 0xff`); no committed test | app-v §2 M13 |
| `status.state` hardcoded 0 | PARTIAL | status.c:68; no test | app-v §2 M7 |
| No OTA transfer timeout | FIXED | `test_recv_stale` | app-v §2 M8 |
| Constants / `E_OTA_*` vs Appendix A | FIXED (spec moved to code, R3) | spec:2763, :2779, :2134-2140 = lt_err.h | app-v §2 M9/M10 |
| cfg RMW lock; save-on-change | PARTIAL | lock at all five RMW sites; memcmp cmd.c:224-226; no test | app-v §2 M11 |
| `ESP_LOGE` tears framed transfers | PARTIAL (NONE windows unpinned; bench 39) / FIXED (stall line once per episode: `test_stall_logs_once_per_episode`) | export_serial.c:281/379/1227 windows restore on every exit | app-v §2 M12; drv-v §2 M6 |
| `json_tok_bool` ignores type | FIXED | test_cfg.c:363 | io-v §2 M3 |
| `ring_pop` CAS retry unbounded | PARTIAL | ring.h:93 cap; only the uncontended path tested (test_ring.c:139) | io-v §2 M1 |
| `sto_write` loop unbounded on `write()==0` | PARTIAL | storage_internal.c:169-176; not host-built; `read_through_ses` `for (;;)` remains (DRV-N18) | drv-v §2 M10 |
| `qsort(cmp_int)` unregistered fn-ptr | FIXED | `test_batt_trim` | drv-v §2 M12 |
| `sign_release.sh` refuses neither sim nor dirty | PARTIAL | `test_sign_release` 12/12; stale embedded version (DRV-N1), untracked `.c` (DRV-N7), dry-run over-claim (DRV-N8) | drv-v §2 M5 |
| CHRG floats → OTA battery bypass | FIXED (code) | board.c:295-303, ota.c:140-146; no test | drv-v §2 M11 |
| devkit.py / ota_push.py auto-pick port; 35 s rule | FIXED in both tools | `probe_is_devkit` tests; `test_note_states_the_window_and_the_consequence`; git-ignored bench helpers not fixed (DC-N7) | dc-v §2 dk:L7; tci-v §2 M9 |
| Dev-kit download cap / idle reset | FIXED (pure half) | `test_linkhost_dl_cap`, `test_linkhost_dl_gap` | dc-v §2 dk:M1 |
| `/api/sessions` `portMAX_DELAY` | PARTIAL | linkhost.c:443 bounded; IDF glue untested | dc-v §2 dk:M2 |
| `selftest framing` misses a bare LF | FIXED | `test_selftest_eval` | dc-v §2 dk:M3 |
| `lt <cmd>` six-verb whitelist | PARTIAL | relay exists (cmd_lt.c:492-551), zero tests, two new defects DC-N2/DC-N3 | dc-v §2 dk:M4 |
| SSE overrun off-by-one | FIXED | `test_sse_cursor` | dc-v §2 dk:M7 |
| `stage_image` keeps sending after STAGE-ERR | FIXED (host) | `test_stops_sending_on_an_early_stage_err` | dc-v §2 dk:M8 |
| `push_and_wait` drops `ERR` lines | FIXED | `test_err_line_raises_at_once_with_the_device_message` | dc-v §2 dk:M9 |
| `exp_nmea` reserve 200 < 230 B | FIXED | `test_nmea_reserve_bounds_both_sentences` | io-v §2 M5 |
| `ses_encode_venue` caps name at 31 | FIXED | test_exp_nmea_json.c:160 (32 chars) | io-v §2 M9 |
| `cfg_apply_profile` unvalidated | FIXED | `test_profile_rejects_a_log_rate_outside_the_spec_set` | io-v §2 M6 |
| `fb->dirty` dead / "changed region" claim | FIXED | grep: no fb field left | io-v §2 M4 |
| §11.4 benches rule claim | FIXED (documented as superseded, spec:1609) | — | io-v §2 FC3-FC7 |
| Un-oriented `g_lon` not clamped | FIXED (clamp); consequence NOT FIXED (CE-N5) | `test_unoriented_g_lon_is_clamped_to_gmax` | ce-v §2 M1 |
| `gate_step` asserts on backward `gps_us` | FIXED | `test_backward_gps_us_step_does_not_lose_a_gate` | ce-v §2 M2 |
| `lap_import_rtc` validates only venue | FIXED (`sector_idx`); rest of snapshot unvalidated (CE-F5) | `test_import_rtc_rejects_bad_layout_and_sector_idx` | ce-v §2 M3, §1 F5 |
| `abort_to_armed` no EV_DRAG_ARMED, recycles run_no | FIXED (by design change) | `test_marginal_g_launch_then_stall_aborts` | ce-v §2 M4 |
| `lap_mark_gate` ignores a degenerate line | FIXED | `test_mark_gate_refuses_an_out_of_range_heading`, `test_gate_line_rejects_*` | ce §1 M5 |
| `FUS_BIAS_STALE` clears without recapture | FIXED | `test_bias_stale_latches_until_a_recapture` | ce-v §2 M7 |

### 2.4 2026-10-13 audit §7.4 linter gaps

| Gap | Verdict | Evidence |
|---|---|---|
| LG-1 rule 3 not blocking | FIXED (`test_lg1_heap_only_file_fails_the_build`) | tci-v §2 LG-1 |
| LG-2 heap API surface | PARTIAL (`_prefer`, `memalign`, `alloca`, `#define` wrapper missed; 0 live instances) | tci §1 LG-2; tci-v §2 |
| LG-3 rule 9 one spelling | PARTIAL — esp_console callbacks still unseen (TCI-T2) | tci-v §1 T2 |
| LG-4 asserts in `#if 0` | PARTIAL (`#if 0`/`#elif 0` FIXED; `#ifdef NEVER` and literal tautologies counted; 0 live) | tci-v §1 T3 |
| LG-5 physical-line counting | FIXED (`test_lg5_statements_not_physical_lines`) | tci-v §2 LG-5 |
| LG-6 rules 1/2/6/7/10 outside core | PARTIAL (rule 1 direct only; rules 2/7 unchecked; callgraph 927 functions, 0 cycles) | tci-v §1 T4 |
| LG-7 devcontroller unscanned | PARTIAL (report-only, 88 lines) | tci-v §2 LG-7 |
| §7.4 → static-analysis required on main | FIXED | §1.3 |
| gen_fonts drift check | NOT FIXED (`grep -c gen_fonts .github/workflows/*.yml` → 0) | tci-v §2 M13d |

### 2.5 2026-10-13 audit §3 / §6 process and doc items

| Item | Verdict | Evidence |
|---|---|---|
| §3.1#1 P10 not a required check | FIXED | §1.3 |
| §3.1#2 roadmap "pending" bench gates | FIXED | doc §1 (roadmap:190,207,221 "PASS") |
| §3.1#3 / §6#1 evidence off-tree | FIXED (bench days 1–5 in measurements.md:21-83) | doc §1; tci-v §2 §6.1 |
| §3.1#5 README false statements | FIXED (one new LOW, README:76) | doc §1 |
| §3.1#6 PR #101 "goldens byte-identical" | FIXED (sentence gone) | doc §1 |
| §3.2 comment ledger items 7, 9, 15 | FIXED (#15's 10 km/h lock is pinned on the host only; bench item 52 cannot discriminate it, doc-v V-1) | doc §1; doc-v §4 |
| §3.2#10 RTC resume never run on hardware | PARTIAL (roadmap discloses #103; bench items 55/57 still ask the sim for it) | doc §1; §5 below |
| §3.2#11 app_main "power start here" | PARTIAL (app_main.c:286-289 survivor = APP-N19) | doc §1; app-v §1 N19 |
| §3.2#12 pipeline "CALIB_ORIENT: later sessions" | NOT FIXED (allow-listed, owner #129) | doc §1 |
| §3.2#14 menu stubs blamed on plan 05/06 | NOT FIXED (ui.c:631-634) | doc row 276 |
| §3.3#17–#21, #23, #26, #28 | FIXED | doc §1 |
| §3.3#22 TIME_MAP/CALIB never written | FIXED (CALIB record race-dependent, doc row 160) | doc §1; app-v §2 claim 10 |
| §3.3#24 / #25 session lifecycle | FIXED in code; residual APP-N1; neo6m path unobservable on the stub | doc §1 |
| §3.3#27 §17.6 self-test | PARTIAL (#133) | doc §1 |
| §3.3#29 §4.3 task map | NOT FIXED (#132) | doc §1; doc-v §1a row 9 |
| §3.3#30 §20.7 menu stubs as features | NOT FIXED (#134) | doc §1; doc-v §1a row 78 |
| §6#3 roadmap as state document | PARTIAL (stale on its own merge: main = e0defd3, F4 "draft") | doc rows 178-179 |
| §6#4 spec gaps get issues | PARTIAL (#129–#137 filed; pr:L5/L6/L7 dropped untracked) | doc row 253 |
| §6#5 comments as ledger | PARTIAL (hygiene pattern misses "plan 0x", "until Plan N") | doc rows 275-276; tci §1 §6.5 |

### 2.6 F-plan headline "fixed" claims that are contradicted

| Claim | Where | Verdict | Evidence |
|---|---|---|---|
| "The drag scenario is now a permanent test" | PR #113 body | FALSE (DC-FC-1) | dc-v §1 FC-1 mutation run |
| C1 fix makes "this chord spans an outage" "a property of the chord, not of its duration" | PR #121 body | FALSE (CE-F4) | ce-v §1 F4; lap.c:1172 `> LAP_SEG_GAP_US` |
| "Bench items 26–29 (the hardware half this PR cannot prove)" | PR #121 body | FALSE for 28 and 26's PIT half (cannot run); 27 runs with unnamed verbs; 29 half-discriminating (doc-v DOWNGRADED to LOW) | §5 rows 26–29; doc-v §1c row 270 |
| bench items 30–32 show "the four new fault glyphs (`dbg flag set 1|2|5|13`)" | PR #122 body | FALSE for bit 1 | ui.c:1550-1554 (read by this synthesis: `else f &= ~(1u << SCR_SYS_GPS_NOFIX)`); doc row 146 |
| "Every on-device claim is a bench item (33–45)" | PR #126 body | PARTIAL (33 cannot run; 40 unobservable) | §5 rows 33, 40 |
| "Parked residuals — fix before merge (Minor ×2), PR stays a draft" | PR #127 body | FALSE in effect: merged e0defd3 with M-1 (APP-N1) and M-2 (APP-N3) present | app-v §1 N15; doc row 263 |
| "`GRACE-STALE` fails the job when an entry stops matching" | PR #138 body | FALSE: rc 0 with a stale triple | tci-v §1 T1 |
| "Draft — not for merge until the user has read the rulings" | PR #138 body | FALSE in effect (merged 2026-10-15T10:48:21Z, isDraft false) | doc row 261 |
| "#107 fixed by R5" | FW §7b | FALSE: #107 OPEN | doc rows 134, 226 |
| FW §4 "every finding id appears in exactly one plan or the filed table" | FW §4/§5 | FALSE: pr:L5 (ui.c:1220), L6, L7 (lap.c:296) neither fixed nor tracked | doc row 253 |

### 2.7 Close-out of the 2026-10-13 audit §10 "Status"

| Plan | PR | Merged (UTC) | Squash | Findings closed (verdicts above) |
|---|---|---|---|---|
| F0 | #101 | 2026-10-13T19:34:45Z | a390151 | pr:H1 H2 M1 M2 L1–L4, #102 closed; pr:L5–L7 dropped untracked (doc row 253) |
| F3 | #113 | 2026-10-14T10:35:06Z | 18c5eac | dk:H1 M1 M3 M7–M11 L1–L8 FIXED; dk:M2 M4 M6 PARTIAL; dk:H2 mitigated; dk:M5 → #112; drv:H2 PARTIAL (no pin) |
| F1a | #121 | 2026-10-14T18:00:39Z | 430b4bc | core:C1 H1 H2(§10.8) H3(engine) H4 H5 M1(clamp) M2–M7 FIXED; LOW list FIXED except rule-5 padding (PARTIAL) |
| F1b | #122 | 2026-10-14T18:06:26Z | 9395a2f | io:C2 H1 H2 M2–M7 M9 L1–L5 FIXED; io:C1 M1 M8 FC8 PARTIAL |
| F2a | #126 | 2026-10-14T21:09:33Z | f38d8a6 | app:H1 M5 M8 FIXED; app:C1 H2 M1 M7 M12 PARTIAL; drv:H3 M11 M12 FIXED; drv:C1 M6 M10 PARTIAL |
| F2b | #127 | 2026-10-15T06:44:36Z | e0defd3 | app:M2 M3 M6 FIXED; app:M11 M13 L1 L2 L7 PARTIAL; drv:H4 M7 PARTIAL (no pin); pr:M3 (#108) PARTIAL; pr:M4 bench-only (item 55 cannot run, §5); #114 NOT FIXED; #128 M-1/M-2 merged unfixed |
| F4 | #138 | 2026-10-15T10:48:21Z | 7265059 | LG-1 LG-5, required checks, coverage, R33 FIXED; LG-2/3/4/6/7 PARTIAL; gen_fonts NOT FIXED; roadmap stale on its own merge |
| F5 | — | — | — | not started; 13-task skeleton (doc row 191) |

Merge times and shas: `gh pr view N --json mergedAt,mergeCommit` (run by this synthesis).

## 3. False-claim register (FALSE rows that survived verification)

Rule: a false claim that is the text form of a §4 defect is listed here once and bucketed with that defect in §6 (the "→" column).
Severity is LOW unless marked. Bench-doc rows are in §5 and only counted here.

### 3.1 Spec (docs/superpowers/specs/2026-09-14-lap-timer-design.md) — 39 rows

| # | spec line / section | claim | contradicting evidence | → |
|---|---|---|---|---|
| S1 | §4.6 :409 | DEVUX=OFF keeps the OTA-receive transport (MEDIUM) | no OTA path in a DEVUX=OFF image (preprocessor proof) | DRV-N3 |
| S2 | §17.1 :2002 | brownout 2.8 V (`LVL_SEL_7`) (MEDIUM) | build ships `LVL_SEL_0` ≈ 2.43 V | DRV-N4 |
| S3 | §4.3 :330-341 | task map = conn, power, …; omits link and the console REPL (MEDIUM, #132) | xTaskCreate sites pipeline.c:1661, logger.c:1251, sup.c:435, ui.c:2210, link.c:261 | doc-v §1a row 9 |
| S4 | §5.1 :524-640 | HAL headers as written (`hal/hal_err.h`, `board_pps_enable`, `sto_list(cb)`) (MEDIUM) | no hal_err.h; board.h:59, :63; storage.h list API | doc-v §1a row 21 |
| S5 | §9.2 :1350-1374 | calibration persisted in `lt_cal/fus`, `EV_CALIB_DONE` emitted by the pipeline (MEDIUM) | no app caller of any fus_calib_* / no emitter | doc-v §1a row 29 |
| S6 | §20.7 :2436-2446 | Calibrate, Export, Diagnostics, Sessions, Sleep presented as features (MEDIUM, #134) | pipeline.c:1440-1441 drops CALIB_ORIENT; ui.c:631-635 "not implemented" | doc-v §1a row 78 |
| S7 | §10.1 :1412-1417 | nine bundled venues (MEDIUM) | tools/tracks has killarney.json and zwartkops.json only | doc-v §1a row 90 |
| S8 | §5.2 :654-657 | blocks generated by `--emit`; hygiene fails on a missing macro/typedef (HIGH) | 26 header macros absent with 0 drift | TCI-T5 |
| S9 | §6.6 | drag timing "±5 ms" | error = fix latency | CE-N2 |
| S10 | §10.5 | `lap_force_layout` "locks immediately" | `locked_layout=2` after `force(1)` | CE-N3 |
| S11 | §11.2 Abort ¶ | a genuine launch "never trips" the abort | one low Doppler sample aborts | CE-N6 |
| S12 | §9.3 :1353 | un-oriented `g_lon` is the "horizontal" magnitude | body x/y incl. gravity (fus.c:275-279) | CE-N5 |
| S13 | §10.10 | `lap_import_rtc` "validates the whole snapshot" | lap.c:370-373 vs :406-415 (ce-v §1 F5) | B |
| S14 | §14.3 :1830-1833 | a damaged log never exports as a silently short file | IO-N2 executed | IO-N2 |
| S15 | §19.5 :2318 | "a refused push never ends a riding session" | a timed-out close still runs (app-v §1 N2) | APP-N2 |
| S16 | §12.2 :1641 | `ses_reader_flush` + frame callback | 0 hits; ses.h push/next/finish (io-v N12; doc row 40) | B |
| S17 | §12.5 / §13.3 / §17.8 / App. A | "4 KB" batch, `LOG_BATCH_BYTES` 4096; 4 KB buffer for a 5,500 B accumulator | BATCH_CAP 3840 (logger.c:63), LAP_ACC_CAP 5500 (io-v N16; doc rows 44, 61, 94) | B |
| S18 | §13.3 :1733 | logger encodes VENUE into the `.log` batch | only rebuild_sum encodes it (logger.c:294) (app-v V-2) | B |
| S19 | §17.1 :2000 | ui uses `esp_task_wdt_add` directly | ui.c:2088 sup_wdt_subscribe (app-v N16; doc row 52) | B |
| S20 | §17.7 :2145 | `E_CONN_XFER_ABORT` unimplemented | ota.c:445, :493 (app-v N17) | B |
| S21 | §15.2 :1896 | "only on change" not honoured, `lt_cfg_save()` writes unconditionally | cmd.c:224-226 compares before saving; ui sites always change a field (app-v N18). doc row 49 read the sentence as TRUE because lt_cfg_save itself has no compare — decided FALSE: the sentence's subject is the on-change behaviour, which callers implement | B |
| S22 | §20.2 :2368 | host tests "write test/snapshots/<screen>.pbm" | pbm.c:63-65 writes only `.actual.pbm` (io-v N13) | B |
| S23 | §4.8 table | one 4.7 KB framebuffer; table complete | 2 × 3,904 B; export_serial 7,989 B unlisted (drv-v §0; doc row 20) | B |
| S24 | §19.1 :2286 | refusal at build.sh:79-82 | at :104-108 (drv-v N15; doc row 68) | B |
| S25 | §21.6 | tools-tests list | omits test_build_refusals.sh (tci-v T14; doc row 86) | B |
| S26 | §10.11 :1574-1577 | pipeline passes the predictive buffers | no app caller (doc row 33; app-v N14) | B |
| S27 | §4.2 :228-322 | layout "generated from git ls-files" | 8 tracked files omitted (doc row 1) | B |
| S28 | §8 / :273 | IMU driver as present behaviour, no stub mark (downgraded MEDIUM→LOW by doc-v: S:2246, :2313 disclose it) | imu_mpu6050.c:16 `-ENODEV` | B |
| S29 | §4.4 :351, :353, :354 | `btn_q` item 4 B; `ui_req_q` exists; `log_request_t` 16 B | btn_raw_t 16 B; no ui_req symbol; `_Static_assert(... == 32)` (doc rows 13-15) | B |
| S30 | §4.5 :400-411 | command table complete | CALIB_ORIENT dropped; 4 commands untabled (doc row 17) | B |
| S31 | §9.1 :1318-1344 | queue-set loop; FUSED every 4 samples | `xQueueReceive` 50 ms; FUSED_DECIM 10 (doc rows 27, 28) | B |
| S32 | §17.4 table | bits 0-14 complete | bit 15 SYS_NVS_DEAD missing (doc row 58) | B |
| S33 | §20.6 :2420 | CALIBRATE one-shot is shown | nothing selects it (doc row 77) | B |
| S34 | §20.8 :2452 | level sampled 30 ms after the edge | hal/board.h:39-41 says not implemented (doc row 80) | B |
| S35 | App. A :2693, :2750; §0 :13 | `FUSED_RING_HZ` 25; every threshold in App. A | no such symbol; 5 constants missing, names drift (doc rows 93, 95) | B |
| S36 | §4.6/§21.5 | (TRUE: sign_release refuses `-dirty`) — the guard exists; the embedded version can still be stale | drv-v N1 | DRV-N1 (not counted) |

(S36 is listed to record that the sentence is literally true; it is not counted. Count = 35 FALSE rows: 2 HIGH-linked, 7 MEDIUM, 26 LOW.)

### 3.2 Bench doc (docs/bench/next-bench-day.md) — 29 rows

The §5 verdicts NEEDS REWRITE / CANNOT RUN / WOULD FALSE-PASS on items 20, 24, 25, 26 (×2), 27, 28, 29, 30, 32, 33, 39, 40, 41, 43,
47, 48/64/69, 49, 50, 52, 54, 55/57, 56, 58, 59, 70, 12/13/15, plus the stale cites (D-12: ui.c:513-529, pipeline.c:492-493) and V-9
(bench doc:301-302 "§17.7 numbered differently until F4"). Severity: HIGH 1 (item 50: passes on RAM alone — app-v kept HIGH, doc reviewer had MEDIUM; decided HIGH because the item
certifies persistence it never exercises), MEDIUM 13 (items 20, 26b, 28, 30, 33, 39, 40, 43, 48/64/69, 52, 54, 55/57, 58 — every
false-pass or cannot-run criterion), LOW 15 (items 24, 25, 26a, 27, 29, 32, 41, 47, 49, 56, 59, 70, 12/13/15, the D-12 cites, V-9).
Evidence per row in §5.

### 3.3 Roadmap, README, FW design, P10 documents — 19 rows

| # | where | claim | contradicting evidence | → |
|---|---|---|---|---|
| R1 | roadmap:4-5, :129, :142, :277, :279, :340; FW R31 | main = e0defd3; F4 "OPEN … draft"; R31 "the roadmap edit describes the merged tree" (MEDIUM, D-9) | main = 7265059; #138 merged 10:48:21Z, not draft; the stale text shipped in that merge (doc-v §1c) | B |
| R2 | FW §4/§5 :101 | every finding routed (MEDIUM, D-10) | pr:L5 ui.c:1220, L6, L7 lap.c:296 untracked (doc-v §1c) | B |
| R3 | roadmap:300-310 | 37 open; #110/#120 open; 87c6eef on main; owner table complete | 36 open, #110/#120 closed, #139 missing, 87c6eef not an ancestor (doc rows 180-182) | B |
| R4 | roadmap:62, :69, :141-145, :151 | tag equalities p00-d2, p5.6-d*, p5.5-d6 attribution | git rev-list (doc rows 185-187) | B |
| R6 | FW :84 | pre-wave baselines 4192 / 5616 B | CI logs: a390151 4072/5496, 7bbc48e 4064/5496, 9395a2f 4040/5472 (doc-v §3) | B |
| R7 | FW R4, R5 (#107 closes), R6, R10a, §4 order, §5 vs §7b (#57), §6, §7b #107 | ruling texts vs tree | doc rows 225-227, 231, 251, 252, 254, 134 | B |
| R8 | README:72-76 | "pull requests only"; tag per session | no review rule (tci-v §2); fix-wave PRs untagged (doc rows 202, 204) | B |
| R9 | docs/power-of-10-deviations.md rule-9 ¶ + PD-5 | esp_console callbacks are checked sites (HIGH) | TCI-T2 run | TCI-T2 |
| R10 | deviations.md :28-34 vs PD-7 | internal contradiction | tci-v V-3 | TCI-T2 |
| R11 | deviations.md PD-7 | cites export_serial.c:238/326/338, cmd.c:290/292 | now :288/:382/:394, cmd.c:363/365 (doc row 218) | B |
| R12 | P10 design §5 | rules 3/4/5 "un-evadable"; struct callbacks seen | tci-v T2, T3 | TCI-T2 / TCI-T3 |
| R13 | 2026-10-13 audit §10 | status table "to be filled" | closed by §2.7 of this report (doc row 277) | closed here |

(Count = 11 rows R1–R12 without R5; R13 is closed by this report and not counted.)

### 3.4 Code comments — 33 rows

drag.c:598-601 (→CE-N6); lap.c:219 (→CE-N3); lap.h:186 (APP-N14); event.h:26-27 ".sum EVENT record" (IO-N14); screens_moto.c:303-304
(IO-N19); exp_json.c:12-18, :27-33 "495 bytes" (worst case 496; IO-V3); export_serial.c dbg_logck "torn tail" footnote (IO-V2);
link.c:62-63 (APP-N16 = doc-v V-6); main/app_main.c:286-288 (APP-N19 = doc row 275); sup.c emit_faults "reach the session log"
(APP-N20); logger.c:89 "never open/write below" (→APP-N8); export_serial.c:594-597 (→DRV-N3); sdkconfig.defaults:47 (DRV-N6);
sign_release.sh:39-42 (→DRV-N7); export_serial.c:149-173 "trips the BUILD" (DRV-N9); board_devkit_v1/CMakeLists.txt:10-13 and
sim_common/CMakeLists.txt:18-20 "non-fatal" (DRV-N13); export_serial.c:18-20 "stubbed" (DRV-N14 = 2026-10-13 FC13, NOT FIXED);
gps_sim.c:39-41 continuity (DRV-V2; dc-v reads the sentence as drag-only — kept as one LOW wording row with DC-N5);
cmd_lt.c:74-77 (→DC-N2); cmd_lt.c:262-265 (DC-FC-2); export_serial.c:496-497, :550 (DC-V-2); lt_proto.h:67-69 (DC-FC-3);
cmd.h:37-40 stream header (DC-FC-4); tools/devkit.py:683-685 help (DC-FC-5); power_of_10.py:505-509 + test_power_of_10.py:78-83
(→TCI-T2, tci-v V-1); ci.yml:101 "24 Unity executables", .clang-tidy:7-9 (TCI-T14); board.c:132 "11 dB" (doc row 4); ui.c:631-634
"plan 05/06" (doc row 276, 2026-10-13 §3.2#14 NOT FIXED); pipeline.c:1441 "later sessions" (allow-listed, #129);
export_serial.c:276-277 storm comment (doc-v V-2, →item 39). Count 33; 9 inherit a §4 defect's bucket (→), 24 standalone (C).

### 3.5 PR bodies and issue comments — 12 rows

| # | where | claim | evidence | → |
|---|---|---|---|---|
| P1 | PR #113 | "drag scenario is now a permanent test" (HIGH) | DC-FC-1 | DC-FC-1 |
| P2 | PR #121 C1 row | "a property of the chord, not of its duration" | ce-v F4 | CE-N1 |
| P3 | PR #121 | bench items 26–29 prove the hardware half (downgraded MEDIUM→LOW by doc-v: 27 runnable, 29 half-discriminating) | doc-v §1c row 270 | C |
| P4 | PR #121 / bench 29 | "best … do not cross venues" | CE-N4 | CE-N4 |
| P5 | PR #122 | `dbg flag set 1|2|5|13` shows four glyphs (MEDIUM) | ui.c:1550-1554 | B (with item 30) |
| P6 | PR #126 | every on-device claim is a bench item | items 33/40 | B (with §5) |
| P7 | PR #127 | M-1/M-2 "fix before merge, PR stays a draft" (MEDIUM) | merged with both (app-v N15; doc-v row 263) | APP-N1 |
| P8 | PR #138 | GRACE-STALE fails the job | tci-v T1 | TCI-T1 |
| P9 | PR #138 | "Draft — not for merge" | merged (doc row 261) | C |
| P10 | issue #118 verification comment | "a one-fix delay" | ce-v F6 | CE-#118 |
| P11 | issue #128 06:24 | M-1/M-2 "fix before merge" | doc row 103 | APP-N1 |
| P12 | issue #92 | "15 unchecked boxes" | 60 at the cited tree, 62 now (doc row 128a) | C |

### 3.6 Counts per document

| Document | rows | linked to a §4 defect | standalone HIGH / MEDIUM / LOW |
|---|---|---|---|
| Spec | 35 | 9 (S1, S2, S8–S12, S14, S15) | 0 / 5 / 21 |
| Bench doc | 29 | 0 (each is its own criterion defect) | 1 / 13 / 15 |
| Roadmap, README, FW design, P10 docs | 11 | 3 (R9, R10, R12) | 0 / 2 / 6 |
| Code comments | 33 | 9 | 0 / 0 / 24 |
| PR bodies + issue comments | 12 | 9 | 0 / 0 / 3 |
| **Total** | **120** | **30** | **1 / 20 / 69** |

## 4. New findings after verification

Severity after verification (verifier overrides reviewer). Ids are area-prefixed with the reviewer's own id. "Release target" =
moto_neo6m: **yes** = the defective path runs there today; **latent** = the code ships there but no input reaches it until Plan 8
(no fix, no session, no IMU: gps_neo6m.c:34-38, imu_mpu6050.c:16); **no** = not in that image. Bench-doc defects (APP-N21,
DRV-N5, IO-N7, APP-N22, DC-V1, the doc D-rows) are carried in §5, and false claims in §3; both are counted in the totals.

### 4.1 HIGH

| id (merged) | sev | file:line | defect | evidence | bench-observable? | release target |
|---|---|---|---|---|---|---|
| IO-N2 | HIGH (reviewer CRITICAL; verifier DOWNGRADED: the firmware cannot itself write a CRC-bad frame followed by good ones) | ses_frame.c:60-80; ses_records.c:123-141; cmd.c:941-975 | One CRC-bad FIX_DELTA is dropped by the reader and every later delta is applied to the un-advanced anchor until the next FIX_KEY (≤ 5 s): VBO/NMEA rows run one sample early, export returns SUCCESS with a valid CRC32; `s_sr.frames_bad` is never consulted | io-v executed the real cmd.c: case C `SUCCESS last=1 … crc32 VALID`, last row `101500.40` vs truth `101500.60`; io-verify-repro-n2-rekey (rows 2..23 shifted) | no — needs physically corrupted flash; item 33 cannot produce it (§5) | latent |
| IO-N1 | HIGH (FALSE-CLAIM half withdrawn by verifier: §14.3's sentence is true; item 33's text is the contradicted one) | exp_json.c:233; cmd.c:941-975 | A CRC-bad LAP frame is absent from the JSON summary while the tail reads `"skipped":0` and the reply is SUCCESS; the `.sum` still holds the lap | io-v case D `laps present n=1:1 n=2:0 n=3:1 ; tail: "skipped":0}` | no | latent |
| IO-N3 | HIGH | cmd.c:1003-1007, :1059 | `open <id> json` builds the summary from the `.log`; after routine eviction (logger.c:1017 deletes only `.log`) it answers `open: no .log` although the never-evicted `.sum` is intact (§12.6 "timing results outlive sample logs") | io-v case A `ERROR code=0x0703 msg="open: no .log"`; fmt 4 on the same fs `SUCCESS` | yes — 30 s check on an evicted session (io-v §5.3) | latent |
| IO-N4 | HIGH | cmd.c:998 `s_meta.has_sf = 0`; exp.h:19 `layout` written by no production line | VBO never carries `[laptiming] Start`, comments line always `Venue <v> / ` (§14.1 "when a layout was locked"); `test_vbo_laptiming_section_when_sf_known` hand-sets `has_sf` | io-v case B `[Venue Killarney / ]`, `[laptiming] present: NO`; grep `has_sf|meta.layout` | yes — item 32's VBO (expected-as-shipped) | yes (every VBO export) |
| APP-N1 (= DOC D-8, doc rows 161/263, #127 M-1, #128 M-1) | HIGH | pipeline.c:553-575, :682-691, :600-612, :624-626; logger.c:401 | Every session opened by a mode rotation, a deferred rotation or the 60 s RETRY carries an empty VENUE name in its `.sum` for life; `list` prints `"venue":""` and exports carry a blank venue; `logger_set_venue` is called only at pipeline.c:727/:787/:1587 | app-v harness A T1b/T1c `logger_set_venue calls=0`; harness B `.sum` decodes `VENUE{id 1000, name ""}` | yes — items 48/64/69, whose criteria do not check it (§5) | latent |
| DRV-N1 | HIGH | CMakeLists.txt:173-184; tools/sign_release.sh:94, :101-106; build.sh:116-121 | `CFG_FW_VERSION`/`PROJECT_VER` come from `git describe` at CMake configure only; sign_release builds incrementally and never reconfigures (idf.py reconfigures only on a changed `-D`), so a signed release can carry a stale or `-dirty` version in banner, `esp_app_desc` and STATUS/DIAG `fw`; a configure-dep on `.git/HEAD` would not catch a new tag | drv-v §1 N1: repro `incremental after clean commit: v1.0.0-dirty <- embedded` vs `git describe now: v1.0.0-1-g7d1f478`; build.ninja regeneration deps contain no project `.git` path | yes — items 40, 70 | yes (release tooling) |
| TCI-T2 (+TCI-V-1, TCI-V-3) | HIGH | tools/lint/power_of_10.py:522, :574; export_serial.c:1286-1288, :1324-1334; docs/power-of-10-deviations.md rule-9 ¶ + PD-5; test_power_of_10.py:78-83 | `--enforce-fnptr` cannot see the 11 `esp_console` callbacks (`.func = func` stores a parameter); PD-5 is a dead register row; deviations.md and the P10 design say they "are caught"; a unit test pins the false rationale | tci-v §1 T2: `--register /nonexistent --paths components main` → 8 RULE-9 lines, `grep -c export_serial` = 0 | no | yes (the 11 callbacks ship) |
| TCI-T5 (+TCI-V-2) | HIGH (reviewer MEDIUM; verifier UPGRADED: live instances) | tools/spec_api_check.py:32, :167-178; spec:654-657 | Spec §5.2 says the blocks are "generated" by `--emit` and that hygiene fails when a block misses a header macro; the tool reports MISSING for prototypes only and skips 9 of 22 headers; 26 header macros are absent from 6 blocks with `0 drift(s)` | tci-v §1 T5: `--emit core/lap.h` prints 3 `#define`s, the block has 0; hm−sm diff lists the 26 macros | no | n/a (doc gate) |
| DC-FC-1 (= drv:H2 PARTIAL) | HIGH (FALSE-CLAIM, a gate that lies) | PR #113 body; test/test_sim_profile.c:91-103 | "The drag scenario is now a permanent test": `test_sim_profile` never compiles gps_sim.c; restoring the pre-R29 bug leaves it green | dc-v §1 FC-1: mutant `4 Tests 0 Failures`, `grep -c gps_sim …/link.txt` → 0; reviewer harness on the mutant `rejected(jump)=160` | yes — item 18 is the only gate | no (sim driver) |
| CE-N2 (+CE-F2, CE-V-3) | HIGH | drag.c:216-228; test/test_drag.c:55-66 | `drag_on_fix` writes the Doppler speed of the fix EPOCH into `v_est` at its ARRIVAL; every speed gate is late by the fix latency (30–120 ms on NEO-6M, §6.2) vs §6.6 "±5 ms"; the host suite uses zero latency everywhere | ce-v §1 N2: `0-100 5665/5695/5725/5755/5785 ms` at 0/30/60/90/120 ms latency | no (sim latency 0) | latent (needs Plan 8 GPS) |
| CE-N3 (+CE-F3) | HIGH | lap.c:213-224, :559, :714-724, :1117-1129; ui.c:598-605 | `lap_force_layout` on a RUNNING, locked engine moves `cand[]` but keeps `locked_layout`/`sec_p[]`/the exported layout id for the session (§10.5 "locks immediately") | ce-v §1 N3: `force(1) -> 0 ; n_cand=1 cand[0]=1 locked_layout=2`, `export.layout_id=2 forced=1` | no — one-layout sim venue; created venues' Reverse faces the other way (ce verify-repro-force-reverse) | latent (reachable from the `Layout:` menu once laps run) |
| CE-N4 (+CE-F7) | HIGH | lap.c:100-137, :176-211, :955, :968, :672-673 | Lap best, prev and the predictive reference survive a venue change (R25 cleared only §10.8); venue B's deltas compare against venue A's best | ce-v §1 N4: `lap_best() after 4 VALID laps at venue B = 26667 ms, lap_no 1`; no pipeline path calls `lap_reset` on a venue change (pipeline.c:711-715, :731-735) | no (item 29 cannot see it, §5) | latent (Plan 8) |
| CE-N5 (+CE-V-1 spec §9.3) | HIGH | fus.c:273-284; drag.c:479-487, :542-547; #93 | Production fusion is never oriented (`fus_calib_orient_capture` has no app caller), so drag runs on the body x/y magnitude incl. gravity: it never arms above 4.06° mount pitch, and at 3.5° t0 is back-dated 1.17 s | ce-v §1 N5: `pitch 4.0 deg: armed=yes`, `5.0 deg: g_lon=0.087 armed=NEVER`; PART2 `t0 early by 1170 ms` | no (imu_sim upright, imu_sim.c:85) | latent (needs a real IMU) |

### 4.2 MEDIUM

| id (merged) | sev | file:line | defect | evidence | bench-observable? | release target |
|---|---|---|---|---|---|---|
| CE-N1 (+CE-F4) | MEDIUM (reviewer HIGH; verifier DOWNGRADED: bounded by the corner cut, conforms to amended §6.4) | lap.c:30, :1172 | A chord of ABSENT (undelivered) fixes shorter than 5 s still evaluates crossings: 2.0–5.0 s gaps give +159..+165 ms VALID laps and a false best | ce-v §1 N1 sweep `D=9..24 … +159..+165 ms VALID`; pipeline.c:1629 feeds only `gps_poll` | no | latent (Plan 8 frame loss) |
| CE-N6 (+CE-F1) | MEDIUM | drag.c:594-605; spec §11.2 Abort ¶ | One low Doppler sample within 2 s of t0 aborts a genuine launch (`v_est` overwritten by every fix); ordinary latency produces ARMED/LAUNCH noise in the log | ce-v §1 N6 case B `t=4.160 ARMED` then `t=4.270 LAUNCH` | no | latent (Plan 8) |
| CE-V-2 | MEDIUM | pipeline.c:711-715, :731-735; ui.c:1265-1310, :1013-1016 | A scan-driven venue change resets neither the pipeline's `s_best` nor the ui's BEST/sector board | ce-v §4 V-2 (code reading) | no | latent (Plan 8) |
| IO-N5 | MEDIUM | ses_records.c:117, :133; exp.c:62 | `gps_us` guard is `<= 0` only: a positive garbage KEY exports a well-formed, valid-checksum NMEA line with a fabricated date; INT64_MAX is signed overflow (UB) at exp.c:62 under CI's UBSan flags | io-v §1 N5: `$GPRMC,070640.00,A,…,151224,,,A*79`; `exp.c:62:51: runtime error: signed integer overflow` | no | latent |
| IO-N6 | MEDIUM | json.c:124-134; cfg_json.c:109; jw.c:62-81 | A `ble.name` containing `"` or `\` grows on every GET→SET round trip until SET rejects the device's own output | io-v §1 N6: round 2 `rc=-1 err=[bad value for ble.name]` | yes (`lt config set`), no item uses it | yes |
| IO-N8 | MEDIUM (magnitude UNVERIFIED) | logger.c:936-956; ses_records.c:157-163 | The logger drains every fix before any fused sample: a stalled drain clamps older FUSED `dt` to 0 and the `.log` order makes VBO sample-and-hold lag | io-v §1 N8 (code reading; app-layer did not report it) | sim-only, with tools/replay | latent |
| APP-N2 (+APP-V-3, spec:2318) | MEDIUM | ota.c (ota_precheck, ota_close_session, ota_fail/ota_poll/ota_abort); lt_ipc.c:88-104; ota.c:231 | A close that times out (-2) still executes later, so a REFUSED push can end the session; after any post-close OTA failure nothing reopens the session (DROP), and END.reason says RESTART | app-v §1 N2: harness T2 `SES_LIVE_DROP -> mirror dropped`, `SET_MODE + 61 s: 0 log_req(s)` | yes — items 34(b), 43(a), 63 on moto_sim | latent (no session on neo6m) |
| APP-N3 (#128 M-2) | MEDIUM | pipeline.c:651-691; session_policy.c | A pending CLOSE plus a mode flip plus a same-venue re-find loses the mode rotation (session HDR mode ≠ engine mode) | app-v harness A T3a-d | no (needs a 100 ms full queue) | latent |
| APP-N4 | MEDIUM | logger.c:1124-1130 | The 9th request dequeued in one drain is dropped silently; a dropped CLOSE leaves the session open and liveness unconfirmable, re-enabling an open during OTA | app-v harness B T4 `CLOSE (9th) dropped … at_open 1 != sent 2` | no | latent |
| APP-N5 | MEDIUM | pipeline.c:248-249, :948-958; logger.c drain_events | The §12.6 pause is a latch driven by EV_MOTION edges sent drop-newest (0 ticks, 16 deep); one dropped edge keeps FIX/FUSED suppressed while riding | app-v harness B T5a-c | no | latent |
| APP-N6 | MEDIUM | logger.c:853, :895 | LOG_RES_VENUE arriving before its OPEN is discarded (TIME_MAP/CALIB are stashed), so a race blanks the name | app-v harness B T6 `VENUE dropped (s_venue_name "")` | race-dependent | latent |
| APP-N7 (= #114, CE-H3 PARTIAL) | MEDIUM | pipeline.c:1240-1246 | `CMD_SET_LAYOUT` discards `lap_force_layout`'s −1 and clears `s_best` unconditionally (also on refusal and on a same-id no-op) | app-v harness A T7 `rc=-1 … s_best.theo_ms after handle_cmd=0` | only via the ui's own ids (item 28 cannot run, §5) | latent |
| APP-N8 (= #104) | MEDIUM | logger.c:218-228, :1074-1083 | Append path has no reserve check; one spurious `free_kb == 0` reading evicts 8 sessions' `.log` | app-v harness B T8a/T8b `unlinked 8` | the #104 bench symptom; no item | yes (storage is real on neo6m) |
| APP-N11 | MEDIUM (reviewer LOW; verifier raised) | logger.c submit(); lt_ipc.h:122 | `xQueueSend(g_result_q, r, 0)` on a 4-deep queue drops a LAP result with no errlog or counter — the only carrier of the full LAP record into `.log` and `.sum` | app-v harness B T11 `no errlog, nothing queued` | no | latent |
| DRV-N2 | MEDIUM | CMakeLists.txt:78-83; ota.c:199-220, :303; sign_release.sh:106 | The panel is in neither the hwid nor the dist name: a ws29v2 image passes the OTA hwid check on a ws213v4 device, and signing both panels at one version overwrites the first | drv-v §1 N2: both `build_config.h` read `CFG_HWID "moto_sim_epaper"` | yes (wrong geometry on glass) | yes |
| DRV-N3 (= doc row 65; FALSE-CLAIM spec:409, export_serial.c:594-597) | MEDIUM | export_serial.c:598-1253 (`#if CFG_HAS_DEVUX`), :1335-1338 | A DEVUX=OFF image has no OTA receive path at all; spec:409 and the code comment say it keeps one | drv-v §1 N3: `xtensa-esp32-elf-gcc -E` with `CFG_HAS_DEVUX 0` → 0 `cmd_ota`; doc-v §1a row 65 | no | no (every build dir has `DEVUX:STRING=ON`; latent trap for a prod-slim image) |
| DRV-N4 (= doc row 53) | MEDIUM | sdkconfig.defaults:17; build/moto_neo6m/sdkconfig:1006, :1014 | Brownout level is the IDF default `LVL_SEL_0` (≈2.43 V, Kconfig.system:23-24), not spec §17.1's 2.8 V (`LVL_SEL_7`, Kconfig.system:37-38); SPI NOR is specified ≥ 2.7 V | drv-v §1 N4 | no (USB-powered bench) | yes |
| TCI-T1 (+FALSE-CLAIM PR #138) | MEDIUM (reviewer HIGH; verifier DOWNGRADED: the linter's own texts say "prints") | power_of_10.py:1016-1021; test_power_of_10.py:134-139 | A stale grace triple only prints `GRACE-STALE` (rc 0), so a fixed site's grace can silently re-grace a later regression in that function | tci-v §1 T1: two stale triples → rc 0 | no | n/a (CI) |
| TCI-T7 | MEDIUM | ci.yml:84-96, :108-122, :193-197, :229-239; firmware.yml:22-29, :54, :68-70 | Each required job probes for its input file and, when absent, runs only `echo "Nothing to do"`: deleting test/CMakeLists.txt (or the dc / tracks / build inputs) turns the required check green with nothing run | tci-v §1 T7 (YAML read) | no | n/a (CI) |
| DC-N2 (+FALSE-CLAIM cmd_lt.c:74-77) | MEDIUM | devconsole/cmd_lt.c:504-529 | The relay refreshes its idle deadline on any UART byte, and the wired lap-timer streams a FUSED frame every 100 ms, so every relayed `lt <verb>` runs to the 15 s cap (the dk:M1 bug class again) | dc-v §1 DC-N2 (cmd_lt.c:514; pipeline.c:1029-1052; the code's own comment :523-529) | yes — item 20 (`rt ≈ 15000 ms`) | no (dev-kit) |
| DC-N3 | MEDIUM | cmd_lt.c:518, :530; tools/devkit.py:373, :464 | Relay rc is 0 whenever any byte arrived — and the lap-timer echoes every input byte — so `lt nosuchverb` and `ERR 0x…` replies return rc 0; with `--json` (always added by devkit.py) the body is cut at 512 B with rc 0 (tracks ≈1.1 KB, errlog ≥ 10 entries, coredump) | dc-v §1 DC-N3 (linenoise.c:1156 echo; bridge_filter.c:41-47) | yes — item 20 would pass on rc 0 (§5) | no (dev-kit) |
| DC-V-3 | MEDIUM | cmd_lt.c:117-120; devkit.py:373, :464 | `devkit.py lt config get` returns a 512 B prefix of the 641 B config JSON with rc 0 and exit 0 | dc-v §4 V-3: `cfg_to_json(defaults) = 641 bytes` (devcontroller-link-sim-verify-repro-cfgsz.c) | yes (any scripted `lt config get`) | no (dev-kit) |
| DC-N4 | MEDIUM | tools/devkit.py:84, :462; cmd_lt.c:356-369 | One 20 s wait for every `lt` verb, while a download may legitimately run up to 12 s + 20 min; any `.log` above ≈170 KB raw (≈7 min riding) times out and its late JSON line poisons the next command | dc-v §1 DC-N4 (arithmetic over cited constants) | yes — item 24(a) via devkit.py | no (host tool) |
| DC-N7 | MEDIUM for bench day (reviewer LOW; verifier raised) | .superpowers/bench-helpers/gate_dsA.py:25, gate_dsB.py:31, gate_p07c.py:24, gate_p07b.py:14, t9_inject.py:13 (git-ignored) | Helpers open the lap-timer port with pyserial's default DTR/RTS asserted (RTS reaches EN, per the helpers' own reset recipe), and gate_dsA.py:9-11 tells the operator to run its `closed` phase right after an OTA push — inside the 35 s trial window → rollback 0x0806 | dc-v §1 DC-N7 (pyserial 3.5 `_dtr_state = True`, `_rts_state = True`) | HAZARD on bench day | no (host tool) |

### 4.3 LOW

| id (merged) | file:line | defect | evidence | bench-observable? | release target |
|---|---|---|---|---|---|
| CE-N7 | lap.c:139-146, :351 | `lap_init` leaves `sec_next = 0`; `lap_export_rtc` asserts and leaves `*out` unwritten | ce-v §1 N7 `venue_id=0xABAB`; unreachable from pipeline.c:698/706 | no | latent |
| CE-N8 (#116) | drag.c:29; lap.c:64, :136, :411; lap_cfg.c:38; trk.c:350, :417; trk_json.c:127 | tautological / literal-only assertions (rule 5) | ce-v §1 N8 | no | yes |
| CE-N9 | trk_json.c:78 | asserts `n > 0` on caller input | ce-v §1 N9 (callers pre-filter: logger.c:679) | no | latent |
| CE-V-3 | test/test_drag.c:55-66 | every drag test has `mono_us == gps_us` (cannot see CE-N2) | ce-v §4 V-3 | no | n/a |
| CE-#118 (+CE-F6) | lap.c:423, :1125 | resume with layout 0 / lap 0 never scores disambiguation (`n_ugate=0`), blind tie-break | ce-v §1 F6 case B; no product snapshot has layout 0 | no | latent |
| CE-#119 | lap.c:473-504 | 10 two-level `L->pred_*[...]` dereferences, no deviation row | ce-v §1 #119 | no | yes |
| CE-#11 | tb.c:81-85 | PPS re-acquisition stalls 25 s behind one glitch | ce-v §1 #11; `has_pps = 0` on every shipped target | no | no |
| IO-N9 | json.c:106 | `strtod` accepts C hex floats (`0x1p3`) | io-v §1 N9 | no | yes |
| IO-N10 | cfg.c:42-96; lap_cfg.c:33-36 | `min_lap_s >= max_lap_s` accepted by `cfg_validate`, silently replaced by defaults in the engine | io-v §1 N10 `corrections=0` | yes (item 26 caveat) | yes |
| IO-N11 | render.c:171-185; screens_moto.c:822 | text walk bounded by 32, source field is `char label[8]` | io-v §1 N11 | no | latent |
| IO-N15 | units.c:11-14, :32-36 | assertions their own comments call unreachable | io-v §1 N15 | no | yes |
| IO-N17 | exp_json.c:109 | a LAP after runs began is dropped uncounted | io-v §1 N17 | no | latent |
| IO-N18 | jw.c:75-77 | non-UTF-8 bytes copied raw into JSON | io-v §1 N18 | no | yes |
| IO-V1 | cmd.c:683-685 | LIST never emits a layout name (spec §14.3 example has one) | io-v §4 V1 | yes | yes |
| IO-V4 | cmd.c:991 | missing `.sum` ignored: export succeeds with blank header and 1970 date | io-v §4 V4 (reading) | no | latent |
| APP-N9 (= #105) | logger.c rebuild_sum | orphan `<id>.sum.tmp` never reaped | app-v §1 N9 | no | yes |
| APP-N10 (#108 residual) | pipeline.c:445-450; logger.c:712-719 | dropped SAVE_TRACKS / ui notify leave the success card; no test of NOT SAVED | app-v §1 N10 | no | yes |
| APP-N12 (#128) | pipeline.c:600-612 | pending rotation of an unconfirmed session + OTA close → open mid-OTA | app-v harness A T12a-d | no | latent |
| APP-N13 | link.c:223-224, :234 | asserts at task entry return from the task (UB); in-loop tautology | app-v §1 N13 | no | yes |
| APP-V-1 | lt_ipc.h:137 `name[24]` vs trk.h:33 `name[32]` | venue names truncated to 23 chars in `.sum`/list/export | app-v §4 V-1 | no (bundled names ≤ 14) | yes |
| DRV-N7 (+FALSE-CLAIM sign_release.sh:39-42) | components/app/CMakeLists.txt:8-10; components/core/CMakeLists.txt:4-6 | untracked `.c` under app/core is compiled and invisible to the dirty-tree guard | drv-v §1 N7 | no | yes (release tooling) |
| DRV-N8 | sign_release.sh:57-60, :84-88 | `--dry-run` passes for envs build.sh refuses; `GPS=`/`IMU=` checks guard variables build.sh never reads | drv-v §1 N8 | no | n/a |
| DRV-N10 (= L16 NOT FIXED) | storage_internal.c:259-284 | `readdir` NULL never errno-checked; listing errors end as success | drv-v §1 N10 | no | yes |
| DRV-N12 | board.c:332-333 | `ESP_ERROR_CHECK` in ISR enable aborts instead of reporting | drv-v §1 N12 | no | yes |
| DRV-N16 | build.sh:116-121; CMakeLists.txt:136 | CHRG is not forced per build; a one-off `-DCHRG=ON` persists into a signed image | drv-v §1 N16 | no | yes (tooling) |
| DRV-N17 | tools/dist_prune.sh:14-19 | substring match keeps `-dirty` images as current | drv-v §1 N17 | no | n/a |
| DRV-N18 | export_serial.c:751 | `read_through_ses` `for (;;)` without an iteration cap | drv-v §1 N18 | no | yes |
| DRV-V1 | export_serial.c:751-760 | a mid-file `sto_read` error ends `dbg sum`/`dbg logck` with a partial tally and rc 0 | drv-v §4 V1 | yes (item 41 reads `EVENT=`) | yes |
| DC-N5 (= DRV-V2; +FALSE-CLAIM gps_sim.c:39-41 per drv-v) | gps_sim.c:201-209, :308-349 | `dbg sim laps` after a drag/park teleports to the capture start; ≤ 5.5 s (27 fixes) rejected by the §6.5 jump filter | dc-v §1 DC-N5 `rejected(jump)=27 max_step=1405.6 m`; drv-v §4 V2 | yes (drag→laps order only) | no |
| DC-N1 | linkhost_proto.c:998, :1051-1058; linkhost.c:545-556 | a 0xFF frame glued to the `OTA-READY` line fails the flash (fails safe, ≈0.1 %/push) | dc-v §1 DC-N1 (verifier DOWNGRADED from MEDIUM) | rarely | no |
| DC-N6 | export_serial.c:1010-1012; pipeline.c:1227-1231 | `dbg sim` on a non-sim build replies "posted" rc 0 | dc-v §1 DC-N6 | yes (moto_neo6m) | yes |
| DC-N8 | cmd_lt.c:454-472 | token-initial quotes stripped from relayed `tracks put` (at parity with the raw console, export_serial.c:499-513) | dc-v §1 DC-N8 | yes | yes (raw console) |
| TCI-T3 (+FALSE-CLAIM P10 design "un-evadable") | power_of_10.py:210, :498 | rules 3/4/5 evadable by spellings with 0 live instances | tci-v §1 T3 | no | n/a |
| TCI-T4 | power_of_10.py:505, :522, :588 | 7 of 9 fn-ptr spellings and mutual recursion unseen; 0 live instances | tci-v §1 T4 | no | n/a |
| TCI-T6 | venue_announce.py:160-205 | 5 of 6 bypass shapes pass; 0 live | tci-v §1 T6 | no | n/a |
| TCI-T8 | test/CMakeLists.txt:97-100 | 44 host binaries cannot observe an assertion trip (21 deliberate-trip tests assert only the fallback) | tci-v §1 T8: 25 trips in 12 binaries | no | n/a |
| TCI-T9 | test_replay_fixtures.c:39-40 | `gps_dropout`/`truncated` fixtures pin engine output, not truth | tci-v §1 T9 | no | n/a |
| TCI-T10 | test/data/sim_capture.expected.json | nothing reads it | tci-v §1 T10 | no | n/a |
| TCI-T11 | test_ui.c:218-231 | two tests with no assertion (rely on ASan) | tci-v §1 T11 | no | n/a |
| TCI-T12 | ci.yml:245 | coverage job green even if its ctest fails | tci-v §1 T12 | no | n/a |
| TCI-T13 | ci.yml:138-146 | tools-tests list hard-coded | tci-v §1 T13 | no | n/a |
| TCI-T15 | firmware.yml:47 | exclusion-justification check accepts any comment naming the test | tci-v §1 T15 | no | n/a |

## 5. Bench-day truth table (docs/bench/next-bench-day.md, open items F0/#102 and 18–71; F4-2 = item 71)

Verdicts: RUNNABLE AS WRITTEN / NEEDS REWRITE (what) / CANNOT RUN (why) / WOULD FALSE-PASS (why) / HAZARD. Every item that runs on
moto_neo6m is listed as such; none of the F1a engine items (26–29) and none of the session-lifecycle items (46–48, 57–58, 64–69) can
run on moto_neo6m until Plan 8 provides fixes (doc §5; gps_neo6m.c:34-38).

| item | verdict | what / why | evidence |
|---|---|---|---|
| Standing rules (bench doc :10-12) + bench helpers | HAZARD | Never open the lap-timer port for 35 s after a push. The git-ignored helpers open it with DTR/RTS asserted, and gate_dsA.py's `closed` phase is documented to run right after a push → rollback 0x0806 (BENCH-H1 = DC-N7) | dc-v §1 DC-N7 |
| F0/#102 DRAG card on a silent transition | not examined | no reviewer or verifier tested this item's path on 7265059 | — |
| 18 drag run, no EV_FIX_LOST | RUNNABLE AS WRITTEN (moto_sim) | R29 holds on the real driver (`max_step_drag=12.40 m`, 0 violations); run in the written order laps → drag → park: drag → laps shows a ≤ 5.5 s rejected-fix burst by design (DC-N5), not an R29 regression; this item is the only gate on R29 (DC-FC-1) | drv-v §2 H2; dc-v §1 DC-N5, FC-1 |
| 19 AP PSK | RUNNABLE AS WRITTEN | — | dc §5; dc-v §2 dk:H1 |
| 20 `lt errlog` relay | WOULD FALSE-PASS | "returns the same body as JSON with rc 0": rc is 0 whenever the echo arrived (DC-N3); the body holds the echoed command and prompt and is cut at 512 B (errlog ≥ 10 entries ≈ 1.7 KB); every relayed verb takes ≈ 15 s (DC-N2). Run before item 25's crash-loop phases or read errlog in the human form | dc-v §1 DC-N2, DC-N3; dc-v §3 |
| 21 Logs tab / 22 `logging` false | RUNNABLE AS WRITTEN | — | dc §5 |
| 23 paced USB OTA | RUNNABLE AS WRITTEN | dev-kit held in reset ⇒ no stream frames ⇒ DC-N1 cannot occur | dc §5; drv §5 |
| 24 linkhost glue | NEEDS REWRITE | (c) the prompt is `devkit> ` (console.c:60), not `dc>`; (a) via `devkit.py lt open … log` any session > ≈7 min times out at 20 s and poisons the next command (DC-N4) — use the raw console as written | dc-v §5 |
| 25 session-2 console | NEEDS REWRITE | (b) `devkit.py flash` never shows the device's own `flash result`/note line (push_and_wait prints progress only); check "exactly ONE further line" on a raw terminal | dc §5; dc-v §5 |
| 26 `cfg.lap.*` reaches the engine | NEEDS REWRITE | first half: the laps do not simply stop — the S/F re-arms 120 s after the last crossing, so one ~133 s VALID lap completes ~130 s after `min_lap_s=120`, then 106.7 s and 26.7 s laps after the revert (ce-v executed this; doc-v's "all laps < 120 s" reading omits the re-arm — decided by ce-v's run); second half (PIT): CANNOT RUN — after `dbg sim park` the sim never reaches the S/F again; the reviewer's "PIT on every lap" mechanism is REFUTED (no capture fix is below 71.6 km/h); a rewrite "keep `dbg sim laps 10` running; the lap containing each wrap standstill carries PIT only after the set" discriminates; caveat IO-N10 (`min_lap_s >= max_lap_s` accepted, silently replaced) | ce-v §5 + core-engines-verify-repro-item26.c; doc-v §1b item 26 (F-a: SIM_FIXES min 19,880 mm/s); io-v §5.5 |
| 27 leaving the venue closes the session | NEEDS REWRITE (name the verbs) | runnable: ≥ 5 back-to-back `dbg sim drag` runs walk the sim 5 × 690 m north (each step ≤ 12.4 m), past 1.5 × the 2,000 m venue radius for VENUE_LEAVE_S 60 s → EV_VENUE_LOST; the item omits the sequence (reviewer's "unexecutable" REFUTED by doc-v); a later `dbg sim laps` then teleports back with a ≤ 5.5 s rejected burst (DC-N5) | doc-v §1b item 27 (gps_sim.c:187-192, :370-395; sim_capture.h:643; consts.h:35-36; lap.c:891-896) |
| 28 unknown layout override | CANNOT RUN | the only `CMD_SET_LAYOUT` producer is ui.c:599 (the venue's own ids or 0); no console, dev-kit or wire verb posts it; R25's −1 path is host-test-only (`test_force_layout_unknown_id_is_refused`) | doc-v §1b item 28; io-v §1 N7 (dbg verb list) |
| 29 best sectors across venues | NEEDS REWRITE | the "blank" half is non-discriminating (CREATE finish → `reset_best_and_stats`, pipeline.c:438-442, clears `s_best` regardless of R25); the "rebuild from the new venue's own splits" half discriminates (pipeline.c:331-345 copies lap.c's sector minima) — make the operator compare against the new splits; blind to the lap-best leak CE-N4 (same sim loop, `arg32b ≈ 0`) | doc-v §1b item 29; ce-v §5 |
| 30 fault glyphs | NEEDS REWRITE | `dbg flag set 1` cannot show the hollow GPS pin: ui.c:1550-1554 rewrites bit 1 from `s_fix_lost` every update (read by this synthesis; io reviewer had rated it observable — decided by these lines); bits 2/5/13 map as described | ui.c:1550-1554; doc row 146 |
| 31 partial refresh | RUNNABLE AS WRITTEN | — | io §5 |
| 32 export after exporter changes | NEEDS REWRITE (expectations) | blind to IO-N1/N2 (undamaged sim log); the VBO will read `Venue <name> / ` with no `[laptiming]` on both builds (IO-N4) — record as expected; use a session whose `.log` still exists or `open <id> json` fails (IO-N3) | io-v §5.2 |
| 33 damaged `.log` export | CANNOT RUN | nothing on the device or in tools/ writes a session file (dbg verb list; no write op 0x01-0x23); if it could, CRC-level damage passes as SUCCESS (IO-N1/N2), i.e. a false pass. Replace with the host harness (io-verify-repro-cmd.c cases C-F) | io-v §1 N7, §5.1; doc row 148 |
| 34 OTA preconditions on moto_sim | RUNNABLE AS WRITTEN | add: after (b) a failed or refused-at-close push logging stays off until a venue event or `dbg reset` (APP-N2) | app-v §1 N2, §5.3 |
| 35 stack high-water | RUNNABLE AS WRITTEN | — | doc row 150 |
| 36 logger WDT on a > 5 s compaction | RUNNABLE AS WRITTEN (record-only) | no injection exists | app §4; doc §5 |
| 37 buttons debounce / IRAM ISR | RUNNABLE AS WRITTEN | ISR chain in IRAM on both ELFs | drv-v §2 C1; doc row 151 |
| 38 moto_neo6m stubs tell the truth | RUNNABLE AS WRITTEN (moto_neo6m) | — | drv-v §2 H3 |
| 39 transfers survive a log storm | WOULD FALSE-PASS | the storm the item relies on (a below-reserve `ESP_LOGE` at ~1 Hz on a full fs) does not exist: logger.c's only such lines are once per OPEN attempt (logger.c:376, ≤ 1/60 s) and tracks_save (:604), and at FULL samples are suppressed (logger.c:932) so storage_internal.c:172 never fires — transfers pass with or without R9's `ESP_LOG_NONE` (doc-v V-2; drv reviewer had rated it runnable — decided by doc-v's grep of every ESP_LOGE site) | doc-v §5 V-2 |
| 40 OTA validates on the production build | NEEDS REWRITE | two images signed from one tree carry the same `git describe`, the same `CFG_FW_VERSION` and the same dist path (the second overwrites the first); with DRV-N1 even a commit does not change the embedded version without a reconfigure — "the new version in the banner" is unobservable. Require a commit + fresh configure between the signs and record both versions (BENCH-40 = DRV-N5 = DOC D-1) | drv-v §1 N5, N1; doc row 152 |
| 41 EV_FAULT both consumers | RUNNABLE AS WRITTEN (moto_sim), LOW caveats | bit-10 "EVENT rises by exactly 0" is +2 when the inject lands between check_heap and emit_faults of one tick (sup.c); `dbg logck` hides a mid-file read error (DRV-V1) | doc-v §1b item 41; drv-v §4 V1; app-v §1 N20 |
| 42 / 60 task-WDT lines | RUNNABLE AS WRITTEN | item 42's ui sentence is superseded by 60 | app §5; doc row 155 |
| 43 OTA receive timeout | NEEDS REWRITE | (a) Ctrl-C of `devkit.py flash` either aborts the host→dev-kit stage (no OTA_BEGIN, no timeout) or leaves the dev-kit's own `flashctl_push` task streaming to completion (flashctl.c:129-144, :187) — use `tools/ota_push.py` over USB + Ctrl-C, or reset the dev-kit mid-push (DC-V1 = doc-v V-3; whether a dev-kit reset on host-port close stops it is UNVERIFIED, hardware settles it); add "logging does not resume" (APP-N2) | dc-v §4 V-1; doc-v §5 V-3; app-v §5.3 |
| 44 RTC reads not torn / 61 `partial_count` | RUNNABLE AS WRITTEN | — | doc row 157 |
| 45 coredump row + heap walk | RUNNABLE AS WRITTEN | — | doc row 158 |
| 46 still-pause | RUNNABLE AS WRITTEN (moto_sim) | "first record after the pause is a key frame" is not observable with `dbg logck` (counts only); the evidence list omits it | app-v §2 M6; doc-v §1b item 46 |
| 47 TIME_MAP + CALIB | NEEDS REWRITE | accept CALIB ≤ 1: CALIB=0 is a legitimate outcome of the race documented at pipeline.c:566-572 | doc-v §1b item 47 |
| 48 rotation on a mode change | WOULD FALSE-PASS | passes on `"mode"` while every rotated session's `.sum` VENUE name is blank (APP-N1); add "`list` shows the venue name on the newest session" | app-v §1 N1, §5.1; doc row 161 |
| 49 neo6m opens no session | NEEDS REWRITE | `(empty)` holds only on a freshly formatted fs (OTA keeps the storage partition), and "errlog holds no 0x04xx" false-fails on a board that ran items 39/54 (the ring persists in NVS) — add an erase or `errlog clear` first | doc-v §1b item 49, §5 V-4 |
| 50 tracks round trip | WOULD FALSE-PASS | "after `dbg reset` it is STILL listed": `dbg reset` posts `CMD_RESET_ENGINE` only (export_serial.c:980-990) and `/tracks/user.bin` is reloaded only at boot (app_main.c:284), so RAM alone passes it; use `dbg crash` or a power cycle (BENCH-50 = APP-N21 = DOC D-6) | app-v §1 N21; doc row 164 |
| 51 coredump | RUNNABLE AS WRITTEN | — | doc row 165 |
| 52 menu lock at 10 km/h | WOULD FALSE-PASS | "while the sim is between 3 and 10 km/h (the capture's slowest corner)" is false: every capture fix is ≥ 71.6 km/h except the 0 km/h wrap/park, so any `MENU_LOCK_SPEED_KMH` in 1..71 passes identically; the 10 km/h value is host-pinned only (app reviewer had rated it runnable — decided by doc-v's scan of the 601 SIM_FIXES rows) | doc-v §5 V-1 |
| 53 cfg lock / only-on-change | RUNNABLE AS WRITTEN | — | doc row 167 |
| 54 NOT SAVED card | CANNOT RUN (code-derived, not executed) | creating the track rotates the logtest-filled session first (pipeline.c:713 before :714) and the rotation's OPEN runs eviction_check, which may evict that just-closed `.log` to 15 % before SAVE_TRACKS drains → save succeeds, no card; and the `dbg logtest` fill is suppressed once the sim parks (APP-N22) — at minimum run `dbg sim laps 1000` first; a bench run settles the eviction half (BENCH-54 = doc-v V-5 + APP-N22) | doc-v §5 V-5; app-v §1 N22 |
| 55 resumed layout | CANNOT RUN (moto_sim) | the resume branch needs `age > 0` (pipeline.c:808-809) and gps_sim re-anchors its clock to `SIM_FIXES[0]` every boot (gps_sim.c:134-146) → the snapshot is always cleared; #103 OPEN; moto_neo6m has no fix source | doc-v §1b item 55 |
| 56 ui stack both paths | NEEDS REWRITE (minor) | `dbg sim drag` alone never reaches `drag_rows_refill`: EV_DRAG_* exist only in DRAG mode (pipeline.c:903); prefix `config set {"mode":"drag"}` | doc-v §5 V-8 |
| 57 mid-lap reboot opens a NEW session | CANNOT RUN (moto_sim) | asks moto_sim for `rtc resume … continued`, which the same freshness gate makes impossible (as item 55; bench doc :153-162 says so for item 17); app reviewer had rated it runnable — decided by doc-v and the bench doc's own item-17 analysis | doc-v §1b item 57; bench doc:153-162 |
| 58 refused OPEN retried | CANNOT RUN | `dbg reset` is not a reboot (export_serial.c:980-990); app-v: with the boot session open a same-venue re-find posts no OPEN (harness A T3c); doc-v: even when an OPEN does come, every close makes the just-closed `.log` evictable before the refusal check (logger.c:368-370, :479), and item 54 leaves no full fs — both mechanisms end in "cannot run"; needs a real reboot into a full fs with no closed `.log` left (BENCH-58, part of APP-N21 / DOC D-6) | app-v §1 N21; doc-v §1b item 58 |
| 59 SAFE MODE persistent | NEEDS REWRITE (minor) | "only a reboot leaves it" is false after SAFE_MODE_CLEAR_S = 600 s uptime (sup.c:275-286); three `dbg crash` count toward the crash loop by intent | doc-v §5 V-7 |
| 62 `dbg logtest` producer | RUNNABLE AS WRITTEN (moto_neo6m) | — | app-v §5.6; doc row 172 |
| 63 OTA tokens one CRLF | RUNNABLE AS WRITTEN | — | doc row 173 |
| 64 `list` newest 48 | RUNNABLE AS WRITTEN for the 48 cap; blind to APP-N1 (rotated sessions list `"venue":""`) | — | doc row 174; app-v §5.1 |
| 65 OTA does not reopen a session | RUNNABLE AS WRITTEN (success path only; APP-N4/N12 not exercised) | — | app-v §5.5 |
| 66 new track rotates | RUNNABLE AS WRITTEN | the new session's name appears only once its layout locks | app §5 |
| 67 `tracks put` existing id refused | RUNNABLE AS WRITTEN | — | doc row 164 (strings exist, cmd.c:873-877) |
| 68 TIME_MAP consistent pair | RUNNABLE AS WRITTEN (limited, self-declared) | — | doc §5 |
| 69 rotation never desyncs | WOULD FALSE-PASS w.r.t. the venue name (APP-N1), as item 48 | — | app-v §5.1; doc row 161 |
| 70 signed release path | NEEDS REWRITE | runs as written but cannot catch DRV-N1; add "banner / `status` fw == `git describe`" | drv-v §5 |
| 71 (F4-2) BT controller off | RUNNABLE AS WRITTEN | `# CONFIG_BT_ENABLED is not set`; the 2026-09-17 heap reference is direction-robust only because R33 frees ≈56 KB | drv-v §2; doc-v §1b item 71 |
| 12 / 13 / 15 (open boxes) | NEEDS REWRITE (tick) | the same file records them PASS on bench day 5 (bench doc:581-593) while the boxes stay open (reviewer row, not re-sampled by doc-v) | doc row 135 |

## 6. Gaps that need a decision — triage proposal

Bucket rules as applied: **A** = a code (or bench-helper script) change is needed before bench day because a bench item depends on
it, it is a bench hazard, or a gate reports success with nothing checked; **B** = text only (bench doc, spec, roadmap, README, FW, P10
documents) before bench day; **C** = hardware-independent code/test/record work deferred to F5 with an issue; **D** = deferred to the plan
that brings the hardware (Plan 8 GPS/IMU, Plan 6 power), because the trigger or magnitude exists only with that hardware. Every
§3/§4/§5 finding is in exactly one bucket; a false claim that is the text of a §4 defect travels with that defect (§3 "→" column).
Counts: **A 8 · B 66 · C 89 · D 6** (= 169 findings: 14 HIGH, 44 MEDIUM, 111 LOW).

### A — fix in code before bench day (8)

| # | id | sev | one-line reason | issue |
|---|---|---|---|---|
| A1 | DC-N7 | MEDIUM | bench HAZARD: helpers open the LT port with DTR/RTS asserted and gate_dsA.py's `closed` phase is told to run right after a push → OTA rollback 0x0806; set `dtr=rts=False` before `open()` as devkit.py:434-437 does, take ports from argv | — |
| A2 | DRV-N1 | HIGH | items 40 and 70 need the signed image to carry the real `git describe`; force a reconfigure in sign_release and refuse when `CFG_FW_VERSION` ≠ `git describe --dirty` (pinnable in test_sign_release.sh) | — |
| A3 | APP-N1 (+P7, P11, doc rows 161/263) | HIGH | items 48/64/69 otherwise certify blank-venue sessions; one `logger_set_venue` inside `session_open` (PR #127's own "fix before merge") | #128 |
| A4 | DC-N3 | MEDIUM | item 20 false-passes on rc 0; derive rc from a parsed `ERR 0x`/`---END` and flag truncation | — |
| A5 | DC-V-3 | MEDIUM | same truncation path (cmd_lt.c:117-120) on `lt config get`, which bench scripts read; fix with A4 | — |
| A6 | DC-N2 | MEDIUM | every relayed bench verb takes 15 s while wired; refresh `idle` only when `w > 0` | — |
| A7 | DC-FC-1 (+P1) | HIGH | the R29 "permanent test" does not exist and item 18 is the only gate; commit the gps_sim host harness (devcontroller-link-sim-repro-gpssim.c / drivers-build-repro-drag.c) as a test | — |
| A8 | TCI-T7 | MEDIUM | a required check goes green with nothing run if its input file is deleted; remove the `present=false` scaffolding before the pre-bench fixes merge under it | — |

### B — fix text before bench day, no code (66: 29 bench rows + 26 spec rows + 8 roadmap/README/FW/P10 rows + DRV-N3, TCI-T2, TCI-T5)

- Bench doc (29, §5/§3.2): item 50 (HIGH — use `dbg crash` or a power cycle); items 20 (rc/echo/512 B wording), 26 (first-half
  criterion; PIT half rewrite), 27 (name the 5× `dbg sim drag` sequence), 28 (mark host-only), 29 (compare against new splits),
  30 (inject NOFIX by a fix loss, not `dbg flag set 1`), 32 (expect no `[laptiming]`, blank layout, `.log` present), 33 (replace by
  the io-verify-repro-cmd.c host harness), 39 (no storm source — add a storm verb or drop the R9 claim), 40 (commit + fresh configure
  between the two signs; record both versions), 41 (bit 10 "0, rarely 2"), 43 (`ota_push.py` + Ctrl-C; "logging does not resume"),
  47 (CALIB ≤ 1), 48/64/69 (add the venue-name check), 49 (erase / `errlog clear` first), 52 (threshold is host-pinned only),
  54 (`dbg sim laps 1000`; eviction may hide the card), 55/57 (move under #103), 56 (set drag mode), 58 (real reboot into a full fs
  with no closed `.log`), 59 (600 s auto-clear), 70 (banner == `git describe`), 24 (`devkit> ` prompt), 25 (raw terminal for the note
  line), 12/13/15 (tick), D-12 stale cites, V-9 stale §17.7 sentence. Reason: each criterion as written cannot run, cannot observe,
  or would pass without the fix (§5). Issue #92 tracks the file.
- Spec (26 standalone rows of §3.1): S3 (#132), S4, S5 (#125), S6 (#134), S7,
  S13, S16–S35. Reason: the spec is declared binding (spec §0) and these sentences describe code that does not exist or differs.
- Roadmap / README / FW / P10 documents: R1 (MEDIUM, one reconciliation commit), R2 (MEDIUM, file the three dropped findings
  pr:L5/L6/L7 as one issue), R3, R4, R6, R7, R8, R11. Reason: state-of-record documents contradicted by git/gh.
- PR #122 / #126 bench claims P5, P6 — corrected by the item 30/33/40 rewrites.
- DRV-N3 (+S1, export_serial.c:594-597) MEDIUM: no env builds DEVUX=OFF (every CMakeCache `DEVUX:STRING=ON`, drv-v N3); correct
  spec:409 and the comment; moving `ota recv` out of the gate is an F5 decision.
- TCI-T2 (+R9, R10, R12, tci-v V-1) HIGH: the linter reports what it implements; the deviation register, P10 design §5 and one test
  rationale claim more — rewrite the rule-9 paragraph to the three spellings implemented and mark PD-5/PD-7 review-only; the
  typedef-aware extension goes to F5 (no runtime impact; no bench item).
- TCI-T5 (+S8) HIGH: amend §5.2 to "prototypes only, 13 headers" or regenerate the 6 blocks; tool extension to F5 (no runtime
  impact).

### C — defer to F5 with an issue (89: 62 §4 rows + 24 code comments + 3 PR/issue records)

| ids | sev | one-line reason | issue |
|---|---|---|---|
| IO-N2, IO-N1 | HIGH | silent-success export of a CRC-damaged log; needs physical flash corruption (the firmware cannot write one, io-v §1 N2); no session exists on moto_neo6m until Plan 8; fix: count reader `frames_bad` into the verdict, drop deltas to the next KEY | — |
| IO-N3 | HIGH | JSON summary must read the `.sum`; bench-confirmable in 30 s but no item depends on it; release impact begins with Plan 8 sessions | — |
| IO-N4, IO-V1 | HIGH / LOW | fill VBO `has_sf`/layout and LIST layout from the trk tables; item 32 records the current output as expected (B) | — |
| CE-N3, CE-N4, CE-V-2 (+S10, P4) | HIGH, HIGH, MEDIUM | deterministic, host-testable engine/pipeline fixes; no bench path reaches them (one-layout sim, same sim loop) and no fix source on moto_neo6m | — |
| APP-N2 (+S15, APP-V-3), APP-N3, APP-N4, APP-N5, APP-N6, APP-N11 | MEDIUM | session-liveness edges; the bench notes are in B (items 34/43); inert on moto_neo6m (no session) | #128 (N3/N4) |
| APP-N7 | MEDIUM | rc discard + `s_best` wipe on refusal; item 28 cannot run | #114 |
| APP-N8 | MEDIUM | append-path reserve; needs LittleFS free-space behaviour (#104) to size | #104 |
| IO-N5, IO-N6, IO-N8 | MEDIUM | export range check + UB, cfg string round trip, logger drain order (measure with a sim `.log` + tools/replay) | — |
| DRV-N2 | MEDIUM | put the panel into hwid or dist name; one panel per bench | — |
| TCI-T1 (+P8) | MEDIUM | make GRACE-STALE fail under `--fail-on-violation`; correct the PR #138 record | #139 is "F4 tooling residuals" (contents not re-read here) |
| DC-N4 | MEDIUM | per-verb devkit.py timeout; item 24(a) uses the raw console | — |
| CE LOW: N7, N8, N9, V-3, #118 (+P10), #119 | LOW | hygiene / latent | #116, #118, #119 |
| IO LOW: N9, N10, N11, N15, N17, N18, V4 | LOW | hygiene / latent | — |
| APP LOW: N9, N10, N12, N13, V-1 | LOW | hygiene / rare races | #105, #108, #128 |
| DRV LOW: N7, N8, N10, N12, N16, N17, N18, V1 | LOW | tooling and driver hygiene (N7/N16 harden sign_release beyond A2) | — |
| DC LOW: N1, N5 (= DRV-V2), N6, N8 | LOW | fail-safe race, sim-only teleport, console reply wording | — |
| TCI LOW: T3, T4, T6, T8, T9, T10, T11, T12, T13, T15 | LOW | linter/test-honesty refinements with 0 live instances | — |
| 24 standalone code comments (§3.4) | LOW | stale/false comments (one sweep) | #129 (pipeline.c:1441) |
| PR/issue records P3, P9, P12 | LOW | correct the record with one comment each | #92 (P12) |

### D — defer to Plan 8 / Plan 6 (6)

| id | sev | reason | plan |
|---|---|---|---|
| CE-N2 (+S9, CE-V-3) | HIGH | error equals real fix latency; the Plan-8 driver must deliver epoch vs arrival and the magnitude is measured there | Plan 8 |
| CE-N5 (+S12) | HIGH | needs orientation capture on a real IMU | Plan 8.4, #93 |
| CE-N1 (+P2) | MEDIUM | trigger is silent UBX frame loss; rate = `gps_stats_frames_bad()` on the real driver | Plan 8 |
| CE-N6 (+S11) | MEDIUM | rate depends on real NEO-6M Doppler near launch | Plan 8 |
| CE-#11 | LOW | PPS path unreachable (`has_pps = 0` on every shipped target) | M10 / Plan 8, #11 |
| DRV-N4 (+S2) | MEDIUM | brownout threshold belongs to the Plan-6 rail decision (2.43 V ships today; SPI NOR ≥ 2.7 V) | Plan 6 |

## 7. Method and counts

- Method (BRIEF.md, binding): every statement in spec, FW design, roadmap, README, measurements, bench doc, P10 docs, code comments,
  PR bodies #113 #121 #122 #126 #127 #138 and the 2026-10-15 issue comments was a hypothesis, confirmed only by a 7265059 line
  citation, a command output, or a host reproducer; every verdict by proof by contradiction. Each reviewer report was then attacked by
  a verifier (VERIFIER-BRIEF.md) who re-ran every reproducer, re-read every cited line and tried to destroy every verdict in both
  directions. This synthesis read the 14 reports and gates-main.txt, re-read ui.c:1540-1560 and bench doc :150-164, :473-492 to
  decide two reviewer/reviewer disagreements, and ran `gh pr view` (merge times) and `gh issue list` (36 open).
- Agents: 7 reviewers + 7 verifiers + 1 synthesiser = 15 agent runs; the docs-claims reviewer ran as two agents (the first was
  stopped by a usage limit after its A.10, doc line 188). No agent used subagents. The synthesiser ran on claude-opus-5-5; the
  reviewer/verifier reports do not record their models.
- Claims checked by the reviewers: core-engines 55, core-io-ui 46, app-layer 58, drivers-build 59, tools-ci-tests 34,
  devcontroller-link-sim 52, docs-claims 308 = 612 (each report's final line).
- Reproducers run: core-engines 7 new + 11 from 2026-10-13 (reviewer), 3 new + 7 originals (verifier); core-io-ui 2 (reviewer),
  3 from 2026-10-13 + 2 new incl. the real cmd.c `cmd_dispatch(CMD_OPEN)` harness (verifier); app-layer 0 (reviewer), 2 harnesses
  including the real pipeline.c (27 checks) and logger.c (18 checks), all green (verifier); drivers-build 3 (reviewer), re-run + a
  gps_sim mutation + an `-E` DEVUX=OFF proof (verifier); tools-ci-tests 8 lint probes, a venue probe, 5 spec-api probes, 2 scripts
  (reviewer), all re-run + an instrumented assert-trip build + an all-conditionals-blanked lint (verifier); devcontroller-link-sim
  3 new + 2 from 2026-10-13 (reviewer), re-run + 1 new + a test_sim_profile mutation (verifier). REFUTED findings: none in any area;
  BROKEN FIXED verdicts: none.
- UNVERIFIABLE on 7265059 and the step that settles each: core_selftest on target (flash test_apps/core_selftest, read
  `=== core_selftest RESULT: PASS`); #106 console INFO silence (at the next silence print `esp_log_level_get("*")` and issue one
  `ESP_LOGI`, app-v §3); heap headroom vs HEAP_LOW_KB 40 (`dbg mem`, items 35/71); LittleFS `free_kb` for an open append file (#104);
  logger WDT vs a real > 5 s compaction (item 36); NEO-6M fix latency, Doppler near launch and UBX frame loss (CE-N2/N6/N1 magnitudes,
  Plan 8 driver logs); the 25 ms OTA pacing margin (scope, or item 23 with `--pace 0`); OTA corrupted-image handling end to end
  (§22.3 OTA matrix, item 43); whether a dev-kit reset on host-port close stops a push (item 43(a)); `probe_is_devkit` against the
  real lap-timer port (`tools/devkit.py status` with only the lap-timer plugged); panel legibility, LINK timing, tactile debounce
  (items 30/31/14/37); item 54's eviction-hides-the-card derivation (one bench run); IO-N8's magnitude (a moto_sim `.log` decoded with
  tools/replay — no hardware); the ring-pop retry cap under contention (a host test with a cooperating producer thread); whether the
  user read PR #138's rulings before merge (user statement). Not read by any reviewer: ui.c:1566-1980 (render ladder) and bench item
  F0/#102.
- Resolved during verification (previously UNVERIFIED): moto_sim DRAM 66,472 (drv-v §0); DEVUX=OFF image (drv-v N3, `-E`);
  imu_sim mount (ce-v §3); N3/N4/0x0703 console path (io-v §3, real cmd.c); historical DRAM baselines and the 47-test count (doc-v §3,
  CI logs).
- Disagreements decided by this synthesis (decisive evidence in brackets): IO item 30 "observable" vs doc row 146 → not observable
  [ui.c:1550-1554]; app "item 57 runnable" vs doc row 169 → cannot run [bench doc:153-162, gps_sim.c:134-146]; doc-v item 26 first
  half "laps < 120 s" vs ce-v → one ~133 s lap completes [ce verify-repro-item26 executed]; app-v vs doc-v on item 58's mechanism →
  both lead to cannot run, both recorded; spec:1896 doc row 49 TRUE vs app N18 FALSE → FALSE [cmd.c:224-226]; doc row 22 (§5.2 TRUE)
  and row 233 (R12 "caught by stored-value check") vs TCI-T5/T2 → FALSE [tci-v runs]; APP-N21 HIGH vs doc D-6 MEDIUM → HIGH for item 50
  (false pass), item 58 MEDIUM; IO-N7 LOW vs doc D-5 MEDIUM → MEDIUM (cannot run); DC-V-1 LOW vs doc-v V-3 MEDIUM → MEDIUM; drv §5
  "item 39 observable" vs doc-v V-2 → would false-pass [logger.c ESP_LOGE census]; app "item 52 runnable" vs doc-v V-1 → would
  false-pass [SIM_FIXES min 19,880 mm/s]; drv-v vs dc-v on the gps_sim.c:39-41 sentence → kept as one LOW wording row.

## Appendix — reports and reproducers (git-ignored, `.superpowers/reviews/2026-10-15/`)

- Briefs: BRIEF.md, VERIFIER-BRIEF.md, SYNTHESIS-BRIEF.md; gates: gates-main.txt, gates-main-host.txt, gates-main-fw.txt.
- core-engines.md, core-engines-verified.md; core-engines-repro-{absent-gap,best-across-venue,drag-latency,drag-stale-doppler,lap-layout,tb-pps,uncal-tilt}.c; core-engines-verify-repro-{absent-gap-sweep,force-reverse,item26}.c.
- core-io-ui.md, core-io-ui-verified.md; core-io-ui-repro-{export,json}.c; core-io-ui-verify-repro-{cmd,n2-rekey}.c.
- app-layer.md, app-layer-verified.md; app-layer-verify-repro-{pipeline,logger}.c, app-layer-verify-stub/, app-layer-verify-run.sh.
- drivers-build.md, drivers-build-verified.md; drivers-build-repro-{drag.c,dram.py,stale-version.sh}, drivers-build-repro-stub/.
- tools-ci-tests.md, tools-ci-tests-verified.md; tools-probe-lint/, tools-probe-venue/, tools-probe-specapi/; tools-ci-tests-repro-{callgraph,testscan}.py.
- devcontroller-link-sim.md, devcontroller-link-sim-verified.md; devcontroller-link-sim-repro-{flashtok,gpssim,splitargv}.c; devcontroller-link-sim-verify-repro-cfgsz.c.
- docs-claims.md, docs-claims-verified.md.
- 2026-10-13 reproducers re-run: `.superpowers/reviews/2026-10-13/` (repro_gap, repro_lap, repro_bestsec, repro_force, repro_drag, repro_misc, repro_uncal, repro1–3, dl.c, fr.c).
