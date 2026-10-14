#include "unity.h"
#include "core/cfg.h"
#include "core/json.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* deterministic LCG, same pattern as the other suites; reseeded in setUp so a second run within the
 * same process (the on-target 6 KB stack rerun in test_apps/core_selftest) replays the exact same
 * fuzz sequence as the first. */
static uint32_t lcg;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

void setUp(void) { lcg = 987654321u; }
void tearDown(void) {}

static void test_defaults_are_valid(void)
{
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);
    TEST_ASSERT_EQUAL_UINT8(3, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(3300, c.power.shutdown_mv);
    TEST_ASSERT_EQUAL_STRING("LapTimer", c.ble.name);
}

/* Every numeric clamp cfg_validate still performs, driven one field at a time. Bounds a uint8_t
 * field cannot violate (0 low, 255 high) are not clamped by cfg_validate and are marked absent. */
typedef struct { const char *name; size_t off; uint8_t width; long lo, hi; bool has_lo, has_hi; } clamp_case_t;
#define CL(field, w, lo, hi, hl, hh) { #field, offsetof(cfg_t, field), (w), (lo), (hi), (hl), (hh) }
static const clamp_case_t CLAMPS[] = {
    CL(lap.min_lap_s,       2,    5,  600, true,  true),
    CL(lap.max_lap_s,       2,   60, 3600, true,  true),
    CL(lap.gate_rearm_m,    2,   10,  500, true,  true),
    CL(lap.pit_speed_kmh,   1,    1,   30, true,  true),
    CL(lap.pit_time_s,      1,    3,   60, true,  true),
    CL(drag.benches_kmh[0], 2,   10,  400, true,  true),
    CL(drag.benches_mph[0], 2,   10,  250, true,  true),
    CL(drag.launch_g_e2,    1,    5,   50, true,  true),
    CL(power.pit_after_s,   2,   10,  600, true,  true),
    CL(power.park_after_s,  2,   60, 7200, true,  true),
    CL(power.shutdown_mv,   2, 3000, 3600, true,  true),
    CL(power.conn_idle_s,   2,   30, 1800, true,  true),
    CL(display.full_every,  1,    1,   50, true,  true),
    CL(ble.adv_s,           2,   15,  600, true,  true),
    CL(gps.dyn_model,       1,    0,    8, false, true),
    CL(gps.rate_hz,         1,    0,   25, false, true),
    CL(imu.mot_thr,         1,    2,  255, true,  false),
    CL(imu.mot_dur_ms,      1,    1,  255, true,  false),
};

static void clamp_set(cfg_t *c, const clamp_case_t *f, long v)
{
    if (f->width == 1) { uint8_t x = (uint8_t)v; memcpy((uint8_t *)c + f->off, &x, 1); }
    else { uint16_t x = (uint16_t)v; memcpy((uint8_t *)c + f->off, &x, 2); }
}
static long clamp_get(const cfg_t *c, const clamp_case_t *f)
{
    if (f->width == 1) { uint8_t x; memcpy(&x, (const uint8_t *)c + f->off, 1); return x; }
    uint16_t x; memcpy(&x, (const uint8_t *)c + f->off, 2); return x;
}

static void test_validate_clamps_each_out_of_range_field(void)
{
    char msg[96];
    for (size_t i = 0; i < sizeof CLAMPS / sizeof CLAMPS[0]; i++) {
        const clamp_case_t *f = &CLAMPS[i];
        if (f->has_lo) {
            cfg_t c; cfg_defaults(&c);
            clamp_set(&c, f, f->lo - 1);
            snprintf(msg, sizeof msg, "%s below %ld", f->name, f->lo);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), msg);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(f->lo, clamp_get(&c, f), msg);
        }
        if (f->has_hi) {
            cfg_t c; cfg_defaults(&c);
            clamp_set(&c, f, f->hi + 1);
            snprintf(msg, sizeof msg, "%s above %ld", f->name, f->hi);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), msg);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(f->hi, clamp_get(&c, f), msg);
        }
    }
    /* the non-clamp corrections: enums, rotation and the fused-rate whitelist */
    cfg_t c; cfg_defaults(&c);
    c.units = 9; c.dist_units = 2; c.mode = 9; c.display.rotation = 90; c.log.fused_hz = 7; c.ble.name[0] = '\0';
    TEST_ASSERT_EQUAL_INT(6, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);
    TEST_ASSERT_EQUAL_UINT8(CFG_MODE_LAP, c.mode);
    TEST_ASSERT_EQUAL_UINT8(0, c.display.rotation);
    TEST_ASSERT_EQUAL_UINT8(10, c.log.fused_hz);
    TEST_ASSERT_EQUAL_STRING("LapTimer", c.ble.name);
}

