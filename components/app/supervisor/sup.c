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
#include "app/lt_assert.h"
#include "app/ota.h"
#include "app/pipeline.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "sup";

#define SUP_ASSERT_CODE 0x0B30

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
static bool         s_ota_boot_checked;    /* §19.4: the boot-time OTA validate-vs-rollback decision ran once */
static bool         s_ota_awaiting;        /* §19.4: a pending image booted and is on trial, awaiting the health gate */

int sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s)
{
    /* hb_id is a task-registration index into s_watch[HB_COUNT]; task is the caller's own
     * xTaskGetCurrentTaskHandle(), never NULL from task context (§4.3 callers). Both anomalies
     * are programmer errors (a bad HB_* constant, or calling from an ISR), not runtime input. */
    LT_ASSERT_RET(hb_id < HB_COUNT, SUP_ASSERT_CODE, -1);
    LT_ASSERT_RET(task != NULL, SUP_ASSERT_CODE, -1);
    s_watch[hb_id] = (watch_t){ .task = task, .hb_id = hb_id, .stall_s = stall_s,
                                .last_hb = g_hb[hb_id], .stalled_s = 0, .used = true };
    return 0;
}

TaskHandle_t sup_task_handle(uint8_t hb_id)
{
    /* The handle a task registered for its heartbeat slot (NULL if none). Lets `dbg mem` sample
     * the pipeline/logger/supervisor stacks without each exposing its own accessor (§22.4). An
     * out-of-range hb_id is a caller bug (assert); an in-range but never-registered slot is a
     * normal state (plain NULL return, not an anomaly). */
    LT_ASSERT_RET(hb_id < HB_COUNT, SUP_ASSERT_CODE, NULL);
    if (!s_watch[hb_id].used) return NULL;
    /* Invariant from sup_register_task (the only writer of .task/.used): a used slot always has a
     * non-NULL task. The fallback on failure is the same value the plain return below would give
     * either way, so this is free to assert even though it can never fire under correct operation. */
    LT_ASSERT_RET(s_watch[hb_id].task != NULL, SUP_ASSERT_CODE, s_watch[hb_id].task);
    return s_watch[hb_id].task;
}

static void check_stalls(void)
{
    for (int i = 0; i < HB_COUNT; i++) {
        watch_t *w = &s_watch[i];
        if (!w->used || w->stall_s == 0) continue;
        /* w->hb_id is stored state (set once at registration, §sup_register_task already bounds
         * it there); re-checking it here before it indexes g_hb[] guards against future slot
         * corruption -- an internal index overflow would otherwise read past g_hb[HB_COUNT]. */
        LT_ASSERT_VOID(w->hb_id < HB_COUNT, SUP_ASSERT_CODE);
        uint32_t cur = g_hb[w->hb_id];
        if (cur != w->last_hb) {                 /* progressing */
            w->last_hb = cur;
            w->stalled_s = 0;
            continue;
        }
        w->stalled_s += SUP_PERIOD_MS / 1000;
        if (w->stalled_s >= w->stall_s) {
            ESP_LOGE(TAG, "task %d stalled %us", w->hb_id, (unsigned)w->stalled_s);
            (void)errlog_add(E_SYS_TASK_STALL, w->hb_id);
            w->stalled_s = 0;
            if (w->hb_id == HB_PIPELINE) {
                /* §17.2: a stalled pipeline restarts (RTC snapshot save lands in 3.5). F3: the HW
                 * reset reason will be ESP_RST_SW (§17.5 normal), so leave a marker the next boot
                 * folds in as abnormal -- three consecutive stall-restarts within the window then
                 * trip the crash-loop -> SYS_SAFE_MODE instead of rebooting forever. */
                (void)lt_counters_flush(true);
                lt_stall_flag_set();
                esp_restart();
            }
        }
    }
}

/* §19.4: the applied image asked (via ota_end) for a reboot after its OTA_END ack; the supervisor
 * owns every system restart, so it performs this one too, flushing counters first. */
static void ota_reboot_check(void)
{
    if (!ota_reboot_due()) return;
    (void)lt_counters_flush(true);
    esp_restart();
}

/* §19.4, once after boot: if an OTA was in flight (ota_pending set) decide which image is running --
 * the new one on trial (PENDING_VERIFY -> validate below once healthy) or the previously-valid image
 * the bootloader rolled back to (log E_OTA_ROLLBACK, count it, clear the flag). No OTA in flight -> nop. */
