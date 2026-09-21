/* linkhost_proto.h -- linkhost's PURE, IDF-free logic (Plan 5.5 Task 3/6). Mirrors the lap-timer's
 * pure-core/IDF-glue split: everything declared here lives in host/linkhost_proto.c and is
 * host-testable under devcontroller/test/. It includes ONLY <stdint.h>/<stddef.h>/<stdbool.h> and
 * the IDF-free wire contract app/lt_proto.h -- NEVER esp_* / driver / freertos. The UART1 transport
 * that drives these state machines lives in the IDF glue (linkhost.c).
 *
 * Contained here:
 *   - the shared value types (lt_status_t / lt_stream_rec_t / linkhost_frame_t / error codes),
 *   - linkhost_crc32 (esp_rom_crc32_le(0,..)-compatible == zlib CRC-32; local, no ROM dep),
 *   - linkhost_status_decode (the binary §18.2 STATUS record),
 *   - linkhost_parse_frame (---BEGIN/---END, base64 for binary formats, CRC over the decoded body),
 *   - lh_dl_* (a STREAMING ---BEGIN/---END parser that never buffers the whole body -- for the
 *     KB..MB session files that overflow LINKHOST_ASM_MAX; the one intentional callback here),
 *   - linkhost_feed (the length-aware, noise-tolerant demux) + its stream ring / response slot,
 *   - the cmd-OTA flash token parser + state machine.
 */
#ifndef LINKHOST_PROTO_H
#define LINKHOST_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/lt_proto.h"   /* LT_STATUS_LEN, LT_ST_OFF_*, lt_stream_hdr_t, LT_FRAME_* markers */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- negative returns from linkhost_status/linkhost_cmd/linkhost_flash/linkhost_parse_frame ---- */
enum {
    LINKHOST_E_TIMEOUT = -1,   /* no/incomplete response within the link timeout */
    LINKHOST_E_NOTCONN = -2,   /* no lap-timer peer detected (no heartbeat / not yet wired) */
    LINKHOST_E_CRC     = -3,   /* decoded body CRC did not match the ---END crc32 */
    LINKHOST_E_PROTO   = -4,   /* malformed framing / oversize body / unexpected token */
    LINKHOST_E_REMOTE  = -5,   /* the lap-timer answered `ERR 0x<code>: <msg>` (see err_code/err_msg) */
    LINKHOST_E_BUSY    = -6,   /* the link is owned by another in-flight request (bounded-take miss) */
};

/* Max bytes of a remote `ERR 0x<code>: <msg>` message carried back to the caller (truncated). */
#define LINKHOST_ERRMSG_MAX 48

/* ---- §18.2 STATUS record, decoded ---- */
typedef struct {
    uint8_t  proto;
    uint8_t  state;
    uint16_t flags;
    uint8_t  batt_pct;
    uint16_t batt_mv;
    uint32_t free_kb;
    uint16_t sessions;
    char     fw[8];
} lt_status_t;

/* ---- §14/§18.1 demuxed stream record. LT_REC_MAX bounds the payload to the largest
 * fused-sample/event record (§14). `type` is the leading SES_T_* byte; `len` is the record byte
 * count that follows it (payload len minus the type byte). ---- */
#define LT_REC_MAX 64

typedef struct {
    uint16_t seq;
    uint8_t  flags;
    uint8_t  type;
    uint8_t  len;
    uint8_t  data[LT_REC_MAX];
} lt_stream_rec_t;

/* ---- §18.4 framed command response, decoded. `body` points into linkhost's static assembly
 * buffer -- valid only until the next linkhost_cmd/linkhost_parse_frame/linkhost_feed call.
 * `size` is the on-wire body byte count (base64 length for binary formats); `body_len` is the
 * decoded length (== size for text formats, the base64-decoded length for binary). ---- */
#define LINKHOST_ASM_MAX 1024   /* bound on one framed response body (config get is ~939 B) */

typedef struct {
    char           name[16];
    uint32_t       size;
    const uint8_t *body;
    size_t         body_len;
    /* Valid only when the response is a remote error (linkhost_pop_response status ==
     * LINKHOST_E_REMOTE): the lap-timer's `ERR 0x<code>: <msg>` code + (truncated) message. */
    uint16_t       err_code;
    char           err_msg[LINKHOST_ERRMSG_MAX];
} linkhost_frame_t;

