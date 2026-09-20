/* linkhost_proto.c -- linkhost's PURE, IDF-free logic (Plan 5.5 Task 3/6). No esp_* / driver / freertos:
 * this file builds host-side (devcontroller/test) and on B's IDF target. See linkhost_proto.h.
 *
 * Pragmatic P10 (Plan 5.5 Global Constraints): every loop has an explicit cap, functions >20 code
 * lines carry >=2 assertions, and no function pointers. The demux is strictly length-driven -- it
 * never scans for the next 0xFF (a §18.1 payload byte can be 0xFF); the Task-1 length prefix makes
 * stream framing unambiguous.
 */
#include "linkhost_proto.h"

#include <assert.h>
#include <string.h>

/* ================================================================================================
 *  CRC-32  (esp_rom_crc32_le(0, buf, len)-compatible == zlib CRC-32)
 * ============================================================================================== */
uint32_t linkhost_crc32(const uint8_t *buf, size_t len)
{
    assert(buf != NULL || len == 0);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {          /* bounded by len */
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {           /* bounded: 8 bit rounds */
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ================================================================================================
 *  Base64 decode (standard alphabet + '=' padding), bounded by input length
 * ============================================================================================== */
static int b64_val(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decodes `n` base64 chars from `src` into `dst` (cap `dst_cap`). Returns decoded length, or <0. */
static int b64_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t dst_cap)
{
    assert(src != NULL);
    assert(dst != NULL);
    if ((n & 3u) != 0) return -1;               /* base64 always arrives in 4-char quanta */
    size_t out = 0;
    for (size_t i = 0; i < n; i += 4) {          /* bounded by n/4 */
        int pad = 0;
        uint32_t acc = 0;
        for (int k = 0; k < 4; k++) {            /* bounded: 4 chars/quantum */
            uint8_t c = src[i + (size_t)k];
            if (c == '=') { acc <<= 6; pad++; continue; }
            int v = b64_val(c);
            if (v < 0 || pad != 0) return -1;    /* invalid char, or data after padding */
            acc = (acc << 6) | (uint32_t)v;
        }
        int nbytes = 3 - pad;                    /* pad 0->3, 1->2, 2->1 bytes */
        if (nbytes <= 0) return -1;
        if (out + (size_t)nbytes > dst_cap) return -1;
        if (nbytes > 0) dst[out++] = (uint8_t)(acc >> 16);
        if (nbytes > 1) dst[out++] = (uint8_t)(acc >> 8);
        if (nbytes > 2) dst[out++] = (uint8_t)(acc);
    }
    return (int)out;
}

/* ================================================================================================
 *  §18.2 STATUS record decoder
 * ============================================================================================== */
bool linkhost_status_decode(const uint8_t rec[LT_STATUS_LEN], lt_status_t *out)
{
    assert(rec != NULL);
    assert(out != NULL);
    if (!rec || !out) return false;

    out->proto    = rec[LT_ST_OFF_PROTO];
    out->state    = rec[LT_ST_OFF_STATE];
    out->flags    = (uint16_t)(rec[LT_ST_OFF_FLAGS] | ((uint16_t)rec[LT_ST_OFF_FLAGS + 1] << 8));
    out->batt_pct = rec[LT_ST_OFF_BATT_PCT];
    out->batt_mv  = (uint16_t)(rec[LT_ST_OFF_BATT_MV] | ((uint16_t)rec[LT_ST_OFF_BATT_MV + 1] << 8));
    out->free_kb  = (uint32_t)rec[LT_ST_OFF_FREE_KB]
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 1] << 8)
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 2] << 16)
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 3] << 24);
    out->sessions = (uint16_t)(rec[LT_ST_OFF_SESS] | ((uint16_t)rec[LT_ST_OFF_SESS + 1] << 8));
    memcpy(out->fw, &rec[LT_ST_OFF_FW], 7);
    out->fw[7] = '\0';
    return true;
}

/* ================================================================================================
 *  ---BEGIN <name> <size>--- header parsing (shared by the parser and the demux classifier)
 * ============================================================================================== */
