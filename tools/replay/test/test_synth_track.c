#include "unity.h"
#include "replay/synth.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/trk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* synth_run_t is ~2.7 MB (203 lap tables x 128 pieces), so every run lives on the heap. */
static synth_run_t *run;
static char errbuf[128];

void setUp(void)   { run = malloc(sizeof *run); TEST_ASSERT_NOT_NULL(run); errbuf[0] = '\0'; }
void tearDown(void){ free(run); run = NULL; }

#define PI 3.14159265358979323846

/* The default 12-gon's 187 m straights never reach v_max, so the cruise branch needs a track with
 * long straights: a 4-gon of 4000 m has 937 m straights. */
static void long_straight_cfg(synth_cfg_t *c)
{
    synth_cfg_defaults(c);
    c->n_vertices = 4;
    c->length_m   = 4000.0;
}

static double norm_deg(double d) { d = fmod(d, 360.0); if (d < 0.0) d += 360.0; return d; }

/* End point, heading and speed of a piece, recomputed from its stored fields alone. */
static void piece_end(const synth_piece_t *p, double *e, double *n, double *head_deg, double *v)
{
    if (p->kind == SYNTH_P_ARC) {
        double cs = cos(p->turn_rad), sn = sin(p->turn_rad);
        double vx = p->e0 - p->ce, vy = p->n0 - p->cn;
        *e = p->ce + vx * cs - vy * sn;
        *n = p->cn + vx * sn + vy * cs;
        *head_deg = norm_deg((p->head0_rad - p->turn_rad) * 180.0 / PI);
    } else {
        *e = p->e0 + p->len * sin(p->head0_rad);
        *n = p->n0 + p->len * cos(p->head0_rad);
        *head_deg = norm_deg(p->head0_rad * 180.0 / PI);
    }
    *v = p->v0 + p->a * p->dur;
}

/* ------------------------------------------------------------------ 1 */
static void test_default_geometry_is_a_regular_12gon(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    TEST_ASSERT_EQUAL_INT(12, run->n_vertices);
    /* length_m is the exact sum of the driven pieces; the scale factor solves for cfg.length_m, so
     * the only error is floating point on a ~2.5e3 sum (about 1e-12 m). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2500.0, run->length_m);

    const double turn = -2.0 * PI / 12.0;          /* clockwise: every corner is a right turn */
    const double arc  = 40.0 * (2.0 * PI / 12.0);  /* r * |turn| */
    double sum = 0.0;
    for (int k = 0; k < 12; k++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, turn, run->turn_rad[k]);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, arc, run->arc_len[k]);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->straight_len[0], run->straight_len[k]);   /* 12 equal straights */
        sum += run->straight_len[k] + run->arc_len[k];
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->length_m, sum);
    /* straight = (2500 - 12*arc)/12 for a regular polygon */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (2500.0 - 12.0 * arc) / 12.0, run->straight_len[0]);
    TEST_ASSERT_EQUAL_INT(3, run->n_gates);
    TEST_ASSERT_EQUAL_INT(13, run->n_tables);      /* cfg.laps + 3 */
}

/* ------------------------------------------------------------------ 2 */
static void test_lap_var_scales_accelerations_and_cap(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    for (int i = 0; i < run->n_tables; i++) {
        const synth_lap_t *tb = &run->tables[i];
        TEST_ASSERT_TRUE(fabs(tb->k - 1.0) <= 0.03 + 1e-12);              /* k = 1 + lap_var*U(-1,1) */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0 * tb->k, tb->a_acc_mps2);    /* the same factor on every scaled field */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 6.0 * tb->k, tb->a_brk_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0 * tb->k, tb->v_max_mps);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 15.0, tb->pieces[0].v0);   /* accel enters at the constant v_corner */
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 15.0, tb->pieces[2].v0);   /* the arc is driven at v_corner too */
    }
    TEST_ASSERT_TRUE(fabs(run->tables[0].k - run->tables[1].k) > 1e-6);
    /* Default lap_var: consecutive laps of the default track differ even though v_corner never moves,
     * because the accelerations and the cruise cap do. */
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(run, 1) - synth_run_lap_time(run, 2)) > 1e-3);

    synth_cfg_t c0; synth_cfg_defaults(&c0); c0.lap_var = 0.0;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c0, errbuf, sizeof errbuf));
    for (int i = 0; i < run->n_tables; i++) {
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, run->tables[i].k);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0, run->tables[i].a_acc_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 6.0, run->tables[i].a_brk_mps2);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0, run->tables[i].v_max_mps);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[0].lap_time_s, run->tables[i].lap_time_s);  /* identical with lap_var 0 */
    }
}

