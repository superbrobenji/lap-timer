"""gate_review_package through main() (strict-delivery S2 Task 4).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_review_package -v
"""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
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
    "diff.patch": "diff --git a/src/fx.py b/src/fx.py\n--- a/src/fx.py\n+++ b/src/fx.py\n@@ -1 +1 @@\n+fixed\n",
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

    def assertFailLine(self, rc, out, line_id, reason=""):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)
        line = next(l for l in out.splitlines() if l.startswith(f"FAIL {line_id}"))
        self.assertIn(reason, line)

    def with_rows(self, edit):
        rows = rf.load_rows()
        edit(rows)
        self.root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, self.root, True)
        self.put(GOOD)

    def test_good_package_passes(self):
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)
        for lid in ("file.commits.txt", "file.diff.patch", "file.gates.txt", "file.siblings.md", "file.claims.md",
                    "commits", "diff", "gates", "mutation.SD-FX-1", "siblings", "claims"):
            self.assertIn(f"PASS review_package.{lid}\n", out + "\n")

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

    # --- fix round 1 (F1-F9 of task-4-review1.md); every test goes through main() ---
    def test_each_empty_file_fails(self):
        for name in grp.REQUIRED:
            with self.subTest(name=name):
                self.put({name: ""})
                self.assertFailLine(*self.check(), f"review_package.file.{name}", "missing or empty")
                self.put({name: GOOD[name]})

    def test_gates_txt_must_hold_gate_output(self):
        for text in (" \n", "hello\n", "Traceback (most recent call last):\nerror\n", "fail register.schema\n",
                     "PASS a\n  FAIL b: x\n", "PASS\n"):
            with self.subTest(text=text):
                self.put({"gates.txt": text})
                self.assertFailLine(*self.check(), "review_package.gates", "gates.txt needs gate output")

    def test_gates_txt_blank_lines_and_crlf_pass(self):
        self.put({"gates.txt": "PASS a.b\r\n\nPASS c: note\r\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_claims_empty_cells_fail(self):
        head = "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
        for row in ("| x |  | c | o | PASS |\n", "| x | d |  | o | PASS |\n", "|  | d | c | o | PASS |\n",
                    "| x | d | c |  | PASS |\n"):
            with self.subTest(row=row):
                self.put({"claims.md": head + row})
                self.assertFailLine(*self.check(), "review_package.claims", "first four cells filled")

    def test_claims_malformed_rows_fail(self):
        head = "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
        for row in ("| x | d | c | PASS |\n", "| x | d | c | o | pass |\n", "| x | d | c | o | PASS | z |\n"):
            with self.subTest(row=row):
                self.put({"claims.md": head + row})
                self.assertFailLine(*self.check(), "review_package.claims", "not 'claim |")

    def test_claims_and_siblings_must_be_tables(self):
        self.put({"claims.md": "just prose | with a bar\n"})
        self.assertFailLine(*self.check(), "review_package.claims", "not a Markdown table")
        self.put({"claims.md": GOOD["claims.md"], "siblings.md": "| sibling | test or finding |\n| x | y |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "not a Markdown table")

    def test_only_the_first_table_counts(self):
        self.put({"claims.md": GOOD["claims.md"] + "\n| a | b |\n|---|---|\n| x | y |\n",
                  "siblings.md": GOOD["siblings.md"] + "\n| a | b |\n|---|---|\n| x | y |\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_must_be_exactly_five_lines(self):
        self.put({"mutation-SD-FX-1.txt": "\n".join(MUT.splitlines()[:4]) + "\n"})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "exactly five lines")
        self.put({"mutation-SD-FX-1.txt": MUT + "step 4 (again): python3 -m unittest t.T.test_fx -> FAIL\n"})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "exactly five lines")
        self.put({"mutation-SD-FX-1.txt": "\n" + MUT.replace("\n", "\n\n")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_line_is_required(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test: ")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "line 2 must be")

    def test_mutation_step_one_and_three_must_pass(self):
        for n, tail in ((1, "(fix present): python3 -m unittest t.T.test_fx -> PASS"),
                        (3, "(fix restored): python3 -m unittest t.T.test_fx -> PASS")):
            with self.subTest(step=n):
                self.put({"mutation-SD-FX-1.txt": MUT.replace(tail, tail[:-4] + "WARN")})
                self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", f"step {n} malformed")

    def test_mutation_step_two_names_how(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("(fix reverted: git stash push src/fx.py)", "(fix reverted:  )")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "names no way")

    def test_mutation_test_name_is_a_whole_component(self):
        for cmd in ("t.T.test_fx_other", "t.T.xtest_fx", "t.test_fx.T"):
            with self.subTest(cmd=cmd):
                self.put({"mutation-SD-FX-1.txt": MUT.replace("t.T.test_fx", cmd)})
                self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "does not name the test")
        self.put({"mutation-SD-FX-1.txt": MUT.replace("t.T.test_fx", "tests/t.py::T::test_fx")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_must_equal_the_pinned_test(self):
        def pin(rows, test):
            rows["findings"][0]["pin"] = {"test": test, "transcript": "docs/x.txt"}
        self.with_rows(lambda r: pin(r, "test_other::test_elsewhere"))
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "pinned test")
        self.with_rows(lambda r: pin(r, "test_mod::test_fx"))
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.with_rows(lambda r: pin(r, "test_fx"))
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_siblings_match_the_exact_first_cell(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-abc | test_x |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "not listed: sib-a")

    def test_sibling_second_cell_is_a_test_or_a_registered_id(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for cell in ("-", "FAIL", "not a test", "SD-FX-404", ""):
            with self.subTest(cell=cell):
                self.put({"siblings.md": head + f"| sib-a | {cell} |\n"})
                self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")
        for cell in ("SD-FX-2", "t.T.test_sib", "tests/t.py::T::test_sib", "test_sib"):
            with self.subTest(cell=cell):
                self.put({"siblings.md": head + f"| sib-a | {cell} |\n"})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_sibling_rows_need_two_cells_and_a_name(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for row in ("| sib-a |\n", "|  | test_x |\n", "| sib-a | test_x | extra |\n"):
            with self.subTest(row=row):
                self.put({"siblings.md": head + "| sib-a | test_a |\n" + row})
                self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")

    def test_commits_txt_holds_sha_subject_lines(self):
        for text in ("\n", "not a commit\n", "abc1234 ok\nnot a commit\n", "abc123 short\n", "abc1234\n"):
            with self.subTest(text=text):
                self.put({"commits.txt": text})
                self.assertFailLine(*self.check(), "review_package.commits", "<sha> <subject>")
        self.put({"commits.txt": "abc1234 one\n\n" + "0" * 40 + " two\r\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_diff_needs_a_header_and_a_body(self):
        self.put({"diff.patch": "diff --git a/x b/x\n"})
        self.assertFailLine(*self.check(), "review_package.diff", "no hunk or file line")
        self.put({"diff.patch": "diff --git x\n--- a/x\n"})
        self.assertFailLine(*self.check(), "review_package.diff", "does not start with")
        self.put({"diff.patch": "diff --git a/x b/x\nBinary files a/x and b/x differ\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_dir_must_be_the_package_layout(self):
        other = ".superpowers/sdd/fx/notreview/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        for d in (other, "/", "..", ".", ".superpowers/sdd/fx", f"{PKG}/x"):
            with self.subTest(dir=d):
                rc, out = gate(self.root, ["--dir", d])
                self.assertFailLine(rc, out, "review_package.input", "is not <root>/.superpowers/sdd/<plan>/review/<task>")
                self.assertNotIn("index", out)

    def test_dir_must_exist(self):
        self.assertFailLine(*gate(self.root, ["--dir", ".superpowers/sdd/fx/review/77"]),
                            "review_package.input", "no such directory")

    def test_dir_accepts_an_absolute_path(self):
        rc, out = gate(self.root, ["--dir", os.path.join(self.root, PKG)])
        self.assertEqual(0, rc, out)

    def test_dir_and_all_together_or_neither_fail(self):
        self.assertFailLine(*gate(self.root, ["--dir", PKG, "--all"]), "review_package.input", "exactly one")
        self.assertFailLine(*gate(self.root, ["--dir", ""]), "review_package.input", "exactly one")

    def test_missing_register_is_input_fail(self):
        os.remove(os.path.join(self.root, rf.STATE, "findings.yml"))
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.input")
        self.assertFailLine(*self.check(), "review_package.input")

    def test_package_under_another_plan_does_not_own_the_row(self):
        other = ".superpowers/sdd/other/review/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.findings", "plan: other, task: 1")

    def test_all_mode_package_with_no_owner_and_no_transcript_fails(self):
        other = ".superpowers/sdd/fx/review/5"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx.5.mutation", "no register row owns")

    def test_all_mode_non_directory_entry_fails(self):
        rf.write(self.root, ".superpowers/sdd/fx/review/2", "a stray file\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx.2.input", "is not a directory")

    def test_all_mode_lone_stray_file_fails_too(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rf.write(self.root, ".superpowers/sdd/fx/review/2", "a stray file\n")
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.fx.2.input")
        self.assertNotIn("review_package.all", out)

    def test_blanks_indentation_and_prose_around_tables(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("\n", "\n \t\n"),
                  "gates.txt": " \nPASS a.b\n \n",
                  "commits.txt": "  \nabc1234 one\n",
                  "claims.md": "Claims |\n\n  " + GOOD["claims.md"].replace("\n", "\n  ").rstrip(" ") + "after the table |\n",
                  "siblings.md": "Siblings |\n\n" + GOOD["siblings.md"] + "closing prose |\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_value_is_stripped(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test:   test_fx")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_script_runs_as_a_program(self):
        env = dict(os.environ, CLAUDE_PROJECT_DIR=self.root)
        p = subprocess.run([sys.executable, grp.__file__, "--all"], capture_output=True, text=True, env=env, check=False)
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertIn("PASS review_package.fx.1.claims", p.stdout)

    # --- fix round 2 (N1-N5 of task-4-review2.md) ---
    def test_dir_with_a_wrong_prefix_is_not_the_layout(self):
        other = "a/b/fx/review/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        rf.write(self.root, f"{other}/mutation-SD-FX-1.txt", MUT)
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.input", "is not <root>/")

    def test_dir_by_realpath_under_a_symlinked_root_passes(self):
        link = os.path.join(tempfile.mkdtemp(), "link")
        self.addCleanup(shutil.rmtree, os.path.dirname(link), True)
        os.symlink(self.root, link)
        for d in (os.path.join(os.path.realpath(self.root), PKG), os.path.join(link, PKG), PKG):
            with self.subTest(dir=d):
                rc, out = gate(link, ["--dir", d])
                self.assertEqual(0, rc, out)
        rc, out = gate(link, ["--all"])
        self.assertEqual(0, rc, out)

    def test_siblings_rows_at_least_the_registers_list_length(self):
        def dup(rows):
            rows["findings"][0]["siblings"] = ["sib-a", "sib-a"]
        self.with_rows(dup)
        self.assertFailLine(*self.check(), "review_package.siblings", "1 row(s) for a register siblings list of 2")
        row = "| sib-a | test_sib_a |\n"
        self.put({"siblings.md": GOOD["siblings.md"] + row})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_all_mode_sweeps_hidden_entries(self):
        hidden = ".superpowers/sdd/fx/review/.hidden"
        rf.write(self.root, f"{hidden}/x", "stray\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx..hidden.mutation")
        shutil.rmtree(os.path.join(self.root, hidden))
        rf.write(self.root, ".superpowers/sdd/fx/review/.DS_Store", "x\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx..DS_Store.input", "is not a directory")

    def test_all_mode_review_that_is_a_file_fails(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rf.write(self.root, ".superpowers/sdd/zz/review", "a file\n")
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.zz.input", "is not a directory")
        self.assertNotIn("review_package.all", out)

    def test_register_sibling_with_outer_whitespace_or_a_pipe_is_listable(self):
        for sibling, cell in (("sib-a ", "sib-a"), (" sib-a", "sib-a"), ("a|b", "a\\|b")):
            with self.subTest(sibling=sibling):
                self.with_rows(lambda rows: rows["findings"][0].update(siblings=[sibling]))
                self.put({"siblings.md": f"| sibling | test or finding |\n|---|---|\n| {cell} | test_x |\n"})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_unescaped_pipe_splits_a_cell(self):
        self.with_rows(lambda rows: rows["findings"][0].update(siblings=["a|b"]))
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| a|b | test_x |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")

    def test_escaped_pipe_in_a_claims_cell(self):
        self.put({"claims.md": GOOD["claims.md"].replace("python3 -m unittest t |", "python3 -m unittest t \\| tail |")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_table_rows_without_spaces_or_a_trailing_pipe(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for row in ("|sib-a|test_x|\n", "| sib-a | test_x\n", "|sib-a|test_x\n"):
            with self.subTest(row=row):
                self.put({"siblings.md": head + row})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_all_mode_skips_a_plan_without_a_review_dir(self):
        rf.write(self.root, ".superpowers/sdd/noreview/notes.md", "notes\n")
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertNotIn("noreview", out)

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("review_package", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "review_package"])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
