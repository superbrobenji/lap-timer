/* Moto riding screens: LAP pages 0/1/2 and DRAG pages 0/1/2 (spec §20.4-20.5) + the §11.4 benches
 * rule + the shared fault-icon strip (§20.5 + §17.4); plus the one-shot screens (§20.6) and the
 * menu list (§20.7) and the top-level screens_render() dispatch (session 4.3). Pure C11, no
 * malloc/float/libm — every layout position is a compile-time constant and every value is
 * formatted with plain integer arithmetic, so every render function here is a pure function of its
 * screen_model_t and the PBM goldens in test/snapshots/ are byte-identical across clang and
 * gcc-16.
 */
#include "core/ui/canvas.h"
#include "core/ui/model.h"
#include "core/ui/units.h"
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
 * row's own label at its left margin — confirmed by eyeballing an 8-char "--:--.--" placeholder,
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

/* Unsigned SS.cc below 100 s, SSS.c from 100 s -- the LAP page 1 sector board's fixed-width value
 * cell (spec 7b §5): <= 5 glyphs either way, matching BOARD_COL_W (canvas.h). Ruling T2-R2: this
 * was specified by the Task 2 brief but not added there (it would have been an unused static under
 * -Werror with no caller yet); Task 3's render_lap_page1 is its first caller. */
static void fmt_secs_ms(char *buf, uint32_t ms)
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    unsigned s = (unsigned)(ms / 1000u);
    char    *p = buf;
    if (s < 100u) {
        unsigned cs = (unsigned)((ms / 10u) % 100u);
        p = put_uint(p, s);
        p = put_char(p, '.');
        if (cs < 10u) {
            p = put_char(p, '0');
        }
        p = put_uint(p, cs);
    } else {
        if (s > 999u) {
            s = 999u;
        }
        p = put_uint(p, s);
        p = put_char(p, '.');
        p = put_uint(p, (unsigned)((ms / 100u) % 10u));
    }
    *p = '\0';
    CORE_ASSERT_VOID(strlen(buf) <= 5u, UI_ASSERT_CODE);
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

/* Shared by fault_strip() and fault_strip_left_x() (PF-4): the icon (icons.h) for `bit`
 * (SCR_SYS_*, model.h), or -1 if that bit draws no icon. The lookup itself lives in the one table
 * above -- this wrapper is what makes "both functions use it" a compile-time fact rather than a
 * convention two call sites could quietly drift apart on. */
static int fault_icon_for_bit(int bit)
{
    CORE_ASSERT_RET(bit >= 0, UI_ASSERT_CODE, -1);
    CORE_ASSERT_RET(bit < 14, UI_ASSERT_CODE, -1);
    return (int)FAULT_ICON_FOR_BIT[bit];
}

/* Strip anchor (spec §20.5: "x descending from ~284, y≈116" on the 296x128 frame; scaled per canvas
 * in core/ui/canvas.h): the rightmost icon's top-left, chosen so x0 + ICON_W(12) and y + ICON_H(12)
 * land flush with the frame's true visible edge on either canvas. */

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
        int icon = fault_icon_for_bit(bit);
        if (icon < 0) {
            continue;
        }
        /* The icon always occupies its own slot at the current x, drawn first so the SYS_BATT_LOW
         * label below is positioned relative to it without the two overlapping (Plan 7b final fix
         * 1, finding 17: the label used to draw at this same x only after the icon's slot had
         * already been shifted left by the label's own width, so the icon's opaque blit clobbered
         * the label's first ICON_W columns). Layout picked (label immediately left of the icon,
         * not right of it) matches fault_strip_left_x()'s existing math below, which already
         * reserves the label's width before the icon's own slot -- so that function's "leftmost x"
         * contract needed no change. */
        fb_icon(fb, (uint8_t)icon, x, y);
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
            fb_text(fb, &FONT_SMALL, x - text_w - 1, text_y, pct_buf); /* immediately left of the icon */
            x -= text_w + 1; /* reserve the label's width too before the next bit's pitch step */
        }
        x -= pitch;
    }
}

