#!/usr/bin/env python3
"""Tests for tools/gates/gate_register.py through main() on fixture trees (the production path).
Run from the repo root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import unittest

from tools.gates import gate_register
from tools.gates.tests import gitfixture as gf

FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "register")


def run(case, *lead):
    """main() on a fixture tree; `lead` goes before --root (--json is recognised only as argv[0])."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = gate_register.main([*lead, "--root", os.path.join(FIX, case)])
    return rc, buf.getvalue()


class GateRegister(unittest.TestCase):
    def test_valid_register_passes_every_category(self):
        rc, out = run("valid")
        self.assertEqual("PASS register.schema\nPASS register.owner\nPASS register.duplicate\n"
                         "PASS register.transition\nPASS register.evidence\nPASS register.report\n", out)
        self.assertEqual(0, rc)

    def test_illegal_transition_fails(self):
        rc, out = run("illegal_transition")
        self.assertIn("FAIL register.transition.T1-JUMP-1: transition: open -> pinned is illegal", out)
        self.assertEqual(1, rc)

    def test_pinned_without_transcript_fails(self):
        rc, out = run("pinned_no_transcript")
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pin transcript "
                      "docs/superpowers/state/demo/mutations/T1-NOPIN-1.txt does not exist", out)
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pinned needs the reviewer's "
                      "confirming report path in evidence", out)
        self.assertEqual(1, rc)

    def test_missing_owner_fails(self):
        rc, out = run("missing_owner")
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: owner: missing", out)
        self.assertNotIn("PASS register.owner", out)
        self.assertEqual(1, rc)

    def test_missing_file_is_a_fail_input(self):
        rc, out = run("missing_file")
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL register.input: "), out)
        self.assertIn("findings.yml: No such file or directory", out)

    def test_registered_report_with_an_unknown_id_fails(self):
        rc, out = run("report_unknown")
        self.assertEqual("FAIL register.report.docs/superpowers/state/briefs/b1-brief.md.T9-NOPE-1: "
                         "not in register\n", out.split("PASS")[0])
        self.assertEqual(1, rc)

    def test_unmarked_report_is_ignored_and_marked_report_with_known_ids_passes(self):
        rc, out = run("valid")      # valid/docs/reviews: one marked (all ids known), one unmarked
        self.assertNotIn("T9-NOPE", out)
        self.assertNotIn("T9-PROSE", out)
        self.assertIn("PASS register.report", out)
        self.assertEqual(0, rc)

    def test_report_ids_reads_table_cells_and_list_items(self):
        path = os.path.join(FIX, "valid", "docs", "reviews", "2026-01-01-registered.md")
        self.assertEqual(["T1-OPEN-1", "T1-DEF-1", "T1-PIN-1"], gate_register.report_ids(path))

    def test_report_ids_ignores_what_is_not_a_finding_id(self):
        root = gf.new_repo()
        try:
            path = gf.write(root, "r.md", "register: true\n| t1-lower-1 | x |\n| T1-OPEN-1x y | x |\n"
                            "- t1-lower-1: no\n- Note: prose\n- T1-OPEN-1, comma\n1. T1-DEF-1 numbered\n")
            self.assertEqual(["T1-DEF-1"], gate_register.report_ids(path))
        finally:
            gf.remove(root)

    def test_duplicate_ids_fail(self):
        root = gf.new_repo()
        try:
            with open(os.path.join(FIX, "missing_owner", gf.STATE, "findings.yml")) as fh:
                row = fh.read().replace("owner: null", "owner: {kind: issue, number: 2}")
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), row + row)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.duplicate.T1-ORPHAN-1: id appears more than once", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_json_output(self):
        rc, out = run("missing_owner", "--json")
        self.assertIn('"ok": false', out)
        self.assertEqual(1, rc)

    def test_the_repository_register_passes(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_register.main(["--root", gf.REPO])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
