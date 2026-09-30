/* app_main.c -- LapTimer firmware entry point and boot sequence (§4.7).
 *
 * Session 3.2 implements boot steps 1-5 (reset reason + crash counters, NVS init, boot
 * counter + crash-loop check, RTC-state validate, config load), step 6 in part (board
 * bring-up + GPS power, needed for the battery reading in `dbg status`), and steps 10-11
 * (task WDT + supervisor, hb[]/sys_flags, static queues). Steps 7-9 (storage, display,
 * self-test) and 12-15 (pipeline/logger/ui/power/conn/OTA) land in 3.3-3.5.
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

#include "hal/board.h"
#include "hal/storage.h"

#include "app/logger.h"
#include "app/lt_err.h"
#include "app/lt_ipc.h"
#include "app/link.h"
#include "app/lt_nvs.h"
#include "app/lt_rtc.h"
#include "app/lt_sup.h"
#include "app/pipeline.h"
#include "app/ui.h"

#if CFG_HAS_EXPORT_SERIAL
#include "export_serial.h"
#endif

#define MAIN_ASSERT_CODE 0x0C80

static const char *TAG = "laptimer";

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

/* §4.7 step 8 banner (kept from 3.1). */
static void boot_banner(esp_reset_reason_t reason)
{
    ESP_LOGI(TAG, "LapTimer %s (%s)", CFG_FW_VERSION, CFG_HWID);
    ESP_LOGI(TAG, "core %s | GPS %s | display %s | fused-log %d Hz",
             core_version(), CFG_GPS_NAME, CFG_DISPLAY_NAME, CFG_FUSED_LOG_HZ);
    ESP_LOGI(TAG, "reset reason: %s (%d)", lt_reset_reason_str((int)reason), (int)reason);
}

/* §4.7 step 2: NVS init, erase + re-init on a version/space fault (log E_SYS_CFG_RESET). */
static void boot_nvs(void)
{
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
    /* core asserts -> error ring via the linked core_assert_report (§17.9) */
    if (nvs_erased) errlog_add(E_SYS_CFG_RESET, 0);
}

/* §4.7 step 1 (cont): count + log + crash-log the reset reason; prev-boot uptime comes
 * from the RTC uptime cell the supervisor maintained last boot. F3: if the previous boot was
 * a supervisor-forced pipeline-stall restart (marker in NVS), its HW reason is ESP_RST_SW
 * (§17.5 normal) -- fold it in as the abnormal LT_RST_STALL so consecutive stalls trip the
 * crash-loop -> safe mode below. Consume the marker either way. */
static void boot_reset_record(esp_reset_reason_t reason)
{
    int record_reason = (int)reason;
    if (lt_stall_flag_take()) {
        record_reason = LT_RST_STALL;
        ESP_LOGW(TAG, "previous boot ended in a pipeline-stall restart (counted abnormal)");
    }
    uint32_t prev_uptime_s = lt_rtc_uptime_prev_s();
    lt_boot_record_reset(record_reason, prev_uptime_s);
}

/* §4.7 step 3: boot counter + crash-loop check (§17.5, debt sweep A #62 recovery-mode amendment).
 * level 0 normal, 1 safe, 2 recovery: a crash loop detected while still inside a prior safe-mode
 * window (boot_cnt <= lt_safe_until_get()) escalates to recovery instead of re-entering safe mode;
 * a loop with no prior window is a plain safe-mode entry, as before. Level persists in NVS
 * (lt_safe_level_get/set) alongside safe_until so a non-loop boot still inside the window resumes
 * at the same level. */
static uint32_t boot_safe_mode(uint8_t *level_out)
{
    /* M5: precondition first -- &level is always non-NULL (app_main's only caller), so there is
     * no useful boot_cnt yet to return; 0u is the safe placeholder (the caller asserts
     * boot_cnt >= 1, which never runs on this path since app_main() itself never passes NULL). */
    CORE_ASSERT_RET(level_out != NULL, MAIN_ASSERT_CODE, 0u);
    uint32_t boot_cnt = lt_nvs_boot_inc();
    lt_counters_inc(LT_CTR_BOOTS, false);      /* batched with the rest */
    uint8_t level = 0;
    bool in_window = boot_cnt <= lt_safe_until_get();
    if (lt_crashlog_is_loop()) {
        level = in_window ? 2u : 1u;           /* a loop inside a safe-mode window escalates (§17.5) */
        lt_safe_until_set(boot_cnt + 1);
        (void)lt_safe_level_set(level);
        ESP_LOGE(TAG, "crash loop: 3 abnormal resets < 60 s -> %s",
                 level == 2u ? "RECOVERY MODE" : "SAFE MODE");
    } else if (in_window) {
        level = lt_safe_level_get();           /* still inside a prior window: same level */
        if (level == 0u) level = 1u;
    }
    if (level >= 1u) { sys_flags_set(SYS_SAFE_MODE); errlog_add(E_SYS_SAFE_MODE, boot_cnt); }
    if (level == 2u) { sys_flags_set(SYS_RECOVERY_MODE); errlog_add(E_SYS_RECOVERY_MODE, boot_cnt); }
    CORE_ASSERT_RET(level <= 2u, MAIN_ASSERT_CODE, boot_cnt);   /* postcondition: level is one of 0/1/2 */
    *level_out = level;
    return boot_cnt;
}

