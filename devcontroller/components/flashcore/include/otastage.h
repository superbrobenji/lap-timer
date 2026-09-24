/* devcontroller/components/flashcore/include/otastage.h -- otastage: streams a signed lap-timer
 * app image into the `ota_stage` flash partition while hashing it (Plan 5.6, dev-kit as primary
 * interface, Task 7). Moved out of webapi.c's stage_ctx_t/stage_commit/stage_sink so both
 * POST /api/flash and a later console `flash stage` share the exact same erase/write/read-back
 * sequence.
 *
 * Single-flight: one otastage_begin, zero or more otastage_write calls, then one otastage_finish
 * (or otastage_abort) -- one such sequence at a time, serialized by the caller (flashctl's
 * single-flight guard). No heap; the 4 KB staging block is one static buffer.
 */
#ifndef OTASTAGE_H
#define OTASTAGE_H

#include <stddef.h>
#include <stdint.h>

#include "image_desc.h"   /* IMG_VER_LEN, IMG_HWID_LEN, IMG_DESC_MIN_LEN */

#ifdef __cplusplus
extern "C" {
#endif

#define OTASTAGE_OK       0
#define OTASTAGE_E_SIZE  (-1)   /* size==0, size>partition capacity, or the running total would */
#define OTASTAGE_E_WRITE (-2)   /* no ota_stage partition, a flash erase/write/read failed, or the SHA engine failed */
#define OTASTAGE_E_IMAGE (-3)   /* fewer than IMG_DESC_MIN_LEN bytes staged, or img_desc_parse rejected the header */

/* Starts a new staging attempt: finds the `ota_stage` partition and resets the running SHA-256.
 * `size` is the caller's declared upper bound for this attempt -- the partition's own size for a
 * front end that does not know the final image size up front (POST /api/flash streams a
 * multipart body of unknown final length), or an operator-declared size for one that does (a
 * console `flash stage <size> <sha>`). Rejects size==0 or size > the partition's capacity.
 * Returns OTASTAGE_OK, OTASTAGE_E_SIZE, or OTASTAGE_E_WRITE (partition missing / SHA engine
 * failed to start). */
int otastage_begin(uint32_t size);

/* Buffers and writes `n` bytes: a lazy 4 KB erase-then-write through the single static staging
 * block, each full block folded into the running SHA-256 as it is written. May be called any
 * number of times after otastage_begin. Returns OTASTAGE_OK, OTASTAGE_E_SIZE (this write would
 * exceed the bound given to otastage_begin), or OTASTAGE_E_WRITE (an erase/write failed). */
int otastage_write(const uint8_t *p, size_t n);

/* Flushes any partial final block, finishes the SHA-256, reads the descriptor back OUT of the
 * partition (so ver/hwid/sha describe the bytes that were actually written) and parses it.
 * On OTASTAGE_OK, `sha`/`ver`/`hwid`/`*size` are filled and the attempt is over (a fresh
 * otastage_begin is required before writing again). Returns OTASTAGE_OK, OTASTAGE_E_WRITE (flush,
 * SHA finish, or read-back failed), or OTASTAGE_E_IMAGE (too few bytes staged, or img_desc_parse
 * rejected the header -- bad magic, version or hwid all collapse to this one code). */
int otastage_finish(uint8_t sha[32], char ver[IMG_VER_LEN], char hwid[IMG_HWID_LEN + 1],
                    uint32_t *size);

/* Releases the SHA engine and resets state after a failed/abandoned attempt (any otastage_write
 * error, or an upload aborted for a reason otastage never saw). Safe to call even when no attempt
 * is active, or after otastage_finish already ended one. */
void otastage_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* OTASTAGE_H */
