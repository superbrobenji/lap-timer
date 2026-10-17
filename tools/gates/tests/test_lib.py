#!/usr/bin/env python3
"""Tests for tools/gates/lib (Gate framework, config, gitinfo). Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

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
        with self.assertRaises(GateError) as ctx:
            gitinfo.changed_files(self.root, "0000000", "HEAD")
        self.assertTrue(str(ctx.exception).startswith("git diff --name-only -z --no-renames 0000000 HEAD: "),
                        str(ctx.exception))
        self.assertNotIn(self.root, str(ctx.exception))

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


class ReviewRound1(unittest.TestCase):
    def test_json_only_recognised_as_first_argument(self):
        seen = []
        def check(args):
            seen.append(args)
            r = Result("demo")
            r.passed("demo.a")
            return r
        rc, out = run(check, ["--root", "--json"])
        self.assertEqual([["--root", "--json"]], seen)
        self.assertEqual("PASS demo.a\n", out)
        rc, out = run(check, ["--json", "--root", "x"])
        self.assertEqual([["--root", "--json"], ["--root", "x"]], seen)
        self.assertTrue(json.loads(out)["ok"])

    def test_repo_root_git_fallback_and_error(self):
        old = os.environ.pop("CLAUDE_PROJECT_DIR", None)
        cwd = os.getcwd()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            os.chdir(lib.CODE_ROOT)
            self.assertEqual(os.path.realpath(gf.git(lib.CODE_ROOT, "rev-parse", "--show-toplevel")),
                             os.path.realpath(lib.repo_root()))
            os.chdir(outside)
            with self.assertRaisesRegex(GateError, "git rev-parse --show-toplevel"):
                lib.repo_root()
            old_path = os.environ["PATH"]
            os.environ["PATH"] = "/nonexistent"
            try:
                with self.assertRaisesRegex(GateError, "git rev-parse --show-toplevel"):
                    lib.repo_root()
            finally:
                os.environ["PATH"] = old_path
        finally:
            os.chdir(cwd)
            gf.remove(outside)
            if old is not None:
                os.environ["CLAUDE_PROJECT_DIR"] = old

    def test_unhashable_key_and_non_utf8_are_gate_errors(self):
        d = tempfile.mkdtemp(prefix="gate-yaml-")
        try:
            with self.assertRaisesRegex(GateError, "unhashable mapping key"):
                lib.load_yaml(gf.write(d, "k.yml", "? [a, b]\n: 1\n"))
            path = os.path.join(d, "b.yml")
            with open(path, "wb") as fh:
                fh.write(b"x: \xff\xfe\n")
            with self.assertRaisesRegex(GateError, "not valid UTF-8"):
                lib.load_yaml(path)
        finally:
            gf.remove(d)

    def test_missing_git_and_gh_binaries_are_gate_errors(self):
        root = gf.new_repo()
        old = os.environ["PATH"]
        os.environ["PATH"] = "/nonexistent"
        try:
            with self.assertRaisesRegex(GateError, "git"):
                gitinfo.head_sha(root)
            with self.assertRaisesRegex(GateError, "gh"):
                gitinfo.pr_for_branch(root)
        finally:
            os.environ["PATH"] = old
            gf.remove(root)

    def test_check_runs_rejects_non_dict_items(self):
        with self.assertRaises(GateError):
            gitinfo.check_runs({"statusCheckRollup": ["oops"]})

    def test_non_ascii_and_renamed_paths_are_verbatim(self):
        root = gf.new_repo()
        try:
            gf.write(root, "old.txt", "some content\nmore\nlines\n")
            gf.commit_all(root)
            base = gitinfo.head_sha(root)
            gf.git(root, "mv", "old.txt", "new.txt")
            gf.write(root, "\u00e9.txt", "x\n")
            gf.git(root, "add", "-A")
            self.assertEqual(["new.txt", "old.txt", "\u00e9.txt"], sorted(gitinfo.staged_files(root)))
            head = gf.commit_all(root)
            self.assertEqual(["new.txt", "old.txt", "\u00e9.txt"], sorted(gitinfo.changed_files(root, base, head)))
        finally:
            gf.remove(root)

    def test_detached_head_is_a_gate_error(self):
        root = gf.new_repo()
        try:
            gf.git(root, "checkout", "-q", "--detach")
            with self.assertRaises(GateError) as ctx:
                gitinfo.branch(root)
            self.assertEqual("git rev-parse --abbrev-ref HEAD: detached HEAD", str(ctx.exception))
        finally:
            gf.remove(root)


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave (items A6-A8, B4, B9, B10)."""

    def test_result_with_no_lines_is_not_ok(self):                                   # B4
        self.assertFalse(Result("g").ok())
        self.assertEqual(False, json.loads(Result("g").render(True))["ok"])
        r = Result("g")
        r.passed("g.a")
        self.assertTrue(r.ok())

    def test_text_render_is_one_physical_line_per_result_line(self):                 # B9
        r = Result("g")
        r.fail("g.a", "while parsing\n  a block\t\tsequence\n\nin x.yml")
        self.assertEqual("FAIL g.a: while parsing a block sequence in x.yml", r.render(False))
        self.assertEqual("while parsing\n  a block\t\tsequence\n\nin x.yml",
                         json.loads(r.render(True))["lines"][0]["reason"])           # JSON keeps the raw text

    def test_help_goes_to_stderr_and_abbreviations_are_refused(self):                # B10
        def check(args):
            lib.gate_args("demo").parse_args(args)
            return Result("demo")
        for flag in ("--help", "-h"):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc, out = run(check, ["--json", flag])
            self.assertEqual(1, rc)
            self.assertFalse(out.startswith("usage"), out)
            self.assertEqual({"status": "FAIL", "id": "demo.input", "reason": "bad arguments (argparse exit 0)"},
                             json.loads(out)["lines"][0])
            self.assertIn("usage: demo", err.getvalue())
            self.assertIn("--root", err.getvalue())
        with contextlib.redirect_stderr(io.StringIO()):
            rc, out = run(check, ["--roo", "x"])
        self.assertEqual(1, rc)
        self.assertIn("FAIL demo.input: bad arguments", out)

    def test_load_yaml_messages_are_relative_to_the_root(self):                      # A7
        d = tempfile.mkdtemp(prefix="gate-yaml-")
        try:
            gf.write(d, "sub/x.yml", "- id: [\n")
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "x.yml"), d)
            msg = str(ctx.exception)
            self.assertTrue(msg.startswith("sub/x.yml: invalid YAML: "), msg)
            self.assertIn('in "sub/x.yml", line', msg)
            self.assertNotIn(d, msg)
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "absent.yml"), d)
            self.assertEqual("sub/absent.yml: No such file or directory", str(ctx.exception))
            with open(os.path.join(d, "sub", "b.yml"), "wb") as fh:
                fh.write(b"x: \xff\xfe\n")
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "b.yml"), d)
            self.assertEqual("sub/b.yml: not valid UTF-8", str(ctx.exception))
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(gf.write(d, "sub/m.yml", "- {<<: {a: 1}}\n"), d)
            self.assertTrue(str(ctx.exception).startswith("sub/m.yml: invalid YAML: "), str(ctx.exception))
            self.assertNotIn(d, str(ctx.exception))
        finally:
            gf.remove(d)

    def test_config_messages_are_relative_to_the_root(self):                         # A7
        d = tempfile.mkdtemp(prefix="gate-cfg-")
        try:
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: No such file or directory", str(ctx.exception))
            gf.write(d, "tools/gates/config.yml", "- a\n")
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: top level must be a mapping", str(ctx.exception))
            gf.write(d, "tools/gates/config.yml", "owner: {handle: x}\n")
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertTrue(str(ctx.exception).startswith("tools/gates/config.yml: missing keys: triggers"))
            with open(os.path.join(lib.CODE_ROOT, "tools", "gates", "config.yml"), encoding="utf-8") as fh:
                good = fh.read()
            gf.write(d, "tools/gates/config.yml", good.replace("owner: {handle: superbrobenji}", "owner: {handle: ' '}"))
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: owner.handle must be a non-empty string", str(ctx.exception))
        finally:
            gf.remove(d)

    def test_git_messages_do_not_carry_the_root(self):                               # A7
        root = gf.new_repo()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            with self.assertRaises(GateError) as ctx:
                gitinfo.head_sha(outside)
            self.assertTrue(str(ctx.exception).startswith("git rev-parse HEAD: "), str(ctx.exception))
            self.assertNotIn(outside, str(ctx.exception))
            with self.assertRaises(GateError) as ctx:
                gitinfo.tags_at(root, "no-such-rev")
            self.assertTrue(str(ctx.exception).startswith("git tag --points-at no-such-rev: "))
            self.assertNotIn(root, str(ctx.exception))
        finally:
            gf.remove(root)
            gf.remove(outside)

    def test_status_clean_ignores_the_users_untracked_files_setting(self):          # A6
        root = gf.new_repo()
        try:
            gf.git(root, "config", "status.showUntrackedFiles", "no")
            gf.write(root, "untracked.txt", "x\n")
            self.assertEqual("", gf.git(root, "status", "--porcelain"))      # the config hides it ...
            self.assertFalse(gitinfo.status_clean(root))                      # ... the helper must not
        finally:
            gf.remove(root)

    def test_commit_exists(self):                                                    # A8
        root = gf.new_repo()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            head = gitinfo.head_sha(root)
            self.assertTrue(gitinfo.commit_exists(root, head))
            self.assertTrue(gitinfo.commit_exists(root, head[:7]))
            self.assertFalse(gitinfo.commit_exists(root, "0000000"))
            self.assertFalse(gitinfo.commit_exists(root, gf.git(root, "rev-parse", "HEAD^{tree}")[:7]))  # a tree
            self.assertFalse(gitinfo.commit_exists(root, "--all"))
            with self.assertRaises(GateError) as ctx:
                gitinfo.commit_exists(outside, head)
            self.assertTrue(str(ctx.exception).startswith("git cat-file -e "), str(ctx.exception))
            self.assertNotIn(outside, str(ctx.exception))
        finally:
            gf.remove(root)
            gf.remove(outside)


if __name__ == "__main__":
    unittest.main()
