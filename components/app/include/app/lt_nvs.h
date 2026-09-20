/* app/lt_nvs.h -- persistent state on NVS (spec §15.2). Thin lap-timer layer over the IDF
 * `nvs`/`nvs_flash` API: the boot counter, the 9 crash/health counters, the 32-entry error
 * ring, the crash log (crash-loop detection, §17.5), the safe-mode gate, and the packed cfg
 * blob (version + CRC16 via core). Write policy per §15.2: error-ring entries persist
 * immediately; counters are batched (>=60 s) except on crash paths; cfg only on change.
 */
#ifndef APP_LT_NVS_H
#define APP_LT_NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/cfg.h"

/* The 9 u32 counters of lt_err/ctr (§15.2), in blob order. */
typedef struct {
    uint32_t boots, crashes, wdt, brownout, gps_reset, i2c_recover, sto_format, ota_ok, ota_rollback;
} lt_counters_t;

typedef enum {
    LT_CTR_BOOTS = 0, LT_CTR_CRASHES, LT_CTR_WDT, LT_CTR_BROWNOUT, LT_CTR_GPS_RESET,
    LT_CTR_I2C_RECOVER, LT_CTR_STO_FORMAT, LT_CTR_OTA_OK, LT_CTR_OTA_ROLLBACK,
} lt_counter_id_t;

/* Opens namespaces and loads counters + error ring + crash log into RAM. 0 ok, <0 on NVS error. */
int  lt_nvs_init(void);

/* Boot counter (lt_sys/boot_cnt). */
uint32_t lt_nvs_boot_inc(void);   /* ++ and persist; returns the new value */
uint32_t lt_nvs_boot_get(void);

/* Counters (lt_err/ctr). inc updates RAM + marks dirty; crash-path callers pass persist=true
 * to write immediately (§15.2). flush persists a dirty set when force || >=60 s since last write. */
void     lt_counters_inc(lt_counter_id_t id, bool persist);
int      lt_counters_flush(bool force);
const lt_counters_t *lt_counters(void);

/* Error ring (lt_err/ring): append {code, uptime_s, boot, arg}; persists immediately. */
int  errlog_add(uint16_t code, uint32_t arg);

/* Public shape of one error-ring entry (§17.7); a copy of the private on-flash record, so the
 * on-flash layout (err_entry_t in lt_nvs.c) stays private and unchanged. */
typedef struct {
    uint16_t code;       /* E_* code (§17.7) */
    uint32_t arg;        /* code-specific argument */
    uint32_t uptime_s;   /* uptime when logged */
    uint16_t boot;       /* boot counter (low 16 bits) when logged */
} lt_err_entry_t;

/* Copy the error ring oldest->newest into `out` (up to `cap` entries, empty slots skipped);
 * returns the number copied. Used by ERRLOG_GET / DIAG_GET (§18.1, §17.10). */
int  lt_errlog_snapshot(lt_err_entry_t *out, int cap);
/* Number of surviving (non-empty) error-ring entries -- the same count lt_errlog_snapshot would
 * return with an unbounded cap. Lets callers stream entries by index without a full-ring copy. */
int  lt_errlog_count(void);
/* Fetch the `index`-th error-ring entry (0-based) in the SAME oldest->newest, empty-slots-skipped
 * order lt_errlog_snapshot produces, into `*out`. Returns 0 on success, <0 if index is out of
 * range. Each call takes a consistent single-entry view under the ring lock. */
int  lt_errlog_at(int index, lt_err_entry_t *out);
/* Clear the error ring (RAM + NVS). On-flash layout is unchanged; the ring is zeroed. */
void lt_errlog_clear(void);

/* Crash log (lt_sys/crash_log): shift in {reset_reason, prev_uptime_s} at boot (§17.5). */
void lt_crashlog_push(uint8_t reset_reason, uint32_t prev_uptime_s);
/* True when the last 3 logged resets are all abnormal with uptime < 60 s (§17.5). */
bool lt_crashlog_is_loop(void);

/* Safe-mode gate (lt_sys/safe_until): boot counter through which safe mode applies (§17.5). */
uint32_t lt_safe_until_get(void);
int      lt_safe_until_set(uint32_t boot_cnt);
/* Clears the persisted safe-mode gate (lt_sys/safe_until := 0) so a later boot is never held in
 * safe mode by it. Used by the supervisor's uptime-based auto-clear (§17.5). */
void     lt_safe_clear(void);

/* Synthetic reset reason (§17.2/§17.5): a supervisor-forced restart after a pipeline stall reports
 * ESP_RST_SW, which §17.5 treats as normal. The supervisor leaves a marker (lt_stall_flag_set) and
 * the boot path folds it in as this ABNORMAL reason so N consecutive stall-restarts trip the
 * crash-loop -> safe mode. Value is outside the esp_reset_reason_t range and fits the crash-log u8. */
#define LT_RST_STALL 0x51

/* Stall-restart marker (lt_sys/stall_rst). set: persist before esp_restart() (supervisor).
 * take: read + clear at boot, returns whether the previous boot was a stall-restart (app_main). */
void lt_stall_flag_set(void);
bool lt_stall_flag_take(void);

/* cfg blob (lt_cfg/cfg): load validates version+CRC16 then cfg_validate (returns corrections,
 * <0 => absent/corrupt so the caller keeps its defaults). save packs + CRC16 + writes. */
int  lt_cfg_load(cfg_t *c);
int  lt_cfg_save(const cfg_t *c);

/* True for reset reasons counted as a crash for §17.5 (panic / any WDT / brownout). */
bool lt_reset_is_abnormal(int reset_reason);
/* Human-readable reset reason (shared by the banner and dbg status). */
const char *lt_reset_reason_str(int reset_reason);
/* Boot-time bookkeeping: count + log + crash-log the reset reason (§4.7 step 1). */
void lt_boot_record_reset(int reset_reason, uint32_t prev_uptime_s);

#endif /* APP_LT_NVS_H */
