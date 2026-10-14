/* devcontroller/test/test_www_path.c -- host tests for the PURE static-asset path whitelist
 * (components/webapi/host/www_path.c, F3 Task 4 Step 1).
 *
 * dk:L4 -- static_file built "/www" + req->uri with no normalization and handed `..` segments to
 * LittleFS. Containment was incidental (lfs clamps at the mount root), not asserted. */
#include "unity.h"
#include "www_path.h"

#include <string.h>

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

/* fix round 1 finding 2: the scan must be structurally bounded by WWW_URI_MAX (a byte count),
 * not by scanning for a NUL that the bound check only notices one index late. A URI of exactly
 * WWW_URI_MAX bytes is still in-contract and must pass; one byte longer must be rejected; and a
 * buffer with NO NUL anywhere within WWW_URI_MAX+1 bytes must be rejected WITHOUT the scan ever
 * reading past index WWW_URI_MAX (ASan catches an out-of-bounds read if it does). */
static void test_uri_exactly_at_the_length_limit_passes(void)
{
    char uri[WWW_URI_MAX + 1];           /* WWW_URI_MAX content bytes + NUL, right at the limit */
    memset(uri, 'a', WWW_URI_MAX);
    uri[0] = '/';
    uri[WWW_URI_MAX] = '\0';
    TEST_ASSERT_TRUE(www_path_ok(uri));
}
static void test_uri_one_byte_past_the_limit_is_rejected(void)
{
    char uri[WWW_URI_MAX + 2];           /* WWW_URI_MAX+1 content bytes + NUL: one byte too long */
    memset(uri, 'a', WWW_URI_MAX + 1u);
    uri[0] = '/';
    uri[WWW_URI_MAX + 1] = '\0';
    TEST_ASSERT_FALSE(www_path_ok(uri));
}
static void test_uri_with_no_terminator_within_the_limit_is_rejected(void)
{
    /* No NUL anywhere in this WWW_URI_MAX+1-byte array -- a structurally-bounded scan must reject
     * without ever dereferencing index WWW_URI_MAX+1 (out of this array's bounds). */
    char uri[WWW_URI_MAX + 1];
    memset(uri, 'a', sizeof uri);
    uri[0] = '/';
    TEST_ASSERT_FALSE(www_path_ok(uri));
}

int main(void) { UNITY_BEGIN(); RUN_TEST(test_normal_assets_pass);
    RUN_TEST(test_traversal_and_junk_are_rejected);
    RUN_TEST(test_uri_exactly_at_the_length_limit_passes);
    RUN_TEST(test_uri_one_byte_past_the_limit_is_rejected);
    RUN_TEST(test_uri_with_no_terminator_within_the_limit_is_rejected);
    return UNITY_END(); }
