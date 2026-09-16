/* sup.c -- supervisor task (spec §4.3: core 0, prio 22, stack 3072, 1000 ms; loop §17.2).
 *
 * 3.2 runs the reduced §17.2 loop: heartbeat-stall watch over the registered tasks, task-WDT
 * reset each loop, the crash-loop uptime tick, and the batched counter flush. The GPS/IMU/
 * storage ladders and heap/stack/temperature/OTA checks are stubbed until their subsystems
 * land (3.3/3.4). Only the supervisor itself is live now; other tasks call sup_register_task
 * as they are created.
 */
#include "app/lt_sup.h"
#include "app/lt_nvs.h"
#include "app/lt_err.h"
#include "app/lt_rtc.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "sup";

#define SUP_PERIOD_MS   1000
#define SUP_PRIO        22
#define SUP_CORE        0
#define SUP_STACK_BYTES 3072
#define SUP_STACK_WORDS (SUP_STACK_BYTES / sizeof(StackType_t))

typedef struct {
    TaskHandle_t task;
    uint8_t      hb_id;
    uint32_t     stall_s;
    uint32_t     last_hb;
    uint32_t     stalled_s;
    bool         used;
} watch_t;

static watch_t      s_watch[HB_COUNT];
static StaticTask_t s_tcb;
static StackType_t  s_stack[SUP_STACK_WORDS];

int sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s)
{
    if (hb_id >= HB_COUNT) return -1;
    s_watch[hb_id] = (watch_t){ .task = task, .hb_id = hb_id, .stall_s = stall_s,
                                .last_hb = g_hb[hb_id], .stalled_s = 0, .used = true };
    return 0;
}

static void check_stalls(void)
{
    for (int i = 0; i < HB_COUNT; i++) {
        watch_t *w = &s_watch[i];
        if (!w->used) continue;
        uint32_t cur = g_hb[w->hb_id];
        if (cur != w->last_hb) {                 /* progressing */
            w->last_hb = cur;
            w->stalled_s = 0;
            continue;
        }
        w->stalled_s += SUP_PERIOD_MS / 1000;
        if (w->stalled_s >= w->stall_s) {
            ESP_LOGE(TAG, "task %d stalled %us", w->hb_id, (unsigned)w->stalled_s);
            errlog_add(E_SYS_TASK_STALL, w->hb_id);
            w->stalled_s = 0;
            if (w->hb_id == HB_PIPELINE) {
                /* §17.2: a stalled pipeline restarts (RTC snapshot save lands in 3.5). */
                lt_counters_flush(true);
                esp_restart();
            }
        }
    }
}

static void sup_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);                       /* §17.1: supervisor subscribes to the task WDT */
    sup_register_task(HB_SUPERVISOR, xTaskGetCurrentTaskHandle(), 0);   /* self: WDT covers a stuck sup */
    ESP_LOGI(TAG, "supervisor up (core %d prio %d)", SUP_CORE, SUP_PRIO);

    for (;;) {
        check_stalls();
        esp_task_wdt_reset();
        lt_rtc_uptime_update_s((uint32_t)(esp_timer_get_time() / 1000000));   /* crash-loop tracker */
        lt_counters_flush(false);                 /* persist if dirty and >=60 s (§15.2) */

        /* Ladders (GPS §7.5 / IMU §8.7 / storage §13) and heap/stack/temp/OTA checks: their
         * subsystems arrive in 3.3/3.4; wired here then. */

        g_hb[HB_SUPERVISOR]++;
        vTaskDelay(pdMS_TO_TICKS(SUP_PERIOD_MS));
    }
}

void sup_start(void)
{
    xTaskCreateStaticPinnedToCore(sup_task, "sup", SUP_STACK_WORDS, NULL, SUP_PRIO,
                                  s_stack, &s_tcb, SUP_CORE);
}
