#!/usr/bin/env python3
"""power_of_10.py -- custom Power of 10 (Holzmann/JPL) linter for the lap-timer firmware.

Design authority: docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md (§1/§3/§4).
Plan: docs/superpowers/plans/2026-09-17-plan-4.5-power-of-10.md, Session 4.5.1 Task 1.

Scans the in-scope on-device C (default: components/core components/app components/drivers
main) for the project-specific gates that generic analysers (clang-tidy/cppcheck) do not cover
well:

  RULE-4  function length > 60 code lines (blank/comment-only/brace-only lines excluded).
  RULE-5  per-component (per top-level scanned dir) average assertions/function < 2, counting
          CORE_ASSERT_RET/CORE_ASSERT_VOID/LT_ASSERT_RET/LT_ASSERT_VOID invocations.
  RULE-3  any malloc/calloc/realloc/free/strdup/aligned_alloc call (no dynamic memory after init).
  RULE-8  '##' token-paste, or '#' stringisation-as-logic, in shipped code.
  RULE-9  (only with --enforce-fnptr) a function-pointer typedef/param/variable whose
          file:symbol is not listed in the deviation register. INERT by default -- the design
          (§3 rule 9) has this gate land last, in session 4.5.5; until then existing function
          pointers are tolerated even with --enforce-fnptr unless newly introduced (this script
          does not track "new" vs "existing"; --enforce-fnptr simply is not passed before 4.5.5).

Pure Python 3 standard library only, no third-party dependencies.

Default mode is REPORT-ONLY: findings are printed and the process always exits 0. Pass
--fail-on-violation to exit 1 when an unregistered RULE-4 or RULE-9 finding exists (NOT used by
CI/the task runner until the retrofit is complete -- see plan Session 4.5.6).
"""
import argparse
import json
import os
import re
import sys

# --------------------------------------------------------------------------------------------
# Scope (design §1): default in-scope roots, and paths that are exempt even inside them.
# --------------------------------------------------------------------------------------------

DEFAULT_PATHS = ["components/core", "components/app", "components/drivers", "main"]

# Directory *names* pruned wherever they occur during the walk.
EXCLUDE_DIR_NAMES = {"managed_components", "build", ".git", ".venv", "__pycache__"}

# Path *fragments* (POSIX-style, matched against the path relative to the repo root) that are
# exempt per design §1: vendored Unity, the vendored jsmn parser, and (defensively) anything
# that looks like a generated build artifact directory.
EXEMPT_PATH_FRAGMENTS = (
    "/test/unity/",
    "test/unity/",
)

SCANNED_EXTENSIONS = (".c", ".h")


def is_exempt_jsmn(relpath):
    """components/core/util/jsmn* (design §1) -- the vendored JSON tokenizer."""
    d, base = os.path.split(relpath)
    return d.replace(os.sep, "/").endswith("core/util") and base.startswith("jsmn")


def is_exempt(relpath, idf_path_real):
    rp = relpath.replace(os.sep, "/")
    for frag in EXEMPT_PATH_FRAGMENTS:
        if frag in ("/" + rp) or rp.startswith(frag):
            return True
    if is_exempt_jsmn(rp):
        return True
    if idf_path_real:
        real = os.path.realpath(relpath)
        if real == idf_path_real or real.startswith(idf_path_real + os.sep):
            return True
    return False


def discover_files(root_paths, base_dir):
    """Returns a list of (component, filepath) pairs, `component` being the --paths entry the
    file was found under (design §3 rule 5: "per component (per top-level dir)")."""
    idf_path = os.environ.get("IDF_PATH")
    idf_path_real = os.path.realpath(idf_path) if idf_path else None

    found = []
    for root in root_paths:
        component = root.rstrip("/")
        if os.path.isfile(root):
            candidates = [root]
        else:
            candidates = []
            for dirpath, dirnames, filenames in os.walk(root):
                dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIR_NAMES and not d.startswith(".")]
                for fn in filenames:
                    if fn.endswith(SCANNED_EXTENSIONS):
                        candidates.append(os.path.join(dirpath, fn))
        for path in candidates:
            relpath = os.path.relpath(path, base_dir)
            if is_exempt(relpath, idf_path_real):
                continue
            found.append((component, path, relpath))
    return found


# --------------------------------------------------------------------------------------------
# A light C "tokenizer": strip // and /* */ comments and "..."/'...' literals so braces,
# semicolons and ## inside them cannot confuse the scanner, while preserving every newline (and
# every other character position) so line numbers computed on the stripped text stay correct.
# --------------------------------------------------------------------------------------------


