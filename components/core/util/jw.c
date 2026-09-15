#include "core/jw.h"
#include <stdio.h>
#include <string.h>

void jw_init(jw_t *w, char *buf, size_t cap)
{
    memset(w, 0, sizeof *w); w->buf = buf; w->cap = cap;
    if (cap) buf[0] = '\0';
    w->first[0] = true;
}

static void put(jw_t *w, const char *s, size_t n)
{
    if (w->overflow) return;
    if (w->len + n + 1 > w->cap) { w->overflow = true; if (w->cap) w->buf[w->cap - 1] = '\0'; return; }
    memcpy(w->buf + w->len, s, n); w->len += n; w->buf[w->len] = '\0';
}
static void putc_(jw_t *w, char c) { put(w, &c, 1); }

static void sep(jw_t *w)
{
    if (w->after_key) { w->after_key = false; return; }
    if (!w->first[w->depth]) putc_(w, ',');
    w->first[w->depth] = false;
}

static void open_(jw_t *w, char c)
{
    sep(w); putc_(w, c);
    if (w->depth + 1 < JW_MAX_DEPTH) w->depth++; else w->overflow = true;
    w->first[w->depth] = true;
}
static void close_(jw_t *w, char c) { if (w->depth) w->depth--; putc_(w, c); }

void jw_obj_open(jw_t *w) { open_(w, '{'); }
void jw_obj_close(jw_t *w) { close_(w, '}'); }
void jw_arr_open(jw_t *w) { open_(w, '['); }
void jw_arr_close(jw_t *w) { close_(w, ']'); }

void jw_str(jw_t *w, const char *s)
{
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

void jw_key(jw_t *w, const char *key) { jw_str(w, key); putc_(w, ':'); w->after_key = true; }

/* Formats into a scratch buffer; a value that does not fit is treated as overflow (nothing written). */
static void put_fmt(jw_t *w, const char *t, int n, size_t cap)
{
    if (n < 0 || (size_t)n >= cap) { w->overflow = true; if (w->cap) w->buf[w->len] = '\0'; return; }
    sep(w); put(w, t, (size_t)n);
}

void jw_int(jw_t *w, int64_t v) { char t[24]; int n = snprintf(t, sizeof t, "%lld", (long long)v); put_fmt(w, t, n, sizeof t); }
void jw_uint(jw_t *w, uint64_t v) { char t[24]; int n = snprintf(t, sizeof t, "%llu", (unsigned long long)v); put_fmt(w, t, n, sizeof t); }
void jw_bool(jw_t *w, bool v) { sep(w); if (v) put(w, "true", 4); else put(w, "false", 5); }
void jw_null(jw_t *w) { sep(w); put(w, "null", 4); }
void jw_double(jw_t *w, double v, int decimals)
{
    if (decimals < 0) decimals = 0;
    if (decimals > JW_MAX_DECIMALS) decimals = JW_MAX_DECIMALS;
    char t[48]; int n = snprintf(t, sizeof t, "%.*f", decimals, v);
    put_fmt(w, t, n, sizeof t);
}
size_t jw_len(const jw_t *w) { return w->len; }
bool jw_overflow(const jw_t *w) { return w->overflow; }
