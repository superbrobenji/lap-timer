#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol"; owner decision DEC-0005).

Reads one JSON event on stdin. The Bash handler of .claude/settings.json has no `if` filter, so this
script sees every Bash call. At strict-delivery block b3 only the Bash branch gates; every other
tool (Agent included, until S2 adds gate_dispatch) exits 0 silently. This hook is a best-effort
textual check; the git hooks (pre-commit, pre-push) are the enforcement (DEC-0005).

1. Hook bypasses: `core.hooksPath` anywhere (any case) or a GIT_CONFIG_PARAMETERS, _COUNT, _KEY_*,
   _GLOBAL, _SYSTEM or _NOSYSTEM assignment anywhere, in any Bash command; `--no-verify` (or an
   abbreviation git accepts), `-n` or a short-option cluster holding n in the arguments of a gated
   verb, quoted (`git "push"`) or not (arguments_end: up to an unquoted separator outside every
   parenthesis the arguments open, an unpaired `)`, quote or backtick, so the rest of an enclosing
   `$(...)` is not read; quotes, `$'...'`, `${...}`, backticks, `$(...)`, `<(...)`, `>(...)`,
   `$((...))` at any depth and the redirections `>&`, `<&`, `&>`, `>|` do not end them; `-n` is
   --no-verify for commit and --dry-run for push, blocked for both). Each exits 2 with
   `FAIL hook.bypass: <form found>` before any gate runs.
2. Gated: `git [options] commit|push`, matched in the text wherever it stands (after any separator,
   `$(`, backtick, `VAR=val`, wrapper or path: `/usr/bin/git`), the verb a whole word (`commits`,
   `pushd`, `commit-graph`, `--push-option` are not verbs). The options are git 2.50.1's global
   options (OPTION; each checked with `git <option> version`), read left to right as git does, plus
   a `$X` word; an option value may hold `$(...)` or backticks. A backslash-newline is removed
   first. No data/code analysis: a mention inside quotes, a heredoc or a comment (`echo "git push"`)
   runs the gates too -- on a healthy tree that costs well under a second.
