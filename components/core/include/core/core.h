#ifndef CORE_CORE_H
#define CORE_CORE_H
#include <stdint.h>

const char *core_version(void);

/* Core assertions (spec §17.9). A failing check reports `code` through core_assert_report() and
 * returns an error to the caller; it never aborts on target. */
void core_assert_fail(uint16_t code, const char *file, int line);

/* The reporter core_assert_fail() calls on every failed CORE_ASSERT_/LT_ASSERT_. It is a plain
 * link-time function, not a stored/registered pointer (Power of 10 rule 9). Core ships a WEAK
 * no-op default (silent), so a core-only link -- e.g. the tools/replay tool -- reports nothing.
 * The app provides a STRONG override (sup_errlog.c) that logs the code into the §17.7 error ring;
 * host tests provide one that records it. It must accept every (code, file, line) a failing
 * assertion anywhere can produce, and must not itself call an assertion macro (that would recurse
 * back through core_assert_fail). */
void core_assert_report(uint16_t code, const char *file, int line);

#define CORE_ASSERT_RET(cond, code, ret) do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return (ret); } } while (0)
#define CORE_ASSERT_VOID(cond, code)     do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return; } } while (0)
#endif
