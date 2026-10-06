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

static void test_user_add_json_parses_into_a_slot(void)
{
    const char *json = "{\"id\":9001,\"name\":\"SIMTRACK\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":[{\"id\":1,\"name\":\"FULL\","
                       "\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],\"sectors\":[]}]}";
    uint16_t id = 0; char err[48] = {0};
    TEST_ASSERT_EQUAL_INT(0, trk_user_add_json(json, strlen(json), &id, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(9001, id);
    const trk_venue_t *v = trk_get(9001);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("SIMTRACK", v->name);
    TEST_ASSERT_EQUAL_UINT8(1, v->n_layouts);
}
static void test_user_add_json_rejects_malformed(void)
{
    uint16_t id = 7; char err[48] = {0};
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add_json("{\"id\":", 6, &id, err, sizeof err));
    TEST_ASSERT_TRUE(err[0] != '\0');
}

static void test_user_add_json_replaces_same_id(void)
{
    /* Ambiguity (1) from the Plan 7 T1 brief: a second trk_user_add_json parse carrying an id
     * already present in the user table replaces that entry in place -- same semantics as
     * trk_user_add -- rather than appending a second, stale copy. */
    const char *json1 = "{\"id\":9002,\"name\":\"FIRST\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
                        "{\"id\":1,\"name\":\"FULL\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],\"sectors\":[]}]}";
    const char *json2 = "{\"id\":9002,\"name\":\"SECOND\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
                        "{\"id\":1,\"name\":\"FULL\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]]},"
                        "{\"id\":2,\"name\":\"HALF\",\"dir\":-1,\"sf\":\"same\"}]}";
    uint16_t id1 = 0, id2 = 0; char err[48] = {0};
    TEST_ASSERT_EQUAL_INT(0, trk_user_add_json(json1, strlen(json1), &id1, err, sizeof err));
    int count_after_first = trk_user_count();
    TEST_ASSERT_EQUAL_INT(0, trk_user_add_json(json2, strlen(json2), &id2, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(9002, id1);
    TEST_ASSERT_EQUAL_UINT16(9002, id2);
    TEST_ASSERT_EQUAL_INT(count_after_first, trk_user_count());   /* replaced in place, not appended */
    const trk_venue_t *v = trk_get(9002);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("SECOND", v->name);
    TEST_ASSERT_EQUAL_UINT8(2, v->n_layouts);
}

/* Fix round 2 (finding 10c): once the user table is full, trk_user_add_json() returns the
 * documented error ("user table full", the same message trk_user_add's own full-table path
 * uses -- fail()'s single call site the two share) and leaves the table exactly as it was: same
 * count, every already-added entry untouched, the overflow id absent, and *venue_id_out left
 * alone (user_slot_for_parse() returns NULL before the parse -- or any write -- ever runs). */
static void test_user_add_json_full_table_rejected_and_unchanged(void)
{
    char json[256];
    uint16_t id = 0; char err[48];
    for (int i = 0; i < TRK_MAX_USER; i++) {
        int n = snprintf(json, sizeof json,
            "{\"id\":%d,\"name\":\"V%d\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
            "{\"id\":1,\"name\":\"FULL\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],\"sectors\":[]}]}",
            9100 + i, 9100 + i);
        err[0] = '\0';
        TEST_ASSERT_EQUAL_INT(0, trk_user_add_json(json, (size_t)n, &id, err, sizeof err));
    }
    TEST_ASSERT_EQUAL_INT(TRK_MAX_USER, trk_user_count());

    int n = snprintf(json, sizeof json,
        "{\"id\":9200,\"name\":\"OVERFLOW\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"FULL\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],\"sectors\":[]}]}");
    err[0] = '\0';
    id = 4242;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add_json(json, (size_t)n, &id, err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("user table full", err);
    TEST_ASSERT_EQUAL_UINT16(4242, id);                        /* venue_id_out untouched on failure */
    TEST_ASSERT_EQUAL_INT(TRK_MAX_USER, trk_user_count());
    TEST_ASSERT_NULL(trk_get(9200));
    for (int i = 0; i < TRK_MAX_USER; i++) {
        const trk_venue_t *v = trk_get((uint16_t)(9100 + i));
        TEST_ASSERT_NOT_NULL(v);
        char want[8]; snprintf(want, sizeof want, "V%d", 9100 + i);
        TEST_ASSERT_EQUAL_STRING(want, v->name);
    }
}

/* ---------------------------------------------------- #97 (§10.9), review fix round 1: M7
 * per-venue record API (trk_user_save_venue/trk_user_load_venue, core/trk.h) -- the logger's
 * actual persistence format since T5-R4. The whole-table trk_user_save/trk_user_load tests above
 * only ever exercised ONE venue against a 16 KB buffer, never the logger's real 3840 B
 * (BATCH_CAP) scratch batch -- exactly why the whole-table blob's cap defect (2 + n *
 * sizeof(trk_venue_t) + 2 overflowing BATCH_CAP at just 2 venues) went unnoticed. These tests
 * drive the real invariant instead: every record stays within TRK_USER_REC_MAX (the bound
 * logger.c's own _Static_assert checks against BATCH_CAP) for 1..TRK_MAX_USER venues, a save-then-
 * later-load round trip (not a same-pass one, matching the logger's own boot-vs-session lifecycle)
 * preserves every venue, and ids never collide after a reload. */

/* Fills v->layouts[0..n_layouts) with n_sectors sectors each, reusing v->layouts[0]'s S/F line
 * shape (already set by mk_venue) for every layout -- good enough for a structurally-valid,
 * serialisable venue; this suite does not care whether the gates are physically sensible, only
 * whether the record format round-trips every field. */
static void fill_layouts(trk_venue_t *v, uint8_t n_layouts, uint8_t n_sectors)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(TRK_MAX_LAYOUTS, n_layouts);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(LAP_MAX_SECTORS, n_sectors);
    v->n_layouts = n_layouts;
    for (uint8_t i = 0; i < n_layouts; i++) {
        trk_layout_t *L = &v->layouts[i];
        *L = v->layouts[0];
        L->id        = (uint16_t)(i + 1);
        L->n_sectors = n_sectors;
        for (uint8_t s = 0; s < n_sectors; s++) {
            L->sectors[s].p1.lat = -26.002 - (double)s * 0.001; L->sectors[s].p1.lon = 28.0;
            L->sectors[s].p2.lat = -26.002 - (double)s * 0.001; L->sectors[s].p2.lon = 28.0003;
        }
    }
}

/* 1..TRK_MAX_USER single-layout venues: save every record (s_blob holds all of them at once,
 * TRK_MAX_USER * TRK_USER_REC_MAX well within its 16 KB), trk_init() (drop the live table, as a
 * reboot would), then load every record back and confirm the table matches -- and that
 * trk_next_user_id() picks up past every reloaded id, never reusing one. */
static void test_user_save_venue_load_venue_round_trip_1_to_max(void)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof s_blob, (size_t)TRK_MAX_USER * TRK_USER_REC_MAX);
    size_t lens[TRK_MAX_USER];
    for (uint8_t count = 1; count <= TRK_MAX_USER; count++) {
        trk_init();
        for (uint8_t i = 0; i < count; i++) {
            mk_venue(&s_v, (uint16_t)(2000 + i));
            TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
        }
        TEST_ASSERT_EQUAL_INT(count, trk_user_count());
        for (uint8_t i = 0; i < count; i++) {
            size_t n = 0;
            TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(i, s_blob + (size_t)i * TRK_USER_REC_MAX,
                                                          TRK_USER_REC_MAX, &n));
            TEST_ASSERT_TRUE(n <= TRK_USER_REC_MAX);
            lens[i] = n;
        }

        trk_init();
        TEST_ASSERT_EQUAL_INT(0, trk_user_count());
        for (uint8_t i = 0; i < count; i++) {
            TEST_ASSERT_EQUAL_INT(0, trk_user_load_venue(s_blob + (size_t)i * TRK_USER_REC_MAX, lens[i]));
        }
        TEST_ASSERT_EQUAL_INT(count, trk_user_count());
        for (uint8_t i = 0; i < count; i++) {
            const trk_venue_t *v = trk_get((uint16_t)(2000 + i));
            TEST_ASSERT_NOT_NULL(v);
            TEST_ASSERT_EQUAL_STRING("User", v->name);
            TEST_ASSERT_EQUAL_UINT8(1, v->n_layouts);
        }
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(2000 + count), trk_next_user_id());   /* no id reuse */
    }
}