/* x of the leftmost icon fault_strip() will draw for `flags`, or CANVAS_VISIBLE_W when it draws
 * none. Walks the same bit order / pitch stepping as fault_strip() above, through the same
 * fault_icon_for_bit() table (PF-4), so the two can never disagree about which bits draw. Unlike
 * fault_strip(), this takes no batt_pct -- so for SYS_BATT_LOW it reserves the widest "%<pct>"
 * label fault_strip() could ever draw ("100%", 4 glyphs) rather than the caller's actual
 * percentage. That makes the x this returns always <= fault_strip()'s real left edge for any
 * batt_pct: a caller clearing its own content of that x never overlaps the strip, even though on
 * a lower (1-2 digit) percentage it reserves a little more margin than fault_strip() actually
 * needs.
 * Plan 7b T2 fix 1 (ruling T2-R1): the LAP page 0 event-card footer (render_card_footer, below)
 * no longer calls this -- CARD_LABEL_Y/CARD_VALUE_Y (canvas.h) were moved above FAULT_STRIP_Y
 * instead, so the footer's row band and the fault strip's row band no longer share any y, and no
 * x-retraction is needed there at all. Only caller is render_dcard_footer (PF-2, below), so this
 * stays file-static. */
static int fault_strip_left_x(uint32_t flags)
{
    const int pitch = ICON_W + 2;
    int       x = FAULT_STRIP_X0;
    int       left = CANVAS_VISIBLE_W;
    CORE_ASSERT_RET(FAULT_STRIP_X0 + ICON_W <= CANVAS_VISIBLE_W, UI_ASSERT_CODE, CANVAS_VISIBLE_W); /* Plan 7b T3 fix 1: the strip's own anchor stays on-canvas -- a real precondition, replacing the tautological loop-index check the for-condition already guarantees */

    for (int bit = 0; bit < 14; bit++) {
        if ((flags & (1u << bit)) == 0u) {
            continue;
        }
        if (fault_icon_for_bit(bit) < 0) {
            continue;
        }
        if (bit == SCR_SYS_BATT_LOW) {
            x -= 4 * (int)FONT_SMALL.w + 1; /* worst case: "100%" */
        }
        left = x;
        x -= pitch;
    }
    CORE_ASSERT_RET(left >= 0 && left <= CANVAS_VISIBLE_W, UI_ASSERT_CODE, CANVAS_VISIBLE_W); /* m3: the returned x never lands outside the visible frame */
    return left;
}

/* ---- LAP page 0 (spec 7b §3-4): the event card -- 64 px delta, LAST/BEST footer, lap/sector
 * marker, inverted BEST tag, fault strip ---- */

/* Signed delta (fmt_delta_ms's "+S.cc"/"-S.cc") with |value| clamped to `max_ms` first, so an
 * out-of-range delta (e.g. a multi-lap gap after a pit stop) still fits FONT_HUGE's fixed 6-glyph
 * budget ("+99.99") instead of growing past it. `buf` must be >= DELTA_BUF_LEN bytes. */
