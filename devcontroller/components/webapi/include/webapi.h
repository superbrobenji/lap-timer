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

#include "config_diff.h"   /* config_diff_minify / config_diff_next_line (pure, host-testable) */
#include "linkhost_proto.h" /* lt_stream_rec_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Registers /api/status, /api/config (GET/POST), /api/sessions, /api/session/<id>, /api/logs,
 * /api/log/<id>, /api/stream (live-monitor SSE), and the static-file SPA catch-all onto an
 * already-started httpd instance, and starts the async download worker on first call. /api/flash
 * is a later step. */
esp_err_t webapi_register(httpd_handle_t server);

/* Called by the stream-consumer task (main.c) to publish one demuxed record to the live monitor. */
void webapi_stream_push(const lt_stream_rec_t *r);

#ifdef __cplusplus
}
#endif

#endif /* WEBAPI_H */