3. Tree: each `-C <dir>` of the gated git word, applied in turn as git does, from the directory of
   a leading `cd <dir> &&` / `;` / newline, else from the event's `cwd`; `git -C <dir> rev-parse
   --show-toplevel` must succeed (`FAIL hook.target: <dir>: <git stderr>`), a missing cwd is
   `FAIL hook.input`. Every tree named is gated once: run_all register + claims; a failure exits 2
   with the gate lines on stderr. Any internal error exits 2 (`FAIL hook.internal`, fail closed).

Documented limits (DEC-0005): A verb hidden behind an expansion, alias, function, `eval`, a payload
file, or a wrapper the regex does not see (`$G push`, `git $V`, `eval "$CMD"`, `bash
script-that-pushes.sh`, `xargs ... git` without a literal `git push`) is not gated by this hook. The
git hooks gate it: pre-commit for commits, pre-push for pushes. Likewise a git word or verb with a
quoted or escaped piece (`g"it" push`, `git p"ush"`); a tree changed in a way the hook does not
follow (a later `cd`, `pushd`, a subshell, `GIT_DIR=`, `--git-dir`, `--work-tree`, `env -C`), where
the hook gates the tree it can name and the git hooks gate the real one; an option value the hook
cannot read as one word (`git -C $(dirname $(pwd)) commit`) or a global option git adds after
2.50.1, where the verb is not seen and so neither is a bypass flag after it; a quoted or escaped
bypass flag (`--no-"verify"`, `core.hooks\\Path`); a flag after a `case` pattern's `)` inside a
substitution (`$(case x in x) ...;; esac)`) or after a double-quoted `$(...)` that both nests
parentheses and quotes (`"$(a $(b ")"))"`), where the argument scan ends early; a bypass in a form
the text does not show (a config file or hook script edited by other means). A `-C` or `cd`
directory is not expanded: one holding a variable, `$(...)` or a backtick (`cd "$WT" &&`,
`git -C $(pwd)`) is `FAIL hook.target`, a block of a real command; name the directory literally. A
commit message that holds `-n`, `--no-verify` or `core.hooksPath` as a word, or a `-n` inside a
substitution in the arguments (`git commit -m "$(echo -n x)"`), is blocked as a bypass: write the
message to a file and use `git commit -F <file>`. Every scan is linear in the command length.
"""
from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
GATED = {"commit": ["register", "claims"], "push": ["register", "claims"]}
# One shell word: pieces that start with different characters, so a word parses one way only (linear).
VALUE = r"""(?:"[^"]*"|'[^']*'|\\.|\$\([^()]*\)|`[^`]*`|[^\s"'\\;&|<>()`])+"""
GIT_WORD = re.compile(r"(?<![\w.-])git[\"']?")       # `git`, `/usr/bin/git`, `$(git`, `"git"`
OPTION = re.compile(r"[ \t]+(?:-C[ \t]+(?P<v>" + VALUE + r")|-c[ \t]+" + VALUE +   # one option git takes before the verb
                    r"|--(?:git-dir|work-tree|namespace|config-env|attr-source|shallow-file)(?:=|[ \t]+)" + VALUE +
                    r"|--(?:exec-path|list-cmds)(?:=" + VALUE + r")?"
                    r"|--(?:no-pager|paginate|bare|html-path|man-path|info-path|literal-pathspecs|no-literal-pathspecs"
                    r"|glob-pathspecs|noglob-pathspecs|icase-pathspecs|no-replace-objects|no-optional-locks|no-lazy-fetch"
                    r"|no-advice)|-[pP]|\$\{?\w+\}?)")
GIT_VERB = re.compile(r"[ \t]+(?P<q>[\"']?)(commit|push)(?![\w-])")   # the verb right after the option run
LEADING_CD = re.compile(r"\s*cd[ \t]+(" + VALUE + r")[ \t]*(?:&&|;|\n)")
# one token of a gated verb's arguments (see arguments_end); every alternative starts differently or is
# tried once per position, so a scan is linear
ARG_TOKEN = re.compile(
    r"""(?:"(?:[^"\\$]|\\.|\$\((?:[^()"']|"[^"]*"|'[^']*')*\)|\$(?!\())*"   # "...", its $(...) may quote
    |"(?:[^"\\]|\\.)*"                                                     # "...", its $(...) may nest
    |\$'(?:[^'\\]|\\.)*'|'[^']*'|`[^`]*`|\$\{[^{}]*\}|\\.|[<>]&|&>|>\|)      # $'...', '...', `...`, ${...}, \x, redirections
    |(?P<stop>["'`])|(?P<sep>[;&|\n])|(?P<open>[$<>]?\()|(?P<close>\))
    |[^"'`\\;&|\n()<>$]+|.""", re.S | re.X)
BYPASS = (
    (re.compile(r"core\.hookspath", re.I), "core.hooksPath (it moves the git hooks away)"),
    (re.compile(r"(?<!\w)GIT_CONFIG_(?:PARAMETERS|COUNT|KEY_\w*|GLOBAL|SYSTEM|NOSYSTEM)="),
     "a GIT_CONFIG_* assignment (it can set core.hooksPath)"),
)
NO_VERIFY = re.compile(r"""(?:^|(?<=[\s"']))(--no-veri(?:fy?)?|-[A-Za-z]+)(?=[\s"'()<>;&|`]|$)""")   # after a gated verb


class Blocked(Exception):
    """A refusal with its own FAIL id (hook.bypass, hook.input, hook.target)."""
    def __init__(self, code: str, reason: str):
        super().__init__(reason)
        self.code = code


def bash_command(event: dict):
    """The Bash command with each backslash-newline removed (the shell joins the lines with nothing
    between them); None for any other tool."""
    if event.get("tool_name") != "Bash":
        return None
    command = (event.get("tool_input") or {}).get("command")
    if not isinstance(command, str):
        raise ValueError("Bash event without tool_input.command")
    return command.replace("\\\n", "")


def git_calls(command: str) -> list:
    """(options text, verb, verb end) of each gated `git [options] commit|push`, one per verb. The
    option run after a git word is memoised by position, so every scan is linear."""
    memo, calls, seen = {}, [], set()
    for g in GIT_WORD.finditer(command):
        pos, path = g.end(), []
        while pos not in memo:
            path.append(pos)
            m = OPTION.match(command, pos)
            if not m:
                memo[pos] = pos
                break
            pos = m.end()
        end = memo[pos]
        for p in path:
            memo[p] = end
        v = GIT_VERB.match(command, end)
        if v:
            stop = v.end() + 1 if v.group("q") and command.startswith(v.group("q"), v.end()) else v.end()
            if stop not in seen:      # a quoted verb (`git "push"`) ends after its closing quote
                seen.add(stop)
                calls.append((command[g.end():end], v.group(2), stop))
    return calls


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    names = []
    for _, verb, _ in git_calls(bash_command(event) or ""):
        names += [n for n in GATED[verb] if n not in names]
    return names


def arguments_end(command: str, pos: int) -> int:
    """Where the arguments of a gated verb that ends at `pos` end: at an unquoted separator outside every
    parenthesis the arguments open (`$(`, `<(`, `>(`, `(`, `$((` count), at an unpaired `)` or at an
    unpaired quote or backtick. So `out=$(git push; ls -n)` stops at the `;` and `git push $(a $(b)) -n`
    reads the -n. One token per step, each read once."""
    depth = 0
    while pos < len(command):
        t = ARG_TOKEN.match(command, pos)
        if t.group("stop") or (depth == 0 and (t.group("sep") or t.group("close"))):
            return pos
        depth += 1 if t.group("open") else -1 if t.group("close") else 0
        pos = t.end()
    return pos


def bypass(command: str, calls: list):
    """The hook bypass the command holds, or None (see the module docstring, item 1)."""
    for pattern, reason in BYPASS:
        if pattern.search(command):
            return reason
    scanned = -1
    for verb in sorted(end for _, _, end in calls):
        if verb < scanned:      # inside the arguments already read: its own arguments lie within them (linear)
            continue
        scanned = arguments_end(command, verb)
        for w in NO_VERIFY.finditer(command, verb, scanned):
            if w.group(1).startswith("--") or "n" in w.group(1):
                return "%s after a gated git verb (--no-verify; -n is --no-verify for commit, --dry-run for push)" % w.group(1)
    return None


def _directory(word: str, base: str) -> str:
    return os.path.normpath(os.path.join(base, os.path.expanduser(shlex.split(word)[0])))


def tree_for(event: dict, command: str, options: str = "") -> str:
    """The directory a git call runs in: the event's cwd, moved by a leading `cd <dir>`, then by each
    `-C <dir>` in `options` (as git applies them)."""
    cwd = event.get("cwd")
    if not isinstance(cwd, str) or not cwd:
        raise Blocked("hook.input", "the event has no cwd")
    lead = LEADING_CD.match(command)
    base = _directory(lead.group(1), cwd) if lead else cwd
    for m in OPTION.finditer(options):
        if m.group("v") is not None:
            base = _directory(m.group("v"), base)
    return base


def toplevel(directory: str) -> str:
    p = subprocess.run(["git", "-C", directory, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise Blocked("hook.target", f"{directory}: {p.stderr.strip()}")
    return p.stdout.strip()


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    command = bash_command(event)
    if command is None:
        return 0
    calls = git_calls(command)
    reason = bypass(command, calls)
    if reason:
        raise Blocked("hook.bypass", reason)
    if not calls:
        return 0
    dirs, roots = [], []
    for options, _, _ in calls:
        d = tree_for(event, command, options)
        if d not in dirs:
            dirs.append(d)
    for d in dirs:    # resolve every tree first: an unusable one fails closed before any gate runs
        top = toplevel(d)
        if top not in roots:
            roots.append(top)
    sys.path.insert(0, CODE_ROOT)
    from tools.gates import run_all   # lazy: an ungated command never needs PyYAML
    failed = [r for r in (run_all.run(gates_for(event), {"root": top}) for top in roots) if not r.ok()]
    for r in failed:
        print(r.render(False), file=sys.stderr)
    return 2 if failed else 0


if __name__ == "__main__":
    try:
        rc = main()
    except Blocked as exc:
        print(f"FAIL {exc.code}: {exc}", file=sys.stderr)
        rc = 2
    except Exception as exc:  # fail closed: a broken hook blocks, it never waves through
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
