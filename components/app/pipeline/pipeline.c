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
#include "app/lt_err.h"      /* E_STO_WRITE -- M1/M2 (review fix round 1, #97): errlog a dropped/failed save */
#include "app/lt_assert.h"
#include "app/link.h"        /* stream_push -- fan the fused-log/event stream to an attached peer (§18) */
#include "app/lt_proto.h"    /* LT_REC_STATUS / LT_STATUS_REC_LEN -- STATUS stream-record wire contract */
#include "app/status.h"      /* status_build() -- storage-free (Plan 5.6 T1 fix 1) */

#include "hal/gps.h"
#include "hal/imu.h"

#include "core/cfg.h"       /* cfg_t / cfg_defaults / CFG_MODE_* -- pipeline reads cfg.mode (T-D) */
#include "core/consts.h"
#include "core/drag.h"
#include "core/event.h"
#include "core/fus.h"
#include "core/geo.h"
#include "core/lap.h"
#include "core/ses.h"      /* SES_T_FUSED / SES_T_EVENT -- stream record type codes */
#include "core/tb.h"
#include "core/trk.h"
#include "core/types.h"

#include "build_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/lt_consts.h"   /* RTC_RESUME_MAX_S (§15.3 / Appendix A) */

static const char *TAG = "pipe";

/* Power-of-10 rule 5 assertion code for the pipeline module (design §3): the app assert hook
 * records this code plus __FILE__/__LINE__, pinning the exact failing check. */
#define PIPE_ASSERT_CODE 0x0B10

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
/* M3 (review fix round 1, #97 §10.9): s_last_fix is never invalidated on its own -- without this,
 * a GPS dropout would leave the last valid fix sitting there indefinitely, letting a MARK_GATE
 * press place a gate at a position from minutes ago. CMD_MARK_GATE requires s_last_fix to be both
 * valid AND younger than this. */
#define CREATE_MARK_FRESH_US (2LL * 1000000)

/* Rule 2 explicit static loop bounds: drains that were textually unbounded while(...) loops backed
 * by a "the queue/driver is finite" argument in a comment only. Each cap is far above the worst
 * real backlog (cmd/btn queue depths are single digits; GPS is <= ~10 fixes/s at a 50 ms period),
 * so the cap is never reached in normal operation -- it only bounds a pathological runaway. */
#define PIPE_CMD_DRAIN_MAX  64
#define PIPE_FIX_DRAIN_MAX  64
#define PIPE_LAPS_SNAP_RETRY_MAX 1024   /* seqlock reader retries; single writer converges in ~1 */

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
/* sim_scenario_set() (CMD_SIM_SCENARIO, Task 3: dbg sim drag|laps|park) and gps_sim_rearm()'s
 * prototype (declared there -- every sim_scenario_set() caller must call it right after, so
 * sim_scenario.h itself carries the contract; see that header). */
#include "sim_scenario.h"
/* Fix round 1, M2: this is the one file that includes both app/lt_ipc.h (unconditionally, above)
 * and sim_scenario.h (here) -- the only place sim_scenario.h's driver-local SIM_SCENARIO_* mirror
 * and lt_ipc.h's canonical SIM_SC_* can ever drift apart unnoticed. Catch that at compile time.
 * Cast to (int): GCC's -Wenum-compare (-Werror here) flags a direct comparison between two
 * different anonymous enum types even though both are just int constants. */
_Static_assert((int)SIM_SC_LAPS == (int)SIM_SCENARIO_LAPS &&
               (int)SIM_SC_DRAG == (int)SIM_SCENARIO_DRAG &&
               (int)SIM_SC_PARK == (int)SIM_SCENARIO_PARK,
               "lt_ipc.h SIM_SC_* and sim_scenario.h SIM_SCENARIO_* must stay numerically identical");
/* No local trk_venue_t static here (Plan 7 Task 1 DRAM reclaim): pipeline_init below parses the
 * sim capture's venue JSON straight into the trk user table via trk_user_add_json, so the venue
 * lives in exactly one place -- the table entry lap_set_venue then points at. */
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

/* Plan 5.6 §4.1 T1 fix 1: LT_REC_STATUS push cadence, driven off the decimated fused-push site
 * below (this task, g_stream_ring's one documented producer -- never link_task, which would
 * race it on the SPSC ring). s_status_tick counts fused pushes (CFG_FUSED_LOG_HZ of them make
 * ~1 s); s_prev_present detects the absent->present edge for an immediate first push. */
static uint8_t s_status_tick;
static bool    s_prev_present;

/* motion / fix-lost edges (§6.5, §9.1) */
static bool s_moving, s_have_moving;

/* §19.4 OTA-validate gate: set once the pipeline has ingested at least one GPS fix (set-once,
 * monotonic). The supervisor reads it via pipeline_gps_seen() before marking a pending image valid. */
static volatile bool s_gps_seen;
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
 * (s_laps_seq, odd while writing) gives the reader a torn-free, ordered copy without a spinlock.
 *
 * Plan 7c T3 (design §2): the SAME seqlock now also guards s_best (best-known sector splits +
 * theoretical lap, app/pipeline.h pipe_best_t) and s_dragsnap (the drag run in progress/last
 * frozen + session-best per gate, pipe_drag_t) -- both are written only by this task, only inside
 * a seq_enter()/seq_leave() section, and read by pipeline_best_snapshot()/pipeline_drag_snapshot()
 * via the shared snap_read() below. */
static lap_result_t   s_laps[PIPE_LAPS_KEEP];
static volatile uint32_t s_lap_total;
static uint32_t          s_laps_seq;       /* even = stable, odd = writer mid-update (F4 seqlock) */
static pipe_best_t       s_best;
static pipe_drag_t       s_dragsnap;

/* §10.9 on-device track creation (#97): CMD_CREATE_BEGIN/CMD_MARK_GATE/CMD_CREATE_CANCEL state.
 * s_create_next_gate is the AUTHORITATIVE next gate index -- the ui mirrors its own copy (for the
 * arg8 it sends) only as a display aid; a dropped or duplicate CMD_MARK_GATE can never desync the
 * engine because this task always marks the gate it itself expects next, never trusting cmd->arg8.
 * s_last_fix is the latest VALID fix (copied in on_fix() below, the one allowed extra static of
 * this task, 56 B -- CMD_MARK_GATE carries no fix of its own, so lap_mark_gate() needs this). */
static bool      s_create_active;
static uint8_t   s_create_next_gate;
static gps_fix_t s_last_fix;

/* ---------------- helpers ---------------- */

static void stats_reset(void)
{
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.min_speed_cms = 0xFFFFu;       /* sentinel: no sample yet */
}

