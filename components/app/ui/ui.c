/* ui.c -- the app-side UI task, menu navigation and button debounce (spec §4.3, §20.3, §20.7-20.8).
 *
 * The ui task (core 0, prio 6, stack 6144) owns a static screen_model_t and a static 296x128 1-bpp
 * framebuffer. It coalesces (§20.3): each wake it drains the button queue and the pipeline event
 * queue (g_ui_evt_q, the pipeline's fan-out copy for the ui), updates the model, and -- only when
 * something changed -- renders ONCE via the pure core/ui screens_render() and hands the result to
 * the pure refresh policy (core/ui/refresh_policy.h, Plan 7 Task 7): ui_refresh_decide() picks
 * none/partial/full given the dirty/motion/fault state, render_and_refresh() carries that out
 * against hal/display.h (disp_set_window/disp_refresh), and a failure ladder (reinit + retry once,
 * three strikes -> SYS_DISP_DEAD, paced 300 s reinit probes while dead) keeps a wedged panel from
 * ever stalling this task's heartbeat.
 *
 * Buttons (§20.8): ui_buttons.c pushes each debounced edge (board 25 ms guard) as a btn_raw_t; the
 * press-duration state machine here classifies short (< 500 ms) / long (>= 1000 ms, fires once while
 * held) / very-long (>= 3000 ms), and UP+DOWN held 2 s -> full-refresh. Each loop also re-reads the
 * live button level (board_buttons_read) to reconcile any edge the ISR's global 25 ms guard
 * coalesced, so the state machine tracks the true button state even if a queue edge was dropped.
 *
 * Menu (§20.7): MODE long-press opens the menu when the cached gspeed proxy is below
 * MENU_LOCK_SPEED_KMH; UP/DOWN move the selection (keeping it visible via menu_top), MODE selects,
 * long-MODE backs/exits, and 30 s idle auto-exits. Items with state now (Mode, Units, Display, and
 * Layout/Calibrate via commands) are wired; items that need later subsystems (Export, Live,
 * Diagnostics export, Sessions, New track, Sleep now) render + are selectable but only log.
 *
 * Events (§4.4): the pipeline fans each event out to BOTH g_evt_q (logger) and g_ui_evt_q (this
 * task), so the two never steal each other's events; this task drains g_ui_evt_q (drop-newest on
 * full). No queue set -- both consumers read their own queue directly.
 */
#include "build_config.h"

#include <stdio.h>
#include <string.h>

#include "core/cfg.h"
#include "core/event.h"
#include "core/ui/canvas.h" /* CANVAS_W/CANVAS_H/MENU_VISIBLE_ROWS: compile-time by PANEL (Plan 7 T3) */
#include "core/ui/model.h" /* pulls in core/ui/render.h: fb_t, fb_init, screens_render, SCR_*, etc. */
#include "core/ui/refresh_policy.h" /* ui_refresh_decide (Plan 7 Task 6): pure partial/full/none decision */

#include "app/lt_assert.h"
#include "app/lt_err.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"
#include "app/ui.h"

#include "hal/board.h"
#include "hal/display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "ui";

/* Power-of-10 rule 5 assertion code for the app-side ui module (design §3): the app assert hook
 * records this code plus __FILE__/__LINE__, pinning the exact failing check. */
#define UI_APP_ASSERT_CODE 0x0B50

/* ---- §4.3 task ---- */
#define UI_CORE 0
#define UI_PRIO 6
/* §4.3 stack: 6144 on every build, moto_sim included. Earlier plans shrank the moto_sim ui stack to
 * 2560 to fit two sim-only DRAM statics alongside it: trk_json.c's 512-token scratch array (10,240
 * B) and pipeline.c's s_venue copy of the sim capture's venue (2,752 B). Plan 7 Task 1 reclaimed
 * both -- the token array is now device-sized (CFG_TRK_JSON_TOKS=64, since only the 330-byte sim
 * capture venue is ever parsed on a firmware build) and the sim venue is parsed straight into the
 * trk user table instead of a separate static -- so the sim's thin DRAM margin is gone and the ui
 * task uses the same 6144-byte stack the display driver (this plan) needs for its refresh line
 * buffer. */
#define UI_STACK_BYTES 6144
#define UI_STACK_WORDS (UI_STACK_BYTES / sizeof(StackType_t))
#define UI_STALL_S     10 /* §17.2 ui heartbeat-stall window (a full refresh may take 2 s) */
#define UI_TICK_MS     100 /* loop timeout: poll cadence + long/idle-timer granularity */
/* Rule 2 explicit static loop bounds for the per-wake queue drains: both queues hold single-digit
 * depths (btn_q is 8; §4.4), so these caps sit far above any real backlog and are never reached. */
#define UI_BTN_DRAIN_MAX 32
#define UI_EVT_DRAIN_MAX 64

/* ---- framebuffer (spec §4.8: 296x128 / 8 = 4.7 KB, sized for the larger panel) ----
 * FB_W/FB_H/FB_STRIDE size the static buffer only, kept at the 296x128 (ws29v2) worst case so one
 * build of this file holds either panel's canvas; the actual render dimensions -- CANVAS_W/CANVAS_H
 * (core/ui/canvas.h), 256x122 on a ws213v4 build -- are what fb_init/render_fb use below (Plan 7
 * T3: compile-time canvas by PANEL). */
#define FB_W      296
#define FB_H      128
#define FB_STRIDE (FB_W / 8)

/* ---- buttons (spec §20.8, Appendix A). Board mask: bit0 MODE, bit1 UP, bit2 DOWN (§5.1). ---- */
#define BTN_MODE 0x1u
#define BTN_UP   0x2u
#define BTN_DOWN 0x4u
#define BTN_SHORT_MS 500  /* < 500 ms => short press */
#define BTN_LONG_MS  1000 /* >= 1000 ms => long press (fires once while held) */
#define BTN_VLONG_MS 3000 /* >= 3000 ms => very-long (Sleep-now confirm) */
#define BTN_COMBO_MS 2000 /* UP+DOWN held 2 s => full refresh now (ghost clearing) */

/* ---- menu (spec §20.7) ---- */
#define MENU_LOCK_SPEED_KMH 10    /* menu entry gated below this (§20.7 / Appendix A) */
#define MENU_IDLE_MS        30000 /* auto-exit after 30 s idle (MENU_IDLE_S) */
#define UI_MENU_MAX         12    /* capacity of s_menu_action[]/s_model.menu_items[] (§20.7) */

