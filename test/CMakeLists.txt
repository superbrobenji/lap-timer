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
  add_executable(${name} ${name}.c)
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE core unity m Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no
# edit here). test_lap is held back: its exit-criterion case drives the host-only synth fixture, so it
# links the replay library instead of core/unity/m directly (replaylib already carries those).
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  if(NOT name STREQUAL "test_lap")
    add_core_test(${name})
  endif()
endforeach()

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)

# test_lap links replaylib (which brings in core and libm), plus unity. The synth-driven case is guarded
# by #ifndef ESP_PLATFORM, so the same file still builds and runs on the ESP32 (core_selftest), where
# the replay tools do not exist.
add_executable(test_lap test_lap.c)
target_compile_options(test_lap PRIVATE ${LAPTIMER_STRICT_FLAGS})
target_link_libraries(test_lap PRIVATE replaylib unity Threads::Threads)
add_test(NAME test_lap COMMAND test_lap)
