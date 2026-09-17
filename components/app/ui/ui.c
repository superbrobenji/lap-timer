/* ui.c -- the app-side UI task, menu navigation and button debounce (spec §4.3, §20.3, §20.7-20.8).
 *
 * The ui task (core 0, prio 6, stack 6144 on real builds / 2560 on moto_sim) owns a static screen_model_t and a static 296x128 1-bpp
 * framebuffer. It coalesces (§20.3): each wake it drains the button queue and the pipeline event
 * queue (g_ui_evt_q, the pipeline's fan-out copy for the ui), updates the model, and -- only when something changed -- renders ONCE via the
 * pure core/ui screens_render(). Plan 04 ships no display driver, so instead of refreshing a panel
 * it logs the dirty box; the real disp_refresh() glue lands with the e-paper driver in a later plan.
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
#include "core/ui/model.h" /* pulls in core/ui/render.h: fb_t, fb_init, screens_render, SCR_*, etc. */

#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"
#include "app/ui.h"

#include "hal/board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "ui";

/* ---- §4.3 task ---- */
#define UI_CORE 0
#define UI_PRIO 6
/* §4.3 stack is 6144, which real builds use. The moto_sim BENCH image is DRAM-bound: its gps_sim
 * replay capture lives in DRAM (~13 KB more .bss than the real GPS driver), leaving too little room
 * for a full 6144-byte ui stack alongside the 4.7 KB framebuffer. On the sim the ui task does no
 * display work (no panel in plan 04 -- it only renders into the framebuffer and logs), and its real
 * high-water is well under the supervisor's own 3072-byte stack (which also runs NVS + logging), so
 * the sim uses 2560 (high-water not yet measured on target -- see the plan-04 review). Note the
 * sim DRAM margin is thin (~1 KB). Restore 6144 here once the sim capture moves to flash / the display driver
 * (which needs the extra margin for its refresh line buffer) lands. */
#if CFG_GPS_SIM
#define UI_STACK_BYTES 2560
#else
#define UI_STACK_BYTES 6144
#endif
#define UI_STACK_WORDS (UI_STACK_BYTES / sizeof(StackType_t))
#define UI_STALL_S     10 /* §17.2 ui heartbeat-stall window (a full refresh may take 2 s) */
#define UI_TICK_MS     100 /* loop timeout: poll cadence + long/idle-timer granularity */

/* ---- framebuffer (spec §4.8: 296x128 / 8 = 4.7 KB, sized for the larger panel) ---- */
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
#define UI_MENU_VISIBLE_ROWS 4    /* mirrors render_menu()'s MENU_VISIBLE_ROWS in core/ui */

/* ---- one-shots (spec §20.6, §17.6: boot + venue banners show ~2 s) ---- */
#define ONESHOT_BOOT_MS  2000
#define ONESHOT_VENUE_MS 2000

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

/* Build the §20.7 menu into s_model.menu_items[]/menu_n; dynamic labels reflect current state. */
static void build_menu(void)
{
    uint8_t n = 0;
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
             s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
             s_cfg.display.live_clock ? "on" : "off");

#define UI_MENU_ADD(lbl, act)               \
    do {                                    \
        s_model.menu_items[n] = (lbl);      \
        s_menu_action[n]      = (uint8_t)(act); \
        n++;                                \
    } while (0)

    UI_MENU_ADD(s_lbl_mode, MA_MODE);
    UI_MENU_ADD("Layout", MA_LAYOUT);
    UI_MENU_ADD("New track", MA_NEWTRACK);
    UI_MENU_ADD("Calibrate", MA_CALIBRATE);
    UI_MENU_ADD(s_lbl_units, MA_UNITS);
    UI_MENU_ADD("Export (BLE)", MA_EXPORT);
#if CFG_HAS_BLE_RC
    UI_MENU_ADD("Live to phone", MA_LIVE);
#endif
    UI_MENU_ADD("Diagnostics", MA_DIAG);
    UI_MENU_ADD("Sessions", MA_SESSIONS);
    UI_MENU_ADD(s_lbl_disp, MA_DISPLAY);
    UI_MENU_ADD("Sleep now", MA_SLEEP);
#undef UI_MENU_ADD

    s_model.menu_n = n;
}

static void menu_scroll_to_sel(void)
{
    if (s_model.menu_sel < s_model.menu_top) {
        s_model.menu_top = s_model.menu_sel;
    } else if (s_model.menu_sel >= (uint8_t)(s_model.menu_top + UI_MENU_VISIBLE_ROWS)) {
        s_model.menu_top = (uint8_t)(s_model.menu_sel - UI_MENU_VISIBLE_ROWS + 1);
    }
}

