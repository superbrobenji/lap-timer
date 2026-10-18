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
                    "git ${X} commit -m x", "git $X push"):     # unexpandable option word: gated conservatively
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr, cmd)

    def test_path_qualified_git_and_wrapper_prefixes_are_gated(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("/usr/bin/git push", "./git push", "'/usr/bin/git' commit -m x", "sudo git push",
                    "sudo -u nobody git commit -m x", "command git push", "nohup git push", "time git push",
                    "env git push", "env FOO=1 git push", "env -i -u X git push", "/usr/bin/env FOO=1 /usr/bin/git push",
                    "timeout 5 git push", "FOO=1 /usr/bin/git commit -m x"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr, cmd)
        for cmd in ("env -C /tmp git push", "sudo -D /tmp git push", "/usr/bin/env -S 'git push'"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, (cmd, p.stderr))
            self.assertIn("FAIL hook.target", p.stderr, cmd)
        for cmd in ("/usr/bin/git status", "sudo ls", "env FOO=1 ls", "mygit push", "echo git-push"):
            self.assertEqual(0, hook("pre_tool_use.py", bash_event(self.root, cmd)).returncode, cmd)

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

    def test_a_gated_command_whose_tree_cannot_be_told_is_blocked(self):
        for cmd in ("cd /a && cd /b && git push", "export X=1 && cd /b && git push", "(cd /b && git push)",
                    "pushd /b && git push", "git commit -m x && cd /y", 'git -C "$WT" push', "git -C `pwd` push",
                    'cd "$WT" && git push', "git --git-dir=/b/.git push", "git --work-tree /b commit",
                    "GIT_DIR=/b/.git git push", "export GIT_WORK_TREE=/b; git push",
                    "bash -c 'cd /wt && git push'", 'sh -c "cd /wt && git commit -m x"', "/bin/zsh -lc 'git push'",
                    "env -C /wt git push", "env --chdir=/wt git push", "eval 'git push'", "git ls-files | xargs git commit",
                    "bash <<EOF\ngit push\nEOF", "echo 'git push' | sh",
                    "bash -o pipefail -c 'cd /wt && git push'", "bash --rcfile x -c 'git push'",   # an option with a value first
                    "/bin/bash -ec 'git push'", "nohup bash -xc 'git push'", "'bash' -c 'git push'", 'bash "-c" "git push"',
                    "bash -s <<'EOF'\ngit push\nEOF", "bash<<EOF\ngit push\nEOF", "bash <<< 'git push'",
                    "cat <<EOF | bash\ncd /wt && git push\nEOF", "find /wt -execdir git push \\;", "sudo -D /wt git push",
                    "git commit -F - <<EOF\nbody\nEOF\ncd /elsewhere"):   # a real cd after the heredoc terminator
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(2, p.returncode, cmd)
            self.assertTrue(p.stderr.startswith("FAIL hook.target: cannot tell which tree "), (cmd, p.stderr))
            self.assertIn("use git -C <dir> or a leading cd <dir> &&", p.stderr)
        for cmd in ('git commit -m "cd elsewhere, GIT_DIR=x"', "git -C . push", "cd . && git push", "cd .; git commit -m x",
                    'git commit -m "then run git push and bash -c x"', "bash build.sh && git push", "env FOO=1 git push",
                    "git commit -m \"$(cat <<'EOF'\nfix cd and git push; bash -c x; GIT_DIR=/y\nEOF\n)\"",
                    "git commit -F - <<'EOF'\nmentions cd /x, GIT_DIR=/y and eval\nEOF",
                    'git commit -F - <<"EOF"\ncd /x\nEOF', "git commit -F - <<-EOF\n\tcd /x\n\tEOF",
                    "git commit -m \"$(cat <<'EOF'\nuses bash -c, xargs and eval | sh\nEOF\n)\" && git push 2>&1 | tail -3",
                    "git commit -m 'run: bash -o pipefail -c x'", "echo $((1<<3)) && git push", "git commit -S -m x"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))   # followed forms and quoted words still work
            self.assertEqual((0, ""), (p.returncode, p.stderr), cmd)

    def test_long_runs_of_backslashes_and_options_do_not_stall_the_hook(self):
        for cmd in ("git -c x=" + "\\" * 300 + " status", "git " + "-c " * 300 + "status", "git " + "-C " * 300 + "status",
                    "git " + '-c "a" ' * 300 + "status", "git " + "$x " * 300 + "status",
                    "git " + "--git-dir " * 300 + "status", "git -C " + "\\ " * 300 + "status"):
            start = time.monotonic()
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(0, p.returncode, (cmd[:30], p.stderr))
            self.assertLess(time.monotonic() - start, 2.0, cmd[:30])
        # heredoc openers without a terminator, shell words, options before -c, quotes: every scan is linear
        for cmd, rc in (("git commit -m x " + "<<A\n" * 20000, 0), ("git commit -m x\n" + "<<A\n" * 20000 + "A", 0),
                        ("git commit -m x && " + "sh " * 20000, 0), ("git commit -m x && " + "'a' " * 20000, 0),
                        ("git commit -m x && " + "bash -o x " * 10000 + "-c y", 2),
                        ("git commit -m x && bash " + "<<A " * 20000, 2), ("git commit -m x " + "export " * 20000, 0)):
            start = time.monotonic()
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(rc, p.returncode, (cmd[:30], p.stderr))
            self.assertTrue(rc == 0 or p.stderr.startswith("FAIL hook.target: "), (cmd[:30], p.stderr))
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
