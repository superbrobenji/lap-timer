/* sup.c -- supervisor task (spec §4.3: core 0, prio 22, stack 3072, 1000 ms; loop §17.2).
 *
 * Runs the §17.2 loop: heartbeat-stall watch with a once-per-episode report, the task-WDT
 * subscribe/feed wrappers the supervised app tasks use (link_task deliberately does not
 * subscribe), the crash-loop uptime tick, the batched counter flush, the §19.4 OTA lifecycle
 * (boot decide / validate / reboot / receive timeout), the §17.5 safe-mode auto-clear, the §17.2
 * heap and stack checks, §17.1's 60 s heap-integrity walk, and EV_FAULT on every sys_flags
 * change. Still absent, each with an owner: the GPS/IMU/storage ladders and the temperature
 * throttle (Plan 8), the "< 20 KB -> conn_stop()" heap step (no conn task; sub-project C).
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
#include "app/logger.h"
#include "app/sup_policy.h"

#include "core/ses.h"

#include "esp_heap_caps.h"
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
    sup_stall_t  stall;          /* R9: episode state owned by sup_policy.c (last_hb/stalled_s/msg_logged) */
    bool         stack_logged;   /* §17.2: E_SYS_STACK_LOW fires once per task per boot */
    bool         used;
} watch_t;

static watch_t      s_watch[HB_COUNT];
static StaticTask_t s_tcb;
static StackType_t  s_stack[SUP_STACK_WORDS];
static uint32_t s_flags_prev;        /* §17.2 "EV_FAULT on changes": last published sys_flags value */
static uint8_t  s_stack_scan;        /* §17.2 stack check: round-robin cursor over s_watch[] */
static uint32_t s_integrity_at_s;    /* §17.1 heap_caps_check_integrity_all: last run (uptime s) */
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
                                .stall = { .last_hb = g_hb[hb_id], .stalled_s = 0, .msg_logged = false },
                                .stack_logged = false, .used = true };
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
        /* w->hb_id is stored state (bounded once at registration); re-checking it here before it
         * indexes g_hb[] guards against future slot corruption reading past g_hb[HB_COUNT]. */
        LT_ASSERT_VOID(w->hb_id < HB_COUNT, SUP_ASSERT_CODE);
        uint8_t act = sup_stall_step(&w->stall, g_hb[w->hb_id], w->stall_s,
                                     (uint32_t)(SUP_PERIOD_MS / 1000), w->hb_id == HB_PIPELINE);
        if (act & SUP_STALL_LOG) {
            /* R9: ONCE per stall episode. This line used to print every second for as long as a
             * task was wedged, landing inside ---BEGIN/---END bodies and the raw OTA byte stream
             * (app:M12) -- exactly the degraded conditions where the operator needs the log. */
            ESP_LOGE(TAG, "task %d stalled %us", w->hb_id, (unsigned)w->stall.stalled_s);
            (void)errlog_add(E_SYS_TASK_STALL, w->hb_id);
        }
        if (act & SUP_STALL_RESTART) {
            /* §17.2: a stalled pipeline restarts. R6 strikes §17.2's "after saving RTC state":
             * the pipeline OWNS the RTC snapshot (lt_rtc_save runs on its task on every S/F and
             * sector event), and a wedged pipeline cannot be asked to save -- so the resume point
             * is the last gate, by design, not by omission. F3: the HW reset reason will be
             * ESP_RST_SW (§17.5 normal), so leave a marker the next boot folds in as abnormal. */
            close_session_before_restart(SES_END_STALL, 500);
            (void)lt_counters_flush(true);
            lt_stall_flag_set();
            esp_restart();
        }
    }
}

/* §19.4: the applied image asked (via ota_end) for a reboot after its OTA_END ack; the supervisor
 * owns every system restart, so it performs this one too, flushing counters first.
 *
 * §17.5 amendment (controller ruling P-8, debt sweep A T6 fix 1): clear the safe/recovery gate
 * before rebooting into the NEW image, so it boots at level 0 and gets its own normal validation
 * trial (ota_try_validate's `!SAFE_MODE && pipeline_sensors_ok()` gate needs the pipeline running,
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

/* §19.4: mark a pending image valid (cancel rollback) once it is not in safe mode, storage is
 * mounted, the sensor gate is satisfied and it has run for OTA_VALID_UPTIME_S. Until then a
 * panic/WDT/brownout lets the bootloader roll back (logged as E_OTA_ROLLBACK on the next boot).
 *
 * R1 (audit §7.1, app:C1, drv:H3): this gate used to require pipeline_gps_seen(), which the
 * Plan-03 gps_neo6m stub can never satisfy (gps_poll always returns 0) -- so on moto_neo6m, the
 * only release env, every applied image stayed PENDING_VERIFY, the bootloader reverted it at the
 * next reset, and the un-cleared ota_pend flag then refused every later push until a USB flash.
 * pipeline_sensors_ok() is true on any build whose GPS driver is not a live receiver
 * (gps_profile_t.live == 0) -- that includes moto_sim, whose gps_sim leaves .live at 0, so there
 * the gate opens as soon as the pipeline has latched its drivers, with NO fix required. Only a
 * live driver (Plan 8) makes it wait for a real fix. */
