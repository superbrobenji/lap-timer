/* app/cmd.h -- transport-agnostic command dispatch (spec §18.1).
 *
 * The command protocol is the single handler behind every transport (serial now, BLE/WiFi
 * later, §18.2/§18.5). A transport parses a request frame (`op u8 | tag u8 | payload[]`),
 * calls cmd_dispatch, and supplies an emit callback; cmd_dispatch runs the one op and streams
 * its output back as data chunks `tag u8 | chunk_seq u16 | flags u8 | payload[<= 496]`. A
 * request with tag T is answered only by chunks carrying tag T (§18.1).
 *
 * flags bit0 LAST marks the final chunk; bit1 ERROR marks an error chunk whose payload is
 * `code u16 (LE) | msg utf8` (§18.1). Protocol errors (unknown op, malformed payload, an op not
 * implemented yet) are reported through an ERROR chunk with E_CONN_PROTO -- cmd_dispatch itself
 * returns 0 in that case; it returns -1 only when the emit callback (the transport) fails.
 */
#ifndef APP_CMD_H
#define APP_CMD_H

#include "app/lt_proto.h"          /* LT_STATUS_LEN -- status_build()'s output size */

#include <stddef.h>
#include <stdint.h>

/* One response chunk. Returns 0 on success, non-zero to abort the stream (transport error). */
typedef int (*cmd_emit_fn)(void *ctx, uint8_t tag, uint16_t seq, uint8_t flags,
                           const uint8_t *payload, size_t len);

enum {
    CMD_CHUNK_MAX  = 496,   /* max data-chunk payload (§18.1) */
    CMD_FLAG_LAST  = 0x01,  /* bit0: final chunk of this response */
    CMD_FLAG_ERROR = 0x02,  /* bit1: payload = code u16 (LE) | msg utf8 */
};

/* The tag reserved for unsolicited fused-log / event STREAM frames pushed to an attached peer
 * (Plan 5 sub-project A, §18): a stream frame reuses the chunk framing (tag | seq u16 | flags |
 * payload) but carries this tag instead of a request tag, so a client demultiplexes stream
 * records from command responses. Payload is a §14 record. */
enum { LINK_STREAM_TAG = 0xFF };

/* Op codes (§18.1). STATUS/CONFIG/ERRLOG/DIAG + LIST/OPEN/READ/DELETE/CLOSE are implemented; the
 * OTA_* ops (§19.4) are the receive-side firmware-update state machine (session 5.4). */
enum { CMD_STATUS=0x01, CMD_LIST=0x02, CMD_OPEN=0x03, CMD_READ=0x04, CMD_CLOSE=0x05, CMD_DELETE=0x06,
       CMD_CONFIG_GET=0x10, CMD_CONFIG_SET=0x11, CMD_ERRLOG_GET=0x14, CMD_ERRLOG_CLEAR=0x15,
       CMD_DIAG_GET=0x16,
       CMD_OTA_BEGIN=0x20, CMD_OTA_DATA=0x21, CMD_OTA_END=0x22, CMD_OTA_ABORT=0x23 };

/* Run one op. Streams the response through `emit`. Returns 0 (including when the op reported an
 * ERROR chunk), or -1 if `emit` signalled a transport error. Not reentrant: one request at a
 * time (the transport enforces §18.1's "one request in flight"). */
int cmd_dispatch(uint8_t op, uint8_t tag, const uint8_t *payload, size_t len,
                 cmd_emit_fn emit, void *ctx);

/* Builds the §18.2 STATUS record (LT_STATUS_LEN bytes). Shared by the framed `status` reply
 * (op_status) and the 1 Hz stream push (link.c, Plan 5.6) so the two can never drift. */
void status_build(uint8_t out[LT_STATUS_LEN]);

#endif /* APP_CMD_H */
