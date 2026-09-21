#include "unity.h"
#include "core/lap.h"
#include "core/trk.h"
#include "core/geo.h"
#include "core/consts.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Lap engine tests (spec §10, §22.1). Pure C11 so this file also builds and runs on the ESP32
 * (core_selftest). Geometry is expressed in ENU metres about the venue centre and converted to the
 * lat/lon a gps_fix_t carries; the engine converts back through the same tangent-plane origin, so the
 * round trip is exact to floating point. The S/F line runs east-west across the centre (±15 m), so a
 * NORTHBOUND crossing has dir_sign +1 and a SOUTHBOUND crossing dir_sign -1 (§6.4). Sector gates are
 * modelled the same way (east-west lines at chosen northings). */

#define DEG_PER_M_LAT   ((180.0 / GEO_PI) / EARTH_R_M)     /* degrees of latitude per metre north */
#define SPD_MMS         40000   /* 40 m/s: matches the 80 m / 2 s crossing segment, so t_cross is exact */

void setUp(void) { trk_init(); }
void tearDown(void) {}

/* ---- event capture ---- */
typedef struct {
    int      n;
    uint8_t  type[64];
    uint8_t  flags[64];
    uint16_t arg16[64];
    uint32_t arg32[64];
    uint32_t arg32b[64];
} evlog_t;
static void ev_cb(const event_t *ev, void *ctx)
{
    evlog_t *e = (evlog_t *)ctx;
    if (e->n < 64) {
        e->type[e->n]   = ev->type;
        e->flags[e->n]  = ev->flags;
        e->arg16[e->n]  = ev->arg16;
        e->arg32[e->n]  = ev->arg32;
        e->arg32b[e->n] = ev->arg32b;
    }
    e->n++;
}
static int count_type(const evlog_t *e, uint8_t type)
{
    int c = 0;
    for (int i = 0; i < e->n && i < 64; i++) if (e->type[i] == type) c++;
    return c;
}

/* Feed one pre-built fix and drain the caller-returned events into log (NULL to discard). */
static void drive_fix(lap_t *L, const gps_fix_t *f, evlog_t *log)
{
    event_t evs[LAP_EVT_MAX];
    int nev = 0;
    lap_on_fix(L, f, NULL, evs, LAP_EVT_MAX, &nev);
    for (int i = 0; i < nev; i++) if (log) ev_cb(&evs[i], log);
}

/* ---- geometry helpers (ENU metres about a venue centre) ---- */
static double lat_of(double lat0, double n_m)             /* latitude n_m metres north of lat0 */
{
    return lat0 + n_m * DEG_PER_M_LAT;
}
static double lon_of(double lon0, double lat0, double e_m) /* longitude e_m metres east of lon0 */
{
    return lon0 + e_m / (EARTH_R_M * cos(lat0 * GEO_PI / 180.0)) * (180.0 / GEO_PI);
}

static gps_fix_t fix_ll(double lat, double lon, int64_t gps_us, int32_t gspeed_mms, bool valid)
{
    gps_fix_t f;
    memset(&f, 0, sizeof f);
    f.gps_us     = gps_us;
    f.mono_us    = gps_us;
    f.lat_e7     = (int32_t)lround(lat * 1e7);
    f.lon_e7     = (int32_t)lround(lon * 1e7);
    f.gspeed_mms = gspeed_mms;
    f.fix_type   = 3;
    f.sats       = 9;
    f.hacc_mm    = 1500;
    f.flags      = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE;
    f.valid      = valid ? 1 : 0;
    return f;
}

/* Feed one fix at ENU (e, n) metres about (lat0, lon0). */
static void feed(lap_t *L, double lat0, double lon0, double e, double n, int64_t t_us,
                 int32_t spd, bool valid, evlog_t *log)
{
    gps_fix_t f = fix_ll(lat_of(lat0, n), lon_of(lon0, lat0, e), t_us, spd, valid);
    drive_fix(L, &f, log);
}

/* An east-west gate line spanning ±GATE_HALF_WIDTH_M about ENU (e_c, n_m): p1 = west (left for a
 * northbound crossing), p2 = east (right), so a northbound crossing has dir_sign +1. */
static trk_line_t ew_gate(double lat0, double lon0, double e_c, double n_m)
{
    trk_line_t g;
    g.p1.lat = lat_of(lat0, n_m); g.p1.lon = lon_of(lon0, lat0, e_c - GATE_HALF_WIDTH_M);
    g.p2.lat = lat_of(lat0, n_m); g.p2.lon = lon_of(lon0, lat0, e_c + GATE_HALF_WIDTH_M);
    return g;
}

/* A venue centred at (lat0, lon0), radius 2 km, one layout with an east-west S/F line across the
 * centre. dir_sign selects the accepted crossing direction. */
static void build_venue(trk_venue_t *v, double lat0, double lon0, int8_t dir_sign)
{
    memset(v, 0, sizeof *v);
    v->id        = TRK_USER_ID_BASE;
    snprintf(v->name, sizeof v->name, "TestVenue");
    v->lat       = lat0;
    v->lon       = lon0;
    v->radius_m  = VENUE_RADIUS_DEFAULT_M;
    v->n_layouts = 1;
    trk_layout_t *ly = &v->layouts[0];
    ly->id        = 1;
    snprintf(ly->name, sizeof ly->name, "Full");
    ly->dir_sign  = dir_sign;
    ly->n_sectors = 0;
    ly->sf        = ew_gate(lat0, lon0, 0.0, 0.0);
    ly->length_m  = 2500;
}

/* A venue with one forward layout and `n_sec` east-west sector gates at the given northings. */
static void build_venue_sec(trk_venue_t *v, double lat0, double lon0, uint8_t n_sec, const double *northings)
{
    build_venue(v, lat0, lon0, 1);
    trk_layout_t *ly = &v->layouts[0];
    ly->n_sectors = n_sec;
    for (uint8_t i = 0; i < n_sec; i++) ly->sectors[i] = ew_gate(lat0, lon0, 0.0, northings[i]);
}

/* Place the vehicle 40 m south of the S/F line and let the engine arm (VENUE_FOUND → ARMED). */
static void arm_at_start(lap_t *L, double lat0, double lon0, int64_t t, evlog_t *log)
{
    feed(L, lat0, lon0, 0.0, -40.0, t, SPD_MMS, true, log);
}

/* One northbound S/F crossing (dir_sign +1) plus a return loop east of the gate that never re-crosses
 * the line. Enters with the vehicle at (0, -40) at t0; the crossing fix is at t0 + 2 s (t_cross =
 * t0 + 1 s) and the vehicle is back at (0, -40) at t0 + lap_us. Requires lap_us >= 40 s. The far
 * corner at t0 + 30 s is > 50 m from the line and > MIN_LAP_S past the crossing, so the gate re-arms
 * (§6.4 step 5) in time for the next lap. */
static void forward_lap(lap_t *L, double lat0, double lon0, int64_t t0, int64_t lap_us,
                        evlog_t *log)
{
    feed(L, lat0, lon0,   0.0,  40.0, t0 + 2000000,  SPD_MMS, true, log);   /* CROSS north */
    feed(L, lat0, lon0, 300.0,  40.0, t0 + 30000000, SPD_MMS, true, log);   /* clear line + re-arm */
    feed(L, lat0, lon0, 300.0, -40.0, t0 + 35000000, SPD_MMS, true, log);
    feed(L, lat0, lon0,   0.0, -40.0, t0 + lap_us,    SPD_MMS, true, log);   /* back to start */
}

