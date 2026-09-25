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
#include "linkstats.h"        /* linkstats_t + the pure counters/ages this header's wrappers lock around */

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
/* Reads the STATUS cache the RX demux stamps from the lap-timer's autonomous 0xFF stream
 * (Plan 5.6 T3): NEVER touches UART1. 0 with *out decoded when the cached STATUS is fresh
 * (< LINK_STATUS_STALE_MS old), else LINKHOST_E_NOTCONN. */
int linkhost_status(lt_status_t *out);

/* Monotonic clock (esp_timer_get_time()) through one door, so pure callers (e.g. the console) can
 * get a timestamp without depending on esp_timer.h directly. */
int64_t linkhost_now_us(void);

/* ---- linkstats: the ONLY door onto the module's shared global state (Plan 5.6 T3 fix 1).
 * linkstats.c stays IDF-free/unlocked; callers outside linkhost.c must go through these locked
 * wrappers rather than linkstats_on_record/linkstats_snapshot directly, or a dual-core race can
 * tear the struct copy (linkstats_snapshot's 20-byte status_rec memcpy in particular). ---- */
/* Folds one demuxed stream record into the stats, stamped with linkhost_now_us() inside the lock.
 * Call from the stream-consumer task only (the sole writer). */
void linkhost_stats_on_record(const lt_stream_rec_t *r);
/* Copies the current stats out under the lock. */
void linkhost_stats_snapshot(linkstats_t *out);
/* Age of a last-seen timestamp in ms; -1 if last_us == 0. Pure (no shared state read) -- exposed
 * unlocked so callers never need to import linkstats.h's function surface directly. */
int64_t linkhost_stats_age_ms(int64_t last_us, int64_t now_us);

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

/* ---- round-trip timing (Plan 5.6 Task 5, the `lt` console relay) ---- */
typedef struct {
    int     attempts;     /* attempts made this call (1..LINK_CMD_RETRIES+1) */
    int64_t latency_us;   /* accepted reply time minus the FIRST write; total elapsed on failure */
    int     result;       /* the linkhost_cmd_timed return code (mirrors the function's return) */
} linkhost_cmd_stats_t;

/* Same contract as linkhost_cmd, but also fills *st (nullable) with round-trip stats: attempts
 * made, latency measured from the first write of the first attempt to the accepted reply (or the
 * total time elapsed if every attempt failed), and the return code. linkhost_cmd is a thin wrapper
 * (linkhost_cmd_timed(cmd, out, NULL)). */
int linkhost_cmd_timed(const char *cmd, linkhost_frame_t *out, linkhost_cmd_stats_t *st);

/* ---- link trace (Plan 5.6 Task 5): verbose ESP_LOGI("trace: ...") of every linkhost_cmd_timed /
 * linkhost_download_cmd attempt (send/reply/timeout-hexdump for a command; header/bytes/result for
 * a download) -- toggled by the console's `link trace on|off`. Off by default. ---- */
void linkhost_trace_set(bool on);
bool linkhost_trace_get(void);

/* ---- raw reply capture wrapper (Plan 5.6 Task 9's `selftest framing`) ----
 * Thin pass-through to linkhost_proto.h's linkhost_rawcap_copy (arm the capture first via
 * linkhost_rawcap_set(true), also declared there -- independent of linkhost_trace_set's ESP_LOGI
 * trace above). Safe to call any time AFTER linkhost_cmd_timed has returned for the command whose
 * reply is being inspected: the demux's RX task is the SOLE writer of the capture, appending bytes
 * as linkhost_feed processes them off UART1; the byte that completes a reply (the '\n' ending its
 * ---END line) is captured BEFORE that same demux step publishes the reply via resp_ready (see
 * linkhost_proto.c's DX_RESP_TAIL case), and linkhost_cmd_timed only returns once it has observed
 * resp_ready true for that reply. So by the time this is called, the capture for that reply is
 * already fully written and the request-owning task (this caller) is the only reader -- no lock
 * needed, unlike linkhost_line_peek's genuinely concurrent (and merely best-effort) cross-task
 * snapshot. Returns the number of bytes copied (<= cap, <= 256). */
size_t linkhost_trace_last_reply(uint8_t *out, size_t cap);

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
 * `OTA-END 0x0000`, else the `OTA-ERR` code (or a LINKHOST_E_* on timeout/protocol failure). The
 * request-owning mutex is taken with a bounded timeout (I4, final review): a `lt shell` bridge can
 * hold it for up to its own 10-minute cap, so a busy link here returns LINKHOST_E_BUSY rather than
 * blocking the push task for the shell's whole duration. */
int linkhost_flash(const char *ver, const char *hwid, uint32_t size,
                   const uint8_t sha256[32], flash_progress_cb cb, void *ctx);

/* ---- raw byte bridge (Plan 5.6 Task 8's `lt shell`) ----
 * Hands UART1 to the caller so it can pump raw bytes both ways between the USB console and the
 * lap-timer's own console: linkhost_bridge_begin takes s_req_mtx (bounded, like linkhost_cmd_timed
 * -- LINKHOST_E_BUSY on a 400 ms miss rather than blocking the REPL task forever), pauses and parks
 * the demux RX task (the same rx_park_and_drain handshake linkhost_download_cmd/linkhost_flash use,
 * #65) so the caller's own uart_read_bytes(DC_LINK_UART, ...) is the ONLY reader, and returns 0.
 * linkhost_bridge_end un-pauses the RX task and gives the mutex back. Must be paired: begin (0),
 * pump bytes, end -- never call end without a successful begin. */
int linkhost_bridge_begin(void);
void linkhost_bridge_end(void);

#ifdef __cplusplus
}
#endif

#endif /* LINKHOST_H */
