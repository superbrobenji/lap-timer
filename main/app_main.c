/* app_main.c -- LapTimer firmware entry point.
 *
 * Session 3.1 is the first bootable firmware: it prints the version banner and
 * idles. The full boot sequence (§4.7) -- NVS, crash-loop check, RTC resume,
 * config, board bring-up, storage, display, self-test, task WDT, queues/rings,
 * and the pipeline/logger/ui/power/conn tasks -- lands across sessions 3.2-3.5.
 */
#include "build_config.h"
#include "core/core.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "laptimer";

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-WDT";
    case ESP_RST_TASK_WDT:  return "task-WDT";
    case ESP_RST_WDT:       return "other-WDT";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
    }
}

void app_main(void)
{
    /* §4.7 step 1: record the reset reason (NVS counters land in 3.2). */
    esp_reset_reason_t reason = esp_reset_reason();

    /* §4.7 step 8: the version banner. */
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", reset_reason_str(reason), (int)reason);

    /* No drivers, tasks, queues or rings yet: idle and let the idle task feed
     * the (not-yet-armed) watchdog. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
