#ifndef CORE_UI_CANVAS_H
#define CORE_UI_CANVAS_H
/* Compile-time canvas (spec deltas §2). Firmware: from build_config.h's CFG_PANEL_*; host tests:
 * default 296x128, or 250x122 with -DCANVAS_FORCE_213=1. */
#if __has_include("build_config.h")
#include "build_config.h"
#endif
#if defined(CANVAS_FORCE_213) || (defined(CFG_PANEL_WS213V4) && CFG_PANEL_WS213V4)
#define CANVAS_W 256
#define CANVAS_H 122
#define CANVAS_213 1
#else
#define CANVAS_W 296
#define CANVAS_H 128
#define CANVAS_213 0
#endif

/* CANVAS_W is the *addressable framebuffer* width, not the ws213v4 panel's true visible width:
 * fb_init() (components/core/ui/render.c) asserts w % 8 == 0 (render.h's documented contract --
 * every row must start on a byte boundary), and the panel's real resolution, 250x122, is not a
 * multiple of 8 (250 % 8 == 2). Passing 250 to fb_init would trip that assertion (a Power-of-10
 * rule-5 check that reports and returns rather than aborting), leaving the framebuffer's `bits`
 * pointer NULL and every subsequent draw a silent no-op. 256 is the next multiple of 8 up from
 * 250, giving a (256/8)*122 = 3904-byte framebuffer (32-byte stride) -- exactly the "3904 bytes of
 * the 4736-byte buffer" the plan's own gate criterion names, confirming 256 (not the panel's raw
 * 250) is the intended CANVAS_W. Every layout constant below that must land flush against the
 * panel's real right/bottom edge (the fault-icon strip, DRAG's ARMED label, the OTA bar's
 * centering) is still anchored to the true 250-wide/122-tall visible area, not CANVAS_W itself --
 * columns 250..255 are simply never drawn into, the same way a real ws213v4 driver pads its SPI
 * row buffer past the panel's visible pixels. CANVAS_H (122) needs no such padding: fb_init only
 * constrains width, since bits are packed horizontally within a row.
 *
 * CANVAS_VISIBLE_W (Plan 7 T3 fix 1, ruling T3-R1) names that true visible width explicitly, so
 * every caller checking "did this draw land where the panel can actually show it" -- ui.c's
 * render_now() dirty-box bounds check, and test_screens.c's per-golden ink-column scan (a content
 * check, not the fb->dirty box: fb_clear() always reports the whole padded CANVAS_W as dirty,
 * since clearing legitimately touches every addressable byte, padding columns included, so
 * fb->dirty can never usefully be compared against CANVAS_VISIBLE_W) -- compares against 250 on
 * the 213 canvas, not the padded 256-wide buffer: nothing may draw at x >= CANVAS_VISIBLE_W. On
 * the 296 canvas the buffer width already equals the visible width, so CANVAS_VISIBLE_W ==
 * CANVAS_W there. */
#if CANVAS_213
#define CANVAS_VISIBLE_W 250
#else
#define CANVAS_VISIBLE_W 296
#endif

/* Shared left text margin (Plan 7b T4, ruling T3-R1): canvas-independent, used by the boot/menu
 * one-shot renderers and by the DRAG run-card/gate-list left edges (screens_moto.c). */
#define TEXT_MARGIN_X 4

/* ---- shared fault-icon strip (screens_moto.c fault_strip, spec §20.5 + §17.4) ----
 * x0 + ICON_W(12) and y + ICON_H(12) land flush with the true visible width/height on both
 * canvases (296/128 and 250/122), matching the fault_strip() comment's own flush-edge reasoning. */
#if CANVAS_213
#define FAULT_STRIP_X0 238
#define FAULT_STRIP_Y  110
#else
#define FAULT_STRIP_X0 284
#define FAULT_STRIP_Y  116
#endif

