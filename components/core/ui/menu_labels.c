#include "core/ui/model.h"
#include "core/core.h"
#include <stdio.h>

/* Power of 10 rule 5 (core/core.h): this module's own assertion code (#98). */
#define MENU_LABELS_ASSERT_CODE 0x0AF3

int ui_layout_label(const trk_venue_t *venue, uint8_t choice, char *buf, size_t cap)
{
    CORE_ASSERT_RET(buf != NULL, MENU_LABELS_ASSERT_CODE, -1);
    CORE_ASSERT_RET(cap > 0u, MENU_LABELS_ASSERT_CODE, -1);
    const char *name = (venue != NULL && choice >= 1u && choice <= venue->n_layouts)
                            ? venue->layouts[choice - 1u].name : NULL;
    int n = (name != NULL) ? snprintf(buf, cap, "Layout: %s", name)
                           : snprintf(buf, cap, "Layout: Auto");
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
