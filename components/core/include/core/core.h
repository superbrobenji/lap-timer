#ifndef CORE_CORE_H
#define CORE_CORE_H
#include <stdint.h>

const char *core_version(void);

/* Core assertions (spec §17.9). A failing check reports `code` through the hook and returns an
 * error to the caller; it never aborts on target. The app installs a hook that logs the code into
 * the error ring; host tests install one that records it. NULL (the default) is silent. */
typedef void (*core_assert_hook_t)(uint16_t code, const char *file, int line);
void core_set_assert_hook(core_assert_hook_t hook);       /* NULL = silent */
void core_assert_fail(uint16_t code, const char *file, int line);

#define CORE_ASSERT_RET(cond, code, ret) do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return (ret); } } while (0)
#define CORE_ASSERT_VOID(cond, code)     do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return; } } while (0)
#endif