/* ------------------------------------------------------------------ tests */

/* §22.1: a fix 1.9 km from the centre finds the venue (< 2 km radius); 2.1 km does not. */
static void test_venue_found_within_radius_not_beyond(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));

    lap_t L; lap_init(&L, NULL);
    gps_fix_t f = fix_ll(lat_of(lat0, 1900.0), lon0, 1000000, 0, true);
    drive_fix(&L, &f, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));      /* found → armed on the same fix (§10.3) */

    lap_init(&L, NULL);
    gps_fix_t f2 = fix_ll(lat_of(lat0, 2100.0), lon0, 1000000, 0, true);
    drive_fix(&L, &f2, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
}

/* Finding a venue emits EV_VENUE_FOUND (arg16 = venue id) then EV_ARMED, and lands in ARMED. */
static void test_venue_found_then_armed_emits_events(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));

    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    gps_fix_t f = fix_ll(lat_of(lat0, -500.0), lon0, 1000000, 0, true);   /* 500 m south of centre */
    drive_fix(&L, &f, &log);

    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(2, log.n);
    TEST_ASSERT_EQUAL_UINT8(EV_VENUE_FOUND, log.type[0]);
    TEST_ASSERT_EQUAL_UINT16(TRK_USER_ID_BASE, log.arg16[0]);
    TEST_ASSERT_EQUAL_UINT8(EV_ARMED, log.type[1]);
}

/* §22.1/§10.3: beyond radius·VENUE_LEAVE_FACTOR (3 km) for VENUE_LEAVE_S (60 s) returns to NO_VENUE. */
static void test_leave_venue_after_factor_radius_for_timeout(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);

    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    double lat_s = lat_of(lat0, -300.0);                          /* 300 m south of the S/F line */

    gps_fix_t f0 = fix_ll(lat_s, lon_of(lon0, lat0, -500.0), 1000000, 20000, true);
    drive_fix(&L, &f0, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    double lon_far = lon_of(lon0, lat0, -4000.0);                 /* ~4.0 km from centre (> 3 km) */
    gps_fix_t f1 = fix_ll(lat_s, lon_far, 1000000 + 1000000, 20000, true);            /* t = 2 s: timer starts */
    drive_fix(&L, &f1, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    gps_fix_t f2 = fix_ll(lat_s, lon_far, 1000000 + 1000000 + 59000000, 0, true);      /* +59 s: not yet */
    drive_fix(&L, &f2, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    gps_fix_t f3 = fix_ll(lat_s, lon_far, 1000000 + 1000000 + 61000000, 0, true);      /* +61 s: leave */
    drive_fix(&L, &f3, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
}

/* §22.1/§6.4 step 3: on a forward (dir_sign +1) layout a northbound crossing is accepted and a
 * southbound one is rejected (the wrong way across the same line). */
static void test_forward_crossing_accepted_reverse_rejected(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);

    feed(&L, lat0, lon0, 0.0,  40.0, 0,       SPD_MMS, true, NULL);   /* arm, north of line */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0, -40.0, 2000000, SPD_MMS, true, NULL);   /* southbound: rejected */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0,  40.0, 4000000, SPD_MMS, true, NULL);   /* northbound: accepted */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));                 /* out-lap open (lap 0) */
}

/* Vice-versa on a reverse layout (dir_sign -1): southbound accepted, northbound rejected. */
static void test_reverse_layout_accepts_reverse_rejects_forward(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, -1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);

    feed(&L, lat0, lon0, 0.0, -40.0, 0,       SPD_MMS, true, NULL);   /* arm, south of line */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0,  40.0, 2000000, SPD_MMS, true, NULL);   /* northbound: rejected */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0, -40.0, 4000000, SPD_MMS, true, NULL);   /* southbound: accepted */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
}

/* §10.4 step 3: the out-lap (lap 0) completes with no time and the OUT_LAP flag, is not valid, and
 * does not become the best. */
static void test_out_lap_has_no_time(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, &log);   /* crossing #1: out-lap opens */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    forward_lap(&L, lat0, lon0, 40000000, 40000000, &log);   /* crossing #2: out-lap completes */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(0, p->lap_no);
    TEST_ASSERT_EQUAL_UINT32(0, p->time_ms);                        /* no time reported */
    TEST_ASSERT_TRUE(p->flags & LAP_F_OUT_LAP);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
    TEST_ASSERT_NULL(lap_best(&L));                                 /* no valid lap yet */
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAP_COMPLETE));
    TEST_ASSERT_EQUAL_UINT16(0, log.arg16[log.n - 1]);              /* EV_LAP_COMPLETE arg16 = lap 0 */
}

/* §6.4 step 5: after a crossing the S/F gate is debounced (MIN_LAP_S), so a jitter re-crossing a few
 * seconds later does not fire a second time; a genuine crossing after re-arm does. */
static void test_min_lap_debounce_prevents_double_fire(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);

    feed(&L, lat0, lon0, 0.0,  40.0, 2000000, SPD_MMS, true, &log);   /* crossing #1: out-lap opens */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    feed(&L, lat0, lon0, 0.0, -40.0, 4000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0, 0.0,  40.0, 6000000, SPD_MMS, true, &log);
    TEST_ASSERT_EQUAL_INT(0, count_type(&log, EV_LAP_COMPLETE));

    feed(&L, lat0, lon0, 300.0,  40.0, 30000000, SPD_MMS, true, &log);   /* re-arm */
    feed(&L, lat0, lon0, 300.0, -40.0, 35000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0, -40.0, 40000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 42000000, SPD_MMS, true, &log);   /* crossing #2 */
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAP_COMPLETE));
    TEST_ASSERT_EQUAL_UINT16(0, lap_prev(&L)->lap_no);                          /* the out-lap */
}

/* §10.4 step 3: a flying lap longer than MAX_LAP_S is flagged TOO_LONG and is not valid. */
static void test_max_lap_marks_too_long_invalid(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    forward_lap(&L, lat0, lon0, 0,                     40000000,   NULL);  /* #1 open out-lap */
    forward_lap(&L, lat0, lon0, 40000000,             1801000000, NULL);  /* #2 out-lap done; lap 1 = 1801 s */
    forward_lap(&L, lat0, lon0, 40000000 + 1801000000, 40000000,  NULL);  /* #3 lap 1 completes */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_TOO_LONG);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
    TEST_ASSERT_NULL(lap_best(&L));                                             /* the only flying lap was invalid */
}

/* §10.4 steps 6-7: prev updates on every completed lap; best tracks the fastest VALID lap only. */
static void test_best_and_prev_best_from_valid_only(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);

    int64_t t = 0;
    forward_lap(&L, lat0, lon0, t, 40000000, NULL); t += 40000000;   /* #1: out-lap opens */
    forward_lap(&L, lat0, lon0, t, 50000000, NULL); t += 50000000;   /* #2: out-lap done; lap 1 = 50 s */
    forward_lap(&L, lat0, lon0, t, 45000000, NULL); t += 45000000;   /* #3: lap 1 completes; lap 2 = 45 s */
    TEST_ASSERT_NOT_NULL(lap_best(&L));
    TEST_ASSERT_UINT32_WITHIN(2, 50000, lap_best(&L)->time_ms);            /* best = lap 1 so far */

    forward_lap(&L, lat0, lon0, t, 42000000, NULL); t += 42000000;   /* #4: lap 2 (45 s) completes */
    TEST_ASSERT_UINT32_WITHIN(2, 45000, lap_best(&L)->time_ms);            /* best updated to the faster valid lap */

    feed(&L, lat0, lon0, 0.0, -40.0, t + 1000000, 0, false, NULL);   /* invalid fix inside lap 3 */
    forward_lap(&L, lat0, lon0, t, 42000000, NULL); t += 42000000;   /* #5: lap 3 completes, invalid */

    TEST_ASSERT_UINT32_WITHIN(2, 45000, lap_best(&L)->time_ms);            /* best NOT taken by the invalid lap */
    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(3, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_GPS_LOST);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
    TEST_ASSERT_UINT32_WITHIN(2, 42000, p->time_ms);                       /* prev = lap 3, still timed */
}