static void test_default_straights_are_accel_brake_only(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[0];
    /* 187.4 m straights at ~3 m/s^2 from ~15 m/s peak at ~31 m/s, far below v_max = 50, so the
     * default track has NO cruise piece: 12 straights x 2 pieces + 12 arcs = 36. */
    TEST_ASSERT_EQUAL_INT(36, tb->n_pieces);
    const double L = run->straight_len[0], vc = c.v_corner_mps, aa = tb->a_acc_mps2, ab = tb->a_brk_mps2;
    double vp2 = (2.0 * aa * ab * L + vc * vc * (aa + ab)) / (aa + ab);
    TEST_ASSERT_TRUE(sqrt(vp2) < tb->v_max_mps);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ACCEL, tb->pieces[0].kind);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_BRAKE, tb->pieces[1].kind);
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ARC,   tb->pieces[2].kind);
    double v_peak = tb->pieces[0].v0 + tb->pieces[0].a * tb->pieces[0].dur;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, sqrt(vp2), v_peak);          /* no-cruise peak formula */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vp2 - vc * vc) / (2.0 * aa), tb->pieces[0].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, L, tb->pieces[0].len + tb->pieces[1].len);
    /* The arc is one piece at the constant corner speed. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->arc_len[1] / vc, tb->pieces[2].dur);
}

static void test_long_straights_reach_cruise(void)
{
    synth_cfg_t c; long_straight_cfg(&c); c.lap_var = 0.0;
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[0];
    TEST_ASSERT_EQUAL_INT(16, tb->n_pieces);                      /* 4 x (accel, cruise, brake) + 4 arcs */
    TEST_ASSERT_EQUAL_INT(SYNTH_P_CRUISE, tb->pieces[1].kind);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 50.0, tb->pieces[1].v0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, tb->pieces[1].a);
    const double vc = 15.0, aa = 3.0, ab = 6.0, vm = 50.0;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vm * vm - vc * vc) / (2.0 * aa), tb->pieces[0].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, (vm * vm - vc * vc) / (2.0 * ab), tb->pieces[2].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->straight_len[0],
                              tb->pieces[0].len + tb->pieces[1].len + tb->pieces[2].len);
}

static void test_piece_table_is_continuous_and_matches_integration(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    const synth_lap_t *tb = &run->tables[1];
    for (int i = 0; i < tb->n_pieces; i++) {
        const synth_piece_t *p = &tb->pieces[i];
        const synth_piece_t *q = &tb->pieces[(i + 1) % tb->n_pieces];
        TEST_ASSERT_TRUE(p->len > 0.0);
        TEST_ASSERT_TRUE(p->dur > 0.0);
        double e, n, hd, v;
        piece_end(p, &e, &n, &hd, &v);
        /* 1e-9 m / 1e-9 m/s / 1e-9 deg: these are exact identities, the slack is float rounding on
         * coordinates of order 500 m (eps*500 ~ 1e-13). */
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->v0, v);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->e0, e);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->n0, n);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, norm_deg(q->head0_rad * 180.0 / PI), hd);
        if (i + 1 < tb->n_pieces) {
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->s0, p->s0 + p->len);
            TEST_ASSERT_DOUBLE_WITHIN(1e-9, q->t0, p->t0 + p->dur);
        }
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->length_m,
                              tb->pieces[tb->n_pieces - 1].s0 + tb->pieces[tb->n_pieces - 1].len);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, tb->lap_time_s,
                              tb->pieces[tb->n_pieces - 1].t0 + tb->pieces[tb->n_pieces - 1].dur);

    /* Independent midpoint integration of int ds/v(s) with v(s) = sqrt(v0^2 + 2a(s-s0)), ~1e5 steps
     * split at the piece boundaries so the integrand is smooth inside each cell. The midpoint error
     * is O(h^2) per piece; measured residual is ~1.2e-6 s, so 1e-4 s is a safe bound. */
    double t_num = 0.0;
    const int nsub = 100000 / tb->n_pieces;
    for (int i = 0; i < tb->n_pieces; i++) {
        const synth_piece_t *p = &tb->pieces[i];
        double h = p->len / (double)nsub;
        for (int k = 0; k < nsub; k++) {
            double ds = ((double)k + 0.5) * h;
            t_num += h / sqrt(p->v0 * p->v0 + 2.0 * p->a * ds);
        }
    }
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, tb->lap_time_s, t_num);
}

