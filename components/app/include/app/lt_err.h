/* app/lt_err.h -- system error codes (spec §17.7). 3.2 needs the E_SYS_* subset used by the
 * boot sequence, crash-loop detection and supervisor; the full code table lands with the
 * serial console (3.5). Codes are stable u16 values logged into the error ring (§15.2). */
#ifndef APP_LT_ERR_H
#define APP_LT_ERR_H

enum {
    /* storage (§17.7); the full code table lands with the 3.5 console. */
    E_STO_WRITE       = 0x0401,
    E_STO_MOUNT       = 0x0402,
    E_STO_FORMAT      = 0x0403,
    E_STO_FULL        = 0x0404,
    E_STO_EVICT       = 0x0405,

    E_SYS_WDT_RESET   = 0x0501,
    E_SYS_PANIC       = 0x0502,
    E_SYS_BROWNOUT    = 0x0503,
    E_SYS_HEAP_LOW    = 0x0504,
    E_SYS_SAFE_MODE   = 0x0505,
    E_SYS_TASK_STALL  = 0x0506,
    E_SYS_RTC_INVALID = 0x0507,
    E_SYS_CFG_RESET   = 0x0508,
    E_SYS_STACK_LOW   = 0x0509,
};

#endif /* APP_LT_ERR_H */
