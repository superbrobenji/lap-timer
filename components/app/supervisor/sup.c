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
#include "app/lt_ipc.h"
#include "app/ota.h"
#include "app/pipeline.h"

#include "core/ses.h"

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
static bool         s_ota_rollback_pending; /* fix round 1 (Critical #1): a revert was detected but
                                              * not yet posted -- g_ui_evt_q may not exist yet */

/* Boot self-test table (Plan 7c T8, design §6): one relaxed atomic byte per BOOT_* slot, zero-
 * initialised to BOOT_UNKNOWN. Plain statics (no init dependency) so any init site -- app_main
 * before sup_start(), the pipeline/ui tasks after -- can report as soon as it knows its result. */
static volatile uint8_t s_boot[BOOT_SLOTS];

void sup_boot_report(uint8_t slot, uint8_t status)
{
    /* Both bad-slot and bad-status are programmer errors (a caller passing a raw int instead of a
     * BOOT_* constant), not runtime input -- asserted, not clamped. */
    LT_ASSERT_VOID(slot < BOOT_SLOTS, SUP_ASSERT_CODE);
    LT_ASSERT_VOID(status <= BOOT_SIM, SUP_ASSERT_CODE);
    __atomic_store_n(&s_boot[slot], status, __ATOMIC_RELAXED);
}

uint8_t sup_boot_status(uint8_t slot)
{
    LT_ASSERT_RET(slot < BOOT_SLOTS, SUP_ASSERT_CODE, BOOT_UNKNOWN);
    uint8_t st = __atomic_load_n(&s_boot[slot], __ATOMIC_RELAXED);
    /* Invariant: sup_boot_report() above is the table's only writer and already range-checks
     * status before storing -- re-checking the read here guards against future slot corruption. */
    LT_ASSERT_RET(st <= BOOT_SIM, SUP_ASSERT_CODE, BOOT_UNKNOWN);
    return st;
}

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

/* Close the open session before a supervisor-owned restart (§17.2/§19.4, #59). Bounded: a
 * logger that cannot answer in time must not block the restart -- timeout_ms is the hard cap on
 * how long this call may hold up the caller (check_stalls: 500 ms, a stalled pipeline must not
 * delay recovery; ota_reboot_check: 2000 ms), and the restart proceeds regardless of rc. */
static void close_session_before_restart(uint8_t reason, uint32_t timeout_ms)
{
    LT_ASSERT_VOID(reason == SES_END_RESTART || reason == SES_END_STALL, SUP_ASSERT_CODE);
    LT_ASSERT_VOID(timeout_ms > 0 && timeout_ms <= 5000, SUP_ASSERT_CODE);
    log_request_t req = { .type = LOGGER_CLOSE_SESSION, .reason = reason, .gps_us = 0 };
    int rc = logger_request_sync(&req, timeout_ms);
    if (rc != 0) {
        ESP_LOGW(TAG, "session close before restart rc %d (reason %u)", rc, (unsigned)reason);
        (void)errlog_add(E_LOG_CLOSE_TIMEOUT, (uint32_t)reason);
    }
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
                close_session_before_restart(SES_END_STALL, 500);
                (void)lt_counters_flush(true);
                lt_stall_flag_set();
                esp_restart();
            }
        }
    }
}

/* §19.4: the applied image asked (via ota_end) for a reboot after its OTA_END ack; the supervisor
 * owns every system restart, so it performs this one too, flushing counters first.
 *
 * §17.5 amendment (controller ruling P-8, debt sweep A T6 fix 1): clear the safe/recovery gate
 * before rebooting into the NEW image, so it boots at level 0 and gets its own normal validation
 * trial (ota_try_validate's `!SAFE_MODE && pipeline_gps_seen()` gate needs the pipeline running,
 * which a resumed safe/recovery level would never start) -- without this, an OTA pushed while the
 * device is inside a safe-mode window would resume the same level forever, never validate, and
 * the bootloader would roll it back. The OLD image (this image, still running until esp_restart()
 * below) remains the rollback target regardless; if the NEW image itself crash-loops, boot_safe_mode
 * re-detects it from a clean crash_log/boot_cnt slate and re-arms safe mode from scratch -- this
 * does not weaken crash-loop detection, only gives every newly-applied image a fair, un-gated first
 * boot. The stall-restart path (check_stalls, same-image self-restart after a wedged pipeline) does
 * NOT clear the gate: a stall IS the crash-loop signal safe/recovery mode exists to catch, and the
 * running image is unchanged, so clearing there would erase the very evidence the next boot's
 * lt_crashlog_is_loop() needs. */
