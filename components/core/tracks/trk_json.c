#include "core/trk.h"
#include "core/json.h"
#include "core/jw.h"
#include "core/core.h"
#include <string.h>
#include <stdio.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions report TRK_ASSERT_CODE
 * (shared with trk.c: components/core/tracks is one assertion "module" for the retrofit). They
 * guard genuine anomalies -- NULL params, an internal count exceeding its TRK_MAX_ or LAP_MAX_
 * bound -- never the rejection of a malformed/untrusted track document, which stays the existing
 * plain `fail(...)` return (that path is routine, exercised by real uploads, and must not fire the
 * fault hook). */
#define TRK_ASSERT_CODE 0x0A80

/* Scratch jsmn-token budget for parsing ONE incoming custom-venue JSON upload (20 B/token with
 * JSMN_PARENT_LINKS -> 10,240 B static). Verify-first sizing (plan 2026-09-20, Task B1): the
 * maximal legal venue under the current spec -- TRK_MAX_LAYOUTS(8) layouts x LAP_MAX_SECTORS(8)
 * sectors, every field present, full sf + sector lines (the "same"/"reverse" shortcuts tokenise to
 * FEWER tokens) -- tokenises to exactly 615 jsmn tokens (host-verified against the real jsmn.c /
 * json_parse; regression-locked in test/test_trk.c test_json_max_venue_token_bound). 512 is
 * therefore ALREADY below that worst case: a truly maximal upload is rejected today at json_parse
 * (JSMN_ERROR_NOMEM -> "malformed json", a clean fail(), never a crash). It is deliberately NOT
 * shrunk here (reclaim 0): a smaller cap would reject yet more legal venues. Growing it to >=615 to
 * accept the maximal upload is a separate DRAM-cost decision, out of scope for the reclaim plan and
 * filed for a spec-reconciliation ticket. */
#define MAX_TOKS 512

static int fail(char *err, size_t cap, const char *m) { if (err && cap) { strncpy(err, m, cap - 1); err[cap - 1] = '\0'; } return -1; }

