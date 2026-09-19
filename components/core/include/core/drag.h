#ifndef CORE_DRAG_H
#define CORE_DRAG_H
#include <stdint.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"
#include "core/event.h"

/* Drag engine (spec §11, §6.6). Pure C11, no IDF, no allocation; all state lives in caller-owned
 * drag_t. Vehicle-agnostic (moto/car): it consumes only fused samples (100 Hz) and GPS fixes.
 *
 * Speed/distance (§6.6): v_est integrates a_lon (= g_lon·G_MPS2) per fused sample by trapezoid and is
 * re-anchored to the Doppler gSpeed at every valid fix (drag_on_fix; Doppler is authoritative, the IMU
 * only bridges the 100–200 ms between fixes). dist is the trapezoid integral of v_est from t0. Both are
 * integrated continuously; arming zeroes them and launch re-bases dist to t0 (see §11.2).
 *
 * State machine (§11.2): IDLE → ARMED → LAUNCHED → DONE.
 *   IDLE     — nothing; watches for the arm condition.
 *   ARMED    — v_est < DRAG_ARM_SPEED_KMH and FUS_STILL held for DRAG_ARM_STILL_S. Emits EV_DRAG_ARMED;
 *              zeroes dist and v_est; the 1 s fused-sample history ring keeps rolling for the launch
 *              back-scan.
 *   LAUNCHED — g_lon > DRAG_LAUNCH_G held for DRAG_LAUNCH_HOLD_MS. t0 is back-dated to the first sample
 *              of the contiguous g_lon > DRAG_LAUNCH_SCAN_G run ending at detection (scanned out of the
 *              history ring); with rollout enabled t0 is moved to where dist reaches DRAG_ROLLOUT_M and
 *              dist re-zeroed there (DRAG_F_ROLLOUT). Emits EV_DRAG_LAUNCH. Gates are evaluated every
 *              sample with the §6.6 linear interpolation for the crossing time.
 *   DONE     — the 1/4-mile gate is hit (DRAG_F_QUARTER); or v_est < 0.5·v_peak with no gate for
 *              DRAG_TIMEOUT_S after a peak; or v_est < DRAG_ARM_SPEED_KMH. The result is frozen and
 *              EV_DRAG_DONE emitted; the braking (100-0) gate may still complete after DONE.
 * Abort (§11.2): v_est < 1 km/h within DRAG_FALSE_START_S of launch discards the run and returns to
 * ARMED (false start, e.g. a clutch-dump stall).
 *
 * Gate kinds (§11.1): SPEED_FROM0 (0→a km/h), SPEED_RANGE (a→b km/h), DIST (a cm), BRAKE (a→b km/h, the
 * 100-0 stopping distance). The 1/4 gate — the trap gate — is the DIST gate with the largest distance
 * a; hitting it ends the run and its trap_cms is the mean v_est over dist ∈ [D − TRAP_DIST_M, D] (§6.6).
 *
 * Units and signs: gate a/b are km/h (SPEED_*) or cm (DIST); g_lon is + when accelerating (core/types.h).
 * All engine times are int64 gps microseconds; result times are uint32 ms rounded to nearest. cfg.units
 * selects only the display/bench list, not the wire values. Default gate list is the §11.1 eleven. */

/* One default gate (spec §5.2). id is stable and logged (DRAG_GATE/DRAG_RUN, §12.3). */
typedef struct { uint8_t id; uint8_t kind; uint16_t a, b; /* km/h (SPEED_*) or cm (DIST) */ } drag_gate_def_t;

enum {                              /* drag_gate_def_t.kind */
    DRAG_SPEED_FROM0 = 0,           /* 0 → a km/h */
    DRAG_SPEED_RANGE = 1,           /* a → b km/h */
    DRAG_DIST        = 2,           /* a cm from t0 */
    DRAG_BRAKE       = 3            /* a → b km/h (100-0 stopping distance) */
};

enum {                              /* drag_state() values (§11.2) */
    DRAG_ST_IDLE     = 0,
    DRAG_ST_ARMED    = 1,
    DRAG_ST_LAUNCHED = 2,
    DRAG_ST_DONE     = 3
};

enum { DRAG_UNITS_KMH = 0, DRAG_UNITS_MPH = 1 };   /* cfg.units: display + bench-list selection only */

/* ≥ 1 s of fused samples at FUSION_HZ for the launch back-scan (§11.2). Rounded up past FUSION_HZ so a
 * full second of samples always fits with headroom; the newest DRAG_HIST_N entries are retained. */
#define DRAG_HIST_N (FUSION_HZ + 28)     /* 128 entries = 1.28 s at 100 Hz */

typedef struct {
    int64_t gps_us;                 /* fused-sample time */
    float   g_lon;                  /* longitudinal g at that sample */
} drag_hist_t;                      /* only the two fields the back-scan and dist re-base need */

