/* pipeline.c -- the pipeline task (spec §4.3 core 1/prio 20/stack 8192; loop §9.1; stats §9.4).
 *
 * The single producer for fix_ring / fused_ring / evt_q (§4.4). It ingests GPS fixes (gps_poll) and
 * IMU samples (imu_read_fifo), timestamps them (tb), runs fusion (fus), drives the lap or drag
 * engine in the exact §9.1 on_fix / on_raw order, accumulates per-lap statistics at 100 Hz (§9.4),
 * emits engine + pipeline events to the logger's evt_q, and hands the logger the COMPLETE
 * lap_result_t / drag_result_t (result_q) so it writes full LAP/SECTOR and DRAG_RUN/DRAG_GATE
 * records. Completed laps are also printed to the console and kept for `dbg laps`.
 *
 * on_fix mirrors tools/replay/lib/replay_run.c line for line (same §6.5 validity, same call order),
 * so on the moto_sim bench build -- where gps_sim replays the committed --pos-sigma 0 capture -- the
 * on-device lap times reproduce test/data/sim_capture.expected.json within the +/-30 ms exit gate.
 *
 * The lap engine ignores its fused argument (§9.1/§9.4: lap timing is a function of GPS only), so the
 * per-lap stats are the pipeline's own job: accumulated here and written into the completing lap.
 */
#include "app/pipeline.h"
#include "app/lt_ipc.h"
#include "app/logger.h"
#include "app/lt_sup.h"
#include "app/lt_rtc.h"
#include "app/lt_nvs.h"

#include "hal/gps.h"
#include "hal/imu.h"

#include "core/consts.h"
#include "core/drag.h"
#include "core/event.h"
#include "core/fus.h"
#include "core/geo.h"
#include "core/lap.h"
#include "core/tb.h"
#include "core/trk.h"
#include "core/types.h"

#include "build_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#include "app/lt_consts.h"   /* RTC_RESUME_MAX_S (§15.3 / Appendix A) */

static const char *TAG = "pipe";

/* §4.3 task */
#define PIPE_CORE        1
#define PIPE_PRIO        20
#define PIPE_STACK_BYTES 8192
#define PIPE_STACK_WORDS (PIPE_STACK_BYTES / sizeof(StackType_t))
#define PIPE_STALL_S     5
#define PIPE_PERIOD_MS   50                                  /* §9.1 50 ms IMU cadence */
#define IMU_BATCH        16                                  /* >= samples due in one period at 100 Hz */
#define FUSED_DECIM      (FUSION_HZ / CFG_FUSED_LOG_HZ)       /* push every Nth fused sample (§9.1) */
#define TEMP_POLL_US     1000000                             /* ~1 Hz imu temperature (§9.2) */
#define PIPE_LAPS_KEEP   24
#define MMS_TO_KMH       0.0036                              /* mm/s -> km/h; mirrors tools/replay MMS_TO_KMH */

static StaticTask_t s_tcb;
static StackType_t  s_stack[PIPE_STACK_WORDS];
static TaskHandle_t s_task;

/* engine state */
static tb_t   s_tb;
static fus_t  s_fus;
static lap_t  s_lap;
static drag_t s_drag;
static uint8_t s_mode;                     /* MODE_LAP / MODE_DRAG */

/* §15.3 RTC continuity: resume the interrupted lap after a reset, and save on every gate. */
static rtc_state_t s_resume;               /* VALID snapshot read at init, consumed on first fix */
static bool        s_resume_pending;       /* armed at init; import (or drop) on the first valid fix */
static bool        s_rtc_save_due;         /* an S/F or sector event fired this fix -> save after on_fix */
static char        s_session_id[10];       /* identity stored in the RTC snapshot (diagnostic) */
static bool        s_resume_active;         /* F2: a resumed lap is running; guard it against a rewound clock */
static int64_t     s_resumed_lap_start_us;  /* F2: resumed lap start; a later fix below it = rewind */

