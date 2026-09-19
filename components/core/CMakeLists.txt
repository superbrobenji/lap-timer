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
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
