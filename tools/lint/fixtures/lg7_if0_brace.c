/* fixture: an unbalanced brace inside #if 0 must not hide later functions (LG-7). */
#if 0
void broken(void) {
#endif

int over_twenty(int n)
{
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}
