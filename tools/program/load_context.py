#!/usr/bin/env python3
"""load_context -- print one session's read list and write its expected list (program tooling Task 1).

  python3 tools/program/load_context.py [--root <tree>] <kind>

<kind> is a key of `lists` in docs/superpowers/state/program/context.yml: a session kind
(stream-a-block, stream-b-block, sync) or a dispatch role (a key of `models` in tools/gates/config.yml).
Each entry is {path, lines "a-b" or "a-end", why, optional ref}. `a-end` (DEC-0166) resolves to the file's
current last line (the last line holding text: a final newline ends that line) and is split into consecutive
entries of at most MAX_LINES lines and MAX_BYTES bytes each; stdout and the expected list carry the resolved
ranges. `ref: board:B` (DEC-0167) reads origin/<branch>, the branch being the first backticked name in the branch
cell of stream B's row on the board (tools/program/board.py), the board file as it is on disk and tracked at HEAD
(DEC-0177). A path holds only [A-Za-z0-9._/-] and printable
non-ASCII characters (DEC-0170). For the stream kinds the front matter `context_files`
list of the kind's handoff is appended (stream-a-block: strict-delivery handoff, stream-b-block: prototype
handoff); an absent field adds nothing.

stdout, success (rc 0), one line per entry, nothing else:
  <path-to-read> lines <a>-<b> blob <sha> -- <why>
<path-to-read> is relative to the root; for an entry with a `ref` it is the copy of `git show <ref>:<path>`
the loader wrote under .superpowers/context/refs/. <sha> is the git blob sha of the bytes on disk (sha1 of
"blob <n>\\0" + bytes, the same value `git hash-object` gives a file with no filters).

Expected list (git-ignored), written only on success, atomically:
  <root>/.superpowers/context/expected/<session id>/<kind>.json
  {"kind": <kind>, "session": <session id>, "entries": [{"path": <path-to-read>, "lines": "a-b", "blob": <sha>}]}
The session id is $CLAUDE_CODE_SESSION_ID (equal to the hook input's session_id, spike 2026-10-30), a lowercase UUID
8-4-4-4-12 matched as a whole string (DEC-0188; form re-checked in fix round 7).

A failure prints only `FAIL context.<kind>.<path>: <reason>` lines (rc 1), each one escaped line, never a
partial list; it also removes the session's earlier expected list for the kind (or empties it, or names it as
still standing, when it cannot be removed): a missing path, a missing or malformed range (a range past the end
of the file included), a range over MAX_LINES lines or over MAX_BYTES bytes, a missing ref, an unsafe path or
ref, an unreadable list or handoff, a read input or write target that is missing, unreadable, of the wrong kind
or reached through a symlink below the root (spec D32), a root that is not the top of a git work tree, no
session id, and a printed list over MAX_OUTPUT bytes. Every value a FAIL line echoes is clipped to CLIP
characters, and FAIL lines are printed while the whole output stays under MAX_OUTPUT bytes, then one line
`FAIL context.<kind>.output: N further failures not shown` (DEC-0172). The input-domain table is
docs/superpowers/state/program-tooling/T1-input-domain.md.
"""
from __future__ import annotations

import contextlib
import errno
import hashlib
import json
import os
import re
import reprlib
import stat
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import yaml  # noqa: E402

from tools.gates.lib import GateError, _StrictLoader, gate_args, load_yaml  # noqa: E402
from tools.program.board import BOARD, BoardError, stream_branch  # noqa: E402

MAX_LINES = 2000        # the Read tool's per-call line limit
MAX_BYTES = 16000       # a range of at most MAX_LINES lines and this many bytes stays under the Read tool's 25,000-token limit
#   (measured 2026-10-30, plan program-tooling Global Constraints: worst case 1.0 token per byte of any content incl. line prefixes,
#   so 16,000 bytes in at most 2000 lines is at most about 16,000 + 2000 x 2 = 20,000 tokens)
MAX_OUTPUT = 16384      # the loader's stdout must be LESS than this (16 KB), FAIL output included; below the Bash tool's 25,000-byte limit
CLIP = 200              # every value a FAIL line echoes is cut to this many characters (DEC-0172)
CONTEXT_YML = "docs/superpowers/state/program/context.yml"
HANDOFFS = {"stream-a-block": "docs/superpowers/state/strict-delivery/handoff.md",
            "stream-b-block": "docs/superpowers/state/prototype/handoff.md"}
