#include "unity.h"
#include "core/lap.h"
#include "core/trk.h"
#include "core/geo.h"
#include "core/consts.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Lap engine tests (spec §10, §22.1). Pure C11 so this file also builds and runs on the ESP32
 * (core_selftest). Geometry is expressed in ENU metres about the venue centre and converted to the
 * lat/lon a gps_fix_t carries; the engine converts back through the same tangent-plane origin, so the
 * round trip is exact to floating point. The S/F line runs east-west across the centre (±15 m), so a
 * NORTHBOUND crossing has dir_sign +1 and a SOUTHBOUND crossing dir_sign -1 (§6.4). */

#define DEG_PER_M_LAT   ((180.0 / GEO_PI) / EARTH_R_M)     /* degrees of latitude per metre north */
#define SPD_MMS         40000   /* 40 m/s: matches the 80 m / 2 s crossing segment, so t_cross is exact */

void setUp(void) { trk_init(); }
void tearDown(void) {}

/* ---- event capture ---- */
typedef struct {
    int      n;
    uint8_t  type[32];
    uint8_t  flags[32];
    uint16_t arg16[32];
    uint32_t arg32[32];
} evlog_t;
static void ev_cb(const event_t *ev, void *ctx)
{
    evlog_t *e = (evlog_t *)ctx;
    if (e->n < 32) {
        e->type[e->n]  = ev->type;
        e->flags[e->n] = ev->flags;
        e->arg16[e->n] = ev->arg16;
        e->arg32[e->n] = ev->arg32;
    }
    e->n++;
}
static int count_type(const evlog_t *e, uint8_t type)
{
    int c = 0;
    for (int i = 0; i < e->n && i < 32; i++) if (e->type[i] == type) c++;
    return c;
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
                 int32_t spd, bool valid, lap_evt_cb_t cb, void *ctx)
{
    gps_fix_t f = fix_ll(lat_of(lat0, n), lon_of(lon0, lat0, e), t_us, spd, valid);
    lap_on_fix(L, &f, NULL, cb, ctx);
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
    /* East-west S/F line spanning ±15 m about the centre (a 30 m gate). p1 = left end, p2 = right end
     * in the driving direction; for a northbound (dir_sign +1) crossing west is left and east right,
     * so p1 is the west end and p2 the east end. */
    ly->sf.p1.lat = lat0;  ly->sf.p1.lon = lon_of(lon0, lat0, -GATE_HALF_WIDTH_M);
    ly->sf.p2.lat = lat0;  ly->sf.p2.lon = lon_of(lon0, lat0, +GATE_HALF_WIDTH_M);
    ly->length_m  = 2500;
}

/* Place the vehicle 40 m south of the S/F line and let the engine arm (VENUE_FOUND → ARMED). */
static void arm_at_start(lap_t *L, double lat0, double lon0, int64_t t, lap_evt_cb_t cb, void *ctx)
{
    feed(L, lat0, lon0, 0.0, -40.0, t, SPD_MMS, true, cb, ctx);
}

/* One northbound S/F crossing (dir_sign +1) plus a return loop east of the gate that never re-crosses
 * the line. Enters with the vehicle at (0, -40) at t0; the crossing fix is at t0 + 2 s (t_cross =
 * t0 + 1 s) and the vehicle is back at (0, -40) at t0 + lap_us. Requires lap_us >= 40 s. The far
 * corner at t0 + 30 s is > 50 m from the line and > MIN_LAP_S past the crossing, so the gate re-arms
 * (§6.4 step 5) in time for the next lap. */
