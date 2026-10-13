/* fixture: rule 3 must see every heap API this codebase could reach (LG-2). */
#include <stdlib.h>

void f(void)
{
    void *a = heap_caps_malloc(16, 0);
    heap_caps_free(a);
    void *b = pvPortMalloc(16);
    vPortFree(b);
    char *c = strndup("x", 1);
    char *d = NULL;
    (void)asprintf(&d, "%d", 1);
    size_t keep = heap_caps_get_minimum_free_size(0) + heap_caps_get_free_size(0);
    (void)keep;
}
