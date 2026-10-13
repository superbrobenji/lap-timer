#!/usr/bin/env python3
"""Venue-announce check (#98 bench defect, 2026-10-07).

The ui learns the current venue ONLY from EV_VENUE_FOUND -- handle_venue_found()
(components/app/ui/ui.c) is the single writer of s_venue_id, which gates the §20.7 Layout menu
item, the venue name and the §20.6 VENUE one-shot. core/lap.h's lap_set_venue() and
lap_import_rtc() carry no event buffer, so they CANNOT announce: every caller must. Inside the
engine both do (lap.c's scan_for_venue()/finalize_create()). Outside it, pipeline.c's
CFG_GPS_SIM boot venue (pipeline_init()) and its §15.3 RTC resume (on_fix_try_resume()) did not --
the ui's s_venue_id stayed 0, trk_get(0) is always NULL (trk_validate_venue() rejects id 0), and
`Layout:` was a dead item that still cleared s_best on every press.

Rule: in app/ and main/, every lap_set_venue() / lap_import_rtc() call must be accompanied, inside
the SAME function body, by an actual CALL to ui_post_venue(...) -- not merely a mention of
EV_VENUE_FOUND. Deliberately conservative: one call per function satisfies it, so a loop over
several venues is not double-counted, and a call in a HELPER invoked from the same function is not
detected -- announce at the set site, next to it, the way lap.c does.

Fix round 1 (review Minor 2/3, #98): the first cut's ANNOUNCE_RE also matched the bare token
EV_VENUE_FOUND anywhere in the function body, so a dispatcher that merely NAMES the code (e.g.
`case EV_VENUE_FOUND:` in engine_cb(), pipeline.c) satisfied the rule with no post at all --
exactly the bypass this linter exists to prevent. It also carried a trailing `\b` across the whole
alternation, which false-positived on an ordinary wrapped call (`ui_post_venue(\n    id, 0)` or
`ui_post_venue( id, 0 )`): the character after `(` is whitespace, not a word character, so no
boundary exists there and the match silently failed. ANNOUNCE_RE now requires only the call token
`ui_post_venue(` (optional whitespace before the paren, no trailing boundary assertion) -- a real
call, wrapped or not, always matches; a bare mention of the event code never does. Comments and
string literals are already blanked by strip_comments_and_literals() before either regex runs, so
neither regex ever sees inside one.

Final review M4: every failure mode probed above fails LOUD (more violations, or a new one) except
one -- renaming the acquisition API itself (lap_set_venue -> anything else, lap_import_rtc ->
anything else) makes ACQUIRE_RE match nothing anywhere in scope, so the whole rule goes silently
vacuous: 0 violations, rc 0, indistinguishable from a tree where every call site announces
correctly. main() now also counts how many ACQUIRE_RE matches landed inside a real function body
(the same test check_file() already does per-match to decide whether it is a violation) and fails
-- regardless of --fail-on-violation -- the moment that total is 0: a floor of "the rule must have
found something to check" that costs nothing on a tree which always has >= 2 such call sites. Not
lap.c's -- is_exempt() drops components/core/lapengine/ before check_file() ever runs, so
scan_for_venue()/finalize_create() contribute zero; the two that actually keep the floor non-zero
are pipeline.c's app-layer acquisitions, lap_import_rtc() (pipeline.c:505) and lap_set_venue()
(pipeline.c:1139). This floor is the only thing that catches this rename.

Fix round 2 (PR #101 review M5, 2026-10-13): "a call to ui_post_venue() anywhere in the same
function body" was still too loose -- it proved the announce happened somewhere, not that it was
ON the acquisition path. Three bypasses it silently accepted: (1) the announce in a sibling
branch of an if/else, never reached on the path that actually calls the acquisition; (2) an
announce that runs BEFORE the acquisition, so it posts whatever venue was previously current, not
the one just acquired; (3) an announce of a different venue (a literal id, or any identifier not
traceable to the acquisition's own arguments) sitting right next to a correct acquisition. The
rule now requires the ui_post_venue() call to be (a) inside the acquisition's own innermost block
(so a sibling branch cannot satisfy it), (b) textually AFTER the acquisition within that block,
and (c) passed a first argument that shares an identifier with the acquisition's own arguments
(past the engine handle) -- so a hard-coded or unrelated venue id no longer satisfies it. One
bypass remains open BY DESIGN, unchanged from fix round 1's "Deliberately conservative" note
above: an announce issued from a HELPER the acquisition calls, rather than inline on the
acquisition's own path, is still not detected -- extending this would require call-graph
analysis this linter does not do, and no acquisition in the current tree needs it.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from power_of_10 import (  # reuse the one C scanner in the tree
    LineIndex,
    find_top_level_functions,
    strip_comments_and_literals,
)

# Fix round 1 (review Minor 8, #98): widened to match power_of_10.py's own DEFAULT_PATHS exactly
# (components/core, components/app, components/drivers, main) -- components/core/lapengine/ is
# still exempt below (that is where the contract is correctly implemented), but a future blind
# venue-set path elsewhere in core/ or in drivers/ is now in scope too, at no cost (verified: 0
# violations with the widened scan on this tree).
DEFAULT_PATHS = ["components/core", "components/app", "components/drivers", "main"]
ACQUIRE_RE = re.compile(r"\b(lap_set_venue|lap_import_rtc)\s*\(")
ANNOUNCE_RE = re.compile(r"\bui_post_venue\s*\(")
SCANNED = (".c",)

_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# tokens that carry no venue identity, so sharing one proves nothing (M5)
_NOISE = {"sizeof", "NULL", "true", "false", "const", "void", "int", "uint16_t", "uint8_t",
          "int64_t", "s_lap", "L", "D"}


def _innermost_block(stripped, pos):
    """(open, close) of the innermost brace pair containing `pos`, or None."""
    stack = []
    best = None
    for i, ch in enumerate(stripped):
        if ch == "{":
            stack.append(i)
        elif ch == "}":
            if not stack:
                continue
            o = stack.pop()
            if o < pos < i and (best is None or o > best[0]):
                best = (o, i)
            if i > pos and not stack:
                break
    return best


def _args_text(stripped, open_paren_idx):
    """(whole argument list, first argument) of the call whose '(' is at open_paren_idx."""
    depth = 0
    for i in range(open_paren_idx, len(stripped)):
        if stripped[i] == "(":
            depth += 1
        elif stripped[i] == ")":
            depth -= 1
            if depth == 0:
                whole = stripped[open_paren_idx + 1:i]
                return whole, (whole.split(",")[0] if "," in whole else whole)
    return "", ""


def _tokens(text):
    return {t for t in _IDENT_RE.findall(text)} - _NOISE


# core/lapengine (lap.c) is the engine's own source: scan_for_venue()/finalize_create() announce
# correctly there (lap.c:871/:924) and are the two call sites this rule must NOT flag -- the rule
# targets the app layer's direct venue sets, not the engine that defines the contract.
EXEMPT_PATH_FRAGMENTS = (
    "components/core/lapengine/",
)


def is_exempt(relpath):
    rp = relpath.replace(os.sep, "/")
    for frag in EXEMPT_PATH_FRAGMENTS:
        if frag in rp:
            return True
    return False


def check_file(path, relpath):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        stripped = strip_comments_and_literals(fh.read())
    lidx = LineIndex(stripped)
    funcs = find_top_level_functions(stripped)
    findings = []
    sites = 0
    for m in ACQUIRE_RE.finditer(stripped):
        body = next(((name, o, c) for name, _ns, o, c, _po, _pc in funcs
                     if o < m.start() < c), None)
        if body is None:
            # M5: distinguish a prototype (brace depth 0) from an acquisition inside a construct
            # find_top_level_functions() did not recognise, which used to be skipped silently.
            if stripped.count("{", 0, m.start()) == stripped.count("}", 0, m.start()):
                continue                  # depth 0 -> a declaration, not a call
            findings.append("%s:%d: %s() is called from a context this linter cannot parse "
                            "(no enclosing top-level function) -- the announce rule cannot be "
                            "checked here; restructure or extend find_top_level_functions()" %
                            (relpath, lidx.line_of(m.start()), m.group(1)))
            continue
        sites += 1                        # M4 (final review): a real acquisition call site
        name, open_idx, close_idx = body
        # M5 (PR #101 review): the announce must be ON the acquisition path, not merely somewhere
        # in the function -- so it must sit in the acquisition's own innermost block, AFTER it, and
        # name the venue the acquisition named (a token shared with the acquisition's arguments
        # past the engine handle). A sibling branch's announce, an announce before the set, and an
        # announce of a different/literal venue id all fail.
        blk = _innermost_block(stripped, m.start()) or (open_idx, close_idx)
        acq_args, _first = _args_text(stripped, m.end() - 1)
        acq_tokens = _tokens(acq_args.split(",", 1)[1] if "," in acq_args else acq_args)
        ok = False
        for a in ANNOUNCE_RE.finditer(stripped, m.end(), blk[1]):
            _whole, first = _args_text(stripped, a.end() - 1)
            if _tokens(first) & acq_tokens:
                ok = True
                break
        if ok:
            continue
        findings.append("%s:%d: %s() in %s() acquires a venue with no ui_post_venue() call on "
                        "that path (same block, after the set, naming the same venue) -- see "
                        "tools/lint/venue_announce.py" %
                        (relpath, lidx.line_of(m.start()), m.group(1), name))
    return findings, sites


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--paths", nargs="+", default=DEFAULT_PATHS)
    ap.add_argument("--fail-on-violation", action="store_true")
    args = ap.parse_args(argv)
    base = os.getcwd()
    findings = []
    total_sites = 0
    for root in args.paths:
        # M5 (TDD fixture run): a --paths entry may itself be a single file (e.g. a fixture),
        # not only a directory to walk -- os.walk() on a file silently yields nothing, which
        # used to make such an entry a silent no-op. Mirrors power_of_10.py's discover_files().
        if os.path.isfile(root):
            candidates = [root]
        else:
            candidates = []
            for dirpath, dirnames, filenames in os.walk(root):
                dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "build"]
                for fn in filenames:
                    if fn.endswith(SCANNED):
                        candidates.append(os.path.join(dirpath, fn))
        for p in candidates:
            relpath = os.path.relpath(p, base)
            if is_exempt(relpath):
                continue
            file_findings, file_sites = check_file(p, relpath)
            findings += file_findings
            total_sites += file_sites
    for f in sorted(findings):
        print(f)
    print("venue-announce: %d violation(s)" % len(findings))
    # M4 (final review): a rename of the acquisition API itself (lap_set_venue/lap_import_rtc)
    # makes ACQUIRE_RE match nothing anywhere in scope -- 0 violations, rc 0, silently vacuous.
    # Fail unconditionally (not gated on --fail-on-violation: this is a self-check that the rule
    # found anything to examine at all, not a lint violation) the moment the real tree -- which
    # always has >= 2 acquisition call sites -- reports none.
    if total_sites == 0:
        print("venue-announce: 0 lap_set_venue()/lap_import_rtc() call sites found anywhere in "
              "scope -- has the acquisition API been renamed? update ACQUIRE_RE "
              "(tools/lint/venue_announce.py)")
        return 1
    return 1 if (findings and args.fail_on_violation) else 0


if __name__ == "__main__":
    sys.exit(main())