static void forward_lap(lap_t *L, double lat0, double lon0, int64_t t0, int64_t lap_us,
                        lap_evt_cb_t cb, void *ctx)
{
    feed(L, lat0, lon0,   0.0,  40.0, t0 + 2000000,  SPD_MMS, true, cb, ctx);   /* CROSS north */
    feed(L, lat0, lon0, 300.0,  40.0, t0 + 30000000, SPD_MMS, true, cb, ctx);   /* clear line + re-arm */
    feed(L, lat0, lon0, 300.0, -40.0, t0 + 35000000, SPD_MMS, true, cb, ctx);
    feed(L, lat0, lon0,   0.0, -40.0, t0 + lap_us,    SPD_MMS, true, cb, ctx);   /* back to start */
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
    lap_on_fix(&L, &f, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));      /* found → armed on the same fix (§10.3) */

    lap_init(&L, NULL);
    gps_fix_t f2 = fix_ll(lat_of(lat0, 2100.0), lon0, 1000000, 0, true);
    lap_on_fix(&L, &f2, NULL, NULL, NULL);
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
    lap_on_fix(&L, &f, NULL, ev_cb, &log);

    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    TEST_ASSERT_EQUAL_INT(2, log.n);
    TEST_ASSERT_EQUAL_UINT8(EV_VENUE_FOUND, log.type[0]);
    TEST_ASSERT_EQUAL_UINT16(TRK_USER_ID_BASE, log.arg16[0]);
    TEST_ASSERT_EQUAL_UINT8(EV_ARMED, log.type[1]);
}

/* §22.1/§10.3: beyond radius·VENUE_LEAVE_FACTOR (3 km) for VENUE_LEAVE_S (60 s) returns to NO_VENUE.
 * The far fixes share one latitude 300 m south of the east-west S/F line and only move east-west, so
 * their path is parallel to the S/F line and never crosses it — the leave timer is what fires. */
static void test_leave_venue_after_factor_radius_for_timeout(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);

    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    double lat_s = lat_of(lat0, -300.0);                          /* 300 m south of the S/F line */

    gps_fix_t f0 = fix_ll(lat_s, lon_of(lon0, lat0, -500.0), 1000000, 20000, true);
    lap_on_fix(&L, &f0, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    double lon_far = lon_of(lon0, lat0, -4000.0);                 /* ~4.0 km from centre (> 3 km) */
    gps_fix_t f1 = fix_ll(lat_s, lon_far, 1000000 + 1000000, 20000, true);            /* t = 2 s: timer starts */
    lap_on_fix(&L, &f1, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    gps_fix_t f2 = fix_ll(lat_s, lon_far, 1000000 + 1000000 + 59000000, 0, true);      /* +59 s: not yet */
    lap_on_fix(&L, &f2, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

    gps_fix_t f3 = fix_ll(lat_s, lon_far, 1000000 + 1000000 + 61000000, 0, true);      /* +61 s: leave */
    lap_on_fix(&L, &f3, NULL, NULL, NULL);
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

    feed(&L, lat0, lon0, 0.0,  40.0, 0,       SPD_MMS, true, NULL, NULL);   /* arm, north of line */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0, -40.0, 2000000, SPD_MMS, true, NULL, NULL);   /* southbound: rejected */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0,  40.0, 4000000, SPD_MMS, true, NULL, NULL);   /* northbound: accepted */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));                 /* out-lap open (lap 0) */
}

/* Vice-versa on a reverse layout (dir_sign -1): southbound accepted, northbound rejected. */
static void test_reverse_layout_accepts_reverse_rejects_forward(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, -1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);

    feed(&L, lat0, lon0, 0.0, -40.0, 0,       SPD_MMS, true, NULL, NULL);   /* arm, south of line */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0,  40.0, 2000000, SPD_MMS, true, NULL, NULL);   /* northbound: rejected */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));
    feed(&L, lat0, lon0, 0.0, -40.0, 4000000, SPD_MMS, true, NULL, NULL);   /* southbound: accepted */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
}

/* §10.4 step 3: the out-lap (lap 0) completes with no time and the OUT_LAP flag, is not valid, and
 * does not become the best. Two forward crossings: the first opens the out-lap, the second ends it. */
