/* board.c -- ESP32 DevKit V1 board HAL (spec §3.3 pin map, §3.4 wiring, §5.1 board.h).
 *
 * Owns the board-level peripherals the app tasks share: GPIO directions, the GPS power
 * MOSFET (RTC GPIO), the I2C bus (IMU/mag), the VSPI bus (e-paper/SD), the battery ADC,
 * the buttons, and the deep-sleep wake config. Sensor chips are driven by their own HAL
 * drivers (gps_*, imu_*, display_*) which attach to the buses this file brings up.
 *
 * IDF drivers used: driver/gpio, driver/rtc_io, driver/i2c_master (new v5.3 API),
 * driver/spi_master, esp_adc (adc_oneshot + adc_cali line-fitting), esp_sleep. No legacy
 * (deprecated) driver is used.
 *
 * Forbidden pins (§3.3): GPIO 0, 2, 12 (strapping) and 6-11 (SPI flash) are never touched;
 * GPIO 12 in particular is never pulled up (it selects 1.8 V flash and bricks boot).
 */
#include "hal/board.h"
#include "build_config.h"
#include "batt_pure.h"

#include "core/core.h"

#include <errno.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define BOARD_ASSERT_CODE 0x0C10   /* Power of 10 rule 5 (core/core.h); board.c's own code */

static const char *TAG = "board";

/* ---- §3.3 pin map ---- */
/* GPIO 4 (peer-connector DETECT, active-low with an internal pull-up) is NOT owned here: it is
 * configured and polled by components/app/link/link.c, which receives it as LINK_DETECT_GPIO from
 * components/app/CMakeLists.txt. Recorded in this block so the board's pin inventory is complete
 * (drv:L22). The §3.3 table gains the same row -- see this plan's spec amendments. Moving the
 * line behind hal/board.h would mean a new HAL entry point for a single app-owned GPIO; it is
 * documented instead, deliberately. */
#define PIN_GPS_PWR   GPIO_NUM_26   /* RTC GPIO; drive LOW = GPS on, held HIGH = off in sleep */
#define PIN_I2C_SDA   GPIO_NUM_21
#define PIN_I2C_SCL   GPIO_NUM_22
#define PIN_IMU_INT   GPIO_NUM_27   /* active-high; EXT1 wake */
#define PIN_SPI_SCK   GPIO_NUM_18   /* VSPI, shared e-paper / SD */
#define PIN_SPI_MOSI  GPIO_NUM_23
#define PIN_SPI_MISO  GPIO_NUM_19
#define PIN_EPD_CS    GPIO_NUM_5    /* idle high (strapping-safe) */
#define PIN_SD_CS     GPIO_NUM_15   /* idle high (strapping-safe) */
#define PIN_BTN_MODE  GPIO_NUM_32   /* active-high, pull-down; EXT1 wake */
#define PIN_BTN_UP    GPIO_NUM_33
#define PIN_BTN_DOWN  GPIO_NUM_25
#define PIN_CHRG      GPIO_NUM_39   /* open-drain active-low; input-only; EXT0 wake */
#define PIN_PPS       GPIO_NUM_36   /* input-only; used only on the M10 (CFG_HAS_PPS) */

#define ADC_BATT_CHANNEL  ADC_CHANNEL_6   /* GPIO34 = ADC1_CH6 (§3.3) */
#define BATT_DIVIDER      2               /* 470k/470k tap -> x2 (§3.4) */
#define BTN_DEBOUNCE_US   (BTN_DEBOUNCE_MS * 1000)   /* §20.8 / Appendix A, via hal/board.h */

static i2c_master_bus_handle_t   s_i2c_bus;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_adc_cali;      /* NULL if eFuse calibration unavailable */
static bool                      s_inited;

/* button ISR -> ui queue (§4.4 button ISR -> ui). Debounced in the ISR, which posts a btn_raw_t
 * straight onto s_btn_q (rule 9: no function-pointer callback) -- IRAM_ATTR, *FromISR only
 * (§17.9). s_btn_q is set once by board_buttons_enable_isr(); NULL until then. */
static QueueHandle_t s_btn_q;

/* drv:L17: one debounce stamp PER PIN. A single shared stamp dropped any edge within the guard of
 * an edge on a DIFFERENT button -- so MODE pressed just after UP was lost, and the UP+DOWN combo
 * (§20.8) was unreliable by construction. Index order matches s_btn_pins/the mask bits below. */
static volatile int64_t s_btn_last_us[3];
static const gpio_num_t s_btn_pins[3] = { PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };

/* Bench injection only (board_buttons_override, hal/board.h): bits forced on regardless of the
 * real GPIO level, OR-ed into read_button_mask()'s result below. Written from the console task,
 * read from the ui task's poll loop -- volatile, no lock needed for an 8-bit OR/AND-mask flag. */
