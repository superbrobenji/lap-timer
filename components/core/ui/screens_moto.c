/* Moto riding screens: LAP pages 0/1/2 and DRAG pages 0/1/2 (spec §20.4-20.5) + the §11.4 benches
 * rule + the shared fault-icon strip (§20.5 + §17.4); plus the one-shot screens (§20.6) and the
 * menu list (§20.7) and the top-level screens_render() dispatch (session 4.3). Pure C11, no
 * malloc/float/libm — every layout position is a compile-time constant and every value is
 * formatted with plain integer arithmetic, so every render function here is a pure function of its
 * screen_model_t and the PBM goldens in test/snapshots/ are byte-identical across clang and
 * gcc-16.
 */
#include "core/ui/model.h"
#include "core/core.h"

#include <string.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions report UI_ASSERT_CODE
 * (shared with render.c: components/core/ui is one assertion "module" for the retrofit). They
 * guard genuine anomalies -- NULL params -- never the existing, tested "up to N" clamps
 * (best_n_sectors/drag_n/menu_n/boot_n_lines and friends): those are this module's documented,
 * specified behavior for an over-long model field (draw the first N and silently trim the rest),
 * not a bug, so they stay plain clamps rather than becoming assertions. */
#define UI_ASSERT_CODE 0x0AA0

/* ---- tiny pure-integer string helpers (no snprintf/stdio: core/ui stays free of <stdio.h>,
 * mirroring render.c's <string.h>-only policy) ---- */

static char *put_char(char *p, char c)
{
    CORE_ASSERT_RET(p != NULL, UI_ASSERT_CODE, p);
    *p++ = c;
    return p;
}

static char *put_str(char *p, const char *s)
{
    CORE_ASSERT_RET(p != NULL, UI_ASSERT_CODE, p);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, p);
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

/* Decimal, no leading zeros (0 itself renders as "0"). */
static char *put_uint(char *p, unsigned v)
{
    CORE_ASSERT_RET(p != NULL, UI_ASSERT_CODE, p);
    char tmp[12];
    int  n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        *p++ = tmp[--n];
    }
    return p;
}

/* "W.FF" from a g-value stored as g*100 (e.g. 132 -> "1.32"). */
static char *put_g_e2(char *p, uint16_t g_e2)
{
    CORE_ASSERT_RET(p != NULL, UI_ASSERT_CODE, p);
    unsigned whole = (unsigned)g_e2 / 100u;
    unsigned frac = (unsigned)g_e2 % 100u;
    p = put_uint(p, whole);
    p = put_char(p, '.');
    if (frac < 10u) {
        p = put_char(p, '0');
    }
    return put_uint(p, frac);
}

/* ---- pure integer time formatters (spec §20.5) ---- */

#define TIME_BUF_LEN  20 /* "M:SS.cc\0" and then some, generous for large minute counts */
#define DELTA_BUF_LEN 20 /* "+S.cc\0" and then some */

/* 7 chars, matching a real "M:SS.cc" value's width for a single-digit minute count (e.g.
 * "1:51.90"): fb_text/fb_text_right blit opaque cells (background pixels included, not just ink),
 * so a right-aligned placeholder wider than the real value it stands in for would eat into the
 * "BEST"/"PREV" label at LAP_LABEL_X — confirmed by eyeballing an 8-char "--:--.--" placeholder,
 * which visibly clobbered the label. */
static const char EMPTY_TIME[] = "-:--.--";

/* M:SS.cc (minutes:seconds.centiseconds), e.g. 111900 -> "1:51.90". `buf` must be >= TIME_BUF_LEN
 * bytes. Pure integer division/modulo, no float. */
static void fmt_time_ms(char *buf, uint32_t ms)
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    unsigned cs = (unsigned)((ms / 10u) % 100u);
    unsigned s = (unsigned)((ms / 1000u) % 60u);
    unsigned m = (unsigned)(ms / 60000u);

    char *p = buf;
    p = put_uint(p, m);
    p = put_char(p, ':');
    if (s < 10u) {
        p = put_char(p, '0');
    }
    p = put_uint(p, s);
    p = put_char(p, '.');
    if (cs < 10u) {
        p = put_char(p, '0');
    }
    p = put_uint(p, cs);
    CORE_ASSERT_VOID((size_t)(p - buf) < TIME_BUF_LEN, UI_ASSERT_CODE); /* room left for the NUL, per this function's own documented buf size */
    *p = '\0';
}

/* Signed S.cc (seconds.centiseconds, no minutes, always shows a sign), e.g. -210 -> "-0.21",
 * +340 -> "+0.34". `buf` must be >= DELTA_BUF_LEN bytes. Handles INT32_MIN without UB (negates via
 * unsigned arithmetic rather than `-dms`). */