/* ---- one-shots (spec §20.6, §17.6: boot + venue banners show ~2 s) ---- */
#define ONESHOT_BOOT_MS  2000
#define ONESHOT_VENUE_MS 2000

/* ---- display refresh ladder (spec §20.3, Plan 7 Task 7) ----
 * DISP_FAIL_STREAK_MAX consecutive disp_refresh()/disp_reinit()-retry failures mark the panel
 * SYS_DISP_DEAD; DISP_DEAD_RETRY_US then paces the reinit probe while dead (ruling R5: the first
 * retry lands 300 s after the flag was set, never immediately). */
#define DISP_FAIL_STREAK_MAX 3
#define DISP_DEAD_RETRY_US   (300LL * 1000000)

/* Menu item actions; the visible order is built in build_menu() (spec §20.7's list). */
enum {
    MA_MODE = 0,
    MA_LAYOUT,
    MA_NEWTRACK,
    MA_CALIBRATE,
    MA_UNITS,
    MA_EXPORT,
    MA_LIVE,
    MA_DIAG,
    MA_SESSIONS,
    MA_DISPLAY,
    MA_SLEEP,
};

/* ---- static storage (no malloc after init, §17.9) ---- */
static StaticTask_t s_tcb;
static StackType_t  s_stack[UI_STACK_WORDS];
static TaskHandle_t s_task;

static screen_model_t s_model;
static uint8_t        s_fb_bits[FB_STRIDE * FB_H]; /* (296/8)*128 = 4736 B */
static fb_t           s_fb;

static cfg_t   s_cfg;         /* ui's working config copy (cmd.c uses the same load-from-NVS pattern) */
static uint8_t s_mode;        /* MODE_LAP / MODE_DRAG (mirrors s_model.mode) */
static uint16_t s_gspeed_kmh; /* menu-lock proxy from EV_MOTION/EV_STILL (see handle_event) */
static bool    s_fix_lost;    /* EV_FIX_LOST/OK -> SCR_SYS_GPS_NOFIX in the fault strip */
static bool    s_dirty;       /* model changed since last render -> render once (§20.3) */
static int64_t s_oneshot_until_us; /* auto-revert time for a transient one-shot (BOOT/VENUE); 0 = none */
static int64_t s_last_input_us;    /* last button activity -> menu idle timeout */

/* refresh policy bookkeeping (spec §20.3, Plan 7 Task 7): rf_in_t's counters/timestamps, plus the
 * failure ladder's own state. All times are esp_timer_get_time() microseconds. The three int64_t
 * fields are declared first (fix round 1, ruling T7-R3) so their 8-byte alignment doesn't force
 * padding ahead of the smaller fields that follow. */
static int64_t  s_last_full_us;    /* stamp of the last full refresh (incl. boot's disp_init) */
static int64_t  s_last_partial_us; /* stamp of the last partial refresh */
static int64_t  s_next_reinit_us;  /* next allowed disp_reinit() probe while SYS_DISP_DEAD */
static uint16_t s_partial_count;   /* partials issued since the last full */
static uint8_t  s_fail_streak;     /* consecutive disp_refresh (+ one reinit retry) failures */
static bool     s_wants_full;      /* next render should be a full refresh (page/menu/combo/etc) */

/* button press-duration state machine (indexed 0=MODE,1=UP,2=DOWN) */
static uint8_t s_prev_mask;
static int64_t s_press_us[3];
static bool    s_long_fired[3];
static bool    s_vlong_fired[3];
static int64_t s_combo_start_us;
static bool    s_combo_fired;

/* menu labels + action map (menu_items[] point at these caller-owned buffers) */
static uint8_t s_menu_action[12];
static char    s_lbl_mode[16];
static char    s_lbl_units[16];
static char    s_lbl_disp[20];

/* ---- helpers ---- */

static void ui_send_cmd(uint8_t type, uint8_t arg8, uint16_t arg16)
{
    LT_ASSERT_VOID(type <= CMD_IMU_MODE, UI_APP_ASSERT_CODE);   /* a valid §4.4 command type */
    if (g_cmd_q == NULL) {
        return;
    }
    command_t c;
    memset(&c, 0, sizeof c);
    c.type  = type;
    c.arg8  = arg8;
    c.arg16 = arg16;
    (void)xQueueSend(g_cmd_q, &c, 0);
}

/* Append one row (label + action) to the parallel menu arrays and advance the count; the label
 * points at caller-owned storage (s_model.menu_items[] does not copy). */
static void menu_add(uint8_t *n, const char *lbl, uint8_t act)
{
    LT_ASSERT_VOID(*n < UI_MENU_MAX, UI_APP_ASSERT_CODE);   /* room left in menu_items[]/s_menu_action[] */
    s_model.menu_items[*n] = lbl;
    s_menu_action[*n]      = act;
    (*n)++;
}

/* Build the §20.7 menu into s_model.menu_items[]/menu_n; dynamic labels reflect current state. */
static void build_menu(void)
{
    LT_ASSERT_VOID(s_mode <= MODE_DRAG, UI_APP_ASSERT_CODE);                     /* label depends on it */
    LT_ASSERT_VOID(s_cfg.units <= CFG_UNITS_MPH, UI_APP_ASSERT_CODE);           /* label depends on it */
    uint8_t n = 0;
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
             s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
             s_cfg.display.live_clock ? "on" : "off");

    menu_add(&n, s_lbl_mode, MA_MODE);
    menu_add(&n, "Layout", MA_LAYOUT);
    menu_add(&n, "New track", MA_NEWTRACK);
    menu_add(&n, "Calibrate", MA_CALIBRATE);
    menu_add(&n, s_lbl_units, MA_UNITS);
    menu_add(&n, "Export (BLE)", MA_EXPORT);
#if CFG_HAS_BLE_RC
    menu_add(&n, "Live to phone", MA_LIVE);
#endif
    menu_add(&n, "Diagnostics", MA_DIAG);
    menu_add(&n, "Sessions", MA_SESSIONS);
    menu_add(&n, s_lbl_disp, MA_DISPLAY);
    menu_add(&n, "Sleep now", MA_SLEEP);

    LT_ASSERT_VOID(n <= UI_MENU_MAX, UI_APP_ASSERT_CODE);   /* did not overrun menu_items[]/action[] */
    s_model.menu_n = n;
}

