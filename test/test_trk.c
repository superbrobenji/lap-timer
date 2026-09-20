#include "unity.h"
#include "core/trk.h"
#include "core/ses.h"
#include "core/consts.h"
#include "core/json.h"
#include <stdio.h>
#include <string.h>

/* core_selftest runs every test/test_*.c on-target and Unity runs one test at a time, so this
 * suite's scratch fixtures are shared file-scope statics rather than per-test function-statics:
 * 15 separate function-static trk_venue_t plus several 16 KB/8 KB scratch arrays would otherwise
 * all land permanently in .bss (they did -- ~110 KB of it, overflowing dram0_0_seg). Each test
 * still memsets/re-populates its slice before reading it (mk_venue/mk_dirty_venue/trk_from_json
 * all memset their destination internally), so reuse is safe. Only two trk_venue_t slots are
 * needed: every test uses one except the JSON round-trip (input v + decoded v2) and the blob-v2
 * CRC test (the venue under test + a second "bad" venue injected into a copy of the blob) which
 * each need two live at once. s_blob/s_copy double as the two-buffer pair for the one test
 * (padding-garbage) that must compare two saved blobs simultaneously. s_toks backs the two tests
 * that call json_parse() directly on a maximal document. */
static trk_venue_t s_v, s_v2;
static uint8_t s_blob[16384];
static uint8_t s_copy[16384];
static char s_js[8192];
static jsmntok_t s_toks[1024];
static char s_out[2048];

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
    memset(&s_v, 0, sizeof s_v);
    s_v.id = 6; strcpy(s_v.name, "Killarney (mine)"); s_v.lat = -33.8567; s_v.lon = 18.5170; s_v.radius_m = 2000; s_v.n_layouts = 1;
    s_v.layouts[0].id = 1; strcpy(s_v.layouts[0].name, "L1"); s_v.layouts[0].dir_sign = 1;
    s_v.layouts[0].sf.p1.lat = -33.8567; s_v.layouts[0].sf.p1.lon = 18.5170;
    s_v.layouts[0].sf.p2.lat = -33.8567; s_v.layouts[0].sf.p2.lon = 18.5173;    /* a real S/F line, not the degenerate default */
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    size_t n;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_blob, sizeof s_blob, &n));
    trk_init();
    TEST_ASSERT_EQUAL_STRING("Killarney", trk_get(6)->name);
    TEST_ASSERT_EQUAL_INT(0, trk_user_load(s_blob, n));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    TEST_ASSERT_EQUAL_UINT16(1000, trk_next_user_id());
}

static void test_user_store_is_bounded(void)
{
    memset(&s_v, 0, sizeof s_v); s_v.radius_m = 100; s_v.n_layouts = 1;
    s_v.layouts[0].id = 1; s_v.layouts[0].dir_sign = 1;
    s_v.layouts[0].sf.p1.lat = -26.001; s_v.layouts[0].sf.p1.lon = 28.0;
    s_v.layouts[0].sf.p2.lat = -26.001; s_v.layouts[0].sf.p2.lon = 28.0003;     /* a real S/F line, not the degenerate default */
    for (int i = 0; i < TRK_MAX_USER; i++) { s_v.id = (uint16_t)(1000 + i); TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v)); }
    s_v.id = 1000 + TRK_MAX_USER;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    TEST_ASSERT_EQUAL_UINT16(1000 + TRK_MAX_USER, trk_next_user_id());
}

static void test_json_round_trip_with_same_and_reverse_expansion(void)
{
    const char *js =
        "{\"id\":1001,\"name\":\"Test\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"verified\":true,\"layouts\":["
        "{\"id\":1,\"name\":\"Full\",\"dir\":1,\"length_m\":2500,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],"
        "\"sectors\":[[[-26.002,28.001],[-26.002,28.0013]],[[-26.003,28.002],[-26.003,28.0023]]]},"
        "{\"id\":2,\"name\":\"Full Reverse\",\"dir\":-1,\"length_m\":2500,\"sf\":\"same\",\"sectors\":\"reverse\"}]}";
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&s_v, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(1001, s_v.id);
    TEST_ASSERT_FALSE(s_v.flags & TRK_F_UNVERIFIED);
    TEST_ASSERT_EQUAL_UINT8(2, s_v.n_layouts);
    TEST_ASSERT_EQUAL_DOUBLE(28.0003, s_v.layouts[1].sf.p2.lon);
    TEST_ASSERT_EQUAL_DOUBLE(-26.003, s_v.layouts[1].sectors[0].p1.lat);
    int n = trk_to_json(&s_v, s_out, sizeof s_out);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&s_v2, s_out, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&s_v, &s_v2, sizeof s_v);
}

