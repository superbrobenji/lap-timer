#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. At strict-delivery block b3 only the Bash branch gates:
`git commit` and `git push` run the register and claim gates on the git toplevel of the event's
`cwd`; every other command, and every other tool (Agent included, until S2 adds gate_dispatch),
exits 0 silently. The tree gated is the one the command names: the cwd, moved by a leading
`cd <dir> &&`/`;` and by each `git -C <dir>` (cumulative, as git does); a named directory that is
not inside a repo fails closed. A failing gate exits 2 with the gate's lines on stderr; any internal
error also exits 2 (fail closed). Limits: the match is textual (aliases, `eval`, `$(...)` and a
`-C $VAR` directory are not followed). A gated command whose tree it cannot tell (another `cd`,
`pushd`, a subshell, `-C`/`cd` with an expansion, --git-dir/--work-tree, GIT_DIR=) exits 2 with
`FAIL hook.target` (so does a nested shell: `bash -c`, `eval`, `xargs`, `env -C`); a gated verb
that only appears inside a quoted string or a heredoc body is data and is not blocked for that; an unexpandable option word before the verb (`git $X commit`) gates the cwd tree.
"""
from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
# A shell word: pieces that start with different characters (a quote, a backslash, anything else), so
# one word parses one way only -- overlapping alternatives backtrack exponentially on a failing match.
_ARG = r"""(?:"[^"]*"|'[^']*'|\\.|[^\s\\"'])+"""
_TAKES_WORD = r"(?:-[Cc]|--(?:git-dir|work-tree|namespace|config-env))(?=\s|$)"    # option whose value is the next word
_OPT = (r"(?:" + _TAKES_WORD + r"\s+" + _ARG                                      # -C <dir>, -c <k=v>, --git-dir <dir>
        + r"|(?!" + _TAKES_WORD + r")--?[A-Za-z][\w-]*(?:=" + _ARG + r")?"         # flags, --opt=value
        + r"|\$\S+)")                                                      # $X / ${X}: unknown, gate conservatively
GIT_VERB = re.compile(r"\bgit\b[\"']?(?P<opts>(?:\s+" + _OPT + r")*)\s+(?P<verb>commit|push)(?![\w-])")
GIT_DIR_OPT = re.compile(r"(?:^|\s)-C\s+(" + _ARG + r")")
LEADING_CD = re.compile(r"^\s*cd\s+(" + _ARG + r")\s*(?:&&|;)")
QUOTED = re.compile(r"\"[^\"]*\"|'[^']*'")
CD_WORD = re.compile(r"(?:^|[;&|(){}`\s])(?:cd|pushd|popd)(?=[\s;&|)]|$)")
TREE_ENV = re.compile(r"(?:^|[\s;&|(])(?:export\s+)?GIT_(?:DIR|WORK_TREE)=")
HEREDOC = re.compile(r"(<<-?[ \t]*(['\"]?)(\w+)\2[^\n]*\n)(.*?)^[ \t]*\3[ \t]*$", re.S | re.M)
_SHELL = r"(?:\S*/)?(?:sh|bash|zsh|dash|ksh)"
LAUNCHER = re.compile(
    r"(?:^|[;&|(\s])" + _SHELL + r"\s+(?:-[A-Za-z-]+\s+)*-[A-Za-z]*c\b"      # sh -c '...'
    r"|(?:^|[;&|(\s])" + _SHELL + r"\b[^;&|\n]*<<"                           # bash <<EOF
    r"|\|\s*" + _SHELL + r"\b"                                              # ... | sh
    r"|(?:^|[;&|(\s])(?:eval|xargs)\b"                                       # eval '...', xargs git push
    r"|(?:^|[;&|(\s])env\b[^;&|\n]*\s(?:-C|--chdir|-S|--split-string)\b")   # env -C <dir> git push
TREE_OPT = re.compile(r"(?:^|\s)--(?:git-dir|work-tree)(?:[=\s]|$)")


class TargetError(Exception):
    """A gated git command whose working tree this hook cannot determine: blocked, not guessed."""
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
    or `;`, then by each `-C <dir>` of the command (cumulative, as git does). A gated command whose
    tree cannot be told -- another `cd`, `pushd`/`popd`, a subshell, a `$` or backtick in a followed
    directory word, --git-dir/--work-tree, GIT_DIR=/GIT_WORK_TREE= -- raises TargetError."""
    command = _command(event)
    matches = list(GIT_VERB.finditer(command or ""))
    if not matches:
        return []
    why = "cannot tell which tree %s runs in" % matches[0].group("verb")
    hint = "; use git -C <dir> or a leading cd <dir> &&"
    base = event.get("cwd") or os.getcwd()
    rest = command
    lead = LEADING_CD.match(command)
    if lead:
        base = _word(lead.group(1), base)
        if base is None:
            raise TargetError(why + " (the leading cd directory is not a plain word)" + hint)
        rest = command[lead.end():]
    # A heredoc body is data (a commit message) unless a shell reads it: blank it first, then the quoted
    # words, so a message that mentions cd or GIT_DIR= passes. LAUNCHER sees `bash <<EOF` before the blanking.
    bare = QUOTED.sub('""', HEREDOC.sub(r"\1", rest))
    if CD_WORD.search(bare) or TREE_ENV.search(bare):
        raise TargetError(why + " (a cd, pushd, popd, subshell or GIT_DIR/GIT_WORK_TREE the hook does not follow)" + hint)
    if LAUNCHER.search(bare):
        raise TargetError(why + " (a nested shell, eval, xargs or env -C the hook does not follow)" + hint)
    dirs = []
    for m in matches:
        if TREE_OPT.search(m.group("opts")):
            raise TargetError(why + " (--git-dir/--work-tree)" + hint)
        cur = base
        for c in GIT_DIR_OPT.finditer(m.group("opts")):
            cur = _word(c.group(1), cur)
            if cur is None:
                raise TargetError(why + " (git -C with a shell expansion)" + hint)
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
    except TargetError as exc:
        print(f"FAIL hook.target: {exc}", file=sys.stderr)
        rc = 2
    except Exception as exc:  # fail closed: a broken hook blocks, it never waves through
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
