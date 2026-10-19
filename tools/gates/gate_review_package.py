#!/usr/bin/env python3
"""gate_review_package -- a task's review package is complete before a reviewer sees it.

Layout and mutation transcript format: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Review package".
Extends superpowers' subagent-driven-development review-package (commits + diff) with gates.txt, one
mutation-<finding-id>.txt per finding the task closes, siblings.md and claims.md.
Usage: gate_review_package.py [--json] --dir .superpowers/sdd/<plan>/review/<task> | [--json] --all
The task's ids: register rows owned by {kind: plan, plan: <plan>, task: <task>} plus mutation-<id>.txt files.
"""
from __future__ import annotations

import glob
import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

STATE = "docs/superpowers/state"
SDD = ".superpowers/sdd"          # git-ignored; packages live at .superpowers/sdd/<plan>/review/<task>/
REQUIRED = ["commits.txt", "diff.patch", "gates.txt", "siblings.md", "claims.md"]
STEP1 = re.compile(r"^step 1 \(fix present\): (?P<cmd>.+) -> PASS$")
STEP2 = re.compile(r"^step 2 \(fix reverted: (?P<how>.+?)\): (?P<cmd>.+) -> FAIL$")
STEP3 = re.compile(r"^step 3 \(fix restored\): (?P<cmd>.+) -> PASS$")
SEPARATOR = re.compile(r"^\|(\s*:?-{3,}:?\s*\|)+$")


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def table_rows(text: str) -> list[list[str]] | None:
    """Data rows of the first Markdown table in text; None when there is no header + separator."""
    lines = [l.strip() for l in text.splitlines() if l.strip().startswith("|")]
    if len(lines) < 2 or not SEPARATOR.match(lines[1]):
        return None
    return [[c.strip() for c in l.strip("|").split("|")] for l in lines[2:]]