#if CFG_GPS_SIM
static trk_venue_t s_venue;                /* the sim capture's venue (from gps_sim's JSON) */
extern const char *gps_sim_venue_json(void);   /* provided by the gps_sim driver (link-time) */
#endif

/* §6.5 validity state (recomputed here exactly as replay does) */
static bool    s_have_last_valid;
static int64_t s_last_valid_gps_us;
static double  s_last_valid_lat, s_last_valid_lon;

/* §9.1 latest fused sample (passed to lap_on_fix; ignored by the engine but mirrors replay) */
static fused_sample_t s_latest_fused;
static bool           s_have_fused;
static uint32_t       s_fused_ctr;

/* motion / fix-lost edges (§6.5, §9.1) */
static bool s_moving, s_have_moving;
static int  s_invalid_run;
static bool s_fix_lost;

/* IMU temperature cadence */
static int64_t s_last_temp_us;

/* per-lap statistics (§9.4), accumulated at 100 Hz, reset at each S/F crossing */
static lap_stats_t s_stats;
static int32_t     s_cur_speed_cms;        /* latest valid GPS speed */
static bool        s_stats_gps_lost;       /* current fix invalid (min_speed ignores these) */

/* completed laps for `dbg laps` (ring, newest last). F4: the pipeline task (core 1) writes s_laps[]
 * / s_lap_total while the console task (core 0) reads them in pipeline_laps_snapshot -- a seqlock
 * (s_laps_seq, odd while writing) gives the reader a torn-free, ordered copy without a spinlock. */
static lap_result_t   s_laps[PIPE_LAPS_KEEP];
static volatile uint32_t s_lap_total;
static uint32_t          s_laps_seq;       /* even = stable, odd = writer mid-update (F4 seqlock) */

/* ---------------- helpers ---------------- */

static void stats_reset(void)
{
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.min_speed_cms = 0xFFFFu;       /* sentinel: no sample yet */
}

static void stats_finalise(lap_stats_t *out)
{
    *out = s_stats;
    if (out->min_speed_cms == 0xFFFFu) out->min_speed_cms = 0;   /* never sampled -> 0 */
}

static int16_t clamp_i16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/* Broadcast one event to the logger's evt_q (§4.4; ui/power copies land with those tasks). */
static void emit_event(const event_t *ev)
{
    if (g_evt_q) { (void)xQueueSend(g_evt_q, ev, 0); logger_notify(); }
}

static void emit_simple(uint8_t type, int64_t gps_us, int64_t mono_us)
{
    event_t ev = { type, 0, 0, gps_us, mono_us, 0, 0 };
    emit_event(&ev);
}

/* Freeze the just-completed lap (lap_prev) + the accumulated stats, store + submit + print. */
static void on_lap_complete(int64_t end_gps_us)
{
    const lap_result_t *p = lap_prev(&s_lap);
    if (!p) return;
    lap_result_t lr = *p;
    stats_finalise(&lr.stats);

    /* F4 seqlock write: bump to odd, publish the slot + total, bump to even. The ACQ_REL RMWs
     * fence the plain stores between them so the reader never sees a torn lap_result_t. */
    __atomic_fetch_add(&s_laps_seq, 1u, __ATOMIC_ACQ_REL);   /* enter: seq -> odd */
    s_laps[s_lap_total % PIPE_LAPS_KEEP] = lr;
    s_lap_total++;
    __atomic_fetch_add(&s_laps_seq, 1u, __ATOMIC_ACQ_REL);   /* leave: seq -> even */

    s_resume_active = false;               /* F2: the resumed lap (if any) has completed; guard done */

    logger_submit_lap(&lr);

    ESP_LOGI(TAG, "LAP %u  %lu.%03lu s  flags=0x%02x  vmax=%u vmin=%u cm/s  leanR=%d cdeg",
             (unsigned)lr.lap_no, (unsigned long)(lr.time_ms / 1000u),
             (unsigned long)(lr.time_ms % 1000u), (unsigned)lr.flags,
             (unsigned)lr.stats.max_speed_cms, (unsigned)lr.stats.min_speed_cms,
             (int)lr.stats.max_lean_r_cdeg);

    (void)end_gps_us;
    stats_reset();                         /* §9.4: reset at each S/F crossing */
}

