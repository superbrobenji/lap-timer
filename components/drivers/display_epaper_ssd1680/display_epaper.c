/* display_epaper.c -- hal/display.h over a Waveshare SSD1680 e-paper panel (spec §20.1, Plan 7
 * Task 5). SPI3_HOST (VSPI), mode 0, 10 MHz, DC via a pre-transfer callback, CS hardware-driven,
 * polling transactions only (no queue -- RULING R5), BUSY polled every 1 ms with a 5 s timeout
 * ladder. The panel geometry (native/logical size, RAM width, LUT/border bytes) comes from
 * epd_pure.h's epd_panel() table (Task 4); epd_rotate_line/epd_window_from_rect (same header) do
 * the landscape-framebuffer -> portrait-RAM-row transposition and dirty-rect -> RAM-window
 * rounding, both pure and host-tested (test/test_epd_pure.c).
 *
 * disp_blit() is legal before disp_init() runs (it only stores the caller's framebuffer pointer);
 * ui.c's boot sequence relies on this to blit the BOOT screen before disp_init's first full
 * refresh paints it. The one heap allocation in this file is the spi_device_handle_t IDF itself
 * allocates inside spi_bus_add_device() (once, guarded by s_added) -- everything else here is
 * static storage or the stack.
 */
#include "hal/display.h"
#include "epd_pure.h"

#include "core/core.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task_wdt.h"

#define DISP_ASSERT_CODE 0x0C90   /* Power of 10 rule 5 (core/core.h); display_epaper.c's own code */

#define PIN_DC   GPIO_NUM_14
#define PIN_RST  GPIO_NUM_13
#define PIN_BUSY GPIO_NUM_35
#define PIN_CS   GPIO_NUM_5
#define BUSY_TIMEOUT_MS 5000
#define BUSY_POLL_MS    1
#define WDT_KICK_MS     500   /* epd_wait_busy() kicks the task WDT every this many ms of waiting */

static spi_device_handle_t s_dev;
static const epd_panel_t  *s_panel;
static const uint8_t      *s_fb;    /* last blitted framebuffer (caller-owned, static in ui.c) */
static epd_window_t        s_win;   /* pending partial window; valid when s_win_set */
static bool                s_win_set;
static uint8_t              s_line[16]; /* one panel RAM row (ram_w/8) -- the only driver buffer */
static disp_caps_t         s_caps;
static bool                s_inited;
static bool                s_added;      /* spi_bus_add_device() has run */

/* DC from the transaction's user field: 0 = command, 1 = data. Polling-only transactions
 * (RULING R5) mean this runs in the calling task's own context, not an ISR, so the plain
 * (non-IRAM-safe) core_assert_fail() call below is legal even though the field stays IRAM_ATTR. */
static void IRAM_ATTR pre_cb(spi_transaction_t *t)
{
    CORE_ASSERT_VOID(t != NULL, DISP_ASSERT_CODE);
    CORE_ASSERT_VOID((uintptr_t)t->user <= 1u, DISP_ASSERT_CODE);

    gpio_set_level(PIN_DC, (uint32_t)(uintptr_t)t->user);
}

static int epd_cmd(uint8_t c)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    spi_transaction_t t = { 0 };
    t.flags      = SPI_TRANS_USE_TXDATA;
    t.length     = 8;
    t.tx_data[0] = c;
    t.user       = (void *)0;

    return spi_device_polling_transmit(s_dev, &t) == ESP_OK ? 0 : -EIO;
}

/* n <= 4: embed the bytes in the transaction descriptor (SPI_TRANS_USE_TXDATA); n > 4: the bus is
 * DMA-only past that, so `d` must live inside the static s_line row buffer (whole or a byte
 * sub-slice for a partial refresh's window) -- never a flash-resident (rodata/const) table, which
 * the DMA engine cannot read. */
