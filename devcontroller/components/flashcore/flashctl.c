/* flashctl.c -- see include/flashctl.h. The exact busy/state machine and push task that used to
 * live in webapi.c as s_flash / flash_task / flash_progress (Plan 5.5 Task 6), moved behind a
 * stable API in Plan 5.6 Task 7. flashctl_get copies the whole status struct under the same
 * portMUX_TYPE spinlock the push task uses to update pct/state/result (mirrors linkhost.c's
 * s_stats_mux pattern) -- a plain struct copy across tasks would tear.
 *
 * flashctl_try_begin_staging is the one change beyond a straight move: the old code's busy check
 * (`if (s_flash.busy)`) and busy claim (`s_flash.busy = true`) were two separate steps with
 * several fallible checks in between (content-type, link status, partition lookup) -- a real
 * TOCTOU for two front ends racing this endpoint. Folding check+claim into one spinlock-protected
 * step here closes that window; the caller now claims first and releases via flashctl_end_staging
 * on any later failure. */
#include "flashctl.h"

#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"   /* esp_timer_get_time -- M7's staged_us stamp / TTL comparison */

#include "linkhost.h"

static const char *TAG = "flashctl";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Guarded by s_mux. ver/hwid/size/sha are written only by flashctl_start_push, before the task it
 * spawns exists (mirrors webapi.c's old comment: "written only while busy is true and the task
 * does not exist yet"), so the task reads them once under the lock at its start with no further
 * synchronisation needed for the rest of its run. sha is not part of the public status struct (no
 * caller has needed to read it back) so it stays a private companion to `s_flash`. */
static flashctl_status_t s_flash;
static uint8_t            s_flash_sha[32];

bool flashctl_try_begin_staging(void)
{
    bool claimed = false;
    int64_t now = esp_timer_get_time();   /* read outside the lock: a plain monotonic clock call */
    portENTER_CRITICAL(&s_mux);
    if (!s_flash.busy) {
        s_flash.busy      = true;
        s_flash.state     = FLASHCTL_STAGING;
        s_flash.staged_us = 0;            /* fresh claim: not staged (finished) yet */
        claimed = true;
    } else if (s_flash.state == FLASHCTL_STAGING && s_flash.staged_us != 0 &&
              (now - s_flash.staged_us) >= FLASHCTL_STAGE_TTL_US) {
        /* M7 (final review): a stage that finished (staged_us stamped by flashctl_end_staging(true)
         * below) but was never pushed within FLASHCTL_STAGE_TTL_US -- its owner is gone (a crashed
         * console session, an abandoned browser tab). Reclaim: reset to a fresh STAGING claim for
         * THIS caller in the SAME critical section as the staleness check, so no third caller can
         * ever observe the momentarily-freed guard and race this one for it.
         *
         * M10 (round 2): this reclaim hands the SAME (busy=true, state=STAGING) shape back out to
         * a brand-new caller while the ORIGINAL caller may still be holding a reference to this
         * claim (a console's s_staged bookkeeping, say) and can turn up later believing it still
         * owns it. That is only safe because staged_us is zeroed right here: the original caller's
         * ownership token (whatever flashctl_end_staging(true) returned to IT) was that OLD
         * staged_us value, and flashctl_start_push / flashctl_release_staged both compare their
         * caller's token against the CURRENT s_flash.staged_us inside their own critical section --
         * once this reclaim (or a later flashctl_start_push) overwrites staged_us, the stale
         * caller's token can never match again, so it can neither push nor abort the new owner's
         * claim. Reclaiming state alone, without the token check on the other end, would let a
         * stale caller do exactly that (the defect M10 fixes). */
        s_flash.busy      = true;
        s_flash.state     = FLASHCTL_STAGING;
        s_flash.staged_us = 0;
        claimed = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return claimed;
}

uint64_t flashctl_end_staging(bool ok)
{
    portENTER_CRITICAL(&s_mux);
    bool              busy  = s_flash.busy;
    flashctl_state_t  state = s_flash.state;
    portEXIT_CRITICAL(&s_mux);
    assert(busy && state == FLASHCTL_STAGING);

    if (ok) {
        /* M7: stamp so an unpushed claim can eventually be reclaimed (see
         * flashctl_try_begin_staging above) -- stays STAGING/busy: ready for flashctl_start_push,
         * now or later. M10 (round 2): this stamp doubles as the ownership token handed back to
         * the caller -- see the doc comment in flashctl.h. */
        int64_t stamp;
        portENTER_CRITICAL(&s_mux);
        stamp = s_flash.staged_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_mux);
        return (uint64_t)stamp;
    }
    portENTER_CRITICAL(&s_mux);
    s_flash.state     = FLASHCTL_IDLE;
    s_flash.busy      = false;
    s_flash.staged_us = 0;
    portEXIT_CRITICAL(&s_mux);
    return 0;
}

void flashctl_clear_result(void)
{
    portENTER_CRITICAL(&s_mux);
    s_flash.pct     = 0;
    s_flash.result  = 0;
    s_flash.size    = 0;
    s_flash.ver[0]  = '\0';
    s_flash.hwid[0] = '\0';
    portEXIT_CRITICAL(&s_mux);
}

/* The one function pointer passed to linkhost_flash: publishes push progress under the lock
 * flashctl_get reads. Same formula as webapi.c's old flash_progress + api_status's percent math,
 * just computed here instead of split across two files. */
static void flash_progress(uint32_t sent, uint32_t total, void *ctx)
{
    (void)ctx;
    unsigned pct = (total > 0u) ? (unsigned)(((uint64_t)sent * 100u) / total) : 0u;
    if (pct > 100u) pct = 100u;
    portENTER_CRITICAL(&s_mux);
    s_flash.pct = (uint8_t)pct;
    portEXIT_CRITICAL(&s_mux);
}

/* The push half. Owns the link for the whole transfer, then publishes the outcome for
 * flashctl_get and releases the single-flight guard. */
static void flash_task(void *arg)
{
    (void)arg;
    char     ver[IMG_VER_LEN];
    char     hwid[IMG_HWID_LEN + 1];
    uint32_t size;
    uint8_t  sha[32];

    portENTER_CRITICAL(&s_mux);
    memcpy(ver, s_flash.ver, sizeof ver);
    memcpy(hwid, s_flash.hwid, sizeof hwid);
    size = s_flash.size;
    memcpy(sha, s_flash_sha, sizeof sha);
    portEXIT_CRITICAL(&s_mux);

    int rc = linkhost_flash(ver, hwid, size, sha, flash_progress, NULL);

    portENTER_CRITICAL(&s_mux);
    s_flash.result = rc;
    s_flash.state  = (rc == 0) ? FLASHCTL_DONE_OK : FLASHCTL_DONE_ERR;
    s_flash.busy   = false;
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "flash push of %lu B finished rc=%d", (unsigned long)size, rc);
    vTaskDelete(NULL);
}

