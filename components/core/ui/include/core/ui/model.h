#ifndef CORE_UI_MODEL_H
#define CORE_UI_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#include "core/types.h" /* LAP_MAX_SECTORS, DRAG_MAX_GATES */
#include "core/ui/render.h"

/* Screen model + moto riding/one-shot/menu screens (spec §20.4-20.7, §17.4). Pure C11, same
 * constraints as the rest of core/ui (no ESP-IDF/FreeRTOS/malloc/float/libm) — screens_moto.c
 * renders a screen_model_t into a framebuffer as a pure function, which is what makes the PBM
 * goldens in test/snapshots/ byte-exact and portable across clang/gcc.
 */

enum { SCR_MODE_LAP = 0, SCR_MODE_DRAG = 1 };

/* LAP page 0's big slot (spec 7b §3-4): what render_lap_page0's 64 px event-card number shows. */
enum { BIG_NONE = 0, BIG_SECTOR_DELTA = 1, BIG_LAP_DELTA = 2 };

/* Top-level screen selector (spec §20.6-20.7): which of the three screen families m->screen
 * selects. screens_render() (screens_moto.c) dispatches on this. */
enum { SCR_RIDING = 0, SCR_MENU = 1, SCR_ONESHOT = 2 };

/* One-shot screen selector (spec §20.6), valid when m->screen == SCR_ONESHOT. */
enum {
    ONESHOT_BOOT = 0,
    ONESHOT_VENUE,
    ONESHOT_SAFE,
    ONESHOT_LOWBATT,
    ONESHOT_OTA,
    ONESHOT_OTA_FAIL,
    ONESHOT_CALIBRATE,
    ONESHOT_NEWTRACK,
};

/* One row of a DRAG screen (benches on page 0, all/best gates on pages 1/2). */
typedef struct {
    /* Sized for the longest §6.6 gate name, "100-200" (the SPEED_RANGE gate), which is 7 chars +
     * NUL = 8 bytes; "1000ft" (6 chars + NUL = 7) is the next longest. The plan's original char[6]
     * (sized to the shorter "0-100"/"1/4"/"60ft"/"100-0" examples in this same comment) is one byte
     * too small even for "1000ft" and two short for "100-200" -- extended here by Task 2, the first
     * DRAG-screen renderer to actually populate every §6.6 gate label. */
    char     label[8];   /* "0-100" / "1/4" / "60ft" / "100-0" / "100-200" / "1000ft" ... */
    uint32_t t_ms;        /* elapsed for the gate; 0 + !present => "--" (ignored if is_distance) */
    bool     present;     /* gate hit this run */
    uint16_t trap_cms;    /* trap speed for the 1/4 row (0 = none), raw cm/s straight from the gate
                            * event (EV_DRAG_GATE's arg32b) -- the display unit conversion happens
                            * at render time only (speed_display(trap_cms, m->units)), same rule as
                            * every other speed on screen (design §3, ruling R-4, Plan 7c T4 fix 1):
                            * freezing it in the display unit at event time would mislabel it after
                            * a later units toggle. */
    bool     has_trap;
    /* #40: the 100-0 braking gate (DRAG_BRAKE, core/drag.h) is a stopping DISTANCE in metres, not
     * an elapsed time -- t_ms has no meaning for it. When is_distance is set, the renderer shows
     * "<dist_m> m" instead of formatting t_ms as a time. present still means "gate hit this run"
     * for both kinds of row. */
    uint16_t dist_m;
    bool     is_distance;
} drag_row_t;

/* OTA one-shot status line (spec §20.6). Carried on EV_OTA.flags by the app (Task 2). */
enum { OTA_PHASE_RECEIVING = 0, OTA_PHASE_VERIFYING = 1, OTA_PHASE_REBOOTING = 2 };

