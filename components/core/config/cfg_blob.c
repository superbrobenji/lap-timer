#include "core/cfg_blob.h"
#include "core/cfg.h"
#include "core/blob.h"
#include "core/core.h"
#include <string.h>

#define CFG_ASSERT_CODE 0x0A70   /* shared with cfg.c/cfg_json.c (same component dir) */

size_t cfg_blob_wrap(const cfg_t *c, uint8_t *out, size_t cap)
{
    CORE_ASSERT_RET(c != NULL && out != NULL, CFG_ASSERT_CODE, 0u);
    return blob_wrap(c->version, (const uint8_t *)c + 1, sizeof(cfg_t) - 1, out, cap);
}

/* tmp is filled in full before it is ever copied into *c (either by the single memcpy inside
 * blob_unwrap -- bytes [1, sizeof(cfg_t)-1] -- or by the explicit version-byte assignment below),
 * so *c only ever changes on the return >= 0 path; every other return leaves it untouched. */
int cfg_blob_unwrap(const uint8_t *in, size_t n, cfg_t *c)
{
    CORE_ASSERT_RET(in != NULL && c != NULL, CFG_ASSERT_CODE, -1);
    cfg_t tmp;
    memset(&tmp, 0, sizeof tmp);
    uint8_t stored = 0;
    int rc = blob_unwrap(CFG_VERSION, in, n, (uint8_t *)&tmp + 1, sizeof(cfg_t) - 1, &stored);
    if (rc == -3) {
        if (!cfg_migrate_supported(stored)) return -3;         /* unsupported: c stays untouched */
        if (blob_unwrap(stored, in, n, (uint8_t *)&tmp + 1, sizeof(cfg_t) - 1, NULL) != 0) return -1;
        if (cfg_migrate(&tmp, stored) != 0) return -3;         /* cannot happen: just confirmed supported */
    } else if (rc != 0) {
        return -1;                                              /* size or CRC mismatch */
    } else {
        tmp.version = CFG_VERSION;                              /* blob_unwrap never writes payload byte 0 */
    }
    int corrected = cfg_validate(&tmp);
    *c = tmp;
    return corrected;
}
