/* fixture: rule 9 part 2 -- a function passed by value or stored (LG-3). */
static int cmpi(const void *a, const void *b) { (void)a; (void)b; return 0; }
static void pre_cb(void *t) { (void)t; }

struct cfgs { void (*pre_cb)(void *); };

void sort_it(int *v, unsigned n)
{
    qsort(v, n, sizeof v[0], cmpi);
    struct cfgs c = { .pre_cb = pre_cb };
    (void)c;
}

void forwarder(void (*cb)(void *))
{
    struct cfgs c = { .pre_cb = cb };
    (void)c;
}