/* §10.6: a continuous slow spell (< PIT_SPEED_KMH) longer than PIT_TIME_S sets LAP_F_PIT. */
static void test_pit_flag_on_slow_section(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    forward_lap(&L, lat0, lon0, 0,        40000000, NULL);   /* out-lap opens */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, NULL);   /* out-lap done; lap 1 begins (~41 s) */

    feed(&L, lat0, lon0, 300.0, 100.0, 80000000, 1000, true, NULL);   /* pit spell starts */
    feed(&L, lat0, lon0, 300.0, 100.0, 86000000, 1000, true, NULL);
    feed(&L, lat0, lon0, 300.0, 100.0, 92000000, 1000, true, NULL);   /* 12 s slow → LAP_F_PIT */
    feed(&L, lat0, lon0, 300.0, -40.0, 96000000,  SPD_MMS, true, NULL);
    feed(&L, lat0, lon0,   0.0, -40.0, 100000000, SPD_MMS, true, NULL);
    feed(&L, lat0, lon0,   0.0,  40.0, 102000000, SPD_MMS, true, NULL);   /* crossing completes lap 1 */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_PIT);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
}

/* §6.4 step 1: an invalid fix while a lap is running sets LAP_F_GPS_LOST; the lap still completes. */
static void test_gps_lost_flag_on_invalid_fix(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    forward_lap(&L, lat0, lon0, 0,        40000000, NULL);   /* out-lap opens */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, NULL);   /* out-lap done; lap 1 begins */
    feed(&L, lat0, lon0, 300.0, 100.0, 81000000, SPD_MMS, false, NULL);   /* invalid fix in lap 1 */
    forward_lap(&L, lat0, lon0, 80000000, 40000000, NULL);   /* lap 1 completes */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_GPS_LOST);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
}

/* ---------------------------------------------------- session 2.5 sector tests */

/* Drive one clean flying lap of a 2-sector venue (sectors at n = 100, 200): cross S/F, sector 1,
 * sector 2 in order, loop back east of the gates, cross S/F again. Legs are >= dt_us apart. Returns
 * with the vehicle at (0, -40); the caller crosses S/F next to complete the lap. */
static void clean_sector_leg(lap_t *L, double lat0, double lon0, int64_t t0, evlog_t *log)
{
    feed(L, lat0, lon0,   0.0,  40.0, t0 + 2000000,  SPD_MMS, true, log);   /* cross S/F north */
    feed(L, lat0, lon0,   0.0, 140.0, t0 + 5000000,  SPD_MMS, true, log);   /* cross sector 1 (n=100) */
    feed(L, lat0, lon0,   0.0, 240.0, t0 + 8000000,  SPD_MMS, true, log);   /* cross sector 2 (n=200) */
    feed(L, lat0, lon0, 400.0, 240.0, t0 + 30000000, SPD_MMS, true, log);   /* east, clear + re-arm */
    feed(L, lat0, lon0, 400.0, -40.0, t0 + 40000000, SPD_MMS, true, log);   /* south */
    feed(L, lat0, lon0,   0.0, -40.0, t0 + 48000000, SPD_MMS, true, log);   /* back to start */
}

/* §10.4 step 5 / §10.8: a clean two-sector lap reports n_sectors = 3 and its splits sum to the lap
 * time exactly (cumulative rounding). */
static void test_sector_splits_sum_to_lap_time(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);

    clean_sector_leg(&L, lat0, lon0, 0,        NULL);   /* out-lap opens, sectors crossed */
    clean_sector_leg(&L, lat0, lon0, 48000000, NULL);   /* out-lap completes; lap 1 runs */
    clean_sector_leg(&L, lat0, lon0, 96000000, NULL);   /* lap 1 completes */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_EQUAL_UINT8(3, p->n_sectors);                 /* 2 sector gates + 1 */
    TEST_ASSERT_TRUE(p->flags & LAP_F_VALID);
    uint32_t sum = p->sector_ms[0] + p->sector_ms[1] + p->sector_ms[2];
    TEST_ASSERT_EQUAL_UINT32(p->time_ms, sum);                /* splits sum exactly to lap time */
}

/* §10.4 step 3 / §10.5: a locked layout whose last sector gate is not crossed completes INCOMPLETE
 * and is not valid. */
static void test_incomplete_flag_when_sector_skipped(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    clean_sector_leg(&L, lat0, lon0, 0, NULL);          /* out-lap opens */

    /* lap 1: cross S/F, cross sector 1 only, detour past sector 2, cross S/F. */
    feed(&L, lat0, lon0,   0.0,  40.0, 50000000, SPD_MMS, true, NULL);   /* S/F -> lap 1 */
    feed(&L, lat0, lon0,   0.0, 150.0, 53000000, SPD_MMS, true, NULL);   /* sector 1 (n=100) */
    feed(&L, lat0, lon0, 400.0, 150.0, 56000000, SPD_MMS, true, NULL);   /* east, skips sector 2 */
    feed(&L, lat0, lon0, 400.0, -40.0, 80000000, SPD_MMS, true, NULL);   /* south, re-arm */
    feed(&L, lat0, lon0,   0.0, -40.0, 90000000, SPD_MMS, true, NULL);
    feed(&L, lat0, lon0,   0.0,  40.0, 92000000, SPD_MMS, true, NULL);   /* S/F completes lap 1 */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_INCOMPLETE);            /* sector 2 was never crossed */
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
}

/* §10.4 step 5: skipping sector 1 but crossing sector 2 still records sector 2 at its own split index
 * (the engine resyncs) and flags the lap INCOMPLETE. */
static void test_sector_resync_after_skip(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    clean_sector_leg(&L, lat0, lon0, 0, NULL);          /* out-lap opens */

    memset(&log, 0, sizeof log);
    /* lap 1: enter the e=0 corridor between the gates, so sector 1 is skipped but sector 2 is crossed. */
    feed(&L, lat0, lon0,   0.0,  40.0, 50000000, SPD_MMS, true, &log);   /* S/F -> lap 1 */
    feed(&L, lat0, lon0, 400.0,  40.0, 52000000, SPD_MMS, true, &log);   /* east (skip sector 1) */
    feed(&L, lat0, lon0, 400.0, 150.0, 55000000, SPD_MMS, true, &log);   /* north, still east */
    feed(&L, lat0, lon0,   0.0, 150.0, 58000000, SPD_MMS, true, &log);   /* west into the corridor */
    feed(&L, lat0, lon0,   0.0, 260.0, 61000000, SPD_MMS, true, &log);   /* north: crosses sector 2 */
    feed(&L, lat0, lon0, 400.0, 260.0, 80000000, SPD_MMS, true, &log);   /* east, re-arm */
    feed(&L, lat0, lon0, 400.0, -40.0, 90000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0, -40.0, 95000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 97000000, SPD_MMS, true, &log);   /* S/F completes lap 1 */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_INCOMPLETE);
    /* exactly one EV_SECTOR, for split index 1 (sector gate 2) — the engine resynced to it. */
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_SECTOR));
    int found = 0;
    for (int i = 0; i < log.n; i++) if (log.type[i] == EV_SECTOR) { TEST_ASSERT_EQUAL_UINT16(1, log.arg16[i]); found = 1; }
    TEST_ASSERT_TRUE(found);
}

