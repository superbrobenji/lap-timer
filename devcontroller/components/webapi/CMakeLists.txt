# webapi -- /api/* URI handlers + static SPA serving (Plan 5.5 Task 4).
idf_component_register(
    SRCS "webapi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