static void test_validate_resets_implausible_battery_calibration(void)
{
    const uint16_t D_ADC0 = 3000, D_TRUE0 = 3000, D_ADC1 = 4200, D_TRUE1 = 4200;
    struct { const char *why; uint16_t a0, t0, a1, t1; } bad[] = {
        { "adc points too close",   3000, 3000, 3050, 4200 },
        { "adc points inverted",    4200, 3000, 3000, 4200 },
        { "true points too close",  3000, 3000, 4200, 3099 },
        { "true points inverted",   3000, 4200, 4200, 3000 },
        { "adc_mv[0] below 1000",    900, 3000, 4200, 4200 },
        { "adc_mv[1] above 5000",   3000, 3000, 5001, 4200 },
        { "true_mv[0] below 1000",  3000,  900, 4200, 4200 },
        { "true_mv[1] above 5000",  3000, 3000, 4200, 5001 },
        { "all zero",                  0,    0,    0,    0 },
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        cfg_t c; cfg_defaults(&c);
        c.battery.adc_mv[0] = bad[i].a0; c.battery.true_mv[0] = bad[i].t0;
        c.battery.adc_mv[1] = bad[i].a1; c.battery.true_mv[1] = bad[i].t1;
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_ADC0, c.battery.adc_mv[0], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_TRUE0, c.battery.true_mv[0], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_ADC1, c.battery.adc_mv[1], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_TRUE1, c.battery.true_mv[1], bad[i].why);
    }
    /* a legitimate calibration survives untouched */
    cfg_t ok; cfg_defaults(&ok);
    ok.battery.adc_mv[0] = 1500; ok.battery.true_mv[0] = 3510;
    ok.battery.adc_mv[1] = 2000; ok.battery.true_mv[1] = 4180;
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&ok));
    TEST_ASSERT_EQUAL_UINT16(1500, ok.battery.adc_mv[0]);
    TEST_ASSERT_EQUAL_UINT16(4180, ok.battery.true_mv[1]);
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

/* #96: dist_units parses/serialises "m"/"ft" exactly like units does "kmh"/"mph", same error path
 * for an unknown string. */
static void test_dist_units_json(void)
{
    cfg_t c; cfg_defaults(&c);
    char err[64];
    const char *js_ft = "{\"dist_units\":\"ft\"}";
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, js_ft, strlen(js_ft), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_FT, c.dist_units);
    const char *js_m = "{\"dist_units\":\"m\"}";
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, js_m, strlen(js_m), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);
    const char *js_bad = "{\"dist_units\":\"yd\"}";
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, js_bad, strlen(js_bad), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);   /* rejected: untouched */

    cfg_t ft; cfg_defaults(&ft); ft.dist_units = CFG_DIST_FT;
    static char js[1024];
    int n = cfg_to_json(&ft, js, sizeof js);
    TEST_ASSERT_GREATER_THAN(0, n);
    cfg_t back; cfg_defaults(&back);
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&back, js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_FT, back.dist_units);
}