typedef struct {
    uint8_t  mode; /* SCR_MODE_LAP / _DRAG (meaningful when screen == SCR_RIDING) */
    uint8_t  page; /* 0/1/2 */
    uint8_t  units; /* Plan 7c T4 (design §3): 0 = km/h, 1 = mph (CFG_UNITS_KMH/CFG_UNITS_MPH); set
                      * from s_cfg.units at boot and on every menu toggle -- every speed_display()
                      * call on screen (LAP page 2 MAX SPD, the DRAG trap row) uses it */

    /* LAP page 0 */
    uint32_t best_ms, prev_ms;
    uint32_t cur_ms;           /* Plan 7c T6 (design §4): running lap-clock time while cur_running,
                                 * fed once a second by ui.c's clock_tick(); rendered as the footer's
                                 * CUR m:ss (fmt_time_s, screens_moto.c) in place of LAST */
    bool     cur_running;      /* true while display.live_clock is on and a lap is in progress; set
                                 * and cleared by ui.c's clock_tick() */
    uint8_t  cur_sector_idx;   /* sectors completed this lap (1-based); 0 = none yet */
    bool     have_best, have_prev, new_best;

    /* LAP page 0 event card (spec 7b §3-4): what the 64 px big slot shows, plus the marker's lap
     * number and the sector-detail row (page 1 row 4) filled from EV_SECTOR as the lap runs. */
    uint8_t  big_kind;                                    /* BIG_* : what LAP page 0's big slot shows */
    int32_t  big_delta_ms;                                /* signed; valid for BIG_SECTOR_DELTA / BIG_LAP_DELTA */
    uint8_t  big_sector_idx;                              /* 0-based sector index as emitted by EV_SECTOR (arg16); reserved -- renderers use cur_sector_idx */
    uint16_t lap_no;                                      /* running lap number (laps_total + 1 while a lap runs) */
    int32_t  last_sector_delta_ms[LAP_MAX_SECTORS + 1];   /* page 1 row 4, index = sector idx */
    bool     have_last_sector_delta[LAP_MAX_SECTORS + 1];

    /* LAP page 1 (best-lap detail) */
    uint32_t best_sector_ms[LAP_MAX_SECTORS + 1];
    bool     have_best_sector[LAP_MAX_SECTORS + 1];   /* Plan 7c T3: gates the value row (design §2) */
    uint8_t  best_n_sectors;
    uint32_t theo_best_ms;
    bool     have_theo;

    /* LAP page 2 (session stats) */
    uint16_t max_speed_cms;   /* Plan 7c T3: raw cm/s; converted to the display unit at render (speed_display) */
    uint8_t  lean_l_deg, lean_r_deg;
    uint16_t lat_g_e2, acc_g_e2, brk_g_e2; /* g x 100 */
    uint16_t laps_total, laps_valid;

    /* DRAG rows (p0 benches / p1 all gates / p2 best per gate) */
    drag_row_t drag[DRAG_MAX_GATES];
    uint8_t    drag_n;
    bool       drag_armed;

    /* venue + status */
    char     venue_name[33], layout_name[25];
    uint32_t flags; /* sys_flags snapshot -> fault-icon strip */
    uint8_t  batt_pct;

    /* ---- top-level screen selector (spec §20.6-20.7) ---- */
    uint8_t screen;  /* SCR_RIDING / SCR_MENU / SCR_ONESHOT */
    uint8_t oneshot; /* ONESHOT_* when screen == SCR_ONESHOT */

    /* BOOT one-shot (§20.6, §17.6): name + version banner, up to 4 self-test "OK"/"FAIL" lines
     * (the caller pre-formats each line, e.g. "IMU     OK"). */
    char    boot_name[16];
    char    boot_ver[32];   /* holds a full `git describe --dirty` version (e.g. "v0.1.0-123-gdeadbee-dirty") */
    char    boot_line[4][22];
    uint8_t boot_n_lines;

    /* OTA one-shot (§20.6): progress bar percentage 0..100. */
    uint8_t ota_pct;
    uint8_t ota_phase;  /* OTA_PHASE_* -- the status line under the percentage (spec §20.6) */

    /* MENU (§20.7): a titled, scrollable list of up to 12 items. */
    uint8_t     menu_sel;       /* selected item index */
    uint8_t     menu_n;         /* item count, <= 12 */
    uint8_t     menu_top;       /* first visible row (caller-driven scroll) */
    const char *menu_items[12]; /* item labels (caller-owned storage) */
} screen_model_t;

