#!/usr/bin/env python3
"""gate_plan -- an implementation plan is executable under strict delivery (spec §6 step 3).

Checks: writing-plans header blocks; '**Files:**' and '**Findings:**' per task; every routed id registered
and in exactly one task; with --source, every source id in exactly one task; file overlap between PARALLEL
tasks of one block; tasks per block <= config block.max_tasks (blocks: '## Block bN' headings or CHECKPOINT
steps; heading numbers increase; '## Block N' or '## Block bN', level 2); the writing-plans "No Placeholders" list over the whole file, code included.
A Files item ('- Create|Modify|Test|Delete: `path` prose') starts with exactly one backticked span, the path;
the prose after it holds no backtick (DEC-0017). Per task also (DEC-0016): '**Siblings:**' lists every sibling
of the register rows its Findings name; '**Gates:**' names run_all or gates X whose tools/gates/gate_X.py is at
the root or a Create item of this or an earlier task; Findings that name ids need a pin step (a step title that
contains 'pin', case-insensitive, whose body names a test test_<module>::test_<name>).
Also owns the per-plan ledger contract the hooks use: plans_in_flight(root).
Usage: gate_plan.py --plan P [--source FILE | --source register:key=pat[,pat][&key=pat]] [--json]
       gate_plan.py --in-flight [--json]
"""
from __future__ import annotations

import argparse
import fnmatch
import glob
import os
import posixpath
import re
import subprocess
import sys
from dataclasses import dataclass, field

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import CODE_ROOT, GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as gconfig  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

STATE = "docs/superpowers/state"
HEADERS = {
    "agentic": re.compile(r"^> \*\*For agentic workers:\*\*"),
    "goal": re.compile(r"^\*\*Goal:\*\* \S"),
    "architecture": re.compile(r"^\*\*Architecture:\*\* \S"),
    "tech-stack": re.compile(r"^\*\*Tech Stack:\*\* \S"),
    "spec": re.compile(r"^\*\*Spec:\*\* \S"),
    "global-constraints": re.compile(r"^## Global Constraints\s*$"),
    "review-focus": re.compile(r"^## Review Focus\s*$"),
}
BLOCK_RE = re.compile(r"^##[ \t]+Block[ \t]+b?(\d+)\b")
FENCE_RE = re.compile(r"^( *)(`{3,}|~{3,})(.*)$")
LIST_ITEM = re.compile(r"^( *)([-*+]|\d{1,9}[.)])( +)\S")
FIELD_RE = re.compile(r"^\*\*[^*\n]+:\*\*")
STEP_RE = re.compile(r"^- \[[ xX]\] \*\*Step\b")
TASK_RE = re.compile(r"^### Task (\d+[A-Za-z]?)\b")
CHECKPOINT_STEP = re.compile(r"^- \[[ xX]\] \*\*Step \d+: CHECKPOINT\b")
FILE_RE = re.compile(r"^- (Create|Modify|Test|Delete):(.*)$")
FILE_PATH = re.compile(r"^`([^`]+)`")
FINDINGS_RE = re.compile(r"^\*\*Findings:\*\*\s*(.*)$")
MODE_RE = re.compile(r"^\*\*Mode:\*\*\s*(.*?)\s*$")
SIBLINGS_RE = re.compile(r"^\*\*Siblings:\*\*\s*(.*?)\s*$")
GATES_RE = re.compile(r"^\*\*Gates:\*\*\s*(.*?)\s*$")
GATE_NAME = re.compile(r"^[a-z0-9_]+$")
STEP_TITLE = re.compile(r"^- \[[ xX]\] \*\*(.*?)\*\*(.*)$")
PIN_TITLE = re.compile(r"pin", re.I)
PIN_NAME = re.compile(r"(?<![A-Za-z0-9_])test_[A-Za-z0-9_]+::test_[A-Za-z0-9_]+")
ID_RE = re.compile(r"^[A-Z0-9]+(-[A-Za-z0-9]+)+$")
NONE_RE = re.compile(r"^none\s+(?:—|--|-)\s+\S")
SIBLINGS_NONE = re.compile(r"^none -- \S")
SIBLINGS_NONE_WORD = re.compile(r"^none(?![A-Za-z0-9_])", re.I)
SPEC_PATH = re.compile(r"`([^`]+)`")
TOKEN_RE = re.compile(r"\b[A-Z0-9]+(?:-[A-Za-z0-9]+)+\b")
# writing-plans "No Placeholders"; each phrase is split so this file never matches itself
PLACEHOLDERS = [re.compile(r"\bT" r"BD\b"), re.compile(r"\bTO" r"DO\b")] + [re.compile(p, re.I) for p in (
    r"implement lat" r"er", r"fill in detai" r"ls", r"add appropriate error handl" r"ing",
    r"add valid" r"ation", r"handle edge cas" r"es", r"write tests for the abo" r"ve",
    r"similar to tas" r"k \d+")]
