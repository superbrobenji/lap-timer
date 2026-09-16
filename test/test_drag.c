#include "unity.h"
#include "core/drag.h"
#include "core/consts.h"
#include "core/types.h"
#include <math.h>
#include <string.h>

/* Drag engine tests (spec §11, §6.6, §22.1). Pure C11 so this file also builds and runs on the ESP32
 * (core_selftest): the fused and GPS streams are built analytically, with no synth/replay library and
 * no host-only headers. drag_t is multi-KB (its history ring), so the engine is a file-scope `static`,
 * never a big stack local — a stack drag_t would overflow the 24 KB self-test task stack. */

static drag_t D;                    /* shared, re-init per test — off the stack */

/* ---- event capture ---- */
typedef struct { int n; uint8_t type[128]; uint16_t arg16[128]; uint32_t arg32[128], arg32b[128]; } evlog_t;
static evlog_t EV;
static void ev_cb(const event_t *ev, void *ctx)
{
    (void)ctx;
    if (EV.n < 128) {
        EV.type[EV.n]   = ev->type;
        EV.arg16[EV.n]  = ev->arg16;
        EV.arg32[EV.n]  = ev->arg32;
        EV.arg32b[EV.n] = ev->arg32b;
    }
    EV.n++;
}
static int ev_count(uint8_t type)
{
    int c = 0;
    for (int i = 0; i < EV.n && i < 128; i++) if (EV.type[i] == type) c++;
    return c;
}

void setUp(void)    { memset(&EV, 0, sizeof EV); }
void tearDown(void) {}

/* ---- stream helpers ---- */
static fused_sample_t fused(int64_t gps_us, float g_lon, uint8_t flags)
{
    fused_sample_t fs;
    memset(&fs, 0, sizeof fs);
    fs.gps_us  = gps_us;
    fs.mono_us = gps_us;
    fs.g_lon   = g_lon;
    fs.flags   = flags;
    return fs;
}
static gps_fix_t gfix(int64_t gps_us, int32_t gspeed_mms, bool valid)
{
    gps_fix_t f;
    memset(&f, 0, sizeof f);
    f.gps_us     = gps_us;
    f.mono_us    = gps_us;
    f.gspeed_mms = gspeed_mms;
    f.fix_type   = 3;
    f.sats       = 9;
    f.flags      = GPS_FLAG_FIXOK;
    f.valid      = valid ? 1 : 0;
    return f;
}

#define DT_US        (1000000 / FUSION_HZ)    /* 10 000 us = one fused sample */

/* ------------------------------------------------------------ Task 1 tests */

/* §6.6: a constant a_lon gives v_est = a·t and dist = ½·a·t². The trapezoid is exact for a linear v,
 * so the only error is floating point. Runs in IDLE (never armed: g_lon > 0 keeps v_est above the arm
 * speed and no FUS_STILL is set), so the integrators run freely from the first sample. */
static void test_integration_constant_accel(void)
{
    drag_init(&D, NULL);
    const double a = 0.30 * G_MPS2;             /* g_lon = 0.30 → a ≈ 2.942 m/s² */
    for (int k = 0; k <= 300; k++) {            /* 3 s at 100 Hz */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.30f, 0);
        drag_on_fused(&D, &fs, NULL, NULL);
        if (k == 100 || k == 200 || k == 300) {
            double t = (double)k * 0.01;
            TEST_ASSERT_DOUBLE_WITHIN(1e-6, a * t, D.v_est);
            TEST_ASSERT_DOUBLE_WITHIN(1e-4, 0.5 * a * t * t, D.dist_m);
        }
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_IDLE, drag_state(&D));   /* never armed */
}

/* §6.6: every valid fix resets v_est to the Doppler gSpeed (no blend), the IMU only bridges between
 * fixes. A lagging fix that arrives after some integration re-anchors again. An invalid fix does not. */
static void test_gps_reanchor_resets_v_est(void)
{
    drag_init(&D, NULL);
    for (int k = 0; k <= 50; k++) { fused_sample_t fs = fused((int64_t)k * DT_US, 0.20f, 0); drag_on_fused(&D, &fs, NULL, NULL); }
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 0.20 * G_MPS2 * 0.5, D.v_est);

    gps_fix_t f1 = gfix(50 * DT_US, 5000, true);           /* Doppler says 5.000 m/s */
    drag_on_fix(&D, &f1);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, D.v_est);

    for (int k = 51; k <= 70; k++) { fused_sample_t fs = fused((int64_t)k * DT_US, 0.20f, 0); drag_on_fused(&D, &fs, NULL, NULL); }
    TEST_ASSERT_DOUBLE_WITHIN(2e-3, 5.0 + 0.20 * G_MPS2 * 0.2, D.v_est);

    double before = D.v_est;
    gps_fix_t bad = gfix(71 * DT_US, 1000, false);         /* invalid: must not re-anchor */
    drag_on_fix(&D, &bad);
    TEST_ASSERT_EQUAL_DOUBLE(before, D.v_est);

    gps_fix_t f2 = gfix(72 * DT_US, 3000, true);           /* lagging valid fix: re-anchors again */
    drag_on_fix(&D, &f2);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3.0, D.v_est);
}

/* §11.2: v_est < DRAG_ARM_SPEED_KMH and FUS_STILL held for DRAG_ARM_STILL_S enters ARMED and emits
 * EV_DRAG_ARMED once. Before the dwell elapses the engine is still IDLE. */
static void test_arm_after_still_dwell(void)
{
    drag_init(&D, NULL);
    for (int k = 0; k <= 199; k++) {            /* t = 0 .. 1.99 s: not yet armed */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drag_on_fused(&D, &fs, ev_cb, &EV);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_IDLE, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(0, ev_count(EV_DRAG_ARMED));

    fused_sample_t fs = fused((int64_t)200 * DT_US, 0.0f, FUS_STILL);   /* t = 2.00 s: arms */
    drag_on_fused(&D, &fs, ev_cb, &EV);
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_ARMED, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_ARMED));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, D.v_est);     /* arming zeroes v_est and dist (§11.2) */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, D.dist_m);
}

/* Motion (no FUS_STILL) never arms, and a broken still spell restarts the dwell. */
static void test_arm_requires_continuous_stillness(void)
{
    drag_init(&D, NULL);
    for (int k = 0; k <= 150; k++) {            /* 1.5 s still */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drag_on_fused(&D, &fs, ev_cb, &EV);
    }
    fused_sample_t bump = fused((int64_t)151 * DT_US, 0.0f, 0);   /* stillness lost: timer restarts */
    drag_on_fused(&D, &bump, ev_cb, &EV);
    for (int k = 152; k <= 300; k++) {          /* another 1.49 s still — still short of 2 s */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drag_on_fused(&D, &fs, ev_cb, &EV);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_IDLE, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(0, ev_count(EV_DRAG_ARMED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_integration_constant_accel);
    RUN_TEST(test_gps_reanchor_resets_v_est);
    RUN_TEST(test_arm_after_still_dwell);
    RUN_TEST(test_arm_requires_continuous_stillness);
    return UNITY_END();
}
