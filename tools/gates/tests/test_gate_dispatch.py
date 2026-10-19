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


def config_with_fallback(fallback=None):
    """The real config.yml with fable_fallback set to `fallback` (None: absent), parsed by config.load
    from a throwaway tree and injected at config.load (gates read config from CODE_ROOT, S1)."""
    d = tempfile.mkdtemp(prefix="gate-cfg-")
    try:
        rf.write(d, "tools/gates/config.yml", rf.config_text(fallback))
        conf = config.load(d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return mock.patch("tools.gates.lib.config.load", return_value=conf)


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

    def test_implementer_on_a_pinned_or_merged_row_fails(self):
        rows = rf.load_rows()
        for fid, state in (("SD-FX-3", "pinned"), ("SD-FX-4", "merged"), ("SD-FX-5", "in-progress")):
            rows["findings"].append(dict(rows["findings"][0], id=fid, state=state))
        root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        for fid, state in (("SD-FX-3", "pinned"), ("SD-FX-4", "merged")):
            rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", f"## Findings: {fid}"), root=root)
            self.assertFailLine(rc, out, f"dispatch.findings.{fid}")
            self.assertIn(f"state {state!r}; an implementer takes only planned or in-progress rows", out)
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-5"), root=root)
        self.assertEqual(0, rc, out)

    def test_implementer_without_findings_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: none"))
        self.assertFailLine(rc, out, "dispatch.findings")

    def test_stale_claim_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.stale"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.stale")

    def test_unregistered_claim_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.404"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.404")
        self.assertIn("FAIL dispatch.claims.C-fx.404: not in docs/superpowers/state/claims.yml", out)

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

    def test_a_five_digit_text_is_a_mention_of_its_first_four_digits(self):
        """DEC-0010 (umbrella pattern DEC-[0-9]{4}, no lookahead): DEC-00021 cites DEC-0002."""
        rc, out = self.dispatch(GOOD + "Scope follows DEC-00021.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_non_ascii_digits_never_form_an_id(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-\uff10\uff10\uff10\uff12 (fullwidth).\n")
        self.assertEqual(0, rc, out)
        self.assertNotIn("dispatch.decisions.DEC-", out.replace("PASS dispatch.decisions\n", ""))

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

    def test_budget_with_trailing_text_fails(self):
        rc, out = self.dispatch(self.variant("## Budget: tokens=150000 minutes=60\n",
                                             "## Budget: tokens=150000 minutes=60 extra\n"))
        self.assertFailLine(rc, out, "dispatch.budget")
        self.assertIn("expected 'tokens=<n> minutes=<n>'", out)

    def test_fork_dispatch_fails(self):
        """Ruling 4 (B4-FIX-1): a fork runs on the session model with the session context."""
        for value in ("fork", "Fork", " fork "):
            with self.subTest(subagent_type=value):
                rc, out = self.dispatch(GOOD, "--tool-model", "sonnet", "--subagent-type", value)
                self.assertFailLine(rc, out, "dispatch.fork")
                self.assertIn("session model", out)
                self.assertIn("general-purpose", out)

    def test_fork_fails_even_when_the_prompt_is_not_in_the_grammar(self):
        rc, out = self.dispatch("not a dispatch prompt\n", "--subagent-type", "fork")
        self.assertFailLine(rc, out, "dispatch.fork")
        self.assertIn("FAIL dispatch.grammar", out)

    def test_other_subagent_types_keep_todays_behaviour(self):
        rc, base = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, base)
        self.assertNotIn("dispatch.fork", base)
        for value in ("general-purpose", "Explore", "claude", "forked", ""):
            with self.subTest(subagent_type=value):
                rc, out = self.dispatch(GOOD, "--tool-model", "sonnet", "--subagent-type", value)
                self.assertEqual(0, rc, out)
                self.assertEqual("PASS dispatch.fork\n" + base, out)

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

    # -- DEC-0018: the fable_fallback key (B5-FIX-2)
    def fable_role_on(self, role, model, fallback, tool_model=None):
        text = self.role_prompt(role).replace("## Model: fable\n", f"## Model: {model}\n", 1)
        self.assertIn(f"## Model: {model}\n", text)
        extra = [] if tool_model is None else ["--tool-model", tool_model]
        with config_with_fallback(fallback):
            return self.dispatch(text, *extra, prompts=self.allp)

    def test_fallback_on_lets_a_fable_role_run_on_opus(self):
        for role in ("final_verifier", "exception_reviewer"):
            with self.subTest(role):
                rc, out = self.fable_role_on(role, "opus", "opus", tool_model="opus")
                self.assertEqual(0, rc, out)
                lines = [l for l in out.splitlines() if l.startswith("PASS dispatch.model.fallback: ")]
                self.assertEqual(1, len(lines), out)
                self.assertIn("DEC-0018", lines[0])
                self.assertIn(f"{role} on opus", lines[0])
                self.assertNotIn("dispatch.model.rank", out)
                self.assertIn("PASS dispatch.model.agree", out)

    def test_fallback_absent_keeps_the_rank_failure(self):
        for role in ("final_verifier", "exception_reviewer"):
            with self.subTest(role):
                rc, out = self.fable_role_on(role, "opus", None)
                self.assertFailLine(rc, out, f"dispatch.model.rank: role {role} needs at least fable, got opus")
                self.assertNotIn("dispatch.model.fallback", out)

    def test_fable_stays_allowed_with_the_fallback_on(self):
        for fallback in ("opus", None):
            with self.subTest(fallback=fallback):
                rc, out = self.fable_role_on("final_verifier", "fable", fallback)
                self.assertEqual(0, rc, out)
                self.assertIn("PASS dispatch.model.rank\n", out)
                self.assertNotIn("dispatch.model.fallback", out)

    def test_a_model_below_the_fallback_still_fails_rank(self):
        for model in ("sonnet", "haiku"):
            with self.subTest(model):
                rc, out = self.fable_role_on("final_verifier", model, "opus")
                self.assertFailLine(rc, out, f"dispatch.model.rank: role final_verifier needs at least fable, got {model}")
                self.assertNotIn("dispatch.model.fallback", out)

    def test_an_invalid_fallback_admits_no_lower_model(self):
        for fallback in ("sonnet", "haiku"):
            with self.subTest(fallback):
                rc, out = self.fable_role_on("final_verifier", fallback, fallback)
                self.assertFailLine(rc, out, "dispatch.model.fallback: ")
                self.assertIn(f"fable_fallback {fallback!r} must be a model_rank model ranked at least opus and below fable", out)
                self.assertNotIn("PASS dispatch.model.fallback", out)

    def test_non_fable_roles_are_unchanged_by_the_fallback(self):
        with config_with_fallback("opus"):
            rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: fable"))
            self.assertFailLine(rc, out, "dispatch.model.fable")
            self.assertNotIn("dispatch.model.fallback", out)
            rc, out = self.dispatch(GOOD)
            self.assertEqual(0, rc, out)
            self.assertNotIn("dispatch.model.fallback", out)
            rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: haiku"))
            self.assertFailLine(rc, out, "dispatch.model.rank")
            for role in ("task_reviewer", "controller"):
                rc, out = self.dispatch_role(self.role_prompt(role))
                self.assertEqual(0, rc, f"{role}: {out}")
                self.assertIn("PASS dispatch.model.rank\n", out)
                self.assertNotIn("dispatch.model.fallback", out)
        with config_with_fallback("opus") as load:    # a fable minimum outside fable_roles (--config-check fails it)
            load.return_value["models"]["task_reviewer"] = "fable"
            rc, out = self.dispatch_role(self.role_prompt("task_reviewer"))    # ## Model: opus
            self.assertFailLine(rc, out, "dispatch.model.rank: role task_reviewer needs at least fable, got opus")
            self.assertNotIn("dispatch.model.fallback", out)

    def test_readme_describes_the_fallback(self):
        """Sibling 'README gate_dispatch section' (B5-FIX-2): the section and the table row name the key, its
        validation, the fallback PASS line and DEC-0018."""
        with open(os.path.join(rf.REAL, "tools", "gates", "README.md"), encoding="utf-8") as f:
            readme = f.read()
        section = readme[readme.index("## gate_dispatch (S2)\n"):readme.index("\n## make_review_package")]
        for word in ("`fable_fallback`", "PASS dispatch.model.fallback", "FAIL dispatch.config.fable_fallback",
                     "DEC-0018", "ranked at", "15 lines."):
            self.assertIn(word, section)
        row = [l for l in readme.splitlines() if l.startswith("| `gate_dispatch.py` |")]
        self.assertEqual(1, len(row))
        for word in ("`fable_fallback`", "DEC-0018", "PASS dispatch.model.fallback"):
            self.assertIn(word, row[0])

    def test_spec_and_config_name_the_fallback(self):
        """Sibling 'spec D18/§5 text' (B5-FIX-2): the spec and tools/gates/config.yml agree on the key."""
        with open(os.path.join(rf.REAL, "docs/superpowers/specs/2026-10-17-strict-delivery-design.md"),
                  encoding="utf-8") as f:
            spec = f.read().splitlines()
        d18 = [l for l in spec if l.startswith("| D18 |")]
        self.assertEqual(1, len(d18), "spec D18 row")
        for word in ("`fable_fallback`", "DEC-0018", "run on opus"):
            self.assertIn(word, d18[0])
        for row in ("| Final adversarial verifier |", "| Exception (P10) reviewer |"):
            hits = [l for l in spec if l.startswith(row)]
            self.assertEqual(1, len(hits), row)
            self.assertIn("Fable (opus while the DEC-0018 fallback is on)", hits[0])
        with open(os.path.join(rf.REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
            text = f.read()
        self.assertRegex(text, r"(?m)^# .*\bDEC-0018\b.*$")
        self.assertRegex(text, r"(?m)^# .*\bfable_fallback\b.*$")
        conf = config.load(rf.REAL)
        self.assertIn(conf.get("fable_fallback", "opus"), ("opus",), "the spec names opus as the fallback")


class ConfigCheck(unittest.TestCase):
    def with_config(self, edit):
        """Config is read from CODE_ROOT (S1), so a bad config is injected at config.load."""
        conf = config.load(rf.REAL)
        edit(conf)
        return mock.patch("tools.gates.lib.config.load", return_value=conf)

    def test_config_check_passes_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.implementer", out)

    def test_config_check_fails_when_implementer_minimum_is_fable(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "fable")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer", out)

    def test_config_check_fails_on_an_unknown_role(self):
        with self.with_config(lambda c: c["models"].__setitem__("helper", "opus")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.models", out)

    def test_config_check_fails_on_a_non_positive_cap(self):
        with self.with_config(lambda c: c["dispatch"].__setitem__("max_prompt_chars", 0)):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_cap_that_is_not_an_integer(self):
        for bad in (True, 1.5, "12000"):
            with self.subTest(cap=bad), \
                    self.with_config(lambda c, b=bad: c["dispatch"].__setitem__("max_prompt_chars", b)):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_minimum_that_is_not_in_model_rank(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "gpt")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer: minimum model 'gpt' not in model_rank", out)

    def test_config_check_fails_on_a_role_with_no_minimum(self):
        with self.with_config(lambda c: c["models"].pop("investigator")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: minimum model None not in model_rank", out)

    def test_config_check_fails_on_an_unknown_fable_role(self):
        with self.with_config(lambda c: c["fable_roles"].append("helper")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.fable_roles: unknown roles: helper", out)

    def test_config_check_fails_when_fable_roles_differ_from_the_fable_minimums(self):
        """DEC-0012 = require-equal: fable_roles is exactly the roles whose minimum model is fable."""
        for name, edit in (("extra", lambda c: c["fable_roles"].append("implementer")),
                           ("missing", lambda c: c["fable_roles"].remove("exception_reviewer"))):
            with self.subTest(name), self.with_config(edit):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_roles: ", out)
                self.assertIn("fable minimums ['exception_reviewer', 'final_verifier']", out)

    def test_config_check_names_both_fable_sets(self):
        with self.with_config(lambda c: c["fable_roles"].append("implementer")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_roles: fable_roles ['exception_reviewer', 'final_verifier',"
                      " 'implementer'] != the roles whose minimum model is fable"
                      " (fable minimums ['exception_reviewer', 'final_verifier'])", out)

    def test_config_check_passes_model_rank_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.model_rank\n", out)
        self.assertIn("PASS dispatch.config.fable_roles\n", out)

    def test_config_check_fails_when_model_rank_is_not_strictly_ordered(self):
        bad = ({"haiku": 1, "sonnet": 2, "opus": 5, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": 3},
               {"haiku": 2, "sonnet": 1, "opus": 3, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3},
               {"haiku": 1, "sonnet": 2, "opus": 3, "fable": "4"}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": True},
               {"haiku": True, "sonnet": 2, "opus": 3, "fable": 4})
        for rank in bad:
            with self.subTest(rank=rank), self.with_config(lambda c, r=rank: c.__setitem__("model_rank", dict(r))):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.model_rank: must order haiku < sonnet < opus < fable", out)

    def test_config_check_fails_when_a_role_has_no_prompt_file(self):
        patched = {k: v for k, v in gate_dispatch.ROLE_PROMPT.items() if k != "investigator"}
        with mock.patch.object(gate_dispatch, "ROLE_PROMPT", patched):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: no prompt file mapped for the role", out)

    def test_neither_prompt_nor_config_check_is_a_bad_argument(self):
        rc, out = gate(rf.REAL, [])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input: bad arguments", out)

    def test_prompt_and_config_check_are_exclusive(self):
        rc, out = gate(rf.REAL, ["--config-check", "--prompt", "x.md"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input", out)

    # -- DEC-0018: the fable_fallback key (B5-FIX-2)
    def test_config_check_passes_the_repo_fallback(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.fable_fallback: opus: the fable_roles run on opus while Fable is "
                      "unavailable (DEC-0018)\n", out + "\n")
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_check_passes_an_absent_fallback(self):
        with config_with_fallback(None) as load:
            self.assertNotIn("fable_fallback", load.return_value)
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)", out)
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_check_fails_an_invalid_fallback(self):
        for value in ("sonnet", "haiku", "fable", "gpt", "null", "[opus]", "3", "Opus"):
            with self.subTest(value), config_with_fallback(value):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback ", out)
                self.assertIn("must be a model_rank model ranked at least opus and below fable", out)
                self.assertNotIn("PASS dispatch.config.fable_fallback", out)

    def test_config_check_ranks_the_fallback_by_model_rank(self):
        """A model_rank that ranks sonnet with opus admits sonnet; one that ranks opus with fable admits nothing."""
        with config_with_fallback("sonnet") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 3, "opus": 3, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("PASS dispatch.config.fable_fallback: sonnet: ", out)
        with config_with_fallback("opus") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 2, "opus": 4, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)
        with config_with_fallback("opus") as load:    # no fable rank: fail closed, no KeyError
            load.return_value["model_rank"].pop("fable")
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)

    def test_run_all_runs_dispatch_in_config_check_mode(self):
        from tools.gates import run_all
        self.assertIn("dispatch", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": rf.REAL}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "dispatch"])
        self.assertEqual(0, rc, buf.getvalue())
        self.assertIn("dispatch.config.implementer", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
