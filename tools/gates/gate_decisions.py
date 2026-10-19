#!/usr/bin/env python3
"""gate_decisions -- the decisions register (spec §4.3, §5 "Decision classes", §7).

  python3 tools/gates/gate_decisions.py [--json] [--root <tree>] [--scan <path> ...]

Fails on: a malformed row; an answer whose `answered_at.by` is not the owner handle in
tools/gates/config.yml (every class in OWNER_ONLY_CLASSES is answered by the owner alone; the
schema check in registers.validate_decision rejects it and this gate files it under `owner`);
an asked_at / answered_at sha that is not a commit of the gated repository (DEC-0003, filed under
`schema`); and any reference to an unanswered decision id (DEC-NNNN) in a --scan file. A --scan
path that cannot be read (missing, a directory, not UTF-8) is a FAIL, never a skip, and
it withholds the `open-ref` PASS line (none of that file's references were examined); repeated --scan
options add up. Row ids print
forgery-safe (entry-<i> when the id is malformed), an empty --root is an error, and a category
some row never reached prints no PASS line (an empty or absent --scan list never prints PASS for
`scan` or `open-ref`).
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

# spec §5: "Decision classes (owner-only, always)" -- every class in the register.
OWNER_ONLY_CLASSES = list(registers.DECISION_CLASSES)
REF = re.compile(r"DEC-[0-9]{4}")    # DEC-0010 (umbrella pattern): four ASCII digits, no lookahead; DEC-00021 cites DEC-0002
CATEGORIES = ["schema", "duplicate", "owner", "open-ref", "scan"]


def check(args: list) -> Result:
    ap = gate_args("gate_decisions")
    ap.add_argument("--scan", nargs="*", action="extend", default=[], help="files that must not cite an unanswered decision")
    ns = ap.parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    owner = registers.owner_handle()
    decisions = registers.load_decisions(root)
    r = Result("decisions")
    exists = registers.memo_commit_exists()    # one git call per distinct sha
    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    if not ns.scan:    # nothing was scanned: no PASS line for a check that never ran
        skipped.update(["scan", "open-ref"])
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
    unanswered = {d["id"] for d in registers.open_decisions(decisions, owner=owner)
                  if isinstance(d.get("id"), str) and registers.DECISION_ID.fullmatch(d["id"])}
    for path in ns.scan:
        full = path if os.path.isabs(path) else os.path.join(root, path)
        rel = registers.safe_text(os.path.relpath(full, root))
        try:
            registers.require_regular(root, full)    # opening a FIFO would hang the gate
        except registers.PathError as exc:
            skipped.add("open-ref")    # this file's references were not examined
            r.fail(f"decisions.scan.{rel}", exc.reason if exc.not_regular else f"cannot read: {exc.reason}")
            continue
        try:
            with open(full, encoding="utf-8") as fh:
                text = fh.read()
        except OSError as exc:
            skipped.add("open-ref")
            r.fail(f"decisions.scan.{rel}", f"cannot read: {exc.strerror or exc}")
            continue
        except UnicodeDecodeError:
            skipped.add("open-ref")
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
