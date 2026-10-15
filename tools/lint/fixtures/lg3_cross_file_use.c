/* fixture: uses cmp_elsewhere (defined in lg3_cross_file_def.c) as a sink argument. */
void sort_other(int *v, unsigned n)
{
    qsort(v, n, sizeof v[0], cmp_elsewhere);
}
