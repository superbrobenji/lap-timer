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
} flashctl_status_t;

/* Atomically claims the single-flight guard (IDLE -> STAGING, busy=true) -- a single-word state
 * claim under a critical section, so two front ends racing this call can never both win. Returns
 * false without changing anything if a staging or push is already under way. */
bool flashctl_try_begin_staging(void);

/* Ends the staging phase claimed by a prior (successful) flashctl_try_begin_staging. ok=false
 * releases the guard back to FLASHCTL_IDLE (the attempt failed or was abandoned). ok=true leaves
 * the guard held (state stays FLASHCTL_STAGING, busy stays true) so the caller can push
 * immediately (flashctl_start_push, as POST /api/flash does) or later (a console `flash push`). */
void flashctl_end_staging(bool ok);

/* Resets pct/result/ver/hwid/size to their idle values (but leaves state/busy untouched) -- call
 * once a fresh staging attempt has claimed the guard, so a previous push's leftover result never
 * bleeds into the new one's reporting. */
void flashctl_clear_result(void);

/* Spawns the one-shot task that pushes the already-staged image: linkhost_flash(ver, hwid, size,
 * sha, cb, ctx) with the progress callback that updates `pct` under the same lock flashctl_get
 * reads. Requires the guard already held in FLASHCTL_STAGING (a prior try_begin_staging +
 * end_staging(true)); transitions to FLASHCTL_PUSHING before the task can possibly finish. `ver`
 * and `hwid` must each be NUL-terminated within IMG_VER_LEN / IMG_HWID_LEN+1 bytes. Returns 0 on
 * success; on a task-creation failure the guard is released back to FLASHCTL_IDLE and a nonzero
 * rc is returned. */
int flashctl_start_push(const char *ver, const char *hwid, uint32_t size, const uint8_t sha[32]);

/* Copies the current status out under the same lock the push task uses to update pct/state/result,
 * so a concurrent reader never sees a torn struct. */
void flashctl_get(flashctl_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FLASHCTL_H */
