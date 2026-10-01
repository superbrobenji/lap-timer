/* lt_ipc.c -- definitions + static storage for the §4.4 pipeline<->logger channels. */
#include "app/lt_ipc.h"
#include "app/lt_assert.h"
#include "app/logger.h"      /* logger_notify() */

#include "freertos/task.h"

#define IPC_ASSERT_CODE 0x0B80

ring_t g_fix_ring;
ring_t g_fused_ring;
static gps_fix_t      s_fix_store[FIX_RING_CAP];
static fused_sample_t s_fused_store[FUSED_RING_CAP];

QueueHandle_t g_evt_q;
QueueHandle_t g_ui_evt_q;
QueueHandle_t g_log_req_q;
QueueHandle_t g_result_q;
static StaticQueue_t s_evt_ctrl;
static uint8_t       s_evt_store[EVT_Q_DEPTH * sizeof(event_t)];
static StaticQueue_t s_ui_evt_ctrl;
static uint8_t       s_ui_evt_store[UI_EVT_Q_DEPTH * sizeof(event_t)];
static StaticQueue_t s_logreq_ctrl;
static uint8_t       s_logreq_store[LOG_REQ_Q_DEPTH * sizeof(log_request_t)];
static StaticQueue_t s_result_ctrl;
static uint8_t       s_result_store[RESULT_Q_DEPTH * sizeof(log_result_t)];
QueueHandle_t g_cmd_q;
static StaticQueue_t s_cmd_ctrl;
static uint8_t       s_cmd_store[CMD_Q_DEPTH * sizeof(command_t)];

void lt_ipc_init(void)
{
    /* Rings: fix_ring drop-newest (overwrite=false), fused_ring overwrite-oldest (overwrite=true). */
    ring_init(&g_fix_ring, s_fix_store, sizeof(gps_fix_t), FIX_RING_CAP, false);
    ring_init(&g_fused_ring, s_fused_store, sizeof(fused_sample_t), FUSED_RING_CAP, true);

    if (!g_evt_q)
        g_evt_q = xQueueCreateStatic(EVT_Q_DEPTH, sizeof(event_t), s_evt_store, &s_evt_ctrl);
    if (!g_ui_evt_q)
        g_ui_evt_q = xQueueCreateStatic(UI_EVT_Q_DEPTH, sizeof(event_t), s_ui_evt_store, &s_ui_evt_ctrl);
    if (!g_log_req_q)
        g_log_req_q = xQueueCreateStatic(LOG_REQ_Q_DEPTH, sizeof(log_request_t),
                                         s_logreq_store, &s_logreq_ctrl);
    if (!g_result_q)
        g_result_q = xQueueCreateStatic(RESULT_Q_DEPTH, sizeof(log_result_t),
                                        s_result_store, &s_result_ctrl);
    if (!g_cmd_q)
        g_cmd_q = xQueueCreateStatic(CMD_Q_DEPTH, sizeof(command_t), s_cmd_store, &s_cmd_ctrl);

    /* Postconditions: each is static queue creation over our own fixed-size store/ctrl pair, which
     * must succeed (idempotent -- also holds true on a second lt_ipc_init() call, where the
     * `if (!g_x)` above is skipped and g_x already carries the first call's non-NULL handle). */
    LT_ASSERT_VOID(g_evt_q != NULL, IPC_ASSERT_CODE);
    LT_ASSERT_VOID(g_ui_evt_q != NULL, IPC_ASSERT_CODE);
    LT_ASSERT_VOID(g_log_req_q != NULL, IPC_ASSERT_CODE);
    LT_ASSERT_VOID(g_result_q != NULL, IPC_ASSERT_CODE);
    LT_ASSERT_VOID(g_cmd_q != NULL, IPC_ASSERT_CODE);
}

/* T3 fix 1 (ruling P-7): process-wide generation counter for logger_request_sync's requests.
 * Incremented atomically (not task-serialised) because callers can run on any task/priority
 * (supervisor now, export_serial/console in Task 4); the 8-bit wrap is harmless -- a stale reply
 * is only misread if a timed-out request's seq equals the CURRENT request's seq, i.e. after
 * exactly 256 later logger_request_sync calls (from ANY task) landed in between. */
static uint8_t s_gen;

/* debt sweep A #59/#73: bounded request/reply over g_log_req_q (see lt_ipc.h's doc comment for
 * the notification-index and generation-tag reasoning). Posts a copy of *req with requester
 * overwritten to the calling task's own handle and seq to a fresh generation tag, wakes the
 * logger, and blocks on this task's own notification (index 0) for the logger's rc.
 *
 * The stale-notification clear guards the CHEAP common case: a caller that reused this helper
 * after a previous call timed out, where that earlier logger reply lands before this call's own
 * xQueueSend/logger_notify below -- xTaskNotifyStateClear(NULL) drops it before it can be seen.
 * T3 fix 1 (ruling P-7): that clear alone is not enough -- the earlier reply can also land AFTER
 * the clear but BEFORE this call's own xTaskNotifyWait (e.g. the logger finally drains the old,
 * already-abandoned request while this call is still queuing/posting its own), which the clear
 * cannot see. The generation tag closes that window: the wait loop discards any notification
 * whose top byte does not match this call's own seq and keeps waiting for the remaining time,
 * bounded to LOG_REQ_Q_DEPTH discards -- the most stale backlog replies that can still be in
 * flight ahead of this one (the queue's own depth bounds how many earlier requests could still be
 * pending an answer). rc is packed into the low 24 bits (sign-preserving for this codebase's small
 * rc range) and sign-extended back out on a match. */
int logger_request_sync(const log_request_t *req, uint32_t timeout_ms)
{
    LT_ASSERT_RET(req != NULL, IPC_ASSERT_CODE, -1);
    LT_ASSERT_RET(g_log_req_q != NULL, IPC_ASSERT_CODE, -1);
    log_request_t r = *req;
    r.requester = xTaskGetCurrentTaskHandle();
    r.seq = (uint8_t)__atomic_add_fetch(&s_gen, 1, __ATOMIC_RELAXED);
    (void)xTaskNotifyStateClear(NULL);                       /* drop a stale notification from an earlier timeout */
    if (xQueueSend(g_log_req_q, &r, 0) != pdTRUE) return -1;
    logger_notify();

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    for (int i = 0; i < LOG_REQ_Q_DEPTH; i++) {
        TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - start);   /* wraparound-safe */
        if (elapsed >= timeout_ticks) return -2;
        uint32_t val = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &val, timeout_ticks - elapsed) != pdTRUE) return -2;
        if ((uint8_t)(val >> 24) == r.seq) return (int)((int32_t)(val << 8) >> 8);
        /* a stale reply for an earlier, already-timed-out request of this task: discard and keep
         * waiting -- this call's own reply has not arrived yet. */
    }
    return -2;   /* rule 2: bounded loop exhausted (LOG_REQ_Q_DEPTH stale replies) without a match */
}
