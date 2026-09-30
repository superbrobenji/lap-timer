#ifndef CORE_BLOB_H
#define CORE_BLOB_H
#include <stdint.h>
#include <stddef.h>

/* On-flash record framing (spec §15.2/§17.9): [version u8][payload n][crc16 LE].
 * CRC = ses_crc16(version byte + payload). Matches the layout today's cfg blob carries
 * (cfg_t.version is the leading byte, ses_crc16 covers all of cfg_t, stored little-endian). */
#define BLOB_OVERHEAD 3u

size_t blob_wrap(uint8_t ver, const void *payload, size_t n, uint8_t *out, size_t cap);
/* writes n + BLOB_OVERHEAD bytes; returns that size, or 0 when cap is too small / n == 0 */

int blob_unwrap(uint8_t expect_ver, const uint8_t *in, size_t n, void *payload, size_t payload_len,
                 uint8_t *ver_out);
/* 0 ok (payload filled); -1 n != payload_len + BLOB_OVERHEAD; -2 CRC mismatch;
 * -3 version mismatch (ver_out = stored version, payload untouched). ver_out may be NULL.
 * M6: payload_len must describe a real buffer of at least that size; the function only
 * cross-checks it against n. */

#endif
