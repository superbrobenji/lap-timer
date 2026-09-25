/* devcontroller/components/devconsole/cmd_flash.c -- the `flash` command: receives a signed
 * lap-timer app image over this console's own USB UART and pushes it to the lap-timer over UART1
 * (Plan 5.6 Task 10). Mirrors the lap-timer's own `ota recv` handshake (export_serial.c's
 * cmd_ota/ota_recv_stream/ota_fill_chunk) so tools/devkit.py's host-side logic (Task 11) is the
 * same shape as tools/ota_push.py's.
 *
 *   flash stage <size> <sha256hex>   raw-bytes handshake into ota_stage (see below) -- NO --json
 *                                    form: its own output IS the wire protocol.
 *   flash push [--json]              pushes the staged image via flashctl_start_push, polling
 *                                    progress until done.
 *   flash status [--json]            flashctl_get, formatted.
 *   flash abort                      cancels an in-flight STAGING attempt (see note below).
 *
 * `flash stage` protocol (spec 2026-09-23-devkit-primary-interface-design.md §5.4):
 *   host:    flash stage <size> <sha256hex>\r
 *   dev-kit: STAGE-READY                      (ota_stage erased for size; console in raw mode)
 *   host:    <size> raw bytes
 *   dev-kit: STAGE-END 0x0000                 (SHA-256 matches; image parsed: ver/hwid)
 *         |  STAGE-ERR <code>                 (usage | badsize | badsha | busy | timeout | write
 *                                             | image | abort)
 * `image` is a Plan 5.6 Task 10 addition beyond the spec's original four codes (OTASTAGE_E_IMAGE
 * has no other code to map to -- too few bytes staged, or a bad esp_app_desc/hwid header); the
 * spec doc itself is updated for this in Task 13. `usage`/`busy`/`abort` are likewise new: `usage`
 * covers a bad `flash stage` invocation (arg grammar, or --json, which this subcommand rejects
 * outright since its own output IS the protocol); `busy` covers flashctl's single-flight guard
 * already being held by a concurrent POST /api/flash; `abort` covers `flash abort` (see below).
 *
 * Every failure once STAGE-READY has been printed leaves NOTHING behind: the SHA context is freed
 * (otastage_abort; otastage_finish itself already frees it on every return path -- OK or error --
 * before this file ever gets a chance to call otastage_abort again, so a stray extra call there
 * would just be a safe no-op guarded by otastage's own `active` flag, never a double-free -- this
 * file still never makes that redundant call, for clarity), flashctl's single-flight guard is
 * released (flashctl_end_staging(false)), and any image bytes still in flight on the wire (e.g. a
 * host that kept sending after we gave up) are drained off UART0 for a bounded window so they
 * never get interpreted as REPL command characters once we return to the prompt.
 *
 * Nothing but the STAGE-ERR/STAGE-READY/STAGE-END/pushing/flash-result protocol lines may reach
 * this console's USB while a stage or push is live: `cmd_stream_tap_off()` plus an
 * `esp_log_level_set("*", ESP_LOG_NONE)`
 * (saved/restored around the raw phase and the push poll, one exit funnel each) silence every
 * other source of console output, mirroring `cmd_shell.c`'s `lt shell` bridge and the lap-timer's
 * own `ota recv` (export_serial.c's cmd_ota saved/restores esp_log_level around its OTA-READY..
 * OTA-END handshake the same way). `stream tap` is left off afterward, same as `lt shell` -- it is
 * an explicit opt-in the operator turns back on themselves; only the log level is restored.
 *
 * `flash abort` note: this console is a single REPL task processing one command line at a time --
 * `flash stage`'s raw-read loop runs to completion (or failure) INSIDE that one command's call,
 * blocking the REPL from reading a second command line meanwhile. So `flash abort` typed at THIS
 * console can never interrupt a `flash stage` currently receiving bytes; the s_abort flag it would
 * set in that case is checked by the raw loop for a FUTURE second front-end (a second UART, or an
 * out-of-band abort mechanism) that Plan 5.6 does not add -- and is always cleared at the start
 * and end of every `flash stage` attempt so a request against one attempt can never leak into the
 * next (fix round 1: it used to latch forever once set, since nothing ever cleared it). What IS
 * reachable today: a `flash stage` that already completed (STAGE-END printed) and is waiting for
 * `flash push` -- flashctl's guard stays held (state FLASHCTL_STAGING) in that window, and `flash
 * abort` there presents this console's saved ownership token (s_stage_token, from the
 * flashctl_end_staging(true) that finished the stage) to flashctl_release_staged (M10, round 2). A
 * match DISCARDS the staged image outright (clears this file's ver/hwid/size/sha/token
 * bookkeeping, releases flashctl's guard) and reports `OK abort (staged image discarded)` /
 * `{"abort":true,"discarded":true}`, real semantics rather than a no-op. A mismatch -- this
 * console's claim was reclaimed out from under it by flashctl_try_begin_staging's TTL path (M7)
 * before this abort ran, and flashctl is now STAGING for a DIFFERENT owner -- refuses instead:
 * `ERR not mine` / `{"err":"not mine"}`, non-zero return, same as the M9 case below (this file's
 * own staged bookkeeping is still forgotten either way, since it no longer describes a live claim).
 * A THIRD case (M9, final review): flashctl reports STAGING but this console holds neither
 * s_staged nor s_stage_inflight -- the claim belongs to a concurrent POST /api/flash (staged, or
 * mid stage-then-push). `flash abort` there refuses: `ERR not mine (staging owned by /api/flash)`
 * / `{"err":"not mine"}`, non-zero return, and touches neither s_abort nor flashctl's guard.
 *
 * Pragmatic-P10: the 4096 B chunk buffer (s_stage_buf) and the staged-image bookkeeping
 * (s_stage_ver/hwid/size/sha, s_staged) are static, not heap -- one `flash stage` attempt at a
 * time, same single-flight rule flashctl itself enforces. Every loop below is bounded: the raw
 * read loop by `recvd` monotonically approaching `size` (or `stall_ms` monotonically approaching
 * FLASH_STALL_MAX_MS), the push poll by a fixed iteration cap, the RX drain by a fixed iteration
 * count -- same shape as export_serial.c's ota_fill_chunk/ota_recv_stream.
 */
