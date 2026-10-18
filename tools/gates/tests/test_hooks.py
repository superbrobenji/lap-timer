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
import time
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
    env = isolated_env()    # defined below: a hook that runs git or install-hooks.sh on a fixture must not read the developer's git config
    raw = event if isinstance(event, str) else json.dumps(event)
    return subprocess.run([sys.executable, os.path.join(hooks_dir, script)], input=raw,
                          capture_output=True, text=True, env=env, check=False, timeout=20)   # a stalled hook is an error


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

    def assert_gated(self, cmd, root=None):
        p = hook("pre_tool_use.py", bash_event(root or self.root, cmd))
        self.assertEqual(2, p.returncode, (cmd[:80], p.stderr))
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr, cmd[:80])

    def assert_silent(self, cmd, root=None):
        p = hook("pre_tool_use.py", bash_event(root or self.root, cmd))
        self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr), cmd[:80])

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
        for cmd in ('git -C "%s" commit -m x' % self.spaced, "git -c 'a=b c' push",       # quoted values
                    "git -C . \\\n  commit -m x",               # backslash-newline continuation
                    "git -C %s commit -m x" % self.spaced.replace(" ", "\\ "),   # escaped space
                    "git --config-env a=B commit", '"git" commit -m x', "'git' push",
                    "git ${X} commit -m x", "git $X push",      # unexpandable option word: gated conservatively
                    "git p\\\nush", "git --git-dir=.git --work-tree . -p --bare --exec-path --literal-pathspecs push",
                    "git --namespace n --no-replace-objects --no-optional-locks --html-path -c x.y=\"a b\" commit",
                    "git --shallow-file x --attr-source=HEAD --no-literal-pathspecs --glob-pathspecs --no-advice push"):
            self.assert_gated(cmd)

    def test_commit_on_a_clean_register_passes_silently(self):
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m 'x'"))
        self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr))

    def test_a_verb_that_only_shares_a_prefix_is_not_gated(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("git commit-graph write", "git push-to-checkout x", "git commits", "git pushd",
                    "git --push-option x", "git stash push -m x", "git -C . log --grep commit"):
            self.assert_silent(cmd)

    def test_the_tree_named_by_dash_C_or_cd_is_the_one_gated(self):
        bad = new_repo()
        self.addCleanup(gf.remove, bad)
        gf.write(bad, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        # failing tree named from a clean cwd: blocked
        for cmd in ("git -C %s commit -m x" % bad, "cd %s && git push" % bad, "cd '%s'; git commit -m x" % bad,
                    "git -C .. -C %s/%s push" % (self.root, os.path.relpath(bad, self.root)),
                    "cd %s\ngit push" % bad, "cd %s && git -C %s push" % (os.path.dirname(bad), os.path.basename(bad))):
            self.assert_gated(cmd)
        # clean tree named from a failing cwd: passes
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        clean = new_repo()
        self.addCleanup(gf.remove, clean)
        for cmd in ("git -C %s commit -m x" % clean, "cd %s && git push" % clean,
                    "cd %s && git -C %s push" % (os.path.dirname(clean), os.path.basename(clean))):
            self.assert_silent(cmd)
        # a relative -C resolves against the event cwd
        p = hook("pre_tool_use.py", bash_event(bad, "git -C %s commit" % os.path.relpath(clean, bad)))
        self.assertEqual(0, p.returncode, p.stderr)

    def test_two_trees_are_each_gated_once(self):
        one, two = new_repo(), new_repo()
        self.addCleanup(gf.remove, one)
        self.addCleanup(gf.remove, two)
        gf.write(one, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        gf.write(two, os.path.join(gf.STATE, "findings.yml"), ORPHAN.replace("T1-ORPHAN-1", "T1-ORPHAN-2"))
        p = hook("pre_tool_use.py", bash_event(self.root, "git -C %s commit -m x && git -C %s push" % (one, two)))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertEqual(1, p.stderr.count("FAIL register.owner.T1-ORPHAN-1:"), p.stderr)
        self.assertEqual(1, p.stderr.count("FAIL register.owner.T1-ORPHAN-2:"), p.stderr)
        for cmd in ("git commit -m x && git push && git -C . push",       # one tree, three calls
                    "git -C docs commit -m x && git push"):               # one tree named by two directories
            p = hook("pre_tool_use.py", bash_event(one, cmd))
            self.assertEqual(2, p.returncode, p.stderr)
            self.assertEqual(1, p.stderr.count("FAIL register.owner.T1-ORPHAN-1:"), (cmd, p.stderr))
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m x; git -C %s push" % two))   # clean cwd, failing two
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertEqual(["FAIL register.owner.T1-ORPHAN-2"], [l.split(":")[0] for l in p.stderr.splitlines()
                                                              if l.startswith("FAIL")])

    def test_documented_limits_are_not_gated(self):
        # DEC-0005: the git hooks gate these (pre-commit for commits, pre-push for pushes)
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("G=git; $G push", "git $V", 'eval "$CMD"', "bash script-that-pushes.sh",
                    "echo push | xargs git", "xargs -a verbs.txt /usr/bin/git", 'g"it" push', "git p\"ush\"",
                    "alias gp='git p''ush'; gp"):
            self.assert_silent(cmd)

    def test_a_mention_in_data_runs_the_gates(self):
        cmds = ('echo "git push"', "python3 - <<'EOF'\nprint('then git commit the change')\nEOF",
                "grep -rn 'git push' docs", "echo hi # git push", "cat <<'EOF'\ngit push\nEOF")
        for cmd in cmds:
            self.assert_silent(cmd)          # a clean register: the gates run and pass, nothing printed
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in cmds:
            self.assert_gated(cmd)

    def test_path_qualified_git_and_prefixes_are_gated(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("/usr/bin/git push", "./git push", "'/usr/bin/git' commit -m x", "sudo git push",
                    "sudo -u nobody git commit -m x", "command git push", "nohup git push", "time git push",
                    "env git push", "env FOO=1 git push", "env -i -u X git push", "/usr/bin/env FOO=1 /usr/bin/git push",
                    "timeout 5 git push", "FOO=1 /usr/bin/git commit -m x", "nice -n 5 git push", "sudo -E nohup git push",
                    "echo hi && git push", "echo hi; git push", "true || git commit -m x", "ls | git commit -m x",
                    "echo hi\ngit push", "x=$(git commit -m y)", "echo `git push`", "(git push)", "{ git push; }",
                    "if git push; then echo ok; fi", "A=1 " * 13 + "git push", "FOO=" + "x" * 300 + " git push"):
            self.assert_gated(cmd)
        for cmd in ("/usr/bin/git status", "sudo ls", "env FOO=1 ls", "mygit push", "echo git-push", "sudo git log",
                    "my-git push", "x.git push"):
            self.assert_silent(cmd)

    def test_hook_bypasses_are_blocked(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd, form in (("git commit --no-verify -m x", "--no-verify after a gated git verb"),
                          ("git commit -m x --no-verif", "--no-verif after"), ("git push --no-veri", "--no-veri after"),
                          ("git commit -n -m x", "-n after"), ("git commit -anm x", "-anm after"),
                          ("git push -n origin HEAD", "-n after"), ("git push --no-verify origin", "--no-verify after"),
                          ("git commit -m 'a; b' --no-verify", "--no-verify after"),
                          ("git commit -m x \"-n\"", "-n after"), ("git add f && git commit -qn -m x", "-qn after"),
                          ("git -c core.hooksPath=/dev/null commit -m x", "core.hooksPath"),
                          ("git --config-env=core.hooksPath=H commit -m x", "core.hooksPath"),
                          ("git config core.hooksPath /tmp/h", "core.hooksPath"),
                          ("git -c CORE.HOOKSPATH=x push", "core.hooksPath"),
                          ("GIT_CONFIG_PARAMETERS=\"'a.b'='c'\" git push", "GIT_CONFIG_* assignment"),
                          ("GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=a.b GIT_CONFIG_VALUE_0=c git push", "GIT_CONFIG_* assignment"),
                          ("env GIT_CONFIG_GLOBAL=/tmp/g git commit -m x", "GIT_CONFIG_* assignment"),
                          ("GIT_CONFIG_SYSTEM=/x git push", "GIT_CONFIG_* assignment"),
                          ("export GIT_CONFIG_NOSYSTEM=1; git push", "GIT_CONFIG_* assignment"),
                          ("GIT_CONFIG_KEY_0=a.b ls", "GIT_CONFIG_* assignment"),
                          # a flag before `)`, a backtick or a redirection; `>&`, `&>` and `$(...;...)` inside the command
                          ("(git commit -m x --no-verify)", "--no-verify after"), ("out=$(git push --no-verify)", "--no-verify after"),
                          ("out=`git push -n`", "-n after"), ("git commit -m x 2>&1 --no-verify", "--no-verify after"),
                          ("git commit -m x &>/dev/null --no-verify", "--no-verify after"),
                          ("git commit -m x --no-verify>/dev/null", "--no-verify after"), ("git push -n</dev/null", "-n after"),
                          ("git commit -m $(date; true) --no-verify", "--no-verify after"),
                          ("git push >&2 --no-verify", "--no-verify after"), ("git commit -m x --no-verify;", "--no-verify after"),
                          # option values with substitutions and the rest of git's global options are read
                          ("git -C $(pwd) commit -m x --no-verify", "--no-verify after"), ("git -C `pwd` push -n", "-n after"),
                          ("git --shallow-file x commit --no-verify", "--no-verify after"),
                          ("git --no-literal-pathspecs push --no-verify", "--no-verify after")):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertTrue(p.stderr.startswith("FAIL hook.bypass: "), (cmd, p.stderr))
            self.assertIn(form, p.stderr, cmd)
            self.assertEqual(1, len(p.stderr.splitlines()), (cmd, p.stderr))     # no gate ran
            self.assertNotIn("register", p.stderr, cmd)
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), "[]\n")
        for cmd in ("git commit -m x && grep -n y f", "git push --dry-run", "nice -n 5 git push", "git log -n 3",
                    "git commit --amend --no-edit", "git push --set-upstream origin x", "git commit -m x\nls -n",
                    "git config --get user.name", "echo GIT_CONFIG", "git push -u origin HEAD",
                    "git commit -m x 2>&1 && ls -n", "git push 2>&1 | grep -n x", "x=$(git log -n 3)",
                    "(git commit -m x) && ls -n", "git commit -m x &>/dev/null; ls -n", "git push >&2; sort -n f",
                    "git commit -m x & ls -n", "echo $(git push) | wc -l"):
            self.assert_silent(cmd)

    def test_long_runs_of_backslashes_and_options_do_not_stall_the_hook(self):
        for cmd in ("git -c x=" + "\\" * 300 + " status", "git " + "-c " * 300 + "status", "git " + "-C " * 300 + "status",
                    "git " + '-c "a" ' * 300 + "status", "git " + "$x " * 300 + "status",
                    "git " + "--git-dir " * 300 + "status", "git -C " + "\\ " * 300 + "status",
                    # git words inside option runs, verbs, segments and option-like words: every scan is linear
                    "git -c " * 30000 + "status", "git -C " * 30000, 'git -c "' * 30000, "git $x " * 30000,
                    "git commit -m x " + "<<A\n" * 20000, "git commit -m x && " + "bash -o x " * 10000 + "-c y",
                    "git commit " * 30000, "git commit -m 'x' ; " * 20000, "git commit " + "-aaaaaaaaa! " * 20000,
                    "git commit \"" + "\\\"" * 50000, "-" + "a" * 100000, '"' * 50000 + "git commit",
                    # unterminated substitutions and backticks in option values and segments, redirection runs
                    "git -C $(" * 20000, "git -C `" * 20000, "git commit $(" * 20000, "git commit " + ">&" * 50000,
                    "$(a" * 30000 + ")", "cd " + "$(" * 30000, "cd `" + "x" * 100000):
            start = time.monotonic()
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual((0, ""), (p.returncode, p.stderr), cmd[:30])
            self.assertLess(time.monotonic() - start, 2.0, cmd[:30])
        gated = "git -c x=" + "\\" * 300 + " commit -m x"      # same runs before a real verb: still gated, still fast
        start = time.monotonic()
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        p = hook("pre_tool_use.py", bash_event(self.root, gated))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertLess(time.monotonic() - start, 2.0)

    def test_a_named_directory_that_is_not_a_repo_fails_closed(self):
        outside = tempfile.mkdtemp(prefix="gate-test-outside-")
        self.addCleanup(gf.remove, outside)
        for cmd, d in (("git -C %s commit -m x" % outside, outside), ("cd %s && git push" % outside, outside),
                       ("git -C /nonexistent-dir push", "/nonexistent-dir"), ('git -C "$WT" push', "%s/$WT" % self.root),
                       ("git -C $(pwd) push", "%s/$(pwd)" % self.root), ("cd `pwd` && git push", "%s/`pwd`" % self.root)):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertTrue(p.stderr.startswith("FAIL hook.target: %s: " % d), (cmd, p.stderr))

    def test_ungated_commands_and_tools_pass_even_on_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("git status", "git log --grep commit", "gh pr view", "ls"):
            self.assert_silent(cmd)
        agent = dict(bash_event(self.root, ""), tool_name="Agent",
                     tool_input={"prompt": "git push --no-verify", "description": "d", "subagent_type": "general-purpose"})
        p = hook("pre_tool_use.py", agent)
        self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr))

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
        for raw in ("", "not json", "[1, 2]", json.dumps(dict(bash_event(self.root, ""), tool_input={"command": 7})),
                    json.dumps(dict(bash_event(self.root, ""), tool_input=None))):
            p = hook("pre_tool_use.py", raw)
            self.assertEqual(2, p.returncode, raw)
            self.assertTrue(p.stderr.startswith("FAIL hook.internal: "), p.stderr)

    def test_cwd_outside_a_repo_fails_closed(self):
        outside = os.path.dirname(self.root)
        p = hook("pre_tool_use.py", bash_event(outside, "git commit -m x"))
        self.assertEqual(2, p.returncode)
        self.assertTrue(p.stderr.startswith("FAIL hook.target: %s: " % outside), p.stderr)
        for cwd in (None, "", 7):
            event = bash_event(self.root, "git commit -m x")
            if cwd is None:
                del event["cwd"]
            else:
                event["cwd"] = cwd
            p = hook("pre_tool_use.py", event)       # the hook's own process cwd is this repository: it must not be used
            self.assertEqual((2, "FAIL hook.input: the event has no cwd\n"), (p.returncode, p.stderr), cwd)


class GitHooks(unittest.TestCase):
    def setUp(self):
        self.root = new_repo()
        copy_gates(self.root)
        gf.commit_all(self.root, "gates")
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.hook = os.path.join(self.root, ".git", "hooks", "pre-commit")
        self.pre_push = os.path.join(self.root, ".git", "hooks", "pre-push")

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

    def test_pre_commit_blocks_an_ignored_file_the_gates_open_but_not_a_ds_store(self):
        gf.write(self.root, ".gitignore", "ignored.md\n.DS_Store\n")
        gf.git(self.root, "add", ".gitignore")
        gf.write(self.root, os.path.join("docs", "reviews", ".DS_Store"), "\x00bud1\n")
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)     # a Finder file no gate can read a marker from
        gf.write(self.root, os.path.join("docs", "reviews", "ignored.md"), "register: true\n- T1-GHOST-1: x\n")
        gf.write(self.root, "y.txt", "y\n")
        gf.git(self.root, "add", "y.txt")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)                       # ignored, yet gate_register opens it
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)
        self.assertIn("ignored.md", p.stderr)

    def test_pre_commit_blocks_an_unstaged_edit_to_a_file_that_decides_the_verdict(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        for rel in ("config.yml", "gate_register.py", "run_all.py", "lib/registers.py", "hooks/git-pre-commit.sh"):
            path = os.path.join(self.root, "tools", "gates", *rel.split("/"))
            with open(path) as fh:
                orig = fh.read()
            with open(path, "w") as fh:
                fh.write(orig + "\n# edited\n")
            p = self.commit()
            self.assertNotEqual(0, p.returncode, rel)
            self.assertIn("FAIL pre-commit.unstaged", p.stderr)
            self.assertIn("tools/gates/" + rel, p.stderr)
            with open(path, "w") as fh:
                fh.write(orig)

    def test_pre_commit_blocks_an_untracked_module_in_the_gate_code(self):
        # run_all.py searches the repo root, then tools/gates, before PyYAML and the stdlib: an untracked
        # yaml.py in either would be imported in place of PyYAML and pass
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        gf.git(self.root, "add", "-A")
        head = gf.git(self.root, "rev-parse", "HEAD")
        for rel in ("tools/gates/yaml.py", "tools/gates/glob/__init__.py", "tools/gates/datetime.pyc",
                    "tools/gates/lib/extra.py", "tools/gates/hooks/new.py",
                    "yaml.py", "yaml/__init__.py", "yaml/__init__.pyc", "glob/__init__.so", "glob.pyc", "_yaml.so"):
            gf.write(self.root, rel, "raise SystemExit(0)\n")
            p = self.commit()
            self.assertNotEqual(0, p.returncode, rel)
            self.assertIn("FAIL pre-commit.unstaged", p.stderr, rel)
            self.assertIn(rel, p.stderr)
            self.assertEqual(head, gf.git(self.root, "rev-parse", "HEAD"), rel)
            path = os.path.join(self.root, *rel.split("/"))
            os.remove(path)
            if not os.listdir(os.path.dirname(path)):
                os.rmdir(os.path.dirname(path))      # the new glob/ and yaml/ packages

    def test_edits_to_gate_tests_and_readme_do_not_block_a_commit(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        gf.write(self.root, os.path.join("tools", "gates", "tests", "new_test.py"), "x = 1\n")
        path = os.path.join(self.root, "tools", "gates", "README.md")
        with open(path, "a") as fh:
            fh.write("\nedited\n")
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)

    def test_bytecode_caches_under_the_gates_do_not_block_a_commit(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        gf.write(self.root, os.path.join("tools", "gates", "__pycache__", "m.cpython-39.pyc"), "x")
        gf.write(self.root, os.path.join("tools", "gates", "lib", "__pycache__", "n.cpython-39.pyc"), "x")
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)

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

    def push(self):
        """`git push` of HEAD to a fresh bare remote; returns (process, the ref the remote holds or "")."""
        remote = tempfile.mkdtemp(prefix="gate-test-remote-")
        self.addCleanup(gf.remove, remote)
        gf.git(remote, "init", "-q", "--bare")
        env = isolated_env(CLAUDE_PROJECT_DIR="/nonexistent-project")   # the hook must ignore it
        p = subprocess.run(["git", "push", "-q", remote, "HEAD:refs/heads/main"], cwd=self.root,
                           capture_output=True, text=True, env=env, check=False, timeout=60)   # a looping hook is an error
        got = subprocess.run(["git", "--git-dir", remote, "rev-parse", "-q", "--verify", "refs/heads/main"],
                             capture_output=True, text=True, env=isolated_env(), check=False)
        return p, got.stdout.strip()

    def test_install_writes_both_shims(self):
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertEqual(["install-hooks: pre-commit -> tools/gates/hooks/git-pre-commit.sh (%s)" % self.hook,
                          "install-hooks: pre-push -> tools/gates/hooks/git-pre-push.sh (%s)" % self.pre_push],
                         [l.replace(os.path.realpath(self.root), self.root) for l in p.stdout.splitlines()])
        for path, script in ((self.hook, "git-pre-commit.sh"), (self.pre_push, "git-pre-push.sh")):
            with open(path) as fh:
                text = fh.read()
            self.assertEqual("#!/bin/sh\n# strict-delivery-gates-hook\n", text[:len("#!/bin/sh\n# strict-delivery-gates-hook\n")])
            self.assertIn("/tools/gates/hooks/%s\"" % script, text)
            self.assertNotIn("@HOOK@", text)
            self.assertTrue(os.access(path, os.X_OK), path)
        with open(self.pre_push) as fh:
            self.assertIn("FAIL pre-push.missing", fh.read())

    def test_a_foreign_pre_push_hook_is_moved_aside(self):
        gf.write(self.root, ".git/hooks/pre-push", "#!/bin/sh\necho mine\n")
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("moved the existing pre-push hook", p.stdout)
        self.assertNotIn("moved the existing pre-commit hook", p.stdout)
        with open(self.pre_push + ".pre-strict-delivery") as fh:
            self.assertEqual("#!/bin/sh\necho mine\n", fh.read())
        with open(self.pre_push) as fh:
            self.assertIn("# strict-delivery-gates-hook", fh.read())
        # a refusal for one hook installs neither
        gf.write(self.root, ".git/hooks/pre-push", "#!/bin/sh\necho two\n")
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho three\n")
        p = self.install()
        self.assertEqual(1, p.returncode)
        self.assertIn("pre-push.pre-strict-delivery already exists", p.stderr)
        with open(self.hook) as fh:
            self.assertEqual("#!/bin/sh\necho three\n", fh.read())
        self.assertFalse(os.path.exists(self.hook + ".pre-strict-delivery"))

    def test_pre_push_blocks_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        gf.commit_all(self.root, "orphan")      # gitfixture commits without hooks: a commit made with --no-verify
        p, got = self.push()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stdout + p.stderr)
        self.assertEqual("", got)

    def test_pre_push_blocks_unstaged_register_edits(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), "[]\n# edited\n")   # not staged
        p, got = self.push()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-push.uncommitted", p.stderr)
        self.assertIn("findings.yml", p.stderr)
        self.assertEqual("", got)
        gf.git(self.root, "add", "-A")        # staged but not committed: HEAD, which is pushed, differs
        p, got = self.push()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-push.uncommitted", p.stderr)
        self.assertEqual("", got)
        gf.git(self.root, "reset", "-q")
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), "[]\n")      # back to HEAD
        gf.write(self.root, os.path.join("docs", "reviews", "new.md"), "x\n")     # untracked
        p, got = self.push()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-push.uncommitted", p.stderr)
        self.assertIn("docs/reviews/new.md", p.stderr)
        self.assertEqual("", got)

    def test_pre_push_passes_a_clean_tree(self):
        gf.write(self.root, "x.txt", "x\n")      # outside the guarded paths: neither staged nor not
        p, got = self.push()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), got)

    def test_a_tree_without_the_gates_cannot_push(self):
        gf.git(self.root, "rm", "-r", "-q", "tools")
        gf.commit_all(self.root, "no gates")
        p, got = self.push()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-push.missing", p.stderr)
        self.assertEqual("", got)

    def test_a_symlinked_hook_is_moved_aside_not_written_through(self):
        # S4's plan links pre-push to the tracked script: a write through the link would replace the
        # tracked script by the shim, which then execs itself
        for hook, path in (("pre-commit", self.hook), ("pre-push", self.pre_push)):
            os.remove(path)
            os.symlink(os.path.join("..", "..", "tools", "gates", "hooks", "git-%s.sh" % hook), path)
        os.symlink("/nonexistent-target", self.pre_push + ".dangling")
        p = self.install()
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("moved the existing pre-commit hook", p.stdout)
        self.assertIn("moved the existing pre-push hook", p.stdout)
        self.assertEqual("", gf.git(self.root, "status", "--porcelain", "--", "tools"))    # the tracked scripts are untouched
        for hook, path in (("pre-commit", self.hook), ("pre-push", self.pre_push)):
            self.assertFalse(os.path.islink(path), path)
            with open(path) as fh:
                self.assertIn("/tools/gates/hooks/git-%s.sh\"" % hook, fh.read())
            self.assertEqual(os.path.join("..", "..", "tools", "gates", "hooks", "git-%s.sh" % hook),
                             os.readlink(path + ".pre-strict-delivery"))
        p, got = self.push()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), got)
        # a backup that is a dangling symlink still counts: a symlinked hook over it is refused
        os.remove(self.pre_push)
        os.remove(self.pre_push + ".pre-strict-delivery")
        os.rename(self.pre_push + ".dangling", self.pre_push + ".pre-strict-delivery")
        os.symlink("/bin/true", self.pre_push)
        p = self.install()
        self.assertEqual(1, p.returncode)
        self.assertIn("pre-push.pre-strict-delivery already exists", p.stderr)
        self.assertEqual("/bin/true", os.readlink(self.pre_push))


