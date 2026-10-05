# Plan 7c — Display Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every riding-screen field show real data (best sectors, theoretical lap, session stats, named DRAG gates, units, boot self-test lines, a live lap clock) and make a single-cell change cost a single-cell refresh.

**Architecture:** Two small seqlock records published by the pipeline task (best sectors + theoretical lap; the drag run + per-gate bests) are read by the ui task on events, mirroring the existing `pipeline_laps_snapshot` pattern. Pure, host-tested helpers do the folding, naming, unit conversion and frame diffing; the ui keeps a copy of the last accepted frame and refreshes only the changed rectangle. A four-slot boot-status table in the supervisor feeds the BOOT screen.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2, Unity host tests + PBM goldens (both canvases).

**Spec:** `docs/superpowers/specs/2026-09-29-plan-7c-display-closure-design.md` (binding). Also `2026-09-27-plan-7b-glanceable-ui-design.md` for the page layouts it amends.

## Global Constraints

- Power of 10 on all firmware and `core` code: every function has >= 2 `CORE_ASSERT_*`/`LT_ASSERT_*` checks (never abort); every loop bounded by a constant or a table field; no recursion; no heap; no goto; <= 60 code lines per function; `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0.
- Zero warnings: host suite (`-Wall -Wextra -Werror -Wshadow -Wconversion`; gcc-16 sweep of `test/build/compile_commands.json` clean) and clean ccache-disabled builds of `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 ./build.sh moto_sim build`.
- Single writer: only the pipeline task writes `s_laps_seq`-protected data; readers use the bounded retry loop (`PIPE_LAPS_SNAP_RETRY_MAX`). The ui task never touches `s_lap`/`s_drag`.
- No layout literals in `screens_moto.c` (constants in `canvas.h`); nothing drawn past `CANVAS_VISIBLE_W`; every screens test asserts `fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W`; goldens for both canvases (`test_screens`, `test_screens_213`) produced by dump → PNG → eyeball → promote, never hand-edited.
- `FONT_HUGE`: digits `: . - +`; `FONT_MED`: + A–Z; anything else (`/`, lowercase, `@`, `(`) in `FONT_SMALL`.
- Speeds convert to the display unit at render time only; times and distances never convert.
- DRAM: the previous-frame buffer (3904 B on the 2.13", 4736 B on the 2.9") plus <= 160 B of new statics; report `.bss` before/after in every task that adds a static.
- Never flash from a subagent; the controller runs the bench gate `p07c-d1` with the user.
- Commit messages end with the session trailers used on this branch (`git log -1`).

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `components/core/ui/stats_fold.c`, `include/core/ui/stats_fold.h` | `session_max_t`, `session_max_fold()` | 1 |
| `components/core/ui/units.c`, `include/core/ui/units.h` | `speed_display()` | 1 |
| `components/core/ui/render.c`, `include/core/ui/render.h` | `fb_diff_rect()` | 1 |
| `test/test_stats_fold.c`, `test/test_units.c`, `test/test_fb_diff.c` | host tests for Task 1 | 1 |
| `components/core/dragengine/drag_cfg.c`, `include/core/drag.h` | `drag_cfg_from_user()`, `drag_gate_label()` | 2 |
| `test/test_drag_cfg.c` | host tests for Task 2 | 2 |
| `components/app/pipeline/pipeline.c`, `include/app/pipeline.h` | drag cfg from user config; `pipeline_best_snapshot()`, `pipeline_drag_snapshot()` | 2, 3 |
| `components/core/ui/include/core/ui/model.h` | model fields | 3, 4, 6 |
| `components/core/ui/screens_moto.c`, `include/core/ui/canvas.h` | `have_best_sector` gate, unit suffixes, `CUR` cell | 3, 4, 6 |
| `components/app/ui/ui.c` | consumers: stats fold, drag rows, units, clock, diff-driven refresh, boot lines | 3, 4, 5, 6, 7, 8 |
| `components/app/include/app/lt_sup.h`, `components/app/supervisor/sup.c`, `main/app_main.c` | boot status table + reporters | 8 |
| `test/test_screens.c` + goldens (both sets) | riding-screen cases | 3, 4, 6, 8 |
| docs (roadmap, 7b spec amendments, 7c spec notes) | | 9 |

---

### Task 1: Pure helpers — session-max fold, speed conversion, frame diff

**Files:**
- Create: `components/core/ui/stats_fold.c`, `components/core/ui/include/core/ui/stats_fold.h`, `components/core/ui/units.c`, `components/core/ui/include/core/ui/units.h`, `test/test_stats_fold.c`, `test/test_units.c`, `test/test_fb_diff.c`
- Modify: `components/core/ui/render.c` (append `fb_diff_rect`), `components/core/ui/include/core/ui/render.h` (prototype)
- Note: `test/CMakeLists.txt` globs `test_*.c` and links `core` for each — no CMake change; `components/core/CMakeLists.txt` globs `*.c` — verify with `grep -n GLOB components/core/CMakeLists.txt` and add the two new sources if it lists files explicitly.

**Interfaces (produced):**

```c
/* core/ui/stats_fold.h */
typedef struct {
    uint16_t max_speed_cms;
    int16_t  max_lean_l_cdeg, max_lean_r_cdeg;
    int16_t  max_glat_e3, max_gacc_e3, max_gbrake_e3;
} session_max_t;
void session_max_fold(session_max_t *acc, const lap_stats_t *lap);   /* element-wise max into *acc */

/* core/ui/units.h */
uint16_t speed_display(uint16_t cms, uint8_t units);   /* cm/s -> whole km/h (units 0) or mph (units 1), rounded; saturates at 65535 */

/* core/ui/render.h */
bool fb_diff_rect(const fb_t *prev, const fb_t *cur, fb_rect_t *out);   /* bbox of differing bytes, x on byte boundaries; false + out->valid=false when identical */
```

- [ ] **Step 1: Failing tests.** `test/test_stats_fold.c`:

```c
#include "unity.h"
#include "core/ui/stats_fold.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}
static void test_fold_from_zero_takes_lap(void)
{
    session_max_t acc; memset(&acc, 0, sizeof acc);
    lap_stats_t lap = { .max_speed_cms = 3306, .min_speed_cms = 1991, .max_lean_l_cdeg = 4800, .max_lean_r_cdeg = 5200,
                        .max_glat_e3 = 1320, .max_gacc_e3 = 610, .max_gbrake_e3 = 1050 };
    session_max_fold(&acc, &lap);
    TEST_ASSERT_EQUAL_UINT16(3306, acc.max_speed_cms);
    TEST_ASSERT_EQUAL_INT16(4800, acc.max_lean_l_cdeg);
    TEST_ASSERT_EQUAL_INT16(5200, acc.max_lean_r_cdeg);
    TEST_ASSERT_EQUAL_INT16(1320, acc.max_glat_e3);
    TEST_ASSERT_EQUAL_INT16(610, acc.max_gacc_e3);
    TEST_ASSERT_EQUAL_INT16(1050, acc.max_gbrake_e3);
}
static void test_fold_keeps_larger(void)
{
    session_max_t acc = { .max_speed_cms = 4000, .max_lean_l_cdeg = 5000, .max_lean_r_cdeg = 100, .max_glat_e3 = 1500, .max_gacc_e3 = 700, .max_gbrake_e3 = 900 };
    lap_stats_t lap = { .max_speed_cms = 3306, .max_lean_l_cdeg = 4800, .max_lean_r_cdeg = 5200, .max_glat_e3 = 1320, .max_gacc_e3 = 610, .max_gbrake_e3 = 1050 };
    session_max_fold(&acc, &lap);
    TEST_ASSERT_EQUAL_UINT16(4000, acc.max_speed_cms);
    TEST_ASSERT_EQUAL_INT16(5000, acc.max_lean_l_cdeg);
    TEST_ASSERT_EQUAL_INT16(5200, acc.max_lean_r_cdeg);
    TEST_ASSERT_EQUAL_INT16(1500, acc.max_glat_e3);
    TEST_ASSERT_EQUAL_INT16(700, acc.max_gacc_e3);
    TEST_ASSERT_EQUAL_INT16(1050, acc.max_gbrake_e3);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_fold_from_zero_takes_lap); RUN_TEST(test_fold_keeps_larger); return UNITY_END(); }