static void on_drag_done(void)
{
    const drag_result_t *cur = drag_current(&s_drag);
    if (!cur) return;
    logger_submit_drag(cur);
    ESP_LOGI(TAG, "DRAG run %u  gates=%u trap=%u cm/s", (unsigned)cur->run_no,
             (unsigned)cur->n_gates, (unsigned)cur->trap_cms);
}

/* Engine callback (lap_on_fix / drag_on_fused). Forward every event to evt_q, and on completion
 * hand the logger the full result. */
static void engine_cb(const event_t *ev, void *ctx)
{
    (void)ctx;
    emit_event(ev);
    switch (ev->type) {
    case EV_LAP_COMPLETE:
        on_lap_complete(ev->gps_us);
        s_rtc_save_due = true;     /* §15.3: save after on_fix, once open_lap has opened the new lap */
        break;
    case EV_SECTOR:
        ESP_LOGI(TAG, "  sector %u  split %lu ms  delta %ld ms", (unsigned)ev->arg16,
                 (unsigned long)ev->arg32, (long)(int32_t)ev->arg32b);
        s_rtc_save_due = true;     /* §15.3: the crossed sector is now the resume point */
        break;
    case EV_DRAG_DONE:    on_drag_done(); break;
    default: break;
    }
}

/* §6.5 fix validity rule, identical to replay_run.c compute_validity. Updates last-valid state. */
static bool compute_validity(const gps_fix_t *fix)
{
    bool ok = fix->fix_type == 3
           && (fix->flags & GPS_FLAG_FIXOK)
           && (fix->flags & GPS_FLAG_TIME)
           && (fix->flags & GPS_FLAG_DATE)
           && fix->gps_us > 0
           && fix->hacc_mm <= (uint32_t)(FIX_HACC_MAX_M * 1000)
           && fix->sats >= FIX_MIN_SATS
           && fix->gspeed_mms <= (int32_t)(FIX_MAX_SPEED_MPS * 1000);
    if (ok && s_have_last_valid) {
        if (fix->gps_us <= s_last_valid_gps_us) {
            ok = false;
        } else {
            double lat = (double)fix->lat_e7 / 1e7, lon = (double)fix->lon_e7 / 1e7;
            double dt_s = (double)(fix->gps_us - s_last_valid_gps_us) / 1e6;
            double d = geo_dist_m(s_last_valid_lat, s_last_valid_lon, lat, lon);
            if (d > (double)FIX_MAX_JUMP_MPS * dt_s + 20.0) ok = false;
        }
    }
    if (ok) {
        s_have_last_valid = true;
        s_last_valid_gps_us = fix->gps_us;
        s_last_valid_lat = (double)fix->lat_e7 / 1e7;
        s_last_valid_lon = (double)fix->lon_e7 / 1e7;
    }
    return ok;
}

