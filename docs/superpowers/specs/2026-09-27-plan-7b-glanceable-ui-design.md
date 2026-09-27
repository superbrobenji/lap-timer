# Plan 7b — Glanceable riding screens (design)

**Status:** approved design, 2026-09-27. Amends base spec §20.2 (fonts) and §20.5 (riding screens) of `2026-09-14-lap-timer-design.md`; everything else in §20 stands. Builds on Plan 7 (`p7-display`, PR #76).

## 1. Problem and goal

On the owned 2.13" panel (250×122 visible) the Plan 7 screens use 24 px digits and 12 px labels and leave the lower 40 % of the panel empty (photos in the Plan 7 ledger). A rider glances for well under a second: one number must dominate, at roughly half the panel height, and every other value must be readable without hunting.

What current dashes lead with (AiM Solo 2, Garmin Catalyst, PZRacing Start Next, Apex Pro): the **delta versus the best lap** (sign + tenths, colour or LED coded), then the **last lap time**, then the **best lap**; sector splits and session stats sit on secondary pages. E-paper cannot show a live clock or a live predictive time (partial refresh ≈ 0.7 s, full ≈ 3.2 s), so the design is **event-driven**: the screen changes at gate crossings and at the line, and is static in between. That matches how the rider uses it — glance after the gate.

## 2. Fonts (amends §20.2)

`tools/fonts/gen_fonts.py` (DejaVu Sans Mono Bold, 50 % threshold, monospace cells):

| Font | px | cell (w×h) | glyphs | use |
|---|---|---|---|---|
| `FONT_HUGE` (new) | 64 | 39×64 | `0-9 : . - +` (14) | the big slot |
| `FONT_MED` | 24 | 14×24 | `0-9 : . - + A-Z` | values, markers |
| `FONT_SMALL` | 12 | 7×12 | ASCII 32..126 | labels, footers |

`FONT_BIG` (40 px) is removed from the generator, `fonts.c` and `fonts.h` in this plan: the redesign leaves it no user on either canvas (`LAP_TIME_FONT`/`LAP_CUR_FONT` in `canvas.h` go away with the old LAP rows). Flash cost of `FONT_HUGE`: 14 glyphs × 64 rows × 5 bytes = 4480 B; no DRAM. Digit ink height at 64 px is ≈ 47 px (cap height), i.e. 2.7× today's 24 px font.

Width budget (250 px visible): `FONT_HUGE` fits six glyphs (234 px) with 4 px margins; a lap time (`m:ss.hh`, 7 glyphs) does **not** fit in `FONT_HUGE` and never goes in the big slot.

## 3. Screen model additions (§20.4)

```c
enum { BIG_NONE = 0, BIG_SECTOR_DELTA = 1, BIG_LAP_DELTA = 2 };
uint8_t  big_kind;          /* what the big slot shows */
int32_t  big_delta_ms;      /* signed; valid for BIG_SECTOR_DELTA / BIG_LAP_DELTA */
uint8_t  big_sector_idx;    /* 1..n for BIG_SECTOR_DELTA */
uint16_t lap_no;            /* running lap number: laps_total + 1 while a lap is in progress */
int32_t  last_sector_delta_ms[LAP_MAX_SECTORS + 1];  /* page 1 row 4; index = sector idx */
bool     have_last_sector_delta[LAP_MAX_SECTORS + 1];
```

`ui.c` fills them from events (all payloads already exist):
- `EV_SECTOR` (arg16 sector idx, arg32 split ms, arg32b delta ms): `big_kind = BIG_SECTOR_DELTA`, `big_delta_ms = (int32_t)arg32b`, `big_sector_idx = arg16`, `cur_sector_idx = arg16`; `last_sector_delta_ms[idx] = delta`, `have_last_sector_delta[idx] = have_best` (a delta is meaningful only once a best lap exists — `EV_SECTOR` sends 0 for both "zero" and "no best yet").
- `EV_LAP_COMPLETE` (arg16 lap no, arg32 lap ms, arg32b lap delta ms, flags): existing PREV/BEST/valid handling from Plan 7 bench fix 1 stays; then `big_kind = BIG_LAP_DELTA`, `big_delta_ms = (int32_t)arg32b` (when a best existed before this lap; else `BIG_NONE`), `new_best` as today, `lap_no = laps_total + 1`, `cur_sector_idx = 0`, and the `have_last_sector_delta[]` flags are cleared (the next lap's sector deltas replace them as they arrive). Out-laps (`LAP_F_OUT_LAP`) do not touch the big slot.
- Session start / first lap: `big_kind = BIG_NONE`, `lap_no = 1`.
- Everything else in the model is unchanged; rendering stays a pure function of the model.

## 4. LAP page 0 — the event card (amends §20.5)

Mockup at 2.13" scale (250×122):

```
┌──────────────────────────────────────────────┐
│                                       L7 S2  │  FONT_SMALL marker, right-aligned at x 246, y 1
│  -0.32                                       │  FONT_HUGE big slot, x 4, cell y 4..68 (digit ink y 17..64)
│                                              │
│ LAST                   BEST                  │  FONT_SMALL labels, y 78 (x 4 and x 128)
│      1:52.34                1:51.90   [⚡▢]  │  FONT_MED values right-aligned at x 122 / x 246, y 90
└──────────────────────────────────────────────┘
```

Rules:
- **Big slot content:** `BIG_SECTOR_DELTA` → signed delta `±s.hh` (e.g. `-0.32`, `+1.05`); `BIG_LAP_DELTA` → the same format for the whole lap; `BIG_NONE` (no best lap yet, or before the first gate of the session) → `LAP n` in `FONT_MED` at x 4, y 40 (the huge font has no letters). Deltas are clamped to ±99.99 (`+99.99` is six glyphs = 234 px, the widest string, ending at x 238).
- **Marker (top-right):** `L<lap_no> S<cur_sector_idx>` in `FONT_SMALL` (e.g. `L7 S2`, `L12 S0`), right-aligned at x 246 on y 1..13. It sits above the huge digits' ink line (DejaVu digits at 64 px start 13 px below the cell top), so it never collides with the big slot whatever its width.
- **New best:** after `EV_LAP_COMPLETE` with `new_best`, a `FONT_SMALL` inverted tag `BEST` (white text on a black 32×14 box) sits 6 px right of the big number's last glyph, at y 50, until the next gate crossing (any change of `big_kind`/`big_delta_ms` clears it). With a six-glyph delta (ends x 238) the tag does not fit; it is then drawn over the marker's row at x 208, y 1 instead of the marker.
- **Footer:** two halves of 125 px. `LAST` label (`FONT_SMALL`) at x 4, y 78; its value (`FONT_MED`, 7 glyphs = 98 px) right-aligned at x 122, y 90. `BEST` label at x 128, y 78; its value right-aligned at x 246, y 90. Missing values show `-:--.--` as today.
- **Fault strip:** unchanged (`FAULT_STRIP_X0` 238 / `FAULT_STRIP_Y` 110), drawn last. When any fault icon is shown the BEST value right-aligns at `FAULT_STRIP_X0 - 4` = 234 instead of 246 so the icons never overprint a digit.
- **Before any lap:** big slot `LAP 1`, footer dashes, marker `L1 S0`.

Rendering is a pure function of the model; the ui's refresh policy (§20.3) is unchanged — each gate crossing dirties the model and yields one partial refresh exactly as today.

## 5. LAP page 1 — sector board

```
┌──────────────────────────────────────────────┐
│ BEST LAP 1:51.90              THEO 1:51.20   │  FONT_SMALL header, y 2 (right part right-aligned)
│   S1        S2        S3                     │  FONT_SMALL labels, y 20, one per column
│  32.10     41.00     39.24                   │  FONT_MED best-lap sector times, y 34
│  -0.12     +0.40     -0.05                   │  FONT_MED last-lap sector deltas vs best, y 66
└──────────────────────────────────────────────┘
```

Columns: `LAP1_SECTOR_COLS` = 3 columns of `(CANVAS_VISIBLE_W - 8) / 3` px on both canvases; the board shows the first three sectors and appends `+n` to the S3 label when the layout has more (up to `LAP_MAX_SECTORS`). Row 4 cells show `----` when `have_last_sector_delta[i]` is false. `THEO` shows `-:--.--` until `have_theo`.

## 6. LAP page 2 — stats grid

```
┌──────────────────────────────────────────────┐
│ MAX SPD                LEAN L/R              │  FONT_SMALL labels, y 2
│  214                    L52 R55              │  FONT_MED values, y 14 (no '/' glyph in FONT_MED)
│ LAT G                  LAPS                  │  FONT_SMALL labels, y 62
│  1.32                   12 (10 valid)        │  FONT_MED value / FONT_MED + FONT_SMALL, y 74
│ ACC 0.61   BRK 1.05                          │  FONT_SMALL footer, y 106
└──────────────────────────────────────────────┘
```

Two columns at x 4 and x 128. Units follow `cfg.units` (`km/h`/`mph` as today, label text unchanged). `LAPS` shows `12 (10 valid)` with the parenthesis part in `FONT_SMALL` right after the number.

## 7. DRAG pages

Page 0 (run in progress):

```
┌──────────────────────────────────────────────┐
│ 1/4                                   ARMED  │  FONT_SMALL latest-gate label; ARMED top-right (FONT_MED) until launch
│  12.84                                       │  FONT_HUGE latest gate time (s.hh; `ss.hh` up to 6 glyphs)
│ @173                                         │  FONT_MED trap speed row when the gate carries a speed (no unit: the model has no units field — follow-up)
│ 60ft 2.01   330ft 5.43   1/8 8.29            │  FONT_SMALL earlier gates of this run, in order
└──────────────────────────────────────────────┘
```

- Before launch: big slot shows `READY` in `FONT_MED` (no letters in `FONT_HUGE`), `ARMED` top-right while `drag_armed`; footer empty.
- As gates hit (`EV_DRAG_GATE`), the newest gate takes the big slot (label above it, time in `FONT_HUGE`, speed row when `arg32b` speed is non-zero), and the previous gates of the run join the footer in hit order (label + time in `FONT_SMALL`, up to six entries, then the oldest scroll off the left).
- The existing `drag_row_t` rows and `drag_n` remain the source; the renderer derives "newest" as `drag[drag_n - 1]`.
- Distance gates (`is_distance`) show metres in the big slot as an integer (`38` for 100-0) with a `FONT_SMALL` `m` after it (`FONT_HUGE` and `FONT_MED` have no lowercase). The trap-speed row is `@173` in `FONT_MED`; a unit suffix waits for a units field in the model (follow-up).

Pages 1 (all gates of the last run) and 2 (session best per gate) share one list layout: a `FONT_SMALL` header at y 2 (`LAST RUN` / `SESSION BEST`), then seven gates in two columns of 123 px (x 4 and x 128) and four rows at y 16, 42, 68, 94; each cell is the `FONT_SMALL` label at the column's left edge (longest `100-200` = 49 px) and the `FONT_MED` value right-aligned at the column's right edge (x 122 / x 246), five glyphs at most (`ss.hh`, or `<dist> m` for the 100-0 gate, the `m` in `FONT_SMALL`). Drag gate times are under 60 s by construction (§6.6), so five glyphs always suffice. Empty gates show `--.--`.

## 8. Canvas constants (§20.5, `core/ui/canvas.h`)

All positions above are for the 2.13" canvas (`CANVAS_213`). The 2.9" (296×128) uses the same design with its own constant set: the big slot stays at 64 px, columns use `CANVAS_VISIBLE_W` proportionally (footer halves at 146/292, page-1/2 second column at x 152), rows shift by +3 px to use the extra 6 px of height. Every layout number lives in `canvas.h` under the existing `#if CANVAS_213` split; `screens_moto.c` holds no literals. Existing constants that the redesign replaces (`LAP_ROW_*`, `LAP_TIME_RIGHT_X`, `LAP_CUR_*`, `LAP_DELTA_*`, `DRAG0_*`, `LAP2_ROW_H`) are removed, not kept alongside.

## 9. Tests

- `test/test_screens.c` cases, each with goldens on both canvases (`test/snapshots/*.pbm` and `test/snapshots/213/*.pbm`), plus the existing ink-column check (`fb_max_ink_col() < CANVAS_VISIBLE_W`):
  `lap_p0_first_lap` (BIG_NONE, `LAP 1`, dashes), `lap_p0_sector_delta` (`-0.32`, `L7 S2`, LAST/BEST), `lap_p0_lap_delta_wide` (`+12.50`, marker relocated to the footer gap), `lap_p0_new_best` (BEST tag), `lap_p0_fault` (BEST value shifted left of the strip), `lap_p1_sectors` (3 sectors + deltas, one `----`), `lap_p1_many_sectors` (5 sectors → `S3 +2` on the 2.13", wrapped on the 2.9"), `lap_p2_stats`, `drag_p0_ready` (READY + ARMED), `drag_p0_gate_speed` (`12.84`, `@173`, three footer gates), `drag_p0_distance` (`38 m`), `drag_p1_gates`, `drag_p2_best`.
- Golden review: each regenerated PBM is rendered to PNG and eyeballed before promotion (as in Plan 7 T3); the reviewer confirms nothing draws past `CANVAS_VISIBLE_W` and no two fields overlap (a new helper `fb_overlap_check` is **not** added — overlap is judged from the goldens).
- Model wiring in `ui.c` is target-only; verified on the bench (photos: sector delta, lap delta, new best, drag ready).

## 10. Out of scope

Menu, one-shots, fault-strip icons, the refresh policy, predictive lap time (needs a distance-indexed reference lap in the engine), a live clock (needs a running CUR time in the model), the 2.9" panel on hardware (not owned; goldens only).

## 11. Bench acceptance

On the 2.13" panel with `moto_sim`: page 0 shows `LAP 1` before the first lap; sector deltas appear in the big slot at each gate; the lap delta and the `BEST` tag appear at the line; page 1 shows the best lap's sectors and last-lap deltas; page 2 the grid; DRAG page 0 `READY`/`ARMED` (via `dbg`/config mode switch) — photos into the ledger. Tag `p07b-done`.
