#include "core/jw.h"
#include "core/core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions report
 * JSON_ASSERT_CODE, shared with json.c (components/core/util is one assertion "module" for the
 * retrofit). They guard genuine writer-contract anomalies -- NULL params, an impossible
 * len>cap/depth state -- never the writer's own documented capacity/decimal clamping, which stays
 * plain conditional logic (it is the tested, specified behavior of an undersized buffer or an
 * out-of-range decimal count, not a bug). */
#define JSON_ASSERT_CODE 0x0AB0

void jw_init(jw_t *w, char *buf, size_t cap)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(buf != NULL || cap == 0, JSON_ASSERT_CODE);
    memset(w, 0, sizeof *w); w->buf = buf; w->cap = cap;
    if (cap) buf[0] = '\0';
    w->first[0] = true;
}

static void put(jw_t *w, const char *s, size_t n)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(s != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(w->len <= w->cap, JSON_ASSERT_CODE); /* invariant: never written past capacity */
    CORE_ASSERT_VOID(n <= 64, JSON_ASSERT_CODE); /* every call site writes a single char, escape or formatted-number chunk (<=48 bytes) */
    if (w->overflow) return;
    if (w->len + n + 1 > w->cap) { w->overflow = true; if (w->cap) w->buf[w->cap - 1] = '\0'; return; }
    memcpy(w->buf + w->len, s, n); w->len += n; w->buf[w->len] = '\0';
}
static void putc_(jw_t *w, char c) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); put(w, &c, 1); }

static void sep(jw_t *w)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(w->depth < JW_MAX_DEPTH, JSON_ASSERT_CODE); /* invariant: first[] index in range */
    if (w->after_key) { w->after_key = false; return; }
    if (!w->first[w->depth]) putc_(w, ',');
    w->first[w->depth] = false;
}

static void open_(jw_t *w, char c)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    sep(w); putc_(w, c);
    if (w->depth + 1 < JW_MAX_DEPTH) w->depth++; else w->overflow = true;
    CORE_ASSERT_VOID(w->depth < JW_MAX_DEPTH, JSON_ASSERT_CODE); /* invariant: first[] index in range */
    w->first[w->depth] = true;
}
static void close_(jw_t *w, char c) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); if (w->depth) w->depth--; putc_(w, c); }

void jw_obj_open(jw_t *w)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    open_(w, '{');
    CORE_ASSERT_VOID(w->overflow || w->depth > 0, JSON_ASSERT_CODE); /* postcondition: a successful open nests one level */
}
void jw_obj_close(jw_t *w) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); close_(w, '}'); }
void jw_arr_open(jw_t *w)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    open_(w, '[');
    CORE_ASSERT_VOID(w->overflow || w->depth > 0, JSON_ASSERT_CODE); /* postcondition: a successful open nests one level */
}
void jw_arr_close(jw_t *w) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); close_(w, ']'); }

void jw_str(jw_t *w, const char *s)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(s != NULL, JSON_ASSERT_CODE);
    sep(w); putc_(w, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  put(w, "\\\"", 2); break;
        case '\\': put(w, "\\\\", 2); break;
        case '\n': put(w, "\\n", 2); break;
        case '\r': put(w, "\\r", 2); break;
        case '\t': put(w, "\\t", 2); break;
        default:
            if (c < 0x20) { char tmp[7]; snprintf(tmp, sizeof tmp, "\\u%04x", c); put(w, tmp, 6); }
            else putc_(w, (char)c);
        }
    }
    putc_(w, '"');
}

void jw_key(jw_t *w, const char *key)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(key != NULL, JSON_ASSERT_CODE);
    jw_str(w, key); putc_(w, ':'); w->after_key = true;
}

/* Formats into a scratch buffer; a value that does not fit is treated as overflow (nothing written). */
static void put_fmt(jw_t *w, const char *t, int n, size_t cap)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(t != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(cap <= 64, JSON_ASSERT_CODE); /* every caller passes sizeof() of a small local scratch buffer (<=48 bytes) */
    if (n < 0 || (size_t)n >= cap) { w->overflow = true; if (w->cap) w->buf[w->len] = '\0'; return; }
    sep(w); put(w, t, (size_t)n);
}

void jw_int(jw_t *w, int64_t v) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); char t[24]; int n = snprintf(t, sizeof t, "%lld", (long long)v); put_fmt(w, t, n, sizeof t); }
void jw_uint(jw_t *w, uint64_t v) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); char t[24]; int n = snprintf(t, sizeof t, "%llu", (unsigned long long)v); put_fmt(w, t, n, sizeof t); }
void jw_bool(jw_t *w, bool v) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); sep(w); if (v) put(w, "true", 4); else put(w, "false", 5); }
void jw_null(jw_t *w) { CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE); sep(w); put(w, "null", 4); }
void jw_double(jw_t *w, double v, int decimals)
{
    CORE_ASSERT_VOID(w != NULL, JSON_ASSERT_CODE);
    CORE_ASSERT_VOID(isfinite(v), JSON_ASSERT_CODE); /* JSON has no NaN/Infinity literal */
    if (decimals < 0) decimals = 0;
    if (decimals > JW_MAX_DECIMALS) decimals = JW_MAX_DECIMALS;
    char t[48]; int n = snprintf(t, sizeof t, "%.*f", decimals, v);
    put_fmt(w, t, n, sizeof t);
}
size_t jw_len(const jw_t *w)
{
    CORE_ASSERT_RET(w != NULL, JSON_ASSERT_CODE, 0);
    CORE_ASSERT_RET(w->len <= w->cap, JSON_ASSERT_CODE, 0); /* invariant: never reports past capacity */
    return w->len;
}
bool jw_overflow(const jw_t *w) { CORE_ASSERT_RET(w != NULL, JSON_ASSERT_CODE, true); return w->overflow; }
