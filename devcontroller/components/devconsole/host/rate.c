/* devcontroller/components/devconsole/host/rate.c -- see include/rate.h. Pure, IDF-free. */
#include "rate.h"

long long rate_x10(uint32_t n_now, uint32_t n_prev, int64_t dt_us)
{
    if (dt_us <= 0) return -1;
    if (n_now < n_prev) return -1;
    return ((long long)(n_now - n_prev) * 10LL * 1000000LL) / dt_us;
}