static void ota_try_validate(uint32_t uptime_s)
{
    if (!s_ota_awaiting) return;
    uint32_t f = sys_flags_get();
    bool healthy = !(f & (1u << SYS_SAFE_MODE)) && !(f & (1u << SYS_STORAGE_DEAD)) &&
                   pipeline_sensors_ok() && uptime_s >= OTA_VALID_UPTIME_S;
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
    ota_poll();                 /* §19.6: a dead peer must not wedge the state machine (app:M8) */
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

/* §17.1 (app:H2 / R7): the task-WDT subscription, wrapped so every app task subscribes the same
 * way and the result is VISIBLE at boot. §17.1 claimed "all six app tasks and both idle tasks
 * subscribed"; only the supervisor and the ui actually were, so a wedged logger -- the Plan-7-T9
 * LittleFS-compaction failure mode -- was detected by check_stalls alone, which logs
 * E_SYS_TASK_STALL and restarts nothing but the pipeline. Call once from a task entry, then
 * sup_wdt_feed() once per loop iteration and inside any long operation (§17.1).
 * link_task deliberately does NOT subscribe -- see the §17.1 amendment: it is a best-effort
 * telemetry drain and a stalled or absent peer must never reset the lap-timer. */
void sup_wdt_subscribe(uint8_t hb_id)
{
    LT_ASSERT_VOID(hb_id < HB_COUNT, SUP_ASSERT_CODE);
    esp_err_t e = esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "wdt subscribe hb=%u rc=%d", (unsigned)hb_id, (int)e);
    if (e != ESP_OK) (void)errlog_add(E_SYS_WDT_SUBSCRIBE, hb_id);
}

void sup_wdt_feed(void) { (void)esp_task_wdt_reset(); }

/* §17.2 heap line + §4.8's "min-free heap stays above 40 KB" hard rule (app:M5: SYS_HEAP_LOW and
 * E_SYS_HEAP_LOW had zero writers firmware-wide, so the hard rule had no enforcement at all).
 * The flag tracks the CURRENT free size so it can clear with R6's hysteresis; the errlog arg
 * carries the min-ever figure the hard rule is written against. */
static void check_heap(void)
{
    uint32_t cur  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t minf = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    bool flagged = (sys_flags_get() & (1u << SYS_HEAP_LOW)) != 0;
    uint8_t act = sup_heap_eval(cur, flagged);
    if (act == SUP_HEAP_ENTER) {
        sys_flags_set(SYS_HEAP_LOW);
        (void)errlog_add(E_SYS_HEAP_LOW, minf);
        ESP_LOGE(TAG, "heap low: free %u B (min ever %u B)", (unsigned)cur, (unsigned)minf);
    } else if (act == SUP_HEAP_CLEAR) {
        sys_flags_clear(SYS_HEAP_LOW);
        ESP_LOGW(TAG, "heap recovered: free %u B", (unsigned)cur);
    }
}

/* §17.2 stack line (app:M5). One registered task per tick, round-robin, so the loop stays short
 * and every task is sampled once per HB_COUNT seconds; once per task per boot (stack_logged),
 * because a task that dipped below the line will keep reading below it -- the high-water mark
 * never recovers. arg packs the task id and the headroom: (hb_id << 16) | high_water. errlog_add
 * collapses consecutive identical CODES (M1), so when two tasks go low on consecutive ticks with
 * no other errlog record between, the second report replaces the first (latest task wins; `dbg
 * mem` shows every task's high-water mark). Same for E_SYS_TASK_STALL (S3-18). */
static void check_stack(void)
{
    uint8_t id = s_stack_scan;
    s_stack_scan = (uint8_t)((s_stack_scan + 1u) % HB_COUNT);
    watch_t *w = &s_watch[id];
    if (!w->used || w->stack_logged || w->task == NULL) return;
    uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(w->task);
    if (!sup_stack_low(hw)) return;
    w->stack_logged = true;
    (void)errlog_add(E_SYS_STACK_LOW, ((uint32_t)id << 16) | (hw & 0xFFFFu));
    ESP_LOGE(TAG, "stack low task %u free %u", (unsigned)id, (unsigned)hw);
}

