/* fixture: an `#elif 0` after a LIVE `#if` is a dead branch too (final review, deferred T1). */
int elif_dead(int n)
{
#if defined(FOO)
    int live = 1;
#elif 0
    CORE_ASSERT_RET(n > 0, 1, -1);
    CORE_ASSERT_RET(n < 99, 1, -1);
#endif
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}
