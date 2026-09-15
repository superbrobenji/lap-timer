#include "core/json.h"
#include <string.h>
#include <stdlib.h>

int json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks)
{
    jsmn_parser p; jsmn_init(&p);
    int r = jsmn_parse(&p, js, n, toks, max_toks);
    return r < 0 ? -1 : r;
}

bool json_tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
    size_t len = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == len && memcmp(js + t->start, s, len) == 0;
}

int json_skip(const jsmntok_t *toks, int i)
{
    int j = i + 1;
    for (int k = 0; k < toks[i].size; k++) j = json_skip(toks, j);
    return j;
}

static size_t tok_copy(const char *js, const jsmntok_t *t, char *tmp, size_t cap)
{
    size_t len = (size_t)(t->end - t->start);
    if (len >= cap) len = cap - 1;
    memcpy(tmp, js + t->start, len); tmp[len] = '\0';
    return len;
}

bool json_tok_int(const char *js, const jsmntok_t *t, int64_t *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[32]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; long long v = strtoll(tmp, &end, 10);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_double(const char *js, const jsmntok_t *t, double *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[48]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; double v = strtod(tmp, &end);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_bool(const char *js, const jsmntok_t *t, bool *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    if (js[t->start] == 't') { *out = true; return true; }
    if (js[t->start] == 'f') { *out = false; return true; }
    return false;
}

size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap)
{
    /* jsmn leaves escapes in place; the config/track keys never contain escapes, so a plain copy suffices */
    return tok_copy(js, t, out, cap);
}

int json_obj_get(const char *js, const jsmntok_t *toks, int obj, const char *key)
{
    if (toks[obj].type != JSMN_OBJECT) return -1;
    int i = obj + 1;
    for (int k = 0; k < toks[obj].size; k++) {
        if (json_tok_eq(js, &toks[i], key)) return i + 1;
        i = json_skip(toks, i);
    }
    return -1;
}
