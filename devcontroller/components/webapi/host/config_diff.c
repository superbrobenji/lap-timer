/* config_diff.c -- pure config-diff/minify (Plan 5.5 Task 4 Step 1/3). See config_diff.h.
 * jsmn is compiled from the vendored local copy (webapi/host/jsmn.c) so this needs no
 * `REQUIRES core` (which would glob the whole lap-timer app/driver tree); here we take the
 * declarations only (JSMN_HEADER) and link the implementation from jsmn.c. The JSMN_* feature
 * macros MUST match jsmn.c so the jsmntok_t layout (JSMN_PARENT_LINKS adds `parent`) agrees. */
#include "config_diff.h"

#include <assert.h>
#include <string.h>

#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#define JSMN_HEADER            /* declarations only; the implementation is jsmn.c */
#include "jsmn.h"

/* Largest JSON document handled (config get is ~939 B; leave headroom). */
#define CFG_JSON_MAX 2048
/* Token budget for one document (each key+value is >=2 tokens). */
#define CFG_MAXTOK   256

/* Single-flight scratch (POST /api/config is serialized by linkhost's one-request-in-flight
 * mutex; the host tests run sequentially). Keeps large buffers off the httpd task stack. */
static jsmntok_t s_ctok[CFG_MAXTOK];
static jsmntok_t s_dtok[CFG_MAXTOK];
static char      s_va[CFG_JSON_MAX];   /* minified current value */
static char      s_vb[CFG_JSON_MAX];   /* minified desired value */

/* Copies src[start,end) to dst stripping whitespace that lies outside JSON strings. Returns 0
 * and sets *outlen, or -1 if dst overflows. */
static int json_minify(const char *src, int start, int end, char *dst, size_t cap, size_t *outlen)
{
    assert(src != NULL);
    assert(dst != NULL);
    size_t o = 0;
    int in_str = 0;
    for (int i = start; i < end; i++) {           /* bounded by end-start */
        char c = src[i];
        if (in_str) {
            if (o >= cap) return -1;
            dst[o++] = c;
            if (c == '\\') {                       /* copy the escaped char verbatim */
                i++;
                if (i < end) { if (o >= cap) return -1; dst[o++] = src[i]; }
            } else if (c == '"') {
                in_str = 0;
            }
        } else if (c == '"') {
            if (o >= cap) return -1;
            dst[o++] = c;
            in_str = 1;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            /* insignificant whitespace: drop */
        } else {
            if (o >= cap) return -1;
            dst[o++] = c;
        }
    }
    *outlen = o;
    return 0;
}

/* Writes the minified JSON representation of value token t (quotes re-added for strings) to dst.
 * Returns 0 and sets *outlen, or -1 on overflow. */
static int value_repr(const char *js, const jsmntok_t *t, char *dst, size_t cap, size_t *outlen)
{
    assert(js != NULL);
    assert(t != NULL);
    if (t->type == JSMN_STRING) {
        size_t need = (size_t)(t->end - t->start) + 2u;
        if (need > cap) return -1;
        size_t o = 0;
        dst[o++] = '"';
        for (int i = t->start; i < t->end; i++) dst[o++] = js[i];   /* bounded */
        dst[o++] = '"';
        *outlen = o;
        return 0;
    }
    return json_minify(js, t->start, t->end, dst, cap, outlen);
}

/* Returns the token index of the value of top-level key (dkey) within the current document, or
 * -1 if the key is absent. */
static int find_value(const char *cjs, const jsmntok_t *ct, int cn,
                      const char *djs, const jsmntok_t *dkey)
{
    int klen = dkey->end - dkey->start;
    for (int j = 1; j + 1 < cn; j++) {            /* bounded by cn */
        if (ct[j].parent != 0 || ct[j].type != JSMN_STRING) continue;
        int len = ct[j].end - ct[j].start;
        if (len == klen &&
            memcmp(cjs + ct[j].start, djs + dkey->start, (size_t)klen) == 0) {
            return j + 1;
        }
    }
    return -1;
}

