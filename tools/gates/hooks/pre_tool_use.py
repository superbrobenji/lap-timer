#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. The Bash handler of .claude/settings.json has no `if` filter, so
this script sees every Bash call and alone decides what is gated. At strict-delivery block b3 only
the Bash branch gates: `git commit` and `git push` run the register and claim gates on the git
toplevel of the event's `cwd`; every other command, and every other tool (Agent included, until S2
adds gate_dispatch), exits 0 silently.

A backslash-newline is removed first (the shell joins the lines with nothing between them:
`git p\\<newline>ush` is `git push`). One linear pass (Scan) then splits the command into data and
code. Data: single-quoted spans and escaped characters, ANSI-C `$'...'` spans (escapes decoded),
quoted-delimiter heredoc bodies, the text of double-quoted spans and unquoted-delimiter heredoc bodies outside `$(...)`, backticks and
`${...}`, and every character of a word that holds one of these. A quoted span or escaped character
made only of `[A-Za-z0-9_./=+-]` (or empty) is code, so quote removal joins it to its word: `g"it"
push`, `gi\\t push`, `git p"ush"` and `$'git' push` are `git push`. A comment (a `#` that starts a
word, never inside `${...}`) is neither. The pass also records each simple command (split at `;`,
`&`, `&&`, `||`, `|`, `|&`, newlines, `(`, `)`, `$(`, backticks, case patterns): its head word (after
`VAR=val` words and the keywords if then else elif do while until ! time), the command whose word
holds it, its pipeline, and where its first word with an expansion, glob or brace expansion starts.

1. Gated: `git [options] commit|push` matched textually in the code (data blanked, quotes removed),
   wherever it stands (any wrapper, assignment, redirection or path-qualified git word before it).
   So `echo "$(git push)"` and an unquoted mention (`echo git push`) are gated: a blocked echo is
   the cost of failing closed.
2. FAIL hook.target `cannot tell which tree <verb> runs in, or whether it runs`, unless every
   command on the way keeps it as text: a `git` word in data with a `commit`/`push` word after it in
   the same pipeline; a lone verb word in data only when the code holds a shell launcher
   (`bash -c 'echo push'`), so `python3 -c "print('push')"` or `pytest -k 'push or commit'` pass; a
   git word whose subcommand word holds an expansion, substitution, quoted text, brace expansion or
   glob, or is missing after a `$X` option or after `xargs git` (`git p*`, `git "$V"`, `git $V`,
   `echo push | xargs git`); a verb word after a word with an expansion, glob or brace expansion that
   is not the verb of a match of 1 (`$G push`, `{git,} push`). A command keeps it as text when its
   head is echo, printf (without -v), grep, egrep, fgrep, rg, gh, cat, tee, jq, awk (with no system,
   getline, | or -f in its words), xargs running echo, grep, egrep, fgrep, rg, cat, jq, head, tail or
   wc, or git (only with the subcommand commit, push, log, show, tag, notes, stash, merge, shortlog,
   status, diff, add, branch, rev-list, describe or blame and without -c/--config-env), the data is not
   in its `VAR=val` or redirection prefix, it has no `>(...)`, and it pipes only into those commands
   or head, tail, wc, sort, uniq, cut, less, more, tr, nl, column or fold; the same holds for each
   command whose word holds it (a substitution) and a subshell group passes it through. sed is not one
   (GNU sed `e` runs its pattern space). A heredoc body belongs to the command on its opener line. So
   `grep -rn 'git push' docs | head`, `grep 'git push' f | awk '{print $1}'`,
   `find . | xargs grep -l 'git push'`, `git log --grep='git push'`,
   `gh pr create --body "$(cat <<'EOF' ...)"` and `git commit -m "$(cat <<'EOF' ... EOF\n)"` (gated
   as a commit) pass, while bash -c, eval, watch, su -c, python3 -c, a heredoc fed to an interpreter,
   `| sh`, sed, sudo, env, xargs sh or an unknown command holding `git push` fail.

