/* test_stage_args.c -- Plan 5.6 (dev-kit as primary interface) Task 7 Step 1: the pure
 * `<size> <64 hex>` argument grammar shared by POST /api/flash's future console twin (`flash
 * stage`). No IDF, no console -- just argv in, size/sha out.
 */
#include "unity.h"
#include "stage_args.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t EXPECT_SHA[32] = {
    0xde, 0xad, 0xbe, 0xef, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
};
static const char *SHA_LOWER = "deadbeef000102030405060708090a0b0c0d0e0f101112131415161718191a1b";
static const char *SHA_UPPER = "DEADBEEF000102030405060708090A0B0C0D0E0F101112131415161718191A1B";

static void test_valid(void)
{
    char *argv[] = { (char *)"589812", (char *)SHA_LOWER };
    uint32_t size = 0;
    uint8_t sha[32];
    memset(sha, 0, sizeof sha);
    TEST_ASSERT_EQUAL_INT(0, stage_args_parse(2, argv, &size, sha));
    TEST_ASSERT_EQUAL_UINT32(589812u, size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(EXPECT_SHA, sha, 32);
}

static void test_valid_uppercase_hex(void)
{
    char *argv[] = { (char *)"1", (char *)SHA_UPPER };
    uint32_t size = 0;
    uint8_t sha[32];
    memset(sha, 0, sizeof sha);
    TEST_ASSERT_EQUAL_INT(0, stage_args_parse(2, argv, &size, sha));
    TEST_ASSERT_EQUAL_UINT32(1u, size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(EXPECT_SHA, sha, 32);
}

static void test_zero_size_rejected(void)
{
    char *argv[] = { (char *)"0", (char *)SHA_LOWER };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-2, stage_args_parse(2, argv, &size, sha));
}

static void test_short_sha_rejected(void)
{
    /* 63 hex chars -- one short */
    char *argv[] = { (char *)"100", (char *)"deadbeef000102030405060708090a0b0c0d0e0f101112131415161718191a" };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-3, stage_args_parse(2, argv, &size, sha));
}

static void test_long_sha_rejected(void)
{
    /* 65 hex chars -- one too many */
    char *argv[] = { (char *)"100", (char *)"deadbeef000102030405060708090a0b0c0d0e0f101112131415161718191a1bcd" };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-3, stage_args_parse(2, argv, &size, sha));
}

static void test_non_hex_sha_rejected(void)
{
    char *argv[] = { (char *)"100", (char *)"zzadbeef000102030405060708090a0b0c0d0e0f101112131415161718191a1b" };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-3, stage_args_parse(2, argv, &size, sha));
}

static void test_trailing_junk_size_rejected(void)
{
    char *argv[] = { (char *)"12x", (char *)SHA_LOWER };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-2, stage_args_parse(2, argv, &size, sha));
}

static void test_size_overflow_rejected(void)
{
    /* 0x100000000 == 0xFFFFFFFF + 1: must not silently wrap/truncate */
    char *argv[] = { (char *)"4294967296", (char *)SHA_LOWER };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(-2, stage_args_parse(2, argv, &size, sha));
}

static void test_size_max_ok(void)
{
    char *argv[] = { (char *)"4294967295", (char *)SHA_LOWER };
    uint32_t size = 0;
    uint8_t sha[32];
    TEST_ASSERT_EQUAL_INT(0, stage_args_parse(2, argv, &size, sha));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, size);
}

static void test_wrong_argc_rejected(void)
{
    uint32_t size = 0;
    uint8_t sha[32];
    char *argv1[] = { (char *)"589812" };
    TEST_ASSERT_EQUAL_INT(-1, stage_args_parse(1, argv1, &size, sha));

    char *argv3[] = { (char *)"589812", (char *)SHA_LOWER, (char *)"extra" };
    TEST_ASSERT_EQUAL_INT(-1, stage_args_parse(3, argv3, &size, sha));

    TEST_ASSERT_EQUAL_INT(-1, stage_args_parse(0, argv1, &size, sha));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid);
    RUN_TEST(test_valid_uppercase_hex);
    RUN_TEST(test_zero_size_rejected);
    RUN_TEST(test_short_sha_rejected);
    RUN_TEST(test_long_sha_rejected);
    RUN_TEST(test_non_hex_sha_rejected);
    RUN_TEST(test_trailing_junk_size_rejected);
    RUN_TEST(test_size_overflow_rejected);
    RUN_TEST(test_size_max_ok);
    RUN_TEST(test_wrong_argc_rejected);
    return UNITY_END();
}