int flashctl_start_push(const char *ver, const char *hwid, uint32_t size, const uint8_t sha[32],
                        uint64_t token)
{
    assert(ver != NULL && hwid != NULL && sha != NULL);

    /* M10 (round 2): the precondition check AND the ownership-token check happen inside the SAME
     * critical section as the state write below (unlike the old two-critical-section shape this
     * replaced) -- so there is no window between "we decided we're the owner" and "we mutated
     * state" for a concurrent flashctl_try_begin_staging TTL-reclaim to interleave. A stale caller
     * (its claim already reclaimed, so token != the live staged_us -- or the guard isn't even
     * STAGING any more) gets FLASHCTL_E_NOT_OWNER and NOTHING here is touched; it must never hit
     * the old `assert(busy && state == STAGING)`, since a stale caller turning up late is an
     * expected, not exceptional, event now (see flashctl_try_begin_staging's TTL reclaim). */
    bool ok;
    portENTER_CRITICAL(&s_mux);
    if (!(s_flash.busy && s_flash.state == FLASHCTL_STAGING) ||
        token != (uint64_t)s_flash.staged_us) {
        ok = false;
    } else {
        memcpy(s_flash.ver, ver, sizeof s_flash.ver);
        memcpy(s_flash.hwid, hwid, sizeof s_flash.hwid);
        s_flash.size = size;
        memcpy(s_flash_sha, sha, sizeof s_flash_sha);
        s_flash.state     = FLASHCTL_PUSHING;     /* must be set before the task can finish */
        s_flash.staged_us = 0;                    /* M7: no longer an unpushed claim to reclaim */
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);

    if (!ok) return FLASHCTL_E_NOT_OWNER;

    if (xTaskCreate(flash_task, "flashctl_push", 4096, NULL, 5, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_mux);
        s_flash.state = FLASHCTL_IDLE;
        s_flash.busy  = false;
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }
    return 0;
}

bool flashctl_release_staged(uint64_t token)
{
    bool released = false;
    portENTER_CRITICAL(&s_mux);
    if (s_flash.busy && s_flash.state == FLASHCTL_STAGING && s_flash.staged_us != 0 &&
        token == (uint64_t)s_flash.staged_us) {
        s_flash.state     = FLASHCTL_IDLE;
        s_flash.busy      = false;
        s_flash.staged_us = 0;
        released = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return released;
}

void flashctl_get(flashctl_status_t *out)
{
    assert(out != NULL);
    portENTER_CRITICAL(&s_mux);
    *out = s_flash;
    portEXIT_CRITICAL(&s_mux);
}
