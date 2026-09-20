/* lt_consts.h -- firmware policy constants (spec Appendix A). Engine/geometry constants stay in
 * core/consts.h; these are app/firmware policy (RTC resume freshness, crash-loop, safe mode). */
#ifndef APP_LT_CONSTS_H
#define APP_LT_CONSTS_H

#define RTC_RESUME_MAX_S     14400   /* §15.3: resume an interrupted session only if the first fix is
                                        within this many seconds of the saved gps time */
#define CRASH_LOOP_N         3       /* §17.5: this many consecutive abnormal resets ... */
#define CRASH_LOOP_WINDOW_S  60      /*        ... each with uptime below this -> safe mode */
#define SAFE_MODE_CLEAR_S    600     /* §17.5: uptime in safe mode after which the supervisor clears it */

/* §19.4/§19.5 OTA receive-side policy. */
#define BATT_OTA_MIN_PCT     50      /* §19.5: OTA allowed at >=50% battery (or on charger) */
#define BATT_OTA_MIN_MV      3800    /* mV stand-in for >=50% single-cell LiPo until the power task
                                        provides a real batt_pct; OTA also proceeds on the charger */
#define OTA_MAX_IMG_BYTES    (0x140000u - 4096u)   /* §19.5: image must fit an OTA slot with margin */
#define OTA_REBOOT_DELAY_S   2       /* §19.4: delay after the OTA_END ack before the supervisor reboots */
#define OTA_VALID_UPTIME_S   30      /* §19.4: min stable uptime before a pending image is marked valid */

#endif /* APP_LT_CONSTS_H */