```

`test/test_units.c`:

```c
#include "unity.h"
#include "core/ui/units.h"
void setUp(void) {} void tearDown(void) {}
static void test_zero(void)        { TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 0)); TEST_ASSERT_EQUAL_UINT16(0, speed_display(0, 1)); }
static void test_kmh(void)         { TEST_ASSERT_EQUAL_UINT16(130, speed_display(3611, 0)); /* 36.11 m/s = 129.996 km/h -> 130 */ }
static void test_mph(void)         { TEST_ASSERT_EQUAL_UINT16(81, speed_display(3611, 1));  /* 80.78 mph -> 81 */ }
static void test_round_half_up(void){ TEST_ASSERT_EQUAL_UINT16(1, speed_display(14, 0));    /* 0.504 km/h -> 1 */ TEST_ASSERT_EQUAL_UINT16(0, speed_display(13, 0)); }
static void test_saturates(void)   { TEST_ASSERT_EQUAL_UINT16(2359, speed_display(65535, 0)); TEST_ASSERT_EQUAL_UINT16(1466, speed_display(65535, 1)); }
int main(void) { UNITY_BEGIN(); RUN_TEST(test_zero); RUN_TEST(test_kmh); RUN_TEST(test_mph); RUN_TEST(test_round_half_up); RUN_TEST(test_saturates); return UNITY_END(); }
```

`test/test_fb_diff.c` (uses `CANVAS_W`/`CANVAS_H` from `core/ui/canvas.h`; the host default canvas is 296×128):

```c
#include "unity.h"
#include "core/ui/canvas.h"
#include "core/ui/render.h"
#include <string.h>
static uint8_t a_bits[(CANVAS_W / 8) * CANVAS_H], b_bits[(CANVAS_W / 8) * CANVAS_H];
static fb_t a, b;
void setUp(void) { fb_init(&a, a_bits, CANVAS_W, CANVAS_H); fb_init(&b, b_bits, CANVAS_W, CANVAS_H); fb_clear(&a, 0); fb_clear(&b, 0); }
void tearDown(void) {}
static void test_identical_is_not_dirty(void)
{
    fb_rect_t r; r.valid = true;
    TEST_ASSERT_FALSE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_FALSE(r.valid);
}
static void test_single_pixel_gives_byte_cell(void)
{
    fb_rect(&b, 13, 7, 1, 1, 1, true);   /* one ink pixel at (13,7) */
    fb_rect_t r;
    TEST_ASSERT_TRUE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(8, r.x0); TEST_ASSERT_EQUAL_UINT16(16, r.x1);
    TEST_ASSERT_EQUAL_UINT16(7, r.y0); TEST_ASSERT_EQUAL_UINT16(8, r.y1);
}
static void test_two_corners_span_frame(void)
{
    fb_rect(&b, 0, 0, 1, 1, 1, true);
    fb_rect(&b, CANVAS_W - 1, CANVAS_H - 1, 1, 1, 1, true);
    fb_rect_t r;
    TEST_ASSERT_TRUE(fb_diff_rect(&a, &b, &r));
    TEST_ASSERT_EQUAL_UINT16(0, r.x0); TEST_ASSERT_EQUAL_UINT16(CANVAS_W, r.x1);
    TEST_ASSERT_EQUAL_UINT16(0, r.y0); TEST_ASSERT_EQUAL_UINT16(CANVAS_H, r.y1);
}
static void test_mismatched_geometry_rejected(void)
{
    static uint8_t c_bits[(CANVAS_W / 8) * CANVAS_H]; fb_t c; fb_init(&c, c_bits, CANVAS_W, CANVAS_H - 8);
    fb_rect_t r; r.valid = true;
    TEST_ASSERT_FALSE(fb_diff_rect(&a, &c, &r));   /* assert path: different h -> false, r.valid false */
    TEST_ASSERT_FALSE(r.valid);
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_identical_is_not_dirty); RUN_TEST(test_single_pixel_gives_byte_cell); RUN_TEST(test_two_corners_span_frame); RUN_TEST(test_mismatched_geometry_rejected); return UNITY_END(); }
```

- [ ] **Step 2: Run to fail.** `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build` → the three new targets fail to compile (missing headers/functions).

- [ ] **Step 3: Implement.**

```c
/* components/core/ui/stats_fold.c */
#include "core/ui/stats_fold.h"
#include "core/core.h"
#define STATS_ASSERT_CODE 0x0AF0
static int16_t max_i16(int16_t a, int16_t b) { return a > b ? a : b; }
void session_max_fold(session_max_t *acc, const lap_stats_t *lap)
{
    CORE_ASSERT_VOID(acc != NULL, STATS_ASSERT_CODE);
    CORE_ASSERT_VOID(lap != NULL, STATS_ASSERT_CODE);
    if (lap->max_speed_cms > acc->max_speed_cms) acc->max_speed_cms = lap->max_speed_cms;
    acc->max_lean_l_cdeg = max_i16(acc->max_lean_l_cdeg, lap->max_lean_l_cdeg);
    acc->max_lean_r_cdeg = max_i16(acc->max_lean_r_cdeg, lap->max_lean_r_cdeg);
    acc->max_glat_e3     = max_i16(acc->max_glat_e3, lap->max_glat_e3);
    acc->max_gacc_e3     = max_i16(acc->max_gacc_e3, lap->max_gacc_e3);
    acc->max_gbrake_e3   = max_i16(acc->max_gbrake_e3, lap->max_gbrake_e3);
}
```

(`max_i16` is under the lint's 20-line assert exemption; keep it that short.) The header includes `core/types.h` for `lap_stats_t`.

```c
/* components/core/ui/units.c */
#include "core/ui/units.h"
#include "core/core.h"
#define UNITS_ASSERT_CODE 0x0AF1
uint16_t speed_display(uint16_t cms, uint8_t units)
{
    CORE_ASSERT_RET(units <= 1u, UNITS_ASSERT_CODE, 0u);
    /* km/h = cms * 36 / 1000; mph = cms * 36 / 1000 / 1.609344 = cms * 22369 / 1000000 (both exact to 1e-5) */
    uint32_t num = (units == 0u) ? (uint32_t)cms * 36u : (uint32_t)cms * 22369u;
    uint32_t den = (units == 0u) ? 1000u : 1000000u;
    uint32_t v   = (num + den / 2u) / den;
    CORE_ASSERT_RET(v <= 65535u, UNITS_ASSERT_CODE, 65535u);
    return (uint16_t)v;
}
```

```c
/* components/core/ui/render.c — append */
bool fb_diff_rect(const fb_t *prev, const fb_t *cur, fb_rect_t *out)
{
    CORE_ASSERT_RET(prev != NULL && cur != NULL && out != NULL, UI_ASSERT_CODE, false);
    out->valid = false;
    CORE_ASSERT_RET(prev->w == cur->w && prev->h == cur->h && prev->stride == cur->stride, UI_ASSERT_CODE, false);
    int bx0 = (int)cur->stride, bx1 = -1, y0 = (int)cur->h, y1 = -1;
    for (int y = 0; y < (int)cur->h; y++) {
        const uint8_t *pa = prev->bits + (size_t)y * prev->stride;
        const uint8_t *pb = cur->bits + (size_t)y * cur->stride;
        for (int i = 0; i < (int)cur->stride; i++) {
            if (pa[i] == pb[i]) continue;
            if (i < bx0) bx0 = i;
            if (i > bx1) bx1 = i;
            if (y < y0) y0 = y;
            y1 = y;
        }
    }
    if (bx1 < 0) return false;
    out->x0 = (uint16_t)(bx0 * 8); out->x1 = (uint16_t)((bx1 + 1) * 8);
    out->y0 = (uint16_t)y0;        out->y1 = (uint16_t)(y1 + 1);
    out->valid = true;
    return true;
}
```

(`fb_rect_t` is the existing `{x0,y0,x1,y1,valid}` in `render.h`; prototype under `fb_bar`. The mismatched-geometry test exercises the second assert's return path — `CORE_ASSERT_RET` logs and returns, never aborts.)

- [ ] **Step 4: Run to pass.** Rebuild; `./test/build/test_stats_fold` 2/2, `./test/build/test_units` 5/5, `./test/build/test_fb_diff` 4/4; full `ctest` green (41 executables).

- [ ] **Step 5: Gate + commit.** Lint 0; host build 0 warnings; gcc-16 sweep clean; clean `moto_sim` build 0 warnings (the new sources compile into the firmware `core` component even before they are used).

```
feat(core/ui): session_max_fold, speed_display, fb_diff_rect — pure helpers for the display closure (Plan 7c T1)
```

---

### Task 2: Drag config builder + gate labels; the engine runs on the user config

**Files:**
- Create: `components/core/dragengine/drag_cfg.c`, `test/test_drag_cfg.c`
- Modify: `components/core/include/core/drag.h` (two prototypes), `components/app/pipeline/pipeline.c:544` (`drag_init(&s_drag, NULL)` → user config)

**Interfaces (produced):**

```c
/* core/drag.h */
void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out);           /* defaults + the bench list/units from cfg */
int  drag_gate_label(const drag_gate_def_t *g, char *buf, size_t cap); /* §6.6 name; returns strlen, -1 on bad input/cap */
```

Rules for `drag_cfg_from_user`: start from `drag_cfg_defaults()`; `out->units = cfg->units` (0 km/h, 1 mph); the bench list is `cfg->drag.benches_kmh[0..n_kmh)` for km/h or `benches_mph[0..n_mph)` for mph, copied into `out->benches_kmh[]` (the field name stays; values are in the configured unit) with `n_benches = min(n, 4)`; the SPEED_FROM0 gates (ids 1–4 in the default table) take the first four bench values as their `a` (unused slots keep the defaults); `rollout = cfg->drag.rollout`. Ids never change. When the bench list is empty, the defaults stand.

Rules for `drag_gate_label` (cap >= 8 required): `DRAG_SPEED_FROM0` → `0-<a>`; `DRAG_SPEED_RANGE` → `<a>-<b>`; `DRAG_BRAKE` → `<a>-0`; `DRAG_DIST` → by `a` in cm: 1829 `60ft`, 10058 `330ft`, 20117 `1/8`, 30480 `1000ft`, 40234 `1/4`, anything else `<a/100>m` (integer metres); unknown kind → -1. Output always NUL-terminated within cap.

- [ ] **Step 1: Failing tests.** `test/test_drag_cfg.c`:

```c
#include "unity.h"
#include "core/drag.h"
#include "core/cfg.h"
#include <string.h>
void setUp(void) {} void tearDown(void) {}
static void test_defaults_when_no_benches(void)
{
    cfg_t c; cfg_defaults(&c); c.drag.n_kmh = 0;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    TEST_ASSERT_EQUAL_UINT8(11, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8(0, d.units);
    TEST_ASSERT_EQUAL_UINT16(100, d.gates[1].a);   /* id 2 default 0-100 */
}
static void test_mph_benches_replace_speed_gates(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH;
    c.drag.n_mph = 2; c.drag.benches_mph[0] = 60; c.drag.benches_mph[1] = 100;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    TEST_ASSERT_EQUAL_UINT8(1, d.units);
    TEST_ASSERT_EQUAL_UINT8(2, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(60, d.gates[0].a);  TEST_ASSERT_EQUAL_UINT8(1, d.gates[0].id);
    TEST_ASSERT_EQUAL_UINT16(100, d.gates[1].a); TEST_ASSERT_EQUAL_UINT8(2, d.gates[1].id);
    TEST_ASSERT_EQUAL_UINT16(200, d.gates[2].a); /* default kept for the unused slot */
}
static void check_label(const drag_gate_def_t *g, const char *want)
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT((int)strlen(want), drag_gate_label(g, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(want, buf);
}
static void test_labels_for_every_default_gate(void)
{
    drag_cfg_t d; drag_cfg_defaults(&d);
    const char *want[11] = { "0-60", "0-100", "0-200", "0-300", "100-200", "60ft", "330ft", "1/8", "1000ft", "1/4", "100-0" };
    for (int i = 0; i < 11; i++) check_label(&d.gates[i], want[i]);
}
static void test_custom_distance_and_bad_cap(void)
{
    drag_gate_def_t g = { 12, DRAG_DIST, 12000, 0 };
    check_label(&g, "120m");
    char small[4];
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&g, small, sizeof small));
    drag_gate_def_t bad = { 13, 9, 1, 0 }; char buf[8];
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&bad, buf, sizeof buf));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_defaults_when_no_benches); RUN_TEST(test_mph_benches_replace_speed_gates); RUN_TEST(test_labels_for_every_default_gate); RUN_TEST(test_custom_distance_and_bad_cap); return UNITY_END(); }
```

- [ ] **Step 2: Run to fail** (undefined symbols).

- [ ] **Step 3: Implement** `components/core/dragengine/drag_cfg.c`:

```c
#include "core/drag.h"
#include "core/cfg.h"
#include "core/core.h"
#include <stdio.h>
#include <string.h>
#define DRAGCFG_ASSERT_CODE 0x0AF2
void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out)
{
    CORE_ASSERT_VOID(cfg != NULL, DRAGCFG_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, DRAGCFG_ASSERT_CODE);
    drag_cfg_defaults(out);
    bool            mph = cfg->units == CFG_UNITS_MPH;
    const uint16_t *src = mph ? cfg->drag.benches_mph : cfg->drag.benches_kmh;
    uint8_t         n   = mph ? cfg->drag.n_mph : cfg->drag.n_kmh;
    if (n > 4u) n = 4u;
    out->units   = mph ? (uint8_t)DRAG_UNITS_MPH : (uint8_t)DRAG_UNITS_KMH;
    out->rollout = cfg->drag.rollout;
    if (n == 0u) return;                          /* no user list: defaults stand */
    out->n_benches = n;
    for (uint8_t i = 0; i < n; i++) {
        out->benches_kmh[i] = src[i];
        if (i < out->n_gates && out->gates[i].kind == DRAG_SPEED_FROM0) out->gates[i].a = src[i];
    }
    CORE_ASSERT_VOID(out->n_gates <= DRAG_MAX_GATES, DRAGCFG_ASSERT_CODE);   /* untouched by the fill above */
}
static int put_num_pair(char *buf, size_t cap, unsigned a, const char *sep, unsigned b)   /* "<a><sep><b>" */
{
    int n = snprintf(buf, cap, "%u%s%u", a, sep, b);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
int drag_gate_label(const drag_gate_def_t *g, char *buf, size_t cap)
{
    CORE_ASSERT_RET(g != NULL && buf != NULL, DRAGCFG_ASSERT_CODE, -1);
    CORE_ASSERT_RET(cap >= 8u, DRAGCFG_ASSERT_CODE, -1);
    switch (g->kind) {
    case DRAG_SPEED_FROM0: return put_num_pair(buf, cap, 0u, "-", g->a);
    case DRAG_SPEED_RANGE: return put_num_pair(buf, cap, g->a, "-", g->b);
    case DRAG_BRAKE:       return put_num_pair(buf, cap, g->a, "-", 0u);
    case DRAG_DIST: {
        const char *name = g->a == 1829u ? "60ft" : g->a == 10058u ? "330ft" : g->a == 20117u ? "1/8"
                         : g->a == 30480u ? "1000ft" : g->a == 40234u ? "1/4" : NULL;
        if (name != NULL) { int n = snprintf(buf, cap, "%s", name); return (n < 0 || (size_t)n >= cap) ? -1 : n; }
        int n = snprintf(buf, cap, "%um", (unsigned)(g->a / 100u));
        return (n < 0 || (size_t)n >= cap) ? -1 : n;
    }
    default: buf[0] = '\0'; return -1;
    }
}
```

(`snprintf` is already used by core files with the same lint; if the lint flags `%u` with `unsigned` casts under `-Wformat`, cast as shown. The `cap >= 8` assert makes the `small[4]` test hit the assert's return path.)

Add the two prototypes to `core/drag.h` (below `drag_cfg_defaults`), and in `pipeline.c` replace `drag_init(&s_drag, NULL);` with:

```c
    {
        cfg_t      uc;   /* pipeline task stack: cfg_t is a few hundred bytes; pipeline has ~4.8 KB free */
        drag_cfg_t dc;
        cfg_defaults(&uc);
        (void)lt_cfg_load(&uc);              /* NVS copy; defaults on a missing/invalid blob */
        drag_cfg_from_user(&uc, &dc);
        drag_init(&s_drag, &dc);
    }
```

(`lt_cfg_load`/`cfg_defaults` are what `ui.c` and `cmd.c` already use — include `app/lt_nvs.h` / `core/cfg.h` as they do. If `sizeof(cfg_t)` exceeds 512 B, make `uc` a file-static instead and note it in the report.)

- [ ] **Step 4: Run to pass.** `./test/build/test_drag_cfg` 4/4; full ctest green.

- [ ] **Step 5: Gate + commit.** Lint 0; host 0 warnings; clean `moto_sim` + `moto_neo6m` 0 warnings; report the pipeline task's stack headroom is unchanged in principle (`uc` + `dc` ≈ 600 B transient).

```
feat(drag): user-config gate table (benches/units/rollout) and §6.6 gate labels; the engine runs on the user config (Plan 7c T2)
```

---

### Task 3: Pipeline snapshots + LAP pages filled (#79)

**Files:**
- Modify: `components/app/include/app/pipeline.h`, `components/app/pipeline/pipeline.c` (`on_lap_complete`, `on_drag_done`/drag event path, two readers), `components/core/ui/include/core/ui/model.h`, `components/core/ui/screens_moto.c` (`render_lap_page1` value row), `components/app/ui/ui.c` (`handle_lap_result`), `test/test_screens.c` + goldens `lap_p1_filled`, `lap_p2_stats_filled` (both sets)

**Interfaces (produced):**

```c
/* app/pipeline.h */
typedef struct {
    uint32_t best_sector_ms[LAP_MAX_SECTORS + 1];
    bool     have_best_sector[LAP_MAX_SECTORS + 1];
    uint8_t  n_sectors;
    uint32_t theo_ms;                       /* 0 = not yet */
} pipe_best_t;
int pipeline_best_snapshot(pipe_best_t *out);   /* 0 on success */
typedef struct {
    drag_result_t current;                  /* zeroed n_gates when no run yet */
    uint32_t      best_time_ms[DRAG_MAX_GATES];   /* index = gate id - 1; BRAKE: dist_cm */
    bool          have_best[DRAG_MAX_GATES];
} pipe_drag_t;
int pipeline_drag_snapshot(pipe_drag_t *out);   /* 0 on success */
```

Model additions (`model.h`): `bool have_best_sector[LAP_MAX_SECTORS + 1];` next to `best_sector_ms[]`; `uint16_t max_speed_cms;` replaces `max_speed_kmh` (page 2 keeps rendering km/h in this task via `speed_display(max_speed_cms, 0)`; Task 4 adds the unit).

- [ ] **Step 1: Failing tests** in `test/test_screens.c` (both canvases; standard assertion trio):

```c
static void test_lap_p1_filled(void)
{
    screen_model_t m; lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 3;
    m.best_sector_ms[0] = 32100; m.best_sector_ms[1] = 41000; m.best_sector_ms[2] = 39240;
    m.have_best_sector[0] = true; m.have_best_sector[1] = true; m.have_best_sector[2] = false;   /* S3 not yet */
    m.have_theo = true; m.theo_best_ms = 111200;
    m.have_last_sector_delta[0] = true; m.last_sector_delta_ms[0] = -120;
    m.have_last_sector_delta[1] = true; m.last_sector_delta_ms[1] = 400;
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("lap_p1_filled.pbm")   /* S1 32.10 / S2 41.00 / S3 --.-- ; deltas -0.12 +0.40 ---- ; THEO 1:51.20 */
}
static void test_lap_p2_stats_filled(void)
{
    screen_model_t m; lap_model_base(&m); m.page = 2;
    m.max_speed_cms = 5944;   /* 214 km/h */
    m.lean_l_deg = 52; m.lean_r_deg = 55; m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12; m.laps_valid = 10;
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("lap_p2_stats_filled.pbm")
}
```

Rework the existing `test_lap_p2_stats`/`test_lap_p2_stats_many_laps`/`test_lap_p2_stats_huge_laps` models from `max_speed_kmh = 214` to `max_speed_cms = 5944` (their goldens must stay byte-identical: 5944 cm/s → 214 km/h). `test_lap_p1_sectors` sets `have_best_sector[0..2] = true` (golden unchanged).

- [ ] **Step 2: Run to fail** (compile: missing fields; then golden mismatch for the two new cases).

- [ ] **Step 3: Implement.**
  - `screens_moto.c` `render_lap_page1`: the value cell shows `fmt_secs_ms(best_sector_ms[i])` only when `i < n && m->have_best_sector[i]`, else `--.--` (was `i < n` alone). `render_lap_page2`: `put_uint(buf, speed_display(m->max_speed_cms, 0))` (include `core/ui/units.h`).
  - `pipeline.c`: file-static `pipe_best_t s_best; pipe_drag_t s_dragsnap;` written inside the SAME seqlock section as `s_laps[]` in `on_lap_complete()` (copy `s_lap.best_sector_ms`/`have_best_sector`/`best_sector_count`, `theo_ms = lap_theoretical_best_ms(&s_lap)`), and for the drag record in the engine-callback path (`case EV_DRAG_GATE:`/`EV_DRAG_LAUNCH`/`EV_DRAG_DONE` — add a helper `publish_drag_snapshot()` that copies `*drag_current(&s_drag)` (or zeroes) and fills `best_time_ms[id-1]`/`have_best[id-1]` from `drag_best(&s_drag, id)` for every id in `s_drag.cfg.gates[]`, all under one seqlock write). Readers `pipeline_best_snapshot()` / `pipeline_drag_snapshot()` copy the same way `pipeline_lap_at` does (bounded retry, `stable` assert). Clear `s_best` where the venue/layout changes (the existing `lap_set_venue` call site).
  - `ui.c` `handle_lap_result()`: after the BEST/PREV logic, `pipe_best_t pb; if (pipeline_best_snapshot(&pb) == 0) { memcpy best/have arrays; s_model.best_n_sectors = pb.n_sectors; s_model.theo_best_ms = pb.theo_ms; s_model.have_theo = pb.theo_ms != 0; }`; then `lap_result_t lr; if (pipeline_lap_at(pipeline_lap_count() - 1, &lr) == 0) { session_max_fold(&s_session_max, &lr.stats); s_model.max_speed_cms = s_session_max.max_speed_cms; s_model.lean_l_deg = (uint8_t)(s_session_max.max_lean_l_cdeg / 100); ... lat_g_e2 = (uint16_t)((max_glat_e3 + 5) / 10); ... }`. Split into `fold_lap_stats()` and `copy_best_snapshot()` helpers (≥ 2 asserts each, ≤ 60 lines). `static session_max_t s_session_max;` (14 B).

- [ ] **Step 4: Goldens** (four dumps) → eyeball → promote → both executables green (existing page-2 goldens byte-identical).

- [ ] **Step 5: Gate + commit.** Lint 0; host 0 warnings; clean `moto_sim` + `moto_neo6m` 0 warnings; `.bss` delta (≈ +14 B ui, + `pipe_best_t` ≈ 50 B + `pipe_drag_t` ≈ 300 B in the pipeline — report exactly).

```
feat(display): pipeline best-sector/theo and drag snapshots; LAP page 1 values and page 2 session stats filled from real laps (Plan 7c T3)
```

---

### Task 4: Units on screen (#83)

**Files:**
- Modify: `model.h` (`uint8_t units;`), `canvas.h` (`DCARD_UNIT_X_AFTER_SPEED` = `DCARD_LABEL_X + FONT_SMALL.w + DCARD_AT_GAP` is runtime; define `DCARD_SPEED_UNIT_GAP 4` shared), `screens_moto.c` (page-2 label, trap row suffix), `ui.c` (`s_model.units` at boot and in `menu_do_units`), `test/test_screens.c` + goldens `lap_p2_stats_mph`, `drag_p0_trap_mph` (both sets)

- [ ] **Step 1: Failing tests.** `test_lap_p2_stats_mph`: as `test_lap_p2_stats_filled` with `m.units = 1` → `MAX SPD mph` label and `133`; `test_drag_p0_trap_mph`: as `test_drag_p0_gate_speed` with `m.units = 1` and gate 4's `trap_kmh` field holding cm/s? — no: keep `drag_row_t.trap_kmh` as the DISPLAY value filled by the ui (rename to `trap_speed` in this task, comment "already in the display unit"), so the renderer prints it as is and appends the unit. Test: `trap_speed = 107`, `m.units = 1` → `@107` + `mph`.

- [ ] **Step 2: Run to fail.**

- [ ] **Step 3: Implement.** `render_lap_page2`: label buffer `MAX SPD ` + (`m->units ? "mph" : "km/h"`), value `speed_display(m->max_speed_cms, m->units)`. `render_dcard_value`: after the `@` + digits, draw the unit in `FONT_SMALL` at `x_after_digits + DCARD_SPEED_UNIT_GAP`, `y = DCARD_SPEED_Y + DCARD_AT_DY`. `ui.c`: `s_model.units = s_cfg.units` at boot; `menu_do_units()` also sets `s_model.units` and `s_dirty = true` (the menu is showing, so the change appears on the next riding render). `handle_drag_gate` (Task 5 rewrites it; here only) fills `trap_speed = speed_display((uint16_t)e->arg32b, s_model.units)` for the trap gate.

- [ ] **Step 4: Goldens** (four dumps) → promote → green; the km/h goldens stay byte-identical.

- [ ] **Step 5: Gate + commit.**

```
feat(display): units on screen — MAX SPD and the DRAG trap speed in km/h or mph with a small unit suffix (Plan 7c T4)
```

---

### Task 5: DRAG rows from the snapshot with real names (#25)

**Files:**
- Modify: `components/app/ui/ui.c` (`handle_drag_gate` rewritten as `drag_rows_refill()`, drag event cases, page-change hook)

**Interfaces (consumed):** `drag_cfg_from_user`, `drag_gate_label` (Task 2), `pipeline_drag_snapshot` (Task 3), `speed_display` (Task 1).

- [ ] **Step 1: Implement** (target-only; no host test — the pure parts are tested in Tasks 1–2, the renderer in Task 4's goldens):
  - `static drag_cfg_t s_drag_cfg;` built at boot with `drag_cfg_from_user(&s_cfg, &s_drag_cfg)` and rebuilt in `menu_do_units()`.
  - `static const drag_gate_def_t *gate_by_id(uint8_t id)` — bounded scan of `s_drag_cfg.gates[0..n_gates)`; NULL when unknown.
  - `static void row_from_gate(drag_row_t *r, const drag_gate_def_t *g, const drag_gate_res_t *res, bool present)`: `drag_gate_label(g, r->label, sizeof r->label)` (fallback `G<id>` when -1), `r->present = present`, `r->is_distance = g->kind == DRAG_BRAKE`, `r->dist_m = (uint16_t)(res->dist_cm / 100u)`, `r->t_ms = res->time_ms`, `r->has_trap = (g->kind == DRAG_DIST && g->a == 40234u && res->speed_cms > 0)`, `r->trap_speed = speed_display(res->speed_cms, s_model.units)`.
  - `static void drag_rows_refill(void)`: `pipe_drag_t d; if (pipeline_drag_snapshot(&d) != 0) return;` then by `s_model.page`: page 0 → rows for `d.current.gates[i]` with `hit`, in table order (hit gates only), `drag_n` = count; page 1 → all `d.current.n_gates` rows with `present = hit`; page 2 → one row per configured gate with `present = d.have_best[id-1]`, `t_ms`/`dist` from `best_time_ms[id-1]`. Bounded by `DRAG_MAX_GATES`.
  - Event cases: `EV_DRAG_ARMED` → `drag_armed = true; drag_rows_refill(); s_dirty = true;` `EV_DRAG_LAUNCH` → `drag_armed = false; s_dirty = true;` `EV_DRAG_GATE` / `EV_DRAG_DONE` → `drag_rows_refill(); s_dirty = true;`. In `btn_short`'s riding page change: when `s_model.mode == SCR_MODE_DRAG`, call `drag_rows_refill()` after changing `page`.
  - Remove the old append-only `handle_drag_gate`.

- [ ] **Step 2: Gate + commit.** Lint 0; clean `moto_sim` + `moto_neo6m` 0 warnings; `.bss` delta (≈ +100 B for `s_drag_cfg`).

```
feat(ui): DRAG rows rebuilt from the pipeline snapshot with §6.6 gate names, per page (Plan 7c T5)
```

---

### Task 6: Live clock (#80)

**Files:**
- Modify: `model.h` (`uint32_t cur_ms; bool cur_running;` replace `cur_ms_at_gate`), `screens_moto.c` (`fmt_time_s`, card footer CUR cell), `ui.c` (lap start stamp, tick, accounting), `test/test_screens.c` + golden `lap_p0_cur_clock` (both sets)

- [ ] **Step 1: Failing test.** `test_lap_p0_cur_clock`: `lap_model_base(&m); m.big_kind = BIG_SECTOR_DELTA; m.big_delta_ms = -320; m.big_sector_idx = 2; m.cur_running = true; m.cur_ms = 83400;` → footer left cell label `CUR`, value `1:23`; BEST unchanged. Golden `lap_p0_cur_clock.pbm`.

- [ ] **Step 2: Run to fail.**

- [ ] **Step 3: Implement.**
  - `screens_moto.c`: `static void fmt_time_s(char *buf, uint32_t ms)` → `m:ss` (`put_uint(m)`, `':'`, two-digit seconds; ≤ 5 glyphs asserted). `render_card_footer`: when `m->cur_running` the left cell draws label `CUR` and `fmt_time_s(m->cur_ms)` right-aligned at `CARD_LEFT_RIGHT_X`; else the LAST cell as today.
  - `ui.c`: `static int64_t s_lap_start_mono_us, s_last_clock_us; static bool s_clock_tick;`. In `handle_lap_complete()` (before the out-lap check): `s_lap_start_mono_us = e->mono_us;`. New `static void clock_tick(int64_t now)` called from `ui_loop_iter` before the render decision: if `s_cfg.display.live_clock && s_lap_start_mono_us != 0 && s_model.screen == SCR_RIDING && s_model.page == 0 && s_model.mode == SCR_MODE_LAP && now - s_last_clock_us >= 1000000` → `s_model.cur_ms = (uint32_t)((now - s_lap_start_mono_us) / 1000); s_model.cur_running = true; s_last_clock_us = now; if (!s_dirty) s_clock_tick = true; s_dirty = true;` else if the clock is off → `s_model.cur_running = false` (one-time dirty when it flips). In `do_refresh()` after a successful `DISP_PARTIAL`: if `s_clock_tick` → do NOT increment `s_partial_count` / `s_last_partial_us`; always clear `s_clock_tick` at the end of `render_and_refresh()`. `build_rf_in`: `wants_full` unchanged (a clock tick never sets it).
  - `menu_do_display()` (the live-clock toggle) sets `s_dirty = true` so the card re-renders with/without CUR.

- [ ] **Step 4: Golden** → promote → green.

- [ ] **Step 5: Gate + commit.** Lint 0; clean builds 0 warnings; `.bss` delta (+17 B).

```
feat(ui): live lap clock — CUR m:ss in the card footer once per second, ticks excluded from the full-refresh count (Plan 7c T6)
```

---

### Task 7: Dirty-region refresh (#84)

**Files:**
- Modify: `components/app/ui/ui.c` (`s_fb_prev_bits`, `s_fb_prev`, `render_and_refresh`, `partial_window_or_full`, `do_refresh`, boot copy)

**Interfaces (consumed):** `fb_diff_rect` (Task 1).

- [ ] **Step 1: Implement.**
  - `static uint8_t s_fb_prev_bits[FB_STRIDE * FB_H]; static fb_t s_fb_prev; static fb_rect_t s_diff;` `fb_init(&s_fb_prev, s_fb_prev_bits, CANVAS_W, CANVAS_H); fb_clear(&s_fb_prev, 0);` next to the existing `fb_init`.
  - `render_and_refresh()`: after `render_fb()`: `bool changed = fb_diff_rect(&s_fb_prev, &s_fb, &s_diff);` — if `!changed && !s_wants_full` → `log_refresh(false, DISP_PARTIAL, 0); s_clock_tick = false; return;` (no policy call, no bookkeeping). Else `in.dirty = true` as today.
  - `partial_window_or_full()`: use `s_diff` instead of `s_fb.dirty` (same clamp to `CANVAS_VISIBLE_W`/`CANVAS_H`, same `>=` guard, same fallback).
  - `do_refresh()`: after `rc == 0` (either mode) → `memcpy(s_fb_prev_bits, s_fb_bits, sizeof s_fb_prev_bits);`. On failure leave it.
  - Boot: after a successful `disp_init()` → the same `memcpy` (the panel holds the BOOT frame).
  - `dead_retry()` success path already sets `s_wants_full` → the next render diffs against a stale copy but forces a full anyway; after it succeeds the copy is refreshed.
  - The log line's `dirty` box now prints `s_diff` (same format).

- [ ] **Step 2: Gate + commit.** Lint 0; clean `moto_sim` + `moto_neo6m` + `PANEL=ws29v2` builds 0 warnings; `.bss` delta = +3904 B (213) — report the free-DRAM figure from the build; it must stay ≥ 4 KB on `moto_sim`.

```
feat(ui): dirty-region refresh — diff against the last accepted frame; unchanged renders cost no refresh, partials cover only the changed rectangle (Plan 7c T7)
```

---

### Task 8: Boot self-test lines (#81)

**Files:**
- Modify: `components/app/include/app/lt_sup.h`, `components/app/supervisor/sup.c` (table + accessors), `main/app_main.c` (`boot_storage`), `components/app/pipeline/pipeline.c` (after `gps_init`/`imu_init`), `components/app/ui/ui.c` (report display; format lines; `ONESHOT_BOOT_MS 3000`; one re-format at +1 s), `test/test_screens.c` + golden `boot_four_lines` (both sets)

**Interfaces (produced):**

```c
/* app/lt_sup.h */
enum { BOOT_STORAGE = 0, BOOT_DISPLAY, BOOT_GPS, BOOT_IMU, BOOT_SLOTS };
enum { BOOT_UNKNOWN = 0, BOOT_OK, BOOT_FAIL, BOOT_SIM };
void    sup_boot_report(uint8_t slot, uint8_t status);   /* any task; relaxed atomic byte store */
uint8_t sup_boot_status(uint8_t slot);
```

- [ ] **Step 1: Failing test.** `test_boot_four_lines`: `m.screen = SCR_ONESHOT; m.oneshot = ONESHOT_BOOT; strcpy(m.boot_name, "LAPTIMER"); strcpy(m.boot_ver, "v0.1.0-77-gabcdef0"); m.boot_n_lines = 4; strcpy(m.boot_line[0], "STORAGE OK"); strcpy(m.boot_line[1], "DISPLAY OK"); strcpy(m.boot_line[2], "GPS SIM"); strcpy(m.boot_line[3], "IMU --");` → golden `boot_four_lines.pbm` (the renderer already draws up to `BOOT_MAX_LINES` lines at `BOOT_LINE_Y0 + i*BOOT_LINE_H`; verify all four fit above `CANVAS_H` on the 213 canvas — `44 + 3*14 + 12 = 98` ✓).

- [ ] **Step 2: Run to fail.**

- [ ] **Step 3: Implement.** `sup.c`: `static volatile uint8_t s_boot[BOOT_SLOTS];` with `__atomic_store_n`/`__atomic_load_n` relaxed; asserts on slot/status ranges. Reporters: `boot_storage()` → `sup_boot_report(BOOT_STORAGE, mrc < 0 ? BOOT_FAIL : BOOT_OK)`; pipeline → `sup_boot_report(BOOT_GPS, CFG_GPS_SIM ? BOOT_SIM : (rc == 0 ? BOOT_OK : BOOT_FAIL))` and the same for IMU with `CFG_IMU_SIM` (add `CFG_IMU_SIM` to `build_config.h.in`/`CMakeLists.txt` next to `CFG_GPS_SIM` if absent — the `IMU=sim` flag already exists in `build.sh`); ui → `sup_boot_report(BOOT_DISPLAY, disp_rc == 0 ? BOOT_OK : BOOT_FAIL)` right after `disp_init()`. `ui.c` new `static void boot_lines_format(void)` (four `snprintf`s into `boot_line[i]` from `sup_boot_status(i)` with the label table `{"STORAGE","DISPLAY","GPS","IMU"}` and status table `{"--","OK","FAIL","SIM"}`; `boot_n_lines = 4`), called before the post-`disp_init` BOOT render (the boot render now happens AFTER `disp_init` for the lines: keep the pre-init render for the panel's first full, then re-render + a partial once the lines are known — simpler: format the lines BEFORE the first `render_fb()` for STORAGE (known) and DISPLAY as `--`, then after `disp_init` re-format and set `s_dirty = true` so the normal loop refreshes the BOOT screen with all four lines) and once more when `now >= boot_arm_us + 1 s` while the BOOT one-shot is still showing (`static bool s_boot_refmt_done`). `ONESHOT_BOOT_MS 3000`.

- [ ] **Step 4: Golden** → promote → green.

- [ ] **Step 5: Gate + commit.** Lint 0; clean `moto_sim` + `moto_neo6m` 0 warnings; `.bss` +4 B.

```
feat(boot): four-slot boot status (storage/display/GPS/IMU) reported by the init sites and shown on the BOOT screen (Plan 7c T8)
```

---

### Task 9: Docs, bench gate, final review, PR

**Files:**
- Modify: `docs/superpowers/plans/2026-09-14-roadmap.md` (Plan 7c entry under Plan 7b; #79/#25/#83/#81/#80/#84 closed), `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` (§3 model fields, §4 CUR cell, §5 `have_best_sector`, §6 unit label, §7 trap suffix + snapshot rows — one line each pointing at the 7c spec), `docs/superpowers/specs/2026-09-25-plan-7-display-deltas.md` §8 (live_clock and boot-lines follow-ups now done; #82 still open), `docs/superpowers/specs/2026-09-29-plan-7c-display-closure-design.md` §11 "Implementation notes" (measured DRAM, any ruling)

- [ ] **Step 1:** Write the edits; `git diff --check`; commit `docs(plan-7c): roadmap entry, 7b spec pointers, implementation notes`.
- [ ] **Step 2 (controller):** bench gate `p07c-d1` per spec §9 with the user (photos into the ledger; 10-min soak with the clock on); final whole-branch review (opus); one fix wave + scoped re-review; combined gate (lint, both host suites, gcc-16 sweep, `moto_sim`, `moto_neo6m`, `PANEL=ws29v2`, `core_selftest`, dev-kit); push; CI; PR against `main`; close #79, #25, #83, #81, #80, #84 with the PR reference.

---

## Self-review

- **Spec coverage:** §2 → T3 (+T1 fold); §3 → T2 (builder, labels, engine cfg), T4 (units), T5 (rows/snapshot use); §4 → T6; §5 → T1 (`fb_diff_rect`) + T7; §6 → T8; §7 model/API → T3/T4/T6/T8; §8 tests → T1, T2, T3, T4, T6, T8 (goldens named as in the spec except `lap_p1_filled`/`lap_p2_stats_filled`/`lap_p2_stats_mph`/`drag_p0_trap_mph`/`lap_p0_cur_clock`/`boot_four_lines` — the spec's `drag_p0_named_kmh`/`drag_p1_named_mph` are covered by `drag_p0_trap_mph` plus Task 2's label tests, since names reach the renderer only as strings); §9 bench → T9; §10 out of scope untouched.
- **Placeholder scan:** none; every code step carries code or an exact rule.
- **Type consistency:** `session_max_t`/`session_max_fold(session_max_t *, const lap_stats_t *)` (T1) used in T3; `speed_display(uint16_t, uint8_t)` (T1) in T3/T4/T5; `fb_diff_rect(const fb_t *, const fb_t *, fb_rect_t *)` (T1) in T7; `drag_cfg_from_user(const cfg_t *, drag_cfg_t *)` and `drag_gate_label(const drag_gate_def_t *, char *, size_t)` (T2) in T5 and the pipeline; `pipe_best_t`/`pipe_drag_t` and their readers (T3) in T3/T5; model fields `have_best_sector[]`, `max_speed_cms`, `units`, `cur_ms`, `cur_running`, `drag_row_t.trap_speed` spelled identically in T3/T4/T5/T6 and the tests; boot enums (T8) used in T8 only.
- **One deviation from the spec, decided here:** `drag_row_t.trap_kmh` is renamed `trap_speed` and holds the display-unit value filled by the ui (the renderer prints it verbatim + suffix), so the renderer never needs the raw cm/s.
