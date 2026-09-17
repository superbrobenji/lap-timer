#ifndef TEST_PBM_H
#define TEST_PBM_H
#include <stdbool.h>

#include "core/ui/render.h"

/* Tiny PBM (portable bitmap, P4 binary variant) writer + byte-exact compare for framebuffer
 * snapshot tests (spec §20.2, test/test_ui.c). PBM's own convention is bit=1 -> black; the
 * framebuffer's convention (core/ui/render.h) is the opposite, `0 = black` — pbm_write inverts
 * every pixel byte on the way out so a PBM viewer shows the image the way the framebuffer
 * intends it, and pbm_eq_file applies the same inversion before comparing so a golden file
 * committed to test/snapshots/ can be opened directly in a viewer. */

/* Writes fb as a binary PBM (P4: "P4\n<w> <h>\n" header, then (w/8)*h inverted pixel bytes) to
 * `path`, truncating/creating the file. Returns true on success. */
bool pbm_write(const char *path, const fb_t *fb);

/* Compares the PBM that pbm_write(path, fb) would produce against the file already at `path`,
 * byte-exact (header included). Returns true on an exact match. On any mismatch — missing file,
 * different dimensions, different pixel bytes, extra trailing bytes — returns false and also
 * writes fb's current content to "<path>.actual.pbm" (via pbm_write) so the divergence can be
 * inspected or promoted to a new golden. */
bool pbm_eq_file(const char *path, const fb_t *fb);

#endif