/* §4.7 step 4: RTC memory validate/clear. Full resume is 3.5; here invalid/absent are
 * cleared and only a present-but-bad snapshot logs E_SYS_RTC_INVALID. */
static void boot_rtc_validate(void)
{
    switch (lt_rtc_validate(NULL)) {
    case RTC_INVALID:
        errlog_add(E_SYS_RTC_INVALID, 0);
        lt_rtc_clear();
        break;
    case RTC_ABSENT:
        lt_rtc_clear();
        break;
    case RTC_VALID:
        break;                                 /* left in place; 3.5 resumes from it */
    }
}

/* §4.7 step 5: config load. defaults -> profile -> NVS blob (stored settings win, §15.1);
*  defaults + a fresh save on absent/corrupt; log corrections. */
static void boot_config(void)
{
    static cfg_t cfg;
    char ble_name[16];
    cfg_profile_t prof;
    cfg_defaults(&cfg);
    make_profile(&prof, ble_name, sizeof(ble_name));
    cfg_apply_profile(&cfg, &prof);
    int corr = lt_cfg_load(&cfg);
    if (corr < 0) {
        lt_cfg_save(&cfg);                     /* no valid blob -> persist the profile defaults */
        errlog_add(E_SYS_CFG_RESET, 0);
    } else if (corr > 0) {
        errlog_add(E_SYS_CFG_RESET, (uint32_t)corr);
        lt_cfg_save(&cfg);                     /* persist the clamped config */
    }
}

/* §4.7 step 7 (internal storage): mount LittleFS; the mount ladder (mount -> retry ->
 * format -> dead) lives in the driver. The driver stays app-agnostic, so the boot sequence
 * owns the sys_flags / error-ring / counter effects of the ladder result (§13.1). */
static void boot_storage(void)
{
    int mrc = sto_mount();
    /* Plan 7c T8 (design §6): the BOOT screen's STORAGE slot, reported before the ui task even
     * starts (boot_subsystems() -> ui_start() runs after this) so ui_task's first boot_lines_format()
     * call already sees the real result. */
    sup_boot_report(BOOT_STORAGE, mrc < 0 ? BOOT_FAIL : BOOT_OK);
    if (mrc < 0) {
        sys_flags_set(SYS_STORAGE_DEAD);
        errlog_add(E_STO_MOUNT, 0);
        ESP_LOGE(TAG, "storage DEAD (summaries fall back to RTC/NVS best-effort, 3.5)");
    } else {
        if (mrc == 1) {                        /* mounted only after a reformat */
            lt_counters_inc(LT_CTR_STO_FORMAT, true);
            errlog_add(E_STO_FORMAT, 0);
        }
        if (sto_probe() != 0) ESP_LOGW(TAG, "storage probe failed");   /* §17.6 self-test */
        sto_info_t si;
        if (sto_info(&si) == 0)
            ESP_LOGI(TAG, "storage: %u/%u KB free", (unsigned)si.free_kb, (unsigned)si.total_kb);
    }
}

/* §4.7 steps 10-13: supervisor, static queues, IPC rings, and the logger/pipeline/ui tasks.
 * level 2 (recovery, §17.5 amendment, debt sweep A #62): only the service tasks below start --
 * pipeline and ui do not -- so a deterministic driver crash inside either converges to a
 * serviceable device instead of looping. The panel keeps whatever it last rendered (no ui task
 * to draw a RECOVERY screen); console/OTA stay reachable via export_serial_start() in app_main. */
