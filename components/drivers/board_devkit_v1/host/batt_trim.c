#include "batt_pure.h"

int batt_trimmed_mean(int *s, int n, int trim)
{
    if (s == NULL || n <= 0 || n > BATT_SAMPLES) return -1;
    if (trim < 0 || n - 2 * trim <= 0) return -1;
    /* insertion sort, ascending. Outer loop <= BATT_SAMPLES - 1 iterations, inner loop <= i, so
     * the whole sort is bounded by BATT_SAMPLES^2 / 2 == 2048 comparisons -- about 20 us on the
     * ESP32 and run at most once per battery read. */
    for (int i = 1; i < n; i++) {
        int v = s[i];
        int j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    long long sum = 0;
    for (int i = trim; i < n - trim; i++) sum += s[i];
    return (int)(sum / (n - 2 * trim));
}
