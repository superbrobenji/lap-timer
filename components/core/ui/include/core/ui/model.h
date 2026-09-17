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
    uint16_t trap_kmh;    /* trap speed for the 1/4 row (0 = none) */
    bool     has_trap;
    /* #40: the 100-0 braking gate (DRAG_BRAKE, core/drag.h) is a stopping DISTANCE in metres, not
     * an elapsed time -- t_ms has no meaning for it. When is_distance is set, the renderer shows
     * "<dist_m> m" instead of formatting t_ms as a time. present still means "gate hit this run"
     * for both kinds of row. */
    uint16_t dist_m;
    bool     is_distance;
} drag_row_t;

typedef struct {
    uint8_t  mode; /* SCR_MODE_LAP / _DRAG (meaningful when screen == SCR_RIDING) */
    uint8_t  page; /* 0/1/2 */

    /* LAP page 0 */
    uint32_t best_ms, prev_ms, cur_ms_at_gate;
    uint8_t  cur_sector_idx;   /* S0..Sn shown on the CUR row */
    int32_t  sector_delta_ms;  /* signed; shown on the last row unless new_best */
    bool     have_best, have_prev, new_best;

    /* LAP page 1 (best-lap detail) */
    uint32_t best_sector_ms[LAP_MAX_SECTORS + 1];
    uint8_t  best_n_sectors;
    uint32_t theo_best_ms;
    bool     have_theo;

    /* LAP page 2 (session stats) */
    uint16_t max_speed_kmh;
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
    char    boot_ver[24];
    char    boot_line[4][22];
    uint8_t boot_n_lines;

    /* OTA one-shot (§20.6): progress bar percentage 0..100. */
    uint8_t ota_pct;

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
};

/* Draws the shared fault-icon strip (spec §20.5 + §17.4): for each set bit in `flags` that maps
 * to an icon, draws its 12x12 icon right-to-left along the bottom-right of the frame. Bits with
 * no icon (§17.4 "--": SYS_DISP_DEAD, SYS_HEAP_LOW, SYS_OTA_PENDING; plus SYS_IMU_DEAD and
 * SYS_FUSION_DISAGREE, which have no matching bitmap in icons.h) draw nothing. Exposed so the
 * DRAG renderer (Task 2) reuses it. */
void fault_strip(fb_t *fb, uint32_t flags, uint8_t batt_pct);

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
