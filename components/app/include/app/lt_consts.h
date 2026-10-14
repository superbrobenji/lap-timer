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

/* §17.2 supervision thresholds (Appendix A). HEAP_CLEAR_KB is new: §17.2 gives only the 40 KB
 * entry threshold, and a single threshold makes SYS_HEAP_LOW -- and therefore EV_FAULT -- flap
 * once per second on a heap hovering at the line, so the flag releases with 8 KB of hysteresis
 * (audit-fix-wave design R6). The "< 20 KB -> conn_stop()" half of §17.2 is struck: there is no
 * conn task (BLE is sub-project C). */
#define HEAP_LOW_KB          40      /* §17.2/§4.8: free 8-bit heap below this -> SYS_HEAP_LOW */
#define HEAP_CLEAR_KB        48      /* ... and at or above this the flag releases (R6) */
#define STACK_LOW_B          256     /* §17.2: task stack high-water below this -> E_SYS_STACK_LOW */
#define HEAP_INTEGRITY_PERIOD_S 60   /* §17.1: heap_caps_check_integrity_all cadence */

/* §19.5/§19.6 OTA policy that is not a battery/charger figure. */
#define OTA_STILL_MIN_MS     30000u  /* §19.5: no EV_MOTION for 30 s before an OTA_BEGIN is accepted */
#define OTA_RECV_TIMEOUT_S   30      /* §19.6: an OTA_RECV with no OTA_DATA for this long is aborted */
#define OTA_CLOSE_TIMEOUT_MS 2000u   /* §19.5: bounded wait for LOGGER_CLOSE_SESSION's ack */

#endif /* APP_LT_CONSTS_H */
