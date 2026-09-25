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
 * (otastage_abort, or otastage_finish -- which frees it whether it returns OK or an error, no
 * matter which -- already ran, so no extra abort call there), flashctl's single-flight guard is
 * released (flashctl_end_staging(false)), and any image bytes still in flight on the wire (e.g. a
 * host that kept sending after we gave up) are drained off UART0 for a bounded window so they
 * never get interpreted as REPL command characters once we return to the prompt.
 *
 * `flash abort` note: this console is a single REPL task processing one command line at a time --
 * `flash stage`'s raw-read loop runs to completion (or failure) INSIDE that one command's call,
 * blocking the REPL from reading a second command line meanwhile. So `flash abort` typed at THIS
 * console can never actually interrupt a `flash stage` in progress on this same console; the
 * s_abort flag it sets is checked by the raw loop for a FUTURE second front-end (a second UART, or
 * an out-of-band abort mechanism) that Plan 5.6 does not add. Today `flash abort` is only useful
 * while a stage has completed and is waiting for `flash push` (state stays FLASHCTL_STAGING) --
 * setting s_abort there is a harmless no-op (nothing is left to check it) but the command still
 * reports OK, since the guard IS in STAGING and the request is honoured as best this design can.
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

    flashctl_end_staging(true);   /* stays STAGING/busy: ready for `flash push` */
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

    if (otastage_begin(size) != OTASTAGE_OK) {
        /* Ruling 2 maps EVERY otastage_begin failure to "badsize" (it can also fail with
         * OTASTAGE_E_WRITE if the ota_stage partition itself is missing/unreadable, a case that
         * has no more specific wire code here) -- no SHA context was ever established either way,
         * so no otastage_abort() call belongs on this path. */
        flashctl_end_staging(false);
        flash_stage_err("badsize");
        return 1;
    }

    printf("STAGE-READY\r\n");
    fflush(stdout);
    uart_wait_tx_done((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(100));

    return flash_stage_recv(size, sha);
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

    flashctl_status_t st;
    flashctl_get(&st);
    if (!st.busy || st.state != FLASHCTL_STAGING) {
        /* flashctl's guard is not where a just-staged image should leave it -- a concurrent
         * POST /api/flash, or a flashctl_start_push retry after this same console already
         * consumed s_staged once (see below). */
        flash_err(json, "busy");
        return 1;
    }

    if (flashctl_start_push(s_stage_ver, s_stage_hwid, s_stage_size, s_stage_sha) != 0) {
        flash_err(json, "push");
        return 1;
    }
    s_staged = false;   /* consumed: flashctl now owns the push; a retry needs a fresh `flash stage` */

    uint8_t last_pct = 0xFFu;   /* not a valid pct (0..100): forces the first print */
    for (int i = 0; i < FLASH_PUSH_MAX_POLLS; i++) {
        vTaskDelay(pdMS_TO_TICKS(FLASH_PUSH_POLL_MS));
        flashctl_get(&st);
        if (!json && st.pct != last_pct) {
            printf("pushing %u%%\n", (unsigned)st.pct);
            last_pct = st.pct;
        }
        if (st.state == FLASHCTL_DONE_OK || st.state == FLASHCTL_DONE_ERR)
            return flash_push_report(json, &st);
    }
    flash_err(json, "push timeout");
    return 1;
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

/* `flash abort` -- see the file header note: only meaningful while a stage is outstanding
 * (FLASHCTL_STAGING), and today that is only reachable between a completed `flash stage` and the
 * `flash push` that follows it, since this same console's own raw loop cannot be interrupted from
 * the same single-threaded REPL. */
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
    s_abort = true;
    if (json) printf("{\"ok\":true}\n");
    else      printf("OK\n");
    return 0;
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
