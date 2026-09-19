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

typedef struct {
    uint8_t  state;                           /* 0 = hunting sync, 1 = collecting */
    uint16_t idx;                             /* bytes collected into buf */
    uint16_t need;                            /* total bytes expected in buf once len is known */
    uint8_t  buf[2 + SES_MAX_PAYLOAD + 2];    /* type, len, payload, crc */
    uint8_t  replay[2 * (2 + SES_MAX_PAYLOAD + 2)];    /* rescan buffer; proven bound is 2+247+2 bytes, kept at 2x for headroom */
    uint16_t replay_len, replay_pos;
    const uint8_t *in;                        /* staged input for the current push (aliases caller's buffer) */
    size_t   in_len, in_pos;                  /* staged length and consume cursor */
    uint8_t  finishing;                       /* set by ses_reader_finish: drain a trailing partial frame at EOF */
    uint32_t frames_ok, frames_bad;
} ses_reader_t;

void ses_reader_init(ses_reader_t *r);
/* Stage n bytes for pulling. Call ses_reader_next() until it returns 0 before the next push;
 * `buf` must stay valid while it is being pulled (its bytes are read, not copied). */
void ses_reader_push(ses_reader_t *r, const uint8_t *buf, size_t n);
/* Pull the next decoded frame from the staged input, resynchronising past corruption. Returns
 * 1 = a frame is ready: *type, *len are set and *payload points at the reader's internal buffer,
 *     valid ONLY until the next push/next/finish call -- copy what you need before calling again;
 * 0 = the staged input (and any pending rescan) is exhausted: push more, or call finish() at EOF.
 * A corrupt/incomplete frame is skipped internally (counted in frames_bad) and never surfaces as a
 * distinct return, so the standard drain loop is `while (ses_reader_next(...) == 1) { ... }`. The
 * -1 return is reserved for a future hard-error surface; this resync policy does not produce it. */
int  ses_reader_next(ses_reader_t *r, uint8_t *type, const uint8_t **payload, uint8_t *len);
/* Call once at the end of a bounded input (a file), then drain with next(). A frame that can never
 * complete is treated as bad and the bytes after its sync are rescanned, so a valid frame hidden
 * behind a spurious sync near EOF is still recovered by the trailing next() calls. Idempotent when
 * the reader is idle; after the post-finish drain, next() returns 0. */
void ses_reader_finish(ses_reader_t *r);

/* ---- Record codecs (spec §12.3, §12.4) ---- */

typedef struct {
    bool      have_prev;
    gps_fix_t prev;               /* reconstructed previous fix (what a decoder holds) */
    int64_t   last_key_gps_us;
    bool      prev_valid;
} ses_fix_state_t;
void ses_fix_state_init(ses_fix_state_t *st);
/* Chooses FIX_KEY or FIX_DELTA. Returns frame length or -1. */
int  ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *fix, uint8_t *out, size_t cap);
/* Returns 1 and fills out for FIX_KEY/FIX_DELTA; 0 for other types; -1 on malformed. */
int  ses_decode_fix(ses_fix_state_t *st, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out);

typedef struct { int64_t ref_gps_us; bool have_ref; } ses_fused_state_t;
void ses_fused_state_init(ses_fused_state_t *st);
void ses_fused_state_on_fix(ses_fused_state_t *st, int64_t fix_gps_us);   /* call on both sides when a FIX_* passes */
int  ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap);
int  ses_decode_fused(ses_fused_state_t *st, const uint8_t *payload, uint8_t len, fused_sample_t *out);

int  ses_encode_lap(const lap_result_t *lap, uint8_t *out, size_t cap);
int  ses_decode_lap(const uint8_t *payload, uint8_t len, lap_result_t *out);

/* Every decoder below validates `len` exactly and returns 1 on success, -1 on a malformed payload.
 * The structs mirror the wire payloads of §12.3; char arrays carry one extra byte so the decoded
 * value is always NUL-terminated (the wire size is unchanged). */