CONTEXT_DIR = os.path.join(".superpowers", "context")
KIND_RE = re.compile(r"[a-z0-9][a-z0-9_-]*")
SESSION_RE = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")    # DEC-0188: the hook's id, a lowercase UUID
RANGE_RE = re.compile(r"([1-9][0-9]*)-([1-9][0-9]*|end)")
MAX_DIGITS = 18         # a line number with more digits is past the end of any file; never converted (3.11+ int() refuses 4,300)
PATH_ASCII = frozenset("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._/-")     # DEC-0170
BOARD_REFS = {"board:B": "B"}      # DEC-0167: the ref is origin/<the branch in this stream's board row>
ENTRY_KEYS = {"path", "lines", "why", "ref"}


class Failure(Exception):
    """One entry cannot be read: `ident` is the <path> part of the FAIL id."""

    def __init__(self, ident: str, reason: str):
        super().__init__(reason)
        self.ident = ident
        self.reason = reason


def blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def git_env() -> dict:
    """The environment of every git call: no GIT_* variable may point git at another repository, index or config
    (the root names the repository), pathspecs are literal, messages in the C locale (spec D32: pinned output forms)."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(LC_ALL="C", GIT_LITERAL_PATHSPECS="1")
    return env


def git_bytes(root: str, *args) -> "subprocess.CompletedProcess":
    return subprocess.run(["git", "-C", root, *args], capture_output=True, check=False, env=git_env())


_REPR = reprlib.Repr()                 # bounded work and size for any parsed YAML value, alias chains included
_REPR.maxlevel, _REPR.maxlist, _REPR.maxdict, _REPR.maxset, _REPR.maxfrozenset, _REPR.maxtuple = 3, 6, 6, 6, 6, 6
_REPR.maxstring = _REPR.maxother = _REPR.maxlong = CLIP


def clip(text: str) -> str:
    return text if len(text) <= CLIP else text[:CLIP - 3] + "..."


def echo(value) -> str:
    """`value` as a FAIL line shows it: escaped (one printable line) and at most CLIP characters (DEC-0172)."""
    return clip(show(value[:CLIP + 1] if isinstance(value, str) else _REPR.repr(value)))


def symlink_below(root: str, rel: str, last: bool = True):
    """The first component of `rel` (relative to the root, the last one only when `last`) that is a symlink, or None."""
    parts = rel.split("/")
    for i in range(len(parts) if last else len(parts) - 1):
        sub = "/".join(parts[:i + 1])
        if os.path.islink(os.path.join(root, sub)):
            return sub
    return None


def input_problem(root: str, rel: str):
    """Why the read input root/rel is outside the default contract (spec D32), or None: missing, reached through
    a symlink below the root, or not a regular file (a directory, FIFO or socket is never opened)."""
    link = symlink_below(root, rel)
    if link is not None:
        return f"symlink in path ({echo(link)})"
    try:
        mode = os.stat(os.path.join(root, rel)).st_mode
    except OSError as exc:
        return f"missing or unreadable ({exc.strerror})"
    return None if stat.S_ISREG(mode) else "not a regular file"


def write_file(root: str, rel: str, data: bytes):
    """Write `data` to root/rel through a temporary file and a rename; None, or why it cannot be written: a symlink
    at any component below the root (never written through) or any OS error (spec D32, F6)."""
    link = symlink_below(root, rel)
    if link is not None:
        return f"cannot write {echo(rel)}: symlink in path ({echo(link)})"
    full = os.path.join(root, rel)
    try:
        os.makedirs(os.path.dirname(full), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(full), prefix=".", suffix=".tmp")
        try:
            with os.fdopen(fd, "wb") as fh:
                fh.write(data)
            os.replace(tmp, full)
        except OSError:
            with contextlib.suppress(OSError):
                os.unlink(tmp)
            raise
    except OSError as exc:
        return f"cannot write {echo(rel)}: {exc.strerror or type(exc).__name__}"
    return None


def remove_list(root: str, rel: str):
    """Remove the earlier expected list root/rel (F4, F6). None when no list stands there any more; else why: a
    symlinked directory component (never removed through), or an OS error, after which the file is emptied in place
    when that is possible, so it lists nothing."""
    link = symlink_below(root, rel, last=False)
    if link is not None:
        return f"earlier expected list not removed: symlink in path ({echo(link)})"
    full = os.path.join(root, rel)
    try:                                             # os.path.lexists() reads EACCES as "absent" (F8)
        os.lstat(full)
    except OSError as exc:
        if isinstance(exc, (FileNotFoundError, NotADirectoryError)) or exc.errno == errno.ENAMETOOLONG:
            return None                              # no file can stand at that name
        return f"earlier expected list cannot be checked ({exc.strerror or type(exc).__name__}): it may still stand"
    try:
        os.unlink(full)
        return None
    except OSError as exc:
        why = exc.strerror or type(exc).__name__
    try:
        os.close(os.open(full, os.O_WRONLY | os.O_TRUNC | os.O_NOFOLLOW))
    except OSError:
        return f"earlier expected list could not be removed or emptied ({why}): it still stands"
    return f"earlier expected list could not be removed ({why}): emptied instead"




def unprintable(text) -> bool:
    """True when `text` holds a character str.isprintable() rejects (controls incl. C1, line and paragraph
    separators, bidi and zero-width format characters, tab, newline): none may reach a path or an output line."""
    return not str(text).isprintable()


def show(text) -> str:
    """text with every non-printable character written as a \\x/\\u escape, so a FAIL line is always one line."""
    return "".join(c if c.isprintable() else c.encode("unicode_escape").decode() for c in str(text))


def path_problem(path):
    """Why `path` is outside the whitelist (relative, no control character, not under .git/), or None."""
    if not isinstance(path, str) or path == "":
        return "path is missing"
    if unprintable(path):
        return "path holds a non-printable character"
    if any(c not in PATH_ASCII and (c.isascii() or not c.isprintable()) for c in path):
        return "path holds a character outside [A-Za-z0-9._/-] and printable non-ASCII (whitespace included)"
    if path.startswith("/") or "\\" in path or os.path.normpath(path) != path or ".." in path.split("/"):
        return "path is absolute, has '..' or a backslash, or is not normalised"
    if path.split("/")[0] == ".git":
        return "path is under .git/"
    return None


def safe_ref(ref) -> bool:
    return isinstance(ref, str) and ref != "" and not ref.startswith("-") and not unprintable(ref) and re.fullmatch(r"[^\s\x00-\x1f\x7f:~^?*\[\\]+(?:[~^][0-9]*)*", ref) is not None


def ref_copy_path(ref: str, path: str) -> str:
    tag = re.sub(r"[^A-Za-z0-9._-]", "_", ref)[:40] + "-" + hashlib.sha1(ref.encode()).hexdigest()[:8]
    return os.path.join(CONTEXT_DIR, "refs", tag, path)


def tree_mode(root: str, rev: str, path: str):
    """The git mode of `path` in tree `rev` ('100644', '120000', '040000', ...), or None when absent."""
    p = git_bytes(root, "ls-tree", "-z", rev, "--", path)
    if p.returncode != 0:
        return None
    for row in p.stdout.split(b"\0"):
        meta, _, name = row.partition(b"\t")
        if os.fsdecode(name) == path:                 # the name as open() and argv encode a path (R8)
            return meta.split(b" ")[0].decode()
    return None


def board_ref(root: str, ident: str, stream: str) -> str:
    """origin/<branch> for the stream's board row (DEC-0167), or a Failure naming the board problem. The board is the
    file on disk (DEC-0177 working-tree: an uncommitted edit counts), tracked at HEAD, through the D32 checks."""
    problem = input_problem(root, BOARD)
    if problem:
        raise Failure(ident, f"ref board:{stream}: board {BOARD} {problem}")
    if tree_mode(root, "HEAD", BOARD) not in ("100644", "100755"):
        raise Failure(ident, f"ref board:{stream}: board {BOARD} is not a regular file tracked at HEAD")
    try:
        with open(os.path.join(root, BOARD), "rb") as fh:
            text = fh.read().decode("utf-8")
    except OSError as exc:
        raise Failure(ident, f"ref board:{stream}: board {BOARD} unreadable ({exc.strerror})") from None
    except UnicodeDecodeError:
        raise Failure(ident, f"ref board:{stream}: board {BOARD} is not valid UTF-8") from None
    try:
        return "origin/" + stream_branch(text, stream)
    except BoardError as exc:
        raise Failure(ident, f"ref board:{stream}: {BOARD}: {echo(str(exc))}") from None


def read_bytes(root: str, e: dict, ident: str):
    """(path-to-read, bytes) for an entry: a regular file tracked at HEAD (at the ref), no symlink component."""
    path, ref = e["path"], e.get("ref")
    if ref is None:
        link = symlink_below(root, path)
        if link is not None:
            raise Failure(ident, f"symlink in path ({echo(link)})")
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            raise Failure(ident, "missing path (not a file under the root)")
        if tree_mode(root, "HEAD", path) not in ("100644", "100755"):
            raise Failure(ident, "not a regular file tracked at HEAD")
        try:
            with open(full, "rb") as fh:
                return path, fh.read()
        except OSError as exc:
            raise Failure(ident, f"unreadable ({exc.strerror})") from None
    origin = ""
    if ref in BOARD_REFS:
        stream = BOARD_REFS[ref]
        ref, origin = board_ref(root, ident, stream), f" (the branch in the board's stream {stream} row)"
    if not safe_ref(ref):
        raise Failure(ident, f"unsafe ref {echo(ref)}")
    rev = git_bytes(root, "rev-parse", "--verify", "--quiet", "--end-of-options", ref + "^{commit}")
    if rev.returncode != 0:
        raise Failure(ident, f"missing ref {echo(ref)}{origin}")
    sha = rev.stdout.decode().strip()
    mode = tree_mode(root, sha, path)
    if mode is None:
        raise Failure(ident, f"missing path at ref {echo(ref)}")
    if mode == "120000":
        raise Failure(ident, f"symlink at ref {echo(ref)}")
    if mode not in ("100644", "100755"):
        raise Failure(ident, f"not a regular file at ref {echo(ref)}")
    shown = git_bytes(root, "cat-file", "blob", f"{sha}:{path}")      # the raw blob: no textconv, no filters
    if shown.returncode != 0:
        raise Failure(ident, f"missing path at ref {echo(ref)}")
    rel = ref_copy_path(ref, path)
    problem = write_file(root, rel, shown.stdout)
    if problem:
        raise Failure(ident, problem)
    return rel, shown.stdout


def parse_range(lines, ident: str) -> tuple:
    """(a, b) for `lines` "a-b", (a, None) for "a-end", or a Failure; the echoed value is clipped (DEC-0172) and no
    digit count reaches int() unbounded (F7)."""
    m = RANGE_RE.fullmatch(lines) if isinstance(lines, str) else None
    if m is None:
        raise Failure(ident, f"missing range: lines must be 'a-b' with 1 <= a <= b, or 'a-end', got {echo(lines)}")
    if any(len(g) > MAX_DIGITS for g in m.groups()):
        raise Failure(ident, f"missing range: a line number of over {MAX_DIGITS} digits is past the end of any file, got {echo(lines)}")
    a, b = int(m.group(1)), (None if m.group(2) == "end" else int(m.group(2)))
    if b is not None and a > b:
        raise Failure(ident, f"missing range: lines must be 'a-b' with 1 <= a <= b, or 'a-end', got {echo(lines)}")
    if b is not None and b - a + 1 > MAX_LINES:
        raise Failure(ident, f"range {echo(lines)} is over {MAX_LINES} lines")
    return a, b


def split_open_range(rows: list, a: int, last: int, lines: str, ident: str) -> list:
    """Consecutive (x, y) ranges covering a..last, each of at most MAX_LINES lines and MAX_BYTES bytes (DEC-0166);
    a single line over MAX_BYTES is a Failure."""
    out, x = [], a
    while x <= last:
        y, size = x, len(rows[x - 1])
        if size > MAX_BYTES:
            raise Failure(ident, f"line {x} is {size} bytes, over {MAX_BYTES} bytes: {echo(lines)} cannot be split into readable ranges")
        while y < last and y - x + 1 < MAX_LINES and size + 1 + len(rows[y]) <= MAX_BYTES:
            size += 1 + len(rows[y])
            y += 1
        out.append((x, y))
        x = y + 1
    return out


def check_entry(root: str, e) -> list:
    """[(path-to-read, lines, blob, why)] for one entry (several for an `a-end` range), or a Failure."""
    if not isinstance(e, dict):
        raise Failure("entry", f"entry is not a mapping: {echo(e)}")
    ident = e["path"] if isinstance(e.get("path"), str) and e["path"] else "entry"
    problem = path_problem(e.get("path"))
    if problem:
        raise Failure(ident, problem)
    unknown = sorted((k for k in e if k not in ENTRY_KEYS), key=repr)      # keys of any YAML type
    if unknown:
        raise Failure(ident, f"unknown key(s) {echo(', '.join(_REPR.repr(k) for k in unknown))}")
    why = e.get("why")
    if not isinstance(why, str) or not why.strip():
        raise Failure(ident, "why is missing or empty")
    if unprintable(why):
        raise Failure(ident, "why holds a non-printable character")
    if "ref" in e and not isinstance(e["ref"], str):
        raise Failure(ident, f"ref is not a string: {echo(e['ref'])}")
    lines = e.get("lines")
    a, b = parse_range(lines, ident)
    read_path, data = read_bytes(root, e, ident)
    if not data:
        raise Failure(ident, "empty file: nothing a Read can cover")
    rows = data.split(b"\n")           # the Read tool counts the empty line after a final newline
    sha, why = blob_sha(data), " ".join(why.split())
    if b is None:                      # a-end: the last line holding text (a final newline ends it)
        last = len(rows) - 1 if data.endswith(b"\n") else len(rows)
        if a > last:
            raise Failure(ident, f"missing range: {echo(lines)} starts past the last line ({last})")
        return [(read_path, f"{x}-{y}", sha, why) for x, y in split_open_range(rows, a, last, lines, ident)]
    if b > len(rows):
        raise Failure(ident, f"missing range: {echo(lines)} is past the end of the file ({len(rows)} lines)")
    size = len(b"\n".join(rows[a - 1:b]))
    if size > MAX_BYTES:
        raise Failure(ident, f"range {echo(lines)} is {size} bytes, over {MAX_BYTES} bytes")
    return [(read_path, f"{a}-{b}", sha, why)]


def handoff_entries(root: str, kind: str) -> list:
    """The `context_files` of the kind's handoff front matter; [] when the kind has none or the field is absent."""
    rel = HANDOFFS.get(kind)
    if rel is None:
        return []
    problem = input_problem(root, rel)               # spec D32: missing, a symlink or not a regular file FAILs
    if problem:
        raise Failure(rel, f"handoff {problem}")
    try:
        with open(os.path.join(root, rel), "rb") as fh:
            text = fh.read().decode("utf-8")
    except OSError as exc:
        raise Failure(rel, f"handoff unreadable ({exc.strerror})") from None
    except UnicodeDecodeError:
        raise Failure(rel, "handoff is not valid UTF-8") from None
    m = re.match(r"---\n(.*?\n)---\n", text, re.S)
    if m is None:
        raise Failure(rel, "handoff has no front matter")
    try:                                             # the shared strict loader: duplicate and merge keys refused
        front = yaml.load(m.group(1), Loader=_StrictLoader)  # noqa: S506 -- _StrictLoader is a SafeLoader
    except Exception as exc:                         # YAMLError, RecursionError, ValueError (int digit limit)
        raise Failure(rel, f"front matter is not valid YAML: {type(exc).__name__}: {echo(str(exc))}") from None
    if not isinstance(front, dict):
        raise Failure(rel, "front matter is not a mapping")
    if "context_files" not in front:
        return []
    if not isinstance(front["context_files"], list):
        raise Failure(rel, "context_files is not a list")
    if not all(isinstance(e, dict) for e in front["context_files"]):
        raise Failure(rel, "context_files holds an entry that is not a mapping")
    return front["context_files"]


