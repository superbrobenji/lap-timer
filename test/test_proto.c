/* test_proto.c -- host tests for the shared lap-timer <-> dev-controller protocol contract
 * (app/lt_proto.h, Plan 5.5 Task 1): the 0xFF stream frame header layout and the §18.2 STATUS
 * record offsets/length, plus the ---BEGIN/---END framing marker prefixes both sides parse on.
 */
#include "unity.h"
#include "app/lt_proto.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_stream_hdr_is_5_bytes(void) {
    TEST_ASSERT_EQUAL_UINT(5, sizeof(lt_stream_hdr_t));
}
void test_status_len_and_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(20, LT_STATUS_LEN);
    TEST_ASSERT_EQUAL_UINT(13, LT_ST_OFF_FW);   /* fw string starts at 13, 7 bytes -> 20 */
}
void test_markers_present(void) {
    TEST_ASSERT_EQUAL_STRING("---BEGIN ", LT_FRAME_BEGIN_PFX);
    TEST_ASSERT_EQUAL_STRING("---END ",   LT_FRAME_END_PFX);
}
void test_status_rec_type_is_outside_ses_space(void) {
    TEST_ASSERT_EQUAL_HEX8(0x40, LT_REC_STATUS);
    TEST_ASSERT_EQUAL_INT(1 + LT_STATUS_LEN, LT_STATUS_REC_LEN);
    /* Cast to int: LT_SES_T_* and LT_REC_STATUS are different anonymous enum types (separate
     * enum blocks in lt_proto.h), and the on-target build's -Werror=all trips -Wenum-compare on
     * a bare `!=` between them -- the same reason link.c's mirroring _Static_assert casts too. */
    TEST_ASSERT_NOT_EQUAL((int)LT_SES_T_FUSED, (int)LT_REC_STATUS);
    TEST_ASSERT_NOT_EQUAL((int)LT_SES_T_EVENT, (int)LT_REC_STATUS);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stream_hdr_is_5_bytes);
    RUN_TEST(test_status_len_and_offsets);
    RUN_TEST(test_markers_present);
    RUN_TEST(test_status_rec_type_is_outside_ses_space);
    return UNITY_END();
}
