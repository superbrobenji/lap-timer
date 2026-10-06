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
#include "core/drag.h" /* drag_cfg_t/drag_gate_def_t/drag_cfg_from_user/drag_gate_label (Plan 7c T2/T5) */
#include "core/event.h"
#include "core/ui/canvas.h" /* CANVAS_W/CANVAS_H/MENU_VISIBLE_ROWS: compile-time by PANEL (Plan 7 T3) */
#include "core/ui/model.h" /* pulls in core/ui/render.h: fb_t, fb_init, screens_render, SCR_*, etc. */
#include "core/ui/refresh_policy.h" /* ui_refresh_decide (Plan 7 Task 6): pure partial/full/none decision */
#include "core/ui/stats_fold.h" /* session_max_t / session_max_fold (Plan 7c T1/T3) */

#include "app/link.h" /* link_peer_present() (#99): the LINK strip glyph */
#include "app/lt_assert.h"
#include "app/lt_err.h"
#include "app/lt_ipc.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"
#include "app/pipeline.h" /* pipeline_best_snapshot / pipeline_lap_at / pipeline_lap_count (Plan 7c T3) */
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

/* ---- framebuffer (spec §4.8: sized by the compile-time canvas -- 256x122/8 = 3904 B on ws213v4,
 * 296x128/8 = 4736 B on ws29v2) ----
 * Pre-review fix (ruling R-5): FB_STRIDE/FB_H used to be pinned to the 296x128 (ws29v2) worst case
 * "so one build of this file holds either panel's canvas" -- but since Plan 7 T3 the canvas is
 * itself a compile-time choice (CANVAS_W/CANVAS_H, core/ui/canvas.h, selected by PANEL), so one
 * build of ui.c never holds the other panel's canvas anyway, and fb_init(&s_fb, ..., CANVAS_W,
 * CANVAS_H) already only ever touches the first (CANVAS_W/8)*CANVAS_H bytes of whatever it's
 * given. The worst-case sizing bought nothing but wasted static DRAM on every ws213v4 build
 * (including s_fb_prev_bits below, added by Plan 7c T7) -- FB_STRIDE/FB_H now just alias
 * CANVAS_W/CANVAS_H directly. */
#define FB_STRIDE (CANVAS_W / 8)
#define FB_H      CANVAS_H

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

/* ---- one-shots (spec §20.6, §17.6: boot + venue banners show ~2-3 s) ---- */
/* Plan 7c T8 (design §6): 2000 -> 3000 so the +1 s boot_refmt_check() re-format (below) has time
 * to land -- and be seen -- before the one-shot auto-reverts to riding. */
#define ONESHOT_BOOT_MS  3000
#define ONESHOT_VENUE_MS 2000
#define ONESHOT_OTAFAIL_MS 3000   /* §19.4: "UPDATE FAILED, REVERTED" for 3 s after the BOOT one-shot */
#define OTA_STALE_MS 60000   /* fix round 1 (Important #2): no EV_OTA for 60 s (>> the ~5 %-step
                               * cadence) while UPDATING shows -> the push died; revert to riding */

/* ---- display refresh ladder (spec §20.3, Plan 7 Task 7) ----
 * DISP_FAIL_STREAK_MAX consecutive disp_refresh()/disp_reinit()-retry failures mark the panel
 * SYS_DISP_DEAD; DISP_DEAD_RETRY_US then paces the reinit probe while dead (ruling R5: the first
 * retry lands 300 s after the flag was set, never immediately). */
#define DISP_FAIL_STREAK_MAX 3
#define DISP_DEAD_RETRY_US   (300LL * 1000000)
/* Mirrors refresh_policy.c's RF_THROTTLE_MIN_US (30 s): gates how often ui_loop_iter() retries a
 * throttle-deferred refresh (s_refresh_pending, Important #2) so it does not re-render every
 * UI_TICK_MS while waiting -- the actual none-vs-partial decision still lives solely in
 * ui_refresh_decide(), never duplicated here. */
#define DISP_THROTTLE_RETRY_US (30LL * 1000000)

/* Menu item actions; the visible order is built in build_menu() (spec §20.7's list). */
enum {
    MA_MODE = 0,
    MA_LAYOUT,
    MA_NEWTRACK,
    MA_CALIBRATE,
    MA_UNITS,
    MA_DIST,
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
static uint8_t        s_fb_bits[FB_STRIDE * FB_H]; /* 3904 B on ws213v4, 4736 B on ws29v2 */
static fb_t           s_fb;

/* Plan 7c T7 (design §5): the last frame the panel actually accepted, same compile-time-canvas
 * sizing as s_fb_bits above (FB_STRIDE * FB_H, which is CANVAS_W/CANVAS_H under the hood -- see
 * the framebuffer block above, ruling R-5). render_and_refresh() diffs the freshly rendered s_fb
 * against this on every render (fb_diff_rect, Task 1) into s_diff; do_refresh() copies s_fb_bits
 * over it after a refresh the panel actually accepted -- never on failure, never on a throttled/
 * skipped render, so a render that was throttled is still "different from the panel" next time and
 * gets refreshed -- and the boot block does the same copy right after a successful disp_init(). */
static uint8_t   s_fb_prev_bits[FB_STRIDE * FB_H]; /* 3904 B on ws213v4, 4736 B on ws29v2 */
static fb_t      s_fb_prev;
static fb_rect_t s_diff; /* bounding box from the most recent fb_diff_rect() call, below */

/* Session-max accumulator (Plan 7c T3, design §2): folded from every completed lap's lap_stats_t
 * in handle_lap_result()/fold_lap_stats() below; drives the LAP page 2 stats grid. Zero-initialised
 * static storage (max of nothing == 0), same reset-at-boot-only lifetime as laps_total/laps_valid. */
static session_max_t s_session_max;

static cfg_t   s_cfg;         /* ui's working config copy (cmd.c uses the same load-from-NVS pattern) */
/* Plan 7c T5 (design §3): the DRAG gate table + bench list, built once at boot from s_cfg
 * (drag_cfg_from_user) and rebuilt in menu_do_units() so a units change relabels the SPEED_FROM0
 * benches too. drag_rows_refill() below reads it for every row's label/kind. */
static drag_cfg_t s_drag_cfg;
/* M5 (final review): the trap (1/4-mile) gate is a RULE -- the DRAG_DIST gate with the largest
 * distance `a` in s_drag_cfg -- not the hardcoded a==40234 literal (only true for the shipped
 * default table). Recomputed by drag_rows_refill() below, from whichever gate table is live, every
 * time it runs; row_from_gate() reads it. 0 = no DRAG_DIST gate configured (never hit by id, since
 * gate ids start at 1). */
static uint8_t s_trap_id;
static uint8_t s_mode;        /* MODE_LAP / MODE_DRAG (mirrors s_model.mode) */
static uint16_t s_gspeed_kmh; /* menu-lock proxy from EV_MOTION/EV_STILL (see handle_event) */
static bool    s_fix_lost;    /* EV_FIX_LOST/OK -> SCR_SYS_GPS_NOFIX in the fault strip */
static bool    s_dirty;       /* model changed since last render -> render once (§20.3) */
static int64_t s_oneshot_until_us; /* auto-revert time for a transient one-shot (BOOT/VENUE); 0 = none */
static bool    s_rollback_pending; /* EV_OTA ROLLED_BACK seen before/while BOOT showed: show OTA_FAIL next */
static int64_t s_ota_seen_us;      /* fix round 1 (Important #2): esp_timer stamp of the last non-
                                     * terminal EV_OTA; ui_loop_iter() reverts a stale OTA screen */
static int64_t s_last_input_us;    /* last button activity -> menu idle timeout */
/* Plan 7c T8 (design §6): s_boot_arm_us is the esp_timer stamp the BOOT one-shot was armed at (0 =
 * not BOOT, e.g. SAFE mode); boot_refmt_check() (below) uses it to fire its +1 s re-format exactly
 * once (s_boot_refmt_done) -- by then the pipeline has reported GPS/IMU. */
static int64_t s_boot_arm_us;
static bool    s_boot_refmt_done;

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
static bool     s_screen_changed;  /* the next render replaces the whole screen (one-shot<->riding,
                                     * menu enter/exit, page change) -- never a partial, regardless
                                     * of motion (bench B-F1, ruling B-9) */
static bool     s_refresh_pending; /* fix round 2 (Important #2): RF_NONE returned while throttled
                                     * -- a refresh is owed once the 30 s throttle window elapses */

/* Live lap clock (Plan 7c T6, design §4): s_lap_start_mono_us is the mono_us stamp of the S/F
 * crossing that started the lap now running (0 = none), set by handle_lap_complete() and cleared
 * when the ui itself learns riding has left LAP mode (menu_do_mode(), below). clock_tick() (below)
 * advances s_model.cur_ms/cur_running from it once a second while display.live_clock is on and
 * LAP page 0 is showing. s_clock_tick marks a render that a clock tick alone caused, so
 * do_refresh()/render_and_refresh() can keep a ticking clock's partials out of the full-refresh
 * ladder's accounting (design §4 last bullet). */
static int64_t s_lap_start_mono_us;
static int64_t s_last_clock_us;
static bool    s_clock_tick;

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
static char    s_lbl_dist[16];
static char    s_lbl_disp[20];

/* ---- helpers ---- */

static void ui_send_cmd(uint8_t type, uint8_t arg8, uint16_t arg16)
{
    LT_ASSERT_VOID(type <= CMD_TYPE_LAST, UI_APP_ASSERT_CODE);   /* a valid §4.4 command type (M1); the ui never actually posts CMD_SIM_SCENARIO (sim-only), it just shares pipeline.c's bound */
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
    LT_ASSERT_VOID(s_cfg.dist_units <= CFG_DIST_FT, UI_APP_ASSERT_CODE);        /* label depends on it */
    uint8_t n = 0;
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
             s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    snprintf(s_lbl_dist, sizeof s_lbl_dist, "Dist: %s", s_cfg.dist_units == CFG_DIST_FT ? "ft" : "m");
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
             s_cfg.display.live_clock ? "on" : "off");

    menu_add(&n, s_lbl_mode, MA_MODE);
    menu_add(&n, "Layout", MA_LAYOUT);
    menu_add(&n, "New track", MA_NEWTRACK);
    menu_add(&n, "Calibrate", MA_CALIBRATE);
    menu_add(&n, s_lbl_units, MA_UNITS);
    menu_add(&n, s_lbl_dist, MA_DIST);
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
        s_screen_changed = true; /* whole-screen replacement (ruling B-9): never a partial */
        s_dirty          = true;
    } else {
        /* §20.7: above the lock speed the menu is ignored; the SCR_UI_MOVING bit (update_flags(),
         * ICON_MOVING in the fault-icon strip) is already on while this holds, so no extra glyph
         * work is needed here -- just log it. */
        ESP_LOGI(TAG, "menu locked (gspeed proxy %u >= %u km/h)", s_gspeed_kmh,
                 (unsigned)MENU_LOCK_SPEED_KMH);
    }
}

