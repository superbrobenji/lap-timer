/* export_serial.h -- serial fallback export console (spec §18.4).
 *
 * An IDF esp_console REPL on UART0 (line editing disabled, §18.4). It registers the text
 * commands of §18.4 that are in scope for plan 03 -- status, config get|set, errlog [clear],
 * diag, delete <id>, close -- each wired to the transport-agnostic app/cmd dispatch, framed per
 * §18.4 (`---BEGIN <name> <size>---` ... `---END <crc32 hex>---`). `list`/`open <id> <fmt>`/`read
 * <offset>` stream session files through the same framing (Base64 body for the binary log/sum
 * formats). It also registers `dbg`, which carries the 3.2-3.4 diagnostics verbs plus
 * `crash`/`hang`/`rtc`. Replaces the minimal 3.2-3.4 dbg_console.
 */
#ifndef EXPORT_SERIAL_H
#define EXPORT_SERIAL_H

/* Spawn the REPL task (boot step 12+). reset_reason is esp_reset_reason() from boot, shown by
 * `dbg status`. */
void export_serial_start(int reset_reason);

#endif /* EXPORT_SERIAL_H */