static void fmt_delta_ms(char *buf, int32_t dms)
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    char    *p = buf;
    uint32_t mag;
    if (dms < 0) {
        p = put_char(p, '-');
        mag = (uint32_t)(-(dms + 1)) + 1u;
    } else {
        p = put_char(p, '+');
        mag = (uint32_t)dms;
    }
    unsigned cs = (unsigned)((mag / 10u) % 100u);
    unsigned s = (unsigned)(mag / 1000u);

    p = put_uint(p, s);
    p = put_char(p, '.');
    if (cs < 10u) {
        p = put_char(p, '0');
    }
    p = put_uint(p, cs);
    CORE_ASSERT_VOID((size_t)(p - buf) < DELTA_BUF_LEN, UI_ASSERT_CODE); /* room left for the NUL, per this function's own documented buf size */
    *p = '\0';
}

/* ---- shared fault-icon strip (spec §20.5 + §17.4) ---- */

/* Bit position (SCR_SYS_*, model.h) -> icon (icons.h), or -1 for a bit with no icon: the §17.4
 * "--" bits (SYS_DISP_DEAD, SYS_HEAP_LOW, SYS_OTA_PENDING) plus SYS_IMU_DEAD and
 * SYS_FUSION_DISAGREE, neither of which has a matching bitmap in icons.h (only ICON_IMU_Q exists,
 * no IMU strike-through / lean "?" icon). SYS_STORAGE_DEAD has no disk-strike bitmap either, so it
 * falls back to the plain ICON_DISK glyph. */
static const int8_t FAULT_ICON_FOR_BIT[14] = {
    (int8_t)ICON_GPS_STRIKE,    /* 0  SYS_GPS_DEAD */
    (int8_t)ICON_GPS_STRIKE,    /* 1  SYS_GPS_NOFIX */
    -1,                         /* 2  SYS_IMU_DEAD */
    (int8_t)ICON_IMU_Q,         /* 3  SYS_IMU_SUSPECT */
    -1,                         /* 4  SYS_DISP_DEAD */
    (int8_t)ICON_DISK,          /* 5  SYS_STORAGE_DEAD */
    (int8_t)ICON_DISK_FULL,     /* 6  SYS_STORAGE_FULL */
    (int8_t)ICON_DISK_WARN,     /* 7  SYS_STORAGE_DEGRADED */
    (int8_t)ICON_BATT_LOW,      /* 8  SYS_BATT_LOW (special-cased below for the "%<pct>" label) */
    (int8_t)ICON_SAFE,          /* 9  SYS_SAFE_MODE */
    -1,                         /* 10 SYS_HEAP_LOW */
    (int8_t)ICON_THERMOMETER,   /* 11 SYS_DISP_TEMP_THROTTLE */
    -1,                         /* 12 SYS_OTA_PENDING */
    -1,                         /* 13 SYS_FUSION_DISAGREE */
};

/* Strip anchor (spec §20.5: "x descending from ~284, y≈116" on the 296x128 frame): the rightmost
 * icon's top-left. 284 + ICON_W(12) = 296 = fb width, so the first icon's right edge lands flush
 * with the frame edge; 116 + ICON_H(12) = 128 = fb height, flush with the bottom edge. */
#define FAULT_STRIP_X0 284
#define FAULT_STRIP_Y   116

void fault_strip(fb_t *fb, uint32_t flags, uint8_t batt_pct)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    const int pitch = ICON_W + 2; /* 2px gap between icons */
    int       x = FAULT_STRIP_X0;
    int       y = FAULT_STRIP_Y;

    for (int bit = 0; bit < 14; bit++) {
        if ((flags & (1u << bit)) == 0u) {
            continue;
        }
        int icon = FAULT_ICON_FOR_BIT[bit];
        if (icon < 0) {
            continue;
        }
        if (bit == SCR_SYS_BATT_LOW) {
            char     pct_buf[8];
            uint8_t  pct = batt_pct > 100u ? 100u : batt_pct;
            char    *p = pct_buf;
            p = put_uint(p, pct);
            p = put_char(p, '%');
            CORE_ASSERT_VOID((size_t)(p - pct_buf) < sizeof pct_buf, UI_ASSERT_CODE); /* room left for the NUL */
            *p = '\0';
            int text_w = (int)strlen(pct_buf) * FONT_SMALL.w;
            int text_y = y + (ICON_H - FONT_SMALL.h) / 2;
            fb_text(fb, &FONT_SMALL, x - text_w - 1, text_y, pct_buf);
            x -= text_w + 1;
        }
        fb_icon(fb, (uint8_t)icon, x, y);
        x -= pitch;
    }
}

/* ---- LAP page 0 (spec §20.5): BEST/PREV/CUR/dS + fault strip ---- */

