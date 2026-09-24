/* devcontroller/components/flashcore/include/otastage.h -- otastage: streams a signed lap-timer
 * app image into the `ota_stage` flash partition while hashing it (Plan 5.6, dev-kit as primary
 * interface, Task 7). Moved out of webapi.c's stage_ctx_t/stage_commit/stage_sink so both
 * POST /api/flash and a later console `flash stage` share the exact same erase/write/read-back
 * sequence.
 *
 * Single-flight: one otastage_begin/otastage_begin_bounded, zero or more otastage_write calls,
 * then one otastage_finish (or otastage_abort) -- one such sequence at a time, serialized by the
 * caller (flashctl's single-flight guard). No heap; the 4 KB staging block is one static buffer.
 *
 * Two begin variants, because the two front ends know different things up front (Plan 5.6 Task 7
 * fix round 1): a console `flash stage <size> <sha>` is told the exact final image size by the
 * operator, so otastage_begin's `size` is that exact target -- otastage_finish rejects a stage
 * that ends with fewer (or, defensively, more) bytes than declared, catching an interrupted
 * upload that would otherwise be silently accepted. POST /api/flash streams an unsized multipart
 * body and only knows an upper bound (the partition's own capacity), so it uses
 * otastage_begin_bounded instead -- otastage_finish there only enforces the existing
 * IMG_DESC_MIN_LEN floor, not an exact byte count.
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
#define OTASTAGE_E_SIZE  (-1)   /* bad size/bound at begin, an overflowing write, or (EXACT only) a finish whose byte count does not match the declared size */
#define OTASTAGE_E_WRITE (-2)   /* no ota_stage partition, a flash erase/write/read failed, or the SHA engine failed */
#define OTASTAGE_E_IMAGE (-3)   /* fewer than IMG_DESC_MIN_LEN bytes staged, or img_desc_parse rejected the header -- see otastage_last_image_rc */

/* Starts a new staging attempt in EXACT-size mode: finds the `ota_stage` partition and resets the
 * running SHA-256. `size` is the exact number of bytes this attempt must end with -- the caller
 * (a console `flash stage <size> <sha>`) already knows it, having been told it by the operator.
 * otastage_write rejects a write that would push the running total past `size`; otastage_finish
 * rejects a stage that ends with fewer bytes than `size` (an interrupted upload). Rejects size==0
 * or size > the partition's capacity. Returns OTASTAGE_OK, OTASTAGE_E_SIZE, or OTASTAGE_E_WRITE
 * (partition missing / SHA engine failed to start). */
int otastage_begin(uint32_t size);

/* Starts a new staging attempt in BOUNDED mode: same partition lookup and SHA reset as
 * otastage_begin, but `max` is only an upper limit, not a declared exact size -- for a front end
 * that does not know the final image size up front (POST /api/flash streams a multipart body of
 * unknown final length; the caller passes the partition's own capacity as `max`, matching the
 * original stage_sink bound). otastage_write still rejects a write that would push the running
 * total past `max`, but otastage_finish only enforces the IMG_DESC_MIN_LEN floor -- any byte count
 * from IMG_DESC_MIN_LEN up to `max` is accepted. Rejects max==0 or max > the partition's capacity.
 * Returns OTASTAGE_OK, OTASTAGE_E_SIZE, or OTASTAGE_E_WRITE (partition missing / SHA engine failed
 * to start). */
int otastage_begin_bounded(uint32_t max);

/* Buffers and writes `n` bytes: a lazy 4 KB erase-then-write through the single static staging
 * block, each full block folded into the running SHA-256 as it is written. May be called any
 * number of times after otastage_begin/otastage_begin_bounded. Returns OTASTAGE_OK, OTASTAGE_E_SIZE
 * (this write would exceed the bound given at begin), or OTASTAGE_E_WRITE (an erase/write failed). */
int otastage_write(const uint8_t *p, size_t n);

/* Flushes any partial final block -- BEFORE any size/image check below, so even a stage that ends
 * up rejected for being the wrong size (EXACT mode) or too short (either mode) still has its
 * trailing partial sector committed to flash; harmless, since a rejected attempt's bytes are
 * meaningless and the next begin/write call erases over them regardless -- then finishes the
 * SHA-256, reads the descriptor back OUT of the partition (so ver/hwid/sha describe
 * the bytes that were actually written) and parses it.
 * On OTASTAGE_OK, `sha`/`ver`/`hwid`/`*size` are filled and the attempt is over (a fresh
 * otastage_begin/otastage_begin_bounded is required before writing again). Returns OTASTAGE_OK,
 * OTASTAGE_E_SIZE (EXACT mode: the flushed byte count does not equal the declared size),
 * OTASTAGE_E_WRITE (flush, SHA finish, or read-back failed), or OTASTAGE_E_IMAGE (too few bytes
 * staged, or img_desc_parse rejected the header -- call otastage_last_image_rc for which). */
int otastage_finish(uint8_t sha[32], char ver[IMG_VER_LEN], char hwid[IMG_HWID_LEN + 1],
                    uint32_t *size);

/* After otastage_finish returns OTASTAGE_E_IMAGE, tells the caller why, so a front end can report
 * the same distinct reasons the pre-flashcore code did: -1 too few bytes staged or bad esp_app_desc
 * magic ("not an ESP32 app image"), -2 bad version field ("bad image version"), -3 bad hwid field
 * ("bad image hwid") -- mirrors img_desc_parse's own return codes (image_desc.h). Meaningless
 * before the first otastage_finish call, or after any return other than OTASTAGE_E_IMAGE. */
int otastage_last_image_rc(void);

/* Releases the SHA engine and resets state after a failed/abandoned attempt (any otastage_write
 * error, or an upload aborted for a reason otastage never saw). Does NOT flush a pending partial
 * block -- unlike otastage_finish, which always flushes first (see its doc above), an aborted
 * attempt's buffered-but-unwritten tail is simply discarded, since the whole point of aborting is
 * that the attempt is being thrown away. Safe to call even when no attempt is active, or after
 * otastage_finish already ended one. */
void otastage_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* OTASTAGE_H */