/* ------------------------------------------------------------------ 2b (review I1) */
/* Table boundaries sit at s = 0, the arc-at-vertex-0 -> straight-0 transition. Truth speed must be
 * continuous there (I1's fix: v_corner is a single cfg-level constant, never scaled per table, so
 * every table both ends its closing arc and starts its opening ACCEL at exactly v_corner). a_lon is
 * NOT continuous there -- every arc-to-straight transition in the lap, including this one, carries the
 * ordinary a = 0 (arc) -> a = a_acc_mps2 (straight ACCEL) step -- but each side must resolve to
 * exactly the value its own piece/table defines, with no boundary-selection glitch. */
static void check_truth_continuous_at_table_boundaries(const synth_run_t *r)
{
    int checked = 0;
    for (int i = 1; i < r->n_tables; i++) {
        double t = r->table_t0[i] - r->t_offset_s;
        if (t <= 0.0 || t >= r->duration_s) continue;      /* this boundary isn't reached by the run */
        /* eps = 1e-9 s: the "after" side is already inside the straight's ACCEL piece, so v is
         * drifting there at up to a_acc_mps2 (~3 m/s^2, ~4.5 with lap_var = 0.5); a 1e-6 s window
         * would itself admit a ~3e-6 to 4.5e-6 m/s drift that has nothing to do with the table
         * boundary, swamping a 1e-6 m/s tolerance. 1e-9 s keeps that drift near 3e-9-4.5e-9 m/s,
         * three orders below the tolerance, while remaining far above double's ~1e-13 s resolution
         * at these run-time magnitudes (up to ~1e3 s). */
        synth_state_t a, b;
        synth_run_state_at(r, t - 1e-9, &a);
        synth_run_state_at(r, t + 1e-9, &b);
        TEST_ASSERT_TRUE(fabs(b.v_mps - a.v_mps) < 1e-6);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, a.a_lon_mps2);                     /* still on the closing arc */
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, r->tables[i].a_acc_mps2, b.a_lon_mps2); /* table i's own ACCEL rate */
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 0);          /* the run must actually reach at least one table boundary */
}

static void test_truth_speed_continuous_across_tables(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    check_truth_continuous_at_table_boundaries(run);

    synth_cfg_t c5; synth_cfg_defaults(&c5); c5.lap_var = 0.5;
    synth_run_t *r5 = malloc(sizeof *r5);
    TEST_ASSERT_NOT_NULL(r5);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(r5, &c5, errbuf, sizeof errbuf));
    check_truth_continuous_at_table_boundaries(r5);
    free(r5);
}

/* ------------------------------------------------------------------ 3 */
static void test_state_at_and_time_at_s_are_inverses(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    /* Round trip t -> s_total -> t. Worst measured residual is 3.4e-13 s (cancellation in
     * s_total - table*length_m at s_total ~ 2.8e4 m divided by v >= 14.5 m/s). */
    const double v_min = c.v_corner_mps;    /* v_corner is constant across every table; nothing is slower */
    for (int i = 0; i <= 500; i++) {
        double t = run->duration_s * (double)i / 500.0;
        synth_state_t st;
        synth_run_state_at(run, t, &st);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, t, synth_run_time_at_s(run, st.s_total_m));
        TEST_ASSERT_TRUE(st.s_m >= 0.0 && st.s_m < run->length_m);
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, st.s_total_m, (double)st.table * run->length_m + st.s_m);
        TEST_ASSERT_TRUE(st.v_mps >= v_min - 1e-9);
        TEST_ASSERT_TRUE(st.heading_deg >= 0.0 && st.heading_deg < 360.0);
    }
}