#define LAP_LABEL_X          4
#define LAP_TIME_RIGHT_X     200
#define LAP_ROW_BEST_Y       4
#define LAP_ROW_PREV_Y       46
#define LAP_ROW_CUR_Y        88
#define LAP_ROW_DELTA_Y      112
#define LAP_CUR_TIME_RIGHT_X 180
#define LAP_CUR_SECTOR_X     210
#define LAP_DELTA_VALUE_X    24

static void render_lap_page0(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char buf[TIME_BUF_LEN];

    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP_ROW_BEST_Y, "BEST");
    if (m->have_best) {
        fmt_time_ms(buf, m->best_ms);
        fb_text_right(fb, &FONT_BIG, LAP_TIME_RIGHT_X, LAP_ROW_BEST_Y, buf);
    } else {
        fb_text_right(fb, &FONT_BIG, LAP_TIME_RIGHT_X, LAP_ROW_BEST_Y, EMPTY_TIME);
    }

    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP_ROW_PREV_Y, "PREV");
    if (m->have_prev) {
        fmt_time_ms(buf, m->prev_ms);
        fb_text_right(fb, &FONT_BIG, LAP_TIME_RIGHT_X, LAP_ROW_PREV_Y, buf);
    } else {
        fb_text_right(fb, &FONT_BIG, LAP_TIME_RIGHT_X, LAP_ROW_PREV_Y, EMPTY_TIME);
    }

    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP_ROW_CUR_Y, "CUR");
    fmt_time_ms(buf, m->cur_ms_at_gate);
    fb_text_right(fb, &FONT_MED, LAP_CUR_TIME_RIGHT_X, LAP_ROW_CUR_Y, buf);
    {
        char  sbuf[8];
        char *p = sbuf;
        p = put_char(p, 'S');
        p = put_uint(p, m->cur_sector_idx);
        CORE_ASSERT_VOID((size_t)(p - sbuf) < sizeof sbuf, UI_ASSERT_CODE); /* room left for the NUL */
        *p = '\0';
        fb_text(fb, &FONT_MED, LAP_CUR_SECTOR_X, LAP_ROW_CUR_Y, sbuf);
    }

    /* Spec §20.5 lists this row's value as FONT_MED, but at y=112 a 24px-tall FONT_MED cell runs
     * to y=136 -- 8px past the 128px frame -- and eyeballing that literal reading showed real
     * clipped/illegible ink (confirmed against the actual rendered PBM, not just cell-box math).
     * The fault-icon strip occupies this same bottom band at 12px tall (FAULT_STRIP_Y=116), which
     * is the same scale as FONT_SMALL, so this row uses FONT_SMALL for the value instead: it fits
     * fully within the frame with no clipping and no collision with the CUR row above it. */
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP_ROW_DELTA_Y, "dS");
    if (m->new_best) {
        fb_text(fb, &FONT_SMALL, LAP_DELTA_VALUE_X, LAP_ROW_DELTA_Y, "BEST");
    } else {
        char dbuf[DELTA_BUF_LEN];
        fmt_delta_ms(dbuf, m->sector_delta_ms);
        fb_text(fb, &FONT_SMALL, LAP_DELTA_VALUE_X, LAP_ROW_DELTA_Y, dbuf);
    }

    fault_strip(fb, m->flags, m->batt_pct);
}

/* ---- LAP page 1 (spec §20.5): best-lap sector splits + THEO ---- */

#define LAP1_TITLE_Y       4
#define LAP1_SECTOR_COLS   3
#define LAP1_SECTOR_X0     4
#define LAP1_SECTOR_COL_W  96
#define LAP1_SECTOR_Y0     20
#define LAP1_SECTOR_ROW_H  16
#define LAP1_THEO_LABEL_Y  88
#define LAP1_THEO_VALUE_Y  80

static void render_lap_page1(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP1_TITLE_Y, "BEST LAP");

    for (uint8_t i = 0; i < m->best_n_sectors && i < LAP_MAX_SECTORS + 1; i++) {
        int col = (int)i % LAP1_SECTOR_COLS;
        int row = (int)i / LAP1_SECTOR_COLS;
        int x = LAP1_SECTOR_X0 + col * LAP1_SECTOR_COL_W;
        int y = LAP1_SECTOR_Y0 + row * LAP1_SECTOR_ROW_H;

        char  tbuf[TIME_BUF_LEN];
        char  sbuf[TIME_BUF_LEN + 8];
        char *p = sbuf;
        fmt_time_ms(tbuf, m->best_sector_ms[i]);
        p = put_char(p, 'S');
        p = put_uint(p, (unsigned)i + 1u);
        p = put_char(p, ' ');
        p = put_str(p, tbuf);
        *p = '\0';
        fb_text(fb, &FONT_SMALL, x, y, sbuf);
    }

    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP1_THEO_LABEL_Y, "THEO");
    if (m->have_theo) {
        char tbuf[TIME_BUF_LEN];
        fmt_time_ms(tbuf, m->theo_best_ms);
        fb_text_right(fb, &FONT_MED, LAP_TIME_RIGHT_X, LAP1_THEO_VALUE_Y, tbuf);
    } else {
        fb_text_right(fb, &FONT_MED, LAP_TIME_RIGHT_X, LAP1_THEO_VALUE_Y, EMPTY_TIME);
    }
}

