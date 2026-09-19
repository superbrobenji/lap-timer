#include "core/drag.h"
#include "core/core.h"
#include <math.h>
#include <string.h>

/* Drag engine (spec §11, §6.6). Session 2.6, complete. §6.6 v_est/dist integration with the
 * drag_on_fix Doppler re-anchor; the IDLE → ARMED → LAUNCHED → DONE state machine with launch
 * detection, t0 back-dating out of the history ring and the optional rollout; gate evaluation
 * (SPEED_FROM0 / SPEED_RANGE / DIST / BRAKE) with §6.6 interpolation; the trap; the false-start
 * abort; the braking (100-0) gate that may complete after DONE; the full run result and the
 * best-per-gate across the session; EV_DRAG_ARMED/LAUNCH/GATE/DONE. */

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define DRAG_ASSERT_CODE 0x0A30

/* ---- unit helpers ---- */
static double kmh_to_mps(double kmh)
{
    CORE_ASSERT_RET(kmh >= 0.0, DRAG_ASSERT_CODE, 0.0);   /* speeds/thresholds are non-negative */
    return kmh / 3.6;
}

/* ---- configuration ---- */

void drag_cfg_defaults(drag_cfg_t *c)
{
    CORE_ASSERT_VOID(c != NULL, DRAG_ASSERT_CODE);
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
    CORE_ASSERT_VOID(c->n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* fits cfg.gates[] */
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
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
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
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
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

/* Light reset: back to IDLE, integration/timers cleared, but the frozen result (cur), run_no and the
 * session best are preserved (used for the auto DONE→IDLE settle). */
static void go_idle_keep_result(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    D->state = DRAG_ST_IDLE;
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
}

static void reset_to_idle(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    go_idle_keep_result(D);
    reset_run(D);                       /* also clears the current result */
}

/* The session best (§11.3) is a composite: best.gates[i] carries the best value seen for that gate id
 * across completed runs. Its slots are laid out (ids, all unhit) up front so drag_best can index it. */
static void init_best(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
    memset(&D->best, 0, sizeof D->best);
    D->best.n_gates = D->cfg.n_gates;
    for (uint8_t i = 0; i < D->cfg.n_gates; i++) D->best.gates[i].gate_id = D->cfg.gates[i].id;
    D->have_best = false;
}

void drag_init(drag_t *D, const drag_cfg_t *cfg)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    memset(D, 0, sizeof *D);
    if (cfg) D->cfg = *cfg;
    else drag_cfg_defaults(&D->cfg);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* gate arrays fit */
    find_quarter(D);
    D->run_no = 0;
    reset_to_idle(D);
    init_best(D);
}

void drag_reset(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    reset_to_idle(D);                   /* keeps cfg, run_no and the session best (§11.3) */
}

/* ---- queries ---- */

uint8_t drag_state(const drag_t *D)
{
    CORE_ASSERT_RET(D != NULL, DRAG_ASSERT_CODE, DRAG_ST_IDLE);
    return D->state;
}

const drag_result_t *drag_current(const drag_t *D)
{
    CORE_ASSERT_RET(D != NULL, DRAG_ASSERT_CODE, NULL);
    return (D->run_no > 0 || D->state != DRAG_ST_IDLE) ? &D->cur : NULL;
}

const drag_result_t *drag_best(const drag_t *D, uint8_t gate_id)
{
    CORE_ASSERT_RET(D != NULL, DRAG_ASSERT_CODE, NULL);
    CORE_ASSERT_RET(D->best.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE, NULL);   /* loop bound */
    if (!D->have_best) return NULL;
    for (uint8_t i = 0; i < D->best.n_gates; i++)
        if (D->best.gates[i].gate_id == gate_id) return D->best.gates[i].hit ? &D->best : NULL;
    return NULL;
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
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->hist_head < DRAG_HIST_N, DRAG_ASSERT_CODE);   /* write slot in range */
    D->hist[D->hist_head].gps_us = gps_us;
    D->hist[D->hist_head].g_lon  = g_lon;
    D->hist_head = (uint16_t)((D->hist_head + 1) % DRAG_HIST_N);
    if (D->hist_count < DRAG_HIST_N) D->hist_count++;
}