static void menu_scroll_to_sel(void)
{
    LT_ASSERT_VOID(s_model.menu_n <= UI_MENU_MAX, UI_APP_ASSERT_CODE);        /* model menu invariant */
    LT_ASSERT_VOID(s_model.menu_sel < s_model.menu_n, UI_APP_ASSERT_CODE);   /* selection is a real row */
    if (s_model.menu_sel < s_model.menu_top) {
        s_model.menu_top = s_model.menu_sel;
    } else if (s_model.menu_sel >= (uint8_t)(s_model.menu_top + MENU_VISIBLE_ROWS)) {
        s_model.menu_top = (uint8_t)(s_model.menu_sel - MENU_VISIBLE_ROWS + 1);
    }
    LT_ASSERT_VOID(s_model.menu_top <= s_model.menu_sel, UI_APP_ASSERT_CODE);   /* selection now visible */
}

static void ui_open_menu(void)
{
    if (s_gspeed_kmh < MENU_LOCK_SPEED_KMH) {
        build_menu();
        LT_ASSERT_VOID(s_model.menu_n > 0, UI_APP_ASSERT_CODE);            /* build_menu always adds rows */
        LT_ASSERT_VOID(s_model.menu_n <= UI_MENU_MAX, UI_APP_ASSERT_CODE);
        s_model.screen   = SCR_MENU;
        s_model.menu_sel = 0;
        s_model.menu_top = 0;
        s_last_input_us  = esp_timer_get_time();
        s_wants_full     = true; /* menu entry: full refresh (§20.3 full-refresh triggers) */
        s_dirty          = true;
    } else {
        /* §20.7: above the lock speed the menu is ignored (a lock-icon flash). No lock glyph exists
         * in icons.h yet, so log it -- the flash arrives with the display driver. */
        ESP_LOGI(TAG, "menu locked (gspeed proxy %u >= %u km/h)", s_gspeed_kmh,
                 (unsigned)MENU_LOCK_SPEED_KMH);
    }
}

static void ui_exit_menu(void)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* leaving a valid screen */
    s_model.screen = SCR_RIDING;
    s_wants_full   = true; /* menu/one-shot exit: full refresh (§20.3 full-refresh triggers) */
    s_dirty        = true;
}

/* Every UI-driven cfg change is a read-modify-write (T-D): reload the blob from NVS immediately
 * before mutating one field, so a CONFIG_SET the dev controller persisted since this task's boot
 * load is NOT silently reverted by writing back a stale s_cfg (cmd.c is the other writer). On a
 * load failure s_cfg keeps its last-known-good value (lt_cfg_load leaves it untouched), which is
 * the same fall-back the boot seed uses. */

/* MA_MODE: toggle Lap/Drag, reset the page (§22.6), persist cfg.mode, push the mode command,
 * refresh the label. (Split verbatim out of menu_select for rule 4.) */
static void menu_do_mode(void)
{
    s_mode        = (s_mode == MODE_DRAG) ? (uint8_t)MODE_LAP : (uint8_t)MODE_DRAG;
    s_model.mode  = s_mode;
    s_model.page  = 0; /* §22.6: a mode switch resets the screen */
    (void)lt_cfg_load(&s_cfg);              /* RMW: don't clobber a peer's CONFIG_SET */
    s_cfg.mode    = s_mode;                 /* T-D: cfg.mode is the single source of truth; persist it */
    (void)lt_cfg_save(&s_cfg);
    ui_send_cmd(CMD_SET_MODE, s_mode, 0);
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
}

/* MA_UNITS: toggle km/h<->mph, persist, refresh the label. (Split verbatim out of menu_select.) */
static void menu_do_units(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D): reload before mutating + saving */
    s_cfg.units = (s_cfg.units == CFG_UNITS_MPH) ? (uint8_t)CFG_UNITS_KMH : (uint8_t)CFG_UNITS_MPH;
    (void)lt_cfg_save(&s_cfg);
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
             s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
}

/* MA_DISPLAY: toggle the live clock, persist, refresh the label. (Split verbatim out of menu_select.) */
static void menu_do_display(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D): reload before mutating + saving */
    s_cfg.display.live_clock = !s_cfg.display.live_clock;
    (void)lt_cfg_save(&s_cfg);
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
             s_cfg.display.live_clock ? "on" : "off");
}

static void menu_select(void)
{
    LT_ASSERT_VOID(s_model.menu_sel < UI_MENU_MAX, UI_APP_ASSERT_CODE);   /* indexes s_menu_action[] */
    LT_ASSERT_VOID(s_model.menu_sel < s_model.menu_n, UI_APP_ASSERT_CODE);
    uint8_t act = s_menu_action[s_model.menu_sel];
    LT_ASSERT_VOID(act <= MA_SLEEP, UI_APP_ASSERT_CODE);   /* a valid menu action enum */
    switch (act) {
    case MA_MODE:    menu_do_mode();    break;
    case MA_UNITS:   menu_do_units();   break;
    case MA_DISPLAY: menu_do_display(); break;
    /* The venue's layout list is not plumbed to the ui yet; select "Auto" (layout id 0). */
    case MA_LAYOUT:    ui_send_cmd(CMD_SET_LAYOUT, 0, 0); ESP_LOGI(TAG, "menu: Layout -> Auto (venue layout list TBD)"); break;
    /* pipeline drops CMD_CALIB_ORIENT until the calib session lands. */
    case MA_CALIBRATE: ui_send_cmd(CMD_CALIB_ORIENT, 0, 0); ESP_LOGI(TAG, "menu: Calibrate -> CMD_CALIB_ORIENT"); break;
    case MA_NEWTRACK: ESP_LOGW(TAG, "menu: New track not implemented (plan 05)"); break;
    case MA_EXPORT:   ESP_LOGW(TAG, "menu: Export (BLE) not implemented (plan 06)"); break;
    case MA_LIVE:     ESP_LOGW(TAG, "menu: Live to phone not implemented (plan 06)"); break;
    case MA_DIAG:     ESP_LOGW(TAG, "menu: Diagnostics export not implemented (§17.10, plan 05)"); break;
    case MA_SESSIONS: ESP_LOGW(TAG, "menu: Sessions ops not implemented (plan 05)"); break;
    case MA_SLEEP:    ESP_LOGW(TAG, "menu: Sleep now -- hold MODE 3 s to confirm (not implemented, plan 07)"); break;
    default: break;
    }
    s_dirty = true;
}

