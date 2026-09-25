# Plan 7 — Display (Waveshare 2.13" V4 e-paper) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the lap-timer's UI on the real 2.13" e-paper panel: SSD1680 driver, partial/full refresh policy with the display ladder, the 250×122 canvas, and button injection for bench use — with every pure part host-tested and every on-target step gated through the dev-kit.

**Architecture:** A new `display_epaper_ssd1680` driver component implements the spec's `hal/display.h` (init/blit/refresh/sleep/wake/reinit + a dirty window); its pure parts (panel table, landscape→portrait transposition, window rounding) live in `host/` and are unit-tested. The `ui` task keeps rendering into its static framebuffer and now decides partial/full/defer through a pure `ui_refresh_decide()` before calling the driver; the ladder and temperature throttle live in `ui.c`. The canvas becomes compile-time by `PANEL` (250×122 for `ws213v4`), with a second golden set. A small DRAM reclaim on the sim build pays for the restored 6144-byte `ui` stack.

**Tech Stack:** ESP-IDF v5.3.2 (`spi_master` with DMA + pre-transfer DC callback, `gpio`, `esp_timer`), FreeRTOS, C11, Unity host harness (`test/`, ASan/UBSan), `tools/lint/power_of_10.py`, dev-kit console + `tools/devkit.py` for all on-target gates.

**Spec:** `docs/superpowers/specs/2026-09-25-plan-7-display-deltas.md` (deltas; binding) over `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §3.3, §3.4, §4.8, §17.2, §17.7, §20, §22.3 (base).

## Global Constraints

- Lap-timer: strict NASA Power-of-10 — no heap after init (the IDF SPI device is allocated once in `disp_init`, like UART/littlefs), bounded loops, ≥ 2 assertions per function via `LT_ASSERT_*`/`CORE_ASSERT_*` (never abort), function-length cap enforced by `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0. The only function pointer this plan adds is the SPI pre-transfer callback the IDF API requires.
- Zero compiler warnings on clean, ccache-disabled builds (`export CCACHE_DISABLE=1 IDF_CCACHE_ENABLE=0`) of `moto_sim`, `moto_neo6m`, `core_selftest` and the host harness (`-Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations`); NO `-Wno-*`, no `#pragma GCC diagnostic`. Every new component's CMake copies that flag line verbatim.
- Review minors are fixed, never deferred.
- DRAM: every task reports the `idf.py -B build/moto_sim size` DRAM line; after Task 1 the budget is `moto_sim ≥ 5 KB free`, `moto_neo6m ≥ 6 KB free`, and no later task may drop below `moto_sim ≥ 1 KB free`.
- Pins (spec deltas §1): CLK 18, DIN 23, CS 5 (hardware CS, idle high), DC 14, **RST 13**, BUSY 35 (input-only, high = busy). GPIO 4 is the dev-kit DETECT line and is never touched by display code.
- Panel: `ws213v4` — SSD1680, native portrait 122×250, landscape logical **250×122**, RAM width 128 (16 bytes/row), border `0x05`, full LUT `0x22 0xF7`, partial LUT `0x3C 0x80` then `0x22 0xFF`. `PANEL` default `ws213v4`; `ws29v2` (296×128) must keep building and keep its goldens.
- SPI: `SPI3_HOST`, mode 0, 10 MHz, queue size 4, DMA, ISR in IRAM; DC set in the pre-transfer callback from the transaction's `user` field; BUSY polled every 1 ms, timeout 5000 ms → `-ETIMEDOUT`.
- Refresh policy (spec §20.3): partial on the listed events with the dirty window; full when `partial_count >= display.full_every` (cfg, default 10, clamp 1..50), or 30 min since the last full, or on wake from PARK, or on page/menu entry when still; a full triggered while moving with `partial_count < 2*full_every` is deferred (a partial is used); never periodic while riding unless `display.live_clock`; temperature throttle (`SYS_DISP_TEMP_THROTTLE`): partials at most one per 30 s, fulls suppressed. Ladder: BUSY timeout → `disp_reinit` → retry once → 3 consecutive failures → `SYS_DISP_DEAD` + `E_DISP_DEAD`, then retry `disp_reinit` every 300 s.
- Error codes (§17.7): `E_DISP_BUSY_TIMEOUT = 0x0301`, `E_DISP_DEAD = 0x0302`, `E_DISP_TEMP = 0x0303`. Sys flags (`app/lt_sup.h`): `SYS_DISP_DEAD` (bit 4), `SYS_DISP_TEMP_THROTTLE` (bit 11).
- Bench rules: flash only after the user's "ready" (BOOT held); all on-target checks go through the dev-kit (`lt shell`, `lt status`, `selftest`, `stream stats`); never open the lap-timer's serial port within 35 s of an OTA push; console lines end in `\r`.
- Build: `source tools/idf-env.sh` from **bash** (prints `ESP-IDF v5.3.2`); `./build.sh moto_sim build`, `./build.sh moto_neo6m build`; host: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build && ctest --test-dir test/build --output-on-failure` (33 executables today).

---

## File structure

| Path | Responsibility |
|------|----------------|
| `components/lt_hal/include/hal/display.h` (new) | The HAL contract from the base spec's `hal/display.h` block + `disp_set_window`. |
| `components/drivers/display_epaper_ssd1680/CMakeLists.txt`, `display_epaper.c` (new) | IDF glue: SPI device, GPIOs, BUSY wait, SSD1680 sequences, boot-time init. |
| `components/drivers/display_epaper_ssd1680/host/epd_panel.c`, `host/epd_rotate.c`, `include/epd_pure.h` (new) | Pure, IDF-free: panel table by `PANEL`, landscape→portrait line transposition, dirty-window rounding. Linked into the host harness. |
| `components/core/ui/include/core/ui/canvas.h` (new) | Compile-time canvas (`CANVAS_W/H`) and per-canvas layout constants; the only place that knows 296×128 vs 250×122. |
| `components/core/ui/screens_moto.c` (modify) | Uses `canvas.h` constants; no literal coordinates. |
| `components/core/ui/refresh_policy.c`, `include/core/ui/refresh_policy.h` (new) | Pure `ui_refresh_decide()` (§20.3 decision table). |
| `components/app/ui/ui.c` (modify) | Calls `disp_*`, keeps the ladder counters and throttle timer, restores the 6144 B stack. |
| `components/app/include/app/lt_err.h` (modify) | `E_DISP_*` codes. |
| `components/core/tracks/trk_json.c`, `trk.c`, `include/core/trk.h` (modify) | Device-sized token array; `trk_user_add_json()` parses straight into a user slot. |
| `components/app/pipeline/pipeline.c` (modify) | Sim venue via `trk_user_add_json` (drops the 2 752 B copy). |
| `components/drivers/board_devkit_v1/board.c` (modify) | Comment/reservation of RST 13 (owned by the display driver), nothing else. |
| `components/drivers/export_serial/export_serial.c` (modify) | `dbg btn …` and `dbg flag …` injection. |
| `CMakeLists.txt`, `main/CMakeLists.txt`, `build.sh` (modify) | `PANEL` default, `display_${DISPLAY}` component wiring, `CFG_TRK_JSON_TOKS`. |
| `test/test_trk.c` (extend), `test/test_epd_pure.c`, `test/test_refresh_policy.c` (new), `test/test_screens.c` (extend), `test/snapshots/213/*.pbm` (new) | Host tests and goldens. |
| Spec §3.3/§3.4 (modify), roadmap, `docs/hardware/bom.md` (modify) | Pin change, Plan 7 status, owned panel. |

Task order: 1 → 2 → 3 → 4 → 5 (flash gate `p07-d1`) → 6 → 7 (flash gate `p07-d2`) → 8 → 9 (bench + flash gate `p07-d3`) → 10. Tasks 4 and 6 are pure and may run in parallel with 3 in separate worktrees (disjoint files); everything else is serial.

---

### Task 1: DRAM reclaim on the sim build + `ui` stack restore

**Files:**
- Modify: `components/core/tracks/trk_json.c:27` (`MAX_TOKS`), `components/core/tracks/trk.c`, `components/core/include/core/trk.h:39-47`
- Modify: `components/app/pipeline/pipeline.c:104-107, 578-590`
- Modify: `components/app/ui/ui.c:60-72` (`UI_STACK_BYTES`)
- Modify: `CMakeLists.txt` (~line 60, the `build_config.h` values block)
- Test: `test/test_trk.c`

**Interfaces:**
- Produces: `int trk_user_add_json(const char *json, size_t n, uint16_t *venue_id_out, char *err, size_t err_cap);` — parses a venue JSON straight into a free (or same-id) user slot; 0 ok, −1 on parse error / table full (`err` filled). `const trk_venue_t *trk_get(uint16_t venue_id)` is unchanged and returns that slot.
- Produces: build flag `CFG_TRK_JSON_TOKS` in `build_config.h` (64 on firmware builds; the host harness, which does not include `build_config.h`, keeps 512 via the fallback below).

- [ ] **Step 1: Failing test** — append to `test/test_trk.c` (register in its `main()`):

```c
static void test_user_add_json_parses_into_a_slot(void)
{
    const char *json = "{\"id\":9001,\"name\":\"SIMTRACK\",\"layouts\":[{\"id\":1,\"name\":\"FULL\","
                       "\"sf\":{\"lat\":-339123456,\"lon\":183123456,\"hdg\":900,\"w_m\":20},\"sectors\":[]}]}";
    uint16_t id = 0; char err[48] = {0};
    TEST_ASSERT_EQUAL_INT(0, trk_user_add_json(json, strlen(json), &id, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(9001, id);
    const trk_venue_t *v = trk_get(9001);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("SIMTRACK", v->name);
    TEST_ASSERT_EQUAL_UINT8(1, v->n_layouts);
}
static void test_user_add_json_rejects_malformed(void)
{
    uint16_t id = 7; char err[48] = {0};
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add_json("{\"id\":", 6, &id, err, sizeof err));
    TEST_ASSERT_TRUE(err[0] != '\0');
}
```
(Adapt the JSON field names to what `trk_from_json` already accepts — copy the smallest venue literal that `test_trk.c` already uses for `trk_from_json`; the point is "same parser, lands in the table".)

- [ ] **Step 2: Run** `cmake --build test/build --target test_trk && ./test/build/test_trk` → fails to link (`trk_user_add_json` undefined).

- [ ] **Step 3: Implement.** In `trk_json.c`:

```c
#ifndef CFG_TRK_JSON_TOKS
#define CFG_TRK_JSON_TOKS 512   /* host tools/tests: the maximal-upload cap (see the note above) */
#endif
#define MAX_TOKS CFG_TRK_JSON_TOKS
```
and include `"build_config.h"` only under `#if __has_include("build_config.h")` (the firmware build tree provides it; the host harness does not). In `CMakeLists.txt`'s `build_config.h` values block add `set(CFG_TRK_JSON_TOKS 64)` with the comment: "device: the only JSON venue parsed on a firmware build is gps_sim's 330-byte capture venue (~40 tokens); uploads of large venues are a host-tool path (512)". In `trk.c` add:

