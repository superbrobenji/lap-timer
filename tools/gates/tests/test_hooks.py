#!/usr/bin/env python3
"""Tests for tools/gates/hooks/*, install-hooks.sh, .claude/settings.json and CLAUDE.md. Each hook
runs as a subprocess with a JSON event on stdin -- the way Claude Code runs it. Run from the repo
root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.gates.tests import gitfixture as gf

HOOKS = os.path.join(gf.REPO, "tools", "gates", "hooks")
ORPHAN = """- id: T1-ORPHAN-1
  source: audit
  severity: MEDIUM
  class: >-
    A finding nobody owns.
  siblings: []
  cites: []
  repro: null
  owner: null
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: audit}]
"""


def hook(script, event, hooks_dir=HOOKS):
    env = {k: v for k, v in os.environ.items() if k != "CLAUDE_PROJECT_DIR"}
    raw = event if isinstance(event, str) else json.dumps(event)
    return subprocess.run([sys.executable, os.path.join(hooks_dir, script)], input=raw,
                          capture_output=True, text=True, env=env, check=False)


def bash_event(root, command):
    return {"session_id": "s", "transcript_path": "/dev/null", "cwd": root, "permission_mode": "default",
            "hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "t",
            "tool_input": {"command": command, "description": "d"}}


def new_repo():
    """gitfixture.new_repo plus the empty docs/reviews directory gate_register requires (a missing
    one is FAIL register.input); gitfixture.new_repo stays bare because test_gate_register pins that."""
    root = gf.new_repo()
    os.makedirs(os.path.join(root, "docs", "reviews"))
    return root


def isolated_env(**extra):
    """The environment for git and install calls on a fixture: no CLAUDE_PROJECT_DIR, and neither the
    developer's global nor system git config (core.hooksPath, gpgsign, templates) reaches the fixture."""
    env = {k: v for k, v in os.environ.items() if k != "CLAUDE_PROJECT_DIR"}
    env.update(GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_NOSYSTEM="1")
    env.update(extra)
    return env


def copy_gates(dst):
    """The real tools/gates (code + scripts) copied into a throwaway repo, without test fixtures."""
    shutil.copytree(os.path.join(gf.REPO, "tools", "gates"), os.path.join(dst, "tools", "gates"),
                    ignore=shutil.ignore_patterns("tests", "__pycache__"))


