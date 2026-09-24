/* devcontroller/components/devconsole/include/console.h -- console: esp_console REPL on the
 * dev-kit's own USB (UART0), the primary developer interface so debugging never needs the WiFi AP
 * (Plan 5.6 Task 4).
 *
 * PINNED interface: console_start brings the REPL up (call LAST in app_main, after every other
 * subsystem -- including webapi -- is already registered, so a command module can safely reach
 * into any of them). console_register is the ONE registration door every command module goes
 * through; the esp_console `int (*)(int, char **)` callback is the one function pointer
 * pragmatic-P10 allows here (esp_console's own contract, not ours). console_wants_json strips a
 * trailing `--json` argument so every `dc`/`lt`/... subcommand supports --json with one line.
 *
 * Command modules (this task: cmd_dc.c; later tasks: cmd_lt.c, cmd_stream.c, ...) each expose one
 * `void cmd_<name>_register(void)`, called from console_start after the REPL is created.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

#include "esp_err.h"

#include "linkhost_proto.h"   /* lt_stream_rec_t (console_stream_tap) */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates and starts the esp_console REPL on the console UART (CONFIG_ESP_CONSOLE_UART_NUM),
 * then calls each command module's registration function. Call LAST in app_main, after every
 * other subsystem a command module may reach into is already up. */
esp_err_t console_start(void);

/* Strips a trailing "--json" argument if present: decrements *argc and returns true; otherwise
 * leaves *argc unchanged and returns false. Every --json-capable subcommand calls this first. */
bool console_wants_json(int *argc, char **argv);

/* The one registration door: wraps esp_console_cmd_register. */
void console_register(const char *name, const char *help, int (*fn)(int, char **));

/* Called by main.c's stream_consumer for every popped record (Plan 5.6 Task 6): a no-op unless
 * `stream tap on` is active, filtered by type when one is set, and rate-limited to 5 rows/s.
 * Implemented in cmd_stream.c. Runs on the consumer task, NOT the console task -- keep it short:
 * no logging, no blocking. */
void console_stream_tap(const lt_stream_rec_t *r);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
