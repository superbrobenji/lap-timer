# display_epaper_ssd1680 -- hal/display.h over a Waveshare SSD1680 e-paper (spec §20.1; Plan 7).
idf_component_register(
    SRCS "display_epaper.c" "host/epd_panel.c" "host/epd_rotate.c"
    INCLUDE_DIRS "include"
    REQUIRES lt_hal core esp_driver_spi esp_driver_gpio esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
