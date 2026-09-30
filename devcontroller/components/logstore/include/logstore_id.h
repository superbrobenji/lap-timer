/* devcontroller/components/logstore/include/logstore_id.h -- PURE, IDF-free shape validator for
 * logstore's own generated file ids (debt sweep A #60 fix round 1, sibling of the lap-timer's
 * own app/lt_proto.h::lt_session_id_ok).
 *
 * logstore.c's open_new_file()/scan_existing() always generate/accept ids as "log_%08u" (exactly
 * "log_" + 8 decimal digits, 12 chars total -- see logstore.c's build_path/parse_log_filename).
 * That is a DIFFERENT shape from app/lt_proto.h's session ids (1..10 chars of [A-Za-z0-9_], no
 * fixed prefix), so it needs its own validator rather than reusing lt_session_id_ok.
 * webapi.c's do_log_download (GET /api/log/<id>) calls this BEFORE logstore_open_read, so a
 * path-shaped id (`..`, `/`, oversize) never reaches build_path's "%s/%s.bin".
 *
 * No esp_* / LittleFS dependency (stdbool.h/stddef.h only), so this also builds host-side --
 * devcontroller/test/test_logstore_id.c links host/logstore_id.c directly, per the devcontroller
 * host-test harness convention (each component's host directory holds its pure sources, while
 * components/<c>/<c>.c is the IDF/LittleFS glue), mirroring logstore_rot.h/logstore_rot.c's own
 * split.
 */
#ifndef LOGSTORE_ID_H
#define LOGSTORE_ID_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGSTORE_ID_LEN 12u   /* "log_" (4 chars) + exactly 8 decimal digits */

/* True iff `id` is exactly LOGSTORE_ID_LEN bytes, "log_" followed by 8 ASCII digits -- the exact
 * shape logstore.c ever generates. Anything else (short, long, wrong prefix, non-digit suffix,
 * NULL) is rejected. */
bool logstore_id_ok(const char *id, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LOGSTORE_ID_H */
