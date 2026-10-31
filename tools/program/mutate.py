#!/usr/bin/env python3
"""mutate -- mechanical mutation of every changed line (program tooling Task 1, fix round 8a; DEC-0178 change 4, DEC-0234).

  python3 tools/program/mutate.py --root <work-tree top> --base <sha> --head <sha> --file <path> [--file ...]
      --test <unittest id> [--test ...] --py <interpreter> [--py ...] [--jobs N] [--ids <id>|<id>-<id>] [--json]
  python3 tools/program/mutate.py --list-operators

Sites. The lines of each --file (a .py file, a regular file at --head) that `git diff -U0 <base> <head>` adds or
changes, every GIT_* variable removed from git's environment as the loader does, the diff's algorithm, heuristic,
hunk joining and text mode pinned (DIFF_PINS) so repository config and attributes cannot move a site. A mutant is in scope when the text it
replaces touches a site line; its reported line is the line that text starts on.

Operators. OPERATORS, a fixed list, each applied alone to every match on the sites (Python ast positions, never a
regular expression over the text). Nothing inside an f-string is mutated except the whole f-string by str-empty (the
positions ast gives the parts of an f-string differ between Python versions). A docstring (the str constant that is the
first statement of a module, class or function body, as Python takes it for __doc__) is not mutated (DEC-0247).

Run. Each worker clones the root into its own scratch directory (tempfile.mkdtemp; `git clone --no-checkout` then
`git reset --hard <head>`, so the copy holds exactly the files at --head and the checkout is never written). The DEC-0032
guard refuses a scratch directory that is the root or inside it and a copy that is not its own work-tree top. The
--test ids first run on the unmutated copy under each --py: they must run at least one test and pass. Then, for each
mutant: the file in the copy is written with the mutant, the --test ids run in one `<py> -W error -m unittest -f` per
--py (cwd the copy, GIT_* and TEST_ENV_DROP removed, bytecode off), the file is restored and byte-compared, and `git status` of the copy
must be empty. A run that fails or errors, or outlives TIMEOUT_FLOOR + TIMEOUT_FACTOR x the unmutated run, KILLS the
mutant; a run that passes means it SURVIVED; a mutant whose text is not found at its position, or equals the original,
or does not compile, is BADMATCH (never run).

Lifecycle (DEC-0275..DEC-0280). Each test run is a small watcher (WATCH, the tool's interpreter, -I) leading the run's
own session with the test command as its child: every WATCH_PERIOD it checks that the tool still runs and otherwise
kills its own process group, so SIGKILL of the tool leaves no test process. Each scratch directory holds PID_FILE, the
tool's pid; a run removes every mutate-* scratch directory whose recorded pid is gone and keeps one whose pid runs.
SIGINT or SIGTERM stops a run at once: no further run starts, the runs in progress are killed with their groups, the
scratch directories are removed and the last line is `FAIL mutate.interrupt: stopped by SIGINT` (or SIGTERM), rc 2.
An unmutated run that outlives BASELINE_TIMEOUT is killed with its group: FAIL mutate.baseline.

Output (stdout), in id order:
  <id> <file>:<line> <operator> <KILLED|SURVIVED|BADMATCH> <py>       one line per mutant per --py, --py order
  summary: base <sha> head <sha>, <n> mutants in <k> file(s), <t> test(s), <p> interpreter(s)
  summary <py>: <k> KILLED, <s> SURVIVED, <b> BADMATCH                one line per --py
  SURVIVED <id> <file>:<line>:<col> <operator> <old!r> -> <new!r>      one line per mutant surviving on any --py
Ids are m0001.. in the order (file, position, operator index): the same base, head and files give the same ids. A
docstring keeps its ids (it is numbered, never run nor printed), so the other ids are the ones they had before docstrings
were excluded (DEC-0246). --ids m0560-m0974 (or one id, m0560) runs only those ids, written as the tool prints them; a
spec of another shape, a range ending before it starts, an id outside m0001..the last id, or a range holding only
docstrings is FAIL mutate.ids. With --ids the first summary line names it: `summary: base <sha> head <sha>, ids <spec>, ...`.
--json prints one object instead: {base, head, files, tests, py, ids (the spec or null), operators, mutants: [{id, file, line, col, operator,
old, new, results: {py: result}, reason: {py: text}}], summary: {py: {KILLED, SURVIVED, BADMATCH}}, rc}.
rc 0: no mutant SURVIVED; rc 1: a mutant SURVIVED on some --py; rc 2: one line `FAIL mutate.<check>: <reason>` (with
--json: {"rc": 2, "fail": [{check, reason}]}) for every input outside these rules, never a traceback.
"""
from __future__ import annotations

import argparse
import ast
import contextlib
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import tokenize
import warnings
from concurrent.futures import ThreadPoolExecutor

