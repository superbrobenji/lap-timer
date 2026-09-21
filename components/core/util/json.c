#include "core/json.h"
#include "core/core.h"
#include <string.h>
#include <stdlib.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions all report
 * JSON_ASSERT_CODE. They guard genuine anomalies -- NULL params, negative/impossible indices --
 * never the ordinary rejection of malformed/untrusted JSON, which stays a plain `return`/`false`
 * (that path is expected, exercised routinely by real uploads, and must not fire the fault hook). */
#define JSON_ASSERT_CODE 0x0AB0

int json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks)
{
    /* An empty document (n == 0) is untrusted input a peer can legitimately send -- an empty config
     * upload reaches cfg_from_json(json, 0) (T-A). Reject it as malformed (return < 1, so callers
     * report "malformed json") WITHOUT firing the fault hook, and BEFORE the js!=NULL invariant:
     * cfg_from_json's own precondition allows json==NULL when n==0. toks/max_toks are the caller's
     * own scratch buffer -- genuine invariants, so they stay assertions. */
    if (n == 0) return -1;
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, -1);
    CORE_ASSERT_RET(toks != NULL, JSON_ASSERT_CODE, -1);
    CORE_ASSERT_RET(max_toks > 0, JSON_ASSERT_CODE, -1);
    jsmn_parser p; jsmn_init(&p);
    int r = jsmn_parse(&p, js, n, toks, max_toks);
    if (r < 0) return -1;
    /* Depth from the parent links jsmn already maintains; bail out as soon as one chain is too long. */
    for (int i = 0; i < r; i++) {
        int d = 0;
        for (int par = toks[i].parent; par >= 0; par = toks[par].parent)
            if (++d > JSON_MAX_DEPTH) return -1;
    }
    return r;
}

bool json_tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(s != NULL, JSON_ASSERT_CODE, false);
    size_t len = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == len && memcmp(js + t->start, s, len) == 0;
}

int json_skip(const jsmntok_t *toks, int ntoks, int i)
{
    CORE_ASSERT_RET(toks != NULL, JSON_ASSERT_CODE, ntoks);
    CORE_ASSERT_RET(ntoks >= 0, JSON_ASSERT_CODE, ntoks);
    /* i out of [0,ntoks) is routine (a caller probing past the last sibling, or a truncated token
     * array) -- not an anomaly, so this stays a plain bounds check, not an assertion. */
    if (i < 0 || i >= ntoks) return ntoks;
    /* Every descendant of i starts before i ends and jsmn emits them contiguously, so a forward
     * scan finds the end of the subtree without recursion. Primitives and strings have no
     * descendants and the loop exits on the first test. */
    int j = i + 1;
    while (j < ntoks && toks[j].start < toks[i].end) j++;
    CORE_ASSERT_RET(j >= i, JSON_ASSERT_CODE, ntoks);   /* postcondition: the scan only ever advances */
    CORE_ASSERT_RET(j <= ntoks, JSON_ASSERT_CODE, ntoks); /* postcondition: the while's own bound */
    return j;
}

static size_t tok_copy(const char *js, const jsmntok_t *t, char *tmp, size_t cap)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(tmp != NULL || cap == 0, JSON_ASSERT_CODE, 0);
    if (cap == 0) return 0;                      /* a zero-capacity destination is a normal, tested call */
    size_t len = (size_t)(t->end - t->start);
    if (len >= cap) len = cap - 1;
    memcpy(tmp, js + t->start, len); tmp[len] = '\0';
    return len;
}

bool json_tok_int(const char *js, const jsmntok_t *t, int64_t *out)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, JSON_ASSERT_CODE, false);
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[32]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; long long v = strtoll(tmp, &end, 10);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_double(const char *js, const jsmntok_t *t, double *out)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, JSON_ASSERT_CODE, false);
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[48]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; double v = strtod(tmp, &end);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_bool(const char *js, const jsmntok_t *t, bool *out)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, false);
    CORE_ASSERT_RET(out != NULL, JSON_ASSERT_CODE, false);
    if (js[t->start] == 't') { *out = true; return true; }
    if (js[t->start] == 'f') { *out = false; return true; }
    return false;
}

size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(t != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(out != NULL || cap == 0, JSON_ASSERT_CODE, 0);
    /* jsmn leaves escapes in place; the config/track keys never contain escapes, so a plain copy suffices */
    size_t r = tok_copy(js, t, out, cap);
    CORE_ASSERT_RET(cap == 0 || r < cap, JSON_ASSERT_CODE, r); /* postcondition: room was left for the NUL */
    return r;
}

int json_obj_get(const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *key)
{
    CORE_ASSERT_RET(js != NULL, JSON_ASSERT_CODE, -1);
    CORE_ASSERT_RET(toks != NULL, JSON_ASSERT_CODE, -1);
    CORE_ASSERT_RET(key != NULL, JSON_ASSERT_CODE, -1);
    CORE_ASSERT_RET(ntoks >= 0, JSON_ASSERT_CODE, -1);
    if (obj < 0 || obj >= ntoks || toks[obj].type != JSMN_OBJECT) return -1;
    int i = obj + 1;                            /* first key token */
    for (int k = 0; k < toks[obj].size && i + 1 < ntoks; k++) {
        if (json_tok_eq(js, &toks[i], key)) return i + 1;
        i = json_skip(toks, ntoks, i + 1);      /* past this key's value subtree */
    }
    return -1;
}
