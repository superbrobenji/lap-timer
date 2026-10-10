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
  pipeline.c:492-493) always clears the snapshot on this build, because `gps_init()` restarts the
  capture's time base from `SIM_FIXES[0].gps_us` on every boot (`sim_clock_init()`,
  `components/drivers/gps_sim/gps_sim.c:124`) — the first fix after any reset predates the saved
  snapshot, so `lap_import_rtc()` is never reached and the resume-site `ui_post_venue()` never
  runs. Tracked by #103 (make the resume path testable); until then the only coverage is host-side
  (`test_lap.c`'s `lap_import_rtc`-is-silent contract test) and real hardware, once item 6
  (`moto_neo6m`) is on the bench and GPS RTC carry-over exists. **Sim-boot half PASSED on bench day 5 (2026-10-10):** `Auto → Full → Auto` in words, the post-reset VENUE one-shot read `Synthetic` / `Full`, the footer BEST survived the cycling (a real layout change clears only the page-1 best-sector board, by design, ruling R-9).

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
