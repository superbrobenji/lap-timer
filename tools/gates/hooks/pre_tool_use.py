#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. At strict-delivery block b3 only the Bash branch gates:
`git commit` and `git push` run the register and claim gates on the git toplevel of the event's
`cwd`; every other command, and every other tool (Agent included, until S2 adds gate_dispatch),
exits 0 silently. The tree gated is the one the command names: the cwd, moved by a leading
`cd <dir> &&`/`;` and by each `git -C <dir>` (cumulative, as git does); a named directory that is
not inside a repo fails closed. A failing gate exits 2 with the gate's lines on stderr; any internal
error also exits 2 (fail closed). Limits: the match is textual (aliases, `eval`, `$(...)` and a
`-C $VAR` directory are not followed; an unexpandable -C word gates the tree before it).
"""
from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
_ARG = r"""(?:"[^"]*"|'[^']*'|(?:\\.|\S)+)"""   # a shell word; quotes may hold spaces, a backslash escapes
_OPT = (r"(?:-[Cc]\s+" + _ARG                                             # -C <dir>, -c <k=v>
        + r"|--(?:git-dir|work-tree|namespace|config-env)\s+" + _ARG        # separate-word long options
        + r"|--?[A-Za-z][\w-]*(?:=" + _ARG + r")?"                         # flags, --opt=value
        + r"|\$\S+)")                                                      # $X / ${X}: unknown, gate conservatively
GIT_VERB = re.compile(r"\bgit\b[\"']?(?P<opts>(?:\s+" + _OPT + r")*)\s+(?P<verb>commit|push)(?![\w-])")
GIT_DIR_OPT = re.compile(r"(?:^|\s)-C\s+(" + _ARG + r")")
LEADING_CD = re.compile(r"^\s*cd\s+(" + _ARG + r")\s*(?:&&|;)")
GATED = {"commit": ["register", "claims"], "push": ["register", "claims"]}


def _command(event: dict):
    """The Bash command with backslash-newline continuations joined; None for any other tool."""
    if event.get("tool_name") != "Bash":
        return None
    command = (event.get("tool_input") or {}).get("command")
    if not isinstance(command, str):
        raise ValueError("Bash event without tool_input.command")
    return command.replace("\\\n", " ")


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    command = _command(event)
    names = []
    for m in GIT_VERB.finditer(command or ""):
        for n in GATED[m.group("verb")]:
            if n not in names:
                names.append(n)
    return names


def _word(arg: str, base: str):
    """`base` joined with the directory a shell word names; None when it cannot be known statically."""
    if "$" in arg or "`" in arg:
        return None
    words = shlex.split(arg)
    if len(words) != 1:
        raise ValueError(f"cannot read directory word: {arg}")
    return os.path.normpath(os.path.join(base, os.path.expanduser(words[0])))


def target_dirs(event: dict) -> list:
    """Directories the gated git commands run in: the event cwd, moved by a leading `cd <dir> &&`
    or `;`, then by each `-C <dir>` of the command (cumulative, as git does). An unexpandable
    directory word keeps the directory before it (conservative: that tree is still gated)."""
    command = _command(event)
    base = event.get("cwd") or os.getcwd()
    lead = LEADING_CD.match(command or "")
    if lead:
        base = _word(lead.group(1), base) or base
    dirs = []
    for m in GIT_VERB.finditer(command or ""):
        cur = base
        for c in GIT_DIR_OPT.finditer(m.group("opts")):
            cur = _word(c.group(1), cur) or cur
        if cur not in dirs:
            dirs.append(cur)
    return dirs


def toplevel(directory: str) -> str:
    p = subprocess.run(["git", "-C", directory, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git -C {directory} rev-parse --show-toplevel: {p.stderr.strip()}")
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
    roots = []
    for d in target_dirs(event):    # resolve every tree first: an unusable one fails closed before any gate runs
        top = toplevel(d)
        if top not in roots:
            roots.append(top)
    failed = [r for r in (run_all.run(names, {"root": top}) for top in roots) if not r.ok()]
    for r in failed:
        print(r.render(False), file=sys.stderr)
    return 2 if failed else 0


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed: a broken hook blocks, it never waves through
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