static void fmt_delta_clamped(char *buf, int32_t dms, int32_t max_ms)
{
    CORE_ASSERT_VOID(buf != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(max_ms > 0 && max_ms <= CARD_DELTA_CLAMP_MS, UI_ASSERT_CODE);
    int32_t d = dms;
    if (d > max_ms) {
        d = max_ms;
    }
    if (d < -max_ms) {
        d = -max_ms;
    }
    fmt_delta_ms(buf, d);
    CORE_ASSERT_VOID(strlen(buf) <= 6u, UI_ASSERT_CODE); /* sign + up to "99.99": never wider than the 6-glyph budget */
}

/* Renders the 64 px big slot (FONT_HUGE): BIG_SECTOR_DELTA/BIG_LAP_DELTA show the signed delta,
 * clamped to CARD_DELTA_CLAMP_MS; BIG_NONE (no best lap yet) falls back to "LAP n" in FONT_MED,
 * since FONT_HUGE has no letters. Returns the pen x after the drawn text (CARD_BIG_X itself for
 * the BIG_NONE case, since the caller only uses this to size the BEST tag / marker-collision
 * check against a delta). */
static int render_card_big(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_RET(fb != NULL, UI_ASSERT_CODE, CARD_BIG_X);
    CORE_ASSERT_RET(m != NULL, UI_ASSERT_CODE, CARD_BIG_X);
    if (m->big_kind == BIG_NONE) {
        char  buf[16];
        char *p = buf;
        p = put_str(p, "LAP ");
        p = put_uint(p, m->lap_no);
        CORE_ASSERT_RET((size_t)(p - buf) < sizeof buf, UI_ASSERT_CODE, CARD_BIG_X); /* room left for the NUL */
        *p = '\0';
        return fb_text(fb, &FONT_MED, CARD_BIG_X, CARD_NONE_Y, buf);
    }
    char dbuf[DELTA_BUF_LEN];
    fmt_delta_clamped(dbuf, m->big_delta_ms, CARD_DELTA_CLAMP_MS);
    return fb_text(fb, &FONT_HUGE, CARD_BIG_X, CARD_BIG_Y, dbuf);
}

/* Top-right marker "L<lap_no> S<cur_sector_idx>" (FONT_SMALL, right-aligned): sits above the huge
 * digits' ink line, so it never collides with the big slot whatever the delta's width. */
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
    CORE_ASSERT_VOID((size_t)(p - buf) < sizeof buf, UI_ASSERT_CODE); /* room left for the NUL */
    *p = '\0';
    fb_text_right(fb, &FONT_SMALL, CARD_MARKER_RIGHT_X, CARD_MARKER_Y, buf);
}

/* Inverted "BEST" tag (spec 7b §4): white FONT_SMALL text on a solid black CARD_TAG_W x
 * CARD_TAG_H box, drawn either beside the big number or over the marker row (render_lap_page0
 * picks x/y for each case). */
static void render_card_tag(fb_t *fb, int x, int y)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(x >= 0 && y >= 0, UI_ASSERT_CODE);
    fb_rect(fb, x, y, CARD_TAG_W, CARD_TAG_H, 1, true);
    fb_text_inv(fb, &FONT_SMALL, x + CARD_TAG_PAD_X, y + CARD_TAG_PAD_Y, "BEST");
}

/* Footer: "LAST"/"BEST" labels (FONT_SMALL) over their right-aligned FONT_MED values, both fixed
 * columns (spec 7b §4). Plan 7b T2 fix 1 (ruling T2-R1): CARD_LABEL_Y/CARD_VALUE_Y now sit the
 * whole footer row band above FAULT_STRIP_Y on both canvases (canvas.h), so BEST no longer needs
 * to retract on x to clear the fault strip -- it always right-aligns at CARD_RIGHT_RIGHT_X, same
 * as LAST always right-aligns at CARD_LEFT_RIGHT_X. The assertion below is the geometry proof
 * that backs this: LAST's value ends at CARD_LEFT_RIGHT_X and BEST's (7-glyph FONT_MED, the
 * widest EMPTY_TIME/fmt_time_ms ever produces for these fields) starts no further left than
 * CARD_RIGHT_RIGHT_X - 7*FONT_MED.w, so the two columns cannot meet on either canvas. */
static void render_card_footer(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(CARD_RIGHT_RIGHT_X - 7 * (int)FONT_MED.w >= CARD_LEFT_RIGHT_X, UI_ASSERT_CODE);
    char buf[TIME_BUF_LEN];

    fb_text(fb, &FONT_SMALL, CARD_LEFT_LABEL_X, CARD_LABEL_Y, "LAST");
    if (m->have_prev) {
        fmt_time_ms(buf, m->prev_ms);
    } else {
        char *p = put_str(buf, EMPTY_TIME);
        *p = '\0';
    }
    fb_text_right(fb, &FONT_MED, CARD_LEFT_RIGHT_X, CARD_VALUE_Y, buf);

    fb_text(fb, &FONT_SMALL, CARD_RIGHT_LABEL_X, CARD_LABEL_Y, "BEST");
    if (m->have_best) {
        fmt_time_ms(buf, m->best_ms);
    } else {
        char *p = put_str(buf, EMPTY_TIME);
        *p = '\0';
    }
    fb_text_right(fb, &FONT_MED, CARD_RIGHT_RIGHT_X, CARD_VALUE_Y, buf);
}

