/* app_main.c -- LapTimer firmware entry point and boot sequence (§4.7).
 *
 * Implements the §4.7 boot sequence: step 1 reset reason + crash counters, step 2 NVS, step 3
 * boot counter + crash-loop/safe-mode/recovery gate, step 4 RTC validate, step 5 config load,
 * step 6 board + GPS power, step 7 internal storage mount, step 9 the coredump self-test row,
 * steps 10-11 supervisor + static queues/rings + the peer link, step 12 logger/pipeline and the
 * §18.4 console, step 13 ui. Steps 14-15 (power task, conn) have no owner until Plan 6 /
 * sub-project C. The BOOT screen has four slots (storage, display, GPS, IMU; app/lt_sup.h
 * BOOT_*); §17.6's GPS MON-VER (gps_get_version is not called at boot) and battery-range rows
 * have no boot-time check at all, and the IMU-genuine row exists only as imu_init()'s WHO_AM_I
 * feeding the IMU slot -- handed off (F2a hand-off table).
 */
#include "build_config.h"

#include <stdio.h>

#include "core/cfg.h"
#include "core/core.h"
#include "core/trk.h"   /* trk_init() -- #97, §10.9, review fix round 1 (T5-R2) */

#include "esp_core_dump.h"
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

/* app:L3: a failed lt_nvs_init() leaves every NVS accessor on handle 0. That is degraded-but-safe
 * (IDF validates handles), but it silently disables crash-loop detection, the safe-mode gate and
 * unique session ids (boot_cnt stays 0, so open_session regenerates S00000_001 every boot) -- so
 * it gets a sys_flag a peer and the fault strip can see, not one ESP_LOGE. sys_flags_set() is an
 * atomic OR on a static word, safe before sup_start(); the supervisor's first emit_faults() after
 * its queues exist publishes the bit (s_flags_prev stays 0 until then). The errlog entry goes
 * after the flag so it exists in the RAM ring even though its NVS persist will fail. */
