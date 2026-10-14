#include <stddef.h>
#include "app/log_pause.h"
#include "core/core.h"

#define LOG_PAUSE_ASSERT_CODE 0x0B21   /* P10 rule 5; logger.c's 0x0B20 + 1 (this file's own code) */

void log_pause_init(log_pause_t *p)
{
    CORE_ASSERT_VOID(p != NULL, LOG_PAUSE_ASSERT_CODE);
    p->still_since_ms = 0u;
    p->still  = false;
    p->paused = false;
}

void log_pause_still(log_pause_t *p, uint32_t now_ms)
{
    CORE_ASSERT_VOID(p != NULL, LOG_PAUSE_ASSERT_CODE);
    if (p->still) return;                    /* already counting down: a repeat edge changes nothing */
    p->still = true;
    p->still_since_ms = now_ms;
}

bool log_pause_motion(log_pause_t *p)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    bool was_paused = p->paused;
    p->still  = false;
    p->paused = false;
    return was_paused;                       /* §12.6: the caller re-keys so a FIX_KEY resumes */
}

bool log_pause_tick(log_pause_t *p, uint32_t now_ms)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    if (!p->still || p->paused) return false;
    /* Unsigned difference: correct across the 32-bit ms wrap (~49.7 days of uptime). */
    if ((uint32_t)(now_ms - p->still_since_ms) < LOG_PAUSE_STILL_MS) return false;
    p->paused = true;
    return true;
}

bool log_pause_active(const log_pause_t *p)
{
    CORE_ASSERT_RET(p != NULL, LOG_PAUSE_ASSERT_CODE, false);
    return p->paused;
}
