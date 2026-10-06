/* app/lt_ipc.h -- pipeline<->logger channels (spec §4.4).
 *
 * The §4.4 sample rings and the logger's control/event queues. The pipeline (3.4) is the single
 * producer for the rings and the broadcaster for the event queue; the logger (3.3) is the single
 * consumer. Objects are defined in lt_ipc.c with static storage (no malloc, §17.9) and created at
 * boot step 11 by lt_ipc_init(). The rings are the core/ring.h lock-free SPSC ring.
 */
#ifndef APP_LT_IPC_H
#define APP_LT_IPC_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app/lt_proto.h"    /* LT_SESSION_ID_MAX */
#include "core/event.h"
#include "core/ring.h"
#include "core/types.h"

/* §4.4 sample rings, pipeline -> logger:
 *   fix_ring   32 x gps_fix_t      drop-newest + counter (fix loss is logged)
 *   fused_ring 64 x fused_sample_t overwrite-oldest    (sample loss is tolerable) */
#define FIX_RING_CAP   32u
#define FUSED_RING_CAP 64u
extern ring_t g_fix_ring;
extern ring_t g_fused_ring;

/* §4.4 evt_q -- the LOGGER's copy of the event broadcast (depth 16, event_t). The pipeline
 * xQueueSends each event to the ui/logger/power queues separately; this is the logger's. */
#define EVT_Q_DEPTH 16
#define UI_EVT_Q_DEPTH 16
extern QueueHandle_t g_evt_q;
/* §4.4 ui_evt_q -- pipeline fans events out to a SECOND queue for the ui task, so the ui and the
 * logger each get the full event stream (a single-consumer g_evt_q would let them steal events).
 * Drop-newest on full (ui rendering is best-effort). */
extern QueueHandle_t g_ui_evt_q;

/* §4.4 log_req_q -- power/conn -> logger control channel (depth 4, log_request_t 32 B). */
#define LOG_REQ_Q_DEPTH 4

typedef enum {
    LOGGER_OPEN_SESSION    = 0,   /* open .log + start .sum; id = boot_cnt + per-boot seq (§12.1) */
    LOGGER_CLOSE_SESSION   = 1,   /* write END, finalise .sum, close .log */
    LOGGER_REBUILD_SUMMARY = 2,   /* force a .sum rewrite now */
    LOGGER_EVICT           = 3,   /* run the §12.7 eviction check now */
    /* 4 was LOGGER_RECOUNT (re-prime the status.h cache); folded into LOGGER_DELETE_SESSION's own
     * handler when op_delete became its only sender (debt sweep A #73) -- the value is retired,
     * not reused, so a stray old build's request is never misread as something else. */
    LOGGER_DELETE_SESSION  = 5,   /* unlink .log + .sum for id, re-prime the cache (debt sweep A #73) */
    LOGGER_SAVE_TRACKS     = 6,   /* serialise the user track table into /tracks/user.bin (requester notified with rc) */
    LOGGER_LOAD_TRACKS     = 7,   /* load /tracks/user.bin into the user track table (boot; requester notified) */
} log_req_type_t;

/* debt sweep A #59/#73: a bounded request/reply. requester == NULL is fire-and-forget (today's
 * OPEN/REBUILD/EVICT callers, unchanged); non-NULL means the logger calls
 * xTaskNotify(requester, ((uint32_t)req.seq << 24) | ((uint32_t)rc & 0x00FFFFFFu),
 * eSetValueWithOverwrite) once handle_request() finishes this request (logger.c's drain loop) --
 * see logger_request_sync() below, the only intended way to set requester/seq. */
typedef struct {
    uint8_t      type;        /* log_req_type_t */
    uint8_t      mode;        /* OPEN: session mode (§12.1) */
    uint8_t      reason;      /* CLOSE: END.reason, core/ses.h SES_END_* (§12.3) */
    uint8_t      seq;         /* generation tag, set by logger_request_sync (T3 fix 1, ruling P-7) */
    uint16_t     venue_id;    /* OPEN: venue id for the .sum HDR/VENUE frame */
    uint16_t     layout_id;   /* OPEN: layout id */
    int64_t      gps_us;      /* OPEN: start_gps_us; CLOSE: END gps_us (0 if unknown) */
    TaskHandle_t requester;   /* NULL = fire-and-forget; else notified with the rc when handled */
    char         id[LT_SESSION_ID_MAX + 1];   /* DELETE_SESSION: NUL-terminated id, validated by the sender */
    uint8_t      _pad2[1];
} log_request_t;
_Static_assert(sizeof(log_request_t) == 32, "log_request_t must be 32 B (§4.4, debt sweep A)");

extern QueueHandle_t g_log_req_q;

