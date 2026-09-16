#include "unity.h"
#include "replay/replay.h"
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/synth_truth.h"     /* synth_generate, SYNTH_FUSED_HZ */
#include "replay/synth_drag.h"
#include "replay/logio.h"
#include "core/consts.h"
#include "core/types.h"
#include "core/drag.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define LOG_CAP (6u * 1024 * 1024)

/* §22.2 exit criterion for the replay engine run: replay_run drives the lap engine off a synthetic
 * .log (through the full §9.1 on_fix order) and every flying lap and sector crossing matches the
 * closed-form truth. pos_sigma = 0 isolates the engine's crossing-timing accuracy (§6.7). */
static void test_replay_run_lap_matches_synth_truth(void)
{
    synth_cfg_t cfg;
    synth_cfg_defaults(&cfg);
    cfg.laps = 5;                              /* out-lap (lap 0) + 4 flying laps */
    synth_gps_cfg_t gcfg;
    synth_gps_cfg_defaults(&gcfg);
    gcfg.rate_hz = 5;
    gcfg.pos_sigma_m = 0.0;

    synth_run_t  *run = (synth_run_t *)malloc(sizeof *run);
    trk_venue_t  *v   = (trk_venue_t *)malloc(sizeof *v);
    uint8_t      *buf = (uint8_t *)malloc(LOG_CAP);
    replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
    TEST_ASSERT_NOT_NULL(run); TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_NOT_NULL(buf); TEST_ASSERT_NOT_NULL(out);

    logw_t w;
    logw_open_mem(&w, buf, LOG_CAP);
    uint32_t n_fix = 0, n_fused = 0;
    TEST_ASSERT_EQUAL_INT(0, synth_generate(&cfg, &gcfg, SYNTH_FUSED_HZ, &w, run, &n_fix, &n_fused));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));
    synth_run_venue(run, v);

    TEST_ASSERT_EQUAL_INT(0, replay_run_mem(buf, w.len, REPLAY_MODE_LAP, v, out));
    TEST_ASSERT_EQUAL_INT(REPLAY_MODE_LAP, out->mode);
    TEST_ASSERT_EQUAL_UINT32(0, out->n_bad);
    TEST_ASSERT_EQUAL_UINT32(n_fix, out->n_fix);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)cfg.laps, out->n_laps);   /* out-lap + (laps-1) flying */
    TEST_ASSERT_EQUAL_UINT16(0, out->laps[0].lap_no);
    TEST_ASSERT_TRUE(out->laps[0].flags & LAP_F_OUT_LAP);

    const int64_t t0 = gcfg.t0_gps_us;
    double worst = 0.0;
    for (int k = 1; k < cfg.laps; k++) {
        const replay_lap_t *L = &out->laps[k];
        TEST_ASSERT_EQUAL_UINT16((uint16_t)k, L->lap_no);
        TEST_ASSERT_TRUE(L->flags & LAP_F_VALID);
        TEST_ASSERT_EQUAL_UINT8(3, L->n_sectors);
        TEST_ASSERT_EQUAL_UINT8(2, L->n_sector_cross);
        TEST_ASSERT_EQUAL_UINT32(L->time_ms, L->sector_ms[0] + L->sector_ms[1] + L->sector_ms[2]);

        /* engine lap k == synth lap k+1 (the first synth lap is consumed as the out-lap) */
        double lap_truth_ms = synth_run_lap_time(run, k + 1) * 1000.0;
        double e = fabs((double)L->time_ms - lap_truth_ms);
        if (e > worst) worst = e;
        TEST_ASSERT_TRUE(e <= 30.0);

        /* absolute crossing gps_us: lap k runs from S/F passage k to passage k+1 */
        double c[SYNTH_MAX_GATES + 1];
        int n = synth_run_lap_crossings(run, k + 1, c, sizeof c / sizeof c[0]);
        TEST_ASSERT_EQUAL_INT(4, n);        /* S/F + 2 sectors + S/F */
        int64_t truth_start = t0 + (int64_t)llround(c[0] * 1e6);
        int64_t truth_end   = t0 + (int64_t)llround(c[n - 1] * 1e6);
        TEST_ASSERT_INT64_WITHIN(30000, truth_start, L->start_gps_us);
        TEST_ASSERT_INT64_WITHIN(30000, truth_end,   L->end_gps_us);
        for (int j = 1; j < n - 1; j++) {   /* sector gate crossings */
            int64_t truth_sec = t0 + (int64_t)llround(c[j] * 1e6);
            TEST_ASSERT_INT64_WITHIN(30000, truth_sec, L->sector_gps_us[j - 1]);
        }
    }
    printf("[replay lap 5 Hz] worst lap-time error = %.1f ms\n", worst);

    free(run); free(v); free(buf); free(out);
}

/* §22.2/§11: replay_run drives the drag engine off a synthetic straight-line .log and every reached
 * §11.1 gate lands within tolerance of the closed-form time; unreached gates are not hit. */
