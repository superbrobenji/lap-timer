#include "core/btn_parse.h"
#include "core/core.h"

#include <stdlib.h>
#include <string.h>

/* Power of 10 rule 5 (spec §17.9); btn_parse.c's own code (grepped 0x0A* first: the next free
 * slot after refresh_policy.c's 0x0AC0). */
#define BTN_PARSE_ASSERT_CODE 0x0AE0

int btn_parse(const char *name, const char *ms, uint8_t *mask, uint32_t *hold_ms)
{
    CORE_ASSERT_RET(name != NULL, BTN_PARSE_ASSERT_CODE, -1);
    CORE_ASSERT_RET(mask != NULL && hold_ms != NULL, BTN_PARSE_ASSERT_CODE, -1);

    uint8_t m;
    if (strcmp(name, "mode") == 0)         m = 0x1;
    else if (strcmp(name, "up") == 0)      m = 0x2;
    else if (strcmp(name, "down") == 0)    m = 0x4;
    else if (strcmp(name, "up+down") == 0) m = 0x6;
    else return -1;

    uint32_t h = 100;
    if (ms != NULL) {
        if (ms[0] == '\0' || ms[0] == '-') return -2;   /* empty, or a sign strtoul would wrap around */
        char *end = NULL;
        unsigned long v = strtoul(ms, &end, 10);
        if (end == NULL || *end != '\0') return -2;      /* full-consumption check: trailing garbage rejected */
        if (v < 20UL)   v = 20UL;
        if (v > 5000UL) v = 5000UL;
        h = (uint32_t)v;
    }

    *mask = m;
    *hold_ms = h;
    return 0;
}
