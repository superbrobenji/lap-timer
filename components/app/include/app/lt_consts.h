/* lt_consts.h -- firmware policy constants (spec Appendix A). Engine/geometry constants stay in
 * core/consts.h; these are app/firmware policy (RTC resume freshness, crash-loop, safe mode). */
#ifndef APP_LT_CONSTS_H
#define APP_LT_CONSTS_H

#define RTC_RESUME_MAX_S     14400   /* §15.3: resume an interrupted session only if the first fix is
                                        within this many seconds of the saved gps time */
#define CRASH_LOOP_N         3       /* §17.5: this many consecutive abnormal resets ... */
#define CRASH_LOOP_WINDOW_S  60      /*        ... each with uptime below this -> safe mode */
#define SAFE_MODE_CLEAR_S    600     /* §17.5: uptime in safe mode after which the supervisor clears it */

#endif /* APP_LT_CONSTS_H */
