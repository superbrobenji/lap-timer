idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../test/unity/src/unity.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../test/unity/src")
target_compile_definitions(${COMPONENT_LIB} PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