static int epd_data(const uint8_t *d, size_t n)
{
    CORE_ASSERT_RET(d != NULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(n > 0, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(n <= sizeof s_line, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    spi_transaction_t t = { 0 };
    t.length = n * 8u;
    t.user   = (void *)1;

    if (n <= 4) {
        t.flags = SPI_TRANS_USE_TXDATA;
        for (size_t i = 0; i < n; i++) {
            t.tx_data[i] = d[i];
        }
    } else {
        /* d >= s_line is checked before the pointer subtraction below runs (short-circuit &&) so
         * the subtraction is never evaluated on an out-of-range pointer. */
        CORE_ASSERT_RET(d >= s_line && (size_t)(d - s_line) + n <= sizeof s_line, DISP_ASSERT_CODE,
                         -EINVAL);
        t.tx_buffer = d;
    }

    return spi_device_polling_transmit(s_dev, &t) == ESP_OK ? 0 : -EIO;
}

/* Bounded by the constant BUSY_TIMEOUT_MS/BUSY_POLL_MS (5000 iterations of 1 ms each). Kicks the
 * task WDT every 500 ms of waiting (ignoring its return, RULING R4); the first failed wait
 * aborts the whole caller sequence with -ETIMEDOUT rather than being retried here. */
static int epd_wait_busy(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    const int max_iters  = BUSY_TIMEOUT_MS / BUSY_POLL_MS;
    const int kick_every = WDT_KICK_MS / BUSY_POLL_MS;

    for (int i = 0; i < max_iters; i++) {
        if (gpio_get_level(PIN_BUSY) == 0) {
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(BUSY_POLL_MS));
        if ((i % kick_every) == kick_every - 1) {
            (void)esp_task_wdt_reset();
        }
    }
    return -ETIMEDOUT;
}

static void epd_hw_reset(void)
{
    CORE_ASSERT_VOID(s_panel != NULL, DISP_ASSERT_CODE);
    CORE_ASSERT_VOID(s_dev != NULL, DISP_ASSERT_CODE);

    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* Sets the RAM X/Y window and cursor to the whole panel (0x44/0x45/0x4E/0x4F grouped, per the
 * §20.1 "Full refresh" bullet); used by epd_full_refresh() before each of its two frame writes.
 * Not reused by epd_send_init_seq(), which interleaves 0x3C/0x18/0x21 between the range (0x44/
 * 0x45) and counter (0x4E/0x4F) commands per the exact §20.1 "Init" sequence order. */
static int epd_set_full_window(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    uint16_t hm1 = (uint16_t)(s_panel->native_h - 1u);
    uint8_t d44[2] = { 0x00, (uint8_t)(s_panel->ram_w / 8u - 1u) };
    uint8_t d45[4] = { 0x00, 0x00, (uint8_t)(hm1 & 0xFFu), (uint8_t)(hm1 >> 8) };
    uint8_t d4e[1] = { 0x00 };
    uint8_t d4f[2] = { 0x00, 0x00 };

    if (epd_cmd(0x44) != 0 || epd_data(d44, sizeof d44) != 0) return -EIO;
    if (epd_cmd(0x45) != 0 || epd_data(d45, sizeof d45) != 0) return -EIO;
    if (epd_cmd(0x4E) != 0 || epd_data(d4e, sizeof d4e) != 0) return -EIO;
    if (epd_cmd(0x4F) != 0 || epd_data(d4f, sizeof d4f) != 0) return -EIO;
    return 0;
}

/* Byte-exact §20.1 "Init" command sequence (driver output control / data entry mode / RAM X-Y
 * range / border / temperature sensor / display update control 1 / RAM counters), ending in one
 * BUSY wait. */
static int epd_send_init_seq(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    uint16_t hm1 = (uint16_t)(s_panel->native_h - 1u);
    uint8_t d01[3] = { (uint8_t)(hm1 & 0xFFu), (uint8_t)(hm1 >> 8), 0x00 };
    uint8_t d11[1] = { 0x03 };
    uint8_t d44[2] = { 0x00, (uint8_t)(s_panel->ram_w / 8u - 1u) };
    uint8_t d45[4] = { 0x00, 0x00, (uint8_t)(hm1 & 0xFFu), (uint8_t)(hm1 >> 8) };
    uint8_t d3c[1] = { s_panel->border };
    uint8_t d18[1] = { 0x80 };
    uint8_t d21[2] = { 0x00, 0x80 };
    uint8_t d4e[1] = { 0x00 };
    uint8_t d4f[2] = { 0x00, 0x00 };

    if (epd_cmd(0x01) != 0 || epd_data(d01, sizeof d01) != 0) return -EIO;
    if (epd_cmd(0x11) != 0 || epd_data(d11, sizeof d11) != 0) return -EIO;
    if (epd_cmd(0x44) != 0 || epd_data(d44, sizeof d44) != 0) return -EIO;
    if (epd_cmd(0x45) != 0 || epd_data(d45, sizeof d45) != 0) return -EIO;
    if (epd_cmd(0x3C) != 0 || epd_data(d3c, sizeof d3c) != 0) return -EIO;
    if (epd_cmd(0x18) != 0 || epd_data(d18, sizeof d18) != 0) return -EIO;
    if (epd_cmd(0x21) != 0 || epd_data(d21, sizeof d21) != 0) return -EIO;
    if (epd_cmd(0x4E) != 0 || epd_data(d4e, sizeof d4e) != 0) return -EIO;
    if (epd_cmd(0x4F) != 0 || epd_data(d4f, sizeof d4f) != 0) return -EIO;
    return epd_wait_busy() == 0 ? 0 : -ETIMEDOUT;
}

/* §20.1 "Reset" + "Init": hw reset, wait, SW reset (0x12), wait, the init command sequence. Shared
 * by disp_init, disp_wake and disp_reinit -- none of them retry a failed wait (RULING R4): the
 * first BUSY timeout aborts the whole sequence. */
static int epd_reset_and_init_seq(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_dev != NULL, DISP_ASSERT_CODE, -ENODEV);

    epd_hw_reset();
    if (epd_wait_busy() != 0) {
        return -ETIMEDOUT;
    }
    if (epd_cmd(0x12) != 0) {
        return -EIO;
    }
    if (epd_wait_busy() != 0) {
        return -ETIMEDOUT;
    }
    return epd_send_init_seq();
}

/* Writes the current blitted framebuffer, rotated one portrait row at a time (bounded by the
 * panel-table field native_h), into RAM bank `cmd` (0x24 or 0x26). RULING R3: invert is always
 * false -- core/ui's fb 0=black already matches the SSD1680 RAM's 0=black, so no bit-flip. */
static int epd_write_full_frame(uint8_t cmd)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_fb != NULL, DISP_ASSERT_CODE, -ENODEV);

    uint16_t fb_w = (uint16_t)((s_panel->logical_w + 7u) & ~7u);
    uint16_t fb_h = s_panel->logical_h;

    if (epd_cmd(cmd) != 0) {
        return -EIO;
    }
    for (uint16_t pr = 0; pr < s_panel->native_h; pr++) {
        size_t n = epd_rotate_line(s_fb, fb_w, fb_h, pr, false, s_line, sizeof s_line);
        if (n == 0 || epd_data(s_line, n) != 0) {
            return -EIO;
        }
    }
    return 0;
}

/* §20.1 "Full refresh": full window, the frame into both 0x24 and 0x26 (so the next partial's
 * diff baseline is correct), lut_full, activate, wait (~2 s). */
static int epd_full_refresh(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_fb != NULL, DISP_ASSERT_CODE, -ENODEV);

    if (epd_set_full_window() != 0) {
        return -EIO;
    }
    if (epd_write_full_frame(0x24) != 0 || epd_write_full_frame(0x26) != 0) {
        return -EIO;
    }

    uint8_t lut[1] = { s_panel->lut_full };
    if (epd_cmd(0x22) != 0 || epd_data(lut, sizeof lut) != 0) {
        return -EIO;
    }
    if (epd_cmd(0x20) != 0) {
        return -EIO;
    }
    return epd_wait_busy() == 0 ? 0 : -ETIMEDOUT;
}

