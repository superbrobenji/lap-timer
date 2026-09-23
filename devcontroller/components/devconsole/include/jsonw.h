/* devcontroller/components/devconsole/include/jsonw.h -- jsonw: a bounded, no-heap JSON object
 * writer for the console's `--json` command bodies (Plan 5.6 Task 4).
 *
 * PINNED interface (host-tested: devcontroller/test/test_jsonw.c). Pure, IDF-free: no heap, no
 * shared state, every append is bounded by the caller-supplied buffer. A write that would not fit
 * sets `overflow` and is dropped (never partially written), but the buffer is ALWAYS left
 * NUL-terminated -- both mid-stream (at the last successful append) and, once overflow trips, at
 * buf[cap-1] as well, so a caller can never be handed an unterminated string.
 *
 * Usage: jsonw_begin, any sequence of jsonw_int/uint/bool/str/obj/close, then jsonw_end. jsonw_end
 * closes every object jsonw_obj opened (so a caller need not match every jsonw_obj with a
 * jsonw_close itself, though doing so to nest correctly is still required) and reports whether the
 * whole thing fit.
 */
#ifndef JSONW_H
#define JSONW_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char   *buf;
    size_t  cap;
    size_t  len;
    int     depth;    /* nested jsonw_obj() calls currently open (0 == just the top object) */
    bool    first;     /* true if no key has been written yet in the CURRENT (innermost) object */
    bool    overflow;  /* true once an append did not fit; all further appends are no-ops */
} jsonw_t;

/* Binds w to buf[0..cap) and writes the opening "{". cap must be > 0. */
void jsonw_begin(jsonw_t *w, char *buf, size_t cap);

/* Appends "<k>":<v> (comma-separated from any prior key at this nesting level). */
void jsonw_int(jsonw_t *w, const char *k, long long v);
void jsonw_uint(jsonw_t *w, const char *k, unsigned long long v);
void jsonw_bool(jsonw_t *w, const char *k, bool v);
/* Escapes '"' and '\\'; drops ASCII control characters (< 0x20) rather than emit invalid JSON. */
void jsonw_str(jsonw_t *w, const char *k, const char *v);

/* Opens a nested object under key k ("<k>":{ ...). Up to 8 levels deep; asserts beyond that. */
void jsonw_obj(jsonw_t *w, const char *k);
/* Closes the innermost object opened by jsonw_obj. Asserts if none is open. */
void jsonw_close(jsonw_t *w);

/* Closes every object still open (including the top one from jsonw_begin) and NUL-terminates buf.
 * Returns false if any append along the way overflowed the buffer (buf is still NUL-terminated,
 * but its content is an incomplete JSON object and must not be sent to a caller). */
bool jsonw_end(jsonw_t *w);

#ifdef __cplusplus
}
#endif

#endif /* JSONW_H */
