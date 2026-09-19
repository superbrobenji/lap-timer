/* assert_support.c -- shared host-test STRONG core_assert_report (spec §17.9). See assert_support.h.
 * Linking this TU into a test binary replaces core.c's weak no-op reporter with a recorder for the
 * whole binary; a test resets it, runs the code under test, then inspects the counters. */
#include "assert_support.h"

#include "core/core.h"
#include <stddef.h>

static unsigned    s_count;
static uint16_t    s_last_code;
static const char *s_last_file;
static int         s_last_line;

/* Strong override of core.c's weak core_assert_report. Records only; like every reporter it must
 * not itself call an assertion macro (that would recurse via core_assert_fail). */
void core_assert_report(uint16_t code, const char *file, int line)
{
    s_count++;
    s_last_code = code;
    s_last_file = file;
    s_last_line = line;
}

void        lt_test_assert_reset(void)     { s_count = 0; s_last_code = 0; s_last_file = NULL; s_last_line = 0; }
unsigned    lt_test_assert_count(void)     { return s_count; }
uint16_t    lt_test_assert_last_code(void) { return s_last_code; }
const char *lt_test_assert_last_file(void) { return s_last_file; }
int         lt_test_assert_last_line(void) { return s_last_line; }
