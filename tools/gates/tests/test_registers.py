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
        self.assertEqual(["DEC-0001", "DEC-0002"], [d["id"] for d in decisions])
        self.assertEqual([[], []], [reg.validate_decision(d) for d in decisions])
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
        gf.write(self.root, "st/demo/mutations/T1-PIN-1.txt", "finding: T1-PIN-1\n")
        gf.write(self.root, "st/demo/review/T1.md", "status: DONE\n")

    def tearDown(self):
        gf.remove(self.root)

    def pinned(self, **kw):
        f = with_(FINDING, state="pinned",
                  pin={"test": "test_x", "transcript": "st/demo/mutations/T1-PIN-1.txt"},
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
        gone = {"test": "test_x", "transcript": "st/demo/mutations/absent.txt"}
        self.assertIn("evidence: pin transcript st/demo/mutations/absent.txt does not exist",
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
        escaped = {"test": "test_x", "transcript": "../" + os.path.basename(outside)}
        self.assertIn(f"evidence: pin transcript {escaped['transcript']} does not exist",
                      reg.evidence_errors(self.pinned(pin=escaped), self.root, []))

    def test_absolute_and_nul_evidence_paths_are_errors_not_crashes(self):
        abs_p = os.path.join(self.root, "st/demo/review/T1.md")
        self.assertIn(f"evidence: path must be relative to the repo root: {abs_p!r}",
                      reg.evidence_errors(self.pinned(evidence=[abs_p]), self.root, []))
        abs_t = {"test": "test_x", "transcript": os.path.join(self.root, "st/demo/mutations/T1-PIN-1.txt")}
        errs = reg.evidence_errors(self.pinned(pin=abs_t), self.root, [])
        self.assertIn("evidence: pin transcript: must be relative to the repo root", errs)
        self.assertFalse(any("does not exist" in e for e in errs), errs)
        nul = "st/a\0b.md"
        self.assertIn(f"evidence: {nul!r} is neither a commit sha nor a file under the repo root",
                      reg.evidence_errors(self.pinned(evidence=[nul]), self.root, []))
        bad = {"test": "test_x", "transcript": "st/a\0b.txt"}
        self.assertIn("evidence: pin transcript st/a\0b.txt does not exist",
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


if __name__ == "__main__":
    unittest.main()
