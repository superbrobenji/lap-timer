/* devcontroller/components/devconsole/include/cmd_lt.h -- the `lt`/`link` command modules
 * (Plan 5.6 Task 5): a manual relay to the lap-timer's framed console commands, with round-trip
 * timing and a `link trace` debug hexdump.
 *
 * Not part of console.h's PINNED interface (console.c only needs cmd_lt_register, called once
 * from console_start) -- this header exists so cmd_lt_register has a prototype in scope at both
 * its call site (console.c) and its definition (cmd_lt.c), per -Wmissing-prototypes. Mirrors
 * cmd_dc.h's one-function-per-header shape.
 */
#ifndef CMD_LT_H
#define CMD_LT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers both the `lt` command (status|list|config get|config set <json>|delete <id>|
 * open <id> <fmt>|shell) and the `link` command (trace on|off) via console_register. `lt shell`
 * (Plan 5.6 Task 8) is dispatched to cmd_shell_run (cmd_shell.c/cmd_shell.h) rather than
 * registered on its own -- it is a subcommand of `lt`, not a separate top-level command. */
void cmd_lt_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_LT_H */