/* ------------------------------------------------------------------ 4 */
static void test_arc_kinematics_signs(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    /* Table 1 piece 2 is the arc at vertex 1; pick its midpoint in run time. */
    const synth_piece_t *p = &run->tables[1].pieces[2];
    TEST_ASSERT_EQUAL_INT(SYNTH_P_ARC, p->kind);
    const double vc = c.v_corner_mps;      /* constant: every table drives every arc at v_corner */
    double t_mid = run->table_t0[1] + p->t0 + 0.5 * p->dur - run->t_offset_s;
    synth_state_t st;
    synth_run_state_at(run, t_mid, &st);
    TEST_ASSERT_TRUE(st.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, vc, st.v_mps);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, st.a_lon_mps2);
    const double g_exp = vc * vc / (40.0 * G_MPS2);               /* v^2/(r*g), + because right turn */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, g_exp, st.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, atan(g_exp) * 180.0 / PI, st.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -(vc / 40.0) * 180.0 / PI, st.yaw_rate_dps);   /* -v/r, + = left */

    /* A point on a straight carries no lateral load. */
    const synth_piece_t *s0 = &run->tables[1].pieces[0];
    synth_state_t stz;
    synth_run_state_at(run, run->table_t0[1] + s0->t0 + 0.5 * s0->dur - run->t_offset_s, &stz);
    TEST_ASSERT_FALSE(stz.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, stz.yaw_rate_dps);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[1].a_acc_mps2, stz.a_lon_mps2);   /* table 1's own scaled rate */

    /* Anticlockwise is the same track driven the other way round: all three signs flip. The rider
     * factor comes from the same RNG stream (clockwise doesn't change how many draws are consumed or
     * in what order), so table 1's factor -- and hence v_corner, which is unaffected by it anyway --
     * is the same draw. */
    synth_cfg_t ca; synth_cfg_defaults(&ca); ca.clockwise = false;
    synth_run_t *acw = malloc(sizeof *acw);
    TEST_ASSERT_NOT_NULL(acw);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(acw, &ca, errbuf, sizeof errbuf));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0 * PI / 12.0, acw->turn_rad[1]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, run->tables[1].k, acw->tables[1].k);
    const synth_piece_t *pa = &acw->tables[1].pieces[2];
    synth_state_t sa;
    synth_run_state_at(acw, acw->table_t0[1] + pa->t0 + 0.5 * pa->dur - acw->t_offset_s, &sa);
    TEST_ASSERT_TRUE(sa.on_arc);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -g_exp, sa.g_lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, -atan(g_exp) * 180.0 / PI, sa.lean_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, (vc / 40.0) * 180.0 / PI, sa.yaw_rate_dps);
    free(acw);
}