/* ---- fix re-anchor (§6.6): reset v_est to the Doppler gSpeed at every valid fix ---- */

void drag_on_fix(drag_t *D, const gps_fix_t *fix)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    if (!fix || !fix->valid) return;
    double v = (double)fix->gspeed_mms / 1000.0;    /* mm/s → m/s */
    if (v < 0.0) v = 0.0;
    D->v_est  = v;
    D->v_prev = v;                                  /* the next fused step integrates from here */
    if (D->state == DRAG_ST_LAUNCHED && v > D->v_peak) D->v_peak = v;
}

/* ---- session best (§11.3): lowest time_ms per gate, shortest dist_cm for BRAKE ---- */

static void update_best(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
    for (uint8_t i = 0; i < D->cfg.n_gates; i++) {
        const drag_gate_res_t *cg = &D->cur.gates[i];
        if (!cg->hit) continue;
        drag_gate_res_t *bg = &D->best.gates[i];
        bool better = !bg->hit ||
                      (D->cfg.gates[i].kind == DRAG_BRAKE ? cg->dist_cm < bg->dist_cm
                                                          : cg->time_ms < bg->time_ms);
        if (better) *bg = *cg;
    }
    if (D->cur.trap_cms > D->best.trap_cms) D->best.trap_cms = D->cur.trap_cms;
    if (D->cur.flags & DRAG_F_QUARTER)      D->best.flags |= DRAG_F_QUARTER;
    D->best.run_no = D->cur.run_no;         /* most recent contributor */
    D->have_best = true;
}

/* ---- gate crossing / trap for one integration step (§6.6 linear interpolation) ----
 * (tp,vp,dp) is the previous sample, (tc,vc,dc) the current one. Records any SPEED_FROM0/SPEED_RANGE/
 * DIST gate crossed in the interval and accumulates the trap window for the 1/4 gate. */

static void record_gate(drag_t *D, drag_evt_cb_t cb, void *ctx, uint8_t idx,
                        int64_t t_cross, double v_cross_mps, double dist_cross_m)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(idx < DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* indexes cur.gates[idx] */
    CORE_ASSERT_VOID(v_cross_mps >= 0.0, DRAG_ASSERT_CODE);     /* crossing speed non-negative */
    CORE_ASSERT_VOID(dist_cross_m >= 0.0, DRAG_ASSERT_CODE);    /* crossing distance non-negative */
    drag_gate_res_t *g = &D->cur.gates[idx];
    if (g->hit) return;
    int64_t rel = t_cross - D->t0_gps_us;
    if (rel < 0) rel = 0;
    g->hit       = 1;
    g->time_ms   = (uint32_t)((rel + 500) / 1000);
    g->speed_cms = (uint16_t)(v_cross_mps * 100.0 + 0.5);
    g->dist_cm   = (uint32_t)(dist_cross_m * 100.0 + 0.5);
    D->last_gate_gps_us = t_cross;
    emit(cb, ctx, EV_DRAG_GATE, D->cfg.gates[idx].id, t_cross, t_cross, g->time_ms, g->speed_cms);
}

