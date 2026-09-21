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
static int ev_first(uint8_t type)
{
    for (int i = 0; i < EV.n && i < 128; i++) if (EV.type[i] == type) return i;
    return -1;
}
static int ev_gate_index(uint16_t gate_id)
{
    for (int i = 0; i < EV.n && i < 128; i++) if (EV.type[i] == EV_DRAG_GATE && EV.arg16[i] == gate_id) return i;
    return -1;
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

/* Feed one fused sample and, when collect is set, drain the caller-returned events into EV. */
static void drive_fused(const fused_sample_t *fs, bool collect)
{
    event_t evs[DRAG_EVT_MAX];
    int nev = 0;
    drag_on_fused(&D, fs, evs, DRAG_EVT_MAX, &nev);
    if (collect) for (int i = 0; i < nev; i++) ev_cb(&evs[i], NULL);
}

#define DT_US        (1000000 / FUSION_HZ)    /* 10 000 us = one fused sample */
#define ARM_LAST_K   200                      /* still samples 0..200: arms at t = 2.00 s */
#define LAUNCH_K     201                      /* first launch sample; its time is the back-dated t0 */
#define T_LAUNCH_US  ((int64_t)LAUNCH_K * DT_US)   /* 2.01 s */

/* Drive still+slow fused samples until the engine arms (t = 2.00 s). */
static void arm_engine(bool collect)
{
    for (int k = 0; k <= ARM_LAST_K; k++) {
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drive_fused(&fs, collect);
    }
}

/* From the armed state, feed a constant longitudinal g at 100 Hz with a 5 Hz Doppler GPS re-anchor
 * (gSpeed = a·(t − t0), the exact speed), until DONE or max_k. */
static void run_const_g(double g, int max_k, bool collect)
{
    double a = g * G_MPS2;
    for (int k = LAUNCH_K; k <= max_k; k++) {
        int64_t t = (int64_t)k * DT_US;
        fused_sample_t fs = fused(t, (float)g, 0);
        drive_fused(&fs, collect);
        if (k % 20 == 0) {                          /* 5 Hz fixes */
            double tl = (double)(t - T_LAUNCH_US) / 1e6;
            if (tl < 0.0) tl = 0.0;
            gps_fix_t f = gfix(t, (int32_t)(a * tl * 1000.0), true);
            drag_on_fix(&D, &f);
        }
        if (drag_state(&D) == DRAG_ST_DONE) break;
    }
}

/* Accelerate at 0.5 g to peak_kmh, then brake at −1 g to a stop (5 Hz Doppler follows the profile).
 * Used for the braking-distance and peak-180 bench cases. */
static void run_accel_then_brake(double peak_kmh, bool collect)
{
    double a = 0.5 * G_MPS2, ab = 1.0 * G_MPS2, vpk = peak_kmh / 3.6;
    int nacc = (int)(vpk / a / 0.01 + 0.5);
    int64_t tpk = T_LAUNCH_US + (int64_t)nacc * DT_US;
    for (int k = LAUNCH_K; k <= LAUNCH_K + nacc + 1200; k++) {
        int64_t t = (int64_t)k * DT_US;
        double gl, v;
        if (k < LAUNCH_K + nacc) { gl = 0.5;  v = a * ((double)(t - T_LAUNCH_US) / 1e6); }
        else                     { gl = -1.0; double tb = (double)(t - tpk) / 1e6; v = vpk - ab * tb; if (v < 0) v = 0; }
        fused_sample_t fs = fused(t, (float)gl, 0);
        drive_fused(&fs, collect);
        if (k % 20 == 0) { gps_fix_t f = gfix(t, (int32_t)(v * 1000.0), true); drag_on_fix(&D, &f); }
        if (D.brake_done) break;
        if (drag_state(&D) == DRAG_ST_IDLE) break;
    }
}

/* Accelerate at 0.5 g until the 1/4 gate ends the run (DONE), then brake at −1 g to a stop — so the
 * braking gate completes *after* DONE (§11.2 DONE-then-brake). */
static void run_quarter_then_brake(bool collect)
{
    double a = 0.5 * G_MPS2, ab = 1.0 * G_MPS2, vpk = 0.0;
    int64_t tpk = 0;
    for (int k = LAUNCH_K; k <= 3000; k++) {
        int64_t t = (int64_t)k * DT_US;
        double gl, v;
        if (drag_state(&D) != DRAG_ST_DONE) { gl = 0.5;  v = a * ((double)(t - T_LAUNCH_US) / 1e6); vpk = v; tpk = t; }
        else                                { gl = -1.0; double tb = (double)(t - tpk) / 1e6; v = vpk - ab * tb; if (v < 0) v = 0; }
        fused_sample_t fs = fused(t, (float)gl, 0);
        drive_fused(&fs, collect);
        if (k % 20 == 0) { gps_fix_t f = gfix(t, (int32_t)(v * 1000.0), true); drag_on_fix(&D, &f); }
        if (D.brake_done) break;
    }
}

static const drag_gate_res_t *gate_by_id(const drag_result_t *r, uint8_t id)
{
    for (int i = 0; i < r->n_gates; i++) if (r->gates[i].gate_id == id) return &r->gates[i];
    return NULL;
}

/* §11.4 screen benches (test-side, since the engine records only per-gate hits): the SPEED_FROM0 gates
 * whose speed is in cfg.benches_kmh and were hit, ascending, then the 1/4 row (0 sentinel). Max `max`
 * rows; if more than max−1 benches were hit the lowest are dropped first. */
static int bench_rows(const drag_cfg_t *c, const drag_result_t *r, uint16_t *out, int max)
{
    uint16_t hit[8]; int nh = 0;
    for (int b = 0; b < c->n_benches; b++) {
        uint16_t bs = c->benches_kmh[b];
        for (int i = 0; i < c->n_gates; i++)
            if (c->gates[i].kind == DRAG_SPEED_FROM0 && c->gates[i].a == bs && r->gates[i].hit) { hit[nh++] = bs; break; }
    }
    int start = 0;
    while (nh - start > max - 1) start++;           /* reserve one row for the 1/4 */
    int n = 0;
    for (int i = start; i < nh; i++) out[n++] = hit[i];
    out[n++] = 0;                                   /* the 1/4 row, always present */
    return n;
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
        drive_fused(&fs, false);
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
    for (int k = 0; k <= 50; k++) { fused_sample_t fs = fused((int64_t)k * DT_US, 0.20f, 0); drive_fused(&fs, false); }
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 0.20 * G_MPS2 * 0.5, D.v_est);

    gps_fix_t f1 = gfix(50 * DT_US, 5000, true);           /* Doppler says 5.000 m/s */
    drag_on_fix(&D, &f1);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, D.v_est);

    for (int k = 51; k <= 70; k++) { fused_sample_t fs = fused((int64_t)k * DT_US, 0.20f, 0); drive_fused(&fs, false); }
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
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_IDLE, drag_state(&D));
    TEST_ASSERT_EQUAL_INT(0, ev_count(EV_DRAG_ARMED));

    fused_sample_t fs = fused((int64_t)200 * DT_US, 0.0f, FUS_STILL);   /* t = 2.00 s: arms */
    drive_fused(&fs, true);
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
        drive_fused(&fs, true);
    }
    fused_sample_t bump = fused((int64_t)151 * DT_US, 0.0f, 0);   /* stillness lost: timer restarts */
    drive_fused(&bump, true);
    for (int k = 152; k <= 300; k++) {          /* another 1.49 s still — still short of 2 s */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, FUS_STILL);
        drive_fused(&fs, true);
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
    arm_engine(true);
    run_const_g(0.5, 1700, true);

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
    arm_engine(false);
    run_const_g(0.5, 1700, false);

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
    arm_engine(false);
    run_const_g(0.5, 1700, false);

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
    arm_engine(false);
    run_const_g(0.5, 1700, false);

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
    arm_engine(false);
    run_const_g(0.5, 1700, false);

    const drag_result_t *r = drag_current(&D);
    TEST_ASSERT_TRUE(r->flags & DRAG_F_ROLLOUT);
    double a = 0.5 * G_MPS2;
    double expect_s = (double)T_LAUNCH_US / 1e6 + sqrt(2.0 * (double)DRAG_ROLLOUT_M / a);
    TEST_ASSERT_DOUBLE_WITHIN(0.02, expect_s, (double)D.t0_gps_us / 1e6);
    TEST_ASSERT_TRUE(r->flags & DRAG_F_QUARTER);                  /* the run still completes */
}