The tree gated is the one the command names: the cwd, moved by a leading `cd <dir> &&`/`;` and by
each `git -C <dir>` (cumulative, as git does); a named directory that is not inside a repo fails
closed. A gated command whose tree it cannot tell (another `cd`, `pushd`, a subshell, `-C`/`cd` with
an expansion, --git-dir/--work-tree, GIT_DIR=) or that holds a launcher in its code (`bash -c`,
`| sh`, a shell fed a heredoc, `eval`, `xargs`, `-exec`/`-execdir`, `env -C`/`-S`, `sudo -D`) exits 2
with `FAIL hook.target`. A `$X` word between git and the subcommand reads as an option, so
`git $X commit` gates the cwd tree and `git $X status` passes. A failing gate exits 2 with the
gate's lines on stderr; any internal error also exits 2. Limits: aliases and functions defined
earlier, a git word and a subcommand that are both variables or globs (`$G $V`, `$G p*`), a `-C $VAR`
directory, a payload written to a file and run later, and text a program assembles at run time
(base64, string concatenation in an interpreter) are not followed; an unterminated quote or heredoc
is code. Every scan is linear in the command length.
"""
from __future__ import annotations

import bisect
import functools
import json
import os
import re
import shlex
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
# A shell word of the original text (the leading cd): pieces that start with different characters (a
# quote, a backslash, anything else), so one word parses one way only -- overlapping alternatives
# backtrack exponentially on a failing match. The code view has no quote or backslash left: a word is \S+.
_ARG = r"""(?:"[^"]*"|'[^']*'|\\.|[^\s\\"'])+"""
# The git words of a view are read word by word (_git_words): every regex below matches one word.
TAKES_WORD = re.compile(r"-[Cc]|--(?:git-dir|work-tree|namespace|config-env)")    # option whose value is the next word
FLAG = re.compile(r"--?[A-Za-z][\w-]*(?:=\S+)?")                                  # -x, --x, --x=value
VERB = re.compile(r"(?:commit|push)(?![\w-])")                                   # a gated verb at a word start
GIT_END = re.compile(r"\bgit\Z")                     # a word ending in the git word (`git`, `/usr/bin/git`, `$(git`)
GIT_IN = re.compile(r"(?<![\w.])git[)}`,]*")         # also closing a substitution or brace (`$(which git)`, `{git,}`)
WORD = re.compile(r"\S+")
SEPARATOR = re.compile(r"[;&|)]")
UNKNOWN_WORD = re.compile(r"[$`_{*?\[]")      # an expansion, a substitution, data (`_`), a brace expansion or a glob
GIT_DIR_OPT = re.compile(r"(?:^|\s)-C\s+(\S+)")
LEADING_CD = re.compile(r"^\s*cd\s+(" + _ARG + r")\s*(?:&&|;)")
CD_WORD = re.compile(r"(?:^|[;&|(){}`\s])(?:cd|pushd|popd)(?=[\s;&|)]|$)")
TREE_ENV = re.compile(r"(?:^|[\s;&|(])(?:export\s+)?GIT_(?:DIR|WORK_TREE)=")
HEREDOC_OPEN = re.compile(r"(?<!<)<<(?!<)-?[ \t]*(?:(?P<q>['\"])(?P<qd>\w+)(?P=q)|(?P<bs>\\)?(?P<d>\w+))")   # <<EOF, <<-EOF, <<'EOF', <<\EOF; not <<< or <<<<
SEP = re.compile(r"(\|\||&&|\|&|[;&|\n(){}`])")   # ends a simple command; a lone | or |& feeds the next one
SHELLS = {"sh", "bash", "zsh", "dash", "ksh"}
TREE_OPT = re.compile(r"(?:^|\s)--(?:git-dir|work-tree)(?:[=\s]|$)")
PLAIN = re.compile(r"[\w./=+-]+")       # a quoted span or escaped character made of these is code (quote removal)
ASSIGN = re.compile(r"[A-Za-z_]\w*\+?=")
DATA_GIT = re.compile(r"(?<![\w.])git(?![\w-])")
DATA_VERB = re.compile(r"\b(?:commit|push)\b")
# commands that keep their data as text, and the ones that may also read it from a pipe
CONSUMERS = ("echo", "printf", "grep", "egrep", "fgrep", "rg", "gh", "cat", "tee", "jq", "git")
SINKS = ("head", "tail", "wc", "sort", "uniq", "cut", "less", "more", "tr", "nl", "column", "fold")
GIT_DATA_SUBCOMMANDS = ("commit", "push", "log", "show", "tag", "notes", "stash", "merge", "shortlog", "status",
                        "diff", "add", "branch", "rev-list", "describe", "blame")
KEYWORDS = ("if", "then", "else", "elif", "do", "while", "until", "!", "time")
XARGS_HOLDERS = ("echo", "grep", "egrep", "fgrep", "rg", "cat", "jq", "head", "tail", "wc")   # xargs <these>: a holder
XARGS_TAKES = ("-I", "-n", "-L", "-P", "-s", "-d", "-E", "-a")       # xargs options whose value is the next word
AWK_EXEC = re.compile(r"system|getline|\|")    # an awk program that can run a command (or read one from a pipe)
CODE_STOP = re.compile(r"[ \t\n;&|()<>'\"\\$`#{}*?\[]")
BRACE_STOP = re.compile(r"[}'\"$`\\]")
DQ_STOP = re.compile(r"[\"$`\\]")
DQB_STOP = re.compile(r"[\"$`\\}]")
HD_STOP = re.compile(r"[$`\\]")
ANSI_STOP = re.compile(r"[\\']")
ANSI_ESC = re.compile(r"\\(?:x([0-9A-Fa-f]{1,2})|u([0-9A-Fa-f]{1,4})|U([0-9A-Fa-f]{1,8})|([0-7]{1,3})|c(.)|(.))", re.S)
ANSI_SIMPLE = {"a": "\a", "b": "\b", "e": "\x1b", "E": "\x1b", "f": "\f", "n": "\n", "r": "\r", "t": "\t",
               "v": "\v", "\\": "\\", "'": "'", '"': '"', "?": "?"}
RUNS = re.compile(rb"\x01+|\x02+")    # runs of data or comment in the mask
# a data piece as a nested shell or an escape-decoding program may read it: \xHH, \uHHHH, \NNN decoded,
# any other backslash dropped, quote characters and a `$` before a quote dropped
DATA_PIECE = re.compile(r"\\(?:x([0-9A-Fa-f]{1,2})|u([0-9A-Fa-f]{1,4})|U([0-9A-Fa-f]{1,8})|([0-7]{1,3})|(.))"
                        r"|['\"]|[^\\'\"$]+|\$", re.S)
QUOTE_OR_BS = re.compile(r"['\"\\]")
DATA, COMMENT = 1, 2


class Call:
    """One gated git command: the options between `git` and the verb (code view), the verb, and the
    original text of each `-C` directory word."""
    def __init__(self, opts: str, verb: str, dirs: list):
        self.opts = opts
        self.verb = verb
        self.dirs = dirs


