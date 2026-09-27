# Plan 7b — Glanceable Riding Screens Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Plan 7 riding screens with the event-card design of the Plan 7b spec: a 64 px delta that fills the top half of the panel, readable LAST/BEST rows, a sector board, a stats grid and matching DRAG pages, on both canvases, with goldens.

**Architecture:** One new bitmap font (`FONT_HUGE`, 64 px) and one new framebuffer primitive (`fb_text_inv`, white glyphs on black) in `core/ui`; the pure renderer `screens_moto.c` is rewritten page by page against new `canvas.h` constants and a slightly extended `screen_model_t`; the ui task fills the new model fields from the sector and lap events it already receives. Everything except the ui glue is host-tested with PBM goldens on both canvases.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2, Unity host tests + PBM goldens, Pillow for the font generator.

**Spec:** `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` (binding; base spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §20 for everything it does not amend).

## Global Constraints

- Power of 10 on all firmware and `core` code: every function has >= 2 `CORE_ASSERT_*`/`LT_ASSERT_*` checks (they never abort), every loop is bounded by a constant, no recursion, no heap, no goto, <= 60 code lines per function; `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0.
- Zero compiler warnings: host suite (`-Wall -Wextra -Werror -Wshadow -Wconversion`), and clean ccache-disabled builds of `moto_sim`, `moto_neo6m` and `PANEL=ws29v2 ./build.sh moto_sim build` (the last builds into `build/moto_sim_ws29v2`).
- `screens_moto.c` contains no layout literals: every position, width and row height is a `canvas.h` constant under the existing `#if CANVAS_213` split (2.13": 256×122 buffer, 250 visible; 2.9": 296×128).
- Every riding-screen test case asserts `fb_max_ink_col(&s_fb) < CANVAS_VISIBLE_W` and compares against a golden in BOTH `test/snapshots/` (296) and `test/snapshots/213/` (via the `test_screens_213` target); goldens are produced by the dump → PNG → eyeball → promote workflow described in `test/test_screens.c`'s header, never hand-edited.
- Fonts: `components/core/ui/fonts.c` is generated only by `python3 tools/fonts/gen_fonts.py > components/core/ui/fonts.c` (Pillow; the committed file is the build input). `FONT_HUGE` glyph set is exactly `0123456789:.-+`; `FONT_BIG` is removed.
- Text strings drawn in `FONT_HUGE`/`FONT_MED` may only use glyphs those fonts have (digits, `: . - +`, and A–Z for `FONT_MED`); anything else (`/`, lowercase, `(`) goes in `FONT_SMALL`.
- Never flash from a subagent; the controller runs the bench gate over the dev-kit OTA path with the user.
- Commit messages end with the session trailers used on this branch (see `git log -1`).

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `tools/fonts/gen_fonts.py` | font list: adds `FONT_HUGE` 64 px, drops `FONT_BIG` | 1 |
| `components/core/ui/fonts.c` (generated), `include/core/ui/fonts.h` | glyph tables and externs | 1 |
| `components/core/ui/render.c`, `include/core/ui/render.h` | `fb_text_inv()` (inverted opaque text) | 1 |
| `test/test_ui.c` + `test/snapshots/text_huge.pbm`, `text_inv.pbm`, composite golden | primitive-level goldens | 1 |
| `components/core/ui/include/core/ui/model.h` | big-slot and lap-number fields | 2 |
| `components/core/ui/include/core/ui/canvas.h` | all new layout constants, old LAP/DRAG constants removed | 2, 3, 4 |
| `components/core/ui/screens_moto.c` | page renderers | 2, 3, 4 |
| `test/test_screens.c` + goldens (both sets) | riding-screen cases | 2, 3, 4 |
| `components/app/ui/ui.c` | fills the new model fields from events | 5 |
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `docs/superpowers/plans/2026-09-14-roadmap.md` | pointers to the 7b spec, roadmap row | 6 |

---

### Task 1: `FONT_HUGE`, drop `FONT_BIG`, `fb_text_inv()`

**Files:**
- Modify: `tools/fonts/gen_fonts.py:29-31` (FONT_SPECS)
- Regenerate: `components/core/ui/fonts.c`
- Modify: `components/core/ui/include/core/ui/fonts.h:19-21`
- Modify: `components/core/ui/render.c` (`fb_blit_1bpp`, `fb_text_draw`, new `fb_text_inv`), `components/core/ui/include/core/ui/render.h`
- Modify: `test/test_ui.c:100-150` (the two `FONT_BIG` cases) + new case; goldens `test/snapshots/text_huge.pbm`, `text_inv.pbm`, and the composite golden used by the case at `test/test_ui.c:140-150`

**Interfaces:**
- Produces: `extern const font_t FONT_HUGE;` (cell 39×64, glyphs `0-9 : . - +`); `int fb_text_inv(fb_t *fb, const font_t *f, int x, int y, const char *s);` — draws `s` as white glyphs on black opaque cells (each cell `f->w × f->h`), returns the pen x after the last glyph exactly like `fb_text()`.
- `FONT_BIG` no longer exists anywhere.

- [ ] **Step 1: Font list.** In `tools/fonts/gen_fonts.py` replace the `FONT_SPECS` entries with:

```python
FONT_SPECS = [
    ("FONT_HUGE", 64, "0123456789:.-+"),
    ("FONT_MED", 24, "0123456789:.-+" + "".join(chr(c) for c in range(ord("A"), ord("Z") + 1))),
    ("FONT_SMALL", 12, "".join(chr(c) for c in range(32, 127))),
]
```

Regenerate: `python3 tools/fonts/gen_fonts.py > components/core/ui/fonts.c` (needs `pip install pillow`; the script exits non-zero if a glyph set is not monospace at that size — 64 px DejaVu Sans Mono Bold digits are 39 px wide). In `fonts.h` replace the `FONT_BIG` extern with `extern const font_t FONT_HUGE;  /* 64 px: glyphs "0-9 : . - +" (cell 39x64) */` and fix the comment on `font_glyph_index()` (`&FONT_HUGE/&FONT_MED/&FONT_SMALL`). `grep -rn FONT_BIG components test tools` must return nothing except this task's own edits in progress.

- [ ] **Step 2: Failing tests for the primitive and the font.** In `test/test_ui.c` replace `test_text_big_renders_lap_time` with:

```c
static void test_text_huge_renders_delta(void)
{
    /* FONT_HUGE's glyph set is "0-9 : . - +" (fonts.h): a signed delta fits it exactly. */
    fb_text(&s_fb, &FONT_HUGE, 4, 4, "-0.32");
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("text_huge.pbm"), &s_fb));
}

static void test_text_inv_is_white_on_black(void)
{
    /* Inverted text: opaque black cells, glyph ink white. The BEST tag (spec §4) is drawn with it. */
    int end = fb_text_inv(&s_fb, &FONT_SMALL, 4, 4, "BEST");
    TEST_ASSERT_EQUAL_INT(4 + 4 * FONT_SMALL.w, end);
    /* corner pixel of the first cell is background for the glyph, so it must now be black (0) */
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8_t)((s_fb.bits[4 * s_fb.stride + 0] >> 3) & 1u)); /* x=4,y=4 */
    /* a pixel outside the cells is untouched (white = 1) */
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)((s_fb.bits[4 * s_fb.stride + 5] >> 7) & 1u)); /* x=40,y=4 */
    TEST_ASSERT_TRUE(pbm_eq_file(SNAP("text_inv.pbm"), &s_fb));
}
```

In the composite case at `test/test_ui.c:140-150` replace the `FONT_BIG` line with `fb_text(&s_fb, &FONT_HUGE, 4, 4, "-0.32"); /* x[4,199) y[4,68) */` and move the `PREV` row it overlaps to y 72 / the `S2` row to y 100 (keep the case's structure and its comment about non-overlap). Register the two new cases in `main()`; remove the old `RUN_TEST(test_text_big_renders_lap_time)`.

- [ ] **Step 3: Run to see them fail.** `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --target test_ui && ./test/build/test_ui` → compile error on `FONT_HUGE`/`fb_text_inv` until Step 4; after Step 4 the golden compares fail and dump `test/snapshots/*.pbm.actual.pbm`.

- [ ] **Step 4: Implement `fb_text_inv`.** In `render.c` give `fb_blit_1bpp` an `bool invert` parameter and set the pixel as `fb_set_px(fb, ix, iy, invert ? (uint8_t)(ink ^ 1u) : ink);` (ink is 1 = black); pass `false` at the icon call site and thread the flag through `fb_text_draw(fb, font, x, y, s, invert)`; `fb_text()`/`fb_text_right()` pass `false`. Add:

```c
int fb_text_inv(fb_t *fb, const font_t *f, int x, int y, const char *s)
{
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, x);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, x);
    return fb_text_draw(fb, f, x, y, s, true);
}
```

and its prototype in `render.h` under `fb_text_right` with the one-line contract from Interfaces above. A cell with no glyph (`font_glyph_index() < 0`) stays a blank cell: white for `fb_text`, black for `fb_text_inv` (the NULL-bitmap path yields ink 0, inverted to 1).

- [ ] **Step 5: Goldens.** Run `./test/build/test_ui`; convert each `*.actual.pbm` to PNG (`python3 -c "from PIL import Image; Image.open('test/snapshots/text_huge.pbm.actual.pbm').save('/tmp/text_huge.png')"`), eyeball (the huge digits must be 39 px wide cells with ~47 px tall ink; the inverted BEST must read white on black), promote each by `mv`-ing the `.actual.pbm` over the golden name, re-run: all `test_ui` cases pass. Delete `test/snapshots/text_big.pbm` (no longer referenced).

- [ ] **Step 6: Gate.** Lint 0; full host suite green (`ctest --test-dir test/build`); clean ccache-disabled `moto_sim` and `moto_neo6m` builds 0 warnings (the firmware links `fonts.c`; report the flash delta — `FONT_HUGE` is 4480 B of tables, `FONT_BIG` was 15 glyphs × 40 × 3 = 1800 B).

- [ ] **Step 7: Commit.**

```bash
git add tools/fonts/gen_fonts.py components/core/ui/fonts.c components/core/ui/include/core/ui/fonts.h components/core/ui/render.c components/core/ui/include/core/ui/render.h test/test_ui.c test/snapshots/text_huge.pbm test/snapshots/text_inv.pbm test/snapshots/<composite golden> && git rm -q test/snapshots/text_big.pbm
git commit -m "feat(ui): FONT_HUGE 64 px digits (FONT_BIG dropped) and fb_text_inv() white-on-black text (Plan 7b T1)"
```

---

### Task 2: Model fields, canvas constants, LAP page 0 event card

**Files:**
- Modify: `components/core/ui/include/core/ui/model.h:53-66` (new fields)
- Modify: `components/core/ui/include/core/ui/canvas.h` (remove the `LAP_TIME_FONT`/`LAP_CUR_FONT`, `LAP_LABEL_X`…`LAP_DELTA_VALUE_X` blocks; add the CARD block)
- Modify: `components/core/ui/screens_moto.c:202-260` (`render_lap_page0` and helpers; `fault_strip` gains `fault_strip_left_x`)
- Test: `test/test_screens.c` (replace the three `lap_p0_*` cases with five), goldens `lap_p0_first_lap`, `lap_p0_sector_delta`, `lap_p0_lap_delta_wide`, `lap_p0_new_best`, `lap_p0_fault` in both snapshot dirs; delete `lap_p0_mid.pbm`, `lap_p0_empty.pbm`, `lap_p0_newbest_fault.pbm` from both.

**Interfaces:**
- Consumes: `FONT_HUGE`, `fb_text_inv()` (Task 1).
- Produces (model.h, used by Tasks 3–5):

```c
enum { BIG_NONE = 0, BIG_SECTOR_DELTA = 1, BIG_LAP_DELTA = 2 };
/* ... inside screen_model_t, after sector_delta_ms: */
uint8_t  big_kind;                                    /* BIG_* : what LAP page 0's big slot shows */
int32_t  big_delta_ms;                                /* signed; valid for BIG_SECTOR_DELTA / BIG_LAP_DELTA */
uint8_t  big_sector_idx;                              /* 1..n for BIG_SECTOR_DELTA */
uint16_t lap_no;                                      /* running lap number (laps_total + 1 while a lap runs) */
int32_t  last_sector_delta_ms[LAP_MAX_SECTORS + 1];   /* page 1 row 4, index = sector idx */
bool     have_last_sector_delta[LAP_MAX_SECTORS + 1];
```

- Produces (screens_moto.c, file-static, used by Tasks 3–4): `static void fmt_delta_clamped(char *buf, int32_t dms, int32_t max_ms)` (sign + `S.cc`, |value| clamped to `max_ms`), `static void fmt_secs_ms(char *buf, uint32_t ms)` (`SS.cc` below 100 s, `SSS.c` from 100 s; always <= 5 glyphs), `int fault_strip_left_x(uint32_t flags)` (x of the leftmost icon `fault_strip()` will draw, or `CANVAS_VISIBLE_W` when it draws none).
- Produces (canvas.h):

```c
/* ---- Plan 7b LAP page 0: the event card (spec 7b §4) ---- */
#if CANVAS_213
#define CARD_MARKER_RIGHT_X 246
#define CARD_MARKER_Y       1
#define CARD_BIG_X          4
#define CARD_BIG_Y          4
#define CARD_NONE_Y         40   /* "LAP n" (FONT_MED) row when the slot has no delta */
#define CARD_TAG_Y          50   /* inverted BEST tag beside the big number */
#define CARD_TAG_ALT_X      208  /* tag on the marker row when the number is six glyphs */
#define CARD_LABEL_Y        78
#define CARD_VALUE_Y        90
#define CARD_LEFT_LABEL_X   4
#define CARD_LEFT_RIGHT_X   122
#define CARD_RIGHT_LABEL_X  128
#define CARD_RIGHT_RIGHT_X  246
#else
#define CARD_MARKER_RIGHT_X 292
#define CARD_MARKER_Y       1
#define CARD_BIG_X          4
#define CARD_BIG_Y          6
#define CARD_NONE_Y         42
#define CARD_TAG_Y          52
#define CARD_TAG_ALT_X      254
#define CARD_LABEL_Y        82
#define CARD_VALUE_Y        94
#define CARD_LEFT_LABEL_X   4
#define CARD_LEFT_RIGHT_X   146
#define CARD_RIGHT_LABEL_X  152
#define CARD_RIGHT_RIGHT_X  292
#endif
#define CARD_TAG_W    32
#define CARD_TAG_H    14
#define CARD_TAG_GAP  6
#define CARD_FAULT_GAP 4         /* BEST value keeps this many px clear of the fault strip */
#define CARD_DELTA_CLAMP_MS 99990
```

- [ ] **Step 1: Failing tests.** Replace the three `lap_p0_*` cases in `test/test_screens.c` with these five (keep the file's existing assertion trio: dirty box, `fb_max_ink_col`, `pbm_eq_file`):

```c
static void lap_model_base(screen_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->mode = SCR_MODE_LAP;
    m->page = 0;
    m->have_best = true;  m->best_ms = 111900; /* 1:51.90 */
    m->have_prev = true;  m->prev_ms = 112340; /* 1:52.34 */
    m->lap_no = 7;
    m->cur_sector_idx = 2;
    m->batt_pct = 87;
}

static void test_lap_p0_first_lap(void)
{
    screen_model_t m;
    lap_model_base(&m);
    m.have_best = false; m.have_prev = false; m.lap_no = 1; m.cur_sector_idx = 0;
    m.big_kind = BIG_NONE;
    screens_moto_render(&s_fb, &m);
    /* ...assertion trio... */ SNAP("lap_p0_first_lap.pbm")
}

static void test_lap_p0_sector_delta(void)
{
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_SECTOR_DELTA; m.big_delta_ms = -320; m.big_sector_idx = 2;
    screens_moto_render(&s_fb, &m);
    /* ... */ SNAP("lap_p0_sector_delta.pbm")
}

static void test_lap_p0_lap_delta_wide(void)
{
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_LAP_DELTA; m.big_delta_ms = 12500; m.cur_sector_idx = 0; m.lap_no = 12;
    m.new_best = true; /* tag must move to the marker row: six glyphs leave no room beside the number */
    screens_moto_render(&s_fb, &m);
    /* ... */ SNAP("lap_p0_lap_delta_wide.pbm")
}

static void test_lap_p0_new_best(void)
{
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_LAP_DELTA; m.big_delta_ms = -440; m.new_best = true; m.cur_sector_idx = 0;
    m.best_ms = 111460; m.prev_ms = 111460;
    screens_moto_render(&s_fb, &m);
    /* ... */ SNAP("lap_p0_new_best.pbm")
}

static void test_lap_p0_fault(void)
{
    screen_model_t m;
    lap_model_base(&m);
    m.big_kind = BIG_SECTOR_DELTA; m.big_delta_ms = 210; m.big_sector_idx = 2;
    m.flags = (1u << SYS_GPS_NOFIX) | (1u << SYS_BATT_LOW); m.batt_pct = 14;
    screens_moto_render(&s_fb, &m);
    /* ... */ SNAP("lap_p0_fault.pbm")
}
```

(`SYS_GPS_NOFIX`/`SYS_BATT_LOW` come from the header `test_screens.c` already uses for its fault case — keep that include.) Register them in `main()` in place of the removed three.

- [ ] **Step 2: Run to fail.** `cmake --build test/build --target test_screens test_screens_213 && ./test/build/test_screens; ./test/build/test_screens_213` → compile errors on the new fields until Step 3, then golden mismatches (dumps).

- [ ] **Step 3: Model + constants.** Add the fields from Interfaces to `model.h` (after `sector_delta_ms`; keep `sector_delta_ms` — page 1 and Task 5 still use it) and the CARD block to `canvas.h`; delete the `LAP_TIME_FONT`/`LAP_CUR_FONT` block and the `LAP_LABEL_X … LAP_DELTA_VALUE_X` block (both canvases) — `LAP_LABEL_X` is also used by pages 1/2 today: replace those uses with `BOARD_X0`/`GRID_COL1_X` in Task 3, so for this task add a temporary `#define LAP_LABEL_X 4` next to the CARD block with the comment `/* removed in Task 3 */`.

- [ ] **Step 4: Renderer.** In `screens_moto.c` add the helpers and replace `render_lap_page0`:

```c
static void fmt_delta_clamped(char *buf, int32_t dms, int32_t max_ms)
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(max_ms > 0 && max_ms <= CARD_DELTA_CLAMP_MS, UI_ASSERT_CODE);
    int32_t d = dms;
    if (d > max_ms) d = max_ms;
    if (d < -max_ms) d = -max_ms;
    fmt_delta_ms(buf, d);
}

static void fmt_secs_ms(char *buf, uint32_t ms)   /* "SS.cc" below 100 s, "SSS.c" from 100 s: <= 5 glyphs */
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    unsigned s = (unsigned)(ms / 1000u);
    char    *p = buf;
    if (s < 100u) {
        unsigned cs = (unsigned)((ms / 10u) % 100u);
        p = put_uint(p, s);
        p = put_char(p, '.');
        if (cs < 10u) p = put_char(p, '0');
        p = put_uint(p, cs);
    } else {
        if (s > 999u) s = 999u;
        p = put_uint(p, s);
        p = put_char(p, '.');
        p = put_uint(p, (unsigned)((ms / 100u) % 10u));
    }
    *p = '\0';
    CORE_ASSERT_VOID(strlen(buf) <= 5u, UI_ASSERT_CODE);
}

/* x of the leftmost icon fault_strip() draws for `flags`, or CANVAS_VISIBLE_W when it draws none.
 * Derived from the same bit->icon table fault_strip() uses (factor that table into a static
 * helper both call), so the two can never disagree. */
int fault_strip_left_x(uint32_t flags);   /* prototype next to fault_strip() in screens.h */

static int render_card_big(fb_t *fb, const screen_model_t *m)   /* returns the pen x after the text */
{
    CORE_ASSERT_RET(fb != NULL, UI_ASSERT_CODE, CARD_BIG_X);
    CORE_ASSERT_RET(m != NULL, UI_ASSERT_CODE, CARD_BIG_X);
    if (m->big_kind == BIG_NONE) {
        char  buf[16];
        char *p = buf;
        p = put_str(p, "LAP ");
        p = put_uint(p, m->lap_no);
        *p = '\0';
        return fb_text(fb, &FONT_MED, CARD_BIG_X, CARD_NONE_Y, buf);
    }
    char dbuf[DELTA_BUF_LEN];
    fmt_delta_clamped(dbuf, m->big_delta_ms, CARD_DELTA_CLAMP_MS);
    return fb_text(fb, &FONT_HUGE, CARD_BIG_X, CARD_BIG_Y, dbuf);
}

static void render_card_marker(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[16];
    char *p = buf;
    p = put_char(p, 'L');
    p = put_uint(p, m->lap_no);
    p = put_char(p, ' ');
    p = put_char(p, 'S');
    p = put_uint(p, m->cur_sector_idx);
    *p = '\0';
    fb_text_right(fb, &FONT_SMALL, CARD_MARKER_RIGHT_X, CARD_MARKER_Y, buf);
}

static void render_card_tag(fb_t *fb, int x, int y)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(x >= 0 && y >= 0, UI_ASSERT_CODE);
    fb_rect(fb, x, y, CARD_TAG_W, CARD_TAG_H, 1, true);
    fb_text_inv(fb, &FONT_SMALL, x + 2, y + 1, "BEST");
}

static void render_card_footer(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char buf[TIME_BUF_LEN];
    int  best_right = fault_strip_left_x(m->flags) - CARD_FAULT_GAP;
    if (best_right > CARD_RIGHT_RIGHT_X) best_right = CARD_RIGHT_RIGHT_X;

    fb_text(fb, &FONT_SMALL, CARD_LEFT_LABEL_X, CARD_LABEL_Y, "LAST");
    if (m->have_prev) fmt_time_ms(buf, m->prev_ms); else { char *p = put_str(buf, EMPTY_TIME); *p = '\0'; }
    fb_text_right(fb, &FONT_MED, CARD_LEFT_RIGHT_X, CARD_VALUE_Y, buf);

    fb_text(fb, &FONT_SMALL, CARD_RIGHT_LABEL_X, CARD_LABEL_Y, "BEST");
    if (m->have_best) fmt_time_ms(buf, m->best_ms); else { char *p = put_str(buf, EMPTY_TIME); *p = '\0'; }
    fb_text_right(fb, &FONT_MED, best_right, CARD_VALUE_Y, buf);
}

static void render_lap_page0(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    int    end_x = render_card_big(fb, m);
    size_t glyphs = (m->big_kind == BIG_NONE) ? 0u : (size_t)((end_x - CARD_BIG_X) / FONT_HUGE.w);
    bool   wide = glyphs > 5u;   /* six glyphs end at x 238: no room for the tag beside them */
    if (m->new_best && wide) {
        render_card_tag(fb, CARD_TAG_ALT_X, CARD_MARKER_Y);   /* replaces the marker this lap */
    } else {
        render_card_marker(fb, m);
        if (m->new_best) render_card_tag(fb, end_x + CARD_TAG_GAP, CARD_TAG_Y);
    }
    render_card_footer(fb, m);
    fault_strip(fb, m->flags, m->batt_pct);
}
```

`fmt_delta_ms` already emits `+S.cc`/`-S.cc`; with the clamp its longest output is `+99.99`. Keep `render_lap_page0`'s old comment block out; the new comments state the spec section.

- [ ] **Step 5: Goldens.** Run both executables; convert the ten dumps to PNG and eyeball against spec §4 (huge digits top-left, marker top-right above the ink line, footer labels above right-aligned values, tag beside the number / on the marker row for the wide case, BEST value clear of the icons in the fault case); promote; re-run → both green; remove the three old goldens from both dirs.

- [ ] **Step 6: Gate.** Lint 0; host suite green; clean `moto_sim`/`moto_neo6m` builds 0 warnings (the firmware compiles the renderer; `ui.c` still compiles because it does not yet touch the new fields).

- [ ] **Step 7: Commit.** `git add` the four source files, `test/test_screens.c`, the ten new goldens; `git rm` the six old ones. Message: `feat(ui): LAP page 0 event card — 64 px delta, LAST/BEST footer, marker, BEST tag; model big-slot fields (Plan 7b T2)`.

---

### Task 3: LAP page 1 sector board and page 2 stats grid

**Files:**
- Modify: `components/core/ui/include/core/ui/canvas.h` (remove `LAP1_*`, `LAP2_ROW_H`, the temporary `LAP_LABEL_X`; add BOARD/GRID blocks)
- Modify: `components/core/ui/screens_moto.c:262-372` (`render_lap_page1`, `render_lap_page2`)
- Test: `test/test_screens.c` (`lap_p1_sectors` reworked, `lap_p1_many_sectors` new, `lap_p2_stats` reworked); goldens in both dirs.

**Interfaces:**
- Consumes: `fmt_secs_ms`, `fmt_delta_clamped`, model fields from Task 2.
- Produces (canvas.h):

```c
/* ---- Plan 7b LAP page 1: sector board (spec 7b §5) ---- */
#define BOARD_COLS  3
#define BOARD_X0    4
#define BOARD_COL_W ((CANVAS_VISIBLE_W - 8) / BOARD_COLS)
#define BOARD_DELTA_CLAMP_MS 9990     /* "+9.99": five glyphs fit an 80 px column */
#if CANVAS_213
#define BOARD_HEADER_Y 2
#define BOARD_LABEL_Y  20
#define BOARD_VALUE_Y  34
#define BOARD_DELTA_Y  66
#else
#define BOARD_HEADER_Y 2
#define BOARD_LABEL_Y  22
#define BOARD_VALUE_Y  36
#define BOARD_DELTA_Y  70
#endif
/* ---- Plan 7b LAP page 2: stats grid (spec 7b §6) ---- */
#if CANVAS_213
#define GRID_COL1_X   4
#define GRID_COL2_X   128
#define GRID_LABEL_Y0 2
#define GRID_VALUE_Y0 14
#define GRID_LABEL_Y1 62
#define GRID_VALUE_Y1 74
#define GRID_FOOTER_Y 106
#else
#define GRID_COL1_X   4
#define GRID_COL2_X   152
#define GRID_LABEL_Y0 2
#define GRID_VALUE_Y0 16
#define GRID_LABEL_Y1 64
#define GRID_VALUE_Y1 78
#define GRID_FOOTER_Y 110
#endif
#define GRID_SUB_GAP 4    /* px between a FONT_MED value and its FONT_SMALL suffix */
#define GRID_SUB_DY  8    /* the suffix sits this many px below the value's top */
```

- [ ] **Step 1: Failing tests.**

```c
static void test_lap_p1_sectors(void)
{
    screen_model_t m; lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 3;
    m.best_sector_ms[0] = 32100; m.best_sector_ms[1] = 41000; m.best_sector_ms[2] = 39240;
    m.have_theo = true; m.theo_best_ms = 111200;
    m.have_last_sector_delta[0] = true; m.last_sector_delta_ms[0] = -120;
    m.have_last_sector_delta[1] = true; m.last_sector_delta_ms[1] = 400;
    /* sector 3 of the current lap not reached yet: cell shows "----" */
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("lap_p1_sectors.pbm")
}

static void test_lap_p1_many_sectors(void)
{
    screen_model_t m; lap_model_base(&m); m.page = 1;
    m.best_n_sectors = 5;
    for (uint8_t i = 0; i < 5; i++) { m.best_sector_ms[i] = 20000u + 1000u * i; m.have_last_sector_delta[i] = true; m.last_sector_delta_ms[i] = 15000; /* clamps to +9.99 */ }
    m.have_theo = false;
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("lap_p1_many_sectors.pbm")   /* S3 label reads "S3 +2"; THEO reads -:--.-- */
}

static void test_lap_p2_stats(void)
{
    screen_model_t m; lap_model_base(&m); m.page = 2;
    m.max_speed_kmh = 214; m.lean_l_deg = 52; m.lean_r_deg = 55;
    m.lat_g_e2 = 132; m.acc_g_e2 = 61; m.brk_g_e2 = 105;
    m.laps_total = 12; m.laps_valid = 10;
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("lap_p2_stats.pbm")
}
```

- [ ] **Step 2: Run to fail** (golden mismatches after the constants compile).

- [ ] **Step 3: Renderers.**

```c
static void render_board_header(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[TIME_BUF_LEN + 12];
    char  t[TIME_BUF_LEN];
    char *p = put_str(buf, "BEST LAP ");
    if (m->have_best) { fmt_time_ms(t, m->best_ms); p = put_str(p, t); } else p = put_str(p, EMPTY_TIME);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, BOARD_X0, BOARD_HEADER_Y, buf);
    p = put_str(buf, "THEO ");
    if (m->have_theo) { fmt_time_ms(t, m->theo_best_ms); p = put_str(p, t); } else p = put_str(p, EMPTY_TIME);
    *p = '\0';
    fb_text_right(fb, &FONT_SMALL, CANVAS_VISIBLE_W - BOARD_X0, BOARD_HEADER_Y, buf);
}

static void render_lap_page1(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    render_board_header(fb, m);
    uint8_t n = m->best_n_sectors > LAP_MAX_SECTORS + 1 ? (uint8_t)(LAP_MAX_SECTORS + 1) : m->best_n_sectors;
    for (uint8_t i = 0; i < BOARD_COLS; i++) {
        int   x = BOARD_X0 + (int)i * BOARD_COL_W;
        char  buf[TIME_BUF_LEN];
        char *p = put_char(buf, 'S');
        p = put_uint(p, (unsigned)i + 1u);
        if (i == BOARD_COLS - 1 && n > BOARD_COLS) { p = put_str(p, " +"); p = put_uint(p, (unsigned)(n - BOARD_COLS)); }
        *p = '\0';
        fb_text(fb, &FONT_SMALL, x, BOARD_LABEL_Y, buf);
        if (i < n) { fmt_secs_ms(buf, m->best_sector_ms[i]); } else { p = put_str(buf, "--.--"); *p = '\0'; }
        fb_text(fb, &FONT_MED, x, BOARD_VALUE_Y, buf);
        if (i < n && m->have_last_sector_delta[i]) { fmt_delta_clamped(buf, m->last_sector_delta_ms[i], BOARD_DELTA_CLAMP_MS); }
        else { p = put_str(buf, "----"); *p = '\0'; }
        fb_text(fb, &FONT_MED, x, BOARD_DELTA_Y, buf);
    }
}

static void grid_cell(fb_t *fb, int x, int ly, int vy, const char *label, const char *value)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(label != NULL && value != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, x, ly, label);
    fb_text(fb, &FONT_MED, x, vy, value);
}

static void render_lap_page2(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[48];
    char *p;
    p = put_uint(buf, m->max_speed_kmh); *p = '\0';
    grid_cell(fb, GRID_COL1_X, GRID_LABEL_Y0, GRID_VALUE_Y0, "MAX SPD", buf);
    p = put_char(buf, 'L'); p = put_uint(p, m->lean_l_deg); p = put_char(p, ' '); p = put_char(p, 'R'); p = put_uint(p, m->lean_r_deg); *p = '\0';
    grid_cell(fb, GRID_COL2_X, GRID_LABEL_Y0, GRID_VALUE_Y0, "LEAN", buf);   /* FONT_MED has no '/', hence L52 R55 */
    p = put_g_e2(buf, m->lat_g_e2); *p = '\0';
    grid_cell(fb, GRID_COL1_X, GRID_LABEL_Y1, GRID_VALUE_Y1, "LAT G", buf);
    p = put_uint(buf, m->laps_total); *p = '\0';
    grid_cell(fb, GRID_COL2_X, GRID_LABEL_Y1, GRID_VALUE_Y1, "LAPS", buf);
    int end = GRID_COL2_X + (int)strlen(buf) * FONT_MED.w;
    p = put_str(buf, "("); p = put_uint(p, m->laps_valid); p = put_str(p, " valid)"); *p = '\0';
    fb_text(fb, &FONT_SMALL, end + GRID_SUB_GAP, GRID_VALUE_Y1 + GRID_SUB_DY, buf);
    p = put_str(buf, "ACC "); p = put_g_e2(p, m->acc_g_e2); p = put_str(p, "   BRK "); p = put_g_e2(p, m->brk_g_e2); *p = '\0';
    fb_text(fb, &FONT_SMALL, GRID_COL1_X, GRID_FOOTER_Y, buf);
}
```

Remove `lap2_row_y()`, the `LAP1_*`/`LAP2_ROW_H` constants and the temporary `LAP_LABEL_X` (replace remaining uses with the BOARD/GRID constants; `render_drag_gate_grid` still uses `LAP_LABEL_X` until Task 4 — give it `#define DRAG_LABEL_X` from the existing DRAG block for now).

- [ ] **Step 4: Goldens** (six dumps: three cases × two canvases): eyeball per spec §5/§6 (three columns, "S3 +2" in the many-sectors case, `----` for the missing delta, the grid's `(10 valid)` suffix small and beside `12`), promote, re-run green.

- [ ] **Step 5: Gate + commit.** Lint 0, host suite, two clean firmware builds 0 warnings. Message: `feat(ui): LAP page 1 sector board and page 2 stats grid in FONT_MED (Plan 7b T3)`.

---

### Task 4: DRAG pages

**Files:**
- Modify: `components/core/ui/include/core/ui/canvas.h` (remove `DRAG_LABEL_X … DRAG_ARMED_Y` and `DRAG12_*`; add DCARD/DLIST blocks)
- Modify: `components/core/ui/screens_moto.c:374-505` (`render_drag_row` removed, `render_drag_page0` rewritten, `render_drag_gate_grid` → `render_drag_gate_list`)
- Test: `test/test_screens.c` (`drag_p0_benches` → `drag_p0_ready`, `drag_p0_gate_speed`, `drag_p0_distance`; `drag_p1_gates`, `drag_p2_best` reworked); goldens both dirs; delete `drag_p0_benches.pbm` from both.

**Interfaces:**
- Consumes: `fmt_secs_ms`, `FONT_HUGE`.
- Produces (canvas.h):

```c
/* ---- Plan 7b DRAG page 0: the run card (spec 7b §7) ---- */
#if CANVAS_213
#define DCARD_LABEL_X       4
#define DCARD_LABEL_Y       2
#define DCARD_BIG_X         4
#define DCARD_BIG_Y         14
#define DCARD_READY_Y       40
#define DCARD_SPEED_Y       80
#define DCARD_FOOTER_Y      108
#define DCARD_ARMED_RIGHT_X 246
#define DCARD_ARMED_Y       2
#else
#define DCARD_LABEL_X       4
#define DCARD_LABEL_Y       2
#define DCARD_BIG_X         4
#define DCARD_BIG_Y         16
#define DCARD_READY_Y       42
#define DCARD_SPEED_Y       84
#define DCARD_FOOTER_Y      114
#define DCARD_ARMED_RIGHT_X 292
#define DCARD_ARMED_Y       2
#endif
#define DCARD_UNIT_GAP   4     /* px between the huge distance digits and the FONT_SMALL "m" */
#define DCARD_UNIT_DY    48    /* the "m" sits this far below the huge cell's top (near the baseline) */
#define DCARD_FOOTER_MAX 6     /* earlier gates listed in the footer, newest last */
#define DCARD_FOOTER_SEP "   "
/* ---- Plan 7b DRAG pages 1/2: gate list (spec 7b §7) ---- */
#define DLIST_ROWS 4
#define DLIST_LABEL_DY 6
#if CANVAS_213
#define DLIST_HEADER_Y     2
#define DLIST_COL1_X       4
#define DLIST_COL2_X       128
#define DLIST_COL1_RIGHT_X 122
#define DLIST_COL2_RIGHT_X 246
#define DLIST_ROW_Y0       16
#define DLIST_ROW_H        26
#else
#define DLIST_HEADER_Y     2
#define DLIST_COL1_X       4
#define DLIST_COL2_X       152
#define DLIST_COL1_RIGHT_X 146
#define DLIST_COL2_RIGHT_X 292
#define DLIST_ROW_Y0       18
#define DLIST_ROW_H        26
#endif
#define DLIST_UNIT_W 10   /* room reserved right of a distance value for its FONT_SMALL "m" */
```

- [ ] **Step 1: Failing tests.**

```c
static void drag_gate(screen_model_t *m, const char *label, uint32_t t_ms, uint16_t trap, bool dist, uint16_t dist_m)
{
    drag_row_t *r = &m->drag[m->drag_n++];
    memset(r, 0, sizeof *r);
    strcpy(r->label, label); r->t_ms = t_ms; r->present = true;
    r->trap_kmh = trap; r->has_trap = trap != 0; r->is_distance = dist; r->dist_m = dist_m;
}

static void test_drag_p0_ready(void)
{
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.drag_armed = true; m.batt_pct = 90;
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("drag_p0_ready.pbm")
}

static void test_drag_p0_gate_speed(void)
{
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1/4", 12840, 173, false, 0);
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("drag_p0_gate_speed.pbm")   /* big 12.84, "@173" row, footer "60ft 2.01   330ft 5.43   1/8 8.29" */
}

static void test_drag_p0_distance(void)
{
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 0; m.batt_pct = 90;
    drag_gate(&m, "100-200", 6120, 0, false, 0); drag_gate(&m, "100-0", 0, 0, true, 38);
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("drag_p0_distance.pbm")     /* big "38" + small "m"; footer "100-200 6.12" */
}

static void test_drag_p1_gates(void)   /* seven gates, two present-false */
{
    screen_model_t m = {0}; m.mode = SCR_MODE_DRAG; m.page = 1; m.batt_pct = 90;
    drag_gate(&m, "60ft", 2010, 0, false, 0); drag_gate(&m, "330ft", 5430, 0, false, 0);
    drag_gate(&m, "1/8", 8290, 0, false, 0);  drag_gate(&m, "1000ft", 10900, 0, false, 0);
    drag_gate(&m, "1/4", 12840, 173, false, 0);
    drag_gate(&m, "100-200", 0, 0, false, 0); m.drag[5].present = false;
    drag_gate(&m, "100-0", 0, 0, true, 38);
    screens_moto_render(&s_fb, &m);
    /* trio */ SNAP("drag_p1_gates.pbm")
}

static void test_drag_p2_best(void)    /* same rows, title SESSION BEST, every gate present */
{ /* as drag_p1_gates with page = 2 and m.drag[5] = 100-200 present 6120 */ SNAP("drag_p2_best.pbm") }
```

- [ ] **Step 2: Run to fail.**

- [ ] **Step 3: Renderers.**

```c
static void render_dcard_value(fb_t *fb, const drag_row_t *r)   /* the newest gate's big value */
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(r != NULL, UI_ASSERT_CODE);
    char buf[TIME_BUF_LEN];
    if (r->is_distance) {
        char *p = put_uint(buf, r->dist_m); *p = '\0';
        int end = fb_text(fb, &FONT_HUGE, DCARD_BIG_X, DCARD_BIG_Y, buf);
        fb_text(fb, &FONT_SMALL, end + DCARD_UNIT_GAP, DCARD_BIG_Y + DCARD_UNIT_DY, "m");
        return;
    }
    fmt_secs_ms(buf, r->t_ms);
    fb_text(fb, &FONT_HUGE, DCARD_BIG_X, DCARD_BIG_Y, buf);
    if (r->has_trap) {
        char *p = put_char(buf, '@'); p = put_uint(p, r->trap_kmh); *p = '\0';
        fb_text(fb, &FONT_MED, DCARD_LABEL_X, DCARD_SPEED_Y, buf);
    }
}

static void render_dcard_footer(fb_t *fb, const screen_model_t *m, uint8_t n)   /* gates 0..n-2 */
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL && n >= 1u && n <= DRAG_MAX_GATES, UI_ASSERT_CODE);
    uint8_t first = (n - 1u > DCARD_FOOTER_MAX) ? (uint8_t)(n - 1u - DCARD_FOOTER_MAX) : 0u;
    int     x = DCARD_LABEL_X;
    for (uint8_t i = first; i + 1u < n && i < DRAG_MAX_GATES; i++) {
        const drag_row_t *r = &m->drag[i];
        char  buf[TIME_BUF_LEN + 12];
        char *p = put_str(buf, r->label);
        p = put_char(p, ' ');
        if (r->is_distance) { p = put_uint(p, r->dist_m); p = put_char(p, 'm'); }
        else { char t[TIME_BUF_LEN]; fmt_secs_ms(t, r->t_ms); p = put_str(p, t); }
        if (i + 2u < n) p = put_str(p, DCARD_FOOTER_SEP);
        *p = '\0';
        if (x + (int)strlen(buf) * FONT_SMALL.w > CANVAS_VISIBLE_W - DCARD_LABEL_X) break;   /* never past the edge */
        x = fb_text(fb, &FONT_SMALL, x, DCARD_FOOTER_Y, buf);
    }
}

static void render_drag_page0(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    uint8_t n = m->drag_n > DRAG_MAX_GATES ? (uint8_t)DRAG_MAX_GATES : m->drag_n;
    if (n == 0u) {
        fb_text(fb, &FONT_MED, DCARD_BIG_X, DCARD_READY_Y, "READY");
    } else {
        fb_text(fb, &FONT_SMALL, DCARD_LABEL_X, DCARD_LABEL_Y, m->drag[n - 1u].label);
        render_dcard_value(fb, &m->drag[n - 1u]);
        render_dcard_footer(fb, m, n);
    }
    if (m->drag_armed) fb_text_right(fb, &FONT_MED, DCARD_ARMED_RIGHT_X, DCARD_ARMED_Y, "ARMED");
    fault_strip(fb, m->flags, m->batt_pct);
}

static void render_drag_gate_list(fb_t *fb, const screen_model_t *m, const char *title)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL && title != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, DLIST_COL1_X, DLIST_HEADER_Y, title);
    uint8_t n = m->drag_n > DRAG_MAX_GATES ? (uint8_t)DRAG_MAX_GATES : m->drag_n;
    for (uint8_t i = 0; i < n && i < 2u * DLIST_ROWS; i++) {
        int col = i / DLIST_ROWS, row = i % DLIST_ROWS;          /* left column fills first */
        int x  = col ? DLIST_COL2_X : DLIST_COL1_X;
        int xr = col ? DLIST_COL2_RIGHT_X : DLIST_COL1_RIGHT_X;
        int y  = DLIST_ROW_Y0 + row * DLIST_ROW_H;
        const drag_row_t *r = &m->drag[i];
        char buf[TIME_BUF_LEN];
        fb_text(fb, &FONT_SMALL, x, y + DLIST_LABEL_DY, r->label);
        if (!r->present) { char *p = put_str(buf, "--.--"); *p = '\0'; fb_text_right(fb, &FONT_MED, xr, y, buf); }
        else if (r->is_distance) {
            char *p = put_uint(buf, r->dist_m); *p = '\0';
            fb_text_right(fb, &FONT_MED, xr - DLIST_UNIT_W, y, buf);
            fb_text(fb, &FONT_SMALL, xr - DLIST_UNIT_W + 2, y + DLIST_LABEL_DY, "m");
        } else { fmt_secs_ms(buf, r->t_ms); fb_text_right(fb, &FONT_MED, xr, y, buf); }
    }
}
```

`render_drag_page1`/`page2` call `render_drag_gate_list` with `"LAST RUN"` / `"SESSION BEST"`. Delete `render_drag_row`, `DRAG_EMPTY_TIME` and the old DRAG constants.

- [ ] **Step 4: Goldens** (ten dumps), eyeball per spec §7 (READY + ARMED; big 12.84 with `@173`; big 38 with a small m; footer order; the list's two columns with `--.--` for the missing gate), promote, re-run green; delete `drag_p0_benches.pbm` (both dirs).

- [ ] **Step 5: Gate + commit.** Lint 0, host suite, two clean builds 0 warnings. Message: `feat(ui): DRAG run card (newest gate huge) and two-column gate lists (Plan 7b T4)`.

---

### Task 5: ui task wiring and bench gate `p07b-d1`

**Files:**
- Modify: `components/app/ui/ui.c` (`handle_sector`, `handle_lap_result`, `handle_lap_complete`, session/boot init of the model)

**Interfaces:**
- Consumes: model fields (Task 2); `EV_SECTOR` (arg16 idx, arg32 split ms, arg32b delta), `EV_LAP_COMPLETE` (arg16 lap no, arg32 lap ms, arg32b lap delta, flags incl. `LAP_F_VALID`/`LAP_F_OUT_LAP`).

- [ ] **Step 1: Boot/session init.** Where `s_model` is first set up in `ui_task()` (after `cfg_defaults`), set `s_model.lap_no = 1; s_model.big_kind = BIG_NONE;` (the rest is zero-initialised static storage).

- [ ] **Step 2: Sector event.** Replace `handle_sector()`:

```c
static void handle_sector(const event_t *e)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(e->arg16 <= LAP_MAX_SECTORS, UI_APP_ASSERT_CODE);   /* engine sector idx in range */
    uint8_t idx = (uint8_t)e->arg16;
    s_model.cur_sector_idx  = idx;
    s_model.cur_ms_at_gate  = e->arg32;
    s_model.sector_delta_ms = (int32_t)e->arg32b;
    s_model.big_kind        = s_model.have_best ? (uint8_t)BIG_SECTOR_DELTA : (uint8_t)BIG_NONE;
    s_model.big_delta_ms    = (int32_t)e->arg32b;
    s_model.big_sector_idx  = idx;
    s_model.last_sector_delta_ms[idx]      = (int32_t)e->arg32b;
    s_model.have_last_sector_delta[idx]    = s_model.have_best;
    s_model.new_best = false;   /* the BEST tag lives until the next gate (spec 7b §4) */
    s_dirty          = true;
}
```

- [ ] **Step 3: Lap event.** `handle_lap_complete()` passes `e->arg32b` into `handle_lap_result(lap_ms, flags, lap_delta_ms)`; in `handle_lap_result` capture `bool had_best = s_model.have_best;` before the existing BEST/PREV/valid logic, then after it:

```c
    s_model.big_kind     = had_best ? (uint8_t)BIG_LAP_DELTA : (uint8_t)BIG_NONE;
    s_model.big_delta_ms = lap_delta_ms;
    s_model.lap_no       = (uint16_t)(s_model.laps_total + 1u);
    s_model.cur_sector_idx = 0;
    for (uint8_t i = 0; i < LAP_MAX_SECTORS + 1; i++) s_model.have_last_sector_delta[i] = false;
```

(`new_best` is set by the existing logic for valid laps; the out-lap path in `handle_lap_complete` stays untouched — it never reaches `handle_lap_result`.) Keep every function ≤ 60 code lines and ≥ 2 asserts; split a helper if the length rule trips.

- [ ] **Step 4: Gate (software).** Lint 0; clean `moto_sim`/`moto_neo6m` builds 0 warnings; DRAM delta ≈ +44 B of model fields (report the `.bss` figure vs the base commit).

- [ ] **Step 5: Commit.** `feat(ui): fill the event card from sector/lap events — big slot, lap number, last-lap sector deltas (Plan 7b T5)`.

- [ ] **Step 6: Flash gate `p07b-d1` (controller + user, dev-kit OTA push, `moto_sim`):** photos into the ledger of: `LAP 1` card before the first lap; a sector delta in the big slot after a gate (`L1 S1`); the lap delta and `BEST` tag after the line (first timed lap is a best); page 1 board (`dbg btn down`) and page 2 grid; DRAG `READY`/`ARMED` (set `mode` to drag via `config set {"mode":"drag"}` then reset; restore `lap` afterwards). Every refresh line `rc=0`, no task WDT over the run. Tag `p07b-d1`.

---

### Task 6: Docs, final review, PR

**Files:**
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §20.2 (font table: HUGE/MED/SMALL, pointer to the 7b spec) and §20.5 (first line: "Superseded by `2026-09-27-plan-7b-glanceable-ui-design.md`; the layouts below are the Plan 4 originals kept for history")
- Modify: `docs/superpowers/plans/2026-09-14-roadmap.md` (Plan 7b row under Plan 7: what, gate `p07b-d1`, done date)
- Modify: `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` §6 (`L52 R55` instead of `52 / 55` — `FONT_MED` has no `/`) and §7 (the trap row shows `@173` without a unit: the model carries no units field; note as a follow-up)

- [ ] **Step 1:** Write the edits; `git diff --check` clean; commit `docs(plan-7b): spec pointers, roadmap row, spec corrections from implementation`.
- [ ] **Step 2 (controller):** final whole-branch review (opus) on `<merge-base>..HEAD`, one fix round + scoped re-review, combined gate (lint, both host suites, gcc-16 sweep, `moto_sim`, `moto_neo6m`, `PANEL=ws29v2`, `core_selftest`, dev-kit), push, CI, PR against `main` (stacked on #76 until that merges). The user chooses the merge.

---

## Self-review

- **Spec coverage:** §2 fonts → T1; §3 model → T2 (fields) + T5 (wiring); §4 card → T2; §5 board → T3; §6 grid → T3; §7 DRAG → T4; §8 constants → T2/T3/T4 (each removes the block it replaces); §9 tests → T1–T4 goldens on both canvases, ink check kept; §10 out of scope → untouched; §11 bench → T5 step 6.
- **Placeholder scan:** none; every code step carries code or an exact command. The "trio" shorthand in tests refers to the three assertions quoted in Task 2 Step 1.
- **Type consistency:** `fb_text_inv(fb_t *, const font_t *, int, int, const char *)` (T1) used in T2; `fmt_delta_clamped(char *, int32_t, int32_t)` and `fmt_secs_ms(char *, uint32_t)` (T2) used in T3/T4; `fault_strip_left_x(uint32_t)` (T2) used in T2 only; model fields spelled identically in T2 tests, T3 tests and T5; `BIG_*` enum values shared.
- **Two spec corrections found while planning** (both recorded in T6 and applied to the spec on the branch before execution): `LEAN` shows `L52 R55` (no `/` in `FONT_MED`), and the trap-speed row has no unit suffix (the model has no units field).