#include "cmd_flash.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cmd_stream.h"     /* cmd_stream_tap_off -- silenced during a stage/push, see file header */
#include "console.h"
#include "flash_fmt.h"
#include "flashctl.h"
#include "image_desc.h"
#include "jsonw.h"
#include "otastage.h"
#include "stage_args.h"

#define FLASH_CHUNK          4096u  /* raw read granularity, one otastage_write per received chunk */
#define FLASH_READ_MS        50     /* per-read UART timeout window (Plan 5.6 T10 ruling 2) */
#define FLASH_STALL_MAX_MS   3000   /* cumulative no-data time before STAGE-ERR timeout */
#define FLASH_DRAIN_ITERS    10     /* FLASH_DRAIN_ITERS * FLASH_DRAIN_MS == 200 ms drain budget */
#define FLASH_DRAIN_MS       20
#define FLASH_PUSH_POLL_MS   500
#define FLASH_PUSH_MAX_POLLS 360    /* 360 * 500 ms == 180 s push-poll bound */

/* The one raw-transfer scratch buffer, and the bookkeeping for a stage that finished cleanly and
 * is waiting for `flash push` (flashctl's own ver/hwid/size only get filled at push time, by
 * flashctl_start_push -- so the console keeps its own copy from the moment `flash stage`
 * succeeds). s_abort: see the file header note on `flash abort`. */
static uint8_t        s_stage_buf[FLASH_CHUNK];
static bool            s_staged;
static char            s_stage_ver[IMG_VER_LEN];
static char            s_stage_hwid[IMG_HWID_LEN + 1];
static uint32_t        s_stage_size;
static uint8_t         s_stage_sha[32];
static volatile bool   s_abort;

/* M10 (final review, round 2): the ownership token flashctl_end_staging(true) returned for the
 * claim s_staged is bookkeeping for -- flash_stage_finish stores it, cmd_flash_push and
 * cmd_flash_abort present it back to flashctl_start_push / flashctl_release_staged so flashctl can
 * tell "this console still owns the claim it staged" apart from "this claim was reclaimed by a
 * fresh caller (TTL expiry) and merely still happens to be STAGING" -- flashctl's state alone
 * cannot distinguish the two (see the defect note at the top of this file). */
