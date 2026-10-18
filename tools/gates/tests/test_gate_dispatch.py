"""gate_dispatch through main() on fixture repos (strict-delivery S2 Tasks 1 and 3).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_dispatch -v
"""
import contextlib
import io
import json
import os
import shutil
import unittest
from unittest import mock

from tools.gates import gate_dispatch
from tools.gates.lib import config
from tools.gates.tests import repo_fixture as rf

DFIX = os.path.join(rf.FIX, "dispatch")
PROMPTS = os.path.join(DFIX, "prompts")


def fixture(name):
    with open(os.path.join(DFIX, name), encoding="utf-8", newline="") as f:
        return f.read()


GOOD = fixture("good_implementer.md")
REVIEWER = fixture("good_reviewer.md")


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf), \
            contextlib.redirect_stderr(io.StringIO()):    # argparse usage goes to stderr
        rc = gate_dispatch.main(argv)
    return rc, buf.getvalue()


class GateDispatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = rf.make_repo(rf.DISPATCH_FILES, rf.load_rows(),
                                after={"docs/moved.md": "moved twice\n"})
        cls.conf = config.load(rf.REAL)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def dispatch(self, text, *extra, root=None):
        root = root or self.root
        path = os.path.join(root, "prompt.under-test.md")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        return gate(root, ["--prompt", path, "--prompts-dir", PROMPTS, *extra])

    def variant(self, old, new, base=GOOD):
        text = base.replace(old, new, 1)
        self.assertNotEqual(base, text, f"fixture lacks {old!r}")
        return text

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)

    def test_good_prompt_passes(self):
        rc, out = self.dispatch(GOOD)
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)

    def test_good_reviewer_with_inner_heading_passes(self):
        self.assertIn("\n## Checks\n", fixture("prompts/reviewer.md"))
        rc, out = self.dispatch(REVIEWER)
        self.assertEqual(0, rc, out)

    def test_wrong_model_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: haiku"))
        self.assertFailLine(rc, out, "dispatch.model.rank")

    def test_fable_on_implementer_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: fable"))
        self.assertFailLine(rc, out, "dispatch.model.fable")
        self.assertNotIn("FAIL dispatch.model.rank", out)

    def test_unknown_model_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: gpt"))
        self.assertFailLine(rc, out, "dispatch.model.known")

    def test_unknown_role_fails(self):
        rc, out = self.dispatch(self.variant("## Role: implementer", "## Role: helper"))
        self.assertFailLine(rc, out, "dispatch.role")

    def test_unregistered_finding_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-404"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-404")

    def test_implementer_on_open_finding_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-2"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-2")

    def test_implementer_without_findings_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: none"))
        self.assertFailLine(rc, out, "dispatch.findings")

    def test_stale_claim_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.stale"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.stale")

    def test_claim_whose_cited_file_changed_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.moved"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.moved")
        self.assertIn("docs/moved.md", out)

    def test_open_decision_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0002.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_unknown_decision_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0404.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0404")

    def test_decision_answered_by_a_non_owner_fails(self):
        rows = rf.load_rows()
        rows["decisions"].append({
            "id": "DEC-0003", "class": "scope", "question": "Does fixture task 4 enter the plan?",
            "options": [{"label": "enter", "cost": "one more task"}, {"label": "defer", "cost": "none"}],
            "asked_at": {"sha": "@BASE@", "by": "fixture"}, "answer": "defer",
            "answered_at": {"sha": "@BASE@", "by": "fixture"}})
        root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0003.\n", root=root)
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0003")
        self.assertIn("unanswered", out)

    def test_decision_id_inside_a_word_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows_DEC-0002.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_answered_decision_passes(self):
        rc, out = self.dispatch(GOOD + "Merge under DEC-0001.\n")
        self.assertEqual(0, rc, out)

    def test_prompt_at_cap_passes_and_one_over_fails(self):
        cap = self.conf["dispatch"]["max_prompt_chars"]
        at_cap = GOOD + "x" * (cap - len(GOOD) - 1) + "\n"
        self.assertEqual(cap, len(at_cap))
        rc, out = self.dispatch(at_cap)
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch(at_cap + "y")
        self.assertFailLine(rc, out, "dispatch.length")

    def test_strict_block_altered_by_one_byte_fails(self):
        block = fixture("prompts/implementer.md")
        i = GOOD.index(block) + len(block) // 2
        text = GOOD[:i] + ("X" if GOOD[i] != "X" else "Y") + GOOD[i + 1:]
        rc, out = self.dispatch(text)
        self.assertFailLine(rc, out, "dispatch.strict")

    def test_strict_block_must_be_followed_by_task(self):
        rc, out = self.dispatch(self.variant("## Task\n", "## Tasks\n"))
        self.assertFailLine(rc, out, "dispatch.strict")

    def test_empty_task_fails(self):
        rc, out = self.dispatch(self.variant("Implement fixture task 1 from docs/brief.md.\n", "\n"))
        self.assertFailLine(rc, out, "dispatch.task")

    def test_missing_brief_fails(self):
        rc, out = self.dispatch(self.variant("## Brief: docs/brief.md", "## Brief: docs/no-brief.md"))
        self.assertFailLine(rc, out, "dispatch.brief")

    def test_report_dir_missing_fails(self):
        rc, out = self.dispatch(self.variant("## Report: reports/t1.md", "## Report: nowhere/t1.md"))
        self.assertFailLine(rc, out, "dispatch.report")

    def test_section_out_of_order_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1\n## Claims: C-fx.ok\n",
                                             "## Claims: C-fx.ok\n## Findings: SD-FX-1\n"))
        self.assertFailLine(rc, out, "dispatch.grammar")

    def test_budget_over_role_cap_fails(self):
        rc, out = self.dispatch(self.variant("tokens=150000", "tokens=150001"))
        self.assertFailLine(rc, out, "dispatch.budget")

    def test_tool_model_must_agree(self):
        rc, out = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch(GOOD, "--tool-model", "opus")
        self.assertFailLine(rc, out, "dispatch.model.agree")
        rc, out = self.dispatch(GOOD, "--tool-model", "")
        self.assertFailLine(rc, out, "dispatch.model.agree")

    def test_no_input_fails(self):
        rc, out = gate(self.root, [])
        self.assertFailLine(rc, out, "dispatch.input")

    def test_json_flag_first_prints_json(self):
        rc, out = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, out)
        path = os.path.join(self.root, "prompt.under-test.md")
        rc, out = gate(self.root, ["--json", "--prompt", path, "--prompts-dir", PROMPTS])
        self.assertEqual(0, rc, out)
        doc = json.loads(out)
        self.assertEqual("dispatch", doc["gate"])
        self.assertTrue(doc["ok"])

    def test_json_flag_after_other_arguments_fails(self):
        rc, out = self.dispatch(GOOD, "--json")
        self.assertFailLine(rc, out, "dispatch.input")

    def test_prompt_from_stdin_passes(self):
        with mock.patch("sys.stdin", io.StringIO(GOOD)):
            rc, out = gate(self.root, ["--prompt", "-", "--prompts-dir", PROMPTS])
        self.assertEqual(0, rc, out)


if __name__ == "__main__":
    unittest.main()