/* §9.1 on_fix. */
static void on_fix(gps_fix_t *fix)
{
    bool valid = compute_validity(fix);
    fix->valid = valid ? 1u : 0u;

    if (valid) {
        tb_on_fix(&s_tb, fix->gps_us, fix->mono_us, 0);   /* sim: no serial transmit time */
        s_cur_speed_cms = fix->gspeed_mms / 10;
    }
    fus_set_gps_speed(&s_fus, (float)fix->gspeed_mms / 1000.0f,
                      (float)fix->head_e5 / 1e5f, fix->mono_us, valid);

    /* §15.3 resume: on the first valid fix, if a fresh RTC snapshot is armed, restore the interrupted
     * lap into LAP_RUNNING (carrying LAP_F_INTERRUPTED) before the engine sees this fix, so it
     * continues and completes normally. Runs once, lap mode only. */
    if (s_mode == MODE_LAP && s_resume_pending && valid) {
        s_resume_pending = false;
        /* F2 (issue #35): the freshness test must be two-sided. `age <= 0` means this fix PREDATES
         * the saved snapshot (a rewound clock, or a sim replay restarting the capture from t0) --
         * resuming then restores lap_start_gps_us into the future relative to incoming fixes and the
         * engine emits a wrapped-negative split. Require the fix to be strictly after the save and
         * within the window; otherwise clear the snapshot and cold-start. */
        int64_t age = fix->gps_us - s_resume.saved_gps_us;
        if (age > 0 && age < (int64_t)RTC_RESUME_MAX_S * 1000000) {
            lap_rtc_t lr = {
                .venue_id         = s_resume.venue_id,
                .layout_id        = s_resume.layout_id,
                .lap_no           = s_resume.lap_no,
                .sector_idx       = s_resume.sector_idx,
                .mode             = LAP_MODE_NORMAL,   /* a resumed lap is always a normal lap */
                .lap_start_gps_us = s_resume.lap_start_gps_us,
                .best             = s_resume.best,
                .prev             = s_resume.prev,
            };
            memcpy(lr.gate_times, s_resume.gate_times, sizeof lr.gate_times);
            if (lap_import_rtc(&s_lap, &lr) == 0) {
                s_resume_active = true;                /* F2: guard this lap against a later rewind */
                s_resumed_lap_start_us = s_resume.lap_start_gps_us;
                ESP_LOGI(TAG, "rtc resume: lap %u venue %u continued (interrupted)",
                         (unsigned)lr.lap_no, (unsigned)lr.venue_id);
            } else {
                /* Unknown venue: keep the cold engine state (sim already set the venue at init; on
                 * real hardware the engine keeps scanning for it). */
                ESP_LOGW(TAG, "rtc resume: venue %u unknown, cold start", (unsigned)lr.venue_id);
            }
        } else {
            lt_rtc_clear();
            ESP_LOGI(TAG, "rtc resume: snapshot not fresh (age %lld us), cleared", (long long)age);
        }
    }

    if (s_mode == MODE_LAP) {
        /* F2 (issue #35): while a resumed lap is running, a valid fix whose gps_us predates the
         * resumed lap's start means the clock rewound (e.g. GPS week rollover) -- feeding it to the
         * engine would produce a wrapped-negative split. Reset the resumed lap and drop the stale
         * snapshot instead; the engine re-arms and starts a clean lap on the next S/F crossing. */
        if (s_resume_active && valid && fix->gps_us < s_resumed_lap_start_us) {
            ESP_LOGW(TAG, "rtc resume: fix %lld predates resumed lap start %lld -> reset",
                     (long long)fix->gps_us, (long long)s_resumed_lap_start_us);
            lap_reset(&s_lap);
            stats_reset();
            s_resume_active = false;
            lt_rtc_clear();
        }
        lap_on_fix(&s_lap, fix, s_have_fused ? &s_latest_fused : NULL, engine_cb, NULL);
        /* §15.3 save-on-gate: if this fix crossed an S/F or sector line, snapshot the engine's
         * (now-updated) resumable state so the most-recent gate becomes the resume point after any
         * reset. On EV_LAP_COMPLETE the engine has already opened the next lap, so the export
         * captures that freshly-opened lap -- exactly the state to resume into. */
        if (s_rtc_save_due) {
            s_rtc_save_due = false;
            lap_rtc_t lr;
            lap_export_rtc(&s_lap, &lr);
            lt_rtc_save(&lr, s_session_id, fix->gps_us, s_mode,
                        0 /* power_state: no owner in plan 03 */, 0 /* partial_count */);
        }
    } else {
        drag_on_fix(&s_drag, fix);
    }

    /* §6.5 fix-lost edge: three consecutive invalid fixes -> EV_FIX_LOST; next valid -> EV_FIX_OK. */
    if (valid) {
        s_stats_gps_lost = false;
        if (s_fix_lost) { s_fix_lost = false; emit_simple(EV_FIX_OK, fix->gps_us, fix->mono_us); }
        s_invalid_run = 0;
    } else {
        s_stats_gps_lost = true;
        if (s_invalid_run < 1000) s_invalid_run++;
        if (s_invalid_run == FIX_LOST_COUNT) { s_fix_lost = true; emit_simple(EV_FIX_LOST, fix->gps_us, fix->mono_us); }
    }

    /* §9.1 motion edge (speed-based). */
    bool moving = valid && ((double)fix->gspeed_mms * MMS_TO_KMH) > (double)MOVING_SPEED_KMH;
    if (!s_have_moving || moving != s_moving) {
        s_have_moving = true;
        s_moving = moving;
        emit_simple(moving ? (uint8_t)EV_MOTION : (uint8_t)EV_STILL, fix->gps_us, fix->mono_us);
    }

    /* §4.4: push to fix_ring (drop-newest + counter) and wake the logger. */
    (void)ring_push(&g_fix_ring, fix);
    logger_notify();
}