OPERATORS = (
    ("cmp-eq-ne", "comparison == to !="),
    ("cmp-ne-eq", "comparison != to =="),
    ("cmp-lt-le", "comparison < to <="),
    ("cmp-le-lt", "comparison <= to <"),
    ("cmp-gt-ge", "comparison > to >="),
    ("cmp-ge-gt", "comparison >= to >"),
    ("cmp-in-notin", "comparison in to not in"),
    ("cmp-notin-in", "comparison not in to in"),
    ("cmp-is-isnot", "comparison is to is not"),
    ("cmp-isnot-is", "comparison is not to is"),
    ("not-remove", "the not of a `not x` removed"),
    ("not-add", "a condition (if, elif, while, assert, conditional expression, comprehension if) x to not (x)"),
    ("and-or", "boolean and to or"),
    ("or-and", "boolean or to and"),
    ("true-false", "literal True to False"),
    ("false-true", "literal False to True"),
    ("int-inc", "integer literal n to n+1"),
    ("int-dec", "integer literal n to n-1"),
    ("cond-true", "a whole if, elif or while condition to True"),
    ("cond-false", "a whole if, elif or while condition to False"),
    ("return-none", "return <expr> to return None"),
    ("stmt-pass", "a statement alone on its lines that is raise, continue, break or a call, to pass"),
    ("fullmatch-match", "attribute .fullmatch to .match"),
    ("match-search", "attribute .match to .search"),
    ("startswith-endswith", "attribute .startswith to .endswith"),
    ("strip-rstrip", "attribute .strip to .rstrip"),
    ("str-empty", "a non-empty string literal (an f-string included) to \"\", except a docstring"),
)
OP_INDEX = {name: i for i, (name, _) in enumerate(OPERATORS)}
CMP_OPS = {ast.Eq: ("==", "!=", "cmp-eq-ne"), ast.NotEq: ("!=", "==", "cmp-ne-eq"), ast.Lt: ("<", "<=", "cmp-lt-le"),
           ast.LtE: ("<=", "<", "cmp-le-lt"), ast.Gt: (">", ">=", "cmp-gt-ge"), ast.GtE: (">=", ">", "cmp-ge-gt"),
           ast.In: ("in", "not in", "cmp-in-notin"), ast.NotIn: ("not in", "in", "cmp-notin-in"),
           ast.Is: ("is", "is not", "cmp-is-isnot"), ast.IsNot: ("is not", "is", "cmp-isnot-is")}
ATTR_OPS = {"fullmatch": ("match", "fullmatch-match"), "match": ("search", "match-search"),
            "startswith": ("endswith", "startswith-endswith"), "strip": ("rstrip", "strip-rstrip")}
PASS_STMTS = (ast.Raise, ast.Continue, ast.Break)
TIMEOUT_FLOOR = 60.0        # seconds a mutated test run may take on top of TIMEOUT_FACTOR x the unmutated run
TIMEOUT_FACTOR = 10.0
BASELINE_TIMEOUT = 600.0    # seconds an unmutated run may take (M-X5, DEC-0275, DEC-0276)
WATCH_PERIOD = 1.0          # seconds between a test run's checks that the tool still runs (M-X7, DEC-0279)
PID_FILE = "mutate.pid"     # in each scratch directory: the pid of the run that made it (M-X7, DEC-0278)
PID_RE = re.compile(r"[1-9][0-9]*\n")
CLIP = 200                  # every value a FAIL line echoes is cut to this many characters
MAX_JOBS = 64
TEST_ID_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")
HUNK_RE = re.compile(rb"^@@ -[0-9]+(?:,[0-9]+)? \+([0-9]+)(?:,([0-9]+))? @@", re.M)
ID_SPEC_RE = re.compile(r"(m[0-9]{4,})(?:-(m[0-9]{4,}))?")
ID_SHAPE = "is not an id m<nnnn> or a range m<nnnn>-m<nnnn> as the tool prints them"
RAN_RE = re.compile(r"^Ran ([0-9]+) tests? in ", re.M)
REGULAR = (b"100644", b"100755")
TEST_ENV_DROP = ("PYTHONPATH", "PYTHONHOME", "PYTHONSAFEPATH", "PYTHONUSERBASE", "PYTHONSTARTUP", "PYTHONPLATLIBDIR")
DIFF_PINS = ("--diff-algorithm=myers", "--indent-heuristic", "--inter-hunk-context=0", "--text")   # M-G3, M-G4 (DEC-0260, DEC-0281)


class Fail(Exception):
    """An input outside the rules, or a run that cannot go on: `FAIL mutate.<check>: <reason>`, rc 2."""

    def __init__(self, check: str, reason: str):
        super().__init__(reason)
        self.check = check
        self.reason = reason


# Each test run is this watcher (the group leader of the run's own session) and the test command as its child. Every
# WATCH_PERIOD it checks that its parent is still the tool; when the tool is gone (SIGKILL included) it kills its own
# process group (M-X7, DEC-0277, DEC-0279). An exec failure of the command is sent back on the report pipe so the tool
# raises it as before; the command's exit status is passed on as its own (a signal death by the same signal).
WATCH = r"""
import json, os, signal, subprocess, sys
tool, report = int(sys.argv[1]), int(sys.argv[2])
period = float(sys.argv[3])
try:
    child = subprocess.Popen(sys.argv[4:])
except OSError as exc:
    os.write(report, json.dumps([exc.errno, exc.filename]).encode())
    sys.exit(127)
os.close(report)
while True:
    try:
        rc = child.wait(timeout=period)
        break
    except subprocess.TimeoutExpired:
        if os.getppid() != tool:
            os.killpg(0, signal.SIGKILL)
if rc < 0:
    try:
        signal.signal(-rc, signal.SIG_DFL)
    except (OSError, ValueError):
        pass
    os.kill(os.getpid(), -rc)
sys.exit(rc)
"""