static void ui_exit_menu(void)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);   /* leaving a valid screen */
    s_model.screen   = SCR_RIDING;
    s_wants_full     = true; /* menu/one-shot exit: full refresh (§20.3 full-refresh triggers) */
    s_screen_changed = true; /* whole-screen replacement (ruling B-9): never a partial */
    s_dirty          = true;
}

/* Every UI-driven cfg change is a read-modify-write (T-D): reload the blob from NVS immediately
 * before mutating one field, so a CONFIG_SET the dev controller persisted since this task's boot
 * load is NOT silently reverted by writing back a stale s_cfg (cmd.c is the other writer). On a
 * load failure s_cfg keeps its last-known-good value (lt_cfg_load leaves it untouched), which is
 * the same fall-back the boot seed uses. */

/* Applies a new riding mode to s_mode/s_model.mode, resets the screen to page 0 (§22.6), and --
 * when leaving LAP mode -- stops the live lap clock. Shared by menu_do_mode (local MA_MODE toggle)
 * and ui_reload_cfg (I2, final review ruling B-6: a remote CONFIG_SET's mode flip must behave
 * identically, not just relabel the menu while page 0 keeps a stale LAP clock on a now-DRAG
 * screen). Plan 7c T6 (design §4): leaving LAP mode means no lap is running any more -- the ui
 * learns this directly here (no EV_* reaches it for a venue/layout loss or a lap reset), so stop
 * the stopwatch now rather than let a later switch back to LAP resume ticking from a stale start
 * stamp; clock_tick() itself already gates on mode == SCR_MODE_LAP. */
static void ui_apply_mode(uint8_t new_mode)
{
    LT_ASSERT_VOID(new_mode == MODE_LAP || new_mode == MODE_DRAG, UI_APP_ASSERT_CODE);
    s_mode        = new_mode;
    s_model.mode  = s_mode;
    s_model.page  = 0; /* §22.6: a mode switch resets the screen */
    s_wants_full     = true; /* I1 (bench-fix review): whole-screen replacement + page change
                               * (§20.3) -- covers BOTH callers: the local menu toggle
                               * (menu_do_mode) and a remote CONFIG_SET mode flip
                               * (ui_reload_cfg -> EV_CFG_CHANGED), neither of which previously
                               * requested a full here. */
    s_screen_changed = true; /* ruling B-9: never a partial, regardless of motion -- the riding
                               * screen's LAP<->DRAG swap + page-0 reset is exactly the kind of
                               * whole-screen replacement B-9 forbids as a dirty-rect partial. */
    if (s_mode == MODE_DRAG) {
        s_lap_start_mono_us = 0;
        s_model.cur_running = false;
        s_model.cur_ms      = 0;
    }
    LT_ASSERT_VOID(s_model.mode == s_mode, UI_APP_ASSERT_CODE);
}

/* MA_MODE: toggle Lap/Drag (ui_apply_mode), persist cfg.mode, push the mode command, refresh the
 * label. (Split verbatim out of menu_select for rule 4.) */
static void menu_do_mode(void)
{
    uint8_t new_mode = (s_mode == MODE_DRAG) ? (uint8_t)MODE_LAP : (uint8_t)MODE_DRAG;
    ui_apply_mode(new_mode);
    (void)lt_cfg_load(&s_cfg);              /* RMW: don't clobber a peer's CONFIG_SET */
    s_cfg.mode    = s_mode;                 /* T-D: cfg.mode is the single source of truth; persist it */
    (void)lt_cfg_save(&s_cfg);
    ui_send_cmd(CMD_SET_MODE, s_mode, 0);
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
}

/* Forward decl: menu_do_units() (below) rebuilds the DRAG gate table on a units change and, if the
 * DRAG screen's rows are the ones showing, refills them immediately so labels/benches follow the
 * new setting the moment riding resumes; drag_rows_refill() itself is defined further down next to
 * the other DRAG row-building helpers (Plan 7c T5). */
static void drag_rows_refill(void);

/* MA_UNITS: toggle km/h<->mph, persist, refresh the label and the model (the menu is showing, so
 * the change appears on screen the moment riding resumes -- Plan 7c T4, design §3). Plan 7c T5:
 * also rebuilds s_drag_cfg (the SPEED_FROM0 bench list is unit-dependent, design §3) and, when the
 * current riding mode is DRAG, refills the rows now so they are already correct once the menu
 * exits. (Split verbatim out of menu_select.) */
static void menu_do_units(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D): reload before mutating + saving */
    s_cfg.units = (s_cfg.units == CFG_UNITS_MPH) ? (uint8_t)CFG_UNITS_KMH : (uint8_t)CFG_UNITS_MPH;
    (void)lt_cfg_save(&s_cfg);
    /* I1 (final review, ruling B-5): units feed the pipeline's own drag gate table
     * (SPEED_FROM0 benches, design §3) -- a local toggle must reload it exactly like a remote
     * CONFIG_SET does (cmd.c cfg_change_notify posts the same command), or the engine keeps
     * firing the old unit's gates until the next reboot. */
    ui_send_cmd(CMD_CONFIG_RELOAD, 0, 0);
    s_model.units = s_cfg.units;
    s_dirty       = true;
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s",
             s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    drag_cfg_from_user(&s_cfg, &s_drag_cfg);
    if (s_model.mode == SCR_MODE_DRAG) {
        drag_rows_refill();
    }
}

/* MA_DIST (#96): toggle the DIST-gate label unit (m <-> ft), persist, refresh the label and the
 * model, and -- when DRAG is the current riding mode -- refill the rows now so they already show
 * the new unit once the menu exits (same pattern as menu_do_units() above). This does not touch
 * s_drag_cfg: dist_units only renames DIST gates for display (drag_gate_label), it never changes
 * which gates fire or their a/b values, unlike a units (km/h<->mph) toggle. */
static void menu_do_dist(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D): reload before mutating + saving */
    s_cfg.dist_units = (s_cfg.dist_units == CFG_DIST_FT) ? (uint8_t)CFG_DIST_M : (uint8_t)CFG_DIST_FT;
    (void)lt_cfg_save(&s_cfg);
    ui_send_cmd(CMD_CONFIG_RELOAD, 0, 0);   /* the pipeline mirrors cfg; labels are ui-side */
    s_model.dist_units = s_cfg.dist_units;
    s_dirty            = true;
    snprintf(s_lbl_dist, sizeof s_lbl_dist, "Dist: %s", s_cfg.dist_units == CFG_DIST_FT ? "ft" : "m");
    if (s_model.mode == SCR_MODE_DRAG) {
        drag_rows_refill();
    }
}

/* MA_DISPLAY: toggle the live clock, persist, refresh the label. (Split verbatim out of
 * menu_select.) s_dirty is set explicitly here (Plan 7c T6, design §4), not left to menu_select()'s
 * own trailing set, so the card re-renders with/without CUR the moment riding resumes. */
static void menu_do_display(void)
{
    (void)lt_cfg_load(&s_cfg);              /* RMW (T-D): reload before mutating + saving */
    s_cfg.display.live_clock = !s_cfg.display.live_clock;
    (void)lt_cfg_save(&s_cfg);
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s",
             s_cfg.display.live_clock ? "on" : "off");
    s_dirty = true;
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
    case MA_DIST:    menu_do_dist();    break;
    case MA_DISPLAY: menu_do_display(); break;
    /* The venue's layout list is not plumbed to the ui yet; select "Auto" (layout id 0). */
    case MA_LAYOUT:    ui_send_cmd(CMD_SET_LAYOUT, 0, 0); ESP_LOGI(TAG, "menu: Layout -> Auto (per-venue layout list: issue #98)"); break;
    /* pipeline drops CMD_CALIB_ORIENT until the calib session lands. */
    case MA_CALIBRATE: ui_send_cmd(CMD_CALIB_ORIENT, 0, 0); ESP_LOGI(TAG, "menu: Calibrate -> CMD_CALIB_ORIENT"); break;
    case MA_NEWTRACK: ESP_LOGW(TAG, "menu: New track not implemented (issue #97)"); break;
    case MA_EXPORT:   ESP_LOGW(TAG, "menu: Export (BLE) not implemented (plan 06)"); break;
    case MA_LIVE:     ESP_LOGW(TAG, "menu: Live to phone not implemented (plan 06)"); break;
    case MA_DIAG:     ESP_LOGW(TAG, "menu: Diagnostics export not implemented (§17.10, plan 05)"); break;
    case MA_SESSIONS: ESP_LOGW(TAG, "menu: Sessions ops not implemented (plan 05)"); break;
    case MA_SLEEP:    ESP_LOGW(TAG, "menu: Sleep now -- hold MODE 3 s to confirm (not implemented, Plan 6.2 power states)"); break;
    default: break;
    }
    s_dirty = true;
}

/* ---- button gestures ---- */

/* Bench/diagnostic trace of every classified press (§20.8 debounce check on real switches): one
 * line per registered short/long/vlong/combo, so a bounce that double-registers shows in the log
 * even when the renders coalesce several presses into one refresh. */
static const char *btn_name(uint8_t bit)
{
    LT_ASSERT_RET(bit == BTN_MODE || bit == BTN_UP || bit == BTN_DOWN, UI_APP_ASSERT_CODE, "?");
    LT_ASSERT_RET((bit & (uint8_t)(bit - 1u)) == 0u, UI_APP_ASSERT_CODE, "?");   /* exactly one bit */
    return bit == BTN_MODE ? "MODE" : (bit == BTN_UP ? "UP" : "DOWN");
}

