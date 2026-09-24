# devconsole -- esp_console REPL on the dev-kit's own USB (UART0), the primary developer interface
# so debugging never needs the WiFi AP (Plan 5.6 Task 4). console.c is the REPL scaffold + the ONE
# registration door (console_register); cmd_dc.c is the first command module (`dc
# status|log|baud`); host/jsonw.c is the pure, IDF-free JSON writer every `--json` command builds
# its body with; host/rate.c is the pure records/sec*10 arithmetic behind `dc status`'s
# fused_rate/status_rate fields (Plan 5.6 T4 fix 1). Both are compiled here for the on-target
# build AND linked directly by devcontroller/test/test_jsonw.c / test_rate.c on the host (mirrors
# webapi's host/status_json.c / logstore's host/logstore_rot.c: one implementation, not two
# copies drifting apart).
#
# NAMED "devconsole", not "console": ESP-IDF's own built-in esp_console library IS a component
# literally named "console" (esp-idf/components/console, provides esp_console.h). A project
# component directory takes priority over a built-in one of the SAME name -- naming this directory
# "console" (as the task brief's file list reads verbatim) doesn't add a component alongside the
# built-in one, it REPLACES it project-wide: `idf.py build` then fails with "esp_console.h: No
# such file or directory" here AND in ESP-IDF's own protocomm (protocomm_console.c also `#include
# <esp_console.h>`), because the real console/esp_console_common.c/esp_console_repl_uart.c etc.
# never get compiled at all. Confirmed empirically (task-4-report.md deviations). "devconsole"
# avoids the collision while PRIV_REQUIRES below unambiguously reaches the real IDF `console`.
#
# cmd_lt.c (Plan 5.6 Task 5): the `lt`/`link` commands, a manual relay to the lap-timer's framed
# console over linkhost's UART1 client, with round-trip timing (linkhost_cmd_timed) and a `link
# trace` debug hexdump. `lt config set <json>` reuses webapi's config_diff_escape (host/config_diff.c)
# so the wire escaping matches POST /api/config exactly -- hence the added webapi PRIV_REQUIRES
# below (one-directional: webapi never depends on devconsole).
#
# PRIV_REQUIRES: `console` is the IDF esp_console component (esp_console.h, used only inside this
# component); esp_driver_uart is `dc baud`'s uart_set_baudrate/uart_wait_tx_done; esp_wifi is `dc
# status`'s esp_wifi_ap_get_sta_list; esp_system is esp_get_free_heap_size; linkhost/logstore are
# the two subsystems `dc status` reports on; webapi is cmd_lt.c's config_diff_escape (above). None
# of these leak into console.h's public surface (console_start/console_wants_json/console_register
# are all plain C), so everything is PRIV.
idf_component_register(
    SRCS "console.c" "cmd_dc.c" "cmd_lt.c" "host/jsonw.c" "host/rate.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES console esp_driver_uart esp_wifi esp_system linkhost logstore webapi)

# build_config.h from the build tree (CFG_DC_VERSION, for `dc status`'s version field).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as the rest of B's own components (Plan 5.5 Global Constraints).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
