# linkhost -- the UART1 cmd-host that drives the lap-timer's shipped export_serial console
# (Plan 5.5 Tasks 2/3/6). Publishes the lap-timer's app/lt_proto.h alongside its own public
# header so linkhost.h's `#include "app/lt_proto.h"` resolves for every consumer; nothing here
# REQUIRES the lap-timer's `app` IDF component itself -- see the top-level CMakeLists.txt note
# for why (it would pull in the whole lap-timer app tree and its driver dependencies).
#
# host/bridge_filter.c (Plan 5.6 Task 8): the pure §18.1 stream-frame remover `lt shell`'s bridge
# loop (devconsole/cmd_shell.c) runs every UART1 byte through before writing to USB -- see
# include/bridge_filter.h. linkhost_bridge_begin/_end (linkhost.c) hand the caller raw ownership of
# UART1 via the same s_req_mtx + rx_park_and_drain handshake linkhost_download_cmd/linkhost_flash
# already use.
idf_component_register(
    SRCS "linkhost.c" "host/linkhost_proto.c" "host/linkstats.c" "host/hexfmt.c" "host/wire_escape.c"
         "host/bridge_filter.c"
    INCLUDE_DIRS "include" "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/include"
    REQUIRES esp_driver_uart esp_timer esp_partition)

# build_config.h from the build tree (DC_LINK_UART/TX/RX/BAUD).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Pragmatic-P10-inspired (Plan 5.5 Global Constraints): same warning bar as the lap-timer's own
# components, no blocking linter for B.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
