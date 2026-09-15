#include "unity.h"
#include "core/core.h"

void setUp(void) {}
void tearDown(void) {}

static void test_version_string(void)
{
    TEST_ASSERT_EQUAL_STRING("0.0.1", core_version());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string);
    return UNITY_END();
}
