"""gate_dispatch through main() on fixture repos (strict-delivery S2 Tasks 1 and 3).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_dispatch -v
"""
import contextlib
import io
import json
import os
import shutil
import tempfile
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
        # one prompt file per prompt name the umbrella lists, each ending in a newline
        cls.allp = tempfile.mkdtemp(prefix="gate-prompts-")
        for name in sorted(set(gate_dispatch.ROLE_PROMPT.values())):
            with open(os.path.join(cls.allp, name + ".md"), "w", encoding="utf-8", newline="") as f:
                f.write(f"You are the {name}.\n")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)
        shutil.rmtree(cls.allp, ignore_errors=True)

    def role_prompt(self, role, *, brief="docs/brief.md", findings=None, budget="tokens=1000 minutes=10",
                    report="reports/x.md", prompts=None):
        """A well-formed prompt for `role` over the all-roles prompts dir (or `prompts`)."""
        if findings is None:
            findings = "SD-FX-1" if role in gate_dispatch.IMPLEMENTERS else "SD-FX-2"
        name = gate_dispatch.ROLE_PROMPT[role]
        with open(os.path.join(prompts or self.allp, name + ".md"), encoding="utf-8", newline="") as f:
            block = f.read()
        return (f"## Role: {role}\n## Brief: {brief}\n## Findings: {findings}\n## Claims: none\n"
                f"## Budget: {budget}\n## Model: {self.conf['models'][role]}\n## Report: {report}\n"
                f"## Strict block\n{block}## Task\nDo the fixture task.\n")

    def dispatch_role(self, text, prompts=None):
        return self.dispatch(text, prompts=prompts or self.allp)

    def dispatch(self, text, *extra, root=None, prompts=None):
        root = root or self.root
        path = os.path.join(root, "prompt.under-test.md")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        return gate(root, ["--prompt", path, "--prompts-dir", prompts or PROMPTS, *extra])

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

    def test_report_naming_a_directory_fails(self):
        for value in ("reports", "reports/"):
            rc, out = self.dispatch(self.variant("## Report: reports/t1.md", f"## Report: {value}"))
            self.assertFailLine(rc, out, "dispatch.report")
            self.assertIn("names a directory", out)

    def test_every_role_passes_with_its_prompt_file(self):
        self.assertEqual({"controller", "implementer", "reviewer", "verifier", "exception-reviewer",
                          "audit-reviewer", "audit-verifier", "investigator"},
                         set(gate_dispatch.ROLE_PROMPT.values()))
        self.assertEqual(set(gate_dispatch.ROLES), set(gate_dispatch.ROLE_PROMPT))
        for role in gate_dispatch.ROLES:
            rc, out = self.dispatch_role(self.role_prompt(role))
            self.assertEqual(0, rc, f"{role}: {out}")

    def test_audit_and_investigator_roles_may_name_no_findings_and_no_brief(self):
        for role in ("audit_reviewer", "audit_verifier", "investigator"):
            rc, out = self.dispatch_role(self.role_prompt(role, brief="none", findings="none"))
            self.assertEqual(0, rc, f"{role}: {out}")

    def test_controller_budget_caps(self):
        rc, out = self.dispatch_role(self.role_prompt("controller", budget="tokens=250000 minutes=90"))
        self.assertEqual(0, rc, out)
        for budget in ("tokens=250001 minutes=90", "tokens=250000 minutes=91"):
            rc, out = self.dispatch_role(self.role_prompt("controller", budget=budget))
            self.assertFailLine(rc, out, "dispatch.budget")

    def test_reviewer_token_cap(self):
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", budget="tokens=120000 minutes=30"))
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", budget="tokens=120001 minutes=30"))
        self.assertFailLine(rc, out, "dispatch.budget")

    def test_zero_budget_fails(self):
        for budget in ("tokens=0 minutes=10", "tokens=1000 minutes=0"):
            rc, out = self.dispatch_role(self.role_prompt("implementer", budget=budget))
            self.assertFailLine(rc, out, "dispatch.budget")

    def test_prompt_file_without_final_newline_fails(self):
        bad = tempfile.mkdtemp(prefix="gate-prompts-")
        self.addCleanup(shutil.rmtree, bad, ignore_errors=True)
        with open(os.path.join(bad, "implementer.md"), "w", encoding="utf-8", newline="") as f:
            f.write("You are the implementer.")
        rc, out = self.dispatch_role(self.role_prompt("implementer"), prompts=bad)
        self.assertFailLine(rc, out, "dispatch.strict")
        self.assertIn("must end with a newline", out)

    def test_transcription_implementer_needs_a_planned_row(self):
        rc, out = self.dispatch_role(self.role_prompt("implementer_transcription", findings="SD-FX-2"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-2")

    def test_reviewer_needs_a_brief(self):
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", brief="none"))
        self.assertFailLine(rc, out, "dispatch.brief")

    def test_prompt_from_stdin_passes(self):
        with mock.patch("sys.stdin", io.StringIO(GOOD)):
            rc, out = gate(self.root, ["--prompt", "-", "--prompts-dir", PROMPTS])
        self.assertEqual(0, rc, out)


if __name__ == "__main__":
    unittest.main()
