# gps_neo6m -- PLAN-03 STUB of hal/gps.h (spec §4.6 GPS=neo6m). The real UBX driver lands in plan 08;
# this only keeps moto_neo6m building now that the pipeline calls the GPS HAL. See gps_neo6m.c.
idf_component_register(
    SRCS "gps_neo6m.c"
    REQUIRES lt_hal core)

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
