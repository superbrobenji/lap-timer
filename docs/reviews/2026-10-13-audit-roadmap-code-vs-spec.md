# Audit 2026-10-13 — roadmap state vs evidence, and code vs spec

## 0. Verdict

- **Build/test health is real:** every advertised gate passes today on a clean re-run (host 47/47, dev-kit 28/28, linter 0, both firmware envs 0 warnings). The claims "0 warnings / lint 0 / 47 tests" were never the problem.
- **The hardware story is thinner than the docs say.** Everything proven on hardware since 2026-09-17 is recorded only in git-ignored ledgers and PR bodies; the roadmap still reads "pending" for three gates that passed; `docs/measurements.md` ends at Plan 04. The spec (declared binding) is wrong about the repo layout, tools, console command set, serial OTA, signing guards, and several constants.
- **"Plan 4.5 done" is not true as written:** the Power-of-10 linter is not a required check on `main`, and the linter itself cannot see most of what it claims to gate (rule 3 is non-blocking, rule 9 sees one declarator spelling, length is counted in physical lines, rules 1/2/6/7 are checked by nothing outside `core`).
- **The production build (`moto_neo6m`) is not a lap timer yet, and that is not stated anywhere:** it opens no logging session, OTA can never validate on it (every push reverts at the next reset), fusion is never calibrated (lean/lateral g never valid), and §17.2 supervision beyond heartbeats does not exist. All of this is masked by the sim build on which every bench ran.
- **Timing correctness has real holes the host suite never exercised** (reproduced first-hand): a <5 s GPS dropout near S/F produces a wrong lap reported VALID; a sector gate close to S/F makes every third lap INCOMPLETE; best-sector/theoretical-best leak across venues; unknown layout ids silently disable timing; a marginal-g stall emits a DONE run; the `cfg.lap.*` block is inert.
- **Exporters can emit garbage with a valid checksum** on one mis-framed record, and the JSON summary can drop every lap while reporting success.
- **PR #101: MERGE-AFTER-FIXES** (spec §10.1 and the track-table ownership comment contradict the shipped code; a false start now leaves the card reading LAUNCHED; three false comments; one false PR claim).

Severity totals (de-duplicated): 4 CRITICAL, 14 HIGH, ~40 MEDIUM, ~45 LOW, 60+ false claims in comments/docs/spec, 7 linter gaps.

