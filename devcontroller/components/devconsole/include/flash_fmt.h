/* devcontroller/components/devconsole/include/flash_fmt.h -- flash_result_str: a PURE formatter
 * for a flashctl push result code, shared by the console's `flash push`/`flash status` (Plan 5.6
 * Task 10). Mirrors the exact "0x%04x" / "link %d" fragment webapi.c's api_status already builds
 * for the SPA's flash_err field (webapi.c ~line 205), so both front ends report the same push
 * result the same way.
 *
 * PINNED interface (host-tested: devcontroller/test/test_flash_fmt.c). Pure, IDF-free: no heap,
 * no shared state, bounded to the caller's buffer -- same cap-bounded / always-NUL-terminated
 * contract as this component's own hexfmt_line precedent (components/linkhost/include/hexfmt.h).
 */
#ifndef FLASH_FMT_H
#define FLASH_FMT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Formats a flashctl push result code (flashctl_status_t.result -- linkhost_flash's own return
 * value, see flashctl.h) into out[0..cap): "0x%04x" for result >= 0 (an LT_ERR_* code the
 * lap-timer's `ota end` returned), "link %d" for result < 0 (a negative linkhost_flash/link-layer
 * error, e.g. LINKHOST_E_*). out is always left NUL-terminated for any cap > 0 (the written prefix
 * is truncated to fit, never overflowing out[cap-1]); cap == 0 writes nothing. Returns the number
 * of characters actually written, excluding the NUL (i.e. bounded by cap - 1) -- same convention
 * as hexfmt_line. No heap, no IDF dependency. */
size_t flash_result_str(int result, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_FMT_H */