/* §9.4 per-lap stats accumulation at 100 Hz. */
static void stats_step(const fused_sample_t *fs)
{
    /* §9.4: max ignores non-positive samples; min ignores only LAP_GPS_LOST (a true 0 counts). */
    if (s_cur_speed_cms > 0 && (uint32_t)s_cur_speed_cms > s_stats.max_speed_cms)
        s_stats.max_speed_cms = (uint16_t)(s_cur_speed_cms > 0xFFFF ? 0xFFFF : s_cur_speed_cms);
    if (!s_stats_gps_lost && s_cur_speed_cms >= 0 && (uint32_t)s_cur_speed_cms < s_stats.min_speed_cms)
        s_stats.min_speed_cms = (uint16_t)s_cur_speed_cms;
    if (fs->flags & FUS_LEAN_VALID) {
        int32_t lean_cdeg = (int32_t)(fs->lean_deg * 100.0f);
        if (lean_cdeg >= 0) { if (lean_cdeg > s_stats.max_lean_r_cdeg) s_stats.max_lean_r_cdeg = clamp_i16(lean_cdeg); }
        else { if (-lean_cdeg > s_stats.max_lean_l_cdeg) s_stats.max_lean_l_cdeg = clamp_i16(-lean_cdeg); }
    }
    int32_t glat = (int32_t)(fs->g_lat * 1000.0f);
    int32_t aglat = glat < 0 ? -glat : glat;
    if (aglat > s_stats.max_glat_e3) s_stats.max_glat_e3 = clamp_i16(aglat);
    int32_t glon = (int32_t)(fs->g_lon * 1000.0f);
    if (glon > 0) { if (glon > s_stats.max_gacc_e3) s_stats.max_gacc_e3 = clamp_i16(glon); }
    else { if (-glon > s_stats.max_gbrake_e3) s_stats.max_gbrake_e3 = clamp_i16(-glon); }
}

/* §9.1 on_raw. */
static void on_raw(const imu_raw_t *raw)
{
    fused_sample_t fused;
    fus_step(&s_fus, raw, &fused);
    fused.gps_us = tb_mono_to_gps(&s_tb, fused.mono_us);

    s_latest_fused = fused;
    s_have_fused = true;

    if (s_mode == MODE_DRAG) drag_on_fused(&s_drag, &fused, engine_cb, NULL);

    stats_step(&fused);

    if (++s_fused_ctr >= (uint32_t)FUSED_DECIM) {   /* every FUSION_HZ/CFG_FUSED_LOG_HZ samples */
        s_fused_ctr = 0;
        (void)ring_push(&g_fused_ring, &fused);     /* overwrite-oldest */
        logger_notify();
    }
}

