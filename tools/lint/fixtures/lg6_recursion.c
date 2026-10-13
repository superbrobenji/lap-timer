/* fixture: direct recursion (LG-6 / rule 1). */
int walk(int d)
{
    if (d <= 0) { return 0; }
    return walk(d - 1) + 1;
}

int iterative(int d)
{
    int t = 0;
    for (int i = 0; i < d; i++) { t += i; }
    return t;
}
