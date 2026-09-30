# Plan 7 (Display) — design deltas to the lap-timer spec §20

**Status:** approved in brainstorm 2026-09-25 · **Base:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §3.3, §3.4, §4.8, §17.2, §20, §22.3 · **Roadmap:** Plan 7 runs now, ahead of Plan 6 (power parts not yet sourced); Plan 6 is otherwise unchanged.

This document only records what differs from, or is left open by, the base spec. Everything not listed here (SSD1680 command sequences, refresh policy, ladder, screen model, menu, buttons) is implemented exactly as the base spec states.

## 1. Hardware (owned panel)

| Item | Base spec | Plan 7 |
|------|-----------|--------|
| Panel | BOM #4: Waveshare 2.9" V2 (`ws29v2`, 296×128) | **Waveshare 2.13" e-Paper HAT rev 2.1, panel V4** (SSD1680, 122×250 native portrait, **250×122 landscape logical**). Spec §20.1 row `ws213v4` applies: on-chip LUTs (`0x22 0xF7` full, `0x3C 0x80` + `0x22 0xFF` partial), border `0x05`, RAM width 128 (16 bytes/row). |
| Interface | 4-wire SPI | Confirmed: the module's `BS` pad is strapped to 0 (4-wire, DC line used). |
| `PANEL` build flag | default `ws29v2` | **default `ws213v4`** for `moto_*` builds (the owned hardware); `ws29v2` stays selectable and keeps its 296×128 goldens. The OTA `hwid` string is unchanged (`<variant>_<gps>_epaper`). |
| E-paper RST | GPIO 4 | **GPIO 13** (MTCK; output-capable, no strapping role, unused). GPIO 4 stays the dev-kit **DETECT** line (Plan 5.5/5.6, RJ45 pin 8). §3.3 table row and §3.4 wiring line change to `RST 13`; `board_devkit_v1` leaves 13 alone at init exactly as it does 14/35 today (owned by the display driver). |
| Other pins | — | unchanged: CLK 18, DIN 23, CS 5 (hardware CS, idle high), DC 14, BUSY 35, 3V3, GND. MISO 19 unused by the panel. |
| Buttons | GPIO 32/33/25 | unchanged; the switches arrive after the panel, so the plan orders button work last and adds the console injection in §4. |

## 2. Canvas rule (compile-time, by `PANEL`)

