# Host-only tools (spec §21.4, §22.2). Built by test/CMakeLists.txt through add_subdirectory; never an
# ESP-IDF component. Sources are globbed so parallel tasks add files without touching this list.
file(GLOB REPLAY_LIB_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)
add_library(replaylib STATIC ${REPLAY_LIB_SRCS})
target_include_directories(replaylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(replaylib PUBLIC core m)
target_compile_options(replaylib PRIVATE ${LAPTIMER_STRICT_FLAGS})

foreach(tool synth replay)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_main.c)
    add_executable(${tool} ${tool}_main.c)
    target_link_libraries(${tool} PRIVATE replaylib)
    target_compile_options(${tool} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  endif()
endforeach()

file(GLOB REPLAY_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/test_*.c)
foreach(src ${REPLAY_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE replaylib unity Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# test_replay_fixtures reads the committed fixtures under test/data (spec §22.2).
if(TARGET test_replay_fixtures)
  target_compile_definitions(test_replay_fixtures PRIVATE
    REPLAY_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../test/data")
endif()

if(TARGET synth AND TARGET replay)
  add_test(NAME synth_smoke COMMAND synth --out ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke --laps 2 --quiet)
  add_test(NAME replay_smoke COMMAND replay ${CMAKE_CURRENT_BINARY_DIR}/synth_smoke.log --json)
  set_tests_properties(replay_smoke PROPERTIES DEPENDS synth_smoke PASS_REGULAR_EXPRESSION "\"bad_frames\":0")
endif()