/* The SPEED_RANGE gate (§11.1 gate 5, a→b km/h): arm at the a-crossing, record the b-crossing. */
static void gate_step_range(drag_t *D, drag_evt_cb_t cb, void *ctx, uint8_t i,
                            int64_t tp, double vp, double dp, int64_t tc, double vc, double dc)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(i < DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* indexes range_*[i], cur.gates[i] */
    const drag_gate_def_t *def = &D->cfg.gates[i];
    CORE_ASSERT_VOID(def->kind == DRAG_SPEED_RANGE, DRAG_ASSERT_CODE);   /* def->b is only meaningful for a range gate */
    double Va = kmh_to_mps((double)def->a);
    double Vb = kmh_to_mps((double)def->b);
    if (!D->range_started[i]) {
        if (vp < Va && vc >= Va && vc > vp) {
            double frac = (Va - vp) / (vc - vp);
            D->range_a_gps_us[i] = tp + (int64_t)(frac * (double)(tc - tp));
            D->range_a_dist_m[i] = dp + frac * (dc - dp);
            D->range_started[i]  = true;
        }
    }
    if (D->range_started[i] && !D->cur.gates[i].hit) {
        if (vp < Vb && vc >= Vb && vc > vp) {
            double frac = (Vb - vp) / (vc - vp);
            int64_t t_b = tp + (int64_t)(frac * (double)(tc - tp));
            double d_b = dp + frac * (dc - dp);
            drag_gate_res_t *g = &D->cur.gates[i];
            int64_t rel = t_b - D->range_a_gps_us[i];
            if (rel < 0) rel = 0;
            g->hit       = 1;
            g->time_ms   = (uint32_t)((rel + 500) / 1000);
            g->speed_cms = (uint16_t)(Vb * 100.0 + 0.5);
            double d_int = d_b - D->range_a_dist_m[i];
            if (d_int < 0) d_int = 0;
            g->dist_cm   = (uint32_t)(d_int * 100.0 + 0.5);
            D->last_gate_gps_us = t_b;
            emit(cb, ctx, EV_DRAG_GATE, def->id, t_b, t_b, g->time_ms, g->speed_cms);
        }
    }
}

static void gate_step(drag_t *D, drag_evt_cb_t cb, void *ctx,
                      int64_t tp, double vp, double dp, int64_t tc, double vc, double dc)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE);   /* loop bound */
    CORE_ASSERT_VOID(tc >= tp, DRAG_ASSERT_CODE);                           /* sample time is monotonic */
    CORE_ASSERT_VOID(vp >= 0.0 && vc >= 0.0, DRAG_ASSERT_CODE);             /* speeds non-negative */
    CORE_ASSERT_VOID(dp >= 0.0 && dc >= 0.0, DRAG_ASSERT_CODE);             /* distances non-negative */
    for (uint8_t i = 0; i < D->cfg.n_gates; i++) {
        const drag_gate_def_t *def = &D->cfg.gates[i];
        switch (def->kind) {
        case DRAG_SPEED_FROM0: {
            if (D->cur.gates[i].hit) break;
            double V = kmh_to_mps((double)def->a);
            if (vp < V && vc >= V && vc > vp) {
                double frac = (V - vp) / (vc - vp);
                int64_t t_cross = tp + (int64_t)(frac * (double)(tc - tp));
                double d_cross = dp + frac * (dc - dp);
                record_gate(D, cb, ctx, i, t_cross, V, d_cross);
            }
            break;
        }
        case DRAG_DIST: {
            if (D->cur.gates[i].hit) break;
            double Dm = (double)def->a / 100.0;
            if (dp < Dm && dc >= Dm && dc > dp) {
                double frac = (Dm - dp) / (dc - dp);
                int64_t t_cross = tp + (int64_t)(frac * (double)(tc - tp));
                double v_cross = vp + frac * (vc - vp);
                record_gate(D, cb, ctx, i, t_cross, v_cross, Dm);
            }
            break;
        }
        case DRAG_SPEED_RANGE:
            gate_step_range(D, cb, ctx, i, tp, vp, dp, tc, vc, dc);
            break;
        case DRAG_BRAKE:
        default:
            break;                      /* braking gate handled in brake_step */
        }
    }

    /* Trap window for the 1/4 gate: mean v_est over samples with dist ∈ [D − TRAP_DIST_M, D] (§6.6). */
    if (D->quarter_idx != DRAG_NO_GATE) {
        double lo = D->quarter_dist_m - (double)TRAP_DIST_M;
        if (dc >= lo && dc <= D->quarter_dist_m) {
            D->trap_sum += vc;
            D->trap_n++;
        }
    }
}