/* The LAP page 0 event card (spec 7b §4): the 64 px big slot top-left, the lap/sector marker
 * top-right (or, when a six-glyph delta leaves no room for the BEST tag beside it, the tag takes
 * the marker's row instead), the LAST/BEST footer, and the fault strip drawn last as always. */
static void render_lap_page0(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    int  end_x = render_card_big(fb, m);
    /* Ruling FR-4: whether the BEST tag fits beside the big number is a real geometry question
     * (does the tag box, plus its gap, still land left of the marker's right edge?), not a glyph
     * count -- a fixed "more than 5 glyphs" threshold was wrong on the 2.9" canvas, where a
     * six-glyph delta still leaves room beside it (CARD_MARKER_RIGHT_X is wider there). */
    bool wide = end_x + CARD_TAG_GAP + CARD_TAG_W > CARD_MARKER_RIGHT_X;
    if (m->new_best && wide) {
        render_card_tag(fb, CARD_TAG_ALT_X, CARD_MARKER_Y); /* replaces the marker this lap */
    } else {
        render_card_marker(fb, m);
        if (m->new_best) {
            render_card_tag(fb, end_x + CARD_TAG_GAP, CARD_TAG_Y);
        }
    }
    render_card_footer(fb, m);
    fault_strip(fb, m->flags, m->batt_pct);
}

/* ---- LAP page 1 (spec 7b §5): sector board -- BEST LAP/THEO header, three columns of
 * (label, best-lap sector time, last-lap sector delta) ---- */

static void render_board_header(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[TIME_BUF_LEN + 12];
    char  t[TIME_BUF_LEN];
    char *p = put_str(buf, "BEST LAP ");
    if (m->have_best) { fmt_time_ms(t, m->best_ms); p = put_str(p, t); } else { p = put_str(p, EMPTY_TIME); }
    *p = '\0';
    fb_text(fb, &FONT_SMALL, BOARD_X0, BOARD_HEADER_Y, buf);
    p = put_str(buf, "THEO ");
    if (m->have_theo) { fmt_time_ms(t, m->theo_best_ms); p = put_str(p, t); } else { p = put_str(p, EMPTY_TIME); }
    *p = '\0';
    fb_text_right(fb, &FONT_SMALL, CANVAS_VISIBLE_W - BOARD_X0, BOARD_HEADER_Y, buf);
}

/* Three columns of BOARD_COL_W px each (spec 7b §5): S1/S2/S3 labels (S3 gains a "+n" suffix when
 * best_n_sectors runs past three columns, up to LAP_MAX_SECTORS+1), the best-lap sector time below
 * each label, and the last-lap sector delta (clamped, "----" when the current lap has not reached
 * that sector yet) on the bottom row. */
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
        /* Plan 7c T3 (design §2): a sector's best time shows only once the pipeline has one on
         * record (have_best_sector[i]) -- i < n alone is not enough, since best_n_sectors reports
         * the locked layout's split count before every sector has completed a valid lap yet. */
        if (i < n && m->have_best_sector[i]) { fmt_secs_ms(buf, m->best_sector_ms[i]); } else { p = put_str(buf, "--.--"); *p = '\0'; }
        fb_text(fb, &FONT_MED, x, BOARD_VALUE_Y, buf);
        /* Ruling FR-2: the delta row depends only on have_last_sector_delta[i] (spec §5 literal),
         * never on best_n_sectors -- ui.c's copy_best_snapshot() (Plan 7c T3, #79) is the producer
         * that fills best_n_sectors now, but the delta row is a different signal (this lap's live
         * sector splits vs. the locked layout's best-sector count) and must not be gated on it: a
         * lap can cross sector i before the pipeline has ever recorded a best time for it. */
        if (m->have_last_sector_delta[i]) { fmt_delta_clamped(buf, m->last_sector_delta_ms[i], BOARD_DELTA_CLAMP_MS); }
        else { p = put_str(buf, "----"); *p = '\0'; }
        fb_text(fb, &FONT_MED, x, BOARD_DELTA_Y, buf);
    }
}

/* ---- LAP page 2 (spec 7b §6): 2x2 stats grid ---- */