static void btn_short(uint8_t bit)
{
    LT_ASSERT_VOID(bit == BTN_MODE || bit == BTN_UP || bit == BTN_DOWN, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);       /* dispatches on it below */
    LT_ASSERT_VOID(s_model.oneshot <= ONESHOT_NEWTRACK, UI_APP_ASSERT_CODE); /* one-shot selector read below */
    ESP_LOGI(TAG, "btn: short %s", btn_name(bit));
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
            s_model.page     = (uint8_t)((s_model.page + 2) % 3); /* previous page (wrap) */
            s_wants_full     = true; /* page change: full refresh (§20.3 full-refresh triggers) */
            s_screen_changed = true; /* whole-screen replacement (ruling B-9): never a partial */
            s_dirty          = true;
        } else if (bit == BTN_DOWN) {
            s_model.page     = (uint8_t)((s_model.page + 1) % 3); /* next page (wrap) */
            s_wants_full     = true;
            s_screen_changed = true;
            s_dirty          = true;
        }
        if ((bit == BTN_UP || bit == BTN_DOWN) && s_model.mode == SCR_MODE_DRAG) {
            /* Plan 7c T5 (design §3): DRAG rows are filled per page -- rebuild for the new one. */
            drag_rows_refill();
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
    ESP_LOGI(TAG, "btn: long %s", btn_name(bit));
    if (s_model.screen == SCR_MENU) {
        /* §20.7: long MODE backs/exits -- except on "Sleep now", which confirms on vlong (3 s). */
        if (s_menu_action[s_model.menu_sel] != MA_SLEEP) {
            ui_exit_menu();
        }
    } else if (s_model.screen == SCR_ONESHOT) {
        if (s_model.oneshot == ONESHOT_OTA) return;   /* never dismiss an update in progress */
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
    ESP_LOGI(TAG, "btn: vlong %s", btn_name(bit));
    /* fix round 1 (Important #3): screen-correct guard -- s_model.oneshot is only meaningful while
     * screen == SCR_ONESHOT (model.h), and it is never cleared on the way out of one, so testing it
     * unconditionally (as the SCR_MENU branch below used to) reads a stale ONESHOT_OTA left behind
     * by an earlier update and silently swallows a real "Sleep now" confirm. btn_long's one-shot
     * branch already makes SCR_MENU-while-OTA unreachable, so this can never fire today; it mirrors
     * btn_long's own guard so the invariant holds if that ever changes. */
    if (s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_OTA) return;
    if (s_model.screen == SCR_MENU && s_menu_action[s_model.menu_sel] == MA_SLEEP) {
        ESP_LOGW(TAG, "menu: Sleep now confirmed -- not implemented (plan 07)");
        ui_exit_menu();
    }
}

static void btn_combo(void)
{
    /* UP+DOWN held 2 s -> full refresh now (§20.8, ghost clearing). */
    ESP_LOGI(TAG, "btn: combo UP+DOWN");
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
    s_wants_full       = true; /* M5 (bench-fix review): keep a full owed for the next still
                                 * moment if a throttled downgrade only yields a partial here --
                                 * matches the other four replacement sites, none of which set
                                 * screen_changed without also setting wants_full. */
    s_screen_changed   = true; /* riding -> one-shot: whole-screen replacement (ruling B-9) */
}

/* EV_OTA (ui-only). Non-terminal phases show/update the persistent OTA one-shot (no auto-revert:
 * the supervisor reboots 2 s after REBOOTING; ABORTED returns to riding; a stale screen with no
 * update for OTA_STALE_MS also reverts, see ui_loop_iter()). ROLLED_BACK's screen depends on what
 * is showing when it arrives (fix round 1, Critical #1 ruling): deferred to OTA_FAIL right after
 * BOOT finishes if BOOT is up now; dropped silently (the errlog already has E_OTA_ROLLBACK) while
 * the persistent SAFE one-shot is up; shown immediately otherwise (riding, menu, or any other
 * screen), replacing whatever was there.
 *
 * M2 (final review): the same SAFE guard now also covers the non-terminal path, below -- a push
 * that starts (or is already running) while SAFE MODE is showing must not evict it for a progress
 * bar the operator does not need in SAFE mode. Computed once, up top, since both branches need it.
 * This also closes the ABORTED/stale-revert gap M2 found for free: once the screen never becomes
 * ONESHOT_OTA, `on_ota` below is never true while SAFE is up, so EV_OTA_ABORTED's
 * `if (on_ota) ui_exit_menu()` and ui_loop_iter()'s own OTA_STALE_MS check (gated on
 * oneshot == ONESHOT_OTA) can never fire against it either -- neither needs its own guard. */
static void handle_ota(const event_t *e, int64_t now)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);
    uint8_t phase   = e->flags;
    uint8_t pct     = e->arg16 > 100u ? 100u : (uint8_t)e->arg16;
    bool    on_safe = s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_SAFE;
    if (phase == EV_OTA_ROLLED_BACK) {
        bool on_boot = s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_BOOT;
        if (on_boot) { s_rollback_pending = true; return; }   /* defer to the BOOT expiry */
        if (on_safe) return;                /* persistent safety screen outranks it */
        s_model.screen     = SCR_ONESHOT;
        s_model.oneshot    = ONESHOT_OTA_FAIL;
        s_oneshot_until_us = now + (int64_t)ONESHOT_OTAFAIL_MS * 1000;
        s_wants_full       = true;
        s_screen_changed   = true;          /* whole-screen replacement (ruling B-9) */
        s_dirty            = true;
        return;
    }
    if (on_safe) return;                    /* M2: SAFE outranks a non-terminal update too */
    bool on_ota = s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_OTA;
    if (phase == EV_OTA_ABORTED) {
        if (on_ota) ui_exit_menu();       /* -> SCR_RIDING, full refresh (ruling B-9) */
        return;
    }
    s_ota_seen_us     = now;              /* I2 (fix round 1): staleness stamp, non-terminal only */
    s_model.ota_pct   = pct;
    s_model.ota_phase = phase;
    if (!on_ota) {
        s_model.screen     = SCR_ONESHOT;
        s_model.oneshot    = ONESHOT_OTA;
        s_oneshot_until_us = 0;           /* persistent: no auto-revert */
        s_wants_full       = true;
        s_screen_changed   = true;        /* whole-screen replacement (ruling B-9) */
    }
    s_dirty = true;
}

/* Clears the page 1 row 4 sector-delta cache (spec 7b §3). Ruling B7b-1 (bench finding 1): called
 * only once, at session start (ui_task()'s init) -- NOT per lap. The last-lap sector deltas
 * persist across the line (a rider still wants to see the lap that just ended); each EV_SECTOR
 * simply overwrites its own slot as the new lap's gates arrive. */
static void clear_last_sector_deltas(void)
{
    LT_ASSERT_VOID((size_t)(LAP_MAX_SECTORS + 1) <=
                       sizeof s_model.have_last_sector_delta / sizeof s_model.have_last_sector_delta[0],
                   UI_APP_ASSERT_CODE);   /* loop bound fits the array */
    for (uint8_t i = 0; i < LAP_MAX_SECTORS + 1; i++) {
        s_model.have_last_sector_delta[i] = false;
    }
    LT_ASSERT_VOID(!s_model.have_last_sector_delta[LAP_MAX_SECTORS], UI_APP_ASSERT_CODE);   /* every slot, including the last, is now clear */
}

/* LAP page 1 (design §2): copies the pipeline's best-sector/theoretical-best snapshot into the
 * model. A snapshot failure (pipeline_best_snapshot() only ever returns 0 today, but the contract
 * allows otherwise) leaves the model's existing values in place rather than clobbering them with a
 * half-read. Split out of handle_lap_result() (rule 4). */
