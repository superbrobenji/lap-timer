#include "unity.h"
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/logio.h"
#include "replay/replay.h"
#include "core/core.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_headers_agree_with_core_limits(void)
{
    TEST_ASSERT_EQUAL_INT(LAP_MAX_SECTORS + 1, SYNTH_MAX_GATES);
    TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VERTICES * 4, SYNTH_MAX_PIECES);
    TEST_ASSERT_TRUE(sizeof(((logr_t *)0)->n_by_type) / sizeof(uint32_t) > SES_T_END);
}

static void test_version_string_is_the_core_version(void)
{
    TEST_ASSERT_EQUAL_STRING(core_version(), replay_version());
    TEST_ASSERT_TRUE(strlen(replay_version()) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_headers_agree_with_core_limits);
    RUN_TEST(test_version_string_is_the_core_version);
    return UNITY_END();
}
