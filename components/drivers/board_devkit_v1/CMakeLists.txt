# board_devkit_v1 -- ESP32 DevKit V1 board HAL implementation (spec §3.3 pin map, §5.1 board.h).
idf_component_register(
    SRCS "board.c"
    REQUIRES lt_hal driver esp_adc esp_timer esp_hw_support core)

# build_config.h (CFG_HAS_PPS etc.) is generated into the build tree by the top-level CMake (§21.1).
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")

# Same strictness as components/core (§17.9): -Werror, conversion warnings non-fatal so IDF
# macros/inline helpers do not break the build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
