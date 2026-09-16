#include "unity.h"
#include "core/lap.h"
#include "core/trk.h"
#include "core/geo.h"
#include "core/consts.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Lap engine tests (spec §10, §22.1). Pure C11 so this file also builds and runs on the ESP32
 * (core_selftest). All geometry is expressed in ENU metres about the venue centre and converted to
 * the lat/lon a gps_fix_t carries; the engine converts back through the same tangent-plane origin, so
 * the round trip is exact to floating point. */

#define DEG_PER_M_LAT   ((180.0 / GEO_PI) / EARTH_R_M)     /* degrees of latitude per metre north */

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

/* A venue centred at (lat0, lon0), radius 2 km, one layout with an east-west S/F line across the
 * centre. dir_sign selects the accepted crossing direction. */
static void build_venue(trk_venue_t *v, double lat0, double lon0, int8_t dir_sign)
{
    memset(v, 0, sizeof *v);
    v->id       = TRK_USER_ID_BASE;
    snprintf(v->name, sizeof v->name, "TestVenue");
    v->lat      = lat0;
    v->lon      = lon0;
    v->radius_m = VENUE_RADIUS_DEFAULT_M;
    v->n_layouts = 1;
    trk_layout_t *ly = &v->layouts[0];
    ly->id       = 1;
    snprintf(ly->name, sizeof ly->name, "Full");
    ly->dir_sign = dir_sign;
    ly->n_sectors = 0;
    /* East-west S/F line spanning ±15 m about the centre (a 30 m gate). p1 = left, p2 = right in the
     * driving direction; a vehicle driving north with dir_sign +1 has west on its left, east on its
     * right, so p1 is the west end and p2 the east end. */
    ly->sf.p1.lat = lat0;  ly->sf.p1.lon = lon_of(lon0, lat0, -GATE_HALF_WIDTH_M);
    ly->sf.p2.lat = lat0;  ly->sf.p2.lon = lon_of(lon0, lat0, +GATE_HALF_WIDTH_M);
    ly->length_m = 2500;
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
    /* found → armed on the same fix (VENUE_FOUND advances to ARMED immediately, §10.3) */
    TEST_ASSERT_EQUAL_UINT8(LAP_ST_ARMED, lap_state(&L));

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

    gps_fix_t f0 = fix_ll(lat_s, lon_of(lon0, lat0, -500.0), 1000000, 20000, true);   /* 583 m out */
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_venue_found_within_radius_not_beyond);
    RUN_TEST(test_venue_found_then_armed_emits_events);
    RUN_TEST(test_leave_venue_after_factor_radius_for_timeout);
    return UNITY_END();
}