class Stop:
    """SIGINT and SIGTERM stop a run at once (M-X3, M-X4; DEC-0275, DEC-0276): the handler records the signal and
    kills the process group of every test run in progress; no further run starts; main prints FAIL mutate.interrupt."""
    signal = None
    procs = set()

    @classmethod
    def reset(cls) -> None:
        cls.signal, cls.procs = None, set()

    @classmethod
    def handler(cls, signum, frame) -> None:
        if cls.signal is None:
            cls.signal = signum
        for p in list(cls.procs):
            kill_group(p)

    @classmethod
    def check(cls) -> None:
        if cls.signal is not None:
            raise Fail("interrupt", f"stopped by {signal.Signals(cls.signal).name}")


def kill_group(p) -> None:
    if p.returncode is None:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(p.pid, signal.SIGKILL)


def clip(text: str) -> str:
    return text if len(text) <= CLIP else text[:CLIP] + "..."


def show(value) -> str:
    """repr of an echoed value, cut to CLIP characters."""
    return clip(repr(value))


def one_line(text: str) -> str:
    """The loader's O3 rule: every non-printable character (line breaks included) escaped, runs of spaces collapsed,
    then cut to CLIP characters (M-X2, DEC-0264)."""
    escaped = "".join(c if c.isprintable() else c.encode("unicode_escape").decode() for c in text)
    return clip(" ".join(escaped.split()))


def git_top(stdout: bytes) -> str:
    """git's toplevel as argv and os.environ decode a path, only git's final newline removed (M-R9, M-R10, M-G1; R7, R8)."""
    return os.fsdecode(stdout).removesuffix("\n")


