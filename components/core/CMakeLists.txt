file(GLOB_RECURSE CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
