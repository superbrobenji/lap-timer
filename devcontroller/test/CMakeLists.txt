cmake_minimum_required(VERSION 3.16)
# Host test harness for the dev controller's PURE, IDF-free logic (mirrors the lap-timer's
# test/CMakeLists.txt). Each component's host-testable code lives in components/<c>/host/*.c and
# includes only IDF-free headers (its own <c>_proto.h + app/lt_proto.h) — never esp_*/driver/*.
# The IDF glue (uart/httpd/littlefs) stays in components/<c>/<c>.c and is on-target only.
# Run:  cmake -S devcontroller/test -B devcontroller/test/build -DCMAKE_BUILD_TYPE=Debug
#       cmake --build devcontroller/test/build && ctest --test-dir devcontroller/test/build --output-on-failure
project(devcontroller_host C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(DC_STRICT -Wall -Wextra -Werror -Wshadow -Wconversion
              -Wno-error=conversion -Wno-error=sign-conversion)

set(DC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/..)                 # devcontroller/
set(APP_INC ${CMAKE_CURRENT_SOURCE_DIR}/../../components/app/include)  # app/lt_proto.h (IDF-free)
set(UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unity/src)         # reuse the lap-timer's vendored Unity

# Pure host-testable sources: components/<c>/host/*.c. CONFIGURE_DEPENDS re-globs on build so a new
# component's host/ dir needs no edit here.
file(GLOB_RECURSE DC_HOST_SRCS CONFIGURE_DEPENDS ${DC_DIR}/components/*/host/*.c)
add_library(dchost STATIC ${DC_HOST_SRCS} ${CMAKE_CURRENT_SOURCE_DIR}/dc_host_stub.c)
target_include_directories(dchost PUBLIC ${APP_INC})
file(GLOB DC_COMP_INCS ${DC_DIR}/components/*/include)
target_include_directories(dchost PUBLIC ${DC_COMP_INCS})
target_compile_options(dchost PRIVATE ${DC_STRICT})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(dchost PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  target_link_options(dchost PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)

enable_testing()
# One executable per devcontroller/test/test_*.c, linking the pure host lib + Unity.
file(GLOB DC_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${DC_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_compile_options(${name} PRIVATE ${DC_STRICT})
  target_link_libraries(${name} PRIVATE dchost unity m)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
