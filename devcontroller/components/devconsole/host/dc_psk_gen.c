/* devcontroller/components/devconsole/host/dc_psk_gen.c -- dc_psk_format/dc_psk_valid: the pure,
 * IDF-free half of the AP passphrase module (F3 Task 5, dk:H1 / R16). Host-tested by
 * devcontroller/test/test_dc_psk.c. No heap, no loops unbounded by DC_PSK_LEN/out_cap.
 */
#include "dc_psk.h"

#include <assert.h>
#include <string.h>

/* dc_psk_format's byte-to-character mapping takes the low 5 bits of a random byte as an index, so
 * it needs exactly 32 alphabet characters to make every value 0-31 land on a real character. */
_Static_assert(sizeof(DC_PSK_ALPHABET) - 1 == 32, "the 5-bit mapping needs exactly 32 characters");

void dc_psk_format(const uint8_t *rnd, size_t n, char *out, size_t out_cap)
{
    assert(rnd != NULL || n == 0);
    assert(out != NULL);
    assert(out_cap > 0);
    static const char A[] = DC_PSK_ALPHABET;
    size_t want = (n < DC_PSK_LEN) ? n : DC_PSK_LEN;
    if (want + 1u > out_cap) want = out_cap - 1u;
    for (size_t i = 0; i < want; i++) out[i] = A[rnd[i] & 0x1Fu];   /* bounded: want <= DC_PSK_LEN */
    out[want] = '\0';
}

bool dc_psk_valid(const char *s)
{
    if (s == NULL) return false;
    size_t i = 0;
    for (; i < DC_PSK_LEN; i++) {                            /* bounded: DC_PSK_LEN */
        if (s[i] == '\0' || strchr(DC_PSK_ALPHABET, s[i]) == NULL) return false;
    }
    return s[i] == '\0';
}
