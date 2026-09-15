#ifndef REPLAY_SYNTH_H
#define REPLAY_SYNTH_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/types.h"
#include "core/trk.h"

/* Synthetic circuit and run model (spec §22.2). Host-only C11, no global state.
 *
 * Geometry: an N-vertex polygon whose corners are replaced by circular arcs of radius
 * corner_radius_m, driven clockwise (right-hand corners) or anticlockwise. Speed profile on every
 * straight: enter at v_corner, accelerate at a_acc, cruise at the lap's v_max if it is reached, brake
 * at a_brk so the next arc is entered at v_corner; every arc is driven at constant v_corner. All
 * kinematics are closed-form (piecewise constant acceleration), so gate crossing times are exact.
 *
 * Frames and signs: ENU metres about the venue centre (cfg origin_lat/lon = polygon centroid).
 * Heading is compass degrees, 0 = north, clockwise positive (gps_fix_t.head_e5 convention). a_lon is
 * + when accelerating. Lateral g and lean are + to the right, yaw rate is + for a left turn
 * (core/types.h). Arc lengths are "lap frame" (0 = start of straight 0, wraps at length_m) unless
 * the name says s_total (unwrapped from the run's first lap table). Times are double seconds of run
 * time ("run time"): 0 at the run start, which is start_before_m before the first S/F crossing. */

#define SYNTH_MAX_VERTICES 32
#define SYNTH_MAX_GATES    (LAP_MAX_SECTORS + 1)          /* S/F plus sector gates */
#define SYNTH_MAX_LAPS     200                             /* full laps; the run adds an out-lap and a run-out */
#define SYNTH_MAX_TABLES   (SYNTH_MAX_LAPS + 3)
#define SYNTH_MAX_PIECES   (SYNTH_MAX_VERTICES * 4)       /* per lap: a straight is up to 3 pieces, an arc 1 */
#define SYNTH_GATE_MARGIN_M 20.0                           /* gates keep this far from a straight's ends */

typedef struct {
    int      n_vertices;        /* 3..SYNTH_MAX_VERTICES; default 12 */
    double   length_m;          /* lap length along the driven path (straights + arcs); default 2500 */
    double   corner_radius_m;   /* default 40 */
    double   irregularity;      /* 0 = regular polygon; else vertex radius k scaled by 1 + irregularity·U(-1,1); default 0 */
    bool     clockwise;         /* default true (right-hand corners: positive lean, negative yaw rate) */
    uint32_t seed;              /* drives irregularity and lap_var; default 1 */
    double   v_corner_mps;      /* speed through every arc; default 15 */
    double   v_max_mps;         /* cruise cap; default 50 */
    double   a_acc_mps2;        /* default 3 */
    double   a_brk_mps2;        /* default 6 (magnitude) */
    double   lap_var;           /* per-table rider factor k = 1 + lap_var·U(-1,1) applied to a_acc, a_brk and v_max (v_corner stays constant, so speed is continuous where tables meet); default 0.03 */
    int      laps;              /* full laps after the first S/F crossing; 1..SYNTH_MAX_LAPS; default 10 */
    double   start_before_m;    /* run starts this far before S/F (out-lap length); default 300 */
    double   stop_after_m;      /* run continues this far past the last S/F; default 200 */
    double   sf_frac;           /* requested S/F arc length as a fraction of length_m; default 0.5 / n_vertices */
    int      n_sector_gates;    /* 0..LAP_MAX_SECTORS; default 2, requested evenly after S/F */
    double   gate_half_width_m; /* default GATE_HALF_WIDTH_M */
    double   origin_lat_deg;    /* default -34.0300 */
    double   origin_lon_deg;    /* default 18.7300 (about 30 km from every bundled venue) */
    uint16_t venue_id;          /* default TRK_USER_ID_BASE */
} synth_cfg_t;
void synth_cfg_defaults(synth_cfg_t *c);

typedef enum { SYNTH_P_ACCEL = 0, SYNTH_P_CRUISE = 1, SYNTH_P_BRAKE = 2, SYNTH_P_ARC = 3 } synth_piece_kind_t;

typedef struct {
    synth_piece_kind_t kind;
    double s0, len;             /* lap-frame arc length at piece start; piece length (> 0) */
    double t0, dur;             /* lap-frame time at piece start (0 at s = 0); piece duration */
    double e0, n0;              /* ENU point at piece start */
    double head0_rad;           /* compass heading at piece start, radians */
    double v0, a;               /* speed at piece start; longitudinal acceleration (signed; 0 for cruise and arcs) */
    double ce, cn, turn_rad;    /* arcs only: centre and signed turn over the piece (+ = left) */
} synth_piece_t;

