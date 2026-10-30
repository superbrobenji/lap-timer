#!/usr/bin/env python3
"""load_context -- print one session's read list and write its expected list (program tooling Task 1).

  python3 tools/program/load_context.py [--root <tree>] <kind>

<kind> is a key of `lists` in docs/superpowers/state/program/context.yml: a session kind
(stream-a-block, stream-b-block, sync) or a dispatch role (a key of `models` in tools/gates/config.yml).
Each entry is {path, lines "a-b", why, optional ref}. For the stream kinds the front matter `context_files`
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
The session id is $CLAUDE_CODE_SESSION_ID (equal to the hook input's session_id, spike 2026-10-30).

A failure prints only `FAIL context.<kind>.<path>: <reason>` lines (rc 1), never a partial list and never
an expected list: a missing path, a missing or malformed range (a range past the end of the file included),
a range over MAX_LINES lines or over MAX_BYTES bytes, a missing ref, an unsafe path or ref, an unreadable
list, no session id, and a printed list over MAX_OUTPUT bytes.
"""
from __future__ import annotations

import contextlib
import hashlib
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import yaml  # noqa: E402

from tools.gates.lib import GateError, gate_args, load_yaml, repo_root  # noqa: E402

MAX_LINES = 2000        # the Read tool's per-call line limit
MAX_BYTES = 64000       # largest range one Read returns whole in the strictest session measured (spike 2026-10-30)
MAX_OUTPUT = 16384      # the loader's whole stdout; below the Bash tool's 25,000-byte limit
CONTEXT_YML = "docs/superpowers/state/program/context.yml"
HANDOFFS = {"stream-a-block": "docs/superpowers/state/strict-delivery/handoff.md",
            "stream-b-block": "docs/superpowers/state/prototype/handoff.md"}
CONTEXT_DIR = os.path.join(".superpowers", "context")
KIND_RE = re.compile(r"[a-z0-9][a-z0-9_-]*")
SESSION_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{7,63}")
RANGE_RE = re.compile(r"([1-9][0-9]*)-([1-9][0-9]*)")
ENTRY_KEYS = {"path", "lines", "why", "ref"}


class Failure(Exception):
    """One entry cannot be read: `ident` is the <path> part of the FAIL id."""

    def __init__(self, ident: str, reason: str):
        super().__init__(reason)
        self.ident = ident
        self.reason = reason


def blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def git_bytes(root: str, *args) -> "subprocess.CompletedProcess":
    return subprocess.run(["git", "-C", root, *args], capture_output=True, check=False)


def safe_relpath(path) -> bool:
    return (isinstance(path, str) and path != "" and "\0" not in path and not path.startswith("/")
            and "\\" not in path and os.path.normpath(path) == path and ".." not in path.split("/"))


def safe_ref(ref) -> bool:
    return isinstance(ref, str) and ref != "" and not ref.startswith("-") and re.fullmatch(r"[^\s\x00-\x1f\x7f:~^?*\[\\]+(?:[~^][0-9]*)*", ref) is not None


def ref_copy_path(ref: str, path: str) -> str:
    tag = re.sub(r"[^A-Za-z0-9._-]", "_", ref)[:40] + "-" + hashlib.sha1(ref.encode()).hexdigest()[:8]
    return os.path.join(CONTEXT_DIR, "refs", tag, path)


