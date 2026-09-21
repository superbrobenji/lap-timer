/* multipart.c -- the PURE streaming multipart/form-data parser behind POST /api/flash
 * (Plan 5.5 Task 6). See multipart.h for the contract.
 *
 * The whole module is one scanner for the delimiter D = CRLF "--" <boundary>. Everything between
 * two delimiters is either a part header block or a part body; body bytes of the wanted part are
 * handed straight to the sink, so a 1.2 MB firmware image never has to be buffered.
 *
 * The scan is O(n) with no backtracking, which is only sound because a boundary may not contain
 * CR (RFC 2046 bchars; mp_init also rejects any non-printable boundary byte). D therefore contains
 * exactly one CR, at D[0]: no delimiter can begin INSIDE a partial match. So when a partial match
 * of `match` bytes fails, those bytes were ordinary body text -- emit them and re-examine only the
 * byte that broke the match, which can start a new match only if it is CR.
 */
#include "multipart.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- small case-insensitive helpers (ASCII only; no locale, no <ctype.h> surprises) ---- */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* True when `s` starts with `prefix`, compared case-insensitively. */
static bool ci_prefix(const char *s, const char *prefix)
{
    size_t i = 0;
    while (prefix[i] != '\0') {                        /* bounded by strlen(prefix) */
        if (s[i] == '\0' || lower(s[i]) != lower(prefix[i])) return false;
        i++;
    }
    return true;
}

/* First occurrence of `needle` in `hay`, compared case-insensitively. NULL if absent. */
static const char *ci_find(const char *hay, const char *needle)
{
    size_t hn = strlen(hay);
    size_t nn = strlen(needle);
    if (nn == 0 || hn < nn) return NULL;
    for (size_t i = 0; i + nn <= hn; i++) {            /* bounded by strlen(hay) */
        if (ci_prefix(hay + i, needle)) return hay + i;
    }
    return NULL;
}

static int mp_emit(mp_sink_cb sink, void *ctx, const uint8_t *p, size_t n)
{
    return (n == 0u) ? 0 : sink(ctx, p, n);
}

/* ---- init ---- */

int mp_init(mp_ctx_t *c, const char *content_type, const char *want_field)
{
    assert(c != NULL);
    assert(content_type != NULL);
    assert(want_field != NULL);

    memset(c, 0, sizeof *c);

    const char *p = content_type;
    while (*p == ' ' || *p == '\t') p++;                /* bounded by the header length */
    if (!ci_prefix(p, "multipart/form-data")) return -1;

    const char *b = ci_find(p, "boundary=");
    if (b == NULL) return -1;
    b += 9;                                             /* strlen("boundary=") */

    char quote = '\0';
    if (*b == '"') { quote = '"'; b++; }

    size_t blen = 0;
    while (b[blen] != '\0' && blen <= (size_t)MP_BOUNDARY_MAX) {   /* bounded by MP_BOUNDARY_MAX+1 */
        char ch = b[blen];
        if (quote != '\0') {
            if (ch == quote) break;
        } else if (ch == ';' || ch == ' ' || ch == '\t') {
            break;
        }
        /* The no-backtracking scan below relies on the boundary holding no CR; reject anything
         * non-printable so a header oddity can never break that invariant. */
        if ((unsigned char)ch < 0x20u || (unsigned char)ch > 0x7Eu) return -1;
        blen++;
    }
    if (blen == 0u || blen > (size_t)MP_BOUNDARY_MAX) return -1;

    memcpy(c->delim, "\r\n--", 4);
    memcpy(c->delim + 4, b, blen);
    c->delim_len = blen + 4u;
    c->delim[c->delim_len] = '\0';

    size_t flen = strlen(want_field);
    if (flen == 0u || flen > (size_t)MP_FIELD_MAX) return -1;
    int wn = snprintf(c->needle, sizeof c->needle, "name=\"%s\"", want_field);
    if (wn <= 0 || (size_t)wn >= sizeof c->needle) return -1;

    c->state = MP_S_BODY;
    /* The first delimiter of a body has no leading CRLF, so start as if D[0..2) had already been
     * matched; `virt` keeps those two phantom bytes out of any flush. */
    c->match = 2u;
    c->virt  = 2u;
    return 0;
}

