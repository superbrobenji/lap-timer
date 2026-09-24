# flashcore -- OTA staging + push state behind the lap-timer firmware update flow (Plan 5.6 Task
# 7), extracted from webapi.c so both POST /api/flash and a later console `flash stage`/`flash
# push` share the exact same code.
#
# SRCS: otastage.c (partition staging + SHA-256) and flashctl.c (the single-flight state machine +
# push task) are the esp_partition/mbedtls/linkhost glue -- on-target only. Everything under host/
# is PURE, host-testable logic (no esp_*/IDF deps) that devcontroller/test/test_*.c links directly:
#   image_desc.c   the app-image ver/hwid header parser (test_image_desc.c; moved here from webapi
#                  in this task so both front ends can read it without depending on webapi)
#   stage_args.c   the `<size> <64 hex>` console argument grammar (test_stage_args.c)
#
# PRIV_REQUIRES: otastage.c streams into the `ota_stage` partition (esp_partition) while hashing
# with mbedtls' SHA-256; flashctl.c spawns the push task and drives it via linkhost_flash.
idf_component_register(
    SRCS "otastage.c" "flashctl.c" "host/image_desc.c" "host/stage_args.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_partition mbedtls linkhost)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
