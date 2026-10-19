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

from tools.gates import gate_review_package as grp
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

    def test_color_config_does_not_reach_the_package(self):
        rf.git(self.root, "config", "color.ui", "always")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertNotIn("\x1b", read(self.root, f"{REVIEW}/1/commits.txt"))
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/"))

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
