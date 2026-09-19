#include "core/core.h"

const char *core_version(void) { return "0.0.1"; }

/* Weak no-op default reporter (spec §17.9): a core-only link (e.g. the tools/replay tool, which
 * provides no override) stays silent -- today's "silent unless overridden" behaviour. The app
 * and host tests supply a STRONG core_assert_report that wins at link time. A weak plain function
 * is not a stored/registered pointer, so this is Power of 10 rule 9 compliant. */
__attribute__((weak)) void core_assert_report(uint16_t code, const char *file, int line)
{
    (void)code; (void)file; (void)line;
}

void core_assert_fail(uint16_t code, const char *file, int line)
{
    core_assert_report(code, file, line);
}
