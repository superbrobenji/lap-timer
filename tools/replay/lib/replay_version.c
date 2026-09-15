#include "replay/replay.h"
#include "core/core.h"

const char *replay_version(void)
{
    return core_version();
}
