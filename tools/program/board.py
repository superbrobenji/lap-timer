"""The stream board's table (docs/superpowers/state/program/board.md, playbook `docs/superpowers/playbooks/two-streams.md`).

Shared by tools/program/load_context.py (DEC-0167: the sync list reads stream B's handoff at origin/<the branch in
stream B's row>) and the drift check (program tooling Task 3). Pure text functions: the caller reads the file and
names the failure; every problem is a BoardError whose message echoes no board text.

The stream table is the first Markdown table whose header's first cell is `stream`. A stream's row is the row whose
first cell's first word is the stream's letter (`A`, `B`); its branch is the first backticked name in the cell under
the first header cell whose first whitespace-separated word is exactly `branch` (DEC-0187 first-word-whole).
"""
from __future__ import annotations

import re

BOARD = "docs/superpowers/state/program/board.md"
BRANCH_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*(?:/[A-Za-z0-9][A-Za-z0-9._-]*)*")
SEPARATOR_RE = re.compile(r":?-{3,}:?")


class BoardError(ValueError):
    """The board does not name what the caller asked for."""


def cells(line: str) -> list:
    """The cells of one Markdown table row `| a | b |`, stripped; a `\\|` stays inside its cell."""
    inner = line.strip()[1:]
    if inner.endswith("|") and not inner.endswith("\\|"):
        inner = inner[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", inner)]


def stream_table(text: str) -> tuple:
    """(header cells, rows of cells) of the stream table, or BoardError."""
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.startswith("|") and cells(line)[0] == "stream":
            header, rows = cells(line), []
            for row in lines[i + 1:]:
                if not row.startswith("|"):
                    break
                c = cells(row)
                if all(SEPARATOR_RE.fullmatch(x) for x in c):
                    continue
                rows.append(c)
            return header, rows
    raise BoardError("no stream table (a table whose header's first cell is `stream`)")


def stream_row(text: str, stream: str) -> tuple:
    """(header cells, the stream's row cells), or BoardError when the row is missing or not unique."""
    header, rows = stream_table(text)
    mine = [r for r in rows if r[0].split(" ")[0] == stream]
    if not mine:
        raise BoardError(f"no stream {stream} row")
    if len(mine) > 1:
        raise BoardError(f"more than one stream {stream} row")
    return header, mine[0]


def stream_branch(text: str, stream: str) -> str:
    """The branch named in the stream's row: the first backticked name of its `branch` cell, or BoardError."""
    header, row = stream_row(text, stream)
    column = [i for i, h in enumerate(header) if h.split()[:1] == ["branch"]]      # DEC-0187 first-word-whole
    if not column:
        raise BoardError("no branch column (a header cell whose first word is `branch`)")
    if column[0] >= len(row):
        raise BoardError(f"stream {stream} row has no branch cell")
    m = re.search(r"`([^`]*)`", row[column[0]])
    if m is None:
        raise BoardError(f"stream {stream} branch cell holds no backticked branch name")
    name = m.group(1)
    if not BRANCH_RE.fullmatch(name) or ".." in name or name.endswith((".lock", ".")) or "/." in name:
        raise BoardError(f"stream {stream} branch cell: the first backticked text is not a branch name")
    return name
