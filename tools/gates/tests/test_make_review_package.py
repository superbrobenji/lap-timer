"""make_review_package.main() on a fixture repo (strict-delivery S2 Task 5).
Run from the repo root: python3 -m unittest tools.gates.tests.test_make_review_package -v
"""
import contextlib
import io
import os
import re
import shutil
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
