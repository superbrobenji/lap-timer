set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../test")
# One wrapper TU per host test file, discovered rather than listed, so a new test/test_*.c is picked
# up without editing this file. The glob cannot use CONFIGURE_DEPENDS: ESP-IDF also evaluates
# component CMakeLists.txt in script mode while expanding requirements, where that keyword is an
# error. The directory property at the bottom does the same job -- adding or removing a file changes
# the directory's timestamp, which re-runs configuration and so re-runs this glob.
file(GLOB TEST_SRCS "${TEST_DIR}/test_*.c")
# test_ui and test_screens are host-only: they render PBM snapshots to the host filesystem
# (TEST_DIR/snapshots) and compare byte-exact, which cannot run on the ESP32.
# test_epd_pure_29 is excluded for a different reason (audit drv:L25, 2026-10-13): it needs
# host/epd_panel.c compiled with EPD_FORCE_PANEL_29, while test_epd_pure needs the same single TU
# compiled with EPD_FORCE_PANEL_213 -- a component compiles that source once, so only the owned
# 2.13" panel row is proven on the target toolchain here. test_batt_trim links the board driver's
# host/batt_trim.c and include dir (batt_pure.h), which this selftest does not carry, so it is
# host-only as well. Everything else that only needs Unity plus a pure host/*.c source now runs on
# target: test_epd_pure (panel table, portrait rotation, RAM-window rounding) and
# test_sim_profile / test_sim_clock (sim drag profile + gps_us clock).
list(FILTER TEST_SRCS EXCLUDE REGEX "/test_(ui|screens|epd_pure_29|batt_trim)\\.c$")
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
# APP_POLICY_SRC: test/test_sup_policy.c (audit fix wave F2a) exercises app/sup_policy.c, which is
# IDF-free by design (see app/sup_policy.h) -- so the suite runs on the Xtensa toolchain too, per
# §22.3, by compiling that ONE app source here rather than by REQUIRES app (which would drag
# nvs_flash/gps/imu/display into this core-only selftest). It needs no include dir of its own:
# APP_INCLUDE_DIR below already resolves "app/sup_policy.h", "app/lt_consts.h" and
# "app/lt_assert.h".
set(APP_POLICY_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/sys/sup_policy.c")

# APP_LOG_PAUSE_SRC: test/test_log_pause.c exercises app/log_pause.c (the §12.6 still-pause state
# machine), which is pure C with no IDF deps -- compiled here the same way APP_POLICY_SRC is, so the
# suite also runs on the Xtensa toolchain (§22.3) without REQUIRES app.
set(APP_LOG_PAUSE_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/logger/log_pause.c")

# APP_SES_POLICY_SRC: test/test_session_policy.c exercises app/session_policy.c (the §12.1 session
# lifecycle decision table), pure C with no IDF deps -- compiled the same way as APP_LOG_PAUSE_SRC.
set(APP_SES_POLICY_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/pipeline/session_policy.c")

# APP_INCLUDE_DIR: test_proto.c includes app/lt_proto.h, a pure standalone header (stdint.h only,
# no ESP-IDF/FreeRTOS deps -- see components/app/include/app/lt_proto.h). Adding just the app
# component's include root (not REQUIRES app) resolves that #include without pulling app's full
# sources/deps (nvs_flash, gps/imu HAL, ...) into this lightweight core-only selftest.
set(APP_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/include")
set(EPD_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/drivers/display_epaper_ssd1680")
set(SIM_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/drivers/sim_common")
set(LT_HAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/lt_hal")
idf_component_register(SRCS "main.c" "${TEST_DIR}/assert_support.c" "${APP_POLICY_SRC}" "${APP_LOG_PAUSE_SRC}" "${APP_SES_POLICY_SRC}" ${WRAPPERS}
                            "${EPD_DIR}/host/epd_panel.c" "${EPD_DIR}/host/epd_rotate.c"
                            "${SIM_COMMON_DIR}/host/sim_profile.c"
                            "${SIM_COMMON_DIR}/host/sim_clock.c"
                       INCLUDE_DIRS "." "${TEST_DIR}" "${APP_INCLUDE_DIR}"
                                    "${EPD_DIR}/include" "${SIM_COMMON_DIR}/include"
                                    "${LT_HAL_DIR}/include"
                       REQUIRES core unity_vendored pthread esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE RING_STRESS_N=20000ULL EPD_FORCE_PANEL_213=1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_DIR})