class SessionStart(unittest.TestCase):
    def setUp(self):
        self.root = new_repo()

    def tearDown(self):
        gf.remove(self.root)

    def event(self):
        return {"session_id": "s", "transcript_path": "/dev/null", "cwd": self.root,
                "hook_event_name": "SessionStart", "source": "startup"}

    def test_reports_missing_gates(self):
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("strict-delivery: gates NOT installed in", p.stdout)

    def test_installs_git_hooks_and_lists_handoffs(self):
        copy_gates(self.root)
        gf.write(self.root, "docs/superpowers/state/demo/handoff.md", "---\nplan: x\n---\n")
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("strict-delivery: gates installed in", p.stdout)
        self.assertIn("strict-delivery: handoff docs/superpowers/state/demo/handoff.md", p.stdout)
        self.assertIn("install-hooks: pre-commit ->", p.stdout)
        with open(os.path.join(self.root, ".git", "hooks", "pre-commit")) as fh:
            self.assertIn("# strict-delivery-gates-hook", fh.read())
        p = hook("session_start.py", self.event())          # second start: already ours
        self.assertNotIn("install-hooks:", p.stdout)
        self.assertIn("invoke the strict-delivery skill", p.stdout)

    def test_session_start_installs_a_missing_pre_push_shim(self):
        copy_gates(self.root)
        self.assertEqual(0, hook("session_start.py", self.event()).returncode)
        pre_push = os.path.join(self.root, ".git", "hooks", "pre-push")
        os.remove(pre_push)                                 # pre-commit is still ours
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("install-hooks: pre-push ->", p.stdout)
        self.assertNotIn("FAIL", p.stdout)
        with open(pre_push) as fh:
            self.assertIn("# strict-delivery-gates-hook", fh.read())
        os.remove(pre_push)                                 # a symlink to the tracked script carries the marker: not ours
        os.symlink(os.path.join("..", "..", "tools", "gates", "hooks", "git-pre-push.sh"), pre_push)
        p = hook("session_start.py", self.event())
        self.assertIn("install-hooks: moved the existing pre-push hook", p.stdout)
        self.assertFalse(os.path.islink(pre_push))

    def assert_failure_reaches_the_model(self, p, line):
        """SessionStart cannot block and only exit-0 stdout reaches the model: the FAIL is on stdout
        (context) and echoed on stderr (owner), the skill line follows it, the exit code is 0."""
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn(line, p.stdout.splitlines()[-2], p.stdout)
        self.assertIn(line, p.stderr)
        self.assertEqual("strict-delivery: invoke the strict-delivery skill before any other action",
                         p.stdout.splitlines()[-1])

    def test_install_failure_is_reported_on_stdout_and_exits_0(self):
        copy_gates(self.root)
        gf.write(self.root, "tools/gates/install-hooks.sh", "echo boom >&2; exit 3\n")
        p = hook("session_start.py", self.event())
        self.assert_failure_reaches_the_model(p, "FAIL hook.install: install-hooks.sh exit 3: boom")
        self.assertIn("strict-delivery: gates installed in", p.stdout)

    def test_a_non_utf8_foreign_hook_is_moved_aside(self):
        copy_gates(self.root)
        with open(os.path.join(self.root, ".git", "hooks", "pre-commit"), "wb") as fh:
            fh.write(b"#!/bin/sh\n# \xff\xfe\n")
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertNotIn("FAIL", p.stdout)
        self.assertIn("install-hooks: moved the existing pre-commit hook", p.stdout)
        self.assertTrue(os.path.isfile(os.path.join(self.root, ".git", "hooks", "pre-commit.pre-strict-delivery")))

    def test_a_refusal_of_install_hooks_is_reported_and_exits_0(self):
        copy_gates(self.root)
        hooks = os.path.join(self.root, ".git", "hooks")
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho mine\n")
        gf.write(self.root, ".git/hooks/pre-commit.pre-strict-delivery", "#!/bin/sh\necho older\n")
        p = hook("session_start.py", self.event())
        self.assert_failure_reaches_the_model(p, "FAIL hook.install: install-hooks.sh exit 1: install-hooks: refusing:")
        with open(os.path.join(hooks, "pre-commit")) as fh:
            self.assertIn("echo mine", fh.read())           # the foreign hook was not touched
        gf.git(self.root, "config", "core.hooksPath", os.path.join(self.root, "..", "shared-hooks"))
        os.remove(os.path.join(hooks, "pre-commit"))
        p = hook("session_start.py", self.event())
        self.assert_failure_reaches_the_model(
            p, "FAIL hook.install: install-hooks.sh exit 1: install-hooks: refusing: the hooks directory")

    def test_a_cwd_that_is_not_a_repository_fails_closed(self):
        outside = tempfile.mkdtemp()
        try:
            event = self.event()
            event["cwd"] = outside
            p = hook("session_start.py", event)
        finally:
            shutil.rmtree(outside)
        self.assert_failure_reaches_the_model(p, "FAIL hook.internal: RuntimeError: git -C")

    def test_a_missing_or_invalid_cwd_is_a_failure_not_a_fallback(self):
        for cwd, line in ((None, "FAIL hook.input: the event has no cwd"), ("", "FAIL hook.input: the event has no cwd"),
                          (7, "FAIL hook.input: the event has no cwd"),
                          (os.path.join(self.root, "nope"), "FAIL hook.input: cwd %s is not a directory"
                           % os.path.join(self.root, "nope"))):
            event = self.event()
            if cwd is None:
                del event["cwd"]
            else:
                event["cwd"] = cwd
            p = hook("session_start.py", event)       # the hook's own process cwd is this repository: it must not be used
            self.assert_failure_reaches_the_model(p, line)
            self.assertNotIn("gates installed", p.stdout)

    def test_malformed_stdin_is_reported_and_exits_0(self):
        for raw, line in (("nope", "FAIL hook.internal: JSONDecodeError:"), ("[]", "FAIL hook.internal: ValueError: stdin is not a JSON object")):
            p = hook("session_start.py", raw)
            self.assert_failure_reaches_the_model(p, line)


