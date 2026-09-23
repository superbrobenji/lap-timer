/* devcontroller/components/console/include/cmd_dc.h -- the `dc` command module (status/log/baud),
 * the dev-kit's own self-diagnostics (Plan 5.6 Task 4).
 *
 * Not part of console.h's PINNED interface (console.c only needs cmd_dc_register, called once
 * from console_start) -- this header exists so cmd_dc_register has a prototype in scope at both
 * its call site (console.c) and its definition (cmd_dc.c), per -Wmissing-prototypes. Later
 * command modules (cmd_lt.h, cmd_stream.h, ...) will follow the same one-function-per-header shape.
 */
#ifndef CMD_DC_H
#define CMD_DC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `dc` command (status [--json] | log <level> | baud <rate>) via console_register. */
void cmd_dc_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DC_H */