def clean_env() -> dict:
    """The caller's environment without any GIT_* variable (the root names the repository), pathspecs literal."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(LC_ALL="C", GIT_LITERAL_PATHSPECS="1")
    return env


def git_run(cwd: str, *args) -> "subprocess.CompletedProcess":
    try:
        return subprocess.run(["git", "-C", cwd, *args], capture_output=True, check=False, env=clean_env())
    except OSError as exc:                      # M-G5 (DEC-0261): git not on PATH is a named FAIL, R5's strerror form
        raise Fail("git", f"git cannot run ({exc.strerror})") from None


def git(cwd: str, *args) -> bytes:
    p = git_run(cwd, *args)
    if p.returncode != 0:
        raise Fail("git", f"git {' '.join(args[:2])} failed: {show(p.stderr.decode('utf-8', 'replace').strip())}")
    return p.stdout


def write_file(path: str, data: bytes) -> None:
    with open(path, "wb") as fh:
        fh.write(data)


def read_file(path: str) -> bytes:
    with open(path, "rb") as fh:
        return fh.read()


# --- arguments -----------------------------------------------------------------------------------------------------

def parse_args(argv: list) -> argparse.Namespace:
    ap = argparse.ArgumentParser(prog="mutate", allow_abbrev=False)
    for name in ("--root", "--base", "--head", "--jobs", "--ids", "--file", "--test", "--py"):
        ap.add_argument(name, action="append", default=[])
    ap.add_argument("--json", action="count", default=0)
    ap.add_argument("--list-operators", action="store_true")

    def refuse(message):                 # argparse would print its usage and exit 2 without a named check
        raise Fail("args", f"bad arguments: {message}")
    ap.error = refuse
    try:
        with contextlib.redirect_stdout(sys.stderr):        # M-O3 (DEC-0272): -h prints the usage on stderr
            ns = ap.parse_args(argv)
    except SystemExit as exc:
        raise Fail("args", f"bad arguments (argparse exit {exc.code})") from None
    if ns.list_operators:
        if argv != ["--list-operators"]:
            raise Fail("args", "--list-operators takes no other argument")
        return ns
    if ns.json > 1:                      # M-O1 (DEC-0265)
        raise Fail("args", "--json is given more than once")
    for name in ("root", "base", "head", "jobs", "ids"):
        values = getattr(ns, name)
        if len(values) > 1:
            raise Fail("args", f"--{name} is given more than once")
        setattr(ns, name, values[0] if values else None)
    for name in ("root", "base", "head"):
        if getattr(ns, name) is None:
            raise Fail("args", f"--{name} is missing")
    for name in ("file", "test", "py"):
        values = getattr(ns, name)
        if not values:
            raise Fail("args", f"no --{name}")
        for v in values:
            if values.count(v) > 1:
                raise Fail("args", f"--{name} {show(v)} is given twice")
    for inner in ns.test:                # M-T4 (DEC-0265, DEC-0276): one id inside another, in either order
        for outer in ns.test:
            if inner.startswith(outer + "."):
                raise Fail("args", f"--test {show(inner)} overlaps --test {show(outer)}")
    jobs = ns.jobs if ns.jobs is not None else "1"
    if not re.fullmatch(r"[1-9][0-9]?", jobs) or int(jobs) > MAX_JOBS:
        raise Fail("args", f"--jobs {show(jobs)} is not an integer from 1 to {MAX_JOBS}")
    ns.jobs = int(jobs)
    if ns.ids is not None:
        ns.id_range = parse_ids(ns.ids)
    return ns


def parse_ids(spec: str) -> tuple:
    """(first, last) id numbers of an --ids spec `m<nnnn>` or `m<nnnn>-m<nnnn>`, each written as the tool prints it."""
    m = ID_SPEC_RE.fullmatch(spec)
    if m is None or any(g is not None and g != f"m{int(g[1:]):04d}" for g in m.groups()):
        raise Fail("ids", f"--ids {show(spec)} {ID_SHAPE}")
    first = int(m.group(1)[1:])
    last = int(m.group(2)[1:]) if m.group(2) is not None else first
    if last < first:
        raise Fail("ids", f"--ids {show(spec)} ends before it starts")
    return first, last


def check_root(root: str) -> str:
    if not root.strip():                 # M-R3 (DEC-0253): whitespace only is empty, as R1
        raise Fail("root", "--root is empty")
    root = os.path.abspath(root)         # M-R5 (DEC-0254): named by its absolute path; a trailing / never hides a link
    if os.path.islink(root):             # M-R6 (DEC-0255, D32)
        raise Fail("root", f"--root {show(root)} is a symlink")
    if not os.path.isdir(root):
        raise Fail("root", f"--root {show(root)} is not a directory")
    top = git_run(root, "rev-parse", "--show-toplevel")
    if top.returncode != 0 or os.path.realpath(git_top(top.stdout)) != os.path.realpath(root):
        raise Fail("root", f"--root {show(root)} is not the top of a git work tree")
    return root


def resolve(root: str, name: str, rev: str) -> str:
    p = None if rev.startswith("-") else git_run(root, "rev-parse", "--verify", "--quiet", rev + "^{commit}")
    if p is None or p.returncode != 0:
        raise Fail("sha", f"--{name} {show(rev)} is not a commit")
    return p.stdout.decode("ascii").strip()


def head_source(root: str, head: str, path: str) -> str:
    """The text of `path` at `head`: a normalized relative .py path, a regular file there, UTF-8, no carriage return,
    Python source."""
    if not path or os.path.isabs(path) or os.path.normpath(path) != path or path.split("/")[0] == "..":
        raise Fail("file", f"{show(path)} is not a normalized path relative to the root")
    entry = git(root, "ls-tree", "-z", head, "--", path)
    fields = entry.split(b"\t", 1)
    if len(fields) != 2 or fields[1] != os.fsencode(path) + b"\0" or fields[0].split(b" ")[0] not in REGULAR:
        raise Fail("file", f"{show(path)} is not a regular file at {head}")
    if not path.endswith(".py"):
        raise Fail("file", f"{show(path)} is not a .py file")
    data = git(root, "cat-file", "blob", fields[0].split(b" ")[2].decode("ascii"))
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise Fail("file", f"{show(path)} at {head} is not UTF-8") from None
    if "\r" in text:
        raise Fail("file", f"{show(path)} at {head} has a carriage return")
    try:
        ast.parse(text)
    except SyntaxError as exc:
        raise Fail("file", f"{show(path)} at {head} is not Python source (line {exc.lineno})") from None
    return text


def changed_lines(root: str, base: str, head: str, path: str) -> set:
    out = git(root, "diff", "-U0", "--no-color", "--no-ext-diff", "--no-textconv", "--no-renames", *DIFF_PINS, base, head,
              "--", path)
    lines = set()
    for m in HUNK_RE.finditer(out):
        start, count = int(m.group(1)), int(m.group(2)) if m.group(2) is not None else 1
        lines.update(range(start, start + count))
    if not lines:
        raise Fail("file", f"{show(path)} has no changed lines in {base}..{head}")
    return lines


def check_py(py: str) -> str:
    if not (os.path.isfile(py) and os.access(py, os.X_OK)):
        raise Fail("py", f"{show(py)} is not an executable file")
    return os.path.abspath(py)


# --- mutants -------------------------------------------------------------------------------------------------------

class Source:
    """One file's text with ast (UTF-8 byte) and tokenize (character) positions mapped to string offsets."""

    def __init__(self, path: str, text: str):
        self.path, self.text = path, text
        self.lines = text.split("\n")
        self.starts = [0]
        for line in self.lines[:-1]:
            self.starts.append(self.starts[-1] + len(line) + 1)
        self.tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))

    def off(self, lineno: int, col: int) -> int:
        """String offset of a character column."""
        return self.starts[lineno - 1] + col

    def boff(self, lineno: int, bcol: int) -> int:
        """String offset of an ast UTF-8 byte column."""
        return self.off(lineno, len(self.lines[lineno - 1].encode("utf-8")[:bcol].decode("utf-8", "replace")))

    def span(self, node) -> tuple:
        return self.boff(node.lineno, node.col_offset), self.boff(node.end_lineno, node.end_col_offset)

    def line_of(self, offset: int) -> int:
        lo, hi = 0, len(self.starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self.starts[mid] <= offset:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1

    def tokens_between(self, start: int, end: int) -> list:
        """(offset, end offset, string) of each NAME or OP token lying within [start, end)."""
        out = []
        for t in self.tokens:
            if t.type in (tokenize.NAME, tokenize.OP):
                s, e = self.off(*t.start), self.off(*t.end)
                if start <= s and e <= end:
                    out.append((s, e, t.string))
        return out


def walk(tree):
    """Every node, but nothing below an f-string (JoinedStr): the f-string itself is yielded."""
    stack = [tree]
    while stack:
        node = stack.pop()
        yield node
        if not isinstance(node, ast.JoinedStr):
            stack.extend(reversed(list(ast.iter_child_nodes(node))))


def gap_ops(src: Source, left, right, words: tuple) -> list:
    """The operator between two operands: the run of tokens in `words` between them, as (start, end, text)."""
    hits = [t for t in src.tokens_between(src.span(left)[1], src.span(right)[0]) if t[2] in words]
    return [(hits[0][0], hits[-1][1], src.text[hits[0][0]:hits[-1][1]])] if hits else []


def compare_mutants(src, node) -> list:
    out = []
    operands = [node.left] + list(node.comparators)
    for i, op in enumerate(node.ops):
        old, new, name = CMP_OPS[type(op)]
        for s, e, _ in gap_ops(src, operands[i], operands[i + 1], tuple(old.split())):
            out.append((s, e, name, old, new))
    return out


def boolop_mutants(src, node) -> list:
    word, new, name = ("and", "or", "and-or") if isinstance(node.op, ast.And) else ("or", "and", "or-and")
    out = []
    for a, b in zip(node.values, node.values[1:]):
        for s, e, text in gap_ops(src, a, b, (word,)):
            out.append((s, e, name, text, new))
    return out


def condition_mutants(src, test, whole: bool) -> list:
    s, e = src.span(test)
    text = src.text[s:e]
    out = [(s, e, "not-add", text, f"not ({text})")]
    if whole:
        out += [(s, e, "cond-true", text, "True"), (s, e, "cond-false", text, "False")]
    return out


def alone_on_its_lines(src, node) -> bool:
    s, e = src.span(node)
    before = src.text[src.starts[node.lineno - 1]:s]
    after = src.lines[node.end_lineno - 1][e - src.starts[node.end_lineno - 1]:].strip()
    return not before.strip() and (not after or after.startswith("#"))


def node_mutants(src, node) -> list:
    """(start, end, operator, expected old text, new text) for every operator that matches this node."""
    out = []
    if isinstance(node, ast.Compare):
        out += compare_mutants(src, node)
    elif isinstance(node, ast.BoolOp):
        out += boolop_mutants(src, node)
    elif isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
        s = src.span(node)[0]
        out.append((s, s + 3, "not-remove", "not", ""))
    elif isinstance(node, (ast.If, ast.While)):
        out += condition_mutants(src, node.test, True)
    elif isinstance(node, (ast.IfExp, ast.Assert)):
        out += condition_mutants(src, node.test, False)
    elif isinstance(node, ast.comprehension):
        for cond in node.ifs:
            out += condition_mutants(src, cond, False)
    elif isinstance(node, ast.Return) and node.value is not None:
        s, e = src.span(node.value)
        out.append((s, e, "return-none", src.text[s:e], "None"))
    elif isinstance(node, PASS_STMTS) or (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)):
        if alone_on_its_lines(src, node):
            s, e = src.span(node)
            out.append((s, e, "stmt-pass", src.text[s:e], "pass"))
    elif isinstance(node, ast.Attribute) and node.attr in ATTR_OPS:
        e = src.boff(node.end_lineno, node.end_col_offset)
        new, name = ATTR_OPS[node.attr]
        out.append((e - len(node.attr), e, name, node.attr, new))
    elif isinstance(node, ast.Constant):
        out += constant_mutants(src, node)
    elif isinstance(node, ast.JoinedStr) and node.values:
        s, e = src.span(node)
        out.append((s, e, "str-empty", src.text[s:e], '""'))
    return out


