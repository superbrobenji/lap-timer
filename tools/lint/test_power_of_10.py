#!/usr/bin/env python3
"""Unit tests for tools/lint/power_of_10.py -- one fixture per gap the 2026-10-13 audit proved
(drv:LG-1..LG-7). Run from the repo root: python3 -m unittest tools/lint/test_power_of_10.py
"""
import io
import contextlib
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import power_of_10 as p10

FIX = "tools/lint/fixtures"
NONE = FIX + "/register_none.md"
SINKS = FIX + "/register_sinks.md"


def run(args):
    """(rc, stdout) of one linter invocation, from the repo root."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = p10.main(args)
    return rc, buf.getvalue()


def findings(out):
    return [l for l in out.splitlines() if ": RULE-" in l]


class Rule3(unittest.TestCase):
    def test_lg1_heap_only_file_fails_the_build(self):
        rc, out = run(["--paths", FIX + "/lg1_heap_only.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertEqual(2, len(findings(out)), out)
        self.assertEqual(1, rc)

    def test_lg2_every_heap_api_is_seen_and_the_query_apis_are_not(self):
        rc, out = run(["--paths", FIX + "/lg2_heap_apis.c", "--register", NONE,
                       "--fail-on-violation"])
        got = [l.split("'")[1] for l in findings(out)]
        self.assertEqual(["heap_caps_malloc(", "heap_caps_free(", "pvPortMalloc(", "vPortFree(",
                          "strndup(", "asprintf("], got)
        self.assertNotIn("free_size", out)
        self.assertEqual(1, rc)


class Rule9Sinks(unittest.TestCase):
    def test_lg3_sink_and_stored_value_are_findings(self):
        rc, out = run(["--paths", FIX + "/lg3_sinks.c", "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation"])
        self.assertIn("unregistered function 'cmpi' passed to qsort()", out)
        self.assertIn("unregistered function 'pre_cb' stored as a value", out)
        self.assertEqual(4, len(findings(out)), out)   # + the two (*name)( declarator sites
        self.assertEqual(1, rc)

    def test_lg3_registered_sites_are_silent(self):
        rc, out = run(["--paths", FIX + "/lg3_sinks.c", "--register", SINKS, "--enforce-fnptr",
                       "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)


class Rule5Counting(unittest.TestCase):
    def test_lg4_asserts_inside_if0_do_not_count_but_the_else_branch_does(self):
        rc, out = run(["--paths", FIX + "/lg4_if0_asserts.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'dead_asserts' has 0 assertion(s) in 23 code lines", out)
        self.assertNotIn("live_asserts", out)
        self.assertEqual(1, rc)

    def test_lg5_statements_not_physical_lines(self):
        rc, out = run(["--paths", FIX + "/lg5_oneliners.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'packed' has 0 assertion(s) in 22 code lines", out)
        self.assertEqual(1, rc)

    def test_lg7_unbalanced_brace_in_if0_does_not_hide_later_functions(self):
        rc, out = run(["--paths", FIX + "/lg7_if0_brace.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'over_twenty' has 0 assertion(s) in 23 code lines", out)
        self.assertEqual(1, rc)


class Rule1Recursion(unittest.TestCase):
    def test_lg6_direct_recursion_is_a_finding(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'walk' calls itself (rule 1: no recursion)", out)
        self.assertNotIn("iterative", out)
        self.assertEqual(1, rc)


class Rollout(unittest.TestCase):
    def test_report_only_rules_reports_without_failing(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-rules", "RULE-1"])
        self.assertIn("[report-only]", out)
        self.assertEqual(0, rc)

    def test_report_only_paths_reports_without_failing(self):
        rc, out = run(["--paths", FIX, "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation", "--report-only-paths", FIX + "/"])
        self.assertIn("[report-only]", out)
        self.assertEqual(0, rc)

    def test_report_only_site_is_graced_and_a_stale_entry_is_announced(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-sites",
                       FIX + "/lg6_recursion.c:walk:RULE-1," + FIX + "/lg6_recursion.c:gone:RULE-1"])
        self.assertIn("GRACE-STALE: " + FIX + "/lg6_recursion.c:gone:RULE-1", out)
        self.assertEqual(0, rc)


class GraceVisibility(unittest.TestCase):
    """Fix round 1, Important finding: site-/rule-based grace must be baked into findings[]
    before either renderer runs, so it shows up in text mode's [report-only] marker AND in
    --json's report_only field -- not only in the exit code."""

    def test_site_graced_finding_is_marked_report_only_in_text_mode_and_does_not_block(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-sites",
                       FIX + "/lg6_recursion.c:walk:RULE-1"])
        self.assertIn(
            "RULE-1: function 'walk' calls itself (rule 1: no recursion) [report-only]", out)
        self.assertEqual(0, rc)

    def test_site_graced_finding_carries_report_only_true_in_json(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-sites",
                       FIX + "/lg6_recursion.c:walk:RULE-1", "--json"])
        data = json.loads(out)
        rule1 = [f for f in data["findings"] if f["rule"] == "RULE-1"]
        self.assertEqual(1, len(rule1), data["findings"])
        self.assertIs(True, rule1[0].get("report_only"))
        self.assertEqual(0, rc)

    def test_rule_graced_finding_carries_report_only_true_in_json(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-rules", "RULE-1", "--json"])
        data = json.loads(out)
        rule1 = [f for f in data["findings"] if f["rule"] == "RULE-1"]
        self.assertEqual(1, len(rule1), data["findings"])
        self.assertIs(True, rule1[0].get("report_only"))
        self.assertEqual(0, rc)

    def test_ungraced_blocking_finding_still_fails_the_build(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("RULE-1: function 'walk' calls itself (rule 1: no recursion)", out)
        self.assertNotIn("[report-only]", out)
        self.assertEqual(1, rc)


if __name__ == "__main__":
    unittest.main()
