#ifndef CORE_JSON_H
#define CORE_JSON_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define JSMN_HEADER
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "core/jsmn.h"

int    json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks);   /* token count or -1 */
bool   json_tok_eq(const char *js, const jsmntok_t *t, const char *s);
/* Recursive in token nesting depth; intended for config/track-sized documents (depth < 16). */
int    json_skip(const jsmntok_t *toks, int i);                                     /* index of the token after subtree i */
bool   json_tok_int(const char *js, const jsmntok_t *t, int64_t *out);
bool   json_tok_double(const char *js, const jsmntok_t *t, double *out);
bool   json_tok_bool(const char *js, const jsmntok_t *t, bool *out);
/* Raw copy of the token's source bytes, NUL-terminated, truncated to cap-1; does NOT unescape JSON escapes. Suitable for the ASCII keys and short values this project exchanges (config, track names). Returns the copied length. */
size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap);     /* copies, NUL-terminates, returns length */
int    json_obj_get(const char *js, const jsmntok_t *toks, int obj, const char *key);   /* value token index or -1 */
#endif
