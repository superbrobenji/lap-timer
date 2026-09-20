/* app/link.h -- peer link: fused-log/event stream fan-out + peer-detect (spec §18, Plan 5 sub-project A).
 *
 * The pipeline pushes one record per fused sample (at cfg.log_fused_hz) and one per event through
 * stream_push(); the link module buffers them in a bounded, non-blocking SPSC ring (drop-newest on
 * full, like fix_ring -- the pipeline is NEVER stalled) and a low-priority drain task fans each out
 * to whichever peer transport is attached, framed as an unsolicited §18.1 LINK_STREAM_TAG (0xFF)
 * chunk: `tag u8 | seq u16 LE | flags u8 | payload`. The payload is a §14-typed stream record:
 * `rec[0]` is the SES_T_* type (SES_T_FUSED / SES_T_EVENT) and the rest is the raw struct.
 *
 * Fan-out sinks are resolved at LINK time (weak no-op default here; the serial transport in
 * export_serial defines the strong link_sink_serial_emit), NOT via a stored function-pointer table
 * -- this mirrors the 4.5.5 rule-9 rework (core_assert_report) and keeps the fan-out free of any
 * function pointer. A SECOND sink (BLE RaceChrono, activated in sub-project C) attaches by defining
 * its own strong link_sink_ble_emit + presence; link_deliver() already fans out to it. Until then
 * the BLE sink stays the weak no-op and is never present, so this build streams over serial only.
 *
 * Peer-detect (§6 connector): presence is asserted either by the GPIO detect line (LINK_DETECT_GPIO,
 * assigned with the connector hardware in Plan 6; disabled by default here) OR by a `cmd` heartbeat
 * -- any request through the serial transport calls link_note_cmd_activity(), and a STATUS poll is
 * the canonical handshake. With no peer the ring is never fed and the drain task idles: no hang, no
 * error spam. Attach mid-run starts streaming; detach stops it cleanly (all state is static -- no
 * allocation, nothing to leak).
 */
#ifndef APP_LINK_H
#define APP_LINK_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Max stream-record payload handed to stream_push (type byte + the largest §14 struct we stream;
 * event_t/fused_sample_t are well under this). A record is always <= CMD_CHUNK_MAX (496). */
enum { LINK_REC_MAX = 64 };

/* Create the drain task + init the ring + configure the detect line. Call once at boot (step 11,
 * before the pipeline produces). Idempotent. */
void link_start(void);

/* The pipeline seam: enqueue one stream record (rec[0] = SES_T_* type, rest = struct). Non-blocking
 * and drop-on-full; a no-op before link_start() or when no peer is attached. Safe from the pipeline
 * task (the single producer). */
void stream_push(const uint8_t *rec, size_t len);

/* Heartbeat: the serial transport calls this on every `cmd` request from a peer (a STATUS poll is
 * the §18 handshake) so the link marks the peer present for LINK_PEER_TIMEOUT_MS. Safe from any task. */
void link_note_cmd_activity(void);

/* True while a peer is attached (detect line asserted, or a recent cmd heartbeat). Safe from any task. */
bool link_peer_present(void);

/* Stream records dropped because the ring was full (diagnostic snapshot). */
uint32_t link_stream_dropped(void);

/* ---- sink contract (transports implement; link_deliver() fans out to them) ----
 * Resolved at LINK time: link.c ships a weak no-op default for each, and a transport that is
 * compiled in provides the STRONG definition that wins (mirrors core_assert_report). No function
 * pointer is stored -- these are ordinary direct calls, so the fan-out is Power of 10 rule-9 clean.
 *   - link_sink_serial_emit: write one fully-framed 0xFF stream frame to the UART peer. Its
 *     presence is the link's own detect/heartbeat state (link_peer_present), so there is no
 *     separate serial-present hook. export_serial defines the strong version (transport half).
 *   - link_sink_ble_emit / link_sink_ble_present: the BLE RaceChrono seam (sub-project C). Until
 *     conn_ble is activated both stay the weak no-op / false, so nothing streams over BLE. */
int  link_sink_serial_emit(const uint8_t *frame, size_t len);
int  link_sink_ble_emit(const uint8_t *frame, size_t len);
bool link_sink_ble_present(void);

#endif /* APP_LINK_H */