static const uint8_t *mem_find(const uint8_t *hay, size_t hn, const char *needle, size_t nn)
{
    if (nn == 0 || hn < nn) return NULL;
    for (size_t i = 0; i + nn <= hn; i++) {      /* bounded by hn */
        if (memcmp(hay + i, needle, nn) == 0) return hay + i;
    }
    return NULL;
}

/* Whether a frame `name` carries a base64-encoded (binary) body. Mirrors export_serial's
 * fmt_from_str: STATUS is binary, session files ".log"/".sum" are binary; json/vbo/nmea and the
 * text acks (config/sessions/errlog/diag/...) are raw text. */
static bool frame_is_binary(const char *name)
{
    if (strcmp(name, "status") == 0) return true;
    size_t l = strlen(name);
    if (l >= 4) {
        const char *ext = name + (l - 4);
        if (strcmp(ext, ".log") == 0 || strcmp(ext, ".sum") == 0) return true;
    }
    return false;
}

/* Parses "---BEGIN <name> <size>---" out of `line` (length `len`, one line, trailing CR/LF ok).
 * Returns a pointer just past the closing "---" on success (NULL on mismatch), filling name/size.
 * `size` is capped at LINKHOST_ASM_MAX -- an oversize announcement fails (NULL). */
static const uint8_t *parse_begin(const uint8_t *line, size_t len, char *name, size_t name_cap,
                                  uint32_t *size)
{
    assert(name != NULL);
    assert(size != NULL);
    static const char PFX[] = LT_FRAME_BEGIN_PFX;          /* "---BEGIN " */
    size_t pl = sizeof(PFX) - 1;
    if (len < pl || memcmp(line, PFX, pl) != 0) return NULL;

    size_t i = pl;
    size_t ns = 0;
    while (i < len && line[i] != ' ' && ns + 1 < name_cap) name[ns++] = (char)line[i++];
    name[ns] = '\0';
    if (i >= len || line[i] != ' ' || ns == 0) return NULL;  /* need a space after the name */
    i++;

    if (i >= len || line[i] < '0' || line[i] > '9') return NULL;
    uint32_t v = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') {    /* bounded by len */
        v = v * 10u + (uint32_t)(line[i] - '0');
        if (v > LINKHOST_ASM_MAX) return NULL;               /* oversize / overflow guard */
        i++;
    }
    if (i + 3 > len || memcmp(line + i, "---", 3) != 0) return NULL;
    *size = v;
    return line + i + 3;
}

/* ================================================================================================
 *  Response frame parser
 * ============================================================================================== */
static uint8_t s_body[LINKHOST_ASM_MAX];   /* decoded body; linkhost_frame_t.body points here */