```c
int trk_user_add_json(const char *json, size_t n, uint16_t *venue_id_out, char *err, size_t err_cap)
{
    CORE_ASSERT_RET(json != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(venue_id_out != NULL, TRK_ASSERT_CODE, -1);
    trk_venue_t *slot = user_slot_for_parse();            /* first free slot, or the one whose id the JSON carries once parsed */
    if (slot == NULL) return fail(err, err_cap, "user table full");
    if (trk_from_json(slot, json, n, err, err_cap) != 0) { memset(slot, 0, sizeof *slot); return -1; }
    *venue_id_out = slot->id;
    return 0;
}
```
where `user_slot_for_parse()` is a static helper returning a free entry of the existing `user[]` table (`trk.c:19`), and — after the parse — if another slot already holds the same id, the helper's caller replaces it exactly as `trk_user_add` does today (call the existing dedupe path; do not duplicate its logic). Keep every function under the lint cap.

- [ ] **Step 4: Sim venue.** In `pipeline.c` replace the `s_venue` static and its use:

```c
#if CFG_GPS_SIM
extern const char *gps_sim_venue_json(void);
#endif
...
        const char *vj = gps_sim_venue_json();
        uint16_t vid = 0; char err[48];
        if (vj && trk_user_add_json(vj, strlen(vj), &vid, err, sizeof err) == 0) {
            const trk_venue_t *v = trk_get(vid);
            LT_ASSERT_VOID(v != NULL, PIPE_ASSERT_CODE);
            LT_ASSERT_VOID(v->n_layouts <= TRK_MAX_LAYOUTS, PIPE_ASSERT_CODE);
            lap_set_venue(&s_lap, v);                     /* lap keeps the pointer: it must point at the table entry, never at a stack copy */
            uint16_t layout_id = (v->n_layouts > 0) ? v->layouts[0].id : 0;
            ... (unchanged from here)
```
`lap_set_venue` stores the pointer (`lap.h:98`), so the venue must live in the user table — which it now does.

- [ ] **Step 5: `ui` stack.** In `ui.c` delete the `#if CFG_GPS_SIM` split and set `#define UI_STACK_BYTES 6144` unconditionally; rewrite the comment above it: the sim's DRAM margin came from the sim-only token array and venue copy, both gone in Plan 7 Task 1.

- [ ] **Step 6: Run + gate** — host `ctest` all green (33 + the two new cases inside `test_trk`); lint 0; clean `moto_sim` + `moto_neo6m` builds 0 warnings; record `idf.py -B build/moto_sim size` (expected ≈ 124188 − 8960 − 2752 + 3584 ≈ 116 060 used → ≈ 8.5 KB free) and the `moto_neo6m` line (expected ≈ +3 584 only if its stack was not already 6144 — it was, so ≈ unchanged).

