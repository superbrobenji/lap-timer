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

#include <errno.h>
#include <stdlib.h>

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

static const char *TAG = "board";

/* ---- §3.3 pin map ---- */
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
#define BATT_SAMPLES      64              /* §16.4: 64 samples ... */
#define BATT_TRIM         8               /* ... discard 8 highest and 8 lowest */
#define BATT_DIVIDER      2               /* 470k/470k tap -> x2 (§3.4) */
#define BTN_DEBOUNCE_US   25000           /* 25 ms contact-bounce guard */

static i2c_master_bus_handle_t   s_i2c_bus;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_adc_cali;      /* NULL if eFuse calibration unavailable */
static bool                      s_inited;

/* button ISR -> caller cb (§4.4 button ISR -> ui). Debounced in the ISR; cb runs in ISR
 * context and MUST be ISR-safe / IRAM_ATTR (§17.9: ISRs only push to queues via *FromISR). */
static void (*s_btn_cb)(uint8_t mask, int64_t mono_us);
static volatile int64_t s_btn_last_us;

#if CFG_HAS_PPS
static void (*s_pps_cb)(int64_t mono_us);
#endif

static uint8_t read_button_mask(void)
{
    uint8_t m = 0;
    if (gpio_get_level(PIN_BTN_MODE)) m |= 0x1;
    if (gpio_get_level(PIN_BTN_UP))   m |= 0x2;
    if (gpio_get_level(PIN_BTN_DOWN)) m |= 0x4;
    return m;
}

static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    if (now - s_btn_last_us < BTN_DEBOUNCE_US) return;   /* debounce */
    s_btn_last_us = now;
    if (s_btn_cb) s_btn_cb(read_button_mask(), now);
}

#if CFG_HAS_PPS
static void IRAM_ATTR pps_isr(void *arg)
{
    (void)arg;
    if (s_pps_cb) s_pps_cb(esp_timer_get_time());
}
#endif

int board_init(void)
{
    if (s_inited) return 0;

    /* --- release any RTC GPIO hold left latched by board_prepare_deep_sleep from a prior
     *     sleep cycle: classic ESP32 RTC holds survive the deep-sleep reset, so without this
     *     the level set below and later board_gps_power() calls would be latched out --- */
    rtc_gpio_hold_dis(PIN_GPS_PWR);

    /* --- GPS power MOSFET gate on RTC GPIO 26: output, start OFF (driven high) --- */
    ESP_ERROR_CHECK(rtc_gpio_init(PIN_GPS_PWR));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(PIN_GPS_PWR, RTC_GPIO_MODE_OUTPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_set_level(PIN_GPS_PWR, 1));   /* high = GPS off until board_gps_power(true) */

    /* --- inputs: IMU INT (27) + buttons (32/33/25) with pull-downs --- */
    gpio_config_t in_pd = {
        .pin_bit_mask = (1ULL << PIN_IMU_INT) | (1ULL << PIN_BTN_MODE) |
                        (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* backs the external 100k; keeps 27 defined w/o IMU */
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_pd));

    /* --- charger sense (39): input-only pin, no internal pulls (external 100k pull-up) --- */
    gpio_config_t in_float = {
        .pin_bit_mask = (1ULL << PIN_CHRG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_float));

    /* --- shared-VSPI chip selects idle-high before any device attaches (5 and 15 are
     *     strapping pins whose idle-high state is boot-safe, §3.3) --- */
    gpio_config_t cs_out = {
        .pin_bit_mask = (1ULL << PIN_EPD_CS) | (1ULL << PIN_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cs_out));
    gpio_set_level(PIN_EPD_CS, 1);
    gpio_set_level(PIN_SD_CS, 1);
    /* e-paper DC(14)/RST(4)/BUSY(35) are owned by display_epaper (3.4); left alone here. */

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
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus));

    /* --- VSPI (SPI3) bus: SCK 18, MOSI 23, MISO 19. Devices (e-paper/SD) attach in 3.3/3.4. --- */
    spi_bus_config_t spi_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    /* --- battery ADC: ADC1_CH6 (GPIO34), 12 dB atten, 12-bit, eFuse line-fit calibration.
     *     Spec §3.3 says "11 dB"; ADC_ATTEN_DB_11 is deprecated in v5.3.2 and aliased to
     *     ADC_ATTEN_DB_12 (identical ~150-2450 mV range), so DB_12 is used. --- */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_BATT_CHANNEL, &chan_cfg));
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

    s_inited = true;
    ESP_LOGI(TAG, "board_devkit_v1 init: I2C0(21/22) VSPI(18/23/19) ADC1_CH6(34) buttons(32/33/25)");
    return 0;
}

