/* core/btn_parse.h -- pure name/hold-ms parser for the bench button injector (Plan 7 T8).
 *
 * export_serial.c's `dbg btn <mode|up|down|up+down> [hold_ms]` is IDF-bound (esp_console,
 * FreeRTOS, board.h), so the parsing that turns the two argv strings into a button mask and a
 * clamped hold duration lives here instead, where the host test build can exercise it directly
 * with no ESP-IDF dependency.
 */
#ifndef CORE_BTN_PARSE_H
#define CORE_BTN_PARSE_H

#include <stdint.h>

/* Parse `name` (one of "mode"/"up"/"down"/"up+down") into *mask (bit0/bit1/bit2/bit1|bit2) and
 * `ms` into *hold_ms: NULL defaults to 100, else `ms` must be a decimal string fully consumed by
 * strtoul, clamped to 20..5000 either way. Returns 0 on success, -1 for an unrecognised `name`,
 * -2 for a malformed `ms` (empty, a leading '-', non-numeric, or not fully consumed). On error
 * neither *mask nor *hold_ms is written. */
int btn_parse(const char *name, const char *ms, uint8_t *mask, uint32_t *hold_ms);

#endif /* CORE_BTN_PARSE_H */
