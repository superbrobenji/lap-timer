cmake_minimum_required(VERSION 3.16)
# components/core is the library under test; components/ holds the vendored Unity wrapper
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/core" "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(core_selftest)