/* ---- Plan 7b LAP page 0: the event card (spec 7b §4) ---- */
#if CANVAS_213
#define CARD_MARKER_RIGHT_X 246
#define CARD_MARKER_Y       1
#define CARD_BIG_X          4
#define CARD_BIG_Y          4
#define CARD_NONE_Y         40   /* "LAP n" (FONT_MED) row when the slot has no delta */
#define CARD_TAG_Y          50   /* inverted BEST tag beside the big number */
#define CARD_TAG_ALT_X      208  /* tag on the marker row when the number is six glyphs */
#define CARD_LABEL_Y        72   /* Plan 7b T2 fix 1 (ruling T2-R1): values end at 108, below FAULT_STRIP_Y (110) */
#define CARD_VALUE_Y        84
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
#define CARD_LABEL_Y        76   /* Plan 7b T2 fix 1 (ruling T2-R1): values end at 112, below FAULT_STRIP_Y (116) */
#define CARD_VALUE_Y        88
#define CARD_LEFT_LABEL_X   4
#define CARD_LEFT_RIGHT_X   146
#define CARD_RIGHT_LABEL_X  152
#define CARD_RIGHT_RIGHT_X  292
#endif
#define CARD_TAG_W    32
#define CARD_TAG_H    14
#define CARD_TAG_GAP  6
/* Plan 7b T2 review minor m2: render_card_tag()'s inner text inset (screens_moto.c), shared by
 * both canvases since the tag box itself (CARD_TAG_W/H above) is canvas-independent. */
#define CARD_TAG_PAD_X 2
#define CARD_TAG_PAD_Y 1
#define DCARD_FAULT_GAP 4        /* px kept clear between DRAG page 0's footer text and the fault strip */
#define CARD_DELTA_CLAMP_MS 99990

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

/* ---- Plan 7b DRAG page 0: the run card (spec 7b §7) ---- */
#if CANVAS_213
#define DCARD_LABEL_Y       2
#define DCARD_BIG_Y         14
#define DCARD_READY_Y       40
#define DCARD_SPEED_Y       80
#define DCARD_FOOTER_Y      108
#define DCARD_ARMED_RIGHT_X 246
#define DCARD_ARMED_Y       2
#else
#define DCARD_LABEL_Y       2
#define DCARD_BIG_Y         16
#define DCARD_READY_Y       42
#define DCARD_SPEED_Y       84
#define DCARD_FOOTER_Y      114
#define DCARD_ARMED_RIGHT_X 292
#define DCARD_ARMED_Y       2
#endif
#define DCARD_LABEL_X    TEXT_MARGIN_X
#define DCARD_BIG_X      TEXT_MARGIN_X
#define DCARD_UNIT_GAP   4     /* px between the huge distance digits and the FONT_SMALL "m" */
#define DCARD_UNIT_DY    48    /* the "m" sits this far below the huge cell's top (near the baseline) */
#define DCARD_FOOTER_MAX 6     /* earlier gates listed in the footer, newest last */
#define DCARD_FOOTER_SEP "   "
/* T4-R1 (Plan 7b T4 fix 1): FONT_MED has no '@' glyph, so the trap row's '@' is drawn in
 * FONT_SMALL, then the speed digits in FONT_MED beside it -- canvas-independent, both canvases
 * share the same small-font baseline offset and gap. */
#define DCARD_AT_DY  8         /* the FONT_SMALL "@" sits this far below the FONT_MED row's top, hugging its baseline */
#define DCARD_AT_GAP 2         /* px between the "@" and the FONT_MED speed digits */
/* Plan 7c T4 (design §3): the trap row's unit suffix (FONT_SMALL "km/h"/"mph"), drawn after the
 * FONT_MED trap-speed digits on the "@" row's baseline (DCARD_SPEED_Y + DCARD_AT_DY) --
 * canvas-independent like DCARD_AT_DY/DCARD_AT_GAP above. */
#define DCARD_SPEED_UNIT_GAP 4 /* px between the trap-speed digits and the FONT_SMALL unit suffix */
/* ---- Plan 7b DRAG pages 1/2: gate list (spec 7b §7) ---- */
#define DLIST_ROWS 4
#define DLIST_LABEL_DY 6
#if CANVAS_213
#define DLIST_HEADER_Y     2
#define DLIST_COL2_X       126
/* Ruling FR-5 (Plan 7b final fix 1): symmetric columns -- col 1 is 4..124 (120 px), col 2 is
 * 126..246 (120 px), 2 px between the columns' boxes -- rather than the old 4..122/126..246
 * (118/120 px) split. */
#define DLIST_COL1_RIGHT_X 124
#define DLIST_COL2_RIGHT_X 246
#define DLIST_ROW_Y0       16
#define DLIST_ROW_H        26
#else
#define DLIST_HEADER_Y     2
#define DLIST_COL2_X       152
#define DLIST_COL1_RIGHT_X 146
#define DLIST_COL2_RIGHT_X 292
#define DLIST_ROW_Y0       18
#define DLIST_ROW_H        26
#endif
#define DLIST_COL1_X TEXT_MARGIN_X
#define DLIST_UNIT_W 10     /* room reserved right of a distance value for its FONT_SMALL "m" */
#define DLIST_UNIT_GAP 2    /* px between the distance digits and the FONT_SMALL "m" */
/* I3 (final review, ruling R-7): px gap between the header title's last glyph and the "+<n>"
 * overflow suffix drawn when more than 2*DLIST_ROWS gates were hit -- canvas-independent, like the
 * other _GAP constants above. */
