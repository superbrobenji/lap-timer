"""gate_plan through main() (strict-delivery S2 Task 6).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_plan -v
"""
import contextlib
import io
import os
import shutil
import unittest
from unittest import mock

from tools.gates import gate_plan
from tools.gates.tests import repo_fixture as rf

with open(os.path.join(rf.FIX, "plan", "good.md"), encoding="utf-8") as _f:
    GOOD = _f.read()
FENCE = "`" * 3
PH = "T" + "BD"   # a writing-plans placeholder, assembled so this file never contains one
LEDGER = "plan: docs/superpowers/plans/fx.md\n# Ledger: fx\n- T1 DONE\n"


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
        rc = gate_plan.main(argv)
    return rc, buf.getvalue()


def plan_repo(ledger=None):
    files = dict(rf.DISPATCH_FILES)
    files["docs/superpowers/plans/fx.md"] = GOOD
    if ledger is not None:
        files["docs/superpowers/state/fx/ledger.md"] = ledger
    return rf.make_repo(files, rf.load_rows())


class GatePlan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = plan_repo()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def check(self, text, *extra):
        path = os.path.join(self.root, "plan.under-test.md")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return gate(self.root, ["--plan", path, *extra])

    def variant(self, old, new, base=GOOD):
        text = base.replace(old, new, 1)
        self.assertNotEqual(base, text, f"fixture lacks {old!r}")
        return text

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)

    def test_good_plan_passes_with_register_source(self):
        rc, out = self.check(GOOD, "--source", "register:plan=fx")
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.overlap.1-2", out)
        self.assertNotIn("plan.overlap.2-3", out)

    def test_report_source_counts_ids_in_the_text(self):
        rf.write(self.root, "docs/report.md", "Findings SD-FX-1 and SD-FX-2 and UTF-8.\n")
        rc, out = self.check(GOOD, "--source", "docs/report.md")
        self.assertEqual(0, rc, out)
        self.assertNotIn("UTF-8", out)

    def test_missing_header_fails(self):
        self.assertFailLine(*self.check(self.variant("**Tech Stack:** Python 3.9.\n", "")), "plan.header.tech-stack")

    def test_spec_path_must_exist(self):
        self.assertFailLine(*self.check(self.variant("`docs/cited.md`", "`docs/none.md`")), "plan.header.spec-path")

    def test_task_without_findings_line_fails(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row\n", "")
        self.assertFailLine(*self.check(text), "plan.task.3.findings")

    def test_findings_line_with_a_non_id_fails(self):
        self.assertFailLine(*self.check(self.variant("**Findings:** SD-FX-2", "**Findings:** see audit")),
                            "plan.task.2.findings")

    def test_task_without_files_fails(self):
        self.assertFailLine(*self.check(self.variant("- Modify: `src/shared.py`\n", "")), "plan.task.3.files")

    def test_parallel_tasks_sharing_a_file_fail(self):
        text = self.variant("- Create: `src/beta.py`", "- Create: `src/alpha.py`")
        self.assertFailLine(*self.check(text), "plan.overlap.1-2")

    def test_line_ranges_do_not_hide_an_overlap(self):
        text = self.variant("- Test: `tests/test_alpha.py`\n",
                            "- Test: `tests/test_alpha.py`\n- Modify: `src/shared.py:1-5`\n")
        rc, out = self.check(text)
        self.assertFailLine(rc, out, "plan.overlap.1-2")
        self.assertIn("src/shared.py", out)

    def test_block_over_the_cap_fails(self):
        extra = "".join(f"### Task {n}: extra\n\n**Files:**\n- Create: `src/x{n}.py`\n\n"
                        f"**Findings:** none — fixture filler\n\n" for n in (4, 5, 6))
        text = self.variant("- [ ] **Step 1: CHECKPOINT b2**", extra + "- [ ] **Step 1: CHECKPOINT b2**")
        self.assertFailLine(*self.check(text), "plan.block.2")

    def test_checkpoint_markers_split_blocks_without_headings(self):
        rc, out = self.check(self.variant("## Block b2 — second\n", ""))
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.block.2", out)

    def test_placeholder_fails(self):
        rc, out = self.check(GOOD + f"\nStep detail: {PH}\n")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL plan.placeholder.L", out)

    def test_placeholder_inside_code_fence_fails(self):
        rc, out = self.check(GOOD + f"\n{FENCE}python\nx = 1  # {PH}\n{FENCE}\n")
        self.assertIn("FAIL plan.placeholder.L", out)

    def test_task_heading_inside_code_fence_is_ignored(self):
        rc, out = self.check(GOOD + f"\n{FENCE}\n### Task 9: not a task\n{FENCE}\n")
        self.assertEqual(0, rc, out)
        self.assertNotIn("plan.task.9", out)

    def test_id_in_two_tasks_fails(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        self.assertFailLine(*self.check(text), "plan.id.SD-FX-1")

    def test_unregistered_id_fails(self):
        self.assertFailLine(*self.check(self.variant("**Findings:** SD-FX-2", "**Findings:** SD-FX-404")),
                            "plan.id.SD-FX-404")

    def test_source_id_missing_from_plan_fails(self):
        text = self.variant("**Findings:** SD-FX-2", "**Findings:** none — moved out of the plan")
        self.assertFailLine(*self.check(text, "--source", "register:plan=fx"), "plan.coverage.SD-FX-2")

    def test_source_with_no_ids_fails(self):
        self.assertFailLine(*self.check(GOOD, "--source", "register:plan=nothing"), "plan.source")

    def test_bad_register_query_fails(self):
        self.assertFailLine(*self.check(GOOD, "--source", "register:owner=fx"), "plan.input")

    def test_no_mode_fails(self):
        self.assertFailLine(*gate(self.root, []), "plan.input")


class InFlight(unittest.TestCase):
    def repo(self, ledger):
        root = plan_repo(ledger)
        self.addCleanup(shutil.rmtree, root, True)
        return root

    def test_in_flight_plan_is_checked(self):
        root = self.repo(LEDGER)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.fx.header.goal", out)

    def test_closed_ledger_is_not_in_flight(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n")
        self.assertEqual([], gate_plan.plans_in_flight(root))
        rc, out = gate(root, ["--in-flight"])
        self.assertIn("PASS plan.in_flight", out)

    def test_uncommitted_checkpoint_line_is_in_flight(self):
        root = self.repo(LEDGER)
        with open(os.path.join(root, "docs/superpowers/state/fx/ledger.md"), "a", encoding="utf-8") as f:
            f.write("CHECKPOINT b1 abc1234\n")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))
        rf.commit(root, "chore(state): checkpoint b1")
        self.assertEqual([], gate_plan.plans_in_flight(root))

    def test_ledger_without_plan_line_fails(self):
        root = self.repo("# Ledger: fx\n")
        self.assertIn("FAIL plan.input", gate(root, ["--in-flight"])[1])

    def test_no_state_dirs_pass(self):
        root = plan_repo()
        self.addCleanup(shutil.rmtree, root, True)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.in_flight", out)

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("plan", run_all.GATES)
        root = self.repo(LEDGER)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "plan"])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