static bool get_pt(const char *js, const jsmntok_t *toks, int ntoks, int arr, trk_pt_t *out)
{
    CORE_ASSERT_RET(js != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(toks != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(ntoks >= 0, TRK_ASSERT_CODE, false);
    if (arr < 0 || arr + 2 >= ntoks || toks[arr].type != JSMN_ARRAY || toks[arr].size != 2) return false;
    return json_tok_double(js, &toks[arr + 1], &out->lat) && json_tok_double(js, &toks[arr + 2], &out->lon);
}
static bool get_line(const char *js, const jsmntok_t *toks, int ntoks, int arr, trk_line_t *out)
{
    CORE_ASSERT_RET(js != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(toks != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(ntoks >= 0, TRK_ASSERT_CODE, false);
    if (arr < 0 || arr >= ntoks || toks[arr].type != JSMN_ARRAY || toks[arr].size != 2) return false;
    int p1 = arr + 1, p2 = json_skip(toks, ntoks, p1);
    /* Degenerate/too-short lines (§6.4 needs a gate direction) are rejected once, by the shared
     * trk_validate_venue() call trk_from_json makes at the end -- not duplicated here. */
    return get_pt(js, toks, ntoks, p1, &out->p1) && get_pt(js, toks, ntoks, p2, &out->p2);
}

/* Not reentrant: static token array (single caller task). */
int trk_from_json(trk_venue_t *v, const char *json, size_t n, char *err, size_t err_cap)
{
    CORE_ASSERT_RET(v != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(json != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(n > 0, TRK_ASSERT_CODE, fail(err, err_cap, "malformed json"));
    static jsmntok_t toks[MAX_TOKS];
    int cnt = json_parse(json, n, toks, MAX_TOKS);
    if (cnt < 1 || toks[0].type != JSMN_OBJECT) return fail(err, err_cap, "malformed json");
    memset(v, 0, sizeof *v);
    int t; int64_t iv; bool bv;
    if ((t = json_obj_get(json, toks, cnt, 0, "id")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 1 || iv > 65535) return fail(err, err_cap, "id");
    v->id = (uint16_t)iv;
    if ((t = json_obj_get(json, toks, cnt, 0, "name")) < 0) return fail(err, err_cap, "name");
    json_tok_str(json, &toks[t], v->name, sizeof v->name);
    if ((t = json_obj_get(json, toks, cnt, 0, "lat")) < 0 || !json_tok_double(json, &toks[t], &v->lat)) return fail(err, err_cap, "lat");
    if ((t = json_obj_get(json, toks, cnt, 0, "lon")) < 0 || !json_tok_double(json, &toks[t], &v->lon)) return fail(err, err_cap, "lon");
    if ((t = json_obj_get(json, toks, cnt, 0, "radius_m")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 100 || iv > 50000) return fail(err, err_cap, "radius_m");
    v->radius_m = (uint32_t)iv;
    v->flags = TRK_F_UNVERIFIED;
    if ((t = json_obj_get(json, toks, cnt, 0, "verified")) >= 0 && json_tok_bool(json, &toks[t], &bv) && bv) v->flags = 0;
    int la = json_obj_get(json, toks, cnt, 0, "layouts");
    if (la < 0 || toks[la].type != JSMN_ARRAY || toks[la].size < 1 || toks[la].size > TRK_MAX_LAYOUTS) return fail(err, err_cap, "layouts");
    int li = la + 1;
    for (int k = 0; k < toks[la].size; k++) {
        trk_layout_t *L = &v->layouts[k];
        if (toks[li].type != JSMN_OBJECT) return fail(err, err_cap, "layout object");
        if ((t = json_obj_get(json, toks, cnt, li, "id")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 1 || iv > 65535) return fail(err, err_cap, "layout id");
        for (int prev = 0; prev < k; prev++) if (v->layouts[prev].id == (uint16_t)iv) return fail(err, err_cap, "duplicate layout id");
        L->id = (uint16_t)iv;
        if ((t = json_obj_get(json, toks, cnt, li, "name")) < 0) return fail(err, err_cap, "layout name");
        json_tok_str(json, &toks[t], L->name, sizeof L->name);
        if ((t = json_obj_get(json, toks, cnt, li, "dir")) < 0 || !json_tok_int(json, &toks[t], &iv) || (iv != 1 && iv != -1)) return fail(err, err_cap, "dir");
        L->dir_sign = (int8_t)iv;
        if ((t = json_obj_get(json, toks, cnt, li, "length_m")) >= 0 && json_tok_int(json, &toks[t], &iv) && iv >= 0) L->length_m = (uint32_t)iv;
        t = json_obj_get(json, toks, cnt, li, "sf");
        if (t < 0) return fail(err, err_cap, "sf");
        if (json_tok_eq(json, &toks[t], "same")) { if (k == 0) return fail(err, err_cap, "sf same on first"); L->sf = v->layouts[0].sf; }
        else if (!get_line(json, toks, cnt, t, &L->sf)) return fail(err, err_cap, "sf line");
        t = json_obj_get(json, toks, cnt, li, "sectors");
        if (t < 0) { L->n_sectors = 0; }
        else if (json_tok_eq(json, &toks[t], "reverse")) {
            if (k == 0) return fail(err, err_cap, "sectors reverse on first");
            L->n_sectors = v->layouts[0].n_sectors;
            for (uint8_t s = 0; s < L->n_sectors; s++) L->sectors[s] = v->layouts[0].sectors[L->n_sectors - 1 - s];
        } else {
            if (toks[t].type != JSMN_ARRAY || toks[t].size > LAP_MAX_SECTORS) return fail(err, err_cap, "sectors");
            L->n_sectors = (uint8_t)toks[t].size;
            int si = t + 1;
            for (uint8_t s = 0; s < L->n_sectors; s++) { if (!get_line(json, toks, cnt, si, &L->sectors[s])) return fail(err, err_cap, "sector line"); si = json_skip(toks, cnt, si); }
        }
        v->n_layouts++;
        li = json_skip(toks, cnt, li);
    }
    CORE_ASSERT_RET(v->n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE, -1); /* the layouts[] array's own bound */
    if (trk_validate_venue(v) != 0) { memset(v, 0, sizeof *v); return fail(err, err_cap, "invalid venue"); }
    return 0;
}

static void put_pt(jw_t *w, const trk_pt_t *p)
{
    CORE_ASSERT_VOID(w != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(p != NULL, TRK_ASSERT_CODE);
    jw_arr_open(w); jw_double(w, p->lat, 7); jw_double(w, p->lon, 7); jw_arr_close(w);
}
static void put_line(jw_t *w, const trk_line_t *l)
{
    CORE_ASSERT_VOID(w != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(l != NULL, TRK_ASSERT_CODE);
    jw_arr_open(w); put_pt(w, &l->p1); put_pt(w, &l->p2); jw_arr_close(w);
}

int trk_to_json(const trk_venue_t *v, char *out, size_t cap)
{
    CORE_ASSERT_RET(v != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(out != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(v->n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE, -1); /* the layouts[] array's own bound */
    jw_t w; jw_init(&w, out, cap);
    jw_obj_open(&w);
    jw_key(&w, "id"); jw_uint(&w, v->id);
    jw_key(&w, "name"); jw_str(&w, v->name);
    jw_key(&w, "lat"); jw_double(&w, v->lat, 7);
    jw_key(&w, "lon"); jw_double(&w, v->lon, 7);
    jw_key(&w, "radius_m"); jw_uint(&w, v->radius_m);
    jw_key(&w, "verified"); jw_bool(&w, !(v->flags & TRK_F_UNVERIFIED));
    jw_key(&w, "layouts"); jw_arr_open(&w);
    for (uint8_t k = 0; k < v->n_layouts; k++) {
        const trk_layout_t *L = &v->layouts[k];
        CORE_ASSERT_RET(L->n_sectors <= LAP_MAX_SECTORS, TRK_ASSERT_CODE, -1); /* the sectors[] array's own bound */
        jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, L->id);
        jw_key(&w, "name"); jw_str(&w, L->name);
        jw_key(&w, "dir"); jw_int(&w, L->dir_sign);
        jw_key(&w, "length_m"); jw_uint(&w, L->length_m);
        jw_key(&w, "sf"); put_line(&w, &L->sf);
        jw_key(&w, "sectors"); jw_arr_open(&w);
        for (uint8_t s = 0; s < L->n_sectors; s++) put_line(&w, &L->sectors[s]);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    return jw_overflow(&w) ? -1 : (int)jw_len(&w);
}
