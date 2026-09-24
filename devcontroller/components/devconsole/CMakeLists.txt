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
# trace` debug hexdump. `lt config set <json>` escapes via linkhost's wire_escape (host/wire_escape.c)
# -- the same escaping POST /api/config uses -- so devconsole depends only on linkhost/logstore
# below, never on webapi (Plan 5.6 Task 5 fix 1 moved wire_escape out of webapi's config_diff
# specifically so this component would not need a webapi dependency just to escape one console
# argument).
#
# REQUIRES linkhost: console.h's console_stream_tap (Plan 5.6 Task 6) takes a `const
# lt_stream_rec_t *`, so console.h itself #includes linkhost_proto.h -- linkhost's public include
# dir must reach anyone who includes console.h, same reasoning as webapi's own REQUIRES linkhost.
#
# cmd_selftest.c (Plan 5.6 Task 9): `selftest link|stream|framing|all`, the dev-kit-native link
# qualification so it can be checked over USB with no WiFi AP. Drives linkhost_cmd_timed /
# linkhost_stats_snapshot / linkhost's raw reply capture (linkhost_rawcap_set +
# linkhost_trace_last_reply) and hands the raw numbers to host/selftest_eval.c -- the pure,
# host-tested verdict logic (test_selftest_eval.c) -- exactly the jsonw/rate.c split cmd_dc.c
# already uses.
#
# cmd_shell.c (Plan 5.6 Task 8): `lt shell`, a transparent raw-byte bridge between this console's
# own USB and the lap-timer's console over UART1 -- a subcommand of `lt` (cmd_lt.c dispatches to
# cmd_shell_run), not a command of its own, so it has no cmd_shell_register/console_register.
# Takes UART1 via linkhost_bridge_begin/_end and filters the lap-timer's autonomous 0xFF stream
# frames out of the forwarded bytes via linkhost's host/bridge_filter.c (pure, host-tested by
# test_bridge_filter.c).
#
# PRIV_REQUIRES: `console` is the IDF esp_console component (esp_console.h, used only inside this
# component); esp_driver_uart is `dc baud`'s uart_set_baudrate/uart_wait_tx_done; esp_wifi is `dc
# status`'s esp_wifi_ap_get_sta_list; esp_system is esp_get_free_heap_size; logstore is the other
# subsystem `dc status` reports on. None of these (linkhost aside) leak into console.h's public
# surface (console_start/console_wants_json/console_register/console_stream_tap are all plain C
# + the one linkhost_proto.h type), so everything but linkhost stays PRIV.
idf_component_register(
    SRCS "console.c" "cmd_dc.c" "cmd_lt.c" "cmd_stream.c" "cmd_selftest.c" "cmd_shell.c"
         "host/jsonw.c" "host/rate.c" "host/selftest_eval.c"
    INCLUDE_DIRS "include"
    REQUIRES linkhost
    PRIV_REQUIRES console esp_driver_uart esp_wifi esp_system logstore)

# build_config.h from the build tree (CFG_DC_VERSION, for `dc status`'s version field).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as the rest of B's own components (Plan 5.5 Global Constraints).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
