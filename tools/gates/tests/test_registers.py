#!/usr/bin/env python3
"""Tests for tools/gates/lib/registers.py: loaders, schema validators, lifecycle rules, and the
bootstrapped registers under docs/superpowers/state/. Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import copy
import os
import tempfile
import unittest

from tools.gates import lib
from tools.gates.lib import GateError, registers as reg
from tools.gates.tests import gitfixture as gf

FINDING = {
    "id": "T1-PIN-1", "source": "review", "severity": "HIGH",
    "class": "A CRC-bad frame is dropped and every later delta is applied to the old anchor.",
    "siblings": ["CRC-bad KEY"], "cites": [{"file": "a.c", "lines": "1-2", "sha": "aaaaaaa"}],
    "repro": None, "owner": {"kind": "plan", "plan": "demo", "task": 1}, "state": "open",
    "pin": None, "ruling": None, "evidence": [],
    "history": [{"state": "open", "sha": "aaaaaaa", "by": "audit"}],
}
CLAIM = {
    "id": "C-spec-1.1-demo", "doc": "docs/spec.md", "anchor": "§1.1", "text": "x is y",
    "direct": "grep -n y docs/spec.md", "contra": "if x were not y, test_x fails",
    "cites": ["a.c"], "verified_at": None, "by": None, "state": "unverified", "finding": None,
}
DECISION = {
    "id": "DEC-9001", "class": "triage", "question": "accept or widen?",
    "options": [{"label": "accept", "cost": "8 tasks"}, {"label": "widen", "cost": "18 tasks"}],
    "asked_at": {"sha": "aaaaaaa", "by": "session"}, "answer": None, "answered_at": None,
}


MUT = "docs/superpowers/state/demo/mutations/"


def with_(row, **kw):
    out = copy.deepcopy(row)
    out.update(kw)
    return out


class Loaders(unittest.TestCase):
    def test_bootstrapped_registers_are_valid(self):
        root = lib.CODE_ROOT
        self.assertEqual([], reg.load_findings(root))
        self.assertEqual([], reg.load_claims(root))
        decisions = reg.load_decisions(root)
        self.assertEqual(["DEC-0001", "DEC-0002", "DEC-0003"], [d["id"] for d in decisions])
        self.assertEqual([[], [], []], [reg.validate_decision(d) for d in decisions])
        self.assertEqual("merge-grant", decisions[0]["class"])
        self.assertEqual("grant:strict-delivery", decisions[0]["question"])

    def test_missing_empty_and_non_list_registers_raise(self):
        root = tempfile.mkdtemp(prefix="gate-reg-")
        try:
            with self.assertRaisesRegex(GateError, "findings.yml: No such file"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "")
            with self.assertRaisesRegex(GateError, "empty file"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "id: A-1\n")
            with self.assertRaisesRegex(GateError, "must be a YAML list"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "- A-1\n")
            with self.assertRaisesRegex(GateError, "entry 0 is not a mapping"):
                reg.load_findings(root)
        finally:
            gf.remove(root)


class FindingSchema(unittest.TestCase):
    def test_a_well_formed_finding_has_no_errors(self):
        self.assertEqual([], reg.validate_finding(FINDING))

    def test_missing_and_unknown_keys(self):
        f = with_(FINDING, extra=1)
        del f["siblings"]
        self.assertEqual(["keys: missing siblings", "keys: unknown extra"], reg.validate_finding(f))

    def test_unquoted_numeric_sha_is_named(self):
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": 7265059}])
        self.assertIn("cites: entry 0 sha: parsed as a number; quote the sha exactly as written in the file",
                      reg.validate_finding(f))

    def test_owner_shapes(self):
        ok = [{"kind": "plan", "plan": "p", "task": 7}, {"kind": "issue", "number": 140},
              {"kind": "roadmap", "plan": 8}]
        for o in ok:
            self.assertEqual([], reg.owner_errors(o), o)
        bad = [None, {"kind": "person"}, {"kind": "issue", "number": "140"},
               {"kind": "issue", "number": True}, {"kind": "plan", "plan": "p"},
               {"kind": "roadmap", "plan": ""}]
        for o in bad:
            errs = reg.owner_errors(o)
            self.assertTrue(errs and all(e.startswith("owner: ") for e in errs), (o, errs))

    def test_history_must_end_at_the_current_state(self):
        f = with_(FINDING, state="planned")
        self.assertIn("history: last entry state 'open' != state 'planned'", reg.validate_finding(f))

    def test_bad_severity_id_and_ruling(self):
        f = with_(FINDING, id="lower-case", severity="SEVERE", ruling="D-1")
        errs = reg.validate_finding(f)
        self.assertTrue(any(e.startswith("id: ") for e in errs), errs)
        self.assertTrue(any(e.startswith("severity: ") for e in errs), errs)
        self.assertTrue(any(e.startswith("ruling: ") for e in errs), errs)


class Transitions(unittest.TestCase):
    def hist(self, *states):
        return with_(FINDING, state=states[-1],
                     history=[{"state": s, "sha": "aaaaaaa", "by": "x"} for s in states])

    def test_every_legal_edge_passes(self):
        for a, tos in reg.FINDING_TRANSITIONS.items():
            for b in tos:
                path = ["open"] if a == "open" else self.path_to(a)
                self.assertEqual([], reg.transition_errors(self.hist(*path, b)), (path, b))

    def path_to(self, state):
        paths = {"planned": ["open", "planned"], "in-progress": ["open", "planned", "in-progress"],
                 "fixed-unpinned": ["open", "planned", "in-progress", "fixed-unpinned"],
                 "pinned": ["open", "planned", "in-progress", "pinned"],
                 "deferred": ["open", "deferred"], "closed": ["open", "closed"],
                 "merged": ["open", "closed", "merged"]}
        return paths[state]

    def test_every_illegal_edge_fails(self):
        for a in reg.FINDING_STATES:
            for b in reg.FINDING_STATES:
                if b in reg.FINDING_TRANSITIONS[a]:
                    continue
                path = ["open"] if a == "open" else self.path_to(a)
                errs = reg.transition_errors(self.hist(*path, b))
                self.assertIn(f"transition: {a} -> {b} is illegal", errs[-1], (a, b))

    def test_history_must_start_open(self):
        self.assertEqual(["transition: history starts at 'planned', not 'open'"],
                         reg.transition_errors(self.hist("planned")))


class Evidence(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="gate-ev-")
        gf.write(self.root, MUT + "T1-PIN-1.txt", "finding: T1-PIN-1\n")
        gf.write(self.root, "st/demo/review/T1.md", "status: DONE\n")

    def tearDown(self):
        gf.remove(self.root)

    def pinned(self, **kw):
        f = with_(FINDING, state="pinned",
                  pin={"test": "test_x", "transcript": MUT + "T1-PIN-1.txt"},
                  evidence=["st/demo/review/T1.md"],
                  history=[{"state": s, "sha": "aaaaaaa", "by": "x"}
                           for s in ("open", "planned", "in-progress", "pinned")])
        f.update(kw)
        return f

    def test_pinned_with_transcript_and_review_passes(self):
        self.assertEqual([], reg.evidence_errors(self.pinned(), self.root, []))

    def test_pinned_needs_pin_transcript_file_and_review(self):
        self.assertIn("evidence: pinned needs pin {test, transcript}",
                      reg.evidence_errors(self.pinned(pin=None), self.root, []))
        gone = {"test": "test_x", "transcript": MUT + "absent.txt"}
        self.assertIn(f"evidence: pin transcript {MUT}absent.txt does not exist",
                      reg.evidence_errors(self.pinned(pin=gone), self.root, []))
        self.assertIn("evidence: pinned needs the reviewer's confirming report path in evidence",
                      reg.evidence_errors(self.pinned(evidence=["bbbbbbb"]), self.root, []))

    def test_evidence_entries_must_exist(self):
        self.assertIn("evidence: 'st/nope.md' is neither a commit sha nor a file under the repo root",
                      reg.evidence_errors(self.pinned(evidence=["st/nope.md"]), self.root, []))

    def test_evidence_paths_cannot_escape_the_repo_root(self):
        outside = os.path.join(os.path.dirname(self.root), os.path.basename(self.root) + "-out.md")
        with open(outside, "w", encoding="utf-8") as fh:
            fh.write("x\n")
        self.addCleanup(os.remove, outside)
        for p in ("../" + os.path.basename(outside),):
            self.assertIn(f"evidence: {p!r} is neither a commit sha nor a file under the repo root",
                          reg.evidence_errors(self.pinned(evidence=[p]), self.root, []), p)
        escaped = {"test": "test_x", "transcript": MUT + "../../../../../" + os.path.basename(outside)}
        self.assertIn(f"evidence: pin transcript must be docs/superpowers/state/<plan>/mutations/<file>, got {escaped['transcript']!r}",
                      reg.evidence_errors(self.pinned(pin=escaped), self.root, []))

    def test_absolute_and_nul_evidence_paths_are_errors_not_crashes(self):
        abs_p = os.path.join(self.root, "st/demo/review/T1.md")
        self.assertIn(f"evidence: path must be relative to the repo root: {abs_p!r}",
                      reg.evidence_errors(self.pinned(evidence=[abs_p]), self.root, []))
        abs_t = {"test": "test_x", "transcript": os.path.join(self.root, MUT + "T1-PIN-1.txt")}
        errs = reg.evidence_errors(self.pinned(pin=abs_t), self.root, [])
        self.assertIn("evidence: pin transcript: must be relative to the repo root", errs)
        self.assertFalse(any("does not exist" in e for e in errs), errs)
        nul = "st/a\0b.md"
        self.assertIn(f"evidence: {nul!r} is neither a commit sha nor a file under the repo root",
                      reg.evidence_errors(self.pinned(evidence=[nul]), self.root, []))
        bad = {"test": "test_x", "transcript": MUT + "a\0b.txt"}
        self.assertIn(f"evidence: pin transcript {MUT}a\0b.txt does not exist",
                      reg.evidence_errors(self.pinned(pin=bad), self.root, []))

    def test_deferred_needs_an_answered_owner_decision(self):
        f = with_(FINDING, state="deferred", ruling="DEC-9001",
                  history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                           {"state": "deferred", "sha": "aaaaaaa", "by": "a"}])
        self.assertIn("evidence: ruling DEC-9001 is not in decisions.yml",
                      reg.evidence_errors(f, self.root, []))
        self.assertIn("evidence: ruling DEC-9001 is not answered by the owner",
                      reg.evidence_errors(f, self.root, [DECISION]))
        answered = with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        self.assertEqual([], reg.evidence_errors(f, self.root, [answered]))
        self.assertIn("evidence: deferred needs ruling = the owner's decision id",
                      reg.evidence_errors(with_(f, ruling=None), self.root, [answered]))

    def test_closed_needs_evidence_and_merged_needs_postmerge(self):
        closed = with_(FINDING, state="closed", history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                                                          {"state": "closed", "sha": "aaaaaaa", "by": "a"}])
        self.assertEqual(["evidence: closed needs evidence (commits or report paths)"],
                         reg.evidence_errors(closed, self.root, []))
        merged = with_(closed, state="merged", evidence=["bbbbbbb"],
                       history=closed["history"] + [{"state": "merged", "sha": "aaaaaaa", "by": "session"}])
        self.assertEqual(["evidence: merged is set only by the post-merge script (by: postmerge), got 'session'"],
                         reg.evidence_errors(merged, self.root, []))


class ClaimSchema(unittest.TestCase):
    def test_a_well_formed_claim_has_no_errors(self):
        self.assertEqual([], reg.validate_claim(CLAIM))

    def test_verified_needs_sha_and_by(self):
        self.assertIn("state: verified needs verified_at and by",
                      reg.validate_claim(with_(CLAIM, state="verified")))

    def test_false_needs_a_finding_and_only_false_has_one(self):
        self.assertIn("finding: a false claim must name the finding id that records it",
                      reg.validate_claim(with_(CLAIM, state="false")))
        self.assertIn("finding: only a false claim carries a finding (set it to null)",
                      reg.validate_claim(with_(CLAIM, finding="A2-IO-N4")))
        self.assertEqual([], reg.validate_claim(with_(CLAIM, state="false", finding="A2-IO-N4")))

    def test_boolean_false_state_is_named(self):
        self.assertEqual(['state: YAML read false as a boolean; quote it: state: "false"',
                          "finding: only a false claim carries a finding (set it to null)"],
                         reg.validate_claim(with_(CLAIM, state=False, finding="A2-IO-N4")))

    def test_a_claim_must_cite_something(self):
        self.assertTrue(any(e.startswith("cites: ") for e in reg.validate_claim(with_(CLAIM, cites=[]))))


class DecisionSchema(unittest.TestCase):
    def test_a_well_formed_decision_has_no_errors(self):
        self.assertEqual([], reg.validate_decision(DECISION))

    def test_unquoted_yes_is_named(self):
        d = with_(DECISION, answer=True, answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        errs = reg.validate_decision(d)
        self.assertTrue(any("quote it" in e for e in errs), errs)

    def test_answer_and_answered_at_go_together(self):
        self.assertIn("answered_at: answer and answered_at are both null or both set",
                      reg.validate_decision(with_(DECISION, answer="accept")))

    def test_owner_questions_need_two_options_but_grants_do_not(self):
        self.assertIn("options: an owner question needs at least two options with their cost",
                      reg.validate_decision(with_(DECISION, options=[DECISION["options"][0]])))
        grant = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": []})
        self.assertEqual([], reg.validate_decision(grant))
        self.assertIn("question: a merge-grant question is 'grant:<plan>'",
                      reg.validate_decision(with_(grant, question="may we merge?")))


class ReviewRound1(unittest.TestCase):
    def test_trailing_newline_is_rejected_in_every_id_and_sha(self):
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_finding(with_(FINDING, id="T1-PIN-1\n"))))
        self.assertTrue(any(e.startswith("ruling: ") for e in
                            reg.validate_finding(with_(FINDING, ruling="DEC-0001\n"))))
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_claim(with_(CLAIM, id="C-a-1\n"))))
        self.assertTrue(any(e.startswith("finding: ") for e in
                            reg.validate_claim(with_(CLAIM, state="false", finding="A2-IO-N4\n"))))
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_decision(with_(DECISION, id="DEC-0001\n"))))
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": "aaaaaaa\n"}])
        self.assertTrue(any(e.startswith("cites: entry 0 sha") for e in reg.validate_finding(f)))

    def test_mixed_type_unknown_keys_do_not_crash(self):
        f = with_(FINDING)
        f[1] = 1
        f["z"] = 2
        self.assertEqual(["keys: unknown z, 1"], reg.validate_finding(f))

    def test_octal_looking_sha_is_not_echoed_as_a_reconstructed_number(self):
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": 0o123456}])
        errs = reg.validate_finding(f)
        self.assertIn("cites: entry 0 sha: parsed as a number; quote the sha exactly as written in the file", errs)
        self.assertFalse(any("42798" in e for e in errs), errs)

    def test_grant_question_needs_a_plan_name(self):
        for q in ("grant:", "grant: x"):
            self.assertIn("question: a merge-grant question is 'grant:<plan>'",
                          reg.validate_decision(with_(DECISION, **{"class": "merge-grant", "question": q,
                                                                   "options": []})), q)
        self.assertEqual([], reg.validate_decision(with_(DECISION, **{
            "class": "merge-grant", "question": "grant:demo some text", "options": []})))

    def grant(self, by="superbrobenji", **kw):
        g = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": [],
                               "answer": "YES", "answered_at": {"sha": "aaaaaaa", "by": by}})
        g.update(kw)
        return g

    def test_grant_for_checks_answerer_and_validity(self):
        g = self.grant()
        self.assertIs(g, reg.grant_for([g], "demo"))                       # two-arg form, owner from config
        self.assertIsNone(reg.grant_for([self.grant(by="someone-else")], "demo"))
        other = self.grant(by="someone-else")
        self.assertIs(other, reg.grant_for([other], "demo", owner="someone-else"))
        self.assertIsNone(reg.grant_for([with_(g, extra=1)], "demo"))      # malformed row
        self.assertIsNone(reg.grant_for([with_(g, id="bad")], "demo"))


class Queries(unittest.TestCase):
    def test_finding_by_id_and_open_decisions(self):
        self.assertIs(FINDING, reg.finding_by_id([FINDING], "T1-PIN-1"))
        self.assertIsNone(reg.finding_by_id([FINDING], "T1-PIN-2"))
        answered = with_(DECISION, id="DEC-9002", answer="accept",
                         answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        self.assertEqual(["DEC-9001"], [d["id"] for d in reg.open_decisions([DECISION, answered])])

    def test_grant_needs_an_exact_plan_and_a_yes(self):
        g = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": [],
                               "answer": "yes", "answered_at": {"sha": "aaaaaaa", "by": "superbrobenji"}})
        self.assertIs(g, reg.grant_for([g], "demo"))
        self.assertIsNone(reg.grant_for([g], "dem"))
        self.assertIsNone(reg.grant_for([with_(g, question="grant:demo-2")], "demo"))
        self.assertIsNone(reg.grant_for([with_(g, answer="no")], "demo"))
        self.assertIsNone(reg.grant_for([with_(g, answer=None, answered_at=None)], "demo"))
        self.assertIs(g, reg.grant_for([DECISION, g], "demo"))


def deferred(ruling="DEC-9001"):
    return with_(FINDING, state="deferred", ruling=ruling,
                 history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                          {"state": "deferred", "sha": "aaaaaaa", "by": "a"}])


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave."""

    def answered(self, **kw):
        return with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "superbrobenji"}, **kw)

    def test_owner_is_evaluated_even_when_other_keys_are_missing(self):               # A1
        f = with_(FINDING, owner=None)
        del f["siblings"]
        self.assertEqual(["keys: missing siblings", "owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"],
                         reg.validate_finding(f))
        g = with_(FINDING, owner={"kind": "person"})
        del g["repro"]
        errs = reg.validate_finding(g)
        self.assertEqual(["keys: missing repro"], errs[:1])
        self.assertTrue(errs[1].startswith("owner: kind must be one of"), errs)
        h = with_(FINDING)
        del h["owner"]
        self.assertEqual(["keys: missing owner"], reg.validate_finding(h))

    def test_only_the_owner_answers_a_decision(self):                                  # A2
        agent = with_(DECISION, answer="yes", answered_at={"sha": "aaaaaaa", "by": "session"})
        self.assertEqual(["answered_at.by: only the owner (superbrobenji) answers a decision, got 'session'"],
                         reg.validate_decision(agent))
        self.assertEqual([], reg.validate_decision(agent, owner="session"))
        self.assertEqual([], reg.validate_decision(self.answered()))

    def test_deferred_needs_a_valid_owner_answered_unique_decision(self):             # A2
        root = tempfile.mkdtemp(prefix="gate-def-")
        self.addCleanup(gf.remove, root)
        f = deferred()
        self.assertEqual([], reg.evidence_errors(f, root, [self.answered()]))
        by_agent = with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "session"})
        errs = reg.evidence_errors(f, root, [by_agent])
        self.assertEqual(["evidence: ruling DEC-9001 is not a valid owner-answered decision "
                          "(answered_at.by: only the owner (superbrobenji) answers a decision, got 'session')"], errs)
        boolean = with_(DECISION, answer=False, answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        errs = reg.evidence_errors(f, root, [boolean])
        self.assertEqual(1, len(errs), errs)
        self.assertTrue(errs[0].startswith("evidence: ruling DEC-9001 is not a valid owner-answered decision (answer: "), errs)
        self.assertIn("quote it", errs[0])
        self.assertEqual(["evidence: ruling DEC-9001 appears more than once in decisions.yml"],
                         reg.evidence_errors(f, root, [self.answered(), self.answered()]))
        self.assertEqual(["evidence: ruling DEC-9001 is not answered by the owner"],
                         reg.evidence_errors(f, root, [DECISION]))

    def test_unicode_digits_are_not_ids(self):                                         # B8
        for bad in ("DEC-\uff10\uff10\uff10\uff11", "DEC-\u0663\u0663\u0663\u0663"):
            self.assertTrue(any(e.startswith("id: ") for e in reg.validate_decision(with_(DECISION, id=bad))), bad)
            self.assertTrue(any(e.startswith("ruling: ") for e in reg.validate_finding(with_(FINDING, ruling=bad))), bad)

    def test_sha_length_bounds(self):                                                   # B14
        for sha in ("aaaaaa", "a", "a" * 41):
            f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": sha}])
            self.assertTrue(any(e.startswith("cites: entry 0 sha") for e in reg.validate_finding(f)), sha)
        for sha in ("a" * 7, "a" * 40):
            f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": sha}])
            self.assertEqual([], reg.validate_finding(f), sha)

    def test_unquoted_dates_and_booleans_in_string_fields_get_the_quote_hint(self):  # B15
        import datetime
        for v in (datetime.date(2026, 10, 17), True, 7, 1.5):
            errs = reg.validate_finding(with_(FINDING, source=v))
            self.assertEqual(["source: must be a non-empty string (YAML read it as a date, boolean or number: quote it)"],
                             errs, v)
        self.assertEqual(["source: must be a non-empty string"], reg.validate_finding(with_(FINDING, source="")))
        self.assertEqual(["doc: must be a non-empty string (YAML read it as a date, boolean or number: quote it)"],
                         reg.validate_claim(with_(CLAIM, doc=datetime.date(2026, 10, 17))))

    def test_pinned_transcript_location_and_test_name(self):                           # B13
        root = tempfile.mkdtemp(prefix="gate-pin-")
        self.addCleanup(gf.remove, root)
        gf.write(root, "README.md", "x\n")
        gf.write(root, MUT + "T1-PIN-1.txt", "t\n")
        gf.write(root, "docs/superpowers/state/demo/review/T1.md", "r\n")
        base = with_(FINDING, state="pinned", evidence=["docs/superpowers/state/demo/review/T1.md"],
                     history=[{"state": s, "sha": "aaaaaaa", "by": "x"}
                              for s in ("open", "planned", "in-progress", "pinned")])
        ok = with_(base, pin={"test": "test_x", "transcript": MUT + "T1-PIN-1.txt"})
        self.assertEqual([], reg.evidence_errors(ok, root, []))
        self.assertEqual([], reg.evidence_errors(with_(ok, pin={"test": "test_mod::test_x", "transcript": MUT + "T1-PIN-1.txt"}), root, []))
        for where in ("README.md", "docs/superpowers/state/demo/review/T1.md", "docs/superpowers/state/mutations/T1-PIN-1.txt",
                      "docs/superpowers/state/../demo/mutations/T1-PIN-1.txt", "docs/superpowers/state/demo/mutations/a/T1-PIN-1.txt"):
            errs = reg.evidence_errors(with_(base, pin={"test": "test_x", "transcript": where}), root, [])
            self.assertEqual([f"evidence: pin transcript must be docs/superpowers/state/<plan>/mutations/<file>, got {where!r}"],
                             errs, where)
        for name in ("not a test", "x", "test_", "test_a::x", "test_a::", "test_a b", "test_a::test_b::test_c", "test_x\n"):
            errs = reg.evidence_errors(with_(ok, pin={"test": name, "transcript": MUT + "T1-PIN-1.txt"}), root, [])
            self.assertEqual([f"evidence: pin test must be test_<name> or test_<name>::test_<name>, got {name!r}"], errs, name)

    def test_load_errors_name_the_register_relative_to_the_root(self):                # A7
        root = tempfile.mkdtemp(prefix="gate-reg-")
        self.addCleanup(gf.remove, root)
        rel = "docs/superpowers/state/findings.yml"
        with self.assertRaises(GateError) as ctx:
            reg.load_findings(root)
        self.assertEqual(f"{rel}: No such file or directory", str(ctx.exception))
        for text, msg in (("", "empty file (an empty register is the YAML list [])"),
                          ("id: A-1\n", "top level must be a YAML list"), ("- A-1\n", "entry 0 is not a mapping")):
            gf.write(root, rel, text)
            with self.assertRaises(GateError) as ctx:
                reg.load_findings(root)
            self.assertEqual(f"{rel}: {msg}", str(ctx.exception))

    def commits(self):
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        return root, gf.git(root, "rev-parse", "--short=7", "HEAD")

    def test_every_sha_field_must_name_a_commit(self):                                 # A8
        root, real = self.commits()
        fake = "0000000"
        msg = "{}: sha 0000000 is not a commit in this repository"
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": real}, {"file": "b.c", "lines": "1", "sha": fake}],
                  evidence=[real, fake, "README.md"],
                  history=[{"state": "open", "sha": real, "by": "a"}])
        self.assertEqual([msg.format("cites[1].sha"), msg.format("evidence[1]")], reg.finding_commit_errors(f, root))
        g = with_(f, cites=[], evidence=[], history=[{"state": "open", "sha": fake, "by": "a"}])
        self.assertEqual([msg.format("history[0].sha")], reg.finding_commit_errors(g, root))
        d = with_(DECISION, asked_at={"sha": fake, "by": "session"}, answer="x",
                  answered_at={"sha": fake, "by": "superbrobenji"})
        self.assertEqual([msg.format("asked_at.sha"), msg.format("answered_at.sha")], reg.decision_commit_errors(d, root))
        self.assertEqual([], reg.decision_commit_errors(with_(DECISION, asked_at={"sha": real, "by": "s"}), root))
        c = with_(CLAIM, verified_at=fake, by="s", state="verified")
        self.assertEqual([msg.format("verified_at")], reg.claim_commit_errors(c, root))
        self.assertEqual([], reg.claim_commit_errors(with_(c, verified_at=real), root))
        self.assertEqual([], reg.claim_commit_errors(CLAIM, root))

    def test_the_commit_lookup_is_injectable_and_a_git_failure_propagates(self):      # A8
        seen = []
        f = with_(FINDING, cites=[], history=[{"state": "open", "sha": "abcdef1", "by": "a"}], evidence=["abcdef1"])
        self.assertEqual([], reg.finding_commit_errors(f, "r", lambda root, sha: seen.append((root, sha)) or True))
        self.assertEqual([("r", "abcdef1"), ("r", "abcdef1")], seen)
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        self.addCleanup(gf.remove, outside)
        with self.assertRaises(GateError):
            reg.finding_commit_errors(f, outside)

    def test_row_id_prints_only_a_well_formed_id(self):
        self.assertEqual("DEC-0001", reg.row_id({"id": "DEC-0001"}, 3, reg.DECISION_ID))
        for bad in ("DEC-1", "DEC-0001\nPASS x", 5, None, ["DEC-0001"]):
            self.assertEqual("entry-3", reg.row_id({"id": bad}, 3, reg.DECISION_ID))
        self.assertEqual("entry-0", reg.row_id({}, 0, reg.DECISION_ID))

    def test_memo_commit_exists_asks_git_once_per_distinct_sha(self):
        calls = []
        real = reg.gitinfo.commit_exists
        reg.gitinfo.commit_exists = lambda root, sha: calls.append((root, sha)) or sha == "abcdef1"
        try:
            exists = reg.memo_commit_exists()
            self.assertEqual([True, True, False, False, True],
                             [exists("r", "abcdef1"), exists("r", "abcdef1"), exists("r", "1234abc"),
                              exists("r", "1234abc"), exists("r2", "abcdef1")])
            self.assertEqual([("r", "abcdef1"), ("r", "1234abc"), ("r2", "abcdef1")], calls)
        finally:
            reg.gitinfo.commit_exists = real

    def test_owner_answer_error_prefix_is_what_validate_decision_says(self):
        d = copy.deepcopy(DECISION)
        d["answered_at"] = {"sha": "abcdef1", "by": "session"}
        d["answer"] = "yes"
        errs = reg.validate_decision(d, owner="boss")
        self.assertEqual(1, len(errs), errs)
        self.assertTrue(errs[0].startswith(reg.OWNER_ANSWER_ERROR + " (boss)"), errs)

    def test_safe_text_escapes_control_characters(self):
        self.assertEqual("docs/a.md", reg.safe_text("docs/a.md"))
        self.assertEqual("a\\nPASS x", reg.safe_text("a\nPASS x"))

    def test_require_regular_accepts_files_and_names_everything_else(self):
        root = tempfile.mkdtemp(prefix="gate-reg-")
        self.addCleanup(gf.remove, root)
        gf.write(root, "d/f.md", "x\n")
        reg.require_regular(root, os.path.join(root, "d", "f.md"))
        for rel, msg in (("d", "d: not a regular file"), ("d/none.md", "d/none.md: No such file or directory")):
            with self.assertRaises(GateError) as cm:
                reg.require_regular(root, os.path.join(root, rel))
            self.assertEqual(msg, str(cm.exception))
        if hasattr(os, "mkfifo"):
            os.mkfifo(os.path.join(root, "pipe"))
            with self.assertRaises(GateError) as cm:
                reg.require_regular(root, os.path.join(root, "pipe"))
            self.assertEqual("pipe: not a regular file", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
