/* display_epaper.c -- hal/display.h over a Waveshare SSD1680 e-paper panel (spec §20.1, Plan 7).
 *
 * Stub: Task 5 replaces every body below with the real SPI3_HOST transactions, the reset/init
 * command sequence, the dirty-rect partial-refresh window and the BUSY-polling timeout ladder.
 * For now disp_init fills and hands back the panel capability table (hardcoded here from the
 * PANEL build flag; Task 4's epd_panel() table becomes the real source) so callers can link
 * against a complete hal/display.h and query caps before the driver exists; every other entry
 * point returns -ENOSYS. host/epd_panel.c (panel table) and host/epd_rotate.c (rotation/window
 * rounding) are placeholder files Task 4 fills in.
 */
#include "hal/display.h"
#include "build_config.h"

#include "core/core.h"

#include <errno.h>
#include <stdbool.h>

#define DISP_ASSERT_CODE 0x0C90   /* Power of 10 rule 5 (core/core.h); display_epaper.c's own code */

#if CFG_PANEL_WS213V4
static const disp_caps_t s_caps = { 250, 122, 1, 0, 45, 2000, 400 };
#else
static const disp_caps_t s_caps = { 296, 128, 1, 0, 45, 2000, 400 };
#endif

static bool s_inited;

int disp_init(const disp_caps_t **caps)
{
    CORE_ASSERT_RET(caps != NULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(!s_inited, DISP_ASSERT_CODE, -EALREADY);   /* disp_reinit() is the re-init path */

    *caps = &s_caps;
    s_inited = true;
    return 0;
}

int disp_blit(const uint8_t *fb)
{
    CORE_ASSERT_RET(fb != NULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);   /* disp_init() must run first */
    return -ENOSYS;
}

int disp_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)x;
    (void)y;
    CORE_ASSERT_RET(w > 0, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(h > 0, DISP_ASSERT_CODE, -EINVAL);
    return -ENOSYS;
}

int disp_refresh(uint8_t mode)
{
    CORE_ASSERT_RET(mode == DISP_PARTIAL || mode == DISP_FULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);
    return -ENOSYS;
}

int disp_sleep(void)
{
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_caps.width > 0 && s_caps.height > 0, DISP_ASSERT_CODE, -EIO);
    return -ENOSYS;
}

int disp_wake(void)
{
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_caps.width > 0 && s_caps.height > 0, DISP_ASSERT_CODE, -EIO);
    return -ENOSYS;
}

int disp_reinit(void)
{
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_caps.width > 0 && s_caps.height > 0, DISP_ASSERT_CODE, -EIO);
    return -ENOSYS;
}