class PreToolUse(unittest.TestCase):
    def setUp(self):
        self.root = new_repo()
        self.spaced = os.path.join(self.root, "a b")    # a directory name with a space, inside the failing repo
        os.makedirs(self.spaced)

    def tearDown(self):
        gf.remove(self.root)

    def test_commit_on_a_failing_register_is_blocked(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m 'x'"))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: owner: missing", p.stderr)
        self.assertEqual("", p.stdout)

    def test_push_with_options_is_gated_too(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        p = hook("pre_tool_use.py", bash_event(self.root, "cd . && git -C . --no-pager push origin HEAD"))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr)
        for cmd in ('git -C "%s" commit -m x' % self.spaced, "git -c 'a=b c' push", "git --git-dir /x commit",
                    "git --work-tree=/x commit",                # quoted values and separate-word option values
                    "git -C . \\\n  commit -m x",               # backslash-newline continuation
                    "git -C %s commit -m x" % self.spaced.replace(" ", "\\ "),   # escaped space
                    "git --config-env a=B commit", '"git" commit -m x', "'git' push",
                    "git ${X} commit -m x", "git $X push"):     # unexpandable option word: gated conservatively
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr, cmd)

    def test_commit_on_a_clean_register_passes_silently(self):
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m 'x'"))
        self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr))

    def test_a_verb_that_only_shares_a_prefix_is_not_gated(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("git commit-graph write", "git push-to-checkout x"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual((0, ""), (p.returncode, p.stderr), cmd)

    def test_the_tree_named_by_dash_C_or_cd_is_the_one_gated(self):
        bad = new_repo()
        self.addCleanup(gf.remove, bad)
        gf.write(bad, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        # failing tree named from a clean cwd: blocked
        for cmd in ("git -C %s commit -m x" % bad, "cd %s && git push" % bad, "cd '%s'; git commit -m x" % bad,
                    "git -C .. -C %s/%s push" % (self.root, os.path.relpath(bad, self.root))):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr, cmd)
        # clean tree named from a failing cwd: passes
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        clean = new_repo()
        self.addCleanup(gf.remove, clean)
        for cmd in ("git -C %s commit -m x" % clean, "cd %s && git push" % clean):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual((0, ""), (p.returncode, p.stderr), cmd)
        # a relative -C resolves against the event cwd
        p = hook("pre_tool_use.py", bash_event(bad, "git -C %s commit" % os.path.relpath(clean, bad)))
        self.assertEqual(0, p.returncode, p.stderr)

    def test_a_named_directory_that_is_not_a_repo_fails_closed(self):
        outside = tempfile.mkdtemp(prefix="gate-test-outside-")
        self.addCleanup(gf.remove, outside)
        for cmd in ("git -C %s commit -m x" % outside, "cd %s && git push" % outside, "git -C /nonexistent-dir push"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL hook.internal: RuntimeError: git -C", p.stderr, cmd)

    def test_ungated_commands_and_tools_pass_even_on_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("git status", "git log --grep commit", "gh pr view", "ls"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(0, p.returncode, (cmd, p.stderr))
        agent = dict(bash_event(self.root, ""), tool_name="Agent",
                     tool_input={"prompt": "p", "description": "d", "subagent_type": "general-purpose"})
        self.assertEqual(0, hook("pre_tool_use.py", agent).returncode)

    def test_hook_fails_closed_when_script_errors(self):
        broken = os.path.join(self.root, "broken")
        os.makedirs(os.path.join(broken, "tools", "gates", "hooks"))
        shutil.copy(os.path.join(HOOKS, "pre_tool_use.py"), os.path.join(broken, "tools", "gates", "hooks"))
        gf.write(broken, "tools/gates/__init__.py", "")
        gf.write(broken, "tools/gates/run_all.py", "def run(names, args):\n    raise RuntimeError('gate exploded')\n")
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m x"),
                 hooks_dir=os.path.join(broken, "tools", "gates", "hooks"))
        self.assertEqual(2, p.returncode)
        self.assertEqual("FAIL hook.internal: RuntimeError: gate exploded\n", p.stderr)

    def test_malformed_stdin_fails_closed(self):
        for raw in ("", "not json", "[1, 2]"):
            p = hook("pre_tool_use.py", raw)
            self.assertEqual(2, p.returncode, raw)
            self.assertTrue(p.stderr.startswith("FAIL hook.internal: "), p.stderr)

    def test_cwd_outside_a_repo_fails_closed(self):
        outside = os.path.dirname(self.root)
        p = hook("pre_tool_use.py", bash_event(outside, "git commit -m x"))
        self.assertEqual(2, p.returncode)
        self.assertIn("FAIL hook.internal: RuntimeError: git -C", p.stderr)


class GitHooks(unittest.TestCase):
    def setUp(self):
        self.root = new_repo()
        copy_gates(self.root)
        gf.commit_all(self.root, "gates")
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.hook = os.path.join(self.root, ".git", "hooks", "pre-commit")

    def tearDown(self):
        gf.remove(self.root)

    def install(self):
        return subprocess.run(["bash", "tools/gates/install-hooks.sh"], cwd=self.root, capture_output=True,
                              text=True, env=isolated_env(), check=False)

    def commit(self):
        env = isolated_env(CLAUDE_PROJECT_DIR="/nonexistent-project")   # the hook must ignore it
        return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
                               "commit", "-q", "-m", "x"], cwd=self.root, capture_output=True, text=True,
                              env=env, check=False)

    def test_install_writes_the_marked_shim_and_is_idempotent(self):
        with open(self.hook) as fh:
            first = fh.read()
        self.assertIn("# strict-delivery-gates-hook", first)
        p = self.install()
        self.assertEqual(0, p.returncode)
        self.assertNotIn("moved the existing", p.stdout)
        with open(self.hook) as fh:
            self.assertEqual(first, fh.read())

    def test_a_foreign_hook_is_moved_aside(self):
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho mine\n")
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("moved the existing pre-commit hook", p.stdout)
        with open(self.hook + ".pre-strict-delivery") as fh:
            self.assertEqual("#!/bin/sh\necho mine\n", fh.read())
        with open(self.hook) as fh:
            self.assertIn("# strict-delivery-gates-hook", fh.read())

    def test_a_second_foreign_hook_does_not_overwrite_the_first_backup(self):
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho one\n")
        self.assertEqual(0, self.install().returncode)
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho two\n")
        p = self.install()
        self.assertEqual(1, p.returncode)
        self.assertIn("pre-strict-delivery already exists", p.stderr)
        with open(self.hook + ".pre-strict-delivery") as fh:
            self.assertEqual("#!/bin/sh\necho one\n", fh.read())
        with open(self.hook) as fh:
            self.assertEqual("#!/bin/sh\necho two\n", fh.read())

    def test_a_hooks_path_outside_the_repository_is_refused(self):
        shared = tempfile.mkdtemp(prefix="gate-test-shared-hooks-")
        self.addCleanup(gf.remove, shared)
        gf.git(self.root, "config", "core.hooksPath", shared)
        p = self.install()
        self.assertEqual(1, p.returncode)
        self.assertIn("install-hooks: refusing: the hooks directory", p.stderr)
        self.assertEqual([], os.listdir(shared))

    def test_pre_commit_blocks_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        gf.git(self.root, "add", "-A")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stdout + p.stderr)

    def test_pre_commit_blocks_unstaged_register_edits(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)   # not staged
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)

    def test_pre_commit_blocks_a_register_removed_from_the_index_but_left_on_disk(self):
        gf.git(self.root, "rm", "--cached", "-q", os.path.join(gf.STATE, "findings.yml"))
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)
        self.assertIn("findings.yml", p.stderr)

    def test_pre_commit_blocks_a_staged_report_with_an_unstaged_edit(self):
        report = os.path.join("docs", "reviews", "r.md")
        gf.write(self.root, report, "register: true\n- T1-GHOST-1: x\n")
        gf.git(self.root, "add", report)
        gf.write(self.root, report, "register: true\n")    # the unstaged edit drops the line the index cites
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)
        self.assertIn("docs/reviews/r.md", p.stderr)

    def test_pre_commit_blocks_an_untracked_file_the_gates_read(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        gf.write(self.root, os.path.join("docs", "reviews", "new.md"), "register: true\n- T1-GHOST-1: x\n")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)
        self.assertIn("docs/reviews/new.md", p.stderr)

    def test_a_staged_report_passes_when_it_is_committed_as_is(self):
        report = os.path.join("docs", "reviews", "r.md")
        gf.write(self.root, report, "register: true\n")
        gf.git(self.root, "add", report)
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)

    def test_a_tree_without_the_gates_cannot_commit(self):
        gf.git(self.root, "rm", "-r", "-q", "tools")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.missing", p.stderr)

    def test_pre_commit_passes_a_clean_tree(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)


if __name__ == "__main__":
    unittest.main()