- [ ] **Step 7: Commit**

```bash
git add components/core/tracks components/core/include/core/trk.h components/app/pipeline/pipeline.c components/app/ui/ui.c CMakeLists.txt test/test_trk.c
git commit -m "perf(dram): device-sized track-JSON tokens, sim venue parsed into the user table, ui stack back to 6144 (Plan 7 T1)"
```

---

### Task 2: `hal/display.h`, pin change, `PANEL` default, component wiring

**Files:**
- Create: `components/lt_hal/include/hal/display.h`
- Create: `components/drivers/display_epaper_ssd1680/CMakeLists.txt`, `components/drivers/display_epaper_ssd1680/display_epaper.c` (stub returning `-ENOSYS` from every function except `disp_init`, which returns 0 and fills caps — replaced in Task 5)
- Modify: `CMakeLists.txt:24` (`PANEL` default), `:173` (`EXTRA_COMPONENT_DIRS` + `display_${DISPLAY}`), `:187-189` (`set(ENV{LT_DISPLAY} "${DISPLAY}")`), `main/CMakeLists.txt:1-2` (`display_$ENV{LT_DISPLAY}` in `LT_MAIN_REQUIRES`), `build.sh` (no change needed: it already passes `DISPLAY=epaper_ssd1680`; confirm)
- Modify: `components/drivers/board_devkit_v1/board.c:166` (comment: "e-paper DC(14)/RST(13)/BUSY(35) are owned by display_epaper"), `docs/superpowers/specs/2026-09-14-lap-timer-design.md:152` (table row `E-paper RST | 13 | out | active low | MTCK; unused by JTAG in this project; GPIO 4 is the dev-kit DETECT line (Plan 5.5)`) and `:179` (`E-paper: SCK 18, DIN 23, CS 5, DC 14, RST 13, BUSY 35`)

**Interfaces:**
- Produces (`hal/display.h`, verbatim contract for Tasks 5/7):

```c
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H
#include <stdint.h>
#include <stddef.h>

#define DISP_PARTIAL 0
#define DISP_FULL    1

typedef struct {
    uint16_t width, height;      /* logical, after rotation */
    uint8_t  partial_ok;
    int8_t   temp_min_c, temp_max_c;
    uint16_t full_refresh_ms, partial_refresh_ms;   /* nominal */
} disp_caps_t;

int  disp_init(const disp_caps_t **caps);            /* hw reset + init + one full refresh of the current blit (boot screen); 0 or -EIO/-ETIMEDOUT */
int  disp_blit(const uint8_t *fb);                   /* full framebuffer, 1 bpp, row-major, MSB = leftmost, 0 = black (core/ui convention) */
int  disp_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h); /* dirty rect for the next DISP_PARTIAL; x/w rounded outward to 8-px columns */
int  disp_refresh(uint8_t mode);                     /* DISP_PARTIAL / DISP_FULL; blocks until BUSY clears or timeout (returns -ETIMEDOUT) */
int  disp_sleep(void);
int  disp_wake(void);
int  disp_reinit(void);                              /* ladder step: hw reset + init, keeps the blitted image */
#endif
```

- [ ] **Step 1:** Write the header exactly as above (add the file-header comment in the style of `hal/gps.h`: "hal/display.h — display HAL contract (spec §20.1, called from the ui task only; not reentrant)").
- [ ] **Step 2:** `PANEL` default `ws213v4` in `CMakeLists.txt:24`; wire the component: append `components/drivers/display_${DISPLAY}` to `EXTRA_COMPONENT_DIRS`, export `set(ENV{LT_DISPLAY} "${DISPLAY}")` next to `LT_GPS`/`LT_IMU`, add `display_$ENV{LT_DISPLAY}` to `LT_MAIN_REQUIRES`.
- [ ] **Step 3:** Component skeleton — `CMakeLists.txt`:

```cmake
# display_epaper_ssd1680 -- hal/display.h over a Waveshare SSD1680 e-paper (spec §20.1; Plan 7).
idf_component_register(
    SRCS "display_epaper.c" "host/epd_panel.c" "host/epd_rotate.c"
    INCLUDE_DIRS "include"
    REQUIRES lt_hal core esp_driver_spi esp_driver_gpio esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
```
`display_epaper.c` stub: every `disp_*` returns `-ENOSYS` except `disp_init`, which fills a static `disp_caps_t` from the panel table (Task 4 provides it; for this task hardcode `{250,122,1,0,45,2000,400}` under `#if CFG_PANEL_WS213V4` else `{296,128,1,0,45,2000,400}`) and returns 0. Create empty `host/epd_panel.c`/`host/epd_rotate.c` with a comment each so the SRCS exist (Task 4 fills them).
- [ ] **Step 4:** Pin-table edits in the spec and the `board.c` comment (no code in `board.c` touches 13).
- [ ] **Step 5: Gate** — clean `moto_sim` + `moto_neo6m` builds rc=0, 0 warnings (the component links even though nothing calls it yet); `PANEL=ws29v2 ./build.sh moto_sim build` also rc=0 (both panels build); host suite unchanged; lint 0; DRAM line (expected +32 B for the caps struct).
- [ ] **Step 6: Commit** — `git commit -m "feat(display): hal/display.h contract, display_epaper_ssd1680 component skeleton, RST->GPIO13, PANEL default ws213v4 (Plan 7 T2)"`

---

### Task 3: Compile-time canvas + 250×122 layouts + second golden set

**Files:**
- Create: `components/core/ui/include/core/ui/canvas.h`
- Modify: `components/core/ui/screens_moto.c` (every `#define` coordinate block: lines 167-168, 205-213, 270-277, 315, 365-372, 480-485, 625-629, 646-647, 668, 679-680, 699-704, 725-726, 739-740, 753)
- Modify: `components/app/ui/ui.c:80-82, 125, 671` (`FB_W/FB_H` → `CANVAS_W/CANVAS_H`; the static buffer stays `(296/8)*128`)
- Modify: `test/test_screens.c:27-28, 35` and `test/test_ui.c:28` (`FB_W/FB_H` → canvas), `test/CMakeLists.txt` (a second executable `test_screens_213` = `test_screens.c` compiled with `-DCANVAS_FORCE_213=1`, snapshots under `test/snapshots/213/`)
- Create: `test/snapshots/213/*.pbm` (one per existing golden)

**Interfaces:**
- Produces (`canvas.h`):