def constant_mutants(src, node) -> list:
    s, e = src.span(node)
    v = node.value
    if v is True or v is False:
        return [(s, e, "true-false" if v else "false-true", str(v), str(not v))]
    if type(v) is int:
        text = src.text[s:e]
        return [(s, e, "int-inc", text, str(v + 1)), (s, e, "int-dec", text, str(v - 1))]
    if type(v) is str and v:
        return [(s, e, "str-empty", src.text[s:e], '""')]
    return []


def file_mutants(src: Source, sites: set) -> list:
    """Every mutant of the file whose replaced text touches a site line, in (position, operator) order."""
    found = []
    tree = ast.parse(src.text)
    docs = docstrings(tree)
    for node in walk(tree):
        for s, e, name, old, new in node_mutants(src, node):
            first, last = src.line_of(s), src.line_of(max(s, e - 1))
            if sites.intersection(range(first, last + 1)):
                found.append((s, e, name, old, new, id(node) in docs))
    found.sort(key=lambda m: (m[0], OP_INDEX[m[2]], m[1]))
    out = []
    for s, e, name, expected, new, doc in found:
        line, old = src.line_of(s), src.text[s:e]
        out.append({"file": src.path, "line": line, "col": s - src.starts[line - 1] + 1, "operator": name,
                    "start": s, "end": e, "old": old, "new": new, "matched": old.split() == expected.split(),
                    "docstring": doc})
    return out


def docstrings(tree) -> set:
    """id() of every docstring node: the str Constant of an expression statement that is the first statement of the
    module or of a class or (async) function body (what Python stores as __doc__)."""
    out = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)) and node.body:
            first = node.body[0]
            if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant) and type(first.value.value) is str:
                out.add(id(first.value))
    return out