static void stats_finalise(lap_stats_t *out)
{
    LT_ASSERT_VOID(out != NULL, PIPE_ASSERT_CODE);
    *out = s_stats;
    if (out->min_speed_cms == 0xFFFFu) out->min_speed_cms = 0;   /* never sampled -> 0 */
    LT_ASSERT_VOID(out->min_speed_cms != 0xFFFFu, PIPE_ASSERT_CODE);   /* sentinel resolved */
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
    LT_ASSERT_VOID(ev != NULL, PIPE_ASSERT_CODE);            /* copied into the queues by value */
    LT_ASSERT_VOID(ev->type <= EV_FAULT, PIPE_ASSERT_CODE);  /* only stable §4.5 codes are broadcast */
    if (g_evt_q) { (void)xQueueSend(g_evt_q, ev, 0); logger_notify(); }
    if (g_ui_evt_q) (void)xQueueSend(g_ui_evt_q, ev, 0);   /* fan-out to the ui task (drop-newest on full) */
    /* §18: fan the event out to an attached peer as a live stream record (drop-on-full,
     * non-blocking -- never stalls this task). rec[0] = §14 type, rest = the raw event_t. */
    uint8_t rec[1 + sizeof(event_t)];
    rec[0] = SES_T_EVENT;
    memcpy(rec + 1, ev, sizeof *ev);
    stream_push(rec, sizeof rec);
}

static void emit_simple(uint8_t type, int64_t gps_us, int64_t mono_us)
{
    LT_ASSERT_VOID(type != EV_NONE && type <= EV_FAULT, PIPE_ASSERT_CODE);   /* a real §4.5 event code */
    LT_ASSERT_VOID(gps_us >= 0 && mono_us >= 0, PIPE_ASSERT_CODE);           /* epoch/mono timestamps */
    event_t ev = { type, 0, 0, gps_us, mono_us, 0, 0 };
    emit_event(&ev);
}

/* F4 seqlock write-section helpers (Plan 7c T3, ruling R-2): bump s_laps_seq to odd on entry --
 * after asserting no writer is already mid-update, since only the pipeline task ever writes it --
 * and back to even on leave. Shared by every writer of s_laps[]/s_best/s_dragsnap; the ACQ_REL
 * RMWs fence the plain stores between the two calls so a reader never sees a torn record. */
static void seq_enter(void)
{
    LT_ASSERT_VOID((__atomic_load_n(&s_laps_seq, __ATOMIC_RELAXED) & 1u) == 0u, PIPE_ASSERT_CODE);
    __atomic_fetch_add(&s_laps_seq, 1u, __ATOMIC_ACQ_REL);   /* enter: seq -> odd */
}
static void seq_leave(void)
{
    __atomic_fetch_add(&s_laps_seq, 1u, __ATOMIC_ACQ_REL);   /* leave: seq -> even */
    LT_ASSERT_VOID((__atomic_load_n(&s_laps_seq, __ATOMIC_RELAXED) & 1u) == 0u, PIPE_ASSERT_CODE); /* left cleanly */
}