```c
#ifndef CORE_UI_CANVAS_H
#define CORE_UI_CANVAS_H
/* Compile-time canvas (spec deltas §2). Firmware: from build_config.h's CFG_PANEL_*; host tests:
 * default 296x128, or 250x122 with -DCANVAS_FORCE_213=1. */
#if __has_include("build_config.h")
#include "build_config.h"
#endif
#if defined(CANVAS_FORCE_213) || (defined(CFG_PANEL_WS213V4) && CFG_PANEL_WS213V4)
#define CANVAS_W 250
#define CANVAS_H 122
#define CANVAS_213 1
#else
#define CANVAS_W 296
#define CANVAS_H 128
#define CANVAS_213 0
#endif
#define CANVAS_STRIDE (CANVAS_W / 8)
#endif
```
plus, in the same header, every layout constant currently in `screens_moto.c`, defined once per canvas with the values from the spec deltas §2 table (the 296 values are the existing ones, verbatim). Also `#define CANVAS_ROW_FONT_BIG (CANVAS_213 ? FONT_MED : FONT_BIG)`-style selection is NOT allowed (fonts are structs) — instead define `LAP_TIME_FONT` as a macro naming the font object: `#if CANVAS_213 / #define LAP_TIME_FONT FONT_MED / #else / #define LAP_TIME_FONT FONT_BIG / #endif`, and likewise `LAP_CUR_FONT` (SMALL/MED).

- [ ] **Step 1: Failing test** — in `test/CMakeLists.txt` add after the explicit `test_screens` target:

```cmake
add_executable(test_screens_213 test_screens.c pbm.c)
target_compile_definitions(test_screens_213 PRIVATE CANVAS_FORCE_213=1 TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}" SNAP_SUBDIR="213/")
target_compile_options(test_screens_213 PRIVATE ${LAPTIMER_STRICT_FLAGS})
target_link_libraries(test_screens_213 PRIVATE core unity m Threads::Threads)
add_test(NAME test_screens_213 COMMAND test_screens_213)
```
and in `test_screens.c` change `#define SNAP(name) TEST_DIR "/snapshots/" name` to `#define SNAP(name) TEST_DIR "/snapshots/" SNAP_SUBDIR name` with `#ifndef SNAP_SUBDIR / #define SNAP_SUBDIR "" / #endif`, and `FB_W/FB_H` → `CANVAS_W/CANVAS_H` from `core/ui/canvas.h`. Add one assertion to every screen case: after `screens_moto_render`, `TEST_ASSERT_TRUE(!s_fb.dirty.valid || (s_fb.dirty.x1 <= CANVAS_W && s_fb.dirty.y1 <= CANVAS_H))`.
- [ ] **Step 2: Run** `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build -R test_screens_213` → fails (`canvas.h` missing / goldens missing).
- [ ] **Step 3: Implement** `canvas.h`; move every layout `#define` out of `screens_moto.c` into it (296 values verbatim; 213 values from the spec table: LAP rows y 2/28/54/70, times right x 170/150, sector x 160, ΔS value x 24; DRAG rows y0 2 pitch 26 (4 rows: 2/28/54/80), time right 150, trap x 160, ARMED right x 250; fault strip x0 238 y 110; LAP1/LAP2/DRAG12 column widths scaled to the canvas: `LAP1_SECTOR_COL_W = (CANVAS_W - 8) / 3`, `DRAG12_COL_W = (CANVAS_W - 8) / 2`, rows pitch 14 from y 16 on 213 (16 on 296), `LAP2_ROW_H` 20 on 213; one-shots: BOOT name y 4 / version y 26 / lines y0 44 pitch 14; VENUE label y 30 value y 52; SAFE text y 50; LOWBATT title y 24 pct y 60; OTA title y 8, bar x 25 y 46 w 200 h 18, pct y 74; OTAFAIL lines y 28/60; CALIBRATE/NEWTRACK title y 20 sub y 56; menu rows pitch 14 from y 2 showing 7 rows). Replace `FONT_BIG` uses on the LAP page with `LAP_TIME_FONT` and the CUR row's `FONT_MED` with `LAP_CUR_FONT`; the ΔS row already uses `FONT_SMALL`.
- [ ] **Step 4: Goldens** — run `test_screens_213` once with no goldens: each case fails and dumps `test/snapshots/213/<name>.pbm.actual.pbm`; convert each to PNG (`python3 -c "from PIL import Image; …"` or `magick`) and eyeball: no clipping at the right/bottom edges, times legible, fault strip inside the frame. Fix constants if anything overlaps, then rename the `.actual.pbm` files to the golden names. Commit the goldens. The 296 suite (`test_screens`) must pass unchanged (byte-identical goldens = the refactor moved constants without changing values).
- [ ] **Step 5: Gate** — host suite all green (34 executables), lint 0, clean `moto_sim` build 0 warnings (the firmware now renders 250×122 into the first 3904 bytes of the 4736-byte buffer), `PANEL=ws29v2 ./build.sh moto_sim build` rc=0; DRAM line unchanged.
- [ ] **Step 6: Commit** — `git commit -m "feat(ui): compile-time canvas by PANEL (250x122 for ws213v4) with scaled layouts and a second golden set (Plan 7 T3)"`

---

### Task 4: Driver pure parts — panel table, transposition, window rounding (host-tested)

**Files:**
- Create: `components/drivers/display_epaper_ssd1680/include/epd_pure.h`, `host/epd_panel.c`, `host/epd_rotate.c`
- Modify: `test/CMakeLists.txt` (link the two host files into a new executable), `test/test_epd_pure.c` (new)

**Interfaces:**
- Produces (`epd_pure.h`, IDF-free — `<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only):

```c
typedef struct {
    const char *name;            /* "ws213v4" / "ws29v2" */
    uint16_t native_w, native_h; /* portrait: 122x250 / 128x296 */
    uint16_t logical_w, logical_h; /* landscape: 250x122 / 296x128 */
    uint16_t ram_w;              /* 128 (16 bytes/row) for both */
    uint8_t  border;             /* 0x05 */
    uint8_t  lut_full, lut_partial; /* 0xF7, 0xFF (0x22 argument) */
} epd_panel_t;
const epd_panel_t *epd_panel(void);            /* by CFG_PANEL_* (build_config.h) or EPD_FORCE_PANEL for host tests */

/* Transposes ONE panel RAM row (portrait row `pr`, 0..native_h-1) out of the landscape framebuffer:
 * panel column c (0..native_w-1) <- logical pixel (x = pr, y = native_w-1-c) with rotation 0, i.e. the
 * landscape image rotated 90 degrees clockwise into the portrait RAM. Writes ram_w/8 bytes (bits
 * beyond native_w are 1 = white). Bit sense: 1 = white on the panel; the framebuffer's 0 = black is
 * inverted here unless `invert`. Returns bytes written (16). */
size_t epd_rotate_line(const uint8_t *fb, uint16_t fb_w, uint16_t fb_h, uint16_t pr, bool invert, uint8_t *out, size_t cap);

/* Rounds a logical dirty rect outward to whole 8-px columns of the panel RAM: the panel's x (RAM
 * columns) is the logical y, so the rect's y range must snap to multiples of 8 (RAM bytes) while the
 * logical x range maps to panel rows one-to-one. Fills panel-space {ram_x0_bytes, ram_x1_bytes,
 * row0, row1} (half-open) and returns false if the rect is empty or out of range. */
typedef struct { uint16_t xb0, xb1, r0, r1; } epd_window_t;
bool epd_window_from_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t fb_w, uint16_t fb_h, epd_window_t *out);
```

- [ ] **Step 1: Failing test** `test/test_epd_pure.c`:

```c
#include "unity.h"
#include "epd_pure.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}

