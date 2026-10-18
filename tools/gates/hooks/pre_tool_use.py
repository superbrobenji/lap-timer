#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. The Bash handler of .claude/settings.json has no `if` filter, so
this script sees every Bash call and alone decides what is gated. At strict-delivery block b3 only
the Bash branch gates: `git commit` and `git push` in COMMAND POSITION (the first word of a simple
command, after `VAR=val`, shell keywords and wrappers such as `sudo`, `env`, `nohup`, `time`; also
inside `$(...)` and backticks; the git word may be path-qualified or quoted) run the register and
claim gates on the git toplevel of the event's `cwd`; every other command, and every other tool
(Agent included, until S2 adds gate_dispatch), exits 0 silently. A verb that is only mentioned
(`echo git push`, `grep 'git push'`, a commit message, a heredoc body, a comment) is data. The tree
gated is the one the command names: the cwd, moved by a leading `cd <dir> &&`/`;` and by each
`git -C <dir>` (cumulative, as git does); a named directory that is not inside a repo fails closed.
A failing gate exits 2 with the gate's lines on stderr; any internal error also exits 2 (fail
closed). Limits: the match is textual (aliases and a `-C $VAR` directory are not followed; a
wrapper not in WRAPPERS hides the verb). A gated command whose tree it cannot tell (another `cd`,
`pushd`, a subshell, `-C`/`cd` with an expansion, --git-dir/--work-tree, GIT_DIR=) exits 2 with
`FAIL hook.target`, and so does a command that holds a launcher -- a nested shell or a moved one:
`bash -c`, `bash -o pipefail -c`, `bash <<EOF`, `| sh`, `eval`, `xargs`, `-exec`/`-execdir`,
`env -C`, `sudo -D` -- and names the verb anywhere, quoted or not. An unexpandable option word
before the verb (`git $X commit`) gates the cwd tree. Every scan is linear in the command length.
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
GIT_REST = re.compile(r"(?P<opts>(?:\s+" + _OPT + r")*)\s+(?P<verb>commit|push)(?![\w-])")   # after the git word
GIT_TEXT = re.compile(r"\bgit\b[\"']?" + GIT_REST.pattern)   # the verb anywhere in the text, quoted or not
WORD = re.compile(r"[ \t]*(" + _ARG + r")")
ASSIGN = re.compile(r"[A-Za-z_]\w*=")
REDIRECT = re.compile(r"\d*[<>]\S*")
SEPARATORS = " \t\n;&|(){}`"
KEYWORDS = {"if", "then", "else", "elif", "while", "until", "do", "!"}
WRAPPERS = {"sudo", "doas", "command", "builtin", "nohup", "time", "timeout", "exec", "nice", "ionice", "setsid",
            "stdbuf", "env", "noglob", "caffeinate"}
VALUE_OPTS = {"-u", "-g", "-h", "-p", "-r", "-t", "-T", "-U", "-C", "-D", "-n", "-s", "-k", "-S", "-P", "-c", "-d", "-R"}
MAX_WORD = 256
MAX_PREFIX = 12      # words before the git word (assignments, wrappers and their options) that are read
GIT_DIR_OPT = re.compile(r"(?:^|\s)-C\s+(" + _ARG + r")")
LEADING_CD = re.compile(r"^\s*cd\s+(" + _ARG + r")\s*(?:&&|;)")
CD_WORD = re.compile(r"(?:^|[;&|(){}`\s])(?:cd|pushd|popd)(?=[\s;&|)]|$)")
TREE_ENV = re.compile(r"(?:^|[\s;&|(])(?:export\s+)?GIT_(?:DIR|WORK_TREE)=")
HEREDOC_OPEN = re.compile(r"(?<!<)<<(?!<)-?[ \t]*(['\"]?)(\w+)\1")   # <<EOF, <<-EOF, <<'EOF'; not <<< or <<<<
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