/* ------------------------------------------------------------------ 5 */
static void test_lap_crossings_and_lap_times(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    double t[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(run->n_gates + 1, synth_run_lap_crossings(run, 1, t, sizeof t / sizeof t[0]));
    for (int i = 0; i < run->n_gates; i++) TEST_ASSERT_TRUE(t[i + 1] > t[i]);
    double sectors = 0.0;
    for (int i = 0; i < run->n_gates; i++) sectors += t[i + 1] - t[i];
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[run->n_gates] - t[0], sectors);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[run->n_gates] - t[0], synth_run_lap_time(run, 1));

    /* S/F crossing 0 is exactly start_before_m past the run start, and lands on the S/F gate. */
    double t_sf0 = synth_run_time_at_s(run, synth_run_sf_s_total(run, 0));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, t[0], t_sf0);
    synth_state_t st;
    synth_run_state_at(run, t_sf0, &st);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, run->gates[0].s_m, st.s_m);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 300.0, st.s_total_m - run->s_total_start);

    /* Default lap_var: consecutive laps of the default track differ (the accelerations and cruise cap
     * move with the table, so it bites even though these straights never cruise). */
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(run, 1) - synth_run_lap_time(run, 2)) > 1e-3);

    /* Out of range and short buffers are rejected. */
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 0, t, sizeof t / sizeof t[0]));
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 11, t, sizeof t / sizeof t[0]));
    TEST_ASSERT_EQUAL_INT(-1, synth_run_lap_crossings(run, 1, t, (size_t)run->n_gates));
    /* synth_run_lap_time itself returns a negative sentinel, not a plausible 0.0, for a bad lap_no. */
    TEST_ASSERT_TRUE(synth_run_lap_time(run, 0) < 0.0);
    TEST_ASSERT_TRUE(synth_run_lap_time(run, 11) < 0.0);

    /* lap_var = 0: every lap is the table lap time exactly. */
    synth_cfg_t c0; synth_cfg_defaults(&c0); c0.lap_var = 0.0;
    synth_run_t *r0 = malloc(sizeof *r0);
    TEST_ASSERT_NOT_NULL(r0);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(r0, &c0, errbuf, sizeof errbuf));
    for (int lap = 1; lap <= c0.laps; lap++)
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, r0->tables[0].lap_time_s, synth_run_lap_time(r0, lap));
    free(r0);

    /* The same holds on the cruising track. */
    synth_cfg_t cv; long_straight_cfg(&cv);
    synth_run_t *rv = malloc(sizeof *rv);
    TEST_ASSERT_NOT_NULL(rv);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(rv, &cv, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(fabs(synth_run_lap_time(rv, 1) - synth_run_lap_time(rv, 2)) > 1e-3);
    free(rv);

    /* review I2: sf_frac = 0.5 puts s_sf = 1250 m, well past start_before_m = 300, so s_start does NOT
     * wrap behind S/F -- this exercises the "extra = 0" branch of synth_run_sf_s_total (every other
     * config in this file has s_sf < start_before_m, always the wrap branch) -- and because some
     * sector gates then land behind s_sf, it also exercises the wrap_s(...) branch inside
     * synth_run_lap_crossings that brings a "behind S/F" gate forward into the lap. */
    synth_cfg_t cw; synth_cfg_defaults(&cw); cw.sf_frac = 0.5;
    synth_run_t *rw = malloc(sizeof *rw);
    TEST_ASSERT_NOT_NULL(rw);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(rw, &cw, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(rw->s_total_start <= rw->gates[0].s_m);                                /* no wrap */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, rw->gates[0].s_m, synth_run_sf_s_total(rw, 0));          /* no "+ length_m" */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, cw.start_before_m, synth_run_sf_s_total(rw, 0) - rw->s_total_start);
    double tw[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(rw->n_gates + 1, synth_run_lap_crossings(rw, 1, tw, sizeof tw / sizeof tw[0]));
    for (int i = 0; i < rw->n_gates; i++) TEST_ASSERT_TRUE(tw[i + 1] > tw[i]);
    free(rw);
}