LEDGER_PLAN = re.compile(r"^plan: (\S+)\s*$")
CHECKPOINT_LINE = re.compile(r"^CHECKPOINT b(\d+)\b")
QUERY_KEYS = {
    "plan": lambda f: (f.get("owner") or {}).get("plan"),
    "task": lambda f: (f.get("owner") or {}).get("task"),
    "state": lambda f: f.get("state"),
    "severity": lambda f: f.get("severity"),
}



@dataclass
class Task:
    num: str
    line: int
    block: int
    files: list = field(default_factory=list)
    findings: list | None = None
    findings_error: str = ""
    parallel: bool = False
    files_errors: list = field(default_factory=list)
    mode_error: str = ""
    mode_seen: bool = False
    creates: list = field(default_factory=list)
    siblings: list | None = None
    siblings_value: str = ""
    siblings_error: str = ""
    gates: list | None = None
    gates_error: str = ""
    steps: list = field(default_factory=list)


@dataclass
class Step:
    title: str
    body: list


@dataclass
class Plan:
    headers: dict
    tasks: list
    placeholders: list
    block_errors: list = field(default_factory=list)
    fence_error: str = ""


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def _resolve(root: str, path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(root, path)


def _findings_value(task: Task, value: str) -> None:
    value = value.strip()
    if NONE_RE.match(value):
        task.findings = []
        return
    ids = [v.strip().strip("`") for v in value.split(",")]
    bad = [i for i in ids if not ID_RE.match(i)]
    task.findings = [] if bad else ids
    if bad:
        task.findings_error = f"not register ids: {', '.join(repr(b) for b in bad)} (or write 'none — <reason>')"


def _fence_open(line: str, item_col):
    """(char, run length, base column) when `line` opens a code fence, else None. CommonMark: up to 3 spaces
    of indent, relative to the content column of the list item the line continues; a backtick fence's info
    string holds no backtick."""
    m = FENCE_RE.match(line)
    if not m:
        return None
    indent, run, info = len(m.group(1)), m.group(2), m.group(3)
    if run[0] == "`" and "`" in info:
        return None
    base = item_col if item_col is not None and indent >= item_col else 0
    return (run[0], len(run), base) if indent - base <= 3 else None


def _fence_close(line: str, fence: tuple) -> bool:
    """Same character, a run at least as long as the opener's, no info string, indent <= 3 past the base."""
    m = FENCE_RE.match(line)
    char, size, base = fence
    return bool(m) and not m.group(3).strip() and m.group(2)[0] == char and len(m.group(2)) >= size \
        and len(m.group(1)) <= base + 3


def _file_item(task: Task, line: str) -> bool:
    """A Files item: '- Create|Modify|Test|Delete: ' then exactly one backticked span, the path (a ':' suffix
    is a line range), then prose holding no backtick. Any other shape is a files error (fail closed)."""
    fm = FILE_RE.match(line)
    if not fm:
        return False
    rest = fm.group(2).strip()
    ticks = rest.count("`")
    m = FILE_PATH.match(rest)
    path = posixpath.normpath(m.group(1).split(":")[0].strip()) if m else ""
    if ticks == 0:
        task.files_errors.append(f"no backticked path: {line.strip()[:80]!r}")
    elif ticks % 2:
        task.files_errors.append(f"unclosed backtick: {line.strip()[:80]!r}")
    elif ticks > 2:
        task.files_errors.append(f"a second backticked span (one path per item; symbols go in plain prose): {line.strip()[:80]!r}")
    elif not m:
        task.files_errors.append(f"the item must start with the backticked path: {line.strip()[:80]!r}")
    elif path in ("", "."):
        task.files_errors.append(f"not a path: {line.strip()[:80]!r}")
    else:
        task.files.append(path)
        if fm.group(1) == "Create":
            task.creates.append(path)
    return True


def _siblings_value(task: Task, value: str) -> None:
    if task.siblings is not None:
        task.siblings_error = "a second **Siblings:** line in one task"
        return
    task.siblings_value = value
    if SIBLINGS_NONE.match(value):
        task.siblings = []
        return
    task.siblings = [v.strip() for v in value.split(",")]
    if SIBLINGS_NONE_WORD.match(value):
        task.siblings_error = f"write 'none -- <reason>' (lower-case none, a reason after ' -- ') or a comma-separated list: {value!r}"
    elif "" in task.siblings:
        task.siblings_error = f"empty entry in **Siblings:** {value!r}"


def _gates_value(task: Task, value: str) -> None:
    if task.gates is not None:
        task.gates_error = "a second **Gates:** line in one task"
        return
    task.gates = [v.strip() for v in value.split(",")]
    if "" in task.gates:
        task.gates_error = f"empty gate name in **Gates:** {value!r}"


def _listed(sibling: str, value: str) -> bool:
    """`sibling` is one whole comma-separated entry of `value`, verbatim (an entry may itself hold a comma)."""
    return re.search(r"(?:^|,)\s*" + re.escape(sibling) + r"\s*(?:,|$)", value) is not None


def parse_plan(text: str) -> Plan:
    headers, tasks, placeholders, block_errors = {}, [], [], []
    fence, fence_line, item_col, block, closed, cur, in_files = None, 0, None, 1, False, None, False
    used, first_head, step = 0, None, None
    for n, line in enumerate(text.splitlines(), 1):
        if any(rx.search(line) for rx in PLACEHOLDERS):
            placeholders.append((n, line.strip()[:80]))
        if fence is not None:
            if _fence_close(line, fence):
                fence = None
                continue
            if not (fence[2] and line.strip() and len(line) - len(line.lstrip(" ")) < fence[2]):
                if step is not None:
                    step.body.append(line)    # code inside a step is part of its body
                continue
            fence, item_col = None, None    # a line left of the list item's content ends the item and its fence
        opened = _fence_open(line, item_col)
        if opened:
            fence, in_files, fence_line = opened, False, n
            continue
        if line.strip():
            lm = LIST_ITEM.match(line)
            if lm:
                item_col = len(lm.group(1)) + len(lm.group(2)) + len(lm.group(3))
            elif item_col is not None and len(line) - len(line.lstrip(" ")) < item_col:
                item_col = None
        m = BLOCK_RE.match(line)
        if m:
            num = int(m.group(1))
            if num <= used:
                block_errors.append(f"line {n}: 'Block b{num}' does not follow block {used}")
            used, first_head = max(used, num), first_head or n
            block, closed, cur, in_files, step = num, False, None, False, None
            continue
        m = TASK_RE.match(line)
        if m:
            if closed:
                block, closed = block + 1, False
            used = max(used, block)
            cur, in_files, step = Task(m.group(1), n, block), False, None
            tasks.append(cur)
            continue
        if STEP_RE.match(line):
            sm = STEP_TITLE.match(line)
            step = Step(sm.group(1), [sm.group(2)]) if sm else Step(line, [])
            if cur is not None:
                cur.steps.append(step)
        elif line.startswith("#"):
            step = None
        elif step is not None:
            step.body.append(line)
        if CHECKPOINT_STEP.match(line):
            closed, in_files = True, False
            continue
        for key, rx in HEADERS.items():
            if key not in headers and rx.match(line):
                headers[key] = line
        if cur is None:
            continue
        if in_files:
            if not line.strip():
                continue
            if _file_item(cur, line):
                continue
            if not (line.startswith("#") or FIELD_RE.match(line) or STEP_RE.match(line)):
                cur.files_errors.append(f"not a '- Create|Modify|Test|Delete: `path`' item inside **Files:**: {line.strip()[:80]!r}")
                continue
            in_files = False
        if line.startswith("**Files:**"):
            in_files = True
            continue
        m = FINDINGS_RE.match(line)
        if m:
            _findings_value(cur, m.group(1))
            continue
        m = SIBLINGS_RE.match(line)
        if m:
            _siblings_value(cur, m.group(1))
            continue
        m = GATES_RE.match(line)
        if m:
            _gates_value(cur, m.group(1))
            continue
        m = MODE_RE.match(line)
        if m:
            if cur.mode_seen:
                cur.mode_error = "a second **Mode:** line in one task"
            cur.mode_seen = True
            cur.parallel = m.group(1) == "PARALLEL"
            if m.group(1) not in ("PARALLEL", "SERIAL"):
                cur.mode_error = f"**Mode:** must be exactly PARALLEL or SERIAL, found {m.group(1)!r}"
    if first_head is not None:
        for t in tasks:
            if t.line < first_head:
                block_errors.append(f"line {t.line}: Task {t.num} comes before the first 'Block' heading")
    err = f"line {fence_line}: code fence never closed" if fence is not None else ""
    return Plan(headers, tasks, placeholders, block_errors, err)


def parse_query(query: str) -> dict:
    out = {}
    for part in query.split("&"):
        key, sep, value = part.partition("=")
        if key not in QUERY_KEYS or not value:
            raise GateError(f"bad register query part {part!r}: use key=pattern[,pattern] with key in {sorted(QUERY_KEYS)}")
        if key in out:
            raise GateError(f"duplicate register query key {key!r}: one key=pattern[,pattern] per key")
        if "" in value.split(","):
            raise GateError(f"empty pattern in register query part {part!r}")
        out[key] = value.split(",")
    return out


def source_ids(root: str, source: str, findings: list) -> list[str]:
    if source.startswith("register:"):
        q = parse_query(source[len("register:"):])
        return sorted(f["id"] for f in findings
                      if all(any(fnmatch.fnmatchcase(str(QUERY_KEYS[k](f)), p) for p in pats) for k, pats in q.items()))
    path = _resolve(root, source)
    if not os.path.isfile(path):
        raise GateError(f"--source {source}: no such file")
    known = {f["id"] for f in findings}
    in_text = {t for t in TOKEN_RE.findall(_read(path)) if t in known}
    rel = posixpath.normpath(os.path.relpath(os.path.realpath(path), os.path.realpath(root)).replace(os.sep, "/"))
    by_source = {f["id"] for f in findings if _names(str(f.get("source") or ""), rel)}
    return sorted(in_text | by_source)


def _names(source: str, path: str) -> bool:
    """A register row's `source` names the repo-relative `path`: its first word (up to ' ', '#' or ':'),
    normalised like the argument, equals it."""
    head = re.split(r"[ #:]", source.strip(), maxsplit=1)[0]
    return posixpath.normpath(head) == path


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def _fields(res, root: str, pfx: str, t: Task, rows: dict, created: set) -> None:
    """Siblings, Gates and the pin step of one task (DEC-0016). An id with no register row adds no siblings:
    plan.id.<id> already fails it."""
    lid = f"{pfx}.siblings.{t.num}"
    if t.siblings is None:
        res.fail(lid, "no **Siblings:** line ('none -- <reason>' or the register rows' siblings, comma-separated)")
    elif t.siblings_error:
        res.fail(lid, t.siblings_error)
    else:
        need = [s for fid in t.findings or [] for s in (rows.get(fid) or {}).get("siblings") or []]
        missing = [s for s in need if not (t.siblings and _listed(s, t.siblings_value))]
        _verdict(res, lid, not missing, f"missing {', '.join(missing)} (register siblings of {', '.join(t.findings or [])})")
    lid = f"{pfx}.gates.{t.num}"
    if t.gates is None:
        res.fail(lid, "no **Gates:** line (run_all or gate names, comma-separated)")
    elif t.gates_error:
        res.fail(lid, t.gates_error)
    else:
        bad = [g for g in t.gates if g != "run_all" and not (
            GATE_NAME.match(g) and (os.path.isfile(os.path.join(root, "tools", "gates", f"gate_{g}.py"))
                                    or f"tools/gates/gate_{g}.py" in created))]
        _verdict(res, lid, not bad, f"unknown gate {', '.join(repr(g) for g in bad)}: no tools/gates/gate_<name>.py "
                                    "at the root or in a Create item of this or an earlier task")
    lid = f"{pfx}.pin.{t.num}"
    pins = [s for s in t.steps if PIN_TITLE.search(s.title)]
    if not t.findings:
        res.passed(lid)
    elif not pins:
        res.fail(lid, "no '- [ ] **Step N: ... pin ...**' step (the Findings line names ids)")
    else:
        named = any(PIN_NAME.search(line) for s in pins for line in s.body)
        _verdict(res, lid, named, "the pin step body does not name a test named test_<module>::test_<name>")


def check_plan(root: str, conf: dict, findings: list, plan_path: str, source: str | None,
               res: Result, pfx: str) -> None:
    full = _resolve(root, plan_path)
    if not os.path.isfile(full):
        res.fail(f"{pfx}.file", f"{plan_path}: no such plan file")
        return
    plan = parse_plan(_read(full))
    for key, rx in HEADERS.items():
        _verdict(res, f"{pfx}.header.{key}", key in plan.headers, f"no header line matching {rx.pattern!r}")
    if "spec" in plan.headers:
        m = SPEC_PATH.search(plan.headers["spec"])
        _verdict(res, f"{pfx}.header.spec-path", bool(m) and os.path.isfile(_resolve(root, m.group(1))),
                 "the **Spec:** line must name an existing file in backticks")
    if not plan.tasks:
        res.fail(f"{pfx}.tasks", "no '### Task N' headings outside code fences")
    routed = {}
    for t in plan.tasks:
        tid = f"{pfx}.task.{t.num}"
        if t.files_errors:
            res.fail(f"{pfx}.files.{t.num}", t.files_errors[0])
        else:
            _verdict(res, f"{pfx}.files.{t.num}", bool(t.files), "no '- Create|Modify|Test|Delete: `path`' lines under **Files:**")
        if t.mode_seen:
            _verdict(res, f"{tid}.mode", not t.mode_error, t.mode_error)
        if t.findings is None:
            res.fail(f"{tid}.findings", "no **Findings:** line (register ids, or 'none — <reason>')")
        else:
            _verdict(res, f"{tid}.findings", not t.findings_error, t.findings_error)
        for fid in t.findings or []:
            routed.setdefault(fid, []).append(t.num)
    rows = {f["id"]: f for f in findings}
    created = set()
    for t in plan.tasks:
        created.update(t.creates)
        _fields(res, root, pfx, t, rows, created)
    known = set(rows)
    for fid, nums in sorted(routed.items()):
        if fid not in known:
            res.fail(f"{pfx}.id.{fid}", "not in docs/superpowers/state/findings.yml")
        else:
            _verdict(res, f"{pfx}.id.{fid}", len(nums) == 1,
                     f"routed to tasks {', '.join(nums)}; each id belongs to exactly one task")
    _verdict(res, f"{pfx}.fence", not plan.fence_error, plan.fence_error)
    for err in plan.block_errors:
        res.fail(f"{pfx}.block.order", err)
    if not plan.block_errors:
        res.passed(f"{pfx}.block.order")
    cap = conf["block"]["max_tasks"]
    by_block = {}
    for t in plan.tasks:
        by_block.setdefault(t.block, []).append(t.num)
    for b, nums in sorted(by_block.items()):
        _verdict(res, f"{pfx}.block.{b}", len(nums) <= cap,
                 f"{len(nums)} tasks ({', '.join(nums)}) > block.max_tasks {cap}")
    pairs = 0
    for i, a in enumerate(plan.tasks):
        for b in plan.tasks[i + 1:]:
            if not (a.parallel and b.parallel and a.block == b.block):
                continue
            pairs += 1
            shared = sorted(set(a.files) & set(b.files))
            _verdict(res, f"{pfx}.overlap.{a.num}-{b.num}", not shared, f"PARALLEL tasks share {', '.join(shared)}")
    if not pairs:
        res.passed(f"{pfx}.overlap")
    for n, text in plan.placeholders:
        res.fail(f"{pfx}.placeholder.L{n}", text)
    if not plan.placeholders:
        res.passed(f"{pfx}.placeholders")
    if source is None:
        return
    ids = source_ids(root, source, findings)
    if not ids:
        res.fail(f"{pfx}.source", f"{source} yields no register ids")
    for fid in ids:
        nums = routed.get(fid, [])
        _verdict(res, f"{pfx}.coverage.{fid}", len(nums) == 1,
                 "in no task" if not nums else f"in tasks {', '.join(nums)}")


def _closed(text: str) -> bool:
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return bool(lines) and bool(CHECKPOINT_LINE.match(lines[-1]))


def _committed(root: str, rel: str) -> str:
    """The ledger at HEAD; empty when there is no HEAD or the file is not in it."""
    r = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=root, capture_output=True, text=True, check=False)
    return r.stdout if r.returncode == 0 else ""