def list_path(kind: str, session: str) -> str:
    return "/".join((CONTEXT_DIR, "expected", session, kind + ".json"))


def write_expected(root: str, kind: str, session: str, entries: list):
    """Write the expected list atomically; None, or why it cannot be written (write_file)."""
    text = json.dumps({"kind": kind, "session": session,
                       "entries": [{"path": p, "lines": l, "blob": s} for p, l, s, _ in entries]}, indent=1) + "\n"
    return write_file(root, list_path(kind, session), text.encode("utf-8"))


def one_line(text) -> str:
    """text with every non-printable character (line breaks included) escaped, then runs of spaces collapsed: one printable line."""
    return " ".join(show(text).split())


def table_problem(doc, kind: str):
    """(problem, list) for the parsed context.yml: problem is None when `kind` names a non-empty list of it."""
    if not isinstance(doc, dict):
        return f"{CONTEXT_YML} is not a mapping", None
    if not all(isinstance(k, str) for k in doc):
        return f"{CONTEXT_YML} has a key that is not a string", None
    table = doc.get("lists")
    if not isinstance(table, dict):
        return f"{CONTEXT_YML} has no `lists` mapping", None
    if not all(isinstance(k, str) for k in table):
        return f"{CONTEXT_YML} `lists` has a key that is not a string", None
    if kind not in table:
        return f"no list named {echo(kind)} in {CONTEXT_YML}", None
    base = table[kind]
    if not isinstance(base, list) or not base:
        return f"list {echo(kind)} is not a non-empty sequence", None
    return None, base