static void copy_best_snapshot(void)
{
    pipe_best_t pb;
    LT_ASSERT_VOID(sizeof pb.best_sector_ms == sizeof s_model.best_sector_ms, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(sizeof pb.have_best_sector == sizeof s_model.have_best_sector, UI_APP_ASSERT_CODE);
    if (pipeline_best_snapshot(&pb) != 0) {
        return;
    }
    memcpy(s_model.best_sector_ms, pb.best_sector_ms, sizeof s_model.best_sector_ms);
    memcpy(s_model.have_best_sector, pb.have_best_sector, sizeof s_model.have_best_sector);
    s_model.best_n_sectors = pb.n_sectors;
    s_model.theo_best_ms   = pb.theo_ms;
    s_model.have_theo      = pb.theo_ms != 0;
}

/* LAP page 2 (design §2): folds the lap that just completed into the session-max accumulator and
 * derives the model's stats-grid fields from it. `lean_*_deg` = cdeg/100 (both fields are already
 * non-negative magnitudes, stats_step/pipeline.c); `*_g_e2` = round(e3/10) (also non-negative).
 * Split out of handle_lap_result() (rule 4). */
static void fold_lap_stats(void)
{
    int idx = pipeline_lap_count() - 1;
    LT_ASSERT_VOID(idx >= 0, UI_APP_ASSERT_CODE);   /* a genuine lap just completed and was published first */
    lap_result_t lr;
    if (pipeline_lap_at(idx, &lr) != 0) {
        return;
    }
    session_max_fold(&s_session_max, &lr.stats);
    s_model.max_speed_cms = s_session_max.max_speed_cms;
    s_model.lean_l_deg = (uint8_t)(s_session_max.max_lean_l_cdeg / 100);
    s_model.lean_r_deg = (uint8_t)(s_session_max.max_lean_r_cdeg / 100);
    s_model.lat_g_e2   = (uint16_t)((s_session_max.max_glat_e3 + 5) / 10);
    s_model.acc_g_e2   = (uint16_t)((s_session_max.max_gacc_e3 + 5) / 10);
    s_model.brk_g_e2   = (uint16_t)((s_session_max.max_gbrake_e3 + 5) / 10);
    LT_ASSERT_VOID(s_model.lean_l_deg <= 90 && s_model.lean_r_deg <= 90, UI_APP_ASSERT_CODE);   /* a lean angle never exceeds 90 deg */
}

/* Genuine (non-out) lap completion: PREV always takes the time; BEST/laps_valid update only when
 * the engine marked the lap valid. `valid` reads LAP_F_VALID straight off the emitted event -- the
 * engine already computes it (lap.c's complete_lap(): `flags |= LAP_F_VALID` iff none of
 * GPS_LOST/PIT/INCOMPLETE/OUT_LAP/TOO_LONG are set) and stamps it into the very flags this event
 * carries, so deriving it a second time here would just duplicate that logic. Split out of
 * handle_lap_complete() to keep it under the 60-line cap (rule 4). */
static void handle_lap_result(uint32_t lap_ms, uint8_t flags, int32_t lap_delta_ms)
{
    bool valid    = (flags & LAP_F_VALID) != 0;
    bool had_best = s_model.have_best;   /* captured before the BEST update below (spec 7b §3) */
    s_model.have_prev = true;
    s_model.prev_ms   = lap_ms;
    if (valid && (!s_model.have_best || lap_ms < s_model.best_ms)) {
        s_model.best_ms   = lap_ms;
        s_model.have_best = true;
        s_model.new_best  = true;
    } else {
        s_model.new_best = false;
    }
    if (s_model.laps_total < UINT16_MAX) {
        s_model.laps_total++;
    }
    if (valid && s_model.laps_valid < UINT16_MAX) {
        s_model.laps_valid++;
    }
    LT_ASSERT_VOID(s_model.laps_valid <= s_model.laps_total, UI_APP_ASSERT_CODE);   /* valid <= total */
    /* best_ms <= prev_ms only holds when THIS lap was valid -- an invalid lap's time can be
     * anything (e.g. a too-long lap can still read faster than the current best on the clock) and
     * never updates best_ms, so it must not be held to that bound. */
    LT_ASSERT_VOID(!valid || !s_model.have_best || s_model.best_ms <= s_model.prev_ms,
                   UI_APP_ASSERT_CODE);

    /* Event card (spec 7b §3): the big slot shows this lap's delta only if a best already existed
     * before it (else BIG_NONE); lap_no advances to the lap now starting. Ruling B7b-1 (bench
     * finding 1): the sector-detail row is deliberately NOT reset here -- last_sector_delta_ms[]/
     * have_last_sector_delta[] persist across the line, each EV_SECTOR overwriting its own slot as
     * the new lap's gates arrive, so page 1 keeps showing the just-finished lap's deltas until
     * then. */
    s_model.big_kind       = had_best ? (uint8_t)BIG_LAP_DELTA : (uint8_t)BIG_NONE;
    s_model.big_delta_ms   = lap_delta_ms;
    s_model.lap_no         = (uint16_t)(s_model.laps_total + 1u);
    s_model.cur_sector_idx = 0;

    /* Plan 7c T3 (design §2): LAP page 1's best-sector board and page 2's session stats grid, both
     * filled from real pipeline data via the F4 seqlock snapshot readers. pipeline.c's engine_cb
     * (Plan 7c T3 fix 1) publishes s_best/s_laps[] for this EV_LAP_COMPLETE BEFORE queuing the
     * event itself, so this read is guaranteed to see this lap's data, not the previous one's --
     * a structural ordering the producer enforces, not a timing assumption made here. */
    copy_best_snapshot();
    fold_lap_stats();
}

/* EV_LAP_COMPLETE -> model (split verbatim out of handle_event for rule 4). An out-lap
 * (LAP_F_OUT_LAP: the engine's very first S/F crossing, lap_no==0, ~0 ms elapsed, r.time_ms forced
 * to 0 in lap.c) is not a lap -- it must never touch PREV/BEST/laps_total/laps_valid (it used to,
 * reading as a bogus BEST 0:00.00). Only the CUR gate resets, for the lap that has just started. */
static void handle_lap_complete(const event_t *e)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);   /* drained from g_ui_evt_q, never NULL */
    LT_ASSERT_VOID(s_model.laps_valid <= s_model.laps_total, UI_APP_ASSERT_CODE);   /* invariant on entry */
    /* Plan 7c T6 (design §4): every completed lap's own S/F crossing starts the clock for the lap
     * now beginning -- out-lap included (its crossing starts lap 1) -- so this is stamped before
     * the out-lap check below, which makes handle_lap_result() an early-return for that case.
     * M2 (final review): cur_ms/s_last_clock_us reset here too, else the card's CUR cell would go
     * on showing the JUST-COMPLETED lap's final time for up to a whole second (clock_tick() only
     * overwrites cur_ms once (now - s_last_clock_us) >= 1000000) -- zeroing s_last_clock_us makes
     * the very next clock_tick() (whenever it next runs) land immediately instead of waiting out
     * the rest of the previous lap's tick interval. */
    s_lap_start_mono_us = e->mono_us;
    s_model.cur_ms      = 0;
    s_last_clock_us     = 0;
    if ((e->flags & LAP_F_OUT_LAP) == 0) {
        handle_lap_result(e->arg32, e->flags, (int32_t)e->arg32b);
    }
    /* have_last_sector_delta[]/last_sector_delta_ms[] are untouched by an out-lap too (spec 7b §3,
     * ruling B7b-1): they persist across every lap boundary, out-laps included, and are simply
     * overwritten per sector as EV_SECTOR fires. */
    s_model.cur_sector_idx = 0;
    s_dirty                = true;
}

/* ---- DRAG rows (Plan 7c T5, design §3): rebuilds s_model.drag[]/drag_n from the pipeline's drag
 * snapshot with real §6.6 gate names, for whichever DRAG page (0/1/2) is currently selected. This
 * replaces the old append-only handle_drag_gate(), which only ever fed page 0 with anonymous
 * "G<n>" rows. ---- */

/* Bounded scan of s_drag_cfg.gates[0..n_gates) for the gate whose id matches; NULL when unknown
 * (a stale/corrupt id from a mismatched snapshot -- defensive, should not happen since both sides
 * build their table from the same cfg_t). */
static const drag_gate_def_t *gate_by_id(uint8_t id)
{
    LT_ASSERT_RET(id >= 1u && id <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE, NULL);
    LT_ASSERT_RET(s_drag_cfg.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE, NULL);
    for (uint8_t i = 0; i < s_drag_cfg.n_gates && i < DRAG_MAX_GATES; i++) {
        if (s_drag_cfg.gates[i].id == id) {
            return &s_drag_cfg.gates[i];
        }
    }
    return NULL;
}

/* Fills one drag_row_t from a gate definition + its result (a real per-run gate_res_t for pages
 * 0/1, or a synthetic one built from the session-best record for page 2 -- see drag_fill_page2).
 * Ruling R-4 (Plan 7c T4 fix 1, reaffirmed for T5): trap_cms is stored RAW (res->speed_cms); the
 * display-unit conversion happens only at render time (screens_moto.c's render_dcard_value), so a
 * units toggle after this run still relabels it correctly -- speed_display() is never called here. */