typedef struct {
    drag_gate_def_t gates[DRAG_MAX_GATES];
    uint8_t  n_gates;
    uint16_t benches_kmh[4];        /* headline benches (§11.4); SPEED_FROM0 gates whose a is listed */
    uint8_t  n_benches;
    bool     rollout;               /* default OFF: for GPS timing the 1 ft rollout adds error, so the
                                     * timer starts at the launch instant, not after 1 ft (§11.2, §15.1) */
    uint8_t  units;                 /* DRAG_UNITS_* — display/bench selection only */
} drag_cfg_t;

void drag_cfg_defaults(drag_cfg_t *c);   /* the §11.1 eleven gates, benches {100,200,300}, rollout off, km/h */

/* Upper bound on the events drag_on_fused can append in a single call: at most one ARMED or LAUNCH or
 * DONE plus a full sweep of gates (DRAG_MAX_GATES) with headroom. Callers pass a buffer this large. */
#define DRAG_EVT_MAX (DRAG_MAX_GATES + 3)

typedef struct {
    drag_cfg_t cfg;
    uint8_t    state;               /* DRAG_ST_* */
    uint16_t   run_no;              /* increments on each launch */

    /* §6.6 speed/distance integration (SI: m/s, m, s from gps_us) */
    double  v_est;                  /* dead-reckoned speed, m/s */
    double  dist_m;                 /* integral of v_est from t0, m */
    double  a_prev;                 /* previous sample a_lon, m/s^2 (trapezoid) */
    double  v_prev;                 /* previous sample v_est, m/s */
    double  dist_prev;              /* previous sample dist, m */
    int64_t prev_gps_us;            /* previous fused-sample time */
    bool    have_prev;
    int64_t t0_gps_us;              /* launch instant (back-dated), 0 until launched */
    double  v_peak;                 /* peak v_est since launch, m/s */
    int64_t last_gate_gps_us;       /* time of the last gate hit (DONE timeout, §11.2) */

    /* arm / launch / false-start / done timers */
    bool    still_run;              /* a slow+still spell is in progress */
    int64_t still_since_us;
    bool    launch_run;             /* a g_lon>DRAG_LAUNCH_G spell is in progress */
    int64_t launch_since_us;
    int64_t done_gps_us;            /* time DONE was entered (DONE→IDLE after 5 s) */
    bool    rollout_done;

    /* braking (100-0) gate accumulation */
    bool    brake_active;
    bool    brake_done;
    int64_t brake_start_gps_us;
    double  brake_dist_m;

    /* trap accumulation for the 1/4 (max-distance DIST) gate */
    double   trap_sum;              /* Σ v_est over the trap window, m/s */
    uint32_t trap_n;
    uint8_t  quarter_idx;           /* index of the 1/4 gate in cfg.gates, or DRAG_NO_GATE */
    double   quarter_dist_m;

    /* SPEED_RANGE bookkeeping, parallel to cfg.gates */
    bool    range_started[DRAG_MAX_GATES];
    int64_t range_a_gps_us[DRAG_MAX_GATES];
    double  range_a_dist_m[DRAG_MAX_GATES];

    /* fused-sample history ring for the launch back-scan (§11.2) */
    drag_hist_t hist[DRAG_HIST_N];
    uint16_t    hist_head;          /* next write slot */
    uint16_t    hist_count;         /* valid entries, ≤ DRAG_HIST_N */

    /* results (§11.3). cur is the run in progress / last frozen run. best is a composite whose gates[]
     * carry the best-per-gate value across the session (see drag_best). */
    drag_result_t cur;
    drag_result_t best;
    bool          have_best;
} drag_t;

#define DRAG_NO_GATE 0xFF

void     drag_init(drag_t *D, const drag_cfg_t *cfg);   /* cfg NULL → defaults; clears results */
void     drag_reset(drag_t *D);                         /* back to IDLE; keeps cfg and session best */
/* 100 Hz. Events for this sample are appended to out[] (a caller-owned buffer of at least DRAG_EVT_MAX
 * entries), in emission order; *n_out is set to 0 on entry and left holding the count. Caller drains. */
void     drag_on_fused(drag_t *D, const fused_sample_t *fs, event_t *out, int cap, int *n_out);
void     drag_on_fix(drag_t *D, const gps_fix_t *fix);  /* re-anchors v_est to Doppler gSpeed */
uint8_t  drag_state(const drag_t *D);
const drag_result_t *drag_current(const drag_t *D);     /* run in progress or last frozen; NULL if none */
/* Best per gate across the session (§11.3): lowest time_ms among hit gates (BRAKE = shortest dist_cm).
 * Returns a composite drag_result_t* whose gates[] slot for gate_id carries that best value, or NULL if
 * gate_id was never hit in any completed run. */
const drag_result_t *drag_best(const drag_t *D, uint8_t gate_id);
#endif