/* Freeze the just-completed lap (lap_prev) + the accumulated stats, store + submit + print. */
static void on_lap_complete(int64_t end_gps_us)
{
    const lap_result_t *p = lap_prev(&s_lap);
    if (!p) return;
    lap_result_t lr = *p;
    stats_finalise(&lr.stats);
    /* M6 (final review): computed BEFORE seq_enter() -- lap_theoretical_best_ms() walks the
     * engine's sector table and has no reason to run while s_laps_seq is held odd, extending the
     * critical section every reader might have to retry against for no benefit. */
    uint32_t theo = lap_theoretical_best_ms(&s_lap);

    /* F4 seqlock write: publish the lap slot + total and (Plan 7c T3, design §2) s_best -- the
     * engine's own best-sector table mirrored verbatim (it only updates on a valid lap, so a copy
     * here needs no extra validity check) plus the derived theoretical best. One section, one
     * struct-sized critical region, so a concurrent reader converges in ~1 retry. */
    seq_enter();
    s_laps[s_lap_total % PIPE_LAPS_KEEP] = lr;
    s_lap_total++;
    LT_ASSERT_VOID(sizeof s_best.best_sector_ms == sizeof s_lap.best_sector_ms, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(sizeof s_best.have_best_sector == sizeof s_lap.have_best_sector, PIPE_ASSERT_CODE);
    memcpy(s_best.best_sector_ms, s_lap.best_sector_ms, sizeof s_best.best_sector_ms);
    memcpy(s_best.have_best_sector, s_lap.have_best_sector, sizeof s_best.have_best_sector);
    s_best.n_sectors = s_lap.best_sector_count;
    s_best.theo_ms   = theo;
    seq_leave();

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
    LT_ASSERT_VOID(cur->n_gates <= DRAG_MAX_GATES, PIPE_ASSERT_CODE);   /* fits gates[]/the log record */
    LT_ASSERT_VOID(cur->t0_gps_us >= 0, PIPE_ASSERT_CODE);             /* launch instant is a valid epoch us */
    logger_submit_drag(cur);
    ESP_LOGI(TAG, "DRAG run %u  gates=%u trap=%u cm/s", (unsigned)cur->run_no,
             (unsigned)cur->n_gates, (unsigned)cur->trap_cms);
}

/* Refreshes s_dragsnap (Plan 7c T3, design §2 follow-up; ruling R-2): called once per pipeline
 * step that produced at least one drag event -- on_raw's drag-event forwarding loop below, which
 * covers ARMED/LAUNCH/GATE/DONE uniformly rather than one publish site per event kind. Builds the
 * whole snapshot in a local first (drag_current/drag_best only ever run on this same task, so
 * there is no concurrency hazard reading D->best here) and publishes it as a single struct copy
 * under one seqlock section, same pattern as on_lap_complete. */
static void publish_drag_snapshot(void)
{
    LT_ASSERT_VOID(s_drag.cfg.n_gates <= DRAG_MAX_GATES, PIPE_ASSERT_CODE);   /* fits cfg.gates[]/the snapshot */
    pipe_drag_t snap;
    memset(&snap, 0, sizeof snap);
    const drag_result_t *cur = drag_current(&s_drag);
    if (cur) snap.current = *cur;   /* else stays zeroed: n_gates 0 == "no run yet" */
    for (uint8_t j = 0; j < s_drag.cfg.n_gates; j++) {
        uint8_t id = s_drag.cfg.gates[j].id;
        LT_ASSERT_VOID(id >= 1u && id <= DRAG_MAX_GATES, PIPE_ASSERT_CODE);   /* 1-based, indexes best_time_ms[id-1] */
        const drag_result_t *br = drag_best(&s_drag, id);
        if (!br) continue;          /* gate never hit this session: have_best[id-1] stays false */
        /* cfg.gates[]/best.gates[] are laid out in the same order at init (drag.c init_best) and
         * never reordered after, so index j names the same gate in both -- verified, not assumed. */
        LT_ASSERT_VOID(br->gates[j].gate_id == id, PIPE_ASSERT_CODE);
        bool brake = (s_drag.cfg.gates[j].kind == DRAG_BRAKE);   /* §11.3: BRAKE's "best" is the shortest dist_cm */
        snap.best_time_ms[id - 1] = brake ? br->gates[j].dist_cm : br->gates[j].time_ms;
        snap.have_best[id - 1]    = true;
    }
    seq_enter();
    s_dragsnap = snap;
    seq_leave();
}

/* Engine callback (lap_on_fix / drag_on_fused). Forward every event to evt_q, and on completion
 * hand the logger the full result.
 *
 * Plan 7c T3 fix 1 (review finding 1): EV_LAP_COMPLETE publishes s_best/s_laps[] under the F4
 * seqlock in on_lap_complete() -- that publish must land BEFORE emit_event() puts this same event
 * on g_ui_evt_q, or the ui task, reacting to the very event it just dequeued, could read the
 * previous lap's data instead of this one's (on the dual-core target, the event and the seqlock
 * write are otherwise ordered only by which runs first on this task -- there is no other fence).
 * Running on_lap_complete() first makes that structural: whichever task next reads s_best/s_laps[]
 * after dequeuing this event is guaranteed to see this lap's publish, not a stale one. Every other
 * event type keeps the original emit-then-handle order -- EV_SECTOR/EV_DRAG_DONE's handlers don't
 * publish anything a same-event consumer reads back (on_drag_done() only hands the result to the
 * logger via its own queue; s_dragsnap is published separately by publish_drag_snapshot(), fix 2). */
/* #97 (§10.9 step 3): the engine finishes CREATE on its own -- finalize_create() (lap.c) calls
 * trk_user_add() and THEN emits this same EV_VENUE_FOUND, both on this task, sequentially, before
 * engine_cb ever sees the event (ruling T5-R0: trk.c's writer-ordering contract), so the user
 * track table already holds the new venue by the time this runs. Ask the logger to persist it so
 * it survives a reboot; fire-and-forget (requester NULL), same pdMS_TO_TICKS(100) queue-send
 * pattern as the CFG_GPS_SIM venue registration's own LOGGER_OPEN_SESSION request in
 * pipeline_init() below (M1, review fix round 1: was a non-blocking 0-tick send that silently
 * dropped the just-created venue on a momentarily-full queue, with only an ESP_LOGW to say so --
 * now also errlog_add()'d, so the drop leaves a trace the device itself keeps). A normal
 * (non-CREATE) venue scan also emits EV_VENUE_FOUND, but s_create_active is false then (only
 * CMD_CREATE_BEGIN sets it), so this is a no-op outside creation. */
static void create_finish_if_active(void)
{
    if (!s_create_active) return;
    s_create_active = false;
    log_request_t req = { .type = LOGGER_SAVE_TRACKS };
    if (g_log_req_q != NULL && xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
        logger_notify();
    } else {
        (void)errlog_add(E_STO_WRITE, 0);   /* M1: the save never ran -- leave a trace, not just a log line */
        ESP_LOGW(TAG, "create: save-tracks request dropped (queue full)");
    }
}

static void engine_cb(const event_t *ev)
{
    LT_ASSERT_VOID(ev != NULL, PIPE_ASSERT_CODE);            /* engine must pass a real event */
    LT_ASSERT_VOID(ev->type <= EV_FAULT, PIPE_ASSERT_CODE);  /* stable §4.5 code, drives the switch */
    if (ev->type == EV_LAP_COMPLETE) {
        on_lap_complete(ev->gps_us);
        s_rtc_save_due = true;     /* §15.3: save after on_fix, once open_lap has opened the new lap */
    }
    emit_event(ev);
    switch (ev->type) {
    case EV_SECTOR:
        LT_ASSERT_VOID(ev->arg16 <= LAP_MAX_SECTORS, PIPE_ASSERT_CODE);   /* engine sector idx in range */
        ESP_LOGI(TAG, "  sector %u  split %lu ms  delta %ld ms", (unsigned)ev->arg16,
                 (unsigned long)ev->arg32, (long)(int32_t)ev->arg32b);
        s_rtc_save_due = true;     /* §15.3: the crossed sector is now the resume point */
        break;
    case EV_DRAG_DONE:     on_drag_done(); break;
    case EV_VENUE_FOUND:   create_finish_if_active(); break;   /* #97: persist a just-created venue */
    default: break;               /* EV_LAP_COMPLETE handled above, before emit_event() */
    }
}

/* §6.5 fix validity rule, identical to replay_run.c compute_validity. Updates last-valid state. */
static bool compute_validity(const gps_fix_t *fix)
{
    LT_ASSERT_RET(fix != NULL, PIPE_ASSERT_CODE, false);   /* the §6.5 rule dereferences it throughout */
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
            LT_ASSERT_RET(d >= 0.0, PIPE_ASSERT_CODE, false);   /* a distance is never negative */
            if (d > (double)FIX_MAX_JUMP_MPS * dt_s + 20.0) ok = false;
        }
    }
    if (ok) {
        /* §6.5 monotonicity: a still-valid fix with a prior valid fix must be strictly newer (the
         * gps_us <= last check above forced ok=false otherwise) -- guards the last-valid update. */
        LT_ASSERT_RET(!s_have_last_valid || fix->gps_us > s_last_valid_gps_us, PIPE_ASSERT_CODE, ok);
        s_have_last_valid = true;
        s_last_valid_gps_us = fix->gps_us;
        s_last_valid_lat = (double)fix->lat_e7 / 1e7;
        s_last_valid_lon = (double)fix->lon_e7 / 1e7;
    }
    return ok;
}

/* §15.3 resume: on the first valid fix, if a fresh RTC snapshot is armed, restore the interrupted
 * lap into LAP_RUNNING (carrying LAP_F_INTERRUPTED) before the engine sees this fix, so it
 * continues and completes normally. Runs once, lap mode only. (Split verbatim out of on_fix for
 * rule 4; the RTC resume semantics are unchanged -- only a non-NULL param precondition is added.) */