/* §10.7: at a sector hit in a later lap the EV_SECTOR delta equals split - best_lap.sector_ms[idx]. */
static void test_sector_delta_vs_best(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);

    clean_sector_leg(&L, lat0, lon0, 0,        NULL);   /* out-lap opens */
    clean_sector_leg(&L, lat0, lon0, 48000000, NULL);   /* out-lap completes; lap 1 runs */
    /* lap 1 completes on the next S/F crossing and becomes best. */
    feed(&L, lat0, lon0, 0.0, 40.0, 98000000, SPD_MMS, true, NULL);      /* S/F: lap 1 completes */
    const lap_result_t *b = lap_best(&L);
    TEST_ASSERT_NOT_NULL(b);
    uint32_t best_s0 = b->sector_ms[0], best_s1 = b->sector_ms[1];

    /* lap 2: drive the sectors on a shifted timeline so the splits differ, capturing EV_SECTOR. */
    evlog_t log; memset(&log, 0, sizeof log);
    feed(&L, lat0, lon0,   0.0, 140.0, 102000000, SPD_MMS, true, &log); /* sector 1 (n=100) */
    feed(&L, lat0, lon0,   0.0, 240.0, 108000000, SPD_MMS, true, &log); /* sector 2 (n=200) */
    feed(&L, lat0, lon0, 400.0, 240.0, 130000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0, 400.0, -40.0, 140000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0, -40.0, 148000000, SPD_MMS, true, &log);

    TEST_ASSERT_EQUAL_INT(2, count_type(&log, EV_SECTOR));
    int seen = 0;
    for (int i = 0; i < log.n; i++) {
        if (log.type[i] != EV_SECTOR) continue;
        uint32_t idx = log.arg16[i];
        int32_t split = (int32_t)log.arg32[i];
        int32_t delta = (int32_t)log.arg32b[i];
        uint32_t best = (idx == 0) ? best_s0 : best_s1;
        TEST_ASSERT_EQUAL_INT32(split - (int32_t)best, delta);   /* §10.7 delta formula */
        seen++;
    }
    TEST_ASSERT_EQUAL_INT(2, seen);
}

/* §10.5 disambiguation: two layouts share an S/F and direction. Layout 1 (Full) has three sector
 * gates on the e=0 corridor; layout 2 (Short) has one gate far east (e=800). Driving through the Full
 * gates locks layout 1 (EV_LAYOUT_LOCKED arg16 = 1), not 2; driving through the Short gate locks 2. */
static void build_venue_2layout(trk_venue_t *v, double lat0, double lon0)
{
    memset(v, 0, sizeof *v);
    v->id = TRK_USER_ID_BASE;
    snprintf(v->name, sizeof v->name, "TwoLayout");
    v->lat = lat0; v->lon = lon0;
    v->radius_m = VENUE_RADIUS_DEFAULT_M;
    v->n_layouts = 2;

    trk_line_t sf = ew_gate(lat0, lon0, 0.0, 0.0);
    trk_layout_t *full = &v->layouts[0];
    full->id = 1; snprintf(full->name, sizeof full->name, "Full");
    full->dir_sign = 1; full->sf = sf; full->length_m = 2500;
    full->n_sectors = 3;
    full->sectors[0] = ew_gate(lat0, lon0, 0.0, 100.0);
    full->sectors[1] = ew_gate(lat0, lon0, 0.0, 200.0);
    full->sectors[2] = ew_gate(lat0, lon0, 0.0, 300.0);

    trk_layout_t *shrt = &v->layouts[1];
    shrt->id = 2; snprintf(shrt->name, sizeof shrt->name, "Short");
    shrt->dir_sign = 1; shrt->sf = sf; shrt->length_m = 2500;
    shrt->n_sectors = 1;
    shrt->sectors[0] = ew_gate(lat0, lon0, 800.0, 100.0);
}

static uint16_t last_locked_id(const evlog_t *log)
{
    uint16_t id = 0;
    for (int i = 0; i < log->n; i++) if (log->type[i] == EV_LAYOUT_LOCKED) id = log->arg16[i];
    return id;
}

static void test_disambiguation_locks_full_layout(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue_2layout(&v, lat0, lon0);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);

    feed(&L, lat0, lon0,   0.0,  40.0, 2000000,  SPD_MMS, true, &log);   /* S/F -> out-lap, union */
    feed(&L, lat0, lon0,   0.0, 360.0, 10000000, SPD_MMS, true, &log);   /* crosses Full gates 100/200/300 */
    feed(&L, lat0, lon0, 400.0, 360.0, 15000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0, 400.0, -40.0, 30000000, SPD_MMS, true, &log);   /* re-arm S/F (>50 m, >20 s) */
    feed(&L, lat0, lon0,   0.0, -40.0, 34000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 36000000, SPD_MMS, true, &log);   /* S/F -> disambiguate + lock */

    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAYOUT_LOCKED));
    TEST_ASSERT_EQUAL_UINT16(1, last_locked_id(&log));         /* Full wins */
}

static void test_disambiguation_locks_short_layout(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue_2layout(&v, lat0, lon0);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);

    feed(&L, lat0, lon0,   0.0,  40.0, 2000000,  SPD_MMS, true, &log);   /* S/F -> out-lap, union */
    feed(&L, lat0, lon0, 800.0,  40.0, 10000000, SPD_MMS, true, &log);   /* east (no Full gate) */
    feed(&L, lat0, lon0, 800.0, 160.0, 18000000, SPD_MMS, true, &log);   /* crosses Short gate (e=800,n=100) */
    feed(&L, lat0, lon0, 800.0, -40.0, 30000000, SPD_MMS, true, &log);   /* south, re-arm */
    feed(&L, lat0, lon0,   0.0, -40.0, 34000000, SPD_MMS, true, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 36000000, SPD_MMS, true, &log);   /* S/F -> disambiguate + lock */

    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAYOUT_LOCKED));
    TEST_ASSERT_EQUAL_UINT16(2, last_locked_id(&log));         /* Short wins */
}

/* ---------------------------------------------------- session 2.5 on-device creation (§10.9) */

/* A fix at ENU (e, n) about (lat0, lon0) with an explicit compass heading of motion. */
static void feed_hdg(lap_t *L, double lat0, double lon0, double e, double n, int64_t t,
                     double hdg_deg, evlog_t *log)
{
    gps_fix_t f = fix_ll(lat_of(lat0, n), lon_of(lon0, lat0, e), t, SPD_MMS, true);
    f.head_e5 = (int32_t)lround(hdg_deg * 1e5);
    drive_fix(L, &f, log);
}

/* §10.9: begin creation, mark S/F + two sector gates while moving north, drive a loop, and cross the
 * S/F to save. The result is a valid user venue with a forward layout and an auto reverse layout. */