static uint64_t        s_stage_token;

/* M9 (final review): true from a successful flashctl_try_begin_staging call inside
 * cmd_flash_stage until that attempt's raw phase ends (finish or fail) -- i.e. while THIS console
 * is the one actively receiving bytes for the flashctl STAGING claim it holds. Paired with
 * s_staged (the OTHER window this console can own the claim: staged and waiting for `flash
 * push`), it lets cmd_flash_abort tell "I hold this claim" apart from a concurrent POST
 * /api/flash's own STAGING window -- flashctl's state alone cannot distinguish the two owners. */
static volatile bool   s_stage_inflight;

static void flash_err(bool json, const char *reason)
{
    if (json) printf("{\"err\":\"%s\"}\n", reason);
    else      printf("ERR %s\n", reason);
}

static const char *flash_state_name(flashctl_state_t s)
{
    switch (s) {
    case FLASHCTL_IDLE:     return "idle";
    case FLASHCTL_STAGING:  return "staging";
    case FLASHCTL_PUSHING:  return "pushing";
    case FLASHCTL_DONE_OK:  return "done_ok";
    case FLASHCTL_DONE_ERR: return "done_err";
    }
    return "unknown";
}

/* Clears this file's own staged-image bookkeeping (s_staged plus the ver/hwid/size/sha/token
 * statics `flash push`/`flash abort` would otherwise reuse). Deliberately does NOT touch flashctl's
 * own guard -- callers need different guard handling: `flash abort`'s discard path calls
 * flashctl_release_staged(s_stage_token) itself (M10, round 2) right before or after calling this
 * (either order is fine -- the token is read/cleared by each independently); a
 * `flashctl_start_push` FLASHCTL_E_NOT_OWNER or xTaskCreate-failure return (fix round 2 / M10) must
 * NOT call flashctl_end_staging or flashctl_release_staged here -- flashctl.c has already either
 * left the OTHER owner's claim untouched (E_NOT_OWNER) or reset state to FLASHCTL_IDLE/busy=false
 * itself (xTaskCreate failure); calling flashctl_end_staging again on either path would violate ITS
 * OWN precondition assert (busy && state == FLASHCTL_STAGING) and crash the firmware. */
static void flash_forget_staged(void)
{
    s_staged = false;
    memset(s_stage_ver, 0, sizeof s_stage_ver);
    memset(s_stage_hwid, 0, sizeof s_stage_hwid);
    s_stage_size = 0;
    memset(s_stage_sha, 0, sizeof s_stage_sha);
    s_stage_token = 0;   /* M10: no claim left to present a token for */
}

/* Reads and discards whatever shows up on the console UART for a fixed ~200 ms budget -- so a
 * host that was still sending image bytes when we gave up never has its leftover bytes land on
 * the REPL as command characters once this call returns. Fixed iteration count (bounded, no
 * wall-clock math). */
static void flash_drain_rx(void)
{
    uint8_t junk[64];
    for (int i = 0; i < FLASH_DRAIN_ITERS; i++) {
        (void)uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, junk, sizeof junk,
                              pdMS_TO_TICKS(FLASH_DRAIN_MS));
    }
}

static void flash_stage_err(const char *reason)
{
    printf("STAGE-ERR %s\r\n", reason);
    fflush(stdout);
}

/* Cleanup for a failure INSIDE the raw loop (otastage's SHA context is still live: otastage_begin
 * succeeded, otastage_finish has not run yet). */
static int flash_stage_fail_raw(const char *reason)
{
    otastage_abort();
    flashctl_end_staging(false);
    flash_drain_rx();
    flash_stage_err(reason);
    return 1;
}

/* Cleanup for a failure AFTER otastage_finish has already run (it frees the SHA context itself,
 * on every return path -- OK or error -- so no otastage_abort() call belongs here; see the file
 * header note). Used by finish's own error returns and by the post-finish SHA mismatch check. */
static int flash_stage_fail_post(const char *reason)
{
    flashctl_end_staging(false);
    flash_drain_rx();
    flash_stage_err(reason);
    return 1;
}

