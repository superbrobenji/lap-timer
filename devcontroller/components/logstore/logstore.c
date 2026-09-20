/* logstore.c -- Plan 5.5 Task 2 scaffold stub. Task 5 implements append-with-rotation on the
 * `logs` LittleFS partition for real; every entry point here is a compiling not-implemented stub
 * until then.
 */
#include "logstore.h"

esp_err_t logstore_init(size_t cap_bytes)
{
    (void)cap_bytes;
    return ESP_ERR_NOT_SUPPORTED;   /* Task 5 mounts/prepares `logs` and opens the current log */
}

int logstore_append(const lt_stream_rec_t *rec)
{
    (void)rec;
    return -1;
}

int logstore_list(logstore_entry_t *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}

int logstore_open_read(const char *id, logstore_file_t *out)
{
    (void)id;
    (void)out;
    return -1;
}