static void ui_open_menu(void)
{
    if (s_gspeed_kmh < MENU_LOCK_SPEED_KMH) {
        build_menu();
        s_model.screen   = SCR_MENU;
        s_model.menu_sel = 0;
        s_model.menu_top = 0;
        s_last_input_us  = esp_timer_get_time();
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
    s_model.screen = SCR_RIDING;
    s_dirty        = true;
}

static void menu_select(void)
{
    uint8_t act = s_menu_action[s_model.menu_sel];
    switch (act) {
    case MA_MODE:
        s_mode        = (s_mode == MODE_DRAG) ? (uint8_t)MODE_LAP : (uint8_t)MODE_DRAG;
        s_model.mode  = s_mode;
        s_model.page  = 0; /* §22.6: a mode switch resets the screen */
        ui_send_cmd(CMD_SET_MODE, s_mode, 0);
        snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
        break;
    case MA_LAYOUT:
        /* The venue's layout list is not plumbed to the ui yet; select "Auto" (layout id 0). */
        ui_send_cmd(CMD_SET_LAYOUT, 0, 0);
        ESP_LOGI(TAG, "menu: Layout -> Auto (venue layout list TBD)");
        break;
    case MA_CALIBRATE:
        ui_send_cmd(CMD_CALIB_ORIENT, 0, 0); /* pipeline drops it until the calib session lands */
        ESP_LOGI(TAG, "menu: Calibrate -> CMD_CALIB_ORIENT");
        break;
    case MA_UNITS:
        s_cfg.units = (s_cfg.units == CFG_UNITS_MPH) ? (uint8_t)CFG_UNITS_KMH : (uint8_t)CFG_UNITS_MPH;
        (void)lt_cfg_save(&s_cfg);
        snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
                 s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
        break;
    case MA_DISPLAY:
        s_cfg.display.live_clock = !s_cfg.display.live_clock;
        (void)lt_cfg_save(&s_cfg);
        snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
                 s_cfg.display.live_clock ? "on" : "off");
        break;
    case MA_NEWTRACK:
        ESP_LOGW(TAG, "menu: New track not implemented (plan 05)");
        break;
    case MA_EXPORT:
        ESP_LOGW(TAG, "menu: Export (BLE) not implemented (plan 06)");
        break;
    case MA_LIVE:
        ESP_LOGW(TAG, "menu: Live to phone not implemented (plan 06)");
        break;
    case MA_DIAG:
        ESP_LOGW(TAG, "menu: Diagnostics export not implemented (§17.10, plan 05)");
        break;
    case MA_SESSIONS:
        ESP_LOGW(TAG, "menu: Sessions ops not implemented (plan 05)");
        break;
    case MA_SLEEP:
        ESP_LOGW(TAG, "menu: Sleep now -- hold MODE 3 s to confirm (not implemented, plan 07)");
        break;
    default:
        break;
    }
    s_dirty = true;
}

/* ---- button gestures ---- */

static void btn_short(uint8_t bit)
{
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
        if (bit == BTN_UP) {
            s_model.page = (uint8_t)((s_model.page + 2) % 3); /* previous page (wrap) */
            s_dirty      = true;
        } else if (bit == BTN_DOWN) {
            s_model.page = (uint8_t)((s_model.page + 1) % 3); /* next page (wrap) */
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
    if (s_model.screen == SCR_MENU && s_menu_action[s_model.menu_sel] == MA_SLEEP) {
        ESP_LOGW(TAG, "menu: Sleep now confirmed -- not implemented (plan 07)");
        ui_exit_menu();
    }
}

static void btn_combo(void)
{
    /* UP+DOWN held 2 s -> full refresh now (§20.8). No panel yet: log + force one render. */
    ESP_LOGI(TAG, "full refresh requested (UP+DOWN)");
    s_dirty = true;
}

/* Apply a new button mask sampled at `now`, driving press/release edges. */
static void process_mask(uint8_t mask, int64_t now)
{
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
        s_combo_fired   = true;
        s_last_input_us = now;
        btn_combo();
    }
}

/* ---- event -> model (§20.3 / §20.4) ---- */

static void show_venue_oneshot(int64_t now)
{
    if (s_model.screen != SCR_RIDING) {
        return; /* don't interrupt the menu or another one-shot */
    }
    s_model.screen     = SCR_ONESHOT;
    s_model.oneshot    = ONESHOT_VENUE;
    s_oneshot_until_us = now + (int64_t)ONESHOT_VENUE_MS * 1000;
}