/* ---- LAP page 2 (spec §20.5): session stats ---- */

#define LAP2_ROW_H 24
static inline int lap2_row_y(int n) { return 8 + n * LAP2_ROW_H; }

static void render_lap_page2(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[48];
    char *p;

    p = buf;
    p = put_str(p, "MAX SPD ");
    p = put_uint(p, m->max_speed_kmh);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, lap2_row_y(0), buf);

    p = buf;
    p = put_str(p, "LEAN L ");
    p = put_uint(p, m->lean_l_deg);
    p = put_str(p, " R ");
    p = put_uint(p, m->lean_r_deg);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, lap2_row_y(1), buf);

    p = buf;
    p = put_str(p, "LAT G ");
    p = put_g_e2(p, m->lat_g_e2);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, lap2_row_y(2), buf);

    p = buf;
    p = put_str(p, "ACC ");
    p = put_g_e2(p, m->acc_g_e2);
    p = put_str(p, " BRK ");
    p = put_g_e2(p, m->brk_g_e2);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, lap2_row_y(3), buf);

    p = buf;
    p = put_str(p, "LAPS ");
    p = put_uint(p, m->laps_total);
    p = put_str(p, " (");
    p = put_uint(p, m->laps_valid);
    p = put_str(p, " valid)");
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, lap2_row_y(4), buf);
}

/* ---- DRAG page 0 (spec §20.5 + §11.4 benches rule): up to 4 rows of benches + the 1/4 row ---- */

#define DRAG_LABEL_X       4
#define DRAG_TIME_RIGHT_X  180
#define DRAG_TRAP_X        190
#define DRAG0_ROW_Y0       8
#define DRAG0_ROW_H        24
#define DRAG0_MAX_ROWS     4
#define DRAG_ARMED_RIGHT_X 296
#define DRAG_ARMED_Y       4

/* A row with no time yet (a bench not hit, or the 1/4 row before it is crossed) shows a literal
 * "--" -- spec §11.4's own worked example is "1/4 --" -- rather than the LAP screens' wider
 * "-:--.--" placeholder. Unlike LAP_TIME_RIGHT_X (200, close enough to LAP_LABEL_X's "BEST"/"PREV"
 * labels that an oversized placeholder visibly clobbered them, per EMPTY_TIME's comment above), a
 * DRAG row's label (x=4) and value (right-aligned x=180) are far enough apart that this is a
 * stylistic match to the spec text rather than a clobbering concern.
 */
static const char DRAG_EMPTY_TIME[] = "--";

/* Draws one DRAG row: `label` FONT_SMALL at DRAG_LABEL_X, a value right-aligned at
 * DRAG_TIME_RIGHT_X, and — only when has_trap — "@ <trap_kmh>" FONT_SMALL at DRAG_TRAP_X (spec's
 * own example: "@ 305").
 *
 * The value is `t_ms` formatted as a time (FONT_MED) for a normal gate, "<dist_m> m" (FONT_SMALL)
 * for a distance gate (#40: the 100-0 braking gate, DRAG_BRAKE in core/drag.h, is a stopping
 * DISTANCE in metres, not an elapsed time), or "--" (FONT_MED) if the gate was not hit this run.
 * The distance value uses FONT_SMALL rather than FONT_MED for the same reason the label does:
 * FONT_MED's glyph set ("0-9 : . - + A-Z", fonts.c FONT_MED_MAP) has no lowercase 'm', so a
 * literal "<dist_m> m" in FONT_MED would draw the unit as a blank cell.
 *
 * The label is drawn in FONT_SMALL rather than the spec text's literal "four rows of FONT_MED"
 * reading: FONT_MED's glyph set is "0-9 : . - + A-Z" (fonts.c FONT_MED_MAP, 40 glyphs) — no '/'
 * and no lowercase — so a FONT_MED "1/4" would render as "1", a blank cell, "4" (the '/' glyph is
 * unmapped, which fb_text draws as blank, per render.h) and a FONT_MED "60ft" would lose both
 * lowercase letters. FONT_SMALL is ASCII 32-126 (every gate-name character is present) and is the
 * same font LAP page 0 already uses for its row labels (LAP_LABEL_X) — this is the same kind of
 * evidence-based call as that screen's dS-row font override, just for missing glyphs rather than
 * vertical overflow. Confirmed by eyeballing: a FONT_MED render of "1/4"/"60ft" actually drew the
 * blank-cell gaps described above. */
