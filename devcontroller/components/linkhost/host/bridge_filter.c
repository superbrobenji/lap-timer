/* bridge_filter.c -- see include/bridge_filter.h. Pure, IDF-free (Plan 5.6 Task 8): no heap, no
 * shared state -- all state lives in the caller-owned bridge_filter_t. Built both into the linkhost
 * IDF component (`lt shell`'s UART1 -> USB forward path, cmd_shell.c) and linked directly by the
 * host test (devcontroller/test/test_bridge_filter.c).
 */
#include "bridge_filter.h"

#include <assert.h>

#include "linkhost_proto.h"   /* LT_STREAM_TAG (app/lt_proto.h), LT_REC_MAX */

/* bf_state_t is kept private to this file -- bridge_filter_t's `st` field is a plain uint8_t (see
 * the header) so bridge_filter.h has no dependency beyond stdint. */
typedef enum {
    BF_TEXT = 0,   /* passing bytes through; watching for the 0xFF stream-frame tag */
    BF_HDR  = 1,   /* tag consumed; collecting the 4 remaining header bytes (skip counts down) */
    BF_SKIP = 2,   /* discarding `skip` remaining payload bytes of a confirmed frame */
} bf_state_t;

#define BF_HDR_BYTES 4u   /* seq_lo, seq_hi, flags, len -- the tag itself is already consumed */

void bridge_filter_reset(bridge_filter_t *f)
{
    assert(f != NULL);
    f->st     = BF_TEXT;
    f->skip   = 0;
    f->frames = 0;
    f->bad    = 0;
}

size_t bridge_filter_run(bridge_filter_t *f, const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    assert(f != NULL);
    assert(in != NULL || n == 0);
    assert(out != NULL || cap == 0);

    size_t w = 0;
    for (size_t i = 0; i < n; i++) {                          /* bounded by n */
        uint8_t b = in[i];
        switch (f->st) {
        case BF_TEXT:
            if (b == (uint8_t)LT_STREAM_TAG) {
                f->st   = BF_HDR;
                f->skip = BF_HDR_BYTES;
            } else if (w < cap) {
                out[w++] = b;                    /* cap reached: drop, but keep scanning `in` */
            }
            break;

        case BF_HDR:
            f->skip--;
            if (f->skip == 0) {                  /* b is the len byte: the header is complete */
                if (b > LT_REC_MAX) {
                    f->bad++;                    /* not a real frame -- resync on the next byte */
                    f->st = BF_TEXT;
                } else if (b == 0) {
                    f->frames++;                 /* zero-payload frame: already done */
                    f->st = BF_TEXT;
                } else {
                    f->skip = (uint16_t)b;
                    f->st   = BF_SKIP;
                }
            }
            break;

        case BF_SKIP:
            f->skip--;
            if (f->skip == 0) {
                f->frames++;
                f->st = BF_TEXT;
            }
            break;

        default:                                 /* defensive: an impossible state resyncs to text */
            f->st = BF_TEXT;
            break;
        }
    }
    return w;
}