def mutated_text(src: Source, m: dict):
    """The file's text with the mutant applied, or None (BADMATCH) when the operator finds no text to change: the
    expected text is not at the position, the new text equals it, or the result does not compile."""
    if not m["matched"] or m["old"] == m["new"]:
        return None
    text = src.text[:m["start"]] + m["new"] + src.text[m["end"]:]
    with warnings.catch_warnings():            # a warning is the tests' business (-W error), not a bad match
        warnings.simplefilter("ignore")
        try:
            compile(text, src.path, "exec", dont_inherit=True)
        except SyntaxError:
            return None
    return text


# --- the scratch copy ----------------------------------------------------------------------------------------------

def inside(path: str, root: str) -> bool:
    path, root = os.path.realpath(path), os.path.realpath(root)
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


class Copy:
    """A guarded clone of the root at head in its own scratch directory (DEC-0032)."""

    def __init__(self, root: str):
        scratch = tempfile.mkdtemp(prefix="mutate-")
        if inside(scratch, root):
            with contextlib.suppress(OSError):   # M-E1 (DEC-0032): the refused (new, empty) directory is not left there
                os.rmdir(scratch)
            raise Fail("guard", f"the scratch copy {show(scratch)} is the root or inside it")
        self.scratch = scratch
        self.tree = os.path.join(scratch, "tree")
        self.pycache = os.path.join(scratch, "pycache")
        with open(os.path.join(scratch, PID_FILE), "w", encoding="ascii") as fh:   # M-X7 (DEC-0278)
            fh.write(f"{os.getpid()}\n")

    def make(self, root: str, head: str) -> None:
        git(self.scratch, "clone", "-q", "--no-hardlinks", "--no-checkout", "--", root, self.tree)
        top = git_run(self.tree, "rev-parse", "--show-toplevel")
        toplevel = git_top(top.stdout) if top.returncode == 0 else "none"
        if top.returncode != 0 or os.path.realpath(toplevel) != os.path.realpath(self.tree) or inside(toplevel, root):
            raise Fail("guard", f"the scratch copy {show(self.tree)} is not its own work-tree top (git toplevel {show(toplevel)})")
        git(self.tree, "reset", "-q", "--hard", head)
        self.assert_clean("the clone")

    def assert_clean(self, who: str) -> None:
        status = git(self.tree, "status", "--porcelain", "--untracked-files=all").decode("utf-8", "replace")
        if status.strip():
            raise Fail("copy", f"{who} left the copy changed: {clip(' '.join(status.split()))}")

    def remove(self) -> None:
        shutil.rmtree(self.scratch, ignore_errors=True)

    def run_tests(self, py: str, tests: list, timeout) -> tuple:
        """(rc or None on timeout, number of tests run, seconds) of one unittest run in the copy."""
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_") and k not in TEST_ENV_DROP}   # M-E3
        env.update(PYTHONDONTWRITEBYTECODE="1", PYTHONPYCACHEPREFIX=self.pycache)
        Stop.check()                               # M-X3, M-X4: no run starts after a signal
        t0 = time.monotonic()
        report, w = os.pipe()
        try:
            try:
                p = subprocess.Popen([sys.executable, "-I", "-c", WATCH, str(os.getpid()), str(w), repr(WATCH_PERIOD),
                                      py, "-W", "error", "-m", "unittest", "-f", *tests], cwd=self.tree, env=env,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True,
                                     pass_fds=(w,))
            finally:
                os.close(w)
            Stop.procs.add(p)
            try:
                if Stop.signal is not None:            # the signal came between the check and the registration
                    kill_group(p)
                try:
                    _, err = p.communicate(timeout=timeout)
                except subprocess.TimeoutExpired:      # the whole process group: no test child outlives its run
                    os.killpg(p.pid, signal.SIGKILL)
                    p.communicate()
                    err = None
            finally:
                Stop.procs.discard(p)
            failed = b"".join(iter(lambda: os.read(report, 4096), b""))
        finally:
            os.close(report)
        Stop.check()
        if failed:                                     # the command could not start: raised as Popen raised it
            errno, filename = json.loads(failed)
            raise OSError(errno, os.strerror(errno), filename)
        if err is None:
            return None, 0, time.monotonic() - t0
        ran = RAN_RE.findall(err.decode("utf-8", "replace"))
        return p.returncode, int(ran[-1]) if ran else 0, time.monotonic() - t0


# --- the run -------------------------------------------------------------------------------------------------------

def baseline(copy: Copy, pys: list, tests: list) -> dict:
    """Timeout per --py (label) from an unmutated run, which must run a test and pass."""
    timeouts = {}
    for label, exe in pys:
        rc, ran, secs = copy.run_tests(exe, tests, BASELINE_TIMEOUT)
        if rc is None:                       # M-X5 (DEC-0275, DEC-0276): its process group is killed by run_tests
            raise Fail("baseline", f"the tests did not finish on the unmutated copy under {show(label)} "
                                   f"within {BASELINE_TIMEOUT:g} s")
        if ran == 0:
            raise Fail("baseline", f"the tests run no test on the unmutated copy under {show(label)}")
        if rc != 0:
            raise Fail("baseline", f"the tests fail on the unmutated copy under {show(label)} (rc {rc})")
        copy.assert_clean("the unmutated run")
        timeouts[label] = TIMEOUT_FLOOR + TIMEOUT_FACTOR * secs
    return timeouts