static void boot_nvs_dead(void)
{
    sys_flags_set(SYS_NVS_DEAD);
    ESP_LOGE(TAG, "lt_nvs_init failed: boot counter, crash-loop detection and the safe-mode "
                  "gate are INOPERATIVE this boot");
    errlog_add(E_NVS_INIT, 0);
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
    if (lt_nvs_init() != 0) boot_nvs_dead();
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

/* §4.7 step 4: RTC memory validate/clear. The resume itself is the pipeline's
 * (pipeline_init() re-validates and arms the §15.3 resume); here invalid/absent are cleared and
 * only a present-but-bad snapshot logs E_SYS_RTC_INVALID. */
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
        break;                                 /* left in place; pipeline_init() resumes */
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
        (void)cfg_validate(&cfg);              /* io:M6/R26: the only path that stores a config
                                                * cfg_validate never saw */
        lt_cfg_save(&cfg);                     /* no valid blob -> persist the validated defaults */
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
        ESP_LOGE(TAG, "storage DEAD (no session logging this boot; the logger stays idle)");
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

/* §4.7 step 9 / §17.6's coredump row. Reported ONLY on a boot whose reset reason was a crash:
 * the image is deliberately KEPT (§17.10 exports it, and ruling R5's `coredump` console verb --
 * plan F2b -- reads it), so an unconditional report would add one errlog entry per boot forever
 * until someone erased it. CRC32 is the checksum §17.1 configures
 * (CONFIG_ESP_COREDUMP_CHECKSUM_CRC32), so a failed check means the dump itself is damaged and
 * the log line says so rather than promising a readable dump. Errlog M1 dedup: on an ESP_RST_PANIC
 * boot this report collapses onto lt_boot_record_reset's E_SYS_PANIC(0) entry (lt_nvs.c) -- the
 * latest arg wins -- so that one entry becomes the panic-with-dump-size summary §17.6 asks for.
 * That holds because boot_reset_record() is the only thing between the two calls and its only
 * errlog_add is that E_SYS_PANIC(0) (the crashlog push and counter writes add no entries);
 * on TASK_WDT/INT_WDT boots it appends after E_SYS_WDT_RESET. */
static void boot_coredump_check(esp_reset_reason_t reason)
{
    if (reason != ESP_RST_PANIC && reason != ESP_RST_TASK_WDT && reason != ESP_RST_INT_WDT) return;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        ESP_LOGW(TAG, "crash reset but no coredump stored");
        return;
    }
    esp_err_t chk = esp_core_dump_image_check();
    ESP_LOGW(TAG, "coredump: %u B at 0x%06x, check %s (kept)", (unsigned)size, (unsigned)addr,
             chk == ESP_OK ? "ok" : esp_err_to_name(chk));
    (void)errlog_add(E_SYS_PANIC, (uint32_t)size);
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

    /* §4.7 step 11: the static button queue (g_btn_q) the ui task and button ISR need. */
    lt_queues_init();

    /* §4.7 step 11 (cont): the §4.4 fix/fused rings and the event queues (g_evt_q for the logger,
     * g_ui_evt_q for the ui) plus the logger's request/result queues and the command queue. All
     * exist before logger_start(), pipeline_start() and ui_start() below create their consumers
     * and producers. */
    lt_ipc_init();

    /* §4.7 step 11 (cont): start the peer link (Plan 5 §18). Creates the bounded stream ring
     * + a low-priority drain task and configures the detect line, BEFORE the pipeline produces
     * (below) so stream_push has somewhere to go. Streams only when a peer is attached; idle and
     * leak-free with none. */
    link_start();

    /* #97 (§10.9), final review F-4/I-4: clear the user track table, then load any venues
     * persisted from a previous boot (/tracks/user.bin) -- BOTH here, on app_main's own task, in
     * that order, strictly BEFORE logger_start() creates the logger task a few lines below and
     * well before pipeline_start() creates the pipeline task further down still. Previously the
     * load ran inside logger_task()'s own boot init instead, on a safety argument (the logger
     * task preempts app_main the instant it is created) that rested on two premises the old
     * comment never stated and nothing in the tree enforced -- same-core task affinity, and the
     * load never yielding. Running both writes here, before either task that could ever race them
     * exists, needs no concurrency premise at all: ordinary single-threaded sequencing (see
     * core/trk.h's ownership comment for the full argument). trk_init()/trk_get() etc. need
     * core/trk.h; logger_load_tracks() is the read-only helper that stays in logger.c (app/
     * logger.h) since it only reads the file tracks_save() (also logger.c) owns writing. The
     * pipeline no longer touches this table at boot at all. */
    trk_init();
    if (logger_load_tracks() != 0) ESP_LOGW(TAG, "user tracks: load failed or none persisted");

    /* §4.7 step 12 (logger): start the logger task (core 0, prio 8); idles until a
     * LOGGER_OPEN_SESSION request arrives (from the pipeline below on the sim build, the power
     * task later, or `dbg logtest`); with storage dead it stays idle (open/load both fail
     * gracefully). */
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
     * drains g_ui_evt_q (the pipeline's fan-out copy) + the button queue, updates a
     * screen_model_t, and renders via core/ui screens_render(). It drives the SSD1680 through
     * hal/display.h and runs the full §20.3 refresh ladder; the menu (§20.7) + buttons (§20.8)
     * are live. */
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
    boot_coredump_check(reason);

    uint8_t level = 0;
    uint32_t boot_cnt = boot_safe_mode(&level);
    CORE_ASSERT_VOID(boot_cnt >= 1, MAIN_ASSERT_CODE);   /* boot counter was just incremented */

    boot_rtc_validate();
    boot_config();

    /* §4.7 step 6 (partial) + §17.6: board bring-up. board_init() reports (0 / -EIO) instead of
     * ESP_ERROR_CHECK-aborting, so a dud bus is a recorded self-test result rather than a panic
     * feeding §17.5's crash loop. The battery ADC, I2C (IMU/mag) and VSPI (panel) all hang off
     * this, so a failure here is what makes the IMU/DISPLAY BOOT slots fail honestly. GPS power
     * follows; recovery (level 2, §17.5 amendment) skips it -- the pipeline that would consume it
     * never starts. */
    int brc = board_init();
    if (brc != 0) {
        ESP_LOGE(TAG, "board_init rc %d: some peripherals are unavailable this boot", brc);
        (void)errlog_add(E_BOARD_INIT, 0);
    }
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

    /* The main task returns: the supervisor, logger, pipeline, ui, link and console tasks (all
     * started above) run on, and the idle tasks on both cores feed the task WDT. There is no
     * power task (Plan 6). */
}