- `CFG_PANEL_WS213V4` → logical canvas **250×122 visible**, backed by a byte-aligned **256×122** buffer (`CANVAS_W` 256 / `CANVAS_VISIBLE_W` 250 / `CANVAS_H` 122 — see §8); `CFG_PANEL_WS29V2` → 296×128, already byte-aligned (no padding). The `ui` task keeps its static 4736-byte framebuffer (sized for the larger panel, §4.8) and initialises `fb_t` with the panel's dimensions (3904 bytes used on the 2.13").
- One header, `core/ui/canvas.h`, owns the dimensions and the per-canvas layout constants; `screens_moto.c` uses those constants only (no literal 296/128/260 coordinates). Rule: **nothing renders below `y = height` or right of `x = width`** — the host test asserts `fb_max_ink_col() < CANVAS_VISIBLE_W` for every golden (no renderer draws ink past the panel's visible width; `fb_clear()` itself dirties the whole padded buffer every render, so a zero-dirty-counter check would not be meaningful here — see `test/test_screens.c`).
- §20.5 font scaling on the 122-px canvas (BIG is never used there):

| Row | 296×128 (unchanged) | 250×122 |
|-----|---------------------|---------|
| LAP BEST | y=4, `FONT_BIG`, time right x=200 | y=2, `FONT_MED`, right x=170 |
| LAP PREV | y=46, `FONT_BIG` | y=28, `FONT_MED` |
| LAP CUR + sector | y=88, `FONT_MED`, right x=180, `S2` at x=210 | y=54, `FONT_SMALL`, right x=150, `S2` at x=160 |
| LAP ΔS | y=112, `FONT_SMALL` | y=70, `FONT_SMALL` |
| DRAG rows (4) | `FONT_MED`, right x=180, `@` at x=190 | y=2/28/54/80, `FONT_MED`, right x=150, `@` at x=160 |
| Fault strip | x0=284, y=116 | x0=238, y=110 |
| Page 1/2, one-shots, menu | as §20.5–20.7 | same fonts (`FONT_MED` titles, `FONT_SMALL` rows), rows packed at 14 px pitch from y=2; the menu shows 6 rows (`MENU_VISIBLE_ROWS`, see §8) at 14 px `MENU_ROW_H`, vs. the 296×128 canvas's 4 rows at 24 px |

  The pixel values above are the starting layout; the committed PBM goldens (eyeballed as PNG, the existing workflow) are the acceptance artefact, and a value may move by a few pixels during that review without a spec change.

## 3. DRAM (lap-timer, measured 2026-09-25 on main `4550f6e`)

`moto_neo6m` 114 732 B used / 9 848 B free; `moto_sim` 124 188 B / 392 B free. The sim's extra 9.5 KB is two sim-only statics: `trk_json.c`'s token array (`MAX_TOKS` 512 × 20 B = 10 240 B, linked only when the venue JSON path exists) and the pipeline's sim-venue copy (2 752 B). The GPS capture (`SIM_FIXES`, 28.8 KB) is already in flash.

Plan 7 needs on every build: the `ui` stack back to **6144 B** (`ui.c` already documents this restore), the driver's 128-byte rotation line buffer, and the SPI device (allocated once at init by the IDF driver, like UART/littlefs). Required steps, in order, each reported with the `idf.py size` DRAM line:
1. Size `trk_json.c`'s token array for the device: a `CFG_TRK_JSON_TOKS` of **64** on firmware builds (the sim venue JSON is 330 B, ~40 tokens); the host tools/tests keep 512. Expected: −8 960 B on `moto_sim`.
2. Restore `UI_STACK_BYTES` 6144 on all builds (+3 584 B on `moto_sim`).
3. If clean, fold the pipeline's sim-venue copy into the existing user-track table instead of a separate static (−2 752 B); otherwise leave it and say so.
Target after the driver lands: ≥ 5 KB free on `moto_sim`, ≥ 6 KB on `moto_neo6m`. Recorded for sub-project C (not Plan 7): the user-track table (`trk.c`, 11 008 B) and the dev-only `dbg` command family are strip/shrink candidates; the framed commands the dev-kit relays are the production link and stay.

## 4. Button injection (until the switches arrive, and for bench scripts after)

Lap-timer console: `dbg btn <mode|up|down> [hold_ms]` posts the same `{mask, mono_us}` press/release pair into `btn_q` that the GPIO ISR would (press now, release after `hold_ms`, default 100; ≥ 1000 = long, ≥ 3000 = very long; `dbg btn up+down 2000` for the combo). It exercises the debounce/press classification unchanged. Reachable from the dev-kit as `lt shell` → `dbg btn …` today and as an `lt` relay later if a bench script needs it.

## 5. Driver and `ui` wiring (deltas only)

- `hal/display.h` is created exactly as the base spec's `hal/display.h` block (`disp_caps_t`, `disp_init/blit/refresh/sleep/wake/reinit`, plus `int disp_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)`; `x`/`w` rounded outward to 8-px columns by the driver). Component `components/drivers/display_epaper_ssd1680/`, selected by `DISPLAY=epaper_ssd1680`, panel table by `PANEL`.
- Pure, host-tested parts of the driver live in `display_epaper_ssd1680/host/`: the landscape→portrait RAM transposition (`epd_rotate_line()`), the dirty-window rounding, and the panel table. The IDF glue (SPI, GPIO, BUSY poll) is target-only.
- The refresh decision of §20.3 (partial / full / defer-until-still / throttled / none) is a pure function `ui_refresh_decide()` in `core/ui/refresh_policy.c` with a host test; `ui.c` calls it once per drained batch. The ladder counters live in `ui.c` (target glue), the errlog codes `E_DISP_DEAD` (§17.7) and `SYS_DISP_DEAD` (§17.2) are added if missing.
- Boot screen: drawn and full-refreshed once at the end of `disp_init` (session 7.1 exit criterion); later boots follow §20.3 (full refresh on wake/boot).

## 6. Testing and gates (deltas only)

- Host: goldens for both canvases (`test/snapshots/` 296×128 unchanged, `test/snapshots/213/` new); `epd_rotate_line` exact-bytes tests (all-black, all-white, checkerboard, one-pixel corners); dirty-window rounding; `ui_refresh_decide` table test; `dbg btn` argument parsing. Zero warnings, lint 0, gcc-16 sweep, DRAM line per task, as in Plan 5.6.
- On target, from the dev-kit: 7.1 boot screen legible (photo in the ledger); 7.2 LAP screen updates on sim laps via partial refreshes, `stream stats` still ≈ 10 fused/s during refreshes, no task-WDT; 7.3 ghosting (50 partials then one full), ladder by lifting BUSY (→ `E_DISP_DEAD` in `errlog`, recovery on reattach), throttle via `dbg` forcing `SYS_DISP_TEMP_THROTTLE`, then real buttons: debounce, short/long/very-long, UP+DOWN combo, menu navigation.
- Bench rules carried over: never open the lap-timer's serial port within 35 s of an OTA push; commands end in `\r`.

## 7. Sequencing

7.0 prep (pin change + spec §3, `PANEL` default, canvas header + 213 goldens, DRAM steps 1–3, `hal/display.h`) → 7.1 driver + boot screen (flash gate, `p07-d1`) → 7.2 partial refresh + `ui` wiring + policy + ladder (gate, `p07-d2`) → 7.3 ghosting/throttle/buttons/menu (gate, `p07-d3` = `plan-07-done`) → docs (roadmap: Plan 7 done, Plan 6 next; BOM #4 → the 2.13" V4 as owned). Subagent-driven, one worktree, reviews per task, final whole-branch review, PR.

## 8. Implementation notes (bench 2026-09-27)

**Hardware.** Confirmed as built: Waveshare 2.13" e-Paper HAT rev 2.1, panel V4 (SSD1680, 122×250 native, 250×122 landscape), owned since 2026-09-25. The `BS` pad is strapped 0 (4-wire SPI). Wiring: CLK GPIO 18, DIN 23, CS 5, DC 14, RST 13, BUSY 35 (RST moved off the spec's original GPIO 4, which is the dev-kit's **DETECT** line). The 2.9" V2 (`ws29v2`) stays the alternative panel behind the `PANEL` build flag; default is now `ws213v4`.

**Canvas.** The 2.13" framebuffer is 256 px wide (byte-aligned) with 250 visible (`CANVAS_W` 256 / `CANVAS_VISIBLE_W` 250 / `CANVAS_H` 122). `fb_clear()` dirties the whole padded buffer, so the driver's window is clamped to 250 and the host tests check ink columns < 250. `MENU_VISIBLE_ROWS` is 6 on the 2.13" (4 on the 2.9"). A second golden set lives in `test/snapshots/213/`.

**DRAM (`moto_sim`, internal DRAM free):**

| Point | Free | Change |
|-------|------|--------|
| Before Plan 7 | 392 B | — |
| After T1 (`CFG_TRK_JSON_TOKS` 64, sim venue parsed into the user-track table, `ui` stack back to 6144) | 8 520 B | device-sized track-JSON tokens |
| After the driver (T5) | 8 264 B | |
| After the `ui` wiring (T7) | ≈ 8 232 B | |

Production `moto_neo6m` is unaffected (~9.5 KB free throughout). Sub-project C strip candidates recorded here: the user-track table (~11 KB) and the dev-only `dbg` command family.

**Driver (`display_epaper_ssd1680`).** SPI3 at 10 MHz mode 0, DC driven from the transaction pre-callback, polling transactions only. BUSY is polled at 1 ms with a 5 s timeout that pets the task WDT every 500 ms and aborts the sequence on the first timeout. Writes ≤ 4 bytes go through `SPI_TRANS_USE_TXDATA` — the DMA-enabled bus cannot read flash-resident constants directly. `disp_blit()` called before `disp_init()` lands the boot screen inside init's own full refresh. A failed partial restores the border and consumes the window; the ladder's retry runs as a full, not a repeated partial. Framebuffer polarity matches the panel (0 = black); no inversion.

**Measured on the bench (2026-09-27, `moto_sim`, v0.1.0-47):** full refresh ≈ 3.2 s end-to-end (button injection to log line, minus ~0.3 s console latency; includes both RAM-plane writes, rotation, and BUSY polling), partial ≈ 0.7 s. `disp_caps` still reports the datasheet-class 2000/400 ms.

Refresh policy verified from the bench logs: partials fire on page changes while moving; a forced full fires at 2×`full_every` (20) partials while moving; full refresh fires on menu entry/exit and on page changes while still; the temperature throttle (`dbg flag set 11`) limits partials to one per 30 s with fulls suppressed; an externally set `SYS_DISP_DEAD` self-heals on the next loop (reinit succeeds); menu open/scroll/select/auto-exit (30 s) all drive the panel; a 10-minute soak ran with no task WDT.

`display.live_clock` was stored but had no effect (the screen model carried `cur_ms_at_gate`, not a running lap time, so a 1 Hz tick would have refreshed identical pixels — ruling T7-R7): **done, Plan 7c §4** — the model now carries `cur_ms`/`cur_running`, ticked from `s_lap_start_mono_us`, and the LAP page 0 footer shows `CUR m:ss` while a lap runs. Similarly, `E_DISP_TEMP` (0x0303) and `SYS_DISP_TEMP_THROTTLE` have no producer yet: nothing sets the throttle from a real over-temperature reading until the MPU6050 temperature channel lands (Plan 8); `dbg flag set 11` (§4 / `export_serial.c`) is the only setter today, for bench-exercising the throttle path in §20.3 — **still open, #82, Plan 8**. Separately, the refresh "dirty window" being always the full visible frame — because every screen render cleared the framebuffer first, so partials were full-window partials — is now **done, Plan 7c T7 (#84)**: `render_and_refresh()` diffs the freshly rendered frame against the last frame the panel actually accepted (`fb_diff_rect`, `core/ui/render.c`) and a partial's window is clamped to just the changed rectangle; an identical frame costs no refresh at all. Likewise the BOOT one-shot's self-test lines (`boot_n_lines`/`boot_line[]`, rendered by `render_oneshot_boot()`) had no producer (`ui_task()` always set `boot_n_lines = 0`, so the boot screen never showed any check lines): **done, Plan 7c §6** — `app_main.c`/`ui.c`/`pipeline.c` now report into a four-slot boot status table and the ui formats it onto the BOOT screen's four lines.

**Bench findings fixed in Plan 7 (branch `p7-t9`):**
1. `gps_sim` now parks at the end of its capture (delivers the last fix at speed 0, 1 Hz) instead of going silent, so `EV_STILL` arrives and the menu can be used on the bench.
2. `dbg btn` injected holds must exceed the 1000/2000/3000 ms thresholds by ≥ 100 ms (e.g. `up+down 2200`); usage text updated.
3. The logger's 60 s eviction scan used a per-entry `stat()` (O(n²) on LittleFS) and starved the ui past the task WDT at ~90 sessions — now a name-only scan, O(n).
4. LittleFS reached 0 free blocks (at which point even `unlink()` fails) — the logger now keeps a reserve: it refuses to open a session below 5 % free, evicts in bounded multi-file passes (max 8 per pass) until 15 % free, and pairs the `SYS_STORAGE_FULL` flag with its latch. See spec §12.7 (updated alongside this doc) for the 5 %/15 %/8-per-pass numbers, on top of the existing 10 % trigger.

**Bench procedure notes.** With the dev-kit's link wired to the lap-timer's UART0, USB `esptool` flashing fails (shared RX) — flash via the dev-kit's OTA push, or hold the dev-kit in reset over its own USB adapter (RTS asserted) for a USB flash. Console commands go through `lt shell`. The lap-timer's own USB log is read-only-usable.
