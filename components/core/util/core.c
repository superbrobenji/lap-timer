#include "core/core.h"
#include <stddef.h>
#include <stdatomic.h>

const char *core_version(void) { return "0.0.1"; }

/* _Atomic with explicit acquire/release (as ring.h already does for the SPSC ring) so installing
 * the hook from core 0 while core 1 is mid-read of it is defined behaviour, not a data race. */
static _Atomic core_assert_hook_t assert_hook;

void core_set_assert_hook(core_assert_hook_t hook) { atomic_store_explicit(&assert_hook, hook, memory_order_release); }

void core_assert_fail(uint16_t code, const char *file, int line)
{
    core_assert_hook_t hook = atomic_load_explicit(&assert_hook, memory_order_acquire);
    if (hook) hook(code, file, line);
}
