/* devcontroller/components/logstore/host/logstore_state.c -- dk:M10: pure predicate for
 * logstore's "is the log ready to accept appends" state. See include/logstore_rot.h. Pure
 * (no esp_* / littlefs deps), so it builds and runs on the host (devcontroller/test links
 * directly). */
#include <stdbool.h>

#include "logstore_rot.h"

bool logstore_accepting(bool open_ok, bool write_ok)
{
    return open_ok && write_ok;
}
