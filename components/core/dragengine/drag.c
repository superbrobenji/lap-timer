#include "core/drag.h"
#include <string.h>

/* Drag engine (spec §11, §6.6). Session 2.6 through Task 2: the §6.6 v_est/dist integration and the
 * drag_on_fix Doppler re-anchor, the full IDLE → ARMED → LAUNCHED → DONE state machine, launch
 * detection with t0 back-dating out of the history ring, the optional rollout, gate evaluation
 * (SPEED_FROM0 / SPEED_RANGE / DIST) with §6.6 interpolation, the trap and DONE, plus EV_DRAG_ARMED/
 * LAUNCH/GATE/DONE. The braking (100-0) gate, the false-start abort and the session best-per-gate are
 * documented stubs here and added in Task 3. */

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

/* Light reset: back to IDLE, integration/timers cleared, but the frozen result (cur), run_no and the
 * session best are preserved (used for the auto DONE→IDLE settle). */
static void go_idle_keep_result(drag_t *D)
{
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
    go_idle_keep_result(D);
    reset_run(D);                       /* also clears the current result */
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

/* ---- gate crossing / trap for one integration step (§6.6 linear interpolation) ----
 * (tp,vp,dp) is the previous sample, (tc,vc,dc) the current one. Records any SPEED_FROM0/SPEED_RANGE/
 * DIST gate crossed in the interval and accumulates the trap window for the 1/4 gate. */

static void record_gate(drag_t *D, drag_evt_cb_t cb, void *ctx, uint8_t idx,
                        int64_t t_cross, double v_cross_mps, double dist_cross_m)
{
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

static void gate_step(drag_t *D, drag_evt_cb_t cb, void *ctx,
                      int64_t tp, double vp, double dp, int64_t tc, double vc, double dc)
{
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
        case DRAG_SPEED_RANGE: {
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
            break;
        }
        case DRAG_BRAKE:
        default:
            break;                      /* braking gate — Task 3 */
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
    if (D->trap_n > 0)
        D->cur.trap_cms = (uint16_t)((D->trap_sum / (double)D->trap_n) * 100.0 + 0.5);
}

/* ---- state transitions ---- */

static void enter_armed(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t gps_us, int64_t mono_us)
{
    D->state = DRAG_ST_ARMED;
    D->v_est = D->dist_m = 0.0;          /* §11.2: dist = 0, v_est = 0 */
    D->v_prev = D->dist_prev = 0.0;
    D->launch_run = false;
    reset_run(D);
    emit(cb, ctx, EV_DRAG_ARMED, 0, gps_us, mono_us, 0, 0);
}

static void enter_done(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t gps_us, int64_t mono_us)
{
    D->state = DRAG_ST_DONE;
    D->done_gps_us = gps_us;
    emit(cb, ctx, EV_DRAG_DONE, D->run_no, gps_us, mono_us, 0, 0);
}

/* Launch: back-date t0 to the first sample of the contiguous g_lon > DRAG_LAUNCH_SCAN_G run ending at
 * detection (scanned out of the history ring), reconstruct v_est/dist from t0 = 0-state, then enter
 * LAUNCHED. Gates are evaluated over the reconstructed samples too (harmless: no default gate can be
 * crossed in the sub-second launch window). Rollout is applied later on the first live sample whose
 * dist crosses DRAG_ROLLOUT_M. */
static void do_launch(drag_t *D, drag_evt_cb_t cb, void *ctx, int64_t mono_us)
{
    if (D->hist_count == 0) return;
    uint16_t newest = (uint16_t)((D->hist_head + DRAG_HIST_N - 1) % DRAG_HIST_N);
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

/* ---- one fused sample (100 Hz) ---- */

void drag_on_fused(drag_t *D, const fused_sample_t *fs, drag_evt_cb_t cb, void *ctx)
{
    if (!fs) return;
    const int64_t now = fs->gps_us;
    const double  a_cur = (double)fs->g_lon * G_MPS2;

    /* §6.6 integration: v_est by trapezoid on a_lon, dist by trapezoid on v_est. */
    if (D->have_prev) {
        double dt = (double)(now - D->prev_gps_us) / 1e6;
        if (dt <= 0.0) dt = 1.0 / (double)FUSION_HZ;
        D->v_est += 0.5 * (D->a_prev + a_cur) * dt;
        if (D->v_est < 0.0) D->v_est = 0.0;
        D->dist_m += 0.5 * (D->v_prev + D->v_est) * dt;
    }

    hist_push(D, now, fs->g_lon);

    switch (D->state) {
    case DRAG_ST_IDLE: {
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
        /* Launch when g_lon > DRAG_LAUNCH_G continuously for DRAG_LAUNCH_HOLD_MS (§11.2). */
        if (fs->g_lon > (float)DRAG_LAUNCH_G) {
            if (!D->launch_run) { D->launch_run = true; D->launch_since_us = now; }
            else if (now - D->launch_since_us >= (int64_t)DRAG_LAUNCH_HOLD_MS * 1000) {
                do_launch(D, cb, ctx, fs->mono_us);
            }
        } else {
            D->launch_run = false;
        }
        break;
    case DRAG_ST_LAUNCHED: {
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

        /* DONE (§11.2): the 1/4 gate is hit; a full stop; or v below half-peak with no gate for the
         * timeout. The braking gate and false-start abort are added in Task 3. */
        bool quarter = (D->quarter_idx != DRAG_NO_GATE && D->cur.gates[D->quarter_idx].hit);
        bool stopped = D->v_est < kmh_to_mps((double)DRAG_ARM_SPEED_KMH);
        bool faded   = D->v_peak > 0.0 && D->v_est < 0.5 * D->v_peak &&
                       D->last_gate_gps_us != 0 &&
                       now - D->last_gate_gps_us >= (int64_t)DRAG_TIMEOUT_S * 1000000;
        if (quarter) {
            D->cur.flags |= DRAG_F_QUARTER;
            finalize_trap(D);
            enter_done(D, cb, ctx, now, fs->mono_us);
        } else if (stopped || faded) {
            finalize_trap(D);
            enter_done(D, cb, ctx, now, fs->mono_us);
        }
        break;
    }
    case DRAG_ST_DONE:
        /* Braking may still complete after DONE (Task 3). Settle back to IDLE 5 s after DONE, keeping
         * the frozen result for the display. */
        if (now - D->done_gps_us >= 5 * 1000000) go_idle_keep_result(D);
        break;
    default:
        break;
    }

    D->a_prev = a_cur;
    D->v_prev = D->v_est;
    D->dist_prev = D->dist_m;
    D->prev_gps_us = now;
    D->have_prev = true;
}
