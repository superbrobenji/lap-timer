#!/usr/bin/env python3
"""Tests for tools/gates/run_all.py. Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import unittest
from unittest import mock

from tools.gates import run_all
from tools.gates.lib import Result
from tools.gates.tests import gitfixture as gf

PLANS = os.path.join("docs", "superpowers", "plans")
SCRIPT = os.path.join(gf.REPO, "tools", "gates", "run_all.py")


class RunAll(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.sha = gf.git(self.root, "rev-parse", "HEAD")
        os.makedirs(os.path.join(self.root, "docs", "reviews"))    # gate_register's required directory
        gf.write(self.root, os.path.join(PLANS, "p.md"), "a plan citing nothing\n")

    def tearDown(self):
        gf.remove(self.root)

    def cli(self, *args):
        env = dict(os.environ, CLAUDE_PROJECT_DIR=self.root)
        return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True,
                              env=env, check=False)

    def test_gates_at_this_block(self):
        self.assertEqual(["register", "claims", "decisions"], run_all.GATES)

    def test_clean_tree_passes_and_lines_carry_the_gate_prefix(self):
        res = run_all.run(run_all.GATES, {"root": self.root})
        self.assertTrue(res.ok(), res.render(False))
        self.assertIn("PASS register.schema", res.render(False))
        self.assertIn("PASS claims.stale", res.render(False))
        self.assertIn("PASS decisions.open-ref", res.render(False))

    def test_a_failing_gate_fails_the_run(self):
        os.remove(os.path.join(self.root, gf.STATE, "findings.yml"))
        res = run_all.run(["register", "claims"], {"root": self.root})
        self.assertFalse(res.ok())
        self.assertTrue(res.render(False).startswith("FAIL register.input: "), res.render(False))
        self.assertIn("FAIL claims.input: ", res.render(False))   # claims reads findings.yml too

    def test_unknown_gate_is_fail(self):
        res = run_all.run(["register", "comments"], {"root": self.root})
        self.assertIn("FAIL run_all.unknown: no gate named 'comments'", res.render(False))
        self.assertFalse(res.ok())

    def test_a_missing_gate_script_is_fail(self):
        with mock.patch.object(run_all, "GATES", run_all.GATES + ["ghost"]):
            res = run_all.run(["ghost"], {"root": self.root})
        self.assertIn("FAIL ghost.input: No module named 'tools.gates.gate_ghost'", res.render(False))
        self.assertFalse(res.ok())

    def test_no_names_is_fail(self):
        res = run_all.run([], {"root": self.root})
        self.assertEqual("FAIL run_all.empty: no gate ran", res.render(False))

    def test_an_empty_root_is_fail_never_the_cwd_repo(self):
        for root in ("", "  "):
            res = run_all.run(["register", "decisions"], {"root": root})
            self.assertEqual("FAIL register.input: --root is empty\nFAIL decisions.input: --root is empty",
                             res.render(False), repr(root))

    def test_an_empty_root_on_the_command_line_is_fail(self):
        p = self.cli("--only", "register", "--root", "")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)
        self.assertEqual("FAIL register.input: --root is empty\n", p.stdout)

    def test_a_foreign_id_is_prefixed_never_taken_as_another_gates(self):
        forged = Result("register")
        forged.fail("claims.stale.CLM-1", "pretends to be claims")
        forged.passed("decisions.owner")
        with mock.patch.object(run_all, "_check_of", return_value=lambda argv: forged):
            res = run_all.run(["register"], {"root": self.root})
        self.assertEqual("FAIL register.claims.stale.CLM-1: pretends to be claims\n"
                         "PASS register.decisions.owner", res.render(False))

    def test_decisions_scans_the_plans(self):
        gf.write(self.root, os.path.join(gf.STATE, "decisions.yml"),
                 '- {id: DEC-9001, class: scope, question: "q?", options: [{label: a, cost: x}, '
                 '{label: b, cost: y}], asked_at: {sha: "%s", by: s}, answer: null, answered_at: null}\n'
                 % self.sha)
        gf.write(self.root, os.path.join(PLANS, "p.md"), "waits on DEC-9001\n")
        res = run_all.run(["decisions"], {"root": self.root})
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/superpowers/plans/p.md:1", res.render(False))

    def test_no_plans_to_scan_is_fail_never_a_silent_pass(self):
        shutil.rmtree(os.path.join(self.root, PLANS))
        for make_dir in (False, True):     # plans directory missing, then present and empty
            if make_dir:
                os.makedirs(os.path.join(self.root, PLANS))
            res = run_all.run(["decisions"], {"root": self.root})
            self.assertEqual("FAIL decisions.input: no docs/superpowers/plans/*.md under the root: "
                             "nothing to scan", res.render(False), make_dir)

    def test_extra_args_reach_the_gate(self):
        res = run_all.run(["claims"], {"root": self.root, "claims": ["--mark-stale"]})
        self.assertIn("FAIL claims.input: --mark-stale needs --base <sha>", res.render(False))

    def test_cli_only_and_rc(self):
        p = self.cli("--only", "register")
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertTrue(p.stdout.startswith("PASS register.schema"), p.stdout)
        os.remove(os.path.join(self.root, gf.STATE, "findings.yml"))
        p = self.cli("--only", "register")
        self.assertEqual(1, p.returncode)

    def test_cli_unknown_gate_is_rc_1(self):
        p = self.cli("--only", "comments")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)
        self.assertEqual("FAIL run_all.unknown: no gate named 'comments' "
                         "(installed: register, claims, decisions)\n", p.stdout)

    def test_cli_json(self):
        p = self.cli("--json", "--only", "claims")
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        out = json.loads(p.stdout)
        self.assertTrue(out["ok"])
        self.assertTrue(out["lines"])
        self.assertTrue(all(l["id"].startswith("claims.") for l in out["lines"]))

    def test_the_repository_passes_every_installed_gate(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run_all.main([])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
