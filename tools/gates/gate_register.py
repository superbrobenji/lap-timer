#!/usr/bin/env python3
"""gate_register -- the findings register (spec §4.1, §7): schema, an owner on every row, legal
lifecycle transitions, the evidence each state needs, and every finding id cited by a registered
report (umbrella "Owner rulings" 5): a file under docs/reviews/ or docs/superpowers/state/briefs/
whose first 20 lines include the line `register: true`. In it, a table row whose first cell is a
finding id, or a list item that starts with one followed by a space or colon (the id matches
registers.FINDING_ID via fullmatch), must name a row in findings.yml. Files without the marker are
ignored; a report directory that does not exist holds no reports, one that exists but is not a
directory (or cannot be read) is a FAIL; symlinked subdirectories are followed once each.

  python3 tools/gates/gate_register.py [--root <tree>] [--json]
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

CATEGORIES = ["schema", "owner", "duplicate", "transition", "evidence", "report"]
REPORT_DIRS = [os.path.join("docs", "reviews"), os.path.join("docs", "superpowers", "state", "briefs")]
MARKER = "register: true"
MARKER_LINES = 20
LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s+([^\s:]+)(?=[ :])")


def _walk_error(exc):
    raise GateError(f"{exc.filename}: {exc.strerror or exc}")


def _first_visit(seen, path) -> bool:
    real = os.path.realpath(path)
    if real in seen:
        return False
    seen.add(real)
    return True


def registered_reports(root) -> list:
    """Files under REPORT_DIRS carrying the marker line in their first MARKER_LINES lines."""
    found = []
    seen = set()    # realpaths of directories already walked: a symlink cycle stops here
    for d in REPORT_DIRS:
        top = os.path.join(root, d)
        if not os.path.lexists(top):
            continue    # no such report directory: it holds no registered reports
        if not os.path.isdir(top):
            raise GateError(f"{top}: not a directory")
        if not _first_visit(seen, top):
            continue    # this directory was already walked through a symlink
        for dirpath, dirnames, filenames in os.walk(top, onerror=_walk_error, followlinks=True):
            dirnames.sort()
            dirnames[:] = [n for n in dirnames if _first_visit(seen, os.path.join(dirpath, n))]
            for name in sorted(filenames):
                path = os.path.join(dirpath, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as fh:
                        head = [next(fh, "") for _ in range(MARKER_LINES)]
                except OSError as exc:
                    raise GateError(f"{os.path.relpath(path, root)}: {exc.strerror or exc}") from None
                if any(l.strip() == MARKER for l in head):
                    found.append(path)
    return found


def report_ids(path) -> list:
    """Finding ids a registered report cites: table first cells and leading list-item ids."""
    ids = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            s = line.strip()
            if s.startswith("|"):
                cell = s.split("|")[1].strip()
                if registers.FINDING_ID.fullmatch(cell):
                    ids.append(cell)
                continue
            m = LIST_ITEM.match(line)
            if m and registers.FINDING_ID.fullmatch(m.group(1)):
                ids.append(m.group(1))
    return ids


def check(args: list) -> Result:
    ns = gate_args("gate_register").parse_args(args)
    root = ns.root or repo_root()
    findings = registers.load_findings(root)
    decisions = registers.load_decisions(root)
    r = Result("register")
    seen = set()
    for i, f in enumerate(findings):
        fid = f["id"] if isinstance(f.get("id"), str) else f"entry-{i}"
        if fid in seen:
            r.fail(f"register.duplicate.{fid}", "id appears more than once")
        seen.add(fid)
        errs = registers.validate_finding(f)
        for e in errs:
            r.fail(f"register.{'owner' if e.startswith('owner') else 'schema'}.{fid}", e)
        if errs:
            continue   # lifecycle rules need a well-formed row
        for e in registers.transition_errors(f):
            r.fail(f"register.transition.{fid}", e)
        for e in registers.evidence_errors(f, root, decisions):
            r.fail(f"register.evidence.{fid}", e)
    known = {f["id"] for f in findings if isinstance(f.get("id"), str)}
    for path in registered_reports(root):
        rel = os.path.relpath(path, root)
        for rid in sorted(set(report_ids(path)) - known):
            r.fail(f"register.report.{rel}.{rid}", "not in register")
    failed = {l.id.split(".")[1] for l in r.lines}
    for cat in CATEGORIES:
        if cat not in failed:
            r.passed(f"register.{cat}")
    return r


def main(argv=None):
    return run_gate("register", check, argv)


if __name__ == "__main__":
    sys.exit(main())
