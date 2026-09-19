/* lt_sys.c -- shared cross-task runtime state (spec §4.4, §17.4).
 *
 * Defines hb[] (heartbeats), sys_flags (atomic fault bits), and btn_q (button ISR -> ui).
 * Kept in its own translation unit so it exists from the app-skeleton task onward, before the
 * supervisor and NVS layer land. Static allocation only (no malloc, §17.9).
 */
#include <stdatomic.h>

#include "app/lt_sup.h"
#include "app/lt_assert.h"

/* Shared with lt_ipc.c (components/app/sys): both are IPC-adjacent cross-task plumbing with no
 * module of their own to name a code after. */
#define IPC_ASSERT_CODE 0x0B80

volatile uint32_t g_hb[HB_COUNT];

static _Atomic uint32_t s_sys_flags;

uint32_t sys_flags_get(void) { return atomic_load(&s_sys_flags); }

void sys_flags_set(uint8_t bit)
{
    /* bit indexes a single-bit shift into a u32; a caller passing an out-of-range SYS_* constant
     * would shift by >= its width, which is undefined behaviour, not a normal fault flag. */
    LT_ASSERT_VOID(bit < 32, IPC_ASSERT_CODE);
    atomic_fetch_or(&s_sys_flags, (uint32_t)1u << bit);
}

void sys_flags_clear(uint8_t bit)
{
    LT_ASSERT_VOID(bit < 32, IPC_ASSERT_CODE);
    atomic_fetch_and(&s_sys_flags, ~((uint32_t)1u << bit));
}

QueueHandle_t g_btn_q;
static StaticQueue_t s_btn_q_ctrl;
static uint8_t       s_btn_q_store[8 * sizeof(button_evt_t)];   /* depth 8 (§4.4) */

void lt_queues_init(void)
{
    if (!g_btn_q) {
        g_btn_q = xQueueCreateStatic(8, sizeof(button_evt_t), s_btn_q_store, &s_btn_q_ctrl);
    }
    /* Postcondition: static queue creation over our own fixed-size s_btn_q_store must succeed. */
    LT_ASSERT_VOID(g_btn_q != NULL, IPC_ASSERT_CODE);
}
