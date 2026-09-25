/* test_idcheck.c -- lt_id_valid (M6, final review): the pure session-id validator `lt delete`/
 * `lt open` run before putting an operator-typed id on the wire. Mirrors the lap-timer's own id
 * shape -- see idcheck.h's header comment for the components/app/logger.c and cmd.c references
 * this mirrors.
 */
#include "unity.h"
#include "idcheck.h"

void setUp(void) {}
void tearDown(void) {}

/* A real generated session id ("S%05u_%03u") and a few other legitimately-shaped ids pass. */
void test_valid_ids(void)
{
    TEST_ASSERT_TRUE(lt_id_valid("S00001_002"));    /* the lap-timer's own generated shape, 10 chars */
    TEST_ASSERT_TRUE(lt_id_valid("a"));              /* shortest possible: 1 char */
    TEST_ASSERT_TRUE(lt_id_valid("abcdefghij"));     /* exactly 10 chars: the bound, not past it */
    TEST_ASSERT_TRUE(lt_id_valid("ABC_123_9"));      /* mixed case + digits + underscore */
    TEST_ASSERT_TRUE(lt_id_valid("_____"));          /* underscores alone are fine */
}

/* NULL and the empty string are both rejected -- an id must be present. */
void test_null_and_empty_rejected(void)
{
    TEST_ASSERT_FALSE(lt_id_valid(NULL));
    TEST_ASSERT_FALSE(lt_id_valid(""));
}

/* Longer than the 10-character bound (mirrors the lap-timer's own s_id[11]/op_delete id[11]) is
 * rejected outright -- NOT silently truncated the way the lap-timer's own op_delete/op_open would
 * truncate an oversize payload. */
void test_too_long_rejected(void)
{
    TEST_ASSERT_FALSE(lt_id_valid("abcdefghijk"));          /* 11 chars: one past the bound */
    TEST_ASSERT_FALSE(lt_id_valid("S00001_0022"));           /* a real shape, one char too many */
}

/* Any byte outside [A-Za-z0-9_] rejects the whole id -- path separators, spaces, and other
 * punctuation have no business reaching the lap-timer's `delete`/`open` console commands as part
 * of an id. */
void test_bad_char_rejected(void)
{
    TEST_ASSERT_FALSE(lt_id_valid("has space"));
    TEST_ASSERT_FALSE(lt_id_valid("a/b"));
    TEST_ASSERT_FALSE(lt_id_valid("../etc"));
    TEST_ASSERT_FALSE(lt_id_valid("id.log"));
    TEST_ASSERT_FALSE(lt_id_valid("id;rm"));
    TEST_ASSERT_FALSE(lt_id_valid("id\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_ids);
    RUN_TEST(test_null_and_empty_rejected);
    RUN_TEST(test_too_long_rejected);
    RUN_TEST(test_bad_char_rejected);
    return UNITY_END();
}