/* ---- button gestures ---- */

static void btn_short(uint8_t bit)
{
    LT_ASSERT_VOID(bit == BTN_MODE || bit == BTN_UP || bit == BTN_DOWN, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);       /* dispatches on it below */
    LT_ASSERT_VOID(s_model.oneshot <= ONESHOT_NEWTRACK, UI_APP_ASSERT_CODE); /* one-shot selector read below */
    /* A deliberate press dismisses a transient (BOOT/VENUE) one-shot. */
    if (s_model.screen == SCR_ONESHOT &&
        (s_model.oneshot == ONESHOT_BOOT || s_model.oneshot == ONESHOT_VENUE)) {
        s_oneshot_until_us = 0;
        ui_exit_menu(); /* -> SCR_RIDING */
        return;
    }

    if (s_model.screen == SCR_MENU) {
        if (bit == BTN_UP) {
            if (s_model.menu_sel > 0) {
                s_model.menu_sel--;
            }
            menu_scroll_to_sel();
            s_dirty = true;
        } else if (bit == BTN_DOWN) {
            if (s_model.menu_n > 0 && s_model.menu_sel + 1 < s_model.menu_n) {
                s_model.menu_sel++;
            }
            menu_scroll_to_sel();
            s_dirty = true;
        } else if (bit == BTN_MODE) {
            menu_select();
        }
    } else if (s_model.screen == SCR_RIDING) {
        LT_ASSERT_VOID(s_model.page < 3, UI_APP_ASSERT_CODE);   /* page wrap math assumes 0..2 */
        if (bit == BTN_UP) {
            s_model.page = (uint8_t)((s_model.page + 2) % 3); /* previous page (wrap) */
            s_wants_full = true; /* page change: full refresh (§20.3 full-refresh triggers) */
            s_dirty      = true;
        } else if (bit == BTN_DOWN) {
            s_model.page = (uint8_t)((s_model.page + 1) % 3); /* next page (wrap) */
            s_wants_full = true;
            s_dirty      = true;
        }
        /* MODE short in riding: reserved (no-op). */
    }
}

static void btn_long(uint8_t bit)
{
    if (bit != BTN_MODE) {
        return; /* only MODE uses long/vlong */
    }
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* dispatches on it below */
    LT_ASSERT_VOID(s_model.menu_sel < UI_MENU_MAX, UI_APP_ASSERT_CODE);  /* indexes s_menu_action[] */
    if (s_model.screen == SCR_MENU) {
        /* §20.7: long MODE backs/exits -- except on "Sleep now", which confirms on vlong (3 s). */
        if (s_menu_action[s_model.menu_sel] != MA_SLEEP) {
            ui_exit_menu();
        }
    } else if (s_model.screen == SCR_ONESHOT) {
        s_oneshot_until_us = 0;
        ui_exit_menu();
    } else { /* SCR_RIDING */
        ui_open_menu();
    }
}

static void btn_vlong(uint8_t bit)
{
    if (bit != BTN_MODE) {
        return;
    }
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* dispatches on it below */
    LT_ASSERT_VOID(s_model.menu_sel < UI_MENU_MAX, UI_APP_ASSERT_CODE);  /* indexes s_menu_action[] */
    if (s_model.screen == SCR_MENU && s_menu_action[s_model.menu_sel] == MA_SLEEP) {
        ESP_LOGW(TAG, "menu: Sleep now confirmed -- not implemented (plan 07)");
        ui_exit_menu();
    }
}

static void btn_combo(void)
{
    /* UP+DOWN held 2 s -> full refresh now (§20.8, ghost clearing). */
    s_wants_full = true;
    s_dirty      = true;
}

/* Apply a new button mask sampled at `now`, driving press/release edges. */
static void process_mask(uint8_t mask, int64_t now)
{
    LT_ASSERT_VOID(mask <= (BTN_MODE | BTN_UP | BTN_DOWN), UI_APP_ASSERT_CODE);   /* only 3 button bits */
    if (mask != s_prev_mask) {
        s_last_input_us = now;
    }
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        bool    was = (s_prev_mask & bit) != 0;
        bool    is  = (mask & bit) != 0;
        if (!was && is) {
            s_press_us[i]    = now;
            s_long_fired[i]  = false;
            s_vlong_fired[i] = false;
        } else if (was && !is) {
            int64_t held = now - s_press_us[i];
            LT_ASSERT_VOID(held >= 0, UI_APP_ASSERT_CODE);   /* now is monotonic vs the press stamp */
            if (!s_long_fired[i] && !s_vlong_fired[i] && held < (int64_t)BTN_SHORT_MS * 1000) {
                btn_short(bit);
            }
            s_press_us[i] = 0;
        }
    }
    bool both = (mask & BTN_UP) && (mask & BTN_DOWN);
    if (both) {
        if (s_combo_start_us == 0) {
            s_combo_start_us = now;
        }
    } else {
        s_combo_start_us = 0;
        s_combo_fired    = false;
    }
    s_prev_mask = mask;
}

/* Fire long/vlong (while held) and the UP+DOWN combo based on elapsed hold time. */
static void check_held(int64_t now)
{
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        if ((s_prev_mask & bit) == 0) {
            continue;
        }
        int64_t held = now - s_press_us[i];
        LT_ASSERT_VOID(held >= 0, UI_APP_ASSERT_CODE);   /* now is monotonic vs the press stamp */
        if (!s_vlong_fired[i] && held >= (int64_t)BTN_VLONG_MS * 1000) {
            s_vlong_fired[i] = true;
            s_long_fired[i]  = true; /* suppress a late long */
            s_last_input_us  = now;
            btn_vlong(bit);
        } else if (!s_long_fired[i] && held >= (int64_t)BTN_LONG_MS * 1000) {
            s_long_fired[i] = true;
            s_last_input_us = now;
            btn_long(bit);
        }
    }
    if (s_combo_start_us != 0 && !s_combo_fired &&
        (now - s_combo_start_us) >= (int64_t)BTN_COMBO_MS * 1000) {
        LT_ASSERT_VOID(now >= s_combo_start_us, UI_APP_ASSERT_CODE);   /* combo timer is monotonic */
        s_combo_fired   = true;
        s_last_input_us = now;
        btn_combo();
    }
}

