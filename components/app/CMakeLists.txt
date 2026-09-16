# app -- IDF-dependent glue (spec §4.1): NVS layer (§15.2), RTC state (§15.3), supervisor
# (§4.3/§17.2), shared runtime state (hb[]/sys_flags, §4.4/§17.4), and the minimal dbg console.
# No collision with an IDF built-in (there is no IDF component named `app`).
file(GLOB_RECURSE APP_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS include
    REQUIRES core lt_hal nvs_flash esp_timer esp_system console)

# build_config.h (CFG_HAS_PPS etc.) from the build tree (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