def build(root: str, kind: str, session: str, fail) -> str:
    """The text to print for `kind`, writing its expected list; every failure goes to fail(ident, reason) and returns ''."""
    problem = input_problem(root, CONTEXT_YML)       # spec D32: never a symlink, FIFO or directory
    if problem:
        fail("list", f"{CONTEXT_YML}: {problem}")
        return ""
    try:
        doc = load_yaml(os.path.join(root, CONTEXT_YML), root)
    except GateError as exc:
        fail("list", echo(str(exc)))
        return ""
    except Exception as exc:                         # RecursionError (deep nesting), ValueError (int digit limit)
        fail("list", f"{CONTEXT_YML}: cannot be parsed: {type(exc).__name__}: {echo(str(exc))}")
        return ""
    problem, base = table_problem(doc, kind)
    if problem:
        fail("list", problem)
        return ""
    items = list(base)
    try:
        items += handoff_entries(root, kind)
    except Failure as f:
        fail(f.ident, f.reason)
    rows = []
    for e in items:
        try:
            rows.extend(check_entry(root, e))
        except Failure as f:
            fail(f.ident, f.reason)
    text = "".join(f"{p} lines {l} blob {s} -- {w}\n" for p, l, s, w in rows)
    if fail.count == 0 and not SESSION_RE.fullmatch(session):
        fail("session", "CLAUDE_CODE_SESSION_ID is unset or not a session id (a lowercase UUID): the expected list has no key")
    if fail.count == 0 and len(text.encode()) >= MAX_OUTPUT:
        fail("output", f"the list prints {len(text.encode())} bytes, {MAX_OUTPUT} bytes or more")
    if fail.count == 0:
        problem = write_expected(root, kind, session, rows)
        if problem:
            fail(list_path(kind, session), problem)
    return text