static void grid_cell(fb_t *fb, int x, int ly, int vy, const char *label, const char *value)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(label != NULL && value != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, x, ly, label);
    fb_text(fb, &FONT_MED, x, vy, value);
}

/* LAPS cell (spec 7b §6), split out of render_lap_page2 (rule 4: keeps that function's line count
 * down) because its "(N valid)" suffix needs its own degrade-by-width logic (ruling T3-R2): unlike
 * every other grid value, its width depends on laps_valid's digit count, so a caller with a large
 * laps_total/laps_valid (e.g. a long track day, 100+ laps) could otherwise push the suffix past
 * CANVAS_VISIBLE_W on the narrower 213 canvas. Draws "(N valid)" when that fits within
 * CANVAS_VISIBLE_W - GRID_COL1_X (the same margin the grid's left column keeps), else the shorter
 * "(N)" when that fits, else nothing. */
static void render_grid_laps(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[24];
    char *p = put_uint(buf, m->laps_total); *p = '\0';
    grid_cell(fb, GRID_COL2_X, GRID_LABEL_Y1, GRID_VALUE_Y1, "LAPS", buf);
    int end   = GRID_COL2_X + (int)strlen(buf) * FONT_MED.w;
    int start = end + GRID_SUB_GAP;
    int limit = CANVAS_VISIBLE_W - GRID_COL1_X;

    p = put_str(buf, "("); p = put_uint(p, m->laps_valid); p = put_str(p, " valid)"); *p = '\0';
    if (start + (int)strlen(buf) * FONT_SMALL.w > limit) {
        p = put_str(buf, "("); p = put_uint(p, m->laps_valid); p = put_str(p, ")"); *p = '\0';
        if (start + (int)strlen(buf) * FONT_SMALL.w > limit) {
            buf[0] = '\0'; /* neither suffix fits: draw nothing rather than clip past the visible edge */
        }
    }
    if (buf[0] != '\0') {
        CORE_ASSERT_VOID(start + (int)strlen(buf) * FONT_SMALL.w <= limit, UI_ASSERT_CODE);
        fb_text(fb, &FONT_SMALL, start, GRID_VALUE_Y1 + GRID_SUB_DY, buf);
    }
}

static void render_lap_page2(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    char  buf[48];
    char  label[16];
    char *p;
    /* Plan 7c T4 (design §3): max_speed_cms is the raw session-max the pipeline folds; the display
     * unit conversion happens here, at render, using the model's units toggle. The label carries
     * the unit too (grid_cell already draws labels in FONT_SMALL, which has the lowercase glyphs
     * "km/h"/"mph" need -- FONT_MED has none). */
    p = put_uint(buf, speed_display(m->max_speed_cms, m->units)); *p = '\0';
    char *lp = put_str(label, "MAX SPD "); lp = put_str(lp, m->units ? "mph" : "km/h"); *lp = '\0';
    grid_cell(fb, GRID_COL1_X, GRID_LABEL_Y0, GRID_VALUE_Y0, label, buf);
    /* A lean angle cannot exceed 90 deg, but lean_l_deg/lean_r_deg are plain uint8_t -- clamp each
     * to 99 before formatting so "L99 R99" is provably the widest this cell ever draws. */
    uint8_t lean_l = m->lean_l_deg > 99u ? 99u : m->lean_l_deg;
    uint8_t lean_r = m->lean_r_deg > 99u ? 99u : m->lean_r_deg;
    p = put_char(buf, 'L'); p = put_uint(p, lean_l); p = put_char(p, ' '); p = put_char(p, 'R'); p = put_uint(p, lean_r); *p = '\0';
    grid_cell(fb, GRID_COL2_X, GRID_LABEL_Y0, GRID_VALUE_Y0, "LEAN L/R", buf);   /* PF-3: FONT_SMALL has '/', FONT_MED does not, hence L52 R55 */
    p = put_g_e2(buf, m->lat_g_e2); *p = '\0';
    grid_cell(fb, GRID_COL1_X, GRID_LABEL_Y1, GRID_VALUE_Y1, "LAT G", buf);
    render_grid_laps(fb, m);
    p = put_str(buf, "ACC "); p = put_g_e2(p, m->acc_g_e2); p = put_str(p, "   BRK "); p = put_g_e2(p, m->brk_g_e2); *p = '\0';
    fb_text(fb, &FONT_SMALL, GRID_COL1_X, GRID_FOOTER_Y, buf);
}

