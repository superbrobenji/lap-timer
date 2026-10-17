#!/usr/bin/env python3
"""Tests for tools/gates/gate_decisions.py through main() on throwaway trees.
Run from the repo root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import signal
import unittest
from unittest import mock

from tools.gates import gate_decisions
from tools.gates.lib import gitinfo, registers
from tools.gates.tests import gitfixture as gf

DECISIONS = os.path.join(gf.STATE, "decisions.yml")
OPEN = """- id: DEC-9001
  class: triage
  question: "accept bucket A or widen it?"
  options: [{label: accept, cost: "8 tasks"}, {label: widen, cost: "18 tasks"}]
  asked_at: {sha: "SHA", by: session}
  answer: null
  answered_at: null
"""
ANSWERED = """- id: DEC-9002
  class: merge-grant
  question: "grant:demo"
  options: []
  asked_at: {sha: "SHA", by: session}
  answer: "yes"
  answered_at: {sha: "SHA", by: BY}
"""
PASS_ALL = ("PASS decisions.schema\nPASS decisions.duplicate\nPASS decisions.owner\n"
            "PASS decisions.open-ref\nPASS decisions.scan\n")


class GateDecisions(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.sha = gf.git(self.root, "rev-parse", "HEAD")

    def tearDown(self):
        gf.remove(self.root)

    def run_gate(self, decisions, *extra):
        gf.write(self.root, DECISIONS, decisions.replace("SHA", self.sha))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_decisions.main(["--root", self.root, *extra])
        return rc, buf.getvalue()

    def test_open_and_owner_answered_decisions_pass(self):
        rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"))
        self.assertEqual(PASS_ALL, out)
        self.assertEqual(0, rc)

    def test_an_agent_answer_fails(self):
        rc, out = self.run_gate(ANSWERED.replace("BY", "session"))
        self.assertIn("FAIL decisions.owner.DEC-9002: answered_at.by: only the owner (superbrobenji) "
                      "answers a decision, got 'session'", out)
        self.assertNotIn("FAIL decisions.schema", out)
        self.assertNotIn("PASS decisions.owner", out)
        self.assertEqual(1, rc)

    def test_every_class_is_owner_only(self):
        self.assertEqual(registers.DECISION_CLASSES, gate_decisions.OWNER_ONLY_CLASSES)
        for cls in gate_decisions.OWNER_ONLY_CLASSES:
            text = ANSWERED.replace("BY", "controller").replace("merge-grant", cls)
            if cls != "merge-grant":
                text = text.replace("options: []", "options: [{label: a, cost: x}, {label: b, cost: y}]") \
                           .replace('"grant:demo"', '"q?"')
            rc, out = self.run_gate(text)
            self.assertEqual(1, rc, cls)
            self.assertIn("FAIL decisions.owner.DEC-9002: answered_at.by: only the owner", out, cls)
            self.assertNotIn("PASS decisions.owner", out, cls)
            self.assertNotIn("FAIL decisions.schema", out, cls)

    def test_reference_to_an_unanswered_decision_fails(self):
        gf.write(self.root, "docs/plan.md", "# plan\n\nBlocked on DEC-9001 (triage).\nDEC-9002 is fine.\n")
        rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"), "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/plan.md:3 references unanswered decision DEC-9001", out)
        self.assertNotIn("DEC-9002", out)
        self.assertNotIn("PASS decisions.open-ref", out)
        self.assertEqual(1, rc)

    def test_a_longer_id_is_not_a_reference_to_a_shorter_one(self):
        gf.write(self.root, "docs/plan.md", "See DEC-90012 for the long form.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertEqual(0, rc, out)

    def test_unknown_ids_are_not_open_references(self):
        gf.write(self.root, "docs/plan.md", "Example id DEC-9999 in prose.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertEqual(0, rc, out)

    def test_a_missing_scan_file_fails(self):
        rc, out = self.run_gate(OPEN, "--scan", "docs/absent.md")
        self.assertIn("FAIL decisions.scan.docs/absent.md: cannot read", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_a_directory_and_a_non_utf8_scan_file_fail(self):
        gf.write(self.root, "docs/dir/x.md", "x\n")
        with open(os.path.join(self.root, "docs", "bin.md"), "wb") as fh:
            fh.write(b"\xff\xfe DEC-9001\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/dir", "docs/bin.md")
        self.assertIn("FAIL decisions.scan.docs/dir: not a regular file", out)
        self.assertIn("FAIL decisions.scan.docs/bin.md: cannot read: not valid UTF-8", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_a_fifo_scan_path_fails_instead_of_hanging(self):
        if not hasattr(os, "mkfifo"):
            self.skipTest("os.mkfifo is not available")
        os.makedirs(os.path.join(self.root, "docs"), exist_ok=True)
        os.mkfifo(os.path.join(self.root, "docs", "pipe.md"))
        old = signal.signal(signal.SIGALRM, lambda *a: (_ for _ in ()).throw(AssertionError("gate hung on a FIFO")))
        signal.alarm(20)
        try:
            rc, out = self.run_gate(OPEN, "--scan", "docs/pipe.md")
        finally:
            signal.alarm(0)
            signal.signal(signal.SIGALRM, old)
        self.assertIn("FAIL decisions.scan.docs/pipe.md: not a regular file", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_an_agent_answered_decision_is_still_unanswered_for_open_refs(self):
        agent = ANSWERED.replace("BY", "session").replace("DEC-9002", "DEC-9003")
        gf.write(self.root, "docs/plan.md", "Proceeding per DEC-9003.\n")
        rc, out = self.run_gate(agent, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.owner.DEC-9003", out)
        self.assertIn("FAIL decisions.open-ref.DEC-9003: docs/plan.md:1 references unanswered decision DEC-9003", out)
        self.assertNotIn("PASS decisions.open-ref", out)
        self.assertEqual(1, rc)

    def test_an_agent_answer_with_an_unknown_commit_fails_owner_and_schema(self):
        text = ANSWERED.replace("BY", "session").replace('answered_at: {sha: "SHA"', 'answered_at: {sha: "1234abc"')
        rc, out = self.run_gate(text)
        self.assertIn("FAIL decisions.owner.DEC-9002", out)
        self.assertIn("FAIL decisions.schema.DEC-9002: answered_at.sha: sha 1234abc is not a commit in this "
                      "repository", out)
        self.assertEqual(1, rc)

    def test_line_numbers_count_newlines_only(self):
        gf.write(self.root, "docs/plan.md", "intro\n\x0c page break\u2028 DEC-9001 here\nlast\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/plan.md:2 references", out)
        self.assertEqual(1, rc)

    def test_a_scan_path_cannot_forge_a_result_line(self):
        rc, out = self.run_gate(OPEN, "--scan", "x\nPASS decisions.forged")
        self.assertFalse([l for l in out.splitlines() if l.startswith("PASS decisions.forged")], out)
        self.assertEqual(1, rc)

    def test_schema_errors_and_duplicates_fail(self):
        rc, out = self.run_gate(OPEN + OPEN.replace("answer: null", "answer: yes"))
        self.assertIn("FAIL decisions.duplicate.DEC-9001", out)
        self.assertIn("quote it", out)
        self.assertEqual(1, rc)

    def test_a_schema_error_skips_the_categories_it_never_reached(self):
        rc, out = self.run_gate(OPEN.replace("answer: null", "answer: yes"))
        self.assertIn("FAIL decisions.schema.DEC-9001", out)
        for cat in ("schema", "owner", "open-ref"):
            self.assertNotIn(f"PASS decisions.{cat}", out)
        self.assertIn("PASS decisions.duplicate", out)
        self.assertEqual(1, rc)

    def test_an_unknown_commit_is_a_schema_failure(self):
        for field in ("asked_at", "answered_at"):
            text = ANSWERED.replace("BY", "superbrobenji")
            text = text.replace(f"{field}: {{sha: \"SHA\"", f"{field}: {{sha: \"1234abc\"")
            rc, out = self.run_gate(text)
            self.assertIn(f"FAIL decisions.schema.DEC-9002: {field}.sha: sha 1234abc is not a commit "
                          "in this repository", out, field)
            self.assertEqual(1, rc, field)

    def test_a_malformed_id_prints_as_entry_index(self):
        forged = OPEN.replace("id: DEC-9001", 'id: "DEC-9\\nFAIL decisions.forged.x"')
        numeric = OPEN.replace("id: DEC-9001", "id: 5")
        rc, out = self.run_gate(forged + numeric)
        self.assertIn("FAIL decisions.schema.entry-0: id: ", out)
        self.assertIn("FAIL decisions.schema.entry-1: id: ", out)
        self.assertFalse([l for l in out.splitlines() if l.startswith("FAIL decisions.forged")], out)
        self.assertEqual(1, rc)

    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_decisions.main(["--root", root])
            self.assertEqual((1, "FAIL decisions.input: --root is empty\n"), (rc, buf.getvalue()))

    def test_one_commit_lookup_per_distinct_sha(self):
        calls = []
        real = gitinfo.commit_exists

        def counting(root, sha):
            calls.append(sha)
            return real(root, sha)

        with mock.patch.object(gitinfo, "commit_exists", counting):
            rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"))
        self.assertEqual(0, rc, out)
        self.assertEqual([self.sha], calls)

    def test_missing_register_is_fail_input(self):
        os.remove(os.path.join(self.root, DECISIONS))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_decisions.main(["--root", self.root])
        self.assertTrue(buf.getvalue().startswith("FAIL decisions.input: "), buf.getvalue())
        self.assertEqual(1, rc)


if __name__ == "__main__":
    unittest.main()
