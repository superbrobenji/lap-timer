/* lt_rtc.c -- RTC-memory state (spec §15.3) + crash-loop uptime tracker.
 *
 * rtc_state_t lives in RTC slow memory so it survives deep sleep (and, on this SoC, WDT/panic
 * resets). 3.2 defines and validates it (§4.7 step 4); 3.5 adds lt_rtc_save() and drives resume
 * from the pipeline. The separate uptime cell lets the boot-time crash-loop check (§17.5) know how
 * long the previous boot ran, which plain esp_timer cannot report (it restarts at every reset).
 */
#include "app/lt_rtc.h"
#include "app/lt_assert.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_rom_crc.h"

#define RTC_ASSERT_CODE 0x0B70

/* RTC_NOINIT_ATTR, not RTC_DATA_ATTR: RTC_DATA_ATTR is re-initialised from the image on a
 * software/panic/WDT reset (it only survives DEEP SLEEP), which would wipe the snapshot exactly
 * when we need it -- after a crash. RTC_NOINIT is never auto-initialised, so it persists across
 * every reset until power-off; lt_rtc_validate gates it on magic+version+CRC, so uninitialised
 * garbage on the first-ever power-on reads as ABSENT/INVALID. */
static RTC_NOINIT_ATTR rtc_state_t s_rtc;

#define UPTIME_MAGIC 0x5054494Du   /* 'PTIM' */
static RTC_NOINIT_ATTR uint32_t s_uptime_magic;
static RTC_NOINIT_ATTR uint32_t s_uptime_s;

static uint32_t rtc_crc(const rtc_state_t *s)
{
    /* CRC32 over all bytes except the trailing crc32 field (§15.3). */
    LT_ASSERT_RET(s != NULL, RTC_ASSERT_CODE, 0);
    return esp_rom_crc32_le(0, (const uint8_t *)s, sizeof(*s) - sizeof(s->crc32));
}

rtc_validity_t lt_rtc_validate(rtc_state_t *out)
{
    if (s_rtc.magic != RTC_STATE_MAGIC) return RTC_ABSENT;   /* cold boot / cleared */
    if (s_rtc.version != RTC_STATE_VERSION) return RTC_INVALID;
    if (s_rtc.crc32 != rtc_crc(&s_rtc)) return RTC_INVALID;
    if (out) memcpy(out, &s_rtc, sizeof(*out));
    return RTC_VALID;
}

void lt_rtc_clear(void)
{
    memset(&s_rtc, 0, sizeof(s_rtc));   /* magic cleared -> ABSENT next validate */
}

void lt_rtc_save(const lap_rtc_t *lr, const char *session_id, int64_t saved_gps_us,
                 uint8_t mode, uint8_t power_state, uint32_t partial_count)
{
    /* WRITER preconditions: lr is dereferenced unconditionally below (unlike session_id, which is
     * documented optional and already NULL-checked). sector_idx is "sector gates crossed so far"
     * (app/lt_rtc.h) -- the lap engine bounds it to <= LAP_MAX_SECTORS (core/lap.c's sec_next is
     * 1..n_sec, n_sec <= LAP_MAX_SECTORS); a wider value would mean a corrupt/foreign snapshot,
     * not a legitimate lap state. mode is MODE_LAP/MODE_DRAG (app/lt_ipc.h: 0/1). */
    LT_ASSERT_VOID(lr != NULL, RTC_ASSERT_CODE);
    LT_ASSERT_VOID(lr->sector_idx <= LAP_MAX_SECTORS, RTC_ASSERT_CODE);
    LT_ASSERT_VOID(mode <= 1u, RTC_ASSERT_CODE);

    /* §15.3 "session_epoch_mono_us: keep existing if set" -- preserve it across saves. */
    int64_t epoch = s_rtc.session_epoch_mono_us;

    /* memset first so every pad byte is deterministic before the CRC (the CRC covers the whole
     * struct except crc32, padding included). */
    memset(&s_rtc, 0, sizeof s_rtc);
    s_rtc.magic       = RTC_STATE_MAGIC;
    s_rtc.version     = RTC_STATE_VERSION;
    s_rtc.mode        = mode;
    s_rtc.power_state = power_state;
    /* _pad, _pad2 stay 0 from the memset */
    s_rtc.saved_gps_us          = saved_gps_us;
    s_rtc.session_epoch_mono_us = epoch;
    if (session_id) (void)snprintf(s_rtc.session_id, sizeof s_rtc.session_id, "%s", session_id);

    s_rtc.venue_id         = lr->venue_id;
    s_rtc.layout_id        = lr->layout_id;
    s_rtc.lap_no           = lr->lap_no;
    s_rtc.sector_idx       = lr->sector_idx;
    s_rtc.lap_start_gps_us = lr->lap_start_gps_us;
    memcpy(s_rtc.gate_times, lr->gate_times, sizeof s_rtc.gate_times);
    s_rtc.best             = lr->best;
    s_rtc.prev             = lr->prev;
    s_rtc.partial_count    = partial_count;

    s_rtc.crc32 = rtc_crc(&s_rtc);   /* last: CRC over all bytes except crc32 */
}

uint32_t lt_rtc_uptime_prev_s(void)
{
    return (s_uptime_magic == UPTIME_MAGIC) ? s_uptime_s : 0;
}

void lt_rtc_uptime_update_s(uint32_t uptime_s)
{
    s_uptime_magic = UPTIME_MAGIC;
    s_uptime_s = uptime_s;
}