static void render_drag_row(fb_t *fb, const drag_row_t *r, int y)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(r != NULL, UI_ASSERT_CODE);
    /* is_distance (the 100-0 braking gate, metres) reaches the layout only via pages 1/2's gate
     * grid, not the page-0 benches; the branch below is defensive and covered by the drag_p1/p2 goldens. */
    fb_text(fb, &FONT_SMALL, DRAG_LABEL_X, y, r->label);

    if (!r->present) {
        fb_text_right(fb, &FONT_MED, DRAG_TIME_RIGHT_X, y, DRAG_EMPTY_TIME);
    } else if (r->is_distance) {
        char  dbuf[16];
        char *p = dbuf;
        p = put_uint(p, r->dist_m);
        p = put_char(p, ' ');
        p = put_char(p, 'm');
        CORE_ASSERT_VOID((size_t)(p - dbuf) < sizeof dbuf, UI_ASSERT_CODE); /* room left for the NUL */
        *p = '\0';
        fb_text_right(fb, &FONT_SMALL, DRAG_TIME_RIGHT_X, y, dbuf);
    } else {
        char buf[TIME_BUF_LEN];
        fmt_time_ms(buf, r->t_ms);
        fb_text_right(fb, &FONT_MED, DRAG_TIME_RIGHT_X, y, buf);
    }

    if (r->has_trap) {
        char  tbuf[8];
        char *p = tbuf;
        p = put_char(p, '@');
        p = put_char(p, ' ');
        p = put_uint(p, r->trap_kmh);
        CORE_ASSERT_VOID((size_t)(p - tbuf) < sizeof tbuf, UI_ASSERT_CODE); /* room left for the NUL -- trap_kmh's worst case (5 digits) exactly fills tbuf */
        *p = '\0';
        fb_text(fb, &FONT_SMALL, DRAG_TRAP_X, y, tbuf);
    }
}

static void render_drag_page0(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    /* §11.4: rows are the SPEED_FROM0 benches that were hit, ascending by target, then the 1/4
     * row; max 4 rows, dropping the lowest (frontmost, since ascending) bench first if more than
     * 3 benches were hit. The caller (the ui task, session 4.3) fills m->drag[]/drag_n in that
     * order already — this renderer only applies the trim, identifying "the 1/4 row" by its label
     * (matching that ordering contract) rather than assuming a fixed slot, so it stays correct
     * (and does not underflow drag_n - 1) even for a model with zero rows. */
    uint8_t n = m->drag_n > DRAG_MAX_GATES ? (uint8_t)DRAG_MAX_GATES : m->drag_n;
    uint8_t quarter = (n > 0 && strcmp(m->drag[n - 1].label, "1/4") == 0) ? 1u : 0u;
    uint8_t bench_n = (uint8_t)(n - quarter);
    uint8_t start = 0;
    while (bench_n > 3u) {
        start++;
        bench_n--;
    }

    int row = 0;
    for (uint8_t i = start; i < n && row < DRAG0_MAX_ROWS; i++, row++) {
        render_drag_row(fb, &m->drag[i], DRAG0_ROW_Y0 + row * DRAG0_ROW_H);
    }

    if (m->drag_armed) {
        fb_text_right(fb, &FONT_SMALL, DRAG_ARMED_RIGHT_X, DRAG_ARMED_Y, "ARMED");
    }

    /* Mirrors LAP page 0 (the other primary in-ride screen): only the page-0 riding view shows the
     * fault strip, not the pages-1/2 review grids below. */
    fault_strip(fb, m->flags, m->batt_pct);
}

/* ---- DRAG pages 1/2 (spec §20.5): all seven gates of the last run / best-per-gate this session
 * — same row set (60ft, 330ft, 1/8, 1000ft, 1/4, 100-200, 100-0), just different values, so one
 * grid layout serves both; they differ only in the title drawn and in which values the caller
 * populated m->drag[] with. Laid out as a 2-column grid (7 rows do not fit one FONT_MED/FONT_SMALL
 * column within 128px; §20.5 leaves the exact grid unspecified beyond "FONT_MED/SMALL rows"),
 * echoing LAP page 1's sector grid: each cell is one "<label> <value>" FONT_SMALL string. ---- */

#define DRAG12_TITLE_Y 4
#define DRAG12_COLS    2
#define DRAG12_COL_X0  4
#define DRAG12_COL_W   148
#define DRAG12_ROW_Y0  20
#define DRAG12_ROW_H   16

