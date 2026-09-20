# webapi -- /api/* URI handlers + static SPA serving (Plan 5.5 Task 4).
#
# SRCS: webapi.c is the esp_http_server glue; host/config_diff.c is the PURE config diff/minify
# logic (no esp_*/httpd deps) that devcontroller/test/test_config_diff.c also links directly on
# the host; host/jsmn.c is a LOCAL vendored copy of jsmn (from components/core/util/jsmn.{c,h}) so
# the diff parses JSON without `REQUIRES core` -- which would drag the whole lap-timer app/driver
# tree into B. config_diff.c takes jsmn's declarations (JSMN_HEADER) and links jsmn.c's impl; both
# resolve "jsmn.h" via their own host/ directory (quote-include), so no extra include dir is needed.
idf_component_register(
    SRCS "webapi.c" "host/config_diff.c" "host/jsmn.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server linkhost logstore)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)

# Vendored third-party jsmn: single-header parser with known (downgraded) signedness conversions;
# keep the build output clean.
set_source_files_properties(host/jsmn.c PROPERTIES
    COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion")
