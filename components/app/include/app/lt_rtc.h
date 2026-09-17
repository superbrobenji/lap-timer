/* app/lt_rtc.h -- RTC-memory state (spec §15.3) + the crash-loop uptime tracker.
 *
 * rtc_state_t is the deep-sleep/resume snapshot. 3.2 defines it and validates/clears it at
 * boot (§4.7 step 4); 3.5 adds the save path and pipeline resume. lt_rtc_save() populates and
 * CRC's the snapshot from the lap engine's resumable state (lap_rtc_t) plus session identity;
 * the pipeline calls it on every S/F and sector event so the most-recent gate is the resume
 * point after any reset. Separately, a tiny RTC_DATA_ATTR uptime cell is kept so the boot-time
 * crash-loop check (§17.5) can learn how long the *previous* boot ran (esp_timer resets on every
 * reset; RTC slow memory survives WDT/panic and, usually, sleep).
 */
#ifndef APP_LT_RTC_H
#define APP_LT_RTC_H

#include <stdbool.h>
#include <stdint.h>

#include "core/types.h"   /* lap_result_t, LAP_MAX_SECTORS */
#include "core/lap.h"     /* lap_rtc_t (resume snapshot produced by lap_export_rtc) */

#define RTC_STATE_MAGIC   0x4C505452u   /* 'LPTR' */
#define RTC_STATE_VERSION 1

typedef struct {
    uint32_t magic;            /* RTC_STATE_MAGIC */
    uint8_t  version;          /* RTC_STATE_VERSION */
    uint8_t  mode, power_state, _pad;
    int64_t  saved_gps_us;     /* resume only if now - saved < RTC_RESUME_MAX_S (checked in 3.5) */
    int64_t  session_epoch_mono_us;
    char     session_id[10];
    uint16_t venue_id, layout_id;
    uint16_t lap_no; uint8_t sector_idx; uint8_t _pad2;
    int64_t  lap_start_gps_us;
    int64_t  gate_times[LAP_MAX_SECTORS + 1];
    lap_result_t best, prev;   /* trimmed copies */
    uint32_t partial_count;    /* e-paper partial refreshes since last full */
    uint32_t crc32;            /* CRC32 over all bytes except crc32 */
} rtc_state_t;

typedef enum { RTC_ABSENT = 0, RTC_VALID, RTC_INVALID } rtc_validity_t;

/* Validate the RTC snapshot: ABSENT (cold / magic clear), VALID (magic+version+crc ok), or
 * INVALID (present but bad -> caller logs E_SYS_RTC_INVALID). If out != NULL a VALID copy is
 * returned for the pipeline's resume path. */
rtc_validity_t lt_rtc_validate(rtc_state_t *out);
void           lt_rtc_clear(void);   /* zero the snapshot (magic cleared) */

/* Populate and CRC the RTC state from the current lap-engine snapshot + session identity, then
 * store it in RTC_DATA_ATTR memory (survives reset/deep-sleep). Called by the pipeline on every
 * S/F and sector event and before a supervised restart. mode is the operating mode (MODE_LAP/
 * MODE_DRAG), power_state has no owner in plan 03 (pass 0). session_epoch_mono_us is preserved
 * across saves. session_id is a bounded copy into the 10-byte field (may truncate). */
void lt_rtc_save(const lap_rtc_t *lr, const char *session_id, int64_t saved_gps_us,
                 uint8_t mode, uint8_t power_state, uint32_t partial_count);

/* Crash-loop uptime tracker. prev_s() returns the uptime the previous boot reached (0 if the
 * RTC cell is cold/invalid); the supervisor calls update_s() each loop with this boot's uptime. */
uint32_t lt_rtc_uptime_prev_s(void);
void     lt_rtc_uptime_update_s(uint32_t uptime_s);

#endif /* APP_LT_RTC_H */