/* The literal worst case trk_user_save_venue()'s TRK_USER_REC_MAX bound is computed for: every
 * layout slot full, TRK_MAX_LAYOUTS layouts x LAP_MAX_SECTORS sectors each. Asserts EXACT equality
 * (not just <=) -- the encoder never pads or wastes a byte, so this is the strongest proof the
 * bound is both sufficient and tight, not just generously oversized. */
static void test_user_save_venue_max_size_is_exactly_rec_max(void)
{
    trk_init();
    mk_venue(&s_v, 3000);
    fill_layouts(&s_v, TRK_MAX_LAYOUTS, LAP_MAX_SECTORS);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));

    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    TEST_ASSERT_EQUAL_UINT(TRK_USER_REC_MAX, n);
    size_t n_short = 0;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_save_venue(0, s_blob, n - 1, &n_short));   /* one byte short: refused */

    trk_init();
    TEST_ASSERT_EQUAL_INT(0, trk_user_load_venue(s_blob, n));
    const trk_venue_t *v = trk_get(3000);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT8(TRK_MAX_LAYOUTS, v->n_layouts);
    TEST_ASSERT_EQUAL_UINT8(LAP_MAX_SECTORS, v->layouts[TRK_MAX_LAYOUTS - 1].n_sectors);
}

