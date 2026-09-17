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
    ICON_GPS = 0,    /* GPS fix acquired */
    ICON_GPS_STRIKE, /* GPS, no fix (SYS_GPS_NOFIX) */
    ICON_IMU_Q,      /* IMU/orientation quality disagreement */
    ICON_DISK,       /* storage OK */
    ICON_DISK_FULL,  /* storage full */
    ICON_DISK_WARN,  /* storage warning (near full / write error) */
    ICON_BATT_LOW,   /* SYS_BATT_LOW */
    ICON_THERMOMETER,/* SYS_DISP_TEMP_THROTTLE */
    ICON_BLE,        /* BLE connected/advertising */
    ICON_SAFE,       /* safe mode */
    ICON_COUNT
} icon_id_t;

/* icon_bitmaps[id][row] is ICON_STRIDE bytes covering the icon's ICON_W columns
 * (the low 4 bits of the 2nd byte are unused padding, always 0). */
extern const uint8_t icon_bitmaps[ICON_COUNT][ICON_H][ICON_STRIDE];

#endif