static void on_fix_try_resume(const gps_fix_t *fix, bool valid)
{
    LT_ASSERT_VOID(fix != NULL, PIPE_ASSERT_CODE);
    if (s_mode == MODE_LAP && s_resume_pending && valid) {
        s_resume_pending = false;
        /* An armed resume snapshot was validated (magic+version+CRC) at init and every save writes
         * a valid fix's gps_us (> 0, §6.5), so a zero save-stamp here is a corrupt/foreign snapshot,
         * not a real resume point -- the two-sided freshness test below would misread it. */
        LT_ASSERT_VOID(s_resume.saved_gps_us != 0, PIPE_ASSERT_CODE);
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
}

/* I2 (review fix round 1, #97 §10.9): the engine can leave CREATE mode entirely on its own, with
 * NO event at all -- finalize_create()'s full-table-or-invalid abort (lap.c) calls
 * reset_to_no_venue(), dropping L->mode back to LAP_MODE_NORMAL without ever posting
 * EV_VENUE_FOUND. Without this, s_create_active would stay true forever: the NEW TRACK one-shot
 * has no revert timer, and every further MODE press just re-refuses (lap_mark_gate's own
 * mode != LAP_MODE_CREATE early return) until a long MODE finally escapes. Called once per
 * lap_on_fix() dispatch batch (on_fix_run_engine, below -- the one call site that can change
 * L->mode out from under CREATE): a successful finish would already have cleared
 * s_create_active (create_finish_if_active(), above, runs inside the very engine_cb() loop this
 * follows, same dispatch batch), so reaching here with s_create_active still true means the
 * engine left CREATE silently. Tell the ui so the screen does not strand the user on it.
 * CMD_RESET_ENGINE/CMD_SET_MODE/a mode-changing CMD_CONFIG_RELOAD cover the other three ways the
 * engine can leave CREATE (cancel_active_create(), below -- those three never call lap_on_fix()
 * first, so this check alone would never see them: CMD_SET_MODE -> DRAG in particular means
 * lap_on_fix() is never called again at all). ui_post_create() is defined below (after
 * pipeline_reload_cfg()); forward-declared here since this runs well before it textually. */
static void ui_post_create(uint8_t phase, uint16_t arg);
static void create_cancel_if_orphaned(void)
{
    if (s_create_active && s_lap.mode != LAP_MODE_CREATE) {
        s_create_active = false;
        ui_post_create(EV_CREATE_CANCELLED, 0);
    }
}

/* Feed the fix to the active engine in the exact §9.1 order, with the §15.3 rewind guard and
 * save-on-gate around the lap engine. (Split verbatim out of on_fix for rule 4.) */
static void on_fix_run_engine(const gps_fix_t *fix, bool valid)
{
    LT_ASSERT_VOID(fix != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);   /* valid engine mode */
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
        event_t evs[LAP_EVT_MAX];
        int nev = 0;
        lap_on_fix(&s_lap, fix, s_have_fused ? &s_latest_fused : NULL, evs, LAP_EVT_MAX, &nev);
        for (int i = 0; i < nev; i++) engine_cb(&evs[i]);
        create_cancel_if_orphaned();   /* I2: catch a silent CREATE abort this dispatch batch may have caused */
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
}

/* §9.1 on_fix. */
static void on_fix(gps_fix_t *fix)
{
    LT_ASSERT_VOID(fix != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);   /* valid engine mode */
    bool valid = compute_validity(fix);
    fix->valid = valid ? 1u : 0u;

    if (valid) {
        tb_on_fix(&s_tb, fix->gps_us, fix->mono_us, 0);   /* sim: no serial transmit time */
        s_cur_speed_cms = fix->gspeed_mms / 10;
        s_last_fix = *fix;   /* §10.9: CMD_MARK_GATE's fix (lap_mark_gate needs a valid, current one) */
    }
    fus_set_gps_speed(&s_fus, (float)fix->gspeed_mms / 1000.0f,
                      (float)fix->head_e5 / 1e5f, fix->mono_us, valid);

    on_fix_try_resume(fix, valid);
    on_fix_run_engine(fix, valid);

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
    LT_ASSERT_VOID(fs != NULL, PIPE_ASSERT_CODE);   /* every field below is read from *fs */
    /* Fusion output is clamped finite (§9.3 G_MAX / lean clamp); the int32 conversions below would
     * be undefined on a NaN/inf, so a non-finite sample here is a fusion bug, not valid data. */
    LT_ASSERT_VOID(isfinite(fs->lean_deg), PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(isfinite(fs->g_lat), PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(isfinite(fs->g_lon), PIPE_ASSERT_CODE);
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
    LT_ASSERT_VOID(raw != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(raw->mono_us >= 0, PIPE_ASSERT_CODE);   /* esp_timer sample stamp is monotonic (§9.1) */
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);   /* valid engine mode */
    fused_sample_t fused;
    fus_step(&s_fus, raw, &fused);
    fused.gps_us = tb_mono_to_gps(&s_tb, fused.mono_us);

    s_latest_fused = fused;
    s_have_fused = true;

    if (s_mode == MODE_DRAG) {
        event_t evs[DRAG_EVT_MAX];
        int nev = 0;
        drag_on_fused(&s_drag, &fused, evs, DRAG_EVT_MAX, &nev);
        /* Plan 7c T3 fix 1 (review finding 2): publish s_dragsnap BEFORE forwarding this step's
         * events -- same argument as fix 1 above (engine_cb's emit_event enqueues onto
         * g_ui_evt_q), and safe to do here because drag_on_fused() has already fully settled
         * D->cur/D->best for every event in evs[] by the time it returns: enter_done() calls
         * update_best(D) before its own emit() (drag.c ~433-434), and step_done()'s brake_step()
         * -> update_best() (drag.c ~601) both run synchronously inside this same drag_on_fused()
         * call, well before any event reaches a queue. One call here still covers ARMED/LAUNCH/
         * GATE/DONE uniformly (>= 1 event this step), same as before. */
        if (nev > 0) publish_drag_snapshot();
        for (int i = 0; i < nev; i++) engine_cb(&evs[i]);
    }

    stats_step(&fused);

    if (++s_fused_ctr >= (uint32_t)FUSED_DECIM) {   /* every FUSION_HZ/CFG_FUSED_LOG_HZ samples */
        s_fused_ctr = 0;
        (void)ring_push(&g_fused_ring, &fused);     /* overwrite-oldest */
        logger_notify();
        /* §18: stream the decimated fused sample (at cfg.log_fused_hz) to an attached peer. */
        uint8_t rec[1 + sizeof(fused_sample_t)];
        rec[0] = SES_T_FUSED;
        memcpy(rec + 1, &fused, sizeof fused);
        stream_push(rec, sizeof rec);

        /* Plan 5.6 §4.1 T1 fix 1: push LT_REC_STATUS at ~1 Hz (once every CFG_FUSED_LOG_HZ fused
         * pushes -- this call site runs at CFG_FUSED_LOG_HZ, so counting to it is ~1 s) and
         * immediately on the absent->present edge, from THIS task -- g_stream_ring's one
         * documented producer (never link_task, which would race it: the ring is SPSC).
         * status_build() (app/status.h) is storage-free, so no I/O runs on the pipeline task. */
        bool present = link_peer_present();
        bool edge = present && !s_prev_present;
        s_prev_present = present;
        if (present && (edge || ++s_status_tick >= CFG_FUSED_LOG_HZ)) {
            s_status_tick = 0;
            uint8_t srec[LT_STATUS_REC_LEN];
            srec[0] = LT_REC_STATUS;
            status_build(&srec[1]);
            stream_push(srec, sizeof srec);
        }
        LT_ASSERT_VOID(s_status_tick < (uint8_t)CFG_FUSED_LOG_HZ, PIPE_ASSERT_CODE);   /* tick resets before reaching cadence */
    }
    LT_ASSERT_VOID(s_fused_ctr < (uint32_t)FUSED_DECIM, PIPE_ASSERT_CODE);   /* decimation counter wrapped */
}

/* #97 (§10.9), review fix round 1 (I2): cancel_active_create() is defined below (after
 * ui_post_create()); forward-declared here for pipeline_reload_cfg()'s mode-changed branch. */
static void cancel_active_create(void);

/* #87: re-read the persisted cfg and rebuild what depends on it -- the drag engine's gate table
 * (a units change redefines the mph gates: drag_init() is a fresh engine, session bests are
 * dropped, documented in spec dsB §2) and the riding mode. The lap engine is untouched. */
static void pipeline_reload_cfg(void)
{
    cfg_t      cfg;
    drag_cfg_t dc;
    cfg_defaults(&cfg);
    (void)lt_cfg_load(&cfg);
    drag_cfg_from_user(&cfg, &dc);
    drag_init(&s_drag, &dc);
    uint8_t new_mode = (cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    /* fix round 1 (Important finding 1): CMD_SET_MODE resets stats on a mode flip (handle_cmd
     * above) -- a reload that silently changes s_mode must do the same, else a mode change
     * arriving mid-lap carries the old window's stats into the next lap. An unchanged mode (the
     * common case -- most CONFIG_SET calls touch units/display, not mode) leaves stats alone.
     * I2 (review fix round 1, #97): and must not strand an in-progress creation either. */
    if (new_mode != s_mode) { stats_reset(); cancel_active_create(); }
    s_mode = new_mode;
    publish_drag_snapshot();
    LT_ASSERT_VOID(s_drag.cfg.n_gates <= DRAG_MAX_GATES, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);
    ESP_LOGI(TAG, "config reloaded (units %u, mode %u)", (unsigned)cfg.units, (unsigned)s_mode);
}

/* #87: tell the ui directly (never emit_event(): EV_LAP_RESET is ui-only, not logged/streamed)
 * that a remote CMD_RESET_ENGINE just cleared the engines, so its running-lap clock stops too. */
static void ui_post_lap_reset(void)
{
    LT_ASSERT_VOID(g_ui_evt_q != NULL, PIPE_ASSERT_CODE);
    event_t ev = { .type = EV_LAP_RESET, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "lap reset: ui queue full");
}

/* #97 (§10.9): tell the ui directly (never emit_event(): EV_CREATE is ui-only, not logged/
 * streamed, same reason as EV_LAP_RESET above) that CREATE-mode state changed -- BEGUN/GATE_SET/
 * FAILED/CANCELLED. The engine's own finish (the closing S/F crossing) is a real EV_VENUE_FOUND,
 * handled by engine_cb()/emit_event() below, not this helper. */
static void ui_post_create(uint8_t phase, uint16_t arg)
{
    LT_ASSERT_VOID(g_ui_evt_q != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(phase <= EV_CREATE_CANCELLED, PIPE_ASSERT_CODE);   /* a real EV_CREATE_* phase */
    event_t ev = { .type = EV_CREATE, .flags = phase, .arg16 = arg, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "create: ui queue full");
}

/* I2 (review fix round 1, #97 §10.9): cancel an in-progress on-device creation and tell the ui --
 * used by every command path that can take the engine out of CREATE mode WITHOUT the engine's own
 * normal exits (a finished EV_VENUE_FOUND, create_finish_if_active(); or the user's own
 * CMD_CREATE_CANCEL, handle_create_cmd()): CMD_RESET_ENGINE, CMD_SET_MODE, and a
 * CMD_CONFIG_RELOAD that flips the mode. create_cancel_if_orphaned() (above) catches the fourth
 * way (the engine leaving CREATE on its own, e.g. finalize_create()'s full-table abort) -- that
 * one only runs after a lap_on_fix() dispatch, so it can never see CMD_SET_MODE -> DRAG (no more
 * lap_on_fix() calls at all once in DRAG) or the synchronous CMD_RESET_ENGINE/CONFIG_RELOAD
 * commands, which is exactly why those three call this directly instead. A no-op when no
 * creation is active. */
static void cancel_active_create(void)
{
    if (!s_create_active) return;
    lap_create_cancel(&s_lap);
    s_create_active = false;
    ui_post_create(EV_CREATE_CANCELLED, 0);
}

/* CMD_RESET_ENGINE's body, split out of handle_cmd's switch for RULE-4-COMPOUND (the #87 ui-post
 * line pushed that switch over the 30-code-line compound-statement cap). M3 (final review, ruling
 * R-9, pipeline part): a dev-console reset must not leave the ui showing a stale best-sector/
 * theoretical-best (from before the reset) or a stale drag "current run" snapshot -- clear s_best
 * (same seqlock pattern as elsewhere) and republish s_dragsnap so its `current` reflects
 * drag_reset()'s fresh IDLE state (drag_reset() itself intentionally keeps the session-best-per-
 * gate table, core/drag.h's documented contract -- publish_drag_snapshot() carries that forward
 * unchanged, only `current` actually changes). #87: ui_post_lap_reset() additionally tells the ui
 * directly so its running-lap clock stops too. */
static void handle_reset_engine(void)
{
    cancel_active_create();   /* I2: a reset must not strand an in-progress creation */
    lap_reset(&s_lap);
    drag_reset(&s_drag);
    stats_reset();
    seq_enter();
    memset(&s_best, 0, sizeof s_best);
    seq_leave();
    publish_drag_snapshot();
    ui_post_lap_reset();
}

/* CMD_SIM_SCENARIO's body (Task 3: `dbg sim drag | laps <n> | park`), split out of handle_cmd's
 * switch for RULE-4-COMPOUND, the same reason CMD_RESET_ENGINE's body was (#87, above). Sim build
 * only: hands the scenario straight to sim_scenario_set(), then rearms the gps_sim driver so the
 * new scenario re-anchors (s_idx/s_started/s_parked reset) on its very next gps_poll(). A non-sim
 * build has neither sim_common's state nor the gps_sim driver linked in, so it just logs. */
#if CFG_GPS_SIM
static void handle_sim_scenario(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    sim_scenario_set(cmd->arg8, cmd->arg16, esp_timer_get_time());
    gps_sim_rearm();
    ESP_LOGI(TAG, "sim scenario %u (laps %u)", (unsigned)cmd->arg8, (unsigned)cmd->arg16);
}
#else
static void handle_sim_scenario(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    ESP_LOGW(TAG, "sim scenario: not a sim build");
}
#endif

/* CMD_SET_LAYOUT's body, split out of handle_cmd's switch for RULE-4-COMPOUND (#97 below pushed
 * the switch over the 30-code-line compound cap; same reason CMD_RESET_ENGINE/CMD_SIM_SCENARIO's
 * bodies were split out, above). */
static void handle_set_layout(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    lap_force_layout(&s_lap, cmd->arg16);
    /* M3 (final review, ruling R-9, pipeline part): a forced layout invalidates the previous
     * layout's best sector splits/theoretical best, same as the venue-change clear in
     * pipeline_init() below -- clear s_best under the same seqlock every other writer uses. */
    seq_enter();
    memset(&s_best, 0, sizeof s_best);
    seq_leave();
}

/* CMD_SET_MODE's body, split out of handle_cmd's switch (I2, review fix round 1, #97): a mode
 * switch must not strand an in-progress creation (CMD_SET_MODE -> DRAG in particular means
 * lap_on_fix() -- and so create_cancel_if_orphaned() -- is never called again at all). */
static void handle_set_mode(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    cancel_active_create();
    s_mode = (cmd->arg8 == MODE_DRAG) ? MODE_DRAG : MODE_LAP;
    stats_reset();
}

/* CMD_CREATE_BEGIN/CMD_CREATE_CANCEL/CMD_MARK_GATE bodies (#97, §10.9), split out of handle_cmd's
 * switch for RULE-4-COMPOUND, the same reason CMD_RESET_ENGINE/CMD_SIM_SCENARIO's bodies were
 * (#87/Task 3, above). s_create_next_gate (declared above) is this task's own authoritative
 * counter -- CMD_MARK_GATE never trusts cmd->arg8 (the ui's own mirror, sent only for its log
 * line), it always marks the gate THIS task expects next, so a dropped/duplicate command can
 * never desync the two. */
static void handle_create_cmd(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(cmd->type == CMD_CREATE_BEGIN || cmd->type == CMD_CREATE_CANCEL ||
                   cmd->type == CMD_MARK_GATE, PIPE_ASSERT_CODE);
    if (cmd->type == CMD_CREATE_BEGIN) {
        lap_create_begin(&s_lap);
        s_create_active     = true;
        s_create_next_gate  = 0;
        ui_post_create(EV_CREATE_BEGUN, 0);
        return;
    }
    if (cmd->type == CMD_CREATE_CANCEL) {
        cancel_active_create();
        return;
    }
    /* CMD_MARK_GATE: a no-op outside CREATE mode (e.g. a stale/duplicate press after CANCEL).
     * M3 (review fix round 1): s_last_fix is never invalidated on its own, so also require it to
     * be recent (CREATE_MARK_FRESH_US) as well as valid -- a stale fix (GPS dropout) is refused
     * the same as no fix at all, rather than placing a gate at a position from minutes ago. At
     * boot s_last_fix is zeroed (mono_us == 0), so this is always stale until the first valid fix
     * arrives, correctly refusing a press before then.
     * M5: lap_mark_gate() exposes no reason for a refusal, but s_create_next_gate (this task's own
     * authoritative counter) already tells full-table apart from everything else: idx beyond
     * LAP_MAX_SECTORS can only mean every sector is already marked (lap_mark_gate's own
     * n_sectors >= LAP_MAX_SECTORS check, lap.c) -- "out of order" is structurally unreachable
     * here since this always marks the gate it itself expects next. */
    if (!s_create_active) return;
    uint8_t idx   = s_create_next_gate;
    bool    fresh = s_last_fix.valid != 0 &&
                    (esp_timer_get_time() - s_last_fix.mono_us) < CREATE_MARK_FRESH_US;
    if (fresh && lap_mark_gate(&s_lap, idx, &s_last_fix, NULL) == 0) {
        s_create_next_gate = (uint8_t)(idx + 1u);
        ui_post_create(EV_CREATE_GATE_SET, idx);
    } else {
        uint8_t reason = (idx > (uint8_t)LAP_MAX_SECTORS) ? EV_CREATE_FAIL_FULL : EV_CREATE_FAIL_NOFIX;
        ui_post_create(EV_CREATE_FAILED, reason);
    }
}

static void handle_cmd(const command_t *cmd)
{
    LT_ASSERT_VOID(cmd != NULL, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(cmd->type <= CMD_TYPE_LAST, PIPE_ASSERT_CODE);   /* valid §4.4 command type (M1) */
    switch (cmd->type) {
    case CMD_SET_MODE:
        handle_set_mode(cmd);
        break;
    case CMD_SET_LAYOUT:
        handle_set_layout(cmd);
        break;
    case CMD_RESET_ENGINE:
        handle_reset_engine();
        break;
    case CMD_CONFIG_RELOAD:
        pipeline_reload_cfg();               /* #87: a peer's CONFIG_SET persisted a new cfg */
        break;
    case CMD_IMU_MODE:
        (void)imu_set_mode(cmd->arg8);
        break;
    case CMD_GPS_POWER:
        (void)gps_set_power_mode(cmd->arg8 ? GPS_PM_FULL : GPS_PM_BACKUP);
        break;
    case CMD_SIM_SCENARIO:
        handle_sim_scenario(cmd);
        break;
    case CMD_MARK_GATE:
    case CMD_CREATE_BEGIN:
    case CMD_CREATE_CANCEL:
        handle_create_cmd(cmd);
        break;
    default:
        break;                              /* CALIB_ORIENT: later sessions */
    }
}

/* GPS/IMU driver bring-up + the BOOT screen's self-test report for both (Plan 7c T8, design §6).
 * Split out of pipeline_init() below to keep that function under RULE-4's 60-code-line cap. */
static void pipeline_init_drivers(void)
{
    const gps_profile_t *prof = NULL;
    bool                  gps_ok = (gps_init(&prof) == 0 && prof);
    if (gps_ok) {
        (void)gps_configure(prof->max_rate_hz);
        ESP_LOGI(TAG, "gps \"%s\" %u Hz", prof->name ? prof->name : "?", (unsigned)prof->max_rate_hz);
    } else {
        ESP_LOGW(TAG, "gps_init failed");
    }
    /* Plan 7c T8: the sim driver always "succeeds" at gps_init() -- report SIM so the BOOT screen
     * shows which driver is actually wired, not just that init passed. */
    sup_boot_report(BOOT_GPS, CFG_GPS_SIM ? BOOT_SIM : (gps_ok ? BOOT_OK : BOOT_FAIL));

    bool imu_ok = (imu_init() == 0);
    if (imu_ok) {
        uint8_t mask = 0;
        if (imu_self_test(&mask) != 0) ESP_LOGW(TAG, "imu self-test fail (mask 0x%02x)", mask);
        (void)imu_set_mode(IMU_FULL);
    } else {
        ESP_LOGW(TAG, "imu_init failed");
    }
    sup_boot_report(BOOT_IMU, CFG_IMU_SIM ? BOOT_SIM : (imu_ok ? BOOT_OK : BOOT_FAIL));
}

static void pipeline_init(void)
{
    tb_init(&s_tb);
    fus_init(&s_fus, NULL, (uint8_t)(CFG_VARIANT_MOTO ? 1 : 0));
    lap_init(&s_lap, NULL);
    /* Plan 7c T2 fix 1: one NVS cfg load feeds both consumers below -- the drag engine's
     * user-derived gate table (benches/units/rollout) and cfg.mode, the single source of truth
     * for the operating mode (T-D). Same load-with-fallback pattern as ui.c/cmd.c; defaults on a
     * missing/invalid blob (lt_cfg_load leaves cfg untouched on failure). */
    cfg_t cfg;   /* pipeline task stack: cfg_t is a few hundred bytes; pipeline has ~4.8 KB free */
    cfg_defaults(&cfg);
    (void)lt_cfg_load(&cfg);
    {
        drag_cfg_t dc;
        drag_cfg_from_user(&cfg, &dc);
        drag_init(&s_drag, &dc);
    }
    /* The ui seeds its own s_mode from cfg.mode and persists a menu toggle there; the pipeline
     * reads the same field here so screen and engine agree at boot (previously this hard-coded
     * MODE_LAP, so a persisted DRAG cfg ran the lap engine until the first menu toggle inverted
     * both). */
    s_mode = (cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);   /* valid engine mode from cfg */
    stats_reset();
    s_last_temp_us = esp_timer_get_time();
    LT_ASSERT_VOID(s_last_temp_us >= 0, PIPE_ASSERT_CODE);   /* esp_timer base is monotonic/non-negative */

    /* §15.3 RTC continuity: derive a boot-scoped session identity for the snapshots (diagnostic
     * only -- not consumed by the resume path), then arm resume. app_main leaves a VALID snapshot
     * in place at boot step 4; if one is present, keep it and import on the first valid fix.
     * Anything else -> start clean.
     *
     * #97 (§10.9), review fix round 1 (T5-R2): the user track table itself is no longer touched
     * here at all. main/app_main.c calls trk_init() once, right before logger_start(), and the
     * logger loads any persisted venues (/tracks/user.bin) directly inside its own boot init
     * (logger_task(), before its task loop) -- both run, in that order, before this task (or any
     * task lower-priority than the logger) can even be created; see core/trk.h's ownership
     * comment for the full ordering argument. By the time pipeline_init() runs, the table already
     * holds whatever survived the reboot, and the CFG_GPS_SIM block below (which registers the
     * sim capture's own venue, always id TRK_USER_ID_BASE) runs against it unchanged -- see that
     * block's own comment for why registering into an already-populated table is still correct. */
    int id_len = snprintf(s_session_id, sizeof s_session_id, "S%05u", (unsigned)(lt_nvs_boot_get() & 0xFFFFu));
    LT_ASSERT_VOID(id_len > 0 && (size_t)id_len < sizeof s_session_id, PIPE_ASSERT_CODE);   /* fit, not truncated */
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
    /* Set the venue exactly as replay does: parse the sim capture's venue JSON straight into the
     * trk user table (trk_user_add_json -- same trk_from_json parser under the hood), then
     * lap_set_venue against that table entry (§15.3: this also makes trk_get(venue_id) resolve, so
     * a resumed lap can rebuild this venue). Same JSON + same parser => the device venue == the
     * replay venue, so the lap engine starts ARMED against the identical S/F line. Parsing into the
     * table -- rather than a local trk_venue_t -- means lap_set_venue's stored pointer (lap.h)
     * points at stable, permanent storage, never a stack/static temporary (Plan 7 Task 1).
     *
     * #97 (§10.9), review fix round 1 (T5-R2, "confirm the sim venue registration cannot
     * duplicate an id the boot load already placed"): confirmed -- no guard needed. The sim
     * capture's venue id is always TRK_USER_ID_BASE (1000, sim_capture.h), the exact id an
     * on-device-created venue would also land at on its FIRST creation (trk_next_user_id()
     * returns TRK_USER_ID_BASE when the table is empty). If a previous boot's persisted table
     * already holds id 1000 (loaded above, before this runs), trk_user_add_json() -> trk_user_add()
     * takes its existing same-id "replace" branch (trk.c) and overwrites that slot in place --
     * user_n is unchanged, no duplicate, and every OTHER loaded venue (any created at id 1001+)
     * is untouched. Skipping this call when trk_get(TRK_USER_ID_BASE) already exists was
     * considered and rejected: the call does far more than add a trk_user_add -- lap_set_venue(),
     * the s_best clear, and the LOGGER_OPEN_SESSION request below all need to run every boot
     * regardless of whether the venue row already existed, so skipping based on existence would
     * silently drop those. */
    {
        const char *vj = gps_sim_venue_json();
        uint16_t vid = 0; char err[96];
        if (vj && trk_user_add_json(vj, strlen(vj), &vid, err, sizeof err) == 0) {
            const trk_venue_t *v = trk_get(vid);
            LT_ASSERT_VOID(v != NULL, PIPE_ASSERT_CODE);
            LT_ASSERT_VOID(v->n_layouts <= TRK_MAX_LAYOUTS, PIPE_ASSERT_CODE);   /* indexes layouts[] */
            lap_set_venue(&s_lap, v);
            /* Plan 7c T3 (design §2): a venue/layout change invalidates the previous layout's best
             * sector splits/theoretical best -- clear s_best under the same seqlock every other
             * writer of it uses. M3 (final review) note: this is the CFG_GPS_SIM-only venue path;
             * when Plan 8 adds runtime (real-GPS-driven) venue detection, that code path must clear
             * s_best here too, the same way -- it is not covered by this #if block. */
            seq_enter();
            memset(&s_best, 0, sizeof s_best);
            seq_leave();
            uint16_t layout_id = (v->n_layouts > 0) ? v->layouts[0].id : 0;
            /* open a logging session for the run + write the real VENUE record. */
            log_request_t req = { .type = LOGGER_OPEN_SESSION, .mode = MODE_LAP,
                                  .venue_id = v->id, .layout_id = layout_id, .gps_us = 0 };
            if (g_log_req_q) { (void)xQueueSend(g_log_req_q, &req, pdMS_TO_TICKS(100)); logger_notify(); }
            logger_set_venue(v->id, layout_id, v->name);
            ESP_LOGI(TAG, "venue \"%s\" (id %u) armed from sim capture", v->name, (unsigned)v->id);
        } else {
            ESP_LOGW(TAG, "sim venue parse failed: %s", err);
        }
    }
#endif

    pipeline_init_drivers();
}

static void pipeline_task(void *arg)
{
    (void)arg;
    sup_register_task(HB_PIPELINE, xTaskGetCurrentTaskHandle(), PIPE_STALL_S);
    pipeline_init();
    LT_ASSERT_VOID(g_cmd_q != NULL, PIPE_ASSERT_CODE);   /* §4.7 lt_ipc_init must precede the task */
    ESP_LOGI(TAG, "pipeline up (core %d prio %d)", PIPE_CORE, PIPE_PRIO);

    static imu_raw_t raw[IMU_BATCH];       /* off-stack: 16 * 20 B */
    for (;;) {
        /* Pace at ~50 ms, waking early on a command (§9.1 queue-set behaviour without a UART/timer
         * object -- the sim has no UART events and imu_sim is polled). */
        command_t cmd;
        if (xQueueReceive(g_cmd_q, &cmd, pdMS_TO_TICKS(PIPE_PERIOD_MS)) == pdTRUE) {
            handle_cmd(&cmd);
            /* rule 2: bounded drain of the rest (queue depth is single digits << the cap). */
            for (int i = 0; i < PIPE_CMD_DRAIN_MAX && xQueueReceive(g_cmd_q, &cmd, 0) == pdTRUE; i++)
                handle_cmd(&cmd);
        }

        /* GPS: drain every fix due, run on_fix (rule 2: bounded; ~1 fix/period << the cap). */
        gps_fix_t fix;
        for (int i = 0; i < PIPE_FIX_DRAIN_MAX && gps_poll(&fix) == 1; i++) { on_fix(&fix); s_gps_seen = true; }

        /* IMU: read the samples due since the last read, run on_raw for each. */
        int64_t now = esp_timer_get_time();
        LT_ASSERT_VOID(now >= 0, PIPE_ASSERT_CODE);   /* esp_timer is monotonic; drives temp cadence */
        size_t n_read = 0;
        if (imu_read_fifo(raw, IMU_BATCH, &n_read, now) == 0) {
            LT_ASSERT_VOID(n_read <= IMU_BATCH, PIPE_ASSERT_CODE);   /* driver must fit raw[IMU_BATCH] */
            for (size_t i = 0; i < n_read; i++) on_raw(&raw[i]);
        }

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
    LT_ASSERT_VOID(s_task != NULL, PIPE_ASSERT_CODE);   /* static creation only fails on bad params */
}

int pipeline_laps_snapshot(lap_result_t *out, int max)
{
    if (!out || max <= 0) return 0;
    /* F4 seqlock read: copy under an even, unchanged sequence; retry if the writer was mid-update
     * (odd) or ran during the copy. The writer's section is a single struct copy, so this converges
     * in ~1 iteration. Rule 2: the retry is explicitly capped (a stable read is reached long before
     * the cap; it only bounds a pathological writer storm). */
    int n = 0;
    uint32_t seq0 = 0, seq1 = 0;
    bool stable = false;
    for (int attempt = 0; attempt < PIPE_LAPS_SNAP_RETRY_MAX && !stable; attempt++) {
        seq0 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
        if (seq0 & 1u) continue;                     /* writer mid-update */
        uint32_t total = s_lap_total;
        n = (total < (uint32_t)PIPE_LAPS_KEEP) ? (int)total : PIPE_LAPS_KEEP;
        if (n > max) n = max;
        uint32_t start = (total > (uint32_t)n) ? total - (uint32_t)n : 0u;
        for (int i = 0; i < n; i++) out[i] = s_laps[(start + (uint32_t)i) % PIPE_LAPS_KEEP];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);     /* copy above happens-before re-reading seq */
        seq1 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
        stable = !((seq0 & 1u) || seq0 != seq1);
    }
    LT_ASSERT_RET(stable, PIPE_ASSERT_CODE, n);   /* the retry cap is never reached in practice */
    return n;
}

int pipeline_lap_count(void)
{
    /* Same count pipeline_laps_snapshot would return with an unbounded max: the ring keeps the
     * newest PIPE_LAPS_KEEP. s_lap_total only grows; an aligned read is a fine loop bound. */
    uint32_t total = s_lap_total;
    return (total < (uint32_t)PIPE_LAPS_KEEP) ? (int)total : PIPE_LAPS_KEEP;
}

int pipeline_lap_at(int index, lap_result_t *out)
{
    LT_ASSERT_RET(out != NULL, PIPE_ASSERT_CODE, -1);   /* caller passes its own local */
    if (index < 0) return -1;                            /* out-of-range is routine, not an anomaly */
    /* F4 seqlock read of ONE lap, in the same newest-last order pipeline_laps_snapshot yields at
     * out[index]: retry while the writer is mid-update (odd) or ran during the read. */
    bool stable = false;
    int rc = -1;
    for (int attempt = 0; attempt < PIPE_LAPS_SNAP_RETRY_MAX && !stable; attempt++) {
        uint32_t seq0 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
        if (seq0 & 1u) continue;                         /* writer mid-update */
        uint32_t total = s_lap_total;
        int n = (total < (uint32_t)PIPE_LAPS_KEEP) ? (int)total : PIPE_LAPS_KEEP;
        if (index < n) {
            uint32_t start = (total > (uint32_t)n) ? total - (uint32_t)n : 0u;
            *out = s_laps[(start + (uint32_t)index) % PIPE_LAPS_KEEP];
            rc = 0;
        } else {
            rc = -1;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);         /* read above happens-before re-reading seq */
        stable = (seq0 == __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE));
    }
    /* M1 (final review): on the (never-in-practice) retry-cap exhaustion, fail loudly with -1, not
     * rc's last, possibly-torn copy -- rc could read 0 (success) from an iteration whose seq check
     * afterwards found it unstable. The caller already treats non-zero as "leave the model alone". */
    LT_ASSERT_RET(stable, PIPE_ASSERT_CODE, -1);
    return rc;
}

enum { PIPE_SNAP_BEST = 0, PIPE_SNAP_DRAG = 1 };   /* snap_read() kind selector */

/* F4 seqlock reader shared by pipeline_best_snapshot()/pipeline_drag_snapshot() (Plan 7c T3,
 * ruling R-2): one bounded-retry loop (same shape as pipeline_lap_at above), selecting the source
 * record by `kind` via a switch rather than a function pointer (lint --enforce-fnptr forbids
 * those). `size` must equal the sizeof of the record `kind` names -- callers always pass
 * sizeof(*out), so a producer/consumer struct-size mismatch trips the assert below instead of a
 * silent short copy. */
static int snap_read(uint8_t kind, void *dst, size_t size)
{
    LT_ASSERT_RET(dst != NULL, PIPE_ASSERT_CODE, -1);
    bool stable = false;
    int  rc = -1;
    for (int attempt = 0; attempt < PIPE_LAPS_SNAP_RETRY_MAX && !stable; attempt++) {
        uint32_t seq0 = __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE);
        if (seq0 & 1u) continue;                          /* writer mid-update */
        switch (kind) {
        case PIPE_SNAP_BEST:
            LT_ASSERT_RET(size == sizeof s_best, PIPE_ASSERT_CODE, -1);
            memcpy(dst, &s_best, size);
            break;
        case PIPE_SNAP_DRAG:
            LT_ASSERT_RET(size == sizeof s_dragsnap, PIPE_ASSERT_CODE, -1);
            memcpy(dst, &s_dragsnap, size);
            break;
        default:
            LT_ASSERT_RET(false, PIPE_ASSERT_CODE, -1);   /* unreachable: only this file calls snap_read */
        }
        rc = 0;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);           /* copy above happens-before re-reading seq */
        stable = (seq0 == __atomic_load_n(&s_laps_seq, __ATOMIC_ACQUIRE));
    }
    /* M1 (final review): on the (never-in-practice) retry-cap exhaustion, fail loudly with -1, not
     * rc's last, possibly-torn copy -- rc is unconditionally set to 0 above the stability check, so
     * without this it could report success for a memcpy that just got proven torn. Both callers
     * (pipeline_best_snapshot/pipeline_drag_snapshot) already treat non-zero as "leave the model
     * alone". */
    LT_ASSERT_RET(stable, PIPE_ASSERT_CODE, -1);
    return rc;
}

int pipeline_best_snapshot(pipe_best_t *out)
{
    LT_ASSERT_RET(out != NULL, PIPE_ASSERT_CODE, -1);   /* caller passes its own local */
    return snap_read(PIPE_SNAP_BEST, out, sizeof *out);
}

int pipeline_drag_snapshot(pipe_drag_t *out)
{
    LT_ASSERT_RET(out != NULL, PIPE_ASSERT_CODE, -1);   /* caller passes its own local */
    return snap_read(PIPE_SNAP_DRAG, out, sizeof *out);
}

bool pipeline_gps_seen(void)
{
    /* §19.4 OTA-validate condition: true once at least one GPS fix has been ingested since boot
     * (set-once). Read cross-task by the supervisor; a set-once volatile bool needs no lock. */
    return s_gps_seen;
}