static void finalize_trap(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->trap_sum >= 0.0, DRAG_ASSERT_CODE);   /* Σ of non-negative speeds */
    if (D->trap_n > 0)
        D->cur.trap_cms = (uint16_t)((D->trap_sum / (double)D->trap_n) * 100.0 + 0.5);
}

/* ---- braking (100-0) gate (§6.6, §11.2) ----
 * Starts when v_est falls through the gate's high speed (100 km/h) after a run peak above it, and ends
 * when v_est < 0.5 km/h; the result is the distance accumulated in between. d_inc is this sample's
 * distance increment. May run before or after DONE. Returns true if the gate completed on this call. */
static bool brake_step(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t now, int64_t mono, double d_inc)
{
    CORE_ASSERT_RET(D != NULL, DRAG_ASSERT_CODE, false);
    CORE_ASSERT_RET(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE, false);   /* loop bound */
    CORE_ASSERT_RET(d_inc >= 0.0, DRAG_ASSERT_CODE, false);                        /* distance increment */
    uint8_t bi = DRAG_NO_GATE;
    for (uint8_t i = 0; i < D->cfg.n_gates; i++)
        if (D->cfg.gates[i].kind == DRAG_BRAKE && !D->cur.gates[i].hit) { bi = i; break; }
    if (bi == DRAG_NO_GATE || D->brake_done) return false;
    CORE_ASSERT_RET(bi < DRAG_MAX_GATES, DRAG_ASSERT_CODE, false);   /* indexes cur.gates[bi] */

    double Vhi = kmh_to_mps((double)D->cfg.gates[bi].a);   /* 100 km/h */
    if (!D->brake_active) {
        if (D->v_peak > Vhi && D->v_prev >= Vhi && D->v_est < Vhi) {
            double span = D->v_prev - D->v_est;
            double fb = span > 0.0 ? (D->v_prev - Vhi) / span : 0.0;   /* fraction before the crossing */
            if (fb < 0.0) fb = 0.0;
            if (fb > 1.0) fb = 1.0;
            D->brake_active = true;
            D->brake_start_gps_us = D->prev_gps_us + (int64_t)(fb * (double)(now - D->prev_gps_us));
            D->brake_dist_m = d_inc * (1.0 - fb);          /* distance travelled after the crossing */
        }
        return false;
    }

    D->brake_dist_m += d_inc;
    if (D->v_est < kmh_to_mps((double)DRAG_BRAKE_STOP_KMH)) {  /* §6.6: braking ends below 0.5 km/h */
        drag_gate_res_t *g = &D->cur.gates[bi];
        int64_t rel = now - D->brake_start_gps_us;
        if (rel < 0) rel = 0;
        g->hit       = 1;
        g->time_ms   = (uint32_t)((rel + 500) / 1000);
        g->speed_cms = 0;                                  /* stopped */
        g->dist_cm   = (uint32_t)(D->brake_dist_m * 100.0 + 0.5);
        D->brake_active = false;
        D->brake_done   = true;
        D->last_gate_gps_us = now;
        emit(cb, ctx, EV_DRAG_GATE, D->cfg.gates[bi].id, now, mono, g->time_ms, 0);
        return true;
    }
    return false;
}

/* ---- state transitions ---- */

static void enter_armed(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t gps_us, int64_t mono_us)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    D->state = DRAG_ST_ARMED;
    D->v_est = D->dist_m = 0.0;          /* §11.2: dist = 0, v_est = 0 */
    D->v_prev = D->dist_prev = 0.0;
    D->launch_run = false;
    reset_run(D);
    emit(cb, ctx, EV_DRAG_ARMED, 0, gps_us, mono_us, 0, 0);
}

/* §11.2 false start: discard the run and return to ARMED (no event, no run_no consumed). */
static void abort_to_armed(drag_t *D)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    if (D->run_no > 0) D->run_no--;      /* the discarded launch does not consume a run number */
    D->state = DRAG_ST_ARMED;
    D->v_est = D->dist_m = 0.0;
    D->v_prev = D->dist_prev = 0.0;
    D->t0_gps_us = 0;
    D->launch_run = false;
    reset_run(D);
}