/* ---- CRC-32 (esp_rom_crc32_le(0, buf, len)-compatible: reflected poly 0xEDB88320, init/xorout
 * 0xFFFFFFFF -- i.e. standard zlib CRC-32). Local implementation, no ROM/IDF dependency. ---- */
uint32_t linkhost_crc32(const uint8_t *buf, size_t len);

/* ---- binary STATUS decoder (host-testable): raw LT_STATUS_LEN record -> *out. ---- */
bool linkhost_status_decode(const uint8_t rec[LT_STATUS_LEN], lt_status_t *out);

/* ---- response parser: parses one already-assembled ---BEGIN/---END frame (as consumed off the
 * wire; leading noise tolerated) into *out. base64-decodes binary formats and verifies the CRC
 * over the decoded body. Returns 0, or LINKHOST_E_CRC/_PROTO. ---- */
int linkhost_parse_frame(const uint8_t *bytes, size_t n, linkhost_frame_t *out);

/* ---- the demux state machine + its stream ring / response slot (module-global state). ---- */
/* Clears the demux state, the stream ring and any pending response (call at test start / attach). */
void linkhost_reset(void);
/* Consumes raw UART bytes: routes 0xFF frames to the stream ring and ---BEGIN responses to the
 * assembler/parser, skipping echo/prompt/ESP_LOG/ERR/boot-log noise. Length-driven -- never scans
 * for the next 0xFF. Returns the number of bytes consumed (always n). */
size_t linkhost_feed(const uint8_t *bytes, size_t n);
/* Pops one demuxed stream record. 0 if one was returned, <0 if the ring is empty. */
int linkhost_stream_pop(lt_stream_rec_t *out);
/* Pops the most-recently-assembled framed response. Returns 1 and sets *status (0 or LINKHOST_E_*)
 * and *out (when status==0) if one was pending; 0 if none is pending. */
int linkhost_pop_response(linkhost_frame_t *out, int *status);

/* ================================================================================================
 *  Streaming session download (Plan 5.5): a pure, IDF-free state machine that parses one
 *  ---BEGIN/---END framed response WITHOUT ever buffering the whole body. Real session files are
 *  KB..MB (a `.log` was 46 KB) and blow past LINKHOST_ASM_MAX, so linkhost_cmd/linkhost_parse_frame
 *  cannot carry them. This machine is fed raw incoming bytes: it parses the header (exposing
 *  name+size), then streams the body -- base64-decoding on the fly for binary formats, carrying
 *  <=3 chars across feed boundaries -- invoking a caller chunk callback with each decoded block,
 *  accumulating a running CRC-32 over the DECODED bytes, and at ---END comparing it to the trailer.
 *  The chunk callback is the ONE intentional function pointer in this file: the streaming sink
 *  (httpd_resp_send_chunk on target). ---- */
#define LH_DL_CHUNK  512u    /* decoded bytes buffered before a chunk_cb call */
#define LH_DL_LINE   96u     /* header/trailer line accumulator (a BEGIN/END line is < 64 B) */

/* Streaming download state. LH_DL_DONE and LH_DL_ERR are terminal (state >= LH_DL_DONE);
 * LH_DL_HDR/_BODY/_TAIL are in-progress. */
typedef enum {
    LH_DL_HDR = 0,   /* scanning for / parsing the ---BEGIN header line */
    LH_DL_BODY,      /* streaming the body (base64-decoding when is_binary) */
    LH_DL_TAIL,      /* parsing the ---END <crc>--- trailer */
    LH_DL_DONE,      /* trailer parsed; crc_ok set (terminal) */
    LH_DL_ERR,       /* malformed framing or a callback abort (terminal) */
} lh_dl_state_t;

/* Chunk sink: fed each decoded block (<= LH_DL_CHUNK bytes). Returns non-zero to abort the
 * transfer (e.g. the transport closed); lh_dl_feed then stops and goes terminal (LH_DL_ERR). */
typedef int (*lh_dl_chunk_cb)(void *ctx, const uint8_t *data, size_t n);

