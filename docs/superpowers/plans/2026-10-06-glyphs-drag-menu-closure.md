# Glyphs, Drag Card and Menu Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the five bench-day-3 requests before the power work: the DRAG card says `NOT READY` until the engine is armed (#95); a `LINK` glyph shows the dev-kit is connected (#99); a distance-unit setting (m / ft) relabels the distance gates while 1/8 and 1/4 mile keep their names (#96); the Layout menu item cycles through the current venue's layouts and the screens show real venue/layout names (#98); the New track menu item drives the engine's CREATE mode end to end — S/F, sector gates, automatic finish on the next S/F crossing, the created venue persisted across reboots (#97).

**Architecture:** Pure first, wiring second, in five tasks each reviewable on its own. Tasks 1–2 are pure UI (+ one `update_flags()` hook). Task 3 adds a config field (`dist_units`, cfg blob version 2 with migration), a second unit argument to `drag_gate_label()`, and a cycling menu item. Task 4 adds a cycling Layout item backed by the track table and resolves venue/layout names in the ui. Task 5 wires CREATE mode: two new commands, a ui-only `EV_CREATE` event, a persistent NEW TRACK one-shot whose sub-line follows the create step, and two logger requests (save/load the user track table in `/tracks/user.bin`) so a created venue survives a reboot.

