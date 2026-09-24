/* devcontroller/components/devconsole/cmd_shell.c -- `lt shell`: a transparent raw-byte bridge
 * between the dev-kit's own USB console and the lap-timer's console over UART1 (Plan 5.6 Task 8).
 *
 * The lap-timer's UART0 carries BOTH its text console (prompts, framed command replies) and its
 * autonomous binary §18.1 stream (0xFF | seq16 | flags | len | payload[len]) -- forwarding those
 * frames verbatim into a human's terminal would inject binary noise (and could itself contain a
 * literal "~." escape sequence), so every UART1 -> USB byte is run through bridge_filter_run
 * first (components/linkhost/host/bridge_filter.c), which drops whole stream frames and passes
 * console text through unchanged.
 *
 * `lt shell` takes ownership of UART1 via linkhost_bridge_begin (the same s_req_mtx +
 * rx_park_and_drain handshake linkhost_download_cmd/linkhost_flash already use to park the demux
 * RX task, #65) so this loop's own uart_read_bytes(DC_LINK_UART, ...) is the sole reader, silences
 * this task's own ESP_LOGx output (it would otherwise land on the same USB UART mid-session), and
 * turns the `stream tap` off so nothing else prints to USB while bridged. Exits on "~." at the
 * start of a line (a bare '~' not followed by '.' is forwarded literally, tilde included) or a
 * 10-minute cap, whichever comes first -- both loops are bounded (the outer by the deadline, the
 * inner by the read count), no heap, no new function pointers (pragmatic P10).
 */
#include "cmd_shell.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "bridge_filter.h"
#include "build_config.h"   /* DC_LINK_UART */
#include "cmd_stream.h"     /* cmd_stream_tap_off */
#include "linkhost.h"       /* linkhost_bridge_begin/_end, linkhost_now_us */

#define SHELL_CAP_US (10LL * 60 * 1000000)   /* 10-minute cap */
#define SHELL_BUF    64u                     /* stack buffer per direction, matches the brief */

int cmd_shell_run(bool json)
{
    if (json) {
        printf("ERR shell has no json form\n");
        return 1;
    }

    if (linkhost_bridge_begin() != 0) {
        printf("busy\n");
        return 1;
    }

    cmd_stream_tap_off();   /* nothing else may print to USB while raw bytes are bridged */

    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("bridged to lap-timer; type ~. on a new line to exit (10 min cap)\n");
    fflush(stdout);

    const uart_port_t usb = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    int64_t deadline = linkhost_now_us() + SHELL_CAP_US;

    bridge_filter_t filt;
    bridge_filter_reset(&filt);

    uint8_t b[SHELL_BUF];
    int at_line_start = 1, tilde = 0;

    while (linkhost_now_us() < deadline) {                      /* bounded by the cap */
        int n = uart_read_bytes(usb, b, sizeof b, pdMS_TO_TICKS(10));
        for (int i = 0; i < n; i++) {                           /* bounded by n */
            if (at_line_start && b[i] == '~') { tilde = 1; continue; }
            if (tilde && b[i] == '.') goto out;
            if (tilde) { uart_write_bytes(DC_LINK_UART, "~", 1); tilde = 0; }
            uart_write_bytes(DC_LINK_UART, &b[i], 1);
            at_line_start = (b[i] == '\r' || b[i] == '\n');
        }

        uint8_t rb[SHELL_BUF];
        n = uart_read_bytes(DC_LINK_UART, rb, sizeof rb, 0);
        if (n > 0) {
            uint8_t filtered[SHELL_BUF];
            size_t w = bridge_filter_run(&filt, rb, (size_t)n, filtered, sizeof filtered);
            if (w > 0) uart_write_bytes(usb, filtered, w);
        }
    }
out:
    esp_log_level_set("*", saved);
    linkhost_bridge_end();
    printf("\nbridge closed (%lu stream frames filtered, %lu bad)\n",
           (unsigned long)filt.frames, (unsigned long)filt.bad);
    return 0;
}
