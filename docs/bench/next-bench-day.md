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

- [ ] **1. Drag mode end-to-end** (prerequisite: `dbg sim drag` — plan `2026-10-05-ota-screen-and-glyphs` Task 3).
  Procedure: `config set {"mode":"drag"}` + reset (or the menu), wait for READY/ARMED, then `dbg sim drag`
  via `lt shell`. Expect: the card shows the launch and gate hits as they happen (sub-rect partials,
  log `ui: refresh ... kind=P`), `pipe:` lines for `EV_DRAG_LAUNCH`/`EV_DRAG_GATE`/`EV_DRAG_DONE`,
  DRAG page 1 LAST RUN lists the hit gates by NAME with times (0-60, 0-100, 60ft, 330ft, 1/8 ...),
  page 2 SESSION BEST filled, a second run updates bests only where faster. Repeat once with
  `config set {"units":"mph","drag":{"n_mph":2,"benches_mph":[60,100]}}` (#86): labels 0-60 / 0-100
  in mph, thresholds converted (the 100 mph gate fires at ~161 km/h in the log). Session log carries
  the EVENT records (`dbg sum <id>`). Note: the first drag launch after a boot may only teach the
  fusion its forward axis (`forward_ok`); run `dbg sim drag` twice and judge the second run.
- [ ] **2. OTA rollback path.** Push any signed image; at ~10 s after its boot (inside the 30 s trial
  window, before `sup: OTA image validated`) send `dbg crash`. Expect on the next boot: the PREVIOUS
  version in the banner, `sup: OTA image rolled back by the bootloader`, errlog `E_OTA_ROLLBACK`
  (0x0806), and the `UPDATE FAILED / REVERTED` one-shot for 3 s right after the BOOT screen (plan
  `2026-10-05-ota-screen-and-glyphs` Task 2). Then push the image again normally and let it validate.
- [ ] **3. OTA push while in recovery mode** (debt sweep A P-8, spec §17.5 amendment). Enter recovery
  (`gate_dsA.py safe` up to the RECOVERY boot, or 5× `dbg crash` 12 s apart), then push a signed
  image. Expect: the push succeeds (console alive in recovery), `sup: OTA reboot: safe/recovery gate
  cleared for the new image`, the new image boots NORMALLY (safe_mode=0) and validates at 30 s.
- [ ] **4. Long soak ≥ 35 min with the live clock on.** `gate_p07c.py clock` then a 35-minute read-only
  capture (`gate_p07c.py soak` is 10 min — run it 4× back to back or extend). Expect exactly one
  age-out full (`kind=F` ~30 min after the previous full), 1 Hz cell-sized partials otherwise, 0 W/E,
  no WDT/reset. Moving-cap full (`partial_count >= 2*full_every` while moving): needs continuous
  motion for > 2×full_every partials — use `dbg sim laps <n>` with n ≥ 20 (Task 3) and watch for a
  `kind=F` while the sim is still lapping.
- [ ] **5. Menu paths never exercised.** With the sim parked: Sleep now (3 s MODE hold on the item →
  sleep; wake by a button → BOOT screen, session continues/new), Calibrate ("Hold upright, press
  MODE" one-shot → `EV_CALIB_DONE` in the log), New track (CREATE mode: the NEW TRACK one-shot, cross
  S/F twice with `dbg sim laps`, press MODE → a user venue appears in `lt list`/`trk`), layout
  override (forced layout id → the VENUE one-shot shows the forced name).
- [ ] **6. Real-GPS smoke (`moto_neo6m` image).** Flash the neo6m build (USB with the dev-kit held in
  reset, or OTA if the hwid matches — check `CFG_HWID`). Indoors: BOOT line `GPS OK`, the NOFIX
  icon in the strip, `EV_FIX_LOST` after FIX_LOST_COUNT invalid fixes; outdoors: fix, speed on the
  card, no SIM glyph. (Venue detection on a real track is a ride test, not a bench item.)
- [ ] **7. Storage full as a planned gate.** Leave the sim lapping (`dbg sim laps 200`) until
  `E_STO_EVICT`/`E_STO_FULL` appear (~2-3 h at 10 Hz fused logging; or pre-fill the fs with
  `dbg logtest`). Expect: eviction keeps a reserve, the open session keeps writing, `lt list`
  answers within the dev-kit's 12 s first-byte window (#91 measures the cost), no WDT, no fs deadlock.
- [ ] **8. OTA progress screen** (plan `2026-10-05-ota-screen-and-glyphs`). During a push: UPDATING,
  bar + percent climbing every 5 %, then VERIFYING, then REBOOTING, then the new image's BOOT screen.
  An aborted push (`flash abort` on the dev-kit mid-transfer) returns to the riding screen.
- [ ] **9. SIM + MOVING glyphs** (same plan). Sim build: SIM glyph always present in the strip; MOVING
  glyph present while the sim laps, gone once parked; the menu opens only once MOVING is gone.
- [ ] **10. #91 list cost** (if the lap-timer fix has landed): `lt list` round trip on the dev-kit
  console (`-- rt N ms`) with ≥ 40 sessions, before/after.
- [ ] **11. Dev-kit first-load after a cold boot** (#65 regression check): power-cycle both boards,
  open Sessions and Config fresh 5× each — 10/10 without Reload.

## Closed on 2026-10-04/05 (for reference)
boot lines; lap pages 0/1/2; mph via menu; menu/caret; live clock 1 Hz; 10-min soak; drag READY/ARMED
card; blob resets; OTA session close + summary; delete/bad-id; SAFE → RECOVERY → clear; link status;
config reload (menu + remote); engine reset; config ring; #67 download; #68 stream indication; #65
first load (after the dev-kit fix).