static void test_create_track_saves_valid_venue(void)
{
    const double lat0 = -34.0, lon0 = 18.7;
    lap_t L; lap_init(&L, NULL);
    lap_create_begin(&L);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));

    /* S/F press at the origin, moving north (heading 0). */
    gps_fix_t sf = fix_ll(lat_of(lat0, 0.0), lon_of(lon0, lat0, 0.0), 1000000, SPD_MMS, true);
    sf.head_e5 = 0;
    trk_layout_t built;
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 0, &sf, &built));
    TEST_ASSERT_EQUAL_INT8(1, built.dir_sign);
    /* the S/F line is ~30 m wide */
    double w = geo_dist_m(built.sf.p1.lat, built.sf.p1.lon, built.sf.p2.lat, built.sf.p2.lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 2.0 * GATE_HALF_WIDTH_M, w);

    /* two sector presses further along (positions arbitrary; not crossed to finalise). */
    gps_fix_t s1 = fix_ll(lat_of(lat0, 100.0), lon_of(lon0, lat0, 0.0), 3000000, SPD_MMS, true); s1.head_e5 = 0;
    gps_fix_t s2 = fix_ll(lat_of(lat0, 200.0), lon_of(lon0, lat0, 0.0), 5000000, SPD_MMS, true); s2.head_e5 = 0;
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 1, &s1, NULL));
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 2, &s2, NULL));
    TEST_ASSERT_EQUAL_INT(-1, lap_mark_gate(&L, 9, &s2, NULL));   /* out-of-order rejected */

    /* drive a loop; distance integrates, and the S/F crossing after MIN_LAP_S finalises. */
    evlog_t log; memset(&log, 0, sizeof log);
    feed_hdg(&L, lat0, lon0,   0.0, 300.0,  8000000,  0.0,   NULL);
    feed_hdg(&L, lat0, lon0, 400.0, 300.0,  15000000, 90.0,  NULL);
    feed_hdg(&L, lat0, lon0, 400.0,-100.0,  25000000, 180.0, NULL);   /* > MIN_LAP_S now */
    feed_hdg(&L, lat0, lon0,   0.0,-100.0,  30000000, 270.0, NULL);
    feed_hdg(&L, lat0, lon0,   0.0,  50.0,  35000000, 0.0,   &log);  /* S/F crossing → save */

    TEST_ASSERT_EQUAL_UINT8(LAP_ST_VENUE_FOUND, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_VENUE_FOUND));

    const trk_venue_t *v = trk_get(TRK_USER_ID_BASE);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(v));               /* structurally valid */
    TEST_ASSERT_EQUAL_UINT8(0, v->flags);                          /* created on-device: not
                                                                     * TRK_F_UNVERIFIED (§10.1/§10.9) */
    TEST_ASSERT_EQUAL_UINT8(2, v->n_layouts);                      /* forward + reverse */
    TEST_ASSERT_EQUAL_UINT8(2, v->layouts[0].n_sectors);
    TEST_ASSERT_EQUAL_INT8(1, v->layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_INT8(-1, v->layouts[1].dir_sign);            /* auto reverse layout */
    TEST_ASSERT_EQUAL_UINT8(2, v->layouts[1].n_sectors);
    TEST_ASSERT_TRUE(v->layouts[0].length_m > 0);
}

/* §10.9 step 5: a long MODE press cancels creation and returns to NO_VENUE with nothing saved. */
static void test_create_cancel(void)
{
    const double lat0 = -34.0, lon0 = 18.7;
    lap_t L; lap_init(&L, NULL);
    lap_create_begin(&L);
    gps_fix_t sf = fix_ll(lat_of(lat0, 0.0), lon_of(lon0, lat0, 0.0), 1000000, SPD_MMS, true);
    sf.head_e5 = 0;
    TEST_ASSERT_EQUAL_INT(0, lap_mark_gate(&L, 0, &sf, NULL));
    lap_create_cancel(&L);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(-1, lap_mark_gate(&L, 0, &sf, NULL));     /* no longer in CREATE */
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                    /* nothing saved */
}

/* ---------------------------------------------------- session 2.5 RTC continuity (§10.10) */

/* §10.10: export mid-lap, restore into a fresh engine → LAP_RUNNING + LAP_F_INTERRUPTED with the
 * running time and best/prev preserved; the resumed lap completes carrying LAP_F_INTERRUPTED. */
static void test_rtc_restore_mid_lap_interrupted(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));                    /* import resolves the venue via trk_get */
    const trk_venue_t *vs = trk_get(TRK_USER_ID_BASE);

    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, vs);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    clean_sector_leg(&L, lat0, lon0, 0,        NULL);        /* out-lap opens */
    clean_sector_leg(&L, lat0, lon0, 48000000, NULL);        /* out-lap completes; lap 1 runs */
    feed(&L, lat0, lon0, 0.0, 40.0, 98000000, SPD_MMS, true, NULL);   /* lap 1 completes → best */
    TEST_ASSERT_NOT_NULL(lap_best(&L));
    uint32_t best_ms = lap_best(&L)->time_ms;

    feed(&L, lat0, lon0, 0.0, 140.0, 102000000, SPD_MMS, true, NULL); /* lap 2: cross sector 1 */
    lap_rtc_t rtc; lap_export_rtc(&L, &rtc);
    uint32_t elapsed_before = lap_current_elapsed_ms(&L, 105000000);
    TEST_ASSERT_TRUE(elapsed_before > 0);
    TEST_ASSERT_EQUAL_UINT16(2, rtc.lap_no);
    TEST_ASSERT_EQUAL_UINT16(1, rtc.layout_id);

    lap_t L2; lap_init(&L2, NULL);                                 /* simulate a reboot */
    TEST_ASSERT_EQUAL_INT(0, lap_import_rtc(&L2, &rtc));
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L2));
    TEST_ASSERT_EQUAL_UINT32(elapsed_before, lap_current_elapsed_ms(&L2, 105000000));   /* running time */
    TEST_ASSERT_NOT_NULL(lap_best(&L2));
    TEST_ASSERT_EQUAL_UINT32(best_ms, lap_best(&L2)->time_ms);     /* best preserved */
    TEST_ASSERT_EQUAL_UINT16(2, L2.lap_no);

    /* resume: establish a segment origin, cross sector 2, loop, cross S/F to complete the lap. */
    feed(&L2, lat0, lon0,   0.0, 150.0, 106000000, SPD_MMS, true, NULL);
    feed(&L2, lat0, lon0,   0.0, 260.0, 110000000, SPD_MMS, true, NULL);   /* sector 2 */
    feed(&L2, lat0, lon0, 400.0, 260.0, 130000000, SPD_MMS, true, NULL);
    feed(&L2, lat0, lon0, 400.0, -40.0, 140000000, SPD_MMS, true, NULL);
    feed(&L2, lat0, lon0,   0.0, -40.0, 148000000, SPD_MMS, true, NULL);
    feed(&L2, lat0, lon0,   0.0,  40.0, 150000000, SPD_MMS, true, NULL);   /* S/F completes lap 2 */
    const lap_result_t *p = lap_prev(&L2);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(2, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_INTERRUPTED);                /* §10.10 */
    TEST_ASSERT_TRUE(p->time_ms > 0);
}

/* import of an unknown venue id fails without disturbing the engine. */
static void test_rtc_import_unknown_venue_fails(void)
{
    lap_rtc_t rtc; memset(&rtc, 0, sizeof rtc);
    rtc.venue_id = 55555;                                          /* not in the store */
    lap_t L; lap_init(&L, NULL);
    TEST_ASSERT_EQUAL_INT(-1, lap_import_rtc(&L, &rtc));
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_NO_VENUE, lap_state(&L));
}