class TargetError(Exception):
    """A gated git command whose working tree this hook cannot determine: blocked, not guessed."""
GATED = {"commit": ["register", "claims"], "push": ["register", "claims"]}


def _command(event: dict):
    """The Bash command with each backslash-newline continuation removed (the shell joins the two lines with
    nothing between them: `git p\\<newline>ush` is `git push`); None for any other tool."""
    if event.get("tool_name") != "Bash":
        return None
    command = (event.get("tool_input") or {}).get("command")
    if not isinstance(command, str):
        raise ValueError("Bash event without tool_input.command")
    return command.replace("\\\n", "")


def _heredoc(m) -> tuple:
    """(delimiter, quoted) of a HEREDOC_OPEN match; <<'E', <<"E" and <<\\E are quoted (the body is literal)."""
    if m.group("q"):
        return m.group("qd"), True
    return m.group("d"), bool(m.group("bs"))


def _ansi_char(m) -> str:
    """One escape of an ANSI-C `$'...'` span, decoded as bash does."""
    if m.group(1):
        return chr(int(m.group(1), 16))
    if m.group(2) or m.group(3):
        code = int(m.group(2) or m.group(3), 16)
        return chr(code) if code <= 0x10FFFF else "?"
    if m.group(4):
        return chr(int(m.group(4), 8) & 0xFF)
    if m.group(5) is not None:
        return chr(ord(m.group(5)) & 0x1F)
    return ANSI_SIMPLE.get(m.group(6), "\\" + m.group(6))


def _dequote(word: str) -> str:
    """A word without its quote characters and backslashes (quote removal of a word with no expansion)."""
    return re.sub(r"\\(.)|['\"]", lambda m: m.group(1) or "", word)


class Seg:
    """One simple command: its head word, where that word starts, the command whose word holds this
    one (parent), the pipeline it belongs to (group), and the command its output is piped into."""
    __slots__ = ("parent", "group", "at", "head", "head_lo", "sub", "xcmd", "skip", "gitc", "printf_v", "awk_exec",
                 "procsub", "pipe_next", "pipe_bad", "bad", "expand_lo", "expand_before")

    def __init__(self, parent, group: int, at: int):
        self.parent, self.group, self.at = parent, group, at
        self.head = None
        self.head_lo = -1
        self.sub = None                     # git's subcommand (also after `xargs git`)
        self.xcmd = None                    # the command xargs runs
        self.skip = self.gitc = self.printf_v = self.awk_exec = self.procsub = self.pipe_bad = False
        self.pipe_next = None
        self.bad = None
        self.expand_lo = sys.maxsize        # where its first word with an expansion, glob or brace starts
        self.expand_before = False          # an enclosing command has such a word before this one


class Frame:
    """One nesting level of the scan: code (top level, `$(`, `(`, backticks), `${...}` in code (brace),
    a double-quoted span (dq), `${...}` inside data (dqbrace), an unquoted heredoc body (hd), or the
    heredoc bodies that follow one line (hdset)."""
    __slots__ = ("kind", "seg", "start", "closer", "owner", "until", "word_lo", "dirty", "quoted", "expand", "runs",
                 "case", "plain", "bodies", "idx", "resume")

    def __init__(self, kind: str, seg, start: int = 0, closer=None, owner=None, until: int = 0):
        self.kind, self.seg, self.start, self.closer, self.owner, self.until = kind, seg, start, closer, owner, until
        self.word_lo = None
        self.dirty = self.quoted = self.expand = False
        self.runs = []
        self.case = self.idx = self.resume = 0
        self.plain = True
        self.bodies = []