**Tech Stack:** ESP-IDF v5.3.2 (`source tools/idf-env.sh`), C11 under the Power-of-10 lint, host harness (CMake/CTest + Unity, PBM goldens on both canvases), `tools/devkit.py` for the bench.

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` — §6.6 (gate names / units), §10.9 (on-device track creation), §11.1 (drag gates), §15.2 (cfg schema), §20.5 (fault-icon strip), §20.6 (one-shots), §20.7 (menu). The approved in-chat design (2026-10-06): NOT READY → READY (the separate ARMED word is dropped, READY now means armed); `dist_units` independent of the speed units, default metres, miles kept for 1/8 and 1/4; Layout as a cycling item (no submenu); the link glyph from `link_peer_present()`; New track with S/F + sector gates (spec §10.9 scope 2).

## Global Constraints

- Zero warnings: host `-Wall -Wextra -Werror -Wshadow -Wconversion`; clean ccache-disabled firmware builds `./build.sh moto_sim build`, `./build.sh moto_neo6m build`, `PANEL=ws29v2 ./build.sh moto_sim build` print 0 for `grep -c -i "warning:"` (rc 0); **and the ESP32 selftest app** `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build` (rc 0, 0 warnings) — CI's required `build (moto_neo6m)` job compiles it and it globs every `test/test_*.c`; a host-only test must be added to the exclude regex in `test_apps/core_selftest/main/CMakeLists.txt` in the same commit.
- Power-of-10 lint 0 violations: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` (functions > 20 code lines need ≥ 2 `CORE_ASSERT_*`/`LT_ASSERT_*`; no function pointers; no goto; compound statements ≤ 30 code lines). gcc-16 sweep: `python3 /Users/benji/projects/personal/lap-timer/.superpowers/bench-helpers/gcc_sweep.py test/build/compile_commands.json` → 0 diagnostics.
- Host suite green (47 executables today; screens goldens byte-exact on BOTH canvases, regenerated only by the documented workflow in `test/test_screens.c`'s header and eyeballed, never hand-edited).
- DRAM: moto_sim free static DRAM baseline 4192 B (ledger 2026-10-05). No new static buffer ≥ 64 B; the user-track blob is serialised/loaded through the logger's existing batch buffer, never a new one. Report the free-DRAM line of every firmware build.
- ui-only events (`EV_CFG_CHANGED 16`, `EV_LAP_RESET 17`, `EV_OTA 18`, new `EV_CREATE 19`) are posted only with `xQueueSend(g_ui_evt_q, &ev, 0)`, never via `emit_event()`. Commands to the pipeline go through `g_cmd_q` (`ui_send_cmd()` in ui.c); every bound assert uses `CMD_TYPE_LAST` (lt_ipc.h).
- Storage mutations are single-owner (debt sweep A #73): only the logger task writes files; other tasks ask it through `log_request_t` + `logger_request_sync()`.
- Entering or leaving a one-shot sets `s_wants_full = true; s_screen_changed = true;` (ui.c, rulings B-9/B-15); in-place text updates are partials.
- Config is a read-modify-write through `lt_cfg_load()` / `lt_cfg_save()` (ui.c T-D pattern), and every ui-driven change posts `CMD_CONFIG_RELOAD` (cfg_change_notify pattern) so the pipeline reloads.
- Commit messages end with the trailer:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_014KyGHgU5MeENuESPYuKjED
  ```
- Never flash, never open a serial port during execution; one firmware build at a time; the bench check is the next flash day (items 5 and 1-drag of `docs/bench/next-bench-day.md`).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `components/core/ui/screens_moto.c` (+ `canvas.h`) | DRAG card `NOT READY`/`READY`; `LINK` icon in the strip table; venue/layout names; NEW TRACK sub-line | 1, 2, 4, 5 |
| `components/core/ui/include/core/ui/icons.h`, `components/core/ui/icons.c` | `ICON_LINK` bitmap | 2 |
| `components/core/ui/include/core/ui/model.h` | `SCR_UI_LINK = 16`, `SCR_STRIP_BITS 17`; `dist_units`; `create_step` | 2, 3, 5 |
| `components/app/ui/ui.c` | `update_flags()` link bit; `Dist:` and `Layout:` cycling items; venue/layout name resolution; New track flow (`EV_CREATE`, `CMD_MARK_GATE`, cancel) | 2, 3, 4, 5 |
| `components/core/include/core/cfg.h`, `components/core/config/cfg.c` | `dist_units` (CFG_DIST_M / CFG_DIST_FT), JSON key `dist_units`, validate, `CFG_VERSION 2` + `cfg_migrate(1→2)` | 3 |
| `components/core/include/core/drag.h`, `components/core/dragengine/drag_cfg.c` | `drag_gate_label(g, units, dist_units, buf, cap)` | 3 |
| `components/app/include/app/lt_ipc.h` | `CMD_CREATE_BEGIN 9`, `CMD_CREATE_CANCEL 10`, `CMD_TYPE_LAST`; `LOGGER_SAVE_TRACKS = 6`, `LOGGER_LOAD_TRACKS = 7` | 5 |
| `components/core/include/core/event.h` | `EV_CREATE 19` + phase codes | 5 |
| `components/app/pipeline/pipeline.c` | CREATE commands, `CMD_MARK_GATE` → `lap_mark_gate`, finish detection → save request; boot load request | 5 |
| `components/app/logger/logger.c` | save/load `/tracks/user.bin` through `trk_user_save`/`trk_user_load` using `s_batch` | 5 |
| `test/test_screens.c` (+ goldens both canvases), `test/test_drag_cfg.c`, `test/test_cfg.c`, `test/test_ui.c` | goldens (drag card, strip, NEW TRACK steps), label/cfg tests | 1–5 |
| `docs/display-glyphs.md`, spec §6.6 §10.9 §15.2 §20.5 §20.6 §20.7, `docs/bench/next-bench-day.md` | docs | 2, 3, 4, 5 |

---

### Task 1: DRAG card — `NOT READY` until armed, `READY` once armed (#95)

**Files:**
- Modify: `components/core/ui/screens_moto.c` (the DRAG card renderer around lines 600-700: the `"READY"` big-slot text at ~683 and the `"ARMED"` right-aligned label at ~689-690)
- Modify: `test/test_screens.c` + goldens `test/snapshots/*.pbm`, `test/snapshots/213/*.pbm` for the DRAG card cases (find them: `grep -n -i 'drag' test/test_screens.c`)
- Modify: spec §11.4 / §20.4 DRAG card wording (grep "READY" in the spec)

**Interfaces:**
- Consumes: `screen_model_t.drag_armed` (bool, set by the ui on `EV_DRAG_ARMED`, cleared on `EV_DRAG_LAUNCH`).
- Produces: nothing new.

- [ ] **Step 1: Read the renderer** — `sed -n 590,700p components/core/ui/screens_moto.c`: the big slot shows `"READY"` before the first gate (`BIG_NONE` while no run), and `"ARMED"` is drawn at `DCARD_ARMED_RIGHT_X/Y` when `m->drag_armed`.

- [ ] **Step 2: Make the existing DRAG card goldens fail first** — change the model setup of the "ready" DRAG card test(s) so one case has `drag_armed = false` and one `drag_armed = true` (add the second case if only one exists, e.g. `test_drag_card_armed`), with the expected goldens `dragcard_notready.pbm` / `dragcard_ready.pbm` (both canvases). Run `./test/build/test_screens` → the new/changed cases fail on the golden compare (red).

- [ ] **Step 3: Implement** — in the big-slot "no run yet" branch:
```c
    /* #95: READY now means armed. Until the engine has armed (2 s of stillness) the card says
     * NOT READY, so a rider never launches on a card that is not timing. The separate right-hand
     * ARMED label is gone for the same reason (it duplicated READY's new meaning). */
    const char *ready = m->drag_armed ? "READY" : "NOT READY";
    fb_text(fb, &FONT_MED, DCARD_BIG_X, DCARD_READY_Y, ready);
```
and delete the `if (m->drag_armed) { fb_text_right(... "ARMED"); }` block. If `"NOT READY"` in FONT_MED (24 px) does not fit the big slot on the 213 canvas (250 px visible; FONT_MED glyphs ~16 px wide → 9 glyphs ≈ 144 px: fits), keep FONT_MED; otherwise use FONT_SMALL for the NOT READY variant and say so in the report. Remove `DCARD_ARMED_RIGHT_X/Y` from `canvas.h` if nothing else uses them (grep).

- [ ] **Step 4: Promote the goldens** (documented workflow, eyeball: NOT READY left-aligned in the big slot on the unarmed card, READY on the armed one, no ARMED text at the top right) and re-run `test_screens` + `test_screens_213` byte-exact.

- [ ] **Step 5: Spec + commit** — spec: "the card reads NOT READY until the engine arms, then READY; launching clears it". Gates: `ctest` 47/47, lint 0, host 0 warnings. Commit: `feat(ui): DRAG card says NOT READY until armed, READY once armed; the duplicate ARMED label is gone (#95)` + trailer.

---

### Task 2: `LINK` glyph — dev-kit connected (#99)

**Files:**
- Modify: `components/core/ui/include/core/ui/icons.h` (`ICON_LINK` before `ICON_COUNT`), `components/core/ui/icons.c` (bitmap)
- Modify: `components/core/ui/include/core/ui/model.h` (`SCR_UI_LINK = 16`, `SCR_STRIP_BITS 17`)
- Modify: `components/core/ui/screens_moto.c` (`FAULT_ICON_FOR_BIT` entry 16 → `ICON_LINK`)
- Modify: `components/app/ui/ui.c` (`update_flags()`: `#include "app/link.h"`, OR in the bit from `link_peer_present()`)
- Modify: `test/test_screens.c` (+ goldens `strip_link.pbm` both canvases), `test/test_ui.c` golden `icons_and_bar.pbm` (it draws every icon — regenerate and eyeball), `docs/display-glyphs.md` (row), spec §20.5

**Interfaces:**
- Consumes: `bool link_peer_present(void)` (`components/app/include/app/link.h`: serial heartbeat/command seen within `LINK_PEER_TIMEOUT_MS`, or a BLE sink present).
- Produces: `SCR_UI_LINK = 16`, `SCR_STRIP_BITS = 17`, `ICON_LINK`.

- [ ] **Step 1: Failing golden** — add to `test/test_screens.c` (register after `test_strip_sim_and_moving`):
```c
/* Strip bit 16 (LINK: the dev-kit is connected) draws ICON_LINK; with SIM+MOVING it is the third slot. */
static void test_strip_link(void)
{
    screen_model_t m = {0};
    m.screen = SCR_RIDING; m.mode = SCR_MODE_LAP; m.page = 0;
    m.flags = (1u << SCR_UI_SIM) | (1u << SCR_UI_MOVING) | (1u << SCR_UI_LINK);
    screens_render(&s_fb, &m);
    TEST_ASSERT_TRUE(fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W);
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("strip_link.pbm"), &s_fb));
    /* three slots: ink in the third slot left of the anchor (LINK is bit 16, drawn last, leftmost) */
    TEST_ASSERT_TRUE(px(&s_fb, FAULT_STRIP_X0 - 2 * (ICON_W + 2) + 6, FAULT_STRIP_Y + 6));
}
```
(`px()` is the pixel probe added by the previous plan's Task 1 in test_screens.c.) Run → compile error on `SCR_UI_LINK`/`ICON_LINK` (red).

- [ ] **Step 2: Model + icon** — model.h, after `SCR_UI_MOVING = 15,`:
```c
    SCR_UI_LINK   = 16,   /* ICON_LINK: the dev-kit (or a BLE peer) is connected -- link_peer_present() */
```
and `#define SCR_STRIP_BITS 17` (keep `SCR_SYS_BITS_MASK 0x3FFFu`). icons.h: `ICON_LINK, /* dev-kit / peer connected (strip bit SCR_UI_LINK) */` before `ICON_COUNT`. icons.c, after `[ICON_MOVING]`:
```c
    /* ICON_LINK — two chain links: a peer is connected */
    [ICON_LINK] = {
        { 0x00, 0x00 }, { 0x1c, 0x00 }, { 0x22, 0x00 }, { 0x22, 0x00 },
        { 0x23, 0x80 }, { 0x1c, 0x40 }, { 0x02, 0x40 }, { 0x1c, 0x40 },
        { 0x23, 0x80 }, { 0x22, 0x00 }, { 0x22, 0x00 }, { 0x1c, 0x00 },
    },
```
screens_moto.c table: `(int8_t)ICON_LINK,          /* 16 SCR_UI_LINK (ui-level, model.h) */` after entry 15; the loops already use `SCR_STRIP_BITS`.

- [ ] **Step 3: Hook** — ui.c `update_flags()` (after the SIM/MOVING ORs):
```c
    if (link_peer_present()) f |= 1u << SCR_UI_LINK;   /* #99: the dev-kit / a BLE peer is talking to us */
```
with `#include "app/link.h"` (check `components/app/CMakeLists.txt` already links the `link` sources into `app`; ui.c is in the same component, so no REQUIRES change). `update_flags()` runs every ui loop iteration, so the glyph follows presence within one loop; the strip repaint is one slot partial.

- [ ] **Step 4: Goldens, docs, gates, commit** — promote `strip_link.pbm` (both canvases) and the regenerated `icons_and_bar.pbm` after eyeballing; `docs/display-glyphs.md`: a row "Chain links — LINK: the dev-kit (or a BLE peer) is connected; disappears a few seconds after the link goes quiet"; spec §20.5 one clause. Gates: `test_screens` ×2, `test_ui`, `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` build 0 warnings (DRAM delta ≈ 0). Commit: `feat(ui): LINK strip glyph when a peer is connected (#99)` + trailer.

---

### Task 3: Distance units m / ft for the distance gates (#96)

**Files:**
- Modify: `components/core/include/core/cfg.h` (field + enum + `CFG_VERSION 2`), `components/core/config/cfg.c` (defaults, validate, JSON parse/serialise, `cfg_migrate` 1→2)
- Modify: `components/core/include/core/drag.h` + `components/core/dragengine/drag_cfg.c` (`drag_gate_label(g, units, dist_units, buf, cap)`), every caller (`grep -rn drag_gate_label components test`)
- Modify: `components/core/ui/include/core/ui/model.h` (`uint8_t dist_units;`), `components/app/ui/ui.c` (menu item `Dist: m|ft`, `MA_DIST`, model field, `ui_reload_cfg()`), `components/app/pipeline/pipeline.c` only if it logs units
- Modify: `test/test_cfg.c`, `test/test_drag_cfg.c`, `test/test_screens.c` (a DRAG page-1 golden with ft and one with m, both canvases)
- Modify: spec §6.6, §15.2 (schema), §20.7 (menu); `docs/display-glyphs.md` is unaffected

**Interfaces:**
- Produces: `enum { CFG_DIST_M = 0, CFG_DIST_FT = 1 };`, `cfg_t.dist_units` (uint8_t, JSON key `"dist_units"` with values `"m"` / `"ft"`), `CFG_VERSION 2`, `int drag_gate_label(const drag_gate_def_t *g, uint8_t units, uint8_t dist_units, char *buf, size_t cap)`, `screen_model_t.dist_units`, menu action `MA_DIST`.

- [ ] **Step 1: Failing tests** — `test/test_drag_cfg.c`:
```c
/* #96: DIST gates print feet or metres by dist_units; 1/8 and 1/4 mile keep their names in both. */
static void test_gate_label_dist_units(void)
{
    char b[16];
    const drag_gate_def_t ft60   = { 6, DRAG_DIST, 1829,  0 };
    const drag_gate_def_t ft330  = { 7, DRAG_DIST, 10058, 0 };
    const drag_gate_def_t eighth = { 8, DRAG_DIST, 20117, 0 };
    const drag_gate_def_t ft1000 = { 9, DRAG_DIST, 30480, 0 };
    const drag_gate_def_t quarter= { 10, DRAG_DIST, 40234, 0 };
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, CFG_DIST_FT, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("60ft", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0);  TEST_ASSERT_EQUAL_STRING("18m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft330, DRAG_UNITS_MPH, CFG_DIST_M, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("101m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft1000, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0);TEST_ASSERT_EQUAL_STRING("305m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&eighth, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("1/8", b);
    TEST_ASSERT_TRUE(drag_gate_label(&quarter, DRAG_UNITS_KMH, CFG_DIST_FT, b, sizeof b) > 0);TEST_ASSERT_EQUAL_STRING("1/4", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, 2, b, sizeof b) < 0);   /* invalid dist_units rejected */
}
```
`test/test_cfg.c`: defaults give `dist_units == CFG_DIST_M`; `cfg_from_json("{\"dist_units\":\"ft\"}")` sets FT and `"m"` sets M, `"yd"` is an error; `cfg_to_json` round-trips the key; `cfg_validate` clamps 2 → M; `cfg_migrate(c, 1)` from a version-1 struct image leaves `dist_units == CFG_DIST_M` and sets `version == 2`; `cfg_migrate_supported(1)` true, `(2)` per the existing rule for the current version (read `cfg_migrate_supported` — if it only accepts older versions, keep that shape). Run → compile errors (red).

- [ ] **Step 2: cfg** — cfg.h: `#define CFG_VERSION 2`, `enum { CFG_DIST_M = 0, CFG_DIST_FT = 1 };`, field `uint8_t dist_units;  /* CFG_DIST_*: DIST gate labels only (#96); 1/8 and 1/4 mile keep their names */` right after `units`. cfg.c: default `c->dist_units = CFG_DIST_M;`, validate `if (c->dist_units > CFG_DIST_FT) { c->dist_units = CFG_DIST_M; n++; }`, JSON: parse `"dist_units"` as the strings `"m"`/`"ft"` next to how `"units"` parses `"kmh"`/`"mph"` (mirror its code exactly — same error path for an unknown string), serialise it next to `"units"`. `cfg_migrate()`: `case 1: c->dist_units = CFG_DIST_M; /* field added in v2 */` then `c->version = CFG_VERSION`; `cfg_migrate_supported(1) == true`. Check how the NVS cfg blob (`lt_cfg_load`, debt sweep A `blob_wrap` with a version) decides to call `cfg_migrate` — read `components/app/sys/lt_nvs.c` `lt_cfg_load`: it must migrate a stored v1 blob (shorter struct) rather than reset it; if the blob layer keys on `sizeof(cfg_t)`, follow its documented path for a grown struct and prove it with the existing `test_lt_nvs`-style host test if one exists (grep `cfg_migrate` in `test/`).

- [ ] **Step 3: Label** — drag.h: `int drag_gate_label(const drag_gate_def_t *g, uint8_t units, uint8_t dist_units, char *buf, size_t cap);` (doc: `dist_units` = `CFG_DIST_*`; applies to DIST gates whose `a` is one of the feet presets 1829/10058/30480 cm → `60ft/330ft/1000ft` or `18m/101m/305m`; 20117 → `1/8`, 40234 → `1/4` in both; any other DIST `a` prints `<a/100>m`). drag_cfg.c: add `CORE_ASSERT_RET(dist_units <= 1u, DRAGCFG_ASSERT_CODE, -1);` and
```c
    case DRAG_DIST: {
        const char *name = g->a == 20117u ? "1/8" : g->a == 40234u ? "1/4" : NULL;      /* miles: both unit modes */
        if (name == NULL && dist_units == CFG_DIST_FT)
            name = g->a == 1829u ? "60ft" : g->a == 10058u ? "330ft" : g->a == 30480u ? "1000ft" : NULL;
        if (name != NULL) { int n = snprintf(buf, cap, "%s", name); return (n < 0 || (size_t)n >= cap) ? -1 : n; }
        int n = snprintf(buf, cap, "%um", (unsigned)((g->a + 50u) / 100u));              /* metres, rounded */
        return (n < 0 || (size_t)n >= cap) ? -1 : n;
    }
```
(`drag_cfg.c` includes `core/cfg.h` for `CFG_DIST_*` — or mirror the two values as `DRAG_DIST_M/FT` with a `_Static_assert` equal to the cfg ones, whichever the existing `DRAG_UNITS_*` / `CFG_UNITS_*` pairing does; follow that precedent.) Update every caller: ui.c:878 passes `s_model.dist_units`; tests pass an explicit value.

- [ ] **Step 4: ui** — model.h `uint8_t dist_units;` next to `units`; ui.c: `MA_DIST` after `MA_UNITS` in the action enum (keep `MA_SLEEP` last — the assert `act <= MA_SLEEP`), `static char s_lbl_dist[16];`, `menu_add(&n, s_lbl_dist, MA_DIST);` right after the Units item (menu capacity `UI_MENU_MAX 12`: 11 items today; the Layout item already exists and is only relabelled in Task 4, so `Dist:` makes 12 = the cap, assert-checked — do not add anything else), and
```c
static void menu_do_dist(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D) */
    s_cfg.dist_units = (s_cfg.dist_units == CFG_DIST_FT) ? (uint8_t)CFG_DIST_M : (uint8_t)CFG_DIST_FT;
    (void)lt_cfg_save(&s_cfg);
    ui_send_cmd(CMD_CONFIG_RELOAD, 0, 0);   /* the pipeline mirrors cfg; labels are ui-side */
    s_model.dist_units = s_cfg.dist_units;
    s_dirty            = true;
    snprintf(s_lbl_dist, sizeof s_lbl_dist, "Dist: %s", s_cfg.dist_units == CFG_DIST_FT ? "ft" : "m");
    if (s_model.mode == SCR_MODE_DRAG) drag_rows_refill();
}
```
`case MA_DIST: menu_do_dist(); break;`; `build_menu()` formats `s_lbl_dist` from `s_cfg`; `ui_reload_cfg()` copies `dist_units` into the model and refreshes the label (mirror what it does for `units`). The dev-kit config page renders the new key automatically (generic form) — note it in the report.

- [ ] **Step 5: Goldens + spec + gates + commit** — DRAG page 1 goldens: one case with `dist_units = CFG_DIST_FT` (today's look) and one with `CFG_DIST_M` (18m/101m/305m), both canvases; §6.6 names table gets the metres column; §15.2 schema gets `dist_units: "m"|"ft"` (default m) and the blob version 2 note; §20.7 the `Dist:` item. Gates: `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` + `moto_neo6m` builds 0 warnings + the selftest app build. Commit: `feat(cfg,ui): dist_units m|ft for the DRAG distance gates (cfg v2 + migrate), Dist: menu item; 1/8 and 1/4 keep their names (#96)` + trailer.

---

### Task 4: Layout cycling item + real venue/layout names (#98)

**Files:**
- Modify: `components/app/ui/ui.c` (`handle_venue_found()`, `handle_layout_locked()`, the Layout item: `s_lbl_layout`, `menu_do_layout()`, `s_venue_id`, `s_layout_choice`)
- Modify: `components/core/ui/screens_moto.c` only if the venue line needs a wider field (venue names are ≤ 31 chars, layout names ≤ 23: check the one-shot and card fields `venue_name[33]`/`layout_name[25]` — they fit)
- Modify: `test/test_ui.c` or a new pure helper test if the label formatting is factored out (see Step 2)
- Modify: spec §20.7 (Layout item), `docs/display-glyphs.md` unaffected

**Interfaces:**
- Consumes: `const trk_venue_t *trk_get(uint16_t venue_id)` (core/trk.h; NULL if unknown), `trk_venue_t.name[32]`, `.n_layouts`, `.layouts[i].id/.name[24]`; `EV_VENUE_FOUND.arg16` = venue id; `EV_LAYOUT_LOCKED.arg16` = layout id; `CMD_SET_LAYOUT` (arg16 = layout id, 0 = Auto → `lap_force_layout()` in the pipeline, which also clears `s_best`).
- Produces: menu label `Layout: Auto | <layout name>`; `screen_model_t.venue_name` now holds the venue's real name, `layout_name` the layout's.

- [ ] **Step 1: Pure label helper + failing test** — add to `components/core/ui/` a tiny pure function (new file `components/core/ui/menu_labels.c` + prototype in `model.h`, or next to an existing pure ui helper if one fits):
```c
/* "Layout: Auto" or "Layout: <name>" for the choice index (0 = Auto, i = venue->layouts[i-1]);
 * a NULL venue or an out-of-range index reads "Layout: Auto". Truncates to cap-1. */
int ui_layout_label(const trk_venue_t *venue, uint8_t choice, char *buf, size_t cap);
```
with a host test in `test/test_ui.c` (register it): NULL venue → `"Layout: Auto"`; a two-layout venue with choice 2 → `"Layout: Short"`; choice 9 → `"Layout: Auto"`; a 16-byte buffer truncates without overflow. Run → red.

- [ ] **Step 2: ui wiring** — statics `static uint16_t s_venue_id; static uint8_t s_layout_choice; static char s_lbl_layout[32];`. `handle_venue_found()`: `s_venue_id = e->arg16; s_layout_choice = 0;` and `const trk_venue_t *v = trk_get(e->arg16); snprintf(s_model.venue_name, sizeof s_model.venue_name, "%s", v ? v->name : "VENUE");` (replaces the `"V%u"` placeholder — this is the #98 "real names" half; keep `layout_name[0] = '\0'`). `handle_layout_locked()`: resolve `e->arg16` to the layout name through `trk_get(s_venue_id)` the same way. `menu_do_layout()`:
```c
static void menu_do_layout(void)
{
    const trk_venue_t *v = trk_get(s_venue_id);
    uint8_t n = (v != NULL) ? v->n_layouts : 0u;
    LT_ASSERT_VOID(n <= TRK_MAX_LAYOUTS, UI_APP_ASSERT_CODE);
    s_layout_choice = (uint8_t)((s_layout_choice + 1u) % (n + 1u));          /* Auto, L1, L2, ..., Auto */
    uint16_t id = (s_layout_choice == 0u || v == NULL) ? 0u : v->layouts[s_layout_choice - 1u].id;
    ui_send_cmd(CMD_SET_LAYOUT, 0, id);                                       /* 0 = Auto (lap_force_layout) */
    (void)ui_layout_label(v, s_layout_choice, s_lbl_layout, sizeof s_lbl_layout);
    ESP_LOGI(TAG, "menu: layout choice %u -> id %u", (unsigned)s_layout_choice, (unsigned)id);
    s_dirty = true;
}
```
`case MA_LAYOUT: menu_do_layout(); break;` replaces the stub; `build_menu()` uses `s_lbl_layout` (initialised to `"Layout: Auto"`). The choice is runtime-only and resets to Auto on the next `EV_VENUE_FOUND` (spec: manual override per venue, not persisted — say so in §20.7).

- [ ] **Step 3: Gates + spec + commit** — `ctest`, lint, gcc-16, host 0 warnings, clean `moto_sim` build 0 warnings (DRAM: +~36 B statics, report). Spec §20.7: the Layout item cycles Auto → each layout of the current venue; the VENUE one-shot shows the forced layout's name when it locks. Commit: `feat(ui): Layout menu item cycles the venue's layouts (Auto first); venue and layout names resolved from the track table (#98)` + trailer.

---

### Task 5: New track — CREATE mode wired end to end, user venues persisted (#97)

**Design restated (spec §10.9, scope 2):** menu "New track" → `CMD_CREATE_BEGIN` → the pipeline calls `lap_create_begin(&s_lap)` and posts `EV_CREATE` (phase BEGUN) → the ui shows the NEW TRACK one-shot (persistent, no timer) with the sub-line "Cross S/F, press MODE". Each short MODE on that one-shot sends `CMD_MARK_GATE` with the next gate index (0 = S/F, then 1..`LAP_MAX_SECTORS`); the pipeline calls `lap_mark_gate(&s_lap, idx, &s_last_fix, NULL)` and posts `EV_CREATE` GATE_SET (arg16 = idx) or FAILED (the engine refuses: no valid fix, not moving, out of order). The engine finishes by itself on the next S/F crossing (`finalize_create()` → `trk_user_add` → `EV_VENUE_FOUND` with the new id); the pipeline, seeing that event while it is in create mode, asks the logger to persist the user table (`LOGGER_SAVE_TRACKS`), and the ui's normal `EV_VENUE_FOUND` handling replaces the NEW TRACK one-shot with the VENUE one-shot showing `Track_YYYYMMDD`. Long MODE on the one-shot sends `CMD_CREATE_CANCEL` (`lap_create_cancel()`), the ui returns to riding. At boot the pipeline asks the logger to load `/tracks/user.bin` (`LOGGER_LOAD_TRACKS`) right after `trk_init()` and before the sim venue is registered, so created venues survive reboots.

**Files:**
- Modify: `components/app/include/app/lt_ipc.h` (`CMD_CREATE_BEGIN = 9`, `CMD_CREATE_CANCEL = 10`, `#define CMD_TYPE_LAST CMD_CREATE_CANCEL`; `log_req_type_t` + `LOGGER_SAVE_TRACKS = 6`, `LOGGER_LOAD_TRACKS = 7` (value 4 is retired, never reuse it))
- Modify: `components/core/include/core/event.h` (`EV_CREATE = 19` + `EV_CREATE_BEGUN 0 / GATE_SET 1 / FAILED 2 / CANCELLED 3`)
- Modify: `components/app/pipeline/pipeline.c` (`handle_cmd` cases, `s_create_active`, `s_create_next_gate`, the EV_VENUE_FOUND hook in `engine_cb`, the boot load after `trk_init()` ~line 773)
- Modify: `components/app/logger/logger.c` (two request handlers using `s_batch`/`BATCH_CAP` as the blob buffer; path `/tracks/user.bin`; `sto_open/sto_write/sto_read/sto_close`; create `/tracks` if the mount ladder does not — check `storage_internal`'s dir list)
- Modify: `components/core/ui/include/core/ui/model.h` (`uint8_t create_step;` 0 = waiting for S/F, k = k gates set; `0xFF` = last mark failed), `components/core/ui/screens_moto.c` (`render_oneshot_newtrack`: sub-line by step), `components/app/ui/ui.c` (`MA_NEWTRACK` → begin; `handle_create()`; `btn_short`/`btn_long` on `ONESHOT_NEWTRACK`; `s_create_next_gate` mirror)
- Modify: `test/test_screens.c` (+ goldens `newtrack_step0/1/3/fail` both canvases), `test/test_lap.c` (already covers create; add a case that `lap_mark_gate` with `gate_idx 2` before `1` is refused if missing), `test/test_trk.c` if a `trk_user_save`/`trk_user_load` round trip is not yet tested (grep)
- Modify: spec §10.9 (the app-side flow and persistence), §20.6 (NEW TRACK one-shot sub-lines), §20.7; `docs/bench/next-bench-day.md` item 5 "New track" procedure

**Interfaces:**
- Consumes: `lap_create_begin(lap_t*)`, `int lap_mark_gate(lap_t*, uint8_t gate_idx, const gps_fix_t*, trk_layout_t *out)` (0 ok / -1 refused), `lap_create_cancel(lap_t*)`, `finalize_create` → `EV_VENUE_FOUND` (arg16 = new id ≥ `TRK_USER_ID_BASE 1000`), `int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)`, `int trk_user_load(const uint8_t *blob, size_t n)`, `logger_request_sync(const log_request_t*, timeout_ms)`, `LAP_MAX_SECTORS 8`, the pipeline's last valid fix (keep a `static gps_fix_t s_last_fix` copy in `on_fix()` when `valid`, 56 B — the one allowed static of this task; report the DRAM delta).
- Produces: the commands/events above; `screen_model_t.create_step`; sub-lines: step 0 `"Cross S/F, press MODE"`, step k (1..8) `"S/F set. MODE: sector k"` (k = next sector number) once k ≤ LAP_MAX_SECTORS else `"Cross S/F to finish"`, failed `"No fix / not moving"` (shown until the next event); `/tracks/user.bin` = the `trk_user_save` blob.

- [ ] **Step 1: Failing goldens + pure sub-line** — put the sub-line choice in a pure function in screens_moto.c (`static const char *newtrack_subline(uint8_t step)`, table-driven: 0, 1..8, 0xFF) and test it through goldens: `test_oneshot_newtrack_step0` (existing golden `newtrack.pbm` keeps its look), `_step1` (`create_step = 1` → "S/F set. MODE: sector 1"), `_step3`, `_fail` (`0xFF`). Register them. Run → the three new goldens fail (red).

- [ ] **Step 2: Model + renderer** — model.h `uint8_t create_step;` (comment as above). screens_moto.c `render_oneshot_newtrack()`: replace the constant sub with `newtrack_subline(m->create_step)`; the string constants:
```c
static const char NEWTRACK_SUB_SF[]     = "Cross S/F, press MODE";
static const char NEWTRACK_SUB_FINISH[] = "Cross S/F to finish";
static const char NEWTRACK_SUB_FAIL[]   = "No fix / not moving";
/* "S/F set. MODE: sector k" for k = 1..LAP_MAX_SECTORS: built into a 24-char buffer by the renderer */
```
Promote the goldens after eyeballing (both canvases).

- [ ] **Step 3: Commands, events, logger requests** — lt_ipc.h: `CMD_CREATE_BEGIN = 9, /* menu New track: lap_create_begin */`, `CMD_CREATE_CANCEL = 10,`, `#define CMD_TYPE_LAST CMD_CREATE_CANCEL`; `log_req_type_t` (members are named `LOGGER_*`; 0-3 and 5 exist, 4 is retired): `LOGGER_SAVE_TRACKS = 6, /* serialise the user track table into /tracks/user.bin (requester notified with rc) */`, `LOGGER_LOAD_TRACKS = 7, /* load /tracks/user.bin into the user track table (boot; requester notified) */`. event.h: `EV_CREATE = 19 /* ui-only: flags = EV_CREATE_* phase, arg16 = gate index (GATE_SET) or reason (FAILED) */` + `#define EV_CREATE_BEGUN 0u`, `EV_CREATE_GATE_SET 1u`, `EV_CREATE_FAILED 2u`, `EV_CREATE_CANCELLED 3u`.

- [ ] **Step 4: Logger** — in the request handler switch:
```c
    case LOGGER_SAVE_TRACKS: rc = tracks_save(); break;
    case LOGGER_LOAD_TRACKS: rc = tracks_load(); break;
```
```c
/* The user track table goes to /tracks/user.bin as trk_user_save()'s blob. s_batch is the only
 * buffer large enough and this task owns it; both requests run between batches (flush first). */
static int tracks_save(void)
{
    LT_ASSERT_RET(s_batch_len == 0 || batch_flush() == 0, LOG_ASSERT_CODE, -1);   /* the batch write lives at logger.c:187 (sto_write(s_log_fd, s_batch, s_batch_len)); factor it into batch_flush() if no helper exists */
    size_t n = 0;
    if (trk_user_save(s_batch, sizeof s_batch, &n) != 0) return -1;
    sto_file_t f;
    if (sto_open(TRACKS_USER_PATH, STO_WR | STO_CREATE, &f) != 0) return -1;
    int rc = sto_write(f, s_batch, n);
    (void)sto_close(f);
    if (rc != 0) (void)errlog_add(E_STO_WRITE, (uint32_t)n);
    return rc;
}
static int tracks_load(void)
{
    LT_ASSERT_RET(s_batch_len == 0, LOG_ASSERT_CODE, -1);           /* boot: nothing buffered yet */
    if (sto_exists(TRACKS_USER_PATH) != 1) return 0;                  /* no file: nothing to load */
    sto_file_t f; size_t n = 0;
    if (sto_open(TRACKS_USER_PATH, STO_RD, &f) != 0) return -1;
    int rc = sto_read(f, s_batch, sizeof s_batch, &n);
    (void)sto_close(f);
    if (rc != 0) return -1;
    return trk_user_load(s_batch, n);
}
```
(`#define TRACKS_USER_PATH "/tracks/user.bin"` — paths are backend-relative, hal/storage.h:15, and `storage_internal.c:95` already creates `/tracks` at mount; `BATCH_CAP` is 3840 B, `sto_close()` exists. Measure `trk_user_save`'s worst-case size (TRK user capacity × `sizeof(trk_venue_t)` + header) and assert it is ≤ `BATCH_CAP` with a `_Static_assert` if the sizes are compile-time, else a host test.) Use the real names of the logger's flush helper, assert code, and `sto_close`.

- [ ] **Step 5: Pipeline** — statics `static bool s_create_active; static uint8_t s_create_next_gate; static gps_fix_t s_last_fix;` (`s_last_fix` copied in `on_fix()` when `valid`). A ui post helper mirroring `ui_post_lap_reset()`: `static void ui_post_create(uint8_t phase, uint16_t arg)`. `handle_cmd()`:
```c
    case CMD_CREATE_BEGIN:
        lap_create_begin(&s_lap);
        s_create_active = true; s_create_next_gate = 0;
        ui_post_create(EV_CREATE_BEGUN, 0);
        break;
    case CMD_CREATE_CANCEL:
        if (s_create_active) { lap_create_cancel(&s_lap); s_create_active = false; ui_post_create(EV_CREATE_CANCELLED, 0); }
        break;
    case CMD_MARK_GATE: {
        if (!s_create_active) break;
        uint8_t idx = s_create_next_gate;
        if (lap_mark_gate(&s_lap, idx, &s_last_fix, NULL) == 0) {
            s_create_next_gate = (uint8_t)(idx + 1u);
            ui_post_create(EV_CREATE_GATE_SET, idx);
        } else {
            ui_post_create(EV_CREATE_FAILED, 0);
        }
        break;
    }
```
(the existing `default:` comment "MARK_GATE / CALIB_ORIENT: later sessions" loses MARK_GATE). In `engine_cb()`, on `EV_LAP_... EV_VENUE_FOUND` while `s_create_active`: `s_create_active = false;` and post `LOGGER_SAVE_TRACKS` fire-and-forget (`requester = NULL`, `xQueueSend(g_log_req_q, …)` — use the queue name lt_ipc.h declares) with a warning on a full queue. Boot: right after `trk_init();` (~line 773): `{ log_request_t r = { .type = LOGGER_LOAD_TRACKS }; int rc = logger_request_sync(&r, 2000); if (rc != 0) ESP_LOGW(TAG, "user tracks: load rc=%d", rc); }` — boot order in `main/app_main.c`: `sup_start()` :204, `logger_start()` :223, `pipeline_start()` :234, `ui_start()` :240 — if `trk_init()` runs from a `pipeline_init()` called BEFORE :223, the logger task is not up yet: then issue the load from `pipeline_start()` (after the logger is running) before the first fix is processed, and say so. If `handle_cmd()`'s switch exceeds the 30-line compound cap, split the create cases into `static void handle_create_cmd(const command_t *cmd)`.

- [ ] **Step 6: ui** — `case MA_NEWTRACK:` → `ui_send_cmd(CMD_CREATE_BEGIN, 0, 0);` (the one-shot appears when `EV_CREATE_BEGUN` arrives). `handle_create(const event_t *e)`:
```c
static void handle_create(const event_t *e)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);
    bool on_nt = s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_NEWTRACK;
    switch (e->flags) {
    case EV_CREATE_BEGUN:
        s_model.create_step = 0;
        s_create_next_gate  = 0;
        if (!on_nt) {                       /* menu -> one-shot: whole-screen replacement (B-9) */
            s_model.screen = SCR_ONESHOT; s_model.oneshot = ONESHOT_NEWTRACK;
            s_oneshot_until_us = 0;         /* persistent until finished/cancelled */
            s_wants_full = true; s_screen_changed = true;
        }
        break;
    case EV_CREATE_GATE_SET:
        s_create_next_gate  = (uint8_t)(e->arg16 + 1u);
        s_model.create_step = s_create_next_gate;      /* 1 after S/F, k+1 after sector k */
        break;
    case EV_CREATE_FAILED:
        s_model.create_step = 0xFF;                    /* sub-line: "No fix / not moving" until the next event */
        break;
    case EV_CREATE_CANCELLED:
        if (on_nt) ui_exit_menu();                     /* -> riding, full */
        break;
    default: break;
    }
    s_dirty = true;
}
```
`case EV_CREATE: handle_create(e); break;` in `handle_event()`. Buttons on `ONESHOT_NEWTRACK`: `btn_short()` — a short MODE sends `ui_send_cmd(CMD_MARK_GATE, s_create_next_gate, 0)` (UP/DOWN ignored); `btn_long()` — long MODE sends `CMD_CREATE_CANCEL` (the ui leaves on `EV_CREATE_CANCELLED`, not immediately, so the engine state and screen stay in step); `btn_vlong()` ignores it. The `EV_VENUE_FOUND` path already replaces the one-shot with the VENUE one-shot via `show_venue_oneshot()` — but that function returns early when `screen != SCR_RIDING`: add `|| (s_model.oneshot == ONESHOT_NEWTRACK)` to its entry condition so creation's finish is allowed to replace the NEW TRACK screen (and only that one-shot). Keep the stale-OTA guard untouched. `create_step` after a FAILED: the next GATE_SET/BEGUN overwrites it.

- [ ] **Step 7: Gates, bench text, spec, commit** — `ctest` green (new goldens, trk round trip, lap refusal case), lint 0, gcc-16 0, host 0 warnings, clean `moto_sim` + `moto_neo6m` + ws29v2 builds 0 warnings + the selftest app build; DRAM delta reported (expect ≈ −60 B for `s_last_fix` + statics). Spec §10.9: add the app-side paragraph (commands, event, one-shot sub-lines, persistence in `/tracks/user.bin` via the logger, boot load); §20.6 the sub-lines; §20.7 "New track". `docs/bench/next-bench-day.md` item 5 → the New track procedure: menu New track → NEW TRACK; `dbg sim laps 2`; press MODE while the sim moves (sub-line → "S/F set. MODE: sector 1"), MODE once more (sector 1), wait for the sim to cross S/F again → VENUE one-shot `Track_2026…`, `lt list`/`trk` shows the venue; reset → the venue is still there. Commit: `feat(track): New track end to end — CREATE mode commands, NEW TRACK one-shot with step sub-lines, user tracks persisted in /tracks/user.bin via the logger (#97)` + trailer.
