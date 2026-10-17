#!/usr/bin/env python3
"""Tests for tools/gates/gate_claims.py through main() on throwaway git repos (the production
path: real git history, real staged files). Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import unittest
from unittest import mock

from tools.gates import gate_claims
from tools.gates.lib import gitinfo, registers
from tools.gates.tests import gitfixture as gf

CLAIMS = os.path.join(gf.STATE, "claims.yml")
ROW = """- id: C-demo-1
  doc: docs/spec.md
  anchor: "§1"
  text: "a.c returns 1"
  direct: "grep -n 'return 1' src/a.c"
  contra: "if a.c returned 0 the grep finds nothing"
  cites: [{cites}]
  verified_at: {verified_at}
  by: {by}
  state: {state}
  finding: {finding}
"""


def row(state="verified", verified_at="null", by="reviewer", finding="null", cites="src/a.c"):
    return ROW.format(state=state, verified_at=verified_at, by=by, finding=finding, cites=cites)


class ClaimsRepo(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        gf.write(self.root, "src/a.c", "int a(void) { return 1; }\n")
        gf.write(self.root, "src/b.c", "int b;\n")
        self.v = gf.commit_all(self.root, "sources")

    def tearDown(self):
        gf.remove(self.root)

    def claims(self, text):
        gf.write(self.root, CLAIMS, text)
        return gf.commit_all(self.root, "claims")

    def run_gate(self, *extra):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_claims.main(["--root", self.root, *extra])
        return rc, buf.getvalue()

    def test_empty_register_passes(self):
        rc, out = self.run_gate()
        self.assertEqual("PASS claims.schema\nPASS claims.duplicate\nPASS claims.false\nPASS claims.stale\n", out)
        self.assertEqual(0, rc)

    def test_verified_row_with_untouched_cites_passes(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/b.c", "int b2;\n")       # not cited
        gf.commit_all(self.root)
        rc, out = self.run_gate()
        self.assertEqual(0, rc, out)

    def test_touching_cited_file_marks_stale(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/a.c", "int a(void) { return 0; }\n")
        gf.commit_all(self.root, "touch the cited file")
        rc, out = self.run_gate()
        self.assertIn(f"FAIL claims.stale.C-demo-1: cited file(s) changed since verified_at {self.v}: src/a.c", out)
        self.assertEqual(1, rc)
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertIn("PASS claims.marked-stale.C-demo-1", out)
        self.assertEqual(0, rc, out)
        self.assertEqual("stale", registers.load_claims(self.root)[0]["state"])
        self.assertEqual(self.v, registers.load_claims(self.root)[0]["verified_at"])

    def test_staged_change_to_a_cited_file_fails_before_the_commit(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/a.c", "int a(void) { return 2; }\n")
        gf.git(self.root, "add", "src/a.c")
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.stale.C-demo-1", out)
        self.assertEqual(1, rc)
        rc, out = self.run_gate("--mark-stale", "--base", "HEAD")
        self.assertIn("PASS claims.marked-stale.C-demo-1", out)
        self.assertEqual(0, rc, out)

    def test_directory_cite_covers_its_files(self):
        self.claims(row(verified_at=f'"{self.v}"', cites="src/"))
        gf.write(self.root, "src/b.c", "int b3;\n")
        gf.commit_all(self.root)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.stale.C-demo-1: cited file(s) changed since verified_at", out)
        self.assertEqual(1, rc)

    def test_verified_at_unknown_commit_fails(self):
        for state in ("verified", "stale"):
            self.claims(row(state=state, verified_at='"1234abc"'))
            rc, out = self.run_gate()
            self.assertIn("FAIL claims.stale.C-demo-1: verified_at: sha 1234abc is not a commit in this repository", out)
            self.assertEqual(1, rc)

    def test_mark_stale_leaves_other_rows_alone(self):
        self.claims(row(verified_at=f'"{self.v}"', cites="src/b.c")
                    + row(state="unverified", verified_at="null", by="null").replace("C-demo-1", "C-demo-2"))
        gf.write(self.root, "src/a.c", "int a(void) { return 5; }\n")
        gf.commit_all(self.root)
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertIn("PASS claims.marked-stale.none", out)
        self.assertEqual(["verified", "unverified"], [c["state"] for c in registers.load_claims(self.root)])
        self.assertEqual(0, rc, out)

    def test_mark_stale_without_base_is_fail_input(self):
        rc, out = self.run_gate("--mark-stale")
        self.assertEqual("FAIL claims.input: --mark-stale needs --base <sha>\n", out)
        self.assertEqual(1, rc)

    def test_false_row_needs_a_registered_finding(self):
        self.claims(row(state='"false"', verified_at="null", by="null", finding="A2-IO-N4"))
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.false.C-demo-1: finding A2-IO-N4 is not in findings.yml", out)
        self.assertEqual(1, rc)

    def test_false_row_without_finding_is_schema_fail(self):
        self.claims(row(state='"false"', verified_at="null", by="null"))
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.C-demo-1: finding: a false claim must name the finding id", out)
        self.assertEqual(1, rc)

    def test_unquoted_false_state_is_named(self):
        self.claims(row(state="false", verified_at="null", by="null", finding="A2-IO-N4"))
        rc, out = self.run_gate()
        self.assertIn('FAIL claims.schema.C-demo-1: state: YAML read false as a boolean; quote it: state: "false"', out)
        self.assertEqual(1, rc)

    def test_duplicate_ids_fail(self):
        self.claims(row(state="unverified", verified_at="null", by="null") * 2)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.duplicate.C-demo-1", out)
        self.assertEqual(1, rc)

    def test_missing_register_is_fail_input(self):
        os.remove(os.path.join(self.root, CLAIMS))
        rc, out = self.run_gate()
        self.assertTrue(out.startswith("FAIL claims.input: "), out)
        self.assertEqual(1, rc)

    def test_a_malformed_id_prints_as_entry_index(self):
        forged = row(state="unverified", verified_at="null", by="null").replace(
            "id: C-demo-1", 'id: "C-x\\nFAIL claims.forged.C-x"')
        numeric = row(state="unverified", verified_at="null", by="null").replace("id: C-demo-1", "id: 5")
        self.claims(forged + numeric)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.entry-0: id: ", out)
        self.assertIn("FAIL claims.schema.entry-1: id: ", out)
        self.assertFalse([l for l in out.splitlines() if l.startswith("FAIL claims.forged")], out)
        self.assertEqual(1, rc)

    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_claims.main(["--root", root])
            self.assertEqual((1, "FAIL claims.input: --root is empty\n"), (rc, buf.getvalue()))

    def test_categories_whose_rows_were_skipped_never_pass(self):
        self.claims(row(state='"false"', verified_at="null", by="null"))     # no finding: schema error
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.C-demo-1", out)
        for cat in ("schema", "false", "stale"):
            self.assertNotIn(f"PASS claims.{cat}", out)
        self.assertIn("PASS claims.duplicate", out)
        self.assertEqual(1, rc)

    def test_one_commit_lookup_per_distinct_sha(self):
        self.claims(row(verified_at=f'"{self.v}"') + row(verified_at=f'"{self.v}"').replace("C-demo-1", "C-demo-2"))
        calls = []
        real = gitinfo.commit_exists

        def counting(root, sha):
            calls.append(sha)
            return real(root, sha)

        with mock.patch.object(gitinfo, "commit_exists", counting):
            rc, out = self.run_gate()
        self.assertEqual(0, rc, out)
        self.assertEqual([self.v], calls)

    def test_verified_at_head_for_dispatch(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        c = registers.load_claims(self.root)[0]
        head = gf.git(self.root, "rev-parse", "HEAD")
        self.assertTrue(gate_claims.verified_at_head(self.root, c, head))
        gf.write(self.root, "src/a.c", "int a(void) { return 9; }\n")
        head = gf.commit_all(self.root)
        self.assertFalse(gate_claims.verified_at_head(self.root, c, head))
        self.assertFalse(gate_claims.verified_at_head(self.root, dict(c, state="unverified"), head))


if __name__ == "__main__":
    unittest.main()
