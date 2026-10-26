"""make_review_package.main() on a fixture repo (strict-delivery S2 Task 5).
Run from the repo root: python3 -m unittest tools.gates.tests.test_make_review_package -v
"""
import contextlib
import io
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.gates import gate_plan, gate_review_package as grp
from tools.gates import make_review_package as mrp
from tools.gates.tests import repo_fixture as rf

REVIEW = ".superpowers/sdd/fx/review"
MUTATION = ("finding: SD-FX-1\n"
            "test: test_fx\n"
            "step 1 (fix present): python3 -m unittest t.T.test_fx -> PASS\n"
            "step 2 (fix reverted: git stash push src/fx.py): python3 -m unittest t.T.test_fx -> FAIL\n"
            "step 3 (fix restored): python3 -m unittest t.T.test_fx -> PASS\n")
SIBLINGS = "| sibling | test or finding |\n|---|---|\n| sib-a | test_sib_a |\n"
CLAIMS = ("| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
          "| fx fixed | grep -n fixed src/fx.py | grep -c unfixed src/fx.py | 1:fixed = True / 0 | PASS |\n")


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


class MakeReviewPackage(unittest.TestCase):
    def setUp(self):
        files = dict(rf.DISPATCH_FILES)
        files["docs/superpowers/state/fx/mutations/.gitkeep"] = ""
        files["docs/reviews/.gitkeep"] = ""    # gate_register needs the report directory
        files[".gitignore"] = ".superpowers/\n"    # as in the real repo: the package dir is not "dirty"
        files["tools/gates/tests/test_fx.py"] = "def test_sib_a():\n    pass\n"    # SIBLINGS' test resolves (B5-FIX-26)
        self.root = rf.make_repo(files, rf.load_rows())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.base = rf.git(self.root, "rev-parse", "HEAD")
        rf.write(self.root, "src/fx.py", "fixed = True\n")
        self.head = rf.commit(self.root, "feat: fixture fix")

    def run_main(self, main, *argv):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = main(list(argv))
        return rc, out.getvalue() + err.getvalue()

    def make(self, *argv):
        return self.run_main(mrp.main, *argv)

    def test_writes_commits_diff_and_gates(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertEqual(1, len(commits.splitlines()), commits)
        self.assertIn("feat: fixture fix", commits)
        diff = read(self.root, f"{REVIEW}/1/diff.patch")
        self.assertTrue(diff.startswith("diff --git"), diff[:80])
        self.assertIn("src/fx.py", diff)
        gates = read(self.root, f"{REVIEW}/1/gates.txt")
        self.assertIn("register", gates)
        self.assertNotIn("FAIL", gates)

    def test_keeps_the_implementer_files(self):
        mine = "| sibling | test or finding |\n|---|---|\n"
        rf.write(self.root, f"{REVIEW}/1/siblings.md", mine)
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertEqual(mine, read(self.root, f"{REVIEW}/1/siblings.md"))
        self.assertIn("claims.md", out)

    def test_empty_range_fails(self):
        rc, out = self.make("fx", "1", self.head, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("empty commit range", out)

    def test_reversed_range_fails(self):
        rc, out = self.make("fx", "1", self.head, self.base)
        self.assertEqual(1, rc, out)
        self.assertIn("not a descendant", out)

    def test_head_must_be_checked_out(self):
        first = rf.git(self.root, "rev-list", "--max-parents=0", "HEAD")
        rf.git(self.root, "checkout", "-q", self.base)
        rc, out = self.make("fx", "1", first, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("check out", out)

    def test_missing_plan_state_dir_fails(self):
        rc, out = self.make("nope", "1", self.base, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("new_plan_state", out)

    def test_red_gate_fails_but_still_writes_gates_txt(self):
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL", read(self.root, f"{REVIEW}/1/gates.txt"))

    def test_decorated_log_config_does_not_reach_commits_txt(self):
        rf.git(self.root, "config", "log.decorate", "short")
        rf.git(self.root, "tag", "task-tip", self.head)
        self.assertIn("tag: task-tip", rf.git(self.root, "log", "--oneline", "-1"))    # the config does decorate
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertRegex(commits, r"^[0-9a-f]{7,40} feat: fixture fix\n$")
        self.assertNotIn("tag:", commits)

    def test_diff_prefix_config_does_not_reach_diff_patch(self):
        rf.git(self.root, "config", "diff.noprefix", "true")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/src/fx.py b/src/fx.py"))

    def test_empty_diff_fails(self):
        rf.git(self.root, "commit", "-q", "--allow-empty", "-m", "no change")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.make("fx", "1", head + "~1", head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("empty diff", out)

    def test_task_name_cannot_leave_the_package_dir(self):
        rc, out = self.make("fx", "../x", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("not a directory name", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/fx/x")))

    def test_package_name_with_a_slash_is_refused(self):    # B5-FIX-9 (M22)
        for plan, task in (("fx/mutations", "1"), ("fx", "a/b"), ("fx", "a\\b")):
            with self.subTest(plan=plan, task=task):
                rc, out = self.make(plan, task, self.base, self.head, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn("not a directory name", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/fx/mutations")))
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW, "a")))

    def test_pending_hint_names_the_mutation_file_until_one_exists(self):    # B5-FIX-9 (M29)
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertIn("the implementer still adds: siblings.md, claims.md, mutation-<finding-id>.txt\n", out)
        rf.write(self.root, f"{REVIEW}/1/mutation-SD-FX-1.txt", MUTATION)
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertIn("the implementer still adds: siblings.md, claims.md\n", out)
        rf.write(self.root, f"{REVIEW}/1/siblings.md", SIBLINGS)
        rf.write(self.root, f"{REVIEW}/1/claims.md", CLAIMS)
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual((0, False), (rc, "still adds" in out), out)

    def test_helper_and_gate_agree_on_a_quoted_path_and_an_empty_subject(self):    # B5-FIX-7 sibling 3
        for what in ("caf\u00e9.md", "empty subject", "space-led subject"):
            with self.subTest(what):
                base = rf.git(self.root, "rev-parse", "HEAD")
                if what.endswith(".md"):
                    rf.write(self.root, what, "x\n")
                    head = rf.commit(self.root, "feat: add a quoted path")
                else:
                    rf.write(self.root, "src/fx.py", f"fixed = {what!r}\n")
                    rf.git(self.root, "add", "-A")
                    rf.git(self.root, "commit", "-q", "--allow-empty-message", "-m", "" if what[0] == "e" else " led")
                    head = rf.git(self.root, "rev-parse", "HEAD")
                rc, out = self.make("fx", "1", base, head)
                self.assertEqual(0, rc, out)
                self.assertIn("gates PASS", out)
                for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
                    rf.write(self.root, f"{REVIEW}/1/{name}", text)
                rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
                self.assertEqual(0, rc, out)
                self.assertNotIn("FAIL", out)

    def test_dot_names_are_refused(self):
        for plan, task in (("fx", ".."), ("fx", "."), ("..", "1"), (".", "1"), ("fx", ".hidden")):
            with self.subTest(plan=plan, task=task):
                rc, out = self.make(plan, task, self.base, self.head, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn("not a directory name", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/fx/commits.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/review")))

    def test_dirty_tree_fails_before_writing(self):
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "checkout", "-q", self.base, "--", "docs/superpowers/state/findings.yml")    # fix, uncommitted
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("working tree is not clean", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_untracked_file_fails_before_writing(self):
        rf.write(self.root, "stray.txt", "x\n")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("working tree is not clean", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_file_hidden_by_info_exclude_or_gitignore_fails_before_writing(self):    # B5-FIX-16 (DEC-0029)
        rf.write(self.root, "docs/superpowers/plans/fx.md", "# plan\n")
        rf.write(self.root, "docs/superpowers/state/fx/ledger.md", "plan: docs/superpowers/plans/fx.md\n")
        base = rf.commit(self.root, "ledger names the plan")
        rf.write(self.root, "src/fx.py", "fixed = 2\n")
        head = rf.commit(self.root, "feat: second fix")
        for how, rel in (("info/exclude", "tools/gates/lib/planted.py"), ("info/exclude", "docs/superpowers/state/x.yml"),
                         (".gitignore", "tools/gates/gate_planted.py"), (".gitignore", "tools/gates/x__pycache__/y.pyc"), (".gitignore", "tools/gates/lib/__pycache__"),
                         (".gitignore", "tools/gates/.superpowers/sdd/x/a.md"), (".gitignore", "tools/gates/lib/stray.pyc"),
                         (".gitignore", "docs/superpowers/plans/fx.md")):    # the plan last: it stays untracked
            with self.subTest(how=how, rel=rel):
                ignore = os.path.join(self.root, ".git/info/exclude" if how == "info/exclude" else ".gitignore")
                with open(ignore, encoding="utf-8") as f:
                    kept = f.read()
                self.addCleanup(rf.write, self.root, os.path.relpath(ignore, self.root), kept)
                rf.write(self.root, rel, "planted = True\n")
                with open(ignore, "a", encoding="utf-8") as f:
                    f.write(f"/{rel}\n")
                if rel.endswith("fx.md"):
                    rf.git(self.root, "rm", "-q", "--cached", rel)
                if how == ".gitignore":
                    rf.commit(self.root, "ignore it")
                    head = rf.git(self.root, "rev-parse", "HEAD")
                self.assertTrue(rf.git(self.root, "check-ignore", rel))
                rc, out = self.make("fx", "1", base, head, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn(f"ignored file(s) under the paths the gates read: {rel}", out)
                self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))
                os.remove(os.path.join(self.root, rel))

    def test_ignored_file_under_another_plan_the_gates_read_fails(self):    # B5-FIX-16 (DEC-0039 widen-all-plans)
        """The plan of every ledger in the state dir counts, not only the package's own: an excluded plan
        that another ledger (gx) names is refused; a state dir with no ledger.md or a line 1 naming no plan
        adds no path."""
        rf.write(self.root, "docs/superpowers/state/fx/ledger.md", "plan: docs/superpowers/plans/fx.md\n")
        rf.write(self.root, "docs/superpowers/plans/fx.md", "# plan\n")
        rf.write(self.root, "docs/superpowers/state/gx/ledger.md", "plan: docs/superpowers/plans/gx.md\n")
        rf.write(self.root, "docs/superpowers/state/hx/ledger.md", "no plan line\n")
        rf.write(self.root, "docs/superpowers/state/ix/notes.md", "not a ledger\n")
        head = rf.commit(self.root, "two ledgers")
        rf.write(self.root, "docs/superpowers/plans/gx.md", "# another plan\n")
        with open(os.path.join(self.root, ".git/info/exclude"), "a", encoding="utf-8") as f:
            f.write("/docs/superpowers/plans/gx.md\n")
        self.assertTrue(rf.git(self.root, "check-ignore", "docs/superpowers/plans/gx.md"))
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("ignored file(s) under the paths the gates read: docs/superpowers/plans/gx.md", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))
        os.remove(os.path.join(self.root, "docs/superpowers/plans/gx.md"))
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)

    def test_packages_dir_and_pycache_stay_allowed(self):    # B5-FIX-16 (DEC-0029, DEC-0033)
        rf.write(self.root, f"{REVIEW}/0/notes.md", "kept\n")    # the packages dir: ignored by .gitignore
        rf.write(self.root, "docs/superpowers/state/fx/ledger.md", "plan:\n")    # names no plan: no extra path
        rf.write(self.root, "tools/gates/__pycache__/gate_x.cpython-39.pyc", "pyc\n")
        rf.write(self.root, "tools/gates/lib/__pycache__/__init__.cpython-312.pyc", "pyc\n")
        rf.write(self.root, "tools/gates/__pycache__/notes.txt", "any file\n")    # DEC-0033: every file under a
        rf.write(self.root, "tools/gates/__pycache__/sub/x.pyc", "pyc\n")       # __pycache__ component is exempt
        with open(os.path.join(self.root, ".gitignore"), "a", encoding="utf-8") as f:
            f.write("__pycache__/\n")
        rf.commit(self.root, "ignore pycache")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertIn("gates PASS", out)

    def ignored_plan(self, line1, plan):
        """Commit a ledger whose line 1 is <line1> (bytes); plant <plan> untracked and excluded; run the helper."""
        path = os.path.join(self.root, "docs/superpowers/state/fx/ledger.md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(line1 + b"\n# Ledger: fx\n")
        head = rf.commit(self.root, "ledger")
        rf.write(self.root, plan, "# plan\n")
        with open(os.path.join(self.root, ".git/info/exclude"), "a", encoding="utf-8") as f:
            f.write(f"/{plan}\n")
        self.assertTrue(rf.git(self.root, "check-ignore", plan))
        return self.make("fx", "1", self.base, head, "--only", "register")

    def test_the_plan_named_by_line_1_is_read_as_gate_plan_reads_it(self):    # B5-FIX-16 (R-A2-2)
        for sep in ("\x0c", "\x0b", "\x1c", "\x85", "\u2028"):
            with self.subTest(sep=ascii(sep)):
                line1 = f"plan: docs/superpowers/plans/fx.md{sep}note".encode("utf-8")
                self.assertEqual("docs/superpowers/plans/fx.md",
                                 gate_plan.LEDGER_PLAN.match(line1.decode("utf-8").splitlines()[0]).group(1))
                rc, out = self.ignored_plan(line1, "docs/superpowers/plans/fx.md")
                self.assertEqual(1, rc, out)
                self.assertIn("ignored file(s) under the paths the gates read: docs/superpowers/plans/fx.md", out)
                os.remove(os.path.join(self.root, "docs/superpowers/plans/fx.md"))

    def test_a_non_utf8_line_1_still_writes_the_package(self):    # B5-FIX-16 (R-A2-4 strict-utf8)
        rc, out = self.ignored_plan(b"plan: docs/superpowers/plans/fx\xff.md", "docs/superpowers/plans/other.md")
        self.assertEqual(0, rc, out)
        self.assertIn("gates PASS", out)

    def test_an_empty_ledger_still_writes_the_package(self):    # B5-FIX-16 (no line 1: no plan path)
        rf.write(self.root, "docs/superpowers/state/fx/ledger.md", "")
        head = rf.commit(self.root, "empty ledger")
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertIn("gates PASS", out)

    def test_pathspec_magic_in_the_plan_path_is_literal(self):    # B5-FIX-16 (R-A2-3)
        rf.write(self.root, "tools/gates/lib/planted.py", "planted = True\n")
        with open(os.path.join(self.root, ".git/info/exclude"), "a", encoding="utf-8") as f:
            f.write("/tools/gates/lib/planted.py\n")
        rc, out = self.ignored_plan(b"plan: :(exclude)tools/gates", "docs/superpowers/plans/none.md")
        self.assertEqual(1, rc, out)
        self.assertIn("ignored file(s) under the paths the gates read: tools/gates/lib/planted.py", out)

    def test_a_plan_in_the_packages_dir_is_excepted(self):    # B5-FIX-16 (DEC-0029 "packages dir excepted")
        rc, out = self.ignored_plan(b"plan: .superpowers/sdd/fx/plan.md", ".superpowers/sdd/fx/plan.md")
        self.assertEqual(0, rc, out)
        self.assertIn("gates PASS", out)
        rc, out = self.ignored_plan(b"plan: .superpowers/sddx/plan.md", ".superpowers/sddx/plan.md")    # not the dir
        self.assertEqual(1, rc, out)
        self.assertIn("ignored file(s) under the paths the gates read: .superpowers/sddx/plan.md", out)

    def test_color_config_does_not_reach_the_package(self):
        rf.git(self.root, "config", "color.ui", "always")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertNotIn("\x1b", read(self.root, f"{REVIEW}/1/commits.txt"))
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/"))

    def assert_package_passes(self, head=None):
        """make_review_package then gate_review_package --dir on the same package: only PASS lines."""
        rc, out = self.make("fx", "1", self.base, head or self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
            rf.write(self.root, f"{REVIEW}/1/{name}", text)
        rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
        lines = [l for l in out.splitlines() if l.strip()]
        self.assertTrue(rc == 0 and lines and all(l.startswith("PASS ") for l in lines), out)

    def test_every_color_key_set_to_always_gives_a_package_that_passes(self):
        # B5-FIX-4 V-1: color.diff beats color.ui and colours both the log sha and the diff
        for key in ("color.ui", "color.diff", "color.decorate", "color.log", "color.status"):
            with self.subTest(key=key):
                rf.git(self.root, "config", key, "always")
                try:
                    self.assert_package_passes()
                    self.assertNotIn("\x1b", read(self.root, f"{REVIEW}/1/commits.txt")
                                     + read(self.root, f"{REVIEW}/1/diff.patch"))
                finally:
                    rf.git(self.root, "config", "--unset", key)

    def test_core_abbrev_4_gives_a_package_that_passes(self):
        rf.git(self.root, "config", "core.abbrev", "4")    # B5-FIX-4 V-2: 4-hex shas fail COMMIT_LINE
        self.assertRegex(rf.git(self.root, "log", "--oneline", "-1"), r"^[0-9a-f]{4} ")    # the config bites
        self.assert_package_passes()
        self.assertRegex(read(self.root, f"{REVIEW}/1/commits.txt"), r"^[0-9a-f]{12} feat: fixture fix\n$")

    def test_utf16_log_encoding_keeps_a_non_ascii_subject(self):
        rf.write(self.root, "src/fx.py", "fixed = 'caf\u00e9'\n")
        head = rf.commit(self.root, "fix: caf\u00e9 \u2014 na\u00efve")
        rf.git(self.root, "config", "i18n.logOutputEncoding", "UTF-16")    # B5-FIX-4 V-2
        self.assert_package_passes(head)
        self.assertIn("fix: caf\u00e9 \u2014 na\u00efve\n", read(self.root, f"{REVIEW}/1/commits.txt"))

    def test_textconv_driver_does_not_reach_diff_patch(self):
        rf.write(self.root, ".gitattributes", "*.x diff=up\n")
        rf.write(self.root, "data.x", "hi\n")
        head = rf.commit(self.root, "add data.x")
        rf.git(self.root, "config", "diff.up.textconv", "false")    # a converter that fails
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertIn("+hi\n", read(self.root, f"{REVIEW}/1/diff.patch"))

    def test_external_diff_driver_does_not_reach_diff_patch(self):
        script = os.path.join(self.root, "..", os.path.basename(self.root) + "-extdiff.sh")
        rf.write(os.path.dirname(script), os.path.basename(script), "#!/bin/sh\necho EXTERNAL $1\n")
        os.chmod(script, 0o755)
        self.addCleanup(os.remove, script)
        rf.git(self.root, "config", "diff.external", script)
        self.assertIn("EXTERNAL", rf.git(self.root, "diff", f"{self.base}..{self.head}"))    # the config does replace the diff
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/"))

    def test_src_dst_prefix_config_gives_a_diff_the_gate_accepts(self):
        rf.git(self.root, "config", "diff.srcPrefix", "SRC/")
        rf.git(self.root, "config", "diff.dstPrefix", "DST/")
        rc, out = self.make("fx", "1", self.base, self.head)
        self.assertEqual(0, rc, out)
        for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
            rf.write(self.root, f"{REVIEW}/1/{name}", text)
        rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.diff", out)
        self.assertEqual("diff --git a/src/fx.py b/src/fx.py", read(self.root, f"{REVIEW}/1/diff.patch").splitlines()[0])

    def test_assume_unchanged_tracked_file_is_not_clean(self):
        self.hide_a_fix("--assume-unchanged")
        rc, out = self.make("fx", "1", self.base, self.broken, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("hidden from git status", out)
        self.assertIn("findings.yml", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_skip_worktree_tracked_file_is_not_clean(self):
        self.hide_a_fix("--skip-worktree")
        rc, out = self.make("fx", "1", self.base, self.broken, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("hidden from git status", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def hide_a_fix(self, flag):
        """HEAD holds a broken register; the good one is back in the worktree, hidden from git status."""
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        self.broken = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "update-index", flag, "docs/superpowers/state/findings.yml")
        rf.write(self.root, "docs/superpowers/state/findings.yml",
                 rf.git(self.root, "show", f"{self.base}:docs/superpowers/state/findings.yml") + "\n")
        self.assertEqual("", rf.git(self.root, "status", "--porcelain"))    # git status does not see it

    def test_diff_has_ten_lines_of_context(self):
        rf.write(self.root, "ctx.txt", "".join(f"line{n:02d}\n" for n in range(1, 31)))
        mid = rf.commit(self.root, "add ctx.txt")
        rf.write(self.root, "ctx.txt", "".join(f"line{n:02d}\n" if n != 15 else "changed\n" for n in range(1, 31)))
        head = rf.commit(self.root, "change line 15")
        rc, out = self.make("fx", "1", mid, head, "--only", "register")
        self.assertEqual(0, rc, out)
        diff = read(self.root, f"{REVIEW}/1/diff.patch")
        self.assertIn(" line05", diff.splitlines())
        self.assertNotIn(" line04", diff.splitlines())

    def test_show_signature_config_does_not_reach_commits_txt(self):
        if shutil.which("ssh-keygen") is None:
            self.skipTest("ssh-keygen is needed to sign a fixture commit")
        keydir = tempfile.mkdtemp(prefix="mrp-key-")
        self.addCleanup(shutil.rmtree, keydir, True)
        key = os.path.join(keydir, "key")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key], check=True)
        rf.git(self.root, "config", "gpg.format", "ssh")
        rf.git(self.root, "config", "user.signingkey", key + ".pub")
        rf.write(self.root, "src/signed.py", "signed = True\n")
        rf.git(self.root, "add", "-A")
        rf.git(self.root, "commit", "-q", "-S", "-m", "feat: signed")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "config", "log.showSignature", "true")
        self.assertGreater(len(rf.git(self.root, "log", "--oneline", "-1").splitlines()), 1)    # the config does add lines
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertEqual(2, len(commits.splitlines()), commits)

    def test_annotated_tag_head_is_accepted(self):
        rf.git(self.root, "tag", "-a", "-m", "task tip", "task-tip", self.head)
        rc, out = self.make("fx", "1", self.base, "task-tip", "--only", "register")
        self.assertEqual(0, rc, out)

    def test_unknown_revisions_name_the_argument(self):
        for label, argv in (("base", ("nosuchrev", self.head)), ("head", (self.base, "nosuchrev"))):
            with self.subTest(label=label):
                rc, out = self.make("fx", "1", *argv, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn(f"unknown revision for {label}: nosuchrev", out)

    def test_not_a_git_repository_fails(self):
        plain = tempfile.mkdtemp(prefix="mrp-plain-")
        self.addCleanup(shutil.rmtree, plain, True)
        os.makedirs(os.path.join(plain, "docs/superpowers/state/fx"))
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": plain}), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = mrp.main(["fx", "1", "a", "b"])
        self.assertEqual(1, rc, err.getvalue())
        self.assertIn("not a git repository", err.getvalue())

    def test_package_path_that_is_a_file_fails(self):
        rf.write(self.root, f"{REVIEW}/1", "in the way\n")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("cannot write", out)

    def test_gates_txt_that_is_a_directory_fails(self):
        os.makedirs(os.path.join(self.root, REVIEW, "1", "gates.txt"))
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("cannot write gates.txt", out)

    def test_only_drops_empty_entries(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register, ,")
        self.assertEqual(0, rc, out)
        gates = read(self.root, f"{REVIEW}/1/gates.txt")
        self.assertTrue(gates and all(l.startswith("PASS register.") for l in gates.splitlines()), gates)

    def test_only_with_no_gate_name_fails_like_run_all(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL run_all.empty", read(self.root, f"{REVIEW}/1/gates.txt"))

    def test_package_passes_gate_review_package(self):
        rc, out = self.make("fx", "1", self.base, self.head)
        self.assertEqual(0, rc, out)
        self.assertRegex(read(self.root, f"{REVIEW}/1/gates.txt"), r"(?m)^PASS register\.")
        for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
            rf.write(self.root, f"{REVIEW}/1/{name}", text)
        rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
        self.assertEqual(0, rc, out)
        lines = [l for l in out.splitlines() if l.strip()]
        self.assertTrue(lines and all(l.startswith("PASS ") for l in lines), out)
        self.assertTrue(any(re.match(r"PASS review_package\.gates", l) for l in lines), out)


if __name__ == "__main__":
    unittest.main()
