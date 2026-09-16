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
 * ARMED evaluates each armed S/F line over the previous->current fix segment (§6.4) and the first
 * accepted crossing opens the out-lap (lap_no 0), narrowing candidates to the layouts whose dir_sign
 * matched, and enters LAP_RUNNING; LAP_RUNNING records sector splits (§10.5/§10.7) and completes a lap
 * (§10.4) at each further S/F crossing. Whenever a venue is held (VENUE_FOUND/ARMED/LAP_RUNNING) the
 * engine also watches for leaving it: distance from the centre above radius*VENUE_LEAVE_FACTOR for
 * VENUE_LEAVE_S returns to NO_VENUE.
 *
 * Session 2.5 adds, on top of 2.4's S/F machine: sector gates and per-gate splits (EV_SECTOR,
 * LAP_F_INCOMPLETE); §10.5 layout disambiguation (score candidates that share an S/F, lock at the
 * out-lap's end, EV_LAYOUT_LOCKED); §10.7 sector delta and §10.8 theoretical best; §10.9 on-device
 * track creation (lap_create_begin / lap_mark_gate / lap_create_cancel); §10.10 RTC continuity
 * (lap_export_rtc / lap_import_rtc); §10.11 predictive delta (lap_live_delta_ms).
 *
 * Signs and units: gate direction per §6.4 (dir_sign = sign(cross(p2-p1, motion)), +1 for a p1-left/
 * p2-right line crossed forward). All engine times are int64 gps microseconds; result times are
 * uint32 milliseconds rounded to nearest. */

typedef void (*lap_evt_cb_t)(const event_t *ev, void *ctx);

enum {                              /* lap_state() values */
    LAP_ST_NO_VENUE    = 0,
    LAP_ST_VENUE_FOUND = 1,
    LAP_ST_ARMED       = 2,
    LAP_ST_RUNNING     = 3
};

enum {                              /* lap_t.mode: normal running vs. on-device track creation (§10.9) */
    LAP_MODE_NORMAL = 0,
    LAP_MODE_CREATE = 1
};

/* Union of all candidates' sector gates while disambiguating an out-lap (§10.5). A disambiguation set
 * has at most TRK_MAX_LAYOUTS candidate layouts, each with at most LAP_MAX_SECTORS sector gates; shared
 * gates are deduplicated, so LAP_MAX_UGATES is a safe upper bound on distinct gates. */
#define LAP_MAX_UGATES (TRK_MAX_LAYOUTS * LAP_MAX_SECTORS)

typedef struct {
    uint16_t default_layout_id;     /* §10.5 tie-break, 0 = none */
} lap_cfg_t;
void lap_cfg_defaults(lap_cfg_t *c);   /* default_layout_id = 0 */

/* Per-candidate S/F gate arm/debounce state (§6.4 step 5) plus its disambiguation score (§10.5). The
 * S/F endpoints are pre-projected into the ENU frame once, when the venue is set, because the origin
 * is fixed for the venue's lifetime. */
typedef struct {
    const trk_layout_t *layout;
    geo_enu_t           sf_p, sf_q;     /* S/F line endpoints in ENU (p1 = left, p2 = right) */
    bool                sf_armed;       /* gate is eligible to fire */
    int64_t             sf_last_cross_us;   /* gps_us of this gate's last accepted crossing (re-arm) */
    int                 hits_matching;  /* §10.5: out-lap crossings of a gate this layout contains */
    int                 hits_foreign;   /* §10.5: out-lap crossings of a gate this layout lacks */
} lap_cand_t;

/* A distinct physical sector gate in the disambiguation union (§10.5). in_layout is a bitmask over the
 * candidate array (bit i set => cand[i]'s layout contains this gate). */
typedef struct {
    geo_enu_t p, q;                 /* ENU endpoints (p1 = left, p2 = right) */
    int64_t   last_cross_us;        /* re-arm timer */
    int64_t   cross_us;             /* gps_us this gate was crossed in the current out-lap, 0 = not yet */
    uint8_t   in_layout;            /* bitmask of candidate indices whose layout has this gate */
    int8_t    dir_sign;             /* +1 / -1 (shared by the candidates that own it) */
    bool      armed;
} lap_ugate_t;

/* POD mirror of the lap engine's resumable state (§10.10). The pipeline embeds this in rtc_state_t
 * (CRC32 + version + RTC_RESUME_MAX_S freshness gate are plan 03, not here); lap_import_rtc is
 * unconditional. sector_idx is the number of sector gates already crossed in the current lap. */
typedef struct {
    uint16_t     venue_id;
    uint16_t     layout_id;                     /* 0 if not locked */
    uint16_t     lap_no;
    uint8_t      sector_idx;                    /* sector gates crossed so far this lap */
    uint8_t      mode;                          /* LAP_MODE_* */
    int64_t      lap_start_gps_us;
    int64_t      gate_times[LAP_MAX_SECTORS + 1];   /* [0] = lap start S/F; [j] = sector gate j */
    lap_result_t best, prev;
} lap_rtc_t;

typedef struct {
    lap_cfg_t          cfg;
    const trk_venue_t *venue;           /* current venue, or NULL */
    geo_origin_t       origin;          /* venue centre; the ENU frame for all crossings */
    uint8_t            state;           /* LAP_ST_* */
    uint8_t            mode;            /* LAP_MODE_* (§10.9) */
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
    double             lap_dist_m;          /* Doppler-integrated distance since lap start (§10.5/§10.9) */

    /* §10.5 layout lock and sector gates of the locked layout (ENU, projected once at lock) */
    bool               locked;
    const trk_layout_t *locked_layout;
    geo_enu_t          sec_p[LAP_MAX_SECTORS], sec_q[LAP_MAX_SECTORS];
    bool               sec_armed[LAP_MAX_SECTORS];
    int64_t            sec_last_cross_us[LAP_MAX_SECTORS];
    uint8_t            n_sec;               /* sector gates of the locked layout */

    /* current-lap sector splits (§10.4 step 5, §10.7). gate_times mirrors lap_rtc_t. */
    uint8_t            sec_next;            /* next expected sector gate number, 1..n_sec */
    int64_t            gate_times[LAP_MAX_SECTORS + 1];

    /* §10.5 disambiguation union, used only during the out-lap when n_cand > 1 */
    lap_ugate_t        ugate[LAP_MAX_UGATES];
    uint8_t            n_ugate;

    /* §10.8 best per-sector split across valid laps */
    uint32_t           best_sector_ms[LAP_MAX_SECTORS + 1];
    bool               have_best_sector[LAP_MAX_SECTORS + 1];
    uint8_t            best_sector_count;   /* number of splits the locked layout has (n_sec + 1) */

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

    /* §10.9 on-device track creation state (mode == LAP_MODE_CREATE) */
    trk_layout_t       create_layout;       /* accumulated layout (id 1, "Layout 1") */
    uint16_t           create_venue_id;
    double             create_lat, create_lon;
    int64_t            create_gps_us;        /* gps_us of the S/F press (venue name + distance origin) */
    geo_enu_t          create_sf_p, create_sf_q;
    bool               create_have_sf;

    /* §10.11 predictive delta (O5), caller-provided so the e-paper build carries none (issue #23).
     * The pipeline passes two PRED_TABLE_MAX-entry buffers via lap_set_predictive; NULL/cap 0 = disabled. */
    uint16_t          *pred_best_dist_m;   /* reference (best lap) — lookups read this */
    uint32_t          *pred_best_t_ms;
    uint16_t           pred_best_n;
    uint16_t          *pred_rec_dist_m;    /* records the lap in progress; promoted to best on a new best */
    uint32_t          *pred_rec_t_ms;
    uint16_t           pred_rec_n;
    uint32_t           pred_rec_fixes;
    uint16_t           pred_cap;           /* entries each buffer holds; 0 = predictive disabled */
} lap_t;

void     lap_init(lap_t *L, const lap_cfg_t *cfg);           /* cfg NULL -> defaults; clears best/prev */
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
uint32_t lap_theoretical_best_ms(const lap_t *L);            /* Sigma best_sector_ms, 0 until every split has one (§10.8) */

/* §10.9 on-device track creation. lap_create_begin enters CREATE (state stays NO_VENUE); each
 * lap_mark_gate press adds a gate (gate_idx 0 = S/F, 1.. = sector n, added in order); the next S/F
 * crossing finalises the venue (trk_user_add, auto reverse layout) and enters VENUE_FOUND;
 * lap_create_cancel (long press) aborts. lap_mark_gate returns 0 on success, -1 otherwise, and fills
 * *out_layout (if non-NULL) with the layout built so far. */
void     lap_create_begin(lap_t *L);
int      lap_mark_gate(lap_t *L, uint8_t gate_idx, const gps_fix_t *fix, trk_layout_t *out_layout);
void     lap_create_cancel(lap_t *L);

/* §10.10 RTC continuity. Export mirrors the resumable state into a POD; import restores it into
 * LAP_RUNNING with LAP_F_INTERRUPTED and returns 0, or -1 if the venue id is unknown. */
void     lap_export_rtc(const lap_t *L, lap_rtc_t *out);
int      lap_import_rtc(lap_t *L, const lap_rtc_t *s);

/* Enable §10.11 predictive delta by giving the engine two cap-entry buffers (reference + recording).
 * Both dist buffers are uint16_t[cap], both t buffers uint32_t[cap]. Pass NULLs / cap 0 to disable
 * (the default after lap_init). The buffers are caller-owned and must outlive the lap_t. */
void     lap_set_predictive(lap_t *L, uint16_t *best_dist, uint32_t *best_t,
                            uint16_t *rec_dist, uint32_t *rec_t, uint16_t cap);

/* §10.11 predictive delta O5: elapsed - t_ref(dist_m) against the best lap, in ms. Returns 0 when no
 * best-lap table exists, the engine is not running, or dist is outside the recorded range. *have (if
 * non-NULL) reports whether a delta was produced. now_gps_us times the current elapsed; dist_m is the
 * Doppler-integrated distance since the current S/F (pass lap_t.lap_dist_m). */
int32_t  lap_live_delta_ms(const lap_t *L, int64_t now_gps_us, double dist_m, bool *have);

/* Pure gate geometry (lap_gate.c). lap_gate_line builds a 2*GATE_HALF_WIDTH_M line perpendicular to a
 * compass heading through (lat,lon) with p1 = left / p2 = right, so a forward crossing yields dir_sign
 * +1 (§6.4, §10.9). lap_layout_reverse derives the reverse layout: same S/F line, sectors reversed,
 * dir_sign negated, id = fwd->id + 1, name = "<fwd name> Reverse". */
trk_line_t lap_gate_line(double lat_deg, double lon_deg, double heading_deg);
void       lap_layout_reverse(const trk_layout_t *fwd, trk_layout_t *rev);
#endif