/* ------------------------------------------------------------ Task 3 tests */

/* §22.1: braking distance from 100 km/h at −1 g = 39.3 m ± 0.5. The run peaks just above 100 km/h,
 * then brakes; the 100-0 gate accumulates dist from the 100 km/h crossing to the stop (< 0.5 km/h). */
static void test_braking_distance_100_to_0(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    run_accel_then_brake(105.0, true);

    const drag_gate_res_t *gb = gate_by_id(drag_current(&D), 11);
    TEST_ASSERT_NOT_NULL(gb);
    TEST_ASSERT_TRUE(gb->hit);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 39.3, (double)gb->dist_cm / 100.0);
}

/* §11.2 false start: v_est < 1 km/h within DRAG_FALSE_START_S of launch discards the run and returns
 * to ARMED. A short g-spike launches, then a Doppler fix of 0 (stall) collapses v_est. No DONE fires
 * and the discarded run leaves no gate hits. */
static void test_false_start_abort(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    for (int k = LAUNCH_K; k <= 215; k++) {                       /* g-spike: launches at ~k=211 */
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.3f, 0);
        drive_fused(&fs, true);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    fused_sample_t s216 = fused((int64_t)216 * DT_US, 0.0f, 0);
    drive_fused(&s216, true);
    gps_fix_t stall = gfix((int64_t)216 * DT_US, 0, true);        /* Doppler: stopped */
    drag_on_fix(&D, &stall);
    fused_sample_t s217 = fused((int64_t)217 * DT_US, 0.0f, 0);   /* v_est ≈ 0 within 2 s of launch */
    drive_fused(&s217, true);

    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_ARMED, drag_state(&D));       /* re-armed, run discarded */
    TEST_ASSERT_EQUAL_INT(0, ev_count(EV_DRAG_DONE));
    TEST_ASSERT_FALSE(gate_by_id(drag_current(&D), 2)->hit);      /* no gate recorded */
}

