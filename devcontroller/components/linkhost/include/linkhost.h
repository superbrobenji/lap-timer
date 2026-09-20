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

/* ---- lifecycle: open UART1 (pins/baud from build_config.h), install the driver, start RX. ---- */
esp_err_t linkhost_init(void);

/* ---- status ---- */
/* Runs `status`, reads the framed base64 record, decodes it. 0 on success, LINKHOST_E_* < 0. */
int linkhost_status(lt_status_t *out);

/* ---- request/response + stream ---- */
/* Sends "<cmd>\r", reads the framed response (base64-decoding + CRC-verifying binary bodies),
 * fills *out. Returns 0, or LINKHOST_E_TIMEOUT/_NOTCONN/_CRC/_PROTO. */
int linkhost_cmd(const char *cmd, linkhost_frame_t *out);
/* True when link activity (a `status` heartbeat / stream frame) has been seen within a few seconds. */
bool linkhost_peer_present(void);

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
