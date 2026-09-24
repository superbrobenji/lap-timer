/* devcontroller/components/devconsole/include/cmd_selftest.h -- the `selftest` command module
 * (link/stream/framing/all), Plan 5.6 Task 9.
 *
 * Not part of console.h's PINNED interface (console.c only needs cmd_selftest_register, called
 * once from console_start) -- this header exists so cmd_selftest_register has a prototype in
 * scope at both its call site (console.c) and its definition (cmd_selftest.c), per
 * -Wmissing-prototypes, matching cmd_dc.h/cmd_lt.h/cmd_stream.h.
 */
#ifndef CMD_SELFTEST_H
#define CMD_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `selftest` command (link [N] | stream [s] | framing | all [--json]) via
 * console_register. */
void cmd_selftest_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_SELFTEST_H */