/* ---- DRAG page 0 (spec 7b §7): the run card -- the newest gate's value fills the 64 px big
 * slot, the earlier gates of this run join a FONT_SMALL footer in hit order, and ARMED/the fault
 * strip sit as before. Before the first gate the big slot shows "READY" (FONT_MED: FONT_HUGE has
 * no letters at all). ---- */

/* The newest gate's big value (spec 7b §7): a normal gate's elapsed time in FONT_HUGE
 * (fmt_secs_ms, <= 5 glyphs), or -- for the 100-0 braking gate (#40: DRAG_BRAKE, core/drag.h, is a
 * stopping DISTANCE in metres, not an elapsed time) -- the distance as a plain integer in
 * FONT_HUGE followed by a FONT_SMALL "m" (FONT_HUGE has no lowercase, so the unit itself must use
 * a different font). A gate with has_trap set also draws its trap speed as "@<trap_speed>" on the
 * row below in FONT_MED -- FONT_MED has no '@' glyph (fonts.c FONT_MED_MAP), so that leading
 * character draws as a blank cell per render.h's no-glyph contract; spec 7b §7's own mock shows
 * "@173" this way. Plan 7c T4 (design §3): trap_speed is already in the display unit (filled by
 * the ui, never converted here); a FONT_SMALL "km/h"/"mph" suffix follows the digits. */
static void render_dcard_value(fb_t *fb, const drag_row_t *r, uint8_t units)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(r != NULL, UI_ASSERT_CODE);
    char buf[TIME_BUF_LEN];
    if (r->is_distance) {
        char *p = put_uint(buf, r->dist_m);
        *p = '\0';
        int end = fb_text(fb, &FONT_HUGE, DCARD_BIG_X, DCARD_BIG_Y, buf);
        fb_text(fb, &FONT_SMALL, end + DCARD_UNIT_GAP, DCARD_BIG_Y + DCARD_UNIT_DY, "m");
        return; /* a distance gate never draws a trap row, even if has_trap happened to be set */
    }
    fmt_secs_ms(buf, r->t_ms);
    fb_text(fb, &FONT_HUGE, DCARD_BIG_X, DCARD_BIG_Y, buf);
    if (r->has_trap) {
        /* T4-R1: FONT_MED has no '@' glyph (fonts.c FONT_MED_MAP), so '@' is drawn in FONT_SMALL,
         * hugging the FONT_MED speed digits' baseline, and the speed itself (no '@') in FONT_MED
         * beside it. */
        fb_text(fb, &FONT_SMALL, DCARD_LABEL_X, DCARD_SPEED_Y + DCARD_AT_DY, "@");
        char *p = put_uint(buf, r->trap_speed);
        *p = '\0';
        int end = fb_text(fb, &FONT_MED, DCARD_LABEL_X + (int)FONT_SMALL.w + DCARD_AT_GAP, DCARD_SPEED_Y, buf);
        /* Same FONT_SMALL baseline as the "@" above (DCARD_SPEED_Y + DCARD_AT_DY, T4-R1). */
        fb_text(fb, &FONT_SMALL, end + DCARD_SPEED_UNIT_GAP, DCARD_SPEED_Y + DCARD_AT_DY, units ? "mph" : "km/h");
    }
}

/* The run-card footer (spec 7b §7): gates 0..n-2 (every gate of this run except the newest, which
 * already fills the big slot), oldest first, up to DCARD_FOOTER_MAX entries -- older ones scroll
 * off the left. Each entry is "<label> <value>" (a time via fmt_secs_ms, or "<dist_m>m" for the
 * distance gate), joined by DCARD_FOOTER_SEP. PF-2: the row stops before it would draw under the
 * fault strip -- its right limit is fault_strip_left_x(m->flags) - DCARD_FAULT_GAP, which is always
 * <= CANVAS_VISIBLE_W, so this also never draws past the visible edge. */
