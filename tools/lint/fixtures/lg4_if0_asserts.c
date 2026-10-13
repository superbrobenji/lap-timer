/* fixture: assertions inside #if 0 do not count (LG-4); the #else branch does. */
int dead_asserts(int n)
{
#if 0
    CORE_ASSERT_RET(n > 0, 1, -1);
    CORE_ASSERT_RET(n < 99, 1, -1);
#endif
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}

int live_asserts(int n)
{
#if 0
    int unused = 0;
#else
    CORE_ASSERT_RET(n > 0, 1, -1);
    CORE_ASSERT_RET(n < 99, 1, -1);
#endif
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}