static void ota_reboot_check(void)
{
    if (!ota_reboot_due()) return;
    close_session_before_restart(SES_END_RESTART, 2000);
    lt_safe_clear();
    ESP_LOGI(TAG, "OTA reboot: safe/recovery gate cleared for the new image");
    (void)lt_counters_flush(true);
    esp_restart();
}

/* §19.4, once after boot: decide which image is running -- the new one on trial (PENDING_VERIFY ->
 * validate below once healthy) or the previously-valid image the bootloader rolled back to
 * (log E_OTA_ROLLBACK, count it, clear the flag). No OTA in flight and nothing on trial -> nop.
 *
 * H3: the running slot's own PENDING_VERIFY state -- not the NVS ota_pending flag -- is the
 * authority. The flag can be lost to an NVS GC/full or a power-cut between ota_end's persist and
 * set_boot, yet the freshly-booted image is still in PENDING_VERIFY and MUST be validated-or-
 * rolled-back. So read the partition state UNCONDITIONALLY; the flag serves only to attribute an
 * actual rollback (it was set, yet a non-pending image is running == the bootloader reverted). */
static void ota_boot_decide(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    LT_ASSERT_VOID(run != NULL, SUP_ASSERT_CODE);
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    bool pending_verify = (esp_ota_get_state_partition(run, &st) == ESP_OK &&
                           st == ESP_OTA_IMG_PENDING_VERIFY);
    if (pending_verify) {
        s_ota_awaiting = true;                 /* new image on trial (flag or not); ota_try_validate() confirms it */
        return;
    }
    if (!lt_ota_pending_get()) return;         /* no OTA in flight and nothing on trial -> nop */
    (void)errlog_add(E_OTA_ROLLBACK, 0);       /* flag set + a non-pending image running == a revert */
    lt_counters_inc(LT_CTR_OTA_ROLLBACK, true);
    lt_ota_pending_clear();
    ESP_LOGW(TAG, "OTA image rolled back by the bootloader");
    /* fix round 1 (Critical #1): sup_start() runs before app_main's lt_ipc_init(), so g_ui_evt_q is
     * still NULL on this, the supervisor's very first tick -- a direct xQueueSend here is always
     * dropped. Latch instead; ota_rollback_post_retry() (called every tick from ota_lifecycle())
     * posts it once the queue exists. */
    s_ota_rollback_pending = true;
}

/* fix round 1 (Critical #1): retried every supervisor tick until g_ui_evt_q exists and the send
 * lands -- see the latch comment in ota_boot_decide() above. A no-op once posted or when nothing
 * is pending. */
static void ota_rollback_post_retry(void)
{
    if (!s_ota_rollback_pending || g_ui_evt_q == NULL) return;
    event_t ev = { .type = EV_OTA, .flags = EV_OTA_ROLLED_BACK, .arg16 = 0, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) == pdTRUE) s_ota_rollback_pending = false;
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
 * first loop decide validate-vs-rollback; retry a latched rollback ui-post every tick until it
 * lands; then mark a pending image valid once it proves healthy.
 * Grouped here so the task entry (which must not assert-early-return) stays a thin dispatcher. */
static void ota_lifecycle(uint32_t uptime_s)
{
    ota_reboot_check();
    if (!s_ota_boot_checked) { s_ota_boot_checked = true; ota_boot_decide(); }
    ota_rollback_post_retry();
    ota_try_validate(uptime_s);
}

/* §17.5 (+ recovery-mode amendment, debt sweep A #62): once safe OR recovery mode has been up for
 * SAFE_MODE_CLEAR_S, clear the persisted gate/level and both runtime flags so the next -- and this
 * -- boot run normally. Fires once per boot. Grouped here (same reason as ota_lifecycle above) so
 * the task entry stays a thin dispatcher. */
static void safe_recovery_clear_check(uint32_t uptime_s)
{
    if (s_safe_mode_cleared) return;
    uint32_t f = sys_flags_get();
    if (!(f & ((1u << SYS_SAFE_MODE) | (1u << SYS_RECOVERY_MODE)))) return;
    if (uptime_s < SAFE_MODE_CLEAR_S) return;
    bool was_recovery = (f & (1u << SYS_RECOVERY_MODE)) != 0;   /* M8: log the mode that was actually active */
    lt_safe_clear();
    sys_flags_clear(SYS_SAFE_MODE);
    sys_flags_clear(SYS_RECOVERY_MODE);
    (void)errlog_add(was_recovery ? E_SYS_RECOVERY_MODE : E_SYS_SAFE_MODE, 0);
    s_safe_mode_cleared = true;
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
        safe_recovery_clear_check(uptime_s);      /* §17.5: uptime auto-clear (see helper) */

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