typedef struct {                /* one lap-frame period's kinematic table (s = 0 .. length_m) */
    synth_piece_t pieces[SYNTH_MAX_PIECES];
    int    n_pieces;
    double lap_time_s;          /* time from s = 0 to s = length_m in this table */
    double k;                   /* this table's rider factor 1 + lap_var·U(-1,1) */
    double v_max_mps;           /* cfg.v_max_mps · k */
    double a_acc_mps2;          /* cfg.a_acc_mps2 · k */
    double a_brk_mps2;          /* cfg.a_brk_mps2 · k */
} synth_lap_t;

typedef struct {                /* truth at one instant */
    int    table;               /* lap table index (0 = the table the run starts in) */
    double s_m;                 /* lap-frame arc length, 0 ≤ s < length_m */
    double s_total_m;           /* unwrapped: table·length_m + s_m */
    double e_m, n_m;
    double heading_deg;         /* compass, [0, 360) */
    double v_mps, a_lon_mps2;
    double g_lat, lean_deg, yaw_rate_dps;
    bool   on_arc;
} synth_state_t;

typedef struct {
    double     s_m;             /* lap-frame arc length of the gate centre after adjustment onto a straight */
    double     e_m, n_m;        /* centre */
    double     heading_deg;     /* driving heading at the centre */
    trk_line_t line;            /* p1 = left end, p2 = right end in the driving direction (dir_sign +1) */
} synth_gate_t;

typedef struct {
    synth_cfg_t cfg;
    /* geometry, shared by every table */
    int    n_vertices;
    double ve[SYNTH_MAX_VERTICES], vn[SYNTH_MAX_VERTICES];      /* scaled polygon vertices, ENU, driving order */
    double straight_len[SYNTH_MAX_VERTICES];                    /* straight k runs from arc k's end to arc k+1's start */
    double arc_len[SYNTH_MAX_VERTICES];                         /* arc k is at vertex k */
    double turn_rad[SYNTH_MAX_VERTICES];                        /* signed turn at vertex k (+ = left) */
    double length_m;            /* Σ straight_len + Σ arc_len (== cfg.length_m within 1e-6) */
    int    n_gates;             /* 1 + cfg.n_sector_gates; gates[0] is S/F */
    synth_gate_t gates[SYNTH_MAX_GATES];
    /* run */
    int    n_tables;            /* cfg.laps + 3 */
    synth_lap_t tables[SYNTH_MAX_TABLES];
    double table_t0[SYNTH_MAX_TABLES];   /* lap-frame-accumulated time at which table i starts (table 0 starts at 0) */
    double s_total_start;       /* where t = 0 is: lap-frame s_start = wrap(s_sf - start_before_m), in table 0 */
    double s_total_end;         /* s_total of S/F crossing cfg.laps plus stop_after_m */
    double t_offset_s;          /* accumulated time at s_total_start; run time = accumulated time - t_offset_s */
    double duration_s;          /* run time at s_total_end */
} synth_run_t;

/* Builds geometry, gates and every lap table. Returns 0, or -1 with a message in err (may be NULL)
 * when the configuration is invalid: n_vertices, laps, gates or fractions out of range; corner arcs
 * that do not fit (tangent lengths exceed an edge); a straight shorter than 2·SYNTH_GATE_MARGIN_M;
 * v_corner ≥ v_max; non-positive accelerations; start_before_m + stop_after_m ≥ length_m. */
int    synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap);

/* Truth at run time t (clamped to [0, duration_s]). */
void   synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out);
/* Inverse: run time at which unwrapped arc length s_total is reached (s_total clamped to the run). */
double synth_run_time_at_s(const synth_run_t *r, double s_total_m);
/* s_total of the n-th S/F passage after the run start, n = 0..cfg.laps. */
double synth_run_sf_s_total(const synth_run_t *r, int n);
/* Run times of a full lap's crossings: out[0] = S/F starting lap_no, out[1..n_gates-1] = sector gates
 * in driving order, out[n_gates] = the S/F ending the lap. lap_no = 1..cfg.laps. Returns n_gates + 1. */
int    synth_run_lap_crossings(const synth_run_t *r, int lap_no, double *out, size_t out_cap);
/* Lap time = out[n_gates] - out[0] of the above; negative if lap_no is out of range. */
double synth_run_lap_time(const synth_run_t *r, int lap_no);
/* Fills a venue that passes trk_validate_venue: id cfg.venue_id, name "Synthetic", centre = origin,
 * radius VENUE_RADIUS_DEFAULT_M, one layout {id 1, "Full", dir_sign +1, sf = gates[0].line,
 * sectors = gates[1..].line, length_m rounded}. */
void   synth_run_venue(const synth_run_t *r, trk_venue_t *v);
/* Exact inverse of geo_to_enu for the same origin (see geo.c): lat = lat0 + n/R, lon = lon0 + e/(R·cos lat0). */
void   synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg);
#endif