/* ---- feed ---- */

int mp_feed(mp_ctx_t *c, const uint8_t *in, size_t n, mp_sink_cb sink, void *ctx)
{
    assert(c != NULL);
    assert(sink != NULL);
    assert(in != NULL || n == 0u);
    assert(c->delim_len >= 5u);                        /* mp_init ran and accepted a boundary */

    size_t i = 0;
    while (i < n) {                                    /* bounded: every branch advances i */
        if (c->state == MP_S_BODY) {
            if (c->match == 0u) {
                /* Fast path: nothing can match until the next CR, so emit that run in one go. */
                size_t j = i;
                while (j < n && in[j] != '\r') j++;     /* bounded by n */
                if (j > i) {
                    if (c->cur_wanted && mp_emit(sink, ctx, in + i, j - i) != 0) return MP_E_SINK;
                    i = j;
                    continue;
                }
                c->match = 1u;                          /* in[i] == '\r' == D[0] */
                i++;
                continue;
            }

            uint8_t b = in[i];
            if (b == (uint8_t)c->delim[c->match]) {
                c->match++;
                i++;
                if (c->match == c->delim_len) {
                    c->match = 0u;
                    c->virt  = 0u;
                    c->ad_len = 0u;
                    c->state = MP_S_AFTER_DELIM;
                }
                continue;
            }

            /* Mismatch: the partial match was body text after all. */
            if (c->cur_wanted && c->match > c->virt &&
                mp_emit(sink, ctx, (const uint8_t *)c->delim + c->virt, c->match - c->virt) != 0)
                return MP_E_SINK;
            c->match = 0u;
            c->virt  = 0u;
            if (b == '\r') {
                c->match = 1u;                          /* only a CR can start the next delimiter */
            } else if (c->cur_wanted && mp_emit(sink, ctx, &b, 1u) != 0) {
                return MP_E_SINK;
            }
            i++;
            continue;
        }

        if (c->state == MP_S_AFTER_DELIM) {
            c->ad[c->ad_len++] = (char)in[i];
            i++;
            if (c->ad_len < 2u) continue;
            if (c->ad[0] == '\r' && c->ad[1] == '\n') { /* another part follows */
                c->state = MP_S_HEADERS;
                c->line_len = 0u;
                c->hdr_lines = 0;
                c->name_matched = false;
                continue;
            }
            if (c->ad[0] == '-' && c->ad[1] == '-') {   /* closing boundary; epilogue ignored */
                c->state = MP_S_DONE;
                return MP_DONE;
            }
            return MP_E_MALFORMED;
        }

        if (c->state == MP_S_HEADERS) {
            char ch = (char)in[i];
            i++;
            if (ch != '\n') {
                if (c->line_len >= (size_t)MP_HDR_LINE_MAX) return MP_E_MALFORMED;
                c->line[c->line_len++] = ch;
                continue;
            }
            size_t len = c->line_len;
            if (len > 0u && c->line[len - 1u] == '\r') len--;
            c->line[len] = '\0';
            c->line_len = 0u;
            if (len == 0u) {                            /* blank line: the part body starts here */
                c->state = MP_S_BODY;
                c->match = 0u;
                c->virt  = 0u;
                c->cur_wanted = c->name_matched;
                c->name_matched = false;
                continue;
            }
            if (++c->hdr_lines > MP_HDR_LINES_MAX) return MP_E_MALFORMED;
            if (ci_prefix(c->line, "content-disposition:") && strstr(c->line, c->needle) != NULL) {
                c->name_matched = true;
                c->found = true;
            }
            continue;
        }

        return MP_DONE;                                 /* MP_S_DONE: ignore trailing input */
    }

    return (c->state == MP_S_DONE) ? MP_DONE : MP_MORE;
}

bool mp_found(const mp_ctx_t *c)
{
    assert(c != NULL);
    return c->found;
}