class Scan:
    """One left-to-right pass over a command: `mask` marks each data (1) and comment (2) character,
    `repl` holds the decoded ANSI-C spans, `segs` the simple commands, and `marks`/`owners` which
    command each offset belongs to (a heredoc body belongs to its opener's command). The terminator
    lines of all heredocs are indexed once, so any number of unclosed delimiters is still linear."""

    def __init__(self, command: str):
        self.s = command
        self.n = len(command)
        self.mask = bytearray(self.n)
        self.repl = []
        self.segs = []
        self.groups = 0
        self.marks, self.owners = [], []
        self.pending = []
        self.untils = []
        self.terminators = None
        self.limit = self.n
        self.stack = [Frame("code", self.new_seg(None, None, 0))]
        self.mark(0)
        self.run()

    # -- bookkeeping
    def new_seg(self, parent, group, at: int):
        if group is None:
            group = self.groups
            self.groups += 1
        seg = Seg(parent, group, at)
        self.segs.append(seg)
        return seg

    def mark(self, i: int):
        seg = self.stack[-1].seg
        if not self.owners or self.owners[-1] is not seg:
            self.marks.append(i)
            self.owners.append(seg)

    def blank(self, lo: int, hi: int, kind: int = DATA):
        if hi > lo:
            self.mask[lo:hi] = bytes((kind,)) * (hi - lo)

    def word(self, w, i: int, dirty: bool = False, expand: bool = False):
        if w is not None:
            if w.word_lo is None:
                w.word_lo = i
            w.dirty = w.dirty or dirty or expand
            w.expand = w.expand or expand

    def word_end(self, f, end: int):
        """Close the open word of code frame `f`: blank it whole when it holds data, and read it as the
        head of its command (or as a git option/subcommand, or printf's -v)."""
        lo = f.word_lo
        if lo is None:
            return
        f.word_lo = None
        dirty, quoted, expand, runs = f.dirty, f.quoted, f.expand, f.runs
        seg = f.seg
        if expand:
            seg.expand_lo = min(seg.expand_lo, lo)
        f.dirty = f.quoted = f.expand = False
        f.runs = []
        if quoted:
            for a, b in runs:
                self.blank(a, b)
        head = seg.head
        if not (head is None or head in ("printf", "awk") or head == "git" and seg.sub is None
                or head == "xargs" and (seg.xcmd is None or seg.xcmd == "git" and seg.sub is None)):
            return
        # a word with an expansion may hold substitutions (nested text): never sliced, so the pass stays linear
        word = "?" if dirty or quoted or expand else _dequote(self.s[lo:end])
        if head is None:
            if ASSIGN.match(self.s, lo, end) or (word in KEYWORDS and self.s[lo:end] == word):
                return
            seg.head, seg.head_lo = word, lo
            if word == "case":
                f.case += 1
            elif word == "esac" and f.case:
                f.case -= 1
        elif head == "printf":
            seg.printf_v = seg.printf_v or word == "-v"
        elif head == "awk":
            seg.awk_exec = seg.awk_exec or expand or word.startswith("-f") or bool(AWK_EXEC.search(self.s, lo, end))
        elif head == "xargs" and seg.xcmd is None:
            if seg.skip:
                seg.skip = False
            elif word in XARGS_TAKES:
                seg.skip = True
            elif not word.startswith("-"):
                seg.xcmd = os.path.basename(word)
        elif seg.skip:
            seg.skip = False
        elif word.startswith(("-c", "--config-env")):
            seg.gitc = True
            seg.skip = word in ("-c", "--config-env")
        elif word in ("-C", "--git-dir", "--work-tree", "--namespace"):
            seg.skip = True
        elif not word.startswith("-"):
            seg.sub = word

    def next_seg(self, f, at: int, pipe: bool):
        old = f.seg
        seg = self.new_seg(old.parent, old.group if pipe or old.parent is not None else None, old.at)
        if pipe:
            old.pipe_next = seg
        f.seg = seg
        self.mark(at)

    def child(self, f, at: int, j: int, closer: str) -> int:
        self.stack.append(Frame("code", self.new_seg(f.seg, f.seg.group, at), start=at, closer=closer))
        self.mark(j)
        return j

    def pop(self, i: int, j: int):
        f = self.stack.pop()
        if f.kind == "code":
            self.word_end(f, i)
        self.mark(j)

    def redirect(self, f, i: int):
        if f.seg.head is None:
            f.seg.head, f.seg.head_lo = "<redirect>", i

    # -- the loop
    def run(self):
        stack, n = self.stack, self.n
        i = 0
        while True:
            top = stack[-1]
            if top.kind == "hd" and i >= top.until:
                stack.pop()
                self.untils.pop()
                self.mark(i)
                continue
            if self.untils and i >= self.untils[-1]:     # a frame inside a heredoc body ends with the body
                self.pop(i, i)
                continue
            if top.kind == "hdset":
                i = self.next_body(top, i)
                continue
            if i >= n:
                break
            self.limit = self.untils[-1] if self.untils else n
            i = getattr(self, "k_" + top.kind)(top, i)
        first = next((f for f in stack if f.kind in ("dq", "dqbrace")), None)
        for f in reversed(stack):
            if f.kind == "code":
                self.word_end(f, n)
        if first is not None:                            # an unterminated double quote: code (fail closed)
            self.mask[first.start + 1:] = bytes(n - first.start - 1)

    # -- code: the top level, $(...), (...), backticks
    def k_code(self, f, i: int) -> int:
        s = self.s
        c = s[i]
        if c in " \t":
            self.word_end(f, i)
            return i + 1
        if c == "\n" or c == ";":
            self.word_end(f, i)
            self.next_seg(f, i + 1, False)
            return self.heredocs(f, i + 1) if c == "\n" and self.pending else i + 1
        if c == "&" and not (i and s[i - 1] in "<>") and not s.startswith(">", i + 1):
            self.word_end(f, i)
            j = i + 2 if s.startswith("&&", i) else i + 1
            self.next_seg(f, j, False)
            return j
        if c == "|":
            self.word_end(f, i)
            if s.startswith("||", i):
                self.next_seg(f, i + 2, False)
                return i + 2
            j = i + 2 if s.startswith("|&", i) else i + 1
            self.next_seg(f, j, True)
            return j
        if c == "(":
            if f.word_lo is None and f.seg.head is None:
                f.seg.head, f.seg.head_lo = "(", i         # a subshell group: its commands hold the data
            else:
                self.word(f, i, expand=True)
            return self.child(f, i, i + 1, ")")
        if c == ")":
            self.word_end(f, i)                         # first: an `esac` word ends the case
            if f.closer == ")" and not f.case:
                self.pop(i, i + 1)
                return i + 1
            self.next_seg(f, i + 1, False)              # a case pattern, or a stray parenthesis
            return i + 1
        if c == "`":
            if f.closer == "`":
                self.pop(i, i + 1)
                return i + 1
            self.word(f, i, expand=True)
            return self.child(f, i, i + 1, "`")
        if c == "'":
            return self.single(i, f)
        if c == '"':
            return self.open_dq(f, i, f)
        if c == "$":
            return self.dollar(f, i, f)
        if c == "#" and f.word_lo is None:
            j = s.find("\n", i, self.limit)
            j = self.limit if j < 0 else j
            self.blank(i, j, COMMENT)
            return j
        if c in "<>":
            m = HEREDOC_OPEN.match(s, i) if c == "<" else None
            if m:
                self.word_end(f, i)
                self.redirect(f, i)
                self.pending.append(_heredoc(m) + (f.seg,))
                return m.end()
            if s.startswith("(", i + 1):                 # <(...) / >(...)
                self.word(f, i, expand=True)
                f.seg.procsub = f.seg.procsub or c == ">"
                return self.child(f, i, i + 2, ")")
            self.word_end(f, i)
            self.redirect(f, i)
            return i + 1
        if c == "\\":
            return self.escape(i, f)
        if c in "{}*?[":
            self.word(f, i, expand=True)
            f.runs.append((i, i + 1))
            return i + 1
        m = CODE_STOP.search(s, i + 1, self.limit)
        j = m.start() if m else self.limit
        self.word(f, i)
        f.runs.append((i, j))
        return j

    def single(self, i: int, w) -> int:
        j = self.s.find("'", i + 1, self.limit)
        self.word(w, i)
        if j < 0:                                       # unterminated: code
            if w is not None:
                w.runs.append((i, i + 1))
            return i + 1
        if j == i + 1 or PLAIN.fullmatch(self.s, i + 1, j):
            if w is not None:
                w.runs.append((i + 1, j))
        else:
            self.blank(i + 1, j)
            if w is not None:
                w.quoted = True
        return j + 1

    def escape(self, i: int, w) -> int:
        self.word(w, i)
        if PLAIN.fullmatch(self.s, i + 1, min(i + 2, self.n)):
            if w is not None:
                w.runs.append((i, i + 2))
        else:
            self.blank(i, min(i + 2, self.n))
            if w is not None:
                w.quoted = True
        return i + 2

    def open_dq(self, f, i: int, w) -> int:
        self.word(w, i)
        self.stack.append(Frame("dq", f.seg, start=i, owner=w))
        return i + 1

    def dollar(self, f, i: int, w) -> int:
        nxt = self.s[i + 1:i + 2]
        self.word(w, i, True, nxt != "'")
        if nxt == "(":
            return self.child(f, i, i + 2, ")")
        if nxt == "{":
            self.stack.append(Frame("brace", f.seg, start=i, owner=w))
            return i + 2
        if nxt == "'":
            return self.ansi(i, w)
        return i + 1

    def ansi(self, i: int, w) -> int:
        j = i + 2
        while True:
            m = ANSI_STOP.search(self.s, j, self.limit)
            if m is None:                               # unterminated: `$` then an unterminated single quote
                return i + 1
            if self.s[m.start()] != "\\":
                break
            j = m.start() + 2
        k = m.start()
        text = ANSI_ESC.sub(_ansi_char, self.s[i + 2:k])
        plain = not text or bool(PLAIN.fullmatch(text))
        if plain:
            if w is not None:
                w.runs.append((i, i + 1))
        else:
            self.blank(i + 2, k)
            if w is not None:
                w.quoted = True
        self.repl.append((i, k + 1, text, plain))
        return k + 1

    # -- ${...} in code: code, but no word or command breaks and no comments
    def k_brace(self, f, i: int) -> int:
        c = self.s[i]
        if c == "}":
            self.stack.pop()
            return i + 1
        if c == "'":
            return self.single(i, f.owner)
        if c == '"':
            return self.open_dq(f, i, f.owner)
        if c == "$":
            return self.dollar(f, i, f.owner)
        if c == "`":
            return self.child(f, i, i + 1, "`")
        if c == "\\":
            return self.escape(i, f.owner)
        m = BRACE_STOP.search(self.s, i + 1, self.limit)
        return m.start() if m else self.limit

    # -- data with substitutions: double quotes, ${...} inside them, unquoted heredoc bodies
    def k_dq(self, f, i: int) -> int:
        s = self.s
        c = s[i]
        if c == '"' and f.kind == "dq":
            self.stack.pop()
            if f.owner is None:                         # nested in data: the quote is data too
                self.blank(i, i + 1)
            elif f.plain:                               # one plain word: code (quote removal)
                self.mask[f.start + 1:i] = bytes(i - f.start - 1)
                f.owner.runs.append((f.start + 1, i))
            else:
                f.owner.quoted = True
            return i + 1
        if c == '"' and f.kind == "dqbrace":            # "${x:-"..."}": a nested double-quoted span
            self.blank(i, i + 1)
            self.stack.append(Frame("dq", f.seg, start=i))
            return i + 1
        if c == "}" and f.kind == "dqbrace":
            self.blank(i, i + 1)
            self.stack.pop()
            return i + 1
        if c in "$`" and f.owner is not None:          # an expansion inside a code-level double quote
            f.owner.expand = True
        if c == "$" and s.startswith("(", i + 1):
            f.plain = False
            return self.child(f, i, i + 2, ")")
        if c == "$" and s.startswith("{", i + 1):
            f.plain = False
            self.blank(i, i + 2)
            self.stack.append(Frame("dqbrace", f.seg, start=i))
            return i + 2
        if c == "`":
            f.plain = False
            return self.child(f, i, i + 1, "`")
        if c == "\\":
            f.plain = False
            j = min(i + 2, self.limit)
            self.blank(i, j)
            return j
        stop = HD_STOP if f.kind == "hd" else DQ_STOP if f.kind == "dq" else DQB_STOP
        m = stop.search(s, i + 1, self.limit)
        j = m.start() if m else self.limit
        if f.plain and not PLAIN.fullmatch(s, i, j):
            f.plain = False
        self.blank(i, j)
        return j

    k_dqbrace = k_dq
    k_hd = k_dq

    # -- heredocs
    def heredocs(self, f, i: int) -> int:
        """Queue the bodies of the heredocs opened on the line that ends at `i`. A delimiter with no
        terminator line (before the end of an enclosing body) leaves that body and the rest as code."""
        s, n = self.s, self.n
        if self.terminators is None:
            self.terminators = {}
            pos = 0
            while pos <= n:
                k = s.find("\n", pos)
                k = n if k < 0 else k
                word = s[pos:k].strip(" \t")
                if re.fullmatch(r"\w+", word):
                    self.terminators.setdefault(word, []).append(pos)
                pos = k + 1
        bodies = []
        body_from = i
        for delim, quoted, owner in self.pending:
            offs = self.terminators.get(delim, [])
            at = bisect.bisect_left(offs, body_from)
            if at == len(offs) or offs[at] >= self.limit:
                break
            end = offs[at]
            bodies.append((body_from, end, quoted, owner))
            k = s.find("\n", end)
            body_from = n if k < 0 else k + 1
        self.pending = []
        if bodies:
            hs = Frame("hdset", f.seg)
            hs.bodies, hs.resume = bodies, body_from
            self.stack.append(hs)
        return i

    def next_body(self, f, i: int) -> int:
        while f.idx < len(f.bodies):
            lo, hi, quoted, owner = f.bodies[f.idx]
            f.idx += 1
            if lo < i:                                  # already scanned as code: stays code
                continue
            if quoted:
                self.blank(lo, hi)
                self.marks.append(lo)
                self.owners.append(owner)
                i = hi
                continue
            self.stack.append(Frame("hd", owner, start=lo, until=hi))
            self.untils.append(hi)
            self.mark(lo)
            return lo
        self.stack.pop()
        i = max(i, f.resume)
        self.mark(i)
        return i

    def owner(self, at: int):
        return self.owners[bisect.bisect_right(self.marks, at) - 1]