/* Reads 8 lowercase/uppercase hex digits at `p` (len `n`) into *out. Returns bytes consumed or 0. */
static size_t parse_hex8(const uint8_t *p, size_t n, uint32_t *out)
{
    if (n < 8) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {                 /* bounded: 8 hex digits */
        uint8_t c = p[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return 0;
        v = (v << 4) | d;
    }
    *out = v;
    return 8;
}

int linkhost_parse_frame(const uint8_t *bytes, size_t n, linkhost_frame_t *out)
{
    assert(bytes != NULL);
    assert(out != NULL);
    if (!bytes || !out) return LINKHOST_E_PROTO;

    /* 1. Locate the ---BEGIN anchor (leading echo/prompt/noise tolerated). */
    const uint8_t *begin = mem_find(bytes, n, LT_FRAME_BEGIN_PFX, sizeof(LT_FRAME_BEGIN_PFX) - 1);
    if (!begin) return LINKHOST_E_PROTO;
    size_t remain = (size_t)(bytes + n - begin);

    /* 2. Parse "<name> <size>---" out of the BEGIN line. */
    uint32_t size = 0;
    const uint8_t *p = parse_begin(begin, remain, out->name, sizeof(out->name), &size);
    if (!p) return LINKHOST_E_PROTO;

    /* 3. Skip to the byte after the BEGIN line's newline. */
    const uint8_t *end = bytes + n;
    while (p < end && *p != '\n') p++;            /* bounded by remaining bytes */
    if (p >= end) return LINKHOST_E_PROTO;
    p++;                                          /* past '\n' */

    /* 4. Exactly `size` body bytes must be present. */
    if ((size_t)(end - p) < size) return LINKHOST_E_PROTO;
    const uint8_t *body = p;
    p += size;

    /* 5. Then the tail: optional CR/LF, then "---END <8hex>---". */
    const uint8_t *tail = mem_find(p, (size_t)(end - p), LT_FRAME_END_PFX,
                                   sizeof(LT_FRAME_END_PFX) - 1);
    if (!tail) return LINKHOST_E_PROTO;
    /* the CR/LF gap between body and ---END must be tiny (\r\n) -- reject a stray body match */
    if ((size_t)(tail - p) > 2) return LINKHOST_E_PROTO;
    const uint8_t *hp = tail + (sizeof(LT_FRAME_END_PFX) - 1);
    uint32_t crc_want = 0;
    if (parse_hex8(hp, (size_t)(end - hp), &crc_want) != 8) return LINKHOST_E_PROTO;

    /* 6. Decode the body (base64 for binary formats) into the static buffer. */
    size_t body_len;
    if (frame_is_binary(out->name)) {
        int dl = b64_decode(body, size, s_body, sizeof(s_body));
        if (dl < 0) return LINKHOST_E_PROTO;
        body_len = (size_t)dl;
    } else {
        if (size > sizeof(s_body)) return LINKHOST_E_PROTO;
        memcpy(s_body, body, size);
        body_len = size;
    }

    /* 7. CRC over the decoded body. */
    if (linkhost_crc32(s_body, body_len) != crc_want) return LINKHOST_E_CRC;

    out->size     = size;
    out->body     = s_body;
    out->body_len = body_len;
    return 0;
}

/* ================================================================================================
 *  Demux state machine + stream ring + response slot (module-global; SPSC lock-free)
 * ============================================================================================== */
#define RING_CAP    32u                             /* power of two */
#define LINE_CAP    64u                             /* a ---BEGIN header line is < 50 B */
#define FRAME_CAP   (LINKHOST_ASM_MAX + 128u)        /* header + body + tail of one framed response */
#define STREAM_CAP  256u                             /* one 0xFF payload (len is a u8) */

typedef enum {
    DX_SCAN = 0,     /* between frames: expect 0xFF or the first byte of a line */
    DX_LINE,         /* accumulating a line to classify (---BEGIN vs noise) */
    DX_SKIP,         /* discarding a noise line until '\n' */
    DX_RESP_BODY,    /* copying `size` body bytes into the frame buffer */
    DX_RESP_TAIL,    /* copying the ---END tail line until '\n' */
    DX_STREAM_HDR,   /* collecting the 5-byte 0xFF header */
    DX_STREAM_BODY,  /* collecting `len` payload bytes */
} dx_state_t;

static struct {
    dx_state_t state;

    uint8_t  line[LINE_CAP];
    size_t   line_len;

    uint8_t  frame[FRAME_CAP];
    size_t   frame_len;
    uint32_t body_need;
    uint32_t body_got;
    size_t   tail_off;    /* frame offset where the ---END tail begins (body-relative scan anchor) */

    uint8_t  sh[5];       /* stream header */
    size_t   sh_got;
    uint8_t  sp[STREAM_CAP];
    size_t   sp_need;
    size_t   sp_got;

    /* stream ring (SPSC: producer = feed, consumer = stream_pop) */
    lt_stream_rec_t ring[RING_CAP];
    volatile uint32_t ring_head;   /* written by feed */
    volatile uint32_t ring_tail;   /* written by stream_pop */

    /* response slot (SPSC: producer = feed, consumer = pop_response) */
    linkhost_frame_t resp;
    int              resp_status;
    volatile bool    resp_ready;
} s_dx;

void linkhost_reset(void)
{
    memset(&s_dx, 0, sizeof(s_dx));
    s_dx.state = DX_SCAN;
}

/* SES_T_* range known to the current lap-timer (core/ses.h: 0x01..0x0E, plus 0x7F END). Unknown
 * types are still consumed length-first, then dropped -- the demux stays synced regardless. */
static bool stream_type_known(uint8_t t)
{
    return (t >= 0x01 && t <= 0x0E) || t == 0x7F;
}

static void ring_push(const lt_stream_rec_t *r)
{
    uint32_t head = s_dx.ring_head;
    if ((uint32_t)(head - s_dx.ring_tail) >= RING_CAP) return;   /* full: drop newest */
    s_dx.ring[head & (RING_CAP - 1u)] = *r;
    s_dx.ring_head = head + 1u;                                  /* publish after the store */
}

int linkhost_stream_pop(lt_stream_rec_t *out)
{
    assert(out != NULL);
    uint32_t tail = s_dx.ring_tail;
    if (tail == s_dx.ring_head) return -1;                       /* empty */
    *out = s_dx.ring[tail & (RING_CAP - 1u)];
    s_dx.ring_tail = tail + 1u;
    return 0;
}

int linkhost_pop_response(linkhost_frame_t *out, int *status)
{
    assert(out != NULL);
    assert(status != NULL);
    if (!s_dx.resp_ready) return 0;
    *status = s_dx.resp_status;
    if (s_dx.resp_status == 0) *out = s_dx.resp;
    s_dx.resp_ready = false;
    return 1;
}

/* Emits one assembled stream record from sh[]/sp[] to the ring (dropping unknown types). */
static void stream_emit(void)
{
    uint8_t len = s_dx.sh[4];
    if (len == 0) return;                       /* no type byte -> nothing to route */
    uint8_t type = s_dx.sp[0];
    uint32_t datalen = (uint32_t)len - 1u;
    if (!stream_type_known(type) || datalen > LT_REC_MAX) return;  /* consume + drop */
    lt_stream_rec_t r;
    r.seq   = (uint16_t)(s_dx.sh[1] | ((uint16_t)s_dx.sh[2] << 8));
    r.flags = s_dx.sh[3];
    r.type  = type;
    r.len   = (uint8_t)datalen;
    memset(r.data, 0, sizeof(r.data));
    if (datalen > 0) memcpy(r.data, &s_dx.sp[1], datalen);
    ring_push(&r);
}

/* Classifies a completed line (in s_dx.line, length s_dx.line_len). If it is a ---BEGIN header,
 * seeds the RESP_BODY state; otherwise the line is noise and we return to SCAN. */
static void classify_line(void)
{
    char name[16];
    uint32_t size = 0;
    const uint8_t *r = parse_begin(s_dx.line, s_dx.line_len, name, sizeof(name), &size);
    if (!r) { s_dx.state = DX_SCAN; return; }         /* echo/prompt/log/ERR/boot -> drop */

    /* Re-emit the header line (plus a '\n') into the frame buffer for linkhost_parse_frame. */
    if (s_dx.line_len + 1u > FRAME_CAP) { s_dx.state = DX_SCAN; return; }
    memcpy(s_dx.frame, s_dx.line, s_dx.line_len);
    s_dx.frame[s_dx.line_len] = '\n';
    s_dx.frame_len = s_dx.line_len + 1u;
    s_dx.body_need = size;
    s_dx.body_got  = 0;
    s_dx.state = DX_RESP_BODY;
}

static void frame_push(uint8_t b)
{
    if (s_dx.frame_len < FRAME_CAP) s_dx.frame[s_dx.frame_len++] = b;
}

size_t linkhost_feed(const uint8_t *bytes, size_t n)
{
    assert(bytes != NULL || n == 0);
    assert(s_dx.state <= DX_STREAM_BODY);         /* the demux never leaves its state set */
    for (size_t i = 0; i < n; i++) {              /* bounded by n */
        uint8_t b = bytes[i];
        switch (s_dx.state) {
        case DX_SCAN:
            if (b == LT_STREAM_TAG) { s_dx.sh[0] = b; s_dx.sh_got = 1; s_dx.state = DX_STREAM_HDR; }
            else if (b == '\n' || b == '\r') { /* blank separators -> stay */ }
            else { s_dx.line[0] = b; s_dx.line_len = 1; s_dx.state = DX_LINE; }
            break;

        case DX_LINE:
            if (b == LT_STREAM_TAG) {            /* ASCII noise never holds 0xFF -> a stream frame */
                s_dx.sh[0] = b; s_dx.sh_got = 1; s_dx.state = DX_STREAM_HDR;
            } else if (b == '\n') {
                if (s_dx.line_len > 0 && s_dx.line[s_dx.line_len - 1] == '\r') s_dx.line_len--;
                classify_line();
            } else if (s_dx.line_len < LINE_CAP) {
                s_dx.line[s_dx.line_len++] = b;
            } else {
                s_dx.state = DX_SKIP;           /* too long to be a header -> noise */
            }
            break;

        case DX_SKIP:
            if (b == LT_STREAM_TAG) { s_dx.sh[0] = b; s_dx.sh_got = 1; s_dx.state = DX_STREAM_HDR; }
            else if (b == '\n') { s_dx.state = DX_SCAN; }
            break;

        case DX_RESP_BODY:
            if (b == LT_STREAM_TAG) {            /* desync: a body is ASCII/base64, never 0xFF */
                s_dx.sh[0] = b; s_dx.sh_got = 1; s_dx.state = DX_STREAM_HDR;
            } else {
                frame_push(b);
                if (++s_dx.body_got >= s_dx.body_need) {
                    s_dx.tail_off = s_dx.frame_len;   /* anchor the ---END scan past the body */
                    s_dx.state = DX_RESP_TAIL;
                }
            }
            break;

        case DX_RESP_TAIL:
            if (b == LT_STREAM_TAG) {            /* desync mid-tail -> resync on the stream */
                s_dx.sh[0] = b; s_dx.sh_got = 1; s_dx.state = DX_STREAM_HDR;
            } else {
                frame_push(b);
                if (b == '\n'
                    && mem_find(s_dx.frame + s_dx.tail_off, s_dx.frame_len - s_dx.tail_off,
                                LT_FRAME_END_PFX, sizeof(LT_FRAME_END_PFX) - 1) != NULL) {
                    int st = linkhost_parse_frame(s_dx.frame, s_dx.frame_len, &s_dx.resp);
                    s_dx.resp_status = st;
                    s_dx.resp_ready = true;
                    s_dx.state = DX_SCAN;
                } else if (s_dx.frame_len >= FRAME_CAP) {
                    s_dx.state = DX_SCAN;       /* runaway tail -> drop, re-sync */
                }
            }
            break;

        case DX_STREAM_HDR:
            s_dx.sh[s_dx.sh_got++] = b;         /* length-driven: read exactly 5 header bytes */
            if (s_dx.sh_got >= 5) {
                s_dx.sp_need = s_dx.sh[4];
                s_dx.sp_got = 0;
                if (s_dx.sp_need == 0) { stream_emit(); s_dx.state = DX_SCAN; }
                else s_dx.state = DX_STREAM_BODY;
            }
            break;

        case DX_STREAM_BODY:
            if (s_dx.sp_got < STREAM_CAP) s_dx.sp[s_dx.sp_got] = b;   /* length-driven, no scan */
            s_dx.sp_got++;
            if (s_dx.sp_got >= s_dx.sp_need) { stream_emit(); s_dx.state = DX_SCAN; }
            break;

        default:
            s_dx.state = DX_SCAN;
            break;
        }
    }
    return n;
}

/* ================================================================================================
 *  cmd-OTA flash token parser + injectable state machine
 * ============================================================================================== */
static bool tok_is(const char *line, const char *tok)
{
    return strcmp(line, tok) == 0;
}

/* Parses "0x%04x" (exactly, after skipping an optional "0x") at *pp; returns true + sets *code. */
static bool parse_ota_hex(const char *s, uint16_t *code)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t v = 0;
    int nd = 0;
    for (int i = 0; i < 8 && s[i]; i++) {         /* bounded: <=8 hex digits */
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
        nd++;
    }
    if (nd == 0) return false;
    *code = (uint16_t)v;
    return true;
}

