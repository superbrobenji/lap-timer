# webapi -- /api/* URI handlers + static SPA serving (Plan 5.5 Tasks 4/5/6).
#
# SRCS: webapi.c is the esp_http_server glue; everything under host/ is PURE, host-testable logic
# (no esp_*/httpd deps) that devcontroller/test/test_*.c links directly:
#   config_diff.c  the config diff/minify logic (test_config_diff.c)
#   multipart.c    the streaming multipart/form-data parser for POST /api/flash (test_multipart.c)
#   image_desc.c   the app-image ver/hwid header parser (test_image_desc.c)
#   status_json.c  the GET /api/status success-body formatter (test_status_json.c, Plan 5.6 T3 fix 2)
# host/jsmn.c is a LOCAL vendored copy of jsmn (from components/core/util/jsmn.{c,h}) so the diff
# parses JSON without `REQUIRES core` -- which would drag the whole lap-timer app/driver tree into
# B. config_diff.c takes jsmn's declarations (JSMN_HEADER) and links jsmn.c's impl; both resolve
# "jsmn.h" via their own host/ directory (quote-include), so no extra include dir is needed.
#
# PRIV_REQUIRES: POST /api/flash streams the upload into the `ota_stage` partition (esp_partition)
# while hashing it with mbedtls' SHA-256.
idf_component_register(
    SRCS "webapi.c" "host/config_diff.c" "host/jsmn.c" "host/multipart.c" "host/image_desc.c"
         "host/status_json.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server linkhost logstore
    PRIV_REQUIRES esp_timer esp_partition mbedtls)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