static void row_from_gate(drag_row_t *r, const drag_gate_def_t *g, const drag_gate_res_t *res, bool present)
{
    LT_ASSERT_VOID(r != NULL && g != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(res != NULL, UI_APP_ASSERT_CODE);
    memset(r, 0, sizeof *r);
    if (drag_gate_label(g, s_model.units, s_model.dist_units, r->label, sizeof r->label) < 0) {
        snprintf(r->label, sizeof r->label, "G%u", (unsigned)g->id);   /* fallback: bounded */
    }
    r->present     = present;
    r->is_distance = g->kind == DRAG_BRAKE;
    r->dist_m      = (uint16_t)(res->dist_cm / 100u);
    r->t_ms        = res->time_ms;
    r->has_trap    = (g->id == s_trap_id && res->speed_cms > 0u);   /* M5: rule, not a literal */
    r->trap_cms    = res->speed_cms;
}

/* Pages 0/1 (design §3, merged into one helper by Plan 7c T5 fix 1, review finding 1): walks the
 * current run's gates in table order. hit_only = true: keep only hit gates, present always true,
 * drag_n = number hit (table order == hit order for a normal forward-progressing run, same as the
 * old append-only behaviour). I3 (final review, ruling R-7): both callers (drag_rows_refill's page
 * 0 AND page 1 cases) now pass true -- page 1 used to pass false ("keep every gate, present = hit")
 * but that "all gates, present/absent" listing is gone; the hit_only = false path stays here,
 * exercised by neither caller today, as this helper's documented general contract rather than
 * being cut down to a single fixed argument. */
static void drag_fill_from_run(const drag_result_t *run, bool hit_only)
{
    LT_ASSERT_VOID(run != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(run->n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    uint8_t n = 0;
    for (uint8_t i = 0; i < run->n_gates && i < DRAG_MAX_GATES && n < DRAG_MAX_GATES; i++) {
        const drag_gate_res_t *res = &run->gates[i];
        if (hit_only && !res->hit) {
            continue;
        }
        const drag_gate_def_t *g = gate_by_id(res->gate_id);
        if (g == NULL) {
            continue;
        }
        row_from_gate(&s_model.drag[n], g, res, hit_only ? true : (res->hit != 0));
        n++;
    }
    s_model.drag_n = n;
}

/* Page 2 (design §3, ruling R-7): one row per configured gate that has a session best
 * (have_best[id-1]) -- a gate never hit this session is skipped entirely (I3: page 2 lists
 * hit/best gates only, matching page 0/1's semantics), so `present` passed to row_from_gate is
 * always true here. best_time_ms[id-1] holds the session-best time_ms, EXCEPT for a BRAKE gate
 * where it holds the best (shortest) stopping dist_cm instead (§11.3, confirmed against
 * pipeline.c's publish_drag_snapshot()) -- M9 (final review): the synthetic gate_res_t below feeds
 * that value into ONLY the field the gate's kind actually uses (dist_cm for BRAKE, time_ms for
 * everything else), never both, so row_from_gate's unused field for this row's kind stays a clean
 * 0 rather than a stray time value reinterpreted as a distance or vice versa. speed_cms is left 0:
 * the session-best record carries no trap speed, so has_trap is always false here, which
 * render_drag_gate_list (screens_moto.c) never reads anyway (only page 0's card does). */
static void drag_fill_page2(const pipe_drag_t *d)
{
    LT_ASSERT_VOID(d != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_drag_cfg.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    uint8_t n = 0;
    for (uint8_t j = 0; j < s_drag_cfg.n_gates && j < DRAG_MAX_GATES && n < DRAG_MAX_GATES; j++) {
        const drag_gate_def_t *g = &s_drag_cfg.gates[j];
        if (g->id < 1u || g->id > DRAG_MAX_GATES) {
            continue;   /* defensive: every id checked before indexing best_time_ms[]/have_best[] */
        }
        uint8_t idx = (uint8_t)(g->id - 1u);
        if (!d->have_best[idx]) {
            continue;   /* I3: never hit this session -- not a row on page 2 */
        }
        drag_gate_res_t res;
        memset(&res, 0, sizeof res);   /* row_from_gate reads g->id, not res.gate_id -- no dead store here */
        if (g->kind == DRAG_BRAKE) {
            res.dist_cm = d->best_time_ms[idx];
        } else {
            res.time_ms = d->best_time_ms[idx];
        }
        row_from_gate(&s_model.drag[n], g, &res, true);
        n++;
    }
    s_model.drag_n = n;
}

/* Rebuilds s_model.drag[]/drag_n for whichever page is currently selected, from a fresh pipeline
 * snapshot -- called on every drag event and on every DRAG page change (design §3). A snapshot
 * failure (pipeline_drag_snapshot() only ever returns 0 today, but the contract allows otherwise)
 * leaves the model's existing rows in place rather than clobbering them with a half-read, same
 * policy as copy_best_snapshot() above.
 * I3 (final review, ruling R-7): pages 0 AND 1 now both list hit gates only (page 1 used to list
 * every configured gate, present/absent) -- page 0's own card semantics are unchanged, it was
 * always hit-only. M5: s_trap_id (the DRAG_DIST gate with the largest distance `a` in s_drag_cfg,
 * 0 if none) is recomputed here, once per refill, from whichever gate table is live -- row_from_gate
 * reads it instead of a hardcoded gate-10/40234 literal. */
static void drag_rows_refill(void)
{
    LT_ASSERT_VOID(s_model.page < 3u, UI_APP_ASSERT_CODE);   /* dispatches on it below */
    LT_ASSERT_VOID(s_drag_cfg.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    s_trap_id      = 0;
    uint16_t trap_a = 0;
    for (uint8_t i = 0; i < s_drag_cfg.n_gates && i < DRAG_MAX_GATES; i++) {
        const drag_gate_def_t *g = &s_drag_cfg.gates[i];
        if (g->kind == DRAG_DIST && g->a > trap_a) {
            trap_a    = g->a;
            s_trap_id = g->id;
        }
    }
    pipe_drag_t d;
    if (pipeline_drag_snapshot(&d) != 0) {
        return;
    }
    LT_ASSERT_VOID(d.current.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    switch (s_model.page) {
    case 0:
    case 1: drag_fill_from_run(&d.current, true); break;   /* I3: both list hit gates only */
    case 2: drag_fill_page2(&d); break;
    default: break;
    }
}

/* EV_SECTOR -> model (split verbatim out of handle_event for rule 4). arg32b is 0 both for a
 * genuine zero delta and for "no best lap yet" (event.h) -- big_kind and have_last_sector_delta[]
 * follow s_model.have_best so a consumer never mistakes "no best yet" for a real zero delta (spec
 * 7b §3). */
static void handle_sector(const event_t *e)
{
    LT_ASSERT_VOID(e != NULL, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(e->arg16 <= LAP_MAX_SECTORS, UI_APP_ASSERT_CODE);   /* engine sector idx in range */
    uint8_t idx = (uint8_t)e->arg16;   /* 0-based split index (lap.c: k-1) */
    /* Ruling FR-1: cur_sector_idx is the 1-based count of sectors completed this lap (idx is
     * 0-based), so the marker reads "S1" after the first gate, not "S0". */
    s_model.cur_sector_idx  = (uint8_t)(idx + 1u);
    s_model.big_kind        = s_model.have_best ? (uint8_t)BIG_SECTOR_DELTA : (uint8_t)BIG_NONE;
    s_model.big_delta_ms    = (int32_t)e->arg32b;
    s_model.big_sector_idx  = idx;
    s_model.last_sector_delta_ms[idx]   = (int32_t)e->arg32b;
    s_model.have_last_sector_delta[idx] = s_model.have_best;
    s_model.new_best = false;   /* the BEST tag lives until the next gate (spec 7b §4) */
    s_dirty          = true;
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

/* #87: a peer CONFIG_SET persisted a new cfg -- reload the ui's working copy and everything
 * derived from it (units suffixes, DRAG gate labels/benches, riding mode). The ui actually has
 * THREE dynamic menu labels (s_lbl_mode/s_lbl_units/s_lbl_disp, build_menu() above) -- all three
 * are rebuilt here with the same snprintf build_menu() itself uses, rather than calling
 * build_menu() directly: that function's other job, rebuilding s_model.menu_items[]/menu_n/
 * s_menu_action[] via menu_add(), is a side effect well beyond "refresh three label strings" and
 * is not needed here (the menu's item list/order never changes, only the label text).
 * fix round 1 (minor finding 2): drag_armed is engine state, not cfg-derived, but drag_init()
 * (called by the pipeline's own reload) always drops ARMED -- clear the ui's mirror of it here
 * too so a remote CONFIG_SET can't leave a stale "ARMED" indicator on screen.
 * I2 (final review, ruling B-6): a remote mode flip must mirror menu_do_mode's own reset
 * (ui_apply_mode above) -- page 0 reset to §22.6 and the live lap clock stopped when leaving LAP
 * -- not just a relabelled menu while the riding screen keeps showing stale LAP state under a
 * now-DRAG mode. Only applied when the mode actually changed: most CONFIG_SET calls touch
 * units/display, and those must NOT reset the current page/clock. */
static void ui_reload_cfg(void)
{
    cfg_defaults(&s_cfg);
    (void)lt_cfg_load(&s_cfg);
    s_model.units      = s_cfg.units;
    s_model.dist_units = s_cfg.dist_units;
    uint8_t new_mode = (s_cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    if (new_mode != s_mode) {
        ui_apply_mode(new_mode);
    }
    s_model.drag_armed = false;
    drag_cfg_from_user(&s_cfg, &s_drag_cfg);
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s", s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    snprintf(s_lbl_dist, sizeof s_lbl_dist, "Dist: %s", s_cfg.dist_units == CFG_DIST_FT ? "ft" : "m");
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
    snprintf(s_lbl_disp, sizeof s_lbl_disp, "Display: clk %s", s_cfg.display.live_clock ? "on" : "off");
    if (s_model.mode == SCR_MODE_DRAG) drag_rows_refill();
    s_dirty = true;
    LT_ASSERT_VOID(s_drag_cfg.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.units <= 1u, UI_APP_ASSERT_CODE);
}
/* #87: the pipeline's engines were reset remotely -- the running-lap clock must not keep counting.
 * fix round 1 (minor finding 2): drag_reset() (called by the pipeline's own CMD_RESET_ENGINE
 * handling) drops ARMED too -- clear the ui's mirror so a stale "ARMED" indicator can't survive.
 * I4 (final review, ruling B-6): lap_init()/drag_init() (also called by the pipeline's own
 * CMD_RESET_ENGINE handling, pipeline.c) drop the engines back to a fresh session -- the ui's
 * mirror of PREV/BEST/session stats/best-sector board must follow the same way, or a stale
 * BEST/MAX SPD/best-sector detail from before the reset survives on screen into a session the
 * engine itself now considers new. Mirrors ui_task()'s own fresh-session init (search
 * clear_last_sector_deltas()) field-for-field; drag_rows_refill() re-reads the just-reset pipeline
 * snapshot so DRAG rows reflect the clear immediately rather than waiting for the next EV_DRAG_*
 * event. */
static void ui_lap_reset(void)
{
    s_lap_start_mono_us = 0;
    s_last_clock_us     = 0;
    s_model.cur_ms      = 0;
    s_model.cur_running = false;
    s_model.drag_armed  = false;

    s_model.best_ms        = 0;
    s_model.prev_ms        = 0;
    s_model.have_best      = false;
    s_model.have_prev      = false;
    s_model.new_best       = false; /* bench B-F4: the inverted BEST tag otherwise outlives the reset */
    s_model.laps_total     = 0;
    s_model.laps_valid     = 0;
    s_model.theo_best_ms   = 0;
    s_model.have_theo      = false;
    s_model.best_n_sectors = 0;
    memset(s_model.best_sector_ms, 0, sizeof s_model.best_sector_ms);
    memset(s_model.have_best_sector, 0, sizeof s_model.have_best_sector);
    clear_last_sector_deltas();
    s_model.big_kind = (uint8_t)BIG_NONE;
    s_model.lap_no   = 1;
    memset(&s_session_max, 0, sizeof s_session_max);
    drag_rows_refill();

    s_dirty = true;

    LT_ASSERT_VOID(s_model.laps_valid <= s_model.laps_total, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.lap_no == 1 && s_model.big_kind == (uint8_t)BIG_NONE && !s_model.new_best,
                   UI_APP_ASSERT_CODE);
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
    /* Plan 7c T5 (design §3): drag_rows_refill() re-derives drag_n from the snapshot itself, so
     * EV_DRAG_ARMED must NOT zero it first -- doing so would race a refill that reads the still-
     * frozen previous run before the engine's own ARMED reset lands in the next snapshot. */
    case EV_DRAG_ARMED:  s_model.drag_armed = true; drag_rows_refill(); s_dirty = true; break;
    case EV_DRAG_LAUNCH: s_model.drag_armed = false; s_dirty = true; break;
    case EV_DRAG_GATE:   drag_rows_refill(); s_dirty = true; break;
    case EV_DRAG_DONE:   drag_rows_refill(); s_dirty = true; break;
    /* #87: ui-only codes (never emit_event()'d) -- CONFIG_SET-triggered reload, remote lap reset. */
    case EV_CFG_CHANGED: ui_reload_cfg(); break;
    case EV_LAP_RESET:   ui_lap_reset();  break;
    case EV_OTA:         handle_ota(e, now); break;
    default: break;
    }
}

/* Snapshot the authoritative fault flags (§17.4) into the model + fold in the ui-tracked GPS
 * no-fix bit; re-render only when the strip actually changes. */
static void update_flags(void)
{
    uint32_t f = sys_flags_get() & SCR_SYS_BITS_MASK;
#if CFG_GPS_SIM || CFG_IMU_SIM
    f |= 1u << SCR_UI_SIM;
#endif
    if (s_gspeed_kmh >= MENU_LOCK_SPEED_KMH) f |= 1u << SCR_UI_MOVING;
    if (link_peer_present()) f |= 1u << SCR_UI_LINK;   /* #99: the dev-kit / a BLE peer is talking to us */
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
    in.dirty           = true; /* called from render_and_refresh(): a dirty render or the deferred (throttled) refresh */
    in.wants_full      = s_wants_full;
    in.screen_changed  = s_screen_changed; /* bench B-F1, ruling B-9: screen replacement -> never a partial */
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

/* Arms the pending partial window from the diff rect (s_diff: the bounding box of bytes that
 * differ from the last frame the panel accepted, computed by render_and_refresh() via
 * fb_diff_rect(), Plan 7c T7 design §5), clamped to the panel's true visible area (ruling R2:
 * disp_set_window() rejects x + w > 250). Returns DISP_PARTIAL if the window was armed, else
 * DISP_FULL -- either because the box was degenerate or disp_set_window() itself failed (logged
 * once here). Before T7 the source was s_fb.dirty (the render's own draw box, always the whole
 * padded buffer since fb_clear() touches everything every render); s_diff is narrower -- only what
 * actually changed versus the panel's last known content, which is the point of this task.
 * Fix round 1: only ever called for kind == RF_PARTIAL, and render_and_refresh() now guarantees
 * s_diff.valid before letting kind == RF_PARTIAL reach here -- an unchanged frame (s_diff invalid)
 * either skips before the policy is even asked, or, if wants_full forces the question, only acts
 * on a policy answer of RF_FULL (which never calls this function; see render_and_refresh()'s
 * decision table). The assert below is therefore a real invariant, not a defensive placeholder. */
static uint8_t partial_window_or_full(void)
{
    LT_ASSERT_RET(s_diff.valid, UI_APP_ASSERT_CODE, DISP_FULL); /* something must have differed */
    LT_ASSERT_RET(s_diff.x0 <= s_diff.x1 && s_diff.y0 <= s_diff.y1, UI_APP_ASSERT_CODE, DISP_FULL);
    uint16_t x0 = s_diff.x0;
    uint16_t y0 = s_diff.y0;
    uint16_t x1 = s_diff.x1 < CANVAS_VISIBLE_W ? s_diff.x1 : (uint16_t)CANVAS_VISIBLE_W;
    uint16_t y1 = s_diff.y1 < CANVAS_H ? s_diff.y1 : (uint16_t)CANVAS_H;
    if (x0 >= x1 || y0 >= y1) {
        /* Minor #7: >= , not > -- an empty box (x0 == x1 or y0 == y1, e.g. the clamp above
         * landed exactly on the visible-area edge) must be caught here, not passed on to
         * disp_set_window(), whose w > 0 / h > 0 asserts are for a genuine driver-contract
         * violation, not this routine degenerate-dirty-box case. */
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
 * and retries once; a second failure counts against s_fail_streak and, at DISP_FAIL_STREAK_MAX,
 * marks the panel dead. Bumps g_hb[HB_UI] after every blocking disp_refresh()/disp_reinit() call
 * (ruling R3: a timed-out refresh + reinit + retry can exceed UI_STALL_S). Returns the final
 * driver rc, with the EFFECTIVE mode actually sent to the panel on that final attempt reported
 * through *effective_mode. Fix round 2 (minor #4): a failed DISP_PARTIAL has already consumed its
 * pending window (epd_partial_refresh() always clears it, success or failure) -- the retry is
 * therefore explicitly DISP_FULL, not a second DISP_PARTIAL relying on the driver's own
 * no-window-pending fallback, so *effective_mode always matches what the panel actually got,
 * never the caller's original request. */
static int disp_refresh_ladder(uint8_t mode, int64_t now, uint8_t *effective_mode)
{
    LT_ASSERT_RET(mode == DISP_PARTIAL || mode == DISP_FULL, UI_APP_ASSERT_CODE, -1);
    LT_ASSERT_RET(effective_mode != NULL, UI_APP_ASSERT_CODE, -1);

    *effective_mode = mode;
    int rc          = disp_refresh(mode);
    g_hb[HB_UI]++;
    if (rc == 0) {
        s_fail_streak = 0;
        return rc;
    }

    /* Ruling T7-R8: pack both facts into one arg -- the pre-failure streak count in the high
     * byte, the driver's -errno magnitude in the low byte -- so errlog distinguishes a BUSY
     * timeout (-ETIMEDOUT = 110) from an SPI failure (-EIO = 5) without a second display code. */
    (void)errlog_add(E_DISP_BUSY_TIMEOUT, ((uint32_t)s_fail_streak << 8) | ((uint32_t)(-rc) & 0xFFu));
    int reinit_rc = disp_reinit();
    g_hb[HB_UI]++;
    if (reinit_rc == 0) {
        uint8_t retry_mode = (mode == DISP_PARTIAL) ? (uint8_t)DISP_FULL : mode;
        *effective_mode = retry_mode;
        rc = disp_refresh(retry_mode);
        g_hb[HB_UI]++;
    }
    if (rc == 0) {
        s_fail_streak = 0;
        return rc;
    }

    s_fail_streak++;
    if (s_fail_streak >= DISP_FAIL_STREAK_MAX) {
        sys_flags_set(SYS_DISP_DEAD);
        (void)errlog_add(E_DISP_DEAD, 0);
        s_next_reinit_us = now + DISP_DEAD_RETRY_US;
    }
    return rc;
}

/* Executes the refresh kind the policy chose (RF_NONE is handled by the caller before this is
 * reached): RF_PARTIAL first tries to arm the dirty window, falling back to DISP_FULL (ruling R2)
 * if that fails; RF_FULL goes straight to DISP_FULL. Bookkeeping follows the EFFECTIVE mode
 * disp_refresh_ladder() reports (fix round 2, minor #4: that now accounts for its own
 * partial-consumed-the-window retry-as-full, not just this function's own partial_window_or_full()
 * fallback) -- a successful full (whether requested or the ladder's retry) resets s_partial_count,
 * stamps s_last_full_us and clears s_wants_full; a FAILED refresh updates none of that bookkeeping.
 * Plan 7c T6 (design §4 last bullet), amended by final review I4 (ruling R-8): a successful partial
 * caused only by a clock tick (s_clock_tick) does NOT bump s_partial_count, but DOES stamp
 * s_last_partial_us like any other successful partial -- so the 30 s temperature-throttle window
 * (spec §20.3) measures real wall-clock time since the panel last actually refreshed, tick or not,
 * rather than silently extending past 30 s while a string of ticks goes unaccounted. It still
 * counts fully as a real full when the policy or the fallback above promotes it to one, since that
 * is a genuine full refresh regardless of what triggered it. Reports the effective mode via
 * *mode_out so the caller's log line reflects what actually happened on the panel.
 * Bench B-F1, ruling B-9: s_screen_changed clears on ANY successful refresh, full or partial --
 * unlike s_wants_full just below, which only clears on a full. A throttled screen replacement's
 * successful partial already painted the new screen content, so there is nothing left for a later
 * full to do on its behalf; leaving s_screen_changed set would otherwise force a second, redundant
 * full once the throttle lifts. */
static int do_refresh(rf_kind_t kind, int64_t now, uint8_t *mode_out)
{
    LT_ASSERT_RET(kind == RF_PARTIAL || kind == RF_FULL, UI_APP_ASSERT_CODE, -1);
    LT_ASSERT_RET(mode_out != NULL, UI_APP_ASSERT_CODE, -1);
    uint8_t mode = (kind == RF_PARTIAL) ? partial_window_or_full() : DISP_FULL;
    int     rc   = disp_refresh_ladder(mode, now, mode_out);
    if (rc == 0) {
        /* Plan 7c T7 (design §5): the panel now shows this frame, whichever mode it took --
         * refresh s_fb_prev_bits so the next diff is against reality. Left untouched on failure so
         * the next diff still covers the union of what changed then and what changes next. */
        memcpy(s_fb_prev_bits, s_fb_bits, sizeof s_fb_prev_bits);
        s_screen_changed = false; /* satisfied by this refresh, whichever mode actually ran */
        if (*mode_out == DISP_PARTIAL) {
            /* I4 (ruling R-8): always stamp the timestamp; only the count stays tick-gated. */
            s_last_partial_us = now;
            if (!s_clock_tick) {
                s_partial_count = (uint16_t)(s_partial_count + 1);
            }
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
 * Fix round 2 (minor #11): one ESP_LOG_LEVEL() call replaces the previous two ESP_LOGW/ESP_LOGI
 * branches, which differed only in level, not in text -- the bench parses this exact line
 * ("refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c" + " rc=%d" for the attempted case), which
 * stays byte-identical for both the P and F outcomes.
 * Plan 7c T7 (design §5): the box printed is s_diff (the diff-vs-panel rect), not the render's own
 * draw box.
 * Fix round 1 (minor #2): s_diff.valid is false whenever the last fb_diff_rect() call found no
 * difference (the diff-unchanged kind=N skip in render_and_refresh(), or fix 1's unchanged-but-
 * forced-full path, where an RF_FULL outcome is logged as kind=F without ever computing a real
 * box) -- x0/y0/x1/y1 are then whatever the last REAL diff computed, not this call's box. Print
 * 0,0,0,0 instead of that stale value in both cases. */
static void log_refresh(bool attempted, uint8_t mode, int rc)
{
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.page < 3, UI_APP_ASSERT_CODE);
    char     kc  = !attempted ? 'N' : ((mode == DISP_FULL) ? 'F' : 'P');
    uint16_t dx0 = s_diff.valid ? s_diff.x0 : 0;
    uint16_t dy0 = s_diff.valid ? s_diff.y0 : 0;
    uint16_t dx1 = s_diff.valid ? s_diff.x1 : 0;
    uint16_t dy1 = s_diff.valid ? s_diff.y1 : 0;
    if (!attempted) {
        ESP_LOGI(TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c", (unsigned)s_model.screen,
                 (unsigned)s_model.page, (unsigned)dx0, (unsigned)dy0, (unsigned)dx1,
                 (unsigned)dy1, kc);
        return;
    }
    /* A ternary of two enum constants used directly as ESP_LOG_LEVEL()'s `level` argument trips
     * -Wint-in-bool-context once macro-expanded into the macro's `if (level==ESP_LOG_ERROR)`
     * chain (GCC 13.2, this toolchain) -- hoisting it into a plain local sidesteps that, since
     * the macro then only ever sees a bare identifier there. */
    esp_log_level_t lvl = (rc != 0) ? ESP_LOG_WARN : ESP_LOG_INFO;
    ESP_LOG_LEVEL(lvl, TAG, "refresh scr=%u pg=%u dirty %u,%u..%u,%u kind=%c rc=%d",
                  (unsigned)s_model.screen, (unsigned)s_model.page, (unsigned)dx0, (unsigned)dy0,
                  (unsigned)dx1, (unsigned)dy1, kc, rc);
}

/* Renders the model, then feeds the pure refresh policy (§20.3) and carries out whatever it
 * decides: RF_NONE is a no-op, RF_PARTIAL/RF_FULL run through the panel + failure ladder above.
 * Split out of the old render_now() (Plan 7 Task 7) to stay under the 60-line function cap.
 * Plan 7c T7 (design §5): the first thing after rendering is a diff of the fresh s_fb against
 * s_fb_prev (the last frame the panel actually accepted). An identical frame with no forced full
 * costs nothing at all -- no policy call, no counters/timestamps touched, and s_refresh_pending is
 * cleared too (a throttled render that reverted to what the panel already shows is no longer owed).
 * Fix round 1 (Important, reachable assert): an unchanged frame can still need the policy asked --
 * s_wants_full (e.g. the UP+DOWN ghost-clear combo, which has no speed gate) can be set while
 * riding, and ui_refresh_decide() only turns wants_full into RF_FULL while "still"; while moving it
 * can hand back RF_PARTIAL (or, if throttled, RF_NONE) instead. s_diff.valid is guaranteed false
 * whenever changed is false (fb_diff_rect() always clears it first, before it ever finds a
 * differing byte), so letting do_refresh(RF_PARTIAL) reach partial_window_or_full() in that state
 * would trip its "something must have differed" assert on ordinary, expected control flow -- so the
 * policy's answer is checked before acting on it.
 * Fix round 2 (Important): the fix round 1 guard below must not blanket-clear s_refresh_pending --
 * an unchanged frame whose RF_NONE came from the temperature throttle (not dead) is still owed a
 * retry once the 30 s window reopens (spec §20.3, Important #2), exactly like the changed==true
 * RF_NONE case just below it. Decision table (changed x wants_full x policy x throttled):
 *   changed=true                            -> always act on kind as returned; s_diff is valid, so
 *                                               partial_window_or_full() has a real box if
 *                                               RF_PARTIAL (the RF_NONE block just below still
 *                                               applies its own throttled handling, unaffected).
 *   changed=false, wants_full=false         -> skip before the policy is even called (below).
 *   changed=false, wants_full=true:
 *     policy returns RF_FULL                -> act on it (DISP_FULL never reads s_diff).
 *     policy returns RF_PARTIAL             -> skip; genuinely nothing to redraw on an unchanged
 *                                               frame, so s_refresh_pending clears.
 *     policy returns RF_NONE, throttled     -> skip, but s_refresh_pending stays owed so
 *                                               ui_loop_iter()'s retry re-asks the policy once the
 *                                               30 s window reopens.
 *     policy returns RF_NONE, not throttled -> skip (dead); s_refresh_pending clears -- dead
 *                                               re-arms on its own via dead_retry().
 *   Every "skip" row leaves s_wants_full set so a later render asks again.
 *   changed=false, screen_changed=true (bench B-F1, ruling B-9): the row above is unchanged by
 *     name -- s_screen_changed is treated exactly like s_wants_full by the guard just below, so a
 *     whole-screen replacement whose rendered frame happens to be byte-identical to what the panel
 *     already shows still asks the policy (rule 3b forces RF_FULL unless throttled, in which case
 *     rule 3 still answers first) instead of being silently dropped; every "skip" outcome above
 *     leaves s_screen_changed set, same as s_wants_full, so a later render asks again.
 * Plan 7c T6 (fix round 1) ruling, carried here: a render that starts with s_refresh_pending
 * already true is never "only a clock tick". tick_only is computed once, at entry -- before
 * anything below can change s_refresh_pending -- as s_clock_tick && !s_refresh_pending, and folded
 * straight back into s_clock_tick, so do_refresh()'s accounting gate (which just reads
 * s_clock_tick) sees the corrected value without a second copy of this rule; the extra clear
 * ui_loop_iter's pending branch used to do before calling this function is gone (T7) -- s_diff/
 * tick_only logic here already produces the same result whenever that branch's own guard
 * (s_refresh_pending true) holds.
 * s_clock_tick is cleared here, on every return path, once this render has either been accounted
 * for (do_refresh(), above) or explicitly skipped (diff-unchanged or RF_NONE) -- never left set for
 * a later, unrelated render to misread. */
static void render_and_refresh(void)
{
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE); /* fb_init ran before any render */
    bool tick_only = s_clock_tick && !s_refresh_pending;
    s_clock_tick   = tick_only;

    render_fb();

    int64_t now = esp_timer_get_time();
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE); /* esp_timer stamp feeds the policy's now_us */

    bool changed = fb_diff_rect(&s_fb_prev, &s_fb, &s_diff);
    if (!changed && !s_wants_full && !s_screen_changed) {
        /* The panel already shows this frame -- no policy call, no bookkeeping of any kind. */
        log_refresh(false, DISP_PARTIAL, 0);
        s_refresh_pending = false;
        s_clock_tick      = false;
        return;
    }

    uint8_t full_every = s_cfg.display.full_every; /* R1: cfg only defaults it, so clamp here too */
    if (full_every < 1) {
        full_every = 1;
    } else if (full_every > 50) {
        full_every = 50;
    }

    rf_in_t   in   = build_rf_in(now, full_every);
    rf_kind_t kind = ui_refresh_decide(&in);

    if (!changed && kind != RF_FULL) {
        /* Fix round 1: an unchanged frame only reaches here with wants_full (or, bench B-F1,
         * screen_changed) set, asking the policy for a forced full; anything but RF_FULL means
         * "not yet" -- skip exactly like the wants_full=false/screen_changed=false case above,
         * never handing an invalid s_diff to do_refresh().
         * Fix round 2: a throttled RF_NONE is still owed a retry (spec §20.3, Important #2) -- only
         * RF_PARTIAL (nothing to redraw) or a non-throttled RF_NONE (dead, which re-arms on its
         * own via dead_retry()) clear s_refresh_pending outright. */
        s_refresh_pending = (kind == RF_NONE) ? in.throttled : false;
        log_refresh(false, DISP_PARTIAL, 0);
        s_clock_tick = false;
        return;
    }

    if (kind == RF_NONE) {
        /* Important #2: a refresh suppressed by the temperature throttle is owed, not dropped
         * (spec §20.3) -- remember exactly that (never for the dead RF_NONE case, which needs no
         * retry of its own: dead already re-arms via dead_retry()). */
        s_refresh_pending = in.throttled;
        log_refresh(false, DISP_PARTIAL, 0);
        s_clock_tick = false;
        return;
    }
    s_refresh_pending = false; /* this call resolved whatever was pending, one way or another */
    uint8_t mode = DISP_PARTIAL;
    int     rc   = do_refresh(kind, now, &mode);
    log_refresh(true, mode, rc);
    s_clock_tick = false;
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

/* Live lap clock tick (Plan 7c T6, design §4): once a second, while display.live_clock is on, a
 * lap is running (s_lap_start_mono_us != 0, armed by handle_lap_complete() above) and LAP page 0
 * is the screen actually showing, advances s_model.cur_ms/cur_running from the lap's start stamp
 * so the card footer's left cell shows a running CUR m:ss instead of LAST. A render this alone
 * causes is tentatively marked s_clock_tick (only when nothing else already made this iteration
 * dirty) so do_refresh() (above) keeps it out of the full-refresh ladder's partial_count
 * accounting (ruling R-8: it still stamps last_partial_us like any refresh, so the 30 s
 * temperature throttle applies to it the same as any other); s_wants_full is never touched here,
 * so a clock tick alone never forces a full. "Tentatively": this function cannot see whether
 * ui_loop_iter's throttle-pending branch (which fires independent of s_dirty) is about to consume
 * this same render to resolve an owed refresh -- that branch clears s_clock_tick itself before calling
 * render_and_refresh() (Plan 7c T6 fix round 1) so such a render is never wrongly exempted from
 * accounting. When the tick conditions stop holding, cur_running drops once (the cell reverts to
 * LAST) -- that transition is a real model change, not a clock-driven one, so it dirties
 * normally. */
static void clock_tick(int64_t now)
{
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE); /* esp_timer stamp, same clock as s_last_clock_us */
    bool run = s_cfg.display.live_clock && s_lap_start_mono_us != 0 &&
               s_model.screen == SCR_RIDING && s_model.page == 0 && s_model.mode == SCR_MODE_LAP;
    if (run && (now - s_last_clock_us) >= 1000000) {
        s_model.cur_ms      = (uint32_t)((now - s_lap_start_mono_us) / 1000);
        s_model.cur_running = true;
        s_last_clock_us     = now;
        if (!s_dirty) {
            s_clock_tick = true;
        }
        s_dirty = true;
    } else if (!run && s_model.cur_running) {
        s_model.cur_running = false;
        s_dirty             = true;
    }
}

/* M7 (final review): BOOT_SLOTS (app/lt_sup.h, the number of self-test slots reported into) must
 * never exceed BOOT_MAX_LINES (core/ui/canvas.h, the renderer's line budget) -- boot_lines_format()
 * below loops BOOT_SLOTS times writing s_model.boot_line[i], and render_oneshot_boot()
 * (screens_moto.c) only ever draws up to BOOT_MAX_LINES of them. A build-time mismatch (either
 * constant edited without the other) would silently drop a line or overrun the model's array. */
_Static_assert(BOOT_SLOTS <= BOOT_MAX_LINES, "BOOT_SLOTS must fit BOOT_MAX_LINES");

/* Plan 7c T8 (design §6): formats the four BOOT self-test lines from sup_boot_report()'s table --
 * STORAGE/DISPLAY/GPS/IMU, each "<LABEL> <STATUS>" ("--"/OK/FAIL/SIM). Called three times over
 * boot (ui_task, below): once before the first BOOT render (STORAGE already known from app_main,
 * the rest read "--"), once right after disp_init() reports DISPLAY, and once more ~1 s later via
 * boot_refmt_check() once the pipeline has reported GPS/IMU. */
static void boot_lines_format(void)
{
    static const char *const label[BOOT_SLOTS]  = { "STORAGE", "DISPLAY", "GPS", "IMU" };
    static const char *const statw[4]           = { "--", "OK", "FAIL", "SIM" };
    for (uint8_t i = 0; i < BOOT_SLOTS; i++) {
        uint8_t st = sup_boot_status(i);
        LT_ASSERT_VOID(st <= BOOT_SIM, UI_APP_ASSERT_CODE);   /* indexes statw[] below */
        int n = snprintf(s_model.boot_line[i], sizeof s_model.boot_line[i], "%s %s", label[i], statw[st]);
        LT_ASSERT_VOID(n > 0 && (size_t)n < sizeof s_model.boot_line[i], UI_APP_ASSERT_CODE);
    }
    s_model.boot_n_lines = BOOT_SLOTS;
}

/* Plan 7c T8: the pipeline's gps_init()/imu_init() calls (separate task) can still land after
 * ui_task's own boot sequence returns, so the first two boot_lines_format() calls above can still
 * show GPS/IMU as "--". Re-formats once more, ~1 s after the BOOT one-shot was armed, while it is
 * still showing, and dirties the model so the normal render loop picks it up (Task 7's diff turns
 * that into a partial over just the changed lines). Fires at most once per boot. */
static void boot_refmt_check(int64_t now)
{
    LT_ASSERT_VOID(now >= 0, UI_APP_ASSERT_CODE);
    if (s_boot_refmt_done || s_boot_arm_us == 0) return;
    if (s_model.screen != SCR_ONESHOT || s_model.oneshot != ONESHOT_BOOT) return;
    if (now < s_boot_arm_us + 1000000) return;
    boot_lines_format();
    s_dirty           = true;
    s_boot_refmt_done = true;
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
     * requests a full, same as ui_exit_menu()'s manual dismissal path. Bench B-F1, ruling B-9:
     * s_screen_changed (not just s_wants_full) forces that full even while the bike is moving --
     * the still-gated wants_full path otherwise left this as a partial, and a partial LUT's
     * ghosting/contrast is visible across a whole-screen content change even when the dirty
     * rectangle itself is computed correctly. Correction (review M4): the stale TOP ROWS actually
     * seen on the bench were a separate bug, B-F2 (the un-mirrored RAM-byte window, now fixed) --
     * not rows left outside a correct dirty rect by this partial-vs-full choice; B-9 stands on its
     * own real justification above, not on that symptom. */
    if (s_model.screen == SCR_ONESHOT && s_oneshot_until_us != 0 && now >= s_oneshot_until_us) {
        if (s_model.oneshot == ONESHOT_BOOT && s_rollback_pending) {
            s_rollback_pending = false;
            s_model.oneshot    = ONESHOT_OTA_FAIL;   /* one-shot -> one-shot: still a replacement */
            s_oneshot_until_us = now + (int64_t)ONESHOT_OTAFAIL_MS * 1000;
        } else {
            s_oneshot_until_us = 0;
            s_model.screen     = SCR_RIDING;
        }
        s_wants_full     = true;
        s_screen_changed = true;
        s_dirty          = true;
    }
    /* I2 (fix round 1): a stale OTA screen (no EV_OTA for OTA_STALE_MS) means the push died with
     * no terminal event ever posted -- escape back to riding rather than block the device forever.
     * A live transfer posts at least every 5 %, far inside this window on any real link. */
    if (s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_OTA &&
        now - s_ota_seen_us >= (int64_t)OTA_STALE_MS * 1000) {
        ESP_LOGW(TAG, "ota screen: stale, reverting");
        ui_exit_menu();
    }
    /* Menu idle auto-exit (§20.7). */
    if (s_model.screen == SCR_MENU && (now - s_last_input_us) >= (int64_t)MENU_IDLE_MS * 1000) {
        ui_exit_menu();
    }

    update_flags();
    dead_retry(now); /* R5: paced disp_reinit() probe while SYS_DISP_DEAD */
    clock_tick(now); /* Plan 7c T6: live lap clock, once a second, before the render decision below */
    boot_refmt_check(now); /* Plan 7c T8: one BOOT re-format ~1 s after arming, once GPS/IMU land */

    /* Important #2: a throttle-deferred refresh is owed, not dropped (spec §20.3) -- re-check
     * every tick but only actually re-render/refresh once the 30 s window has elapsed (never on
     * every UI_TICK_MS while waiting); render_and_refresh() re-runs the pure policy itself, so
     * the none-vs-partial call stays in one place.
     * Plan 7c T6 fix round 1 / T7: clock_tick() (above) tags s_clock_tick purely off !s_dirty, which
     * cannot see that THIS branch fires independent of s_dirty -- a tick due on the very iteration
     * the 30 s throttle window reopens must not get free-ridden into this call's do_refresh()
     * partial_count bookkeeping (I4/ruling R-8: s_last_partial_us itself is now stamped
     * unconditionally on any successful partial, tick or not, so only partial_count is at stake
     * here), even though this render is resolving a real owed backlog, not "only a tick".
     * render_and_refresh() (T7) now computes
     * tick_only = s_clock_tick && !s_refresh_pending itself, at its own entry, so this branch no
     * longer clears s_clock_tick before calling it -- s_refresh_pending is true here (that is this
     * branch's own guard), so tick_only comes out false regardless of s_clock_tick's value. */
    if (s_refresh_pending && (now - s_last_partial_us) >= DISP_THROTTLE_RETRY_US) {
        render_and_refresh();
        s_dirty = false;
    } else if (s_dirty) {
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
    s_mode        = (s_cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    s_model.mode  = s_mode;
    s_model.units = s_cfg.units; /* Plan 7c T4 (design §3): every speed_display() call on screen uses it */
    s_model.dist_units = s_cfg.dist_units; /* #96: DIST-gate naming only, consumed by row_from_gate */
    drag_cfg_from_user(&s_cfg, &s_drag_cfg); /* Plan 7c T5: gate table for DRAG row labels/benches */
    /* Event card (spec 7b §3): session/first-lap state -- no delta to show yet, lap 1 in progress.
     * The rest of s_model is zero-initialised static storage, which is already BIG_NONE/0.
     * clear_last_sector_deltas() (ruling B7b-1, bench finding 1) is called here and ONLY here --
     * once at session start -- since have_last_sector_delta[] persists across every lap boundary
     * thereafter (each EV_SECTOR just overwrites its own slot); this call documents that intent
     * explicitly rather than relying on zero-init alone. */
    s_model.lap_no    = 1;
    s_model.big_kind  = (uint8_t)BIG_NONE;
    clear_last_sector_deltas();
    LT_ASSERT_VOID(s_mode <= MODE_DRAG, UI_APP_ASSERT_CODE);   /* valid engine mode from cfg */

    fb_init(&s_fb, s_fb_bits, CANVAS_W, CANVAS_H);
    LT_ASSERT_VOID(s_fb.bits != NULL, UI_APP_ASSERT_CODE);   /* framebuffer is armed for render_fb */
    fb_init(&s_fb_prev, s_fb_prev_bits, CANVAS_W, CANVAS_H); /* Plan 7c T7: last-accepted-frame copy */
    fb_clear(&s_fb_prev, 0); /* known state until the post-disp_init() memcpy below overwrites it */

    /* First screen: SAFE MODE one-shot in safe mode (§17.5, boot self-test skipped), else BOOT. */
    uint32_t f0 = sys_flags_get();
    if (f0 & (1u << SYS_SAFE_MODE)) {
        s_model.screen  = SCR_ONESHOT;
        s_model.oneshot = ONESHOT_SAFE; /* persistent (§17.5: one full-screen render, then idle) */
    } else {
        /* FONT_MED has no lowercase glyphs (digits, ": . - +", A-Z only -- fonts.h) -- "LapTimer"
         * rendered as "L      T" on the real panel. All-caps has ink (host goldens already say
         * "LAPTIMER"). */
        snprintf(s_model.boot_name, sizeof s_model.boot_name, "LAPTIMER");
        snprintf(s_model.boot_ver, sizeof s_model.boot_ver, "%s", CFG_FW_VERSION);
        /* Plan 7c T8 (design §6): format from sup_boot_report()'s table now -- STORAGE is already
         * known (app_main's boot_storage() ran synchronously before this task started); DISPLAY/
         * GPS/IMU still read "--" here and are filled in by the two later calls below. */
        boot_lines_format();
        s_model.screen       = SCR_ONESHOT;
        s_model.oneshot      = ONESHOT_BOOT;
        /* s_oneshot_until_us is armed further down, AFTER disp_init() returns -- not here. Arming
         * it here would start the 3 s window before disp_init()'s ~3.5 s blocking bring-up, so the
         * window would already be expired by the time the BOOT screen is first visible and the LAP
         * page would replace it immediately. */
    }
    /* M1 (final review): mask to SCR_SYS_BITS_MASK here too -- update_flags() already does (its
     * own comment explains why: bit 14, SYS_RECOVERY_MODE, must never alias SCR_UI_SIM). Harmless
     * today (recovery mode returns before ui_start(), app_main.c, so bit 14 cannot be set yet) but
     * leaves the masking invariant correct in both places instead of one. */
    s_model.flags = f0 & SCR_SYS_BITS_MASK;
    LT_ASSERT_VOID(s_model.screen <= SCR_ONESHOT, UI_APP_ASSERT_CODE);        /* first screen valid */
    LT_ASSERT_VOID(s_model.oneshot <= ONESHOT_NEWTRACK, UI_APP_ASSERT_CODE);  /* one-shot selector valid */
    render_fb();

    /* Display bring-up (Plan 7 Task 5): blit the just-rendered BOOT screen BEFORE disp_init so
     * its first full refresh shows it on the panel. disp_init()'s own full refresh counts as the
     * refresh policy's first full (ruling R4): on success it seeds s_last_full_us/s_partial_count
     * as if RF_FULL had just run (and, Plan 7c T7, copies s_fb_bits into s_fb_prev_bits -- the
     * panel really does hold this BOOT frame now); on failure nothing is seeded -- SYS_DISP_DEAD
     * makes the policy return RF_NONE until dead_retry()'s 300 s probe (armed here) clears it. */
    (void)disp_blit(s_fb_bits);   /* cannot fail here: s_fb_bits is a static array (never NULL) and
                                    * epd_panel() (bound as a side effect) never returns NULL either */
    const disp_caps_t *disp_caps;
    int                disp_rc = disp_init(&disp_caps);
    /* Plan 7c T8 (design §6): report DISPLAY regardless of which one-shot is showing first (BOOT or
     * SAFE); only BOOT re-formats + re-renders from it below -- SAFE has no self-test lines. */
    sup_boot_report(BOOT_DISPLAY, disp_rc == 0 ? BOOT_OK : BOOT_FAIL);
    if (s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_BOOT) {
        boot_lines_format();
        s_dirty = true;   /* Task 7's diff turns this into a partial over just the changed lines */
    }
    if (disp_rc == 0) {
        /* Minor #5: catches a PANEL/canvas mismatch at boot -- disp_init() always fills *caps by
         * the time it returns 0 (moved ahead of its own boot-refresh call, fix round 2). */
        LT_ASSERT_VOID(disp_caps != NULL, UI_APP_ASSERT_CODE);
        LT_ASSERT_VOID(disp_caps->width == CANVAS_VISIBLE_W && disp_caps->height == CANVAS_H &&
                       disp_caps->partial_ok, UI_APP_ASSERT_CODE);
        s_last_full_us  = esp_timer_get_time();
        s_partial_count = 0;
        /* Plan 7c T7: the panel now holds the BOOT frame blitted above -- seed s_fb_prev_bits so
         * the first riding render's diff is against reality, not the fb_clear() placeholder. */
        memcpy(s_fb_prev_bits, s_fb_bits, sizeof s_fb_prev_bits);
    } else {
        (void)errlog_add(E_DISP_DEAD, (uint32_t)(-disp_rc));
        sys_flags_set(SYS_DISP_DEAD);
        s_next_reinit_us = esp_timer_get_time() + DISP_DEAD_RETRY_US;
    }

    /* Arm the BOOT one-shot's 3 s auto-revert window now, whether disp_init() succeeded or failed
     * (either way the screen is now on the panel or as on-panel as it will get) -- see the comment
     * above where s_model.oneshot was set to ONESHOT_BOOT. SAFE mode's one-shot is persistent
     * (§17.5) and is left untouched (s_oneshot_until_us / s_boot_arm_us stay 0). */
    if (s_model.screen == SCR_ONESHOT && s_model.oneshot == ONESHOT_BOOT) {
        int64_t arm_now    = esp_timer_get_time();
        s_boot_arm_us      = arm_now;   /* Plan 7c T8: drives boot_refmt_check()'s +1 s re-format */
        s_oneshot_until_us = arm_now + (int64_t)ONESHOT_BOOT_MS * 1000;
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
