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
#define ARM_LAST_K   200                      /* still samples 0..200: arms at t = 2.00 s */
#define LAUNCH_K     201                      /* first launch sample; its time is the back-dated t0 */
#define T_LAUNCH_US  ((int64_t)LAUNCH_K * DT_US)   /* 2.01 s */

/* Drive still+slow fused samples until the engine arms (t = 2.00 s). */
static void arm_engine(drag_evt_cb_t cb, void *ctx)
{
    for (int k = 0; k <= ARM_LAST_K; k++) {
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drag_on_fused(&D, &fs, cb, ctx);
    }
}

/* From the armed state, feed a constant longitudinal g at 100 Hz with a 5 Hz Doppler GPS re-anchor
 * (gSpeed = a·(t − t0), the exact speed), until DONE or max_k. */
static void run_const_g(double g, int max_k, drag_evt_cb_t cb, void *ctx)
{
    double a = g * G_MPS2;
    for (int k = LAUNCH_K; k <= max_k; k++) {
        int64_t t = (int64_t)k * DT_US;
        fused_sample_t fs = fused(t, (float)g, 0);
        drag_on_fused(&D, &fs, cb, ctx);
        if (k % 20 == 0) {                          /* 5 Hz fixes */
            double tl = (double)(t - T_LAUNCH_US) / 1e6;
            if (tl < 0.0) tl = 0.0;
            gps_fix_t f = gfix(t, (int32_t)(a * tl * 1000.0), true);
            drag_on_fix(&D, &f);
        }
        if (drag_state(&D) == DRAG_ST_DONE) break;
    }
}

static const drag_gate_res_t *gate_by_id(const drag_result_t *r, uint8_t id)
{
    for (int i = 0; i < r->n_gates; i++) if (r->gates[i].gate_id == id) return &r->gates[i];
    return NULL;
}

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

/* ------------------------------------------------------------ Task 2 tests */

/* §22.1: a synthetic constant 0.5 g run (fused 100 Hz, Doppler 5 Hz) hits 0-100 at 5.66 s and the
 * 1/4 (402.34 m) at 12.81 s, both within ±20 ms; the interpolation actually lands them within ±5 ms.
 * Launch back-dates t0 to the first g-spike sample, the launch/gate/done events fire, and the run
 * finishes DONE with DRAG_F_QUARTER. */
static void test_run_0p5g_zero_to_hundred_and_quarter(void)
{
    drag_init(&D, NULL);
    arm_engine(ev_cb, &EV);
    run_const_g(0.5, 1700, ev_cb, &EV);

    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_DONE, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_LAUNCH));
    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_DONE));
    const drag_result_t *r = drag_current(&D);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT64(T_LAUNCH_US, D.t0_gps_us);      /* t0 = the first g-spike sample */
    TEST_ASSERT_TRUE(r->flags & DRAG_F_QUARTER);

    const drag_gate_res_t *g100 = gate_by_id(r, 2);
    const drag_gate_res_t *gq   = gate_by_id(r, 10);
    TEST_ASSERT_NOT_NULL(g100);  TEST_ASSERT_NOT_NULL(gq);
    TEST_ASSERT_TRUE(g100->hit); TEST_ASSERT_TRUE(gq->hit);
    /* 0-100 at 5.665 s, 1/4 at 12.810 s — both within ±5 ms of analytic (well inside §22.1's ±20 ms) */
    TEST_ASSERT_INT_WITHIN(5, 5665, (int)g100->time_ms);
    TEST_ASSERT_INT_WITHIN(5, 12810, (int)gq->time_ms);
    TEST_ASSERT_TRUE(ev_count(EV_DRAG_GATE) >= 2);
}

/* §22.1 trap. NOTE — a real spec conflict, ruled here: §6.6 defines the trap as the mean v_est over
 * the samples with dist ∈ [D − TRAP_DIST_M, D] (the 66 ft speed trap). For this run that mean is
 * 223.3 km/h. §22.1's "trap ≈ 226 km/h" is the *instantaneous* speed at the 1/4 line (a·t_quarter),
 * a different quantity. We implement §6.6 (the physically-correct trap) and additionally assert the
 * line speed so both numbers are pinned. */