class UserPromptSubmit(unittest.TestCase):
    def test_one_line_reminder(self):
        p = hook("user_prompt_submit.py", {"hook_event_name": "UserPromptSubmit", "prompt": "hi", "cwd": "/"})
        self.assertEqual(0, p.returncode)
        self.assertEqual(1, len(p.stdout.splitlines()))
        self.assertTrue(p.stdout.startswith("strict-delivery: "))

    def test_malformed_stdin_fails_closed(self):
        for raw, line in (("nope", "FAIL hook.internal: JSONDecodeError:"),
                          ("[]", "FAIL hook.internal: ValueError: stdin is not a JSON object")):
            p = hook("user_prompt_submit.py", raw)
            self.assertEqual(2, p.returncode, raw)
            self.assertIn(line, p.stderr)
            self.assertEqual("", p.stdout)


class ProjectWiring(unittest.TestCase):
    def test_settings_json_is_the_expected_wiring_without_stop_hooks(self):
        def command(script, timeout=None):
            h = {"type": "command", "command": 'python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/%s"' % script}
            if timeout:
                h["timeout"] = timeout
            return {"hooks": [h]}
        # No "if" on the Bash handler: pre_tool_use.py alone decides what is gated (owner ruling 5).
        # No Stop/SubagentStop: S2 adds them together with their scripts.
        expected = {"hooks": {
            "PreToolUse": [dict(command("pre_tool_use.py", 300), matcher="Agent"),
                           dict(command("pre_tool_use.py", 900), matcher="Bash")],
            "SessionStart": [command("session_start.py")],
            "UserPromptSubmit": [command("user_prompt_submit.py")]}}
        with open(os.path.join(gf.REPO, ".claude", "settings.json")) as fh:
            self.assertEqual(expected, json.load(fh))
        for groups in expected["hooks"].values():
            for g in groups:
                script = g["hooks"][0]["command"].split('"')[1].replace("${CLAUDE_PROJECT_DIR}", gf.REPO)
                self.assertTrue(os.path.isfile(script), script)

    def test_claude_md_carries_spec_section_12_verbatim_and_nothing_else(self):
        with open(os.path.join(gf.REPO, "docs/superpowers/specs/2026-10-17-strict-delivery-design.md")) as fh:
            spec = fh.read()
        section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
        bullets = [l for l in section.splitlines() if l.startswith("- ")]
        self.assertEqual(11, len(bullets))
        with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
            claude = fh.read().splitlines()
        self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])


if __name__ == "__main__":
    unittest.main()