def _view(scan: Scan, code: bool) -> tuple:
    """(text, offsets): the command after quote removal, with each character's offset in the command.
    Code view: each run of data is one `_` (a word character, so it joins its word) and comments are
    dropped. Data view: the data is kept too (without quote characters and backslashes)."""
    s, n, mask, repl = scan.s, scan.n, scan.mask, scan.repl
    runs = [(m.start(), m.end(), mask[m.start()]) for m in RUNS.finditer(mask)]
    out, pos = [], []
    r = k = i = 0
    while i < n:
        nxt = repl[r][0] if r < len(repl) else n
        if i == nxt:
            lo, hi, text, plain = repl[r]
            r += 1
            if code and (not plain or mask[lo]):
                if text:
                    out.append("_")
                    pos.append(lo)
            elif text:
                at = lo if plain else lo + 2
                out.append(text)
                pos.extend([at] * len(text))
            i = hi
            continue
        while k < len(runs) and runs[k][1] <= i:
            k += 1
        if k < len(runs) and runs[k][0] <= i:
            j = min(runs[k][1], nxt)
            if runs[k][2] == DATA:
                if code:
                    out.append("_")
                    pos.append(i)
                else:
                    for t in DATA_PIECE.finditer(s, i, j):
                        g = t.group()
                        if g[0] == "\\":
                            num = t.group(1) or t.group(2) or t.group(3)
                            g = (chr(min(int(num, 16), 0x10FFFF)) if num else chr(int(t.group(4), 8) & 0xFF)
                                 if t.group(4) else t.group(5))
                        if g in ("'", '"') or g == "$" and s[t.end():t.end() + 1] in ("'", '"'):
                            continue
                        out.append(g)
                        pos.extend(range(t.start(), t.start() + len(g)) if len(g) > 1 else [t.start()])
            i = j
            continue
        c = s[i]
        if c in "'\"":
            i += 1
            continue
        if c == "\\":
            if i + 1 < n:
                out.append(s[i + 1])
                pos.append(i + 1)
            i += 2
            continue
        j = min(runs[k][0] if k < len(runs) else n, nxt)
        m = QUOTE_OR_BS.search(s, i, j)
        j = m.start() if m else j
        out.append(s[i:j])
        pos.extend(range(i, j))
        i = j
    return "".join(out), pos