/* Sets the RAM window/cursor to the pending partial rect (s_win, panel-space byte/row range from
 * epd_window_from_rect). */
static int epd_set_partial_window(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_win_set, DISP_ASSERT_CODE, -EINVAL);

    uint16_t r1m1   = (uint16_t)(s_win.r1 - 1u);
    uint8_t  d44[2] = { (uint8_t)s_win.xb0, (uint8_t)(s_win.xb1 - 1u) };
    uint8_t  d45[4] = { (uint8_t)(s_win.r0 & 0xFFu), (uint8_t)(s_win.r0 >> 8), (uint8_t)(r1m1 & 0xFFu),
                         (uint8_t)(r1m1 >> 8) };
    uint8_t  d4e[1] = { (uint8_t)s_win.xb0 };
    uint8_t  d4f[2] = { (uint8_t)(s_win.r0 & 0xFFu), (uint8_t)(s_win.r0 >> 8) };

    if (epd_cmd(0x44) != 0 || epd_data(d44, sizeof d44) != 0) return -EIO;
    if (epd_cmd(0x45) != 0 || epd_data(d45, sizeof d45) != 0) return -EIO;
    if (epd_cmd(0x4E) != 0 || epd_data(d4e, sizeof d4e) != 0) return -EIO;
    if (epd_cmd(0x4F) != 0 || epd_data(d4f, sizeof d4f) != 0) return -EIO;
    return 0;
}