/* ---- event -> model (§20.3 / §20.4) ---- */

static void show_venue_oneshot(int64_t now)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* gated on it below */
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE);   /* stamp for the one-shot revert timer */
    if (s_model.screen != SCR_RIDING) {
        return; /* don't interrupt the menu or another one-shot */
    }
    s_model.screen     = SCR_ONESHOT;
    s_model.oneshot    = ONESHOT_VENUE;
    s_oneshot_until_us = now + (int64_t)ONESHOT_VENUE_MS * 1000;
}

/* EV_LAP_COMPLETE -> model (split verbatim out of handle_event for rule 4). */
static void handle_lap_complete(const event_t *e)
{
    uint32_t lap_ms = e->arg32;
    s_model.have_prev = true;
    s_model.prev_ms   = lap_ms;
    if (!s_model.have_best || lap_ms < s_model.best_ms) {
        s_model.best_ms   = lap_ms;
        s_model.have_best = true;
        s_model.new_best  = true;
    } else {
        s_model.new_best = false;
    }
    if (s_model.laps_total < UINT16_MAX) {
        s_model.laps_total++;
    }
    if (s_model.laps_valid < UINT16_MAX) {
        s_model.laps_valid++;
    }
    s_model.cur_ms_at_gate = 0;
    s_model.cur_sector_idx = 0;
    s_dirty                = true;
    LT_ASSERT_VOID(s_model.laps_valid <= s_model.laps_total, UI_APP_ASSERT_CODE);   /* valid <= total */
    LT_ASSERT_VOID(!s_model.have_best || s_model.best_ms <= s_model.prev_ms, UI_APP_ASSERT_CODE);
}

/* EV_DRAG_GATE -> model (split verbatim out of handle_event for rule 4). */
static void handle_drag_gate(const event_t *e)
{
    LT_ASSERT_VOID(s_model.drag_n <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);   /* indexes s_model.drag[] */
    if (s_model.drag_n < DRAG_MAX_GATES) {
        drag_row_t *r = &s_model.drag[s_model.drag_n++];
        memset(r, 0, sizeof *r);
        /* gate-id -> §6.6 label map needs the drag cfg gate list (not plumbed to ui yet); a
         * compact "G<id>" placeholder is enough until that lands. */
        snprintf(r->label, sizeof r->label, "G%u", (unsigned)e->arg16);
        r->t_ms    = e->arg32;
        r->present = true;
    }
    LT_ASSERT_VOID(s_model.drag_n <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);   /* append kept it bounded */
    s_dirty = true;
}

/* EV_SECTOR -> model (split verbatim out of handle_event for rule 4). */
static void handle_sector(const event_t *e)
{
    LT_ASSERT_VOID(e->arg16 <= LAP_MAX_SECTORS, UI_APP_ASSERT_CODE);   /* engine sector idx in range */
    s_model.cur_sector_idx = (uint8_t)e->arg16;
    s_model.cur_ms_at_gate = e->arg32;
    s_model.sector_delta_ms = (int32_t)e->arg32b;
    s_dirty                = true;
}

/* EV_VENUE_FOUND -> model (split verbatim out of handle_event for rule 4). */
static void handle_venue_found(const event_t *e, int64_t now)
{
    snprintf(s_model.venue_name, sizeof s_model.venue_name, "V%u", (unsigned)e->arg16);
    s_model.layout_name[0] = '\0'; /* venue phase: render shows venue_name */
    show_venue_oneshot(now);
    s_dirty = true;
}

/* EV_LAYOUT_LOCKED -> model (split verbatim out of handle_event for rule 4). */
static void handle_layout_locked(const event_t *e, int64_t now)
{
    snprintf(s_model.layout_name, sizeof s_model.layout_name, "L%u", (unsigned)e->arg16);
    show_venue_oneshot(now); /* layout phase: render shows layout_name (non-empty) */
    s_dirty = true;
}

static void handle_event(const event_t *e, int64_t now)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* model screen stays valid */
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE);   /* esp_timer stamp drives the one-shot timers */
    switch (e->type) {
    case EV_LAP_COMPLETE:  handle_lap_complete(e); break;
    case EV_SECTOR:        handle_sector(e); break;
    case EV_VENUE_FOUND:   handle_venue_found(e, now); break;
    case EV_LAYOUT_LOCKED: handle_layout_locked(e, now); break;
    case EV_FIX_LOST: s_fix_lost = true; s_dirty = true; break;
    case EV_FIX_OK:   s_fix_lost = false; s_dirty = true; break;
    /* Proxy: no per-fix speed is broadcast to the ui yet, so moving conservatively locks the menu
     * (== the lock threshold). EV_STILL clears it. Flagged as a coarse gate. */
    case EV_MOTION: s_gspeed_kmh = MENU_LOCK_SPEED_KMH; break;
    case EV_STILL:  s_gspeed_kmh = 0; break;
    case EV_DRAG_ARMED:  s_model.drag_armed = true; s_model.drag_n = 0; s_dirty = true; break;
    case EV_DRAG_LAUNCH: s_model.drag_armed = false; s_dirty = true; break;
    case EV_DRAG_GATE:   handle_drag_gate(e); break;
    case EV_DRAG_DONE:   s_dirty = true; break;
    default: break;
    }
}

/* Snapshot the authoritative fault flags (§17.4) into the model + fold in the ui-tracked GPS
 * no-fix bit; re-render only when the strip actually changes. */
static void update_flags(void)
{
    uint32_t f = sys_flags_get();
    if (s_fix_lost) {
        f |= (1u << SCR_SYS_GPS_NOFIX);
    } else {
        f &= ~(1u << SCR_SYS_GPS_NOFIX);
    }
    if (f != s_model.flags) {
        s_model.flags = f;
        s_dirty       = true;
    }
}

/* Renders the current model into s_fb (no panel I/O). Split out of the old render_now() (Plan 7
 * Task 7) so ui_task's boot render -- before disp_init() has even run -- can fill the framebuffer
 * without going through the refresh policy/ladder below, which assumes the panel is already
 * initialised. */
