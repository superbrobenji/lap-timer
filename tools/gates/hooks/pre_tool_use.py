#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. At strict-delivery block b3 only the Bash branch gates:
`git commit` and `git push` run the register and claim gates on the git toplevel of the event's
`cwd`; every other command, and every other tool (Agent included, until S2 adds gate_dispatch),
exits 0 silently. A failing gate exits 2 with the gate's lines on stderr; any internal error also
exits 2 (fail closed). Limits: a commit run in another tree via `cd <dir> && git commit` is gated
here against the session's cwd -- the git pre-commit hook gates the tree actually committed.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
_ARG = r"""(?:"[^"]*"|'[^']*'|\S+)"""   # a shell word; quotes may hold spaces
GIT_VERB = re.compile(r"\bgit\b(?:\s+(?:-[Cc]\s+" + _ARG + r"|--(?:git-dir|work-tree|namespace)\s+" + _ARG
                      + r"|--?[A-Za-z][\w-]*(?:=" + _ARG + r")?))*\s+(commit|push)\b")
GATED = {"commit": ["register", "claims"], "push": ["register", "claims"]}


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    if event.get("tool_name") != "Bash":
        return []
    command = (event.get("tool_input") or {}).get("command")
    if not isinstance(command, str):
        raise ValueError("Bash event without tool_input.command")
    names = []
    for m in GIT_VERB.finditer(command):
        for n in GATED[m.group(1)]:
            if n not in names:
                names.append(n)
    return names


def project_root(event: dict) -> str:
    cwd = event.get("cwd") or os.getcwd()
    p = subprocess.run(["git", "-C", cwd, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git -C {cwd} rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    names = gates_for(event)
    if not names:
        return 0
    sys.path.insert(0, CODE_ROOT)
    from tools.gates import run_all   # lazy: an ungated command never needs PyYAML
    result = run_all.run(names, {"root": project_root(event)})
    if result.ok():
        return 0
    print(result.render(False), file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed: a broken hook blocks, it never waves through
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
