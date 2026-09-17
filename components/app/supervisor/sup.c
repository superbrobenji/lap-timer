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
#include "app/lt_consts.h"

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
static bool         s_safe_mode_cleared;   /* guards the §17.5 uptime auto-clear to fire once */

int sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s)
{
    if (hb_id >= HB_COUNT) return -1;
    s_watch[hb_id] = (watch_t){ .task = task, .hb_id = hb_id, .stall_s = stall_s,
                                .last_hb = g_hb[hb_id], .stalled_s = 0, .used = true };
    return 0;
}

TaskHandle_t sup_task_handle(uint8_t hb_id)
{
    /* The handle a task registered for its heartbeat slot (NULL if none). Lets `dbg mem` sample
     * the pipeline/logger/supervisor stacks without each exposing its own accessor (§22.4). */
    if (hb_id >= HB_COUNT || !s_watch[hb_id].used) return NULL;
    return s_watch[hb_id].task;
}

static void check_stalls(void)
{
    for (int i = 0; i < HB_COUNT; i++) {
        watch_t *w = &s_watch[i];
        if (!w->used || w->stall_s == 0) continue;
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
                /* §17.2: a stalled pipeline restarts (RTC snapshot save lands in 3.5). F3: the HW
                 * reset reason will be ESP_RST_SW (§17.5 normal), so leave a marker the next boot
                 * folds in as abnormal -- three consecutive stall-restarts within the window then
                 * trip the crash-loop -> SYS_SAFE_MODE instead of rebooting forever. */
                lt_counters_flush(true);
                lt_stall_flag_set();
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
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        lt_rtc_uptime_update_s(uptime_s);          /* crash-loop tracker */
        lt_counters_flush(false);                 /* persist if dirty and >=60 s (§15.2) */

        /* §17.5: once safe mode has been up for SAFE_MODE_CLEAR_S, clear the persisted gate and
         * the runtime flag so the next -- and this -- boot run normally. Fires once per boot. */
        if (!s_safe_mode_cleared && (sys_flags_get() & (1u << SYS_SAFE_MODE)) &&
            uptime_s >= SAFE_MODE_CLEAR_S) {
            lt_safe_clear();
            sys_flags_clear(SYS_SAFE_MODE);
            errlog_add(E_SYS_SAFE_MODE, 0);
            s_safe_mode_cleared = true;
        }

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
