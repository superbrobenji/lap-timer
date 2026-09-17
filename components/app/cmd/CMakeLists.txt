# components/app/cmd -- command protocol (spec §18.1), part of the `app` component.
#
# There is no idf_component_register() here: `cmd/` is a subdirectory of the `app` component,
# not a component of its own. components/app/CMakeLists.txt globs its sources recursively
# (file(GLOB_RECURSE ...)/*.c), so cmd.c is compiled, included and warning-checked exactly like
# the rest of `app`, and app/cmd.h is already on the `app` include path. This file exists only
# to document that cmd/ is the §18.1 command-protocol home (as named by app/lt_ipc.h); it is not
# processed by CMake (a component subdirectory's CMakeLists is never add_subdirectory'd by IDF).
