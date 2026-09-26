if(CMAKE_BUILD_EARLY_EXPANSION)
    # CONFIGURE_DEPENDS is invalid in the script-mode early-expansion pass IDF uses to pull
    # each component's REQUIRES (§ build-system "early expansion"); SRCS is unused there anyway.
    file(GLOB_RECURSE CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
else()
    file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
endif()
# ui/render.c, ui/fonts.c, ui/icons.c (plan 04 session 4.1) and ui/screens_moto.c (session 4.2,
# the moto LAP/DRAG screens + fault-icon strip) are picked up by the CORE_SRCS glob above
# automatically. ui/include holds the ui/-scoped public headers (core/ui/fonts.h, core/ui/icons.h,
# core/ui/render.h, core/ui/model.h).
idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include ui/include)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)

# build_config.h from the build tree (§21.1), mirrored from components/app and main: without this,
# trk_json.c's `#if __has_include("build_config.h")` (Plan 7 Task 1 DRAM reclaim) silently resolves
# false even on a firmware build, and CFG_TRK_JSON_TOKS never overrides the host 512-token default.
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}")
