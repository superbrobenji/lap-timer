#include "core/drag.h"
#include <string.h>

/* Drag engine (spec §11, §6.6). Session 2.6 part 1 (Task 1): the §6.6 v_est/dist integration and the
 * drag_on_fix Doppler re-anchor, the IDLE → ARMED transition and the fused-sample history ring.
 * Launch detection, t0 back-dating, gate evaluation, the trap, DONE, braking, false-start and the
 * best-per-gate result are documented no-op stubs here; Task 2 adds launch/gates/trap/DONE and Task 3
 * adds braking, the false-start abort and the session best. */

/* ---- unit helpers ---- */
static double kmh_to_mps(double kmh) { return kmh / 3.6; }

/* ---- configuration ---- */

void drag_cfg_defaults(drag_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    /* §11.1 default gate list (km/h for SPEED_*, cm for DIST). ids are stable and logged. */
    static const drag_gate_def_t def[] = {
        { 1,  DRAG_SPEED_FROM0,  60,   0 },
        { 2,  DRAG_SPEED_FROM0,  100,  0 },
        { 3,  DRAG_SPEED_FROM0,  200,  0 },
        { 4,  DRAG_SPEED_FROM0,  300,  0 },
        { 5,  DRAG_SPEED_RANGE,  100,  200 },
        { 6,  DRAG_DIST,         1829, 0 },      /* 60 ft */
        { 7,  DRAG_DIST,         10058, 0 },     /* 330 ft */
        { 8,  DRAG_DIST,         20117, 0 },     /* 1/8 mile */
        { 9,  DRAG_DIST,         30480, 0 },     /* 1000 ft */
        { 10, DRAG_DIST,         40234, 0 },     /* 1/4 mile (trap) */
        { 11, DRAG_BRAKE,        100,  0 },      /* 100-0 */
    };
    c->n_gates = (uint8_t)(sizeof def / sizeof def[0]);
    for (uint8_t i = 0; i < c->n_gates; i++) c->gates[i] = def[i];
    c->benches_kmh[0] = 100;
    c->benches_kmh[1] = 200;
    c->benches_kmh[2] = 300;
    c->n_benches = 3;
    c->rollout = false;
    c->units   = DRAG_UNITS_KMH;
}

/* The 1/4 (trap) gate is the DIST gate with the largest distance a (§11.1 lists it as gate 10). */
static void find_quarter(drag_t *D)
{
    D->quarter_idx    = DRAG_NO_GATE;
    D->quarter_dist_m = 0.0;
    uint16_t best_a = 0;
    for (uint8_t i = 0; i < D->cfg.n_gates; i++) {
        if (D->cfg.gates[i].kind == DRAG_DIST && D->cfg.gates[i].a >= best_a) {
            best_a = D->cfg.gates[i].a;
            D->quarter_idx    = i;
            D->quarter_dist_m = (double)D->cfg.gates[i].a / 100.0;
        }
    }
}

/* Lay out cur.gates[] to mirror cfg.gates order, all unhit; clears the run accumulators. */
static void reset_run(drag_t *D)
{
    memset(&D->cur, 0, sizeof D->cur);
    D->cur.n_gates = D->cfg.n_gates;
    for (uint8_t i = 0; i < D->cfg.n_gates; i++) D->cur.gates[i].gate_id = D->cfg.gates[i].id;
    D->v_peak = 0.0;
    D->last_gate_gps_us = 0;
    D->rollout_done = false;
    D->brake_active = false;
    D->brake_done   = false;
    D->brake_start_gps_us = 0;
    D->brake_dist_m = 0.0;
    D->trap_sum = 0.0;
    D->trap_n   = 0;
    memset(D->range_started, 0, sizeof D->range_started);
}

static void reset_to_idle(drag_t *D)
{
    D->state = DRAG_ST_IDLE;            /* run_no is left untouched: it persists across resets */
    D->v_est = D->dist_m = 0.0;
    D->a_prev = D->v_prev = D->dist_prev = 0.0;
    D->prev_gps_us = 0;
    D->have_prev = false;
    D->t0_gps_us = 0;
    D->still_run = false;
    D->still_since_us = 0;
    D->launch_run = false;
    D->launch_since_us = 0;
    D->done_gps_us = 0;
    D->hist_head = 0;
    D->hist_count = 0;
    reset_run(D);
}

void drag_init(drag_t *D, const drag_cfg_t *cfg)
{
    memset(D, 0, sizeof *D);
    if (cfg) D->cfg = *cfg;
    else drag_cfg_defaults(&D->cfg);
    find_quarter(D);
    D->run_no = 0;
    reset_to_idle(D);
    D->have_best = false;
    memset(&D->best, 0, sizeof D->best);
}

