/* dk:M10 -- what GET /api/status's `logging` (and `dc status`'s logstore.ready) must report. The
 * two inputs are deliberately separate: `open_ok` is "a log file is open" (cleared only when
 * open_new_file fails, which needs a re-init), `write_ok` is "the last append actually wrote". A
 * persistent write error must read false, and must return to true on the next successful append --
 * which is why it cannot be folded into open_ok: logstore_append() early-returns on !open_ok, so a
 * single latch could never recover. */
#include "unity.h"
#include "logstore_rot.h"
void setUp(void) {}
void tearDown(void) {}
static void test_accepting_is_open_and_writing(void)
{
    TEST_ASSERT_TRUE(logstore_accepting(true, true));
    TEST_ASSERT_FALSE(logstore_accepting(true, false));
    TEST_ASSERT_FALSE(logstore_accepting(false, true));
    TEST_ASSERT_FALSE(logstore_accepting(false, false));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_accepting_is_open_and_writing); return UNITY_END(); }
