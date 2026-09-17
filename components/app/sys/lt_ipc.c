/* lt_ipc.c -- definitions + static storage for the §4.4 pipeline<->logger channels. */
#include "app/lt_ipc.h"

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
}
