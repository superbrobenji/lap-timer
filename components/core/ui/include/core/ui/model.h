#ifndef CORE_UI_MODEL_H
#define CORE_UI_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#include "core/types.h" /* LAP_MAX_SECTORS, DRAG_MAX_GATES */
#include "core/ui/render.h"

/* Screen model + moto riding screens (spec §20.4-20.5, §17.4). Pure C11, same constraints as the
 * rest of core/ui (no ESP-IDF/FreeRTOS/malloc/float/libm) — screens_moto.c renders a
 * screen_model_t into a framebuffer as a pure function, which is what makes the PBM goldens in
 * test/snapshots/lap_*.pbm byte-exact and portable across clang/gcc.
 */

enum { SCR_MODE_LAP = 0, SCR_MODE_DRAG = 1 };

/* One row of a DRAG screen (benches on page 0, all/best gates on pages 1/2). */
typedef struct {
    /* Sized for the longest §6.6 gate name, "100-200" (the SPEED_RANGE gate), which is 7 chars +
     * NUL = 8 bytes; "1000ft" (6 chars + NUL = 7) is the next longest. The plan's original char[6]
     * (sized to the shorter "0-100"/"1/4"/"60ft"/"100-0" examples in this same comment) is one byte
     * too small even for "1000ft" and two short for "100-200" -- extended here by Task 2, the first
     * DRAG-screen renderer to actually populate every §6.6 gate label. */
    char     label[8];   /* "0-100" / "1/4" / "60ft" / "100-0" / "100-200" / "1000ft" ... */
    uint32_t t_ms;        /* elapsed for the gate; 0 + !present => "--" */
    bool     present;     /* gate hit this run */
    uint16_t trap_kmh;    /* trap speed for the 1/4 row (0 = none) */
    bool     has_trap;
} drag_row_t;

typedef struct {
    uint8_t  mode; /* SCR_MODE_LAP / _DRAG */
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

/* Single render entry point: dispatches on m->mode + m->page, clears the fb, draws the screen and
 * leaves fb->dirty as the changed region (the whole frame for a full screen render). SCR_MODE_DRAG
 * renders pages 0 (benches, §11.4)/1 (all gates of the last run)/2 (best per gate this session);
 * see the SCR_MODE_DRAG case in screens_moto.c. */
void screens_moto_render(fb_t *fb, const screen_model_t *m);

#endif
