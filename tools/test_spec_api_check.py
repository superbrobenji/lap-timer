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


class Extraction(unittest.TestCase):
    def test_prototypes_typedefs_and_macros(self):
        protos, typedefs, macros = sac.api_of(HDR)
        self.assertEqual({"thing_do", "thing_reset"}, set(protos))
        self.assertEqual("int thing_do(thing_t *t, int n);", protos["thing_do"])
        self.assertEqual({"cb_t"}, typedefs)
        self.assertEqual({"X_H"}, macros)


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


if __name__ == "__main__":
    unittest.main()