/* The review's literal example: a 4-layout, 8-sector venue round-trips (a realistic multi-layout
 * upload, short of the absolute TRK_MAX_LAYOUTS worst case the test above already covers). */
static void test_user_save_venue_four_layouts_eight_sectors_round_trip(void)
{
    trk_init();
    mk_venue(&s_v, 5000);
    fill_layouts(&s_v, 4, LAP_MAX_SECTORS);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));

    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    TEST_ASSERT_TRUE(n <= TRK_USER_REC_MAX);

    trk_init();
    TEST_ASSERT_EQUAL_INT(0, trk_user_load_venue(s_blob, n));
    const trk_venue_t *v = trk_get(5000);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT8(4, v->n_layouts);
    for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT8(LAP_MAX_SECTORS, v->layouts[i].n_sectors);
}

/* T5-R4 refinement 3: a truncated/malformed record is refused cleanly (nothing partially
 * installed), and does not disturb an already-loaded venue or block loading a later, valid one --
 * the core-level primitive the logger's tracks_load_record() "skip just this record, keep
 * reading" behaviour (components/app/logger/logger.c) relies on. That loop itself is firmware-
 * only (FreeRTOS/hal/storage.h) and not host-testable; this is the pure-core proof underneath it,
 * documented as such in the Task 5 fix-round-1 report rather than left unverified. */
static void test_user_load_venue_rejects_truncated_record(void)
{
    trk_init();
    mk_venue(&s_v, 4000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&s_v));
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));

    trk_init();
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, n - 1));   /* one byte short */
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                     /* nothing installed */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, 0));       /* empty */
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());

    /* the truncated attempts above left the table untouched -- a second, valid record still loads */
    TEST_ASSERT_EQUAL_INT(0, trk_user_load_venue(s_blob, n));
    TEST_ASSERT_EQUAL_INT(1, trk_user_count());
    TEST_ASSERT_NOT_NULL(trk_get(4000));
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
    RUN_TEST(test_user_add_json_parses_into_a_slot);
    RUN_TEST(test_user_add_json_rejects_malformed);
    RUN_TEST(test_user_add_json_replaces_same_id);
    RUN_TEST(test_user_add_json_full_table_rejected_and_unchanged);
    RUN_TEST(test_user_save_venue_load_venue_round_trip_1_to_max);
    RUN_TEST(test_user_save_venue_max_size_is_exactly_rec_max);
    RUN_TEST(test_user_save_venue_four_layouts_eight_sectors_round_trip);
    RUN_TEST(test_user_load_venue_rejects_truncated_record);
    return UNITY_END();
}
