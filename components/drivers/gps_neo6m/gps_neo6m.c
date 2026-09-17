/* gps_neo6m.c -- PLAN-03 STUB of hal/gps.h for the NEO-6M (spec §4.6 GPS=neo6m, §7.3).
 *
 * The real UBX driver (UART setup, config push, gps_ubx_common parser, fault ladder) lands with the
 * physical sensor in plan 08. Plan 03 only needs moto_neo6m to BUILD green (a REQUIRED CI check) now
 * that the pipeline calls the GPS HAL: this stub satisfies every hal/gps.h symbol, advertises the
 * NEO-6M profile, and simply never produces a fix (gps_poll returns 0), so the pipeline task starts
 * and idles on that variant. The moto_sim bench build (gps_sim) is the one that runs real laps.
 */
#include "hal/gps.h"

#include <stdio.h>

static const gps_profile_t s_profile = {
    .max_rate_hz = 5,
    .baud        = 38400,
    .has_pps     = 0,
    .name        = "neo6m",
};

int gps_init(const gps_profile_t **out_profile)
{
    if (out_profile) *out_profile = &s_profile;
    return 0;
}

int gps_configure(uint8_t rate_hz) { (void)rate_hz; return 0; }
int gps_poll(gps_fix_t *out)       { (void)out; return 0; }          /* no receiver yet (plan 08) */
int gps_feed_bytes(const uint8_t *buf, size_t n) { (void)buf; (void)n; return 0; }
int gps_set_power_mode(uint8_t mode) { (void)mode; return 0; }
int gps_wake(void)                 { return 0; }
int gps_reinit_uart(uint32_t baud) { (void)baud; return 0; }

int gps_get_version(char *buf, size_t n)
{
    if (!buf || n == 0) return -1;
    (void)snprintf(buf, n, "neo6m-stub");
    return 0;
}

uint32_t gps_stats_frames_ok(void)    { return 0; }
uint32_t gps_stats_frames_bad(void)   { return 0; }
int64_t  gps_last_frame_mono_us(void) { return 0; }