/* `size` bytes have been received and written; flush/finish/parse the staged image and compare
 * its SHA-256 against the operator's argument. */
static int flash_stage_finish(uint32_t size, const uint8_t sha_arg[32])
{
    char     ver[IMG_VER_LEN];
    char     hwid[IMG_HWID_LEN + 1];
    uint8_t  sha[32];
    uint32_t out_size = 0;

    int frc = otastage_finish(sha, ver, hwid, &out_size);
    if (frc == OTASTAGE_E_SIZE)  return flash_stage_fail_post("badsize");
    if (frc == OTASTAGE_E_IMAGE) return flash_stage_fail_post("image");
    if (frc == OTASTAGE_E_WRITE) return flash_stage_fail_post("write");
    assert(frc == OTASTAGE_OK);
    (void)size;   /* only used by the invariant check below; keeps -Wunused-parameter quiet if
                    * assertions were ever compiled out */
    assert(out_size == size);   /* EXACT mode: OTASTAGE_OK guarantees the flushed count matches */

    if (memcmp(sha, sha_arg, sizeof sha) != 0)
        return flash_stage_fail_post("badsha");

    memcpy(s_stage_ver, ver, sizeof s_stage_ver);
    memcpy(s_stage_hwid, hwid, sizeof s_stage_hwid);
    s_stage_size = out_size;
    memcpy(s_stage_sha, sha, sizeof s_stage_sha);
    s_staged = true;

    /* stays STAGING/busy: ready for `flash push`. M10 (round 2): the returned stamp IS this
     * claim's ownership token -- stash it so a later `flash push`/`flash abort` can prove to
     * flashctl it's still the same claim, not a stale caller whose claim was reclaimed meanwhile. */
    s_stage_token = flashctl_end_staging(true);
    printf("STAGE-END 0x0000\r\n");
    fflush(stdout);
    return 0;
}

/* The raw-byte phase, entered right after STAGE-READY. Bounded: `recvd` strictly approaches
 * `size` on every successful read and `stall_ms` strictly approaches FLASH_STALL_MAX_MS on every
 * empty one, so the loop below is guaranteed to exit -- same shape as export_serial.c's
 * ota_recv_stream `while (recvd < size)`. */
static int flash_stage_recv(uint32_t size, const uint8_t sha_arg[32])
{
    uint32_t recvd    = 0;
    uint32_t stall_ms = 0;

    while (recvd < size) {
        if (s_abort) return flash_stage_fail_raw("abort");

        uint32_t remaining = size - recvd;
        uint32_t want = (remaining < FLASH_CHUNK) ? remaining : FLASH_CHUNK;
        int r = uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, s_stage_buf, want,
                                pdMS_TO_TICKS(FLASH_READ_MS));
        if (r < 0) {
            /* A genuine UART driver error, not a stall -- the wire protocol has no dedicated code
             * for it (only "timeout" and "write" are defined for this phase), so it is folded
             * into "timeout": both mean "the raw transfer did not complete". */
            return flash_stage_fail_raw("timeout");
        }
        if (r == 0) {
            stall_ms += FLASH_READ_MS;
            if (stall_ms >= FLASH_STALL_MAX_MS) return flash_stage_fail_raw("timeout");
            continue;
        }
        stall_ms = 0;

        if (otastage_write(s_stage_buf, (size_t)r) != OTASTAGE_OK)
            return flash_stage_fail_raw("write");

        recvd += (uint32_t)r;
    }

    return flash_stage_finish(size, sha_arg);
}

/* `flash stage <size> <sha256hex>` -- argv[0]/[1] are "flash"/"stage"; stage_args_parse only
 * wants the positional args after them. */
