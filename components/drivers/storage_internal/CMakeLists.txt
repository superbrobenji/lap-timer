# storage_internal -- hal/storage.h implemented over LittleFS (spec §13.1).
#
# LittleFS is not in ESP-IDF core; joltwallet/littlefs is the ecosystem-standard component,
# pulled (pinned) via idf_component.yml by the IDF component manager -- using the standard, not
# reinventing it. The dependency is private: hal/storage.h exposes no esp_littlefs type, so a
# consumer of this driver never sees the managed component.
idf_component_register(
    SRCS "storage_internal.c"
    REQUIRES lt_hal
    PRIV_REQUIRES joltwallet__littlefs)

# Same strictness as components/core (§17.9): -Werror, conversion warnings non-fatal so IDF /
# esp_littlefs macros and inline helpers do not break the build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
