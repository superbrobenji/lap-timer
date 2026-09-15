#include "unity.h"
#include "core/cfg.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_defaults_are_valid(void)
{
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);
    TEST_ASSERT_EQUAL_UINT8(3, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(3300, c.power.shutdown_mv);
    TEST_ASSERT_EQUAL_STRING("LapTimer", c.ble.name);
}

static void test_validate_clamps_each_out_of_range_field(void)
{
    cfg_t c; cfg_defaults(&c);
    c.lap.min_lap_s = 1; c.lap.max_lap_s = 9999; c.power.shutdown_mv = 100; c.display.full_every = 0; c.units = 9;
    TEST_ASSERT_EQUAL_INT(5, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT16(5, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(3600, c.lap.max_lap_s);
    TEST_ASSERT_EQUAL_UINT16(3000, c.power.shutdown_mv);
    TEST_ASSERT_EQUAL_UINT8(1, c.display.full_every);
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
}

static void test_from_json_merges_only_given_keys_and_ignores_unknown(void)
{
    cfg_t c; cfg_defaults(&c);
    const char *js = "{\"lap\":{\"min_lap_s\":30},\"units\":\"mph\",\"bogus\":1,\"drag\":{\"benches_kmh\":[80,160],\"rollout\":true}}";
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(30, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(1800, c.lap.max_lap_s);            /* untouched */
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_MPH, c.units);
    TEST_ASSERT_EQUAL_UINT8(2, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(160, c.drag.benches_kmh[1]);
    TEST_ASSERT_TRUE(c.drag.rollout);
}

static void test_from_json_rejects_malformed(void)
{
    cfg_t c; cfg_defaults(&c);
    char err[64];
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, "{\"lap\":", 7, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, "{\"units\":\"furlongs\"}", 20, err, sizeof err));
}

static void test_json_round_trip_is_lossless(void)
{
    cfg_t a; cfg_defaults(&a);
    a.lap.min_lap_s = 33; a.drag.n_mph = 2; a.drag.benches_mph[0] = 60; a.drag.benches_mph[1] = 100;
    a.lap.n_default_layout = 1; a.lap.default_layout[0].venue = 6; a.lap.default_layout[0].layout = 2;
    a.battery.adc_mv[0] = 1500; a.battery.true_mv[0] = 3510; a.battery.adc_mv[1] = 2000; a.battery.true_mv[1] = 4180;
    strcpy(a.ble.name, "LapTimer-AB12"); a.display.live_clock = true;
    char js[1024];
    int n = cfg_to_json(&a, js, sizeof js);
    TEST_ASSERT_GREATER_THAN(0, n);
    cfg_t b; cfg_defaults(&b);
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&b, js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

static void test_migrate_v1_is_noop(void)
{
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_migrate(&c, 1));
    TEST_ASSERT_EQUAL_INT(-1, cfg_migrate(&c, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_validate_clamps_each_out_of_range_field);
    RUN_TEST(test_from_json_merges_only_given_keys_and_ignores_unknown);
    RUN_TEST(test_from_json_rejects_malformed);
    RUN_TEST(test_json_round_trip_is_lossless);
    RUN_TEST(test_migrate_v1_is_noop);
    return UNITY_END();
}