static void render_fb(void)
{
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE);            /* fb_init ran before any render */
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE); /* screens_render dispatches on it */
    LT_ASSERT_VOID(s_model.page < 3, UI_APP_ASSERT_CODE);              /* riding renderer dispatches on it */
    screens_render(&s_fb, &s_model);
    /* The pure renderer clips every primitive to the fb, so the reported dirty box must lie within
     * the framebuffer -- a box past CANVAS_W/CANVAS_H would mean a renderer clipping bug.
     *
     * Deliberately CANVAS_W here, not CANVAS_VISIBLE_W (Plan 7 T3 fix 1, ruling T3-R1 asked for the
     * latter): fb_clear() (render.c), called at the top of every screens_render() path, always sets
     * dirty.x1 = fb->w -- clearing legitimately touches every addressable byte, including the
     * padding columns between CANVAS_VISIBLE_W and CANVAS_W on the 213 canvas -- so dirty.x1 is
     * CANVAS_W (256) after literally every render, never less. Binding this check to
     * CANVAS_VISIBLE_W (250) would make it fire on every single refresh on a ws213v4 build, not
     * just a genuine clipping bug -- confirmed by hitting exactly that failure in test_screens_213
     * once test/test_screens.c's dirty-box assertions were rebound the same way (see that file's
     * fb_max_ink_col() comment for the content-based check that actually verifies "nothing draws at
     * x >= CANVAS_VISIBLE_W", which this cheap structural bounds check on the hot render path is
     * not the right place for). CANVAS_W remains the correct bound for "did the renderer clip
     * itself to the addressable buffer". */
    LT_ASSERT_VOID(!s_fb.dirty.valid || s_fb.dirty.x1 <= CANVAS_W, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(!s_fb.dirty.valid || s_fb.dirty.y1 <= CANVAS_H, UI_APP_ASSERT_CODE);
}

/* Builds one rf_in_t snapshot for ui_refresh_decide() (spec §20.3). `full_every` is already
 * clamped to 1..50 by the caller (ruling R1: cfg only defaults it; the policy asserts >= 1). */
static rf_in_t build_rf_in(int64_t now, uint8_t full_every)
{
    rf_in_t  in;
    uint32_t flags     = sys_flags_get();
    in.dirty           = true; /* only ever called when s_dirty gated the render (render_and_refresh) */
    in.wants_full      = s_wants_full;
    in.still           = s_gspeed_kmh < MENU_LOCK_SPEED_KMH;
    in.throttled       = (flags & (1u << SYS_DISP_TEMP_THROTTLE)) != 0;
    in.dead            = (flags & (1u << SYS_DISP_DEAD)) != 0;
    in.full_every      = full_every;
    in.partial_count   = s_partial_count;
    in.now_us          = now;
    in.last_full_us    = s_last_full_us;
    in.last_partial_us = s_last_partial_us;
    LT_ASSERT_RET(in.full_every >= 1 && in.full_every <= 50, UI_APP_ASSERT_CODE, in); /* R1 clamp held */
    LT_ASSERT_RET(in.now_us >= 0, UI_APP_ASSERT_CODE, in); /* esp_timer stamp is monotonic non-negative */
    return in;
}

/* Arms the pending partial window from the render's dirty box, clamped to the panel's true visible
 * area (ruling R2: disp_set_window() rejects x + w > 250; fb_clear() always reports the whole
 * padded buffer as dirty). Returns DISP_PARTIAL if the window was armed, else DISP_FULL -- either
 * because the box was degenerate or disp_set_window() itself failed (logged once here). */
static uint8_t partial_window_or_full(void)
{
    LT_ASSERT_RET(s_fb.dirty.valid, UI_APP_ASSERT_CODE, DISP_FULL); /* something must have been drawn */
    LT_ASSERT_RET(s_fb.dirty.x0 <= s_fb.dirty.x1 && s_fb.dirty.y0 <= s_fb.dirty.y1, UI_APP_ASSERT_CODE,
                  DISP_FULL);
    uint16_t x0 = s_fb.dirty.x0;
    uint16_t y0 = s_fb.dirty.y0;
    uint16_t x1 = s_fb.dirty.x1 < CANVAS_VISIBLE_W ? s_fb.dirty.x1 : (uint16_t)CANVAS_VISIBLE_W;
    uint16_t y1 = s_fb.dirty.y1 < CANVAS_H ? s_fb.dirty.y1 : (uint16_t)CANVAS_H;
    if (x0 > x1 || y0 > y1) {
        return DISP_FULL; /* the visible-area clamp emptied the box: repaint the whole panel */
    }
    if (disp_set_window(x0, y0, (uint16_t)(x1 - x0), (uint16_t)(y1 - y0)) != 0) {
        ESP_LOGW(TAG, "disp_set_window(%u,%u,%u,%u) failed; falling back to full refresh",
                 (unsigned)x0, (unsigned)y0, (unsigned)(x1 - x0), (unsigned)(y1 - y0));
        return DISP_FULL;
    }
    return DISP_PARTIAL;
}

/* Runs `mode` through the panel and the failure ladder (spec §20.3): on failure it logs, reinits
 * and retries the SAME refresh once (ruling: disp_refresh(DISP_PARTIAL) with no pending window --
 * already consumed by the failed attempt -- runs a full, which is the intended fallback); a second
 * failure counts against s_fail_streak and, at DISP_FAIL_STREAK_MAX, marks the panel dead. Bumps
 * g_hb[HB_UI] after every blocking disp_refresh()/disp_reinit() call (ruling R3: a timed-out
 * refresh + reinit + retry can exceed UI_STALL_S). Returns the final driver rc. */
