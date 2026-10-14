/* app/lt_err.h -- system error codes (spec §17.7). The table below is still a SUBSET of §17.7:
 * E_GPS_*, E_IMU_*, E_STO_SD_DEGRADED, E_PWR_*, E_LAP_PLAUSIBILITY and E_FUSION_* are absent
 * because their producers are (GPS/IMU/fusion ladders -> Plan 8; power/SD -> Plan 6). Every code
 * defined here has at least one writer except E_DISP_TEMP and E_CONN_BLE_INIT (reserved: no
 * producer yet; BLE is deferred to sub-project C). Codes are stable u16 values logged into the
 * error ring (§15.2) and are APPEND-ONLY -- never renumber one. */
#ifndef APP_LT_ERR_H
#define APP_LT_ERR_H

enum {
    /* display (§17.7, §20.1/§20.3): the SSD1680 driver + refresh ladder (Plan 7 Task 5). */
    E_DISP_BUSY_TIMEOUT = 0x0301,
    E_DISP_DEAD         = 0x0302,
    E_DISP_TEMP         = 0x0303,

    /* storage (§17.7); all five codes have writers; E_STO_SD_DEGRADED waits for Plan 6 (SD). */
    E_STO_WRITE       = 0x0401,
    E_STO_MOUNT       = 0x0402,
    E_STO_FORMAT      = 0x0403,
    E_STO_FULL        = 0x0404,
    E_STO_EVICT       = 0x0405,

    E_SYS_WDT_RESET     = 0x0501,
    E_SYS_PANIC         = 0x0502,
    E_SYS_BROWNOUT      = 0x0503,
    E_SYS_HEAP_LOW      = 0x0504,
    E_SYS_SAFE_MODE     = 0x0505,
    E_SYS_TASK_STALL    = 0x0506,
    E_SYS_RTC_INVALID   = 0x0507,
    E_SYS_CFG_RESET     = 0x0508,
    E_SYS_STACK_LOW     = 0x0509,
    E_SYS_RECOVERY_MODE = 0x050A, /* arg = boot count (§17.5 recovery-mode amendment, debt sweep A #62) */
    E_NVS_BLOB_RESET    = 0x050B,   /* arg = blob tag 1 ctr / 2 ring / 3 crash */
    E_LOG_CLOSE_TIMEOUT = 0x050C, /* arg = END.reason (core/ses.h SES_END_*): supervisor-owned
                                    * restart proceeded without a clean session close (debt sweep A #59) */
    E_SYS_WDT_SUBSCRIBE = 0x050D, /* arg = hb_id: esp_task_wdt_add() failed for that task (§17.1) */
    E_SYS_HEAP_CORRUPT  = 0x050E, /* arg = uptime_s: heap_caps_check_integrity_all() failed (§17.1) */
    E_NVS_INIT          = 0x050F, /* arg = 0: lt_nvs_init() failed; SYS_NVS_DEAD set (app:L3) */
    E_BOARD_INIT        = 0x0510, /* arg = 0: board_init() reported a peripheral bring-up failure (drv:L18) */

    /* connectivity / command protocol (§17.7, §18.1); written by the §18.4 serial console and
     * command layer; E_CONN_BLE_INIT is reserved for sub-project C. */
    E_CONN_BLE_INIT   = 0x0701,
    E_CONN_XFER_ABORT = 0x0702,
    E_CONN_PROTO      = 0x0703,   /* unknown op / malformed payload / not-yet-implemented op */

    /* OTA (§17.7, §19.6). Land with the OTA receive-side (Plan 5 sub-project A, session 5.4).
     * NOTE (audit fix wave ruling R3, 2026-10-13): these six values disagree
     * with §17.7's table on five of six codes. The FIRMWARE values below are authoritative -- they
     * are the ones in the field, in the project's operating notes and in every errlog already
     * captured; §17.7 and Appendix A are amended to match (owner: plan F4). Any host-side decoder
     * must be written from this enum, not from §17.7's historical table. */
    E_OTA_PRECOND     = 0x0801,   /* precondition failed (§19.5: low battery, already pending, ...) */
    E_OTA_HWID        = 0x0802,   /* target image hwid / project_name mismatch */
    E_OTA_WRITE       = 0x0803,   /* esp_ota_write / SHA-256 mismatch */
    E_OTA_SIG         = 0x0804,   /* ECDSA signature verification failed (esp_ota_end) */
    E_OTA_VALIDATED   = 0x0805,   /* pending image passed self-test -> marked valid (info) */
    E_OTA_ROLLBACK    = 0x0806,   /* bootloader rolled back a bad image (logged next boot) */
    E_OTA_BATT_UNWIRED = 0x0807,  /* arg = 0: §19.5's battery/charger precondition was SKIPPED
                                   * because this build has no power hardware (CFG_CHRG_WIRED == 0,
                                   * #54/Plan 6). Logged once per boot so the deviation is visible
                                   * in the errlog instead of hidden in a floating pin (drv:M11). */
};

#endif /* APP_LT_ERR_H */