def plans_in_flight(root: str) -> list[tuple[str, str]]:
    """(state-dir name, plan path) for every ledger whose last line is not a committed CHECKPOINT line."""
    state = os.path.join(root, STATE)
    if not os.path.isdir(state):
        raise GateError(f"{STATE}/ missing")
    out = []
    for ledger in sorted(glob.glob(os.path.join(state, "*", "ledger.md"))):
        name = os.path.basename(os.path.dirname(ledger))
        rel = os.path.relpath(ledger, root).replace(os.sep, "/")
        text = _read(ledger)
        first = text.splitlines()[0] if text else ""
        m = LEDGER_PLAN.match(first)
        if not m:
            raise GateError(f"{rel}: line 1 must be 'plan: <plan file>'")
        if not (_closed(text) and _closed(_committed(root, rel))):
            out.append((name, m.group(1)))
    return out


def check(args: list) -> Result:
    p = gate_args("gate_plan")
    p.add_argument("--plan")
    p.add_argument("--source")
    p.add_argument("--in-flight", action="store_true")
    p.add_argument("--json", action="store_true")
    a = p.parse_args(args)
    if bool(a.plan) == a.in_flight:
        raise GateError("give exactly one of --plan P or --in-flight")
    if a.in_flight and a.source:
        raise GateError("--source goes with --plan")
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    findings = registers.load_findings(root)
    res = Result("plan")
    if a.plan:
        check_plan(root, conf, findings, a.plan, a.source, res, "plan")
        return res
    pairs = plans_in_flight(root)
    if not pairs:
        res.passed("plan.in_flight")
    for name, path in pairs:
        check_plan(root, conf, findings, path, None, res, f"plan.{name}")
    return res


def main(argv=None):
    return run_gate("plan", check, argv)


if __name__ == "__main__":
    sys.exit(main())