static int disp_refresh_ladder(uint8_t mode, int64_t now)
{
    LT_ASSERT_RET(mode == DISP_PARTIAL || mode == DISP_FULL, UI_APP_ASSERT_CODE, -1);
    LT_ASSERT_RET(now >= 0, UI_APP_ASSERT_CODE, -1);

    int rc = disp_refresh(mode);
    g_hb[HB_UI]++;
    if (rc == 0) {
        s_fail_streak = 0;
        return rc;
    }

    /* Ruling T7-R8: pack both facts into one arg -- the pre-failure streak count in the high
     * byte, the driver's -errno magnitude in the low byte -- so errlog distinguishes a BUSY
     * timeout (-ETIMEDOUT = 110) from an SPI failure (-EIO = 5) without a second display code. */
    errlog_add(E_DISP_BUSY_TIMEOUT, ((uint32_t)s_fail_streak << 8) | ((uint32_t)(-rc) & 0xFFu));
    int reinit_rc = disp_reinit();
    g_hb[HB_UI]++;
    if (reinit_rc == 0) {
        rc = disp_refresh(mode);
        g_hb[HB_UI]++;
    }
    if (rc == 0) {
        s_fail_streak = 0;
        return rc;
    }

    s_fail_streak++;
    if (s_fail_streak >= DISP_FAIL_STREAK_MAX) {
        sys_flags_set(SYS_DISP_DEAD);
        errlog_add(E_DISP_DEAD, 0);
        s_next_reinit_us = now + DISP_DEAD_RETRY_US;
    }
    return rc;
}

/* Executes the refresh kind the policy chose (RF_NONE is handled by the caller before this is
 * reached): RF_PARTIAL first tries to arm the dirty window, falling back to DISP_FULL (ruling R2)
 * if that fails; RF_FULL goes straight to DISP_FULL. Fix round 1 (Important #2): bookkeeping
 * follows the EFFECTIVE mode actually sent to the panel, not the policy's kind -- when the
 * fallback above turns a decided partial into a full, a successful refresh must still reset
 * s_partial_count, stamp s_last_full_us and clear s_wants_full like any other full. A FAILED
 * refresh (either mode) updates none of that bookkeeping -- only the ladder's own
 * fail_streak/dead accounting moves on failure. Reports the effective mode via *mode_out so the
 * caller's log line reflects what actually happened on the panel. */
static int do_refresh(rf_kind_t kind, int64_t now, uint8_t *mode_out)
{
    LT_ASSERT_RET(kind == RF_PARTIAL || kind == RF_FULL, UI_APP_ASSERT_CODE, -1);
    LT_ASSERT_RET(mode_out != NULL, UI_APP_ASSERT_CODE, -1);
    uint8_t mode = (kind == RF_PARTIAL) ? partial_window_or_full() : DISP_FULL;
    *mode_out    = mode;
    int rc       = disp_refresh_ladder(mode, now);
    if (rc == 0) {
        if (mode == DISP_PARTIAL) {
            s_partial_count   = (uint16_t)(s_partial_count + 1);
            s_last_partial_us = now;
        } else {
            s_partial_count = 0;
            s_last_full_us  = now;
            s_wants_full    = false;
        }
    }
    return rc;
}

/* Logs the render+refresh outcome (ruling R6, refined by fix round 1): kind=N when the policy
 * chose RF_NONE (`attempted` false, no mode/rc to report); otherwise kind=P|F reflects the
 * EFFECTIVE mode sent to the panel (Important #2 -- not necessarily the policy's original
 * decision, since partial_window_or_full() can fall back to a full), with the driver rc appended.
 * Minor #6: ESP_LOGW when the refresh failed (rc != 0), ESP_LOGI otherwise. */
static void log_refresh(bool attempted, uint8_t mode, int rc)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.page < 3, UI_APP_ASSERT_CODE);
    char kc = !attempted ? 'N' : ((mode == DISP_FULL) ? 'F' : 'P');
    if (!attempted) {
        ESP_LOGI(TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c", (unsigned)s_model.screen,
                 (unsigned)s_model.page, (unsigned)s_fb.dirty.x0, (unsigned)s_fb.dirty.y0,
                 (unsigned)s_fb.dirty.x1, (unsigned)s_fb.dirty.y1, kc);
        return;
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c rc=%d", (unsigned)s_model.screen,
                 (unsigned)s_model.page, (unsigned)s_fb.dirty.x0, (unsigned)s_fb.dirty.y0,
                 (unsigned)s_fb.dirty.x1, (unsigned)s_fb.dirty.y1, kc, rc);
    } else {
        ESP_LOGI(TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c rc=%d", (unsigned)s_model.screen,
                 (unsigned)s_model.page, (unsigned)s_fb.dirty.x0, (unsigned)s_fb.dirty.y0,
                 (unsigned)s_fb.dirty.x1, (unsigned)s_fb.dirty.y1, kc, rc);
    }
}

/* Renders the model, then feeds the pure refresh policy (§20.3) and carries out whatever it
 * decides: RF_NONE is a no-op, RF_PARTIAL/RF_FULL run through the panel + failure ladder above.
 * Split out of the old render_now() (Plan 7 Task 7) to stay under the 60-line function cap. */
static void render_and_refresh(void)
{
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE); /* fb_init ran before any render */
    render_fb();

    int64_t now = esp_timer_get_time();
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE); /* esp_timer stamp feeds the policy's now_us */

    uint8_t full_every = s_cfg.display.full_every; /* R1: cfg only defaults it, so clamp here too */
    if (full_every < 1) {
        full_every = 1;
    } else if (full_every > 50) {
        full_every = 50;
    }

    rf_in_t   in   = build_rf_in(now, full_every);
    rf_kind_t kind = ui_refresh_decide(&in);

    if (kind == RF_NONE) {
        log_refresh(false, DISP_PARTIAL, 0);
        return;
    }
    uint8_t mode = DISP_PARTIAL;
    int     rc   = do_refresh(kind, now, &mode);
    log_refresh(true, mode, rc);
}

/* While the panel is SYS_DISP_DEAD, probes disp_reinit() no more often than every
 * DISP_DEAD_RETRY_US (ruling R5): success clears the flag and asks for a full refresh next; a
 * repeat failure just reschedules the next probe. */
static void dead_retry(int64_t now)
{
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE);              /* esp_timer stamp drives the backoff */
    LT_ASSERT_VOID(s_next_reinit_us >= 0, UI_APP_ASSERT_CODE); /* armed by the ladder or boot failure */
    uint32_t flags = sys_flags_get();
    if ((flags & (1u << SYS_DISP_DEAD)) == 0 || now < s_next_reinit_us) {
        return;
    }
    if (disp_reinit() == 0) {
        sys_flags_clear(SYS_DISP_DEAD);
        s_fail_streak = 0;
        s_wants_full  = true;
        s_dirty       = true;
    } else {
        s_next_reinit_us = now + DISP_DEAD_RETRY_US;
    }
}

/* ---- task ---- */

