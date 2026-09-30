#include "unity.h"
#include "core/drag.h"
#include "core/cfg.h"
#include <string.h>
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
/* I1/I2 (final review, Ruling R-6): the bench list -- in ANY unit -- must never rewrite the gate
 * table. cfg.units = mph with a 2-entry mph bench list still yields a gate table byte-identical to
 * drag_cfg_defaults(), and units still reflects the display choice. */
static void test_units_never_touch_gates(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH;
    c.drag.n_mph = 2; c.drag.benches_mph[0] = 60; c.drag.benches_mph[1] = 100;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_UINT8(1, d.units);
    TEST_ASSERT_EQUAL_UINT8(want.n_gates, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)want.gates, (const uint8_t *)d.gates,
                                   (unsigned)(want.n_gates * sizeof want.gates[0]));
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
    for (int i = 0; i < 11; i++) TEST_ASSERT_TRUE(drag_gate_label(&d.gates[i], labels[i], sizeof labels[i]) > 0);
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
    TEST_ASSERT_EQUAL_INT((int)strlen(want), drag_gate_label(g, buf, sizeof buf));
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
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&g, small, sizeof small));
    drag_gate_def_t bad = { 13, 9, 1, 0 }; char buf[8];
    TEST_ASSERT_EQUAL_INT(-1, drag_gate_label(&bad, buf, sizeof buf));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_defaults_when_no_benches); RUN_TEST(test_units_never_touch_gates); RUN_TEST(test_shipped_defaults_keep_table); RUN_TEST(test_labels_for_every_default_gate); RUN_TEST(test_custom_distance_and_bad_cap); return UNITY_END(); }