/* Writes only the pending window's rows/byte-range (s_win) into RAM bank `cmd`; bounded by
 * s_win.r1 (checked <= native_h below, itself derived from epd_window_from_rect's panel-width
 * clamp). */
static int epd_write_partial_frame(uint8_t cmd)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_win_set, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(s_win.r1 <= s_panel->native_h, DISP_ASSERT_CODE, -EINVAL);

    uint16_t fb_w = (uint16_t)((s_panel->logical_w + 7u) & ~7u);
    uint16_t fb_h = s_panel->logical_h;
    size_t   off  = s_win.xb0;
    size_t   cnt  = (size_t)(s_win.xb1 - s_win.xb0);

    if (epd_cmd(cmd) != 0) {
        return -EIO;
    }
    for (uint16_t pr = s_win.r0; pr < s_win.r1; pr++) {
        size_t n = epd_rotate_line(s_fb, fb_w, fb_h, pr, false, s_line, sizeof s_line);
        if (n == 0 || epd_data(&s_line[off], cnt) != 0) {
            return -EIO;
        }
    }
    return 0;
}

/* §20.1 "Partial refresh": border hold, window, the rect into 0x24, lut_partial, activate, wait
 * (~0.3-0.5 s), the same rect into 0x26 (diff baseline for the next partial), border restore. */
static int epd_partial_refresh(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_win_set, DISP_ASSERT_CODE, -EINVAL);

    uint8_t border_hold[1]    = { 0x80 };
    uint8_t border_restore[1] = { s_panel->border };
    uint8_t lut[1]            = { s_panel->lut_partial };

    if (epd_cmd(0x3C) != 0 || epd_data(border_hold, sizeof border_hold) != 0) {
        return -EIO;
    }
    if (epd_set_partial_window() != 0 || epd_write_partial_frame(0x24) != 0) {
        return -EIO;
    }
    if (epd_cmd(0x22) != 0 || epd_data(lut, sizeof lut) != 0) {
        return -EIO;
    }
    if (epd_cmd(0x20) != 0) {
        return -EIO;
    }
    if (epd_wait_busy() != 0) {
        return -ETIMEDOUT;
    }
    if (epd_write_partial_frame(0x26) != 0) {
        return -EIO;
    }
    if (epd_cmd(0x3C) != 0 || epd_data(border_restore, sizeof border_restore) != 0) {
        return -EIO;
    }

    s_win_set = false;
    return 0;
}