def _orig(s: str, pos: list, a: int, b: int) -> str:
    """The command text of the code-view span [a, b), with the quote characters around it."""
    lo = pos[a - 1] + 1 if a > 0 else 0
    hi = pos[b] if b < len(pos) else len(s)
    return s[lo:hi].strip()


def _consumer(seg) -> bool:
    if seg.head == "git":
        return not seg.gitc and seg.sub in GIT_DATA_SUBCOMMANDS
    if seg.head == "printf":
        return not seg.printf_v
    if seg.head == "awk":
        return not seg.awk_exec
    if seg.head == "xargs":
        return seg.xcmd in XARGS_HOLDERS
    return seg.head in CONSUMERS


def _name(head) -> str:
    if head is None:
        return "a command with no name (only assignments or redirections)"
    if head == "?":
        return "a command the hook cannot name (an expansion or quoted word)"
    if head == "(":
        return "the subshell group"
    if head == "<redirect>":
        return "a command that starts with a redirection"
    return "`%s`" % head[:40]


def _judge(segs: list):
    """Set seg.bad: why a command may run what its data names (None: it, and every command whose word
    holds it, keeps the data as text); and seg.expand_before."""
    for seg in reversed(segs):
        nxt = seg.pipe_next
        seg.pipe_bad = nxt is not None and (nxt.procsub or nxt.pipe_bad or not (_consumer(nxt) or nxt.head in SINKS))
    for seg in segs:
        p = seg.parent
        if p is not None and (p.head is None or seg.at < p.head_lo):
            why = "a `VAR=...` assignment or redirection prefix"
        elif seg.head != "(" and not _consumer(seg):
            why = _name(seg.head)
        elif seg.procsub:
            why = "the >(...) process substitution of %s" % _name(seg.head)
        elif seg.pipe_bad:
            why = "the command that %s pipes its output into" % _name(seg.head)
        else:
            why = None
        seg.bad = why or (p.bad if p is not None else None)
        seg.expand_before = p is not None and (p.expand_lo <= seg.at or p.expand_before)


