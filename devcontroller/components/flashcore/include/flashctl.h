/* devcontroller/components/flashcore/include/flashctl.h -- flashctl: the single-flight cmd-OTA
 * push state machine (Plan 5.6, dev-kit as primary interface, Task 7). Moved out of webapi.c's
 * s_flash struct / flash_task so both POST /api/flash and a later console `flash stage`+`flash
 * push` drive the same guard, progress percentage and result reporting.
 *
 * Two front ends, one guard: `flashctl_try_begin_staging` claims it atomically (no TOCTOU between
 * checking busy and setting it -- the two front ends can call this concurrently). POST /api/flash
 * stages then immediately pushes in one HTTP request; a console can stage now and push later, so
 * `flashctl_end_staging(true)` leaves the guard held (state stays FLASHCTL_STAGING) rather than
 * starting the push itself -- the caller decides when (or whether) to call flashctl_start_push.
 */
#ifndef FLASHCTL_H
#define FLASHCTL_H

#include <stdbool.h>
#include <stdint.h>

#include "image_desc.h"   /* IMG_VER_LEN, IMG_HWID_LEN */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLASHCTL_IDLE = 0,
    FLASHCTL_STAGING,   /* an image is being (or has been) staged into ota_stage */
    FLASHCTL_PUSHING,   /* the worker task owns the link (linkhost_flash) */
    FLASHCTL_DONE_OK,
    FLASHCTL_DONE_ERR
} flashctl_state_t;

typedef struct {
    flashctl_state_t state;
    bool             busy;    /* covers the whole operation: staging through the end of the push */
    uint8_t          pct;     /* push progress 0..100; meaningful in FLASHCTL_PUSHING */
    int              result;  /* linkhost_flash rc; meaningful in FLASHCTL_DONE_* */
    char             ver[IMG_VER_LEN];
    char             hwid[IMG_HWID_LEN + 1];
    uint32_t         size;
    /* M7 (final review): esp_timer_get_time() at the moment a stage completed (the LAST
     * flashctl_end_staging(true) call) -- 0 while no stage has finished yet (a fresh claim, or one
     * still mid-transfer). Internal bookkeeping for flashctl_try_begin_staging's stale-STAGING
     * reclaim below; not part of the console/HTTP status surface (cmd_flash.c/webapi.c don't print
     * it) but exposed here anyway since flashctl_get copies this whole struct as one unit. */
    int64_t          staged_us;
} flashctl_status_t;

/* M7 (final review): how long an image may sit STAGED-but-never-PUSHED before
 * flashctl_try_begin_staging treats its claim as abandoned and reclaims it for a fresh caller --
 * 10 minutes. Without this, a `flash stage` (or a POST /api/flash whose client vanished between
 * the stage and the push) would hold the single-flight guard forever: no `flash push`/`flash
 * abort` ever arrives to release it, and every later staging attempt (either front end) fails
 * with "busy" until the device reboots. */
#define FLASHCTL_STAGE_TTL_US ((int64_t)10 * 60 * 1000000)

/* M10 (final review, round 2): flashctl_start_push's return when the caller's ownership token
 * (see below) does not match the current STAGING claim -- either because the claim was reclaimed
 * out from under a stale caller (FLASHCTL_STAGE_TTL_US expired), or because flashctl is not even
 * in a STAGING state to push at all. Value choice: -7. linkhost_proto.h's LINKHOST_E_* range is
 * -1..-6 (LINKHOST_E_TIMEOUT..LINKHOST_E_BUSY) and flashctl_start_push's own pre-existing failure
 * code is -1 (xTaskCreate failure) -- -7 is the next free slot in flashctl's own return-code
 * space and cannot be confused with any linkhost_flash result flash_task might later store in
 * s_flash.result (flashctl_start_push's return value and s_flash.result are never compared
 * against each other, but keeping them out of the same negative range avoids a maintainer
 * ever conflating the two). */
#define FLASHCTL_E_NOT_OWNER (-7)

/* Atomically claims the single-flight guard (IDLE -> STAGING, busy=true) -- a single-word state
 * claim under a critical section, so two front ends racing this call can never both win. Returns
 * false without changing anything if a staging or push is already under way -- UNLESS that
 * staging claim already finished (flashctl_end_staging(true) was called, staged_us != 0) and has
 * sat unpushed for at least FLASHCTL_STAGE_TTL_US (M7, final review): that claim is treated as
 * abandoned and reclaimed for THIS caller instead (reset to a fresh STAGING claim, same critical
 * section as the check -- no window for a third caller to interleave). A claim still mid-transfer
 * (staged_us == 0: no flashctl_end_staging(true) yet) is never reclaimed this way regardless of
 * age; that window is bounded separately (cmd_flash.c's own FLASH_STALL_MAX_MS, or the lifetime of
 * the POST /api/flash HTTP request). */
