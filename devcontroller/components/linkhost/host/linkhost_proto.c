/* linkhost_proto.c -- linkhost's PURE, IDF-free logic (Plan 5.5 Task 3/6). No esp_* / driver / freertos:
 * this file builds host-side (devcontroller/test) and on B's IDF target. See linkhost_proto.h.
 *
 * Pragmatic P10 (Plan 5.5 Global Constraints): every loop has an explicit cap and functions >20
 * code lines carry >=2 assertions. Function pointers are avoided except for the ONE deliberate
 * streaming sink -- lh_dl_*'s chunk callback (a whole session file cannot be buffered to return by
 * value). The demux is strictly length-driven -- it never scans for the next 0xFF (a §18.1 payload
 * byte can be 0xFF); the Task-1 length prefix makes stream framing unambiguous.
 */
#include "linkhost_proto.h"

#include <assert.h>
#include <string.h>

/* ================================================================================================
 *  CRC-32  (esp_rom_crc32_le(0, buf, len)-compatible == zlib CRC-32)
 * ============================================================================================== */
/* Folds `len` bytes into a running CRC-32 register (pre-final-xor). Seed with 0xFFFFFFFF; the
 * caller xors with 0xFFFFFFFF at the end. Lets the streaming download accumulate a CRC block by
 * block without buffering the whole body. */
