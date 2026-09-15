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
target_include_directories(core PUBLIC ${CORE_DIR}/include)
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

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no edit here)
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_core_test(${name})
endforeach()

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)
