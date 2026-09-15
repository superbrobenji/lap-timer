#include "core/ses.h"
#include "core/core.h"
#include <string.h>

uint16_t ses_crc16(const uint8_t *buf, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

int ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap)
{
    if (len > SES_MAX_PAYLOAD) return -1;
    size_t total = (size_t)len + SES_FRAME_OVERHEAD;
    if (cap < total) return -1;
    out[0] = SES_SYNC; out[1] = type; out[2] = len;
    if (len) memcpy(out + 3, payload, len);
    uint16_t crc = ses_crc16(out + 1, (size_t)2 + len);
    out[3 + len] = (uint8_t)crc;
    out[4 + len] = (uint8_t)(crc >> 8);
    return (int)total;
}

void ses_reader_init(ses_reader_t *r) { memset(r, 0, sizeof *r); }

/* returns 0 = continue, 1 = frame complete and valid, 2 = frame bad */
static int step(ses_reader_t *r, uint8_t b)
{
    if (r->state == 0) {
        if (b == SES_SYNC) { r->state = 1; r->idx = 0; r->need = 0; }
        return 0;
    }
    r->buf[r->idx++] = b;
    if (r->idx == 2) {
        uint8_t len = r->buf[1];
        if (len > SES_MAX_PAYLOAD) return 2;
        r->need = (uint16_t)(2 + len + 2);
    }
    if (r->need && r->idx == r->need) {
        uint8_t len = r->buf[1];
        uint16_t crc = ses_crc16(r->buf, (size_t)2 + len);
        uint16_t got = (uint16_t)(r->buf[2 + len] | (r->buf[3 + len] << 8));
        return (crc == got) ? 1 : 2;
    }
    return 0;
}

static void on_bad(ses_reader_t *r)
{
    /* Re-scan everything collected after the sync byte, plus whatever replay input was still pending. */
    uint16_t collected = r->idx;
    uint16_t pending = (uint16_t)(r->replay_len - r->replay_pos);
    /* Reset the reader state first: whether or not the bounds guard below trips, a caller must be
     * able to keep feeding bytes afterwards without the reader staying wedged mid-frame. */
    r->frames_bad++;
    r->state = 0; r->idx = 0; r->need = 0;
    /* collected <= 2+SES_MAX_PAYLOAD+2 and pending is what is left of an equally bounded replay,
     * so the sum fits the 2x-sized replay buffer. Checked rather than assumed: a corrupted reader
     * struct must not turn into a memcpy past the end. */
    CORE_ASSERT_VOID((size_t)collected + pending <= sizeof r->replay, 0x0A02);
    uint8_t tmp[sizeof r->replay];
    memcpy(tmp, r->buf, collected);
    memcpy(tmp + collected, r->replay + r->replay_pos, pending);
    r->replay_len = (uint16_t)(collected + pending);
    r->replay_pos = 0;
    memcpy(r->replay, tmp, r->replay_len);
}

void ses_reader_feed(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx)
{
    size_t in_pos = 0;
    for (;;) {
        uint8_t b;
        if (r->replay_pos < r->replay_len) b = r->replay[r->replay_pos++];
        else if (in_pos < n) b = buf[in_pos++];
        else break;
        int st = step(r, b);
        if (st == 1) {
            r->frames_ok++;
            cb(r->buf[0], r->buf + 2, r->buf[1], ctx);
            r->state = 0; r->idx = 0; r->need = 0;
        } else if (st == 2) {
            on_bad(r);
        }
    }
    /* The loop exits only once the replay is drained and the input consumed; reset for the next call. */
    r->replay_pos = 0; r->replay_len = 0;
}

void ses_reader_flush(ses_reader_t *r, ses_frame_cb_t cb, void *ctx)
{
    /* A partial frame at EOF can never complete: discard its sync byte and rescan the rest.
     * Each round consumes at least one byte, so this terminates. */
    while (r->state == 1) {
        on_bad(r);
        ses_reader_feed(r, NULL, 0, cb, ctx);
    }
}
