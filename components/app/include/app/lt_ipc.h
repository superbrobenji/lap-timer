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
extern QueueHandle_t g_evt_q;

/* §4.4 log_req_q -- power/conn -> logger control channel (depth 4, log_request_t 16 B). */
#define LOG_REQ_Q_DEPTH 4

typedef enum {
    LOGGER_OPEN_SESSION    = 0,   /* open .log + start .sum; id = boot_cnt + per-boot seq (§12.1) */
    LOGGER_CLOSE_SESSION   = 1,   /* write END, finalise .sum, close .log */
    LOGGER_REBUILD_SUMMARY = 2,   /* force a .sum rewrite now */
    LOGGER_EVICT           = 3,   /* run the §12.7 eviction check now */
} log_req_type_t;

typedef struct {
    uint8_t  type;        /* log_req_type_t */
    uint8_t  mode;        /* OPEN: session mode (§12.1) */
    uint8_t  reason;      /* CLOSE: END.reason (§12.3) */
    uint8_t  _pad;
    uint16_t venue_id;    /* OPEN: venue id for the .sum HDR/VENUE frame */
    uint16_t layout_id;   /* OPEN: layout id */
    int64_t  gps_us;      /* OPEN: start_gps_us; CLOSE: END gps_us (0 if unknown) */
} log_request_t;
_Static_assert(sizeof(log_request_t) == 16, "log_request_t must be 16 B (§4.4)");

extern QueueHandle_t g_log_req_q;

/* result_q -- pipeline -> logger, full engine results (depth 4). The 3.3 logger could only build a
 * minimal LAP/DRAG_RUN from the EV_LAP_COMPLETE/EV_DRAG_DONE payload (§4.5); 3.4 hands it the whole
 * lap_result_t / drag_result_t (sectors + per-lap stats §9.4, gates) plus the real venue for the
 * VENUE record, so the logger writes complete LAP/SECTOR and DRAG_RUN/DRAG_GATE records (§12.3). A
 * queue (copy-by-value, cross-core safe) rather than log_request_t, which is frozen at 16 B. */
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
} command_type_t;

enum { MODE_LAP = 0, MODE_DRAG = 1 };   /* CMD_SET_MODE arg8 (matches core CFG_MODE_*) */

extern QueueHandle_t g_cmd_q;

/* Create the rings + queues. Idempotent; call once at boot step 11 (§4.7). */
void lt_ipc_init(void);

#endif /* APP_LT_IPC_H */