static volatile uint8_t s_btn_override;

/* drv:C1 + §17.9 ("ISRs ... are IRAM_ATTR"): btn_isr is IRAM_ATTR and calls this, so this must be
 * too -- the committed moto_sim ELF had it at 0x400d999c, i.e. in the flash XIP window, not
 * inlined. s_btn_override and the three PIN_* constants it reads are already DRAM/immediate, and
 * gpio_get_level is IRAM via CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y (§17.8), so this is the only piece
 * that had to move. */
static uint8_t IRAM_ATTR read_button_mask(void)
{
    uint8_t m = 0;
    if (gpio_get_level(PIN_BTN_MODE)) m |= 0x1;
    if (gpio_get_level(PIN_BTN_UP))   m |= 0x2;
    if (gpio_get_level(PIN_BTN_DOWN)) m |= 0x4;
    return (uint8_t)(m | s_btn_override);
}

static void IRAM_ATTR btn_isr(void *arg)
{
    /* arg is the pin index this handler was registered with (gpio_isr_handler_add's void* -- not
     * a function pointer, so rule 9 is unaffected). Bounded by 3 by construction. */
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    if (idx >= 3u) return;
    int64_t now = esp_timer_get_time();
    if (now - s_btn_last_us[idx] < BTN_DEBOUNCE_US) return;   /* per-pin debounce (§20.8, 30 ms) */
    s_btn_last_us[idx] = now;
    if (s_btn_q != NULL) {
        btn_raw_t ev = { .mask = read_button_mask(), .mono_us = now };
        BaseType_t hpw = pdFALSE;
        (void)xQueueSendFromISR(s_btn_q, &ev, &hpw);   /* drop-on-full: a stale edge is harmless */
        portYIELD_FROM_ISR(hpw);
    }
}

/* drv:L18: a failed peripheral bring-up is a §17.6 self-test result, not a reason to panic --
 * ESP_ERROR_CHECK would abort() here and feed §17.5's crash-loop detector, turning a dud I2C
 * pull-up into a safe-mode device. Each failure is logged by board_chk() and OR-ed into rc (a function, not a macro: rule 8); board_init returns
 * -EIO so app_main can record it (and so the §17.6 rows that depend on a bus -- IMU, display --
 * fail honestly rather than mysteriously). */
static int board_chk(esp_err_t e, const char *what)
{
    if (e == ESP_OK) return 0;
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(e));
    return -EIO;
}

/* Battery ADC bring-up (ADC1_CH6/GPIO34): 12-bit, 12 dB atten, eFuse line-fit calibration.
 * Spec §3.3 says "11 dB"; ADC_ATTEN_DB_11 is deprecated in v5.3.2 and aliased to
 * ADC_ATTEN_DB_12 (identical ~150-2450 mV range), so DB_12 is used. Split verbatim out of
 * board_init (rule 4). s_adc_cali may legitimately end NULL (approximate mV) if eFuse Vref
 * is absent -- that is not an error. */
static int board_init_battery_adc(void)
{
    int rc = 0;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    rc |= board_chk(adc_oneshot_new_unit(&unit_cfg, &s_adc), "adc_oneshot_new_unit");
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    rc |= board_chk(adc_oneshot_config_channel(s_adc, ADC_BATT_CHANNEL, &chan_cfg), "adc_oneshot_config_channel");
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,   /* only used if eFuse Vref is absent; this board has eFuse Vref */
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali) != ESP_OK) {
        s_adc_cali = NULL;
        ESP_LOGW(TAG, "ADC eFuse calibration unavailable; battery mV will be approximate");
    }
    return rc;
}

static int board_init_buses(void)
{
    int rc = 0;
    /* --- I2C master bus (new v5.3 API): SDA 21 / SCL 22. The 400 kHz SCL is a per-device
     *     property in this API; the IMU driver (3.4) sets scl_speed_hz=400000 when it adds
     *     its device. Internal pull-ups enabled as a weak backup to the GY-521's 4.7k. --- */
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    rc |= board_chk(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus), "i2c_new_master_bus");

    /* --- VSPI (SPI3) bus: SCK 18, MOSI 23, MISO 19. Devices (e-paper/SD) attach in 3.3/3.4. --- */
    spi_bus_config_t spi_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    rc |= board_chk(spi_bus_initialize(SPI3_HOST, &spi_cfg, SPI_DMA_CH_AUTO), "spi_bus_initialize");
    return rc;

}

