#ifndef CORE_LAP_H
#define CORE_LAP_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/trk.h"
#include "core/event.h"

/* Lap engine (spec §10). Pure C11, no IDF, no allocation; all state lives in caller-owned lap_t.
 *
 * State machine (§10.3): NO_VENUE scans trk_find_nearest every VENUE_SCAN_S and, on a hit within the
 * venue radius, enters VENUE_FOUND (EV_VENUE_FOUND); VENUE_FOUND sets the ENU origin to the venue
 * centre, arms the S/F gate of every candidate layout and enters ARMED (EV_ARMED) on the same fix;
 * ARMED evaluates each armed S/F line over the previous→current fix segment (§6.4) and the first
 * accepted crossing opens the out-lap (lap_no 0), narrowing candidates to the layouts whose dir_sign
 * matched, and enters LAP_RUNNING; LAP_RUNNING completes a lap (§10.4) at each further S/F crossing.
 * Whenever a venue is held (VENUE_FOUND/ARMED/LAP_RUNNING) the engine also watches for leaving it:
 * distance from the centre above radius·VENUE_LEAVE_FACTOR for VENUE_LEAVE_S returns to NO_VENUE.
 * (§10.3 lists the leave test under LAP_RUNNING; evaluating it while ARMED too is a strict superset
 * — an armed-but-departed venue must not stay armed — and lets the transition be reached without a
 * crossing.)
 *
 * Session 2.4 scope: S/F only. Sectors, §10.5 layout disambiguation, §10.7 deltas, theoretical best,
 * on-device creation (lap_mark_gate), RTC restore and predictive delta are session 2.5. Completed
 * results therefore report n_sectors 0 and no sector splits, lap_theoretical_best_ms returns 0 and
 * lap_mark_gate returns -1. Multi-layout venues that share an S/F line all keep running with
 * identical times until 2.5 disambiguates them.
 *
 * Signs and units: gate direction per §6.4 (dir_sign = sign(cross(p2−p1, motion)), +1 for a p1-left/
 * p2-right line crossed forward). All engine times are int64 gps microseconds; result times are
 * uint32 milliseconds rounded to nearest. */

typedef void (*lap_evt_cb_t)(const event_t *ev, void *ctx);

enum {                              /* lap_state() values */
    LAP_ST_NO_VENUE    = 0,
    LAP_ST_VENUE_FOUND = 1,
    LAP_ST_ARMED       = 2,
    LAP_ST_RUNNING     = 3
};

typedef struct {
    uint16_t default_layout_id;     /* §10.5 tie-break, 0 = none; used in 2.5 */
    /* The rest of the tunables (predictive table sizing, per-venue defaults) arrive in 2.5. */
} lap_cfg_t;
void lap_cfg_defaults(lap_cfg_t *c);   /* default_layout_id = 0 */

/* Per-candidate S/F gate arm/debounce state (§6.4 step 5). The S/F endpoints are pre-projected into
 * the ENU frame once, when the venue is set, because the origin is fixed for the venue's lifetime. */
typedef struct {
    const trk_layout_t *layout;
    geo_enu_t           sf_p, sf_q;     /* S/F line endpoints in ENU (p1 = left, p2 = right) */
    bool                sf_armed;       /* gate is eligible to fire */
    int64_t             sf_last_cross_us;   /* gps_us of this gate's last accepted crossing (re-arm) */
} lap_cand_t;

typedef struct {
    lap_cfg_t          cfg;
    const trk_venue_t *venue;           /* current venue, or NULL */
    geo_origin_t       origin;          /* venue centre; the ENU frame for all crossings */
    uint8_t            state;           /* LAP_ST_* */
    uint16_t           forced_layout_id;    /* lap_force_layout; 0 = none */

    /* candidate layouts armed for S/F (all of the venue, or just the forced one) */
    lap_cand_t         cand[TRK_MAX_LAYOUTS];
    uint8_t            n_cand;

    /* NO_VENUE scan cadence */
    int64_t            last_scan_us;
    bool               have_scan;

    /* current lap accumulation */
    uint16_t           lap_no;              /* 0 = out-lap */
    uint8_t            lap_flags;           /* LAP_F_* accumulated during the current lap */
    int64_t            lap_start_gps_us;    /* t_cross that opened the current lap */
    double             lap_dist_m;          /* Doppler-integrated distance since lap start (§10.5/§10.9 input) */

    /* previous VALID fix, for segment crossing (§6.4) and distance integration */
    bool               have_prev_fix;
    geo_enu_t          prev_enu;
    int64_t            prev_gps_us;
    double             prev_speed_mps;

    /* pit detection (§10.6): a continuous slow spell inside LAP_RUNNING */
    bool               pit_slow;
    int64_t            pit_since_us;

    /* leave-venue timer (§10.3) */
    bool               leaving;
    int64_t            leave_since_us;

    /* best / previous results for the session */
    lap_result_t       best, prev;
    bool               have_best, have_prev_result;
} lap_t;

void     lap_init(lap_t *L, const lap_cfg_t *cfg);           /* cfg NULL → defaults; clears best/prev */
void     lap_set_venue(lap_t *L, const trk_venue_t *v);      /* enters VENUE_FOUND, arms candidates */
void     lap_force_layout(lap_t *L, uint16_t layout_id);     /* restrict candidates to this layout id */
void     lap_reset(lap_t *L);                                /* back to NO_VENUE; keeps best/prev (§10.3) */
/* Feed one fix (valid or not) and the current fused stats; may emit events via cb (NULL to ignore).
 * An invalid fix inside LAP_RUNNING sets LAP_F_GPS_LOST on the current lap and is otherwise skipped. */
void     lap_on_fix(lap_t *L, const gps_fix_t *fix, const fused_sample_t *fs, lap_evt_cb_t cb, void *ctx);
uint8_t  lap_state(const lap_t *L);
const lap_result_t *lap_best(const lap_t *L);                /* NULL until a valid lap completes */
const lap_result_t *lap_prev(const lap_t *L);                /* NULL until any lap completes */
uint32_t lap_current_elapsed_ms(const lap_t *L, int64_t now_gps_us);   /* 0 unless LAP_RUNNING */
uint32_t lap_theoretical_best_ms(const lap_t *L);            /* 0 in 2.4 (Σ best sector ms, 2.5) */
/* On-device track creation (§10.9). Returns -1 in 2.4 (implemented in 2.5). */
int      lap_mark_gate(lap_t *L, uint8_t gate_idx, const gps_fix_t *fix, trk_layout_t *out_layout);
#endif
