/* lt_rtc.c -- RTC-memory state (spec §15.3) + crash-loop uptime tracker.
 *
 * rtc_state_t lives in RTC slow memory so it survives deep sleep (and, on this SoC, WDT/panic
 * resets). 3.2 defines and validates it (§4.7 step 4) but does not resume from it -- resume is
 * 3.5. The separate uptime cell lets the boot-time crash-loop check (§17.5) know how long the
 * previous boot ran, which plain esp_timer cannot report (it restarts at every reset).
 */
#include "app/lt_rtc.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_rom_crc.h"

static RTC_DATA_ATTR rtc_state_t s_rtc;

#define UPTIME_MAGIC 0x5054494Du   /* 'PTIM' */
static RTC_DATA_ATTR uint32_t s_uptime_magic;
static RTC_DATA_ATTR uint32_t s_uptime_s;

static uint32_t rtc_crc(const rtc_state_t *s)
{
    /* CRC32 over all bytes except the trailing crc32 field (§15.3). */
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

uint32_t lt_rtc_uptime_prev_s(void)
{
    return (s_uptime_magic == UPTIME_MAGIC) ? s_uptime_s : 0;
}

void lt_rtc_uptime_update_s(uint32_t uptime_s)
{
    s_uptime_magic = UPTIME_MAGIC;
    s_uptime_s = uptime_s;
}
