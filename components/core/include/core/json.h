#ifndef CORE_JSON_H
#define CORE_JSON_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define JSMN_HEADER
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "core/jsmn.h"

/* Maximum nesting depth accepted by json_parse: a token with more than JSON_MAX_DEPTH
 * ancestors makes the whole document invalid. Config and track documents nest 6 deep. */
#define JSON_MAX_DEPTH 16

/* Parses and enforces JSON_MAX_DEPTH. Returns the token count, or -1 on a jsmn error or too deep. */
int    json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks);   /* token count or -1 */
bool   json_tok_eq(const char *js, const jsmntok_t *t, const char *s);
/* Iterative (no recursion): the subtree of token i is the run of following tokens that start
 * before toks[i].end, which JSMN_PARENT_LINKS guarantees is contiguous. Depth is capped by
 * json_parse, so neither helper can be driven to unbounded stack use. */
int    json_skip(const jsmntok_t *toks, int ntoks, int i);                          /* index of the token after subtree i */
bool   json_tok_int(const char *js, const jsmntok_t *t, int64_t *out);
bool   json_tok_double(const char *js, const jsmntok_t *t, double *out);
bool   json_tok_bool(const char *js, const jsmntok_t *t, bool *out);
/* Raw copy of the token's source bytes, NUL-terminated, truncated to cap-1; does NOT unescape JSON escapes. Suitable for the ASCII keys and short values this project exchanges (config, track names). Returns the copied length. */
size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap);     /* copies, NUL-terminates, returns length */
int    json_obj_get(const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *key);   /* value token index or -1 */
#endif