static void test_rotate_all_white_fb_gives_all_ones(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);   /* core/ui: bit 1 = white */
    uint8_t out[16]; size_t n = epd_rotate_line(fb, 250, 122, 0, false, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(16, n);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}
static void test_rotate_all_black_fb_gives_zeros_inside_panel_width(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0x00, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 5, false, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, out[15]);   /* columns 120,121 black (bits 7,6); 122..127 beyond the panel stay white */
}
static void test_rotate_single_black_pixel_lands_in_the_right_column(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    /* logical (x=7, y=0) black -> panel row 7, column native_w-1-0 = 121 -> byte 15, bit 6 */
    fb[0 * 32 + 0] &= (uint8_t)~(1u << 0);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 7, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xBF, out[15]);
    epd_rotate_line(fb, 250, 122, 8, false, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[15]);
}
static void test_rotate_invert_flips_ink(void)
{
    uint8_t fb[32 * 122]; memset(fb, 0xFF, sizeof fb);
    uint8_t out[16]; epd_rotate_line(fb, 250, 122, 0, true, out, sizeof out);
    for (int i = 0; i < 15; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}
static void test_window_snaps_logical_y_to_ram_bytes(void)
{
    epd_window_t w;
    TEST_ASSERT_TRUE(epd_window_from_rect(10, 3, 20, 10, 250, 122, &w));
    TEST_ASSERT_EQUAL_UINT16(0, w.xb0); TEST_ASSERT_EQUAL_UINT16(2, w.xb1);   /* y 3..13 -> RAM bytes [0,2) */
    TEST_ASSERT_EQUAL_UINT16(10, w.r0); TEST_ASSERT_EQUAL_UINT16(30, w.r1);   /* x 10..30 -> panel rows */
    TEST_ASSERT_FALSE(epd_window_from_rect(0, 0, 0, 5, 250, 122, &w));
    TEST_ASSERT_FALSE(epd_window_from_rect(240, 0, 20, 5, 250, 122, &w));
}
static void test_panel_table_ws213v4(void)
{
    const epd_panel_t *p = epd_panel();
    TEST_ASSERT_EQUAL_STRING("ws213v4", p->name);
    TEST_ASSERT_EQUAL_UINT16(250, p->logical_w); TEST_ASSERT_EQUAL_UINT16(122, p->logical_h);
    TEST_ASSERT_EQUAL_UINT16(128, p->ram_w); TEST_ASSERT_EQUAL_HEX8(0xF7, p->lut_full); TEST_ASSERT_EQUAL_HEX8(0xFF, p->lut_partial);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_rotate_all_white_fb_gives_all_ones); RUN_TEST(test_rotate_all_black_fb_gives_zeros_inside_panel_width);
    RUN_TEST(test_rotate_single_black_pixel_lands_in_the_right_column); RUN_TEST(test_rotate_invert_flips_ink);
    RUN_TEST(test_window_snaps_logical_y_to_ram_bytes); RUN_TEST(test_panel_table_ws213v4); return UNITY_END(); }
```
`test/CMakeLists.txt`: add `set(EPD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/drivers/display_epaper_ssd1680)`, exclude `test_epd_pure` from the generic loop, and `add_executable(test_epd_pure test_epd_pure.c ${EPD_DIR}/host/epd_panel.c ${EPD_DIR}/host/epd_rotate.c)` with `target_include_directories(... PRIVATE ${EPD_DIR}/include)`, `target_compile_definitions(test_epd_pure PRIVATE EPD_FORCE_PANEL_213=1)`, strict flags, link unity/m/Threads.

- [ ] **Step 2: Run** → build failure (header missing).
- [ ] **Step 3: Implement.** `epd_panel.c`: the two-row table; `epd_panel()` picks by `CFG_PANEL_WS213V4` (from `build_config.h` under `__has_include`) or `EPD_FORCE_PANEL_213`. `epd_rotate.c`: `epd_rotate_line` loops `c` over `0..ram_w-1` (bounded 128): for `c < native_w`, `y = native_w-1-c`, `x = pr`, bit = `(fb[y*stride + x/8] >> (7 - x%8)) & 1`, panel bit = invert ? !bit : bit, packed MSB-first into `out[c/8]`; for `c >= native_w` panel bit = 1. Both asserts: `fb != NULL && out != NULL`, `cap >= ram_w/8`, `pr < native_h` (return 0 on violation). `epd_window_from_rect`: reject `w == 0 || h == 0 || x + w > fb_w || y + h > fb_h`; `xb0 = y / 8`, `xb1 = (y + h + 7) / 8`, `r0 = x`, `r1 = x + w`.
- [ ] **Step 4: Run** → 6/6 pass; also run the whole host suite.
- [ ] **Step 5: Commit** — `git commit -m "feat(display): pure SSD1680 helpers — panel table, landscape->portrait line transposition, RAM window rounding (Plan 7 T4)"`

---

### Task 5: Driver IDF glue + boot screen on the panel (flash gate `p07-d1`)

**Files:**
- Modify: `components/drivers/display_epaper_ssd1680/display_epaper.c` (replace the Task 2 stub)
- Modify: `components/app/ui/ui.c` (~line 671 after `fb_init`: `disp_init` + first blit/refresh; ~line 131 `render_now` unchanged)

**Interfaces:**
- Consumes: `hal/display.h` (Task 2), `epd_pure.h` (Task 4), `board.c`'s already-initialised `SPI3_HOST` bus (`spi_bus_initialize` in `board_init`), pins per Global Constraints.
- Produces: a working `disp_init/blit/set_window/refresh/sleep/wake/reinit`; `disp_refresh(DISP_FULL)` ≈ 2 s, `DISP_PARTIAL` ≈ 0.3–0.5 s; `-ETIMEDOUT` after 5000 ms of BUSY.

- [ ] **Step 1: SPI + GPIO bring-up** (`display_epaper.c`):

```c
#define PIN_DC   GPIO_NUM_14
#define PIN_RST  GPIO_NUM_13
#define PIN_BUSY GPIO_NUM_35
#define PIN_CS   GPIO_NUM_5
#define BUSY_TIMEOUT_MS 5000
#define BUSY_POLL_MS    1

static spi_device_handle_t s_dev;
static const epd_panel_t  *s_panel;
static const uint8_t      *s_fb;          /* last blitted framebuffer (caller-owned, static in ui.c) */
static epd_window_t        s_win;         /* pending partial window; valid when s_win_set */
static bool                s_win_set;
static uint8_t             s_line[16];    /* one panel RAM row (ram_w/8) -- the only driver buffer */
static disp_caps_t         s_caps;

