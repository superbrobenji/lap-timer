#ifndef CORE_UI_ICONS_H
#define CORE_UI_ICONS_H
#include <stdint.h>

/* Status-strip icons (spec §20.2), 12x12 1bpp, MSB-first, row-major, bit=1 is icon ink —
 * same convention as fonts.h. render.c's fb_icon(fb, icon_id, x, y) (Task 2) blits
 * icon_bitmaps[icon_id] at the given framebuffer position. */
#define ICON_W      12
#define ICON_H      12
#define ICON_STRIDE 2 /* (ICON_W + 7) / 8 */

typedef enum {
    ICON_GPS_STRIKE = 0, /* SYS_GPS_DEAD: hollow GPS ring with a diagonal strike */
    ICON_GPS_NOFIX,      /* SYS_GPS_NOFIX: hollow GPS pin -- searching, not dead (§20.5) */
    ICON_IMU_STRIKE,     /* SYS_IMU_DEAD: the IMU "?" with a diagonal strike */
    ICON_IMU_Q,          /* SYS_IMU_SUSPECT: IMU/orientation quality disagreement */
    ICON_DISK_STRIKE,    /* SYS_STORAGE_DEAD: floppy outline with a diagonal strike */
    ICON_DISK_FULL,      /* SYS_STORAGE_FULL */
    ICON_DISK_WARN,      /* SYS_STORAGE_DEGRADED (near full / write error) */
    ICON_BATT_LOW,       /* SYS_BATT_LOW */
    ICON_SAFE,           /* SYS_SAFE_MODE */
    ICON_THERMOMETER,    /* SYS_DISP_TEMP_THROTTLE */
    ICON_LEAN_Q,         /* SYS_FUSION_DISAGREE: lean "?" */
    ICON_SIM,            /* SCR_UI_SIM */
    ICON_MOVING,         /* SCR_UI_MOVING */
    ICON_LINK,           /* SCR_UI_LINK */
    ICON_COUNT
} icon_id_t;

/* icon_bitmaps[id][row] is ICON_STRIDE bytes covering the icon's ICON_W columns: byte 0 holds
 * columns 0-7 (MSB = column 0) and byte 1's TOP four bits hold columns 8-11, so byte 1's LOW
 * nibble is the unused padding and is always 0 (io false claim 6: icons.h and icons.c used to
 * contradict each other about which nibble pads). */
extern const uint8_t icon_bitmaps[ICON_COUNT][ICON_H][ICON_STRIDE];

#endif