/* ------------------------------------------------------------------ 6 */
static void test_gate_geometry_and_crossing(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    geo_origin_t o;
    geo_origin_set(&o, c.origin_lat_deg, c.origin_lon_deg);
    double t[SYNTH_MAX_GATES + 1];
    TEST_ASSERT_EQUAL_INT(run->n_gates + 1, synth_run_lap_crossings(run, 1, t, sizeof t / sizeof t[0]));

    for (int i = 0; i < run->n_gates; i++) {
        geo_enu_t p1 = geo_to_enu(&o, run->gates[i].line.p1.lat, run->gates[i].line.p1.lon);
        geo_enu_t p2 = geo_to_enu(&o, run->gates[i].line.p2.lat, run->gates[i].line.p2.lon);
        /* 1e-6 m: the equirectangular round trip through degrees loses ~2e-10 m at this origin. */
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2.0 * GATE_HALF_WIDTH_M, hypot(p2.x - p1.x, p2.y - p1.y));
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, run->gates[i].e_m, 0.5 * (p1.x + p2.x));
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, run->gates[i].n_m, 0.5 * (p1.y + p2.y));

        /* Every gate sits at least SYNTH_GATE_MARGIN_M inside a straight, so a +/-0.5 s chord around
         * the crossing is a straight line at constant acceleration and §6.4 recovers it exactly. */
        synth_state_t a, b;
        synth_run_state_at(run, t[i] - 0.5, &a);
        synth_run_state_at(run, t[i] + 0.5, &b);
        TEST_ASSERT_FALSE(a.on_arc);
        TEST_ASSERT_FALSE(b.on_arc);
        geo_enu_t ea = { a.e_m, a.n_m }, eb = { b.e_m, b.n_m };
        double frac; int dir = 0;
        TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(ea, eb, p1, p2, &frac, &dir));
        TEST_ASSERT_EQUAL_INT(1, dir);                      /* p1 left, p2 right (§6.4, §10.2) */
        double d   = frac * hypot(eb.x - ea.x, eb.y - ea.y);
        double tau = geo_interp_time(d, a.v_mps, b.v_mps, 1.0);
        /* 1e-6 s: the interpolation is exact here, residual measured at 3.5e-12 s. */
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, t[i], t[i] - 0.5 + tau);
    }

    /* A requested S/F on the arc at vertex 1 moves forward to the start of straight 1 plus the
     * margin (straight 0 + arc 1 + SYNTH_GATE_MARGIN_M). 0.08 * 2500 = 200 m is inside that arc. */
    synth_cfg_t ca; synth_cfg_defaults(&ca); ca.sf_frac = 0.08; ca.n_sector_gates = 0;
    synth_run_t *ra = malloc(sizeof *ra);
    TEST_ASSERT_NOT_NULL(ra);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(ra, &ca, errbuf, sizeof errbuf));
    TEST_ASSERT_EQUAL_INT(1, ra->n_gates);
    TEST_ASSERT_TRUE(200.0 > ra->straight_len[0]);                      /* the request really is on the arc */
    TEST_ASSERT_TRUE(200.0 < ra->straight_len[0] + ra->arc_len[1]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, ra->straight_len[0] + ra->arc_len[1] + SYNTH_GATE_MARGIN_M,
                              ra->gates[0].s_m);
    free(ra);
}

/* ------------------------------------------------------------------ 7 */
static void test_venue_validates_and_round_trips(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    trk_venue_t v;
    synth_run_venue(run, &v);
    TEST_ASSERT_EQUAL_INT(0, trk_validate_venue(&v));
    TEST_ASSERT_EQUAL_UINT16(TRK_USER_ID_BASE, v.id);
    TEST_ASSERT_EQUAL_STRING("Synthetic", v.name);
    TEST_ASSERT_EQUAL_UINT8(1, v.n_layouts);
    TEST_ASSERT_EQUAL_STRING("Full", v.layouts[0].name);
    TEST_ASSERT_EQUAL_INT8(1, v.layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_UINT8(2, v.layouts[0].n_sectors);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)llround(run->length_m), v.layouts[0].length_m);
    TEST_ASSERT_EQUAL_UINT32(VENUE_RADIUS_DEFAULT_M, v.radius_m);

    static char js[8192];
    int n = trk_to_json(&v, js, sizeof js);
    TEST_ASSERT_TRUE(n > 0);
    trk_venue_t back;
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&back, js, (size_t)n, errbuf, sizeof errbuf));
    /* trk_to_json writes coordinates with 7 decimals (jw_double), so the round trip is exact to
     * 5e-8 deg (~6 mm); 1e-7 deg is the tightest bound the writer's precision allows. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p1.lat, back.layouts[0].sf.p1.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p1.lon, back.layouts[0].sf.p1.lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p2.lat, back.layouts[0].sf.p2.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-7, v.layouts[0].sf.p2.lon, back.layouts[0].sf.p2.lon);
    TEST_ASSERT_EQUAL_UINT32(v.layouts[0].length_m, back.layouts[0].length_m);
    TEST_ASSERT_EQUAL_UINT8(v.layouts[0].n_sectors, back.layouts[0].n_sectors);
}

/* ------------------------------------------------------------------ 8 */
static void test_enu_to_ll_inverts_geo_to_enu(void)
{
    synth_cfg_t c; synth_cfg_defaults(&c);
    geo_origin_t o;
    geo_origin_set(&o, c.origin_lat_deg, c.origin_lon_deg);
    double lat, lon;
    synth_enu_to_ll(c.origin_lat_deg, c.origin_lon_deg, 1000.0, -700.0, &lat, &lon);
    geo_enu_t back = geo_to_enu(&o, lat, lon);
    /* 1e-6 m: both directions are the same equirectangular formula, so only float rounding on
     * degrees of order 1e1 remains (measured 2.3e-10 m). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1000.0, back.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -700.0, back.y);
}

/* ------------------------------------------------------------------ 9 */
static void expect_reject(synth_cfg_t *c)
{
    errbuf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, synth_run_build(run, c, errbuf, sizeof errbuf));
    TEST_ASSERT_TRUE(strlen(errbuf) > 0);
}

