#!/usr/bin/env python3
"""spec_api_check.py -- prove spec §5.2's API blocks still match components/core/include/core/*.h.

The §5.2 blocks are prose-maintained copies of the real headers, so they rot silently (audit
2026-10-13 §3.3 item 8: three declarations in them exist nowhere in the tree, and one exported
function is missing). This script extracts each fenced `c` block that follows a
`#### `core/<name>.h`` heading and compares its DECLARATIONS against the header's:

  MISSING    the header exports a prototype the spec block does not list
  STALE      the spec block lists a prototype/typedef/macro the header does not have
  SIGNATURE  both have the name, but the normalised declaration text differs

Only function prototypes, function-pointer typedefs and macro names are compared; struct/enum
bodies and object typedefs are ignored (the blocks are API lists, not whole headers). `--emit`
prints the list a block must contain, in header order, for pasting into the spec.

Pure Python 3 standard library. Run from the repo root.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lint"))
from power_of_10 import strip_comments_and_literals  # the one C scanner in the tree

SPEC = "docs/superpowers/specs/2026-09-14-lap-timer-design.md"
HDR_DIR = "components/core/include/core"
HEADERS = ["core", "tb", "geo", "types", "event", "fus", "lap", "drag", "ses", "exp", "json",
           "cfg", "trk"]

_HEADING_RE = re.compile(r"^#### `core/([a-z_]+)\.h`", re.MULTILINE)
_PROTO_RE = re.compile(r"^(.*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*;$", re.DOTALL)
_FNPTR_TYPEDEF_RE = re.compile(r"^typedef\s+.*\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(")
_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)")


def _decls(text):
    """Every depth-0 `;`-terminated declaration, whitespace-normalised; preprocessor lines are
    removed and their macro names returned separately."""
    macros = set()
    kept = []
    for line in text.split("\n"):
        m = _DEFINE_RE.match(line)
        if m:
            macros.add(m.group(1))
        kept.append("" if line.lstrip().startswith("#") else line)
    body = strip_comments_and_literals("\n".join(kept))
    out = []
    depth = 0
    start = 0
    for i, ch in enumerate(body):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif ch == ";" and depth == 0:
            out.append(body[start:i + 1])
            start = i + 1
    return [" ".join(d.split()) for d in out if d.strip()], macros


def api_of(text):
    """(prototypes {name: normalised decl}, fn-ptr typedef names, macro names)."""
    decls, macros = _decls(text)
    protos, typedefs = {}, set()
    for d in decls:
        t = _FNPTR_TYPEDEF_RE.match(d)
        if t:
            typedefs.add(t.group(1))
            continue
        if d.startswith("typedef") or "{" in d or "(" not in d:
            continue
        m = _PROTO_RE.match(d)
        if m and m.group(1).strip():          # a return type must precede the name
            protos[m.group(2)] = d
    return protos, typedefs, macros


def spec_blocks(spec_text):
    """{header stem: fenced block text} for each `#### `core/<x>.h`` heading."""
    blocks = {}
    for m in _HEADING_RE.finditer(spec_text):
        rest = spec_text[m.end():]
        open_f = rest.find("```c")
        if open_f < 0:
            continue
        close_f = rest.find("```", open_f + 4)
        blocks[m.group(1)] = rest[open_f + 4:close_f]
    return blocks


def emit(stem):
    with open(os.path.join(HDR_DIR, stem + ".h"), encoding="utf-8") as f:
        text = f.read()
    protos, typedefs, _macros = api_of(text)
    for name in sorted(typedefs):
        for line in text.split("\n"):
            if re.search(r"\(\s*\*\s*%s\s*\)" % re.escape(name), line):
                print(" ".join(line.split()))
                break
    order = {}
    for i, line in enumerate(text.split("\n")):
        for name in protos:
            if name not in order and re.search(r"(?<![A-Za-z0-9_])%s\s*\(" % re.escape(name), line):
                order[name] = i
    for name in sorted(protos, key=lambda n: order.get(n, 1 << 30)):
        print(protos[name])
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="spec §5.2 API drift check")
    ap.add_argument("--spec", default=SPEC)
    ap.add_argument("--emit", metavar="core/<x>.h",
                    help="print the declaration list this header's §5.2 block must contain")
    args = ap.parse_args(argv)
    if args.emit:
        return emit(os.path.basename(args.emit).replace(".h", ""))
    with open(args.spec, encoding="utf-8") as f:
        spec_text = f.read()
    blocks = spec_blocks(spec_text)
    findings = []
    for stem in HEADERS:
        if stem not in blocks:
            findings.append("spec §5.2: no `core/%s.h` block found in %s" % (stem, args.spec))
            continue
        with open(os.path.join(HDR_DIR, stem + ".h"), encoding="utf-8") as f:
            hp, ht, hm = api_of(f.read())
        sp, st, sm = api_of(blocks[stem])
        for name in sorted(hp):
            if name not in sp:
                findings.append("core/%s.h: MISSING   %s" % (stem, hp[name]))
            elif hp[name] != sp[name]:
                findings.append("core/%s.h: SIGNATURE %s | spec has: %s" % (stem, hp[name], sp[name]))
        for name in sorted(sp):
            if name not in hp:
                findings.append("core/%s.h: STALE     %s" % (stem, sp[name]))
        for name in sorted(st - ht):
            findings.append("core/%s.h: STALE     typedef %s" % (stem, name))
        for name in sorted(sm - hm):
            findings.append("core/%s.h: STALE     #define %s" % (stem, name))
    for f in findings:
        print(f)
    print("spec_api_check: %d drift(s) across %d §5.2 blocks" % (len(findings), len(HEADERS)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