static void handle_event(const event_t *e, int64_t now)
{
    switch (e->type) {
    case EV_LAP_COMPLETE: {
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
        break;
    }
    case EV_SECTOR:
        s_model.cur_sector_idx = (uint8_t)e->arg16;
        s_model.cur_ms_at_gate = e->arg32;
        s_model.sector_delta_ms = (int32_t)e->arg32b;
        s_dirty                = true;
        break;
    case EV_VENUE_FOUND:
        snprintf(s_model.venue_name, sizeof s_model.venue_name, "V%u", (unsigned)e->arg16);
        s_model.layout_name[0] = '\0'; /* venue phase: render shows venue_name */
        show_venue_oneshot(now);
        s_dirty = true;
        break;
    case EV_LAYOUT_LOCKED:
        snprintf(s_model.layout_name, sizeof s_model.layout_name, "L%u", (unsigned)e->arg16);
        show_venue_oneshot(now); /* layout phase: render shows layout_name (non-empty) */
        s_dirty = true;
        break;
    case EV_FIX_LOST:
        s_fix_lost = true;
        s_dirty    = true;
        break;
    case EV_FIX_OK:
        s_fix_lost = false;
        s_dirty    = true;
        break;
    case EV_MOTION:
        /* Proxy: no per-fix speed is broadcast to the ui yet, so moving conservatively locks the
         * menu (== the lock threshold). EV_STILL clears it. Flagged as a coarse gate. */
        s_gspeed_kmh = MENU_LOCK_SPEED_KMH;
        break;
    case EV_STILL:
        s_gspeed_kmh = 0;
        break;
    case EV_DRAG_ARMED:
        s_model.drag_armed = true;
        s_model.drag_n     = 0;
        s_dirty            = true;
        break;
    case EV_DRAG_LAUNCH:
        s_model.drag_armed = false;
        s_dirty            = true;
        break;
    case EV_DRAG_GATE:
        if (s_model.drag_n < DRAG_MAX_GATES) {
            drag_row_t *r = &s_model.drag[s_model.drag_n++];
            memset(r, 0, sizeof *r);
            /* gate-id -> §6.6 label map needs the drag cfg gate list (not plumbed to ui yet); a
             * compact "G<id>" placeholder is enough for the no-panel dirty-box log this milestone. */
            snprintf(r->label, sizeof r->label, "G%u", (unsigned)e->arg16);
            r->t_ms    = e->arg32;
            r->present = true;
        }
        s_dirty = true;
        break;
    case EV_DRAG_DONE:
        s_dirty = true;
        break;
    default:
        break;
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

static void render_now(void)
{
    screens_render(&s_fb, &s_model);
    ESP_LOGI(TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u", (unsigned)s_model.screen,
             (unsigned)s_model.page, (unsigned)s_fb.dirty.x0, (unsigned)s_fb.dirty.y0,
             (unsigned)s_fb.dirty.x1, (unsigned)s_fb.dirty.y1);
}

/* ---- task ---- */

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

    fb_init(&s_fb, s_fb_bits, FB_W, FB_H);

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
    render_now();
    esp_task_wdt_reset();

    QueueHandle_t btn_q = ui_buttons_queue();

    for (;;) {
        esp_task_wdt_reset();

        btn_raw_t ev;
        if (btn_q != NULL && xQueueReceive(btn_q, &ev, pdMS_TO_TICKS(UI_TICK_MS)) == pdTRUE) {
            process_mask(ev.mask, ev.mono_us);
            while (xQueueReceive(btn_q, &ev, 0) == pdTRUE) {
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

        /* Coalesce (§20.3): drain the whole event queue before rendering once. */
        event_t e;
        while (g_ui_evt_q != NULL && xQueueReceive(g_ui_evt_q, &e, 0) == pdTRUE) {
            handle_event(&e, now);
        }

        /* Transient one-shot expiry (BOOT/VENUE) -> back to riding. */
        if (s_model.screen == SCR_ONESHOT && s_oneshot_until_us != 0 && now >= s_oneshot_until_us) {
            s_oneshot_until_us = 0;
            s_model.screen     = SCR_RIDING;
            s_dirty            = true;
        }
        /* Menu idle auto-exit (§20.7). */
        if (s_model.screen == SCR_MENU && (now - s_last_input_us) >= (int64_t)MENU_IDLE_MS * 1000) {
            ui_exit_menu();
        }

        update_flags();

        if (s_dirty) {
            render_now();
            s_dirty = false;
        }

        g_hb[HB_UI]++;
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
}
