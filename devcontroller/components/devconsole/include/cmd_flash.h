/* devcontroller/components/devconsole/include/cmd_flash.h -- the `flash` command module
 * (stage/push/status/abort), Plan 5.6 Task 10.
 *
 * Not part of console.h's PINNED interface (console.c only needs cmd_flash_register, called once
 * from console_start) -- this header exists so cmd_flash_register has a prototype in scope at
 * both its call site (console.c) and its definition (cmd_flash.c), per -Wmissing-prototypes,
 * matching cmd_dc.h/cmd_lt.h/cmd_stream.h/cmd_selftest.h.
 */
#ifndef CMD_FLASH_H
#define CMD_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `flash` command (stage <size> <sha256hex> | push [--json] | status [--json] |
 * abort) via console_register. */
void cmd_flash_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_FLASH_H */