static void test_json_rejects_bad_line(void)
{
    const char *js = "{\"id\":1001,\"name\":\"T\",\"lat\":0,\"lon\":0,\"radius_m\":100,\"layouts\":[{\"id\":1,\"name\":\"L\",\"dir\":1,\"sf\":[[0,0]]}]}";
    char err[64];
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, js, strlen(js), err, sizeof err));
}

/* a venue that passes trk_validate_venue; filled in place so the suite fits a 6 KB task stack */
static void mk_venue(trk_venue_t *v, uint16_t id)
{
    memset(v, 0, sizeof *v);
    v->id = id; strcpy(v->name, "User"); v->lat = -26.0; v->lon = 28.0; v->radius_m = 1500; v->n_layouts = 1;
    v->layouts[0].id = 1; strcpy(v->layouts[0].name, "Full"); v->layouts[0].dir_sign = 1;
    v->layouts[0].sf.p1.lat = -26.001; v->layouts[0].sf.p1.lon = 28.0;
    v->layouts[0].sf.p2.lat = -26.001; v->layouts[0].sf.p2.lon = 28.0003;
}

static void test_user_add_rejects_invalid_venue(void)
{
    mk_venue(&s_v, 1000);
    s_v.layouts[0].id = 0;                                         /* layout id must be non-zero */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());
    mk_venue(&s_v, 0);                                             /* venue id must be non-zero */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); s_v.radius_m = 50;                       /* radius out of range */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); s_v.n_layouts = TRK_MAX_LAYOUTS + 1;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); memset(s_v.name, 'x', sizeof s_v.name);  /* name not NUL-terminated */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); s_v.lat = 91.0;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); s_v.layouts[0].dir_sign = 0;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    mk_venue(&s_v, 1000); s_v.layouts[0].n_sectors = LAP_MAX_SECTORS + 1;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&s_v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                    /* nothing was stored */
    mk_venue(&s_v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
}

static void test_user_blob_v2_rejects_bad_version_count_crc_and_venue(void)
{
    mk_venue(&s_v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    size_t n;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_blob, sizeof s_blob, &n));
    TEST_ASSERT_EQUAL_UINT8(2, s_blob[0]);
    TEST_ASSERT_EQUAL_UINT(2 + sizeof(trk_venue_t) + 2, n);      /* version, count, venue, crc16 */

    memcpy(s_copy, s_blob, n);
    TEST_ASSERT_EQUAL_INT(0, trk_user_load(s_copy, n));          /* round trip */
    TEST_ASSERT_EQUAL_INT(1, trk_user_count());

    memcpy(s_copy, s_blob, n); s_copy[0] = 1;                    /* old version */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());

    memcpy(s_copy, s_blob, n); s_copy[1] = TRK_MAX_USER + 1;     /* count over capacity */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    memcpy(s_copy, s_blob, n);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n - 1));     /* size mismatch */
    memcpy(s_copy, s_blob, n); s_copy[40] ^= 0x01;               /* one flipped payload bit */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    memcpy(s_copy, s_blob, n); s_copy[n - 1] ^= 0x80;            /* flipped CRC byte */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());

    /* a structurally impossible venue (bit rot that survives no CRC, so re-CRC it) */
    mk_venue(&s_v2, 1000); s_v2.n_layouts = 200;
    memcpy(s_copy, s_blob, n);
    memcpy(s_copy + 2, &s_v2, sizeof s_v2);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));         /* CRC catches it first */
    trk_init();
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_blob, sizeof s_blob, &n));
    memcpy(s_copy, s_blob, n); memcpy(s_copy + 2, &s_v2, sizeof s_v2);
    uint16_t crc = ses_crc16(s_copy, n - 2);                      /* recompute so only validation can reject */
    s_copy[n - 2] = (uint8_t)crc; s_copy[n - 1] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                  /* store left empty */
    TEST_ASSERT_NULL(trk_get(1000));
}

