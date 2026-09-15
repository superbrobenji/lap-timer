#include "unity.h"
#include "core/trk.h"
#include <string.h>

void setUp(void) { trk_init(); }
void tearDown(void) {}

static void test_bundled_contains_killarney_with_four_layouts(void)
{
    const trk_venue_t *v = trk_get(6);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("Killarney", v->name);
    TEST_ASSERT_EQUAL_UINT8(4, v->n_layouts);
    TEST_ASSERT_EQUAL_INT8(1, v->layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_INT8(-1, v->layouts[1].dir_sign);
    /* reverse shares the S/F line and has reversed sector order */
    TEST_ASSERT_EQUAL_DOUBLE(v->layouts[0].sf.p1.lat, v->layouts[1].sf.p1.lat);
    TEST_ASSERT_EQUAL_UINT8(v->layouts[0].n_sectors, v->layouts[1].n_sectors);
    TEST_ASSERT_EQUAL_DOUBLE(v->layouts[0].sectors[0].p1.lat, v->layouts[1].sectors[v->layouts[1].n_sectors - 1].p1.lat);
    TEST_ASSERT_TRUE(v->flags & TRK_F_UNVERIFIED);
}

static void test_nearest_inside_and_outside_radius(void)
{
    uint32_t d;
    const trk_venue_t *v = trk_find_nearest(-33.8567 + 0.01, 18.5170, &d);   /* ~1.1 km north */
    TEST_ASSERT_NOT_NULL(v); TEST_ASSERT_EQUAL_UINT16(6, v->id); TEST_ASSERT_UINT32_WITHIN(50, 1112, d);
    TEST_ASSERT_NULL(trk_find_nearest(-33.8567 + 0.03, 18.5170, &d));         /* ~3.3 km: outside 2 km radius */
}

static void test_user_venue_wins_on_id_clash_and_persists(void)
{
    trk_venue_t u; memset(&u, 0, sizeof u);
    u.id = 6; strcpy(u.name, "Killarney (mine)"); u.lat = -33.8567; u.lon = 18.5170; u.radius_m = 2000; u.n_layouts = 1;
    u.layouts[0].id = 1; strcpy(u.layouts[0].name, "L1"); u.layouts[0].dir_sign = 1;
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&u));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    uint8_t blob[16384]; size_t n;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob, sizeof blob, &n));
    trk_init();
    TEST_ASSERT_EQUAL_STRING("Killarney", trk_get(6)->name);
    TEST_ASSERT_EQUAL_INT(0, trk_user_load(blob, n));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    TEST_ASSERT_EQUAL_UINT16(1000, trk_next_user_id());
}

static void test_user_store_is_bounded(void)
{
    trk_venue_t u; memset(&u, 0, sizeof u); u.radius_m = 100; u.n_layouts = 1; u.layouts[0].dir_sign = 1;
    for (int i = 0; i < TRK_MAX_USER; i++) { u.id = (uint16_t)(1000 + i); TEST_ASSERT_EQUAL_INT(0, trk_user_add(&u)); }
    u.id = 1000 + TRK_MAX_USER;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&u));
    TEST_ASSERT_EQUAL_UINT16(1000 + TRK_MAX_USER, trk_next_user_id());
}

static void test_json_round_trip_with_same_and_reverse_expansion(void)
{
    const char *js =
        "{\"id\":1001,\"name\":\"Test\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"verified\":true,\"layouts\":["
        "{\"id\":1,\"name\":\"Full\",\"dir\":1,\"length_m\":2500,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],"
        "\"sectors\":[[[-26.002,28.001],[-26.002,28.0013]],[[-26.003,28.002],[-26.003,28.0023]]]},"
        "{\"id\":2,\"name\":\"Full Reverse\",\"dir\":-1,\"length_m\":2500,\"sf\":\"same\",\"sectors\":\"reverse\"}]}";
    trk_venue_t v; char err[64];
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(1001, v.id);
    TEST_ASSERT_FALSE(v.flags & TRK_F_UNVERIFIED);
    TEST_ASSERT_EQUAL_UINT8(2, v.n_layouts);
    TEST_ASSERT_EQUAL_DOUBLE(28.0003, v.layouts[1].sf.p2.lon);
    TEST_ASSERT_EQUAL_DOUBLE(-26.003, v.layouts[1].sectors[0].p1.lat);
    char out[2048];
    int n = trk_to_json(&v, out, sizeof out);
    TEST_ASSERT_GREATER_THAN(0, n);
    trk_venue_t v2;
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v2, out, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&v, &v2, sizeof v);
}

static void test_json_rejects_bad_line(void)
{
    const char *js = "{\"id\":1001,\"name\":\"T\",\"lat\":0,\"lon\":0,\"radius_m\":100,\"layouts\":[{\"id\":1,\"name\":\"L\",\"dir\":1,\"sf\":[[0,0]]}]}";
    trk_venue_t v; char err[64];
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, js, strlen(js), err, sizeof err));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bundled_contains_killarney_with_four_layouts);
    RUN_TEST(test_nearest_inside_and_outside_radius);
    RUN_TEST(test_user_venue_wins_on_id_clash_and_persists);
    RUN_TEST(test_user_store_is_bounded);
    RUN_TEST(test_json_round_trip_with_same_and_reverse_expansion);
    RUN_TEST(test_json_rejects_bad_line);
    return UNITY_END();
}