def _git_words(view: str) -> tuple:
    """(calls, unknown) of a view, in one linear pass over its words: `end[i]` (a dynamic programme from
    the right) is the first word at or after i that is not an option, so no word is re-read per git word.
    Options: `-C`, `-c`, `--git-dir`, `--work-tree`, `--namespace`, `--config-env` with the next word as
    value, `-x`/`--x[=v]` flags, and `$X` words (unknown, read as options). calls: (git offset, options
    start, verb offset) of each `git [options] commit|push`, git ending its word (`/usr/bin/git`, `$(git`).
    unknown: (git offset, subcommand offset) of each git word (also `$(which git)`, `{git,}`, `git$V`)
    whose subcommand word holds an expansion, substitution, data (`_`), brace expansion or glob, or is
    missing after a `$X` word (a `$X;` word ends the command)."""
    words = [(m.start(), m.end()) for m in WORD.finditer(view)]
    n = len(words)
    kinds = []                                      # 2 takes a value, 1 flag, 3 $X, 4 $X ending the command
    for s, e in words:
        if TAKES_WORD.fullmatch(view, s, e):
            kinds.append(2)
        elif FLAG.fullmatch(view, s, e):
            kinds.append(1)
        elif view.startswith("$", s) and e - s > 1 and view[s + 1] not in ";&|)":
            kinds.append(4 if SEPARATOR.search(view, s, e) else 3)
        else:
            kinds.append(0)
    end, end_u, ended = [n] * (n + 2), [n] * (n + 2), [False] * (n + 2)
    for i in range(n - 1, -1, -1):
        k = kinds[i]
        if k == 2 and i + 1 < n:
            end[i], end_u[i], ended[i] = end[i + 2], end_u[i + 2], ended[i + 2]
        elif k in (1, 3):
            end[i], end_u[i], ended[i] = end[i + 1], end_u[i + 1], ended[i + 1]
        elif k == 4:
            end[i], end_u[i], ended[i] = end[i + 1], i + 1, True
        else:
            end[i], end_u[i] = i, i
    calls, unknown = [], []
    after = 0                                       # matches do not overlap: a match ends at its verb word
    for g, (s, e) in enumerate(words):
        if g < after or not GIT_END.search(view, s, e):
            continue
        v = end[g + 1]
        if v < n and VERB.match(view, words[v][0]):
            calls.append((e - 3, e, words[v][0]))
            after = v
    for g, (s, e) in enumerate(words):
        for m in GIT_IN.finditer(view, s, e):
            if m.end() < e:                         # a subcommand glued to the git word: `git$V`
                cut = SEPARATOR.search(view, m.end(), e)
                if UNKNOWN_WORD.search(view, m.end(), cut.start() if cut else e):
                    unknown.append((m.start(), m.end()))
                continue
            v = end_u[g + 1]
            if ended[g + 1] or v >= n or "\n" in view[words[v - 1][1] if v > g + 1 else e:words[v][0]]:
                if v > g + 1 and view.startswith("$", words[v - 1][0]):
                    unknown.append((m.start(), len(view) if v >= n else words[v][0]))
                continue
            cut = SEPARATOR.search(view, words[v][0], words[v][1])
            if UNKNOWN_WORD.search(view, words[v][0], cut.start() if cut else words[v][1]):
                unknown.append((m.start(), words[v][0]))
    return calls, unknown