/* §11.2/§6.6 false-start guard regression: a sustained ~0.2 g launch is itself below
 * DRAG_FALSE_START_KMH for the first ~140 ms after t0 (v_est is still climbing from 0), which is not
 * a stall — the abort requires v_peak to have already cleared the threshold before a drop-below
 * counts as a false start. Without that guard this legitimate low-g launch gets discarded and
 * re-armed the instant it enters LAUNCHED, emitting a duplicate EV_DRAG_LAUNCH; the fixed engine
 * launches exactly once and runs its gates through to the 1/4. */
static void test_low_g_launch_no_false_start(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    run_const_g(0.2, 2800, true);

    TEST_ASSERT_EQUAL_INT(1, ev_count(EV_DRAG_LAUNCH));
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_DONE, drag_state(&D));
    const drag_result_t *r = drag_current(&D);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(r->flags & DRAG_F_QUARTER);
}

/* §11.2: the braking gate may complete after DONE. A 0.5 g run ends DONE at the 1/4, then braking
 * from ~226 km/h through 100 records the 100-0 gate after the DONE event. */
static void test_done_then_brake(void)
{
    drag_init(&D, NULL);
    arm_engine(true);
    run_quarter_then_brake(true);

    const drag_gate_res_t *gb = gate_by_id(drag_current(&D), 11);
    TEST_ASSERT_TRUE(gb->hit);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 39.3, (double)gb->dist_cm / 100.0);
    /* the braking gate's EV_DRAG_GATE fired after EV_DRAG_DONE */
    int done_i  = ev_first(EV_DRAG_DONE);
    int brake_i = ev_gate_index(11);
    TEST_ASSERT_TRUE(done_i >= 0 && brake_i >= 0);
    TEST_ASSERT_TRUE(brake_i > done_i);
}

