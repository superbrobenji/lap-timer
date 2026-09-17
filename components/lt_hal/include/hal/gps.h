/* hal/gps.h -- GPS HAL contract (spec §5.1, called from the pipeline task).
 *
 * The declarations below are the §5.1 gps.h slice verbatim; the include guard, the
 * <stdint.h>/<stddef.h> includes (size_t, fixed-width types) and the GPS_PM_* power-mode values
 * are added here so this is a self-contained, compilable header. gps_fix_t and the GPS_FLAG_*
 * fix-flag bits come transitively from core/types.h (included below), so the engines and the
 * driver share one definition. gps_fix_t / gps_profile_t are the §5.1 structs.
 * All functions return int (0 = OK, negative =
 * -errno-style) unless noted; each is called from one task only (the pipeline) and is not
 * reentrant. The concrete implementation is components/drivers/gps_${GPS} (gps_sim replays a
 * committed synthetic capture; gps_neo6m/gps_m10 parse UBX from the real receiver).
 */
#ifndef HAL_GPS_H
#define HAL_GPS_H

#include <stddef.h>
#include <stdint.h>

#include "core/types.h"   /* gps_fix_t + GPS_FLAG_* (one shared definition) */

/* gps_set_power_mode values (§5.1, §7.5 fault ladder). */
enum {
    GPS_PM_FULL       = 0,   /* continuous navigation */
    GPS_PM_CYCLIC_1HZ = 1,   /* 1 Hz cyclic tracking (power save) */
    GPS_PM_BACKUP     = 2,   /* receiver in backup; gps_wake() resumes */
};

/* GPS receiver profile advertised by gps_init (§5.1). */
typedef struct {
    uint8_t  max_rate_hz;      /* 5 for NEO-6M, 10 for M10 */
    uint32_t baud;             /* 38400 / 115200 */
    uint8_t  has_pps;
    const char *name;
} gps_profile_t;

int  gps_init(const gps_profile_t **out_profile);   /* UART setup, no config push */
int  gps_configure(uint8_t rate_hz);                 /* push full config (§7.3 / §7.4); blocks <= 2 s */
int  gps_poll(gps_fix_t *out);                       /* non-blocking; 1 if a new fix was assembled, 0 if none, <0 error */
int  gps_feed_bytes(const uint8_t *buf, size_t n);   /* pipeline pushes UART bytes; parser runs here */
int  gps_set_power_mode(uint8_t mode);               /* GPS_PM_* */
int  gps_wake(void);                                 /* from BACKUP */
int  gps_get_version(char *buf, size_t n);           /* UBX-MON-VER swVersion; used by self-test */
int  gps_reinit_uart(uint32_t baud);                 /* ladder step */
uint32_t gps_stats_frames_ok(void);
uint32_t gps_stats_frames_bad(void);
int64_t  gps_last_frame_mono_us(void);

#endif /* HAL_GPS_H */