lt_ota_tok_t linkhost_flash_parse_token(const char *line, uint16_t *code)
{
    assert(line != NULL);
    assert(code != NULL);
    *code = 0;
    if (tok_is(line, "OTA-READY")) return LT_OTA_READY;

    if (strncmp(line, "OTA-END ", 8) == 0) {
        if (!parse_ota_hex(line + 8, code)) return LT_OTA_NONE;
        return LT_OTA_END;
    }
    if (strncmp(line, "OTA-ERR ", 8) == 0) {
        const char *r = line + 8;
        if (tok_is(r, "timeout")) { *code = LT_OTA_ERR_TIMEOUT; return LT_OTA_ERR; }
        if (tok_is(r, "read"))    { *code = LT_OTA_ERR_READ;    return LT_OTA_ERR; }
        if (tok_is(r, "usage"))   { *code = LT_OTA_ERR_USAGE;   return LT_OTA_ERR; }
        if (tok_is(r, "badsize")) { *code = LT_OTA_ERR_BADSIZE; return LT_OTA_ERR; }
        if (tok_is(r, "badsha"))  { *code = LT_OTA_ERR_BADSHA;  return LT_OTA_ERR; }
        if (parse_ota_hex(r, code)) return LT_OTA_ERR;
        return LT_OTA_NONE;
    }
    return LT_OTA_NONE;
}