/* ---------------------------------------------------- session 2.5 predictive delta (§10.11) */

/* §10.11: with a recorded best lap, lap_live_delta_ms(dist) = elapsed - t_ref(dist). At a distance the
 * reference table holds, t_ref is exact, so the delta tracks the elapsed offset; outside the range no
 * delta is produced. */
static void test_predictive_delta(void)
{
    uint16_t *pbd = malloc(PRED_TABLE_MAX * sizeof *pbd);
    uint32_t *pbt = malloc(PRED_TABLE_MAX * sizeof *pbt);
    uint16_t *prd = malloc(PRED_TABLE_MAX * sizeof *prd);
    uint32_t *prt = malloc(PRED_TABLE_MAX * sizeof *prt);
    TEST_ASSERT_NOT_NULL(pbd); TEST_ASSERT_NOT_NULL(pbt); TEST_ASSERT_NOT_NULL(prd); TEST_ASSERT_NOT_NULL(prt);

    const double lat0 = -45.0, lon0 = 170.0;
    const double northings[2] = { 100.0, 200.0 };
    trk_venue_t v; build_venue_sec(&v, lat0, lon0, 2, northings);
    lap_t L; lap_init(&L, NULL);
    lap_set_predictive(&L, pbd, pbt, prd, prt, PRED_TABLE_MAX);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    clean_sector_leg(&L, lat0, lon0, 0,        NULL);        /* out-lap opens */
    clean_sector_leg(&L, lat0, lon0, 48000000, NULL);        /* out-lap completes; lap 1 runs */
    feed(&L, lat0, lon0, 0.0, 40.0, 98000000, SPD_MMS, true, NULL);   /* lap 1 → best + predictive ref */
    TEST_ASSERT_NOT_NULL(lap_best(&L));
    TEST_ASSERT_TRUE(L.pred_best_n >= 2);

    feed(&L, lat0, lon0, 0.0, 140.0, 102000000, SPD_MMS, true, NULL); /* lap 2 running */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));

    uint16_t dq   = L.pred_best_dist_m[1];       /* a distance the reference holds exactly */
    uint32_t tref = L.pred_best_t_ms[1];
    bool have;
    int64_t now = L.lap_start_gps_us + (int64_t)(tref + 3000) * 1000;   /* elapsed = t_ref + 3000 ms */
    int32_t d = lap_live_delta_ms(&L, now, (double)dq, &have);
    TEST_ASSERT_TRUE(have);
    TEST_ASSERT_INT32_WITHIN(2, 3000, d);                          /* delta = elapsed - t_ref = 3000 ms */

    lap_live_delta_ms(&L, now, 1.0e9, &have);                      /* beyond the table: no delta */
    TEST_ASSERT_FALSE(have);
    lap_live_delta_ms(&L, now, -10.0, &have);
    TEST_ASSERT_FALSE(have);

    free(pbd); free(pbt); free(prd); free(prt);
}

/* §10.11 table cap: a lap with far more than PRED_TABLE_MAX fixes stays within the fixed table. */
static void test_predictive_table_cap(void)
{
    uint16_t *pbd = malloc(PRED_TABLE_MAX * sizeof *pbd);
    uint32_t *pbt = malloc(PRED_TABLE_MAX * sizeof *pbt);
    uint16_t *prd = malloc(PRED_TABLE_MAX * sizeof *prd);
    uint32_t *prt = malloc(PRED_TABLE_MAX * sizeof *prt);
    TEST_ASSERT_NOT_NULL(pbd); TEST_ASSERT_NOT_NULL(pbt); TEST_ASSERT_NOT_NULL(prd); TEST_ASSERT_NOT_NULL(prt);

    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);                 /* no sector gates */
    lap_t L; lap_init(&L, NULL);
    lap_set_predictive(&L, pbd, pbt, prd, prt, PRED_TABLE_MAX);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    feed(&L, lat0, lon0, 0.0, 40.0, 2000000, SPD_MMS, true, NULL);    /* S/F → out-lap RUNNING */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));

    int64_t t = 2000000;
    for (int i = 0; i < 700; i++) {                                /* bounce north of the S/F, dist grows */
        t += 200000;
        double n = 100.0 + ((i & 1) ? 40.0 : 0.0);
        double e = ((i & 1) ? 30.0 : 0.0);
        feed(&L, lat0, lon0, e, n, t, SPD_MMS, true, NULL);
    }
    TEST_ASSERT_TRUE(L.pred_rec_fixes >= 700);
    TEST_ASSERT_TRUE(L.pred_rec_n > 0);
    TEST_ASSERT_TRUE(L.pred_rec_n <= PRED_TABLE_MAX);              /* never overflows the fixed table */

    free(pbd); free(pbt); free(prd); free(prt);
}

/* §10.11 with predictive left disabled (no lap_set_predictive call, the lap_init default: pred_cap ==
 * 0, all four buffer pointers NULL), lap_live_delta_ms must not dereference them — it returns the "no
 * delta" sentinel (0, *have == false) instead of crashing. This is the common case for every other test
 * in this file, which run a plain stack lap_t. */
static void test_predictive_disabled_returns_no_delta(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL);
    feed(&L, lat0, lon0, 0.0, 40.0, 2000000, SPD_MMS, true, NULL);    /* S/F → out-lap RUNNING */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));

    bool have = true;
    int32_t d = lap_live_delta_ms(&L, 3000000, 50.0, &have);
    TEST_ASSERT_FALSE(have);
    TEST_ASSERT_EQUAL_INT32(0, d);
}

#ifndef ESP_PLATFORM
/* Exit criterion (§22.2, roadmap): drive the engine off the synthetic circuit and confirm every
 * flying lap time matches the closed-form truth, and (2.5) that sectors are populated and sum to the
 * lap time. Needs the host-only replay library and a ~12 MB lap_t + synth_run_t, so this case is
 * host-only (ESP_PLATFORM excludes it; the rest of the file still runs on the target). */
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include <stdlib.h>

typedef struct {
    int      n;
    uint16_t lap_no[32];
    uint32_t time_ms[32];
    uint8_t  flags[32];
    uint8_t  n_sectors[32];
    uint32_t sec_ms[32][LAP_MAX_SECTORS + 1];
    const lap_t *L;
} laplog_t;
static void lap_cb(const event_t *ev, void *ctx)
{
    laplog_t *l = (laplog_t *)ctx;
    if (ev->type != EV_LAP_COMPLETE) return;
    if (l->n < 32) {
        l->lap_no[l->n]  = ev->arg16;
        l->time_ms[l->n] = ev->arg32;
        l->flags[l->n]   = ev->flags;
        const lap_result_t *p = lap_prev(l->L);        /* prev = the just-completed lap */
        uint8_t ns = p ? p->n_sectors : 0;
        l->n_sectors[l->n] = ns;
        for (uint8_t i = 0; i < ns && i <= LAP_MAX_SECTORS; i++) l->sec_ms[l->n][i] = p->sector_ms[i];
    }
    l->n++;
}

/* Runs the whole synth session at `rate` Hz, asserts the out-lap (lap 0) plus 10 valid flying laps,
 * each within tol_ms of synth_run_lap_time with three sector splits that sum to the lap time. Returns
 * the worst |error| in ms. */
