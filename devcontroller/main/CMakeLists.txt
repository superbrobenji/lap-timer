# main -- app_main (Plan 5.5 Task 2): NVS init, SoftAP up, mount `www`/`logs` LittleFS
# partitions, linkhost_init, start esp_http_server + register /api/status and the static-file
# catch-all. logstore/webapi are required (and their lifecycle entry points called, even though
# both are still Task 5/4 stubs) so the whole devcontroller/ tree actually compiles and links --
# not just the pieces main happens to call today.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server linkhost logstore webapi devconsole
    PRIV_REQUIRES joltwallet__littlefs)

# build_config.h from the build tree (CFG_DC_VERSION).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as the lap-timer's own components (spec §17.9 rule 6/10), pragmatic-P10-inspired
# per Plan 5.5 Global Constraints (no blocking linter for B, but the same warning bar).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