static void render_drag_gate_grid(fb_t *fb, const screen_model_t *m, const char *title)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(title != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, DRAG12_TITLE_Y, title);

    uint8_t n = m->drag_n > DRAG_MAX_GATES ? (uint8_t)DRAG_MAX_GATES : m->drag_n;
    for (uint8_t i = 0; i < n; i++) {
        int col = i % DRAG12_COLS;
        int row = i / DRAG12_COLS;
        int x = DRAG12_COL_X0 + col * DRAG12_COL_W;
        int y = DRAG12_ROW_Y0 + row * DRAG12_ROW_H;

        char  tbuf[TIME_BUF_LEN];
        char  sbuf[40]; /* label (<=7 chars) + ' ' + value (<=~8 chars) + NUL, generous */
        char *p = sbuf;
        p = put_str(p, m->drag[i].label);
        p = put_char(p, ' ');
        if (!m->drag[i].present) {
            p = put_str(p, DRAG_EMPTY_TIME);
        } else if (m->drag[i].is_distance) {
            /* #40: the 100-0 braking gate is a distance, not a time -- see render_drag_row's
             * comment above for the same distinction (this grid is already all-FONT_SMALL, so no
             * font-fallback concern for the lowercase 'm' unit here). */
            p = put_uint(p, m->drag[i].dist_m);
            p = put_char(p, ' ');
            p = put_char(p, 'm');
        } else {
            fmt_time_ms(tbuf, m->drag[i].t_ms);
            p = put_str(p, tbuf);
        }
        CORE_ASSERT_VOID((size_t)(p - sbuf) < sizeof sbuf, UI_ASSERT_CODE); /* room left for the NUL */
        *p = '\0';
        fb_text(fb, &FONT_SMALL, x, y, sbuf);
    }
}

static void render_drag_page1(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    render_drag_gate_grid(fb, m, "LAST RUN");
}

static void render_drag_page2(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    render_drag_gate_grid(fb, m, "SESSION BEST");
}

/* ---- dispatch ---- */

/* Page dispatch for SCR_MODE_LAP, pulled out of screens_moto_render's switch (rule 4-compound:
 * keeps each switch body <= 30 code lines) -- pure code motion, byte-identical rendering. */
static void render_lap_dispatch(fb_t *fb, const screen_model_t *m)
{
    switch (m->page) {
    case 0:
        render_lap_page0(fb, m);
        break;
    case 1:
        render_lap_page1(fb, m);
        break;
    case 2:
        render_lap_page2(fb, m);
        break;
    default:
        break;
    }
}

/* Page dispatch for SCR_MODE_DRAG; see render_lap_dispatch's comment above. */
static void render_drag_dispatch(fb_t *fb, const screen_model_t *m)
{
    switch (m->page) {
    case 0:
        render_drag_page0(fb, m);
        break;
    case 1:
        render_drag_page1(fb, m);
        break;
    case 2:
        render_drag_page2(fb, m);
        break;
    default:
        break;
    }
}

void screens_moto_render(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_clear(fb, 0); /* white background before every full-screen render */

    switch (m->mode) {
    case SCR_MODE_LAP:
        render_lap_dispatch(fb, m);
        break;
    case SCR_MODE_DRAG:
        render_drag_dispatch(fb, m);
        break;
    default:
        break;
    }
}

/* ---- one-shot screens (spec §20.6) ---- */

/* Horizontal centering helper shared by the one-shot screens: the left x that centers `s` set in
 * font `f` within the framebuffer's width. Pure integer arithmetic; a string wider than the frame
 * (should not happen for the short one-shot captions used here) clamps to x=0 rather than going
 * negative -- fb_text clips off-frame draws safely either way, but a negative x would left-crop
 * the string instead of just running off the right edge. */
static int center_x(const fb_t *fb, const font_t *f, const char *s)
{
    CORE_ASSERT_RET(fb != NULL, UI_ASSERT_CODE, 0);
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, 0);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, 0);
    int w = (int)strlen(s) * (int)f->w;
    int x = ((int)fb->w - w) / 2;
    return x < 0 ? 0 : x;
}

static const char ONESHOT_VENUE_LABEL[]     = "VENUE";
static const char ONESHOT_LAYOUT_LABEL[]    = "LAYOUT";
static const char ONESHOT_SAFE_TEXT[]       = "SAFE MODE";
static const char ONESHOT_LOWBATT_TITLE[]   = "LOW BATT";
static const char ONESHOT_OTA_TITLE[]       = "UPDATING";
static const char ONESHOT_OTAFAIL_LINE1[]   = "UPDATE FAILED";
static const char ONESHOT_OTAFAIL_LINE2[]   = "REVERTED";
static const char ONESHOT_CALIBRATE_TITLE[] = "CALIBRATE";
static const char ONESHOT_CALIBRATE_SUB[]   = "Hold upright, press MODE";
static const char ONESHOT_NEWTRACK_TITLE[]  = "NEW TRACK";
static const char ONESHOT_NEWTRACK_SUB[]    = "Cross S/F, press MODE";