static void render_dcard_footer(fb_t *fb, const screen_model_t *m, uint8_t n) /* gates 0..n-2 */
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL && n >= 1u && n <= DRAG_MAX_GATES, UI_ASSERT_CODE);
    uint8_t first = (n - 1u > DCARD_FOOTER_MAX) ? (uint8_t)(n - 1u - DCARD_FOOTER_MAX) : 0u;
    int     x = DCARD_LABEL_X;
    int     limit = fault_strip_left_x(m->flags) - DCARD_FAULT_GAP; /* PF-2: never under a fault icon */
    for (uint8_t i = first; i + 1u < n && i < DRAG_MAX_GATES; i++) {
        const drag_row_t *r = &m->drag[i];
        char  buf[TIME_BUF_LEN + 12];
        char *p = put_str(buf, r->label);
        p = put_char(p, ' ');
        if (r->is_distance) {
            p = put_uint(p, r->dist_m);
            p = put_char(p, 'm');
        } else {
            char t[TIME_BUF_LEN];
            fmt_secs_ms(t, r->t_ms);
            p = put_str(p, t);
        }
        if (i + 2u < n) {
            p = put_str(p, DCARD_FOOTER_SEP);
        }
        *p = '\0';
        if (x + (int)strlen(buf) * FONT_SMALL.w > limit) {
            break; /* never past the fault strip (PF-2) */
        }
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
        render_dcard_value(fb, &m->drag[n - 1u], m->units);
        render_dcard_footer(fb, m, n);
    }
    if (m->drag_armed) {
        fb_text_right(fb, &FONT_MED, DCARD_ARMED_RIGHT_X, DCARD_ARMED_Y, "ARMED");
    }

    /* Mirrors LAP page 0 (the other primary in-ride screen): only the page-0 riding view shows the
     * fault strip, not the pages-1/2 review lists below. */
    fault_strip(fb, m->flags, m->batt_pct);
}

/* ---- DRAG pages 1/2 (spec 7b §7): all seven gates of the last run / best-per-gate this session
 * -- same row set (60ft, 330ft, 1/8, 1000ft, 1/4, 100-200, 100-0), just different values, so one
 * list layout serves both; they differ only in the title drawn and in which values the caller
 * populated m->drag[] with. Two columns of DLIST_ROWS rows each (the left column fills first):
 * each cell is a FONT_SMALL label at the column's left edge and a FONT_MED value right-aligned at
 * the column's right edge, "--.--" for a gate not hit this run/session. ---- */

static void render_drag_gate_list(fb_t *fb, const screen_model_t *m, const char *title)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL && title != NULL, UI_ASSERT_CODE);
    fb_text(fb, &FONT_SMALL, DLIST_COL1_X, DLIST_HEADER_Y, title);
    uint8_t n = m->drag_n > DRAG_MAX_GATES ? (uint8_t)DRAG_MAX_GATES : m->drag_n;
    for (uint8_t i = 0; i < n && i < 2u * DLIST_ROWS; i++) {
        int col = i / DLIST_ROWS, row = i % DLIST_ROWS; /* left column fills first */
        int x = col ? DLIST_COL2_X : DLIST_COL1_X;
        int xr = col ? DLIST_COL2_RIGHT_X : DLIST_COL1_RIGHT_X;
        int y = DLIST_ROW_Y0 + row * DLIST_ROW_H;
        const drag_row_t *r = &m->drag[i];
        char buf[TIME_BUF_LEN];
        fb_text(fb, &FONT_SMALL, x, y + DLIST_LABEL_DY, r->label);
        if (!r->present) {
            char *p = put_str(buf, "--.--");
            *p = '\0';
            fb_text_right(fb, &FONT_MED, xr, y, buf);
        } else if (r->is_distance) {
            char *p = put_uint(buf, r->dist_m);
            *p = '\0';
            fb_text_right(fb, &FONT_MED, xr - DLIST_UNIT_W, y, buf);
            fb_text(fb, &FONT_SMALL, xr - DLIST_UNIT_W + DLIST_UNIT_GAP, y + DLIST_LABEL_DY, "m");
        } else {
            fmt_secs_ms(buf, r->t_ms);
            fb_text_right(fb, &FONT_MED, xr, y, buf);
        }
    }
}