def _held(scan: Scan, verbs_at: set, unknown_at: list, launcher: bool):
    """(verb, why) when the command may run a gated verb this hook cannot read as a git call; None
    otherwise. Counted: a `git` word in data with a verb word after it in the same pipeline; a verb word
    alone in data only when the code holds a shell launcher (_launcher: `bash -c '${G}it push'`); a git
    word whose subcommand is unknown (_git_words), in code (`unknown_at`) or in data, or missing after
    `xargs git` (xargs supplies it); and a verb word in code that is not the verb of a matched git call
    but follows a word with an expansion, substitution, brace expansion or glob (`$G push`,
    `{git,} push`). Each is allowed only when the command holding it, and every command whose word
    holds that one, keeps it as text (_judge); data in a `VAR=val` or redirection prefix never is."""
    data, pos = _view(scan, False)
    toks = sorted([(m.start(), None) for m in DATA_GIT.finditer(data)]
                  + [(m.start(), m.group()) for m in DATA_VERB.finditer(data)])
    hits = []                                       # (verb, kind, command, offset), last first
    later = {}
    for k, verb in reversed(toks):
        at = pos[k]
        seg = scan.owner(at)
        isdata = scan.mask[at] == DATA
        if verb is not None:
            later[seg.group] = verb
            if isdata and launcher:
                hits.append((verb, "data", seg, at))
            elif at not in verbs_at:
                hits.append((verb, "code", seg, at))
        elif isdata and seg.group in later:
            hits.append((later[seg.group], "data", seg, at))
    for git_at, sub_at in _git_words(data)[1]:
        at = next((pos[k] for k in (sub_at, git_at) if k < len(pos) and scan.mask[pos[k]] == DATA), None)
        if at is not None:
            hits.append(("commit or push", "data", scan.owner(at), at))
    hits.extend(("commit or push", "unknown", scan.owner(at), at) for at in unknown_at)
    hits.extend(("commit or push", "unknown", seg, seg.head_lo) for seg in scan.segs
                if seg.head == "xargs" and seg.xcmd == "git" and seg.sub is None)
    if not hits:
        return None
    _judge(scan.segs)
    for verb, kind, seg, at in reversed(hits):
        if kind == "data" and (seg.head is None or at < seg.head_lo):
            return verb, "a `VAR=...` assignment or redirection prefix holds the %s in its data" % verb
        if kind == "data" and seg.bad:
            return verb, ("%s may run the %s its quoted or heredoc data names (only %s, awk without system, "
                          "getline or | and xargs running %s keep such data as text)"
                          % (seg.bad, verb, ", ".join(CONSUMERS), "/".join(XARGS_HOLDERS)))
        if kind == "unknown" and seg.bad:
            return verb, ("%s may run git with a subcommand word that holds an expansion, substitution, quoted "
                          "text, brace expansion or glob, or with none after a $X word or `xargs git`" % seg.bad)
        if kind == "code" and seg.bad and (seg.expand_lo <= at or seg.expand_before):
            return verb, "%s may run the %s after an expansion, substitution, brace expansion or glob" % (
                seg.bad, verb)
    return None


@functools.lru_cache(maxsize=8)
def _analyse(command: str) -> tuple:
    """(calls, held, code view, code offsets) of a command; see the module docstring."""
    scan = Scan(command)
    code, pos = _view(scan, True)
    calls = []
    starts, verbs_at = set(), set()
    found, unknown = _git_words(code)
    for git_at, a, verb_at in found:
        opts = code[a:verb_at].rstrip()
        dirs = [_orig(command, pos, a + d.start(1), a + d.end(1)) for d in GIT_DIR_OPT.finditer(opts)]
        calls.append(Call(opts, VERB.match(code, verb_at).group(), dirs))
        starts.add(git_at)
        verbs_at.add(pos[verb_at])
    unknown_at = [pos[git_at] for git_at, _ in unknown if git_at not in starts]
    return calls, _held(scan, verbs_at, unknown_at, _launcher(code)), code, pos


def git_calls(command: str) -> list:
    """The gated git commands (commit, push) the code of the command may run (see the module docstring)."""
    return _analyse(command)[0]


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    calls, held, _, _ = _analyse(_command(event) or "")
    names = []
    for verb in [c.verb for c in calls] + (["commit", "push"] if held else []):
        for n in GATED[verb]:
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


def _short_opts(word: str) -> str:
    """The letters of a short-option cluster (`-ec` -> `ec`); "" for any other word."""
    return word[1:] if word[:1] == "-" and word[1:].isalpha() else ""


def _launcher(text: str) -> bool:
    """True when a simple command in `text` (a code view: data blanked, quotes removed) runs code this
    hook cannot read: a shell word (sh, bash, zsh, dash, ksh, by any path) followed in the same simple
    command by `-c` or a short-option cluster holding c (`-o pipefail -c`, `-ec`) or by a heredoc or
    here-string (`<<`), a shell fed by a pipe, `eval`, `xargs`, `-exec`/`-execdir`/`-ok`/`-okdir`,
    `env -C/--chdir/-S/--split-string` or `sudo -D/--chdir`. One pass over the words (linear)."""
    parts = SEP.split(text)
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
    or `;`, then by each `-C <dir>` of the command (cumulative, as git does). Raises TargetError for
    data a command may run (see the module docstring) and for a gated command whose tree cannot be
    told -- another `cd`, `pushd`/`popd`, a subshell, a launcher, a `$` or backtick in a followed
    directory word, --git-dir/--work-tree, GIT_DIR=/GIT_WORK_TREE=."""
    command = _command(event) or ""
    calls, held, code, pos = _analyse(command)
    hint = "; use git -C <dir> or a leading cd <dir> &&"
    if held:
        raise TargetError("cannot tell which tree %s runs in, or whether it runs: %s%s" % (held + (hint,)))
    if not calls:
        return []
    why = "cannot tell which tree %s runs in" % calls[0].verb
    base = event.get("cwd") or os.getcwd()
    rest = code
    lead = LEADING_CD.match(command)
    if lead:
        base = _word(lead.group(1), base)
        if base is None:
            raise TargetError(why + " (the leading cd directory is not a plain word)" + hint)
        rest = code[bisect.bisect_left(pos, lead.end()):]
    if CD_WORD.search(rest) or TREE_ENV.search(rest):
        raise TargetError(why + " (a cd, pushd, popd, subshell or GIT_DIR/GIT_WORK_TREE the hook does not follow)" + hint)
    if _launcher(rest):
        raise TargetError(why + " (a nested shell, eval, xargs, -execdir, env -C or sudo -D the hook does not follow)" + hint)
    dirs = []
    for call in calls:
        if TREE_OPT.search(call.opts):
            raise TargetError(why + " (--git-dir/--work-tree)" + hint)
        cur = base
        for d in call.dirs:
            cur = _word(d, cur)
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
