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
        self.assertEqual(["register", "claims", "decisions", "dispatch"], run_all.GATES)

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

    def test_a_root_that_is_not_a_path_says_so(self):
        for root in (None, 5):
            res = run_all.run(["register", "decisions"], {"root": root})
            self.assertEqual("FAIL register.input: --root is not a path\n"
                             "FAIL decisions.input: --root is not a path", res.render(False), repr(root))

    def test_a_root_that_is_missing_or_a_file_is_not_a_directory(self):
        missing = os.path.join(self.root, "absent")
        a_file = os.path.join(self.root, "README.md")
        for root in (missing, a_file):
            res = run_all.run(["decisions"], {"root": root})
            self.assertEqual(f"FAIL decisions.input: --root is not a directory: {root}",
                             res.render(False), root)

    def test_a_gate_named_twice_runs_once_and_fails(self):
        res = run_all.run(["register", "register"], {"root": self.root})
        out = res.render(False)
        self.assertEqual(1, out.count("PASS register.schema"), out)
        self.assertIn("FAIL run_all.duplicate: gate 'register' is named more than once", out)
        self.assertFalse(res.ok())
        p = self.cli("--only", "register,register")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)

    def test_cli_bad_arguments_are_a_fail_line(self):
        for bad in (("--bogus",), ("--ro", "/x"), ("--only",), ("--only", "register", "--json")):
            p = self.cli(*bad)
            self.assertEqual(1, p.returncode, (bad, p.stdout, p.stderr))
            self.assertEqual("FAIL run_all.input: bad arguments (argparse exit 2)\n", p.stdout, bad)

    def test_cli_help_goes_to_stderr_and_json_stdout_stays_json(self):
        p = self.cli("--json", "-h")
        self.assertIn("usage: run_all", p.stderr)
        self.assertNotIn("usage", p.stdout)
        self.assertEqual("run_all.input", json.loads(p.stdout)["lines"][0]["id"])

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

    def test_an_extra_scan_adds_to_the_plans_never_replaces_them(self):
        gf.write(self.root, os.path.join(gf.STATE, "decisions.yml"),
                 '- {id: DEC-9001, class: scope, question: "q?", options: [{label: a, cost: x}, '
                 '{label: b, cost: y}], asked_at: {sha: "%s", by: s}, answer: null, answered_at: null}\n'
                 % self.sha)
        gf.write(self.root, os.path.join(PLANS, "p.md"), "waits on DEC-9001\n")
        gf.write(self.root, "prompt.md", "a clean prompt\n")
        res = run_all.run(["decisions"], {"root": self.root, "decisions": ["--scan", "prompt.md"]})
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/superpowers/plans/p.md:1", res.render(False))

    def test_a_relative_root_works_from_any_cwd(self):
        parent, name = os.path.split(self.root)
        old = os.getcwd()
        os.chdir(parent)
        try:
            for root in (name, os.path.join(".", name), os.path.join(name, "docs", "..")):
                res = run_all.run(run_all.GATES, {"root": root})
                self.assertTrue(res.ok(), (root, res.render(False)))
                self.assertIn("PASS decisions.open-ref", res.render(False))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                self.assertEqual(0, run_all.main(["--root", name]), buf.getvalue())
        finally:
            os.chdir(old)

    def test_cli_help_is_not_called_bad_arguments(self):
        p = self.cli("-h")
        self.assertEqual(1, p.returncode)
        self.assertIn("usage: run_all", p.stderr)
        self.assertEqual("FAIL run_all.input: help requested; no gate was run\n", p.stdout)

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
                         "(installed: register, claims, decisions, dispatch)\n", p.stdout)

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
