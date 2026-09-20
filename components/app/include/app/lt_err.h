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

    /* connectivity / command protocol (§17.7, §18.1); land with the 3.5 serial console. */
    E_CONN_BLE_INIT   = 0x0701,
    E_CONN_XFER_ABORT = 0x0702,
    E_CONN_PROTO      = 0x0703,   /* unknown op / malformed payload / not-yet-implemented op */

    /* OTA (§17.7, §19.6). Land with the OTA receive-side (Plan 5 sub-project A, session 5.4). */
    E_OTA_PRECOND     = 0x0801,   /* precondition failed (§19.5: low battery, already pending, ...) */
    E_OTA_HWID        = 0x0802,   /* target image hwid / project_name mismatch */
    E_OTA_WRITE       = 0x0803,   /* esp_ota_write / SHA-256 mismatch */
    E_OTA_SIG         = 0x0804,   /* ECDSA signature verification failed (esp_ota_end) */
    E_OTA_VALIDATED   = 0x0805,   /* pending image passed self-test -> marked valid (info) */
    E_OTA_ROLLBACK    = 0x0806,   /* bootloader rolled back a bad image (logged next boot) */
};

#endif /* APP_LT_ERR_H */
