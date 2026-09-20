# export_serial -- §18.4 serial fallback export console (esp_console REPL on UART0).
# Gated ON/OFF by the EXPORT_SERIAL build flag (§4.6): the top-level CMakeLists appends this dir
# to EXTRA_COMPONENT_DIRS only when EXPORT_SERIAL is ON. It wires the §18.1 ops through app/cmd
# and carries the migrated dbg verbs, so it requires `app` (cmd_dispatch, logger, lt_nvs, lt_rtc,
# pipeline, ipc), lt_hal (hal/storage.h), core (ses/types/event) and the IDF `console` component.
idf_component_register(
    SRCS "export_serial.c"
    INCLUDE_DIRS "include"
    REQUIRES app lt_hal core console esp_timer esp_system esp_driver_uart)

# build_config.h from the build tree (§21.1): export_serial reads CFG_HAS_DEVUX to gate the
# interactive `dbg` UX from the always-on cmd + stream transport (§4.6, Plan 5 sub-project A).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9); conversion warnings non-fatal for IDF macros.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