static void handle_cmd(const command_t *cmd)
{
    switch (cmd->type) {
    case CMD_SET_MODE:
        s_mode = (cmd->arg8 == MODE_DRAG) ? MODE_DRAG : MODE_LAP;
        stats_reset();
        break;
    case CMD_SET_LAYOUT:
        lap_force_layout(&s_lap, cmd->arg16);
        break;
    case CMD_RESET_ENGINE:
        lap_reset(&s_lap);
        drag_reset(&s_drag);
        stats_reset();
        break;
    case CMD_IMU_MODE:
        (void)imu_set_mode(cmd->arg8);
        break;
    case CMD_GPS_POWER:
        (void)gps_set_power_mode(cmd->arg8 ? GPS_PM_FULL : GPS_PM_BACKUP);
        break;
    default:
        break;                              /* MARK_GATE / CALIB_ORIENT / CONFIG_RELOAD: later sessions */
    }
}

static void pipeline_init(void)
{
    const gps_profile_t *prof = NULL;

    tb_init(&s_tb);
    fus_init(&s_fus, NULL, (uint8_t)(CFG_VARIANT_MOTO ? 1 : 0));
    lap_init(&s_lap, NULL);
    drag_init(&s_drag, NULL);
    s_mode = MODE_LAP;
    stats_reset();
    s_last_temp_us = esp_timer_get_time();

    /* §15.3 RTC continuity: derive a boot-scoped session identity for the snapshots (diagnostic
     * only -- not consumed by the resume path), then arm resume. app_main leaves a VALID snapshot
     * in place at boot step 4; if one is present, keep it and import on the first valid fix.
     * Anything else -> start clean. */
    trk_init();   /* clear the user track store before the sim venue is registered (§15.3 resume needs trk_get) */
    (void)snprintf(s_session_id, sizeof s_session_id, "S%05u", (unsigned)(lt_nvs_boot_get() & 0xFFFFu));
    if (lt_rtc_validate(&s_resume) == RTC_VALID) {
        s_resume_pending = true;
        ESP_LOGI(TAG, "rtc resume armed: lap %u venue %u saved@%lld us",
                 (unsigned)s_resume.lap_no, (unsigned)s_resume.venue_id,
                 (long long)s_resume.saved_gps_us);
    } else {
        lt_rtc_clear();
        s_resume_pending = false;
    }

#if CFG_GPS_SIM
    /* Set the venue exactly as replay does: parse the sim capture's venue JSON with trk_from_json,
     * then lap_set_venue. Same JSON + same parser => the device venue == the replay venue, so the
     * lap engine starts ARMED against the identical S/F line. */
    {
        char err[96];
        const char *vj = gps_sim_venue_json();
        if (vj && trk_from_json(&s_venue, vj, strlen(vj), err, sizeof err) == 0) {
            lap_set_venue(&s_lap, &s_venue);
            (void)trk_user_add(&s_venue);   /* §15.3: make trk_get(venue_id) resolve so a resumed lap can rebuild this venue */
            uint16_t layout_id = (s_venue.n_layouts > 0) ? s_venue.layouts[0].id : 0;
            /* open a logging session for the run + write the real VENUE record. */
            log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = MODE_LAP,
                                  .venue_id = s_venue.id, .layout_id = layout_id, .gps_us = 0 };
            if (g_log_req_q) { (void)xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100)); logger_notify(); }
            logger_set_venue(s_venue.id, layout_id, s_venue.name);
            ESP_LOGI(TAG, "venue \"%s\" (id %u) armed from sim capture", s_venue.name, (unsigned)s_venue.id);
        } else {
            ESP_LOGW(TAG, "sim venue parse failed: %s", err);
        }
    }