def run_mutant(copy: Copy, m: dict, src: Source, pys: list, tests: list, timeouts: dict) -> dict:
    """{py label: (result, reason)} for one mutant; the file is restored and byte-compared, the copy must be clean."""
    text = m["text"]
    if text is None:
        return {label: ("BADMATCH", "no text to change") for label, _ in pys}
    path = os.path.join(copy.tree, m["file"])
    original = src.text.encode("utf-8")
    out = {}
    try:
        write_file(path, text.encode("utf-8"))
        for label, exe in pys:
            rc, _, _ = copy.run_tests(exe, tests, timeouts[label])
            out[label] = (("KILLED", "timeout") if rc is None else ("KILLED", f"tests failed (rc {rc})") if rc != 0
                       else ("SURVIVED", "tests passed"))
    finally:
        try:
            write_file(path, original)
            same = read_file(path) == original
        except OSError as exc:
            raise Fail("restore", f"{m['id']} {m['file']} could not be restored ({exc})") from None
    if not same:
        raise Fail("restore", f"{m['id']} {m['file']} differs from its original after the restore")
    copy.assert_clean(m["id"])
    return out


class Printer:
    """Prints result lines in id order as soon as every earlier mutant is done (text mode)."""

    def __init__(self, mutants: list, enabled: bool):
        self.mutants, self.enabled = mutants, enabled
        self.done, self.next, self.lock = {}, 0, threading.Lock()

    def record(self, index: int, results: dict) -> None:
        with self.lock:
            self.done[index] = results
            while self.next in self.done:
                m = self.mutants[self.next]
                for label, (res, _) in self.done[self.next].items():
                    if self.enabled:
                        print(f"{m['id']} {m['file']}:{m['line']} {m['operator']} {res} {label}", flush=True)
                self.next += 1