/* §17.2's last line, "sys_flags -> ui (fault icons); EV_FAULT on changes" (app:M5: never
 * emitted -- the EV_FAULT constant was used only as an upper bound in assertions). One EV_FAULT
 * per CHANGED bit onto BOTH event queues: g_evt_q is the logger's copy (EV_FAULT is a logged
 * §12.3 EVENT record, so a session file finally carries when each fault appeared and cleared) and
 * g_ui_evt_q is the ui's. A second producer onto a FreeRTOS queue is legal -- xQueueSend is
 * multi-producer safe -- whereas g_stream_ring is a relaxed-head SPSC ring with the pipeline as
 * its single documented producer, so stream_push() is NEVER called from here (design §2 R6).
 * arg16 = the sys_flags bit index; flags = 1 when the bit was SET, 0 when it was CLEARED (the
 * ui copy carries both, but ui.c currently ignores EV_FAULT -- default: break -- and reads
 * sys_flags_get() directly for its fault strip; R6 mandates the post regardless). The logger's handle_event encodes only arg32 (ses_encode_event),
 * so arg32 = (bit << 8) | set is what reaches the session log.
 * Bounded by 32 iterations. The early return keeps s_flags_prev un-advanced until both queues
 * exist (sup_start() runs before app_main's lt_ipc_init(), so the first tick has neither), which
 * is what makes the boot-time flags -- SAFE_MODE, STORAGE_DEAD, NVS_DEAD -- reach the queues (the
 * session log and the ui queue) at all; the ui's strip sees them through sys_flags either way. */
static void emit_faults(void)
{
    if (g_evt_q == NULL || g_ui_evt_q == NULL) return;
    uint32_t cur = sys_flags_get();
    if (cur == s_flags_prev) return;
    int64_t now = esp_timer_get_time();
    for (uint8_t b = sup_flag_next_change(s_flags_prev, cur, 0);
         b < 32u; b = sup_flag_next_change(s_flags_prev, cur, (uint8_t)(b + 1u))) {
        event_t ev = { .type = EV_FAULT, .flags = (uint8_t)((cur >> b) & 1u), .arg16 = b,
                       .gps_us = 0, .mono_us = now,
                       .arg32 = ((uint32_t)b << 8) | ((cur >> b) & 1u), .arg32b = 0 };
        (void)xQueueSend(g_evt_q, &ev, 0);        /* a dropped event is lost (s_flags_prev still advances): */
        (void)xQueueSend(g_ui_evt_q, &ev, 0);     /* the ui reads sys_flags directly, so only the log loses it */
    }
    s_flags_prev = cur;
    logger_notify();                              /* drain the EVENT record promptly */
}

/* §17.1's last protection: heap_caps_check_integrity_all() every HEAP_INTEGRITY_PERIOD_S. §17.1
 * says "in debug builds only"; this project ships one build flavour (no poisoning is configured
 * in sdkconfig.defaults, so a #if CONFIG_HEAP_POISONING_* guard would compile the line out on
 * every env and leave §17.1 claiming a check that never runs). It therefore runs unconditionally:
 * without poisoning the walk validates the TLSF control structure and every block header (no
 * canaries: CONFIG_HEAP_POISONING_DISABLED, multi_heap_check is tlsf_check only), it is a
 * header walk over a few hundred blocks once a minute, and the supervisor has the budget. §17.1
 * is amended accordingly (Task 10). */
static void check_heap_integrity(uint32_t uptime_s)
{
    if (uptime_s - s_integrity_at_s < (uint32_t)HEAP_INTEGRITY_PERIOD_S) return;
    s_integrity_at_s = uptime_s;
    if (!heap_caps_check_integrity_all(true)) {
        (void)errlog_add(E_SYS_HEAP_CORRUPT, uptime_s);
        ESP_LOGE(TAG, "heap integrity check FAILED");
    }
}

static void sup_task(void *arg)
{
    (void)arg;
    sup_wdt_subscribe(HB_SUPERVISOR);             /* §17.1: supervisor subscribes to the task WDT */
    (void)sup_register_task(HB_SUPERVISOR, xTaskGetCurrentTaskHandle(), 0);   /* self: WDT covers a stuck sup */
    ESP_LOGI(TAG, "supervisor up (core %d prio %d)", SUP_CORE, SUP_PRIO);

    /* Never assert()-early-return from a FreeRTOS task entry: returning from this function would
     * return from the task itself, which is undefined behaviour. Every genuine anomaly this loop
     * could hit is instead asserted inside the (non-task-entry) helper it calls. */
    for (;;) {
        check_stalls();
        sup_wdt_feed();
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        lt_rtc_uptime_update_s(uptime_s);          /* crash-loop tracker */
        (void)lt_counters_flush(false);           /* persist if dirty and >=60 s (§15.2) */

        ota_lifecycle(uptime_s);                  /* §19.4: reboot / boot-decide / validate / recv timeout */
        safe_recovery_clear_check(uptime_s);      /* §17.5: uptime auto-clear (see helper) */
        check_heap();                             /* §17.2 heap line (+ §4.8's hard rule) */
        check_stack();                            /* §17.2 stack line, one task per tick */
        check_heap_integrity(uptime_s);           /* §17.1, every 60 s */
        emit_faults();                            /* §17.2 "sys_flags -> ui; EV_FAULT on changes" */

        /* Still absent by design, each with an owner (design §5): the GPS §7.5 and IMU §8.7
         * ladders and their CMD_GPS_LADDER_STEP / CMD_IMU_RECOVER commands, and the storage §13
         * ladder, need the real drivers -- filed against Plan 8. The §17.2 temperature throttle
         * needs a calibrated IMU temperature producer -- Plan 8/#93. The "< 20 KB -> conn_stop()"
         * heap step needs a conn task -- sub-project C. */

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