def mutation_errors(text: str, fid: str) -> list[str]:
    lines = [l.rstrip() for l in text.splitlines() if l.strip()]
    if len(lines) < 5:
        return ["fewer than five lines (finding, test, step 1, step 2, step 3)"]
    errs = []
    if lines[0] != f"finding: {fid}":
        errs.append(f"line 1 must be 'finding: {fid}'")
    test = lines[1][len("test: "):].strip() if lines[1].startswith("test: ") else ""
    if not test:
        errs.append("line 2 must be 'test: <test name>'")
    steps = [STEP1.match(lines[2]), STEP2.match(lines[3]), STEP3.match(lines[4])]
    for n, m in enumerate(steps, 1):
        if not m:
            errs.append(f"step {n} malformed or wrong verdict (want PASS/FAIL/PASS): {lines[n + 1][:80]!r}")
    if all(steps):
        cmds = {m.group("cmd") for m in steps}
        if len(cmds) != 1:
            errs.append("steps 1-3 must run the same command")
        elif test and test not in steps[0].group("cmd"):
            errs.append(f"the command does not name the test {test!r}")
    return errs


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def _siblings(pkg, needed, res, pfx):
    path = os.path.join(pkg, "siblings.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path))
    if rows is None:
        res.fail(f"{pfx}.siblings", "siblings.md is not a Markdown table (header row + separator)")
        return
    bad = [r for r in rows if len(r) < 2 or not r[0] or not r[1]]
    missing = [s for s in needed if not any(s in r[0] for r in rows)]
    if bad:
        res.fail(f"{pfx}.siblings", f"{len(bad)} row(s) lack 'sibling | test name or finding id'")
    elif len(rows) < len(needed) or missing:
        res.fail(f"{pfx}.siblings", f"{len(rows)} row(s) for {len(needed)} register sibling(s); "
                 f"not listed: {', '.join(missing) or '-'}")
    else:
        res.passed(f"{pfx}.siblings")


def _claims(pkg, res, pfx):
    path = os.path.join(pkg, "claims.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path))
    if rows is None:
        res.fail(f"{pfx}.claims", "claims.md is not a Markdown table (header row + separator)")
        return
    malformed = [r for r in rows if len(r) != 5 or r[4] not in ("PASS", "FAIL")]
    failed = [r[0] for r in rows if len(r) == 5 and r[4] == "FAIL"]
    if not rows:
        res.fail(f"{pfx}.claims", "no executed claims; a claim with no executable check is a finding")
    elif malformed:
        res.fail(f"{pfx}.claims", f"{len(malformed)} row(s) not 'claim | direct | contradiction | output | PASS/FAIL'")
    elif failed:
        res.fail(f"{pfx}.claims", f"FAIL rows: {'; '.join(failed)}")
    else:
        res.passed(f"{pfx}.claims")


def task_findings(findings: list, pkg: str) -> list[str]:
    """Ids the package's task closes: register rows with owner {kind: plan, plan: <plan>, task: <task>}
    (the directory names in .superpowers/sdd/<plan>/review/<task>) plus every mutation-<id>.txt present."""
    parts = os.path.normpath(pkg).split(os.sep)
    plan, task = parts[-3], parts[-1]
    owned = {f["id"] for f in findings
             if (f.get("owner") or {}).get("kind") == "plan"
             and str(f["owner"].get("plan")) == plan and str(f["owner"].get("task")) == task}
    files = {n[len("mutation-"):-len(".txt")] for n in os.listdir(pkg)
             if n.startswith("mutation-") and n.endswith(".txt")}
    return sorted(owned | files)


def check_package(root: str, pkg: str, ids: list, findings: list, res: Result, pfx: str) -> None:
    for name in REQUIRED:
        path = os.path.join(pkg, name)
        _verdict(res, f"{pfx}.file.{name}", os.path.isfile(path) and os.path.getsize(path) > 0,
                 f"{os.path.relpath(path, root)} missing or empty")
    diff = os.path.join(pkg, "diff.patch")
    if os.path.isfile(diff):
        _verdict(res, f"{pfx}.diff", _read(diff).startswith("diff --git"), "diff.patch does not start with 'diff --git'")
    gates = os.path.join(pkg, "gates.txt")
    if os.path.isfile(gates):
        red = [l for l in _read(gates).splitlines() if l.startswith("FAIL ")]
        _verdict(res, f"{pfx}.gates", not red, f"gates.txt has {len(red)} FAIL line(s); first: {red[0] if red else ''}")
    needed = []
    for fid in ids:
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"{pfx}.finding.{fid}", "not in docs/superpowers/state/findings.yml")
            continue
        needed.extend(str(s) for s in (f.get("siblings") or []))
        path = os.path.join(pkg, f"mutation-{fid}.txt")
        if not os.path.isfile(path):
            res.fail(f"{pfx}.mutation.{fid}", f"mutation-{fid}.txt missing")
            continue
        errs = mutation_errors(_read(path), fid)
        _verdict(res, f"{pfx}.mutation.{fid}", not errs, "; ".join(errs))
    _siblings(pkg, needed, res, pfx)
    _claims(pkg, res, pfx)


def check(args: list) -> Result:
    p = gate_args("gate_review_package")
    p.add_argument("--dir")
    p.add_argument("--all", action="store_true")
    a = p.parse_args(args)
    if a.all == bool(a.dir):
        raise GateError("give exactly one of --all or --dir .superpowers/sdd/<plan>/review/<task>")
    root = a.root or repo_root()
    findings = registers.load_findings(root)
    res = Result("review_package")
    if a.all:
        if not os.path.isdir(os.path.join(root, STATE)):
            raise GateError(f"{STATE}/ missing")
        pkgs = sorted(d for d in glob.glob(os.path.join(root, SDD, "*", "review", "*")) if os.path.isdir(d))
        if not pkgs:
            res.passed("review_package.all")
            return res
        for pkg in pkgs:
            parts = os.path.normpath(pkg).split(os.sep)
            pfx = f"review_package.{parts[-3]}.{parts[-1]}"
            ids = task_findings(findings, pkg)
            if not ids:
                res.fail(f"{pfx}.mutation", "no register row owns this task and no mutation-<finding-id>.txt exists")
            check_package(root, pkg, ids, findings, res, pfx)
        return res
    pkg = os.path.normpath(a.dir if os.path.isabs(a.dir) else os.path.join(root, a.dir))
    if not os.path.isdir(pkg):
        raise GateError(f"--dir {a.dir}: no such directory")
    ids = task_findings(findings, pkg)
    if not ids:
        plan, task = pkg.split(os.sep)[-3], pkg.split(os.sep)[-1]
        res.fail("review_package.findings", f"no register row has owner {{kind: plan, plan: {plan}, task: {task}}}")
    check_package(root, pkg, ids, findings, res, "review_package")
    return res


def main(argv=None):
    return run_gate("review_package", check, argv)


if __name__ == "__main__":
    sys.exit(main())