/* Post req (requester is filled in with the calling task's own handle, seq with a fresh per-
 * request generation tag), wake the logger, and wait up to timeout_ms for its rc. Returns the
 * logger's rc (0 ok, <0 its error), -1 when the queue is full, -2 on timeout. Waits on the
 * CALLING task's own notification index 0 -- confirmed clear of the two intended callers
 * (supervisor, export_serial/console): a firmware-wide grep for task notification calls before
 * this landed found only logger.c and link.c, each notifying its OWN task (a different task from
 * either caller) -- see the debt sweep A T3 report. Never call from the ui task: it must never
 * block on the logger (every render-loop iteration matters). The pipeline task must likewise
 * never call this from its main loop -- but #97 (§10.9) adds the ONE narrow exception: a single
 * LOGGER_LOAD_TRACKS call from pipeline_init(), before pipeline_task() enters its loop (and so
 * before any g_hb[HB_PIPELINE] heartbeat is due) -- a one-time, bounded (2000 ms) boot stall, well
 * inside the supervisor's PIPE_STALL_S window, same order of cost as the driver bring-up already
 * running at that point in pipeline_init(). No other pipeline call site may use this helper.
 *
 * T3 fix 1 (ruling P-7): the generation tag in the notification's top byte is what makes this
 * helper safe for ANY caller priority or call pattern, not just a low-traffic one -- without it, a
 * reply to an EARLIER call from this same task that timed out (and so was abandoned by its own
 * logger_request_sync return) can still land on the notification word between this call's
 * xTaskNotifyStateClear and its own xTaskNotifyWait, and would otherwise be misread as this call's
 * answer. The helper discards any reply whose top byte does not match its own seq and keeps
 * waiting for the remaining time, bounded to LOG_REQ_Q_DEPTH discards (the most stale backlog
 * replies that can still be in flight ahead of this one). */
int logger_request_sync(const log_request_t *req, uint32_t timeout_ms);

/* result_q -- pipeline -> logger, full engine results (depth 4). The 3.3 logger could only build a
 * minimal LAP/DRAG_RUN from the EV_LAP_COMPLETE/EV_DRAG_DONE payload (§4.5); 3.4 hands it the whole
 * lap_result_t / drag_result_t (sectors + per-lap stats §9.4, gates) plus the real venue for the
 * VENUE record, so the logger writes complete LAP/SECTOR and DRAG_RUN/DRAG_GATE records (§12.3). A
 * queue (copy-by-value, cross-core safe) rather than log_request_t, which is no longer frozen at
 * 16 B (it grew to 32 B in debt sweep A T3, #59/#73). */
#define RESULT_Q_DEPTH 4

typedef enum {
    LOG_RES_LAP   = 0,   /* u.lap  -> LAP (+ SECTOR) records */
    LOG_RES_DRAG  = 1,   /* u.drag -> DRAG_RUN (+ DRAG_GATE) records */
    LOG_RES_VENUE = 2,   /* u.venue -> the .sum VENUE record's id/layout/name */
} log_result_kind_t;

typedef struct {
    uint8_t kind;        /* log_result_kind_t */
    union {
        lap_result_t  lap;
        drag_result_t drag;
        struct { uint16_t venue_id, layout_id; char name[24]; } venue;
    } u;
} log_result_t;

extern QueueHandle_t g_result_q;

/* §4.4 cmd_q -- ui/conn/power -> pipeline (depth 8, command_t 24 B). The producers (ui/conn/power)
 * land in later sessions; 3.4 creates the queue and the pipeline drains it (CMD_SET_MODE /
 * CMD_SET_LAYOUT / CMD_RESET_ENGINE at least). command_t is the §4.5 struct; the full command
 * protocol + transport is components/app/cmd (3.5). */
#define CMD_Q_DEPTH 8

typedef struct {
    uint8_t type;        /* command_type_t */
    uint8_t arg8;
    uint16_t arg16;
    int32_t arg32;
    double  lat;
    double  lon;
} command_t;

typedef enum {
    CMD_SET_MODE      = 0,   /* arg8 = MODE_LAP / MODE_DRAG */
    CMD_SET_LAYOUT    = 1,   /* arg16 = layout id */
    CMD_MARK_GATE     = 2,   /* arg8 = 0 S/F, n sector n */
    CMD_CALIB_ORIENT  = 3,
    CMD_RESET_ENGINE  = 4,
    CMD_CONFIG_RELOAD = 5,
    CMD_GPS_POWER     = 6,   /* arg8 = 0/1 */
    CMD_IMU_MODE      = 7,   /* arg8 = IMU_FULL / IMU_LOWPOWER */
    CMD_SIM_SCENARIO  = 8,   /* arg8 = SIM_SC_* (sim build only), arg16 = laps */
    CMD_CREATE_BEGIN  = 9,   /* menu New track: lap_create_begin */
    CMD_CREATE_CANCEL = 10,  /* long MODE on the NEW TRACK one-shot: lap_create_cancel */
} command_type_t;
/* Fix round 1, M1: the single bound every `cmd->type <= ...` check (pipeline.c's handle_cmd,
 * ui.c's ui_send_cmd) must use, instead of a literal last enumerator that silently goes stale the
 * next time a command is added. */
#define CMD_TYPE_LAST CMD_CREATE_CANCEL

enum { MODE_LAP = 0, MODE_DRAG = 1 };   /* CMD_SET_MODE arg8 (matches core CFG_MODE_*) */
/* Fix round 1, M2: CMD_SIM_SCENARIO's arg8 contract, same precedent as MODE_LAP/MODE_DRAG above.
 * components/drivers/sim_common/include/sim_scenario.h carries a driver-local mirror of these
 * three values under different names (SIM_SCENARIO_*) -- gps_sim/imu_sim are drivers `app` itself
 * depends on, so they must not #include this FreeRTOS-laden header (see sim_scenario.h for why);
 * pipeline.c _Static_asserts the two numeric sets agree. */
enum { SIM_SC_LAPS = 0, SIM_SC_DRAG = 1, SIM_SC_PARK = 2 };   /* CMD_SIM_SCENARIO arg8 (sim build only) */

extern QueueHandle_t g_cmd_q;

/* Create the rings + queues. Idempotent; call once at boot step 11 (§4.7). */
void lt_ipc_init(void);

#endif /* APP_LT_IPC_H */
