/* devcontroller/components/webapi/include/webapi.h -- webapi: the api/ URI handlers + static
 * SPA serving (docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md, sub-project B,
 * Task 4).
 *
 * PINNED interface for Task 4. Task 2 (this scaffold) ships a compiling not-implemented stub;
 * Task 4 implements the real handlers + config_diff_minify (via jsmn).
 */
#ifndef WEBAPI_H
#define WEBAPI_H

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers /api/status, /api/config (GET/POST), /api/sessions, /api/session/<id>, and the
 * static-file catch-all onto an already-started httpd instance. */
esp_err_t webapi_register(httpd_handle_t server);

/* Produces the minimal `config set` argument (only the keys that differ between current_json and
 * desired_json, minified, \"-escaped) into out[out_cap]. Returns the written length, or <0 if it
 * would exceed the ~250 B `config set` line budget (the caller then splits or errors). */
int config_diff_minify(const char *current_json, const char *desired_json, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* WEBAPI_H */
