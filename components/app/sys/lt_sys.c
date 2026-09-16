/* lt_sys.c -- shared cross-task runtime state (spec §4.4, §17.4).
 *
 * Defines hb[] (heartbeats), sys_flags (atomic fault bits), and btn_q (button ISR -> ui).
 * Kept in its own translation unit so it exists from the app-skeleton task onward, before the
 * supervisor and NVS layer land. Static allocation only (no malloc, §17.9).
 */
#include <stdatomic.h>

#include "app/lt_sup.h"

volatile uint32_t g_hb[HB_COUNT];

static _Atomic uint32_t s_sys_flags;

uint32_t sys_flags_get(void) { return atomic_load(&s_sys_flags); }
void     sys_flags_set(uint8_t bit) { atomic_fetch_or(&s_sys_flags, (uint32_t)1u << bit); }
void     sys_flags_clear(uint8_t bit) { atomic_fetch_and(&s_sys_flags, ~((uint32_t)1u << bit)); }

QueueHandle_t g_btn_q;
static StaticQueue_t s_btn_q_ctrl;
static uint8_t       s_btn_q_store[8 * sizeof(button_evt_t)];   /* depth 8 (§4.4) */

void lt_queues_init(void)
{
    if (!g_btn_q) {
        g_btn_q = xQueueCreateStatic(8, sizeof(button_evt_t), s_btn_q_store, &s_btn_q_ctrl);
    }
}
