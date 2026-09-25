/* devcontroller/components/devconsole/host/idcheck.c -- see include/idcheck.h. Pure, IDF-free: no
 * heap, no shared state.
 */
#include "idcheck.h"

#include <string.h>

#define LT_ID_MAX_LEN 10u   /* mirrors the lap-timer's own session id bound -- see idcheck.h */

bool lt_id_valid(const char *s)
{
    if (s == NULL) return false;

    size_t len = strlen(s);
    if (len == 0 || len > LT_ID_MAX_LEN) return false;

    for (size_t i = 0; i < len; i++) {   /* bounded: len <= LT_ID_MAX_LEN */
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}