static int cmd_flash_stage(int argc, char **argv)
{
    s_abort = false;   /* fix round 1: clear any abort request left over from a prior attempt --
                        * see the file header note. Must happen before flashctl_try_begin_staging
                        * so a fresh claim never starts pre-armed. */

    uint32_t size = 0;
    uint8_t  sha[32];

    int prc = stage_args_parse(argc - 2, argv + 2, &size, sha);
    if (prc == -1) { flash_stage_err("usage");   return 1; }
    if (prc == -2) { flash_stage_err("badsize"); return 1; }
    if (prc == -3) { flash_stage_err("badsha");  return 1; }
    assert(prc == 0);

    if (!flashctl_try_begin_staging()) {
        flash_stage_err("busy");
        return 1;
    }
    flashctl_clear_result();
    s_stage_inflight = true;   /* M9: this console now owns the claim -- see the flag's own comment */

    if (otastage_begin(size) != OTASTAGE_OK) {
        /* Ruling 2 maps EVERY otastage_begin failure to "badsize" (it can also fail with
         * OTASTAGE_E_WRITE if the ota_stage partition itself is missing/unreadable, a case that
         * has no more specific wire code here) -- no SHA context was ever established either way,
         * so no otastage_abort() call belongs on this path. */
        flashctl_end_staging(false);
        flash_stage_err("badsize");
        s_stage_inflight = false;
        return 1;
    }

    /* Silence every other USB writer for the whole raw handshake (fix round 1, item 2): nothing
     * but STAGE-READY/STAGE-END/STAGE-ERR may appear on the wire from here until this function
     * returns, on ANY exit path (success or any STAGE-ERR) -- this is the one exit funnel that
     * restores the log level, matching cmd_shell.c's `lt shell` / export_serial.c's cmd_ota. */
    cmd_stream_tap_off();
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);

    /* C1 (final review): this console runs with CR line endings, but a host tool (or a human
     * pasting the command) commonly sends "...\r\n" -- the '\n' has no meaning to this console's
     * line editor but is still a real byte that lands in UART0's RX ring right behind the '\r'
     * that ended the `flash stage` command line. Left alone, flash_stage_recv's raw uart_read_bytes
     * would eat that stray '\n' as image byte 0, corrupting every transfer from such a host. The
     * host cannot have sent any image bytes yet -- by protocol it is still waiting to see
     * STAGE-READY before it starts the raw phase -- so flushing the RX ring here can only ever
     * discard that stray newline (or nothing at all), never a real image byte. */
    uart_flush_input((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM);

    printf("STAGE-READY\r\n");
    fflush(stdout);
    uart_wait_tx_done((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(100));

    int rc = flash_stage_recv(size, sha);

    esp_log_level_set("*", saved);
    s_abort = false;   /* this attempt is over either way -- never let a request outlive it */
    s_stage_inflight = false;
    return rc;
}

/* Formats and prints the final `flash result <str>` (or its --json object) once flashctl reaches
 * FLASHCTL_DONE_OK/FLASHCTL_DONE_ERR. Returns 0 iff the push itself succeeded (result == 0),
 * matching every other cmd_*.c's "0 iff success" exit-code convention. */
static int flash_push_report(bool json, const flashctl_status_t *st)
{
    char rs[24];
    flash_result_str(st->result, rs, sizeof rs);

    if (json) {
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_int(&w, "result", st->result);
        jsonw_str(&w, "result_str", rs);
        jsonw_str(&w, "ver", st->ver);
        jsonw_str(&w, "hwid", st->hwid);
        jsonw_uint(&w, "size", (unsigned long long)st->size);
        if (!jsonw_end(&w)) {
            flash_err(true, "result body too large");
            return 1;
        }
        printf("%s\n", buf);
    } else {
        printf("flash result %s\n", rs);
    }
    return (st->result == 0) ? 0 : 1;
}

/* `flash push [--json]` -- pushes the image `flash stage` left staged. Progress lines are
 * suppressed in --json mode (ruling 3); only ONE line/object is ever printed after completion. */
static int cmd_flash_push(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        flash_err(json, "usage: flash push [--json]");
        return 1;
    }
    if (!s_staged) {
        flash_err(json, "nothing staged");
        return 1;
    }

    /* M10 (round 2): ownership is no longer inferred from flashctl's state alone (that was the
     * defect this round fixes -- a stale caller whose claim was reclaimed by
     * flashctl_try_begin_staging's TTL path could pass a state-only check and push/abort a
     * DIFFERENT owner's in-flight claim). flashctl_start_push now makes the authoritative call
     * itself, atomically, by comparing s_stage_token against its live staged_us. */
    int prc = flashctl_start_push(s_stage_ver, s_stage_hwid, s_stage_size, s_stage_sha,
                                  s_stage_token);
    if (prc == FLASHCTL_E_NOT_OWNER) {
        /* This console's claim is stale: past FLASHCTL_STAGE_TTL_US, flashctl_try_begin_staging()
         * reclaimed an unpushed STAGING claim for a fresh caller (M7, final review) -- POST
         * /api/flash, or another `flash stage` -- before this `flash push` finally ran. The image
         * this file remembers is gone either way; forget it and say so plainly, not "busy": a
         * retry of `flash push` can never help here, only a fresh `flash stage` can. */
        flash_forget_staged();
        if (json) printf("{\"err\":\"nothing staged\"}\n");
        else      printf("ERR nothing staged (claim expired or taken by /api/flash)\n");
        return 1;
    }
    if (prc != 0) {
        /* The only remaining failure is an xTaskCreate failure, and flashctl.c's own failure
         * branch already reset state to FLASHCTL_IDLE/busy=false unconditionally before returning
         * -- flashctl itself has nothing staged any more. Forget this console's own copy too (fix
         * round 2), or `flash abort`/`flash status` would keep reporting an image staged that
         * flashctl has already discarded, and only a fresh `flash stage` (not the `flash push` the
         * operator would naturally retry) could ever recover. */
        flash_forget_staged();
        flash_err(json, "push (image discarded, re-stage)");
        return 1;
    }
    s_staged = false;   /* consumed: flashctl now owns the push; a retry needs a fresh `flash stage` */
    s_stage_token = 0;  /* M10: this claim is no longer this console's to abort/re-push */

    flashctl_status_t st;

    /* Same silencing as `flash stage`'s raw phase (fix round 1, item 2): only `pushing NN%` /
     * `flash result ...` (or the one --json object) may reach this console's USB while the push
     * task (flashcore/flashctl.c's flash_task) is running -- that task ESP_LOGI's a one-line
     * summary on completion via the "flashctl" tag, which has no per-tag override anywhere in this
     * tree, so it inherits the "*" level set here and is suppressed for the duration too. One exit
     * funnel restores it regardless of outcome (done/timeout). */
    cmd_stream_tap_off();
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);

    int rc = 1;
    uint8_t last_pct = 0xFFu;   /* not a valid pct (0..100): forces the first print */
    for (int i = 0; i < FLASH_PUSH_MAX_POLLS; i++) {
        vTaskDelay(pdMS_TO_TICKS(FLASH_PUSH_POLL_MS));
        flashctl_get(&st);
        if (!json && st.pct != last_pct) {
            printf("pushing %u%%\n", (unsigned)st.pct);
            last_pct = st.pct;
        }
        if (st.state == FLASHCTL_DONE_OK || st.state == FLASHCTL_DONE_ERR) {
            rc = flash_push_report(json, &st);
            goto push_done;
        }
    }
    flash_err(json, "push timeout");

