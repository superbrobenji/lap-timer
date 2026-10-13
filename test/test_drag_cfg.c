#include "unity.h"
#include "core/drag.h"
#include "core/cfg.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
void setUp(void) {} void tearDown(void) {}
static void test_defaults_when_no_benches(void)
{
    cfg_t c; cfg_defaults(&c); c.drag.n_kmh = 0;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    TEST_ASSERT_EQUAL_UINT8(11, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8(0, d.units);
    TEST_ASSERT_EQUAL_UINT16(100, d.gates[1].a);   /* id 2 default 0-100 */
    /* Residual (final review re-review): an empty user bench list must not discard the shipped
     * default benches (drag_cfg_defaults()'s 100/200/300) by zeroing n_benches. */
    TEST_ASSERT_EQUAL_UINT8(3, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(100, d.benches_kmh[0]);
    TEST_ASSERT_EQUAL_UINT16(200, d.benches_kmh[1]);
    TEST_ASSERT_EQUAL_UINT16(300, d.benches_kmh[2]);
}
/* #86 (spec dsB §3): mph mode with a bench list converts the first n SPEED_FROM0 gates (ids 1..4,
 * table order) to km/h and sets benches_kmh/n_benches to match; gates 3/4 (not covered by a
 * 2-entry list) and every non-SPEED_FROM0 gate stay the drag_cfg_defaults() values. */
static void test_mph_benches_define_speed_gates(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH;
    c.drag.n_mph = 2; c.drag.benches_mph[0] = 60; c.drag.benches_mph[1] = 100;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_UINT8(1, d.units);
    TEST_ASSERT_EQUAL_UINT8(want.n_gates, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8(1, d.gates[0].id);   TEST_ASSERT_EQUAL_UINT16(97, d.gates[0].a);    /* 60 mph */
    TEST_ASSERT_EQUAL_UINT8(2, d.gates[1].id);   TEST_ASSERT_EQUAL_UINT16(161, d.gates[1].a);   /* 100 mph */
    TEST_ASSERT_EQUAL_UINT16(want.gates[2].a, d.gates[2].a);                                    /* defaults kept */
    TEST_ASSERT_EQUAL_UINT16(want.gates[3].a, d.gates[3].a);
    for (uint8_t i = 4; i < want.n_gates; i++) TEST_ASSERT_EQUAL_MEMORY(&want.gates[i], &d.gates[i], sizeof(drag_gate_def_t));
    TEST_ASSERT_EQUAL_UINT8(2, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(97, d.benches_kmh[0]); TEST_ASSERT_EQUAL_UINT16(161, d.benches_kmh[1]);
    char buf[8];
    TEST_ASSERT_EQUAL_INT(4, drag_gate_label(&d.gates[0], 1, CFG_DIST_FT, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("0-60", buf);
    TEST_ASSERT_EQUAL_INT(5, drag_gate_label(&d.gates[1], 1, CFG_DIST_FT, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("0-100", buf);
    TEST_ASSERT_EQUAL_INT(7, drag_gate_label(&d.gates[4], 1, CFG_DIST_FT, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("100-200", buf);   /* range: raw km/h */
}
/* #86: an empty mph bench list (n_mph == 0) must leave drag_cfg_defaults()'s gate table and km/h
 * benches in place, exactly like the existing empty-km/h-list case above. */
static void test_mph_empty_list_keeps_defaults(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH; c.drag.n_mph = 0;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_MEMORY(want.gates, d.gates, sizeof want.gates);
    TEST_ASSERT_EQUAL_UINT8(3, d.n_benches);
}
/* #86: round-trip exactness -- round(round(m*1.609344)/1.609344) == m for every integer mph 1..300
 * (spec dsB §3: verified, no .5 ties occur), so the mph label never drifts from the stored km/h a. */
static void test_mph_round_trip_is_exact(void)
{
    for (unsigned m = 1; m <= 300; m++) {
        drag_gate_def_t g = { 1, DRAG_SPEED_FROM0, (uint16_t)lround((double)m * DRAG_MPH_PER_KMH), 0 };
        char buf[8], want[8];
        (void)snprintf(want, sizeof want, "0-%u", m);
        TEST_ASSERT_TRUE(drag_gate_label(&g, 1, CFG_DIST_FT, buf, sizeof buf) > 0);
        TEST_ASSERT_EQUAL_STRING(want, buf);
    }
}

/* I1/I2 (final review, Ruling R-6): cfg_defaults() untouched (its shipped km/h bench list, 3 x
 * 100/200/300) must round-trip through drag_cfg_from_user() with the gate table still identical to
 * drag_cfg_defaults() and every one of the 11 default labels intact and pairwise distinct. */
static void test_shipped_defaults_keep_table(void)
{
    cfg_t c; cfg_defaults(&c);
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_UINT8(want.n_gates, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)want.gates, (const uint8_t *)d.gates,
                                   (unsigned)(want.n_gates * sizeof want.gates[0]));
    TEST_ASSERT_EQUAL_UINT8(3, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(100, d.benches_kmh[0]);
    TEST_ASSERT_EQUAL_UINT16(200, d.benches_kmh[1]);
    TEST_ASSERT_EQUAL_UINT16(300, d.benches_kmh[2]);
    char labels[11][8];
    for (int i = 0; i < 11; i++) TEST_ASSERT_TRUE(drag_gate_label(&d.gates[i], 0, CFG_DIST_FT, labels[i], sizeof labels[i]) > 0);
    TEST_ASSERT_EQUAL_STRING("0-60", labels[0]);   /* id 1 */
    for (int i = 0; i < 11; i++) {
        for (int j = i + 1; j < 11; j++) {
            TEST_ASSERT_TRUE(strcmp(labels[i], labels[j]) != 0);   /* no two labels equal */
        }
    }
}
static void check_label(const drag_gate_def_t *g, const char *want)
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT((int)strlen(want), drag_gate_label(g, 0, CFG_DIST_FT, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(want, buf);
}
static void test_labels_for_every_default_gate(void)
{
    drag_cfg_t d; drag_cfg_defaults(&d);
    const char *want[11] = { "0-60", "0-100", "0-200", "0-300", "100-200", "60ft", "330ft", "1/8", "1000ft", "1/4", "100-0" };
    for (int i = 0; i < 11; i++) check_label(&d.gates[i], want[i]);
}
static void test_custom_distance_and_bad_cap(void)
{
    drag_gate_def_t g = { 12, DRAG_DIST, 12000, 0 };
    check_label(&g, "120m");
    char small[4];
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&g, 0, CFG_DIST_FT, small, sizeof small));
    drag_gate_def_t bad = { 13, 9, 1, 0 }; char buf[8];
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&bad, 0, CFG_DIST_FT, buf, sizeof buf));
}
/* #96: DIST gates print feet or metres by dist_units; 1/8 and 1/4 mile keep their names in both. */
static void test_gate_label_dist_units(void)
{
    char b[16];
    const drag_gate_def_t ft60   = { 6, DRAG_DIST, 1829,  0 };
    const drag_gate_def_t ft330  = { 7, DRAG_DIST, 10058, 0 };
    const drag_gate_def_t eighth = { 8, DRAG_DIST, 20117, 0 };
    const drag_gate_def_t ft1000 = { 9, DRAG_DIST, 30480, 0 };
    const drag_gate_def_t quarter= { 10, DRAG_DIST, 40234, 0 };
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, CFG_DIST_FT, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("60ft", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0);  TEST_ASSERT_EQUAL_STRING("18m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft330, DRAG_UNITS_MPH, CFG_DIST_M, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("101m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft1000, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0);TEST_ASSERT_EQUAL_STRING("305m", b);
    TEST_ASSERT_TRUE(drag_gate_label(&eighth, DRAG_UNITS_KMH, CFG_DIST_M, b, sizeof b) > 0); TEST_ASSERT_EQUAL_STRING("1/8", b);
    TEST_ASSERT_TRUE(drag_gate_label(&quarter, DRAG_UNITS_KMH, CFG_DIST_FT, b, sizeof b) > 0);TEST_ASSERT_EQUAL_STRING("1/4", b);
    TEST_ASSERT_TRUE(drag_gate_label(&ft60, DRAG_UNITS_KMH, 2, b, sizeof b) < 0);   /* invalid dist_units rejected */
}
/* Bench regression 2026-10-07 (#96): dist_units is DISPLAY only (the DIST-gate label naming here,
 * and since bench B4-F5 the gate list's distance-row value too, screens_moto.c/core/ui/units.c)
 * -- it must not reach the engine's drag_cfg_t at all, so a Distance: m|ft toggle can never be
 * mistaken for a table change (which is what made pipeline_reload_cfg() drop a completed run and
 * the session bests). */
static void test_dist_units_does_not_touch_engine_cfg(void)
{
    cfg_t m; cfg_defaults(&m); m.dist_units = CFG_DIST_M;
    cfg_t f; cfg_defaults(&f); f.dist_units = CFG_DIST_FT;
    drag_cfg_t dm, df;
    drag_cfg_from_user(&m, &dm);
    drag_cfg_from_user(&f, &df);
    TEST_ASSERT_EQUAL_MEMORY(&dm, &df, sizeof dm);          /* byte-identical: no dist_units member */
    TEST_ASSERT_FALSE(drag_cfg_engine_differs(&dm, &df));
}

/* The comparison must key off the BUILT table, not the user's unit byte: with the shipped mph
 * bench list a km/h<->mph flip really does move gates 1..3 (apply_mph_benches), but with an empty
 * mph list it moves nothing and the session must survive. Rollout defines t0, so it counts. */
static void test_engine_differs_only_on_table_or_rollout(void)
{
    cfg_t k; cfg_defaults(&k);                              /* km/h, n_mph = 3 (60/120/180) */
    drag_cfg_t dk; drag_cfg_from_user(&k, &dk);

    cfg_t mph = k; mph.units = CFG_UNITS_MPH;               /* non-empty mph list -> table moves */
    drag_cfg_t dmph; drag_cfg_from_user(&mph, &dmph);
    TEST_ASSERT_EQUAL_UINT16(97, dmph.gates[0].a);          /* 60 mph, proof the table moved */
    TEST_ASSERT_TRUE(drag_cfg_engine_differs(&dk, &dmph));

    cfg_t mph0 = k; mph0.units = CFG_UNITS_MPH; mph0.drag.n_mph = 0;   /* empty list -> no move */
    drag_cfg_t dmph0; drag_cfg_from_user(&mph0, &dmph0);
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_MPH, dmph0.units);    /* display byte DID change ... */
    TEST_ASSERT_FALSE(drag_cfg_engine_differs(&dk, &dmph0)); /* ... but the engine's view did not */

    cfg_t bench = k; bench.drag.n_kmh = 2;                  /* km/h bench list: benches only */
    bench.drag.benches_kmh[0] = 80; bench.drag.benches_kmh[1] = 160;
    drag_cfg_t dbench; drag_cfg_from_user(&bench, &dbench);
    TEST_ASSERT_EQUAL_UINT8(2, dbench.n_benches);
    TEST_ASSERT_FALSE(drag_cfg_engine_differs(&dk, &dbench));

    cfg_t ro = k; ro.drag.rollout = !k.drag.rollout;        /* rollout redefines t0 */
    drag_cfg_t dro; drag_cfg_from_user(&ro, &dro);
    TEST_ASSERT_TRUE(drag_cfg_engine_differs(&dk, &dro));

    drag_cfg_t fewer = dk; fewer.n_gates = (uint8_t)(dk.n_gates - 1u);   /* table size */
    TEST_ASSERT_TRUE(drag_cfg_engine_differs(&dk, &fewer));
}

int main(void) { UNITY_BEGIN(); RUN_TEST(test_defaults_when_no_benches); RUN_TEST(test_mph_benches_define_speed_gates); RUN_TEST(test_mph_empty_list_keeps_defaults); RUN_TEST(test_mph_round_trip_is_exact); RUN_TEST(test_shipped_defaults_keep_table); RUN_TEST(test_labels_for_every_default_gate); RUN_TEST(test_custom_distance_and_bad_cap); RUN_TEST(test_gate_label_dist_units); RUN_TEST(test_dist_units_does_not_touch_engine_cfg); RUN_TEST(test_engine_differs_only_on_table_or_rollout); return UNITY_END(); }