void drag_reset(drag_t *D)
{
    reset_to_idle(D);                   /* keeps cfg, run_no and the session best (§11.3) */
}

/* ---- queries ---- */

uint8_t drag_state(const drag_t *D) { return D->state; }

const drag_result_t *drag_current(const drag_t *D)
{
    return (D->run_no > 0 || D->state != DRAG_ST_IDLE) ? &D->cur : NULL;
}

const drag_result_t *drag_best(const drag_t *D, uint8_t gate_id)
{
    (void)D; (void)gate_id;
    return NULL;                        /* session best — Task 3 */
}

/* ---- events ---- */

static void emit(drag_evt_cb_t cb, void *ctx, uint8_t type, uint16_t arg16,
                 int64_t gps_us, int64_t mono_us, uint32_t arg32, uint32_t arg32b)
{
    if (!cb) return;
    event_t ev = { type, 0, arg16, gps_us, mono_us, arg32, arg32b };
    cb(&ev, ctx);
}

/* ---- history ring ---- */

static void hist_push(drag_t *D, int64_t gps_us, float g_lon)
{
    D->hist[D->hist_head].gps_us = gps_us;
    D->hist[D->hist_head].g_lon  = g_lon;
    D->hist_head = (uint16_t)((D->hist_head + 1) % DRAG_HIST_N);
    if (D->hist_count < DRAG_HIST_N) D->hist_count++;
}

/* ---- fix re-anchor (§6.6): reset v_est to the Doppler gSpeed at every valid fix ---- */

void drag_on_fix(drag_t *D, const gps_fix_t *fix)
{
    if (!fix || !fix->valid) return;
    double v = (double)fix->gspeed_mms / 1000.0;    /* mm/s → m/s */
    if (v < 0.0) v = 0.0;
    D->v_est  = v;
    D->v_prev = v;                                  /* the next fused step integrates from here */
    if (D->state == DRAG_ST_LAUNCHED && v > D->v_peak) D->v_peak = v;
}

/* ---- arm detection (IDLE → ARMED, §11.2) ---- */

static void enter_armed(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t gps_us, int64_t mono_us)
{
    D->state = DRAG_ST_ARMED;
    D->v_est = D->dist_m = 0.0;          /* §11.2: dist = 0, v_est = 0 */
    D->v_prev = D->dist_prev = 0.0;
    D->launch_run = false;
    reset_run(D);
    emit(cb, ctx, EV_DRAG_ARMED, 0, gps_us, mono_us, 0, 0);
}

/* ---- one fused sample (100 Hz) ---- */

void drag_on_fused(drag_t *D, const fused_sample_t *fs, drag_evt_cb_t cb, void *ctx)
{
    if (!fs) return;
    const int64_t now = fs->gps_us;
    const double  a_cur = (double)fs->g_lon * G_MPS2;

    /* §6.6 integration: v_est by trapezoid on a_lon, dist by trapezoid on v_est. Both run continuously;
     * arming zeroes them and (Task 2) launch re-bases dist to t0. */
    if (D->have_prev) {
        double dt = (double)(now - D->prev_gps_us) / 1e6;
        if (dt <= 0.0) dt = 1.0 / (double)FUSION_HZ;         /* out-of-order / duplicate guard */
        D->v_est += 0.5 * (D->a_prev + a_cur) * dt;
        if (D->v_est < 0.0) D->v_est = 0.0;
        D->dist_m += 0.5 * (D->v_prev + D->v_est) * dt;
    }

    hist_push(D, now, fs->g_lon);

    switch (D->state) {
    case DRAG_ST_IDLE: {
        /* Arm when slow (v_est < DRAG_ARM_SPEED_KMH) and still (FUS_STILL) for DRAG_ARM_STILL_S. */
        bool slow  = D->v_est < kmh_to_mps((double)DRAG_ARM_SPEED_KMH);
        bool still = (fs->flags & FUS_STILL) != 0;
        if (slow && still) {
            if (!D->still_run) { D->still_run = true; D->still_since_us = now; }
            else if (now - D->still_since_us >= (int64_t)DRAG_ARM_STILL_S * 1000000) {
                enter_armed(D, cb, ctx, now, fs->mono_us);
            }
        } else {
            D->still_run = false;
        }
        break;
    }
    case DRAG_ST_ARMED:
        /* Launch detection, t0 back-dating and the LAUNCHED machine are added in Task 2. The history
         * ring above keeps rolling so the back-scan has ≥ 1 s of samples ready. */
        break;
    case DRAG_ST_LAUNCHED:
    case DRAG_ST_DONE:
        break;                          /* Task 2 / Task 3 */
    default:
        break;
    }

    D->a_prev = a_cur;
    D->v_prev = D->v_est;
    D->dist_prev = D->dist_m;
    D->prev_gps_us = now;
    D->have_prev = true;
}