static void IRAM_ATTR pre_cb(spi_transaction_t *t)   /* DC from the transaction's user field: 0 = command, 1 = data */
{
    gpio_set_level(PIN_DC, (int)(uintptr_t)t->user);
}
```
`disp_init`: configure DC/RST as outputs (RST high), BUSY input; `spi_device_interface_config_t` = mode 0, `clock_speed_hz 10*1000*1000`, `spics_io_num PIN_CS`, `queue_size 4`, `pre_cb`, `flags 0`; `spi_bus_add_device(SPI3_HOST, ...)` once (guard with a `static bool s_added`); then `epd_hw_reset()` → `epd_wait_busy()` → `0x12` → wait → the §20.1 init sequence (`0x01` with `native_h-1`; `0x11 0x03`; `0x44 0x00, ram_w/8-1`; `0x45 0x00 0x00, (native_h-1)&0xFF, (native_h-1)>>8`; `0x3C border`; `0x18 0x80`; `0x21 0x00 0x80`; `0x4E 0x00`; `0x4F 0x00 0x00`; wait) → if a framebuffer was blitted (`s_fb != NULL`) do one full refresh (boot screen). Fill `s_caps` from the panel table (`width/height` = logical, `partial_ok 1`, temp `0/45`, `2000/400` ms). Return 0, or `-ETIMEDOUT`/`-EIO`.
`epd_cmd(uint8_t c)` / `epd_data(const uint8_t *d, size_t n)`: one `spi_device_polling_transmit` each, `user = (void*)0` / `(void*)1`, `length = n*8`, `tx_buffer = d` (the 16-byte `s_line` is DMA-capable static DRAM). `epd_wait_busy()`: loop `BUSY_TIMEOUT_MS / BUSY_POLL_MS` times (bounded), `vTaskDelay(pdMS_TO_TICKS(1))`, return 0 when BUSY reads low, else `-ETIMEDOUT`.
- [ ] **Step 2: Blit + refresh.** `disp_blit(fb)`: store the pointer (assert non-NULL); no SPI. `disp_refresh(DISP_FULL)`: set the full RAM window (`0x44/0x45/0x4E/0x4F` for the whole panel), `0x24` then one `epd_rotate_line` per portrait row (`native_h` rows, bounded) each followed by `epd_data(s_line, 16)`; repeat into `0x26`; `0x22 lut_full`; `0x20`; `epd_wait_busy()`. `DISP_PARTIAL`: require `s_win_set` (else treat as full); `0x3C 0x80`; window = `0x44 xb0, xb1-1`; `0x45 r0&0xFF, r0>>8, (r1-1)&0xFF, (r1-1)>>8`; `0x4E xb0`; `0x4F r0&0xFF, r0>>8`; `0x24` + rows `r0..r1-1` writing only bytes `xb0..xb1-1` of each rotated line (`epd_data(&s_line[xb0], xb1-xb0)`); `0x22 lut_partial`; `0x20`; wait; then the same window/rows into `0x26`; `0x3C border` (restore); clear `s_win_set`. `disp_set_window`: `epd_window_from_rect` into `s_win`, set `s_win_set`, return `-EINVAL` on false. `disp_sleep`: `0x10 0x01`. `disp_wake` and `disp_reinit`: `epd_hw_reset` + the init sequence (no refresh). Every function: `CORE_ASSERT_RET(s_panel != NULL, ...)` and one more invariant; all loops bounded by `native_h`/`ram_w`.
- [ ] **Step 3: Boot screen in `ui.c`.** After `fb_init(...)` and the first `render_now()` of the BOOT one-shot: `const disp_caps_t *caps; if (disp_init(&caps) != 0) { errlog E_DISP_DEAD (code 0x0302 — add `E_DISP_BUSY_TIMEOUT = 0x0301, E_DISP_DEAD = 0x0302, E_DISP_TEMP = 0x0303` to `lt_err.h` now); sys_flags_set(SYS_DISP_DEAD); }` — but call `disp_blit(s_fb_bits)` BEFORE `disp_init` so init's full refresh shows the boot screen. Nothing else in `ui.c` changes in this task (the dirty-box log stays; Task 7 wires the policy).
- [ ] **Step 4: Gate (software)** — lint 0; host suite green; clean `moto_sim` + `moto_neo6m` builds 0 warnings; DRAM line (+~48 B statics + the SPI device on the heap: report `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` from `dbg mem` on the bench).
- [ ] **Step 5: Flash gate `p07-d1` (user present, both boards on USB, jumper out, panel wired):** flash `moto_sim`; boot log shows `disp_init` ok and the BOOT screen appears on the panel within ~3 s of the ui task start, legible (name, version, self-test lines), no task-WDT, `lt status` still 34 ms-class, `stream stats` ≈ 10 fused/s. Photo of the panel into the ledger. If BUSY never clears: check the BS pad (must be 0) and the DC/RST/BUSY wiring before touching code. Tag `p07-d1`.
- [ ] **Step 6: Commit** — `git commit -m "feat(display): SSD1680 driver (SPI3 DMA, DC pre-cb, BUSY poll, init/full/partial/sleep/wake/reinit) + boot screen on the panel (Plan 7 T5)"`

---

### Task 6: Refresh decision as a pure function (host-tested)

**Files:**
- Create: `components/core/ui/refresh_policy.c`, `components/core/ui/include/core/ui/refresh_policy.h`
- Test: `test/test_refresh_policy.c`

**Interfaces:**
- Produces:

```c
typedef enum { RF_NONE = 0, RF_PARTIAL = 1, RF_FULL = 2 } rf_kind_t;
typedef struct {
    bool     dirty;              /* something changed this batch */
    bool     wants_full;         /* event asked for a full (page/menu entry, wake, UP+DOWN combo) */
    bool     still;              /* gspeed below MENU_LOCK_SPEED_KMH */
    bool     throttled;          /* SYS_DISP_TEMP_THROTTLE set */
    bool     dead;               /* SYS_DISP_DEAD set */
    uint8_t  full_every;         /* cfg display.full_every (1..50) */
    uint16_t partial_count;      /* partials since the last full */
    int64_t  now_us, last_full_us, last_partial_us;
} rf_in_t;
rf_kind_t ui_refresh_decide(const rf_in_t *in);
```
Rules (in this order): `dead` → `RF_NONE`; `!dirty && !wants_full` → `RF_NONE`; `throttled` → `RF_PARTIAL` only if `now - last_partial >= 30 s` else `RF_NONE` (fulls suppressed); full if `wants_full && still`, or `partial_count >= full_every && still`, or `now - last_full >= 30 min`, or `partial_count >= 2*full_every` (moving cap) → `RF_FULL`; otherwise `RF_PARTIAL`.

- [ ] **Step 1: Failing test** `test/test_refresh_policy.c` — one case per rule:
```c
static rf_in_t base(void) { rf_in_t i = {0}; i.dirty = true; i.full_every = 10; i.now_us = 100000000; i.last_full_us = 90000000; i.last_partial_us = 99000000; return i; }
static void test_dead_never_refreshes(void) { rf_in_t i = base(); i.dead = true; i.wants_full = true; TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i)); }
static void test_not_dirty_is_none(void) { rf_in_t i = base(); i.dirty = false; TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i)); }
static void test_plain_dirty_is_partial(void) { rf_in_t i = base(); TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i)); }
static void test_counter_full_only_when_still(void) { rf_in_t i = base(); i.partial_count = 10; TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i)); i.still = true; TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i)); }
static void test_moving_cap_forces_full(void) { rf_in_t i = base(); i.partial_count = 20; TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i)); }
static void test_thirty_minutes_forces_full(void) { rf_in_t i = base(); i.last_full_us = i.now_us - 1800000000LL; TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i)); }
static void test_wants_full_deferred_while_moving(void) { rf_in_t i = base(); i.wants_full = true; TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i)); i.still = true; TEST_ASSERT_EQUAL_INT(RF_FULL, ui_refresh_decide(&i)); }
static void test_throttle_limits_partials_and_blocks_full(void) { rf_in_t i = base(); i.throttled = true; i.still = true; i.wants_full = true; TEST_ASSERT_EQUAL_INT(RF_NONE, ui_refresh_decide(&i)); i.last_partial_us = i.now_us - 31000000LL; TEST_ASSERT_EQUAL_INT(RF_PARTIAL, ui_refresh_decide(&i)); }
```
(+ `main()` registering all eight.)
- [ ] **Step 2: Run** → link failure. **Step 3: Implement** the rules verbatim (constants `RF_THROTTLE_MIN_US 30000000LL`, `RF_FULL_MAX_AGE_US 1800000000LL`; two `CORE_ASSERT_RET`s: `in != NULL`, `in->full_every >= 1`). **Step 4: Run** → 8/8. **Step 5: Commit** — `git commit -m "feat(ui): pure refresh-policy decision (partial/full/defer/throttle) with a table test (Plan 7 T6)"`

---

### Task 7: `ui` wired to the driver — policy, ladder, throttle (flash gate `p07-d2`)

**Files:**
- Modify: `components/app/ui/ui.c` (`render_now` ~L582-596, `ui_loop_iter` ~L602-656, `ui_task` ~L658-700, `btn_combo` ~L386)
- Modify: `components/app/include/app/lt_err.h` (codes from Task 5 if not yet added)

**Interfaces:**
- Consumes: `ui_refresh_decide` (Task 6), `disp_*` (Task 5), `sys_flags_get/set/clear`, `errlog_add`, `s_cfg.display.full_every`, `s_gspeed_kmh` (still = `< MENU_LOCK_SPEED_KMH`), `EV_*` handling already in `ui.c`.
- Produces: on-panel behaviour per §20.3; statics (all in `ui.c`): `uint16_t s_partial_count; int64_t s_last_full_us, s_last_partial_us; uint8_t s_fail_streak; int64_t s_next_reinit_us; bool s_wants_full;`.

- [ ] **Step 1:** `render_now()` becomes `render_and_refresh()`: render as today; then build an `rf_in_t` (`dirty = true`, `wants_full = s_wants_full`, `still = s_gspeed_kmh < MENU_LOCK_SPEED_KMH`, `throttled = sys_flags_get() & (1u << SYS_DISP_TEMP_THROTTLE)`, `dead = sys_flags_get() & (1u << SYS_DISP_DEAD)`, counters), `kind = ui_refresh_decide(&in)`; `RF_NONE` → return; `RF_PARTIAL` → `disp_set_window(dirty box)` + `disp_refresh(DISP_PARTIAL)`, `s_partial_count++`, `s_last_partial_us = now`; `RF_FULL` → `disp_refresh(DISP_FULL)`, `s_partial_count = 0`, `s_last_full_us = now`, `s_wants_full = false`. Keep the `ESP_LOGI` line but add the kind (`kind=P/F/N`).
- [ ] **Step 2: Ladder** in a helper `disp_after(int rc)`: `rc == 0` → `s_fail_streak = 0`; `rc == -ETIMEDOUT` → `errlog_add(E_DISP_BUSY_TIMEOUT, s_fail_streak)`, `disp_reinit()`, retry the same refresh once; a second failure → `s_fail_streak++`; `s_fail_streak >= 3` → `sys_flags_set(SYS_DISP_DEAD)`, `errlog_add(E_DISP_DEAD, 0)`, `s_next_reinit_us = now + 300 s`. In `ui_loop_iter`, when `SYS_DISP_DEAD` is set and `now >= s_next_reinit_us`: `disp_reinit()` → 0 clears the flag (`sys_flags_clear`), `s_fail_streak = 0`, `s_wants_full = true`; else reschedule +300 s.
- [ ] **Step 3: Full-refresh triggers**: set `s_wants_full = true` on page change, menu entry/exit, the UP+DOWN combo (`btn_combo`, replacing its "log only" body), wake from PARK (where the ui learns of a wake — the `EV_WAKE`-equivalent path already present; if none exists, on the first loop after `ui_start`), and every 30 min via the policy. Never refresh on a timer while riding unless `s_cfg.display.live_clock` (then a 1 s tick sets `s_dirty` only for the CUR row on the LAP page).
- [ ] **Step 4: Heartbeat**: `g_hb[HB_UI]++` must still happen at least once per loop iteration including the ~2 s full refresh (`UI_STALL_S` is 10 s — fine; assert the loop's own accounting: one heartbeat per iteration, unchanged).
- [ ] **Step 5: Gate (software)** — lint 0 (split helpers to stay under the cap), host suite green, clean builds 0 warnings, DRAM line (+~24 B).
- [ ] **Step 6: Flash gate `p07-d2` (user present):** flash `moto_sim`; on the panel: BOOT → VENUE one-shot → LAP page; sim laps update BEST/PREV/CUR with partial refreshes (log lines `kind=P`), a full refresh (`kind=F`) after `full_every` partials once still (sim: speed 0 between laps? if the sim never goes still, verify the moving cap at 20); `stream stats 10` on the dev-kit ≈ 10 fused/s, 0 gaps during refreshes; no task-WDT over 10 min. Tag `p07-d2`.
- [ ] **Step 7: Commit** — `git commit -m "feat(ui): panel refreshes through the pure policy — partial with dirty window, full on counter/still/30 min/wake, ladder + temp throttle (Plan 7 T7)"`

---

### Task 8: Button + flag injection for the bench (`dbg btn`, `dbg flag`)

**Files:**
- Modify: `components/drivers/export_serial/export_serial.c` (`cmd_dbg` ~L776-800 + two helpers), `components/app/include/app/ui.h` (expose `QueueHandle_t ui_buttons_queue(void)` if not already public — `ui_buttons.c:42` returns it)

**Interfaces:**
- Produces: console `dbg btn <mode|up|down|up+down> [hold_ms]` → posts `btn_raw_t{mask, mono_us}` press now and schedules the release `hold_ms` later (default 100) by posting the release event with `mono_us = now + hold_ms*1000` — the ui's debounce samples levels 30 ms after an edge, so the injection must also make `board_buttons_read()` report the pressed mask during the hold: add `void board_buttons_override(uint8_t mask, bool on)` to `board.h`/`board.c` (a static override mask OR-ed into `read_button_mask()`; cleared by the release). `dbg flag set|clear <bit>` → `sys_flags_set/clear(bit)` (bit 0..15; used on the bench to force `SYS_DISP_TEMP_THROTTLE` = 11).
- Consumes: `ui_buttons_queue()`, `board_buttons_read`, `sys_flags_*`.

- [ ] **Step 1:** Implement `dbg_btn(argc, argv)`: parse the name → mask (`mode`=bit0, `up`=bit1, `down`=bit2, `up+down`=bit1|bit2); `hold_ms` via `strtoul` with full-consumption check, clamp 20..5000; `board_buttons_override(mask, true)`; post the press; `vTaskDelay(pdMS_TO_TICKS(hold_ms))` (the console task blocks — acceptable, bounded); `board_buttons_override(mask, false)`; post the release; print `OK btn <name> <ms>`. `dbg flag set|clear <bit>`: bounds check, print `OK flag <bit> <state>`. Bad args → `usage:` line, return 1.
- [ ] **Step 2: Test on host** — the mask/hold parser is a pure helper `int btn_parse(const char *name, const char *ms, uint8_t *mask, uint32_t *hold_ms)` (0 ok, -1 bad name, -2 bad hold) in `components/core/util/btn_parse.c` + `include/core/btn_parse.h` (`export_serial.c` is IDF-bound, so the parser lives in core to be host-testable); `test/test_btn_parse.c` covers the four names, `up+down`, a bad name, and `hold` default 100 / clamp 20..5000 / garbage — RED → GREEN.
- [ ] **Step 3: Gate** — lint 0, host suite green, clean builds 0 warnings; on the bench (after Task 7's flash or with the next one): from the dev-kit `lt shell` → `dbg btn mode 1200` → the LT log shows a long press → menu opens on the panel; `dbg btn up+down 2000` → full refresh (`kind=F`); `dbg flag set 11` → subsequent partials limited to one per 30 s; `dbg flag clear 11` restores.
- [ ] **Step 4: Commit** — `git commit -m "feat(export_serial): dbg btn / dbg flag injection for bench-driven UI tests (Plan 7 T8)"`

---

### Task 9: Bench session 7.3 — ghosting, ladder, throttle, real buttons, menu (flash gate `p07-d3`)

**Files:** none planned; fixes found on the bench get their own commits (each with host test/lint/build as usual). Ledger entries for every measurement.

- [ ] **Step 1 (before the switches):** with Task 8's image: `dbg btn` drives the menu end-to-end on the panel (open, UP/DOWN scroll, select Units, long-MODE back, 30 s auto-exit); ghosting test: 50 partial refreshes — `lt shell` → `dbg btn down 100` fifty times on the LAP page (each page change is a partial; the sim's laps add more) — then `dbg btn up+down 2000` → one full refresh clears the ghosting (photo before/after). Ladder: lift the BUSY wire → next refresh times out (5 s) → `E_DISP_BUSY_TIMEOUT` in `errlog`, reinit + retry, after 3 consecutive → `E_DISP_DEAD` + `SYS_DISP_DEAD` (`lt status` flags bit 4), refresh attempts stop; reattach BUSY → within 300 s the reinit succeeds and the flag clears (or `dbg flag clear 4` + a combo full to shortcut, note which). Throttle: `dbg flag set 11` → partials ≤ 1/30 s, no fulls; clear → normal.
- [ ] **Step 2 (switches wired to 32/33/25 with 100 kΩ pull-downs):** debounce: 20 rapid presses register 20 short presses (LT log); long ≥ 1000 ms fires once; very long ≥ 3000 ms; UP+DOWN 2 s → full refresh; menu navigation on the panel with real buttons; PIT/PARK wake (if the power state machine already parks on the sim — otherwise note "PARK wake deferred to Plan 6 power").
- [ ] **Step 3:** All results and photos in the ledger; every deviation from §20.3/§20.7/§20.8 fixed with a commit or recorded as a ruling. Tag `p07-d3` = `plan-07-done`.

---

### Task 10: Docs — roadmap, BOM, spec notes

**Files:**
- Modify: `docs/superpowers/plans/2026-09-14-roadmap.md` (Plan 7 rows → done with tags; "Plan 7 ran before Plan 6 (2026-09-25)"; sub-project C note: user-track table 11 KB + dev-only `dbg` family as strip candidates), `docs/hardware/bom.md` (item #4 → "Waveshare 2.13\" e-Paper HAT rev2.1, panel V4 (SSD1680, 122×250) — owned"; the 2.9" as an alternative panel), `docs/superpowers/specs/2026-09-25-plan-7-display-deltas.md` (an "Implementation notes" section: measured refresh times, DRAM after Task 1, anything the bench changed), `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §20.1 (panel table row `ws213v4` marked VERIFIED against the real panel, with the observed full/partial times).
- [ ] **Step 1:** Write the edits; `git diff --check` clean; commit `docs(plan-7): roadmap, BOM (owned 2.13\" V4), spec implementation notes`.
- [ ] **Step 2:** Final whole-branch review (opus), one fix round + scoped re-review, combined gate (lint, both host suites, all firmware builds, gcc sweep), push, CI, PR (user chooses merge).