push_done:
    esp_log_level_set("*", saved);
    return rc;
}

/* `flash status [--json]` -- a plain flashctl_get snapshot, formatted. Always returns 0 (the
 * READ succeeded); a done_err state/nonzero result is reported IN the body, not as a command
 * failure -- mirrors GET /api/status's own "the read worked even if the push didn't" contract. */
static int cmd_flash_status(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        flash_err(json, "usage: flash status [--json]");
        return 1;
    }

    flashctl_status_t st;
    flashctl_get(&st);
    char rs[24];
    flash_result_str(st.result, rs, sizeof rs);

    if (json) {
        char buf[256];
        jsonw_t w;
        jsonw_begin(&w, buf, sizeof buf);
        jsonw_str(&w, "state", flash_state_name(st.state));
        jsonw_bool(&w, "busy", st.busy);
        jsonw_uint(&w, "pct", (unsigned long long)st.pct);
        jsonw_str(&w, "result", rs);
        jsonw_str(&w, "ver", st.ver);
        jsonw_str(&w, "hwid", st.hwid);
        jsonw_uint(&w, "size", (unsigned long long)st.size);
        if (!jsonw_end(&w)) {
            flash_err(true, "status body too large");
            return 1;
        }
        printf("%s\n", buf);
        return 0;
    }

    printf("state: %s\n", flash_state_name(st.state));
    printf("busy: %s\n", st.busy ? "true" : "false");
    printf("pct: %u\n", (unsigned)st.pct);
    printf("result: %s\n", rs);
    printf("ver: %s\n", st.ver);
    printf("hwid: %s\n", st.hwid);
    printf("size: %lu\n", (unsigned long)st.size);
    return 0;
}

