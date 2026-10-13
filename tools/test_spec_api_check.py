#!/usr/bin/env python3
"""Unit tests for tools/spec_api_check.py (audit §3.3 item 8: spec §5.2 drifted silently)."""
import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spec_api_check as sac

HDR = """
#ifndef X_H
#define X_H
#include <stdint.h>
typedef void (*cb_t)(int x);
typedef struct { int a; int b; } thing_t;
int  thing_do(thing_t *t, int n);
void thing_reset(thing_t *t);
#endif
"""

# A backslash-continued multi-line macro (the shape of core/trk.h's TRK_USER_REC_MAX) directly
# followed by a prototype -- the continuation lines must not bleed into that prototype.
HDR_BACKSLASH_DEFINE = """
#ifndef Y_H
#define Y_H
#define Y_MAX \\
    (1 + \\
     2)
int y_do(int n);
#endif
"""

# A #define whose own trailing comment spans multiple physical lines (the shape of the spec's
# §5.2 TRK_USER_REC_MAX placeholder) directly followed by a declaration -- the orphaned `*/` the
# naive "blank the whole #define line" approach leaves behind must not bleed into that
# declaration either.
HDR_COMMENT_DEFINE = """
#ifndef Z_H
#define Z_H
#define Z_MAX /* worst-case value; see also
              * the second line of this comment */
int z_do(int n);
#endif
"""


class Extraction(unittest.TestCase):
    def test_prototypes_typedefs_and_macros(self):
        protos, typedefs, macros = sac.api_of(HDR)
        self.assertEqual({"thing_do", "thing_reset"}, set(protos))
        self.assertEqual("int thing_do(thing_t *t, int n);", protos["thing_do"])
        self.assertEqual({"cb_t"}, typedefs)
        self.assertEqual({"X_H"}, macros)

    def test_backslash_continued_define_does_not_corrupt_the_next_prototype(self):
        protos, _typedefs, macros = sac.api_of(HDR_BACKSLASH_DEFINE)
        self.assertEqual("int y_do(int n);", protos["y_do"])
        self.assertIn("Y_MAX", macros)

    def test_comment_style_define_placeholder_does_not_corrupt_the_next_declaration(self):
        protos, _typedefs, macros = sac.api_of(HDR_COMMENT_DEFINE)
        self.assertEqual("int z_do(int n);", protos["z_do"])
        self.assertIn("Z_MAX", macros)


class Drift(unittest.TestCase):
    # repaired by Task 9 of docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md; the decorator is deleted in that task
    @unittest.expectedFailure
    def test_the_committed_spec_and_headers_agree(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sac.main([])
        self.assertEqual(0, rc, buf.getvalue())

    def test_emit_lists_every_prototype(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            sac.main(["--emit", "core/cfg.h"])
        self.assertIn("int cfg_validate(cfg_t *c);", buf.getvalue())
        self.assertIn("bool cfg_migrate_supported(uint8_t from_version);", buf.getvalue())

    def test_emit_trk_h_declares_a_clean_trk_from_json(self):
        # core/trk.h carries a real backslash-continued macro (TRK_USER_REC_MAX) immediately
        # before trk_from_json's prototype -- this is the real-content regression case, not a
        # synthetic one: --emit must hand Task 9 the clean prototype, not the macro's value
        # glued onto its front.
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            sac.main(["--emit", "core/trk.h"])
        out = buf.getvalue()
        self.assertIn(
            "int trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap);",
            out,
        )
        self.assertNotIn("TRK_USER_VENUE_HDR_LEN", out)


if __name__ == "__main__":
    unittest.main()