typedef struct {
    lh_dl_state_t  state;
    bool           is_binary;    /* base64-decode the body (log/sum); raw text otherwise (json/vbo/nmea) */
    lh_dl_chunk_cb cb;
    void          *cb_ctx;

    /* ---- public: valid once the header has parsed (state >= LH_DL_BODY) ---- */
    char           name[16];     /* frame name from ---BEGIN <name> ... */
    uint32_t       body_size;    /* on-wire body byte count (base64 length for binary) */
    uint32_t       decoded_len;  /* running count of decoded bytes handed to cb */

    /* ---- internal ---- */
    char           line[LH_DL_LINE];
    size_t         line_len;
    bool           line_skip;    /* the current line overflowed -> skip to newline */
    uint32_t       body_got;     /* on-wire body bytes consumed */
    uint32_t       tail_seen;    /* bytes scanned in the trailer (bounded) */
    uint8_t        b64[4];       /* base64 carry across feed boundaries */
    size_t         b64_len;
    uint8_t        chunk[LH_DL_CHUNK];
    size_t         chunk_len;
    uint32_t       crc;          /* running CRC-32 register (pre-final-xor) over decoded bytes */
    bool           crc_ok;       /* set at ---END (valid when state == LH_DL_DONE) */
    int            result;       /* LINKHOST_E_* recorded for the LH_DL_ERR state */
    uint16_t       err_code;     /* remote `ERR 0x<code>` code (valid when result == LINKHOST_E_REMOTE) */
    char           err_msg[LINKHOST_ERRMSG_MAX];  /* the remote error message (truncated) */
} lh_dl_ctx_t;

/* Initialises a download context. `is_binary` selects on-the-fly base64 decoding of the body. */
void lh_dl_init(lh_dl_ctx_t *c, bool is_binary, lh_dl_chunk_cb cb, void *cb_ctx);
/* Feeds raw incoming bytes; invokes cb with each decoded block. Returns the (possibly terminal)
 * running state; stops consuming once terminal. */
lh_dl_state_t lh_dl_feed(lh_dl_ctx_t *c, const uint8_t *bytes, size_t n);
/* Terminal result: 0 (complete + CRC ok), LINKHOST_E_CRC / LINKHOST_E_PROTO, or LINKHOST_E_TIMEOUT
 * while still incomplete (not yet terminal). */
int lh_dl_result(const lh_dl_ctx_t *c);

/* ---- cmd-OTA flash (Task 6): the OTA-token parser + a pure, injectable state machine. ---- */
/* Mapped codes for the named OTA-ERR reasons (a bare 0x%04x reason passes through unchanged). */
enum {
    LT_OTA_ERR_TIMEOUT = 0x0001,
    LT_OTA_ERR_READ    = 0x0002,
    LT_OTA_ERR_USAGE   = 0x0003,
    LT_OTA_ERR_BADSIZE = 0x0004,
    LT_OTA_ERR_BADSHA  = 0x0005,
};

typedef enum {
    LT_OTA_NONE = 0,   /* the line carried no OTA token (noise) */
    LT_OTA_READY,      /* "OTA-READY" */
    LT_OTA_END,        /* "OTA-END 0x%04x"  -> *code (0x0000 == success) */
    LT_OTA_ERR,        /* "OTA-ERR <reason>" -> *code (mapped named reason or 0x%04x) */
} lt_ota_tok_t;

/* Classifies one (newline-stripped) console line as an OTA token; sets *code for END/ERR. */
lt_ota_tok_t linkhost_flash_parse_token(const char *line, uint16_t *code);

typedef enum {
    LH_FLASH_WAIT_READY = 0,   /* sent `ota recv`, awaiting OTA-READY */
    LH_FLASH_STREAMING,        /* OTA-READY seen; the image may be streamed */
    LH_FLASH_DONE_OK,          /* OTA-END 0x0000 */
    LH_FLASH_DONE_ERR,         /* OTA-ERR / OTA-END nonzero -> code */
} lh_flash_state_t;

typedef struct {
    lh_flash_state_t state;
    uint16_t         code;      /* final OTA code (valid in DONE_ERR; 0 in DONE_OK) */
    char             line[80];  /* line accumulator */
    size_t           line_len;
} lh_flash_ctx_t;

/* Resets a flash controller to LH_FLASH_WAIT_READY. */
void linkhost_flash_ctx_init(lh_flash_ctx_t *c);
/* Feeds console bytes from the lap-timer; advances the state as OTA tokens arrive. Returns state. */
lh_flash_state_t linkhost_flash_feed(lh_flash_ctx_t *c, const uint8_t *bytes, size_t n);
/* Maps the controller state to a linkhost_flash() return: 0 (OK), the OTA code (<err>), or
 * LINKHOST_E_TIMEOUT while still waiting/streaming. */
int linkhost_flash_result(const lh_flash_ctx_t *c);

#ifdef __cplusplus
}
#endif

#endif /* LINKHOST_PROTO_H */
