#include "unity.h"
#include "batt_pure.h"
#include <stdlib.h>
#include <string.h>
void setUp(void) {} void tearDown(void) {}

static void test_sorted_input(void)
{
    int s[8]; for (int i = 0; i < 8; i++) s[i] = i * 10;       /* 0..70 */
    TEST_ASSERT_EQUAL_INT(35, batt_trimmed_mean(s, 8, 2));     /* mean of 20,30,40,50 */
}
static void test_reverse_input_is_sorted_in_place(void)
{
    int s[8]; for (int i = 0; i < 8; i++) s[i] = 70 - i * 10;
    TEST_ASSERT_EQUAL_INT(35, batt_trimmed_mean(s, 8, 2));
    for (int i = 1; i < 8; i++) TEST_ASSERT_TRUE(s[i - 1] <= s[i]);
}
static void test_outliers_are_discarded(void)
{
    /* the §16.4 case the trim exists for: ADC spikes at both ends must not move the mean */
    int s[BATT_SAMPLES];
    for (int i = 0; i < BATT_SAMPLES; i++) s[i] = 2000;
    for (int i = 0; i < BATT_TRIM; i++) { s[i] = 0; s[BATT_SAMPLES - 1 - i] = 4095; }
    TEST_ASSERT_EQUAL_INT(2000, batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM));
}
static void test_matches_a_reference_sort_on_random_input(void)
{
    /* R19 replaces qsort(cmp_int) with a bounded insertion sort; prove the two agree. */
    int s[BATT_SAMPLES], ref[BATT_SAMPLES];
    unsigned seed = 12345u;
    for (int trial = 0; trial < 200; trial++) {
        for (int i = 0; i < BATT_SAMPLES; i++) {
            seed = seed * 1103515245u + 12345u;
            s[i] = (int)((seed >> 16) % 4096u);
        }
        memcpy(ref, s, sizeof ref);
        int got = batt_trimmed_mean(s, BATT_SAMPLES, BATT_TRIM);
        /* reference: selection sort, independent implementation */
        for (int i = 0; i < BATT_SAMPLES - 1; i++) {
            int m = i;
            for (int j = i + 1; j < BATT_SAMPLES; j++) if (ref[j] < ref[m]) m = j;
            int t = ref[i]; ref[i] = ref[m]; ref[m] = t;
        }
        long long sum = 0;
        for (int i = BATT_TRIM; i < BATT_SAMPLES - BATT_TRIM; i++) sum += ref[i];
        TEST_ASSERT_EQUAL_INT((int)(sum / (BATT_SAMPLES - 2 * BATT_TRIM)), got);
    }
}
static void test_bad_args(void)
{
    int s[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(NULL, 4, 1));
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 4, 2));     /* n - 2*trim == 0 */
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 4, 3));     /* negative window */
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, 0, 0));
    TEST_ASSERT_EQUAL_INT(-1, batt_trimmed_mean(s, BATT_SAMPLES + 1, 0));   /* above the rule-2 cap */
    TEST_ASSERT_EQUAL_INT(2, batt_trimmed_mean(s, 4, 0));      /* no trim: mean(1,2,3,4) == 2 */
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sorted_input);
    RUN_TEST(test_reverse_input_is_sorted_in_place);
    RUN_TEST(test_outliers_are_discarded);
    RUN_TEST(test_matches_a_reference_sort_on_random_input);
    RUN_TEST(test_bad_args);
    return UNITY_END();
}
