# logstore -- bounded rotating black-box log on the `logs` LittleFS partition (Plan 5.5 Task 5).
# SRCS: logstore.c is the IDF/LittleFS glue; host/logstore_rot.c is the PURE rotation-decision
# logic and host/logstore_json.c is the PURE on-flash-record -> NDJSON transcode (issue #67) --
# neither has esp_* / littlefs deps -- that devcontroller/test/test_logstore.c and
# test_logstore_json.c also link directly on the host; both are compiled here too so the on-target
# build and the host tests share one implementation instead of two copies drifting apart.
#
# logstore.h's `#include "linkhost.h"` (for lt_stream_rec_t) in turn needs "app/lt_proto.h" --
# both resolved via direct INCLUDE_DIRS entries rather than REQUIRES linkhost, mirroring
# components/linkhost/CMakeLists.txt's own rationale: this pulls in just the header paths, not a
# REQUIRES on linkhost's .c (linkhost is a UART/esp_driver_uart component; logstore.c itself only
# needs its type definitions). host/logstore_json.c is the exception: it CALLS
# linkhost_stream_to_json (components/linkhost/host/linkhost_proto.c), so PRIV_REQUIRES linkhost
# below actually links that symbol in, not just its header. REQUIRES the littlefs managed
# component directly since logstore.c calls esp_vfs_littlefs_register/esp_littlefs_mounted itself
# (main.c already mounts `logs`, but logstore_init tolerates being the first/only mounter too).
# esp_timer is needed for the esp_timer_get_time() per-record monotonic-us stamp (implementation
# detail, not exposed via logstore.h, hence PRIV_REQUIRES alongside littlefs).
idf_component_register(
    SRCS "logstore.c" "host/logstore_rot.c" "host/logstore_json.c"
    INCLUDE_DIRS "include"
                 "${CMAKE_CURRENT_LIST_DIR}/../linkhost/include"
                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/app/include"
    PRIV_REQUIRES joltwallet__littlefs esp_timer linkhost)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
