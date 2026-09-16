/* app/dbg_console.h -- minimal diagnostics console for session 3.2.
 *
 * Starts an IDF esp_console REPL on UART0 and registers a single `dbg status` command
 * (boot count, reset reason, uptime, crash counters, sys_flags, heartbeats). This is the
 * 3.2 roadmap exit command; the full §18.4 console (status/list/open/... and the other dbg
 * verbs) replaces it in session 3.5 on the same REPL. */
#ifndef APP_DBG_CONSOLE_H
#define APP_DBG_CONSOLE_H

void dbg_console_start(int reset_reason);   /* reset_reason: esp_reset_reason() from boot */

#endif /* APP_DBG_CONSOLE_H */
