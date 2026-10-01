/* devcontroller/components/logstore/host/logstore_id.c -- PURE logstore id-shape validator. See
 * include/logstore_id.h for the contract; devcontroller/test/test_logstore_id.c exercises this
 * directly on the host.
 */
#include "logstore_id.h"

#include <string.h>

bool logstore_id_ok(const char *id, size_t len)
{
    if (id == NULL || len != LOGSTORE_ID_LEN) return false;
    if (strncmp(id, "log_", 4) != 0) return false;
    for (size_t i = 4; i < len; i++) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}