def strip_comments_and_literals(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                out.append(" ")
                i += 2
            continue
        if c == '"':
            out.append(" ")
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append("\n" if text[i + 1] == "\n" else " ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        if c == "'":
            out.append(" ")
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append("\n" if text[i + 1] == "\n" else " ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


class LineIndex:
    """Maps a character offset in a text to a 1-based line number."""

    def __init__(self, text):
        self._starts = [0]
        for i, ch in enumerate(text):
            if ch == "\n":
                self._starts.append(i + 1)

    def line_of(self, pos):
        import bisect

        return bisect.bisect_right(self._starts, pos)


# --------------------------------------------------------------------------------------------
# Top-level function definitions: a '{' at brace-depth 0 immediately preceded (skipping
# whitespace) by ')' opens a function body -- the closing '}' is found by simple brace matching
# via a stack, which is correct regardless of what nests inside (control blocks, struct/array
# initializers, nested compound literals, ...).
# --------------------------------------------------------------------------------------------

_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*$")
_RESERVED = {"if", "for", "while", "switch", "do", "else", "return"}


def _name_before_paren(stripped, close_paren_idx):
    """Given the index of a ')' that closes a parameter list, walk back over the matching '(' and
    return (name, name_start_idx, open_paren_idx), or (None, None, None)."""
    depth = 1
    k = close_paren_idx - 1
    n = len(stripped)
    while k >= 0 and depth > 0:
        if stripped[k] == ")":
            depth += 1
        elif stripped[k] == "(":
            depth -= 1
        k -= 1
    # k now points just before the matching '('; that '(' itself is at k + 1.
    open_paren_idx = k + 1
    m = k
    while m >= 0 and stripped[m] in " \t\r\n":
        m -= 1
    end_name = m + 1
    while m >= 0 and (stripped[m].isalnum() or stripped[m] == "_"):
        m -= 1
    start_name = m + 1
    if end_name <= start_name:
        return None, None, None
    name = stripped[start_name:end_name]
    if not _IDENT_RE.match(name) or name in _RESERVED:
        return None, None, None
    return name, start_name, open_paren_idx


def find_top_level_functions(stripped):
    """Returns a list of (name, name_start_idx, open_brace_idx, close_brace_idx, param_open_idx,
    param_close_idx) -- param_open/close is the function's own '(' ... ')' parameter-list span,
    used so a function-pointer PARAMETER (rule 9) can be attributed to its enclosing function."""
    funcs = []
    stack = []  # entries: dict(is_func, name, name_start, open, param_open, param_close)
    depth = 0
    n = len(stripped)
    i = 0
    while i < n:
        c = stripped[i]
        if c == "{":
            is_func = False
            name = None
            name_start = None
            param_open = None
            param_close = None
            if depth == 0:
                j = i - 1
                while j >= 0 and stripped[j] in " \t\r\n":
                    j -= 1
                if j >= 0 and stripped[j] == ")":
                    name, name_start, param_open = _name_before_paren(stripped, j)
                    if name is not None:
                        is_func = True
                        param_close = j
            stack.append({"is_func": is_func, "name": name, "name_start": name_start, "open": i,
                          "param_open": param_open, "param_close": param_close})
            depth += 1
        elif c == "}":
            depth -= 1
            if stack:
                entry = stack.pop()
                if entry["is_func"]:
                    funcs.append((entry["name"], entry["name_start"], entry["open"], i,
                                  entry["param_open"], entry["param_close"]))
        i += 1
    funcs.sort(key=lambda f: f[2])
    return funcs


def _enclosing_function_name(funcs, pos):
    """If `pos` falls within some function's own parameter-list span (param_open..param_close),
    return that function's name; else None (a typedef or a file/variable-scope declaration)."""
    for name, _name_start, _open, _close, param_open, param_close in funcs:
        if param_open is not None and param_open < pos < param_close:
            return name
    return None


_BRACE_ONLY_RE = re.compile(r"^[{};\s]*$")


def count_code_lines(stripped_lines, line_start, line_end):
    """Code lines strictly within [line_start, line_end] (1-based, inclusive) of stripped_lines
    (0-based list), excluding blank, comment-only (already blanked), and brace-only lines."""
    count = 0
    for ln in range(line_start, line_end + 1):
        text = stripped_lines[ln - 1]
        stripped_text = text.strip()
        if stripped_text == "":
            continue
        if _BRACE_ONLY_RE.match(stripped_text):
            continue
        count += 1
    return count


# --------------------------------------------------------------------------------------------
# Rule checks
# --------------------------------------------------------------------------------------------

RULE3_RE = re.compile(r"\b(malloc|calloc|realloc|free|strdup|aligned_alloc)\s*\(")
ASSERT_RE = re.compile(r"\b(CORE_ASSERT_RET|CORE_ASSERT_VOID|LT_ASSERT_RET|LT_ASSERT_VOID)\s*\(")
FNPTR_RE = re.compile(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(")


def check_rule3_heap(stripped, relpath, lidx, findings):
    for m in RULE3_RE.finditer(stripped):
        line = lidx.line_of(m.start())
        findings.append({"file": relpath, "line": line, "rule": "RULE-3",
                          "message": "dynamic memory call '%s(' (rule 3: no heap after init)" % m.group(1)})


def check_rule8_preprocessor(stripped, relpath, lidx, findings):
    for m in re.finditer(r"##", stripped):
        line = lidx.line_of(m.start())
        findings.append({"file": relpath, "line": line, "rule": "RULE-8",
                          "message": "'##' token-paste in shipped code (rule 8: restricted preprocessor)"})
    for m in re.finditer(r"#", stripped):
        pos = m.start()
        if pos > 0 and stripped[pos - 1] == "#":
            continue
        if pos + 1 < len(stripped) and stripped[pos + 1] == "#":
            continue
        line_start = stripped.rfind("\n", 0, pos) + 1
        prefix = stripped[line_start:pos]
        if prefix.strip() == "":
            continue  # a directive ('#define'/'#include'/'#if'/...), not stringisation
        line = lidx.line_of(pos)
        findings.append({"file": relpath, "line": line, "rule": "RULE-8",
                          "message": "'#' stringisation-as-logic in shipped code (rule 8: restricted preprocessor)"})


def check_rule9_fnptr(stripped, relpath, lidx, findings, register, funcs):
    """A function-pointer typedef/parameter/variable (design §3 rule 9). For a PARAMETER, the
    site is attributed to (and registered under) its enclosing function's name -- e.g.
    'board.c:board_buttons_enable_isr' -- matching how the register names these sites; a typedef
    or a variable declaration is attributed to its own declared identifier."""
    for m in FNPTR_RE.finditer(stripped):
        pos = m.start()
        enclosing = _enclosing_function_name(funcs, pos)
        symbol = enclosing if enclosing is not None else m.group(1)
        line = lidx.line_of(pos)
        if register.is_registered(9, relpath, symbol):
            continue
        findings.append({"file": relpath, "line": line, "rule": "RULE-9", "symbol": symbol,
                          "message": "unregistered function pointer '%s' (rule 9: no function pointers outside the register)" % symbol})


def check_rule4_length(stripped, relpath, lidx, register, funcs, max_lines=60):
    """Returns (findings_list, function_count)."""
    findings = []
    stripped_lines = stripped.split("\n")
    for name, name_start, open_idx, close_idx, _param_open, _param_close in funcs:
        line_open = lidx.line_of(open_idx)
        line_close = lidx.line_of(close_idx)
        n_lines = count_code_lines(stripped_lines, line_open, line_close)
        if n_lines > max_lines:
            name_line = lidx.line_of(name_start) if name_start is not None else line_open
            if register.is_registered(4, relpath, name):
                continue
            findings.append({"file": relpath, "line": name_line, "rule": "RULE-4", "symbol": name,
                              "message": "function '%s' is %d code lines (> %d)" % (name, n_lines, max_lines)})
    return findings, len(funcs)


# --------------------------------------------------------------------------------------------
# The deviation register (design §4): a markdown table
#   | id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
# --------------------------------------------------------------------------------------------


class Register:
    def __init__(self):
        self._sites = set()  # {(rule_num:int, file:str, symbol:str)}
        self.rows = []

    @staticmethod
    def _rule_num(cell):
        m = re.search(r"\d+", cell)
        return int(m.group()) if m else None

    def load(self, path):
        if not path or not os.path.isfile(path):
            return
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        for raw in lines:
            line = raw.strip()
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < 3:
                continue
            if cells[0].lower() in ("id", ""):
                continue
            if set(cells[0]) <= {"-", ":"}:
                continue  # markdown header separator row
            rule_num = self._rule_num(cells[1])
            site = cells[2]
            if rule_num is None or ":" not in site:
                continue
            file_part, symbol_part = site.split(":", 1)
            self._sites.add((rule_num, file_part.strip(), symbol_part.strip()))
            self.rows.append(cells)

    def is_registered(self, rule_num, relpath, symbol):
        relpath = relpath.replace(os.sep, "/")
        return (rule_num, relpath, symbol) in self._sites


# --------------------------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------------------------


def run(paths, register_path, enforce_fnptr, base_dir):
    register = Register()
    register.load(register_path)

    files = discover_files(paths, base_dir)

    all_findings = []
    per_component = {}  # component -> {"functions":N, "assertions":N, "files":N,
    #                                    "rule3":N, "rule4":N, "rule8":N, "rule9":N}

    for component, filepath, relpath in files:
        comp = per_component.setdefault(component, {
            "functions": 0, "assertions": 0, "files": 0,
            "rule3": 0, "rule4": 0, "rule8": 0, "rule9": 0,
        })
        comp["files"] += 1
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        stripped = strip_comments_and_literals(text)
        lidx = LineIndex(stripped)
        funcs = find_top_level_functions(stripped)

        rule4_findings, n_funcs = check_rule4_length(stripped, relpath, lidx, register, funcs)
        comp["functions"] += n_funcs
        comp["rule4"] += len(rule4_findings)
        all_findings.extend(rule4_findings)

        comp["assertions"] += len(ASSERT_RE.findall(stripped))

        rule3_findings = []
        check_rule3_heap(stripped, relpath, lidx, rule3_findings)
        comp["rule3"] += len(rule3_findings)
        all_findings.extend(rule3_findings)

        rule8_findings = []
        check_rule8_preprocessor(stripped, relpath, lidx, rule8_findings)
        comp["rule8"] += len(rule8_findings)
        all_findings.extend(rule8_findings)

        if enforce_fnptr:
            rule9_findings = []
            check_rule9_fnptr(stripped, relpath, lidx, rule9_findings, register, funcs)
            comp["rule9"] += len(rule9_findings)
            all_findings.extend(rule9_findings)

    all_findings.sort(key=lambda f: (f["file"], f["line"], f["rule"]))

    summary = {}
    for component, comp in per_component.items():
        avg = (comp["assertions"] / comp["functions"]) if comp["functions"] else 0.0
        summary[component] = dict(comp)
        summary[component]["avg_assertions_per_function"] = round(avg, 3)
        summary[component]["rule5_below_2"] = comp["functions"] > 0 and avg < 2.0

    return all_findings, summary, register


def render_text(findings, summary, enforce_fnptr):
    lines = []
    for f in findings:
        lines.append("%s:%d: %s: %s" % (f["file"], f["line"], f["rule"], f["message"]))

    lines.append("")
    lines.append("-- power_of_10.py summary (report-only) --")
    header = "%-28s %9s %11s %8s %10s %10s %10s" % (
        "component", "functions", "assertions", "avg/fn", "rule4>60", "rule3-heap", "rule8-pp")
    lines.append(header)
    for component in sorted(summary):
        s = summary[component]
        flag = " *" if s["rule5_below_2"] else ""
        lines.append("%-28s %9d %11d %8.2f%s %10d %10d %10d" % (
            component, s["functions"], s["assertions"], s["avg_assertions_per_function"], flag,
            s["rule4"], s["rule3"], s["rule8"]))
    lines.append("(* = RULE-5: component average assertions/function < 2.0)")

    total_rule3 = sum(s["rule3"] for s in summary.values())
    total_rule4 = sum(s["rule4"] for s in summary.values())
    total_rule8 = sum(s["rule8"] for s in summary.values())
    total_rule9 = sum(s["rule9"] for s in summary.values())
    lines.append("")
    lines.append("totals: rule3(heap)=%d rule4(over-60)=%d rule8(preprocessor)=%d rule9(fnptr)=%s" % (
        total_rule3, total_rule4, total_rule8, (str(total_rule9) if enforce_fnptr else "inert (pass --enforce-fnptr)")))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Power of 10 project-specific static linter (report-only by default).")
    ap.add_argument("--paths", nargs="+", default=DEFAULT_PATHS,
                     help="Roots to scan (default: %s)" % " ".join(DEFAULT_PATHS))
    ap.add_argument("--register", default="docs/power-of-10-deviations.md",
                     help="Deviation register path (default: docs/power-of-10-deviations.md)")
    ap.add_argument("--enforce-fnptr", action="store_true",
                     help="Enable RULE-9 (no unregistered function pointer). INERT unless passed; "
                          "not used before plan Session 4.5.5.")
    ap.add_argument("--fail-on-violation", action="store_true",
                     help="Exit 1 if any unregistered RULE-4/RULE-9 finding exists. NOT used before "
                          "plan Session 4.5.6 (report-only rollout).")
    ap.add_argument("--json", action="store_true", help="Emit JSON instead of text.")
    args = ap.parse_args(argv)

    base_dir = os.getcwd()
    findings, summary, register = run(args.paths, args.register, args.enforce_fnptr, base_dir)

    if args.json:
        print(json.dumps({
            "findings": findings,
            "summary": summary,
            "register_rows": len(register.rows),
            "enforce_fnptr": args.enforce_fnptr,
        }, indent=2, sort_keys=True))
    else:
        print(render_text(findings, summary, args.enforce_fnptr))
        print("\nregister: %d row(s) loaded from %s" % (len(register.rows), args.register))
        print("mode: %s" % ("fail-on-violation" if args.fail_on_violation else "report-only (always exit 0 unless --fail-on-violation)"))

    if args.fail_on_violation:
        blocking = [f for f in findings if f["rule"] in ("RULE-4", "RULE-9")]
        if blocking:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
