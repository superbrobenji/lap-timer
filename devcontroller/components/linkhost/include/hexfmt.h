/* devcontroller/components/linkhost/include/hexfmt.h -- hexfmt: a pure, IDF-free, bounded hex
 * dumper (Plan 5.6 Task 5). The one job: format up to 32 raw bytes as lowercase space-separated hex
 * pairs ("ff 01 0a") for linkhost's `link trace` timeout hexdump and any other bounded debug dump.
 * No heap, no shared state, always NUL-terminated within the caller's buffer -- host-tested
 * (devcontroller/test/test_hexfmt.c).
 */
#ifndef HEXFMT_H
#define HEXFMT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Formats up to 32 bytes of b[0..n) into out[0..cap) as lowercase hex pairs separated by single
 * spaces ("ff 01 0a"), no trailing space. n > 32 formats only the first 32 bytes then appends
 * " \xE2\x80\xA6" (a UTF-8 ellipsis) to mark the truncation. The output is always NUL-terminated
 * within cap, even if cap is too small to hold every byte (the partial result written so far is
 * still terminated) -- cap == 0 writes nothing and returns 0. Returns the number of characters
 * written, excluding the NUL. */
size_t hexfmt_line(const uint8_t *b, size_t n, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* HEXFMT_H */
