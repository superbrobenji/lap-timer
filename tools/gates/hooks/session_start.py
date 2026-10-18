#!/usr/bin/env python3
"""Claude Code SessionStart hook (spec §8). Prints to stdout (injected as context): whether the
gates are installed in the event's project, every handoff document, and the skill instruction;
installs the git hooks when .git/hooks/pre-commit is not ours. Exit 2 (shown to the owner) when
the install fails (install-hooks.sh refuses included) or the hook itself breaks."""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys

MARKER = "# strict-delivery-gates-hook"


def toplevel(cwd: str) -> str:
    p = subprocess.run(["git", "-C", cwd, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git -C {cwd} rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


def pre_commit_path(root: str) -> str:
    p = subprocess.run(["git", "-C", root, "rev-parse", "--git-path", "hooks/pre-commit"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git rev-parse --git-path hooks/pre-commit: {p.stderr.strip()}")
    return os.path.join(root, p.stdout.strip())


def ours(path: str) -> bool:
    try:
        with open(path, encoding="utf-8") as fh:
            return MARKER in fh.read()
    except OSError:
        return False


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    root = toplevel(event.get("cwd") or os.getcwd())
    if not os.path.isfile(os.path.join(root, "tools", "gates", "run_all.py")):
        print(f"strict-delivery: gates NOT installed in {root} (no tools/gates/run_all.py); apply the "
              "skill's judgment rules and state that the gates are missing")
        return 0
    try:
        import yaml  # noqa: F401
        yaml_state = "PyYAML ok"
    except ImportError:
        yaml_state = ("PyYAML MISSING for this python3: python3 -m pip install PyYAML==6.0.2 "
                      "(every gated git commit/push fails closed until then)")
    print(f"strict-delivery: gates installed in {root} (tools/gates/run_all.py); {yaml_state}")
    for h in sorted(glob.glob(os.path.join(root, "docs", "superpowers", "state", "*", "handoff.md"))):
        print(f"strict-delivery: handoff {os.path.relpath(h, root)} -- read it before any dispatch")
    rc = 0
    if not ours(pre_commit_path(root)):
        p = subprocess.run(["bash", os.path.join(root, "tools", "gates", "install-hooks.sh")],
                           cwd=root, capture_output=True, text=True, check=False)
        if p.stdout.strip():
            print(p.stdout.strip())
        if p.returncode != 0:
            print(f"FAIL hook.install: install-hooks.sh exit {p.returncode}: {p.stderr.strip()}", file=sys.stderr)
            rc = 2
    print("strict-delivery: invoke the strict-delivery skill before any other action")
    return rc


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