bool flashctl_try_begin_staging(void);

/* Ends the staging phase claimed by a prior (successful) flashctl_try_begin_staging. ok=false
 * releases the guard back to FLASHCTL_IDLE (the attempt failed or was abandoned) and returns 0.
 * ok=true leaves the guard held (state stays FLASHCTL_STAGING, busy stays true) so the caller can
 * push immediately (flashctl_start_push, as POST /api/flash does) or later (a console `flash
 * push`) -- stamps staged_us (M7) so a caller that never comes back to push can eventually be
 * reclaimed by flashctl_try_begin_staging above, and RETURNS that same stamp as an opaque
 * ownership token (M10, round 2): the caller must hold onto it and present it back to
 * flashctl_start_push / flashctl_release_staged, which check it against the live s_flash.staged_us
 * inside their own critical section before touching anything. This is what lets
 * flashctl_try_begin_staging's TTL reclaim (above) be safe: reclaiming stamps a brand-new
 * staged_us, so the ORIGINAL caller's token from the finished-but-abandoned claim can never match
 * again, no matter how late it turns up.
 *
 * MUST be called only by the code path that synchronously holds the in-flight staging claim
 * (cmd_flash.c's `flash stage` raw-phase completion; webapi.c's api_flash_post between
 * otastage_finish and flashctl_start_push) -- never speculatively by a caller that merely
 * *believes* it might still own a STAGING claim (that belief is exactly what M10 replaces with an
 * explicit token check; see flashctl_start_push and flashctl_release_staged below). */
uint64_t flashctl_end_staging(bool ok);

/* Resets pct/result/ver/hwid/size to their idle values (but leaves state/busy untouched) -- call
 * once a fresh staging attempt has claimed the guard, so a previous push's leftover result never
 * bleeds into the new one's reporting. */
void flashctl_clear_result(void);

/* Spawns the one-shot task that pushes the already-staged image: linkhost_flash(ver, hwid, size,
 * sha, cb, ctx) with the progress callback that updates `pct` under the same lock flashctl_get
 * reads. `token` must be the value flashctl_end_staging(true) returned for the claim the caller
 * believes it still owns (M10, round 2). Inside one critical section: if the guard is not held in
 * FLASHCTL_STAGING, or `token` does not equal the live s_flash.staged_us, NOTHING is touched and
 * FLASHCTL_E_NOT_OWNER is returned -- this replaces the old unconditional
 * `assert(busy && state == STAGING)`, because a stale caller (its claim reclaimed by
 * flashctl_try_begin_staging's TTL path after it sat too long between staging and pushing) must
 * be told "no" rather than crash the firmware. Otherwise, still inside that same critical section:
 * `ver`/`hwid`/`size`/`sha` are copied in and the state transitions to FLASHCTL_PUSHING before the
 * task can possibly finish. `ver` and `hwid` must each be NUL-terminated within IMG_VER_LEN /
 * IMG_HWID_LEN+1 bytes. Returns 0 on success; FLASHCTL_E_NOT_OWNER as above; on a task-creation
 * failure the guard is released back to FLASHCTL_IDLE and -1 is returned. */
int flashctl_start_push(const char *ver, const char *hwid, uint32_t size, const uint8_t sha[32],
                        uint64_t token);

/* M10 (final review, round 2): releases a STAGING claim the caller believes it owns (the
 * `flash abort` counterpart to flashctl_start_push above) WITHOUT starting a push. `token` must be
 * the value a prior flashctl_end_staging(true) returned. Inside one critical section: if the
 * guard is held in FLASHCTL_STAGING, its staged_us is nonzero (a finished stage, not one still
 * mid-transfer) and `token` matches it, resets to FLASHCTL_IDLE/not-busy and returns true;
 * otherwise (wrong state, or a stale/mismatched token because the claim was reclaimed) touches
 * nothing and returns false. Unlike flashctl_end_staging(false), this is safe to call
 * speculatively -- a caller that is no longer sure it owns the claim is exactly the case this
 * function exists for. */
bool flashctl_release_staged(uint64_t token);

/* Copies the current status out under the same lock the push task uses to update pct/state/result,
 * so a concurrent reader never sees a torn struct. */
void flashctl_get(flashctl_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FLASHCTL_H */