static void test_out_lap_has_no_time(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    evlog_t log; memset(&log, 0, sizeof log);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, ev_cb, &log);
    forward_lap(&L, lat0, lon0, 0,        40000000, ev_cb, &log);   /* crossing #1: out-lap opens */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    forward_lap(&L, lat0, lon0, 40000000, 40000000, ev_cb, &log);   /* crossing #2: out-lap completes */

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
    arm_at_start(&L, lat0, lon0, 0, ev_cb, &log);

    feed(&L, lat0, lon0, 0.0,  40.0, 2000000, SPD_MMS, true, ev_cb, &log);   /* crossing #1: out-lap opens */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_RUNNING, lap_state(&L));
    /* jitter across the line 2 s and 4 s later, still inside the S/F debounce window: no completion */
    feed(&L, lat0, lon0, 0.0, -40.0, 4000000, SPD_MMS, true, ev_cb, &log);
    feed(&L, lat0, lon0, 0.0,  40.0, 6000000, SPD_MMS, true, ev_cb, &log);
    TEST_ASSERT_EQUAL_INT(0, count_type(&log, EV_LAP_COMPLETE));

    /* clear the line and let > MIN_LAP_S pass, then cross for real: the out-lap now completes */
    feed(&L, lat0, lon0, 300.0,  40.0, 30000000, SPD_MMS, true, ev_cb, &log);   /* re-arm */
    feed(&L, lat0, lon0, 300.0, -40.0, 35000000, SPD_MMS, true, ev_cb, &log);
    feed(&L, lat0, lon0,   0.0, -40.0, 40000000, SPD_MMS, true, ev_cb, &log);
    feed(&L, lat0, lon0,   0.0,  40.0, 42000000, SPD_MMS, true, ev_cb, &log);   /* crossing #2 */
    TEST_ASSERT_EQUAL_INT(1, count_type(&log, EV_LAP_COMPLETE));
    TEST_ASSERT_EQUAL_UINT16(0, lap_prev(&L)->lap_no);                          /* the out-lap */
}

/* §10.4 step 3: a flying lap longer than MAX_LAP_S is flagged TOO_LONG and is not valid. lap 1 here
 * spans 1801 s (the duration of the second forward lap). */
static void test_max_lap_marks_too_long_invalid(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL, NULL);
    forward_lap(&L, lat0, lon0, 0,                     40000000,   NULL, NULL);  /* #1 open out-lap */
    forward_lap(&L, lat0, lon0, 40000000,             1801000000, NULL, NULL);  /* #2 out-lap done; lap 1 = 1801 s */
    forward_lap(&L, lat0, lon0, 40000000 + 1801000000, 40000000,  NULL, NULL);  /* #3 lap 1 completes */

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
    arm_at_start(&L, lat0, lon0, 0, NULL, NULL);

    int64_t t = 0;
    forward_lap(&L, lat0, lon0, t, 40000000, NULL, NULL); t += 40000000;   /* #1: out-lap opens */
    forward_lap(&L, lat0, lon0, t, 50000000, NULL, NULL); t += 50000000;   /* #2: out-lap done; lap 1 = 50 s */
    forward_lap(&L, lat0, lon0, t, 45000000, NULL, NULL); t += 45000000;   /* #3: lap 1 completes; lap 2 = 45 s */
    TEST_ASSERT_NOT_NULL(lap_best(&L));
    TEST_ASSERT_UINT32_WITHIN(2, 50000, lap_best(&L)->time_ms);            /* best = lap 1 so far */

    forward_lap(&L, lat0, lon0, t, 42000000, NULL, NULL); t += 42000000;   /* #4: lap 2 (45 s) completes */
    TEST_ASSERT_UINT32_WITHIN(2, 45000, lap_best(&L)->time_ms);            /* best updated to the faster valid lap */

    /* lap 3 (42 s) will be the fastest but an invalid fix mid-lap marks it GPS_LOST, so it must not
     * become the best. */
    feed(&L, lat0, lon0, 0.0, -40.0, t + 1000000, 0, false, NULL, NULL);   /* invalid fix inside lap 3 */
    forward_lap(&L, lat0, lon0, t, 42000000, NULL, NULL); t += 42000000;   /* #5: lap 3 completes, invalid */

    TEST_ASSERT_UINT32_WITHIN(2, 45000, lap_best(&L)->time_ms);            /* best NOT taken by the invalid lap */
    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_EQUAL_UINT16(3, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_GPS_LOST);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
    TEST_ASSERT_UINT32_WITHIN(2, 42000, p->time_ms);                       /* prev = lap 3, still timed */
}

