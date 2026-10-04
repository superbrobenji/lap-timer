/* hal/display.h -- display HAL contract (spec §20.1, called from the ui task only; not
 * reentrant).
 *
 * The declarations below are the §20.1 display.h slice verbatim; the include guard and the
 * <stdint.h>/<stddef.h> includes are added here so this is a self-contained, compilable header.
 * All functions return int (0 = OK, negative = -errno-style) unless noted. The concrete
 * implementation is components/drivers/display_${DISPLAY} (display_epaper_ssd1680 drives a
 * Waveshare SSD1680 panel over SPI3_HOST; display_oled_ssd1309 is the car-variant alternative).
 */
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H
#include <stdint.h>
#include <stddef.h>

#define DISP_PARTIAL 0
#define DISP_FULL    1

typedef struct {
    uint16_t width, height;      /* logical, after rotation */
    uint8_t  partial_ok;
    int8_t   temp_min_c, temp_max_c;
    uint16_t full_refresh_ms, partial_refresh_ms;   /* nominal */
} disp_caps_t;

int  disp_init(const disp_caps_t **caps);            /* hw reset + init + one full refresh of the current blit (boot screen); 0 or -EIO/-ETIMEDOUT */
int  disp_blit(const uint8_t *fb);                   /* full framebuffer, 1 bpp, row-major, MSB = leftmost, 0 = black (core/ui convention) */
int  disp_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h); /* dirty rect for the next DISP_PARTIAL; y/h rounded outward to whole 8-px RAM byte columns (and mirrored, ruling B-10); x/w map one-to-one to RAM rows */
int  disp_refresh(uint8_t mode);                     /* DISP_PARTIAL / DISP_FULL; blocks until BUSY clears or timeout (returns -ETIMEDOUT) */
int  disp_sleep(void);
int  disp_wake(void);
int  disp_reinit(void);                              /* ladder step: hw reset + init, keeps the blitted image */
#endif
