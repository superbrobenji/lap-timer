/* devcontroller/components/linkhost/include/linkhost.h -- linkhost: the UART1 cmd-host that
 * drives the lap-timer's shipped export_serial console as an automated peer
 * (docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md, sub-project B).
 *
 * This is the PINNED interface for Tasks 2/3/6 -- the parallel implementers of linkhost's core
 * (Task 3), the web API (Task 4, consumes linkhost_cmd/linkhost_status/linkhost_stream_pop), and
 * cmd-OTA flash (Task 6) all build against these exact types/signatures. Task 2 (this scaffold)
 * implements ONLY linkhost_status_decode for real (pure, host-testable, no UART); every other
 * entry point is a compiling not-implemented stub until its owning task lands.
 */
#ifndef LINKHOST_H
#define LINKHOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app/lt_proto.h"   /* LT_STATUS_LEN, LT_ST_OFF_*, lt_stream_hdr_t, framing markers */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- errors: negative returns from linkhost_status/linkhost_cmd/linkhost_flash/
 * linkhost_parse_frame ---- */
enum {
    LINKHOST_E_TIMEOUT = -1,   /* no/incomplete response within the link timeout */
    LINKHOST_E_NOTCONN = -2,   /* no lap-timer peer detected (no heartbeat / not yet wired) */
    LINKHOST_E_CRC     = -3,   /* decoded body CRC did not match the ---END crc32 */
    LINKHOST_E_PROTO   = -4,   /* malformed framing / oversize body / unexpected token */
};

/* ---- §18.2 STATUS record, decoded (Task 2) ---- */
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

/* ---- §14/§18.1 demuxed stream record (Task 3). LT_REC_MAX bounds the payload to the largest
 * fused-sample/event record (§14). ---- */
#define LT_REC_MAX 64

typedef struct {
    uint16_t seq;
    uint8_t  flags;
    uint8_t  type;
    uint8_t  len;
    uint8_t  data[LT_REC_MAX];
} lt_stream_rec_t;

/* ---- §18.4 framed command response, decoded (Task 3). `body` points into linkhost's static
 * assembly buffer -- valid only until the next linkhost_cmd/linkhost_parse_frame call. ---- */
#define LINKHOST_ASM_MAX 1024   /* bound on one framed response body (config get is ~939 B) */

typedef struct {
    char           name[16];
    uint32_t       size;
    const uint8_t *body;
    size_t         body_len;
} linkhost_frame_t;

/* ---- cmd-OTA flash progress callback (Task 6): sent/total bytes of the staged image pushed
 * so far. ---- */
typedef void (*flash_progress_cb)(uint32_t sent, uint32_t total, void *ctx);

/* ---- lifecycle (Task 2): open UART1 (pins/baud from build_config.h), install the driver. ---- */
esp_err_t linkhost_init(void);

/* ---- status (Task 2) ---- */
/* Runs `status`, reads the framed base64 record, decodes it. 0 on success, LINKHOST_E_* < 0. */
int linkhost_status(lt_status_t *out);
/* Pure decoder (host-testable): decodes a raw LT_STATUS_LEN-byte record into *out. */
bool linkhost_status_decode(const uint8_t rec[LT_STATUS_LEN], lt_status_t *out);

/* ---- request/response + stream (Task 3) ---- */
/* Sends "<cmd>\r", reads the framed response (base64-decoding + CRC-verifying binary bodies),
 * fills *out. Returns 0, or LINKHOST_E_TIMEOUT/_NOTCONN/_CRC/_PROTO. */
int linkhost_cmd(const char *cmd, linkhost_frame_t *out);
/* Pops one demuxed stream record. Returns 0 if one was returned, <0 if the ring is empty. */
int linkhost_stream_pop(lt_stream_rec_t *out);
/* True when a `status` heartbeat has been seen within the last few seconds. */
bool linkhost_peer_present(void);

/* ---- cmd-OTA flash (Task 6) ----
 * Runs `ota recv <size> <sha> <ver> <hwid>` against the image already staged in the `ota_stage`
 * partition, streaming it in bounded chunks and reporting progress via cb. Returns 0 on
 * `OTA-END 0x0000`, else the `OTA-ERR` code (or a LINKHOST_E_* on timeout/protocol failure). */
int linkhost_flash(const char *ver, const char *hwid, uint32_t size,
                    const uint8_t sha256[32], flash_progress_cb cb, void *ctx);

/* ---- exposed for host tests (Task 3) ---- */
/* Parses one already-assembled ---BEGIN/---END response (as consumed off the wire) into *out. */
int linkhost_parse_frame(const uint8_t *bytes, size_t n, linkhost_frame_t *out);
/* The demux state machine: consumes raw UART bytes, routes framed-response bytes to the response
 * assembler and 0xFF frames to the stream ring, skips echo/prompt/log/ERR noise. Returns the
 * number of bytes consumed. */
size_t linkhost_feed(const uint8_t *bytes, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* LINKHOST_H */
