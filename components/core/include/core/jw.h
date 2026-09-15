#ifndef CORE_JW_H
#define CORE_JW_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define JW_MAX_DEPTH 12

typedef struct {
    char   *buf; size_t cap; size_t len; bool overflow;
    uint8_t depth; bool first[JW_MAX_DEPTH]; bool after_key;
} jw_t;

void   jw_init(jw_t *w, char *buf, size_t cap);
void   jw_obj_open(jw_t *w);  void jw_obj_close(jw_t *w);
void   jw_arr_open(jw_t *w);  void jw_arr_close(jw_t *w);
void   jw_key(jw_t *w, const char *key);
void   jw_int(jw_t *w, int64_t v);
void   jw_uint(jw_t *w, uint64_t v);
void   jw_bool(jw_t *w, bool v);
void   jw_null(jw_t *w);
void   jw_str(jw_t *w, const char *s);
void   jw_double(jw_t *w, double v, int decimals);
size_t jw_len(const jw_t *w);
bool   jw_overflow(const jw_t *w);
#endif