/* §11.4 bench visibility: a peak-180 run hits only the 100 bench (rows = {100, 1/4}); a peak-320 run
 * hits all three default benches (rows = {100, 200, 300, 1/4}), ascending, capped at 4 rows. */
static void test_bench_visibility_180_vs_320(void)
{
    uint16_t rows[8]; int n;

    drag_init(&D, NULL);
    arm_engine(false);
    run_accel_then_brake(180.0, false);        /* peaks at 180, brakes to a stop → DONE */
    const drag_result_t *r180 = drag_current(&D);
    TEST_ASSERT_TRUE(gate_by_id(r180, 2)->hit);     /* 0-100 hit */
    TEST_ASSERT_FALSE(gate_by_id(r180, 3)->hit);    /* 0-200 not */
    TEST_ASSERT_FALSE(gate_by_id(r180, 4)->hit);    /* 0-300 not */
    TEST_ASSERT_FALSE(gate_by_id(r180, 10)->hit);   /* 1/4 not */
    n = bench_rows(&D.cfg, r180, rows, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_UINT16(100, rows[0]);
    TEST_ASSERT_EQUAL_UINT16(0,   rows[1]);         /* the 1/4 row */

    drag_init(&D, NULL);
    arm_engine(false);
    run_const_g(1.0, 1300, false);             /* 1 g reaches the 1/4 at ~320 km/h */
    const drag_result_t *r320 = drag_current(&D);
    TEST_ASSERT_TRUE(gate_by_id(r320, 2)->hit && gate_by_id(r320, 3)->hit &&
                     gate_by_id(r320, 4)->hit && gate_by_id(r320, 10)->hit);
    n = bench_rows(&D.cfg, r320, rows, 4);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_UINT16(100, rows[0]);
    TEST_ASSERT_EQUAL_UINT16(200, rows[1]);
    TEST_ASSERT_EQUAL_UINT16(300, rows[2]);
    TEST_ASSERT_EQUAL_UINT16(0,   rows[3]);
}

/* §11.4 drop rule: with four benches configured {60,100,200,300}, a 1 g run hits all four, so the
 * lowest (60) is dropped to keep the four-row cap: rows = {100, 200, 300, 1/4}. */
static void test_bench_drop_lowest_when_over_four(void)
{
    drag_cfg_t c; drag_cfg_defaults(&c);
    c.benches_kmh[0] = 60; c.benches_kmh[1] = 100; c.benches_kmh[2] = 200; c.benches_kmh[3] = 300;
    c.n_benches = 4;
    drag_init(&D, &c);
    arm_engine(false);
    run_const_g(1.0, 1300, false);

    uint16_t rows[8];
    int n = bench_rows(&D.cfg, drag_current(&D), rows, 4);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_UINT16(100, rows[0]);         /* 60 dropped */
    TEST_ASSERT_EQUAL_UINT16(200, rows[1]);
    TEST_ASSERT_EQUAL_UINT16(300, rows[2]);
    TEST_ASSERT_EQUAL_UINT16(0,   rows[3]);
}

/* §11.3 best per gate: across a 0.5 g run and a faster 0.6 g run, drag_best returns the lower time_ms
 * for each gate. A gate never hit in any run (0-300, unreached by either) returns NULL. */
static void test_best_per_gate_two_runs(void)
{
    drag_init(&D, NULL);
    TEST_ASSERT_NULL(drag_best(&D, 2));             /* nothing completed yet */

    arm_engine(false);
    run_const_g(0.5, 1700, false);             /* 0-100 = 5665 ms, 1/4 = 12810 ms */
    drag_reset(&D);                                 /* keeps the session best */
    arm_engine(false);
    run_const_g(0.6, 1500, false);             /* 0-100 = 4721 ms, 1/4 = 11694 ms (faster) */

    const drag_result_t *b100 = drag_best(&D, 2);
    const drag_result_t *bq   = drag_best(&D, 10);
    TEST_ASSERT_NOT_NULL(b100);
    TEST_ASSERT_NOT_NULL(bq);
    TEST_ASSERT_INT_WITHIN(20, 4721, (int)gate_by_id(b100, 2)->time_ms);
    TEST_ASSERT_INT_WITHIN(20, 11694, (int)gate_by_id(bq, 10)->time_ms);
    TEST_ASSERT_NULL(drag_best(&D, 4));             /* 0-300 unreached by either run */
}

/* H4 regression (§6.6): a Doppler re-anchor must not hide a speed-gate crossing. The IMU integrates
 * v_est up to just below the 100 km/h gate; then a valid fix reports a Doppler speed just above it
 * (the receiver is ahead of the IMU). The vehicle physically crossed 100 km/h at the anchor, so the
 * engine must record gate id 2. The defect: drag_on_fix overwrote v_prev with the anchored speed, so
 * the next fused step saw vp >= V and the crossing (and its BRAKE mirror) was lost for good. Fixtures
 * elsewhere feed Doppler == integrated speed, so the anchor step is zero and never exercises this. */
static void test_gps_reanchor_does_not_hide_speed_gate(void)
{
    const double V100 = 100.0 / 3.6;                 /* 27.778 m/s = the id-2 (0-100) gate */
    drag_init(&D, NULL);
    arm_engine(false);

    /* Launch at 0.5 g and climb by IMU alone (NO Doppler fix yet) until v_est sits just below the
     * 100 km/h gate -- so the IMU has not itself crossed it. */
    int k = LAUNCH_K;
    for (; k <= 2000 && D.v_est < 27.0; k++) {
        fused_sample_t fs = fused((int64_t)k * DT_US, 0.5f, 0);
        drive_fused(&fs, false);
    }
    TEST_ASSERT_EQUAL_UINT8(DRAG_ST_LAUNCHED, drag_state(&D));
    TEST_ASSERT_TRUE(D.v_est < V100);                        /* not yet at 100 km/h */
    TEST_ASSERT_FALSE(gate_by_id(drag_current(&D), 2)->hit); /* 0-100 not yet recorded */

    /* A valid Doppler fix reports 28.0 m/s (100.8 km/h): the anchor steps v_est across the gate. */
    gps_fix_t jump = gfix((int64_t)(k - 1) * DT_US, 28000, true);
    drag_on_fix(&D, &jump);

    /* A few more cruising samples (g = 0): v_est holds ~28 m/s. */
    for (int j = 0; j < 5; j++) { fused_sample_t fs = fused((int64_t)k * DT_US, 0.0f, 0); drive_fused(&fs, false); k++; }

    const drag_gate_res_t *g100 = gate_by_id(drag_current(&D), 2);
    TEST_ASSERT_NOT_NULL(g100);
    TEST_ASSERT_TRUE(g100->hit);                             /* the anchored crossing must be recorded */
    TEST_ASSERT_INT_WITHIN(3, 2778, (int)g100->speed_cms);   /* recorded at the 100 km/h threshold */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_integration_constant_accel);
    RUN_TEST(test_gps_reanchor_does_not_hide_speed_gate);
    RUN_TEST(test_gps_reanchor_resets_v_est);
    RUN_TEST(test_arm_after_still_dwell);
    RUN_TEST(test_arm_requires_continuous_stillness);
    RUN_TEST(test_run_0p5g_zero_to_hundred_and_quarter);
    RUN_TEST(test_run_0p5g_trap_and_line_speed);
    RUN_TEST(test_run_0p5g_speed_range_100_200);
    RUN_TEST(test_interpolation_resolution_5ms);
    RUN_TEST(test_rollout_shifts_t0);
    RUN_TEST(test_braking_distance_100_to_0);
    RUN_TEST(test_false_start_abort);
    RUN_TEST(test_low_g_launch_no_false_start);
    RUN_TEST(test_done_then_brake);
    RUN_TEST(test_bench_visibility_180_vs_320);
    RUN_TEST(test_bench_drop_lowest_when_over_four);
    RUN_TEST(test_best_per_gate_two_runs);
    return UNITY_END();
}
