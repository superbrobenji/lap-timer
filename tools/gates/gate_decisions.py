#!/usr/bin/env python3
"""gate_decisions -- the decisions register (spec §4.3, §5 "Decision classes", §7).

  python3 tools/gates/gate_decisions.py [--json] [--root <tree>] [--scan <path> ...]

Fails on: a malformed row; an answer whose `answered_at.by` is not the owner handle in
tools/gates/config.yml (every class in OWNER_ONLY_CLASSES is answered by the owner alone; the
schema check in registers.validate_decision rejects it and this gate files it under `owner`);
an asked_at / answered_at sha that is not a commit of the gated repository (DEC-0003, filed under
`schema`); and any reference to an unanswered decision id (DEC-NNNN) in a --scan file. A --scan
path that cannot be read (missing, a directory, not UTF-8) is a FAIL, never a skip. Row ids print
forgery-safe (entry-<i> when the id is malformed), an empty --root is an error, and a category
some row never reached prints no PASS line.
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import CODE_ROOT, GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config, registers  # noqa: E402

# spec §5: "Decision classes (owner-only, always)" -- every class in the register.
OWNER_ONLY_CLASSES = list(registers.DECISION_CLASSES)
REF = re.compile(r"DEC-\d{4}(?!\d)")
CATEGORIES = ["schema", "duplicate", "owner", "open-ref", "scan"]


def check(args: list) -> Result:
    ap = gate_args("gate_decisions")
    ap.add_argument("--scan", nargs="*", default=[], help="files that must not cite an unanswered decision")
    ns = ap.parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    owner = config.load(CODE_ROOT)["owner"]["handle"]
    decisions = registers.load_decisions(root)
    r = Result("decisions")
    exists = registers.memo_commit_exists()    # one git call per distinct sha
    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    seen = set()
    for i, d in enumerate(decisions):
        did = registers.row_id(d, i, registers.DECISION_ID)
        if did in seen:
            r.fail(f"decisions.duplicate.{did}", "id appears more than once")
        seen.add(did)
        errs = registers.validate_decision(d, owner=owner)
        for e in errs:
            r.fail(f"decisions.{'owner' if e.startswith(registers.OWNER_ANSWER_ERROR) else 'schema'}.{did}", e)
        if any(not e.startswith(registers.OWNER_ANSWER_ERROR) for e in errs):
            skipped.update(["schema", "owner", "open-ref"])    # the sha and answer checks need a well-formed row
            continue
        for e in registers.decision_commit_errors(d, root, exists):
            r.fail(f"decisions.schema.{did}", e)
    # only the owner's answer is an answer (spec §5): an agent-answered row is still unanswered
    unanswered = {d["id"] for d in decisions
                  if isinstance(d.get("id"), str) and registers.DECISION_ID.fullmatch(d["id"])
                  and (d.get("answer") is None or not isinstance(d.get("answered_at"), dict)
                       or d["answered_at"].get("by") != owner)}
    for path in ns.scan:
        full = path if os.path.isabs(path) else os.path.join(root, path)
        rel = registers.safe_text(os.path.relpath(full, root))
        try:
            registers.require_regular(root, full)    # opening a FIFO would hang the gate
        except GateError as exc:
            why = str(exc)[len(os.path.relpath(full, root)) + 2:]    # drop the "<path>: " prefix
            r.fail(f"decisions.scan.{rel}", why if why == "not a regular file" else f"cannot read: {why}")
            continue
        try:
            with open(full, encoding="utf-8") as fh:
                text = fh.read()
        except OSError as exc:
            r.fail(f"decisions.scan.{rel}", f"cannot read: {exc.strerror or exc}")
            continue
        except UnicodeDecodeError:
            r.fail(f"decisions.scan.{rel}", "cannot read: not valid UTF-8")
            continue
        for n, line in enumerate(text.split("\n"), 1):
            for did in sorted(set(REF.findall(line)) & unanswered):
                r.fail(f"decisions.open-ref.{did}", f"{rel}:{n} references unanswered decision {did}; "
                                                    "the owner answers it first")
    failed = {l.id.split(".")[1] for l in r.lines if l.status == "FAIL"}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"decisions.{cat}")
    return r


def main(argv=None):
    return run_gate("decisions", check, argv)


if __name__ == "__main__":
    sys.exit(main())
