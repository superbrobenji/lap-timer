set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../test")
# One wrapper TU per host test file, discovered rather than listed, so a new test/test_*.c is picked
# up without editing this file. The glob cannot use CONFIGURE_DEPENDS: ESP-IDF also evaluates
# component CMakeLists.txt in script mode while expanding requirements, where that keyword is an
# error. The directory property at the bottom does the same job -- adding or removing a file changes
# the directory's timestamp, which re-runs configuration and so re-runs this glob.
file(GLOB TEST_SRCS "${TEST_DIR}/test_*.c")
# test_ui and test_screens are host-only: they render PBM snapshots to the host filesystem
# (TEST_DIR/snapshots) and compare byte-exact, which cannot run on the ESP32. They are exercised
# by the host `test/` build only.
list(FILTER TEST_SRCS EXCLUDE REGEX "/test_(ui|screens)\\.c$")
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
# assert_support.c is the shared STRONG core_assert_report recorder + lt_test_assert_* helpers
# (test/assert_support.h) that test_smoke/test_ses_frame call. It is not a test_*.c suite, so the
# glob above skips it; compile it directly so the on-target link resolves lt_test_assert_* and the
# strong recorder overrides core.c's weak core_assert_report default.
#
# APP_INCLUDE_DIR: test_proto.c includes app/lt_proto.h, a pure standalone header (stdint.h only,
# no ESP-IDF/FreeRTOS deps -- see components/app/include/app/lt_proto.h). Adding just the app
# component's include root (not REQUIRES app) resolves that #include without pulling app's full
# sources/deps (nvs_flash, gps/imu HAL, ...) into this lightweight core-only selftest.
set(APP_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/include")
idf_component_register(SRCS "main.c" "${TEST_DIR}/assert_support.c" ${WRAPPERS}
                       INCLUDE_DIRS "." "${TEST_DIR}" "${APP_INCLUDE_DIR}"
                       REQUIRES core unity_vendored pthread esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE RING_STRESS_N=20000ULL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_DIR})
