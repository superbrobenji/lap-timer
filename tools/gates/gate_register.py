#!/usr/bin/env python3
"""gate_register -- the findings register (spec §4.1, §7): schema, an owner on every row, legal
lifecycle transitions, the evidence each state needs, every sha naming a commit in the gated
repository (DEC-0003), and every finding id cited by a registered report (umbrella "Owner rulings"
5, widened by DEC-0002). A registered report is a file under docs/reviews/ or
docs/superpowers/state/briefs/ with the exact line `register: true` among its first 20 lines; a
line there that is a near miss (`register: yes`, `Register: true`, `register:true`, trailing
spaces, a UTF-8 BOM before the marker ...) is a FAIL `register.marker.<file>`, never a silent
un-register. In a registered report a table row whose first cell is a finding id (optionally
wrapped in `**...**` or backticks), or a list item (`-`, `*`, `+`, `1.`, `1)`, optionally followed
by a `[ ]` / `[x]` checkbox) that starts with one (bare, `**id**` or `` `id` ``) followed by a
space, tab, colon, comma, period or the end of the line, must name a row in findings.yml (the id
matches registers.FINDING_ID via fullmatch). Files without the marker are ignored.
docs/reviews must exist (a missing one is a FAIL); docs/superpowers/state/briefs may be absent;
a report path that is not a directory, an entry that is not a regular file, or anything that
cannot be read is a FAIL; symlinked subdirectories are followed once each.

  python3 tools/gates/gate_register.py [--json] [--root <tree>]
"""
from __future__ import annotations

import os
import re
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

CATEGORIES = ["schema", "owner", "duplicate", "transition", "evidence", "report"]
REPORT_DIRS = [os.path.join("docs", "reviews"), os.path.join("docs", "superpowers", "state", "briefs")]
REQUIRED_REPORT_DIRS = [os.path.join("docs", "reviews")]
MARKER = "register: true"
MARKER_LINES = 20
# anything that reads as an attempt at the marker: it must then be the marker, exactly
MARKER_ATTEMPT = re.compile(r"^\ufeff?\s*register\s*:\s*(true|yes|on|y|1)\s*$", re.I)
LIST_ITEM = re.compile(r"^\s*(?:[-*+]|[0-9]+[.)])\s+(?:\[[ xX]\]\s+)?"
                       r"(?:\*\*([^\s*]+)\*\*|`([^`\s]+)`|([^\s:,.*`]+))(?=[\s:,.]|$)")


def _rel_error(root, path, exc) -> GateError:
    """A GateError worded `<path relative to root>: <strerror>`, so the output is machine-independent."""
    return GateError(f"{os.path.relpath(path, root)}: {exc.strerror or exc}")


def _require_regular(root, path) -> None:
    """A report entry must be a regular file (after following symlinks): opening a FIFO would hang."""
    try:
        mode = os.stat(path).st_mode
    except OSError as exc:
        raise _rel_error(root, path, exc) from None
    if not stat.S_ISREG(mode):
        raise GateError(f"{os.path.relpath(path, root)}: not a regular file")


def _first_visit(seen, path) -> bool:
    real = os.path.realpath(path)
    if real in seen:
        return False
    seen.add(real)
    return True


def _raise(exc):
    raise exc


def registered_reports(root) -> tuple:
    """(found, bad): `found` lists the files under REPORT_DIRS with the exact marker line in their
    first MARKER_LINES lines; `bad` lists (file, line) for a file with a near-miss marker line."""
    found, bad = [], []
    seen = set()    # realpaths of directories already walked: a symlink cycle stops here
    for d in REPORT_DIRS:
        top = os.path.join(root, d)
        if not os.path.lexists(top):
            if d in REQUIRED_REPORT_DIRS:
                raise GateError(f"{d}: missing report directory")
            continue    # an optional report directory that does not exist holds no reports
        if not os.path.isdir(top):
            raise GateError(f"{d}: not a directory")
        if not _first_visit(seen, top):
            continue    # this directory was already walked through a symlink
        for dirpath, dirnames, filenames in os.walk(top, onerror=lambda exc: _raise(_rel_error(root, exc.filename, exc)), followlinks=True):
            dirnames.sort()
            dirnames[:] = [n for n in dirnames if _first_visit(seen, os.path.join(dirpath, n))]
            for name in sorted(filenames):
                path = os.path.join(dirpath, name)
                _require_regular(root, path)
                try:
                    with open(path, encoding="utf-8", errors="replace") as fh:
                        head = [next(fh, "").rstrip("\r\n") for _ in range(MARKER_LINES)]
                except OSError as exc:
                    raise _rel_error(root, path, exc) from None
                attempts = [l for l in head if MARKER_ATTEMPT.match(l)]
                wrong = next((l for l in attempts if l != MARKER), None)
                if wrong is not None:
                    bad.append((path, wrong))
                if MARKER in attempts:
                    found.append(path)
    return found, bad


