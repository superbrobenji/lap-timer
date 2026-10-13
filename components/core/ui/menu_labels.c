#include "core/ui/model.h"
#include "core/core.h"
#include <stdio.h>

/* Power of 10 rule 5 (core/core.h): this module's own assertion code (#98). */
#define MENU_LABELS_ASSERT_CODE 0x0AF3

/* snprintf's return -> this module's truncation contract (format length, or -1 if it did not fit
 * in `cap`; buf is still left NUL-terminated either way, snprintf's own guarantee). Shared by the
 * normal "Layout: Auto" formatting below and the out-of-bounds venue fallback, so both compute the
 * same return value from the same literal. */
static int label_rc(size_t cap, int n) { return (n < 0 || (size_t)n >= cap) ? -1 : n; }

int ui_layout_label(const trk_venue_t *venue, uint8_t choice, char *buf, size_t cap)
{
    CORE_ASSERT_RET(buf != NULL, MENU_LABELS_ASSERT_CODE, -1);
    CORE_ASSERT_RET(cap > 0u, MENU_LABELS_ASSERT_CODE, -1);
    /* Review finding M1 (#98 fix round 1): bound the index locally rather than trusting the
     * caller -- every live call site (menu_do_layout, handle_layout_locked) already asserts
     * n_layouts <= TRK_MAX_LAYOUTS before calling this, but this is a standalone pure helper and
     * nothing stops a future caller from skipping that. Falls back to "Layout: Auto", same as a
     * NULL venue. */
    CORE_ASSERT_RET(venue == NULL || venue->n_layouts <= TRK_MAX_LAYOUTS, MENU_LABELS_ASSERT_CODE,
                     label_rc(cap, snprintf(buf, cap, "Layout: Auto")));
    const char *name = (venue != NULL && choice >= 1u && choice <= venue->n_layouts)
                            ? venue->layouts[choice - 1u].name : NULL;
    int n = (name != NULL) ? snprintf(buf, cap, "Layout: %s", name)
                           : snprintf(buf, cap, "Layout: Auto");
    return label_rc(cap, n);
}
