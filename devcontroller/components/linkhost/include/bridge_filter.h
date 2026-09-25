/* devcontroller/components/linkhost/include/bridge_filter.h -- bridge_filter: pure, IDF-free
 * removal of the lap-timer's autonomous §18.1 0xFF stream frames from a byte stream forwarded to
 * `lt shell`'s USB console (Plan 5.6 Task 8). The lap-timer's UART0 carries BOTH its text console
 * (framed command replies, prompts, logs) and the binary stream frame (tag(0xFF) | seq_lo | seq_hi
 * | flags | len | payload[len], mirrors app/lt_proto.h's lt_stream_hdr_t) -- forwarding the raw
 * bytes verbatim to a human's terminal would inject binary noise (and could itself contain a
 * literal "~." sequence) into an interactive shell session, so the bridge filters every stream
 * frame out before writing to USB, leaving the console text untouched.
 *
 * A pure state machine (no heap; each call is bounded by the byte count fed) so a frame may
 * straddle two bridge_filter_run calls (the bridge reads UART1 in bounded chunks) -- all state
 * lives in the caller-owned bridge_filter_t between calls.
 */
#ifndef BRIDGE_FILTER_H
#define BRIDGE_FILTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* st: an internal bf_state_t (TEXT=0 / HDR=1, collecting the 4 header bytes after the 0xFF tag /
 * SKIP=2, discarding payload) -- kept as a plain uint8_t so this header has no dependency beyond
 * stdint. skip: while st==HDR, header bytes still needed (counts down from 4 to the len byte);
 * while st==SKIP, payload bytes still to discard. frames: whole stream frames filtered out. bad:
 * frames abandoned because their header's len exceeded LT_REC_MAX (linkhost_proto.h) -- the lone
 * 0xFF byte that looked like a tag is dropped, not forwarded and not re-scanned as text. */
typedef struct {
    uint8_t  st;
    uint16_t skip;
    uint32_t frames;
    uint32_t bad;
} bridge_filter_t;

/* Resets f to the initial TEXT state with every counter zeroed. Call once before the first
 * bridge_filter_run of a bridge session. */
void bridge_filter_reset(bridge_filter_t *f);

/* Copies the non-frame bytes of in[0..n) to out (cap bytes), skipping whole stream frames
 * (0xFF | seq16 | flags | len | payload[len]); frames may straddle calls. A header whose len byte
 * exceeds LT_REC_MAX cannot be a real stream frame (the largest §14 record is LT_REC_MAX bytes) --
 * f->bad is incremented, the tag+header bytes already consumed are dropped (never forwarded), and
 * classification resumes on the very next input byte as text; a genuine frame instead increments
 * f->frames once its payload has been fully skipped. Never writes past cap: once cap output bytes
 * have been written, further would-be-text bytes are simply dropped, but the rest of `in` is still
 * scanned (frame detection/counting keeps working past a full output buffer). Returns the number
 * of bytes written to out (<= cap). */
size_t bridge_filter_run(bridge_filter_t *f, const uint8_t *in, size_t n, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_FILTER_H */
