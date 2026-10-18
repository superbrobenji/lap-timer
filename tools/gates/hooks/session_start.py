#!/usr/bin/env python3
"""Claude Code SessionStart hook (spec §8). Prints to stdout (injected as context): whether the
gates are installed in the event's project, every handoff document, and the skill instruction;
installs the git hooks when the repository's pre-commit or pre-push hook (git rev-parse --git-path
hooks/<hook>) is not ours. SessionStart cannot block, and Claude Code puts only the stdout of an
exit-0 hook into the model's context. So every failure -- install-hooks.sh refusing or failing, a
missing or invalid cwd, any internal error -- is a `FAIL ...` line on stdout (the model sees it),
followed by the skill instruction, and the exit code is 0. The same line is copied to stderr, which
Claude Code shows to neither the model nor the owner on exit 0 (a debug-log copy only)."""
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


def hook_path(root: str, hook: str) -> str:
    p = subprocess.run(["git", "-C", root, "rev-parse", "--git-path", f"hooks/{hook}"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git rev-parse --git-path hooks/{hook}: {p.stderr.strip()}")
    return os.path.join(root, p.stdout.strip())


def ours(path: str) -> bool:
    try:
        with open(path, "rb") as fh:       # bytes: a foreign hook need not be UTF-8
            return MARKER.encode() in fh.read()
    except OSError:
        return False


def fail(code: str, reason: str) -> None:
    line = f"FAIL {code}: {reason}"
    print(line)
    print(line, file=sys.stderr)


def main() -> None:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    cwd = event.get("cwd")
    if not isinstance(cwd, str) or not cwd:
        fail("hook.input", "the event has no cwd")
        return
    if not os.path.isdir(cwd):
        fail("hook.input", f"cwd {cwd} is not a directory")
        return
    root = toplevel(cwd)
    if not os.path.isfile(os.path.join(root, "tools", "gates", "run_all.py")):
        print(f"strict-delivery: gates NOT installed in {root} (no tools/gates/run_all.py); apply the "
              "skill's judgment rules and state that the gates are missing")
        return
    try:
        import yaml  # noqa: F401
        yaml_state = "PyYAML ok"
    except ImportError:
        yaml_state = ("PyYAML MISSING for this python3: python3 -m pip install PyYAML==6.0.2 "
                      "(every gated git commit/push fails closed until then)")
    print(f"strict-delivery: gates installed in {root} (tools/gates/run_all.py); {yaml_state}")
    for h in sorted(glob.glob(os.path.join(root, "docs", "superpowers", "state", "*", "handoff.md"))):
        print(f"strict-delivery: handoff {os.path.relpath(h, root)} -- read it before any dispatch")
    if not all(ours(hook_path(root, h)) for h in ("pre-commit", "pre-push")):
        p = subprocess.run(["bash", os.path.join(root, "tools", "gates", "install-hooks.sh")],
                           cwd=root, capture_output=True, text=True, check=False)
        if p.stdout.strip():
            print(p.stdout.strip())
        if p.returncode != 0:
            fail("hook.install", f"install-hooks.sh exit {p.returncode}: {p.stderr.strip()}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # reported, never raised: SessionStart cannot block
        fail("hook.internal", f"{type(exc).__name__}: {exc}")
    print("strict-delivery: invoke the strict-delivery skill before any other action")
    sys.exit(0)
