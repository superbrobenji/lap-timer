/* fixture: a file whose ONLY finding is rule 3 must still fail --fail-on-violation (LG-1). */
#include <stdlib.h>

void g(void)
{
    void *p = malloc(8);
    free(p);
}