static void test_invalid_configs_are_rejected(void)
{
    synth_cfg_t c;
    synth_cfg_defaults(&c); c.n_vertices = 2;                       expect_reject(&c);
    synth_cfg_defaults(&c); c.laps = 0;                             expect_reject(&c);
    synth_cfg_defaults(&c); c.v_corner_mps = 60.0;                  expect_reject(&c);   /* >= v_max */
    synth_cfg_defaults(&c); c.n_sector_gates = LAP_MAX_SECTORS + 1; expect_reject(&c);
    synth_cfg_defaults(&c); c.length_m = 200000.0;                  expect_reject(&c);   /* > MAX_LENGTH_M */
    /* A 3-vertex 300 m track: r = 40 leaves 16 m straights, r = 80 makes the tangents longer than
     * the edge itself. start/stop are shortened so the geometry check is the one that fires. */
    synth_cfg_defaults(&c); c.n_vertices = 3; c.length_m = 300.0; c.start_before_m = 50.0; c.stop_after_m = 50.0;
    expect_reject(&c);
    c.corner_radius_m = 80.0;                                       expect_reject(&c);
    synth_cfg_defaults(&c); c.start_before_m = 1500.0; c.stop_after_m = 1200.0; expect_reject(&c);
    /* 9 gates on a 1000 m triangle: the sector gate at 353.3 m is already at a straight's first
     * admissible point, and the one before it is pushed onto the same point. */
    synth_cfg_defaults(&c); c.n_vertices = 3; c.length_m = 1000.0; c.n_sector_gates = 8;
    c.sf_frac = 0.01; c.start_before_m = 100.0; c.stop_after_m = 100.0;
    expect_reject(&c);

    /* An irregular 8-gon is still a valid track and still closes on cfg.length_m exactly. */
    synth_cfg_defaults(&c); c.n_vertices = 8; c.length_m = 1800.0; c.irregularity = 0.3;
    errbuf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, synth_run_build(run, &c, errbuf, sizeof errbuf));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1800.0, run->length_m);
    double sum = 0.0;
    for (int k = 0; k < 8; k++) sum += run->straight_len[k] + run->arc_len[k];
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1800.0, sum);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_geometry_is_a_regular_12gon);
    RUN_TEST(test_lap_var_scales_accelerations_and_cap);
    RUN_TEST(test_default_straights_are_accel_brake_only);
    RUN_TEST(test_long_straights_reach_cruise);
    RUN_TEST(test_piece_table_is_continuous_and_matches_integration);
    RUN_TEST(test_truth_speed_continuous_across_tables);
    RUN_TEST(test_state_at_and_time_at_s_are_inverses);
    RUN_TEST(test_arc_kinematics_signs);
    RUN_TEST(test_lap_crossings_and_lap_times);
    RUN_TEST(test_gate_geometry_and_crossing);
    RUN_TEST(test_venue_validates_and_round_trips);
    RUN_TEST(test_enu_to_ll_inverts_geo_to_enu);
    RUN_TEST(test_invalid_configs_are_rejected);
    return UNITY_END();
}
