/* app/lt_assert.h -- LT_ASSERT_RET/LT_ASSERT_VOID, the app/driver-layer Power of 10 rule 5
 * assertion macros (design docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md
 * §3 rule 5; plan docs/superpowers/plans/2026-09-17-plan-4.5-power-of-10.md Session 4.5.1).
 *
 * These mirror core/core.h's CORE_ASSERT_RET/CORE_ASSERT_VOID exactly, for app/driver/main
 * code, and call the SAME reporter: on failure they report `code` through
 * core_assert_fail()/the installed core_assert_hook_t (core_set_assert_hook, §17.9) -- the app
 * installs a hook (sup_install_assert_hook) that logs the code into the §17.7 error ring -- and
 * then return a safe value to the caller. They NEVER abort/panic on target: a failing
 * LT_ASSERT_* is a reported, recovered anomaly, not a crash. Assertions must be side-effect-free
 * (the condition is only ever evaluated, never relied on to run code).
 *
 * Session 4.5.1 is tooling-only: this header is new and nothing calls it yet, so there is no
 * behavior change. Rule 5 retrofit (raising assertion density in components/app/drivers/main)
 * lands in later Plan 4.5 sessions.
 */
#ifndef APP_LT_ASSERT_H
#define APP_LT_ASSERT_H

#include <stdint.h>

#include "core/core.h"

#define LT_ASSERT_RET(cond, code, ret) \
    do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return (ret); } } while (0)

#define LT_ASSERT_VOID(cond, code) \
    do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return; } } while (0)

#endif