/* One iteration of the ui task loop (§20.3): reset the WDT, drain buttons + events, expire
 * transient one-shots, auto-exit an idle menu, then render once if anything changed. Split
 * verbatim out of the intentionally-non-terminating ui_task loop for rule 4. */
static void ui_loop_iter(QueueHandle_t btn_q)
{
    esp_task_wdt_reset();

    btn_raw_t ev;
    if (btn_q != NULL && xQueueReceive(btn_q, &ev, pdMS_TO_TICKS(UI_TICK_MS)) == pdTRUE) {
        process_mask(ev.mask, ev.mono_us);
        /* rule 2: bounded drain of the rest (btn_q depth 8 << the cap). */
        for (int i = 0; i < UI_BTN_DRAIN_MAX && xQueueReceive(btn_q, &ev, 0) == pdTRUE; i++) {
            process_mask(ev.mask, ev.mono_us);
        }
    } else if (btn_q == NULL) {
        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }

    int64_t now = esp_timer_get_time();

    /* Reconcile with the live level: catches an edge the ISR's global 25 ms guard coalesced. */
    uint8_t cur = 0;
    if (board_buttons_read(&cur) == 0) {
        process_mask(cur, now);
    }
    check_held(now);

    /* Coalesce (§20.3): drain the whole event queue before rendering once (rule 2: bounded). */
    event_t e;
    for (int i = 0; i < UI_EVT_DRAIN_MAX && g_ui_evt_q != NULL
                    && xQueueReceive(g_ui_evt_q, &e, 0) == pdTRUE; i++) {
        handle_event(&e, now);
    }

    /* Model invariants the post-drain screen logic below depends on. */
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.oneshot <= ONESHOT_NEWTRACK, UI_APP_ASSERT_CODE);

    /* Transient one-shot expiry (BOOT/VENUE) -> back to riding. Ruling T7-R9: every transition
     * that replaces the whole screen content (one-shot -> riding, menu enter/exit, page change)
     * requests a full, same as ui_exit_menu()'s manual dismissal path. */
    if (s_model.screen == SCR_ONESHOT && s_oneshot_until_us != 0 && now >= s_oneshot_until_us) {
        s_oneshot_until_us = 0;
        s_model.screen     = SCR_RIDING;
        s_wants_full       = true;
        s_dirty            = true;
    }
    /* Menu idle auto-exit (§20.7). */
    if (s_model.screen == SCR_MENU && (now - s_last_input_us) >= (int64_t)MENU_IDLE_MS * 1000) {
        ui_exit_menu();
    }

    update_flags();
    dead_retry(now); /* R5: paced disp_reinit() probe while SYS_DISP_DEAD */

    if (s_dirty) {
        render_and_refresh();
        s_dirty = false;
    }

    g_hb[HB_UI]++;
}

static void ui_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL); /* §17.2 subscribe to the task WDT; reset once per loop below */
    sup_register_task(HB_UI, xTaskGetCurrentTaskHandle(), UI_STALL_S);

    /* ui's own config copy (units / live-clock toggles + save), like cmd.c. */
    cfg_defaults(&s_cfg);
    (void)lt_cfg_load(&s_cfg);
    s_mode       = (s_cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    s_model.mode = s_mode;
    LT_ASSERT_VOID(s_mode <= MODE_DRAG, UI_APP_ASSERT_CODE);   /* valid engine mode from cfg */

    fb_init(&s_fb, s_fb_bits, CANVAS_W, CANVAS_H);
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE);   /* framebuffer is armed for render_fb */

    /* First screen: SAFE MODE one-shot in safe mode (§17.5, boot self-test skipped), else BOOT. */
    uint32_t f0 = sys_flags_get();
    if (f0 & (1u << SYS_SAFE_MODE)) {
        s_model.screen  = SCR_ONESHOT;
        s_model.oneshot = ONESHOT_SAFE; /* persistent (§17.5: one full-screen render, then idle) */
    } else {
        snprintf(s_model.boot_name, sizeof s_model.boot_name, "LapTimer");
        snprintf(s_model.boot_ver, sizeof s_model.boot_ver, "%s", CFG_FW_VERSION);
        s_model.boot_n_lines = 0;
        s_model.screen       = SCR_ONESHOT;
        s_model.oneshot      = ONESHOT_BOOT;
        s_oneshot_until_us   = esp_timer_get_time() + (int64_t)ONESHOT_BOOT_MS * 1000;
    }
    s_model.flags = f0;
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);        /* first screen valid */
    LT_ASSERT_VOID(s_model.oneshot <= ONESHOT_NEWTRACK, UI_APP_ASSERT_CODE);  /* one-shot selector valid */
    render_fb();

    /* Display bring-up (Plan 7 Task 5): blit the just-rendered BOOT screen BEFORE disp_init so
     * its first full refresh shows it on the panel. disp_init()'s own full refresh counts as the
     * refresh policy's first full (ruling R4): on success it seeds s_last_full_us/s_partial_count
     * as if RF_FULL had just run; on failure nothing is seeded -- SYS_DISP_DEAD makes the policy
     * return RF_NONE until dead_retry()'s 300 s probe (armed here) clears it. */
    disp_blit(s_fb_bits);
    const disp_caps_t *disp_caps;
    int                disp_rc = disp_init(&disp_caps);
    if (disp_rc == 0) {
        s_last_full_us  = esp_timer_get_time();
        s_partial_count = 0;
    } else {
        errlog_add(E_DISP_DEAD, (uint32_t)(-disp_rc));
        sys_flags_set(SYS_DISP_DEAD);
        s_next_reinit_us = esp_timer_get_time() + DISP_DEAD_RETRY_US;
    }

    esp_task_wdt_reset();

    QueueHandle_t btn_q = ui_buttons_queue();

    for (;;) {
        ui_loop_iter(btn_q);
    }
}

void ui_start(void)
{
    if (s_task != NULL) {
        return;
    }
    ui_buttons_init(); /* create btn_q + attach the board ISR before the task drains it */
    s_task = xTaskCreateStaticPinnedToCore(ui_task, "ui", UI_STACK_WORDS, NULL, UI_PRIO, s_stack,
                                           &s_tcb, UI_CORE);
    LT_ASSERT_VOID(s_task != NULL, UI_APP_ASSERT_CODE);   /* static creation only fails on bad params */
}