Scope: `main` @ f4d4db2 (plus PR #101 @ df75497 where noted). Method: every claim in the roadmap,
plan ledgers, PR bodies, memory notes and code comments was treated as a hypothesis and checked
against the tree, git, GitHub, and fresh local runs. "Contradiction" below = a claim that the
evidence falsifies. "Unverified" = a claim with no in-tree or on-GitHub evidence either way.

## 1. What is actually proven today (re-run on this machine, 2026-10-13)

| Gate | Result | Evidence |
|---|---|---|
| Host suite (`ctest`, clang) | 47/47 pass | local run |
| Dev-kit host suite | 28/28 pass | local run |
| `tools/test_devkit.py` | 17/17 pass | local run |
| Power-of-10 linter `--enforce-fnptr --fail-on-violation` | 0 violations, 9 register rows | local run |
| `moto_sim` clean build, ccache off, IDF 5.3.2 | rc 0, **0 compiler warnings**; DRAM remain **4192 B** (.bss 104664); image 579788 B (bin 589744 B padded) | local run |
| `moto_neo6m` clean build, ccache off | rc 0, 0 compiler warnings; DRAM remain **5616 B**; image 545156 B | local run |
| Branch protection on `main` | required checks = hygiene, host-tests, tracks-generated, build (moto_neo6m), build (moto_sim); linear history; admins enforced | `gh api .../branches/main/protection` |
| PR #101 | OPEN, MERGEABLE, all 9 checks green | `gh pr view 101` |

Not proven by anything in the repository: every on-hardware result after Plan 03 (see §6).

## 2. Roadmap vs reality

| Plan | Roadmap says | Reality (evidence) | Verdict |
|---|---|---|---|
| 00–04 | done, tagged | tags exist, tests pass | OK |
| 4.5 Power of 10 | `plan-4.5-done`; 4.5.6 exit = "blocking analysers **required on `main`**" | `static-analysis` job exists and fails on violations, but is **not** a required check (protection lists 5 checks, none is static-analysis). Ledger 4.5.6 itself downgraded this to "USER ACTION (optional)". | **Contradiction**: tag applied with an exit criterion unmet. A PR with a linter violation can be merged by anyone with the 5 required checks green. |
| 5 (A) prod link | done #55 | cmd/stream/OTA/prod-slim present | OK |
| 5.2 BLE | "code-complete, fits neo6m, parked on s5.2-conn-ble" | branch = 3 commits, +564 lines, based on 116d199 (pre-Plan-7 main); never flashed; `conn_ble/` dir absent on main; `CONN=ble` is a no-op in CMake (no dir appended) while `CFG_HAS_BLE=1` and `CONFIG_BT_*=y` are still set | **Unverified** ("fits" was measured on a tree 30+ merges ago); dead config on main |
| 5.5 / 5.6 dev controller | done | present, tests pass | OK |
| 7 / 7b / 7c | 7c "Bench gate p07c-d1: **pending**" (roadmap line 187); dsA-d1 "pending" (207); dsB-d1 "pending" (226) | PR #88/#89/#90 bodies + git-ignored ledgers say PASS 2026-10-04/05; tags p07c-d1/dsA-d1/dsB-d1 exist | **Roadmap stale** (three "pending" lines are false); PR #101 does not touch the roadmap either |
| 6 Power | next, waits on BOM | no `app/power/`, no §16 code at all | OK (correctly not claimed) |
| 8 Real sensors | not started | `gps_neo6m.c` (47 lines) / `imu_mpu6050.c` (34 lines) are stubs | OK |
| 9 Field | not started | — | OK |
| Sub-project C | last | — | OK |
| Glyphs/drag/menu (#101) | READY | 78 files, bench days 4+5 recorded in PR body only | see §7 PR-101 findings |

## 3. False-claim register (claims contradicted by evidence)

Each row: where the claim lives → what the evidence shows.

### 3.1 Claims about process/state
1. **"Plan 4.5 done = blocking analysers required on main"** (roadmap 4.5.6 exit; memory `lap-timer-project` "blocking CI"; spec §17.9 "enforced by blocking CI") → protection has no `static-analysis` check. Linter is advisory at the merge gate.
2. **Roadmap "pending" bench gates** (p07c-d1, dsA-d1, dsB-d1) → passed 2026-10-04/05 per PR bodies; never written back.
3. **`docs/measurements.md` "newest first"** → newest entry is Plan 04 (2026-09-17). Every bench result since (Plan 7/7b/7c, bench days 2–5, 36-min soak, OTA rollback, recovery-mode OTA, stack high-water after the display landed) lives only in git-ignored `.superpowers/sdd/*/progress.md` and PR descriptions. Spec §22.4 and roadmap 3.6/9.3 require `docs/measurements.md`.
4. **Memory `lap-timer-project.md` STATE block** says main = 69055d9, "Plans 1–7b merged" → main is f4d4db2 with 7c, dsA, dsB, #94 merged (the index line is current, the file is not).
5. **README** → "exports to RaceChrono over BLE" (BLE not built), `components/hal/` (is `lt_hal/`), "`tools/` web client" (none exists), "`build.sh` arrives with plan 03" (stale), "all five CI checks required" (true, but implies the P10 gate is one of them; it is not).
6. **PR #101 body: "Every pre-existing gate-row golden is byte-identical"** → `git diff --stat f4d4db2...df75497 -- test/snapshots` shows 13 pre-existing PBMs modified (drag_p0_armed_gates, drag_p1_gates, drag_p1_overflow, drag_p2_best, drag_p2_overflow, icons_and_bar, menu_scrolled × 2 canvases). Some are expected from the list redesign; the sentence as written is false. (PR reviewer §7 checks whether the content changes are intended.)

### 3.2 Claims in code comments contradicted by the code
7. `sup.c:5-6, 293-294` "ladders and heap/stack/temperature checks … wired here [in 3.3/3.4]" → never wired. No `heap_caps_get_minimum_free_size`, no `uxTaskGetStackHighWaterMark` outside `dbg mem`, no `E_SYS_HEAP_LOW`/`E_SYS_STACK_LOW` producer, no temperature producer (#82), no GPS/IMU ladder, no `EV_FAULT` on flag change. Plan 03 was tagged done with §17.2 half-implemented and no issue tracks it except #53/#82.
8. `sup.c:144` "RTC snapshot save lands in 3.5" (stall restart path) → `check_stalls` still saves nothing before `esp_restart`; the pipeline's save-on-gate is the only writer.
9. `logger.c:317-318` "gps_hz real rate filled by the pipeline (3.4)", "calib block left zero until 3.4" → never filled; every SESSION_HDR carries `gps_hz=0` and a zero calib block.
10. `app_main.c:143` "RTC_VALID left in place; 3.5 resumes from it" and `lt_rtc.c:4` "3.5 … drives resume" → resume exists but issue #103 (filed 2026-10-08) records it has **never run on hardware**; measurements.md 3.5 claims the exit criterion "met (mechanism)" on a sim whose clock rewinds so the resumed lap can never complete.
11. `app_main.c:290-291` "Pipeline/logger/ui/power start here in 3.4" → there is no power task.
12. `pipeline.c:711` "MARK_GATE / CALIB_ORIENT: later sessions" → on main both commands are dropped; the menu sends `CMD_CALIB_ORIENT` (ui.c:447) which is silently discarded (#93).
13. `pipeline.c:803-806` "when Plan 8 adds runtime (real-GPS-driven) venue detection … must clear s_best" → the lap engine already scans every 5 s in NO_VENUE (lap.c:907-921) on every build; what is missing is the pipeline reacting to `EV_VENUE_FOUND` (no `logger_set_venue`, no `LOGGER_OPEN_SESSION`, no `s_best` clear) — see §4 F8.
14. `ui.c:449-452` menu stubs blamed on "plan 06"/"plan 05" → BLE export is sub-project C, plan 05 is long merged; "Diagnostics" (§17.10) and "Sessions" (§20.7) have no owner at all.
15. `ui.c:1118-1121` menu lock "== the lock threshold" → `EV_MOTION` fires at `MOVING_SPEED_KMH`=3, so the menu locks above 3 km/h, not the spec's 10 (F6, §20.7). Documented as "coarse" in a comment, not in the spec.
16. `lt_consts.h:17` `BATT_OTA_MIN_MV 3800` "until the power task provides a real batt_pct" → the OTA precondition is a bare ADC read on a floating divider (#54) plus a floating CHRG pin; on the bench it always passes.

### 3.3 Spec claims contradicted by the tree (spec is "source of truth", §0)
17. §4.2 repository layout lists files that do not exist: `fusion/fus_calib.c`, `session/ses_crc.c`, `ui/screens_car.c`, `drivers/gps_ubx_common`, `drivers/conn_ble`, `tools/web/export.html`, `tools/gps_sim.py`, `tools/serial_export.py`, `tools/log2csv.py`, `test/test_ses.c`, `test/test_exp.c`.
18. §4.6 / §21.5 "`sign_release.sh` refuses [sim builds]" and "refuses `-dirty`" → `tools/sign_release.sh` has no such check; `dist/` holds seven signed `moto_sim_epaper` images.
19. §18.4 "OTA over serial is not implemented; use idf.py flash" → `ota recv` + `tools/ota_push.py` + dev-kit `flash push` are the primary flash path.
20. §18.4 command list → console lacks `tracks get/put`, `coredump`, `dbg gps raw|imu raw|power|sim on/off`; has `ota recv` and a different `dbg` family.
21. §18.1 op table → `cmd.c` implements 0x01–0x06, 0x10, 0x11, 0x14–0x16, 0x20–0x23 only; `TRACKS_GET/PUT`, `COREDUMP_GET/CLEAR`, `TIME_SYNC` absent, no issue tracks them.
22. §12.3/§12.4 record types `TIME_MAP`, `MARK`, `CALIB`, `POWER` are never written on the device (no `ses_encode_time_map/mark/calib/power` call outside core/tests); §12.4 "`TIME_MAP` at session start, on quality change, every 60 s" is unimplemented, so a `.log` has no mono→gps map.
23. §12.6 "Logging pauses when still for 10 s" → no `LOG_PAUSE_STILL_S` constant, no pause logic.
24. §12.1 "A new session also starts on mode change … ends … when the venue changes" → `CMD_SET_MODE` and `EV_VENUE_FOUND` open/close nothing.
25. §13.3 "Session open/close is commanded by the power task" → opened only by the sim pipeline path (`pipeline.c:812`) and `dbg logtest`; **on `moto_neo6m` no session is ever opened** (nothing logged, no `.sum`, no VENUE record) — F8 is unmet on the production build.
26. §19.5 preconditions "CONNECTED, `fus_is_still`, no `EV_MOTION` for 30 s, logger session closed" → `ota_begin` checks only battery/charger and no-pending; an OTA can be applied while riding and with a session open (closed later at reboot, #59).
27. §17.6 self-test table → only storage probe exists; no battery range check, no coredump `esp_core_dump_image_check`, no `E_SYS_PANIC` summary, GPS/IMU rows are Plan 8.
28. §21.6 "`web`: HTML validity check" and §22.1 "≥ 90 % lines in core (gcov in CI)" → neither job exists.
29. §4.3 task map: `conn` and `power` tasks absent; `link` task (prio 4) exists but is not in the spec table; `ui` stack is not 6144 on moto_sim (reduced, #53).
30. §20.7 menu: 8 of 11 items are stubs on main (Layout = always Auto, New track, Calibrate, Export, Live, Diagnostics, Sessions, Sleep now); "Display: … full refresh now" absent.

## 4. Requirements F1–F12 (spec §2.1) — status on main

| Req | Status | Evidence / gap |
|---|---|---|
| F1 lap timing ±50 ms | **host-proven, sim-proven, never on real GPS** | replay fixtures; on-device sim laps 0 ms error; `gps_neo6m` is a stub |
| F2 sectors / theoretical best | proven (host) | test_lap |
| F3 venue + layout auto-detect, manual override | engine proven (host, Killarney fixtures); override is `Auto` only on main, real cycling in PR #101 | |
| F4 drag benches / rollout / hit-only rows | host + sim bench | 100-0 gate unreliable until fusion is oriented (#93) |
| F5 lean/g recording | **not met in production**: fusion is never calibrated (#93), so `FUS_LEAN_VALID` is never set, lean maxima stay 0, `g_lon` is sign-less | pipeline drops `CMD_CALIB_ORIENT`; no forward-learning call site |
| F6 three-button UI, menu lock >10 km/h | bench-proven; lock threshold is 3 km/h (proxy) | ui.c:1120 |
| F7 on-device track creation + persistence | **not on main**; PR #101 implements it; persistence bench-verified only by log, name on screen unread (user's reading open) | |
| F8 persist sessions/laps/logs | sim build yes (power-cut test 3.3); **`moto_neo6m` opens no session** | §3.3 #25 |
| F9 BLE export + RaceChrono import | **not met**: no BLE on main; serial path works; RaceChrono import of a VBO never verified | |
| F10 power management | not started (Plan 6) | |
| F11 self-healing: WDT, ladders, safe mode, continuity | WDT/safe/recovery bench-proven; GPS/IMU/heap/stack ladders absent; continuity never completed on hardware (#103); acceptance "dbg crash mid-lap … within 50 ms" never run | |
| F12 signed OTA, hwid check, rollback | good-image + crash-rollback + recovery-push bench-proven (day 3); wrong-hwid proven at the 5.5 gate; **corrupt-image → `E_OTA_SIG` never bench-proven** | |

## 5. Spec drift that must be reconciled (spec is binding per §0)
- §18.4 vs reality (serial OTA, command set) — spec wrong.
- §4.2 layout, §23 tools — spec wrong.
- §19.5 preconditions — code wrong (or spec must be amended with a ruling).
- §12.1/§12.4/§12.6/§13.3 session lifecycle + TIME_MAP + still-pause — code wrong, no issues.
- §17.2 supervisor loop — code wrong, partially tracked (#53, #82).
- §17.6 self-test — code wrong, untracked.
- §20.7 menu — code wrong, partially tracked (#93, #97, #98; nothing for Export/Live/Diag/Sessions/Sleep).
- §21.5/§4.6 sign_release guards — code wrong, untracked.
- §21.6/§22.1 CI web/gcov jobs — spec aspirational, untracked.

## 6. Process findings
1. **Evidence is off-tree.** All hardware evidence since 2026-09-17 is in git-ignored ledgers and PR bodies; `docs/measurements.md` stopped at Plan 04. A clone cannot audit any bench claim.
2. **P10 gate is not a merge gate** (§3.1 #1).
3. **Roadmap is not maintained as a state document** (three stale "pending"; PR #101 adds none of its own state).
4. **Issue hygiene is good** for things the bench found, poor for spec gaps found by reading (items 22–30 above have no issue).
5. **Comments are used as a ledger** ("lands in 3.4", "later sessions") and then never revisited; 10+ such notes are now false (§3.2).

## 7. Code review findings (strict; consolidated from 6 reviewers + orchestrator spot-checks)

Legend: [V] orchestrator re-verified in code/ELF/reproducer; [R] reviewer-verified with a reproducer or ELF; [C] reviewer claim read-checked only; [X] reviewer claim corrected by orchestrator.

### 7.1 CRITICAL
- **OTA can never stick on any real-GPS build** [V]. `sup.c:233` gates validation on `pipeline_gps_seen()`; `gps_neo6m.c:31` `gps_poll` always returns 0 (stub) → image stays PENDING_VERIFY; next reset → bootloader rollback; while pending, `ota_begin` (`ota.c:161`) refuses every push. Net: on `moto_neo6m` every OTA is reverted at the next reset and the device can only be flashed over USB. Sim-only bench never sees it. No issue. (Also true for the future real driver whenever no fix is obtained before the first reset, e.g. indoors.)
- **Export: negative/absurd `gps_us` produces garbage UTC and truncated rows** [V+R]. `exp.c:52-56` has no guard; `ses_decode_fix` does no range check; a resynced mis-framed record (16-bit CRC) can admit one. NMEA emits a valid-checksum garbage sentence; VBO row loses its `\r\n` (merges two rows). `snprintf` returns ignored.
- **JSON summary export silently truncated and reported success** [V+R]. `exp_json.c:38` sets `json_stage = 1` before `emit()` can fail; header > 400 B (escape expansion of control chars in id/fw/venue) drops every LAP and `exp_finish` returns 0.

### 7.2 HIGH
- §19.5 OTA preconditions absent (stillness/CONNECTED/session closed) [V]; logger keeps writing flash during the push; §17.8's "OTA only when stationary" unenforced.
- Task WDT: only sup + ui subscribe; pipeline/logger never do [V]. A wedged logger (the Plan-7 LittleFS-compaction failure mode) only logs `E_SYS_TASK_STALL`.
- Fault strip bit→icon table wrong [V]: `SYS_GPS_NOFIX` draws the strike-through (same as DEAD); `SYS_IMU_DEAD`, `SYS_FUSION_DISAGREE` draw nothing; `SYS_STORAGE_DEAD` draws the healthy disk; `ICON_GPS`/`ICON_BLE` dead data. (`screens_moto.c:203-220`)
- `cfg_json.c:124` `walk()` is directly recursive (rule 1); no deviation row; no analyser gates rule 1 outside `core` [V].
- `dbg sim drag` integrates position cumulatively from the previous frame (`gps_sim.c:333,352`) [V+R]: beyond ~104 km/h every drag fix fails the §6.5 jump filter → `EV_FIX_LOST`, no Doppler re-anchor; the bench-day-3 drag pass was effectively IMU-only. Contradicts §4.6's "continues from that same position".
- §18.4/§18.1: `tracks get/put`, `coredump`, `TIME_SYNC` do not exist → no way to upload a user track or fetch a coredump on any build [V].
- Dev-kit AP passphrase derivable from the beacon BSSID (`devcontroller/main/main.c:89-94`) [C]: T-F's threat (anyone in radio range) is not closed.
- Dev-kit `linkhost_flash` streams 512 B chunks unpaced into the lap-timer's fixed 256 B console RX ring (`linkhost.c:552-568`); the per-chunk ACK the lap-timer source says "lands with the dev-controller integration step" never landed [C]. Fails safe via SHA/ECDSA; #57 remains the real fix.

### 7.3 MEDIUM (selected; full lists in the reviewer appendices)
- Button ISR: `btn_isr` is `IRAM_ATTR` but calls flash-resident `read_button_mask` (`board.c:78-94`, confirmed in the ELF) [X]: NOT a panic — `gpio_install_isr_service(0)` has no `ESP_INTR_FLAG_IRAM`, so IDF masks the interrupt during cache-off windows; effect is a deferred edge, plus a §17.9 deviation and a misleading `IRAM_ATTR`.
- `g_fix_ring` has two producers (`export_serial.c:580` `dbg logtest` + pipeline) on a relaxed-head SPSC ring → spliced FIX records in a live session [V].
- errlog NVS rate-limit dedups on `(code,arg)`; `E_STO_WRITE` carries a varying batch length → ~1 Hz NVS blob writes forever on a full/dead fs [C].
- Side effects inside assertions at 12 sites (`cmd.c`, `logger.c` `n++` in `LT_ASSERT`) — the loop bound is the assertion [C]; plus ~20 tautological padding assertions across ui.c/render.c/jw.c/json.c/pipeline.c/lt_ipc.c [C].
- §17.2 supervisor: ladders, heap/stack checks, `EV_FAULT`, RTC save before stall-restart all absent [V]; `CMD_GPS_LADDER_STEP`/`CMD_IMU_RECOVER` do not exist; `SYS_GPS_DEAD/IMU_DEAD/IMU_SUSPECT/FUSION_DISAGREE/HEAP_LOW` have no writer.
- §12.6 still-pause not implemented; §12.1 session lifecycle (mode change/venue change) not implemented; `s_seq` saturates at 255 and then appends two sessions into one file [C].
- `status.state` hardcoded 0 (§19.4 `STATE_OTA` never reported) [C]; OTA has no transfer timeout on the framed path (dropped peer wedges `OTA_RECV` until reboot) [C].
- Constants vs Appendix A: `OTA_VALID_UPTIME_S` 30 vs 60, pipeline `HB_STALL_S` 5 vs 1, `E_OTA_*` codes differ from §17.7 on 5 of 6 values [V] — no deviation recorded, spec still reads the old numbers.
- `lt_cfg` load→mutate→save has no lock (ui + cmd on core 0) [C]; `op_config_set` saves even when unchanged (§15.2).
- `ESP_LOGE` is neither quiesced nor UART-mutexed: any ERROR line (assert sink, stall message at 1 Hz, storage errors) lands inside a framed body or the raw OTA stream [V-by-reading `export_serial.c:235/327`].
- `json_tok_bool` ignores token type: `"rollout":"take a hike"` sets rollout=1 (changes drag ET) [R].
- `ring_pop` CAS retry unbounded (rule 2) [C]; `sto_write` drain loop unbounded on `write()==0` [C].
- `qsort(..., cmp_int)` in `board.c:234` is an unregistered function pointer invisible to the linter regex [V].
- `sign_release.sh` refuses neither sim nor dirty, never runs `size` [V].
- `devkit.py`/`ota_push.py` auto-pick the first usbserial port; opening the lap-timer port inside the 35 s trial window reverts the OTA; neither tool mentions the rule [C].
- Dev-kit: download cap scales off an unbounded announced size (36 h on a garbled BEGIN) and the idle timer is reset by the resumed stream → link mutex held indefinitely [R]; `/api/sessions` uses `portMAX_DELAY` against spec §7 [C]; `selftest framing` cannot detect a missing CR [R]; `lt <cmd>` is a 6-verb whitelist not the spec'd relay [C]; SSE overrun guard off-by-one [C]; `stage_image` keeps sending after an early STAGE-ERR [C]; `push_and_wait` reports console `ERR` lines as a 120 s timeout [C].
- `exp_nmea` 200 B reserve < 230 B worst case → export aborts mid-stream on valid input [C]; `ses_encode_venue` caps the 32-byte wire name at 31 [R]; `cfg_apply_profile` validates nothing but the name and the no-blob boot path saves without `cfg_validate` [C].
- `fb->dirty` is dead state; render.h/model.h claim a "changed region" that is always the whole frame [C]. §11.4 benches rule (ascending, max 4, `1/4 --`) is not implemented despite `screens_moto.c:1-2` claiming it [C].

### 7.4 Linter / enforcement gaps (all proven with probe files by the drivers reviewer)
- `--fail-on-violation` does not block RULE-3 (`power_of_10.py:785`); RULE-3 regex misses `heap_caps_malloc`, `pvPortMalloc`, `strndup`, `asprintf`.
- RULE-9 regex sees only the `(*name)(` declarator: function addresses as arguments, typedef'd fn-ptr params/fields are invisible — "zero unregistered sites" measures the regex.
- RULE-5 counts assertions textually, including inside `#if 0`; "meaningful" is unchecked.
- Length counted in physical lines, so `a; b; c;` on one line evades rules 4/5 (`read_through_ses` sits at 18 lines with 0 assertions and does file I/O).
- Rules 1/2/6/7/10: nothing blocking checks them for app/drivers/main (clang-tidy runs on core only, advisory).
- `devcontroller/` scanned by nothing.

### 7.5 Core engines (lap / drag / fusion / tb / geo / trk) — all reproducers re-run by the orchestrator [V]
- **CRITICAL — short GPS dropout near S/F yields a wrong lap reported VALID.** `lap.c:1069-1072` skips invalid fixes without updating `prev_*`; the next valid fix forms a chord across the whole dropout and `process_segment` evaluates crossings on it; `LAP_F_GPS_LOST` is set on the opened lap only when the chord exceeds `LAP_SEG_GAP_US` (5 s). Reproducer: 2.8 s dropout spanning the corner before S/F → lap 4 = 26894 ms, flags VALID, truth 26667 ms (227 ms, eligible for best). §6.4 step 1 says no crossing is evaluated on an invalid fix. Prior review H5 fixed only the >5 s case.
- **HIGH — sector gate in the same segment as S/F is lost → lap INCOMPLETE.** `lap.c:981-987` evaluates sectors before S/F and never orders by `t_cross`; gate disarmed, re-armed after the car is past. Reproducer (sector 1 at 6 m past S/F, 60 m/s, 5 Hz): laps 3, 6, 9 INCOMPLETE. §6.4's own example (S/F and sector 1 within 11 m at 200 km/h) is the counter-case to the comment at `lap.c:981-983`.
- **HIGH — best sectors / theoretical best leak across venues and layouts.** `reset_to_no_venue` clears `best_sector_count` only; `lap_set_venue` clears nothing. Reproducer: venue B reports A's splits; theoretical 71667 ms vs real 80000.
- **HIGH — `lap_force_layout(unknown id)` and `lap_import_rtc(unknown layout_id)` silently arm zero candidates**: no S/F gate, no error, `lap_import_rtc` returns 0 — lap timing dead for the session. Reachable from `CMD_SET_LAYOUT` (`pipeline.c:687`).
- **HIGH — the entire `cfg.lap.*` block is inert.** `lap_cfg_t` carries only `default_layout_id`; `lap.c` reads the `consts.h` macros for min/max lap, rearm, pit; no consumer of `cfg.lap.*` exists outside `core/config/`; `pipeline.c:746` passes `lap_init(&s_lap, NULL)`. §15.1 keys parse/clamp/persist/export and do nothing; §10.5's default-layout tie-break never applies.
- **HIGH — marginal-g launch then stall emits a spurious DONE run** (`drag.c:571-581`: abort guard `v_peak > 1 km/h` vs minimum reconstructable `v_peak` 0.53 km/h) — consumes `run_no`, writes a gate-less `DRAG_RUN`. Reproducer confirms.
- MEDIUM: un-oriented `g_lon` not clamped to `G_MAX` and ≈1 g on a non-flat mount → `v_est` runs away between Doppler anchors and the drag engine may never arm (`fus.c:273-276`); `gate_step` asserts `tc >= tp` and skips the step on a legitimate backward `gps_us` (tb half-window rollover / PPS) — also write-storms the errlog; `lap_import_rtc` validates only `venue_id` (`sector_idx=255` → `sec_next=0`); `abort_to_armed` emits no `EV_DRAG_ARMED` and recycles `run_no`; `lap_mark_gate` ignores `lap_gate_line` rejection and returns success with a (0,0) line; `FUS_BIAS_STALE` clears without a recapture (§9.2).
- LOW: `geo_dist_m` NaN at antipodes; no ±180° wrap in `geo_to_enu`; predictive "half density" comment false (`pred_rec_fixes` dead); 11 two-level dereferences in `lap.c` against the project's declared strict N=1 (linter cannot see them); ~30 padding assertions (`state == X` right after the dispatching switch, `kmh >= 0` on unsigned inputs).
- Verified OK: prior H4/H5(>5 s) fixes hold; tb/geo/gate math, e7/e5/unit conversions, §11.1 constants, trk validation and user-blob gauntlet, fusion orientation handedness, no recursion/heap/unbounded loops in the slice.

### 7.6 PR #101 (glyphs-drag-menu-closure @ df75497) — verdict MERGE-AFTER-FIXES
Verified holding: engine-state card, LINK glyph, cfg v2 migration order and unchanged `sizeof(cfg_t)=110`, `drag_cfg_engine_differs` compares exactly the fields `drag.c` reads, run_no preserved, announce-only flag isolated to `EV_VENUE_FOUND`, boot ordering enforced by code (`lt_ipc_init → trk_init/logger_load_tracks → logger_start → pipeline_start → ui_start`), tmp+rename, goldens' ink inside the visible canvas, both linters rc 0, host 47/47.
Blocking before merge:
- **Spec §10.1 still documents the deleted API and the old blob v2 format**; the code writes `"LTRK" | u8 ver=1 | u8 count | per-venue (u16 len | payload | u16 crc16)` and skips a bad record instead of emptying the store. Also `lt_ipc.h:54,96` and spec §10.9 say the logger loads the table "inside its own boot init / from logger_task()"; it is `logger_load_tracks()` on app_main's task.
- **`trk.h/trk.c` ownership comment omits the logger task** as a post-boot unlocked reader (`LOGGER_SAVE_TRACKS`); safe today only by argument (captured `count`, append-only writer, queue-send ordering) that is written nowhere.
- **Silent engine transitions leave the card lying, one direction worse than before**: a false start (`abort_to_armed`) leaves the card reading `LAUNCHED` indefinitely (pre-PR it read NOT READY); DONE→IDLE after 5 s reads DONE until re-arm. Publish the snapshot per drag step or on `drag_state()` change.
- Three false comments/strings (`lt_ipc.h` load site; `event.h` "FAILED always 0" while `EV_CREATE_FAIL_FULL=1` is sent; `app.js` "labels only").
- PR body claim "every pre-existing gate-row golden byte-identical" is false (13 changed; expected from the list redesign, but the claim would hide a regression).
Follow-ups: `finalize_create`'s 2752 B `trk_venue_t` local on the 8 KB pipeline stack is newly reachable and unmeasured (the same frame was treated as blocking on the logger); creation while storage is dead/full reports success and bypasses the 5 % reserve; after an RTC resume the Layout menu reads Auto on a layout-locked lap and the first press changes the layout mid-lap; the venue linter passes a call anywhere in the function; file-level corrupt `/tracks/user.bin` paths (magic/version/count/len) have no host test; `s_menu_action[12]` literal; `lap_mark_gate` accepts 1 mm/s as "moving" so the S/F line can be pinned to heading noise; `Track_YYYYMMDD` is UTC and non-unique per day. (The open bench question: the sim capture's first fix is 2026-09-15 10:02 UTC, so `Track_20260915` is the correct name.)

## 8. Recommended order of work
1. **Stop the bleeding on timing correctness (core, host-testable, no hardware):** short-dropout chord (set GPS_LOST on any chord that spans an invalid fix, per §6.4 step 1); order crossings by `t_cross` across sectors and S/F; clear best-sector tables on venue/layout change; reject unknown layout ids; marginal-launch abort guard; `g_lon` clamp in the un-oriented branch; `gate_step` backward-time tolerance. Add the reviewer's reproducers as `test_lap.c`/`test_drag.c` cases.
2. **Export correctness:** guard `gps_us <= 0` in `exp_utc_parts`/`ses_decode_fix`, check `snprintf` returns, flip `json_stage` only after a successful emit, size the header buffer for escape expansion, reserve 230 B in `exp_nmea_feed`.
3. **OTA on the production build:** decouple validation from `pipeline_gps_seen()` on builds whose driver is a stub (or clear `ota_pend` when validation is structurally unreachable); add the §19.5 stillness/session-closed preconditions; add an `OTA_RECV` transfer timeout on the framed path; quiesce or mutex `ESP_LOGE` during transfers.
4. **Supervision gaps:** subscribe pipeline/logger to the task WDT; implement the §17.2 heap/stack checks (would have surfaced #53); file issues for ladders/`EV_FAULT`/RTC-save-before-restart or amend §17.2 with a ruling.
5. **Process:** make `static-analysis` (and `dc-host-tests`, `tools-tests`, `build (devcontroller)`) required checks; fix the linter (rule 3 blocking + `heap_caps_*`, statement-based length counting, `#if 0` exclusion, devcontroller scope); move the bench evidence (bench days 2–5, soak, OTA matrix results, stack high-water) into `docs/measurements.md`; correct the three roadmap "pending" lines; reconcile the spec sections listed in §5 and §7.6; update README; retire or re-base `s5.2-conn-ble` against current main before any "fits" claim is repeated.
6. **PR #101:** apply the five blocking fixes in §7.6, then merge; file the follow-ups.
7. **Sim fidelity:** fix `dbg sim drag`'s cumulative-origin bug before using the sim for any further drag-engine bench claims.
8. **Dev-kit:** random NVS-stored AP PSK printed at boot; bound the download cap; bounded mutex take on the download path; fix `selftest framing`; make `push_and_wait` parse `ERR` lines; honour/document the 35 s trial window in `devkit.py` and `ota_push.py`.

## Appendix — reviewer reports (full text)
The six reviewer reports (core algorithms, core io/ui, app layer, drivers/build/tools, dev controller, PR #101) are archived verbatim under `.superpowers/reviews/2026-10-13/` (git-ignored) together with the compiled reproducers; every finding above cites the file:line it came from.

## 9. Open-issues audit (added 2026-10-13 on request)

All 21 open issues were re-read against the tree. Verdicts, evidence and owners are in `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` §7b (one row per issue). Summary: 5 are fixed by PR #101 and close at its merge (#95 #96 #97 #98 #99); 2 are fixed inside the fix wave (#102 by F0, #107 by F2b); 1 has a strong root-cause candidate inside the wave (#106 ↔ ruling R9) and is confirmed on the bench; 8 are valid and hardware-independent and go to the close-out wave F5 (#53 #57 #61 #91 #100 #103 #104 #105, plus the wiring half of #93); 3 are hardware-gated and are recorded under the plan that owns the hardware (#11 M10, #54 Plan 6 with an F2a mitigation, #82 Plan 8); #92 is a tracking issue that closes when only hardware-gated boxes remain. Two issue texts carry stale statements (#11 "Owner: plan 04"; #53's numbers predate the 6144 B ui stack) and get a correcting comment. A spot-check of ten closed issues found none closed without its code.

## 10. Status (to be filled as the fix wave lands)

| Plan | PR | Merged | Findings closed |
|---|---|---|---|
| F0 | #101 | — | pr:H1 H2 M1 M2 L1–L4, #102 |
| F1a | — | — | core:C1 H1–H5 M1–M7 + LOW |
| F1b | — | — | io:C1 C2 H1 H2 M1–M9 L1–L5 |
| F2a | — | — | app:C1 H1 H2 M1 M5 M7 M8 M12 drv:C1 H3 M6 M10–M12 |
| F2b | — | — | app:M2 M3 M6 M11 M13 L1 L2 L7 drv:H4 M7 pr:M3 M4 |
| F3 | — | — | drv:H2 L23 dk:H1 M1–M11 L1–L8 |
| F4 | — | — | audit §3.1 §3.3 §5 §6, drv:M5 M13 LG-1..7, R33 |
| F5 | — | — | §7b open issues |