def _command_starts(command: str) -> list:
    """Offsets where a simple command starts in code that runs: the start of the text, after a separator
    (`; & | ( ) { }` newline, `&&`, `||`), after `$(` and after a backtick, also inside double quotes.
    Not inside single quotes, a quoted-delimiter heredoc body, a comment, or double-quoted text outside
    `$(...)`/backticks (a message or an argument). An unquoted heredoc body is data except around its
    `$(` and backticks (starts are added after each separator following the first one on a line). A
    heredoc with no terminator line is scanned as code. One pass (linear)."""
    n = len(command)
    starts = [0]
    stack = []          # ["dq"], ["sub", open parentheses], ["bt"]
    pending = []        # heredocs opened on this line: (delimiter, quoted)
    open_ended = set()  # delimiters with no terminator line from some offset on, so none after it either
    i = 0
    while i < n:
        c = command[i]
        top = stack[-1] if stack else None
        if top is not None and top[0] == "dq":
            if c == "\\":
                i += 2
            elif c == '"':
                stack.pop()
                i += 1
            elif command.startswith("$(", i):
                stack.append(["sub", 1])
                starts.append(i + 2)
                i += 2
            elif c == "`":
                stack.append(["bt"])
                starts.append(i + 1)
                i += 1
            else:
                i += 1
            continue
        if c == "\\":
            i += 2
        elif c == "'":
            j = command.find("'", i + 1)
            i = n if j < 0 else j + 1
        elif c == '"':
            stack.append(["dq"])
            i += 1
        elif command.startswith("$(", i):
            stack.append(["sub", 1])
            starts.append(i + 2)
            i += 2
        elif c == "`":
            if top is not None and top[0] == "bt":
                stack.pop()
            else:
                stack.append(["bt"])
            starts.append(i + 1)
            i += 1
        elif c == "#" and (i == 0 or command[i - 1] in SEPARATORS):
            j = command.find("\n", i)
            i = n if j < 0 else j
        elif c == "<" and HEREDOC_OPEN.match(command, i):
            m = HEREDOC_OPEN.match(command, i)
            if m.group(2) not in open_ended:
                pending.append((m.group(2), m.group(1) != ""))
            i = m.end()
        elif c in "(){};&|":
            if c == "(" and top is not None and top[0] == "sub":
                top[1] += 1
            elif c == ")" and top is not None and top[0] == "sub":
                top[1] -= 1
                if top[1] == 0:
                    stack.pop()
            starts.append(i + 1)
            i += 1
        elif c == "\n":
            i += 1
            while pending:
                delim, quoted = pending.pop(0)
                j = i
                while j < n:
                    k = command.find("\n", j)
                    line = command[j:] if k < 0 else command[j:k]
                    if line.strip(" \t") == delim:
                        break
                    j = n if k < 0 else k + 1
                if j >= n:
                    open_ended.add(delim)       # no terminator: the rest is scanned as code
                    continue
                if not quoted:
                    body = command[i:j]
                    seen = False
                    for m in re.finditer(r"\$\(|`|[;&|]", body):
                        seen = seen or m.group() in ("$(", "`")
                        if seen:
                            starts.append(i + m.end())
                i = j
            starts.append(i)
        else:
            i += 1
    return starts


def _git_word_end(command: str, start: int):
    """Offset just past the `git` word when the simple command starting at `start` runs git: the words
    before it are only keywords, assignments, redirections, wrappers (`sudo`, `env`, `nohup`, ...) with
    their options, and the word itself may be path-qualified or quoted. None otherwise."""
    pos = start
    while pos < len(command) and command[pos] in " \t":
        pos += 1
    wrapper = skip_value = False
    for _ in range(MAX_PREFIX + 1):
        m = WORD.match(command, pos, pos + MAX_WORD)       # a longer word is not a wrapper, an option or git
        if not m:
            return None
        word = m.group(1)
        plain = re.sub(r"[\\\"']", "", word)
        pos = m.end()
        if os.path.basename(plain) == "git":
            return pos
        if wrapper and skip_value:
            skip_value = False
        elif word in KEYWORDS or ASSIGN.match(word) or REDIRECT.fullmatch(word):
            pass
        elif os.path.basename(plain) in WRAPPERS:
            wrapper = True
        elif wrapper and (plain.startswith("-") or re.fullmatch(r"\d+[smhd]?", plain)):
            skip_value = plain in VALUE_OPTS
        else:
            return None
    return None


def git_calls(command: str) -> list:
    """The gated git commands (commit, push) the command runs. A verb counts in command position only
    (the first word of a simple command, after assignments and wrappers): `echo git push`, `grep 'git push'`,
    a commit message or a heredoc body are data. When the command also holds a launcher (`bash -c`, `eval`,
    `xargs`, `| sh`, `env -C` ...) whose payload may hold the verb, a textual match counts too, and
    target_dirs then blocks it as a tree it cannot tell."""
    calls = []
    for s in _command_starts(command):
        end = _git_word_end(command, s)
        m = GIT_REST.match(command, end) if end is not None else None
        if m:
            calls.append(Call(m.group("opts"), m.group("verb")))
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
            while j < len(lines) and lines[j].strip(" \t") != m.group(2):
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
