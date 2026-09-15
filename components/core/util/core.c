#include "core/core.h"
#include <stddef.h>

const char *core_version(void) { return "0.0.1"; }

static core_assert_hook_t assert_hook;

void core_set_assert_hook(core_assert_hook_t hook) { assert_hook = hook; }

void core_assert_fail(uint16_t code, const char *file, int line)
{
    if (assert_hook) assert_hook(code, file, line);
}