static void enter_done(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t gps_us, int64_t mono_us)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    D->state = DRAG_ST_DONE;
    D->done_gps_us = gps_us;
    finalize_trap(D);
    update_best(D);                      /* freeze the result into the session best */
    emit(cb, ctx, EV_DRAG_DONE, D->run_no, gps_us, mono_us, 0, 0);
}

/* Launch: back-date t0 to the first sample of the contiguous g_lon > DRAG_LAUNCH_SCAN_G run ending at
 * detection (scanned out of the history ring), reconstruct v_est/dist from t0 = 0-state, then enter
 * LAUNCHED. Gates are evaluated over the reconstructed samples too (harmless: no default gate can be
 * crossed in the sub-second launch window). Rollout is applied later on the first live sample whose
 * dist crosses DRAG_ROLLOUT_M. */
static void do_launch(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t mono_us)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->hist_count <= DRAG_HIST_N, DRAG_ASSERT_CODE);   /* ring count in range */
    if (D->hist_count == 0) return;
    uint16_t newest = (uint16_t)((D->hist_head + DRAG_HIST_N - 1) % DRAG_HIST_N);
    CORE_ASSERT_VOID(newest < DRAG_HIST_N, DRAG_ASSERT_CODE);           /* ring index in range */
    uint16_t idx = newest, t0idx = newest;
    for (uint16_t i = 0; i < D->hist_count; i++) {
        if (D->hist[idx].g_lon > (float)DRAG_LAUNCH_SCAN_G) {
            t0idx = idx;
            if (i + 1 < D->hist_count) idx = (uint16_t)((idx + DRAG_HIST_N - 1) % DRAG_HIST_N);
            else break;
        } else {
            break;
        }
    }
    CORE_ASSERT_VOID(t0idx < DRAG_HIST_N, DRAG_ASSERT_CODE);            /* ring index in range */

    D->run_no++;
    reset_run(D);
    D->t0_gps_us      = D->hist[t0idx].gps_us;
    D->cur.run_no     = D->run_no;
    D->cur.t0_gps_us  = D->t0_gps_us;

    /* reconstruct v_est/dist from the launch instant (v = 0 at t0) forward to the newest sample */
    double v = 0.0, dist = 0.0;
    uint16_t cur = t0idx;
    double a_p = (double)D->hist[cur].g_lon * G_MPS2, v_p = 0.0, d_p = 0.0;
    int64_t t_p = D->hist[cur].gps_us;
    while (cur != newest) {
        uint16_t nxt = (uint16_t)((cur + 1) % DRAG_HIST_N);
        int64_t tc = D->hist[nxt].gps_us;
        double a_c = (double)D->hist[nxt].g_lon * G_MPS2;
        double dt = (double)(tc - t_p) / 1e6;
        if (dt <= 0.0) dt = 1.0 / (double)FUSION_HZ;
        v += 0.5 * (a_p + a_c) * dt;
        if (v < 0.0) v = 0.0;
        dist += 0.5 * (v_p + v) * dt;
        gate_step(D, cb, ctx, t_p, v_p, d_p, tc, v, dist);
        a_p = a_c; v_p = v; d_p = dist; t_p = tc;
        cur = nxt;
    }
    D->v_est = v;
    D->dist_m = dist;
    D->v_peak = v;
    D->state = DRAG_ST_LAUNCHED;
    emit(cb, ctx, EV_DRAG_LAUNCH, 0, D->t0_gps_us, mono_us, 0, 0);
}

