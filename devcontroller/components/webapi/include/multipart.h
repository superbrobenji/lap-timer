/* devcontroller/components/webapi/include/multipart.h -- a PURE, streaming multipart/form-data
 * parser (Plan 5.5 sub-project B, Task 6).
 *
 * POST /api/flash uploads a ~1.2 MB firmware image as multipart/form-data, which never fits in
 * RAM: the body has to be parsed as it arrives off the socket and pushed straight into the
 * `ota_stage` flash partition. This module is that parser -- byte-wise, O(n), no backtracking, no
 * heap, and IDF-free (no esp_ or httpd headers) so devcontroller/test/test_multipart.c links it
 * directly on the host. The httpd glue (recv loop + the partition sink) stays in webapi.c.
 */
#ifndef MULTIPART_H
#define MULTIPART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MP_BOUNDARY_MAX  70    /* RFC 2046: a boundary is 1..70 characters */
#define MP_FIELD_MAX     32    /* longest form-field name we will look for */
#define MP_HDR_LINE_MAX  256   /* longest per-part header line accepted */
#define MP_HDR_LINES_MAX 16    /* most header lines accepted in one part */

/* The delimiter the parser actually scans for: CRLF + "--" + boundary. */
#define MP_DELIM_MAX (4 + MP_BOUNDARY_MAX)

/* mp_feed returns */
#define MP_MORE         0      /* more input expected */
#define MP_DONE         1      /* closing boundary seen; further input is ignored */
#define MP_E_MALFORMED (-1)    /* the body is not valid multipart/form-data */
#define MP_E_SINK      (-2)    /* the sink callback asked to abort */

/* Receives the wanted part's body bytes. Returns nonzero to abort the parse (-> MP_E_SINK); the
 * reason (if any) belongs in the caller's own ctx. */
typedef int (*mp_sink_cb)(void *ctx, const uint8_t *data, size_t n);

typedef enum {
    MP_S_BODY = 0,      /* inside a part body (or the preamble): scanning for the delimiter */
    MP_S_AFTER_DELIM,   /* delimiter matched: the next two bytes say CRLF (new part) or "--" (end) */
    MP_S_HEADERS,       /* reading one part's header lines */
    MP_S_DONE           /* closing boundary seen */
} mp_state_t;

/* Opaque-ish: fixed buffers only, no heap. Allocate one per upload (single-flight in webapi.c). */
typedef struct {
    char       delim[MP_DELIM_MAX + 1];    /* "\r\n--" + boundary */
    size_t     delim_len;
    char       needle[MP_FIELD_MAX + 10];  /* name="<want_field>" */
    mp_state_t state;
    size_t     match;         /* delimiter bytes matched so far */
    size_t     virt;          /* leading delim bytes that were never in the input (preamble only) */
    bool       cur_wanted;    /* the part being streamed is the wanted one */
    bool       name_matched;  /* the part being parsed declared the wanted name */
    bool       found;         /* the wanted part was seen at least once */
    char       ad[2];         /* the two bytes after a delimiter */
    size_t     ad_len;
    char       line[MP_HDR_LINE_MAX + 1];
    size_t     line_len;
    int        hdr_lines;
} mp_ctx_t;

/* Initialises `c` from a Content-Type header VALUE (e.g. `multipart/form-data; boundary=abc` or
 * `...; boundary="abc"`). Returns 0, or -1 when the type is not multipart/form-data, there is no
 * usable `boundary=`, the boundary is empty/too long/contains non-printable bytes, or
 * `want_field` is empty or longer than MP_FIELD_MAX. */
int mp_init(mp_ctx_t *c, const char *content_type, const char *want_field);

/* Feeds `n` bytes of body. Body bytes of the `want_field` part are handed to `sink` (possibly
 * across several calls; never a copy of more than what arrived). Returns MP_MORE, MP_DONE or an
 * MP_E_* code. Once MP_DONE has been returned, further input is ignored and MP_DONE is
 * returned again. */
int mp_feed(mp_ctx_t *c, const uint8_t *in, size_t n, mp_sink_cb sink, void *ctx);

/* True once the wanted part's headers were seen (its body may still be empty). */
bool mp_found(const mp_ctx_t *c);

#ifdef __cplusplus
}
#endif

#endif /* MULTIPART_H */
