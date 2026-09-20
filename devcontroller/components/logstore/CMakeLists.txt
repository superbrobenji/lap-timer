# logstore -- bounded rotating black-box log on the `logs` LittleFS partition (Plan 5.5 Task 5).
# REQUIRES linkhost (public) so logstore.h's `#include "linkhost.h"` (for lt_stream_rec_t)
# resolves for every consumer too.
idf_component_register(
    SRCS "logstore.c"
    INCLUDE_DIRS "include"
    REQUIRES linkhost)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations
    -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