static void test_json_round_trip_is_lossless(void)
{
    static cfg_t a; cfg_defaults(&a);
    a.lap.min_lap_s = 33; a.drag.n_mph = 2; a.drag.benches_mph[0] = 60; a.drag.benches_mph[1] = 100;
    a.lap.n_default_layout = 1; a.lap.default_layout[0].venue = 6; a.lap.default_layout[0].layout = 2;
    a.battery.adc_mv[0] = 1500; a.battery.true_mv[0] = 3510; a.battery.adc_mv[1] = 2000; a.battery.true_mv[1] = 4180;
    strcpy(a.ble.name, "LapTimer-AB12"); a.display.live_clock = true; a.dist_units = CFG_DIST_FT;
    static char js[1024];
    int n = cfg_to_json(&a, js, sizeof js);
    TEST_ASSERT_GREATER_THAN(0, n);
    static cfg_t b; cfg_defaults(&b);
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&b, js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* #96: cfg_migrate(c, 1) is the v1->v2 path -- dist_units did not exist in v1, so migrate sets it
 * to the default (CFG_DIST_M) regardless of whatever byte was sitting there; every other field is
 * untouched (cfg_t.dist_units is appended at the struct's end, core/cfg.h, precisely so this is
 * true -- see the Task 3 report's blob-migration analysis). cfg_migrate_supported() is exactly
 * {1}: the version this firmware can receive from NVS; 0 and CFG_VERSION (2, i.e. "migrate to
 * itself") are both unsupported (cfg_migrate_supported_versions in test_cfg_blob.c covers this
 * too, from the blob layer's side). */
static void test_migrate_v1_is_noop(void)
{
    cfg_t c; cfg_defaults(&c);
    c.mode = CFG_MODE_DRAG;       /* a real stored field: migrate must not touch it */
    c.dist_units = 0xAAu;         /* simulates whatever byte a v1 image left there -- must not survive */
    TEST_ASSERT_EQUAL_INT(0, cfg_migrate(&c, 1));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);
    TEST_ASSERT_EQUAL_UINT8(CFG_MODE_DRAG, c.mode);
    TEST_ASSERT_EQUAL_INT(-1, cfg_migrate(&c, 0));
    TEST_ASSERT_FALSE(cfg_migrate_supported(0));
    TEST_ASSERT_TRUE(cfg_migrate_supported(1));
    TEST_ASSERT_FALSE(cfg_migrate_supported(CFG_VERSION));
}

static void test_version_is_owned_by_firmware(void)
{
    cfg_t c; cfg_defaults(&c);
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, "{\"version\":7}", 13, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    c.version = 200;
    TEST_ASSERT_EQUAL_INT(1, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
}

static void test_profile_defaults_apply(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_profile_t p = { true, 25, "LapTimer-AB12" };
    TEST_ASSERT_EQUAL_INT(0, cfg_apply_profile(&c, &p));
    TEST_ASSERT_TRUE(c.display.live_clock);
    TEST_ASSERT_EQUAL_UINT8(25, c.log.fused_hz);
    TEST_ASSERT_EQUAL_STRING("LapTimer-AB12", c.ble.name);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    cfg_profile_t bad = { false, 10, "this-name-is-way-too-long" };
    TEST_ASSERT_EQUAL_INT(-1, cfg_apply_profile(&c, &bad));
}

static void test_oversized_arrays_are_rejected(void)
{
    cfg_t c; cfg_defaults(&c); char err[64];
    const char *js = "{\"drag\":{\"benches_kmh\":[1,2,3,4,5]}}";
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(3, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);
}

static void test_err_buffer_is_optional(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_t before = c;
    const char *bad_value = "{\"units\":\"furlongs\"}";
    const char *bad_section = "{\"lap\":5}";
    const char *malformed = "{\"lap\":";
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_value, strlen(bad_value), NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_section, strlen(bad_section), NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, malformed, strlen(malformed), NULL, 0));
    char err[8];
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_value, strlen(bad_value), err, 0));   /* zero capacity */
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_from_json_rejects_document_deeper_than_the_depth_cap(void)
{
    static char js[256]; int p = 0;
    p += snprintf(js + p, sizeof js - (size_t)p, "{\"lap\":{\"min_lap_s\":30},\"z\":");
    for (int i = 0; i < 40; i++) js[p++] = '[';
    for (int i = 0; i < 40; i++) js[p++] = ']';
    js[p++] = '}';
    cfg_t c; cfg_defaults(&c); cfg_t before = c;
    char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, js, (size_t)p, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_profile_with_bad_name_leaves_the_struct_untouched(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_t before = c;
    cfg_profile_t bad = { true, 25, "this-name-is-way-too-long" };
    TEST_ASSERT_EQUAL_INT(-1, cfg_apply_profile(&c, &bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_fuzz_mutated_documents_never_corrupt_the_struct(void)
{
    static cfg_t seed; cfg_defaults(&seed);
    seed.lap.min_lap_s = 33; seed.drag.n_mph = 2; seed.drag.benches_mph[0] = 60; seed.drag.benches_mph[1] = 100;
    seed.lap.n_default_layout = 1; seed.lap.default_layout[0].venue = 6; seed.lap.default_layout[0].layout = 2;
    static char base[1024];
    int n = cfg_to_json(&seed, base, sizeof base);
    TEST_ASSERT_GREATER_THAN(0, n);
    int accepted = 0, rejected = 0;
    for (int it = 0; it < 500; it++) {
        static char js[1024]; memcpy(js, base, (size_t)n);
        int muts = 1 + (int)(rnd() % 4u);
        for (int m = 0; m < muts; m++) js[rnd() % (uint32_t)n] = (char)(rnd() % 256u);
        cfg_t c; cfg_defaults(&c); cfg_t before = c;
        char err[64]; err[0] = '\0';
        int r = cfg_from_json(&c, js, (size_t)n, err, sizeof err);
        if (r < 0) {
            rejected++;
            TEST_ASSERT_TRUE(strlen(err) > 0);
            TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);   /* -1 must not have moved anything */
        } else {
            accepted++;
            cfg_validate(&c);                                   /* whatever got through must still validate */
        }
    }
    TEST_ASSERT_GREATER_THAN(0, rejected);
    TEST_ASSERT_EQUAL_INT(500, accepted + rejected);
}

static void test_bool_fields_reject_a_wrong_json_type(void)
{
    /* io:M3: json_tok_bool indexed js[t->start] without checking the type, so any quoted value
     * starting with 't'/'f' set the flag -- and drag.rollout changes reported drag ET by 1 ft. */
    cfg_t c; char err[64];
    const char *j1 = "{\"drag\":{\"rollout\":\"take a hike\"}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j1, strlen(j1), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for drag.rollout", err);
    TEST_ASSERT_FALSE(c.drag.rollout);
    const char *j2 = "{\"display\":{\"invert\":\"frobnicate\"}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j2, strlen(j2), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("bad value for display.invert", err);
    const char *j3 = "{\"display\":{\"live_clock\":1}}";          /* 1 is not a bool */
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, j3, strlen(j3), err, sizeof err));
    const char *ok = "{\"drag\":{\"rollout\":true},\"display\":{\"invert\":false}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, ok, strlen(ok), err, sizeof err));
    TEST_ASSERT_TRUE(c.drag.rollout);
    TEST_ASSERT_FALSE(c.display.invert);
}