/* §10.6: a continuous slow spell (< PIT_SPEED_KMH) longer than PIT_TIME_S sets LAP_F_PIT on the lap
 * in progress, which then completes but is not valid. */
static void test_pit_flag_on_slow_section(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL, NULL);
    forward_lap(&L, lat0, lon0, 0,        40000000, NULL, NULL);   /* out-lap opens */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, NULL, NULL);   /* out-lap done; lap 1 begins (~41 s) */

    /* Slow crawl (1 m/s ≈ 3.6 km/h) held for 12 s well east of the S/F line. The 62 s sample is both
     * > 50 m from the line and > MIN_LAP_S past the last crossing, so the gate re-arms here too. */
    feed(&L, lat0, lon0, 300.0, 100.0, 80000000, 1000, true, NULL, NULL);   /* pit spell starts */
    feed(&L, lat0, lon0, 300.0, 100.0, 86000000, 1000, true, NULL, NULL);
    feed(&L, lat0, lon0, 300.0, 100.0, 92000000, 1000, true, NULL, NULL);   /* 12 s slow → LAP_F_PIT */
    /* return south and cross for real to complete lap 1 */
    feed(&L, lat0, lon0, 300.0, -40.0, 96000000,  SPD_MMS, true, NULL, NULL);
    feed(&L, lat0, lon0,   0.0, -40.0, 100000000, SPD_MMS, true, NULL, NULL);
    feed(&L, lat0, lon0,   0.0,  40.0, 102000000, SPD_MMS, true, NULL, NULL);   /* crossing completes lap 1 */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_PIT);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
}

/* §6.4 step 1: an invalid fix while a lap is running sets LAP_F_GPS_LOST; the lap still completes at
 * the next S/F crossing but is not valid. */
static void test_gps_lost_flag_on_invalid_fix(void)
{
    const double lat0 = -45.0, lon0 = 170.0;
    trk_venue_t v; build_venue(&v, lat0, lon0, 1);
    lap_t L; lap_init(&L, NULL);
    lap_set_venue(&L, &v);
    arm_at_start(&L, lat0, lon0, 0, NULL, NULL);
    forward_lap(&L, lat0, lon0, 0,        40000000, NULL, NULL);   /* out-lap opens */
    forward_lap(&L, lat0, lon0, 40000000, 40000000, NULL, NULL);   /* out-lap done; lap 1 begins */
    feed(&L, lat0, lon0, 300.0, 100.0, 81000000, SPD_MMS, false, NULL, NULL);   /* invalid fix in lap 1 */
    forward_lap(&L, lat0, lon0, 80000000, 40000000, NULL, NULL);   /* lap 1 completes */

    const lap_result_t *p = lap_prev(&L);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(1, p->lap_no);
    TEST_ASSERT_TRUE(p->flags & LAP_F_GPS_LOST);
    TEST_ASSERT_FALSE(p->flags & LAP_F_VALID);
}

#ifndef ESP_PLATFORM
/* Exit criterion (§22.2, roadmap): drive the engine off the synthetic circuit and confirm every
 * flying lap time matches the closed-form truth. Needs the host-only replay library and a ~2.7 MB
 * synth_run_t, so this case is host-only (ESP_PLATFORM excludes it; the rest of the file still runs
 * on the target). */
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include <stdlib.h>

typedef struct { int n; uint16_t lap_no[32]; uint32_t time_ms[32]; uint8_t flags[32]; } laplog_t;
static void lap_cb(const event_t *ev, void *ctx)
{
    laplog_t *l = (laplog_t *)ctx;
    if (ev->type != EV_LAP_COMPLETE) return;
    if (l->n < 32) {
        l->lap_no[l->n]  = ev->arg16;
        l->time_ms[l->n] = ev->arg32;
        l->flags[l->n]   = ev->flags;
    }
    l->n++;
}

/* Runs the whole synth session at `rate` Hz, asserts the out-lap (lap 0) plus 10 valid flying laps
 * and that each flying lap is within tol_ms of synth_run_lap_time. Returns the worst |error| in ms. */
