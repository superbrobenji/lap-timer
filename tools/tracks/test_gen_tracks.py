#!/usr/bin/env python3
"""Unit tests for tools/tracks/gen_tracks.py -- validation must survive `python3 -O` (drv:L24)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tracks

GOOD = {
    "id": 42, "name": "Fixture", "lat": -34.0, "lon": 18.5, "radius_m": 2000, "verified": True,
    "layouts": [{"id": 1, "name": "Full", "dir": 1,
                 "sf": [[-34.0000, 18.5000], [-34.0000, 18.5003]], "sectors": [], "length_m": 1000}],
}


def write(tmp, doc, name="v.json"):
    p = os.path.join(tmp, name)
    with open(p, "w") as fh:
        json.dump(doc, fh)
    return p


class Validation(unittest.TestCase):
    def test_good_venue_expands(self):
        self.assertEqual(42, gen_tracks.expand(json.loads(json.dumps(GOOD)))["id"])

    def test_short_sf_line_is_rejected(self):
        bad = json.loads(json.dumps(GOOD))
        bad["layouts"][0]["sf"] = [[-34.0, 18.5], [-34.0, 18.50005]]
        with self.assertRaises(gen_tracks.GenError) as e:
            gen_tracks.expand(bad)
        self.assertIn("outside 10", str(e.exception))

    def test_duplicate_layout_ids_rejected(self):
        bad = json.loads(json.dumps(GOOD))
        bad["layouts"].append(json.loads(json.dumps(bad["layouts"][0])))
        with self.assertRaises(gen_tracks.GenError):
            gen_tracks.expand(bad)

    def test_bundled_id_range_enforced(self):
        bad = json.loads(json.dumps(GOOD))
        bad["id"] = 1000
        with self.assertRaises(gen_tracks.GenError):
            gen_tracks.expand(bad)

    def test_duplicate_venue_ids_rejected_by_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = write(tmp, GOOD, "a.json")
            b = write(tmp, GOOD, "b.json")
            out = os.path.join(tmp, "out.c")
            self.assertEqual(2, gen_tracks.main([a, b, "-o", out]))

    def test_validation_survives_dash_O(self):
        """-O strips `assert`; the committed generator must still refuse a bad venue."""
        with tempfile.TemporaryDirectory() as tmp:
            bad = json.loads(json.dumps(GOOD))
            bad["layouts"][0]["sf"] = [[-34.0, 18.5], [-34.0, 18.5]]
            p = write(tmp, bad)
            out = os.path.join(tmp, "out.c")
            r = subprocess.run([sys.executable, "-O", "tools/tracks/gen_tracks.py", p, "-o", out],
                               capture_output=True, text=True)
            self.assertEqual(2, r.returncode, r.stdout + r.stderr)
            self.assertIn("degenerate", r.stdout + r.stderr)

    def test_the_committed_venues_validate(self):
        for f in ("tools/tracks/killarney.json", "tools/tracks/zwartkops.json"):
            with open(f) as fh:
                gen_tracks.expand(json.load(fh))


if __name__ == "__main__":
    unittest.main()