static double drive_synth_and_check(int rate, double tol_ms)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = 11;   /* out-lap (lap 0) plus 10 flying laps */

    synth_run_t *run = (synth_run_t *)malloc(sizeof *run);
    trk_venue_t *v   = (trk_venue_t *)malloc(sizeof *v);
    lap_t       *L   = (lap_t *)malloc(sizeof *L);     /* ~12 MB region between run/lap: heap, not stack */
    TEST_ASSERT_NOT_NULL(run);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_NOT_NULL(L);
    char err[128];
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &cfg, err, sizeof err));
    synth_run_venue(run, v);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)(v->layouts[0].n_sectors));   /* synth default: 2 sector gates */

    synth_gps_cfg_t gcfg;
    synth_gps_cfg_defaults(&gcfg);
    gcfg.rate_hz = rate;
    gcfg.pos_sigma_m = 0.0;      /* measure the engine's crossing timing, not the receiver (§6.7) */
    synth_gps_t gps;
    synth_gps_init(&gps, &gcfg);

    lap_init(L, NULL);
    lap_set_venue(L, v);
    lap_force_layout(L, 1);      /* the synthetic venue's single layout */

    laplog_t log;
    memset(&log, 0, sizeof log);
    log.L = L;
    gps_fix_t fix;
    int rc;
    while ((rc = synth_gps_next(&gps, run, &fix, NULL)) >= 0)
        if (rc == 1) {
            event_t evs[LAP_EVT_MAX];
            int nev = 0;
            lap_on_fix(L, &fix, NULL, evs, LAP_EVT_MAX, &nev);
            for (int i = 0; i < nev; i++) lap_cb(&evs[i], &log);
        }

    TEST_ASSERT_EQUAL_INT(11, log.n);                  /* out-lap + 10 flying laps */
    TEST_ASSERT_EQUAL_UINT16(0, log.lap_no[0]);
    TEST_ASSERT_TRUE(log.flags[0] & LAP_F_OUT_LAP);

    double worst = 0.0;
    for (int k = 1; k <= 10; k++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)k, log.lap_no[k]);
        TEST_ASSERT_TRUE(log.flags[k] & LAP_F_VALID);
        TEST_ASSERT_EQUAL_UINT8(3, log.n_sectors[k]);          /* 2 sector gates + 1 */
        uint32_t sum = log.sec_ms[k][0] + log.sec_ms[k][1] + log.sec_ms[k][2];
        TEST_ASSERT_EQUAL_UINT32(log.time_ms[k], sum);         /* splits sum exactly to the lap time */
        double truth_ms = synth_run_lap_time(run, k + 1) * 1000.0;
        double e = fabs((double)log.time_ms[k] - truth_ms);
        if (e > worst) worst = e;
        TEST_ASSERT_TRUE(e <= tol_ms);
    }

    /* §10.8 theoretical best = Sigma of the best split at each index over the valid laps. */
    uint32_t expect_tb = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t bi = 0xFFFFFFFFu;
        for (int k = 1; k <= 10; k++) if (log.sec_ms[k][i] < bi) bi = log.sec_ms[k][i];
        expect_tb += bi;
    }
    TEST_ASSERT_EQUAL_UINT32(expect_tb, lap_theoretical_best_ms(L));
    TEST_ASSERT_TRUE(lap_theoretical_best_ms(L) <= lap_best(L)->time_ms);

    printf("[synth %d Hz] worst lap-time error = %.1f ms (tol %.0f), theoretical best = %u ms\n",
           rate, worst, tol_ms, lap_theoretical_best_ms(L));
    free(run);
    free(v);
    free(L);
    return worst;
}

static void test_ten_synth_laps_within_30ms_at_5hz(void)  { drive_synth_and_check(5, 30.0); }
static void test_ten_synth_laps_within_15ms_at_10hz(void) { drive_synth_and_check(10, 15.0); }

/* Realistic-noise exit criterion: the same 11-lap run at 5 Hz with default GPS noise. A lap time is
 * the difference of two S/F crossings ~114 s apart; at that gap the Gauss-Markov position error is
 * near-independent between crossings, a GPS-receiver limit per §6.7 (not an engine one). Bound 200 ms;
 * the RNG seed is fixed, so a re-run reproduces the printed worst value — never loosen the bound
 * silently. */
static void test_ten_synth_laps_realistic_noise_within_200ms_at_5hz(void)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = 11;

    synth_run_t *run = (synth_run_t *)malloc(sizeof *run);
    trk_venue_t *v   = (trk_venue_t *)malloc(sizeof *v);
    lap_t       *L   = (lap_t *)malloc(sizeof *L);
    TEST_ASSERT_NOT_NULL(run);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_NOT_NULL(L);
    char err[128];
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &cfg, err, sizeof err));
    synth_run_venue(run, v);

    synth_gps_cfg_t gcfg;
    synth_gps_cfg_defaults(&gcfg);
    gcfg.rate_hz = 5;
    synth_gps_t gps;
    synth_gps_init(&gps, &gcfg);

    lap_init(L, NULL);
    lap_set_venue(L, v);
    lap_force_layout(L, 1);

    laplog_t log;
    memset(&log, 0, sizeof log);
    log.L = L;
    gps_fix_t fix;
    int rc;
    while ((rc = synth_gps_next(&gps, run, &fix, NULL)) >= 0)
        if (rc == 1) {
            event_t evs[LAP_EVT_MAX];
            int nev = 0;
            lap_on_fix(L, &fix, NULL, evs, LAP_EVT_MAX, &nev);
            for (int i = 0; i < nev; i++) lap_cb(&evs[i], &log);
        }

    TEST_ASSERT_EQUAL_INT(11, log.n);
    TEST_ASSERT_EQUAL_UINT16(0, log.lap_no[0]);
    TEST_ASSERT_TRUE(log.flags[0] & LAP_F_OUT_LAP);

    const double tol_ms = 200.0;
    double worst = 0.0;
    for (int k = 1; k <= 10; k++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)k, log.lap_no[k]);
        TEST_ASSERT_TRUE(log.flags[k] & LAP_F_VALID);
        TEST_ASSERT_FALSE(log.flags[k] & (LAP_F_TOO_LONG | LAP_F_GPS_LOST | LAP_F_PIT | LAP_F_OUT_LAP));
        uint32_t sum = log.sec_ms[k][0] + log.sec_ms[k][1] + log.sec_ms[k][2];
        TEST_ASSERT_EQUAL_UINT32(log.time_ms[k], sum);
        double truth_ms = synth_run_lap_time(run, k + 1) * 1000.0;
        double e = fabs((double)log.time_ms[k] - truth_ms);
        if (e > worst) worst = e;
        TEST_ASSERT_TRUE(e <= tol_ms);
    }
    printf("[synth 5 Hz realistic noise] worst lap-time error = %.1f ms (tol %.0f)\n", worst, tol_ms);
    free(run);
    free(v);
    free(L);
}
#endif /* ESP_PLATFORM */

/* §10.5 union-gate upper bound (plan 2026-09-20, Task B2, verify-first): a legal venue may carry
 * TRK_MAX_LAYOUTS(8) candidate layouts that all share one S/F line yet each define LAP_MAX_SECTORS(8)
 * ENTIRELY DISTINCT sector gates. build_union() deduplicates only gates whose endpoints coincide, so
 * with no shared sector gates the disambiguation union saturates at 8 x 8 = 64 = LAP_MAX_UGATES. This
 * proves the ugate[] worst case is exactly its current size (no margin), i.e. ugate[] cannot be
 * shrunk -- reclaim 0. Nothing in the spec/data model forces cross-layout sector sharing, so this is
 * a reachable legal venue, not a synthetic one. */
