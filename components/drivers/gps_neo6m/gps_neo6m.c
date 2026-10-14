/* gps_neo6m.c -- PLAN-03 STUB of hal/gps.h for the NEO-6M (spec §4.6 GPS=neo6m, §7.3).
 *
 * The real UBX driver (UART setup, config push, gps_ubx_common parser, §7.5 fault ladder) lands
 * with the physical sensor in Plan 08. This stub exists so moto_neo6m -- a REQUIRED CI build --
 * links and the pipeline task starts.
 *
 * Ruling R28 (2026-10-13 audit fix wave, drv:H3): it reports FAILURE, not success. It used to
 * return 0 from gps_init, so §17.6's boot self-test and the BOOT screen read "GPS OK" on a board
 * with no receiver attached. On moto_neo6m the BOOT screen now reads GPS FAIL until Plan 08 --
 * which is the truth. The profile is still published (with live = 0: see gps_profile_t.live),
 * because the §19.4 OTA-validate gate needs to know this is not a live receiver even when init
 * fails, and because the pipeline logs the advertised rate either way.
 */
#include "hal/gps.h"
#include "core/core.h"

#include <errno.h>
#include <stdio.h>

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define GPS_NEO_ASSERT_CODE 0x0C30

static const gps_profile_t s_profile = {
    .max_rate_hz = 5,
    .baud        = 38400,
    .has_pps     = 0,
    .live        = 0,            /* R1: not a real receiver until the Plan-08 driver replaces this */
    .name        = "neo6m-stub",
};

int gps_init(const gps_profile_t **out_profile)
{
    if (out_profile) *out_profile = &s_profile;
    return -ENODEV;              /* R28: no receiver is attached and none can be (Plan 08) */
}

int gps_configure(uint8_t rate_hz) { (void)rate_hz; return -ENODEV; }
int gps_poll(gps_fix_t *out)       { (void)out; return 0; }    /* 0 == "no new fix", not an error */
int gps_feed_bytes(const uint8_t *buf, size_t n) { (void)buf; (void)n; return -ENODEV; }
int gps_set_power_mode(uint8_t mode) { (void)mode; return -ENODEV; }
int gps_wake(void)                 { return -ENODEV; }
int gps_reinit_uart(uint32_t baud) { (void)baud; return -ENODEV; }

int gps_get_version(char *buf, size_t n)
{
    CORE_ASSERT_RET(buf != NULL, GPS_NEO_ASSERT_CODE, -1);   /* written by snprintf below */
    CORE_ASSERT_RET(n > 0, GPS_NEO_ASSERT_CODE, -1);         /* destination has room */
    (void)snprintf(buf, n, "neo6m-stub (no receiver)");
    return -ENODEV;              /* §17.6's "GPS present: MON-VER reply" row: no reply exists */
}

uint32_t gps_stats_frames_ok(void)    { return 0; }
uint32_t gps_stats_frames_bad(void)   { return 0; }
int64_t  gps_last_frame_mono_us(void) { return 0; }
