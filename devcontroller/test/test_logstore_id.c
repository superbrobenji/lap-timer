/* test_logstore_id.c -- host tests for logstore's generated-id shape validator
 * (components/logstore/host/logstore_id.c, debt sweep A #60 fix round 1): guards
 * webapi.c's do_log_download (GET /api/log/<id>) before logstore_open_read/build_path.
 */
#include "unity.h"
#include "logstore_id.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* A real generated id ("log_%08u", logstore.c's open_new_file/scan_existing) passes. */
void test_accepts_real_generated_id(void)
{
    TEST_ASSERT_TRUE(logstore_id_ok("log_00000042", 12));
    TEST_ASSERT_TRUE(logstore_id_ok("log_00000000", 12));
    TEST_ASSERT_TRUE(logstore_id_ok("log_99999999", 12));
}

/* Path traversal / separators, empty, wrong prefix, non-digit suffix, and over/under-length are
 * all rejected -- nothing but logstore.c's own exact "log_" + 8-digit shape passes. */
void test_rejects_traversal_and_shape(void)
{
    TEST_ASSERT_FALSE(logstore_id_ok("", 0));
    TEST_ASSERT_FALSE(logstore_id_ok("..", 2));
    TEST_ASSERT_FALSE(logstore_id_ok("a/b", 3));
    TEST_ASSERT_FALSE(logstore_id_ok("../../etc/passwd", strlen("../../etc/passwd")));
    TEST_ASSERT_FALSE(logstore_id_ok("log_0000042", 11));       /* one digit short (7 digits) */
    TEST_ASSERT_FALSE(logstore_id_ok("log_000000420", 13));     /* one char over-length */
    TEST_ASSERT_FALSE(logstore_id_ok("log_0000004x", 12));      /* non-digit in the suffix */
    TEST_ASSERT_FALSE(logstore_id_ok("session_0001", 12));      /* wrong prefix, same length */
    TEST_ASSERT_FALSE(logstore_id_ok(NULL, 12));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_real_generated_id);
    RUN_TEST(test_rejects_traversal_and_shape);
    return UNITY_END();
}