static void test_user_blob_rejects_sub_metre_gate_line(void)
{
    mk_venue(&s_v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    size_t n;                                                    /* one venue's worth, not the 16 KB multi-venue headroom */
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_blob, sizeof(trk_venue_t) + 16, &n));

    /* structurally valid except the S/F line is under the 1 m gate rule (trk_from_json's rule,
     * shared via trk_validate_venue -- the loader must enforce it too). s_v is done being read
     * by trk_user_save above, so it is reused here instead of a second venue slot. */
    mk_venue(&s_v, 1000);
    s_v.layouts[0].sf.p2.lat = s_v.layouts[0].sf.p1.lat;
    s_v.layouts[0].sf.p2.lon = s_v.layouts[0].sf.p1.lon;
    memcpy(s_copy, s_blob, n);
    memcpy(s_copy + 2, &s_v, sizeof s_v);
    uint16_t crc = ses_crc16(s_copy, n - 2);                      /* recompute so only validation can reject */
    s_copy[n - 2] = (uint8_t)crc; s_copy[n - 1] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(s_copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());
    TEST_ASSERT_NULL(trk_get(1000));
}

/* Same named fields as mk_venue, built on top of a struct pre-filled with `fill` so any byte the
 * assignments below do not touch (compiler padding between fields) keeps the fill pattern instead
 * of being zero, unlike mk_venue which memsets to 0 first. */
static void mk_dirty_venue(trk_venue_t *v, uint16_t id, uint8_t fill)
{
    memset(v, (int)fill, sizeof *v);
    v->id = id;
    memset(v->name, 0, sizeof v->name); strcpy(v->name, "User");
    v->lat = -26.0; v->lon = 28.0; v->radius_m = 1500; v->flags = 0; v->n_layouts = 1;
    trk_layout_t *L = &v->layouts[0];
    L->id = 1;
    memset(L->name, 0, sizeof L->name); strcpy(L->name, "Full");
    L->dir_sign = 1; L->n_sectors = 0; L->length_m = 0;
    L->sf.p1.lat = -26.001; L->sf.p1.lon = 28.0;
    L->sf.p2.lat = -26.001; L->sf.p2.lon = 28.0003;
}

static void test_save_produces_identical_blobs_regardless_of_padding_garbage(void)
{
    /* a and b are used one at a time (never simultaneously live), so one shared venue slot -- kept
     * off the stack for the same 6 KB task-stack reason as the rest of this suite -- is reused for
     * both fill patterns instead of allocating two. blob_a/blob_b, however, must stay live together
     * for the final comparison, so they borrow the s_blob/s_copy pair instead of a third buffer. */
    trk_init();
    mk_dirty_venue(&s_v, 1000, 0xAA);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));          /* struct assignment carries v's padding into the store */
    size_t n_a;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_blob, sizeof(trk_venue_t) + 16, &n_a));

    trk_init();
    mk_dirty_venue(&s_v, 1000, 0x55);                       /* same fields, different padding garbage */
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    size_t n_b;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(s_copy, sizeof(trk_venue_t) + 16, &n_b));

    TEST_ASSERT_EQUAL_UINT(n_a, n_b);
    TEST_ASSERT_EQUAL_MEMORY(s_blob, s_copy, n_a);          /* including the CRC: identical bytes throughout */
}

static void test_json_rejects_degenerate_line_and_duplicate_layout_ids(void)
{
    char err[64];
    /* the two S/F endpoints are the same point */
    const char *same_pt =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.0,28.0],[-26.0,28.0]]}]}";
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, same_pt, strlen(same_pt), err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    /* 0.9 m apart: still under MIN_GATE_LEN_M */
    const char *too_short =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.0,28.0],[-26.0000081,28.0]]}]}";
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, too_short, strlen(too_short), err, sizeof err));
    /* two layouts sharing an id */
    const char *dup =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]]},"
        "{\"id\":1,\"name\":\"R\",\"dir\":-1,\"sf\":\"same\"}]}";
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, dup, strlen(dup), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("duplicate layout id", err);
}

