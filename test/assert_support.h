#ifndef LT_TEST_ASSERT_SUPPORT_H
#define LT_TEST_ASSERT_SUPPORT_H
#include <stdint.h>

/* Shared host-test support (spec §17.9). Provides the STRONG core_assert_report that overrides
 * core.c's weak no-op default across a test binary, recording each failed CORE_ASSERT_/LT_ASSERT_
 * so a test can assert whether the assertion fired and inspect its code/file/line. Link this TU
 * (test/CMakeLists.txt) into any test binary that inspects assertions. There is no runtime "off"
 * state -- reset the recorder before the code under test, then assert on the counters. */
void        lt_test_assert_reset(void);
unsigned    lt_test_assert_count(void);
uint16_t    lt_test_assert_last_code(void);
const char *lt_test_assert_last_file(void);
int         lt_test_assert_last_line(void);

#endif /* LT_TEST_ASSERT_SUPPORT_H */
