#!/usr/bin/env python3
"""run_all -- every installed gate, one aggregated Result (umbrella plan, "run_all.py").

  python3 tools/gates/run_all.py [--json] [--only a,b] [--root <tree>]

A gate not yet implemented at this block is absent from GATES, never stubbed to PASS. A gate
module that is missing or raises is a FAIL `<name>.input`, never a skip. A --root that is empty, not
a path or not a directory, a gate named twice, and a root with no docs/superpowers/plans/*.md for
`decisions` to scan are each a FAIL, never a pass. The command line follows lib's gate
conventions (run_gate): --json first, help on stderr, bad arguments are `FAIL run_all.input`.
"""
from __future__ import annotations

import glob
import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Line, Result, gate_args, repo_root, run_check, run_gate  # noqa: E402

GATES = ["register", "claims", "decisions"]
PLANS_GLOB = os.path.join("docs", "superpowers", "plans", "*.md")


def gate_argv(name: str, args: dict) -> list:
    """argv for one gate: --root, the gate's defaults, then args[name] (a list) verbatim."""
    root = args["root"] if "root" in args else repo_root()
    if not isinstance(root, str):
        raise GateError("--root is not a path")
    if not root.strip():
        raise GateError("--root is empty")
    if not os.path.isdir(root):
        raise GateError(f"--root is not a directory: {root}")
    argv = ["--root", root]
    if name == "decisions":
        plans = sorted(glob.glob(os.path.join(root, PLANS_GLOB)))
        if not plans:
            raise GateError(f"no {PLANS_GLOB.replace(os.sep, '/')} under the root: nothing to scan")
        argv += ["--scan", *plans]
    return argv + list(args.get(name, []))


def _check_of(name: str):
    def check(argv: list) -> Result:
        return importlib.import_module(f"tools.gates.gate_{name}").check(argv)
    return check


def run(names: list, args: dict) -> Result:
    """Run each named gate; lines are prefixed '<name>.' unless they already are."""
    total = Result("run_all")
    ran = set()
    for name in names:
        if name in ran:
            total.fail("run_all.duplicate", f"gate {name!r} is named more than once")
            continue
        ran.add(name)
        if name not in GATES:
            total.fail("run_all.unknown", f"no gate named {name!r} (installed: {', '.join(GATES)})")
            continue
        res = run_check(name, lambda argv, n=name: _check_of(n)(gate_argv(n, args)), [])
        for l in res.lines:
            lid = l.id if l.id.startswith(name + ".") else f"{name}.{l.id}"
            total.lines.append(Line(l.status, lid, l.reason))
    if not total.lines:
        total.fail("run_all.empty", "no gate ran")
    return total


def check(args: list) -> Result:
    ap = gate_args("run_all")
    ap.add_argument("--only", default=None, help="comma-separated gate names")
    ns = ap.parse_args(args)
    names = GATES if ns.only is None else [n.strip() for n in ns.only.split(",") if n.strip()]
    return run(names, {} if ns.root is None else {"root": ns.root})


def main(argv=None) -> int:
    return run_gate("run_all", check, argv)


if __name__ == "__main__":
    sys.exit(main())
