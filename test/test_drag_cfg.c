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
}
static void test_mph_benches_replace_speed_gates(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH;
    c.drag.n_mph = 2; c.drag.benches_mph[0] = 60; c.drag.benches_mph[1] = 100;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    TEST_ASSERT_EQUAL_UINT8(1, d.units);
    TEST_ASSERT_EQUAL_UINT8(2, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(60, d.gates[0].a);  TEST_ASSERT_EQUAL_UINT8(1, d.gates[0].id);
    TEST_ASSERT_EQUAL_UINT16(100, d.gates[1].a); TEST_ASSERT_EQUAL_UINT8(2, d.gates[1].id);
    TEST_ASSERT_EQUAL_UINT16(200, d.gates[2].a); /* default kept for the unused slot */
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
int main(void) { UNITY_BEGIN(); RUN_TEST(test_defaults_when_no_benches); RUN_TEST(test_mph_benches_replace_speed_gates); RUN_TEST(test_labels_for_every_default_gate); RUN_TEST(test_custom_distance_and_bad_cap); return UNITY_END(); }
