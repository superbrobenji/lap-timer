#ifndef CORE_SES_H
#define CORE_SES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"

/* Record types (spec §12.3) */
enum {
    SES_T_SESSION_HDR = 0x01, SES_T_FIX_KEY = 0x02, SES_T_FIX_DELTA = 0x03, SES_T_FUSED = 0x04,
    SES_T_LAP = 0x05, SES_T_SECTOR = 0x06, SES_T_DRAG_RUN = 0x07, SES_T_DRAG_GATE = 0x08,
    SES_T_EVENT = 0x09, SES_T_CALIB = 0x0A, SES_T_MARK = 0x0B, SES_T_TIME_MAP = 0x0C,
    SES_T_VENUE = 0x0D, SES_T_POWER = 0x0E, SES_T_END = 0x7F
};

#define SES_FRAME_OVERHEAD 5      /* sync + type + len + crc16 */

uint16_t ses_crc16(const uint8_t *buf, size_t n);
/* Writes sync|type|len|payload|crc16 into out. Returns bytes written, or -1 if cap is too small or len > SES_MAX_PAYLOAD. */
int      ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap);

/* Invoked once per valid frame. `payload` points into the reader's internal buffer and is valid
 * only for the duration of the callback: copy what you need before returning. */
typedef void (*ses_frame_cb_t)(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx);

typedef struct {
    uint8_t  state;                           /* 0 = hunting sync, 1 = collecting */
    uint16_t idx;                             /* bytes collected into buf */
    uint16_t need;                            /* total bytes expected in buf once len is known */
    uint8_t  buf[2 + SES_MAX_PAYLOAD + 2];    /* type, len, payload, crc */
    uint8_t  replay[2 * (2 + SES_MAX_PAYLOAD + 2)];    /* rescan buffer; proven bound is 2+247+2 bytes, kept at 2x for headroom */
    uint16_t replay_len, replay_pos;
    uint32_t frames_ok, frames_bad;
} ses_reader_t;

void ses_reader_init(ses_reader_t *r);
/* Feed any number of bytes; cb is invoked once per valid frame. Resynchronises after corruption. */
void ses_reader_feed(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx);
/* Call once at the end of a bounded input (a file). A frame that can never complete is treated as
 * bad and the bytes after its sync are rescanned, so a valid frame hidden behind a spurious sync
 * near EOF is still recovered. Idempotent when the reader is idle. */
void ses_reader_flush(ses_reader_t *r, ses_frame_cb_t cb, void *ctx);

/* Record codecs are declared in Task 7 below this line. */
#endif