---

## Self-review

- **Spec coverage:** deltas §1 hardware → T2 (pins, PANEL default), T5 (BS/wiring check at the gate); §2 canvas → T3; §3 DRAM → T1 (+ DRAM line in every task); §4 button injection → T8; §5 driver/ui → T2 (`hal/display.h`), T4/T5 (driver), T6/T7 (policy, ladder, `E_DISP_*`, boot screen); §6 testing → T3 goldens, T4/T6/T8 host tests, T5/T7/T9 gates; §7 sequencing → task order and tags. Base §20.3 rules → T6 table; §20.8 debounce → T9 Step 2; §22.3 ladder/throttle → T9 Step 1.
- **Placeholder scan:** none; every code step carries code or an exact command; T9 is a bench procedure by design.
- **Type consistency:** `disp_caps_t`/`disp_*` (T2) used by T5/T7; `epd_panel_t`, `epd_rotate_line`, `epd_window_t`, `epd_window_from_rect` (T4) used by T5; `rf_in_t`/`rf_kind_t`/`ui_refresh_decide` (T6) used by T7; `trk_user_add_json` (T1) used by T1's pipeline step; `CANVAS_W/H`, `LAP_TIME_FONT`, `LAP_CUR_FONT` (T3) used by `screens_moto.c`/`ui.c`/tests; `board_buttons_override` (T8) used only in T8.
