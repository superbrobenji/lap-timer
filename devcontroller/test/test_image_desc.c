/* test_image_desc.c -- Plan 5.5 Task 6: the PURE app-image header parser that tells
 * POST /api/flash which ver/hwid to put on the lap-timer's `ota recv` line. Builds synthetic
 * images at the spec 19.3 offsets (esp_app_desc_t at 0x20, version at 0x30, hwid at 0x120) and
 * checks both the happy path and every reject the handler maps to 412.
 */
#include "unity.h"
#include "image_desc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define IMG_LEN       0x140
#define DESC_OFF      0x20
#define VER_OFF       0x30
#define HWID_OFF      0x120
#define DESC_MAGIC_LE "\x32\x54\xCD\xAB"

static const char VER[]  = "v0.1.0-5-gabc";
static const char HWID[] = "moto_sim_epaper";

/* A minimal image whose only meaningful content is the descriptor magic, the version and the
 * hwid; everything else stays zero (the parser must not care). */
static void make_img(uint8_t img[IMG_LEN])
{
    memset(img, 0, IMG_LEN);
    memcpy(img + DESC_OFF, DESC_MAGIC_LE, 4);
    memcpy(img + VER_OFF, VER, sizeof VER);
    memcpy(img + HWID_OFF, HWID, sizeof HWID);
}

static void test_parses_ver_and_hwid(void)
{
    uint8_t img[IMG_LEN];
    make_img(img);

    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    TEST_ASSERT_EQUAL_INT(0, img_desc_parse(img, IMG_LEN, ver, hwid));
    TEST_ASSERT_EQUAL_STRING(VER, ver);
    TEST_ASSERT_EQUAL_STRING(HWID, hwid);

    /* exactly IMG_DESC_MIN_LEN bytes is enough -- that is all the handler reads back */
    TEST_ASSERT_EQUAL_INT(0, img_desc_parse(img, IMG_DESC_MIN_LEN, ver, hwid));
    TEST_ASSERT_EQUAL_STRING(HWID, hwid);
}

static void test_bad_magic(void)
{
    uint8_t img[IMG_LEN];
    make_img(img);
    img[DESC_OFF] ^= 0xFFu;

    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    TEST_ASSERT_EQUAL_INT(-1, img_desc_parse(img, IMG_LEN, ver, hwid));
}

static void test_short_buffer(void)
{
    uint8_t img[IMG_LEN];
    make_img(img);

    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    TEST_ASSERT_EQUAL_INT(-1, img_desc_parse(img, IMG_DESC_MIN_LEN - 1, ver, hwid));
    TEST_ASSERT_EQUAL_INT(-1, img_desc_parse(img, 0, ver, hwid));
}

static void test_bad_version(void)
{
    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    uint8_t img[IMG_LEN];

    /* a space would split the `ota recv` line into an extra console argument */
    make_img(img);
    img[VER_OFF + 2] = ' ';
    TEST_ASSERT_EQUAL_INT(-2, img_desc_parse(img, IMG_LEN, ver, hwid));

    /* a quote would unbalance esp_console_split_argv's quoting */
    make_img(img);
    img[VER_OFF + 2] = '"';
    TEST_ASSERT_EQUAL_INT(-2, img_desc_parse(img, IMG_LEN, ver, hwid));

    /* empty version */
    make_img(img);
    img[VER_OFF] = 0;
    TEST_ASSERT_EQUAL_INT(-2, img_desc_parse(img, IMG_LEN, ver, hwid));
}

static void test_bad_hwid(void)
{
    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    uint8_t img[IMG_LEN];

    make_img(img);
    img[HWID_OFF] = 0;                                  /* empty */
    TEST_ASSERT_EQUAL_INT(-3, img_desc_parse(img, IMG_LEN, ver, hwid));

    make_img(img);
    img[HWID_OFF + 3] = 0x01;                           /* control byte */
    TEST_ASSERT_EQUAL_INT(-3, img_desc_parse(img, IMG_LEN, ver, hwid));
}

/* The hwid descriptor is a char[24] with no guaranteed terminator: a 24-character hwid fills it
 * completely and must still come back as a 24-character string. */
static void test_hwid_fills_the_field(void)
{
    uint8_t img[IMG_LEN];
    make_img(img);
    memset(img + HWID_OFF, 'h', IMG_HWID_LEN);

    char ver[IMG_VER_LEN];
    char hwid[IMG_HWID_LEN + 1];
    TEST_ASSERT_EQUAL_INT(0, img_desc_parse(img, IMG_LEN, ver, hwid));
    TEST_ASSERT_EQUAL_size_t((size_t)IMG_HWID_LEN, strlen(hwid));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_ver_and_hwid);
    RUN_TEST(test_bad_magic);
    RUN_TEST(test_short_buffer);
    RUN_TEST(test_bad_version);
    RUN_TEST(test_bad_hwid);
    RUN_TEST(test_hwid_fills_the_field);
    return UNITY_END();
}
