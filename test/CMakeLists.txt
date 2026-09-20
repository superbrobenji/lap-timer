cmake_minimum_required(VERSION 3.16)
project(laptimer_host C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# One strict flag set for core, tools and tests (spec §17.9, §21.4).
set(LAPTIMER_STRICT_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/core)
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)

add_library(core STATIC ${CORE_SRCS})
# ui/ headers live under their own include/ root (components/core/ui/include/core/ui/*.h),
# mirroring the ESP-IDF component's INCLUDE_DIRS (components/core/CMakeLists.txt).
target_include_directories(core PUBLIC ${CORE_DIR}/include ${CORE_DIR}/ui/include)
target_compile_options(core PRIVATE ${LAPTIMER_STRICT_FLAGS})
# vendored third-party sources get relaxed warnings (file added in Task 8)
set_source_files_properties(${CORE_DIR}/util/jsmn.c PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-unused-function")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # -fno-sanitize-recover makes a UBSan finding abort the test run instead of printing and continuing,
  # so a ctest pass really means no undefined behaviour was executed.
  target_compile_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  target_link_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)

enable_testing()
find_package(Threads REQUIRED)
function(add_core_test name)
  # Extra ARGN sources let one test link a host-only helper that isn't itself a test_*.c (so the
  # glob below doesn't pick it up as its own suite) — e.g. test_ui + pbm.c.
  add_executable(${name} ${name}.c ${ARGN})
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE core unity m Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no
# edit here). test_lap, test_ui and test_screens are held back from the generic loop and added
# explicitly below: test_lap's exit-criterion case drives the host-only synth fixture, so it links
# the replay library instead of core/unity/m directly (replaylib already carries those); test_ui
# and test_screens (session 4.2, moto LAP screens, §20.4-20.5) both link pbm.c (§20.2's PBM
# snapshot writer/comparator, test/pbm.h) alongside core/unity/m.
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  if(NOT name STREQUAL "test_lap" AND NOT name STREQUAL "test_ui" AND NOT name STREQUAL "test_screens"
     AND NOT name STREQUAL "test_smoke" AND NOT name STREQUAL "test_ses_frame")
    add_core_test(${name})
  endif()
endforeach()

# test_proto only needs the shared lap-timer <-> dev-controller protocol header (app/lt_proto.h,
# a pure standalone header -- stdint.h only, no ESP-IDF/FreeRTOS deps), so it just needs the app
# component's include root on its path for `#include "app/lt_proto.h"` to resolve (mirrors the
# INCLUDE_DIRS the on-device build gets from components/app/CMakeLists.txt).
if(TARGET test_proto)
  target_include_directories(test_proto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../components/app/include)
endif()

# test_smoke and test_ses_frame inspect failed CORE_ASSERT_ reports, so they link assert_support.c
# (test/assert_support.h) -- the shared STRONG core_assert_report recorder that overrides core.c's
# weak no-op default for those binaries. It is not a test_*.c, so the glob above never builds it as
# its own suite.
add_core_test(test_smoke assert_support.c)
add_core_test(test_ses_frame assert_support.c)

# test_ui and test_screens compare rendered framebuffers against PBM goldens in test/snapshots/ by
# absolute path (TEST_DIR), since CTest runs a test's working directory from the build tree, not
# this source dir.
add_core_test(test_ui pbm.c)
target_compile_definitions(test_ui PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_core_test(test_screens pbm.c)
target_compile_definitions(test_screens PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)

# test_lap links replaylib (which brings in core and libm), plus unity. The synth-driven case is guarded
# by #ifndef ESP_PLATFORM, so the same file still builds and runs on the ESP32 (core_selftest), where
# the replay tools do not exist.
add_executable(test_lap test_lap.c)
target_compile_options(test_lap PRIVATE ${LAPTIMER_STRICT_FLAGS})
target_link_libraries(test_lap PRIVATE replaylib unity Threads::Threads)
add_test(NAME test_lap COMMAND test_lap)