/* sys_flags bit positions (spec §17.4), mirrored locally so core/ui stays pure C11 and does not
 * include the app-layer app/lt_sup.h (which pulls in FreeRTOS headers). The app's lt_sup.h enum
 * (SYS_GPS_DEAD..SYS_FUSION_DISAGREE) uses these same bit positions; keep the two in sync by hand
 * if §17.4 ever changes. Prefixed SCR_SYS_* (rather than SYS_*) so a translation unit that somehow
 * pulls in both headers does not hit a duplicate-enumerator redefinition. */
enum {
    SCR_SYS_GPS_DEAD = 0,
    SCR_SYS_GPS_NOFIX,
    SCR_SYS_IMU_DEAD,
    SCR_SYS_IMU_SUSPECT,
    SCR_SYS_DISP_DEAD,
    SCR_SYS_STORAGE_DEAD,
    SCR_SYS_STORAGE_FULL,
    SCR_SYS_STORAGE_DEGRADED,
    SCR_SYS_BATT_LOW,
    SCR_SYS_SAFE_MODE,
    SCR_SYS_HEAP_LOW,
    SCR_SYS_DISP_TEMP_THROTTLE,
    SCR_SYS_OTA_PENDING,
    SCR_SYS_FUSION_DISAGREE,
    /* ui-level strip bits (not sys flags): folded into screen_model_t.flags by the ui task after
     * masking the sys snapshot with SCR_SYS_BITS_MASK, so SYS_RECOVERY_MODE (bit 14 in lt_sup.h,
     * never reaches the ui -- recovery mode does not start it) can never alias SCR_UI_SIM. */
    SCR_UI_SIM    = 14,   /* ICON_SIM: this firmware feeds simulated GPS/IMU (CFG_GPS_SIM || CFG_IMU_SIM) */
    SCR_UI_MOVING = 15,   /* ICON_MOVING: the menu is motion-locked (gspeed >= MENU_LOCK_SPEED_KMH, §20.7) */
    SCR_UI_LINK   = 16,   /* ICON_LINK: the dev-kit (or a BLE peer) is connected -- link_peer_present() */
};
#define SCR_SYS_BITS_MASK 0x3FFFu   /* bits 0..13: the sys_flags snapshot the strip may show */
#define SCR_STRIP_BITS    17        /* bits 0..16: everything fault_strip() walks */

/* Draws the shared fault-icon strip (spec §20.5 + §17.4): for each set bit in `flags` that maps
 * to an icon, draws its 12x12 icon right-to-left along the bottom-right of the frame. Bits with
 * no icon (§17.4 "--": SYS_DISP_DEAD, SYS_HEAP_LOW, SYS_OTA_PENDING; plus SYS_IMU_DEAD and
 * SYS_FUSION_DISAGREE, which have no matching bitmap in icons.h) draw nothing. Exposed so the
 * DRAG renderer (Task 2) reuses it. */
void fault_strip(fb_t *fb, uint32_t flags, uint8_t batt_pct);

/* Status line under the OTA one-shot's percentage (spec §20.6): any phase value other than the
 * three OTA_PHASE_* enumerators reads as RECEIVING, the phase the screen is first shown in. */
const char *ota_phase_label(uint8_t phase);

/* Renders the moto riding screens: dispatches on m->mode + m->page, clears the fb, draws the
 * screen and leaves fb->dirty as the changed region (the whole frame for a full screen render).
 * SCR_MODE_DRAG renders pages 0 (benches, §11.4)/1 (all gates of the last run)/2 (best per gate
 * this session); see the SCR_MODE_DRAG case in screens_moto.c. Unlike screens_render() below, this
 * does not look at m->screen -- it always renders the riding layout, which is what the session 4.2
 * host tests exercise directly. */
void screens_moto_render(fb_t *fb, const screen_model_t *m);

/* Top-level render entry point (spec §20.6-20.7): dispatches on m->screen -- SCR_RIDING to
 * screens_moto_render(), SCR_MENU to the menu list renderer, SCR_ONESHOT to the one-shot screen
 * renderer (selected by m->oneshot). Every path clears the fb and leaves fb->dirty as the changed
 * region, same contract as screens_moto_render(). This is the entry point the app-side ui task
 * (session 4.3 Task 2) calls every render. */
void screens_render(fb_t *fb, const screen_model_t *m);

#endif