static void test_run_0p5g_trap_and_line_speed(void)
{
    drag_init(&D, NULL);
    arm_engine(NULL, NULL);
    run_const_g(0.5, 1700, NULL, NULL);

    const drag_result_t *r = drag_current(&D);
    double trap_kmh = (double)r->trap_cms * 0.036;                 /* cm/s → km/h */
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 223.3, trap_kmh);               /* §6.6 trap-window mean */

    const drag_gate_res_t *gq = gate_by_id(r, 10);
    double line_kmh = (double)gq->speed_cms * 0.036;
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 226.1, line_kmh);               /* §22.1's "226" = the line speed */
}

/* §11.1: the SPEED_RANGE 100-200 gate records the interval between the 100 km/h and 200 km/h
 * crossings. For a constant 0.5 g run that interval equals the 0-100 time, 5.665 s. */
static void test_run_0p5g_speed_range_100_200(void)
{
    drag_init(&D, NULL);
    arm_engine(NULL, NULL);
    run_const_g(0.5, 1700, NULL, NULL);

    const drag_gate_res_t *g = gate_by_id(drag_current(&D), 5);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_TRUE(g->hit);
    TEST_ASSERT_INT_WITHIN(20, 5665, (int)g->time_ms);            /* (200−100) km/h at 0.5 g */
}

/* §6.6: linear interpolation gives ≤ ±5 ms timing resolution. Checked on the 60 ft distance gate
 * (18.29 m, crossing at 2.731 s) and the 0-100 speed gate: both land within 5 ms of analytic even
 * though the true crossing falls between two 10 ms samples. */
static void test_interpolation_resolution_5ms(void)
{
    drag_init(&D, NULL);
    arm_engine(NULL, NULL);
    run_const_g(0.5, 1700, NULL, NULL);

    const drag_result_t *r = drag_current(&D);
    double a = 0.5 * G_MPS2;
    int t60  = (int)(sqrt(2.0 * (1829.0 / 100.0) / a) * 1000.0 + 0.5);   /* 2731 ms */
    int t100 = (int)((100.0 / 3.6) / a * 1000.0 + 0.5);                   /* 5665 ms */
    TEST_ASSERT_INT_WITHIN(5, t60,  (int)gate_by_id(r, 6)->time_ms);
    TEST_ASSERT_INT_WITHIN(5, t100, (int)gate_by_id(r, 2)->time_ms);
}

/* §11.2 rollout: with rollout enabled, t0 moves to where dist reaches DRAG_ROLLOUT_M (1 ft) and dist
 * is re-zeroed there. From rest at 0.5 g that is 0.353 s after the launch instant, and DRAG_F_ROLLOUT
 * is set. (Default is rollout OFF, verified by the run above whose t0 stays at the launch instant.) */
static void test_rollout_shifts_t0(void)
{
    drag_cfg_t c; drag_cfg_defaults(&c);
    c.rollout = true;
    drag_init(&D, &c);
    arm_engine(NULL, NULL);
    run_const_g(0.5, 1700, NULL, NULL);

    const drag_result_t *r = drag_current(&D);
    TEST_ASSERT_TRUE(r->flags & DRAG_F_ROLLOUT);
    double a = 0.5 * G_MPS2;
    double expect_s = (double)T_LAUNCH_US / 1e6 + sqrt(2.0 * (double)DRAG_ROLLOUT_M / a);
    TEST_ASSERT_DOUBLE_WITHIN(0.02, expect_s, (double)D.t0_gps_us / 1e6);
    TEST_ASSERT_TRUE(r->flags & DRAG_F_QUARTER);                  /* the run still completes */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_integration_constant_accel);
    RUN_TEST(test_gps_reanchor_resets_v_est);
    RUN_TEST(test_arm_after_still_dwell);
    RUN_TEST(test_arm_requires_continuous_stillness);
    RUN_TEST(test_run_0p5g_zero_to_hundred_and_quarter);
    RUN_TEST(test_run_0p5g_trap_and_line_speed);
    RUN_TEST(test_run_0p5g_speed_range_100_200);
    RUN_TEST(test_interpolation_resolution_5ms);
    RUN_TEST(test_rollout_shifts_t0);
    return UNITY_END();
}
