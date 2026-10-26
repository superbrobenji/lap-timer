#!/usr/bin/env python3
"""gate_review_package -- a task's review package is complete before a reviewer sees it.

Layout and mutation transcript format: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Review package".
Extends superpowers' subagent-driven-development review-package (commits + diff) with gates.txt, one
mutation-<finding-id>.txt per finding the task closes, siblings.md and claims.md.
Usage: gate_review_package.py [--json] --dir .superpowers/sdd/<plan>/review/<task> | [--json] --all
The task's ids: register rows owned by {kind: plan, plan: <plan>, task: <task>} plus mutation-<id>.txt files.
--all (DEC-0022, absent-packages-pass): no package is not a missing input (no .superpowers/sdd, no review/ or
only empty ones is one PASS review_package.all), but a non-directory where a directory is expected FAILs
(.superpowers or .superpowers/sdd: review_package.input; <plan>/review or a review/ entry: <prefix>.input).
A mutation transcript is evidence at its row's pinned history sha and replays in a checkout of that sha;
a later change to the pinned code needs no transcript update (DEC-0021).
"""
from __future__ import annotations

import os
import re
import shlex
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

SDD = ".superpowers/sdd"          # git-ignored; packages live at .superpowers/sdd/<plan>/review/<task>/
REQUIRED = ["commits.txt", "diff.patch", "gates.txt", "siblings.md", "claims.md"]
STEP1 = re.compile(r"^step 1 \(fix present\): (?P<cmd>.+) -> PASS$")
STEP2 = re.compile(r"^step 2 \(fix reverted: (?P<how>.+?)\): (?P<cmd>.+) -> FAIL$")
STEP3 = re.compile(r"^step 3 \(fix restored\): (?P<cmd>.+) -> PASS$")
SEPARATOR = re.compile(r"^\|(\s*:?-{3,}:?\s*\|)+$")
COMMIT_LINE = re.compile(r"^[0-9a-f]{7,40}(?: .*)?$")    # B5-FIX-7: git allows an empty or space-led subject
GATE_LINE = re.compile(r"^(PASS|FAIL) \S+")
DIFF_BODY = re.compile(r"^(@@ |--- |\+\+\+ |index |new file mode|deleted file mode|old mode|new mode"
                       r"|similarity index|rename |copy |Binary files )")
TEST_NAME = re.compile(r"^(?:[\w.\-/]+::)*(?:\w+\.)*test_\w+$")
TEST_FILE = re.compile(r"^test_\w*\.(py|c)$")    # DEC-0037: tracked test_*.py, and Unity test_*.c sources
DEFINES = {"py": r"^[ \t]*(?:async[ \t]+)?def {name}\(", "c": r"^[ \t]*void[ \t]+{name}[ \t]*\([ \t]*void[ \t]*\)"}


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def _lines(text: str) -> list[str]:
    return [l.rstrip() for l in text.splitlines() if l.strip()]


def table_rows(text: str, every: bool = False) -> list[list[str]] | None:
    """Data rows of the first Markdown table: the first '|' line is the header, the next line must be the
    separator, and the table ends at the first line that does not start with '|'. every=True (claims.md,
    B5-FIX-22: spec \u00a77 and umbrella :266, the gate fails 'if claims.md has a FAIL row') reads every '|' line
    after the first header instead: a later table's header (a '|' line followed by a separator) and every
    separator are skipped, any other '|' line is a row, after a blank line or prose too. Cells split on an
    unescaped '|' only; '\\|' inside a cell is a literal '|'. None when there is no header + separator."""
    lines = [l.strip() for l in text.splitlines()]
    start = next((i for i, l in enumerate(lines) if l.startswith("|")), None)
    if start is None or start + 1 >= len(lines) or not SEPARATOR.match(lines[start + 1]):
        return None
    rows = []
    for i in range(start + 2, len(lines)):
        l = lines[i]
        if not l.startswith("|"):
            if every:
                continue
            break
        if every and (SEPARATOR.match(l) or (i + 1 < len(lines) and SEPARATOR.match(lines[i + 1]))):
            continue
        rows.append(_cells(l))
    return rows


def _cells(line: str) -> list[str]:
    inner = line[1:]
    if inner.endswith("|") and not inner.endswith("\\|"):
        inner = inner[:-1]
    return [c.replace("\\|", "|").strip() for c in re.split(r"(?<!\\)\|", inner)]


def _tokens(cmd: str) -> list[str]:
    """cmd split as the shell splits it (B5-FIX-18: a quoted test id is one token); unbalanced quotes: whitespace."""
    try:
        return shlex.split(cmd)
    except ValueError:
        return cmd.split()


