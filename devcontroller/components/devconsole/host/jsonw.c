/* devcontroller/components/console/host/jsonw.c -- see include/jsonw.h. Pure, IDF-free; no heap,
 * no shared state (Plan 5.6 Task 4). Every append funnels through jw_raw, which is the ONLY place
 * that touches w->buf/len/overflow, so the "always NUL-terminated, never a partial write" contract
 * holds no matter which jsonw_* entry point is called.
 */
#include "jsonw.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JSONW_MAX_DEPTH 8

/* Appends exactly n raw bytes, atomically: either the whole thing fits (buf/len advance, buf
 * stays NUL-terminated at the new len) or none of it does (overflow is set, and -- since the
 * buffer may otherwise never get a terminator past the last complete append if the caller's cap
 * is small enough that no successful append ever ran -- buf[cap-1] is force-terminated too, so
 * the whole buffer is always safe to treat as a C string up to cap). Once overflow is set, every
 * further call is a no-op: the JSON built so far is already unrecoverable (an append got dropped
 * mid-object), so there is nothing further to preserve. */
static void jw_raw(jsonw_t *w, const char *s, size_t n)
{
    assert(w != NULL);
    assert(w->buf != NULL);
    assert(w->len < w->cap);   /* invariant: always room for at least the terminator */
    if (w->overflow) return;
    if (n >= w->cap - w->len) {
        w->overflow = true;
        w->buf[w->cap - 1] = '\0';
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

/* Formats a number into a small stack buffer, then hands it to jw_raw -- keeps every actual
 * buffer write funneled through the one atomic/bounded path above (no direct snprintf into
 * w->buf, which could otherwise leave a truncated numeral committed). 32 bytes comfortably fits
 * the longest value either jsonw_int or jsonw_uint can format (a 20-digit uint64 max, or a sign
 * plus 19 digits). */
static void jw_num(jsonw_t *w, const char *fmt, ...)
{
    char tmp[32];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    assert(n >= 0 && (size_t)n < sizeof tmp);
    jw_raw(w, tmp, (size_t)n);
}

/* Comma (if this is not the first key at the current nesting level) + "<k>": -- shared by every
 * value-writing entry point. */
static void jw_key(jsonw_t *w, const char *k)
{
    if (!w->first) jw_raw(w, ",", 1);
    w->first = false;
    jw_raw(w, "\"", 1);
    jw_raw(w, k, strlen(k));
    jw_raw(w, "\":", 2);
}

void jsonw_begin(jsonw_t *w, char *buf, size_t cap)
{
    assert(w != NULL);
    assert(buf != NULL);
    assert(cap > 0);
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->depth = 0;
    w->first = true;
    w->overflow = false;
    buf[0] = '\0';
    jw_raw(w, "{", 1);
}

void jsonw_int(jsonw_t *w, const char *k, long long v)
{
    assert(w != NULL);
    assert(k != NULL);
    jw_key(w, k);
    jw_num(w, "%lld", v);
}

void jsonw_uint(jsonw_t *w, const char *k, unsigned long long v)
{
    assert(w != NULL);
    assert(k != NULL);
    jw_key(w, k);
    jw_num(w, "%llu", v);
}

void jsonw_bool(jsonw_t *w, const char *k, bool v)
{
    assert(w != NULL);
    assert(k != NULL);
    jw_key(w, k);
    if (v) jw_raw(w, "true", 4);
    else   jw_raw(w, "false", 5);
}

void jsonw_str(jsonw_t *w, const char *k, const char *v)
{
    assert(w != NULL);
    assert(k != NULL);
    assert(v != NULL);
    jw_key(w, k);
    jw_raw(w, "\"", 1);
    /* Bounded by v's own NUL terminator (the caller's string is already a bounded C string) and,
     * independently, by w->overflow tripping the moment the destination buffer is full. */
    for (const unsigned char *p = (const unsigned char *)v; *p != '\0' && !w->overflow; p++) {
        if (*p == '"' || *p == '\\') {
            char esc[2] = { '\\', (char)*p };
            jw_raw(w, esc, 2);
        } else if (*p < 0x20) {
            continue;   /* drop control chars rather than emit invalid JSON */
        } else {
            char c = (char)*p;
            jw_raw(w, &c, 1);
        }
    }
    jw_raw(w, "\"", 1);
}

void jsonw_obj(jsonw_t *w, const char *k)
{
    assert(w != NULL);
    assert(k != NULL);
    assert(w->depth < JSONW_MAX_DEPTH);
    jw_key(w, k);
    jw_raw(w, "{", 1);
    w->depth++;
    w->first = true;
}

void jsonw_close(jsonw_t *w)
{
    assert(w != NULL);
    assert(w->depth > 0);
    jw_raw(w, "}", 1);
    w->depth--;
    /* Back in the parent scope, which is guaranteed non-first: the object just closed was itself
     * written as a key there (jsonw_obj's jw_key call), so a single un-stacked `first` flag is
     * enough -- no per-depth stack needed. */
    w->first = false;
}

bool jsonw_end(jsonw_t *w)
{
    assert(w != NULL);
    while (w->depth > 0) {
        jw_raw(w, "}", 1);
        w->depth--;
    }
    jw_raw(w, "}", 1);   /* the top object opened by jsonw_begin */
    return !w->overflow;
}