def load(root: str, kind: str) -> tuple:
    """(rc, text). Every failure is one `FAIL context.<kind>.<id>: <reason>` line, escaped, whatever the failure level."""
    fails = []

    def fail(ident, reason):
        fails.append(f"FAIL context.{echo(kind)}.{clip(one_line(ident))}: {one_line(reason)}")
        fail.count = len(fails)
    fail.count = 0
    session = os.environ.get("CLAUDE_CODE_SESSION_ID", "")
    text = ""
    try:
        text = build(root, kind, session, fail)
    except Exception as exc:          # fail closed: nothing outside the whitelist may escape as a traceback
        fail("internal", f"unexpected {type(exc).__name__}: {echo(str(exc))}")
    if fails:
        if SESSION_RE.fullmatch(session):     # fail closed: a failed load leaves no earlier list standing
            problem = remove_list(root, list_path(kind, session))
            if problem:
                fail(list_path(kind, session), problem)
        return 1, capped(kind, fails)
    return 0, text


def capped(kind: str, fails: list) -> str:
    """The FAIL lines while the whole output stays under MAX_OUTPUT bytes; when they do not all fit, the lines
    before the first that would cross it, then one line counting the rest (DEC-0172: every failure counted)."""
    lines = [f + "\n" for f in fails]
    if len("".join(lines).encode()) < MAX_OUTPUT:
        return "".join(lines)
    shown, used = 0, 0
    while True:
        rest = f"FAIL context.{echo(kind)}.output: {len(lines) - shown} further failures not shown\n"
        if used + len(lines[shown].encode()) + len(rest.encode()) >= MAX_OUTPUT:
            return "".join(lines[:shown]) + rest
        used += len(lines[shown].encode())
        shown += 1


