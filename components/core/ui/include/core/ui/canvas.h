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
#define CARD_FAULT_GAP 4         /* BEST value keeps this many px clear of the fault strip */
#define CARD_DELTA_CLAMP_MS 99990

/* LAP_LABEL_X is still used by pages 1/2 and the DRAG grid (both canvases) today; those uses move
 * to BOARD_X0/GRID_COL1_X in Task 3. LAP_TIME_RIGHT_X is still used by page 1's THEO row value
 * (render_lap_page1) today; that use moves with the rest of page 1's layout in Task 3 too. Both
 * are temporary reintroductions of constants this task's CARD block otherwise replaces. */
#define LAP_LABEL_X 4 /* removed in Task 3 */
#if CANVAS_213
#define LAP_TIME_RIGHT_X 170 /* removed in Task 3 */
#else
#define LAP_TIME_RIGHT_X 200 /* removed in Task 3 */
#endif

/* ---- LAP page 1 (best-lap sector splits + THEO) ---- */
#if CANVAS_213
#define LAP1_TITLE_Y       4
#define LAP1_SECTOR_COLS   3
#define LAP1_SECTOR_X0     4
#define LAP1_SECTOR_COL_W  ((CANVAS_W - 8) / 3)
#define LAP1_SECTOR_Y0     16
#define LAP1_SECTOR_ROW_H  14
#define LAP1_THEO_LABEL_Y  84
#define LAP1_THEO_VALUE_Y  76
#else
#define LAP1_TITLE_Y       4
#define LAP1_SECTOR_COLS   3
#define LAP1_SECTOR_X0     4
#define LAP1_SECTOR_COL_W  96
#define LAP1_SECTOR_Y0     20
#define LAP1_SECTOR_ROW_H  16
#define LAP1_THEO_LABEL_Y  88
#define LAP1_THEO_VALUE_Y  80
#endif

/* ---- LAP page 2 (session stats, 5 rows from a fixed y=8) ---- */
#if CANVAS_213
#define LAP2_ROW_H 20
#else
#define LAP2_ROW_H 24
#endif

/* ---- DRAG page 0 (benches + 1/4 row, spec §11.4) ---- */
#if CANVAS_213
#define DRAG_LABEL_X       4
#define DRAG_TIME_RIGHT_X  150
#define DRAG_TRAP_X        160
#define DRAG0_ROW_Y0       2
#define DRAG0_ROW_H        26
#define DRAG0_MAX_ROWS     4
#define DRAG_ARMED_RIGHT_X 250
#define DRAG_ARMED_Y       4
#else
#define DRAG_LABEL_X       4
#define DRAG_TIME_RIGHT_X  180
#define DRAG_TRAP_X        190
#define DRAG0_ROW_Y0       8
#define DRAG0_ROW_H        24
#define DRAG0_MAX_ROWS     4
#define DRAG_ARMED_RIGHT_X 296
#define DRAG_ARMED_Y       4
#endif

/* ---- DRAG pages 1/2 (2-col gate grid) ---- */
#if CANVAS_213
#define DRAG12_TITLE_Y 4
#define DRAG12_COLS    2
#define DRAG12_COL_X0  4
#define DRAG12_COL_W   ((CANVAS_W - 8) / 2)
#define DRAG12_ROW_Y0  16
#define DRAG12_ROW_H   14
#else
#define DRAG12_TITLE_Y 4
#define DRAG12_COLS    2
#define DRAG12_COL_X0  4
#define DRAG12_COL_W   148
#define DRAG12_ROW_Y0  20
#define DRAG12_ROW_H   16
#endif

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
