/* app/lt_proto.h -- shared lap-timer <-> dev-controller protocol contract (Plan 5.5 Task 1).
 *
 * The lap-timer's serial console (components/drivers/export_serial, spec §18.4) and the dev
 * controller's `linkhost` UART1 client (Plan 5.5, sub-project B) both speak the same wire
 * protocol: framed `---BEGIN/---END` command responses and an unsolicited §18.1 0xFF stream
 * frame. This header is the single source of truth for that wire contract so the two sides
 * cannot drift -- it is included by both export_serial.c (the producer) and B's linkhost (the
 * consumer). It has no ESP-IDF/FreeRTOS dependency (stdint.h only) so it also builds host-side
 * (test/test_proto.c) and on B's own IDF target.
 *
 * Framing markers (§18.4): a command response is printed as
 *     ---BEGIN <name> <size>---\r\n <bytes> \r\n---END <crc32 hex>---\r\n
 * LT_FRAME_*_FMT are the exact printf format strings export_serial.c uses to emit that framing
 * (byte-identical to the pre-lt_proto.h literals); LT_FRAME_*_PFX are the matching anchors a
 * parser scans for (no trailing newline assumed, since a parser reads line-by-line).
 *
 * Stream frame (§18.1, mirrors app/link.h): an unsolicited record is framed as a 5-byte header
 * (lt_stream_hdr_t) followed by `len` payload bytes: `tag(0xFF) | seq_lo | seq_hi | flags | len`.
 * LT_STREAM_TAG mirrors app/cmd.h's LINK_STREAM_TAG (link.c asserts the two stay equal).
 *
 * §18.2 STATUS record (20 bytes, cmd.c's op_status): LT_STATUS_LEN + the LT_ST_OFF_* field
 * offsets, copied verbatim from the current op_status body so a peer can decode the record
 * without re-deriving the layout.
 */
#ifndef APP_LT_PROTO_H
#define APP_LT_PROTO_H

#include <stdint.h>

/* ---- §18.4 command-response framing markers ---- */
#define LT_FRAME_BEGIN_FMT "---BEGIN %s %u---\r\n"   /* args: name, size */
#define LT_FRAME_END_FMT   "---END %08x---\r\n"       /* args: crc32 (hex, zero-padded) */
#define LT_FRAME_BEGIN_PFX "---BEGIN "                /* parse anchor: line starts with this */
#define LT_FRAME_END_PFX   "---END "                  /* parse anchor: line starts with this */

/* ---- §18.1 unsolicited stream frame ---- */
enum { LT_STREAM_TAG = 0xFF };   /* mirrors app/cmd.h's LINK_STREAM_TAG */

/* 5-byte stream frame header; `len` payload bytes (a §14 stream record) follow immediately. */
typedef struct __attribute__((packed)) {
    uint8_t tag;      /* always LT_STREAM_TAG (0xFF): an unsolicited stream frame */
    uint8_t seq_lo;   /* frame sequence, little-endian low byte */
    uint8_t seq_hi;   /* frame sequence, little-endian high byte */
    uint8_t flags;    /* CMD_FLAG_* (app/cmd.h); always CMD_FLAG_LAST for a stream frame today */
    uint8_t len;      /* payload byte count that follows this header */
} lt_stream_hdr_t;

_Static_assert(sizeof(lt_stream_hdr_t) == 5, "lt_stream_hdr_t must be exactly 5 bytes (packed)");

/* ---- §18.2 STATUS record (20 bytes total) -- verbatim from cmd.c's op_status ---- */
enum {
    LT_STATUS_LEN     = 20,

    LT_ST_OFF_PROTO    = 0,    /* u8       protocol version */
    LT_ST_OFF_STATE    = 1,    /* u8       device state machine */
    LT_ST_OFF_FLAGS    = 2,    /* u16 LE   sys_flags_get() & 0xFFFF */
    LT_ST_OFF_BATT_PCT = 4,    /* u8       battery percent */
    LT_ST_OFF_BATT_MV  = 5,    /* u16 LE   battery millivolts */
    LT_ST_OFF_FREE_KB  = 7,    /* u32 LE   storage_free_kb() */
    LT_ST_OFF_SESS     = 11,   /* u16 LE   session_count() */
    LT_ST_OFF_FW       = 13,   /* char[7]  fw_short() -- 13 + 7 == LT_STATUS_LEN */
};

/* ---- §18.1/§18.4 command + format name constants shared with a machine peer ---- */
#define LT_CMD_STATUS     "status"
#define LT_CMD_LIST       "list"
#define LT_CMD_CONFIG_GET "config get"
#define LT_CMD_CONFIG_SET "config set"
#define LT_CMD_OPEN       "open"
#define LT_CMD_READ       "read"

#define LT_FMT_JSON       "json"
#define LT_FMT_VBO        "vbo"
#define LT_FMT_NMEA       "nmea"
#define LT_FMT_LOG        "log"
#define LT_FMT_SUM        "sum"

#endif /* APP_LT_PROTO_H */
