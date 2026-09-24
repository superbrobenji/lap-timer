/* devcontroller/components/devconsole/include/cmd_shell.h -- `lt shell`: a transparent raw-byte
 * bridge between the dev-kit's own USB console and the lap-timer's console over UART1 (Plan 5.6
 * Task 8).
 *
 * Not part of console.h's PINNED interface -- this header exists so cmd_shell_run has a
 * prototype in scope at both its call site (cmd_lt.c's `lt` dispatcher, NOT its own
 * console_register: `shell` is a subcommand of `lt`, same shape as `lt status`/`lt list`/...) and
 * its definition (cmd_shell.c), per -Wmissing-prototypes.
 */
#ifndef CMD_SHELL_H
#define CMD_SHELL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs `lt shell`: bridges raw bytes between the console UART (USB) and DC_LINK_UART (the
 * lap-timer) until the user types "~." at the start of a line, or a 10-minute cap elapses.
 * `json` is true when the caller stripped a trailing --json -- shell has no JSON form, so that
 * prints `ERR shell has no json form` and returns 1 without touching the link. Otherwise takes
 * linkhost's bridge (linkhost_bridge_begin/_end); a busy link prints "busy" and returns 1.
 * Returns 0 on a clean exit (the escape sequence or the cap), 1 on --json or busy. */
int cmd_shell_run(bool json);

#ifdef __cplusplus
}
#endif

#endif /* CMD_SHELL_H */