static double drive_synth_and_check(int rate, double tol_ms)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = 11;   /* the engine's first S/F crossing opens the out-lap (lap 0, §10.3); 12 S/F
                      * passages (cfg.laps + 1) therefore give the out-lap plus 10 flying laps. */

    synth_run_t *run = (synth_run_t *)malloc(sizeof *run);   /* ~2.7 MB: heap, never the stack */
    trk_venue_t *v   = (trk_venue_t *)malloc(sizeof *v);
    TEST_ASSERT_NOT_NULL(run);
    TEST_ASSERT_NOT_NULL(v);
    char err[128];
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &cfg, err, sizeof err));
    synth_run_venue(run, v);

    synth_gps_cfg_t gcfg;
    synth_gps_cfg_defaults(&gcfg);
    gcfg.rate_hz = rate;
    /* Zero the GPS position noise so this exit criterion measures the ENGINE's crossing timing, not
     * the receiver. A lap time is the difference of two S/F crossings ~114 s apart; the default
     * 1.5 m Gauss-Markov position noise (τ = 20 s) is independent across that gap, so it alone puts
     * absolute lap-vs-truth error near 100 ms — a receiver limit characterised in §6.7, not an engine
     * one. Speed, heading, latency and jitter stay at their defaults; crossing gps_us is interpolated
     * from the exact per-fix gps_us (§6.4 step 4), so latency/jitter (which touch only mono_us) do not
     * affect lap timing. What remains is the fractional-crossing interpolation of geo_segment_cross —
     * and it lands every lap within a fraction of a millisecond, far inside the ±30/±15 ms bound. */
    gcfg.pos_sigma_m = 0.0;
    synth_gps_t gps;
    synth_gps_init(&gps, &gcfg);

    lap_t L;
    lap_init(&L, NULL);
    lap_set_venue(&L, v);
    lap_force_layout(&L, 1);        /* the synthetic venue's single layout */

    laplog_t log;
    memset(&log, 0, sizeof log);
    gps_fix_t fix;
    int rc;
    while ((rc = synth_gps_next(&gps, run, &fix, NULL)) >= 0)
        if (rc == 1) lap_on_fix(&L, &fix, NULL, lap_cb, &log);

    TEST_ASSERT_EQUAL_INT(11, log.n);                        /* out-lap + 10 flying laps */
    TEST_ASSERT_EQUAL_UINT16(0, log.lap_no[0]);
    TEST_ASSERT_TRUE(log.flags[0] & LAP_F_OUT_LAP);

    double worst = 0.0;
    for (int k = 1; k <= 10; k++) {
        TEST_ASSERT_EQUAL_UINT16((uint16_t)k, log.lap_no[k]);
        TEST_ASSERT_TRUE(log.flags[k] & LAP_F_VALID);
        /* engine lap k spans S/F passages k → k+1, which is synth_run_lap_time(k + 1) (§10.3). */
        double truth_ms = synth_run_lap_time(run, k + 1) * 1000.0;
        double e = fabs((double)log.time_ms[k] - truth_ms);
        if (e > worst) worst = e;
        TEST_ASSERT_TRUE(e <= tol_ms);
    }
    printf("[synth %d Hz] worst lap-time error = %.1f ms (tol %.0f)\n", rate, worst, tol_ms);
    free(run);
    free(v);
    return worst;
}

/* 5 Hz: the roadmap/§22.2 bound is 30 ms. */
static void test_ten_synth_laps_within_30ms_at_5hz(void)
{
    drive_synth_and_check(5, 30.0);
}
/* 10 Hz: 15 ms. */
static void test_ten_synth_laps_within_15ms_at_10hz(void)
{
    drive_synth_and_check(10, 15.0);
}
#endif /* ESP_PLATFORM */

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
#ifndef ESP_PLATFORM
    RUN_TEST(test_ten_synth_laps_within_30ms_at_5hz);
    RUN_TEST(test_ten_synth_laps_within_15ms_at_10hz);
#endif
    return UNITY_END();
}