def names_test(cmd: str, test: str, module: str | None = None) -> bool:
    """True when test is a whole component of cmd: a token's last dotted component or its part after '::'.
    With module (B5-FIX-17: the pin's test_<module>), the same token must also name that module: a dotted
    component of a unittest id, or the file name (<module>.py) of a pytest path."""
    for tok in _tokens(cmd):
        parts = tok.split("::")
        if parts[-1].split(".")[-1] != test:
            continue
        if module is None:
            return True
        dotted = (parts[0] if len(parts) == 1 else "").split(".")
        if module in dotted[:-1] or (len(parts) > 1 and os.path.basename(parts[0]) in (module, module + ".py")):
            return True
    return False


def mutation_errors(text: str, fid: str, pin_test: str | None = None) -> list[str]:
    lines = _lines(text)
    if len(lines) != 5:
        return [f"want exactly five lines (finding, test, step 1, step 2, step 3), got {len(lines)}"]
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
    if steps[1] and not steps[1].group("how").strip():
        errs.append("step 2 names no way the fix was reverted")
    if all(steps):
        cmds = {m.group("cmd") for m in steps}
        if len(cmds) != 1:
            errs.append("steps 1-3 must run the same command")
        elif test and not names_test(steps[0].group("cmd"), test.split("::")[-1]):
            errs.append(f"the command does not name the test {test!r}")
        elif pin_test is not None and "::" in pin_test and \
                not names_test(steps[0].group("cmd"), pin_test.split("::")[-1], pin_test.split("::")[0]):
            errs.append(f"the command does not run the register's pinned test {pin_test!r} (module and name)")
    if pin_test is not None and test and test.split("::")[-1] != pin_test.split("::")[-1]:
        errs.append(f"test {test!r} is not the register's pinned test {pin_test!r}")
    if pin_test is not None and "::" in test and test != pin_test:
        errs.append(f"test {test!r} is not the register's pinned test {pin_test!r}")
    return errs


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def _commits(pkg, res, pfx):
    path = os.path.join(pkg, "commits.txt")
    if os.path.isfile(path):
        lines = _lines(_read(path))
        bad = [l for l in lines if not COMMIT_LINE.match(l)]
        _verdict(res, f"{pfx}.commits", lines and not bad,
                 f"commits.txt needs one '<sha> <subject>' line per commit; "
                 f"{len(bad)} bad of {len(lines)}: {bad[0][:60] if bad else '-'!r}")


def diff_error(text: str) -> str:
    if not text.startswith(("diff --git a/", 'diff --git "a/')):    # B5-FIX-7: git quotes an unusual path
        return "diff.patch does not start with 'diff --git a/' (or git's quoted 'diff --git \"a/')"
    if not any(DIFF_BODY.match(l) for l in text.splitlines()[1:]):
        return "diff.patch has no hunk or file line after 'diff --git'"
    return ""


def _gates(pkg, res, pfx):
    path = os.path.join(pkg, "gates.txt")
    if os.path.isfile(path):
        lines = _lines(_read(path))
        bad = [l for l in lines if not GATE_LINE.match(l)]
        red = [l for l in lines if l.startswith("FAIL ")]
        _verdict(res, f"{pfx}.gates", lines and not bad and not red,
                 f"gates.txt needs gate output ('PASS|FAIL <id>' lines, no FAIL): {len(lines)} line(s), "
                 f"{len(bad)} not gate lines, {len(red)} FAIL; first: {(bad or red or [''])[0][:60]!r}")


def _sibling_ok(cell: str, findings: list) -> bool:
    return bool(TEST_NAME.match(cell)) or registers.finding_by_id(findings, cell) is not None


def test_modules(root: str) -> dict:
    """{key: [(kind, source), ...]} of every tracked test file (DEC-0037: `git ls-files` under root, basename
    test_*.py or a Unity test_*.c); a file's keys are its root-relative path, that path dotted without the
    extension, and its bare stem. A key two files share (a stem in two dirs) lists both."""
    r = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=False)
    out = {}
    for rel in sorted(r.stdout.decode("utf-8", "surrogateescape").split("\0")):    # a failing git prints nothing
        m = TEST_FILE.match(os.path.basename(rel))
        path = os.path.join(root, rel)
        if not m or not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            entry = (m.group(1), f.read())
        stem = rel[:-len(m.group(1)) - 1]
        for key in (rel, stem.replace("/", "."), os.path.basename(stem)):
            out.setdefault(key, []).append(entry)
    return out