#define BOOT_NAME_Y    6
#define BOOT_VER_Y     34
#define BOOT_LINE_Y0   56
#define BOOT_LINE_H    16
#define BOOT_MAX_LINES 4

/* BOOT (§20.6, §17.6): name + version banner, then up to 4 pre-formatted self-test lines
 * ("<check>  OK"/"FAIL", caller's job to pad/format -- boot_line is plain text, not a table). */
static void render_oneshot_boot(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, m->boot_name), BOOT_NAME_Y, m->boot_name);
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, m->boot_ver), BOOT_VER_Y, m->boot_ver);

    uint8_t n = m->boot_n_lines > BOOT_MAX_LINES ? (uint8_t)BOOT_MAX_LINES : m->boot_n_lines;
    for (uint8_t i = 0; i < n; i++) {
        fb_text(fb, &FONT_SMALL, LAP_LABEL_X, BOOT_LINE_Y0 + (int)i * BOOT_LINE_H, m->boot_line[i]);
    }
}

#define VENUE_LABEL_Y 40
#define VENUE_VALUE_Y 62

/* VENUE (§20.6): "venue name big, then layout name" -- the ui task (session 4.3 Task 2) shows
 * this one-shot twice, once on EV_VENUE_FOUND and again on EV_LAYOUT_LOCKED, each a pure render of
 * a model with only the relevant field populated. The caller sets which phase this render is by
 * whether layout_name is populated: non-empty means "layout locked" (show layout_name), empty
 * means "venue found" (show venue_name). */
static void render_oneshot_venue(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    const char *label = ONESHOT_VENUE_LABEL;
    const char *value = m->venue_name;
    if (m->layout_name[0] != '\0') {
        label = ONESHOT_LAYOUT_LABEL;
        value = m->layout_name;
    }
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, label), VENUE_LABEL_Y, label);
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, value), VENUE_VALUE_Y, value);
}

#define SAFE_TEXT_Y 52

static void render_oneshot_safe(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    (void)m;
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_SAFE_TEXT), SAFE_TEXT_Y,
             ONESHOT_SAFE_TEXT);
}

#define LOWBATT_TITLE_Y 32
#define LOWBATT_PCT_Y   68

static void render_oneshot_lowbatt(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_LOWBATT_TITLE), LOWBATT_TITLE_Y,
             ONESHOT_LOWBATT_TITLE);

    char     buf[8];
    char    *p = buf;
    uint8_t  pct = m->batt_pct > 100u ? 100u : m->batt_pct;
    p = put_uint(p, pct);
    p = put_char(p, '%');
    CORE_ASSERT_VOID((size_t)(p - buf) < sizeof buf, UI_ASSERT_CODE); /* room left for the NUL */
    *p = '\0';
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, buf), LOWBATT_PCT_Y, buf);
}

#define OTA_TITLE_Y 16
#define OTA_BAR_X   48
#define OTA_BAR_Y   56
#define OTA_BAR_W   200
#define OTA_BAR_H   20
#define OTA_PCT_Y   84

static void render_oneshot_ota(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_OTA_TITLE), OTA_TITLE_Y,
             ONESHOT_OTA_TITLE);

    uint8_t pct = m->ota_pct > 100u ? 100u : m->ota_pct;
    fb_bar(fb, OTA_BAR_X, OTA_BAR_Y, OTA_BAR_W, OTA_BAR_H, pct);

    char  buf[8];
    char *p = buf;
    p = put_uint(p, pct);
    p = put_char(p, '%');
    CORE_ASSERT_VOID((size_t)(p - buf) < sizeof buf, UI_ASSERT_CODE); /* room left for the NUL */
    *p = '\0';
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, buf), OTA_PCT_Y, buf);
}

#define OTAFAIL_LINE1_Y 36
#define OTAFAIL_LINE2_Y 68

static void render_oneshot_ota_fail(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    (void)m;
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_OTAFAIL_LINE1), OTAFAIL_LINE1_Y,
             ONESHOT_OTAFAIL_LINE1);
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_OTAFAIL_LINE2), OTAFAIL_LINE2_Y,
             ONESHOT_OTAFAIL_LINE2);
}

#define CALIBRATE_TITLE_Y 28
#define CALIBRATE_SUB_Y   68

static void render_oneshot_calibrate(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    (void)m;
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_CALIBRATE_TITLE), CALIBRATE_TITLE_Y,
             ONESHOT_CALIBRATE_TITLE);
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, ONESHOT_CALIBRATE_SUB), CALIBRATE_SUB_Y,
             ONESHOT_CALIBRATE_SUB);
}

