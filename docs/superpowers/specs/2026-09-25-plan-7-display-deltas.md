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

- `CFG_PANEL_WS213V4` → logical canvas **250×122**; `CFG_PANEL_WS29V2` → 296×128. The `ui` task keeps its static 4736-byte framebuffer (sized for the larger panel, §4.8) and initialises `fb_t` with the panel's dimensions (3904 bytes used on the 2.13").
- One header, `core/ui/canvas.h`, owns the dimensions and the per-canvas layout constants; `screens_moto.c` uses those constants only (no literal 296/128/260 coordinates). Rule: **nothing renders below `y = height` or right of `x = width`** — the host test asserts the renderer's clip counters stay zero for every golden.
- §20.5 font scaling on the 122-px canvas (BIG is never used there):

| Row | 296×128 (unchanged) | 250×122 |
|-----|---------------------|---------|
| LAP BEST | y=4, `FONT_BIG`, time right x=200 | y=2, `FONT_MED`, right x=170 |
| LAP PREV | y=46, `FONT_BIG` | y=28, `FONT_MED` |
| LAP CUR + sector | y=88, `FONT_MED`, right x=180, `S2` at x=210 | y=54, `FONT_SMALL`, right x=150, `S2` at x=160 |
| LAP ΔS | y=112, `FONT_SMALL` | y=70, `FONT_SMALL` |
| DRAG rows (4) | `FONT_MED`, right x=180, `@` at x=190 | y=2/28/54/80, `FONT_MED`, right x=150, `@` at x=160 |
| Fault strip | x0=284, y=116 | x0=238, y=110 |
| Page 1/2, one-shots, menu | as §20.5–20.7 | same fonts (`FONT_MED` titles, `FONT_SMALL` rows), rows packed at 14 px pitch from y=2; the menu shows 7 rows instead of 8 |

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