static void test_replay_run_drag_matches_analytic_gates(void)
{
    synth_drag_cfg_t dc;
    synth_drag_cfg_defaults(&dc);
    dc.target_mps = 320.0 / 3.6;               /* 320 km/h at the 1/4 line: exercises every gate */
    synth_gps_cfg_t gcfg;
    synth_gps_cfg_defaults(&gcfg);
    gcfg.rate_hz = 10;
    gcfg.pos_sigma_m = 0.0;
    gcfg.speed_sigma_mps = 0.0;
    gcfg.head_sigma_deg = 0.0;

    synth_drag_run_t *run = (synth_drag_run_t *)malloc(sizeof *run);
    uint8_t      *buf = (uint8_t *)malloc(LOG_CAP);
    replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
    TEST_ASSERT_NOT_NULL(run); TEST_ASSERT_NOT_NULL(buf); TEST_ASSERT_NOT_NULL(out);

    logw_t w;
    logw_open_mem(&w, buf, LOG_CAP);
    uint32_t n_fix = 0, n_fused = 0;
    TEST_ASSERT_EQUAL_INT(0, synth_drag_generate(&dc, &gcfg, FUSION_HZ, &w, run, &n_fix, &n_fused));
    TEST_ASSERT_EQUAL_INT(0, logw_close(&w));

    TEST_ASSERT_EQUAL_INT(0, replay_run_mem(buf, w.len, REPLAY_MODE_DRAG, NULL, out));
    TEST_ASSERT_EQUAL_INT(REPLAY_MODE_DRAG, out->mode);
    TEST_ASSERT_EQUAL_UINT32(0, out->n_bad);
    TEST_ASSERT_EQUAL_UINT16(1, out->n_runs);
    const replay_drag_t *R = &out->runs[0];
    TEST_ASSERT_TRUE(R->flags & DRAG_F_QUARTER);

    drag_cfg_t defc;
    drag_cfg_defaults(&defc);
    TEST_ASSERT_EQUAL_UINT8(defc.n_gates, R->n_gates);

    double worst = 0.0;
    int checked = 0;
    for (uint8_t i = 0; i < R->n_gates; i++) {
        const drag_gate_def_t *d = &defc.gates[i];
        const drag_gate_res_t *g = &R->gates[i];
        TEST_ASSERT_EQUAL_UINT8(d->id, g->gate_id);
        double t = -1.0;
        switch (d->kind) {
        case DRAG_SPEED_FROM0: t = synth_drag_t_at_speed(run, (double)d->a / 3.6); break;
        case DRAG_SPEED_RANGE: {
            double ta = synth_drag_t_at_speed(run, (double)d->a / 3.6);
            double tb = synth_drag_t_at_speed(run, (double)d->b / 3.6);
            t = (ta >= 0.0 && tb >= 0.0) ? (tb - ta) : -1.0;
            break;
        }
        case DRAG_DIST:  t = synth_drag_t_at_dist(run, (double)d->a / 100.0); break;
        case DRAG_BRAKE: t = (run->v_peak_mps >= (double)d->a / 3.6) ? ((double)d->a / 3.6) / run->cfg.a_brake_mps2 : -1.0; break;
        default: break;
        }
        if (t >= 0.0) {
            TEST_ASSERT_TRUE_MESSAGE(g->hit, "expected gate hit");
            double e = fabs((double)g->time_ms - t * 1000.0);
            if (e > worst) worst = e;
            TEST_ASSERT_TRUE(e <= 40.0);
            checked++;
        } else {
            TEST_ASSERT_FALSE(g->hit);
        }
    }
    TEST_ASSERT_TRUE(checked >= 10);           /* 320 run hits every §11.1 gate */

    /* trap ≈ analytic mean speed over the last TRAP_DIST_M before the 1/4 line */
    double trap_truth_cms = synth_drag_trap_mps(run) * 100.0;
    TEST_ASSERT_TRUE(fabs((double)R->trap_cms - trap_truth_cms) <= 200.0);   /* ≤ 2 m/s */
    printf("[replay drag 10 Hz] worst gate-time error = %.1f ms, trap %u cm/s (truth %.0f)\n",
           worst, (unsigned)R->trap_cms, trap_truth_cms);

    free(run); free(buf); free(out);
}

/* Lap mode without a venue is rejected. */
static void test_replay_run_lap_needs_a_venue(void)
{
    replay_run_t *out = (replay_run_t *)malloc(sizeof *out);
    TEST_ASSERT_NOT_NULL(out);
    uint8_t dummy[8] = {0};
    TEST_ASSERT_EQUAL_INT(-2, replay_run_mem(dummy, sizeof dummy, REPLAY_MODE_LAP, NULL, out));
    free(out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_replay_run_lap_matches_synth_truth);
    RUN_TEST(test_replay_run_drag_matches_analytic_gates);
    RUN_TEST(test_replay_run_lap_needs_a_venue);
    return UNITY_END();
}
