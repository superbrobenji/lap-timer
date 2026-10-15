/* fixture: statements, not physical lines (LG-5). 22 statements on 8 physical lines. */
int packed(int n)
{
    int a = n; int b = n; int c = n;
    int d = n; int e = n; int f = n;
    a += b; b += c; c += d; d += e; e += f;
    a += b; b += c; c += d; d += e; e += f;
    a += b; b += c; c += d; d += e;
    for (int i = 0; i < 3; i++) { a += i; }
    return a + b + c + d + e + f;
}
