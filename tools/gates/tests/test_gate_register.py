#!/usr/bin/env python3
"""Tests for tools/gates/gate_register.py through main() on fixture trees (the production path).
Run from the repo root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import re
import signal
import tempfile
import unittest

from tools.gates import gate_register
from tools.gates.tests import gitfixture as gf

FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "register")


def fixture_repo(case):
    """A throwaway git repo holding the fixture tree `case`. The fixtures write their shas as
    aaaaaaa / bbbbbbb; here those become two real commits of the repo (DEC-0003)."""
    root = gf.new_repo({"findings": None, "claims": None, "decisions": None})
    first = gf.git(root, "rev-parse", "--short=7", "HEAD")
    gf.write(root, "second.txt", "second\n")
    second = gf.commit_all(root)[:7]
    top = os.path.join(FIX, case)
    for dirpath, _, names in os.walk(top):
        for name in names:
            with open(os.path.join(dirpath, name), encoding="utf-8") as fh:
                text = fh.read()
            gf.write(root, os.path.relpath(os.path.join(dirpath, name), top),
                     text.replace("aaaaaaa", first).replace("bbbbbbb", second))
    gf.git(root, "add", "-A")      # staged: an evidence file must be tracked (b3 F1)
    return root


def main_out(root, *args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = gate_register.main(list(args) if args else ["--root", root])
    return rc, buf.getvalue()


def report_repo():
    """An empty-register git repo that has the required docs/reviews directory."""
    root = gf.new_repo()
    os.makedirs(os.path.join(root, "docs", "reviews"))
    return root


def run(case, *lead):
    """main() on a fixture tree; `lead` goes before --root (--json is recognised only as argv[0])."""
    root = fixture_repo(case)
    try:
        return main_out(root, *lead, "--root", root)
    finally:
        gf.remove(root)


class GateRegister(unittest.TestCase):
    def test_valid_register_passes_every_category(self):
        rc, out = run("valid")
        self.assertEqual("PASS register.schema\nPASS register.owner\nPASS register.duplicate\n"
                         "PASS register.transition\nPASS register.evidence\nPASS register.report\n", out)
        self.assertEqual(0, rc)

    def test_illegal_transition_fails(self):
        rc, out = run("illegal_transition")
        self.assertIn("FAIL register.transition.T1-JUMP-1: transition: open -> pinned is illegal", out)
        self.assertEqual(1, rc)

    def test_pinned_without_transcript_fails(self):
        rc, out = run("pinned_no_transcript")
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pin transcript "
                      "docs/superpowers/state/demo/mutations/T1-NOPIN-1.txt does not exist", out)
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pinned needs the reviewer's "
                      "confirming report path in evidence", out)
        self.assertEqual(1, rc)

    def test_missing_owner_fails(self):
        rc, out = run("missing_owner")
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: owner: missing", out)
        self.assertNotIn("PASS register.owner", out)
        self.assertEqual(1, rc)

    def test_missing_file_is_a_fail_input(self):
        rc, out = run("missing_file")
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL register.input: "), out)
        self.assertIn("findings.yml: No such file or directory", out)

    def test_registered_report_with_an_unknown_id_fails(self):
        rc, out = run("report_unknown")
        self.assertEqual("FAIL register.report.docs/superpowers/state/briefs/b1-brief.md.T9-NOPE-1: "
                         "not in register\n", out.split("PASS")[0])
        self.assertEqual(1, rc)

    def test_unmarked_report_is_ignored_and_marked_report_with_known_ids_passes(self):
        rc, out = run("valid")      # valid/docs/reviews: one marked (all ids known), one unmarked
        self.assertNotIn("T9-NOPE", out)
        self.assertNotIn("T9-PROSE", out)
        self.assertIn("PASS register.report", out)
        self.assertEqual(0, rc)

    def test_report_ids_reads_table_cells_and_list_items(self):
        path = os.path.join(FIX, "valid", "docs", "reviews", "2026-01-01-registered.md")
        self.assertEqual(["T1-OPEN-1", "T1-DEF-1", "T1-PIN-1"],
                         gate_register.report_ids(path, os.path.join(FIX, "valid")))

    def test_report_ids_ignores_what_is_not_a_finding_id(self):
        root = gf.new_repo()
        try:
            path = gf.write(root, "r.md", "register: true\n| t1-lower-1 | x |\n| T1-OPEN-1x y | x |\n"
                            "- t1-lower-1: no\n- Note: prose\n- T1-OPEN-1x\u00e9 y\n1. T1-DEF-1 numbered\n"
                            "- T1-FW-\uff11 full-width digit\n-T1-NOSPACE-1: no bullet space\n"
                            "| `T1-A-1 | unbalanced\n| ** T1-B-1** | spaces inside the wrapper\n")
            self.assertEqual(["T1-DEF-1"], gate_register.report_ids(path, root))
        finally:
            gf.remove(root)

    def test_report_ids_accepts_every_dec_0002_form(self):
        root = gf.new_repo()
        try:
            forms = ["- T1-A-1 space", "- T1-A-2: colon", "- T1-A-3, comma", "- T1-A-4. period", "- T1-A-5\ttab",
                     "- T1-A-6", "- **T1-A-7** bold", "- **T1-A-8**: bold colon", "- `T1-A-9` tick",
                     "- `T1-A-10`, tick comma", "- [ ] T1-A-11: open box", "- [x] T1-A-12 done box",
                     "- [X] **T1-A-13**: both", "* T1-A-14 star", "+ T1-A-15 plus", "1. T1-A-16 num", "2) T1-A-17 paren",
                     "  - T1-A-18 nested", "- T1-A-19\r", "| **T1-A-20** | bold cell |", "| `T1-A-21` | tick cell |",
                     "| T1-A-22 | plain cell |"]
            text = "register: true\n" + "\n".join(forms) + "\n"
            path = gf.write(root, "r.md", text)
            self.assertEqual([f"T1-A-{n}" for n in range(1, 23)], gate_register.report_ids(path, root))
            bare = gf.write(root, "b.md", "register: true\n- T1-B-1\n- T1-B-2")      # no newline at the end of the file
            self.assertEqual(["T1-B-1", "T1-B-2"], gate_register.report_ids(bare, root))
            crlf = gf.write(root, "c.md", "register: true\r\n- T1-C-1\r\n- **T1-C-2**\r\n")
            self.assertEqual(["T1-C-1", "T1-C-2"], gate_register.report_ids(crlf, root))
        finally:
            gf.remove(root)

    def test_report_ids_needs_the_root(self):
        with self.assertRaises(TypeError):
            gate_register.report_ids("x.md")

    def test_unreadable_report_subdirectory_fails_closed(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores directory permissions")
        root = gf.new_repo()
        locked = os.path.join(root, "docs", "reviews", "locked")
        try:
            gf.write(root, os.path.join("docs", "reviews", "locked", "x.md"), "register: true\n")
            os.chmod(locked, 0)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.input: ", buf.getvalue())
            self.assertEqual("FAIL register.input: docs/reviews/locked: Permission denied\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            if os.path.exists(locked):
                os.chmod(locked, 0o755)
            gf.remove(root)

    def test_report_path_that_is_a_file_fails_closed(self):
        root = gf.new_repo()
        try:
            gf.write(root, os.path.join("docs", "reviews"), "not a directory\n")
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.input: ", buf.getvalue())
            self.assertEqual("FAIL register.input: docs/reviews: not a directory\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_symlinked_report_directory_is_followed_and_cycles_stop(self):
        root = report_repo()
        try:
            briefs = os.path.join(root, "docs", "superpowers", "state", "briefs")
            gf.write(root, os.path.join("docs", "superpowers", "state", "real", "b.md"),
                     "register: true\n- T9-NOPE-1: x\n")
            os.makedirs(briefs)
            os.symlink(os.path.join(root, "docs", "superpowers", "state", "real"), os.path.join(briefs, "link"))
            os.symlink(briefs, os.path.join(briefs, "loop"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.report.docs/superpowers/state/briefs/link/b.md.T9-NOPE-1: "
                          "not in register", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_unreadable_report_file_fails_with_a_relative_path(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores file permissions")
        root = gf.new_repo()
        path = os.path.join(root, "docs", "reviews", "r.md")
        try:
            gf.write(root, os.path.join("docs", "reviews", "r.md"), "register: true\n- T1-X-1: x\n")
            os.chmod(path, 0)
            with self.assertRaises(gate_register.GateError) as ctx:
                gate_register.report_ids(path, root)
            self.assertEqual("docs/reviews/r.md: Permission denied", str(ctx.exception))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual("FAIL register.input: docs/reviews/r.md: Permission denied\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            if os.path.exists(path):
                os.chmod(path, 0o644)
            gf.remove(root)

    def test_top_directory_reached_through_a_symlink_is_reported_once(self):
        root = gf.new_repo()
        try:
            briefs = os.path.join(root, "docs", "superpowers", "state", "briefs")
            gf.write(root, os.path.join("docs", "superpowers", "state", "briefs", "b.md"),
                     "register: true\n- T9-NOPE-1: x\n")
            os.symlink(briefs, os.path.join(briefs, "loop"))
            os.symlink(briefs, os.path.join(root, "docs", "reviews"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual(1, buf.getvalue().count("T9-NOPE-1: not in register"), buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_dangling_symlink_in_a_report_directory_fails_with_a_relative_path(self):
        root = gf.new_repo()
        try:
            os.makedirs(os.path.join(root, "docs", "reviews"))
            os.symlink(os.path.join(root, "nowhere.md"), os.path.join(root, "docs", "reviews", "dangling.md"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual("FAIL register.input: docs/reviews/dangling.md: No such file or directory\n",
                             buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_duplicate_ids_fail(self):
        root = report_repo()
        try:
            with open(os.path.join(FIX, "missing_owner", gf.STATE, "findings.yml")) as fh:
                row = fh.read().replace("owner: null", "owner: {kind: issue, number: 2}")
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), row + row)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.duplicate.T1-ORPHAN-1: id appears more than once", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_json_output(self):
        rc, out = run("missing_owner", "--json")
        self.assertIn('"ok": false', out)
        self.assertEqual(1, rc)

    def test_the_repository_register_passes(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_register.main(["--root", gf.REPO])
        self.assertEqual(0, rc, buf.getvalue())


ORPHAN = os.path.join(FIX, "missing_owner", gf.STATE, "findings.yml")
NO_OWNER_MSG = "owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"


def findings_text(**edits):
    """The missing_owner fixture row with `key: value` lines replaced (value None deletes the line)."""
    with open(ORPHAN, encoding="utf-8") as fh:
        lines = fh.read().splitlines(keepends=True)
    out = []
    for line in lines:
        lead = "- " if line.startswith("- ") else "  "
        key = line.strip().split(":")[0].lstrip("- ")
        if key in edits and (line.startswith("- ") or line.startswith("  " + key + ":")):
            if edits[key] is not None:
                out.append(f"{lead}{key}: {edits[key]}\n")
            elif lead == "- ":
                raise AssertionError("cannot delete the first line")
            continue
        out.append(line)
    return "".join(out)


def gate_on(registers=None, files=None):
    """main() on a fresh repo (empty registers unless given) plus extra files; returns (rc, out, root)."""
    root = report_repo()
    for name, text in (registers or {}).items():
        gf.write(root, os.path.join(gf.STATE, f"{name}.yml"), text)
    for rel, text in (files or {}).items():
        gf.write(root, rel, text)
    gf.commit_all(root)
    rc, out = main_out(root)
    return rc, out, root


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave, every case through main()."""

    def gate(self, **kw):
        rc, out, root = gate_on(**kw)
        self.addCleanup(gf.remove, root)
        return rc, out, root

    # A1 -- a category PASS needs every row evaluated and none failed
    def test_a_row_without_an_owner_key_fails_owner_and_never_passes_it(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner=None)})
        self.assertEqual("FAIL register.owner.T1-ORPHAN-1: keys: missing owner\n"
                         "PASS register.schema\nPASS register.duplicate\nPASS register.report\n", out)
        self.assertEqual(1, rc)

    def test_null_owner_plus_another_missing_key_fails_both_categories(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner="null", siblings=None)})
        self.assertEqual(f"FAIL register.schema.T1-ORPHAN-1: keys: missing siblings\n"
                         f"FAIL register.owner.T1-ORPHAN-1: {NO_OWNER_MSG}\n"
                         "PASS register.duplicate\nPASS register.report\n", out)
        self.assertEqual(1, rc)

    def test_owner_and_other_keys_missing_together_fails_both_categories(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner=None, siblings=None)})
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: keys: missing siblings, owner", out)
        self.assertIn("FAIL register.schema.T1-ORPHAN-1: keys: missing siblings, owner", out)
        self.assertNotIn("PASS register.owner", out)
        self.assertNotIn("PASS register.schema", out)

    def test_categories_whose_rows_were_skipped_never_pass(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(severity="SEVERE", owner="{kind: issue, number: 2}")})
        self.assertIn("FAIL register.schema.T1-ORPHAN-1: severity:", out)
        for cat in ("schema", "transition", "evidence"):
            self.assertNotIn(f"PASS register.{cat}", out)
        self.assertIn("PASS register.owner", out)
        rc, out, _ = self.gate(registers={"decisions": "- {id: DEC-0001, class: triage}\n"})
        self.assertIn("FAIL register.schema.DEC-0001: keys: missing ", out)
        self.assertNotIn("PASS register.schema", out)
        self.assertNotIn("PASS register.evidence", out)
        self.assertIn("PASS register.transition", out)

    # A2 -- deferred needs a valid, unique, owner-answered decision
    def deferred_gate(self, decisions):
        row = findings_text(owner="{kind: issue, number: 2}", state="deferred", ruling="DEC-9001").replace(
            "history: [{state: open, sha: \"aaaaaaa\", by: audit}]",
            "history: [{state: open, sha: \"aaaaaaa\", by: audit}, {state: deferred, sha: \"aaaaaaa\", by: s}]")
        root = report_repo()
        self.addCleanup(gf.remove, root)
        first = gf.git(root, "rev-parse", "--short=7", "HEAD")
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), row.replace("aaaaaaa", first))
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), decisions.replace("aaaaaaa", first))
        gf.commit_all(root)
        return main_out(root)

    DECISION = ('- id: DEC-9001\n  class: scope\n  question: "defer it?"\n'
                '  options: [{label: a, cost: x}, {label: b, cost: y}]\n'
                '  asked_at: {sha: "aaaaaaa", by: session}\n  answer: ANSWER\n  answered_at: AT\n')

    def decision(self, answer='"defer"', at='{sha: "aaaaaaa", by: superbrobenji}'):
        return self.DECISION.replace("ANSWER", answer).replace("AT", at)

    def test_deferred_on_an_owner_answered_decision_passes(self):
        rc, out = self.deferred_gate(self.decision())
        self.assertEqual(0, rc, out)

    def test_deferred_on_a_decision_the_agent_answered_fails(self):
        rc, out = self.deferred_gate(self.decision(at='{sha: "aaaaaaa", by: session}'))
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.schema.DEC-9001: answered_at.by: only the owner (superbrobenji) answers a decision, got 'session'", out)
        self.assertIn("FAIL register.evidence.T1-ORPHAN-1: evidence: ruling DEC-9001 is not a valid owner-answered decision", out)

    def test_deferred_on_a_boolean_answer_fails(self):
        rc, out = self.deferred_gate(self.decision(answer="no"))
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.schema.DEC-9001: answer: must be null or a non-empty string, got False", out)
        self.assertIn("evidence: ruling DEC-9001 is not a valid owner-answered decision (answer:", out)

    def test_duplicate_decision_ids_fail(self):
        rc, out = self.deferred_gate(self.decision() + self.decision())
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.duplicate.DEC-9001: id appears more than once", out)
        self.assertIn("evidence: ruling DEC-9001 appears more than once in decisions.yml", out)

    # A3
    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            self.assertEqual((1, "FAIL register.input: --root is empty\n"), main_out(None, "--root", root))

    # A4 -- marker near misses
    def report(self, first_line, body="- T9-NOPE-1: x\n"):
        return self.gate(files={"docs/reviews/r.md": first_line + "\n" + body})

    def test_exact_marker_registers_the_report(self):
        for marker in ("register: true", "register: true\r"):
            rc, out, _ = self.report(marker)
            self.assertEqual(1, rc)
            self.assertIn("FAIL register.report.docs/reviews/r.md.T9-NOPE-1: not in register", out, marker)
            self.assertNotIn("register.marker", out)

    def test_near_miss_markers_fail_instead_of_unregistering_the_report(self):
        for marker in ("register: yes", "register: True", "Register: true", "register:true", "register: TRUE",
                       "register: on", "register: y", "register: 1", "REGISTER : Y", "  register: true",
                       "register: true  ", "register:\ttrue", "\ufeffregister: true",
                       'register: "true"', "register: 'true'", "register: true # x", "register: true.",
                       "register:  true", "register: false", "register: maybe", "register: true now", "register:"):
            rc, out, _ = self.report(marker)
            self.assertEqual(1, rc, marker)
            self.assertEqual(f"FAIL register.marker.docs/reviews/r.md: marker must be exactly 'register: true' "
                             f"(found {marker.rstrip(chr(13))!r})\n"
                             "PASS register.schema\nPASS register.owner\nPASS register.duplicate\n"
                             "PASS register.transition\nPASS register.evidence\n", out, marker)

    def test_two_space_marker_shows_both_spaces(self):
        rc, out, _ = self.report("register:  true")
        self.assertIn("(found 'register:  true')", out)

    def test_lines_that_are_not_marker_attempts_are_ignored(self):
        for marker in ("registered: true", "# register: true", "the register: true", "- register: true"):
            rc, out, _ = self.report(marker)
            self.assertEqual(0, rc, marker)
        late = "\n" * 20 + "register: yes\n- T9-NOPE-1: x\n"       # beyond the first 20 lines
        rc, out, _ = self.gate(files={"docs/reviews/late.md": late})
        self.assertEqual(0, rc, out)

    def test_a_filename_with_control_characters_cannot_split_a_line(self):
        rc, out, _ = self.gate(files={"docs/reviews/a\nPASS b.md": "register: yes\n",
                                      "docs/reviews/c\nPASS d.md": "register: true\n- T9-NOPE-1: x\n"})
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.marker.docs/reviews/a\\nPASS b.md: marker must be exactly", out)
        self.assertIn("FAIL register.report.docs/reviews/c\\nPASS d.md.T9-NOPE-1: not in register", out)
        self.assertEqual([], [l for l in out.splitlines() if l.startswith("PASS") and "register." not in l])
        self.assertEqual(["PASS register.schema", "PASS register.owner", "PASS register.duplicate",
                          "PASS register.transition", "PASS register.evidence"],
                         [l for l in out.splitlines() if l.startswith("PASS")])

    def test_a_shallow_clone_fails_the_sha_check_by_design(self):
        import subprocess
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        old = gf.git(root, "rev-parse", "--short=7", "HEAD")
        gf.write(root, os.path.join(gf.STATE, "findings.yml"),
                 findings_text(owner="{kind: issue, number: 2}").replace("aaaaaaa", old))
        gf.write(root, "docs/reviews/.gitkeep", "")
        gf.commit_all(root)
        self.assertEqual((0, ), (main_out(root)[0], ))
        parent = tempfile.mkdtemp(prefix="gate-clone-")
        self.addCleanup(gf.remove, parent)
        gf.git(parent, "clone", "-q", "--depth", "1", "file://" + root, "shallow")
        rc, out = main_out(os.path.join(parent, "shallow"))
        self.assertEqual(1, rc)
        self.assertIn(f"FAIL register.evidence.T1-ORPHAN-1: history[0].sha: sha {old} is not a commit in this repository", out)

    # A7 / A8 -- machine-independent texts, real commits
    def test_load_failures_name_files_relative_to_the_root(self):
        root = report_repo()
        self.addCleanup(gf.remove, root)
        os.remove(os.path.join(root, gf.STATE, "findings.yml"))
        self.assertEqual((1, "FAIL register.input: docs/superpowers/state/findings.yml: No such file or directory\n"),
                         main_out(root))
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), "- id: [\n")
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "[]\n")
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertEqual(1, out.count("\n"), out)                       # one physical line
        self.assertTrue(out.startswith("FAIL register.input: docs/superpowers/state/findings.yml: invalid YAML: "), out)
        self.assertIn('in "docs/superpowers/state/findings.yml", line', out)
        self.assertNotIn(root, out)
        for text, msg in (("", "empty file (an empty register is the YAML list [])"), ("id: x\n", "top level must be a YAML list"),
                          ("- x\n", "entry 0 is not a mapping")):
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), text)
            self.assertEqual((1, f"FAIL register.input: docs/superpowers/state/findings.yml: {msg}\n"), main_out(root))
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), "[]\n")
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "- {id: DEC-0001, id: DEC-0002}\n")
        rc, out = main_out(root)
        self.assertTrue(out.startswith("FAIL register.input: docs/superpowers/state/decisions.yml: invalid YAML: "), out)
        self.assertNotIn(root, out)

    def test_a_root_that_is_not_a_git_repository_is_a_fail_without_the_path(self):
        root = tempfile.mkdtemp(prefix="gate-plain-")
        self.addCleanup(gf.remove, root)
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), findings_text(owner="{kind: issue, number: 2}"))
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "[]\n")
        os.makedirs(os.path.join(root, "docs", "reviews"))
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL register.input: git cat-file -e aaaaaaa^{commit}: "), out)
        self.assertNotIn(root, out)

    def test_every_sha_in_findings_and_decisions_must_be_a_commit(self):
        row = findings_text(owner="{kind: issue, number: 2}", evidence='["0000000"]',
                            cites='[{file: a.c, lines: "1", sha: "0000000"}]').replace('sha: "aaaaaaa"', 'sha: "0000000"')
        dec = ('- id: DEC-9001\n  class: scope\n  question: "q?"\n  options: [{label: a, cost: x}, {label: b, cost: y}]\n'
               '  asked_at: {sha: "0000000", by: session}\n  answer: "a"\n  answered_at: {sha: "0000000", by: superbrobenji}\n')
        rc, out, _ = self.gate(registers={"findings": row, "decisions": dec})
        self.assertEqual(1, rc)
        gone = "sha 0000000 is not a commit in this repository"
        for fid, field in (("T1-ORPHAN-1", "history[0].sha"), ("T1-ORPHAN-1", "cites[0].sha"),
                           ("T1-ORPHAN-1", "evidence[0]"), ("DEC-9001", "asked_at.sha"), ("DEC-9001", "answered_at.sha")):
            self.assertIn(f"FAIL register.evidence.{fid}: {field}: {gone}\n", out)
        self.assertNotIn("PASS register.evidence", out)

    def test_the_repository_registers_name_commits_of_this_repository(self):
        rc, out = main_out(gf.REPO)
        self.assertEqual(0, rc, out)

    # B6
    def test_a_malformed_id_cannot_forge_or_split_a_result_line(self):
        evil = findings_text(owner="{kind: issue, number: 2}", id='"T1-A-1\\nPASS register.forged"')
        rc, out, _ = self.gate(registers={"findings": evil})
        self.assertEqual(1, rc)
        self.assertEqual(["FAIL register.schema.entry-0: id: 'T1-A-1\\nPASS register.forged' does not match "
                          + "^[A-Z0-9]+(-[A-Za-z0-9]+)+$"], [l for l in out.splitlines() if l.startswith("FAIL")])
        self.assertEqual([], [l for l in out.splitlines() if "forged" in l and not l.startswith("FAIL")])
        self.assertEqual(4, len(out.splitlines()), out)             # one FAIL + three PASS: no line was split

    # B11
    def test_a_fifo_in_a_report_directory_fails_instead_of_hanging(self):
        root = report_repo()
        self.addCleanup(gf.remove, root)
        os.mkfifo(os.path.join(root, "docs", "reviews", "pipe.md"))
        old = signal.signal(signal.SIGALRM, lambda *a: (_ for _ in ()).throw(AssertionError("gate hung on a FIFO")))
        signal.alarm(10)
        try:
            rc, out = main_out(root)
            self.assertEqual((1, "FAIL register.input: docs/reviews/pipe.md: not a regular file\n"), (rc, out))
            with self.assertRaises(gate_register.GateError):
                gate_register.report_ids(os.path.join(root, "docs", "reviews", "pipe.md"), root)
        finally:
            signal.alarm(0)
            signal.signal(signal.SIGALRM, old)

    # B12
    def test_docs_reviews_must_exist_but_briefs_may_be_absent(self):
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        self.assertEqual((1, "FAIL register.input: docs/reviews: missing report directory\n"), main_out(root))
        os.makedirs(os.path.join(root, "docs", "reviews"))
        rc, out = main_out(root)
        self.assertEqual(0, rc, out)
        self.assertFalse(os.path.exists(os.path.join(root, "docs", "superpowers", "state", "briefs")))

    # b3 F1 -- an evidence path must be a file git tracks: an untracked or ignored one fails
    def test_an_evidence_file_must_be_tracked(self):
        row = findings_text(owner="{kind: issue, number: 2}", evidence="[notes/r.md]")
        rc, out, root = self.gate(registers={"findings": row}, files={".gitignore": "ignored/\n"})
        self.assertEqual(1, rc)
        missing = "FAIL register.evidence.T1-ORPHAN-1: evidence: 'notes/r.md' is neither a commit sha nor a file tracked at the repo root\n"
        self.assertIn(missing, out)
        row = row.replace("aaaaaaa", gf.git(root, "rev-parse", "--short=7", "HEAD"))   # a commit of this repo
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), row)
        gf.write(root, "notes/r.md", "untracked\n")
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertIn(missing, out)
        self.assertNotIn("PASS register.evidence", out)
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), row.replace("notes/r.md", "ignored/r.md"))
        gf.write(root, "ignored/r.md", "ignored\n")
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertIn(missing.replace("notes/r.md", "ignored/r.md"), out)
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), row)
        gf.commit_all(root)
        rc, out = main_out(root)
        self.assertEqual(0, rc, out)
        self.assertIn("PASS register.evidence\n", out)


if __name__ == "__main__":
    unittest.main()
