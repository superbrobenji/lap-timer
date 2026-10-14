/* devcontroller/test/test_www_path.c -- host tests for the PURE static-asset path whitelist
 * (components/webapi/host/www_path.c, F3 Task 4 Step 1).
 *
 * dk:L4 -- static_file built "/www" + req->uri with no normalization and handed `..` segments to
 * LittleFS. Containment was incidental (lfs clamps at the mount root), not asserted. */
#include "unity.h"
#include "www_path.h"
void setUp(void) {}
void tearDown(void) {}
static void test_normal_assets_pass(void)
{
    TEST_ASSERT_TRUE(www_path_ok("/index.html"));
    TEST_ASSERT_TRUE(www_path_ok("/app.js"));
    TEST_ASSERT_TRUE(www_path_ok("/assets/app.css"));
    TEST_ASSERT_TRUE(www_path_ok("/a-b_c.1.png"));
}
static void test_traversal_and_junk_are_rejected(void)
{
    TEST_ASSERT_FALSE(www_path_ok("/../nvs.bin"));
    TEST_ASSERT_FALSE(www_path_ok("/assets/../../logs/1.bin"));
    TEST_ASSERT_FALSE(www_path_ok("/.."));
    TEST_ASSERT_FALSE(www_path_ok("//double"));
    TEST_ASSERT_FALSE(www_path_ok("no-leading-slash"));
    TEST_ASSERT_FALSE(www_path_ok("/with space.html"));
    TEST_ASSERT_FALSE(www_path_ok("/ctrl\x01.html"));
    TEST_ASSERT_FALSE(www_path_ok(""));
    TEST_ASSERT_FALSE(www_path_ok(NULL));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_normal_assets_pass);
    RUN_TEST(test_traversal_and_junk_are_rejected); return UNITY_END(); }