static void render_drag_page1(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    render_drag_gate_list(fb, m, "LAST RUN");
}

static void render_drag_page2(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    render_drag_gate_list(fb, m, "SESSION BEST");
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
 * font `f` within the panel's true visible width. Centers on CANVAS_VISIBLE_W (core/ui/canvas.h),
 * not fb->w: fb->w is the padded, byte-aligned buffer width (256 on the 213 canvas), 6px wider
 * than the panel's true visible area (250) -- centering on fb->w biases every one-shot caption
 * (VENUE/SAFE/LOWBATT/OTA/OTAFAIL/CALIBRATE/NEWTRACK) 3px right of the panel's actual center
 * (Plan 7 T3 fix 2). On the 296 canvas CANVAS_VISIBLE_W == fb->w, so this is unchanged there.
 * Pure integer arithmetic; a string wider than the frame (should not happen for the short
 * one-shot captions used here) clamps to x=0 rather than going negative -- fb_text clips
 * off-frame draws safely either way, but a negative x would left-crop the string instead of just
 * running off the right edge. */
static int center_x(const fb_t *fb, const font_t *f, const char *s)
{
    CORE_ASSERT_RET(fb != NULL, UI_ASSERT_CODE, 0);
    CORE_ASSERT_RET(f != NULL, UI_ASSERT_CODE, 0);
    CORE_ASSERT_RET(s != NULL, UI_ASSERT_CODE, 0);
    int w = (int)strlen(s) * (int)f->w;
    int x = (CANVAS_VISIBLE_W - w) / 2;
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
        fb_text(fb, &FONT_SMALL, TEXT_MARGIN_X, BOOT_LINE_Y0 + (int)i * BOOT_LINE_H, m->boot_line[i]);
    }
}

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

static void render_oneshot_safe(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    (void)m;
    fb_text(fb, &FONT_MED, center_x(fb, &FONT_MED, ONESHOT_SAFE_TEXT), SAFE_TEXT_Y,
             ONESHOT_SAFE_TEXT);
}

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

/* True when every character of `s` has a glyph in FONT_MED (a space is also accepted: unmapped
 * chars -- including space -- draw a blank cell in any font, per render.h, so a space "fits" any
 * font visually even though it has no glyph index). A menu caption with '/' or lowercase (most of
 * §20.7's own item list, e.g. "Mode: Lap / Drag") fails this and falls back to FONT_SMALL -- same
 * missing-glyph reasoning as the DRAG gate labels above (render_drag_gate_list). */
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
 * item text uses FONT_MED when it fits (item_fits_font_med) AND the canvas allows it
 * (MENU_ITEM_ALLOW_MED, core/ui/canvas.h -- 0 on the 213 canvas, whose MENU_ROW_H is shorter than
 * FONT_MED's cell), else FONT_SMALL, vertically centered within the MENU_ROW_H slot either way. */
static void render_menu(fb_t *fb, const screen_model_t *m)
{
    CORE_ASSERT_VOID(fb != NULL, UI_ASSERT_CODE);
    CORE_ASSERT_VOID(m != NULL, UI_ASSERT_CODE);
    fb_clear(fb, 0);

    fb_text(fb, &FONT_MED, TEXT_MARGIN_X, MENU_TITLE_Y, "MENU");
    /* CANVAS_VISIBLE_W (core/ui/canvas.h), not fb->w: fb->w is the padded, byte-aligned buffer
     * width (256 on the 213 canvas), 6px wider than the panel's true visible area (250) -- a
     * full-fb->w line would draw ink past the panel's right edge into that invisible padding
     * (Plan 7 T3 fix 1, ruling T3-R1: "nothing may draw at x >= 250"). On the 296 canvas
     * CANVAS_VISIBLE_W == fb->w, so this is unchanged there. */
    fb_hline(fb, 0, MENU_SEP_Y, CANVAS_VISIBLE_W, 1);

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

        if (MENU_ITEM_ALLOW_MED && item_fits_font_med(item)) {
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
