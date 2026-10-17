#!/usr/bin/env python3
"""Tests for tools/gates/lib (Gate framework, config, gitinfo). Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
import contextlib
import io
import json
import os
import stat
import tempfile
import unittest

from tools.gates import lib
from tools.gates.lib import GateError, Result, config, gitinfo
from tools.gates.tests import gitfixture as gf


def run(check, args, name="demo"):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = lib.run_gate(name, check, args)
    return rc, buf.getvalue()


class GateFramework(unittest.TestCase):
    def test_missing_input_is_fail(self):
        def check(args):
            raise GateError("docs/superpowers/state/findings.yml: No such file or directory")
        rc, out = run(check, [])
        self.assertEqual(1, rc)
        self.assertEqual("FAIL demo.input: docs/superpowers/state/findings.yml: No such file or directory\n", out)

    def test_an_empty_result_is_fail_never_pass(self):
        rc, out = run(lambda args: Result("demo"), [])
        self.assertEqual(1, rc)
        self.assertEqual("FAIL demo.empty: gate produced no verdict\n", out)

    def test_an_unexpected_exception_is_fail(self):
        def check(args):
            raise KeyError("state")
        rc, out = run(check, [])
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL demo.input: "), out)

    def test_bad_arguments_are_fail_not_exit(self):
        def check(args):
            lib.gate_args("demo").parse_args(args)
            return Result("demo")
        with contextlib.redirect_stderr(io.StringIO()):
            rc, out = run(check, ["--no-such-flag"])
        self.assertEqual(1, rc)
        self.assertIn("FAIL demo.input: bad arguments", out)

    def test_pass_and_fail_lines_render_and_set_rc(self):
        def check(args):
            r = Result("demo")
            r.passed("demo.a")
            if args == ["bad"]:
                r.fail("demo.b", "broken")
            return r
        self.assertEqual((0, "PASS demo.a\n"), run(check, []))
        self.assertEqual((1, "PASS demo.a\nFAIL demo.b: broken\n"), run(check, ["bad"]))

    def test_json_flag_is_stripped_and_renders_json(self):
        seen = []
        def check(args):
            seen.append(args)
            r = Result("demo")
            r.fail("demo.x", "why")
            return r
        rc, out = run(check, ["--json", "--root", "r"])
        self.assertEqual([["--root", "r"]], seen)
        self.assertEqual({"gate": "demo", "ok": False,
                          "lines": [{"status": "FAIL", "id": "demo.x", "reason": "why"}]}, json.loads(out))
        self.assertEqual(1, rc)

    def test_repo_root_prefers_claude_project_dir(self):
        old = os.environ.get("CLAUDE_PROJECT_DIR")
        os.environ["CLAUDE_PROJECT_DIR"] = "/somewhere"
        try:
            self.assertEqual("/somewhere", lib.repo_root())
        finally:
            if old is None:
                del os.environ["CLAUDE_PROJECT_DIR"]
            else:
                os.environ["CLAUDE_PROJECT_DIR"] = old

    def test_code_root_is_the_checkout_holding_the_gates(self):
        self.assertTrue(os.path.isfile(os.path.join(lib.CODE_ROOT, "tools", "gates", "lib", "__init__.py")))


class StrictYaml(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="gate-yaml-")

    def tearDown(self):
        gf.remove(self.dir)

    def load(self, text):
        return lib.load_yaml(gf.write(self.dir, "x.yml", text))

    def test_duplicate_key_is_refused(self):
        with self.assertRaisesRegex(GateError, "duplicate key 'state'"):
            self.load("- {id: A-1, state: open, state: pinned}\n")

    def test_merge_key_is_refused(self):
        with self.assertRaisesRegex(GateError, "merge keys"):
            self.load("- {<<: {a: 1}, b: 2}\n")

    def test_invalid_and_missing_files_raise(self):
        with self.assertRaisesRegex(GateError, "invalid YAML"):
            self.load("x: [\n")
        with self.assertRaisesRegex(GateError, "No such file"):
            lib.load_yaml(os.path.join(self.dir, "absent.yml"))


class Config(unittest.TestCase):
    def test_repo_config_loads_with_the_owner_handle(self):
        cfg = config.load(lib.CODE_ROOT)
        self.assertEqual("superbrobenji", cfg["owner"]["handle"])
        self.assertEqual(["final_verifier", "exception_reviewer"], cfg["fable_roles"])
        self.assertEqual(12000, cfg["dispatch"]["max_prompt_chars"])

    def test_missing_config_and_missing_keys_raise(self):
        d = tempfile.mkdtemp(prefix="gate-cfg-")
        try:
            with self.assertRaisesRegex(GateError, "config.yml"):
                config.load(d)
            gf.write(d, "tools/gates/config.yml", "owner: {handle: x}\n")
            with self.assertRaisesRegex(GateError, "missing keys: triggers"):
                config.load(d)
        finally:
            gf.remove(d)


class GitInfo(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()

    def tearDown(self):
        gf.remove(self.root)

    def test_head_branch_status_worktrees(self):
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), gitinfo.head_sha(self.root))
        self.assertEqual("main", gitinfo.branch(self.root))
        self.assertTrue(gitinfo.status_clean(self.root))
        gf.write(self.root, "new.txt", "x\n")
        self.assertFalse(gitinfo.status_clean(self.root))
        self.assertEqual([os.path.realpath(self.root)],
                         [os.path.realpath(p) for p in gitinfo.worktrees(self.root)])

    def test_changed_and_staged_files(self):
        base = gitinfo.head_sha(self.root)
        gf.write(self.root, "a/b.c", "int x;\n")
        gf.git(self.root, "add", "a/b.c")
        self.assertEqual(["a/b.c"], gitinfo.staged_files(self.root))
        head = gf.commit_all(self.root)
        self.assertEqual(["a/b.c"], gitinfo.changed_files(self.root, base, head))
        self.assertEqual([], gitinfo.staged_files(self.root))

    def test_tags_at(self):
        gf.git(self.root, "tag", "demo-b1")
        self.assertEqual(["demo-b1"], gitinfo.tags_at(self.root, "HEAD"))

    def test_bad_revision_raises_gate_error(self):
        with self.assertRaisesRegex(GateError, "git -C .* diff --name-only"):
            gitinfo.changed_files(self.root, "0000000", "HEAD")

    def test_check_runs_reads_every_check_kind(self):
        pr = {"statusCheckRollup": [
            {"__typename": "CheckRun", "name": "gates", "conclusion": "SUCCESS"},
            {"__typename": "CheckRun", "name": "coverage", "conclusion": "FAILURE"},
            {"__typename": "StatusContext", "context": "ext/ci", "state": "PENDING"}]}
        self.assertEqual([{"name": "gates", "conclusion": "SUCCESS"},
                          {"name": "coverage", "conclusion": "FAILURE"},
                          {"name": "ext/ci", "conclusion": "PENDING"}], gitinfo.check_runs(pr))
        with self.assertRaises(GateError):
            gitinfo.check_runs({})


class PrForBranch(unittest.TestCase):
    """pr_for_branch() through a fake `gh` first on PATH (the real call path, no network)."""

    def setUp(self):
        self.bin = tempfile.mkdtemp(prefix="gate-gh-")
        self.old_path = os.environ["PATH"]
        os.environ["PATH"] = self.bin + os.pathsep + self.old_path

    def tearDown(self):
        os.environ["PATH"] = self.old_path
        gf.remove(self.bin)

    def fake_gh(self, script):
        path = gf.write(self.bin, "gh", "#!/bin/sh\n" + script)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

    def test_no_pr_is_none(self):
        self.fake_gh('echo \'no pull requests found for branch "x"\' >&2; exit 1\n')
        self.assertIsNone(gitinfo.pr_for_branch(self.bin))

    def test_pr_json_is_returned(self):
        self.fake_gh("echo '{\"number\": 7, \"statusCheckRollup\": []}'\n")
        self.assertEqual({"number": 7, "statusCheckRollup": []}, gitinfo.pr_for_branch(self.bin))

    def test_any_other_gh_failure_raises(self):
        self.fake_gh("echo 'HTTP 401: Bad credentials' >&2; exit 1\n")
        with self.assertRaisesRegex(GateError, "Bad credentials"):
            gitinfo.pr_for_branch(self.bin)


if __name__ == "__main__":
    unittest.main()
