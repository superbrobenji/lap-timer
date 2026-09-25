/* status.c -- §18.2 STATUS record builder (Plan 5.6 T1 fix 1). See app/status.h: storage-free by
 * design so it is safe to call from any task. free_kb/sessions live here as a small single-word
 * atomic cache (mirrors app/sys/lt_sys.c's s_sys_flags) that the logger task -- the storage
 * owner -- refreshes via status_cache_update(); status_build() itself never touches
 * hal/storage.h.
 */
#include "app/status.h"

#include "app/lt_assert.h"
#include "app/lt_sup.h"      /* sys_flags_get */

#include "build_config.h"    /* CFG_FW_VERSION */

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#define STATUS_ASSERT_CODE 0x0BA0   /* Power of 10 rule 5 (app/lt_assert.h); status.c's own code */

/* Relaxed, not the default seq_cst (which costs an extra `memw` fence on xtensa per access):
 * each word is independent (no ordering relation between s_free_kb/s_sessions or to anything
 * else the reader/writer touch), the writer is always the logger task (status_cache_update()),
 * and a reader on any task just wants the latest whole u32/u16 -- a plain relaxed load/store on
 * a single word can never tear and needs no fence to be correct here. */
static _Atomic uint32_t s_free_kb;
static _Atomic uint16_t s_sessions;

static void put_u16le(uint8_t *p, uint16_t v)
{
    LT_ASSERT_VOID(p != NULL, STATUS_ASSERT_CODE);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put_u32le(uint8_t *p, uint32_t v)
{
    LT_ASSERT_VOID(p != NULL, STATUS_ASSERT_CODE);
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* fw char[7]: the git version trimmed of a leading 'v', truncated to fit 7 bytes incl. NUL.
 * (moved from cmd.c's op_status, Plan 5.6 T1 fix 1 -- byte-identical behavior.) */
static void fw_short(char *dst, size_t cap)
{
    LT_ASSERT_VOID(dst != NULL, STATUS_ASSERT_CODE);
    LT_ASSERT_VOID(cap > 0, STATUS_ASSERT_CODE);   /* dst[i] = '\0' below would write out of bounds at cap == 0 */
    const char *v = CFG_FW_VERSION;
    if (*v == 'v' || *v == 'V') v++;
    size_t i = 0;
    for (; i + 1 < cap && v[i]; i++) dst[i] = v[i];
    dst[i] = '\0';
}

void status_cache_update(uint32_t free_kb, uint16_t sessions)
{
    atomic_store_explicit(&s_free_kb, free_kb, memory_order_relaxed);
    atomic_store_explicit(&s_sessions, sessions, memory_order_relaxed);
}

/* Builds the §18.2 STATUS record. Shared by the framed `status` reply (cmd.c's op_status) and
 * the 1 Hz stream push (pipeline.c, Plan 5.6) so the two can never drift. Storage-free: free_kb/
 * sessions come from the atomic cache above, never from a live hal/storage.h call. */
void status_build(uint8_t out[LT_STATUS_LEN])
{
    LT_ASSERT_VOID(out != NULL, STATUS_ASSERT_CODE);
    memset(out, 0, LT_STATUS_LEN);
    out[LT_ST_OFF_PROTO] = 1;
    out[LT_ST_OFF_STATE] = 0;                              /* device state machine lands later */
    put_u16le(&out[LT_ST_OFF_FLAGS], (uint16_t)(sys_flags_get() & 0xFFFFu));
    out[LT_ST_OFF_BATT_PCT] = 0;                           /* power lands in Plan 6 */
    put_u16le(&out[LT_ST_OFF_BATT_MV], 0);
    put_u32le(&out[LT_ST_OFF_FREE_KB], atomic_load_explicit(&s_free_kb, memory_order_relaxed));
    put_u16le(&out[LT_ST_OFF_SESS], atomic_load_explicit(&s_sessions, memory_order_relaxed));
    fw_short((char *)&out[LT_ST_OFF_FW], 7);
}