static void boot_subsystems(uint8_t level)
{
    CORE_ASSERT_VOID(level <= 2u, MAIN_ASSERT_CODE);

    /* §4.7 step 10: the task WDT is already enabled via sdkconfig; hb[]/sys_flags exist
     * (lt_sys). Start the supervisor first -- it subscribes itself to the task WDT. */
    sup_start();

    /* §4.7 step 11: static queues the supervisor + button ISR need (btn_q). The pipeline
     * rings arrive in 3.4. */
    lt_queues_init();

    /* §4.7 step 11 (cont): the §4.4 pipeline<->logger rings + the logger's event/control
     * queues. The pipeline producer lands in 3.4; 3.3 creates them and the logger consumes. */
    lt_ipc_init();

    /* §4.7 step 11 (cont): start the peer link (Plan 5 §18). Creates the bounded stream ring
     * + a low-priority drain task and configures the detect line, BEFORE the pipeline produces
     * (below) so stream_push has somewhere to go. Streams only when a peer is attached; idle and
     * leak-free with none. */
    link_start();

    /* §4.7 step 12 (logger): start the logger task (core 0, prio 8). It idles until a
     * LOGGER_OPEN_SESSION request arrives (from the pipeline below on the sim build, the power
     * task later, or `dbg logtest`); with storage dead it stays idle (open fails gracefully). */
    logger_start();

    if (level == 2u) {                                 /* recovery (§17.5): service tasks only */
        ESP_LOGE(TAG, "RECOVERY MODE: pipeline, GPS power and ui not started");
        return;
    }

    /* §4.7 step 12 (pipeline): start the pipeline task (core 1, prio 20). It brings up the GPS/IMU
     * drivers, runs tb + fusion + lap/drag + per-lap stats (§9.1/§9.4), and on the moto_sim bench
     * build opens a session, arms the sim venue, and streams the committed capture into laps. On the
     * real GPS variant it idles until the sensor driver (plan 08) delivers fixes. */
    pipeline_start();

    /* §4.7 step 13 (ui): start the ui task (core 0, prio 6). It renders the BOOT one-shot, then
     * drains the pipeline event queue + the button queue, updates a screen_model_t, and renders via
     * core/ui screens_render(). Plan 04 ships no display driver, so it logs the dirty box instead of
     * refreshing a panel; the menu (§20.7) + buttons (§20.8) are live. */
    ui_start();
}

/* ESP-IDF calls app_main() as the framework entry point; it has no project header to declare it
 * in (rule 6: -Wmissing-prototypes needs a prototype in scope at the definition). */
void app_main(void);

void app_main(void)
{
    int64_t t_boot = esp_timer_get_time();
    CORE_ASSERT_VOID(t_boot > 0, MAIN_ASSERT_CODE);   /* monotonic boot timer already running */

    /* §4.7 step 1: reset reason (crash counters recorded below, after NVS is up). */
    esp_reset_reason_t reason = esp_reset_reason();

    boot_banner(reason);
    boot_nvs();
    boot_reset_record(reason);

    uint8_t level = 0;
    uint32_t boot_cnt = boot_safe_mode(&level);
    CORE_ASSERT_VOID(boot_cnt >= 1, MAIN_ASSERT_CODE);   /* boot counter was just incremented */

    boot_rtc_validate();
    boot_config();

    /* §4.7 step 6 (partial): board bring-up + GPS power on (battery read feeds `dbg status`).
     * Recovery (level 2, §17.5 amendment) skips GPS power -- the pipeline that would consume it
     * never starts. */
    board_init();
    if (level != 2u) board_gps_power(true);

    boot_storage();
    boot_subsystems(level);

    /* §4.7 step 12 (console): the §18.4 serial export console -- STATUS/CONFIG/ERRLOG/DIAG/
     * DELETE/CLOSE wired through app/cmd, plus the migrated `dbg` diagnostics verbs. Spawns its
     * own REPL task on UART0 (gated by the EXPORT_SERIAL build flag / CFG_HAS_EXPORT_SERIAL). */
#if CFG_HAS_EXPORT_SERIAL
    export_serial_start((int)reason);
#endif

    ESP_LOGI(TAG, "boot #%u complete in %lld ms (safe_mode=%u)", (unsigned)boot_cnt,
             (long long)((esp_timer_get_time() - t_boot) / 1000), (unsigned)level);
#if CFG_HAS_EXPORT_SERIAL && !CFG_HAS_DEVUX
    /* §4.6 DEVUX=OFF: the interactive dbg UX is compiled out; the serial link is the
     * cmd + stream + OTA-receive transport only (Plan 5 sub-project A prod-slim). */
    ESP_LOGI(TAG, "serial link: prod-slim (DEVUX off -- transport only, no dbg UX)");
#endif

    /* The main task returns: the supervisor and console tasks run on, and the idle tasks on
     * both cores feed the task WDT. Pipeline/logger/ui/power start here in 3.4. */
}
