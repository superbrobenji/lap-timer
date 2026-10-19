"""gate_plan through main() (strict-delivery S2 Task 6).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_plan -v
"""
import contextlib
import copy
import io
import os
import shutil
import subprocess
import tempfile
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


def words(*parts):
    """A phrase assembled from pieces, so this file never contains a writing-plans placeholder itself."""
    return "".join(parts)


PLACEHOLDER_TOKENS = (words("T", "BD"), words("TO", "DO"))
PHRASES = [words("T", "BD"), words("TO", "DO"), words("implement lat", "er"), words("fill in detai", "ls"),
           words("add appropriate error handl", "ing"), words("add valid", "ation"), words("handle edge cas", "es"),
           words("write tests for the abo", "ve"), words("similar to tas", "k 3")]


class Branches(unittest.TestCase):
    """One production-path test per load-bearing branch of gate_plan (review R6-6)."""

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

    def fail(self, text, line_id, message="", *extra):
        rc, out = self.check(text, *extra)
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)
        self.assertIn(message, out)
        return out

    def ok(self, text, *extra):
        rc, out = self.check(text, *extra)
        self.assertEqual(0, rc, out)
        return out

    # -- PARALLEL overlap and the Mode line
    def test_parallel_tasks_in_different_blocks_may_share_a_file(self):
        out = self.ok(self.variant("### Task 3: gamma\n\n", "### Task 3: gamma\n\n**Mode:** PARALLEL\n\n"))
        self.assertNotIn("overlap.2-3", out)
        self.assertNotIn("overlap.1-3", out)

    def test_parallel_and_serial_pair_may_share_a_file(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                            "**Mode:** SERIAL\n\n**Files:**\n- Create: `src/alpha.py`")
        out = self.ok(text)
        self.assertIn("PASS plan.task.2.mode", out)
        self.assertIn("PASS plan.overlap\n", out + "\n")

    def test_mode_with_a_suffix_fails(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                            "**Mode:** PARALLEL (with Task 1)\n\n**Files:**\n- Create: `src/alpha.py`")
        self.fail(text, "plan.task.2.mode", "exactly PARALLEL or SERIAL")

    def test_lowercase_mode_fails(self):
        self.fail(self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                               "**Mode:** parallel\n\n**Files:**\n- Create: `src/beta.py`"),
                  "plan.task.2.mode", "'parallel'")

    def test_overlap_names_the_shared_file(self):
        out = self.fail(self.variant("- Create: `src/beta.py`", "- Create: `src/alpha.py`"), "plan.overlap.1-2",
                        "PARALLEL tasks share src/alpha.py")
        self.assertNotIn("tests/test_alpha.py", out)

    def test_unnormalised_paths_do_not_hide_an_overlap(self):
        for spelled in ("./src/alpha.py", "src/../src/alpha.py", "src/alpha.py ", "src//alpha.py", "src/alpha.py:3-4"):
            with self.subTest(spelled):
                text = self.variant("- Create: `src/beta.py`", f"- Create: `{spelled}`")
                self.fail(text, "plan.overlap.1-2", "src/alpha.py")

    # -- the Files list
    def test_prose_line_inside_files_list_fails_and_is_quoted(self):
        text = self.variant("- Create: `src/beta.py`\n", "- Create: `src/beta.py`\n- Not modified: `x.yml` (a note)\n")
        self.fail(text, "plan.task.2.files", "- Not modified: `x.yml` (a note)")

    def test_blank_line_inside_files_list_keeps_collecting(self):
        text = self.variant("- Create: `src/beta.py`\n", "- Create: `src/beta.py`\n\n- Modify: `src/alpha.py`\n")
        self.fail(text, "plan.overlap.1-2", "src/alpha.py")

    def test_blank_line_before_the_first_files_item_is_skipped(self):
        out = self.ok(self.variant("**Files:**\n- Modify: `src/shared.py`\n", "**Files:**\n\n- Modify: `src/shared.py`\n"))
        self.assertIn("PASS plan.task.3.files", out)

    def test_files_list_ends_at_a_fence_a_step_or_a_field(self):
        for tail in (f"{FENCE}\nx\n{FENCE}\nprose line\n", "- [ ] **Step 1: go**\nprose line\n", "**Mode:** SERIAL\nprose\n",
                     "## Notes\nprose line\n"):
            with self.subTest(tail):
                text = self.variant("**Files:**\n- Modify: `src/shared.py`\n", f"**Files:**\n- Modify: `src/shared.py`\n{tail}")
                self.ok(text)

    def test_stray_files_after_a_block_heading_do_not_join_the_previous_task(self):
        text = self.variant("## Block b2 — second\n", "## Block b2 — second\n\n**Files:**\n- Create: `src/alpha.py`\n")
        self.ok(text)

    # -- placeholders
    def test_every_placeholder_phrase_fails(self):
        for phrase in PHRASES:
            # the two tokens are exact-case; the phrases are case-insensitive
            spellings = [phrase] if phrase in PLACEHOLDER_TOKENS else [phrase, phrase.upper(), phrase.title()]
            for spelled in spellings:
                with self.subTest(spelled):
                    rc, out = self.check(GOOD + f"\nStep detail: {spelled}\n")
                    self.assertEqual(1, rc, out)
                    self.assertIn("FAIL plan.placeholder.L", out)

    def test_placeholder_line_number_and_text_are_reported(self):
        rc, out = self.check(GOOD + f"\nStep detail: {PHRASES[0]}\n")
        self.assertRegex(out, r"FAIL plan\.placeholder\.L\d+: Step detail: ")

    # -- Findings forms
    def test_backtick_ids_are_accepted(self):
        out = self.ok(self.variant("**Findings:** SD-FX-2", "**Findings:** `SD-FX-2`"), "--source", "register:plan=fx")
        self.assertIn("PASS plan.coverage.SD-FX-2", out)

    def test_none_forms(self):
        for form in ("none - ascii hyphen", "none -- double hyphen", "none — dash"):
            with self.subTest(form):
                out = self.ok(self.variant("none — fixture maintenance with no register row", form))
                self.assertIn("PASS plan.task.3.findings", out)
        self.fail(self.variant("none — fixture maintenance with no register row", "none"), "plan.task.3.findings",
                  "not register ids")

    def test_non_id_in_a_list_is_named(self):
        out = self.fail(self.variant("**Findings:** SD-FX-2", "**Findings:** SD-FX-2, see audit"),
                        "plan.task.2.findings", "'see audit'")
        self.assertNotIn("plan.id.SD-FX-2", out)    # nothing is routed from a bad line
        self.assertNotIn("plan.id.see", out)

    def test_source_id_in_two_tasks_fails_coverage_with_both_numbers(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        rc, out = self.check(text, "--source", "register:plan=fx")
        self.assertIn("FAIL plan.coverage.SD-FX-1: in tasks 1, 3", out)

    # -- the register query
    def test_query_grammar_errors_are_messages_not_crashes(self):
        for q, msg in (("plan=fx&&task=1", "bad register query part ''"), ("owner=fx", "bad register query part 'owner=fx'"),
                       ("plan", "bad register query part 'plan'"), ("plan=", "bad register query part 'plan='"),
                       ("plan=fx&task=1&plan=fy", "duplicate register query key 'plan'"),
                       ("task=1,", "empty pattern in register query part 'task=1,'"),
                       ("task=,1", "empty pattern")):
            with self.subTest(q):
                self.fail(GOOD, "plan.input", msg, "--source", f"register:{q}")

    def test_query_keys_are_anded_and_patterns_are_ored(self):
        rc, out = self.check(GOOD, "--source", "register:plan=fx&task=2")
        self.assertIn("PASS plan.coverage.SD-FX-2", out)
        self.assertNotIn("coverage.SD-FX-1", out)
        rc, out = self.check(GOOD, "--source", "register:plan=fx,fz&task=1,2")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.coverage.SD-FX-2", out)
        self.fail(GOOD, "plan.source", "yields no register ids", "--source", "register:plan=fx&task=9")

    def test_query_state_and_severity_keys(self):
        rc, out = self.check(GOOD, "--source", "register:state=planned&severity=LOW")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertNotIn("coverage.SD-FX-2", out)

    # -- report sources
    def test_missing_source_file_fails_with_its_name(self):
        self.fail(GOOD, "plan.input", "--source docs/none.md: no such file", "--source", "docs/none.md")

    def test_report_source_matches_rows_by_their_source_field(self):
        rc, out = self.check(GOOD, "--source", "docs/cited.md")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.coverage.SD-FX-2", out)

    def test_report_source_path_forms_and_row_source_suffixes(self):
        rows = rf.load_rows()
        extra = []
        for rid, src, task in (("SD-FX-3", "docs/cited.md §2", "3"), ("SD-FX-4", "docs/cited.md#sec", "4"),
                               ("SD-FX-5", "docs/cited.md:7", "5"), ("SD-FX-6", "docs/cited.mdx", "6"),
                               ("SD-FX-8", "./docs/cited.md", "8"), ("SD-FX-9", "docs/./cited.md#sec", "9"),
                               ("SD-FX-7", "xdocs/cited.md", "7")):
            row = copy.deepcopy(rows["findings"][0])
            row.update(id=rid, source=src)
            row["owner"] = {"kind": "plan", "plan": "other", "task": task}
            extra.append(row)
        rows["findings"] = rows["findings"] + extra
        files = dict(rf.DISPATCH_FILES)
        files["docs/superpowers/plans/fx.md"] = GOOD
        root = rf.make_repo(files, rows)
        self.addCleanup(shutil.rmtree, root, True)
        plan = os.path.join(root, "plan.under-test.md")
        with open(plan, "w", encoding="utf-8") as f:
            f.write(GOOD)
        for spelled in ("docs/cited.md", "./docs/cited.md", os.path.join(root, "docs", "cited.md")):
            with self.subTest(spelled):
                rc, out = gate(root, ["--plan", plan, "--source", spelled])
                for rid in ("SD-FX-3", "SD-FX-4", "SD-FX-5", "SD-FX-8", "SD-FX-9"):
                    self.assertIn(f"FAIL plan.coverage.{rid}: in no task", out)
                for rid in ("SD-FX-6", "SD-FX-7"):
                    self.assertNotIn(rid, out)

    def test_source_given_through_a_symlinked_root(self):
        link = tempfile.mkdtemp(prefix="gate-link-")
        self.addCleanup(shutil.rmtree, link, True)
        os.symlink(self.root, os.path.join(link, "tree"))
        rc, out = self.check(GOOD, "--source", os.path.join(link, "tree", "docs", "cited.md"))
        self.assertIn("PASS plan.coverage.SD-FX-1", out)

    # -- code fences (CommonMark)
    def test_fences_hide_task_headings(self):
        inner = "### Task 9: not a task\n"
        for name, text in (
                ("tilde", f"~~~\n{inner}~~~\n"),
                ("four backticks around three", f"{FENCE}`\n{FENCE}python\n{inner}{FENCE}\n{inner}{FENCE}`\n"),
                ("indented", f"paragraph\n\n  {FENCE}\n{inner}  {FENCE}\n"),
                ("info string does not close", f"{FENCE}\n{FENCE}python\n{inner}{FENCE}\n"),
                ("other character does not close", f"{FENCE}\n~~~\n{inner}{FENCE}\n"),
                ("shorter run does not close", f"{FENCE}`\n{FENCE}\n{inner}{FENCE}`\n"),
                ("longer tilde run closes", f"~~~\n{inner}~~~~\n")):
            with self.subTest(name):
                out = self.ok(GOOD + "\n" + text)
                self.assertNotIn("plan.task.9", out)

    def test_text_after_a_closed_fence_is_parsed_again(self):
        for name, text in (("info string closes nothing", f"{FENCE}\nx\n{FENCE}python\n### Task 9: swallowed\n"),
                           ("indent of four is no fence", f"text\n\n    {FENCE}\n### Task 9: real\n"),
                           ("inline code is no fence", f"{FENCE}inline{FENCE}\n### Task 9: real\n"),
                           ("tilde closed by backticks", f"~~~\nx\n{FENCE}\n### Task 9: swallowed\n")):
            with self.subTest(name):
                rc, out = self.check(GOOD + "\n" + text)
                self.assertEqual("real" in text, "plan.task.9.files" in out, out)

    def test_list_item_fences(self):
        item = "- an item\n"
        rc, out = self.check(GOOD + f"\n{item}  {FENCE}\n### Task 9: left of the item ends it\n  {FENCE}\n")
        self.assertIn("FAIL plan.task.9.files", out)
        rc, out = self.check(GOOD + f"\ntext\n\n  {FENCE}\n### Task 9: inside a plain fence\n  {FENCE}\n")
        self.assertNotIn("plan.task.9", out)
        rc, out = self.check(GOOD + f"\n{item}    {FENCE}\n    code\n    {FENCE}\n### Task 9: after\n")
        self.assertIn("FAIL plan.task.9.files", out)

    def test_every_backticked_path_of_a_files_item_counts(self):
        text = self.variant("- Create: `src/beta.py`", "- Create: `src/beta.py`, `src/alpha.py`")
        self.fail(text, "plan.overlap.1-2", "src/alpha.py")
        text = self.variant("- Create: `src/beta.py`", "- Create: `src/beta.py` (`src/alpha.py` is only a comment)")
        self.ok(text)

    def test_a_second_mode_line_fails(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/alpha.py`",
                            "**Mode:** PARALLEL\n**Mode:** SERIAL\n\n**Files:**\n- Create: `src/alpha.py`")
        self.fail(text, "plan.task.1.mode", "a second **Mode:** line")

    def test_only_level_two_headings_are_blocks(self):
        for level in ("###", "####", "#"):
            with self.subTest(level):
                out = self.ok(self.variant("### Task 2: beta", f"{level} Block b9 notes\n\n### Task 2: beta"))
                self.assertIn("PASS plan.block.order", out)

    def test_an_unclosed_fence_fails_and_names_its_line(self):
        text = self.variant("## Block b2 — second", f"{FENCE}\n## Block b2 — second").replace(
            "**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        line = text.split("\n").index(FENCE) + 1
        self.fail(text, "plan.fence", f"line {line}: code fence never closed")
        self.assertIn("PASS plan.fence", self.ok(GOOD))

    def test_first_matching_header_line_wins(self):
        out = self.ok(GOOD + "\n**Spec:** `docs/none.md`\n")
        self.assertIn("PASS plan.header.spec-path", out)

    # -- block order and plan-level failures
    def test_tasks_before_the_first_block_heading_fail(self):
        self.fail(self.variant("## Block b1 — first\n", ""), "plan.block.order", "before the first 'Block' heading")

    def test_block_numbers_must_increase(self):
        self.fail(self.variant("## Block b2 — second", "## Block b1 — second"), "plan.block.order",
                  "'Block b1' does not follow block 1")
        self.fail(self.variant("## Block b2 — second", "## Block b2 — second\n\n## Block b2 — again"), "plan.block.order",
                  "'Block b2' does not follow block 2")

    def test_a_heading_cannot_reuse_a_checkpoint_split_block(self):
        text = self.variant("## Block b2 — second\n", "") + "\n## Block b2 — again\n"
        self.fail(text, "plan.block.order", "'Block b2' does not follow block 2")

    def test_missing_plan_file_fails(self):
        rc, out = gate(self.root, ["--plan", "docs/none-plan.md"])
        self.assertIn("FAIL plan.file: docs/none-plan.md: no such plan file", out)

    def test_plan_without_tasks_fails(self):
        self.fail(GOOD.split("---")[0], "plan.tasks", "no '### Task N' headings")

    def test_plan_and_in_flight_together_fail(self):
        self.fail(GOOD, "plan.input", "exactly one of", "--in-flight")

    def test_source_with_in_flight_fails(self):
        rc, out = gate(self.root, ["--in-flight", "--source", "docs/cited.md"])
        self.assertIn("FAIL plan.input: --source goes with --plan", out)


class Ledgers(unittest.TestCase):
    def repo(self, ledger):
        root = plan_repo(ledger)
        self.addCleanup(shutil.rmtree, root, True)
        return root

    def append(self, root, text):
        with open(os.path.join(root, "docs/superpowers/state/fx/ledger.md"), "a", encoding="utf-8") as f:
            f.write(text)

    def test_line_one_rule_names_the_ledger(self):
        for first in ("# Ledger: fx", "", "plan:docs/superpowers/plans/fx.md", "plan:  x y"):
            with self.subTest(first):
                root = self.repo(first + "\n- T1 DONE\n")
                rc, out = gate(root, ["--in-flight"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL plan.input: docs/superpowers/state/fx/ledger.md: line 1 must be 'plan:", out)

    def test_head_closed_but_working_tree_appended_is_in_flight(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n")
        self.assertEqual([], gate_plan.plans_in_flight(root))
        self.append(root, "- T2 DONE\n")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))

    def test_working_tree_closed_but_head_open_is_in_flight(self):
        root = self.repo(LEDGER)
        self.append(root, "CHECKPOINT b1 abc1234\n")
        self.assertEqual(1, len(gate_plan.plans_in_flight(root)))

    def test_only_the_last_line_closes_a_ledger(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n- T2 DONE\n")
        self.assertEqual(1, len(gate_plan.plans_in_flight(root)))
        rc, out = gate(root, ["--in-flight"])
        self.assertIn("PASS plan.fx.header.goal", out)

    def test_trailing_blank_lines_do_not_hide_the_checkpoint(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n\n  \n")
        self.assertEqual([], gate_plan.plans_in_flight(root))

    def test_a_repository_without_a_commit_is_in_flight(self):
        root = tempfile.mkdtemp(prefix="gate-test-")
        self.addCleanup(shutil.rmtree, root, True)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        rf.write_registers(root, rf.load_rows(), "0" * 40)
        rf.write(root, "docs/superpowers/plans/fx.md", GOOD)
        rf.write(root, "docs/cited.md", "cited\n")
        rf.write(root, "docs/superpowers/state/fx/ledger.md", LEDGER + "CHECKPOINT b1 abc1234\n")
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.fx.header.goal", out)

    def test_two_ledgers_are_each_judged(self):
        root = self.repo(LEDGER)
        rf.write(root, "docs/superpowers/state/gx/ledger.md", LEDGER + "CHECKPOINT b1 abc1234\n")
        rf.commit(root, "second ledger")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))

    def test_missing_state_directory_fails(self):
        root = rf.make_repo(dict(rf.DISPATCH_FILES), rf.load_rows())
        self.addCleanup(shutil.rmtree, root, True)
        shutil.rmtree(os.path.join(root, "docs/superpowers/state"), ignore_errors=True)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL plan.input", out)


if __name__ == "__main__":
    unittest.main()
