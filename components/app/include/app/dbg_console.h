/* app/dbg_console.h -- diagnostics console entry point (spec §18.4; 3.2 status, 3.3 storage/logger verbs).
 *
 * Starts an IDF esp_console REPL on UART0 and registers a single `dbg` command with the verbs
 * `status` (boot count, reset reason, uptime, crash counters, sys_flags, heartbeats), `logtest [n]`
 * (drive the logger end-to-end for the power-cut exit test), `fs` (mount/free/eviction state),
 * `sum <id>` and `logck <id>` (read a session's `.sum`/`.log` back through a `core/ses` reader). The
 * full §18.4 export console (status/list/open/get/... and export) replaces this in session 3.5. */
#ifndef APP_DBG_CONSOLE_H
#define APP_DBG_CONSOLE_H

void dbg_console_start(int reset_reason);   /* reset_reason: esp_reset_reason() from boot */

#endif /* APP_DBG_CONSOLE_H */
