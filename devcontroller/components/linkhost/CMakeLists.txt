# linkhost -- the UART1 cmd-host that drives the lap-timer's shipped export_serial console
# (Plan 5.5 Tasks 2/3/6). Publishes the lap-timer's app/lt_proto.h alongside its own public
# header so linkhost.h's `#include "app/lt_proto.h"` resolves for every consumer; nothing here
# REQUIRES the lap-timer's `app` IDF component itself -- see the top-level CMakeLists.txt note
# for why (it would pull in the whole lap-timer app tree and its driver dependencies).
idf_component_register(
    SRCS "linkhost.c"
    INCLUDE_DIRS "include" "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/include"
    REQUIRES esp_driver_uart)

# build_config.h from the build tree (DC_LINK_UART/TX/RX/BAUD).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Pragmatic-P10-inspired (Plan 5.5 Global Constraints): same warning bar as the lap-timer's own
# components, no blocking linter for B.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
