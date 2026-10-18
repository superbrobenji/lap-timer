#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. The Bash handler of .claude/settings.json has no `if` filter, so
this script sees every Bash call and alone decides what is gated. At strict-delivery block b3 only
the Bash branch gates: `git commit` and `git push` run the register and claim gates on the git
toplevel of the event's `cwd`; every other command, and every other tool (Agent included, until S2
adds gate_dispatch), exits 0 silently. The verb is matched textually, anywhere in the command (any
wrapper, assignment, redirection or path-qualified git word before it), after the data is blanked:
single-quoted spans, quoted-delimiter heredoc bodies, comments, and the text of double-quoted spans
and unquoted-delimiter heredoc bodies outside `$(...)` and backticks. So `grep 'git push' docs`
passes while `echo git push` and `echo "$(git push)"` are gated (a mention outside quotes costs a
blocked command: fail closed). The tree gated is the one the command names: the cwd, moved by a leading
`cd <dir> &&`/`;` and by each `git -C <dir>` (cumulative, as git does); a named directory that is
not inside a repo fails closed. A failing gate exits 2 with the gate's lines on stderr; any internal
error also exits 2 (fail closed). Limits: aliases and a `-C $VAR` directory are not followed; an
unterminated quote or heredoc is not blanked. A gated command whose tree it cannot tell (another `cd`,
`pushd`, a subshell, `-C`/`cd` with an expansion, --git-dir/--work-tree, GIT_DIR=) exits 2 with
`FAIL hook.target`, and so does a command that holds a launcher -- a nested shell or a moved one:
`bash -c`, `bash -o pipefail -c`, `bash <<EOF`, `| sh`, `eval`, `xargs`, `-exec`/`-execdir`,
`env -C`, `sudo -D` -- and names the verb anywhere, quoted or not. An unexpandable option word
before the verb (`git $X commit`) gates the cwd tree. Every scan is linear in the command length.
"""
from __future__ import annotations

import bisect
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
GIT_REST = re.compile(r"(?P<opts>(?:\s+" + _OPT + r")*)\s+(?P<verb>commit|push)(?![\w-])")   # after the git word
GIT_TEXT = re.compile(r"\bgit\b[\"']?" + GIT_REST.pattern)   # the verb anywhere in the text, quoted or not
GIT_DIR_OPT = re.compile(r"(?:^|\s)-C\s+(" + _ARG + r")")
LEADING_CD = re.compile(r"^\s*cd\s+(" + _ARG + r")\s*(?:&&|;)")
CD_WORD = re.compile(r"(?:^|[;&|(){}`\s])(?:cd|pushd|popd)(?=[\s;&|)]|$)")
TREE_ENV = re.compile(r"(?:^|[\s;&|(])(?:export\s+)?GIT_(?:DIR|WORK_TREE)=")
HEREDOC_OPEN = re.compile(r"(?<!<)<<(?!<)-?[ \t]*(?:(?P<q>['\"])(?P<qd>\w+)(?P=q)|(?P<bs>\\)?(?P<d>\w+))")   # <<EOF, <<-EOF, <<'EOF', <<\EOF; not <<< or <<<<
QUOTED = re.compile(r"\"([^\"]*)\"|'([^']*)'")
PLAIN = re.compile(r"[\w./=+-]+")       # a quoted word with no space or shell syntax in it: read as the bare word
SEP = re.compile(r"(\|\||&&|\|&|[;&|\n(){}`])")   # ends a simple command; a lone | or |& feeds the next one
SHELLS = {"sh", "bash", "zsh", "dash", "ksh"}
TREE_OPT = re.compile(r"(?:^|\s)--(?:git-dir|work-tree)(?:[=\s]|$)")


class Call:
    """One gated git command: the options between `git` and the verb, and the verb."""
    def __init__(self, opts: str, verb: str):
        self.opts = opts
        self.verb = verb


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


def _heredoc(m) -> tuple:
    """(delimiter, quoted) of a HEREDOC_OPEN match; <<'E', <<"E" and <<\\E are quoted (the body is literal)."""
    if m.group("q"):
        return m.group("qd"), True
    return m.group("d"), bool(m.group("bs"))


def _blank_data(command: str) -> str:
    """`command` with the data blanked, same length (every blanked character becomes a space, newlines
    stay): single-quoted spans; the bodies of heredocs with a quoted delimiter; comments (a `#` that
    starts a word, so not `${#X}` or `$#`); and, inside double-quoted spans and the bodies of heredocs
    with an unquoted delimiter, everything except the contents of `$(...)` and backticks, which run and
    stay code, recursively. A quoted span that is one plain word (`'git'`, `"commit"`) stays. An
    unterminated single quote, double quote or heredoc is not blanked (fail closed). One left-to-right
    pass; the terminator lines of all heredocs are indexed once, so any number of unclosed delimiters is
    still linear."""
    n = len(command)
    out = list(command)
    stack = []              # ["dq", start, plain], ["hd", until, start], ["sub", open parentheses], ["bt"]
    pending = []            # heredocs opened on the current line: (delimiter, quoted)
    jobs = []               # unquoted heredoc bodies still to scan: (start, until), in order
    after = 0               # where code resumes once the jobs are done
    terminators = None      # delimiter -> offsets of the lines that are exactly that word

    def blank(lo, hi):
        for k in range(lo, hi):
            if out[k] != "\n":
                out[k] = " "

    i = 0
    while i < n:
        c = command[i]
        top = stack[-1] if stack else None
        if top is not None and top[0] in ("dq", "hd"):
            if top[0] == "hd" and i >= top[1]:
                stack.pop()
                if jobs:
                    start, until = jobs.pop(0)
                    stack.append(["hd", until, start])
                    i = start
                else:
                    i = after
                continue
            if c == "$" and command.startswith("$(", i):
                stack.append(["sub", 1])
                if top[0] == "dq":
                    top[2] = False
                i += 2
            elif c == "`":
                stack.append(["bt"])
                if top[0] == "dq":
                    top[2] = False
                i += 1
            elif top[0] == "dq" and c == '"':
                stack.pop()
                if top[2]:
                    out[top[1] + 1:i] = command[top[1] + 1:i]       # a plain word stays
                i += 1
            else:
                step = 2 if c == "\\" else 1
                blank(i, min(i + step, n))
                if top[0] == "dq" and not (c.isalnum() or c in "_./=+-"):
                    top[2] = False
                i += step
            continue
        if c == "\\":
            i += 2
        elif c == "'":
            j = command.find("'", i + 1)
            if j < 0:
                i += 1                              # unterminated: stays code
            else:
                if not re.fullmatch(r"[\w./=+-]+", command[i + 1:j]):
                    blank(i + 1, j)
                i = j + 1
        elif c == '"':
            stack.append(["dq", i, True])
            i += 1
        elif c == "$" and command.startswith("$(", i):
            stack.append(["sub", 1])
            i += 2
        elif c == "`":
            if top is not None and top[0] == "bt":
                stack.pop()
            else:
                stack.append(["bt"])
            i += 1
        elif c == "#" and (i == 0 or command[i - 1] in " \t\n;&|("):
            j = command.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif c == "<" and HEREDOC_OPEN.match(command, i):
            m = HEREDOC_OPEN.match(command, i)
            pending.append(_heredoc(m))
            i = m.end()
        elif c == "(" and top is not None and top[0] == "sub":
            top[1] += 1
            i += 1
        elif c == ")" and top is not None and top[0] == "sub":
            top[1] -= 1
            if top[1] == 0:
                stack.pop()
            i += 1
        elif c == "\n" and pending:
            i += 1
            if terminators is None:
                terminators = {}
                pos = 0
                while pos <= n:
                    k = command.find("\n", pos)
                    k = n if k < 0 else k
                    word = command[pos:k].strip(" \t")
                    if re.fullmatch(r"\w+", word):
                        terminators.setdefault(word, []).append(pos)
                    pos = k + 1
            body_from = i
            scan = []
            for delim, quoted in pending:
                offs = terminators.get(delim, [])
                at = bisect.bisect_left(offs, body_from)
                if at == len(offs):
                    continue                        # no terminator line: the body stays code
                end = offs[at]
                if quoted:
                    blank(body_from, end)
                else:
                    scan.append((body_from, end))
                k = command.find("\n", end)
                body_from = n if k < 0 else k + 1
            pending = []
            if scan:
                jobs = scan[1:]
                after = body_from
                stack.append(["hd", scan[0][1], scan[0][0]])
                i = scan[0][0]
            else:
                i = max(i, body_from)
        else:
            i += 1
    for frame in stack:                             # an unterminated double quote or heredoc: fail closed
        if frame[0] == "dq":
            out[frame[1] + 1:] = command[frame[1] + 1:]
            break
    return "".join(out)


def git_calls(command: str) -> list:
    """The gated git commands (commit, push) the command may run: the textual match GIT_TEXT on the
    command with its data blanked (_blank_data), wherever the verb stands (an unquoted `echo git push`
    is gated, a mention in quotes, a comment or a heredoc body is not). The options are read from the
    original text at the same offsets, so a quoted directory still resolves. When nothing matches and the
    command holds a launcher (`bash -c`, `eval`, `xargs`, `| sh`, `find -exec`, `env -C` ...) whose payload
    may hold the verb, the textual matches of the unblanked command count, and target_dirs then blocks
    them as a tree it cannot tell."""
    calls = [Call(command[m.start("opts"):m.end("opts")], m.group("verb"))
             for m in GIT_TEXT.finditer(_blank_data(command))]
    if not calls and _launcher(_blank_heredocs(command)):
        calls = [Call(m.group("opts"), m.group("verb")) for m in GIT_TEXT.finditer(command)]
    return calls


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    command = _command(event)
    names = []
    for call in git_calls(command or ""):
        for n in GATED[call.verb]:
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


def _blank_heredocs(text: str) -> str:
    """`text` without heredoc bodies and their terminator lines; the line that opens each heredoc stays.
    One pass over the lines (linear). A body with no terminator line is kept, so it is scanned as code."""
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        i += 1
        for m in HEREDOC_OPEN.finditer(line):
            j = i
            while j < len(lines) and lines[j].strip(" \t") != _heredoc(m)[0]:
                j += 1
            if j == len(lines):
                return "\n".join(out + lines[i:])
            i = j + 1
    return "\n".join(out)


def _short_opts(word: str) -> str:
    """The letters of a short-option cluster (`-ec` -> `ec`); "" for any other word."""
    return word[1:] if word[:1] == "-" and word[1:].isalpha() else ""


def _unquote_plain(m) -> str:
    """A quoted word with no space or shell syntax in it (`'bash'`, `"-c"`) as the bare word; any other
    quoted string as `""`."""
    inner = m.group(1) if m.group(1) is not None else m.group(2)
    return inner if PLAIN.fullmatch(inner) else '""'


def _launcher(text: str) -> bool:
    """True when a simple command in `text` (heredoc bodies blanked) runs code this hook cannot read: a
    shell word (sh, bash, zsh, dash, ksh, by any path) followed in the same simple command by `-c` or a
    short-option cluster holding c (`-o pipefail -c`, `-ec`) or by a heredoc or here-string (`<<`), a
    shell fed by a pipe, `eval`, `xargs`, `-execdir`/`-okdir`, `env -C/--chdir/-S/--split-string` or
    `sudo -D/--chdir`. Quoted words without spaces count as bare words (`'bash' -c`), other quoted
    strings are data. One pass over the words (linear)."""
    parts = SEP.split(QUOTED.sub(_unquote_plain, text))
    for k in range(0, len(parts), 2):
        piped = k > 0 and parts[k - 1] in ("|", "|&")
        shell = env = sudo = False
        for w in parts[k].replace("<<", " << ").split():
            name = os.path.basename(w)
            opts = _short_opts(w)
            if name in ("eval", "xargs") or w in ("-exec", "-execdir", "-ok", "-okdir"):
                return True
            if shell and ("c" in opts or w.startswith("<<")):
                return True
            if env and (w.startswith(("--chdir", "--split-string")) or "C" in opts or "S" in opts
                        or w.startswith(("-C", "-S"))):
                return True
            if sudo and (w.startswith("--chdir") or "D" in opts or w.startswith("-D")):
                return True
            if name in SHELLS:
                if piped:
                    return True
                shell = True
            env = env or name == "env"
            sudo = sudo or name == "sudo"
    return False


def target_dirs(event: dict) -> list:
    """Directories the gated git commands run in: the event cwd, moved by a leading `cd <dir> &&`
    or `;`, then by each `-C <dir>` of the command (cumulative, as git does). A gated command whose
    tree cannot be told -- another `cd`, `pushd`/`popd`, a subshell, a `$` or backtick in a followed
    directory word, --git-dir/--work-tree, GIT_DIR=/GIT_WORK_TREE= -- raises TargetError."""
    command = _command(event)
    matches = git_calls(command or "")
    if not matches:
        return []
    why = "cannot tell which tree %s runs in" % matches[0].verb
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
    # words, so a message that mentions cd or GIT_DIR= passes. The opening line stays, so `bash <<EOF` is seen.
    code = _blank_heredocs(rest)
    bare = QUOTED.sub('""', code)
    if CD_WORD.search(bare) or TREE_ENV.search(bare):
        raise TargetError(why + " (a cd, pushd, popd, subshell or GIT_DIR/GIT_WORK_TREE the hook does not follow)" + hint)
    if _launcher(code):
        raise TargetError(why + " (a nested shell, eval, xargs, -execdir, env -C or sudo -D the hook does not follow)" + hint)
    dirs = []
    for m in matches:
        if TREE_OPT.search(m.opts):
            raise TargetError(why + " (--git-dir/--work-tree)" + hint)
        cur = base
        for c in GIT_DIR_OPT.finditer(m.opts):
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