int disp_init(const disp_caps_t **caps)
{
    s_panel = epd_panel();   /* pure, constant -- bind so every entry point can assert on it */
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -EIO);
    CORE_ASSERT_RET(caps != NULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(!s_inited, DISP_ASSERT_CODE, -EALREADY);   /* disp_reinit() is the re-init path */

    gpio_config_t dc_rst = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dc_rst);
    gpio_set_level(PIN_RST, 1);

    gpio_config_t busy_in = {
        .pin_bit_mask = (1ULL << PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_in);

    if (!s_added) {
        spi_device_interface_config_t devcfg = {
            .mode = 0,
            .clock_speed_hz = 10 * 1000 * 1000,
            .spics_io_num = PIN_CS,
            .queue_size = 4,
            .pre_cb = pre_cb,
            .flags = 0,
        };
        if (spi_bus_add_device(SPI3_HOST, &devcfg, &s_dev) != ESP_OK) {
            return -EIO;
        }
        s_added = true;   /* the one heap allocation in this driver: IDF's own device handle */
    }

    int rc = epd_reset_and_init_seq();
    if (rc != 0) {
        return rc;
    }
    s_inited = true;

    if (s_fb != NULL) {   /* a framebuffer was already blitted -- show it now (boot screen) */
        rc = epd_full_refresh();
        if (rc != 0) {
            return rc;
        }
    }

    s_caps.width              = s_panel->logical_w;
    s_caps.height             = s_panel->logical_h;
    s_caps.partial_ok         = 1;
    s_caps.temp_min_c         = 0;
    s_caps.temp_max_c         = 45;
    s_caps.full_refresh_ms    = 2000;
    s_caps.partial_refresh_ms = 400;
    *caps = &s_caps;

    return 0;
}

int disp_blit(const uint8_t *fb)
{
    s_panel = epd_panel();   /* pure, constant -- legal (and bound) before disp_init ever runs */
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -EIO);
    CORE_ASSERT_RET(fb != NULL, DISP_ASSERT_CODE, -EINVAL);

    s_fb = fb;
    return 0;
}

int disp_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(w > 0, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(h > 0, DISP_ASSERT_CODE, -EINVAL);

    uint16_t     fb_w = (uint16_t)((s_panel->logical_w + 7u) & ~7u);
    uint16_t     fb_h = s_panel->logical_h;
    epd_window_t win;

    if (!epd_window_from_rect(x, y, w, h, fb_w, fb_h, &win)) {
        return -EINVAL;
    }
    s_win     = win;
    s_win_set = true;
    return 0;
}

int disp_refresh(uint8_t mode)
{
    CORE_ASSERT_RET(mode == DISP_PARTIAL || mode == DISP_FULL, DISP_ASSERT_CODE, -EINVAL);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_fb != NULL, DISP_ASSERT_CODE, -ENODEV);

    if (mode == DISP_PARTIAL && s_win_set) {   /* else treat as full (no window pending) */
        return epd_partial_refresh();
    }
    return epd_full_refresh();
}

int disp_sleep(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);

    uint8_t d[1] = { 0x01 };
    if (epd_cmd(0x10) != 0 || epd_data(d, sizeof d) != 0) {
        return -EIO;
    }
    return 0;
}

int disp_wake(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);

    return epd_reset_and_init_seq();
}

int disp_reinit(void)
{
    CORE_ASSERT_RET(s_panel != NULL, DISP_ASSERT_CODE, -ENODEV);
    CORE_ASSERT_RET(s_inited, DISP_ASSERT_CODE, -ENODEV);

    return epd_reset_and_init_seq();
}