int board_gps_power(bool on)
{
    /* §3.4: P-MOSFET gate on GPIO26. Drive LOW = on, HIGH = off. */
    return rtc_gpio_set_level(PIN_GPS_PWR, on ? 0 : 1) == ESP_OK ? 0 : -EIO;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int board_battery_read_mv(uint16_t *mv)
{
    if (!mv) return -EINVAL;
    if (!s_adc) return -EIO;

    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_BATT_CHANNEL, &raw) != ESP_OK) return -EIO;
        s[i] = raw;
    }
    /* §16.4: discard the 8 highest and 8 lowest, mean the middle 48 */
    qsort(s, BATT_SAMPLES, sizeof(s[0]), cmp_int);
    int64_t sum = 0;
    for (int i = BATT_TRIM; i < BATT_SAMPLES - BATT_TRIM; i++) sum += s[i];
    int mean_raw = (int)(sum / (BATT_SAMPLES - 2 * BATT_TRIM));

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
    *mv = (uint16_t)batt;
    return 0;
}

int board_charger_present(bool *out)
{
    if (!out) return -EINVAL;
    /* CHRG is open-drain active-low (§3.3): present/charging => pin low. */
    *out = (gpio_get_level(PIN_CHRG) == 0);
    return 0;
}

int board_buttons_read(uint8_t *mask)
{
    if (!mask) return -EINVAL;
    *mask = read_button_mask();
    return 0;
}

int board_buttons_enable_isr(void (*cb)(uint8_t mask, int64_t mono_us))
{
    s_btn_cb = cb;
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;   /* INVALID_STATE = already installed */
    const gpio_num_t pins[] = { PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        ESP_ERROR_CHECK(gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], btn_isr, NULL));
    }
    return 0;
}

int board_prepare_deep_sleep(void)
{
    /* Called from the power task in 3.6+; implemented now per §16.3 step 5 / §3.3 wake rule.
     * Classic ESP32 EXT1 is one polarity across the whole mask, so buttons + IMU INT are all
     * active-high and wake ANY_HIGH; the charger uses EXT0 (single pin, level 0). */
    board_gps_power(false);
    rtc_gpio_hold_en(PIN_GPS_PWR);                 /* keep GPS off through sleep */

    const gpio_num_t ext1_pins[] = { PIN_IMU_INT, PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN };
    uint64_t ext1_mask = 0;
    for (size_t i = 0; i < sizeof(ext1_pins) / sizeof(ext1_pins[0]); i++) {
        rtc_gpio_pulldown_en(ext1_pins[i]);        /* RTC pull-downs in addition to externals (§3.3) */
        rtc_gpio_pullup_dis(ext1_pins[i]);
        ext1_mask |= (1ULL << ext1_pins[i]);
    }
    esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext0_wakeup(PIN_CHRG, 0);     /* charger low; harmless if CHRG unwired */
    return 0;
}

int board_pps_enable(void (*cb)(int64_t mono_us))
{
#if CFG_HAS_PPS
    s_pps_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_PPS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,            /* TIMEPULSE rising edge */
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -EIO;
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_PPS, pps_isr, NULL));
    return 0;
#else
    (void)cb;
    return 0;   /* no usable PPS on this build (NEO-6M v2 / sim) */
#endif
}

const char *board_name(void)
{
    return "devkit_v1";
}
