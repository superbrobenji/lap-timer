"""gate_review_package through main() (strict-delivery S2 Task 4).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_review_package -v
"""
import contextlib
import io
import os
import shutil
import unittest
from unittest import mock

from tools.gates import gate_review_package as grp
from tools.gates.tests import repo_fixture as rf

PKG = ".superpowers/sdd/fx/review/1"
MUT = ("finding: SD-FX-1\n"
       "test: test_fx\n"
       "step 1 (fix present): python3 -m unittest t.T.test_fx -> PASS\n"
       "step 2 (fix reverted: git stash push src/fx.py): python3 -m unittest t.T.test_fx -> FAIL\n"
       "step 3 (fix restored): python3 -m unittest t.T.test_fx -> PASS\n")
GOOD = {
    "commits.txt": "abc1234 feat: fixture\n",
    "diff.patch": "diff --git a/src/fx.py b/src/fx.py\n+fixed\n",
    "gates.txt": "PASS register.schema\n",
    "siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-a | test_sib_a |\n",
    "claims.md": ("| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
                  "| fx fixed | python3 -m unittest t | git stash; python3 -m unittest t | OK / FAILED | PASS |\n"),
    "mutation-SD-FX-1.txt": MUT,
}


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
        rc = grp.main(argv)
    return rc, buf.getvalue()


class GateReviewPackage(unittest.TestCase):
    def setUp(self):
        self.root = rf.make_repo(rf.DISPATCH_FILES, rf.load_rows())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.put(GOOD)

    def put(self, files):
        for name, text in files.items():
            rf.write(self.root, f"{PKG}/{name}", text)

    def check(self):
        return gate(self.root, ["--dir", PKG])

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)

    def test_good_package_passes(self):
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)

    def test_each_missing_file_fails(self):
        expect = {n: f"review_package.file.{n}" for n in grp.REQUIRED}
        expect["mutation-SD-FX-1.txt"] = "review_package.mutation.SD-FX-1"
        for name, line_id in expect.items():
            with self.subTest(name=name):
                os.remove(os.path.join(self.root, PKG, name))
                rc, out = self.check()
                self.assertFailLine(rc, out, line_id)
                self.put({name: GOOD[name]})

    def test_mutation_step_two_must_fail(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("-> FAIL", "-> PASS")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_steps_must_run_one_command(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("restored): python3 -m unittest t.T.test_fx",
                                                      "restored): python3 -m unittest t.T.test_other")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_command_must_name_the_test(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test: test_elsewhere")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_for_another_finding_fails(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("finding: SD-FX-1", "finding: SD-FX-2")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_fewer_sibling_rows_than_register_fails(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n"})
        self.assertFailLine(*self.check(), "review_package.siblings")

    def test_sibling_named_in_register_must_appear(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-z | test_z |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings")

    def test_claims_fail_row_fails(self):
        self.put({"claims.md": GOOD["claims.md"].replace("| PASS |", "| FAIL |")})
        self.assertFailLine(*self.check(), "review_package.claims")

    def test_claims_without_rows_fails(self):
        self.put({"claims.md": "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"})
        self.assertFailLine(*self.check(), "review_package.claims")

    def test_red_gates_output_fails(self):
        self.put({"gates.txt": "FAIL register.schema: broken\n"})
        self.assertFailLine(*self.check(), "review_package.gates")

    def test_diff_must_be_a_git_diff(self):
        self.put({"diff.patch": "not a diff\n"})
        self.assertFailLine(*self.check(), "review_package.diff")

    def test_unregistered_finding_fails(self):
        self.put({"mutation-SD-FX-404.txt": MUT.replace("SD-FX-1", "SD-FX-404")})
        self.assertFailLine(*self.check(), "review_package.finding.SD-FX-404")

    def test_task_with_no_owned_rows_fails(self):
        other = ".superpowers/sdd/fx/review/9"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.findings")

    def test_all_mode_checks_every_package(self):
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.fx.1.claims", out)
        os.remove(os.path.join(self.root, PKG, "mutation-SD-FX-1.txt"))
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.fx.1.mutation")

    def test_all_mode_with_no_packages_passes(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.all", out)

    def test_no_mode_fails(self):
        self.assertFailLine(*gate(self.root, []), "review_package.input")

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("review_package", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "review_package"])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
