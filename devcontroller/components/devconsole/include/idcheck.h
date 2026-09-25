/* devcontroller/components/devconsole/include/idcheck.h -- lt_id_valid: a PURE validator for a
 * session id argument the console is about to put on the wire to the lap-timer (M6, final review:
 * `lt delete <id>`/`lt open <id> <fmt>` sent an operator-typed id straight to `delete`/`open`
 * unvalidated).
 *
 * Mirrors the lap-timer's own session id shape: components/app/logger/logger.c's s_id[11] is built
 * as "S%05u_%03u" (10 characters -- 'S', 5 digits, '_', 3 digits), and components/app/cmd/cmd.c's
 * op_delete/op_open both bound an incoming id to 10 bytes (char id[11]) before touching the
 * filesystem. lt_id_valid enforces that same 10-character bound plus an identifier-safe charset
 * ([A-Za-z0-9_]) so a malformed id (empty, too long, or containing a character like '/' or a space
 * that has no business in a filename) is rejected by THIS console before it ever reaches the wire,
 * rather than being silently truncated/mangled by the lap-timer's own console tokenizer or turned
 * into a path-adjacent surprise.
 *
 * PINNED interface (host-tested: devcontroller/test/test_idcheck.c). Pure, IDF-free: no heap, no
 * shared state.
 */
#ifndef IDCHECK_H
#define IDCHECK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True iff `s` is a non-NULL, non-empty, at-most-10-character C string containing only
 * [A-Za-z0-9_] bytes. Any other input (NULL, empty, >10 chars, or any other byte) is rejected. */
bool lt_id_valid(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* IDCHECK_H */
