/* devcontroller/components/devconsole/include/cmd_stream.h -- the `stream` command module
 * (stats/tap), Plan 5.6 Task 6.
 *
 * Not part of console.h's PINNED interface (console.c only needs cmd_stream_register, called once
 * from console_start) -- this header exists so cmd_stream_register has a prototype in scope at
 * both its call site (console.c) and its definition (cmd_stream.c), per -Wmissing-prototypes.
 * console_stream_tap itself (the tap hook main.c's stream_consumer calls) is declared in
 * console.h, not here, since it is consumed outside devconsole.
 */
#ifndef CMD_STREAM_H
#define CMD_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `stream` command (stats [--json] | tap on|off [fused|event|status]) via
 * console_register. */
void cmd_stream_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_STREAM_H */