static void test_numeric_tokens_too_long_or_out_of_range_are_rejected(void)
{
    /* io:L2: tok_copy's return was discarded (a 40-digit token was silently truncated to 31
     * chars) and strtoll/strtod errno was never checked (1e999 saturated to inf and passed). */
    cfg_t c; char err[64];
    const char *big = "{\"lap\":{\"min_lap_s\":1111111111111111111111111111111111111111}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, big, strlen(big), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);

    /* Ruling S2-C: traced against the CURRENT (pre-fix) code -- the 40-digit case above is already
     * rejected pre-fix because its 31-char truncation saturates strtoll to LLONG_MAX (errno ERANGE,
     * but *end == '\0', so pre-fix json_tok_int returns true) and get_u16 then rejects v > 65535. It
     * is green before AND after this fix and proves nothing about truncation on its own. This
     * 34-char token's 31-char truncation ("0" x 31) parses IN range as 0, so pre-fix it was
     * SILENTLY ACCEPTED (rc 0, min_lap_s set to 0) -- verified empirically against the unmodified
     * tree before writing this assertion. Post-fix tok_copy's 34 >= sizeof tmp (32) rejects it. */
    const char *padded = "{\"lap\":{\"min_lap_s\":0000000000000000000000000000000045}}";
    cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, padded, strlen(padded), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);

    /* Ruling S2-B: "1e999" alone is a bare root primitive, which JSMN_STRICT's end-of-buffer check
     * (core/jsmn.h:174-178) rejects regardless of this fix -- json_parse would return -1. Wrapped in
     * an array it parses to 2 tokens (verified empirically): toks[0] the array, toks[1] the
     * primitive "1e999" that strtod saturates to inf. */
    jsmntok_t toks[8];
    const char *inf = "[1e999]";
    int n = json_parse(inf, strlen(inf), toks, 8);
    TEST_ASSERT_EQUAL_INT(2, n);
    double d = 0.0;
    TEST_ASSERT_FALSE(json_tok_double(inf, &toks[1], &d));      /* ERANGE -> not a value */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_validate_clamps_each_out_of_range_field);
    RUN_TEST(test_from_json_merges_only_given_keys_and_ignores_unknown);
    RUN_TEST(test_from_json_rejects_malformed);
    RUN_TEST(test_dist_units_json);
    RUN_TEST(test_json_round_trip_is_lossless);
    RUN_TEST(test_migrate_v1_is_noop);
    RUN_TEST(test_version_is_owned_by_firmware);
    RUN_TEST(test_profile_defaults_apply);
    RUN_TEST(test_oversized_arrays_are_rejected);
    RUN_TEST(test_validate_resets_implausible_battery_calibration);
    RUN_TEST(test_err_buffer_is_optional);
    RUN_TEST(test_from_json_rejects_document_deeper_than_the_depth_cap);
    RUN_TEST(test_profile_with_bad_name_leaves_the_struct_untouched);
    RUN_TEST(test_fuzz_mutated_documents_never_corrupt_the_struct);
    RUN_TEST(test_bool_fields_reject_a_wrong_json_type);
    RUN_TEST(test_numeric_tokens_too_long_or_out_of_range_are_rejected);
    return UNITY_END();
}