void linkhost_flash_ctx_init(lh_flash_ctx_t *c)
{
    assert(c != NULL);
    memset(c, 0, sizeof(*c));
    c->state = LH_FLASH_WAIT_READY;
}

/* Applies one completed line to the flash state machine. */
static void flash_apply_line(lh_flash_ctx_t *c)
{
    uint16_t code = 0;
    lt_ota_tok_t t = linkhost_flash_parse_token(c->line, &code);
    if (t == LT_OTA_NONE) return;               /* noise between tokens */

    if (t == LT_OTA_READY) {
        if (c->state == LH_FLASH_WAIT_READY) c->state = LH_FLASH_STREAMING;
        return;
    }
    if (t == LT_OTA_END) {
        c->code = code;
        c->state = (code == 0) ? LH_FLASH_DONE_OK : LH_FLASH_DONE_ERR;
        return;
    }
    /* LT_OTA_ERR */
    c->code = code;
    c->state = LH_FLASH_DONE_ERR;
}

lh_flash_state_t linkhost_flash_feed(lh_flash_ctx_t *c, const uint8_t *bytes, size_t n)
{
    assert(c != NULL);
    assert(bytes != NULL || n == 0);
    for (size_t i = 0; i < n; i++) {              /* bounded by n */
        uint8_t b = bytes[i];
        if (b == '\n' || b == '\r') {
            if (c->line_len > 0) {
                c->line[c->line_len] = '\0';
                flash_apply_line(c);
                c->line_len = 0;
            }
        } else if (c->line_len + 1 < sizeof(c->line)) {
            c->line[c->line_len++] = (char)b;
        } else {
            c->line_len = 0;                     /* overlong noise line -> drop, stay synced */
        }
        if (c->state == LH_FLASH_DONE_OK || c->state == LH_FLASH_DONE_ERR) {
            /* keep consuming the rest but the result is latched */
        }
    }
    return c->state;
}

int linkhost_flash_result(const lh_flash_ctx_t *c)
{
    assert(c != NULL);
    switch (c->state) {
    case LH_FLASH_DONE_OK:  return 0;
    case LH_FLASH_DONE_ERR: return (int)c->code;
    default:                return LINKHOST_E_TIMEOUT;   /* still waiting/streaming */
    }
}
