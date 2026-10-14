# Next bench day — checklist (opened 2026-10-05)

Everything the 2026-10-04/05 bench day (gates `p07c-d1`, `dsA-d1`, `dsB-d1`) did NOT exercise, plus
the items the plans since then add. Each item has a procedure, the evidence that closes it, and any
prerequisite. Tick items here (commit the edit) and record the evidence in the plan ledger of the day;
close the tracking issue when the file is empty of open boxes. Bench helpers live in the main
checkout under `.superpowers/bench-helpers/` (git-ignored): `gate_p07c.py`, `gate_dsA.py`,
`gate_dsB.py` — identify the two USB ports first (the usbserial-0001/-3 assignment swaps between
re-plugs; `tools/devkit.py --port <dk> status` answers only on the dev-kit).

Standing rules: flash the lap-timer by dev-kit OTA push (`tools/devkit.py --port <dk> flash dist/<signed>.bin`),
never open the lap-timer's USB port for 35 s after a push, read-only captures only on that port,
sign in place with `keys/laptimer_priv.pem` from the main checkout.

## Open items

- [x] **1. Drag mode end-to-end** (prerequisite: `dbg sim drag` — plan `2026-10-05-ota-screen-and-glyphs` Task 3).
  Procedure: `config set {"mode":"drag"}` + reset (or the menu), wait for the card to read `NOT READY`,
  then `READY` once the drag engine arms (#95: `READY` now means armed; the separate top-right ARMED
  label is gone), then `dbg sim drag` via `lt shell`. Expect: the card shows the launch and gate hits as
  they happen (sub-rect partials, log `ui: refresh ... kind=P`), `pipe:` lines for
  `EV_DRAG_LAUNCH`/`EV_DRAG_GATE`/`EV_DRAG_DONE`, DRAG page 1 LAST RUN lists the hit gates by NAME with
  times (0-60, 0-100, 18m, 101m, 305m, 1/8 ... — #96: `dist_units` now defaults to `m`, so the
  distance gates print their metre names; `lt config set {"dist_units":"ft"}` + a repeat prints the
  same gates as 60ft/330ft/1000ft instead, the 1/8 and 1/4 mile gates unchanged either way), page 2
  SESSION BEST filled, a second run updates bests only where faster. Note (final review M4): the
  100-0 brake gate is the only gate that depends on the GPS Doppler anchor rather than pure IMU
  integration — production fusion is never oriented (issue #93), so it reports un-oriented `|g_lon|`
  (`fus.c:274`), and only the 5 Hz GPS re-anchor (`drag_on_fix`) makes `v_est` actually fall; a
  missing 100-0 row is expected-ish today, not a defect. Repeat once with
  `config set {"units":"mph","drag":{"n_mph":2,"benches_mph":[60,100]}}` (#86): labels 0-60 / 0-100
  in mph, thresholds converted (the 100 mph gate fires at ~161 km/h in the log). Session log carries
  the EVENT records (`dbg sum <id>`). Re-check after #95/#96 (final review I-6): ticked below is
  bench day 3's result against this item's OLD "wait for READY/ARMED" / ft-only wording — the
  `NOT READY`→`READY` card text and the `m`-default distance labels above have not themselves been
  re-run on hardware yet (items 12/13 below are the dedicated re-checks for #95/#96 in isolation).
- [x] **2. OTA rollback path.** Push any signed image; at ~10 s after its boot (inside the 30 s trial
  window, before `sup: OTA image validated`) send `dbg crash`. Expect on the next boot: the PREVIOUS
  version in the banner, `sup: OTA image rolled back by the bootloader`, errlog `E_OTA_ROLLBACK`
  (0x0806), and the `UPDATE FAILED / REVERTED` one-shot for 3 s right after the BOOT screen (plan
  `2026-10-05-ota-screen-and-glyphs` Task 2). Then push the image again normally and let it validate.
  Note (#96, review finding I2): a rollback across a cfg-version bump resets the config to
  defaults (forward migration is one-way) — re-check Units/Dist/mode after one.
- [x] **3. OTA push while in recovery mode** (debt sweep A P-8, spec §17.5 amendment). Enter recovery
  (`gate_dsA.py safe` up to the RECOVERY boot, or 5× `dbg crash` 12 s apart), then push a signed
  image. Expect: the push succeeds (console alive in recovery), `sup: OTA reboot: safe/recovery gate
  cleared for the new image`, the new image boots NORMALLY (safe_mode=0) and validates at 30 s.
- [x] **4. Long soak ≥ 35 min with the live clock on.** `gate_p07c.py clock` then a 35-minute read-only
  capture (`gate_p07c.py soak` is 10 min — run it 4× back to back or extend). Expect exactly one
  age-out full (`kind=F` ~30 min after the previous full), 1 Hz cell-sized partials otherwise, 0 W/E,
  no WDT/reset. Moving-cap full (`partial_count >= 2*full_every` while moving): needs continuous
  motion for > 2×full_every partials — use `dbg sim laps <n>` with n ≥ 20 (Task 3) and watch for a
  `kind=F` while the sim is still lapping. Note (fix round 1, I1): each repeat ends with a 6 s
  standstill (parked at the capture's last position) before the next one starts — expect a clean
  STILL/MOTION pair at every lap boundary, not a flicker.
- [ ] **5. Menu paths never exercised** — checked 2026-10-05: four stubs then; New track landed 2026-10-06 (Task 5, #97), code-complete, procedure below. Layout override landed 2026-10-06 (#98) and WAS exercised on bench day 4 (2026-10-07): it failed — the sim-boot and RTC-resume venue paths never announced to the ui (`lap_set_venue`/`lap_import_rtc` carry no event buffer and neither caller told the ui directly), so `Layout:` stayed a dead item (`menu: layout choice 0 -> id 0`) that also wiped `s_best` on every press (B4-F3). Fixed by this plan (a ui-only announce at both call sites, plus the `venue_announce.py` structural linter); re-check is item 17 below, not this item. Sleep now → Plan 6.2 (power state machine, light/deep sleep, button wake); Calibrate → Plan 8.4 (#93: the fusion calibration is never invoked). Re-test Sleep now (3 s MODE hold → sleep; wake by a button → BOOT) and Calibrate ("Hold upright, press MODE" → `EV_CALIB_DONE`) when each lands.
  New track procedure (#97, Task 5; persistence redesigned in the review's fix round 1, T5-R4; cancel/DRAG-mode paths hardened in the final review, I2/I3/M1; rewritten after bench day 5, 2026-10-10, to what a `moto_sim` bench can observe): park the sim (the menu is motion-locked), menu "New track" (two DOWNs from the top) → `NEW TRACK` one-shot, sub-line "Cross S/F, press MODE"; then `dbg sim laps 1` and press MODE while the sim moves (sub-line → "S/F set. MODE: sector 1"), MODE again (→ "MODE: sector 2"). **Timing:** the sim loop is ~28 s, so the engine finishes on the FIRST re-crossing of the marked S/F, ~28-30 s after the S/F press, with however many sectors were marked by then (day 5: marks 9 s apart landed S/F + one sector, the third press fell after the finish and hit the card; `lap 0 ... splits[16779 11252]` = a 1-sector layout proves the created venue went live). **The `VENUE` one-shot for the new venue reads `Track_20260915`** (the capture's first fix is 2026-09-15 UTC; `finalize_create()` names from `create_gps_us`), holds only 2 s and lands straight after the crossing — read it live. A second VENUE one-shot ~28 s later is the new venue's own layout lock. **Readback:** there is NO console readback of the track table (#107): `lt list` is the session list and `moto_sim` never rotates its boot session, so it keeps naming `Synthetic`; `dbg fs` lists `/sessions` only. **After `dbg reset`** the rescan may legitimately re-lock onto `Synthetic`: `trk_find_nearest()` picks the nearest centre among venues whose radius contains the fix, and Synthetic's 2000 m radius contains the whole loop — a two-choice `Layout:` cycle (`Auto → Full`) means Synthetic won, a three-choice one (`Auto → Layout 1 → Layout 1 Reverse`, the two layouts every created venue stores) means the created venue did; neither is a defect. Still open sub-checks: the riding screen's BEST/PREV/theoretical-best/best-sector board reads fresh after the creation (final review I2); press MODE one more time after the last sector it allows (sub-line stays "Cross S/F to finish", not "No fix / not moving" — M5); long MODE on the one-shot cancels back to riding, and a second long MODE immediately after always works too, even on a repeat (final review I3); `Mode: Drag` first and then "New track" must be refused silently — no one-shot ever shows (final review M1); interrupt a creation with an OTA push or `dbg crash` (`CMD_RESET_ENGINE`) and confirm the ride resumes normally afterward (no stuck NEW TRACK screen, no dead engine still waiting in CREATE — I2/M4).
  Layout procedure (#98, landed): menu → "Layout: Auto" item; MODE (short, on that item) cycles
  through the venue's real layout names and back to `Auto` (on the sim capture's single-layout
  venue: `Auto → Full → Auto`; `ui_layout_label()` renders each layout's real name, never a
  generic `L%u` — that form only appears for an unresolved id, i.e. a broken state), the menu label
  updating live on every press; pick a non-Auto choice and exit the menu (long MODE) back to riding — the engine is now locked to that forced layout (`CMD_SET_LAYOUT`), and the next `VENUE` one-shot (a fresh venue found, or the engine's own next layout lock) shows the forced layout's real name, never a stale previously-locked one (final review M-10).
- [ ] **6. Real-GPS smoke (`moto_neo6m` image)** — deferred 2026-10-05: waits for Plan 8 (real sensors) and the neo6m board on the bench. Then: flash over USB (dev-kit held in reset / BOOT), indoors BOOT line `GPS OK`, NOFIX icon, `EV_FIX_LOST`; outdoors a fix, speed on the card, no SIM glyph.
- [x] **7. Storage full as a planned gate.** Leave the sim lapping (`dbg sim laps 200`) until
  `E_STO_EVICT`/`E_STO_FULL` appear (~2-3 h at 10 Hz fused logging; or pre-fill the fs with
  `dbg logtest`). Expect: eviction keeps a reserve, the open session keeps writing, `lt list`
  answers within the dev-kit's 12 s first-byte window (#91 measures the cost), no WDT, no fs deadlock.
- [x] **8. OTA progress screen** (plan `2026-10-05-ota-screen-and-glyphs`). During a push: UPDATING,
  bar + percent climbing every 5 %, then VERIFYING, then REBOOTING, then the new image's BOOT screen.
  Abort (final review I2: `tools/devkit.py` has no `flash abort` verb): interrupt the host push
  (Ctrl-C during `devkit.py flash`) and let `ota recv`'s chunk read time out — expect `OTA-ERR
  timeout` on the console and the riding screen back. Second escape: leave the push stalled > 60 s
  and the OTA screen reverts on its own (`ui: ota screen: stale, reverting`, `OTA_STALE_MS`).
- [x] **9. SIM + MOVING glyphs** (same plan). Sim build: SIM glyph always present in the strip; MOVING
  glyph present while the sim laps, gone once parked; the menu opens only once MOVING is gone.
- [x] **10. #91 list cost** (if the lap-timer fix has landed): `lt list` round trip on the dev-kit
  console (`-- rt N ms`) with ≥ 40 sessions, before/after.
- [x] **11. Dev-kit first-load after a cold boot** (#65 regression check): power-cycle both boards,
  open Sessions and Config fresh 5× each — 10/10 without Reload.
- [ ] **12. DRAG card NOT READY/READY/LAUNCHED/DONE (#95, final review I-6 + I1).** Procedure:
  `config set {"mode":"drag"}` + reset, observe the big slot before anything arms the drag engine:
  reads `NOT READY` (FONT_MED — FONT_HUGE has no letters). Arm it (the usual condition — stationary
  then moving past the arm threshold — or `dbg sim drag`'s own arm phase): the big slot switches to
  `READY`. Expect no separate ARMED label anywhere on the card — the top-right slot that used to
  show it (`DCARD_ARMED_RIGHT_X/Y`) is gone; `READY` alone is the arm indicator now.
  Launch it (`dbg sim drag` continues on its own): between the launch and the first gate value the
  big slot must read `LAUNCHED`, never fall back to `NOT READY` — this was the original bug report
  (#95): a run is live and timing for that whole window, and the bench saw `NOT READY` there before
  this plan. Let a run finish: the big slot reads `DONE` for ~5 s, including the zero-gate case (a
  run that stops or fades before the first gate) — it must still read `DONE`, not `NOT READY`.
  Toggle-while-armed (final review I1): with the card reading `READY` (parked, armed, no run on
  screen), open the menu and toggle `Distance:` — exit and confirm the card stays `READY`
  throughout (no reload is posted at all, ui.c:513-529). Then toggle `Speed:` instead (shipped
  benches, `n_mph = 3`, so this moves the table) — exit and confirm the card reads `NOT READY` the
  instant the menu closes, then `READY` again once the engine re-arms (the
  `DRAG_ARM_STILL_S` still window later); a `dbg sim drag` run started once `READY` returns must
  launch normally (big slot reaches `LAUNCHED`, never `NOT READY`). Item 16 below is the stronger
  version of this same check, with a completed run already on screen.
- [ ] **13. `Speed:`/`Distance:` menu items (renamed from `Units:`/`Dist:`, bench B4-F6) + remote
  config (#96, final review I-6).** Procedure: open the menu, confirm the speed item reads
  `Speed: km/h` and the distance item reads `Distance: m` by default (fresh NVS, or after a
  v1→v2 cfg migration); press MODE on the distance item to toggle `Distance: ft` and back; confirm
  bench item 1's drag-card distance-gate labels follow the toggle live. Then from the dev-kit (or
  `lt shell`): `lt config set {"dist_units":"ft"}`, confirm the device menu label updates to
  `Distance: ft` with no manual reload, and `lt config get` echoes `"dist_units":"ft"` back.
- [ ] **14. LINK glyph timing (#99, final review I-6).** Procedure: with the lap-timer running and
  the dev-kit unplugged, confirm the LINK glyph is absent from the fault-icon strip. Plug the
  dev-kit in: the glyph must appear within about 1 s of the connection (the detect-pin debounce,
  `LINK_DETECT_STABLE`, is sized for this). Unplug it: the glyph must disappear within about 3 s.
- [ ] **15. DRAG gate-list pairing + distance-row unit, list AND card/footer (bench
  B4-F4/B4-F5/B4-R5, #96).** Procedure: run a drag session that hits the 100-0 braking gate
  (`dbg sim drag`, which arms/launches/brakes on its own). On DRAG page 1 (LAST RUN) or page 2
  (SESSION BEST): every row reads as one line — each FONT_SMALL label's text sits immediately
  above its own FONT_MED value's baseline, not the next column's label's; a thin horizontal line
  separates the `LAST RUN`/`SESSION BEST` header from the rows, and a thin vertical line separates
  the two columns for the full height of the rows (not across the header). The 100-0 row's value
  reads `<n> m` with `Distance: m` set and `<n> ft` with `Distance: ft` set (toggle item 13's
  distance setting and confirm this value — not just the DIST-gate labels in item 1 — follows it;
  before B4-F5 the value stayed in metres regardless of the setting). Then, on DRAG page 0 (the
  run card, while the 100-0 gate is the newest hit or sits in the footer): confirm BOTH the big
  64 px slot's distance value and the footer's "100-0 <n><unit>" entry also follow `Distance:`
  the same way (ruling B4-R5, Task 4 review round 1) — before this fix page 0 hardcoded `m`
  regardless of the setting, even though the list had already been fixed.
- [ ] **16. Config-reload vs. a run on screen (final review I1; B4-F2,
  `investigation-cfg-reload.md` §6.3).** Procedure: `dbg sim drag` to a completed run (page 0
  shows the last run, page 2 SESSION BEST filled, note the run number from the console's
  `DRAG run <n> ...` log line printed when the run completes — `dbg sum <id>` does not print the
  run number). From the
  menu, toggle `Distance:` (m ↔ ft) and exit: the card, DRAG page 1 (LAST RUN), page 2 (SESSION
  BEST) and the run number are all UNCHANGED (`Distance:` is display-only — it never reaches the
  engine). Re-arm and repeat the same check with a remote `lt config set` of an unrelated key
  (e.g. `{"display":{"live_clock":false}}`). Then, with another completed run on screen, toggle
  `Speed:` (km/h ↔ mph) — with the shipped benches (`n_mph = 3`) this moves gates 1–3, so the
  engine legitimately drops the run: the card, LAST RUN, SESSION BEST and the run number ARE
  cleared (`run_no` resets to 0, so the next run does not corrupt the `.log`/`.sum` with a repeated
  number) — but the card must read `NOT READY`, **never `READY`**, the moment the menu closes
  (final review I1: a `READY` card on an engine the reload just dropped to IDLE invites an untimed
  launch). Repeat the `Speed:` check once from the remote `CONFIG_SET` path
  (`lt config set {"units":"mph"}`).
- [x] **17. Venue announce on sim boot — `Layout:` cycles the real layouts, a post-reset VENUE
  one-shot names them correctly, and neither wipes BEST (final review I1/B4-F3, the venue half of
  #98; rescoped by the merge re-review's N1 — the original text described observables the shipped
  code does not, and cannot, produce on this build).** Procedure: a normal sim boot shows NO VENUE
  card — that is correct, not a failure (ruling B4-R2: the boot announce is deliberately
  announce-only, precisely so it can never replace a persistent screen; the card only appears
  later, on the engine's own layout lock). Open the menu: the `Layout:` item must step through the
  venue's REAL layout names, `Auto → Full → Auto` (never stuck on `Auto`, never the `L%u`
  fallback — the logged symptom before this fix was `menu: layout choice 0 -> id 0`). `dbg reset`:
  the engine re-finds the sim venue on its very next fix, and the VENUE one-shot this produces
  must show the real name (`Synthetic`/`Full`), never a fallback. Note the card's BEST (footer)
  value, cycle `Layout:` once more to a non-Auto choice, and confirm BEST still reads the value
  noted before the press — a `Layout:` press must never silently wipe the best-lap snapshot.
  **Not testable on `moto_sim`:** the mid-session RTC-resume half of this fix — `dbg crash`
  mid-lap, then on the next boot's first fix confirm the log reads `rtc resume: lap N venue M
  continued`. The §15.3 freshness gate (`age = fix->gps_us - saved_gps_us`, must be `> 0`,
  pipeline.c:492-493) always clears the snapshot on this build, because `gps_init()` seeds the
  capture's time base at `SIM_FIXES[0].gps_us` and `pipeline_init_drivers()`'s immediately
  following `gps_configure()` call then re-anchors it to `SIM_FIXES[0].gps_us` +
  `SIM_DRAG_PERIOD_US` (`sim_clock_init()`/`sim_clock_rearm()`,
  `components/drivers/gps_sim/gps_sim.c:134`) on every boot — the first fix after any reset still
  predates the saved snapshot either way, so `lap_import_rtc()` is never reached and the
  resume-site `ui_post_venue()` never runs. Tracked by #103 (make the resume path testable); until then the only coverage is host-side
  (`test_lap.c`'s `lap_import_rtc`-is-silent contract test) and real hardware, once item 6
  (`moto_neo6m`) is on the bench and GPS RTC carry-over exists. **Sim-boot half PASSED on bench day 5 (2026-10-10):** `Auto → Full → Auto` in words, the post-reset VENUE one-shot read `Synthetic` / `Full`, the footer BEST survived the cycling (a real layout change clears only the page-1 best-sector board, by design, ruling R-9).
- [ ] **F0/#102 — DRAG card on a silent transition.** In DRAG mode with the sim: `dbg sim drag`, then stall the run within 2 s of launch so the engine false-starts. Then let a full run reach `DONE` and wait > 5 s stationary: the card must stop reading `LAUNCHED` (false start) / `DONE` (settle) within one refresh — expect `READY` (the engine re-arms); `NOT READY` only in the short IDLE window right after the settle.
- [ ] **18. Drag run shows no `EV_FIX_LOST`** (F3 T1, drv:H2). `config set {"mode":"drag"}`, wait for
  READY/ARMED, then `dbg sim drag` via `lt shell`. Expect: NO `EV_FIX_LOST` and no `pipe: fix invalid`
  line for the whole run (before the fix, every frame past ~104 km/h was rejected and the bench-day-3
  drag pass was effectively IMU-only); the 100-0 brake gate hits, which it can only do with the 5 Hz
  Doppler re-anchor working. Also: `dbg sim laps 1` → `dbg sim drag` → `dbg sim park` in sequence, and
  check `lt status`'s free_kb/sessions stay sane across the switches (no gps_us rewind).
- [ ] **19. AP PSK printed and rotatable** (F3 T5, dk:H1). Cold boot the dev-kit on USB: the banner
  prints `SoftAP up: ssid=laptimer-dev psk=<12 chars>`. Join the AP with it; `devkit.py status` shows
  the same `ap_psk`. Reboot: the SAME passphrase (persisted in NVS `dc/psk`). `dc psk reset` prints a
  NEW one, `applied: true`, and the joined client is dropped and must rejoin with the new value.
  Then reboot again: the new one persists. **Redact `ap_psk` from anything pasted into a PR or docs.**
  (The first-boot-from-erased-NVS path and the NVS-failure suffix are NOT covered here — see item 25.)
- [ ] **20. `lt errlog` and friends relay** (F3 T6, dk:M4). On the dev-kit console: `lt errlog` prints
  the lap-timer's error ring, `lt diag` its diagnostics JSON, `lt dbg mem` its heap lines, `lt close`
  acks — each ending with the `-- rt N ms, attempts 1, rc 0` summary. `lt dc status` is REFUSED with
  "that is a dev-kit verb". `devkit.py lt errlog` returns the same body as JSON with `rc: 0`.
- [ ] **21. Logs tab lists every file with its creation time** (F3 T4, dk:L2). With ≥ 20 rotated
  dev-kit log files, the Logs tab lists them all (not 16) with a Created (UTC) column; note the load
  time (one open+read+close per file). The `rx_us` note reads "since the dev-kit last booted".
- [ ] **22. `logging` goes false while appends fail** (F3 T9, dk:M10). Fill the `logs` partition (leave
  the link streaming until rotation has run several times), or mount it read-only, and confirm
  `/api/status` and `dc status` report `logging:false`/`logstore.ready: false` while nothing is being
  written, then `true` again once appends succeed.
- [ ] **23. A paced USB OTA push completes** (F3 T10, drv:M8 mitigation). `tools/ota_push.py --port <lt>
  --image dist/<signed>.bin` with the dev-kit held in reset: expect `OTA-END 0x0000` and a total time
  ~3-4 s longer than the un-paced push, then the printed 35 s rule — and honour it. Repeat with
  `--pace 0` and record whether it also completes (the mitigation's value is the comparison).
  Note: a `devcontroller/web/*` change does NOT reach the dev-kit over OTA — the SPA lives in the
  `www` LittleFS partition and needs `cd devcontroller && idf.py -p <dk> flash` over USB.
- [ ] **24. `linkhost.c` glue** (F3 T3, dk:M1/M2 — no automated test covers this: it is on-target IDF
  glue, and the host harness only reaches the pure `host/*.c` half, so these bench items are its only
  verification).
  **(a) Normal download:** `lt list --json` for a session id, then `lt open <id> vbo --json`. Expect
  `"rc":0` and `"bytes"` matching the session's real size, finished in a few seconds.
  **(b) Busy while the mutex is held, `dc status` exempt:** from a browser, start a slow transfer that
  runs on the webapi worker task — open `http://<dk-ip>/api/session/<id>?fmt=vbo` for a large session,
  or start a web OTA push from the SPA's Flash tab — and WHILE it streams, on the dev-kit console run
  `lt status --json` (expect it answers BUSY/non-zero within about the bounded 400 ms mutex take, not
  after the whole transfer) and `dc status --json` (expect it answers normally with real fields — it
  takes no mutex). From a second browser tab: `GET /api/status` still answers normally (cache read);
  `POST /api/config` answers `503 {"error":"link busy, retry"}`.
  **(c) `lt shell` exits clean and resynced:** `lt shell`, type `status` on the lap-timer console, exit
  with `~.`. Expect: back at the `dc>` prompt immediately, usable; `lt status --json` right after
  succeeds with `rc:0` (proves the demux parser resynced rather than stuck mid-frame); `stream stats`'s
  seq-gap/ring counters read sanely (not reset to zero) — `linkstats` survived the bridge.
  **(d) A download cannot hold the link forever — not independently bench-testable** (would need a
  corrupted wire header to trigger; covered by `devcontroller/test/test_linkhost_dl_cap.c` and
  `test_linkhost_dl_gap.c`). Noted for completeness: an announced body over 2 MiB (`LH_DL_BODY_MAX`)
  is rejected rather than streamed, the absolute window is 20 min (`LH_DL_HARD_MAX_MS`) from the
  header, and the idle deadline is refreshed only by real progress (body bytes before `---END`, or a
  parser-state advance) — a normal download should finish in well under either bound; if one is ever
  approached on real data that is itself worth a follow-up, not evidence the bound misfired. The
  real residual of this item's no-automated-test ruling: the six-line idle-deadline wiring in
  `linkhost.c` itself (the `if (p != prog || dl.state != seen)` refresh) is covered by neither CI
  nor any bench item here, since `test_linkhost_dl_cap.c` tests only the pure `lh_dl_progress`
  helper it calls, not that call site — so a regression there would pass every gate.
  **(e) dk:M11's 15 s OTA-READY timeout and the dev-kit's own 25 ms OTA pacing (`OTA_READY_TMO_MS`
  / `OTA_PACE_MS`, both linkhost.c) are not independently bench-tested, but both ARE exercised by
  item 25(b)'s successful `flash push`:** a too-short `OTA_READY_TMO_MS` surfaces there as `flash
  result link -1` (the lap-timer's erase stalled the relay past the timeout) rather than
  `0x0000` — that is the one observable distinguishing a healthy push from a regression in either
  constant, so watch for it on every `flash push`.
- [ ] **25. Session 2 console behaviour** (F3 T5-T8, dk:M3/M8/H1).
  **(a) `flash stage` abort mid-stream:** `tools/devkit.py --port <dk> flash dist/<signed>.bin`; after
  `STAGE-READY` prints and raw bytes start streaming, Ctrl-C the host partway through (before
  `STAGE-END`). Expect: the console's stall timeout fires (`STAGE-ERR timeout`), and immediately after,
  `dc status` (or `tools/devkit.py --port <dk> status`) answers normally — the REPL is not left in raw
  mode and does not reboot. Re-run `flash stage`+`flash push` from scratch and confirm it still
  succeeds.
  **(b) The 35 s note, and never under `--json`:** push a signed image plainly
  (`tools/devkit.py --port <dk> flash dist/<signed>.bin`). Expect exactly ONE further line right after
  `flash result 0x0000`: `note: do not reset the lap-timer or open its USB port for 35 s (OTA trial
  window; an early reset rolls back, 0x0806)`. Then, on a plain serial terminal attached directly to
  the dev-kit console (not `devkit.py`, which never appends `--json` to `push`), stage another image
  and run `flash push --json`: the ENTIRE output must be the one JSON line and nothing else — no
  `pushing NN%` lines, no `flash result` text line, and critically the 35 s note never appears even
  though the result is again success.
  **(c) `selftest framing` still PASSes on real hardware:** `tools/devkit.py --port <dk> selftest
  framing`. Expect PASS, `markers:2`, `bad:0` — confirming the stricter one-CR rule (dk:M3) does not
  turn a normal, correctly-framed `status` round trip into a false FAIL.
  **(d) AP passphrase first-boot path:** `cd devcontroller && idf.py -p <dk> erase-flash && idf.py -p
  <dk> flash` (wipes the dev-kit's NVS, including `dc/psk`, and reflashes — this is the dev-kit, not
  the lap-timer, so the USB-port rule does not apply). On the fresh boot, the banner reads `SoftAP up:
  ssid=laptimer-dev psk=<12 chars>` with NO `(NOT persisted...)` suffix — the first-boot generation
  path (entropy drawn before Wi-Fi starts) ran cleanly. Reboot again without re-erasing: the same
  passphrase persists. The NVS-failure half of this path (the banner's
  `(NOT persisted: NVS error -- changes on reboot)` suffix) is NOT independently bench-testable
  without deliberately corrupting flash — confirmed present in `devcontroller/main/main.c` by code
  review only, not a bench pass/fail here.
- [ ] **30. Fault glyphs on the panel** (F1b T10, io:H1). `dbg flag set 1` → the strip shows a
  **hollow** GPS pin, visibly different from `dbg flag set 0`'s struck-through ring. Then
  `dbg flag set 2` (IMU "?" with a slash), `set 5` (floppy outline with a slash), `set 13` ("?"
  beside a leaning mast); `dbg flag clear <n>` after each. All four were invisible or wrong before
  this plan, so each is a first-light check at 12x12 on real e-paper: photograph the strip and
  confirm each glyph is distinguishable at arm's length.
- [ ] **31. Partial refresh after the dirty-box removal** (F1b T11, io:M4). The partial window has
  come from `fb_diff_rect` since 7c T7, so nothing should change — confirm on the panel that a page
  change still refreshes in full (ruling B-9) and a gate crossing still refreshes partially, with
  no stale rows. One lap of sim run is enough.
- [ ] **32. Session export after the exporter changes** (F1b T1-T3, io:C1/C2/M5). `lt open <session>
  vbo`, `lt open <session> nmea` and `lt open <session> json` (no `--json` wrapper on any of the
  three — that flag reports only `{cmd,rc,attempts,rt_ms,bytes}` and omits the body, and the body
  is exactly what this item inspects) on a sim session: every VBO `[data]` row ends with CRLF,
  every NMEA checksum verifies, the JSON parses whole and its `"skipped"` reads 0.

Also, `tools/devkit.py` now refuses an auto-picked port that does not answer `dc status --json`
within ~2 s and opens every port with DTR/RTS deasserted — if it refuses, you have the lap-timer's
port (identify, don't `--force-port`).

- [ ] **26. `cfg.lap.*` really reaches the engine (F1a, R24).** On the sim build with `dbg sim laps 10`
  running: `lt config set {"lap":{"min_lap_s":120}}` then `lt config get` (confirm 120 persisted) and
  watch the lap card — the ~28 s sim laps must stop completing while the one in progress keeps its
  clock (the reload applies in place, it does not reset the engine). Then
  `lt config set {"lap":{"min_lap_s":20}}` and confirm laps complete again. Second half:
  `lt config set {"lap":{"pit_speed_kmh":30,"pit_time_s":3}}`, `dbg sim park`, and confirm the next
  completed lap carries PIT. Before F1a every one of these keys was inert.
- [ ] **27. Leaving the venue closes the session (F1a emits `EV_VENUE_LOST`, F2b consumes it).** Needs
  F2b merged. `dbg sim laps 2`, then drive the sim origin > 3 km out for 60 s (or power-cycle with a
  venue-less capture): expect one `EV_VENUE_LOST` EVENT record with the venue id in the session, the
  session closed, and a new one opened on the next `EV_VENUE_FOUND`.
- [ ] **28. A layout override the venue does not have (F1a, R25).** `lt` → `CMD_SET_LAYOUT` with an id
  the sim venue lacks (e.g. 99): lap timing must keep working (the override is refused and the engine
  stays on Auto), not go silently dead for the rest of the session. The console gives no error yet —
  surfacing the −1 is F2b's follow-up below.
- [ ] **29. Best sectors do not cross venues (F1a, R25).** With the sim venue locked and a few laps
  banked, create a second venue on-device (NEW TRACK, item 5) and let it lock: the theoretical-best
  line must blank and then rebuild from the new venue's own splits, never show the old venue's number.

### F2a bench items (audit fix wave — firmware supervision + OTA, 2026-10-13)

Not yet run — bench day after F2b merges. Standing rules unchanged: flash by dev-kit OTA push
(`tools/devkit.py --port <dk> flash dist/<signed>.bin`), never reset or open the lap-timer's USB port for
35 s after a push (validation lands at 30 s), sign in place with `keys/laptimer_priv.pem`, and identify the
two ports first. `dbg flag` bit numbers are the `SYS_*` order in `app/lt_sup.h` (5 = `SYS_STORAGE_DEAD`,
10 = `SYS_HEAP_LOW`, 13 = `SYS_FUSION_DISAGREE`).
Error codes below are `lt_err.h`'s values; the spec's §17.7 OTA rows are numbered differently until plan F4
reconciles them (audit M9/M10), so trust the code table when a number disagrees.

- [ ] **33. A damaged `.log` no longer exports as a silently short file** (F2a T0, the F1b R26 drop-and-count follow-up).
  Damage a record type the format decodes in a CRC-valid frame (VBO/NMEA count dropped FIX/fused records;
  JSON counts dropped LAP/DRAG_RUN records). Then `open <id> vbo` and `open <id> nmea` on the console must
  print `ERR 0x0703: export: N records skipped` with NO `---BEGIN` frame; `open <id> json` must print a full
  frame whose body ends `"skipped":N` with a valid CRC32. An undamaged log exports all three with rc 0.
- [ ] **34. OTA preconditions on `moto_sim`** (F2a T4, R2/H1, plus the M11 battery skip). (a) `dbg sim laps 20`
  and, while it is lapping, push a signed image: the push is refused at OTA_BEGIN (`flash result 0x0801`
  via the dev-kit; `OTA-ERR 0x0801` on a direct `ota recv`) and the session keeps logging — `dbg fs` shows
  the `.log` still growing and `dbg sum <id>` reads `END present = no (session still open ...)`. A refusal
  is also an `errlog` entry: `0x0801` (`E_OTA_PRECOND`) with arg 3 (not still long enough; 1 in flight,
  2 size, 4 battery, 5 slot awaiting validation, 6 short payload, 7 session close not acked). (b) `dbg sim park`, wait >= 35 s (30 s of
  stillness plus margin), push again: accepted, the transfer completes (`flash result 0x0000`), and after
  the 35 s hold `dbg sum <id>` reads `END present = yes` — the begin closed the open session (logging is
  silenced during a push, so the `log: session <id> closed (reason 1)` line is NOT visible; the
  ordering "close before the first flash write" is covered by the host tests, not the bench). (c) `errlog`
  holds exactly one `0x0807` (`E_OTA_BATT_UNWIRED`) per boot — the §19.5 battery check is an explicit,
  logged skip on a build with no CHRG wiring, and every `ota_begin` evaluates it, refused or not.
- [ ] **35. Stack high-water after the supervisor's first 60 s integrity walk** (F2a T3, R6; also the #53
  follow-up). Let the device run >= 70 s, then `dbg mem` twice, once with the menu open and once riding.
  Record the pipeline / logger / supervisor / ui / console `B free` figures (the #53 reference, taken on
  an older image: pipeline 4924, logger 736, supervisor 1189, ui 340). Expect every figure > 256 B and
  `errlog` carrying no `0x0509` (`E_SYS_STACK_LOW`). There is no injection verb for a low stack, so this
  records that the detector is live and quiet; the threshold arithmetic is proven by `test_sup_policy`'s
  `test_stack_low`. If any figure IS under 256 B, a `0x0509` entry (arg = `task<<16 | headroom`) is the
  pass condition for that task.
- [ ] **36. Logger WDT: a LittleFS compaction longer than 5 s now panics** (F2a T3, R7 — intended).
  The logger is now subscribed to the 5 s task WDT (it was not before, so a wedged logger hung silently).
  There is no safe injection verb; this item records the intent and is checked indirectly by item 42's
  boot lines. If a real compaction ever trips it the symptom is a `TASK_WDT` reset (`errlog` `0x0501`, a
  coredump line on the next boot) — report it, do not mask it.
- [ ] **37. Buttons: per-pin 30 ms debounce, IRAM ISR** (F2a T6, L6/L17/C1). With the 6 mm tactiles (#78):
  press MODE and UP within 30 ms of each other, ten times; the ui log must show both `btn: short MODE`
  and `btn: short UP` every time (one shared 25 ms stamp used to drop the second edge). Hold UP+DOWN for
  2 s: `btn: combo UP+DOWN` and a `kind=F` refresh line. Start a session (`dbg sim laps 20`) and press
  buttons through a logger flush (every 2 s): presses arrive on time with no multi-second stall (the ISR
  is `IRAM_ATTR` and the service is installed with `ESP_INTR_FLAG_IRAM`, so flash-write cache stalls no
  longer delay edges). `dbg btn <name> <ms>` is unaffected (it bypasses the ISR).
- [ ] **38. `moto_neo6m` with the Plan-03 stub drivers tells the truth** (F2a T7, R28 — expected result, not a
  regression). On the USB-flashed `moto_neo6m` image the BOOT screen reads `GPS FAIL` and `IMU FAIL`
  (`gps_init`/`imu_init` return `-ENODEV` instead of faking an OK). The pipeline still starts and idles. OTA validation is NOT withheld on this build: `gps_profile_t.live == 0`
  makes `pipeline_sensors_ok()` true as soon as the pipeline has latched its driver init (a pipeline that
  never initialised stays fail-closed), so a pushed image validates at ~30 s unless `SYS_SAFE_MODE` or
  `SYS_STORAGE_DEAD` is set (item 40 is the end-to-end check). The fake 25.00 C IMU temperature is gone
  (`imu_read_temp_c100` returns `-ENODEV`), but nothing on the device prints that value, so there is no
  bench observation for it. The BOOT slots flip to OK when Plan 8 lands the real drivers.
- [ ] **39. Transfers survive a log storm** (F2a T8, R9/M12/M6). With the fs full — `dbg sim laps 200` until
  `E_STO_EVICT`/`E_STO_FULL` appear, so the logger's below-reserve `ESP_LOGE` is on the wire at ~1 Hz — and/or
  an assert storm, run `errlog`, `list`, `open <id> json`, `open <id> log` (to completion) and an OTA
  push. Every framed body must carry NO interleaved log line: verify the byte count and CRC32 against the
  `---BEGIN`/`---END` values on the host, and for the push confirm `OTA-READY`..`OTA-END 0x0000` is clean.
  After each transfer, normal `I (...)` lines must resume (the previous level was restored). An undersized
  response is an `ERR` frame, never a silently truncated body.
- [ ] **40. OTA validates on the production build** (F2a T2-T4, R1 — the audit's CRITICAL in this slice).
  Prerequisite: a signed `moto_neo6m` image, flashed once over USB (hold the dev-kit in reset via RTS on
  its adapter; `tools/sign_release.sh moto_neo6m`). Push a SECOND signed `moto_neo6m` image with
  `tools/devkit.py --port <dk> flash dist/<signed>.bin` and wait 35 s untouched. Expect `flash result
  0x0000`, the new version in the banner after the reboot, and at ~30 s `sup: OTA image validated
  (uptime 30s)` (31 s is fine) with NO GPS fix and no receiver attached — plus `errlog` carrying `0x0805`
  (`E_OTA_VALIDATED`) and NOT `0x0806`. Then power-cycle the lap-timer (not `dbg reset`, which only resets the lap engine) after the 35 s
  window: the NEW version must still be running (before this fix the bootloader reverted here, every time). Also push
  with a stale flag if one can be produced (an `ota_pending` left set with no PENDING_VERIFY slot): the
  push is accepted, `errlog` gains `0x0806` with arg 1, and the log shows `stale ota_pending cleared`.
- [ ] **41. `EV_FAULT` reaches both consumers** (F2a T3, R6). With a session open (`dbg sim laps 20`) note
  `EVENT=` from `dbg logck <id>`, then `dbg flag set 13` (`SYS_FUSION_DISAGREE`, no other writer), wait 2 s,
  `dbg flag clear 13`, wait 2 s. Expect the strip to gain and then lose the "? beside a leaning mast"
  glyph within ~2 s each way (the ui reads `sys_flags` directly and ignores the event), and
  `dbg logck <id>` `EVENT=` to rise by exactly 2 (one SET, one CLEARED `EV_FAULT`, type 15; the record's
  `arg` is `bit<<8 | set` = `0x0D01` then `0x0D00`; `dbg logck` counts records, it does not decode
  them — decode the `.log` on the host via `open <id> log` for the values). Repeat with bit 10
  (`SYS_HEAP_LOW`): the supervisor owns this bit, so with >= 48 KB of heap free its next tick
  clears the injected bit (`check_heap`, a `heap recovered` log line) BEFORE `emit_faults` runs; no
  change is ever observed, so expect `EVENT=` to rise by 0 (not 2), the `heap recovered` line, and no
  glyph (that bit has none, §17.4). Finish with bit 5 (`SYS_STORAGE_DEAD`, floppy-with-slash glyph); `dbg flag clear 5`.
- [ ] **42. Task-WDT coverage is real** (F2a T3, R7). Read the boot log (a read-only capture). Expect exactly
  these `sup: wdt subscribe hb=N rc=0` lines: `hb=1` (supervisor), `hb=0` (pipeline — printed AFTER its
  GPS/IMU init lines, not before), `hb=2` (logger); none for the link task (deliberate, §17.1) and none for
  the ui, which subscribes directly with `esp_task_wdt_add(NULL)` until F2b converts it
  (superseded: on the F2b build the ui subscribes through `sup_wdt_subscribe` and a fourth line `hb=3` appears — item 60). A missing or
  `rc != 0` line also leaves `E_SYS_WDT_SUBSCRIBE` (0x050D) in `errlog`. Then `dbg hang` and confirm the
  task WDT still panics in ~5 s as before.
- [ ] **43. OTA receive timeout on both transports** (F2a T4, M8/M7, §19.6). (a) Start `tools/devkit.py --port
  <dk> flash dist/<signed>.bin`, Ctrl-C the host at ~20 % and leave the lap-timer alone. Expect within
  ~30 s the supervisor's `ota: recv timeout after 30 s at <n>/<size> B` (visible only if the capture
  was opened outside any framed transfer), `errlog` `0x0702` (`E_CONN_XFER_ABORT`, arg = bytes
  received), and — with no reboot — a second push succeeding. (b) The 20-byte STATUS record
  (`lt status`) byte 1 (`state`) reads 1 (`LT_STATE_OTA`) right after the kill and 0 after the timeout
  fires. (c) A push that is progressing slowly but steadily (> 1 chunk per 30 s) is NOT aborted, and
  `ota_end`'s long verify never trips the timeout (the `OTA_VERIFY` state).
- [ ] **44. RTC snapshot reads are not torn** (F2a T5, L4). While `dbg sim laps 20` runs, issue `dbg rtc`
  twenty times. Expect 20/20 `rtc: VALID` with a sensible, non-decreasing `lap_no`, and zero `rtc: INVALID`.
  The unlocked read used to fail the CRC on a torn copy.
- [ ] **45. The §17.6 coredump row and the 60 s heap walk** (F2a T9 + T3, L3/§17.6/R6). `dbg crash`; on the
  next boot expect `coredump: <n> B at
  0x<addr>, check ok (kept)` and `errlog` `0x0502` (`E_SYS_PANIC`) whose `arg` is that byte count. Then a
  10-minute read-only capture: `hb[1]` (`dbg status`) must advance once per second throughout (the 60 s
  `heap_caps_check_integrity_all` walk must not perturb the supervisor's cadence) and no `0x050E`
  (`E_SYS_HEAP_CORRUPT`) may appear.

### F2b bench items (audit fix wave — firmware logger + cmd, 2026-10-13)

Not yet run — bench gate `f2b-d1`, after F2b merges. Standing rules unchanged: flash by dev-kit OTA push, never
reset or open the lap-timer's USB port for 35 s after a push, sign in place, identify the two ports first.
Log lines are `I (<ms>) <tag>: ...` with tags `log` (logger), `pipe`, `ui`, `sup`; they are silenced while a
framed console response or an `ota recv` push is in flight, so capture them outside those. Framed responses are
`---BEGIN <name> <size>---` ... `---END <crc32 hex>---` (`lt_proto.h`) and a refusal is
`ERR 0x<code>: <msg>`; every console command line is sent with a bare `\r`. `config set` takes the JSON with
escaped quotes (`config set {\"mode\":\"drag\"}`; `mode` is `"lap"|"drag"`, `units` is `"kmh"|"mph"`). Items
46-56 are the plan's own eleven; 57-64 cover what the Session B/C fix rounds added.

- [ ] **46. §12.6 still-pause** (F2b T2/T4, R4). On `moto_sim`: `dbg sim laps 2` (so the speed goes above
  3 km/h), then `dbg sim park`. Take `dbg logck <id>` (id from `dbg fs`) 2 s after the line
  `log: logging paused: still 10 s (§12.6)` appears (~10 s after the stop), wait 10 s, take it again. Expect
  `FIX=` and `FUSED=` identical in the two readings (other counts may grow: `EVENT`, `TIME_MAP`). Then
  `dbg sim laps 2`: `FIX=` grows again, the first record after the pause being a key frame, and `dbg sum <id>`
  still reads `HDR ok`, `frames: ok=N bad=0`. Evidence: the pause log line + the two `logck` lines.
- [ ] **47. §12.4 TIME_MAP + §12.3 CALIB** (F2b T4/T5, R4). `dbg logck <id>` on any session opened by this
  build: the line carries ` TIME_MAP=<n> CALIB=<n> END=<n> other=<n>`. Expect `TIME_MAP>=1` and `CALIB=1`
  (before this build both fell into `other=`). Leave the session open 70 s and repeat: `TIME_MAP` is at least
  one higher (60 s cadence, or sooner on a time-base quality change). Evidence: both `logck` lines.
- [ ] **48. §12.1 session rotation on a mode change** (F2b T5, R4). With a session open run
  `config set {\"mode\":\"drag\"}`. Expect, in order, `pipe: config reloaded (units <n>, mode 1)`,
  `log: session S<boot>_<k> closed (reason 0)`, `log: session S<boot>_<k+1> open`; `dbg fs` lists both
  `.log`/`.sum` pairs; `list` shows the older as `"mode":"lap"` and the newer as `"mode":"drag"`. Repeat
  with `{\"mode\":\"lap\"}`. The same two sessions appear when the mode is changed from the menu.
  Evidence: the three log lines + the `list` body.
- [ ] **49. Real-GPS build opens no session without a venue** (F2b T5, R4, audit §3.3 #25). Flash `moto_neo6m`
  (stub GPS): no venue is ever acquired, so the honest observable is the ABSENCE of a session and of any
  crash: `dbg fs` prints `/sessions:` then `(empty)` (only `dbg logtest` opens one there), `errlog` holds no
  `0x04xx` (`E_STO_*`) entry. The positive half needs the real driver (Plan 8, #93) and stays open there.
- [ ] **50. R5 tracks round trip** (F2b T6/T7). `tracks get`: a `---BEGIN tracks <n>---` frame whose body is
  a JSON array, one object per bundled venue with `"src":"bundled"` (and on `moto_sim` the sim venue,
  id 1004, as `"src":"user"`), ending `---END <crc32>---`. Then (one line, 256 B console cap):
  `tracks put {\"id\":900,\"name\":\"Bench\",\"lat\":-33.9,\"lon\":18.4,\"radius_m\":500,\"layouts\":[{\"id\":1,\"name\":\"L\",\"dir\":1,\"sf\":[[-33.9,18.4],[-33.9001,18.4001]]}]}`
  Expect a `---BEGIN tracks 0---` ... `---END 00000000---` ack, then `tracks get` lists id 900 with
  `"src":"user"`, and after `dbg reset` it is STILL listed (it went through `/tracks/user.bin`).
  Negatives: `tracks put {}` -> `ERR 0x0703: bad venue json`; repeat the SAME put (id 900) -> `ERR 0x0703: id exists`
  (the user table is append-only; the earlier venue is unchanged, `tracks get` still lists it once; I-3); repeat the put with NEW ids (901, 902, ...) until
  `ERR 0x0703: table full`. The table refuses once it already holds `TRK_MAX_USER` (4) user venues
  (`put_track` is refused when `trk_user_scratch()` finds no free slot, `user_n >= 4`), and the sim venue (id 1004) and anything already in
  `/tracks/user.bin` count. Starting from an EMPTY `user.bin` (check `tracks get` shows no other `"src":"user"`
  entries first) that is: on `moto_sim` the first 3 puts (900-902) succeed and the 4th refuses; on `moto_neo6m`
  the first 4 succeed and the 5th refuses; every venue already persisted pre-fills a slot and moves the
  refusal one put earlier. A re-put of an existing id while the table is full is refused as `table full` (the
  full check comes first; with room it is `id exists`). A PUT during an active New track creation answers
  `ERR 0x0703: creation in progress; retry`. Size: the console line caps a venue at about 245 B (I-6).
  Evidence: the frames and the two errors.
- [ ] **51. R5 coredump** (F2b T6/T7). `coredump` with nothing stored -> `ERR 0x0703: coredump: none stored`.
  `dbg crash`, let it reboot, then `coredump` -> `---BEGIN coredump <n>---` with a Base64 body and
  `---END <crc32>---`; decode host-side and check the ELF magic (`espcoredump.py info_corefile` is the full
  check). `coredump clear` -> an empty ack frame, then `coredump` -> `none stored` again. Evidence: the three
  responses.
- [ ] **52. R11 menu lock at 10 km/h** (F2b T5/T8). `dbg sim laps 5`; long-MODE (`dbg btn mode 1200`, or the
  real button) while the sim is between 3 and 10 km/h (the capture's slowest corner) and again above 10.
  Expect: the low-speed press OPENS the menu with no `menu locked` line; the high-speed press logs
  `ui: menu locked (<n> >= 10 km/h, §20.7)` with the real speed and the menu stays closed; the MOVING glyph
  is on only at or above 10 km/h (it follows `pipeline_speed_kmh() >= MENU_LOCK_SPEED_KMH`, the same figure as
  the lock). Evidence: the two log lines + a photo of the strip.
- [ ] **53. R10 cfg lock + only-on-change reload** (F2b T1/T6/T8). Run `config set {\"units\":\"mph\"}` and
  `config set {\"units\":\"kmh\"}` from the console while repeatedly toggling Units in the menu. Expect no
  silently reverted write: every change ends up in `config get`, and contention shows as
  `ui: units: cfg busy, not saved` or `ERR 0x0703: config busy; retry`, never a lost value. Also, a
  `config set` with the SAME values as `config get` logs no `pipe: config reloaded` line (§15.2 only-on-change)
  while one that changes a value logs it. Evidence: `config get` before/after + the absence of the reload line.
- [ ] **54. PR #101 M3: NOT SAVED card** (F2b T4/T8). Fill the filesystem below the 5 % reserve (`dbg logtest
  60000` repeatedly until `dbg fs` shows storage FULL), then create a track from the menu. Expect the VENUE card
  reads `VENUE` / `NOT SAVED`, `errlog` carries `0x0404` (`E_STO_FULL`) with arg 1 (the tracks writer; arg 0 is `open_session`'s), the
  venue is still armed (the Layout item offers it) but gone after a reset. Free the space again afterwards
  (`delete <id>` on the logtest sessions; never leave the filesystem at 0 free). Evidence: card photo + the
  errlog line.
- [ ] **55. PR #101 M4: resumed layout** (F2b T8). Lock a layout, let a lap run, `dbg crash` mid-lap, and on
  the next boot open the menu. Expect `Layout: <the locked layout's name>`, NOT `Auto`, and the first MODE
  press advances to the NEXT layout rather than silently switching away. Evidence: the two menu photos.
- [ ] **56. L9: ui stack high-water on BOTH deep paths** (context for #53). `dbg mem` right after a
  `drag_rows_refill` burst (`dbg sim drag`) and again right after a full refresh (UP+DOWN 2 s,
  `dbg btn up+down 2200`). Record both `stack ui` `B free` figures (the event branch
  `handle_event -> drag_rows_refill` and the render branch `render_and_refresh -> disp_refresh` are mutually
  exclusive, so #53's 340 B may have been taken on only one). Evidence: both `dbg mem` lines, pasted into #53.
- [ ] **57. Mid-lap reboot opens a NEW session carrying the resumed laps** (F2b T5 fix round, B5-5). On
  `moto_sim`: `dbg sim laps 5`, `dbg crash` during lap 2. After the reboot expect
  `pipe: rtc resume: lap <n> venue <id> continued (interrupted)` and `log: session S<boot+1>_001 open` (a new
  boot counter, so a new id; NOT the crashed session reopened). Let two more laps finish, then `dbg fs` lists
  the new pair and `dbg sum <new id>` reads `LAP count >= 2`, `VENUE ok`, `frames: ok=N bad=0`;
  `dbg logck <new id>` shows `LAP>=2`. The crashed session's `.sum` is untouched. Evidence: the two log lines +
  both `dbg sum` outputs.
- [ ] **58. A refused OPEN is retried and succeeds once space returns** (F2b T4/T5, B5-6). Starting from item
  54's full filesystem on `moto_sim`, `dbg reset`: the boot-time OPEN is refused
  (`log: storage below reserve (<free>/<total> KB): session not opened`). Without another reset, free space
  with `delete <id>` (a few old sessions, until `dbg fs` shows free above the reserve). Within ~60 s expect
  `log: session S<boot>_<k> open` and `dbg logck` of it growing. Evidence: both log lines + the timestamps.
- [ ] **59. SAFE MODE is persistent** (F2b T8, C8-2). Enter safe mode: three `dbg crash` in a row, each issued
  within 60 s of boot (§17.5). On the SAFE MODE card, `dbg btn mode 1200`, `dbg btn up 1200`,
  `dbg btn down 1200` and `dbg btn up+down 2200` (and the real buttons) must each leave the card up; only a
  reboot leaves it. Finish with `dbg safe clear` and a reboot (normal screen). Evidence: a photo after the four
  presses.
- [ ] **60. Task-WDT coverage now includes the ui** (F2b T8, R7; amends item 42). Read the boot log: expect FOUR
  `sup: wdt subscribe hb=N rc=0` lines: `hb=1` (supervisor), `hb=0` (pipeline), `hb=2` (logger) and `hb=3`
  (ui — it now subscribes through `sup_wdt_subscribe`, not `esp_task_wdt_add` directly); none for the link task.
  No `0x050D` (`E_SYS_WDT_SUBSCRIBE`) in `errlog`. Evidence: the four lines.
- [ ] **61. The RTC snapshot carries `partial_count`** (F2b T8, C8-5). `dbg rtc` prints `partial_count`. On
  `moto_sim`, `dbg sim laps 3`, let several lap-card updates (partial refreshes) happen, then, right after a lap
  completes (the save-on-gate), `dbg rtc` -> `rtc: VALID` with `partial_count : <n>` > 0. Limitation: the count is
  reset by each full refresh and only sampled at a gate crossing, so a reading of 0 right after a full refresh
  is correct — repeat until a gate follows a run of partials. Evidence: the `dbg rtc` output.
- [ ] **62. `dbg logtest` fixes come from the pipeline** (F2b T5/T7, R8). On `moto_neo6m` (no other fix source):
  note `fix_ring drop:` from `dbg fs`, run `dbg logtest 200`, wait >= 3 s (the logger writes its batch every 1 s),
  `dbg logck <id>` and `dbg fs` again. Every fix the logger pops while the session is open and not suppressed
  (safe mode, storage full, still-pause: none applies here, the synthetic fixes never reach the pipeline's
  motion edge) becomes exactly one `FIX_KEY` or `FIX_DELTA` record (`ses_encode_fix` always emits one, and
  `logck` counts both under `FIX=`), so expect `FIX=` equal to 200 minus the INCREASE in the `fix_ring drop:`
  figure (the 32-slot ring drops the newest on overflow; the counter is cumulative since boot). Also
  `frames ok=N bad=0`, `LAP=2` and no decode error — no spliced records, because the console is no longer a second producer. Repeat with
  `dbg logtest 60000` once to confirm the heartbeat stays alive (`dbg status` `hb[0]` advancing).
  Evidence: the `logck` line + `dbg fs` drop count. The session itself is opened by the PIPELINE (m-3: `dbg logtest`
  posts no `LOGGER_OPEN_SESSION` any more): with none open the console prints "asked the pipeline to open a session
  if none is open (venue 0)" and `dbg fs` shows one new session whose header has venue 0; with a session already
  open (e.g. `moto_sim`'s boot session) no new one appears and the laps/fixes go into it.
- [ ] **63. OTA tokens end in ONE CRLF; a refusal is the `0x0801` family** (F2b T7, L15). With the lap-timer
  moving (`dbg sim laps 20` on `moto_sim`), capture a refused push raw on the lap-timer port, e.g.
  `python3 -c "import serial,sys;s=serial.Serial(sys.argv[1],115200,timeout=1);s.write(b'ota recv 100 '+b'0'*64+b' v moto_sim_epaper\r');print(s.read(300).hex(' '))" <lt port>`
  (the last argument must be the device's own hwid, `moto_sim_epaper` on `moto_sim`, the string
  `tools/ota_push.py` reads from the image header: `ota_begin` rejects a mismatching hwid with `OTA-ERR 0x0802`
  BEFORE the §19.5 preconditions, so an arbitrary string never reaches them). Traced order: `ota_build_begin`
  accepts size 100, an all-zero 64-hex sha, any ver and hwid; `ota_begin` then checks length, in-flight, the
  hwid (0x0802), and `ota_precheck` -> `ota_precond_eval`: in-flight, size (0 or > max), STILL (< 30 s
  still), battery, slot -- size 100 passes, so while moving it refuses at STILL. The reply contains
  `OTA-ERR 0x0801` followed by exactly `0d 0a` (never `0d 0d 0a`). If the sim has been parked >= 30 s the begin
  would SUCCEED (and print `OTA-READY`, closing the session); keep it lapping. No flash happens, so
  the 35 s rule does not apply. `errlog` then carries `0x0801` with arg 3 (not still; item 34). On a real
  push (item 23) capture the same way: `OTA-READY` and `OTA-END 0x0000` each end `0d 0a`. Note: the
  `ota precondition: <reason>` text of `cmd.c`'s `OTA_BEGIN` only reaches a client through a framed transport
  (BLE/WiFi, sub-project C); the console and dev-kit paths use the `OTA-ERR 0x0801` token, so the reason is read
  from `errlog`'s arg. Evidence: the hex dump + the errlog line.
- [ ] **64. `list` shows the NEWEST 48 sessions** (F2b T6b + final review I-5; #91 follows in F5). Create more than 48 sessions: on
  `moto_sim` alternate `config set {\"mode\":\"drag\"}` / `{\"mode\":\"lap\"}` 52 times, 400 ms apart (each
  change rotates the session, item 48). `list`: the body holds exactly 48 `{"id":"S...` objects
  (`... | grep -o '"id":"S' | wc -l` -> 48), the frame CRC is valid, `dbg fs` shows more than 48 pairs, the
  NEWEST session id (the last in `dbg fs`) IS in the `list` body and the oldest ids are not.
  Delete a few (`delete <id>`) and `list` shows 48 again until fewer remain. Evidence: the count + the `dbg fs`
  pair count.
- [ ] **65. An OTA push does not reopen a session** (F2b final review I-1). With a session open for more than 60 s
  (`moto_sim` boot session, or a venue acquired) and the bike still >= 30 s, `dbg fs` and note the newest session id.
  Push an image (`flash push` / `tools/ota_push.py`); do not touch the lap-timer port for 35 s after it (item 23's
  rule). During and after the push the newest id is UNCHANGED: no `session ... open` line appears after `OTA-READY`
  (the log silence of the transfer holds), and after the reboot the new boot's session is the only new one. Before
  the fix a new near-empty session opened ~50 ms after the OTA close. Evidence: the `dbg fs` before and after.
- [ ] **66. New track at venue A rotates the session** (F2b final review I-4). On `moto_sim` (or a real venue) with
  a session open for venue A, start Menu -> New track, mark the gates and finish. `dbg fs` / `list`: A's session is
  closed (its `.sum` ends) and a NEW session opened for the created venue (venue_id = the new id). A's `.sum` holds
  only A's laps, the new one only the new venue's; no session carries two venue ids. Evidence: two `dbg sum` lines.
- [ ] **67. `tracks put` of an existing id is refused** (F2b final review I-3): see item 50 (`ERR 0x0703: id exists`,
  `creation in progress; retry` while New track is active). Evidence: the ERR lines.
- [ ] **68. TIME_MAP is written from a consistent pair** (F2b final review I-2). Console limit: `dbg logck <id>`
  only COUNTS records (`TIME_MAP=<n>`); it does not decode the gps/mono values, so the pair's consistency is NOT
  observable on the console (it is covered by the host decode tests). What can be checked: `dbg logck <id>` of a
  fresh session reports no decode error, and a session opened with GPS lost for minutes (parked indoors, sim
  stopped) shows `TIME_MAP=0` (none written rather than a wrong one) until a valid fix returns, after which a later
  `dbg logck` shows `TIME_MAP>=1`. Evidence: the `logck` lines.
- [ ] **69. Rotation never desyncs the lifecycle** (F2b closure N-1). `config set {\"mode\":\"drag\"}`, `dbg fs`
  shows the rotation (a second `.log`/`.sum` pair), then `config set {\"mode\":\"lap\"}` rotates again (a third
  pair), 5 s apart; repeat once with the flips 400 ms apart (item 64). Every flip must produce exactly one
  close + one open line; no flip may be skipped. Evidence: the log lines + `dbg fs`.

## Closed on 2026-10-10 (bench day 5)
Image `v0.1.0-47-g4ae6426` (= branch head, PR #101), dev-kit 076f327. Re-test of the four bench-day-4
fixes on hardware: **item 12** (the four card states, #95/B4-F1) PASS; **item 15** (list pairing,
units, menu names, B4-F4/F6) PASS; **item 13** (B4-F2: a `Distance:` flip keeps the run; a `Speed:`
flip clears it and the card reads `NOT READY`, never `READY`) PASS — the second half needed a
discriminating test, because on a parked sim the engine re-arms after ~2 s and `READY` is then the
honest end state: flipped `Speed:` remotely (`lt config set {"units":"mph"}`) while the sim was
MOVING, where the engine cannot arm, and the card read `NOT READY` and stayed so; **item 17**
(sim-boot venue announce, #98/B4-F3) PASS, see the item. **Item 5's #97 main path** passed on the
log (one-shot opens, advances per mark, the engine finishes on the closing S/F crossing and times
laps on the created 1-sector layout); its on-screen name and the sub-checks listed in the item are
still open, and its "appears in `lt list` / survives a reset" wording was rewritten (see the item
and #107). **Item 5's #99 LINK glyph was not run** (needs the operator to unplug the connector).
Side findings, all pre-existing and filed, none from this branch: #104 (`free_kb` read 0 with one
open session, 472 KB after a remount; the append path has no reserve guard), #105 (an orphaned
zero-byte `.sum.tmp` is never reaped), #106 (console INFO logging went silent mid-session until a
restart). A stale `storage flags: FULL` with 43 % free cleared with `dbg flag clear 6` and stayed
clear — a leftover bench injection, not a latch.

## Closed on 2026-10-07 (bench day 4)
Image `v0.1.0-37-g4153025`. Re-run of items 1 (drag run), 2 (OTA rollback), 3 (OTA push in
recovery), 4 (soak), 7 (storage full), 8 (OTA progress screen), 9 (SIM/MOVING glyphs) and 10 (#91
list timing) all passed (hence their day-3 ticks above still stand). Item 5's Layout half (#98)
and items 12/13/15's checks — 12 and 13 were already written (final review fix wave, see the
day-3 section below) but had not yet been run on hardware; only 15 was newly written for this
plan — together surfaced four defects, logged as
B4-F1..F6 and root-caused in `.superpowers/sdd/2026-10-07-bench-day-4-fixes/`:
B4-F1 (the drag card collapses IDLE/LAUNCHED onto one `NOT READY` text, #95), B4-F2 (a display-only
config reload could still drop a live run and corrupt the run numbering, #96), B4-F3 (the sim-boot
and RTC-resume venue paths never announced to the ui, so `Layout:` was dead and wiped `s_best`,
#98), and B4-F4/F5/F6 (the gate-list's label/value pairing, the distance-row unit and the menu's
renamed items, #96). All four are fixed by this plan (`glyphs-drag-menu-closure`, PR #101); items
12/13/15 above are their dedicated re-checks, and item 16/17 above are two further gaps the fix
wave's own final review (I1/I2) found in those re-checks themselves.

## Closed on 2026-10-05 (bench day 3)
items 1, 2, 3, 4, 7, 8, 9, 10, 11 (11 = the #65 re-test on 2026-10-05: 4/4 list relays clean, fresh loads without Reload). Open: 5 (menu stubs — Plan 6.2, Plan 8.4/#93; New track (#97) and Layout override (#98) are code-complete, pending this bench day), 6 (Plan 8 + hardware), and 12/13/14 (#95/#96/#99, added in the final review fix wave, I-6 — card text, `dist_units` menu+remote, and LINK glyph timing never had their own bench item before).

## Closed on 2026-10-04/05 (bench day 2, for reference)
boot lines; lap pages 0/1/2; mph via menu; menu/caret; live clock 1 Hz; 10-min soak; drag READY/ARMED
card; blob resets; OTA session close + summary; delete/bad-id; SAFE → RECOVERY → clear; link status;
config reload (menu + remote); engine reset; config ring; #67 download; #68 stream indication; #65
first load (after the dev-kit fix).