#define DLIST_MORE_GAP 4

/* ---- one-shot screens (spec §20.6) ---- */
#if CANVAS_213
#define BOOT_NAME_Y    4
#define BOOT_VER_Y     26
#define BOOT_LINE_Y0   44
#define BOOT_LINE_H    14
#define BOOT_MAX_LINES 4
#else
#define BOOT_NAME_Y    6
#define BOOT_VER_Y     34
#define BOOT_LINE_Y0   56
#define BOOT_LINE_H    16
#define BOOT_MAX_LINES 4
#endif

#if CANVAS_213
#define VENUE_LABEL_Y 30
#define VENUE_VALUE_Y 52
#else
#define VENUE_LABEL_Y 40
#define VENUE_VALUE_Y 62
#endif

#if CANVAS_213
#define SAFE_TEXT_Y 50
#else
#define SAFE_TEXT_Y 52
#endif

#if CANVAS_213
#define LOWBATT_TITLE_Y 24
#define LOWBATT_PCT_Y   60
#else
#define LOWBATT_TITLE_Y 32
#define LOWBATT_PCT_Y   68
#endif

#if CANVAS_213
#define OTA_TITLE_Y 8
#define OTA_BAR_X   25
#define OTA_BAR_Y   46
#define OTA_BAR_W   200
#define OTA_BAR_H   18
#define OTA_PCT_Y   74
#else
#define OTA_TITLE_Y 16
#define OTA_BAR_X   48
#define OTA_BAR_Y   56
#define OTA_BAR_W   200
#define OTA_BAR_H   20
#define OTA_PCT_Y   84
#endif

#if CANVAS_213
#define OTAFAIL_LINE1_Y 28
#define OTAFAIL_LINE2_Y 60
#else
#define OTAFAIL_LINE1_Y 36
#define OTAFAIL_LINE2_Y 68
#endif

#if CANVAS_213
#define CALIBRATE_TITLE_Y 20
#define CALIBRATE_SUB_Y   56
#else
#define CALIBRATE_TITLE_Y 28
#define CALIBRATE_SUB_Y   68
#endif

#if CANVAS_213
#define NEWTRACK_TITLE_Y 20
#define NEWTRACK_SUB_Y   56
#else
#define NEWTRACK_TITLE_Y 28
#define NEWTRACK_SUB_Y   68
#endif

/* ---- menu (spec §20.7) ---- */
#if CANVAS_213
#define MENU_TITLE_Y      2
#define MENU_SEP_Y        26
#define MENU_LIST_Y0      30
#define MENU_ROW_H        14
#define MENU_VISIBLE_ROWS 6
#define MENU_MARKER_X     4
#define MENU_ITEM_X       18
#define MENU_ITEM_MAX     12
#else
#define MENU_TITLE_Y      2
#define MENU_SEP_Y        26
#define MENU_LIST_Y0      30
#define MENU_ROW_H        24
#define MENU_VISIBLE_ROWS 4
#define MENU_MARKER_X     4
#define MENU_ITEM_X       18
#define MENU_ITEM_MAX     12
#endif

/* render_menu() (screens_moto.c) draws an item's caption in FONT_MED (24px tall) when every
 * character has a glyph there (item_fits_font_med()), else FONT_SMALL (12px tall) -- a choice
 * that's about glyph coverage, not vertical space, on the 296 canvas because MENU_ROW_H there
 * (24) exactly matches FONT_MED's height. MENU_ROW_H shrinks to 14 on the 213 canvas so 6 rows
 * fit (see above); a FONT_MED cell no longer fits that pitch (24 > 14) and would stamp over the
 * row below it -- confirmed by eyeballing the CAPS-item golden (menu_top) with this gate off,
 * which showed exactly that. So the 213 canvas caps every menu row at FONT_SMALL regardless of
 * what item_fits_font_med() says; render_menu() ANDs this into that check. */
#if CANVAS_213
#define MENU_ITEM_ALLOW_MED 0
#else
#define MENU_ITEM_ALLOW_MED 1
#endif

#endif
