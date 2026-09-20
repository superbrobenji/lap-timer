/* devcontroller/test/test_logstore.c -- host tests for logstore's PURE rotation-decision logic
 * (components/logstore/host/logstore_rot.c, Plan 5.5 Task 5). Exercises logstore_should_rotate
 * and logstore_pick_drop directly -- no LittleFS/UART, no on-target logstore.c involved.
 */
#include "unity.h"
#include "logstore_rot.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- logstore_should_rotate: fires exactly when cur_file_bytes + incoming > max_file_bytes ---- */

void test_should_rotate_fires_at_max_file_bytes(void)
{
    /* well under the cap: no rotation */
    TEST_ASSERT_FALSE(logstore_should_rotate(0, 100, 50, 100000, 200));
    /* exactly at the cap: still fits, no rotation */
    TEST_ASSERT_FALSE(logstore_should_rotate(0, 150, 50, 100000, 200));
    /* one byte over: rotation fires */
    TEST_ASSERT_TRUE(logstore_should_rotate(0, 150, 51, 100000, 200));
    /* current file already at/over max, any further record rotates */
    TEST_ASSERT_TRUE(logstore_should_rotate(0, 200, 1, 100000, 200));
}

void test_should_rotate_ignores_total_cap_inputs(void)
{
    /* cur_total/cap_bytes are accepted for signature symmetry with the cap-accounting side but
     * must not affect this decision -- only cur_file_bytes vs max_file_bytes matters. */
    TEST_ASSERT_FALSE(logstore_should_rotate(999999, 10, 10, 5, 1000));
    TEST_ASSERT_TRUE(logstore_should_rotate(0, 10, 10, 1000000, 15));
}

/* ---- logstore_pick_drop: drop oldest-first to fit cap_bytes, never drop the newest ---- */

void test_pick_drop_no_drop_when_already_within_cap(void)
{
    logstore_file_info_t files[3] = {
        { "log_00000001", 100 },
        { "log_00000002", 100 },
        { "log_00000003", 100 },
    };
    int drop_idx[3];
    int n = logstore_pick_drop(files, 3, 50, 1000, drop_idx, 3);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_pick_drop_drops_oldest_until_it_fits(void)
{
    /* 4 files x 100B = 400, incoming 0, cap 250 -> must drop the 2 oldest (200) to reach 200<=250 */
    logstore_file_info_t files[4] = {
        { "log_00000001", 100 },   /* oldest */
        { "log_00000002", 100 },
        { "log_00000003", 100 },
        { "log_00000004", 100 },   /* newest / current -- must never be dropped */
    };
    int drop_idx[4] = { -1, -1, -1, -1 };
    int n = logstore_pick_drop(files, 4, 0, 250, drop_idx, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(0, drop_idx[0]);
    TEST_ASSERT_EQUAL_INT(1, drop_idx[1]);

    /* apply the decision and confirm the surviving total is <= cap and the newest is present */
    uint32_t remaining = 0;
    for (int i = 0; i < 4; i++) {
        bool dropped = (i == drop_idx[0] || i == drop_idx[1]);
        if (!dropped) remaining += files[i].bytes;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(250, remaining);
    /* the newest id (index 3, "log_00000004") must survive -- it's never in drop_idx */
    for (int i = 0; i < n; i++) TEST_ASSERT_NOT_EQUAL_INT(3, drop_idx[i]);
}

void test_pick_drop_accounts_for_incoming_bytes(void)
{
    /* 3 files x 50B = 150 total; incoming 80 -> would-be 230; cap 150. Dropping just the oldest
     * (50) leaves 180 > 150, so a second (also 50) must go too, leaving 130 <= 150. */
    logstore_file_info_t files[3] = {
        { "log_00000001", 50 },
        { "log_00000002", 50 },
        { "log_00000003", 50 },   /* newest / current */
    };
    int drop_idx[3];
    int n = logstore_pick_drop(files, 3, 80, 150, drop_idx, 3);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(0, drop_idx[0]);
    TEST_ASSERT_EQUAL_INT(1, drop_idx[1]);
}

void test_pick_drop_never_drops_the_newest_even_if_still_over_cap(void)
{
    /* the newest file alone already exceeds cap_bytes -- there is nothing more to drop without
     * losing the file currently being written, so pick_drop stops at count-1 regardless. */
    logstore_file_info_t files[2] = {
        { "log_00000001", 500 },
        { "log_00000002", 500 },   /* current: must survive */
    };
    int drop_idx[2];
    int n = logstore_pick_drop(files, 2, 0, 100, drop_idx, 2);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(0, drop_idx[0]);
}

void test_pick_drop_zero_or_one_file_drops_nothing(void)
{
    int drop_idx[1];
    TEST_ASSERT_EQUAL_INT(0, logstore_pick_drop(NULL, 0, 999999, 1, drop_idx, 1));

    logstore_file_info_t one[1] = { { "log_00000001", 999999 } };
    TEST_ASSERT_EQUAL_INT(0, logstore_pick_drop(one, 1, 0, 1, drop_idx, 1));
}

void test_pick_drop_respects_drop_idx_cap(void)
{
    /* 4 files needing 2 drops to fit, but the caller only gave room for 1 index -- pick_drop must
     * not overrun drop_idx[]; it stops early and reports only what it actually wrote. */
    logstore_file_info_t files[4] = {
        { "a", 100 }, { "b", 100 }, { "c", 100 }, { "d", 100 },
    };
    int drop_idx[1] = { -1 };
    int n = logstore_pick_drop(files, 4, 0, 250, drop_idx, 1);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(0, drop_idx[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_should_rotate_fires_at_max_file_bytes);
    RUN_TEST(test_should_rotate_ignores_total_cap_inputs);
    RUN_TEST(test_pick_drop_no_drop_when_already_within_cap);
    RUN_TEST(test_pick_drop_drops_oldest_until_it_fits);
    RUN_TEST(test_pick_drop_accounts_for_incoming_bytes);
    RUN_TEST(test_pick_drop_never_drops_the_newest_even_if_still_over_cap);
    RUN_TEST(test_pick_drop_zero_or_one_file_drops_nothing);
    RUN_TEST(test_pick_drop_respects_drop_idx_cap);
    return UNITY_END();
}