#define NEWTRACK_TITLE_Y 28
#define NEWTRACK_SUB_Y   68

static void render_oneshot_newtrack(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    (void)m;
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_NEWTRACK_TITLE), NEWTRACK_TITLE_Y,
             ONESHOT_NEWTRACK_TITLE);
    fb_text(fb, &FONT_SMALL, center_x(fb, &FONT_SMALL, ONESHOT_NEWTRACK_SUB), NEWTRACK_SUB_Y,
             ONESHOT_NEWTRACK_SUB);
}

static void render_oneshot(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_clear(fb, 0);

    switch (m->oneshot) {
    case ONESHOT_BOOT:
        render_oneshot_boot(fb, m);
        break;
    case ONESHOT_VENUE:
        render_oneshot_venue(fb, m);
        break;
    case ONESHOT_SAFE:
        render_oneshot_safe(fb, m);
        break;
    case ONESHOT_LOWBATT:
        render_oneshot_lowbatt(fb, m);
        break;
    case ONESHOT_OTA:
        render_oneshot_ota(fb, m);
        break;
    case ONESHOT_OTA_FAIL:
        render_oneshot_ota_fail(fb, m);
        break;
    case ONESHOT_CALIBRATE:
        render_oneshot_calibrate(fb, m);
        break;
    case ONESHOT_NEWTRACK:
        render_oneshot_newtrack(fb, m);
        break;
    default:
        break;
    }
}

/* ---- menu (spec §20.7) ---- */

#define MENU_TITLE_Y      2
#define MENU_SEP_Y        26
#define MENU_LIST_Y0      30
#define MENU_ROW_H        24
#define MENU_VISIBLE_ROWS 4
#define MENU_MARKER_X     4
#define MENU_ITEM_X       18
#define MENU_ITEM_MAX     12

/* True when every character of `s` has a glyph in FONT_MED (a space is also accepted: unmapped
 * chars -- including space -- draw a blank cell in any font, per render.h, so a space "fits" any
 * font visually even though it has no glyph index). A menu caption with '/' or lowercase (most of
 * §20.7's own item list, e.g. "Mode: Lap / Drag") fails this and falls back to FONT_SMALL -- same
 * missing-glyph reasoning as the DRAG row label above (render_drag_row). */
static bool item_fits_font_med(const char *s)
{
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, false);
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == ' ') {
            continue;
        }
        if (font_glyph_index(&FONT_MED, *p) < 0) {
            return false;
        }
    }
    return true;
}

/* Titled, scrolled list of m->menu_items[0..menu_n): the selected row (menu_sel) is marked with a
 * ">" in a fixed-width gutter (FONT_SMALL, so the marker itself never depends on the item's own
 * font choice); menu_top is the caller-driven first visible row (the ui task, session 4.3 Task 2,
 * keeps it scrolled so menu_sel is always visible -- this renderer trusts menu_top as given,
 * mirroring how every other screen here trusts its model rather than re-deriving it). Each row's
 * item text uses FONT_MED when it fits (item_fits_font_med), else FONT_SMALL, vertically centered
 * within the MENU_ROW_H slot either way. */
static void render_menu(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_clear(fb, 0);

    fb_text(fb, &FONT_MED, LAP_LABEL_X, MENU_TITLE_Y, "MENU");
    fb_hline(fb, 0, MENU_SEP_Y, (int)fb->w, 1);

    uint8_t n = m->menu_n > MENU_ITEM_MAX ? (uint8_t)MENU_ITEM_MAX : m->menu_n;
    for (uint8_t row = 0; row < MENU_VISIBLE_ROWS; row++) {
        uint8_t idx = (uint8_t)(m->menu_top + row);
        if (idx >= n) {
            break;
        }
        const char *item = m->menu_items[idx];
        if (item == NULL) {
            continue;
        }
        int y = MENU_LIST_Y0 + (int)row * MENU_ROW_H;

        if (idx == m->menu_sel) {
            fb_text(fb, &FONT_SMALL, MENU_MARKER_X, y + (MENU_ROW_H - FONT_SMALL.h) / 2, ">");
        }

        if (item_fits_font_med(item)) {
            fb_text(fb, &FONT_MED, MENU_ITEM_X, y, item);
        } else {
            fb_text(fb, &FONT_SMALL, MENU_ITEM_X, y + (MENU_ROW_H - FONT_SMALL.h) / 2, item);
        }
    }
}

/* ---- top-level dispatch (spec §20.6-20.7) ---- */

void screens_render(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    switch (m->screen) {
    case SCR_MENU:
        render_menu(fb, m);
        break;
    case SCR_ONESHOT:
        render_oneshot(fb, m);
        break;
    case SCR_RIDING:
    default:
        screens_moto_render(fb, m);
        break;
    }
}