def _unwrap(cell: str) -> str:
    """`**id**` and `id` in backticks are the id."""
    for w in ("**", "`"):
        if len(cell) > 2 * len(w) and cell.startswith(w) and cell.endswith(w):
            return cell[len(w):-len(w)]
    return cell


def report_ids(path, root) -> list:
    """Finding ids a registered report cites: table first cells and leading list-item ids.
    An unreadable file is a GateError worded relative to `root`."""
    ids = []
    _require_regular(root, path)
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        raise _rel_error(root, path, exc) from None
    for line in lines:
        s = line.strip()
        if s.startswith("|"):
            cell = _unwrap(s.split("|")[1].strip())
            if registers.FINDING_ID.fullmatch(cell):
                ids.append(cell)
            continue
        m = LIST_ITEM.match(line)
        if m:
            token = m.group(1) or m.group(2) or m.group(3)
            if registers.FINDING_ID.fullmatch(token):
                ids.append(token)
    return ids


def _row_id(row, index, pattern) -> str:
    """The id to print in a line id: the row's own id only when it is well-formed, else entry-<index>
    (a raw malformed id could carry a newline and forge or split a result line)."""
    rid = row.get("id")
    return rid if isinstance(rid, str) and pattern.fullmatch(rid) else f"entry-{index}"


def _categories(error: str) -> list:
    """Which categories a finding-schema error belongs to: owner errors (including a missing
    `owner` key) are `owner`; every other error is `schema`."""
    if error.startswith("owner"):
        return ["owner"]
    if error.startswith("keys: missing "):
        names = [n.strip() for n in error[len("keys: missing "):].split(",")]
        return (["owner"] if "owner" in names else []) + (["schema"] if set(names) - {"owner"} else [])
    return ["schema"]


def check(args: list) -> Result:
    ns = gate_args("gate_register").parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    findings = registers.load_findings(root)
    decisions = registers.load_decisions(root)
    r = Result("register")
    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    commits = {}

    def exists(root, sha):    # one git call per distinct sha
        if sha not in commits:
            commits[sha] = gitinfo.commit_exists(root, sha)
        return commits[sha]

    seen = set()
    for i, f in enumerate(findings):
        fid = _row_id(f, i, registers.FINDING_ID)
        if fid in seen:
            r.fail(f"register.duplicate.{fid}", "id appears more than once")
        seen.add(fid)
        errs = registers.validate_finding(f)
        for e in errs:
            for cat in _categories(e):
                r.fail(f"register.{cat}.{fid}", e)
        if errs:
            skipped.update(["transition", "evidence"])   # lifecycle rules need a well-formed row
            continue
        for e in registers.transition_errors(f):
            r.fail(f"register.transition.{fid}", e)
        for e in registers.evidence_errors(f, root, decisions) + registers.finding_commit_errors(f, root, exists):
            r.fail(f"register.evidence.{fid}", e)
    seen = set()
    for i, d in enumerate(decisions):
        did = _row_id(d, i, registers.DECISION_ID)
        if did in seen:
            r.fail(f"register.duplicate.{did}", "id appears more than once")
        seen.add(did)
        errs = registers.validate_decision(d)
        for e in errs:
            r.fail(f"register.schema.{did}", e)
        if errs:
            skipped.add("evidence")
            continue
        for e in registers.decision_commit_errors(d, root, exists):
            r.fail(f"register.evidence.{did}", e)
    known = {f["id"] for f in findings
             if isinstance(f.get("id"), str) and registers.FINDING_ID.fullmatch(f["id"])}
    reports, bad = registered_reports(root)
    for path, line in bad:
        r.fail(f"register.marker.{os.path.relpath(path, root)}",
               f"marker must be exactly '{MARKER}' (found {line!r})")
    if bad:
        skipped.add("report")
    for path in reports:
        rel = os.path.relpath(path, root)
        for rid in sorted(set(report_ids(path, root)) - known):
            r.fail(f"register.report.{rel}.{rid}", "not in register")
    failed = {l.id.split(".")[1] for l in r.lines}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"register.{cat}")
    return r


def main(argv=None):
    return run_gate("register", check, argv)


if __name__ == "__main__":
    sys.exit(main())