typedef struct { uint16_t lap_no; uint8_t idx; int64_t gps_us; uint32_t split_ms; int32_t delta_ms; } ses_sector_t;
typedef struct { uint16_t run_no; uint8_t gate_id; int64_t gps_us; uint32_t time_ms; uint16_t speed_cms; uint32_t dist_cm; } ses_drag_gate_t;
typedef struct { int64_t mono_us, gps_us; uint16_t code; uint32_t arg; } ses_event_t;
typedef struct { int64_t mono_us, gps_us; uint8_t quality; } ses_time_map_t;
typedef struct { uint16_t venue_id, layout_id; char name[33]; } ses_venue_t;     /* name is char[32] on the wire */
typedef struct { int64_t mono_us; uint8_t state; uint16_t batt_mv; } ses_power_t;
typedef struct { int64_t gps_us; uint8_t reason; } ses_end_t;
typedef struct { int64_t gps_us; uint8_t kind; } ses_mark_t;
typedef struct { int16_t r_e4[9]; int16_t gbias[3]; uint8_t calib_flags; } ses_calib_t;   /* 25 B payload */

int  ses_encode_sector(uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms, uint8_t *out, size_t cap);
int  ses_decode_sector(const uint8_t *payload, uint8_t len, ses_sector_t *out);
int  ses_encode_drag_run(const drag_result_t *run, uint8_t *out, size_t cap);
int  ses_decode_drag_run(const uint8_t *payload, uint8_t len, drag_result_t *out);
int  ses_encode_drag_gate(uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm, uint8_t *out, size_t cap);
int  ses_decode_drag_gate(const uint8_t *payload, uint8_t len, ses_drag_gate_t *out);
int  ses_encode_event(int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg, uint8_t *out, size_t cap);
int  ses_decode_event(const uint8_t *payload, uint8_t len, ses_event_t *out);
int  ses_encode_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality, uint8_t *out, size_t cap);
int  ses_decode_time_map(const uint8_t *payload, uint8_t len, ses_time_map_t *out);
int  ses_encode_venue(uint16_t venue_id, uint16_t layout_id, const char *name, uint8_t *out, size_t cap);
int  ses_decode_venue(const uint8_t *payload, uint8_t len, ses_venue_t *out);
int  ses_encode_power(int64_t mono_us, uint8_t state, uint16_t batt_mv, uint8_t *out, size_t cap);
int  ses_decode_power(const uint8_t *payload, uint8_t len, ses_power_t *out);
int  ses_encode_end(int64_t gps_us, uint8_t reason, uint8_t *out, size_t cap);
int  ses_decode_end(const uint8_t *payload, uint8_t len, ses_end_t *out);
int  ses_encode_mark(int64_t gps_us, uint8_t kind, uint8_t *out, size_t cap);
int  ses_decode_mark(const uint8_t *payload, uint8_t len, ses_mark_t *out);
int  ses_encode_calib(const ses_calib_t *c, uint8_t *out, size_t cap);
int  ses_decode_calib(const uint8_t *payload, uint8_t len, ses_calib_t *out);

typedef struct {
    char     session_id[11];      /* char[10] on the wire + NUL */
    uint8_t  mode, variant;
    uint16_t venue_id, layout_id;
    char     fw[17];              /* char[16] on the wire + NUL */
    char     hwid[25];            /* char[24] on the wire + NUL */
    uint8_t  log_profile, fused_hz, gps_hz;
    int64_t  start_gps_us;
    int16_t  r_e4[9];             /* rotation matrix × 1e4, row-major */
    int16_t  gbias[3];
    uint8_t  calib_flags;
} ses_hdr_t;
int  ses_encode_hdr(const ses_hdr_t *h, uint8_t *out, size_t cap);
int  ses_decode_hdr(const uint8_t *payload, uint8_t len, ses_hdr_t *out);
#endif
