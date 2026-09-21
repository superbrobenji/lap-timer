/* devcontroller/components/linkhost/include/linkhost.h -- linkhost: the UART1 cmd-host that
 * drives the lap-timer's shipped export_serial console as an automated peer
 * (docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md, sub-project B).
 *
 * This is the PINNED public interface. The pure, IDF-free logic (the framing/CRC/base64 parser,
 * the length-aware demux + stream ring, the STATUS decoder and the cmd-OTA flash state machine)
 * now lives in the host-testable linkhost_proto.h / host/linkhost_proto.c -- included below so the
 * value types (lt_status_t / lt_stream_rec_t / linkhost_frame_t / LINKHOST_E_*) and the pure
 * entry points (linkhost_status_decode / linkhost_parse_frame / linkhost_feed / linkhost_stream_pop)
 * have a single source of truth. THIS header adds only the IDF glue: UART1 lifecycle + the
 * request/response, status, heartbeat and flash transports (linkhost.c).
 */
#ifndef LINKHOST_H
#define LINKHOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "linkhost_proto.h"   /* pure logic: value types, error codes, parser/demux/status/flash decls */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- cmd-OTA flash progress callback (Task 6): sent/total bytes of the staged image pushed
 * so far. ---- */
typedef void (*flash_progress_cb)(uint32_t sent, uint32_t total, void *ctx);

/* ---- a remote error surfaced by a command/download that returned LINKHOST_E_REMOTE: the
 * lap-timer's `ERR 0x<code>: <msg>` code + (truncated) message, for HTTP-status mapping. ---- */
typedef struct {
    uint16_t code;
    char     msg[LINKHOST_ERRMSG_MAX];
} linkhost_remote_err_t;

/* ---- lifecycle: open UART1 (pins/baud from build_config.h), install the driver, start RX. ---- */
esp_err_t linkhost_init(void);

/* ---- status ---- */
/* Runs `status`, reads the framed base64 record, decodes it. 0 on success, LINKHOST_E_* < 0. */
int linkhost_status(lt_status_t *out);

/* ---- request/response + stream ---- */
/* Sends "<cmd>\r", reads the framed response (base64-decoding + CRC-verifying binary bodies),
 * fills *out. The response body is copied into a caller-stable buffer (out->body is valid until
 * the next linkhost_cmd), so a late duplicate reply can never tear an in-flight httpd send. The
 * reply's frame name is matched against the command (status/config), and a non-framed
 * "ERR 0x<code>: <msg>" reply completes immediately as LINKHOST_E_REMOTE (out->err_code/err_msg).
 * The request-owning mutex is taken with a bounded timeout: a busy link returns LINKHOST_E_BUSY
 * rather than blocking the httpd task. Returns 0, or
 * LINKHOST_E_TIMEOUT/_NOTCONN/_CRC/_PROTO/_REMOTE/_BUSY. */
int linkhost_cmd(const char *cmd, linkhost_frame_t *out);
/* True when link activity (a `status` heartbeat / stream frame) has been seen within a few seconds. */
bool linkhost_peer_present(void);

/* ---- streaming session download ----
 * Runs `open <id> <fmt>` and STREAMS the framed response body to `chunk_cb` without ever buffering
 * the whole file (real .log/.vbo/... are KB..MB and overflow LINKHOST_ASM_MAX). `chunk_cb` receives
 * each decoded block (base64-decoded on the fly for the binary log/sum formats; raw text for
 * json/vbo/nmea) and returns non-zero to abort (e.g. the HTTP socket closed). Returns 0 on a
 * complete, CRC-verified transfer, or LINKHOST_E_CRC/_PROTO/_TIMEOUT/_NOTCONN. NOTE: on any
 * non-zero return AFTER bytes have already been streamed, the sent prefix cannot be un-sent -- the
 * caller must abort the transport so the peer sees a truncated download. Use linkhost_cmd for the
 * small fixed JSON ops (config/status). `rerr` (nullable) is filled when the return is
 * LINKHOST_E_REMOTE. */
int linkhost_download(const char *id, const char *fmt, lh_dl_chunk_cb chunk_cb, void *ctx,
                      linkhost_remote_err_t *rerr);

/* Generic streaming relay: sends "<cmd>\r" and streams the framed response body to `chunk_cb`
 * exactly like linkhost_download, but for an arbitrary command. Used for `list`, whose response
 * (measured at 3.6 KB on hardware) overflows linkhost_cmd's LINKHOST_ASM_MAX buffer. `is_binary`
 * selects on-the-fly base64 decoding (false for the raw-text `list`/session JSON). `rerr`
 * (nullable) is filled on LINKHOST_E_REMOTE. Returns 0 or LINKHOST_E_*. */
int linkhost_download_cmd(const char *cmd, bool is_binary, lh_dl_chunk_cb chunk_cb, void *ctx,
                          linkhost_remote_err_t *rerr);

/* ---- cmd-OTA flash ----
 * Runs `ota recv <size> <sha> <ver> <hwid>` against the image already staged in the `ota_stage`
 * partition, streaming it in bounded chunks and reporting progress via cb. Returns 0 on
 * `OTA-END 0x0000`, else the `OTA-ERR` code (or a LINKHOST_E_* on timeout/protocol failure). */
int linkhost_flash(const char *ver, const char *hwid, uint32_t size,
                   const uint8_t sha256[32], flash_progress_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LINKHOST_H */