static void build_venue_8x8_distinct(trk_venue_t *v, double lat0, double lon0)
{
    memset(v, 0, sizeof *v);
    v->id = TRK_USER_ID_BASE;
    snprintf(v->name, sizeof v->name, "Saturate");
    v->lat = lat0; v->lon = lon0;
    v->radius_m = VENUE_RADIUS_DEFAULT_M;
    v->n_layouts = TRK_MAX_LAYOUTS;
    trk_line_t sf = ew_gate(lat0, lon0, 0.0, 0.0);           /* one S/F shared by every layout */
    for (uint8_t k = 0; k < TRK_MAX_LAYOUTS; k++) {
        trk_layout_t *ly = &v->layouts[k];
        ly->id = (uint16_t)(k + 1);
        snprintf(ly->name, sizeof ly->name, "L%u", (unsigned)k);
        ly->dir_sign = 1; ly->sf = sf; ly->length_m = 2500;
        ly->n_sectors = LAP_MAX_SECTORS;
        for (uint8_t s = 0; s < LAP_MAX_SECTORS; s++) {
            double northing = 100.0 + (double)(k * LAP_MAX_SECTORS + s) * 10.0;   /* all 64 distinct */
            ly->sectors[s] = ew_gate(lat0, lon0, 0.0, northing);
        }
    }
}

static void test_ugate_union_saturates_at_lap_max_ugates(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue_8x8_distinct(&v, lat0, lon0);
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(&v));        /* it is a LEGAL venue */

    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);
    feed(&L, lat0, lon0, 0.0, 40.0, 2000000, SPD_MMS, true, &log);   /* S/F northbound -> out-lap + union */

    TEST_ASSERT_EQUAL_UINT8(TRK_MAX_LAYOUTS, L.n_cand);      /* all 8 layouts are candidates */
    TEST_ASSERT_EQUAL_UINT8(LAP_MAX_UGATES, L.n_ugate);      /* union saturates the array exactly */
    TEST_ASSERT_EQUAL_INT(64, LAP_MAX_UGATES);               /* == TRK_MAX_LAYOUTS * LAP_MAX_SECTORS */
}

/* H5 regression (§6.4): a crossing found on a chord that spans a GPS outage must not silently open a
 * clean VALID lap. Invalid fixes are skipped without advancing prev_*, so the first valid fix after a
 * long gap forms a long chord from the last pre-outage fix; if that chord crosses S/F, cross_time
 * interpolates a garbage t_cross and open_lap used to zero the flags -- so the FOLLOWING lap could
 * complete VALID with a start tens of seconds wrong (and poison best). The lap opened over such a gap
 * must carry LAP_F_GPS_LOST. Existing outage tests inject a single invalid fix, so the chord stays
 * ~2 s and interpolation is accurate; this long-gap case was uncovered. */
static void test_lap_across_gps_outage_not_clean_valid(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, &log);

    forward_lap(&L, lat0, lon0, 0,        40000000, &log);   /* out-lap opens (~1 s) */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, &log);   /* out-lap completes; lap 1 runs (~41 s) */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    TEST_ASSERT_EQUAL_UINT16(1, L.lap_no);                   /* lap 1 in progress, prev = (0,-40)@80 s */

    /* A ~40 s GPS outage: invalid fixes set GPS_LOST on lap 1 and leave prev_* unchanged. */
    feed(&L, lat0, lon0, 0.0, -40.0, 90000000,  0, false, &log);
    feed(&L, lat0, lon0, 0.0, -40.0, 100000000, 0, false, &log);
    feed(&L, lat0, lon0, 0.0, -40.0, 110000000, 0, false, &log);

    /* First valid fix after the outage is 40 s later and north of S/F: the (0,-40)@80 s -> (0,40)@120 s
     * chord crosses S/F, completing lap 1 (correctly GPS_LOST) and opening lap 2 at a garbage t_cross. */
    feed(&L, lat0, lon0, 0.0, 40.0, 120000000, SPD_MMS, true, &log);

    const lap_result_t *l1 = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(1, l1->lap_no);
    TEST_ASSERT_TRUE(l1->flags & LAP_F_GPS_LOST);            /* the completing lap is flagged (pre-existing) */
    TEST_ASSERT_EQUAL_UINT16(2, L.lap_no);                   /* lap 2 opened over the gap chord */
    TEST_ASSERT_TRUE(L.lap_flags & LAP_F_GPS_LOST);          /* FIX: the opened lap carries GPS_LOST */

    /* lap 2 completes on clean fixes; it must NOT be reported as a clean VALID lap. */
    feed(&L, lat0, lon0, 300.0,  40.0, 150000000, SPD_MMS, true, &log);   /* east, clear + re-arm */
    feed(&L, lat0, lon0, 300.0, -40.0, 155000000, SPD_MMS, true, &log);   /* south */
    feed(&L, lat0, lon0,   0.0, -40.0, 165000000, SPD_MMS, true, &log);   /* back to start */
    feed(&L, lat0, lon0,   0.0,  40.0, 167000000, SPD_MMS, true, &log);   /* cross S/F -> completes lap 2 */

    const lap_result_t *l2 = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(2, l2->lap_no);
    TEST_ASSERT_TRUE(l2->flags & LAP_F_GPS_LOST);            /* start was unreliable */
    TEST_ASSERT_FALSE(l2->flags & LAP_F_VALID);              /* so lap 2 is not a clean VALID lap */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_venue_found_within_radius_not_beyond);
    RUN_TEST(test_venue_found_then_armed_emits_events);
    RUN_TEST(test_leave_venue_after_factor_radius_for_timeout);
    RUN_TEST(test_forward_crossing_accepted_reverse_rejected);
    RUN_TEST(test_reverse_layout_accepts_reverse_rejects_forward);
    RUN_TEST(test_out_lap_has_no_time);
    RUN_TEST(test_min_lap_debounce_prevents_double_fire);
    RUN_TEST(test_max_lap_marks_too_long_invalid);
    RUN_TEST(test_best_and_prev_best_from_valid_only);
    RUN_TEST(test_pit_flag_on_slow_section);
    RUN_TEST(test_gps_lost_flag_on_invalid_fix);
    RUN_TEST(test_lap_across_gps_outage_not_clean_valid);
    RUN_TEST(test_sector_splits_sum_to_lap_time);
    RUN_TEST(test_incomplete_flag_when_sector_skipped);
    RUN_TEST(test_sector_resync_after_skip);
    RUN_TEST(test_sector_delta_vs_best);
    RUN_TEST(test_disambiguation_locks_full_layout);
    RUN_TEST(test_disambiguation_locks_short_layout);
    RUN_TEST(test_ugate_union_saturates_at_lap_max_ugates);
    RUN_TEST(test_create_track_saves_valid_venue);
    RUN_TEST(test_create_cancel);
    RUN_TEST(test_rtc_restore_mid_lap_interrupted);
    RUN_TEST(test_rtc_import_unknown_venue_fails);
    RUN_TEST(test_predictive_delta);
    RUN_TEST(test_predictive_table_cap);
    RUN_TEST(test_predictive_disabled_returns_no_delta);
#ifndef ESP_PLATFORM
    RUN_TEST(test_ten_synth_laps_within_30ms_at_5hz);
    RUN_TEST(test_ten_synth_laps_within_15ms_at_10hz);
    RUN_TEST(test_ten_synth_laps_realistic_noise_within_200ms_at_5hz);
#endif
    return UNITY_END();
}