def read_bytes(root: str, e: dict, ident: str):
    """(path-to-read, bytes) for an entry; a Failure when the path, the ref or the path at the ref is absent."""
    path, ref = e["path"], e.get("ref")
    if ref is None:
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            raise Failure(ident, "missing path (not a file under the root)")
        if not os.path.realpath(full).startswith(os.path.realpath(root) + os.sep):
            raise Failure(ident, "path resolves outside the root")
        with open(full, "rb") as fh:
            return path, fh.read()
    if not safe_ref(ref):
        raise Failure(ident, f"unsafe ref {ref!r}")
    if git_bytes(root, "rev-parse", "--verify", "--quiet", "--end-of-options", ref + "^{commit}").returncode != 0:
        raise Failure(ident, f"missing ref {ref}")
    shown = git_bytes(root, "show", f"{ref}:{path}")
    if shown.returncode != 0:
        raise Failure(ident, f"missing path at ref {ref}")
    rel = ref_copy_path(ref, path)
    full = os.path.join(root, rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as fh:
        fh.write(shown.stdout)
    return rel, shown.stdout


def check_entry(root: str, e) -> tuple:
    """(path-to-read, lines, blob, why) for one entry, or a Failure."""
    if not isinstance(e, dict):
        raise Failure("entry", f"entry is not a mapping: {e!r}")
    ident = e["path"] if isinstance(e.get("path"), str) and e["path"] else "entry"
    if not safe_relpath(e.get("path")):
        raise Failure(ident, f"path is missing or unsafe (absolute, '..', backslash): {e.get('path')!r}")
    unknown = sorted(set(e) - ENTRY_KEYS)
    if unknown:
        raise Failure(ident, f"unknown key(s) {', '.join(map(str, unknown))}")
    why = e.get("why")
    if not isinstance(why, str) or not why.strip():
        raise Failure(ident, "why is missing or empty")
    if "ref" in e and not isinstance(e["ref"], str):
        raise Failure(ident, f"ref is not a string: {e['ref']!r}")
    lines = e.get("lines")
    m = RANGE_RE.fullmatch(lines) if isinstance(lines, str) else None
    if m is None or int(m.group(1)) > int(m.group(2)):
        raise Failure(ident, f"missing range: lines must be 'a-b' with 1 <= a <= b, got {lines!r}")
    a, b = int(m.group(1)), int(m.group(2))
    if b - a + 1 > MAX_LINES:
        raise Failure(ident, f"range {a}-{b} is over {MAX_LINES} lines")
    read_path, data = read_bytes(root, e, ident)
    rows = data.split(b"\n")           # the Read tool counts the empty line after a final newline
    if b > len(rows):
        raise Failure(ident, f"missing range: {a}-{b} is past the end of the file ({len(rows)} lines)")
    size = len(b"\n".join(rows[a - 1:b]))
    if size > MAX_BYTES:
        raise Failure(ident, f"range {a}-{b} is {size} bytes, over {MAX_BYTES} bytes")
    return read_path, f"{a}-{b}", blob_sha(data), " ".join(why.split())


def handoff_entries(root: str, kind: str) -> list:
    """The `context_files` of the kind's handoff front matter; [] when the kind has none or the field is absent."""
    rel = HANDOFFS.get(kind)
    if rel is None or not os.path.isfile(os.path.join(root, rel)):
        return []
    with open(os.path.join(root, rel), encoding="utf-8") as fh:
        text = fh.read()
    m = re.match(r"---\n(.*?\n)---\n", text, re.S)
    if m is None:
        raise Failure(rel, "handoff has no front matter")
    try:
        front = yaml.safe_load(m.group(1))
    except yaml.YAMLError as exc:
        raise Failure(rel, f"front matter is not valid YAML: {' '.join(str(exc).split())}") from None
    if not isinstance(front, dict):
        raise Failure(rel, "front matter is not a mapping")
    if "context_files" not in front:
        return []
    if not isinstance(front["context_files"], list):
        raise Failure(rel, "context_files is not a list")
    if not all(isinstance(e, dict) for e in front["context_files"]):
        raise Failure(rel, "context_files holds an entry that is not a mapping")
    return front["context_files"]


def write_expected(root: str, kind: str, session: str, entries: list) -> None:
    d = os.path.join(root, CONTEXT_DIR, "expected", session)
    os.makedirs(d, exist_ok=True)
    tmp = os.path.join(d, f".{kind}.json.tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump({"kind": kind, "session": session,
                   "entries": [{"path": p, "lines": l, "blob": s} for p, l, s, _ in entries]}, fh, indent=1)
        fh.write("\n")
    os.replace(tmp, os.path.join(d, kind + ".json"))


def load(root: str, kind: str) -> tuple:
    """(rc, text)."""
    fails = []

    def fail(ident, reason):
        fails.append(f"FAIL context.{kind}.{ident}: {reason}")

    try:
        lists = load_yaml(os.path.join(root, CONTEXT_YML), root)
    except GateError as exc:
        return 1, f"FAIL context.{kind}.list: {exc}\n"
    table = lists.get("lists") if isinstance(lists, dict) else None
    if not isinstance(table, dict):
        return 1, f"FAIL context.{kind}.list: {CONTEXT_YML} has no `lists` mapping\n"
    if kind not in table:
        return 1, f"FAIL context.{kind}.list: no list named {kind!r} in {CONTEXT_YML}\n"
    base = table[kind]
    if not isinstance(base, list) or not base:
        return 1, f"FAIL context.{kind}.list: list {kind!r} is not a non-empty sequence\n"
    items = list(base)
    try:
        items += handoff_entries(root, kind)
    except Failure as f:
        fail(f.ident, f.reason)
    rows = []
    for e in items:
        try:
            rows.append(check_entry(root, e))
        except Failure as f:
            fail(f.ident, f.reason)
    session = os.environ.get("CLAUDE_CODE_SESSION_ID", "")
    if not fails and not SESSION_RE.fullmatch(session):
        fail("session", "CLAUDE_CODE_SESSION_ID is unset or not a session id: the expected list has no key")
    text = "".join(f"{p} lines {l} blob {s} -- {w}\n" for p, l, s, w in rows)
    if not fails and len(text.encode()) > MAX_OUTPUT:
        fail("output", f"the list prints {len(text.encode())} bytes, over {MAX_OUTPUT} bytes")
    if fails:
        if SESSION_RE.fullmatch(session):     # fail closed: a failed load leaves no earlier list standing
            with contextlib.suppress(FileNotFoundError):
                os.unlink(os.path.join(root, CONTEXT_DIR, "expected", session, kind + ".json"))
        return 1, "\n".join(fails) + "\n"
    write_expected(root, kind, session, rows)
    return 0, text


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    ap = gate_args("load_context")
    ap.add_argument("kind", help="a session kind or dispatch role named in context.yml")
    try:
        ns = ap.parse_args(argv)
        if not KIND_RE.fullmatch(ns.kind):
            raise ValueError(f"kind {ns.kind!r} is not [a-z0-9_-]+")
        root = os.path.abspath(ns.root) if ns.root else repo_root()
    except (SystemExit, ValueError, GateError) as exc:
        why = str(exc) if isinstance(exc, (ValueError, GateError)) else f"bad arguments (argparse exit {exc.code})"
        print(f"FAIL context.input: {why}")
        return 1
    rc, text = load(root, ns.kind)
    sys.stdout.write(text)
    return rc


if __name__ == "__main__":
    sys.exit(main())
