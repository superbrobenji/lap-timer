/* test_session_id.c -- host tests for the shared lap-timer <-> dev-controller session-id
 * validator (app/lt_proto.h's lt_session_id_ok, debt sweep A #60), compiled into the dev-kit's
 * own harness so the shared header is exercised by both sides independently (P-2). The same two
 * test functions appear verbatim in test/test_proto.c on the lap-timer side.
 */
#include "unity.h"
#include "app/lt_proto.h"

void setUp(void) {}
void tearDown(void) {}

void test_session_id_accepts_real_ids(void) {
    TEST_ASSERT_TRUE(lt_session_id_ok("S00001_001", 10));
    TEST_ASSERT_TRUE(lt_session_id_ok("abc", 3));
    TEST_ASSERT_TRUE(lt_session_id_ok("A_1", 3));
}
void test_session_id_rejects_traversal_and_shape(void) {
    TEST_ASSERT_FALSE(lt_session_id_ok("", 0));
    TEST_ASSERT_FALSE(lt_session_id_ok("S00001_0011", 11));
    TEST_ASSERT_FALSE(lt_session_id_ok("..", 2));
    TEST_ASSERT_FALSE(lt_session_id_ok("a/b", 3));
    TEST_ASSERT_FALSE(lt_session_id_ok("a.b", 3));
    TEST_ASSERT_FALSE(lt_session_id_ok("S00001_00 ", 10));
    TEST_ASSERT_FALSE(lt_session_id_ok("-", 1));
    TEST_ASSERT_FALSE(lt_session_id_ok(NULL, 3));
    TEST_ASSERT_FALSE(lt_session_id_ok("ab\0cd", 5));           /* NUL inside the counted range */
    TEST_ASSERT_FALSE(lt_session_id_ok("\xC3\xA9", 2));         /* bytes >= 0x80 (UTF-8 "e") */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_session_id_accepts_real_ids);
    RUN_TEST(test_session_id_rejects_traversal_and_shape);
    return UNITY_END();
}