static void test_json_rejects_document_deeper_than_the_depth_cap(void)
{
    int p = 0;
    p += snprintf(s_js + p, sizeof s_js - (size_t)p, "{\"z\":");
    for (int i = 0; i < 40; i++) s_js[p++] = '[';
    for (int i = 0; i < 40; i++) s_js[p++] = ']';
    p += snprintf(s_js + p, sizeof s_js - (size_t)p, ",\"id\":1000,\"name\":\"X\",\"lat\":-26.0,\"lon\":28.0,"
                  "\"radius_m\":1500,\"layouts\":[{\"id\":1,\"name\":\"F\",\"dir\":1,"
                  "\"sf\":[[-26.001,28.0],[-26.001,28.0003]]}]}");
    char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, s_js, (size_t)p, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* Emit a syntactically valid venue JSON with `n_layouts` layouts, each carrying `n_sec` sector gates,
 * every optional field present and full (non-shortcut) sf + sector lines -- i.e. the token-maximal
 * shape for a given (layouts, sectors). Coordinates are distinct and >= MIN_GATE_LEN_M apart so the
 * document also passes trk_validate_venue whenever (n_layouts, n_sec) are within spec. Returns the
 * byte length written. Verify-first support for plan Task B1 (toks[] sizing). */
static int emit_venue_json(char *b, size_t cap, int n_layouts, int n_sec)
{
    int p = 0;
    p += snprintf(b + p, cap - (size_t)p,
        "{\"id\":65535,\"name\":\"MaxVenue\",\"lat\":-26.0,\"lon\":28.0,"
        "\"radius_m\":50000,\"verified\":true,\"layouts\":[");
    for (int k = 0; k < n_layouts; k++) {
        p += snprintf(b + p, cap - (size_t)p,
            "%s{\"id\":%d,\"name\":\"L\",\"dir\":%d,\"length_m\":9999,"
            "\"sf\":[[-26.0,28.%04d],[-26.0,28.%04d]],\"sectors\":[",
            k ? "," : "", k + 1, (k % 2) ? -1 : 1, 1000 + k, 1003 + k);
        for (int s = 0; s < n_sec; s++) {
            int g = k * LAP_MAX_SECTORS + s;                 /* unique per (layout, sector) */
            p += snprintf(b + p, cap - (size_t)p,
                "%s[[-26.%04d,28.0],[-26.%04d,28.0003]]", s ? "," : "", 1000 + g, 1000 + g);
        }
        p += snprintf(b + p, cap - (size_t)p, "]}");
    }
    p += snprintf(b + p, cap - (size_t)p, "]}");
    return p;
}

/* Task B1 verify-first: the maximal legal venue (TRK_MAX_LAYOUTS x LAP_MAX_SECTORS, all fields)
 * tokenises to exactly 615 jsmn tokens -- more than trk_from_json's MAX_TOKS(512) scratch buffer --
 * so it is already rejected today at the json_parse stage, cleanly (return -1, err set, no crash;
 * this suite runs under ASan/UBSan). This locks the computed worst-case bound and the clean-rejection
 * behaviour: shrinking MAX_TOKS below 512 would fail this, and so would a grammar change that pushes
 * the true bound off 615. */
static void test_json_max_venue_token_bound(void)
{
    int n = emit_venue_json(s_js, sizeof s_js, TRK_MAX_LAYOUTS, LAP_MAX_SECTORS);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    int cnt = json_parse(s_js, (size_t)n, s_toks, 1024);
    TEST_ASSERT_EQUAL_INT(615, cnt);                 /* the verified worst-case token count */
    TEST_ASSERT_GREATER_THAN_INT(512, cnt);          /* ... which exceeds MAX_TOKS */

    char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&s_v, s_js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("malformed json", err);   /* JSMN_ERROR_NOMEM path, not a fault */
}

/* The success side of the 512-token boundary: a large but sub-cap legal venue
 * (TRK_MAX_LAYOUTS x 6 sectors = 503 tokens) still tokenises under MAX_TOKS and parses successfully,
 * confirming reclaim 0 does not regress any venue that works today. */
static void test_json_large_venue_within_cap_parses(void)
{
    int n = emit_venue_json(s_js, sizeof s_js, TRK_MAX_LAYOUTS, 6);
    int cnt = json_parse(s_js, (size_t)n, s_toks, 1024);
    TEST_ASSERT_GREATER_THAN_INT(0, cnt);
    TEST_ASSERT_LESS_OR_EQUAL_INT(512, cnt);         /* fits the current scratch buffer */

    char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&s_v, s_js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(TRK_MAX_LAYOUTS, s_v.n_layouts);
    TEST_ASSERT_EQUAL_UINT8(6, s_v.layouts[0].n_sectors);
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
    RUN_TEST(test_user_add_rejects_invalid_venue);
    RUN_TEST(test_user_blob_v2_rejects_bad_version_count_crc_and_venue);
    RUN_TEST(test_user_blob_rejects_sub_metre_gate_line);
    RUN_TEST(test_save_produces_identical_blobs_regardless_of_padding_garbage);
    RUN_TEST(test_json_rejects_degenerate_line_and_duplicate_layout_ids);
    RUN_TEST(test_json_rejects_document_deeper_than_the_depth_cap);
    RUN_TEST(test_json_max_venue_token_bound);
    RUN_TEST(test_json_large_venue_within_cap_parses);
    return UNITY_END();
}