def find_root() -> str:
    """$CLAUDE_PROJECT_DIR if set, else the top of the git work tree holding the cwd, found with every GIT_* variable
    removed (DEC-0175: the caller's GIT_DIR/GIT_WORK_TREE never choose the tree); ValueError when there is none."""
    env = os.environ.get("CLAUDE_PROJECT_DIR")
    if env:
        if not env.strip():                          # DEC-0250 treat-as-empty: the --root "is empty" form (R5, R1)
            raise ValueError("--root is empty")
        return env
    try:
        p = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, check=False, env=git_env())
    except OSError as exc:
        raise ValueError(f"git cannot run ({exc.strerror})") from None
    if p.returncode != 0:
        raise ValueError("no --root, no CLAUDE_PROJECT_DIR, and the working directory is not in a git work tree")
    return os.fsdecode(p.stdout).removesuffix("\n")     # as argv and os.environ decode a path (R8); only git's newline (R7)


def root_problem(root: str):
    """Why `root` (--root, else $CLAUDE_PROJECT_DIR, else the git top level) is not the top of a git work tree, or
    None: a symlink (spec D32, DEC-0093), not a directory, or not that tree's top (entries are repository paths)."""
    if os.path.islink(root):
        return f"root {echo(root)} is a symlink"
    if not os.path.isdir(root):
        return f"root {echo(root)} is not a directory"
    try:
        top = git_bytes(root, "rev-parse", "--show-toplevel")
    except OSError as exc:
        return f"git cannot run ({exc.strerror})"
    if top.returncode != 0 or os.path.realpath(os.fsdecode(top.stdout).removesuffix("\n")) != os.path.realpath(root):
        return f"root {echo(root)} is not the top of a git work tree"
    return None


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if hasattr(sys.stdout, "reconfigure"):          # the output is UTF-8 whatever the locale: the bytes MAX_OUTPUT counts
        sys.stdout.reconfigure(encoding="utf-8")
    ap = gate_args("load_context")
    ap.add_argument("kind", help="a session kind or dispatch role named in context.yml")

    def refuse(message):                             # argparse would print the usage and the bad value, unbounded, on stderr
        raise ValueError(f"bad arguments: {message}")
    ap.error = refuse
    try:
        ns = ap.parse_args(argv)
        if ns.root is not None and not ns.root.strip():      # F15: as the gates, never the cwd's tree
            raise ValueError("--root is empty")
        if not KIND_RE.fullmatch(ns.kind):
            raise ValueError(f"kind {echo(ns.kind)} is not [a-z0-9_-]+")
        root = os.path.abspath(ns.root if ns.root is not None else find_root())
        problem = root_problem(root)
        if problem:
            raise ValueError(problem)
    except (SystemExit, ValueError, GateError) as exc:
        why = str(exc) if isinstance(exc, (ValueError, GateError)) else f"bad arguments (argparse exit {exc.code})"
        print(f"FAIL context.input: {clip(one_line(why))}")
        return 1
    rc, text = load(root, ns.kind)
    sys.stdout.write(text)
    return rc


if __name__ == "__main__":
    sys.exit(main())