/* Is a braking result still expected? (keeps the run in DONE until braking resolves.) */
static bool brake_pending(const drag_t *D)
{
    CORE_ASSERT_RET(D != NULL, DRAG_ASSERT_CODE, false);
    CORE_ASSERT_RET(D->cfg.n_gates <= DRAG_MAX_GATES, DRAG_ASSERT_CODE, false);   /* loop bound */
    bool has_gate = false;
    for (uint8_t i = 0; i < D->cfg.n_gates; i++)
        if (D->cfg.gates[i].kind == DRAG_BRAKE && !D->cur.gates[i].hit) { has_gate = true; break; }
    if (!has_gate) return false;
    return D->brake_active || D->v_est > kmh_to_mps((double)DRAG_FALSE_START_KMH);
}

/* ---- per-state fused-sample handling (§11.2), one helper per DRAG_ST_* ---- */

/* IDLE: watch for the arm condition (slow + FUS_STILL held for DRAG_ARM_STILL_S). */
static void step_idle(drag_t *D, drag_evt_cb_t cb, void *ctx, const fused_sample_t *fs, int64_t now)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(fs != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->state == DRAG_ST_IDLE, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->v_est >= 0.0, DRAG_ASSERT_CODE);   /* module invariant: re-anchor/integration keep it non-negative */
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
}

/* ARMED: launch when g_lon > DRAG_LAUNCH_G continuously for DRAG_LAUNCH_HOLD_MS (§11.2). */
static void step_armed(drag_t *D, drag_evt_cb_t cb, void *ctx, const fused_sample_t *fs, int64_t now)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(fs != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->state == DRAG_ST_ARMED, DRAG_ASSERT_CODE);
    if (fs->g_lon > (float)DRAG_LAUNCH_G) {
        if (!D->launch_run) { D->launch_run = true; D->launch_since_us = now; }
        else if (now - D->launch_since_us >= (int64_t)DRAG_LAUNCH_HOLD_MS * 1000) {
            do_launch(D, cb, ctx, fs->mono_us);
        }
    } else {
        D->launch_run = false;
    }
}

/* LAUNCHED: rollout, gate evaluation, braking, false-start abort and the DONE transitions (§11.2). */
static void step_launched(drag_t *D, drag_evt_cb_t cb, void *ctx, const fused_sample_t *fs,
                          int64_t now, double d_inc)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(fs != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->state == DRAG_ST_LAUNCHED, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(d_inc >= 0.0, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->quarter_idx == DRAG_NO_GATE || D->quarter_idx < DRAG_MAX_GATES, DRAG_ASSERT_CODE);
    /* Rollout (once): move t0 to where dist reaches DRAG_ROLLOUT_M and re-zero dist there. */
    if (D->cfg.rollout && !D->rollout_done && D->dist_m >= (double)DRAG_ROLLOUT_M) {
        double dp = D->dist_prev, dc = D->dist_m;
        double frac = (dc > dp) ? ((double)DRAG_ROLLOUT_M - dp) / (dc - dp) : 0.0;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        D->t0_gps_us     = D->prev_gps_us + (int64_t)(frac * (double)(now - D->prev_gps_us));
        D->cur.t0_gps_us = D->t0_gps_us;
        D->dist_m = 0.0;
        D->dist_prev = 0.0;
        D->rollout_done = true;
        D->cur.flags |= DRAG_F_ROLLOUT;
    }
    if (D->v_est > D->v_peak) D->v_peak = D->v_est;
    gate_step(D, cb, ctx, D->prev_gps_us, D->v_prev, D->dist_prev, now, D->v_est, D->dist_m);
    brake_step(D, cb, ctx, now, fs->mono_us, d_inc);   /* may record a braking result before DONE */

    /* §11.2 false start: v_est < 1 km/h within DRAG_FALSE_START_S of launch → discard, re-arm.
     * Guarded on v_peak having already cleared the threshold once: a sustained low-g launch is
     * itself briefly under 1 km/h just after t0 (v_est is still climbing from 0), and that is not
     * a stall — only a drop-below after the run has actually moved past the threshold counts. */
    if (now - D->t0_gps_us <= (int64_t)DRAG_FALSE_START_S * 1000000 &&
        D->v_peak > kmh_to_mps((double)DRAG_FALSE_START_KMH) &&
        D->v_est < kmh_to_mps((double)DRAG_FALSE_START_KMH)) {
        abort_to_armed(D);
        return;
    }

    /* DONE (§11.2): the 1/4 gate is hit; a full stop; or v below half-peak with no gate for the
     * timeout. */
    bool quarter = (D->quarter_idx != DRAG_NO_GATE && D->cur.gates[D->quarter_idx].hit);
    bool stopped = D->v_est < kmh_to_mps((double)DRAG_ARM_SPEED_KMH);
    bool faded   = D->v_peak > 0.0 && D->v_est < 0.5 * D->v_peak &&
                   D->last_gate_gps_us != 0 &&
                   now - D->last_gate_gps_us >= (int64_t)DRAG_TIMEOUT_S * 1000000;
    if (quarter) {
        D->cur.flags |= DRAG_F_QUARTER;
        enter_done(D, cb, ctx, now, fs->mono_us);
    } else if (stopped || faded) {
        enter_done(D, cb, ctx, now, fs->mono_us);
    }
}