def defines_test(cell: str, modules: dict) -> bool:
    """B5-FIX-26 (DEC-0035, DEC-0037): the test a siblings.md cell names exists: a file its module (the part
    before the first '::', else the longest dotted prefix that is a module key) names defines it (`def <name>(`
    in Python, `void <name>(void)` in Unity C); a bare `test_<x>` is defined in any tracked test file. A named
    module that is no key resolves nothing."""
    parts = cell.split("::")
    name = parts[-1].split(".")[-1]

    def defined(files):
        return any(re.search(DEFINES[kind].format(name=name), text, re.M) for kind, text in files)
    dotted = parts[0].split(".")[:-1]
    if len(parts) == 1 and not dotted:
        return any(defined(files) for files in modules.values())
    mods = parts[:1] if len(parts) > 1 else [".".join(dotted[:k]) for k in range(len(dotted), 0, -1)]
    mod = next((m for m in mods if m in modules), None)
    return mod is not None and defined(modules[mod])


def _siblings(root, pkg, needed, longest, findings, res, pfx):
    path = os.path.join(pkg, "siblings.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path))
    if rows is None:
        res.fail(f"{pfx}.siblings", "siblings.md is not a Markdown table (header row + separator)")
        return
    bad = [r for r in rows if len(r) != 2 or not r[0] or not _sibling_ok(r[1], findings)]
    missing = [s for s in needed if not any(s == r[0] for r in rows)]
    modules = test_modules(root)
    undefined = [r[1] for r in rows if r not in bad and registers.finding_by_id(findings, r[1]) is None
                 and not defines_test(r[1], modules)]
    if len(rows) < longest:
        res.fail(f"{pfx}.siblings", f"{len(rows)} row(s) for a register siblings list of {longest}")
    elif bad:
        res.fail(f"{pfx}.siblings", f"{len(bad)} row(s) are not 'sibling | test name or registered finding id'")
    elif missing:
        res.fail(f"{pfx}.siblings", f"register sibling(s) not listed: {', '.join(missing)}")
    elif undefined:
        res.fail(f"{pfx}.siblings", f"test(s) defined in no test module named: {', '.join(undefined)}")
    else:
        res.passed(f"{pfx}.siblings")


def fail_lines(text: str) -> list[str]:
    """DEC-0038 (any-fail-line): the first cell of every claims.md line whose last '|'-separated field (the result
    cell, a pipe-less GFM row's trailing field, or a line with no '|' as a whole) is FAIL, whatever its table
    position: a header, a line after a separator or a blank line, a row with no leading '|'."""
    out = []
    for l in text.splitlines():
        l = l.strip()
        cells = _cells(l if l.startswith("|") else "|" + l)
        if cells[-1] == "FAIL":
            out.append(cells[0])
    return out


def _claims(pkg, res, pfx):
    path = os.path.join(pkg, "claims.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path), every=True)
    if rows is None:
        res.fail(f"{pfx}.claims", "claims.md is not a Markdown table (header row + separator)")
        return
    malformed = [r for r in rows if len(r) != 5 or not all(r[:4]) or r[4] not in ("PASS", "FAIL")]
    failed = fail_lines(_read(path))
    if not rows:
        res.fail(f"{pfx}.claims", "no executed claims; a claim with no executable check is a finding")
    elif malformed:
        res.fail(f"{pfx}.claims", f"{len(malformed)} row(s) not 'claim | direct | contradiction | output | PASS/FAIL' "
                 "with the first four cells filled")
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


def _pin_test(row: dict):
    pin = row.get("pin")
    test = pin.get("test") if isinstance(pin, dict) else None
    return test if isinstance(test, str) else None


def _mutations(pkg, ids, findings, res, pfx):
    """(register siblings still to list, longest register siblings list among the task's findings)."""
    needed, longest = [], 0
    for fid in ids:
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"{pfx}.finding.{fid}", "not in docs/superpowers/state/findings.yml")
            continue
        listed = [str(s).strip() for s in (f.get("siblings") or [])]
        longest = max(longest, len(listed))
        needed.extend(s for s in listed if s not in needed)
        path = os.path.join(pkg, f"mutation-{fid}.txt")
        if not os.path.isfile(path):
            res.fail(f"{pfx}.mutation.{fid}", f"mutation-{fid}.txt missing")
            continue
        errs = mutation_errors(_read(path), fid, _pin_test(f))
        _verdict(res, f"{pfx}.mutation.{fid}", not errs, "; ".join(errs))
    return needed, longest


def check_package(root: str, pkg: str, ids: list, findings: list, res: Result, pfx: str) -> None:
    for name in REQUIRED:
        path = os.path.join(pkg, name)
        _verdict(res, f"{pfx}.file.{name}", os.path.isfile(path) and os.path.getsize(path) > 0,
                 f"{os.path.relpath(path, root)} missing or empty")
    _commits(pkg, res, pfx)
    diff = os.path.join(pkg, "diff.patch")
    if os.path.isfile(diff):
        err = diff_error(_read(diff))
        _verdict(res, f"{pfx}.diff", not err, err)
    _gates(pkg, res, pfx)
    needed, longest = _mutations(pkg, ids, findings, res, pfx)
    _siblings(root, pkg, needed, longest, findings, res, pfx)
    _claims(pkg, res, pfx)


def _layout(roots: tuple, wanted: str) -> tuple[str, str]:
    """(plan, task) of <root>/.superpowers/sdd/<plan>/review/<task> for the root as given or its realpath;
    lexical only (no component below the root is resolved, so --dir agrees with --all); else GateError."""
    for root in roots:
        parts = os.path.relpath(wanted, root).split(os.sep)
        if len(parts) == 5 and parts[:2] == SDD.split("/") and parts[3] == "review":
            return parts[2], parts[4]
    raise GateError(f"--dir {wanted} is not <root>/{SDD}/<plan>/review/<task>")


def _check_all(root: str, findings: list, res: Result) -> None:
    """Every entry under <root>/.superpowers/sdd/<plan>/review/, dot-entries included; no entry at all
    (no review/, or only empty ones: B5-FIX-4 F1) is one PASS review_package.all; .superpowers or
    .superpowers/sdd that exists but is not a directory is FAIL review_package.input (B5-FIX-6, DEC-0022)."""
    for rel in (".superpowers", SDD):    # DEC-0022: a non-directory where a directory is expected
        path = os.path.join(root, rel)
        if os.path.lexists(path) and not os.path.isdir(path):
            res.fail("review_package.input", f"{rel} is not a directory")
            return
    sdd = os.path.join(root, SDD)
    seen = False
    for plan in sorted(os.listdir(sdd)) if os.path.isdir(sdd) else []:
        review = os.path.join(sdd, plan, "review")
        try:
            os.lstat(review)
        except (FileNotFoundError, NotADirectoryError):
            continue
        except OSError as e:    # B5-FIX-21: <plan> unreadable (mode 000, or no search bit): FAIL, never skip
            seen = True
            res.fail(f"review_package.{plan}.input", f"{os.path.relpath(review, root)}: {e.strerror or e}")
            continue
        if not os.path.isdir(review):
            seen = True
            res.fail(f"review_package.{plan}.input", f"{os.path.relpath(review, root)} is not a directory")
            continue
        try:
            tasks = sorted(os.listdir(review))
        except OSError as e:    # B5-FIX-21: an unreadable review/ FAILs
            seen = True
            res.fail(f"review_package.{plan}.input", f"{os.path.relpath(review, root)}: {e.strerror or e}")
            continue
        for task in tasks:
            seen = True
            _check_entry(root, os.path.join(review, task), f"review_package.{plan}.{task}", findings, res)
    if not seen:
        res.passed("review_package.all")


def _check_entry(root: str, pkg: str, pfx: str, findings: list, res: Result) -> None:
    if not os.path.isdir(pkg):
        res.fail(f"{pfx}.input", f"{os.path.relpath(pkg, root)} is not a directory")
        return
    try:
        os.listdir(pkg)
    except OSError as e:    # B5-FIX-21: an unreadable <task> dir FAILs under its own prefix
        res.fail(f"{pfx}.input", f"{os.path.relpath(pkg, root)}: {e.strerror or e}")
        return
    ids = task_findings(findings, pkg)
    if not ids:
        res.fail(f"{pfx}.mutation", "no register row owns this task and no mutation-<finding-id>.txt exists")
    check_package(root, pkg, ids, findings, res, pfx)


def check(args: list) -> Result:
    p = gate_args("gate_review_package")
    p.add_argument("--dir")
    p.add_argument("--all", action="store_true")
    a = p.parse_args(args)
    if a.all == bool(a.dir):
        raise GateError("give exactly one of --all or --dir .superpowers/sdd/<plan>/review/<task>")
    given = os.path.abspath(a.root or repo_root())
    root = os.path.realpath(given)
    findings = registers.load_findings(root)
    res = Result("review_package")
    if a.all:
        _check_all(root, findings, res)
        return res
    wanted = os.path.normpath(os.path.join(given, a.dir))    # an absolute --dir wins in join
    plan, task = _layout((given, root), wanted)
    pkg = os.path.join(root, SDD, plan, "review", task)    # symlinks below the root are followed, as in --all
    if not os.path.isdir(pkg):
        raise GateError(f"--dir {a.dir}: no such directory")
    ids = task_findings(findings, pkg)
    if not ids:
        res.fail("review_package.findings", f"no register row has owner {{kind: plan, plan: {plan}, task: {task}}}")
    check_package(root, pkg, ids, findings, res, "review_package")
    return res


def main(argv=None):
    return run_gate("review_package", check, argv)


if __name__ == "__main__":
    sys.exit(main())
