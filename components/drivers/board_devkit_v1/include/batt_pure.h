/* batt_pure.h -- the pure, host-testable part of the board HAL's §16.4 battery reading: the
 * trimmed mean over BATT_SAMPLES ADC samples (audit fix wave F2a, drv:M12 / ruling R19).
 *
 * Extracted from board.c for two reasons. (1) board.c:234 called qsort(s, 64, sizeof(int),
 * cmp_int) -- an unregistered rule-9 function pointer the linter's FNPTR_RE structurally cannot
 * see, and newlib's qsort is itself a recursive implementation, which rule 1 forbids in shipped
 * code. A trimmed mean over 64 entries needs no general-purpose sort. (2) There is no way to
 * observe battery millivolts on the bench (the §18.2 STATUS record's batt_mv is hardcoded 0 until
 * Plan 6), so a host test is the only honest verification this arithmetic can have.
 *
 * IDF-free (stddef.h only) so it links into test/test_batt_trim.c as well as the firmware. */
#ifndef BATT_PURE_H
#define BATT_PURE_H
#include <stddef.h>

#define BATT_SAMPLES 64   /* §16.4: 64 samples ... */
#define BATT_TRIM    8    /* ... discard the 8 highest and the 8 lowest, mean the middle 48 */

/* Sorts s[0..n-1] ascending IN PLACE with a bounded insertion sort (rule 1: no recursion; rule 2:
 * the two loops are bounded by n <= BATT_SAMPLES) and returns the integer mean of the middle
 * n - 2*trim entries. -1 on a bad argument (NULL, n outside 1..BATT_SAMPLES, or an empty/negative
 * middle window), which the caller reports as an ADC read failure. */
int batt_trimmed_mean(int *s, int n, int trim);

#endif /* BATT_PURE_H */