static uint32_t crc32_feed(uint32_t crc, const uint8_t *buf, size_t len)
{
    assert(buf != NULL || len == 0);
    for (size_t i = 0; i < len; i++) {          /* bounded by len */
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {           /* bounded: 8 bit rounds */
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}

uint32_t linkhost_crc32(const uint8_t *buf, size_t len)
{
    assert(buf != NULL || len == 0);
    return crc32_feed(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
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

/* Decodes one 4-char base64 quantum `g` into `out` (up to 3 bytes). Returns byte count (1..3), or
 * <0 on an invalid char / misplaced padding. Shared by the buffered and the streaming decoders. */
static int b64_quantum(const uint8_t g[4], uint8_t out[3])
{
    assert(g != NULL);
    assert(out != NULL);
    int pad = 0;
    uint32_t acc = 0;
    for (int k = 0; k < 4; k++) {                /* bounded: 4 chars/quantum */
        uint8_t c = g[k];
        if (c == '=') { acc <<= 6; pad++; continue; }
        int v = b64_val(c);
        if (v < 0 || pad != 0) return -1;        /* invalid char, or data after padding */
        acc = (acc << 6) | (uint32_t)v;
    }
    int nbytes = 3 - pad;                        /* pad 0->3, 1->2, 2->1 bytes */
    if (nbytes <= 0) return -1;
    out[0] = (uint8_t)(acc >> 16);
    if (nbytes > 1) out[1] = (uint8_t)(acc >> 8);
    if (nbytes > 2) out[2] = (uint8_t)(acc);
    return nbytes;
}

/* Decodes `n` base64 chars from `src` into `dst` (cap `dst_cap`). Returns decoded length, or <0. */
static int b64_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t dst_cap)
{
    assert(src != NULL);
    assert(dst != NULL);
    if ((n & 3u) != 0) return -1;               /* base64 always arrives in 4-char quanta */
    size_t out = 0;
    for (size_t i = 0; i < n; i += 4) {          /* bounded by n/4 */
        uint8_t tmp[3];
        int nbytes = b64_quantum(src + i, tmp);
        if (nbytes < 0) return -1;
        if (out + (size_t)nbytes > dst_cap) return -1;
        for (int k = 0; k < nbytes; k++) dst[out++] = tmp[k];   /* bounded: <=3 */
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

/* Parses a non-framed error line "ERR 0x<code>: <msg>" -- what export_serial.c prints on any
 * command failure (malformed json, unknown session, storage error, second-pass abort). Returns
 * true and fills *code + msg[msg_cap] (NUL-terminated, truncated) on a match; false otherwise.
 * Shared by the buffered demux (classify_line) and the streaming download parser (dl_feed_hdr) so
 * both fail fast with a real remote error instead of stalling on the link timeout. */
static bool parse_err_line(const uint8_t *line, size_t len, uint16_t *code, char *msg, size_t msg_cap)
{
    assert(code != NULL);
    assert(msg != NULL || msg_cap == 0);
    static const char PFX[] = "ERR 0x";
    size_t pl = sizeof(PFX) - 1;
    if (len < pl || memcmp(line, PFX, pl) != 0) return false;

    size_t i = pl;
    uint32_t v = 0;
    int nd = 0;
    while (i < len && nd < 4) {                    /* bounded: <=4 hex digits (u16 code) */
        uint8_t c = line[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
        i++;
        nd++;
    }
    if (nd == 0 || i >= len || line[i] != ':') return false;   /* need "<hex>:" */
    i++;
    if (i < len && line[i] == ' ') i++;            /* skip the one separator space */
    *code = (uint16_t)v;

    size_t o = 0;
    while (i < len && o + 1u < msg_cap) msg[o++] = (char)line[i++];   /* bounded by len */
    if (msg_cap > 0) msg[o] = '\0';
    return true;
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
 *  Streaming ---BEGIN/---END download (lh_dl_*): parses one framed response WITHOUT buffering the
 *  whole body. Fed raw incoming bytes; base64-decodes binary bodies on the fly (carrying <=3 chars
 *  across feeds); hands each decoded block to the caller chunk_cb; verifies a running CRC-32 at
 *  ---END. Reuses parse_hex8 / b64_quantum / crc32_feed above.
 * ============================================================================================== */
#define LH_DL_TAIL_MAX  128u   /* bytes tolerated after the body before ---END (bound) */

/* Parses "---BEGIN <name> <size>---" out of a completed header line. UNLIKE parse_begin() this
 * does NOT cap the size at LINKHOST_ASM_MAX (the whole point is to stream oversize bodies); only a
 * u32 overflow guards it. Returns true + fills name/size on a match. */
static bool dl_parse_begin(const char *line, size_t len, char *name, size_t name_cap,
                           uint32_t *size_out)
{
    assert(name != NULL);
    assert(size_out != NULL);
    static const char PFX[] = LT_FRAME_BEGIN_PFX;          /* "---BEGIN " */
    size_t pl = sizeof(PFX) - 1;
    if (len < pl || memcmp(line, PFX, pl) != 0) return false;

    size_t i = pl, ns = 0;
    while (i < len && line[i] != ' ' && ns + 1 < name_cap) name[ns++] = line[i++];  /* bounded by len */
    name[ns] = '\0';
    if (i >= len || line[i] != ' ' || ns == 0) return false;
    i++;

    if (i >= len || line[i] < '0' || line[i] > '9') return false;
    uint32_t v = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') {   /* bounded by len */
        if (v > (0xFFFFFFFFu - 9u) / 10u) return false;    /* u32 overflow guard */
        v = v * 10u + (uint32_t)(line[i] - '0');
        i++;
    }
    if (i + 3 > len || memcmp(line + i, "---", 3) != 0) return false;
    *size_out = v;
    return true;
}

/* Parses "---END <8hex>---" out of a completed trailer line into *crc_out. Returns true on a match. */
static bool dl_parse_end(const char *line, size_t len, uint32_t *crc_out)
{
    assert(line != NULL);
    assert(crc_out != NULL);
    static const char PFX[] = LT_FRAME_END_PFX;            /* "---END " */
    size_t pl = sizeof(PFX) - 1;
    if (len < pl + 8u + 3u || memcmp(line, PFX, pl) != 0) return false;
    if (parse_hex8((const uint8_t *)line + pl, len - pl, crc_out) != 8) return false;
    if (memcmp(line + pl + 8u, "---", 3) != 0) return false;
    return true;
}

void lh_dl_init(lh_dl_ctx_t *c, bool is_binary, lh_dl_chunk_cb cb, void *cb_ctx)
{
    assert(c != NULL);
    memset(c, 0, sizeof(*c));
    c->state     = LH_DL_HDR;
    c->is_binary = is_binary;
    c->cb        = cb;
    c->cb_ctx    = cb_ctx;
    c->crc       = 0xFFFFFFFFu;                  /* running CRC seed; xored at ---END */
}

/* Flushes the pending decoded chunk to the sink. Returns non-zero if the sink asked to abort. */
static int dl_flush_chunk(lh_dl_ctx_t *c)
{
    assert(c != NULL);
    if (c->chunk_len == 0) return 0;
    int r = c->cb ? c->cb(c->cb_ctx, c->chunk, c->chunk_len) : 0;
    c->chunk_len = 0;
    return r;
}

/* Feeds `n` decoded bytes into the running CRC + the bounded chunk buffer, flushing to the sink
 * whenever it fills. Returns non-zero if the sink asked to abort. */
static int dl_emit(lh_dl_ctx_t *c, const uint8_t *data, size_t n)
{
    assert(c != NULL);
    assert(data != NULL || n == 0);
    c->crc = crc32_feed(c->crc, data, n);
    c->decoded_len += (uint32_t)n;
    for (size_t i = 0; i < n; i++) {             /* bounded by n */
        c->chunk[c->chunk_len++] = data[i];
        if (c->chunk_len == LH_DL_CHUNK && dl_flush_chunk(c)) return 1;
    }
    return 0;
}

static void dl_fail(lh_dl_ctx_t *c, int result)
{
    c->state  = LH_DL_ERR;
    c->result = result;
}

/* HDR: accumulate a candidate line; on newline classify it. A ---BEGIN line seeds the body;
 * anything else (command echo, prompt, boot/ESP_LOG noise) is discarded and scanning continues. */
static void dl_feed_hdr(lh_dl_ctx_t *c, uint8_t b)
{
    assert(c != NULL);
    assert(c->state == LH_DL_HDR);
    if (b != '\n') {
        if (!c->line_skip && c->line_len + 1u < LH_DL_LINE) c->line[c->line_len++] = (char)b;
        else c->line_skip = true;                /* overlong -> not a header; skip to newline */
        return;
    }
    size_t len = c->line_len;
    if (len > 0 && c->line[len - 1] == '\r') len--;
    c->line[len] = '\0';
    uint32_t size = 0;
    if (len > 0 && dl_parse_begin(c->line, len, c->name, sizeof c->name, &size)) {
        c->body_size = size;
        c->body_got  = 0;
        c->b64_len   = 0;
        c->line_len  = 0;
        c->line_skip = false;
        c->tail_seen = 0;
        c->state = (size == 0) ? LH_DL_TAIL : LH_DL_BODY;   /* empty body -> straight to the trailer */
        return;
    }
    /* A non-framed "ERR 0x<code>: <msg>" line means the lap-timer refused the command (bad id,
     * malformed request, storage error) before emitting a frame -- fail fast with a remote error
     * rather than waiting out the idle timeout. */
    {
        uint16_t code = 0;
        char emsg[LINKHOST_ERRMSG_MAX];
        if (len > 0 && parse_err_line((const uint8_t *)c->line, len, &code, emsg, sizeof emsg)) {
            c->err_code = code;
            memcpy(c->err_msg, emsg, sizeof c->err_msg);
            dl_fail(c, LINKHOST_E_REMOTE);
            return;
        }
    }
    c->line_len  = 0;                            /* echo/prompt/noise line -> discard, keep scanning */
    c->line_skip = false;
}

/* BODY: consume exactly body_size on-wire bytes; base64-decode 4->3 (carry across feeds) for binary
 * bodies or pass text bytes through, emitting decoded blocks; then hand off to the trailer. */
static void dl_feed_body(lh_dl_ctx_t *c, uint8_t b)
{
    assert(c != NULL);
    assert(c->state == LH_DL_BODY);
    if (c->is_binary) {
        c->b64[c->b64_len++] = b;
        if (c->b64_len == 4u) {
            uint8_t tmp[3];
            int nb = b64_quantum(c->b64, tmp);
            c->b64_len = 0;
            if (nb < 0) { dl_fail(c, LINKHOST_E_PROTO); return; }
            if (dl_emit(c, tmp, (size_t)nb)) { dl_fail(c, LINKHOST_E_PROTO); return; }
        }
    } else if (dl_emit(c, &b, 1)) {
        dl_fail(c, LINKHOST_E_PROTO);
        return;
    }
    if (++c->body_got >= c->body_size) {         /* body complete */
        if (c->is_binary && c->b64_len != 0) { dl_fail(c, LINKHOST_E_PROTO); return; }  /* ragged base64 */
        if (dl_flush_chunk(c)) { dl_fail(c, LINKHOST_E_PROTO); return; }
        c->line_len  = 0;
        c->line_skip = false;
        c->tail_seen = 0;
        c->state = LH_DL_TAIL;
    }
}

/* TAIL: scan bounded lines after the body for "---END <crc>---"; compare the running CRC. */
static void dl_feed_tail(lh_dl_ctx_t *c, uint8_t b)
{
    assert(c != NULL);
    assert(c->state == LH_DL_TAIL);
    if (++c->tail_seen > LH_DL_TAIL_MAX) { dl_fail(c, LINKHOST_E_PROTO); return; }
    if (b != '\n') {
        if (!c->line_skip && c->line_len + 1u < LH_DL_LINE) c->line[c->line_len++] = (char)b;
        else c->line_skip = true;
        return;
    }
    size_t len = c->line_len;
    if (len > 0 && c->line[len - 1] == '\r') len--;
    c->line[len] = '\0';
    uint32_t crc_want = 0;
    if (len > 0 && dl_parse_end(c->line, len, &crc_want)) {
        c->crc_ok = ((c->crc ^ 0xFFFFFFFFu) == crc_want);
        c->result = c->crc_ok ? 0 : LINKHOST_E_CRC;
        c->state  = LH_DL_DONE;
        return;
    }
    c->line_len  = 0;                            /* not the END line (e.g. the \r\n gap) -> keep scanning */
    c->line_skip = false;
}

lh_dl_state_t lh_dl_feed(lh_dl_ctx_t *c, const uint8_t *bytes, size_t n)
{
    assert(c != NULL);
    assert(bytes != NULL || n == 0);
    for (size_t i = 0; i < n && c->state < LH_DL_DONE; i++) {   /* bounded by n; stop when terminal */
        uint8_t b = bytes[i];
        switch (c->state) {
        case LH_DL_HDR:  dl_feed_hdr(c, b);  break;
        case LH_DL_BODY: dl_feed_body(c, b); break;
        case LH_DL_TAIL: dl_feed_tail(c, b); break;
        default:         break;
        }
    }
    return c->state;
}

int lh_dl_result(const lh_dl_ctx_t *c)
{
    assert(c != NULL);
    switch (c->state) {
    case LH_DL_DONE: return c->crc_ok ? 0 : LINKHOST_E_CRC;
    case LH_DL_ERR:  return c->result;
    default:         return LINKHOST_E_TIMEOUT;   /* still HDR/BODY/TAIL: incomplete */
    }
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
    *out = s_dx.resp;   /* copy name + err_code/err_msg always; body pointer valid only when status==0 */
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
    /* Tolerate leading echo/prompt/noise glued to the header with no newline before ---BEGIN
     * (e.g. "laptimer> ---BEGIN status ..."), matching linkhost_parse_frame's mem_find
     * tolerance -- otherwise the glued line fails the strict prefix check and the whole
     * response is dropped (a status reply lost -> /api/status 503 while the stream flows). */
    const uint8_t *begin = mem_find(s_dx.line, s_dx.line_len, LT_FRAME_BEGIN_PFX,
                                    sizeof(LT_FRAME_BEGIN_PFX) - 1);
    const size_t   blen  = begin ? s_dx.line_len - (size_t)(begin - s_dx.line) : 0;
    const uint8_t *r     = begin ? parse_begin(begin, blen, name, sizeof(name), &size) : NULL;
    if (!r) {
        /* Not a framed header. A non-framed "ERR 0x<code>: <msg>" is the lap-timer refusing the
         * command -- complete it as a remote-error response so the caller fails fast (400/404/502)
         * instead of waiting out the link timeout, unless a response is already pending unread
         * (never clobber a completed frame; a real command's slot is drained before it is sent). */
        if (!s_dx.resp_ready) {
            uint16_t code = 0;
            char emsg[LINKHOST_ERRMSG_MAX];
            if (parse_err_line(s_dx.line, s_dx.line_len, &code, emsg, sizeof emsg)) {
                s_dx.resp.err_code = code;
                memcpy(s_dx.resp.err_msg, emsg, sizeof s_dx.resp.err_msg);
                s_dx.resp.body     = NULL;
                s_dx.resp.body_len = 0;
                s_dx.resp_status   = LINKHOST_E_REMOTE;
                s_dx.resp_ready    = true;
            }
        }
        s_dx.state = DX_SCAN;
        return;                                       /* echo/prompt/log/boot -> drop */
    }

    /* Re-emit the header line from ---BEGIN (dropping any leading noise) + a '\n'. */
    if (blen + 1u > FRAME_CAP) { s_dx.state = DX_SCAN; return; }
    memcpy(s_dx.frame, begin, blen);
    s_dx.frame[blen] = '\n';
    s_dx.frame_len = blen + 1u;
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