int config_diff_minify(const char *current_json, const char *desired_json,
                       char *out, size_t out_cap)
{
    if (!current_json || !desired_json || !out || out_cap < 3) return -1;

    size_t clen = strnlen(current_json, CFG_JSON_MAX + 1u);
    size_t dlen = strnlen(desired_json, CFG_JSON_MAX + 1u);
    if (clen > CFG_JSON_MAX || dlen > CFG_JSON_MAX) return -1;

    jsmn_parser cp, dp;
    jsmn_init(&cp);
    jsmn_init(&dp);
    int cn = jsmn_parse(&cp, current_json, clen, s_ctok, CFG_MAXTOK);
    int dn = jsmn_parse(&dp, desired_json, dlen, s_dtok, CFG_MAXTOK);
    if (cn < 1 || dn < 1) return -1;                         /* NOMEM / malformed */
    if (s_ctok[0].type != JSMN_OBJECT || s_dtok[0].type != JSMN_OBJECT) return -1;

    size_t o = 0;
    out[o++] = '{';
    int emitted = 0;

    for (int i = 1; i + 1 < dn; i++) {                       /* bounded by dn */
        if (s_dtok[i].parent != 0 || s_dtok[i].type != JSMN_STRING) continue;  /* top-level key */
        const jsmntok_t *kt = &s_dtok[i];
        const jsmntok_t *vt = &s_dtok[i + 1];

        size_t dvlen = 0;
        if (value_repr(desired_json, vt, s_vb, sizeof s_vb, &dvlen) < 0) return -1;

        int cvi = find_value(current_json, s_ctok, cn, desired_json, kt);
        int changed;
        if (cvi < 0) {
            changed = 1;
        } else {
            size_t cvlen = 0;
            if (value_repr(current_json, &s_ctok[cvi], s_va, sizeof s_va, &cvlen) < 0) return -1;
            changed = (cvlen != dvlen) || (memcmp(s_va, s_vb, dvlen) != 0);
        }
        if (!changed) continue;

        size_t klen = (size_t)(kt->end - kt->start);
        size_t fraglen = klen + 3u + dvlen;                  /* "key": + value */
        if (fraglen > CFG_SET_FRAG_MAX) return -1;           /* one change too big -> 413 */

        size_t need = (emitted ? 1u : 0u) + fraglen;         /* optional comma + fragment */
        if (o + need + 2u > out_cap) return -1;              /* + '}' + NUL */

        if (emitted) out[o++] = ',';
        out[o++] = '"';
        memcpy(out + o, desired_json + kt->start, klen);
        o += klen;
        out[o++] = '"';
        out[o++] = ':';
        memcpy(out + o, s_vb, dvlen);
        o += dvlen;
        emitted++;
    }

    out[o++] = '}';
    out[o] = '\0';
    return (int)o;
}

int config_diff_next_line(const char *obj, size_t *cursor, char *out, size_t out_cap)
{
    if (!obj || !cursor || !out || out_cap < 3) return -1;
    size_t n = strnlen(obj, CFG_JSON_MAX + 1u);
    if (n < 2 || n > CFG_JSON_MAX || obj[0] != '{' || obj[n - 1] != '}') return -1;

    size_t p = (*cursor == 0) ? 1u : *cursor;                /* skip the opening '{' */
    if (p >= n - 1u) return 0;                                /* reached the closing '}' */

    size_t o = 0;
    out[o++] = '{';
    int frags = 0;

    while (p < n - 1u) {                                       /* bounded by n */
        /* delimit the fragment [fs,fe): up to a top-level comma or the closing brace */
        size_t fs = p;
        size_t fe = fs;
        int in_str = 0;
        int depth = 0;
        while (fe < n - 1u) {                                 /* bounded by n */
            char c = obj[fe];
            if (in_str) {
                if (c == '\\') fe++;                           /* skip escaped char */
                else if (c == '"') in_str = 0;
            } else if (c == '"') {
                in_str = 1;
            } else if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                depth--;
            } else if (c == ',' && depth == 0) {
                break;
            }
            fe++;
        }
        size_t flen = fe - fs;
        size_t extra = (frags ? 1u : 0u) + flen;              /* optional comma + fragment */
        if (o + extra + 1u > CFG_SET_OBJ_MAX || o + extra + 2u > out_cap) {
            if (frags == 0) return -1;                        /* one fragment cannot fit */
            break;                                            /* flush; resume at fs next call */
        }
        if (frags) out[o++] = ',';
        memcpy(out + o, obj + fs, flen);
        o += flen;
        frags++;

        p = fe;
        if (p < n - 1u && obj[p] == ',') p++;
    }

    out[o++] = '}';
    out[o] = '\0';
    *cursor = p;
    return (int)o;
}