/* `flash abort` -- see the file header note. Only meaningful while flashctl's guard is held in
 * FLASHCTL_STAGING. Three sub-cases, distinguished by this file's own s_staged/s_stage_inflight
 * flags (flashctl's state alone cannot tell any of them apart -- it has no notion of WHICH front
 * end holds a STAGING claim):
 *   - s_staged true: a `flash stage` already completed (STAGE-END printed) and is waiting for
 *     `flash push`. Reachable today (this console is otherwise idle in that window) -- discards
 *     the staged image outright: real semantics, not a no-op.
 *   - s_stage_inflight true (s_staged still false): a raw receive owned by THIS console would be
 *     in flight. NOT reachable from this same console today (its REPL task is blocked inside
 *     `flash stage`'s own call the whole time), but wired for a future second front-end -- sets
 *     s_abort, which the raw loop checks every iteration.
 *   - neither: this console holds NEITHER half of the claim, yet flashctl reports STAGING -- the
 *     claim belongs to somebody else (POST /api/flash, mid stage-then-push; M9, final review).
 *     Answering OK here would let this console cancel a web upload it has no part in and no
 *     visibility into; refuse instead. */
static int cmd_flash_abort(int argc, char **argv, bool json)
{
    (void)argv;
    if (argc != 2) {
        flash_err(json, "usage: flash abort");
        return 1;
    }

    flashctl_status_t st;
    flashctl_get(&st);
    if (st.state != FLASHCTL_STAGING) {
        flash_err(json, "not staging");
        return 1;
    }

    if (s_staged) {
        /* M10 (round 2): as with `flash push` above, ownership is proven with the token, not
         * inferred from flashctl reporting STAGING -- a reclaimed claim (past FLASHCTL_STAGE_TTL_US)
         * would otherwise let this stale abort cancel a DIFFERENT owner's in-flight claim. */
        bool released = flashctl_release_staged(s_stage_token);
        flash_forget_staged();
        if (!released) {
            flash_err(json, "not mine");
            return 1;
        }
        if (json) printf("{\"abort\":true,\"discarded\":true}\n");
        else      printf("OK abort (staged image discarded)\n");
        return 0;
    }

    if (s_stage_inflight) {
        s_abort = true;
        if (json) printf("{\"ok\":true}\n");
        else      printf("OK\n");
        return 0;
    }

    /* M9: flashctl is STAGING, but neither of this console's own ownership flags is set -- the
     * claim belongs to a concurrent POST /api/flash (or a reclaimed-then-reclaimed-again claim
     * from a different front end entirely). Not ours to cancel. */
    if (json) printf("{\"err\":\"not mine\"}\n");
    else      printf("ERR not mine (staging owned by /api/flash)\n");
    return 1;
}

static int cmd_flash_main(int argc, char **argv)
{
    assert(argv != NULL);
    bool json = console_wants_json(&argc, argv);

    if (argc < 2) {
        flash_err(json, "usage: flash stage <size> <sha256hex> | flash push [--json] | "
                        "flash status [--json] | flash abort");
        return 1;
    }

    if (strcmp(argv[1], "stage") == 0) {
        if (json) {
            /* `flash stage` has no --json form: its own output IS the wire protocol. */
            flash_stage_err("usage");
            return 1;
        }
        return cmd_flash_stage(argc, argv);
    }
    if (strcmp(argv[1], "push")   == 0) return cmd_flash_push(argc, argv, json);
    if (strcmp(argv[1], "status") == 0) return cmd_flash_status(argc, argv, json);
    if (strcmp(argv[1], "abort")  == 0) return cmd_flash_abort(argc, argv, json);

    flash_err(json, "unknown subcommand (want stage|push|status|abort)");
    return 1;
}

void cmd_flash_register(void)
{
    console_register("flash",
                     "flash stage <size> <sha256hex> | flash push [--json] | "
                     "flash status [--json] | flash abort",
                     cmd_flash_main);
}
