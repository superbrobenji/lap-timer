#!/usr/bin/env python3
"""Unit tests for tools/lint/venue_announce.py (PR #101 review M5)."""
import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import venue_announce as va

FIX = "tools/lint/fixtures/venue_bypass.c"
NO_SITES = "tools/lint/fixtures/venue_no_sites.c"


def run(args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = va.main(args)
    return rc, buf.getvalue()


class AcquisitionPath(unittest.TestCase):
    def test_three_bypasses_are_violations_and_the_correct_shape_is_not(self):
        rc, out = run(["--paths", FIX, "--fail-on-violation"])
        self.assertIn("in sibling_branch()", out)
        self.assertIn("in announce_before()", out)
        self.assertIn("in wrong_venue()", out)
        self.assertNotIn("in correct()", out)
        self.assertIn("venue-announce: 3 violation(s)", out)
        self.assertEqual(1, rc)

    def test_the_real_tree_still_passes(self):
        rc, out = run(["--fail-on-violation"])
        self.assertIn("venue-announce: 0 violation(s)", out)
        self.assertEqual(0, rc)


class ZeroSitesFloor(unittest.TestCase):
    """The floor (main(), venue_announce.py) fails rc 1 the moment a scan finds zero
    lap_set_venue()/lap_import_rtc() call sites at all -- a self-check that the rule found
    anything to examine, not a lint violation -- and it fires regardless of --fail-on-violation."""

    def test_zero_sites_floor_fires_with_the_flag(self):
        rc, out = run(["--paths", NO_SITES, "--fail-on-violation"])
        self.assertIn("venue-announce: 0 lap_set_venue()/lap_import_rtc() call sites found", out)
        self.assertEqual(1, rc)

    def test_zero_sites_floor_fires_without_the_flag(self):
        rc, out = run(["--paths", NO_SITES])
        self.assertIn("venue-announce: 0 lap_set_venue()/lap_import_rtc() call sites found", out)
        self.assertEqual(1, rc)


if __name__ == "__main__":
    unittest.main()
