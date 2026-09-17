/* Moto riding screens: LAP pages 0/1/2 (spec §20.4-20.5) + the shared fault-icon strip
 * (§20.5 + §17.4). Pure C11, no malloc/float/libm — every layout position is a compile-time
 * constant and every value is formatted with plain integer arithmetic, so screens_moto_render()
 * is a pure function of its screen_model_t and the PBM goldens in test/snapshots/lap_*.pbm are
 * byte-identical across clang and gcc-16.
 *
 * DRAG (mode SCR_MODE_DRAG, §20.5's DRAG pages + the §11.4 benches rule) is Task 2's seam: see
 * screens_moto_render()'s switch below.
 */
#include "core/ui/model.h"

#include <string.h>

/* ---- tiny pure-integer string helpers (no snprintf/stdio: core/ui stays free of <stdio.h>,
 * mirroring render.c's <string.h>-only policy) ---- */

static char *put_char(char *p, char c)
{
    *p++ = c;
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

/* Decimal, no leading zeros (0 itself renders as "0"). */
static char *put_uint(char *p, unsigned v)
{
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
    *p = '\0';
}

/* Signed S.cc (seconds.centiseconds, no minutes, always shows a sign), e.g. -210 -> "-0.21",
 * +340 -> "+0.34". `buf` must be >= DELTA_BUF_LEN bytes. Handles INT32_MIN without UB (negates via
 * unsigned arithmetic rather than `-dms`). */
static void fmt_delta_ms(char *buf, int32_t dms)
{
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
#define LAP2_ROW_Y(n) (8 + (n) * LAP2_ROW_H)

static void render_lap_page2(fb_t *fb, const screen_model_t *m)
{
    char  buf[48];
    char *p;

    p = buf;
    p = put_str(p, "MAX SPD ");
    p = put_uint(p, m->max_speed_kmh);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP2_ROW_Y(0), buf);

    p = buf;
    p = put_str(p, "LEAN L ");
    p = put_uint(p, m->lean_l_deg);
    p = put_str(p, " R ");
    p = put_uint(p, m->lean_r_deg);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP2_ROW_Y(1), buf);

    p = buf;
    p = put_str(p, "LAT G ");
    p = put_g_e2(p, m->lat_g_e2);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP2_ROW_Y(2), buf);

    p = buf;
    p = put_str(p, "ACC ");
    p = put_g_e2(p, m->acc_g_e2);
    p = put_str(p, " BRK ");
    p = put_g_e2(p, m->brk_g_e2);
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP2_ROW_Y(3), buf);

    p = buf;
    p = put_str(p, "LAPS ");
    p = put_uint(p, m->laps_total);
    p = put_str(p, " (");
    p = put_uint(p, m->laps_valid);
    p = put_str(p, " valid)");
    *p = '\0';
    fb_text(fb, &FONT_SMALL, LAP_LABEL_X, LAP2_ROW_Y(4), buf);
}

/* ---- dispatch ---- */

void screens_moto_render(fb_t *fb, const screen_model_t *m)
{
    fb_clear(fb, 0); /* white background before every full-screen render */

    switch (m->mode) {
    case SCR_MODE_LAP:
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
        break;
    case SCR_MODE_DRAG:
        /* Task 2 seam: DRAG pages 0 (benches, §11.4)/1 (all gates)/2 (best per gate) land here as
         * render_drag_page0/1/2(fb, m), reusing fault_strip()/fmt_time_ms() above. Until then, a
         * DRAG-mode call renders an empty (cleared) frame. */
        break;
    default:
        break;
    }
}
