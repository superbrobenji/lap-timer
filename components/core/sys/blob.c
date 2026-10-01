#include "core/blob.h"
#include "core/ses.h"      /* ses_crc16 */
#include <string.h>

/* I1 (final review, Ruling F-2): blob_wrap/blob_unwrap are assert-free by design, not oversight --
 * their only app-side caller (lt_nvs.c's save_framed/load_framed) always runs under s_blob_lock,
 * and that lock's own sink (errlog_add) is unreachable from anything running under it. Plain
 * guarded returns only; the subtract-form bounds checks below are the sole validation. */
size_t blob_wrap(uint8_t ver, const void *payload, size_t n, uint8_t *out, size_t cap)
{
    if (payload == NULL || out == NULL) return 0u;
    if (n == 0u || cap < BLOB_OVERHEAD || n > cap - BLOB_OVERHEAD) return 0u;
    out[0] = ver;
    memcpy(out + 1, payload, n);
    uint16_t crc = ses_crc16(out, n + 1u);
    out[n + 1u] = (uint8_t)(crc & 0xFFu);
    out[n + 2u] = (uint8_t)(crc >> 8);
    return n + BLOB_OVERHEAD;
}

int blob_unwrap(uint8_t expect_ver, const uint8_t *in, size_t n, void *payload, size_t payload_len,
                 uint8_t *ver_out)
{
    if (in == NULL || payload == NULL) return -1;
    if (payload_len == 0u) return -1;
    if (n < BLOB_OVERHEAD || n - BLOB_OVERHEAD != payload_len) return -1;
    uint16_t want = ses_crc16(in, payload_len + 1u);
    uint16_t got  = (uint16_t)(in[payload_len + 1u] | (in[payload_len + 2u] << 8));
    if (want != got) return -2;
    if (ver_out != NULL) *ver_out = in[0];
    if (in[0] != expect_ver) return -3;
    memcpy(payload, in + 1, payload_len);
    return 0;
}