static void ota_boot_decide(void)
{
    if (!lt_ota_pending_get()) return;
    const esp_partition_t *run = esp_ota_get_running_partition();
    LT_ASSERT_VOID(run != NULL, SUP_ASSERT_CODE);
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_ota_awaiting = true;                 /* new image on trial; ota_try_validate() confirms it */
        return;
    }
    (void)errlog_add(E_OTA_ROLLBACK, 0);       /* reverted image sees ota_pending with itself running */
    lt_counters_inc(LT_CTR_OTA_ROLLBACK, true);
    lt_ota_pending_clear();
    ESP_LOGW(TAG, "OTA image rolled back by the bootloader");
}

/* §19.4: mark a pending image valid (cancel rollback) once self-test passed (not SAFE_MODE), a GPS
 * frame arrived, storage is mounted, and it has run for OTA_VALID_UPTIME_S. Until then a
 * panic/WDT/brownout lets the bootloader roll back (logged as E_OTA_ROLLBACK on the next boot). */
static void ota_try_validate(uint32_t uptime_s)
{
    if (!s_ota_awaiting) return;
    uint32_t f = sys_flags_get();
    bool healthy = !(f & (1u << SYS_SAFE_MODE)) && !(f & (1u << SYS_STORAGE_DEAD)) &&
                   pipeline_gps_seen() && uptime_s >= OTA_VALID_UPTIME_S;
    if (!healthy) return;
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return;
    lt_counters_inc(LT_CTR_OTA_OK, true);
    (void)errlog_add(E_OTA_VALIDATED, uptime_s);
    lt_ota_pending_clear();
    s_ota_awaiting = false;
    ESP_LOGI(TAG, "OTA image validated (uptime %us)", (unsigned)uptime_s);
}

/* §19.4 OTA lifecycle, driven once per supervisor loop: perform the applied image's reboot; on the
 * first loop decide validate-vs-rollback; then mark a pending image valid once it proves healthy.
 * Grouped here so the task entry (which must not assert-early-return) stays a thin dispatcher. */
static void ota_lifecycle(uint32_t uptime_s)
{
    ota_reboot_check();
    if (!s_ota_boot_checked) { s_ota_boot_checked = true; ota_boot_decide(); }
    ota_try_validate(uptime_s);
}

static void sup_task(void *arg)
{
    (void)arg;
    (void)esp_task_wdt_add(NULL);                 /* §17.1: supervisor subscribes to the task WDT */
    (void)sup_register_task(HB_SUPERVISOR, xTaskGetCurrentTaskHandle(), 0);   /* self: WDT covers a stuck sup */
    ESP_LOGI(TAG, "supervisor up (core %d prio %d)", SUP_CORE, SUP_PRIO);

    /* Never assert()-early-return from a FreeRTOS task entry: returning from this function would
     * return from the task itself, which is undefined behaviour. Every genuine anomaly this loop
     * could hit is instead asserted inside the (non-task-entry) helper it calls. */
    for (;;) {
        check_stalls();
        (void)esp_task_wdt_reset();
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        lt_rtc_uptime_update_s(uptime_s);          /* crash-loop tracker */
        (void)lt_counters_flush(false);           /* persist if dirty and >=60 s (§15.2) */

        ota_lifecycle(uptime_s);                  /* §19.4: reboot / boot-decide / validate (see helper) */

        /* §17.5: once safe mode has been up for SAFE_MODE_CLEAR_S, clear the persisted gate and
         * the runtime flag so the next -- and this -- boot run normally. Fires once per boot. */
        if (!s_safe_mode_cleared && (sys_flags_get() & (1u << SYS_SAFE_MODE)) &&
            uptime_s >= SAFE_MODE_CLEAR_S) {
            lt_safe_clear();
            sys_flags_clear(SYS_SAFE_MODE);
            (void)errlog_add(E_SYS_SAFE_MODE, 0);
            s_safe_mode_cleared = true;
        }

        /* Ladders (GPS §7.5 / IMU §8.7 / storage §13) and heap/stack/temp checks: their
         * subsystems arrive in 3.3/3.4; wired here then. */

        g_hb[HB_SUPERVISOR]++;
        vTaskDelay(pdMS_TO_TICKS(SUP_PERIOD_MS));
    }
}

void sup_start(void)
{
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(sup_task, "sup", SUP_STACK_WORDS, NULL, SUP_PRIO,
                                                   s_stack, &s_tcb, SUP_CORE);
    /* Postcondition: static task creation over our own fixed-size s_stack/s_tcb must succeed --
     * a NULL handle here would mean the supervisor (and its WDT coverage of every other task)
     * never started. */
    LT_ASSERT_VOID(h != NULL, SUP_ASSERT_CODE);
}