def pid_gone(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    except (OSError, OverflowError):     # another user's process, or no pid this system has: kept
        return False
    return False


def remove_stale(parent: str) -> None:
    """M-X7 (DEC-0278): remove each mutate-* scratch directory whose PID_FILE names a pid that is gone (a run killed
    by SIGKILL); a directory whose pid runs (a concurrent run's), a symlink, or one without a regular PID_FILE of the
    form this tool writes, is kept."""
    try:
        names = os.listdir(parent)
    except OSError:                      # a TMPDIR that cannot be listed: nothing is removed
        return
    for name in names:
        pidfile = os.path.join(parent, name, PID_FILE)
        if not name.startswith("mutate-") or os.path.islink(pidfile) or not os.path.isfile(pidfile):
            continue
        try:
            text = read_file(pidfile).decode("ascii", "replace")
        except OSError:
            continue
        if PID_RE.fullmatch(text) and pid_gone(int(text)):
            shutil.rmtree(os.path.join(parent, name), ignore_errors=True)   # never through a symlink (rmtree)


def run_all(root: str, head: str, mutants: list, sources: dict, pys: list, tests: list, jobs: int,
            printer: Printer) -> None:
    """Every mutant under every --py, `jobs` guarded copies at a time; results land in each mutant's dict."""
    copies, stop, failures = [], threading.Event(), []
    tmp = os.environ.get("TMPDIR")
    if tmp is not None and not (os.path.isdir(tmp) and os.access(tmp, os.W_OK | os.X_OK)):   # M-E2 (DEC-0262)
        raise Fail("guard", f"TMPDIR {show(tmp)} is not a writable directory")
    try:
        for _ in range(max(1, min(jobs, len(mutants)))):
            Stop.check()
            copies.append(Copy(root))
            if len(copies) == 1:             # inside the guard: the copies' own directory, never the root
                remove_stale(os.path.dirname(copies[0].scratch))
            copies[-1].make(root, head)
        timeouts = baseline(copies[0], pys, tests)
        queue, qlock = list(range(len(mutants))), threading.Lock()

        def worker(copy):
            while not stop.is_set():
                with qlock:
                    if not queue:
                        return
                    i = queue.pop(0)
                try:
                    res = run_mutant(copy, mutants[i], sources[mutants[i]["file"]], pys, tests, timeouts)
                except Fail as exc:
                    failures.append(exc)
                    stop.set()
                    return
                except Exception as exc:            # never a traceback, and the other workers stop too
                    failures.append(Fail("internal", one_line(f"{type(exc).__name__}: {exc}")))
                    stop.set()
                    return
                mutants[i]["results"] = {label: r for label, (r, _) in res.items()}
                mutants[i]["reason"] = {label: why for label, (_, why) in res.items()}
                printer.record(i, res)
        with ThreadPoolExecutor(max_workers=len(copies)) as pool:
            list(pool.map(worker, copies))
        if failures:
            raise min(failures, key=lambda f: f.reason)
    finally:
        for c in copies:
            c.remove()


def summary(mutants: list, labels: list) -> dict:
    out = {}
    for label in labels:
        counts = {"KILLED": 0, "SURVIVED": 0, "BADMATCH": 0}
        for m in mutants:
            counts[m["results"][label]] += 1
        out[label] = counts
    return out


def report_text(ctx: dict, mutants: list, labels: list, counts: dict) -> None:
    files = len(ctx["files"])
    ids = f", ids {ctx['ids']}" if ctx["ids"] is not None else ""
    print(f"summary: base {ctx['base']} head {ctx['head']}{ids}, {len(mutants)} mutants in {files} file{'s' * (files != 1)}, "
          f"{len(ctx['tests'])} test{'s' * (len(ctx['tests']) != 1)}, {len(labels)} interpreter{'s' * (len(labels) != 1)}")
    for label in labels:
        c = counts[label]
        print(f"summary {label}: {c['KILLED']} KILLED, {c['SURVIVED']} SURVIVED, {c['BADMATCH']} BADMATCH")
    for m in mutants:
        if "SURVIVED" in m["results"].values():
            print(f"SURVIVED {m['id']} {m['file']}:{m['line']}:{m['col']} {m['operator']} {show(m['old'])} -> {show(m['new'])}")


def prepare(ns) -> tuple:
    """Validated inputs and the mutant list."""
    root = check_root(ns.root)
    base, head = resolve(root, "base", ns.base), resolve(root, "head", ns.head)
    sources, mutants = {}, []
    for path in sorted(ns.file):
        src = Source(path, head_source(root, head, path))
        sources[path] = src
        mutants += file_mutants(src, changed_lines(root, base, head, path))
    for t in ns.test:
        if not TEST_ID_RE.fullmatch(t):
            raise Fail("test", f"{show(t)} is not a unittest id")
    pys = [(p, check_py(p)) for p in ns.py]
    for i, (b, exe) in enumerate(pys):   # M-P4 (DEC-0265, DEC-0276): one interpreter under two spellings
        for a, earlier in pys[:i]:
            if os.path.realpath(exe) == os.path.realpath(earlier):
                raise Fail("args", f"--py {show(b)} is the same interpreter as --py {show(a)}")
    for i, m in enumerate(mutants, 1):
        m["id"] = f"m{i:04d}"
    last = len(mutants)
    mutants = [m for m in mutants if not m["docstring"]]
    if ns.ids is not None:
        first, end = ns.id_range
        if first < 1 or end > last:
            raise Fail("ids", f"--ids {show(ns.ids)} is outside m0001-m{last:04d}")
        mutants = [m for m in mutants if first <= int(m["id"][1:]) <= end]
        if not mutants:
            raise Fail("ids", f"--ids {show(ns.ids)} selects no mutant: every id in it is a docstring")
    for m in mutants:
        m["text"] = mutated_text(sources[m["file"]], m)
    ctx = {"base": base, "head": head, "files": sorted(ns.file), "tests": list(ns.test), "py": list(ns.py),
           "ids": ns.ids}
    return root, ctx, sources, mutants, pys


def execute(ns) -> int:
    if ns.list_operators:
        for name, what in OPERATORS:
            print(f"{name} -- {what}")
        return 0
    root, ctx, sources, mutants, pys = prepare(ns)
    printer = Printer(mutants, not ns.json)
    run_all(root, ctx["head"], mutants, sources, pys, list(ns.test), ns.jobs, printer)
    counts = summary(mutants, ns.py)
    rc = 1 if any(c["SURVIVED"] for c in counts.values()) else 0
    if ns.json:
        keys = ("id", "file", "line", "col", "operator", "old", "new", "results", "reason")
        doc = dict(ctx, operators=[n for n, _ in OPERATORS], mutants=[{k: m[k] for k in keys} for m in mutants],
                   summary=counts, rc=rc)
        print(json.dumps(doc, indent=1))
    else:
        report_text(ctx, mutants, ns.py, counts)
    return rc


def json_form(argv: list) -> bool:
    """Whether a FAIL prints in JSON form: --json as argparse parses it (M-O2, DEC-0272), so `-- --json` is not it."""
    ap = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    ap.add_argument("--json", action="count", default=0)

    def refuse(message):
        raise Fail("args", message)
    ap.error = refuse
    try:
        return ap.parse_known_args(argv)[0].json > 0
    except Fail:
        return False


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if hasattr(sys.stdout, "reconfigure"):   # M-E4 (DEC-0263, O4): UTF-8 whatever the locale; a name git stores as
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")   # bytes that are not UTF-8 prints escaped
    as_json = json_form(argv)
    Stop.reset()
    previous = {}
    if threading.current_thread() is threading.main_thread():   # M-X3, M-X4: for this run only
        for sig in (signal.SIGINT, signal.SIGTERM):
            previous[sig] = signal.signal(sig, Stop.handler)
    try:
        try:
            try:
                rc = execute(parse_args(argv))
            except Fail:
                raise
            except Exception as exc:      # never a traceback: an unexpected error is a named FAIL too (one line, M-X2)
                raise Fail("internal", one_line(f"{type(exc).__name__}: {exc}")) from None
            Stop.check()                  # a signal during the report still ends the output with its FAIL
            return rc
        except Fail as exc:
            if Stop.signal is not None:   # whatever the killed runs made of it, the run was stopped by the signal
                exc = Fail("interrupt", f"stopped by {signal.Signals(Stop.signal).name}")
            if as_json:
                print(json.dumps({"rc": 2, "fail": [{"check": f"mutate.{exc.check}", "reason": exc.reason}]}))
            else:
                print(f"FAIL mutate.{exc.check}: {exc.reason}")
            return 2
    finally:
        for sig, handler in previous.items():   # None: a handler not set from Python, put back as the default
            signal.signal(sig, signal.SIG_DFL if handler is None else handler)


if __name__ == "__main__":
    sys.exit(main())
