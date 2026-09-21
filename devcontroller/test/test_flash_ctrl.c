/* test_flash_ctrl.c -- Plan 5.5 Task 6 Steps 1-4: the cmd-OTA flash token parser + state machine.
 * Drives linkhost_flash_feed with injected bytes (no real UART): OTA-READY then OTA-END 0x0000 ->
 * success; OTA-ERR 0x0801 -> the precondition code surfaces; no bytes -> timeout. Also covers the
 * named OTA-ERR reasons, noise tolerance, and OTA-END with a nonzero code. */
#include "unity.h"
#include "linkhost_proto.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static lh_flash_state_t feed_str(lh_flash_ctx_t *c, const char *s)
{
    return linkhost_flash_feed(c, (const uint8_t *)s, strlen(s));
}

static void test_flash_success(void)
{
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    TEST_ASSERT_EQUAL_INT(LH_FLASH_WAIT_READY, c.state);

    TEST_ASSERT_EQUAL_INT(LH_FLASH_STREAMING, feed_str(&c, "OTA-READY\r\n"));
    TEST_ASSERT_EQUAL_INT(LH_FLASH_DONE_OK, feed_str(&c, "OTA-END 0x0000\r\n"));
    TEST_ASSERT_EQUAL_INT(0, linkhost_flash_result(&c));
}

static void test_flash_err_precond(void)
{
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    feed_str(&c, "OTA-READY\n");
    TEST_ASSERT_EQUAL_INT(LH_FLASH_DONE_ERR, feed_str(&c, "OTA-ERR 0x0801\n"));
    TEST_ASSERT_EQUAL_HEX16(0x0801, c.code);
    TEST_ASSERT_EQUAL_INT(0x0801, linkhost_flash_result(&c));
}

static void test_flash_timeout(void)
{
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    /* no bytes fed: still WAIT_READY -> the glue maps this to a timeout */
    TEST_ASSERT_EQUAL_INT(LH_FLASH_WAIT_READY, c.state);
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_TIMEOUT, linkhost_flash_result(&c));

    /* READY but never a terminal token -> STREAMING, still a timeout */
    feed_str(&c, "OTA-READY\n");
    TEST_ASSERT_EQUAL_INT(LH_FLASH_STREAMING, c.state);
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_TIMEOUT, linkhost_flash_result(&c));
}

static void test_flash_named_errors(void)
{
    uint16_t code = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(LT_OTA_READY, linkhost_flash_parse_token("OTA-READY", &code));
    TEST_ASSERT_EQUAL_INT(LT_OTA_END, linkhost_flash_parse_token("OTA-END 0x0000", &code));
    TEST_ASSERT_EQUAL_HEX16(0x0000, code);
    TEST_ASSERT_EQUAL_INT(LT_OTA_ERR, linkhost_flash_parse_token("OTA-ERR timeout", &code));
    TEST_ASSERT_EQUAL_HEX16(LT_OTA_ERR_TIMEOUT, code);
    TEST_ASSERT_EQUAL_INT(LT_OTA_ERR, linkhost_flash_parse_token("OTA-ERR badsha", &code));
    TEST_ASSERT_EQUAL_HEX16(LT_OTA_ERR_BADSHA, code);
    TEST_ASSERT_EQUAL_INT(LT_OTA_ERR, linkhost_flash_parse_token("OTA-ERR badsize", &code));
    TEST_ASSERT_EQUAL_HEX16(LT_OTA_ERR_BADSIZE, code);
    TEST_ASSERT_EQUAL_INT(LT_OTA_ERR, linkhost_flash_parse_token("OTA-ERR 0x1234", &code));
    TEST_ASSERT_EQUAL_HEX16(0x1234, code);
    TEST_ASSERT_EQUAL_INT(LT_OTA_NONE, linkhost_flash_parse_token("laptimer> ", &code));
    TEST_ASSERT_EQUAL_INT(LT_OTA_NONE, linkhost_flash_parse_token("I (12) app: streaming", &code));

    /* a named error via the state machine surfaces the mapped code */
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    feed_str(&c, "OTA-READY\nOTA-ERR read\n");
    TEST_ASSERT_EQUAL_INT(LH_FLASH_DONE_ERR, c.state);
    TEST_ASSERT_EQUAL_INT(LT_OTA_ERR_READ, linkhost_flash_result(&c));
}

static void test_flash_noise_then_success(void)
{
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    /* boot/log noise interleaved with the tokens must not derail the machine */
    lh_flash_state_t st = feed_str(&c,
        "I (10) ota: begin\nOTA-READY\nW (20) ota: slow\nsome junk line\nOTA-END 0x0000\n");
    TEST_ASSERT_EQUAL_INT(LH_FLASH_DONE_OK, st);
    TEST_ASSERT_EQUAL_INT(0, linkhost_flash_result(&c));
}

static void test_flash_end_nonzero_is_error(void)
{
    lh_flash_ctx_t c;
    linkhost_flash_ctx_init(&c);
    feed_str(&c, "OTA-READY\n");
    TEST_ASSERT_EQUAL_INT(LH_FLASH_DONE_ERR, feed_str(&c, "OTA-END 0x00ab\n"));
    TEST_ASSERT_EQUAL_INT(0x00ab, linkhost_flash_result(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flash_success);
    RUN_TEST(test_flash_err_precond);
    RUN_TEST(test_flash_timeout);
    RUN_TEST(test_flash_named_errors);
    RUN_TEST(test_flash_noise_then_success);
    RUN_TEST(test_flash_end_nonzero_is_error);
    return UNITY_END();
}
