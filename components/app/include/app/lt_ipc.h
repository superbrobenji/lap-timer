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

/* Create the rings + queues. Idempotent; call once at boot step 11 (§4.7). */
void lt_ipc_init(void);

#endif /* APP_LT_IPC_H */
