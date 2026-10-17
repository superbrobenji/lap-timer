#!/usr/bin/env python3
"""run_all -- every installed gate, one aggregated Result (umbrella plan, "run_all.py").

  python3 tools/gates/run_all.py [--only a,b] [--json] [--root <tree>]

A gate not yet implemented at this block is absent from GATES, never stubbed to PASS. A gate
module that is missing or raises is a FAIL `<name>.input`, never a skip. An empty --root, or a
root with no docs/superpowers/plans/*.md for `decisions` to scan, is a FAIL, never a pass.
"""
from __future__ import annotations

import argparse
import glob
import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Line, Result, repo_root, run_check  # noqa: E402

GATES = ["register", "claims", "decisions"]
PLANS_GLOB = os.path.join("docs", "superpowers", "plans", "*.md")


def gate_argv(name: str, args: dict) -> list:
    """argv for one gate: --root, the gate's defaults, then args[name] (a list) verbatim."""
    root = args["root"] if "root" in args else repo_root()
    if not isinstance(root, str) or not root.strip():
        raise GateError("--root is empty")
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
    for name in names:
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


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="run_all")
    ap.add_argument("--only", default=None, help="comma-separated gate names")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--root", default=None, help="tree to gate (default: repo_root())")
    ns = ap.parse_args(argv)
    names = GATES if ns.only is None else [n.strip() for n in ns.only.split(",") if n.strip()]
    result = run(names, {} if ns.root is None else {"root": ns.root})
    print(result.render(ns.json))
    return 0 if result.ok() else 1


if __name__ == "__main__":
    sys.exit(main())
