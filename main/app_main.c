/* app_main.c -- LapTimer firmware entry point and boot sequence (§4.7).
 *
 * Session 3.2 Task 3 implements boot steps 1-5: reset reason + crash counters, NVS init, boot
 * counter + crash-loop check, RTC-state validate/clear, and config load. Board bring-up
 * (step 6), the task WDT + supervisor (steps 10-11) and the console land in Task 4, which
 * appends to this file; until then app_main ends in a temporary idle loop. Steps 7-9 (storage,
 * display, self-test) and 12-15 (pipeline/logger/ui/power/conn/OTA) land in 3.3-3.5.
 */
#include "build_config.h"

#include <stdio.h>

#include "core/cfg.h"
#include "core/core.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/lt_err.h"
#include "app/lt_nvs.h"
#include "app/lt_rtc.h"
#include "app/lt_sup.h"

static const char *TAG = "laptimer";

/* Task 3 build-gate shim: `sup_install_assert_hook` (declared in app/lt_sup.h) is really
 * implemented by Task 4's components/app/supervisor/sup_errlog.c, which is out of scope here.
 * Step 2 below already calls it (byte-for-byte from the plan block), so a standalone Task 3
 * link needs a definition; this one does the real §17.9 job. Task 4 replaces this whole file
 * wholesale (its app_main.c is a full rewrite, not a diff), so this shim is superseded, not
 * duplicated, once sup_errlog.c lands. Plan block drift -- see the Task 3 report. */
static void assert_hook_shim(uint16_t code, const char *file, int line)
{
    ESP_LOGE(TAG, "core assert 0x%04x at %s:%d", code, file ? file : "?", line);
    errlog_add(code, (uint32_t)line);
}

void sup_install_assert_hook(void)
{
    core_set_assert_hook(assert_hook_shim);
}

/* Build the profile the app applies after cfg_defaults and before the NVS blob (§15.1), so
 * stored user settings always win. BLE name is MAC-derived ("LapTimer-XXXX"). */
static void make_profile(cfg_profile_t *p, char *name, size_t name_cap)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, name_cap, "LapTimer-%02X%02X", mac[4], mac[5]);
    p->display_live_clock = (CFG_VARIANT_CAR != 0);   /* profile sets true for the OLED (car) */
    p->log_fused_hz = CFG_FUSED_LOG_HZ;
    p->ble_name = name;
}

void app_main(void)
{
    int64_t t_boot = esp_timer_get_time();

    /* §4.7 step 1: reset reason (crash counters recorded below, after NVS is up). */
    esp_reset_reason_t reason = esp_reset_reason();

    /* §4.7 step 8 banner (kept from 3.1). */
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", lt_reset_reason_str((int)reason), (int)reason);

    /* §4.7 step 2: NVS init, erase + re-init on a version/space fault (log E_SYS_CFG_RESET). */
    esp_err_t nerr = nvs_flash_init();
    bool nvs_erased = false;
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
        nvs_erased = true;
    } else {
        ESP_ERROR_CHECK(nerr);
    }
    if (lt_nvs_init() != 0) ESP_LOGE(TAG, "lt_nvs_init failed");
    sup_install_assert_hook();                 /* core asserts -> error ring (§17.9) */
    if (nvs_erased) errlog_add(E_SYS_CFG_RESET, 0);

    /* §4.7 step 1 (cont): count + log + crash-log the reset reason. */
    uint32_t prev_uptime_s = lt_rtc_uptime_prev_s();
    lt_boot_record_reset((int)reason, prev_uptime_s);

    /* §4.7 step 3: boot counter + crash-loop check (§17.5); 3.2 detects + flags only. */
    uint32_t boot_cnt = lt_nvs_boot_inc();
    lt_counters_inc(LT_CTR_BOOTS, false);
    bool safe = false;
    if (lt_crashlog_is_loop()) {
        lt_safe_until_set(boot_cnt + 1);
        safe = true;
    } else if (boot_cnt <= lt_safe_until_get()) {
        safe = true;
    }
    if (safe) { sys_flags_set(SYS_SAFE_MODE); errlog_add(E_SYS_SAFE_MODE, boot_cnt); }

    /* §4.7 step 4: RTC memory validate/clear (full resume is 3.5). */
    switch (lt_rtc_validate(NULL)) {
    case RTC_INVALID: errlog_add(E_SYS_RTC_INVALID, 0); lt_rtc_clear(); break;
    case RTC_ABSENT:  lt_rtc_clear(); break;
    case RTC_VALID:   break;
    }

    /* §4.7 step 5: config load (defaults -> profile -> NVS blob; stored settings win). */
    static cfg_t cfg;
    char ble_name[16];
    cfg_profile_t prof;
    cfg_defaults(&cfg);
    make_profile(&prof, ble_name, sizeof(ble_name));   /* MAC-derived BLE name */
    cfg_apply_profile(&cfg, &prof);
    int corr = lt_cfg_load(&cfg);
    if (corr < 0) { lt_cfg_save(&cfg); errlog_add(E_SYS_CFG_RESET, 0); }
    else if (corr > 0) { errlog_add(E_SYS_CFG_RESET, (uint32_t)corr); lt_cfg_save(&cfg); }

    /* Task 4 appends step 6 board bring-up, steps 10-11 (task WDT + supervisor, static
     * queues) and starts the console. For a standalone Task 3 build gate: idle. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