/* DONE: braking may still complete (§11.2); settle back to IDLE 5 s after DONE once it has resolved. */
static void step_done(drag_t *D, drag_evt_cb_t cb, void *ctx, const fused_sample_t *fs,
                      int64_t now, double d_inc)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(fs != NULL, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(D->state == DRAG_ST_DONE, DRAG_ASSERT_CODE);
    CORE_ASSERT_VOID(d_inc >= 0.0, DRAG_ASSERT_CODE);
    if (brake_step(D, cb, ctx, now, fs->mono_us, d_inc)) update_best(D);
    if (now - D->done_gps_us >= (int64_t)DRAG_DONE_SETTLE_S * 1000000 && !brake_pending(D))
        go_idle_keep_result(D);
}

/* ---- one fused sample (100 Hz) ---- */

void drag_on_fused(drag_t *D, const fused_sample_t *fs, drag_evt_cb_t cb, void *ctx)
{
    CORE_ASSERT_VOID(D != NULL, DRAG_ASSERT_CODE);
    if (!fs) return;
    CORE_ASSERT_VOID(isfinite(fs->g_lon), DRAG_ASSERT_CODE);   /* accumulated into v_est/dist_m: a NaN would corrupt them permanently */
    const int64_t now = fs->gps_us;
    const double  a_cur = (double)fs->g_lon * G_MPS2;
    CORE_ASSERT_VOID(D->state <= DRAG_ST_DONE, DRAG_ASSERT_CODE);   /* valid state enum */

    /* §6.6 integration: v_est by trapezoid on a_lon, dist by trapezoid on v_est. d_inc is this
     * sample's distance increment (used by the braking gate). */
    double d_inc = 0.0;
    if (D->have_prev) {
        double dt = (double)(now - D->prev_gps_us) / 1e6;
        if (dt <= 0.0) dt = 1.0 / (double)FUSION_HZ;
        D->v_est += 0.5 * (D->a_prev + a_cur) * dt;
        if (D->v_est < 0.0) D->v_est = 0.0;
        d_inc = 0.5 * (D->v_prev + D->v_est) * dt;
        D->dist_m += d_inc;
    }

    hist_push(D, now, fs->g_lon);

    switch (D->state) {
    case DRAG_ST_IDLE:     step_idle(D, cb, ctx, fs, now); break;
    case DRAG_ST_ARMED:    step_armed(D, cb, ctx, fs, now); break;
    case DRAG_ST_LAUNCHED: step_launched(D, cb, ctx, fs, now, d_inc); break;
    case DRAG_ST_DONE:     step_done(D, cb, ctx, fs, now, d_inc); break;
    default:               break;
    }

    D->a_prev = a_cur;
    D->v_prev = D->v_est;
    D->dist_prev = D->dist_m;
    D->prev_gps_us = now;
    D->have_prev = true;
}
