#include "unity.h"
#include "sim_profile.h"
#include "core/drag.h"
#include "core/event.h"
#include "core/types.h"
#include "hal/gps.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Standstill for the hold, then monotonic distance, 0-100 km/h inside 5 s of launch, the 1/4 mile
 * (402.3 m) crossed before braking starts, a full stop, then parked. */
static void test_drag_profile_shape(void)
{
    sim_drag_state_t s;
    sim_drag_at(0, &s);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, s.speed_mps);
    TEST_ASSERT_FALSE(s.parked);
    sim_drag_at(2999999, &s);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, s.dist_m);
    sim_drag_at(3000000 + 4700000, &s);                 /* 4.7 s after launch */
    TEST_ASSERT_TRUE(s.speed_mps * 3.6 >= 100.0);
    double last = -1.0; bool crossed_quarter_while_accel_or_cruise = false;
    for (int64_t t = 0; t <= 40000000; t += 200000) {
        sim_drag_at(t, &s);
        TEST_ASSERT_TRUE(s.dist_m >= last);           /* monotonic */
        TEST_ASSERT_TRUE(s.speed_mps >= 0.0);
        if (s.dist_m >= 402.336 && s.accel_mps2 >= 0.0) crossed_quarter_while_accel_or_cruise = true;
        last = s.dist_m;
    }
    TEST_ASSERT_TRUE(crossed_quarter_while_accel_or_cruise);
    sim_drag_at(40000000, &s);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, s.speed_mps);
    TEST_ASSERT_TRUE(s.parked);
    TEST_ASSERT_TRUE(s.dist_m > 600.0 && s.dist_m < 800.0);
}

/* The profile satisfies the drag engine end to end (ARMED -> LAUNCH -> every default gate up to
 * the 1/4 trap -> DONE) when fed the way the pipeline ACTUALLY feeds it (final review I1/F-2):
 * fused samples at FUSION_HZ with g_lon = |a|/9.81 -- the un-oriented fallback fus_step() emits in
 * production (fus.c:274, g_lon = sqrtf(a[0]^2+a[1]^2)), since CMD_CALIB_ORIENT has no handler
 * (issue #93) and orient_ok/forward_ok never go true -- and GPS fixes at 5 Hz marked valid only
 * when gps_us is strictly greater than the last valid one, the §6.5 monotonic rule
 * compute_validity() (pipeline.c) enforces. The original version fed the SIGNED a/9.81 (never
 * true on-device) and f.valid = 1 unconditionally (bypassing §6.5 entirely) -- exactly what let
 * the Critical C1 gps_us-rewind regression (gps_sim.c) go unnoticed by this suite. */
static drag_t D;
static int    s_hits[16];
static int    s_launch, s_done;
static void feed_events(const event_t *evs, int n)
{
    for (int i = 0; i < n; i++) {
        if (evs[i].type == EV_DRAG_LAUNCH) s_launch++;
        else if (evs[i].type == EV_DRAG_DONE) s_done++;
        else if (evs[i].type == EV_DRAG_GATE && evs[i].arg16 < 16) s_hits[evs[i].arg16]++;
    }
}
static void test_drag_profile_drives_the_engine(void)
{
    drag_init(&D, NULL);
    memset(s_hits, 0, sizeof s_hits); s_launch = 0; s_done = 0;
    const int64_t dt = 1000000 / FUSION_HZ;
    int64_t last_valid_gps_us = -1;   /* §6.5: "gps_us > the last valid gps_us", mirrored here */
    for (int64_t t = 0; t <= 30000000; t += dt) {
        sim_drag_state_t s; sim_drag_at(t, &s);
        fused_sample_t fs; memset(&fs, 0, sizeof fs);
        fs.gps_us = t; fs.mono_us = t;
        fs.g_lon  = fabsf((float)(s.accel_mps2 / 9.81));   /* I1/F-2: un-oriented |g_lon| fallback */
        fs.flags  = (s.speed_mps == 0.0 && s.accel_mps2 == 0.0) ? FUS_STILL : 0;
        event_t evs[DRAG_EVT_MAX]; int nev = 0;
        drag_on_fused(&D, &fs, evs, DRAG_EVT_MAX, &nev);
        feed_events(evs, nev);
        if (t % 200000 == 0) {                       /* 5 Hz GPS */
            gps_fix_t f; memset(&f, 0, sizeof f);
            f.gps_us = t; f.mono_us = t; f.gspeed_mms = (int32_t)(s.speed_mps * 1000.0);
            f.fix_type = 3; f.sats = 9; f.flags = GPS_FLAG_FIXOK;
            f.valid = (f.gps_us > last_valid_gps_us) ? 1 : 0;   /* I1/F-2: §6.5 monotonic rule */
            if (f.valid) last_valid_gps_us = f.gps_us;
            drag_on_fix(&D, &f);
        }
        if (s_done) break;
    }
    TEST_ASSERT_EQUAL_INT(1, s_launch);
    TEST_ASSERT_EQUAL_INT(1, s_done);
    const uint8_t want[] = { 1, 2, 3, 6, 7, 8, 9, 10 };   /* 0-60, 0-100, 0-200, 60ft, 330ft, 1/8, 1000ft, 1/4 */
    for (size_t i = 0; i < sizeof want; i++) TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_hits[want[i]], "gate id missed");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_drag_profile_shape);
    RUN_TEST(test_drag_profile_drives_the_engine);
    return UNITY_END();
}
