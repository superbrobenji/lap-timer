/* dk:H1 / R16 -- the AP passphrase must be random, not a public function of the beacon BSSID.
 * Pure half: the alphabet mapping and the stored-value validator. */
#include "unity.h"
#include "dc_psk.h"
#include <string.h>
void setUp(void) {}
void tearDown(void) {}

static void test_format_length_charset_and_nul(void)
{
    uint8_t rnd[DC_PSK_LEN];
    for (size_t i = 0; i < sizeof rnd; i++) rnd[i] = (uint8_t)(i * 17u + 3u);
    char out[DC_PSK_LEN + 1];
    dc_psk_format(rnd, sizeof rnd, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(DC_PSK_LEN, strlen(out));
    for (size_t i = 0; i < DC_PSK_LEN; i++)
        TEST_ASSERT_NOT_NULL(strchr(DC_PSK_ALPHABET, out[i]));
    TEST_ASSERT_TRUE(dc_psk_valid(out));
}

static void test_format_uses_only_the_low_five_bits_so_every_byte_maps(void)
{
    uint8_t zeros[DC_PSK_LEN] = { 0 };
    char out[DC_PSK_LEN + 1];
    dc_psk_format(zeros, sizeof zeros, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("222222222222", out);
    uint8_t ones[DC_PSK_LEN];
    memset(ones, 0xFF, sizeof ones);
    dc_psk_format(ones, sizeof ones, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ZZZZZZZZZZZZ", out);
}

static void test_distinct_input_gives_distinct_output(void)
{
    uint8_t a[DC_PSK_LEN] = { 1 }, b[DC_PSK_LEN] = { 2 };
    char oa[DC_PSK_LEN + 1], ob[DC_PSK_LEN + 1];
    dc_psk_format(a, sizeof a, oa, sizeof oa);
    dc_psk_format(b, sizeof b, ob, sizeof ob);
    TEST_ASSERT_TRUE(strcmp(oa, ob) != 0);
}

static void test_valid_rejects_anything_not_from_this_generator(void)
{
    TEST_ASSERT_FALSE(dc_psk_valid(NULL));
    TEST_ASSERT_FALSE(dc_psk_valid(""));
    TEST_ASSERT_FALSE(dc_psk_valid("22222222222"));          /* 11 chars */
    TEST_ASSERT_FALSE(dc_psk_valid("2222222222222"));        /* 13 chars */
    TEST_ASSERT_FALSE(dc_psk_valid("ltdev-aabbcc"));         /* the old MAC-derived form */
    TEST_ASSERT_FALSE(dc_psk_valid("22222222222O"));         /* excluded character */
}

static void test_format_never_overflows_a_short_buffer(void)
{
    uint8_t rnd[DC_PSK_LEN] = { 5 };
    char small[4] = { 'x', 'x', 'x', 'x' };
    dc_psk_format(rnd, sizeof rnd, small, sizeof small);
    TEST_ASSERT_EQUAL_UINT(3, strlen(small));                /* 3 chars + NUL, no overrun */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_format_length_charset_and_nul);
    RUN_TEST(test_format_uses_only_the_low_five_bits_so_every_byte_maps);
    RUN_TEST(test_distinct_input_gives_distinct_output);
    RUN_TEST(test_valid_rejects_anything_not_from_this_generator);
    RUN_TEST(test_format_never_overflows_a_short_buffer);
    return UNITY_END();
}
