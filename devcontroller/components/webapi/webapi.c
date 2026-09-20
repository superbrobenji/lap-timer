/* webapi.c -- Plan 5.5 Task 2 scaffold stub. Task 4 implements the real api/ handlers (config
 * diff/minify via jsmn, sessions list + download) and the static-file catch-all migrates here
 * from main.c; every entry point here is a compiling not-implemented stub until then.
 */
#include "webapi.h"

esp_err_t webapi_register(httpd_handle_t server)
{
    (void)server;
    return ESP_ERR_NOT_SUPPORTED;   /* Task 4 registers /api/status, /api/config, /api/session* */
}

int config_diff_minify(const char *current_json, const char *desired_json, char *out, size_t out_cap)
{
    (void)current_json;
    (void)desired_json;
    (void)out;
    (void)out_cap;
    return -1;
}