int board_init(void)
{
    if (s_inited) return 0;
    int rc = 0;

    /* --- release any RTC GPIO hold left latched by board_prepare_deep_sleep from a prior
     *     sleep cycle: classic ESP32 RTC holds survive the deep-sleep reset, so without this
     *     the level set below and later board_gps_power() calls would be latched out --- */
    rtc_gpio_hold_dis(PIN_GPS_PWR);

    /* --- GPS power MOSFET gate on RTC GPIO 26: output, start OFF (driven high) --- */
    rc |= board_chk(rtc_gpio_init(PIN_GPS_PWR), "rtc_gpio_init");
    rc |= board_chk(rtc_gpio_set_direction(PIN_GPS_PWR, RTC_GPIO_MODE_OUTPUT_ONLY), "rtc_gpio_set_direction");
    rc |= board_chk(rtc_gpio_set_level(PIN_GPS_PWR, 1), "rtc_gpio_set_level"); /* high = GPS off until board_gps_power(true) */

    /* --- inputs: IMU INT (27) + buttons (32/33/25) with pull-downs --- */
    gpio_config_t in_pd = {
        .pin_bit_mask = (1ULL << PIN_IMU_INT) | (1ULL << PIN_BTN_MODE) |
                        (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* backs the external 100k; keeps 27 defined w/o IMU */
        .intr_type = GPIO_INTR_DISABLE,
    };
    rc |= board_chk(gpio_config(&in_pd), "gpio_config(inputs)");

    /* --- charger sense (39): input-only pin, no internal pulls (external 100k pull-up) --- */
    gpio_config_t in_float = {
        .pin_bit_mask = (1ULL << PIN_CHRG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    rc |= board_chk(gpio_config(&in_float), "gpio_config(chrg)");

    /* --- shared-VSPI chip selects idle-high before any device attaches (5 and 15 are
     *     strapping pins whose idle-high state is boot-safe, §3.3) --- */
    gpio_config_t cs_out = {
        .pin_bit_mask = (1ULL << PIN_EPD_CS) | (1ULL << PIN_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    rc |= board_chk(gpio_config(&cs_out), "gpio_config(cs)");
    gpio_set_level(PIN_EPD_CS, 1);
    gpio_set_level(PIN_SD_CS, 1);
    /* e-paper DC(14)/RST(13)/BUSY(35) are owned by display_epaper (3.4); left alone here. */

    if (board_init_buses() != 0) rc = -EIO;   /* each failure already logged by board_chk */

    /* --- battery ADC (split out to board_init_battery_adc for rule 4): ADC1_CH6, 12-bit --- */
    if (board_init_battery_adc() != 0) rc = -EIO;
    if (rc == 0) CORE_ASSERT_RET(s_i2c_bus != NULL, BOARD_ASSERT_CODE, -EIO);   /* ESP_OK must imply a usable bus handle */
    if (rc == 0) CORE_ASSERT_RET(s_adc != NULL, BOARD_ASSERT_CODE, -EIO);   /* ADC bring-up must yield a usable unit handle */

    s_inited = (rc == 0);
    ESP_LOGI(TAG, "board_devkit_v1 init rc %d: I2C0(21/22) VSPI(18/23/19) ADC1_CH6(34) buttons(32/33/25)", rc);
    return rc;
}

int board_gps_power(bool on)
{
    /* §3.4: P-MOSFET gate on GPIO26. Drive LOW = on, HIGH = off. */
    return rtc_gpio_set_level(PIN_GPS_PWR, on ? 0 : 1) == ESP_OK ? 0 : -EIO;
}

int board_battery_read_mv(uint16_t *mv)
{
    CORE_ASSERT_RET(mv != NULL, BOARD_ASSERT_CODE, -EINVAL);
    if (!s_adc) return -EIO;

    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_BATT_CHANNEL, &raw) != ESP_OK) return -EIO;
        s[i] = raw;
    }
    /* §16.4 via batt_pure.h: bounded insertion sort + mean of the middle 48 (R19 -- this was
     * qsort(cmp_int), an unregistered rule-9 function pointer into a recursive newlib sort). */
    int mean_raw = batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM);
    if (mean_raw < 0) return -EIO;

    int v_tap_mv;
    if (s_adc_cali) {
        if (adc_cali_raw_to_voltage(s_adc_cali, mean_raw, &v_tap_mv) != ESP_OK) return -EIO;
    } else {
        /* uncalibrated fallback: 12-bit over the ~2450 mV DB_12 full scale */
        v_tap_mv = mean_raw * 2450 / 4095;
    }
    /* board returns the tap voltage x2 (§3.4 divider); the two-point battery.cal correction
     * (§15.1) is applied by the power task, which owns cfg. */
    int batt = v_tap_mv * BATT_DIVIDER;
    if (batt < 0) batt = 0;
    if (batt > 65535) batt = 65535;
    CORE_ASSERT_RET(batt >= 0 && batt <= 65535, BOARD_ASSERT_CODE, -EIO);   /* fits the u16 out */
    *mv = (uint16_t)batt;
    return 0;
}

int board_charger_present(bool *out)
{
    if (!out) return -EINVAL;
#if CFG_CHRG_WIRED
    /* CHRG is open-drain active-low (§3.3): present/charging => pin low. */
    *out = (gpio_get_level(PIN_CHRG) == 0);
#else
    /* drv:M11 / #54: no external pull-up and no battery on this board -- GPIO 39 floats, and a
     * floating input-only pin must never be allowed to answer a power question. hal/board.h's
     * contract ("false if not wired") is honoured literally. */
    *out = false;
#endif
    return 0;
}

int board_buttons_read(uint8_t *mask)
{
    if (!mask) return -EINVAL;
    *mask = read_button_mask();
    return 0;
}

void board_buttons_override(uint8_t mask, bool on)
{
    if (on) s_btn_override = (uint8_t)(s_btn_override | mask);
    else    s_btn_override = (uint8_t)(s_btn_override & (uint8_t)~mask);
}

int board_buttons_enable_isr(QueueHandle_t evt_q)
{
    s_btn_q = evt_q;
    /* drv:C1: ESP_INTR_FLAG_IRAM is the OTHER half of the §17.9 claim. Without it IDF masks the
     * GPIO interrupt for the duration of every cache-off flash write and replays the edge after,
     * so a press during the logger's 4 KB batch arrives late (up to ~40 ms) instead of on time;
     * with it -- and with every function the handler reaches now in IRAM (see read_button_mask)
     * -- the handler runs during the write, as §17.8 requires of a button path shared with a
     * 1 Hz flash writer. */
    esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;   /* INVALID_STATE = already installed */
    for (uint32_t i = 0; i < 3u; i++) {
        ESP_ERROR_CHECK(gpio_set_intr_type(s_btn_pins[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_btn_pins[i], btn_isr, (void *)(uintptr_t)i));
    }
    return 0;
}

int board_prepare_deep_sleep(void)
{
    /* Called from the power task in 3.6+; implemented now per §16.3 step 5 / §3.3 wake rule.
     * Classic ESP32 EXT1 is one polarity across the whole mask, so buttons + IMU INT are all
     * active-high and wake ANY_HIGH; the charger uses EXT0 (single pin, level 0). */
    int rc = 0;
    if (board_gps_power(false) != 0) { ESP_LOGW(TAG, "gps power-off failed before sleep"); rc = -EIO; }
    rc |= board_chk(rtc_gpio_hold_en(PIN_GPS_PWR), "rtc_gpio_hold_en");   /* keep GPS off through sleep */

    const gpio_num_t ext1_pins[] = { PIN_IMU_INT, PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
    uint64_t ext1_mask = 0;
    for (size_t i = 0; i < sizeof(ext1_pins) / sizeof(ext1_pins[0]); i++) {
        rc |= board_chk(rtc_gpio_pulldown_en(ext1_pins[i]), "rtc_gpio_pulldown_en");   /* RTC pull-downs in addition to externals (§3.3) */
        rc |= board_chk(rtc_gpio_pullup_dis(ext1_pins[i]), "rtc_gpio_pullup_dis");
        ext1_mask |= (1ULL << ext1_pins[i]);
    }
    rc |= board_chk(esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH), "esp_sleep_enable_ext1_wakeup");
#if CFG_CHRG_WIRED
    /* §3.3: the charger is the single EXT0 wake pin (level 0). Only armed when CHRG is really
     * wired: on the prototype GPIO 39 floats, and a float reading low is an IMMEDIATE EXT0 wake
     * -- it would defeat PARK/SHUTDOWN entirely, the opposite of the "harmless if CHRG unwired"
     * this line used to claim (drv:M11, false-claim 2). */
    if (esp_sleep_enable_ext0_wakeup(PIN_CHRG, 0) != ESP_OK) ESP_LOGW(TAG, "ext0 (CHRG) wake not armed");
#endif
    return rc;
}

/* board_pps_enable() removed (session 4.5.5 rule-9 cleanup): s_pps_cb/pps_isr/board_pps_enable
 * had zero callers anywhere (dead code behind #if CFG_HAS_PPS). M10 PPS support will re-add a
 * queue-based enable (no function pointer), mirroring board_buttons_enable_isr above, in its own
 * future plan. PIN_PPS (§3.3 pin map) is left defined for that future wiring. */

const char *board_name(void)
{
    return "devkit_v1";
}