#endif

    if (gps_init(&prof) == 0 && prof) {
        (void)gps_configure(prof->max_rate_hz);
        ESP_LOGI(TAG, "gps \"%s\" %u Hz", prof->name ? prof->name : "?", (unsigned)prof->max_rate_hz);
    } else {
        ESP_LOGW(TAG, "gps_init failed");
    }

    if (imu_init() == 0) {
        uint8_t mask = 0;
        if (imu_self_test(&mask) != 0) ESP_LOGW(TAG, "imu self-test fail (mask 0x%02x)", mask);
        (void)imu_set_mode(IMU_FULL);
    } else {
        ESP_LOGW(TAG, "imu_init failed");
    }
}

static void pipeline_task(void *arg)
{
    (void)arg;
    sup_register_task(HB_PIPELINE, xTaskGetCurrentTaskHandle(), PIPE_STALL_S);
    pipeline_init();
    ESP_LOGI(TAG, "pipeline up (core %d prio %d)", PIPE_CORE, PIPE_PRIO);

    static imu_raw_t raw[IMU_BATCH];       /* off-stack: 16 * 20 B */
    for (;;) {
        /* Pace at ~50 ms, waking early on a command (§9.1 queue-set behaviour without a UART/timer
         * object -- the sim has no UART events and imu_sim is polled). */
        command_t cmd;
        if (xQueueReceive(g_cmd_q, &cmd, pdMS_TO_TICKS(PIPE_PERIOD_MS)) == pdTRUE) {
            handle_cmd(&cmd);
            while (xQueueReceive(g_cmd_q, &cmd, 0) == pdTRUE) handle_cmd(&cmd);
        }

        /* GPS: drain every fix due, run on_fix. */
        gps_fix_t fix;
        int r;
        while ((r = gps_poll(&fix)) == 1) on_fix(&fix);

        /* IMU: read the samples due since the last read, run on_raw for each. */
        int64_t now = esp_timer_get_time();
        size_t n_read = 0;
        if (imu_read_fifo(raw, IMU_BATCH, &n_read, now) == 0)
            for (size_t i = 0; i < n_read; i++) on_raw(&raw[i]);

        /* ~1 Hz IMU temperature into fusion (§9.2). */
        if (now - s_last_temp_us >= TEMP_POLL_US) {
            int16_t tc = 0;
            if (imu_read_temp_c100(&tc) == 0) fus_set_temp(&s_fus, tc);
            s_last_temp_us = now;
        }

        g_hb[HB_PIPELINE]++;
    }
}

void pipeline_start(void)
{
    if (s_task) return;
    s_task = xTaskCreateStaticPinnedToCore(pipeline_task, "pipeline", PIPE_STACK_WORDS, NULL,
                                           PIPE_PRIO, s_stack, &s_tcb, PIPE_CORE);
}

int pipeline_laps_snapshot(lap_result_t *out, int max)
{
    if (!out || max <= 0) return 0;
    /* F4 seqlock read: copy under an even, unchanged sequence; retry if the writer was mid-update
     * (odd) or ran during the copy. The writer's section is a single struct copy, so this converges
     * immediately; diagnostic-only, so an unbounded retry is acceptable. */
    int n = 0;
    uint32_t seq0 = 0, seq1 = 0;
    do {
        seq0 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
        if (seq0 & 1u) continue;                     /* writer mid-update */
        uint32_t total = s_lap_total;
        n = (total < (uint32_t)PIPE_LAPS_KEEP) ? (int)total : PIPE_LAPS_KEEP;
        if (n > max) n = max;
        uint32_t start = (total > (uint32_t)n) ? total - (uint32_t)n : 0u;
        for (int i = 0; i < n; i++) out[i] = s_laps[(start + (uint32_t)i) % PIPE_LAPS_KEEP];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);     /* copy above happens-before re-reading seq */
        seq1 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
    } while ((seq0 & 1u) || seq0 != seq1);
    return n;
}
