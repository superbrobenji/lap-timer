cmake_minimum_required(VERSION 3.16)
# components/core is the library under test; components/ holds the vendored Unity wrapper
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/core" "${CMAKE_CURRENT_LIST_DIR}/components")
# IDF ships its own "unity" component (and "cmock", which requires it). Its component name collides
# with the "unity_vendored" name only in spirit, not in CMake target name -- but both compile a
# translation unit exporting the *same* global Unity C symbols (UnityBegin, UnityAssertEqualNumber, ...).
# IDF's copy is built without our UNITY_SUPPORT_64/UNITY_INCLUDE_DOUBLE configuration, so when both
# libunity.a (IDF's) and libunity_vendored.a (ours) land on the link line, the linker silently
# resolves Unity symbols from whichever archive it scans first -- observed to be IDF's, with a
# mismatched calling ABI, corrupting every assertion call. Exclude IDF's unity/cmock entirely so this
# project links only the pinned vendored Unity 2.6.0 in components/unity_vendored.
set(EXCLUDE_COMPONENTS "unity" "cmock")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(core_selftest)
