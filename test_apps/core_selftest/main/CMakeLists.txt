set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../test")
# One wrapper TU per host test file, discovered rather than listed, so a new test/test_*.c is picked
# up without editing this file. The glob cannot use CONFIGURE_DEPENDS: ESP-IDF also evaluates
# component CMakeLists.txt in script mode while expanding requirements, where that keyword is an
# error. The directory property at the bottom does the same job -- adding or removing a file changes
# the directory's timestamp, which re-runs configuration and so re-runs this glob.
file(GLOB TEST_SRCS "${TEST_DIR}/test_*.c")
set(SUITES "")
foreach(f ${TEST_SRCS})
  get_filename_component(base "${f}" NAME_WE)          # .../test_cfg.c -> test_cfg
  string(REGEX REPLACE "^test_" "" s "${base}")        # test_cfg       -> cfg
  list(APPEND SUITES "${s}")
endforeach()
list(SORT SUITES)
set(WRAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/wrap")
file(MAKE_DIRECTORY "${WRAP_DIR}")
set(WRAPPERS "")
foreach(s ${SUITES})
  set(w "${WRAP_DIR}/wrap_test_${s}.c")
  file(WRITE "${w}" "#define main run_test_${s}\n#define setUp setUp_test_${s}\n#define tearDown tearDown_test_${s}\n#include \"${TEST_DIR}/test_${s}.c\"\n")
  list(APPEND WRAPPERS "${w}")
endforeach()
# suites_gen.h: one SUITE(name) line per globbed test, sorted, consumed twice as an X-macro by
# main.c -- so a new test/test_*.c is always declared, built into the suite table and run; it
# cannot be linked but silently skipped.
set(SUITES_GEN "${CMAKE_CURRENT_BINARY_DIR}/suites_gen.h")
set(SUITES_GEN_CONTENT "")
foreach(s ${SUITES})
  string(APPEND SUITES_GEN_CONTENT "SUITE(${s})\n")
endforeach()
file(WRITE "${SUITES_GEN}" "${SUITES_GEN_CONTENT}")
idf_component_register(SRCS "main.c" ${WRAPPERS} INCLUDE_DIRS "." REQUIRES core unity_vendored pthread esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE RING_STRESS_N=20000ULL)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-parameter)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_DIR})
