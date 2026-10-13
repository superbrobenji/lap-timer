# F4 — Process, tooling and docs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the project's enforcement claims true (linter sees what CI says it gates, branch protection requires every check, release signing refuses what the spec says it refuses) and make every process/doc/spec claim the 2026-10-13 audit falsified either true or struck.

**Architecture:** Six independent file groups land in parallel (linter+tests, venue linter, spec-API checker, CI workflows, release/generator/selftest tooling, record-decoder tests, docs). Two serial tasks close the wave: the spec reconciliation runs LAST (after F1a/F1b/F2a/F2b/F3 merge, so the spec describes the merged tree), and the linter's blocking flip runs after the plans that own the offending files have merged. No firmware C in this plan — the only `.c` this plan writes are linter fixtures under `tools/lint/fixtures/` and host-test cases in `test/test_trk.c`.

**Tech Stack:** Python 3 stdlib (linters, generators, unit tests via `unittest`), bash (`sign_release.sh` + its harness), GitHub Actions YAML, `gh` CLI, markdown.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (row **F4**; rulings R3, R12, R13, R14, R15, R18, R31; §4 gates; §5 filed-not-fixed). Audit: `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` (§2, §3, §5, §6 are F4's). Reviewer reports: `.superpowers/reviews/2026-10-13/{drivers-build-tools,core-io-ui,app-layer,pr-101}.md` (git-ignored).

## Global Constraints

- Branch `fix-process-docs`, worktree `fix-process-docs`, off `main` **after F0 (PR #101) merges**. Parallel with F1a/F1b/F3.
- **File ownership (hard boundary — nobody else touches these in the wave, and this plan touches nothing else):** `tools/lint/power_of_10.py`, `tools/lint/test_power_of_10.py` (new), `tools/lint/fixtures/**` (new), `tools/lint/venue_announce.py`, `tools/lint/test_venue_announce.py` (new), `tools/spec_api_check.py` (new), `tools/test_spec_api_check.py` (new), `tools/sign_release.sh`, `tools/test_sign_release.sh` (new), `tools/tracks/gen_tracks.py`, `tools/tracks/test_gen_tracks.py` (new), `.github/workflows/ci.yml`, `.github/workflows/firmware.yml`, `test_apps/core_selftest/main/CMakeLists.txt`, `test/test_trk.c` (**additions only**, record-decoder level), `README.md`, `docs/measurements.md`, `docs/superpowers/plans/2026-09-14-roadmap.md`, `docs/power-of-10-deviations.md`, `docs/power-of-10-checklist.md`, `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` §5, and **exactly these sections** of `docs/superpowers/specs/2026-09-14-lap-timer-design.md`: §4.2, §4.6, §4.8, §5.2, §11.4, §17.2 (the `HB_STALL_S` sentence only), §17.7, §17.8 (one sentence), §18.4, §19.1 (one sentence), §21.2, §21.3 (one sentence), §21.5, §21.6, §22.1 (one line), §22.3 (one paragraph), §23, Appendix A (3 rows). **Not F4's:** §6.4, §10.1, §10.3, §12.x, §17.1, §19.4, §19.5, §20.5, §20.6, §20.7 — other plans amend those; never edit them here.
- Python: standard library only, no third-party imports (`tools/requirements.txt` stays `pyserial`/`Pillow`/`esptool`).
- Every new Python tool runs from the repo root and is exercised by `python3 -m unittest` in the `tools-tests` CI job.
- Zero-warning rule (memory `feedback-zero-warnings`, P10 rule 10) applies to anything that compiles.
- Commit trailer on every commit (as `git log -2 --format=%B glyphs-drag-menu-closure` shows):
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_<this session>
  ```
- No flashing by subagents. No bench item is claimed as passed by this plan. (Task 8 **does** build
  firmware — it has to, to read the map file — but never flashes.)
- **Checkpoint protocol (`/clear`-safe).** Every task ends with a CHECKPOINT step: one ledger line
  appended to `.superpowers/sdd/2026-10-13-fix-F4/progress.md` (`Task N complete <sha> — <one-line
  summary>`), the branch pushed, and the resume prompt recorded verbatim:
  `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task N+1 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree <path>; branch fix-process-docs.`
  Tasks are grouped into **sessions of ≤ 4 tasks**; at a session end, stop for review before the
  next session's first task. Create the ledger before Task 1:
  `mkdir -p .superpowers/sdd/2026-10-13-fix-F4 && printf '# F4 — process, tooling, docs\n\nBranch fix-process-docs. Plan: docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md\n' > .superpowers/sdd/2026-10-13-fix-F4/progress.md`
  (`.superpowers/` is git-ignored — the ledger is session state, never a deliverable; everything
  that must survive goes into `docs/`.)

---

## Task order and sessions

Sessions are `/clear` boundaries: ≤ 4 tasks each, every task ending in a CHECKPOINT, and a stop for
review at the session end.

| Session | # | Task | Mode | Entry condition |
|---|---|---|---|---|
| **1 — tooling + CI** | 1 | Power-of-10 linter gaps (R12) | PARALLEL | branch off `main` with F0 merged |
| | 2 | Venue linter on the acquisition path (PR #101 M5) | PARALLEL | — |
| | 3 | `tools/spec_api_check.py` (R18 §5.2 method) | PARALLEL | — |
| | 4 | CI workflows (R13, R14, drv:M13) | PARALLEL | names from 1–3, 5 (steps are probe-guarded) |
| **2 — release tooling, tests, docs, DRAM** | 5 | `sign_release.sh` + `gen_tracks.py` + `core_selftest` CMake (R15, L24, L25) | PARALLEL | — |
| | 6 | `test/test_trk.c` record-decoder cases (PR #101 M6) | PARALLEL | — |
| | 7 | README + `docs/measurements.md` (R31) | PARALLEL | — |
| | 8 | BT/NimBLE off: ~56 KB of reserved DRAM (R33) | PARALLEL | — |
| **3 — spec, roadmap, flip, issues** | 9 | Spec reconciliation (R3, R14, R18) | SERIAL | F1a+F1b+F2a+F2b+F3 merged |
| | 10 | Roadmap rewritten as a state document (R31) | SERIAL | F2b merged; Task 9 done |
| | 11 | Linter blocking flip (R12 step 2) | SERIAL | F1a+F1b+F2a+F2b merged |
| | 12 | Open-issues audit, one verdict comment each (design §7b) | SERIAL | F0 merged (it closes #95–#99) |
| **4 — cleanup and close** | 13 | Repo cleanup sweep (R34, design §6) | SERIAL | Tasks 1–12 done |
| | 14 | Branch protection, filed issues, final wave gate | SERIAL | Task 13 done |

**Two-step linter rollout (R12).** Task 1 lands the tightened checks with the ten known
pre-existing sites in a `--report-only-sites` grace list, so `main` cannot go red for findings in
files this plan does not own; Task 11 — a separate task, executed only after F1a/F1b/F2a/**F2b**
have merged — deletes the grace entries their owners fixed and makes the remaining checks block
unconditionally. Nothing between those two tasks weakens a gate that was blocking before.

## Session 1 — tooling and CI (Tasks 1–4)

Four independent file groups; any order, or in parallel worktrees. Stop for review after Task 4.

### Task 1: Power-of-10 linter gaps (R12 / drv:LG-1..LG-7) — PARALLEL

**Files:**
- Modify: `tools/lint/power_of_10.py`
- Create: `tools/lint/test_power_of_10.py`, `tools/lint/fixtures/{lg1_heap_only,lg2_heap_apis,lg3_sinks,lg4_if0_asserts,lg5_oneliners,lg6_recursion,lg7_if0_brace}.c`, `tools/lint/fixtures/{register_none,register_sinks}.md`

**Interfaces:**
- Consumes: nothing.
- Produces: `power_of_10.py` CLI gains `--report-only-paths PFX...` (default `devcontroller/`), `--report-only-sites path:symbol:RULE,...`, `--report-only-rules RULE-x,...`; new rule ids `RULE-1` (recursion) and blocking `RULE-3`; new functions `blank_dead_regions(text)`, `count_statements(body)`, `effective_lines(stripped, stripped_lines, lidx, open_idx, close_idx)`, `check_rule1_recursion(...)`, `check_rule9_sinks(...)`, module constant `FNPTR_SINKS`. `run()` becomes `run(paths, register_path, enforce_fnptr, base_dir, report_only_paths=())`. Task 2 imports `LineIndex`, `find_top_level_functions`, `strip_comments_and_literals` from this module (unchanged signatures). Task 4 wires the CLI into CI.

- [ ] **Step 1: Write the failing fixtures and test**

Create `tools/lint/fixtures/lg1_heap_only.c`:
```c
/* fixture: a file whose ONLY finding is rule 3 must still fail --fail-on-violation (LG-1). */
#include <stdlib.h>

void g(void)
{
    void *p = malloc(8);
    free(p);
}
```

`tools/lint/fixtures/lg2_heap_apis.c`:
```c
/* fixture: rule 3 must see every heap API this codebase could reach (LG-2). */
#include <stdlib.h>

void f(void)
{
    void *a = heap_caps_malloc(16, 0);
    heap_caps_free(a);
    void *b = pvPortMalloc(16);
    vPortFree(b);
    char *c = strndup("x", 1);
    char *d = NULL;
    (void)asprintf(&d, "%d", 1);
    size_t keep = heap_caps_get_minimum_free_size(0) + heap_caps_get_free_size(0);
    (void)keep;
}
```

`tools/lint/fixtures/lg3_sinks.c`:
```c
/* fixture: rule 9 part 2 -- a function passed by value or stored (LG-3). */
static int cmpi(const void *a, const void *b) { (void)a; (void)b; return 0; }
static void pre_cb(void *t) { (void)t; }

struct cfgs { void (*pre_cb)(void *); };

void sort_it(int *v, unsigned n)
{
    qsort(v, n, sizeof v[0], cmpi);
    struct cfgs c = { .pre_cb = pre_cb };
    (void)c;
}

void forwarder(void (*cb)(void *))
{
    struct cfgs c = { .pre_cb = cb };
    (void)c;
}
```

`tools/lint/fixtures/lg4_if0_asserts.c`:
```c
/* fixture: assertions inside #if 0 do not count (LG-4); the #else branch does. */
int dead_asserts(int n)
{
#if 0
    CORE_ASSERT_RET(n > 0, 1, -1);
    CORE_ASSERT_RET(n < 99, 1, -1);
#endif
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}

int live_asserts(int n)
{
#if 0
    int unused = 0;
#else
    CORE_ASSERT_RET(n > 0, 1, -1);
    CORE_ASSERT_RET(n < 99, 1, -1);
#endif
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}
```

`tools/lint/fixtures/lg5_oneliners.c`:
```c
/* fixture: statements, not physical lines (LG-5). 22 statements on 8 physical lines. */
int packed(int n)
{
    int a = n; int b = n; int c = n;
    int d = n; int e = n; int f = n;
    a += b; b += c; c += d; d += e; e += f;
    a += b; b += c; c += d; d += e; e += f;
    a += b; b += c; c += d; d += e;
    for (int i = 0; i < 3; i++) { a += i; }
    return a + b + c + d + e + f;
}
```

`tools/lint/fixtures/lg6_recursion.c`:
```c
/* fixture: direct recursion (LG-6 / rule 1). */
int walk(int d)
{
    if (d <= 0) { return 0; }
    return walk(d - 1) + 1;
}

int iterative(int d)
{
    int t = 0;
    for (int i = 0; i < d; i++) { t += i; }
    return t;
}
```

`tools/lint/fixtures/lg7_if0_brace.c`:
```c
/* fixture: an unbalanced brace inside #if 0 must not hide later functions (LG-7). */
#if 0
void broken(void) {
#endif

int over_twenty(int n)
{
    int t = 0;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    t += n; t += n; t += n; t += n; t += n; t += n; t += n;
    return t;
}
```

`tools/lint/fixtures/register_none.md` (header + separator row only, so the linter loads an empty register):
```markdown
| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
```

`tools/lint/fixtures/register_sinks.md`:
```markdown
| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
| TD-1 | 9 | tools/lint/fixtures/lg3_sinks.c:cmpi | n/a (fixture) | n/a | n/a | fixture |
| TD-2 | 9 | tools/lint/fixtures/lg3_sinks.c:pre_cb | n/a (fixture) | n/a | n/a | fixture |
| TD-3 | 9 | tools/lint/fixtures/lg3_sinks.c:forwarder | n/a (fixture) | n/a | n/a | fixture |
```

`tools/lint/test_power_of_10.py`:
```python
#!/usr/bin/env python3
"""Unit tests for tools/lint/power_of_10.py -- one fixture per gap the 2026-10-13 audit proved
(drv:LG-1..LG-7). Run from the repo root: python3 -m unittest tools/lint/test_power_of_10.py
"""
import io
import contextlib
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import power_of_10 as p10

FIX = "tools/lint/fixtures"
NONE = FIX + "/register_none.md"
SINKS = FIX + "/register_sinks.md"


def run(args):
    """(rc, stdout) of one linter invocation, from the repo root."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = p10.main(args)
    return rc, buf.getvalue()


def findings(out):
    return [l for l in out.splitlines() if ": RULE-" in l]


class Rule3(unittest.TestCase):
    def test_lg1_heap_only_file_fails_the_build(self):
        rc, out = run(["--paths", FIX + "/lg1_heap_only.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertEqual(2, len(findings(out)), out)
        self.assertEqual(1, rc)

    def test_lg2_every_heap_api_is_seen_and_the_query_apis_are_not(self):
        rc, out = run(["--paths", FIX + "/lg2_heap_apis.c", "--register", NONE,
                       "--fail-on-violation"])
        got = [l.split("'")[1] for l in findings(out)]
        self.assertEqual(["heap_caps_malloc(", "heap_caps_free(", "pvPortMalloc(", "vPortFree(",
                          "strndup(", "asprintf("], got)
        self.assertNotIn("free_size", out)
        self.assertEqual(1, rc)


class Rule9Sinks(unittest.TestCase):
    def test_lg3_sink_and_stored_value_are_findings(self):
        rc, out = run(["--paths", FIX + "/lg3_sinks.c", "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation"])
        self.assertIn("unregistered function 'cmpi' passed to qsort()", out)
        self.assertIn("unregistered function 'pre_cb' stored as a value", out)
        self.assertEqual(4, len(findings(out)), out)   # + the two (*name)( declarator sites
        self.assertEqual(1, rc)

    def test_lg3_registered_sites_are_silent(self):
        rc, out = run(["--paths", FIX + "/lg3_sinks.c", "--register", SINKS, "--enforce-fnptr",
                       "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)


class Rule5Counting(unittest.TestCase):
    def test_lg4_asserts_inside_if0_do_not_count_but_the_else_branch_does(self):
        rc, out = run(["--paths", FIX + "/lg4_if0_asserts.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'dead_asserts' has 0 assertion(s) in 23 code lines", out)
        self.assertNotIn("live_asserts", out)
        self.assertEqual(1, rc)

    def test_lg5_statements_not_physical_lines(self):
        rc, out = run(["--paths", FIX + "/lg5_oneliners.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'packed' has 0 assertion(s) in 22 code lines", out)
        self.assertEqual(1, rc)

    def test_lg7_unbalanced_brace_in_if0_does_not_hide_later_functions(self):
        rc, out = run(["--paths", FIX + "/lg7_if0_brace.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'over_twenty' has 0 assertion(s) in 23 code lines", out)
        self.assertEqual(1, rc)


class Rule1Recursion(unittest.TestCase):
    def test_lg6_direct_recursion_is_a_finding(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'walk' calls itself (rule 1: no recursion)", out)
        self.assertNotIn("iterative", out)
        self.assertEqual(1, rc)


class Rollout(unittest.TestCase):
    def test_report_only_rules_reports_without_failing(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-rules", "RULE-1"])
        self.assertIn("[report-only]", out)
        self.assertEqual(0, rc)

    def test_report_only_paths_reports_without_failing(self):
        rc, out = run(["--paths", FIX, "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation", "--report-only-paths", FIX + "/"])
        self.assertIn("[report-only]", out)
        self.assertEqual(0, rc)

    def test_report_only_site_is_graced_and_a_stale_entry_is_announced(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--report-only-sites",
                       FIX + "/lg6_recursion.c:walk:RULE-1," + FIX + "/lg6_recursion.c:gone:RULE-1"])
        self.assertIn("GRACE-STALE: " + FIX + "/lg6_recursion.c:gone:RULE-1", out)
        self.assertEqual(0, rc)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools/lint/test_power_of_10.py -v`
Expected: every test in `Rule9Sinks`, `Rule1Recursion`, `Rollout` errors (`AttributeError`/`unrecognized arguments`); `test_lg1_heap_only_file_fails_the_build` fails with `1 != 0` (LG-1); `test_lg2_*` fails (regex misses `heap_caps_*`); `test_lg4/lg5/lg7` fail (counts differ / no finding).

- [ ] **Step 3: Scope — add `devcontroller`, report-only paths, exemptions**

In `tools/lint/power_of_10.py`:
```python
DEFAULT_PATHS = ["components/core", "components/app", "components/drivers", "main", "devcontroller"]

# Paths scanned but never able to fail the build (LG-7): the dev-kit firmware is a sibling
# ESP-IDF project that was never retrofitted. Findings print with a "[report-only]" marker.
DEFAULT_REPORT_ONLY_PATHS = ["devcontroller/"]
```
and extend `EXEMPT_PATH_FRAGMENTS`:
```python
EXEMPT_PATH_FRAGMENTS = (
    "/test/unity/",
    "test/unity/",
    "devcontroller/components/webapi/host/jsmn.",
    "devcontroller/managed_components/",
    "devcontroller/test/",
)
```

- [ ] **Step 4: LG-4 — blank `#if 0` regions before any counting**

Insert immediately above `class LineIndex:`:
```python
_IF0_RE = re.compile(r"^[ \t]*#[ \t]*if[ \t]+0\b")
_DIRECTIVE_RE = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|else|elif|endif)\b")


def blank_dead_regions(text):
    """LG-4: blank every line inside an `#if 0` region (and anything nested under it) so
    assertions, statements and code lines in dead code are not counted. The `#else` branch of an
    `#if 0` IS live and is kept. Character offsets and line numbers are preserved: a dead line
    becomes the same number of spaces. This also fixes LG-7's brace desync, whose only known cause
    is an unbalanced brace inside such a region."""
    out = []
    stack = []          # one entry per open conditional: True while that branch is dead
    for line in text.split("\n"):
        m = _DIRECTIVE_RE.match(line)
        kw = m.group(1) if m else None
        dead_before = any(stack)
        if kw in ("if", "ifdef", "ifndef"):
            stack.append(bool(_IF0_RE.match(line)))
        elif kw in ("else", "elif") and stack:
            if stack[-1]:
                stack[-1] = False   # the #else of an `#if 0` is live code
        elif kw == "endif" and stack:
            stack.pop()
        dead = dead_before or any(stack)
        out.append(" " * len(line) if (dead and kw is None) else line)
    return "\n".join(out)
```

- [ ] **Step 5: LG-5 — count statements, not physical lines**

Insert above the `# ---- Compound-statement bodies` banner:
```python
def count_statements(body):
    """LG-5: top-level statements in a body span -- ';' at parenthesis depth 0, so a
    `for (i = 0; i < n; i++)` header contributes nothing and `a; b; c;` on one physical line
    counts 3."""
    n = 0
    depth = 0
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = depth - 1 if depth > 0 else 0
        elif ch == ";" and depth == 0:
            n += 1
    return n


def effective_lines(stripped, stripped_lines, lidx, open_idx, close_idx):
    """LG-5: a body's size is max(physical code lines, top-level statements), so packing
    statements onto fewer physical lines cannot evade rules 4/5."""
    phys = count_code_lines(stripped_lines, lidx.line_of(open_idx), lidx.line_of(close_idx))
    return max(phys, count_statements(stripped[open_idx:close_idx]))
```
Then in `check_rule4_length`, `check_rule4_compound` and `check_rule5_assertions` replace each
```python
        line_close = lidx.line_of(close_idx)
        n_lines = count_code_lines(stripped_lines, line_open, line_close)
```
with
```python
        n_lines = effective_lines(stripped, stripped_lines, lidx, open_idx, close_idx)
```
(`line_close` has no other use in those three functions).

- [ ] **Step 6: LG-2 — the rule-3 regex**

```python
RULE3_RE = re.compile(
    r"(?<![A-Za-z0-9_])("
    r"heap_caps_[a-z_]*(?:alloc|free)|pvPortMalloc|vPortFree|"
    r"malloc|calloc|realloc|reallocarray|free|strdup|strndup|asprintf|vasprintf|aligned_alloc"
    r")\s*\("
)
```
The lookbehind keeps `sto_free(`/`xxx_malloc(` out; `heap_caps_get_minimum_free_size(` and
`heap_caps_get_free_size(` do not match because `free` is not immediately followed by `(`.

- [ ] **Step 7: LG-3 and LG-6 — sinks, stored functions, recursion**

Insert above `def check_rule3_heap(`:
```python
# LG-3: rule 9 part 2 -- APIs that take a function BY VALUE. The value is the 0-based index of
# every function-valued parameter.
FNPTR_SINKS = {
    "qsort": [3], "bsearch": [4],
    "xTaskCreate": [0], "xTaskCreateStatic": [0],
    "xTaskCreatePinnedToCore": [0], "xTaskCreateStaticPinnedToCore": [0],
    "esp_timer_create": [0],
    "gpio_isr_handler_add": [1],
    "esp_console_cmd_register": [0],
    "spi_bus_add_device": [1],
}
_SINK_RE = re.compile(r"(?<![A-Za-z0-9_])(%s)\s*\(" % "|".join(sorted(FNPTR_SINKS)))
# `.field = fn` / `= fn;` where fn is a function defined in this file (LG-3's struct-field case).
_FNVAL_ASSIGN_RE = re.compile(r"=\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*[,;}]")
_IDENT_ONLY_RE = re.compile(r"^\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*$")


def _arg_spans(stripped, open_paren_idx):
    """Split one call's argument list into raw text slices at top-level commas; [] if unterminated."""
    depth = 0
    args = []
    start = open_paren_idx + 1
    i = open_paren_idx
    n = len(stripped)
    while i < n:
        c = stripped[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
            if depth == 0:
                args.append(stripped[start:i])
                return args
        elif c == "," and depth == 1:
            args.append(stripped[start:i])
            start = i + 1
        i += 1
    return []


def check_rule9_sinks(stripped, relpath, lidx, findings, register, funcs):
    """RULE-9 (LG-3): a function passed by value to a known sink, or stored into a struct field /
    variable, must be registered. The registered symbol is the function's own name."""
    local_fns = {f[0] for f in funcs}
    for m in _SINK_RE.finditer(stripped):
        sink = m.group(1)
        args = _arg_spans(stripped, m.end() - 1)
        for idx in FNPTR_SINKS[sink]:
            if idx >= len(args):
                continue
            im = _IDENT_ONLY_RE.match(args[idx])
            if not im:
                continue
            symbol = im.group(1)
            if symbol not in local_fns:
                continue          # a parameter or an extern: not a function this file defines
            if register.is_registered(9, relpath, symbol):
                continue
            findings.append({"file": relpath, "line": lidx.line_of(m.start()), "rule": "RULE-9",
                             "symbol": symbol,
                             "message": "unregistered function '%s' passed to %s() "
                                        "(rule 9: no function pointers outside the register)"
                                        % (symbol, sink)})
    for m in _FNVAL_ASSIGN_RE.finditer(stripped):
        symbol = m.group(1)
        if symbol not in local_fns or register.is_registered(9, relpath, symbol):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(m.start()), "rule": "RULE-9",
                         "symbol": symbol,
                         "message": "unregistered function '%s' stored as a value "
                                    "(rule 9: no function pointers outside the register)" % symbol})


def check_rule1_recursion(stripped, relpath, lidx, findings, register, funcs):
    """RULE-1 (LG-6): direct recursion -- a function body that calls its own name. Indirect
    recursion stays with the review checklist (clang-tidy misc-no-recursion is advisory)."""
    for name, _name_start, open_idx, close_idx, _po, _pc in funcs:
        body = stripped[open_idx:close_idx]
        m = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"\s*\(").search(body)
        if m is None or register.is_registered(1, relpath, name):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(open_idx + m.start()),
                         "rule": "RULE-1", "symbol": name,
                         "message": "function '%s' calls itself (rule 1: no recursion)" % name})
```

- [ ] **Step 8: Wire the new checks into `run()`**

```python
def run(paths, register_path, enforce_fnptr, base_dir, report_only_paths=()):
```
Per-component counters gain `"rule1": 0` (add it to the `setdefault` dict). Inside the file loop:
```python
        comp["files"] += 1
        n_before = len(all_findings)
        posix_relpath = relpath.replace(os.sep, "/")
```
```python
        stripped = blank_dead_regions(strip_comments_and_literals(text))
        lidx = LineIndex(stripped)
        ro = any(posix_relpath.startswith(pfx) for pfx in report_only_paths)
```
and after the rule-8a count:
```python
        rule1_findings = []
        check_rule1_recursion(stripped, relpath, lidx, rule1_findings, register, funcs)
        comp["rule1"] += len(rule1_findings)
        all_findings.extend(rule1_findings)

        if enforce_fnptr:
            rule9_findings = []
            check_rule9_fnptr(stripped, relpath, lidx, rule9_findings, register, funcs)
            check_rule9_sinks(stripped, relpath, lidx, rule9_findings, register, funcs)
            comp["rule9"] += len(rule9_findings)
            all_findings.extend(rule9_findings)

        if ro:
            for f in all_findings[n_before:]:
                f["report_only"] = True
```

- [ ] **Step 9: CLI — the two-step rollout flags and blocking rule 3**

In `render_text(findings, summary, enforce_fnptr, rule8a, report_only_rules=())` mark graced lines:
```python
        mark = " [report-only]" if (f.get("report_only") or f["rule"] in report_only_rules) else ""
        lines.append("%s:%d: %s: %s%s" % (f["file"], f["line"], f["rule"], f["message"], mark))
```
and print `totals: rule1(recursion)=%d` above the existing totals line. In `main()`:
```python
    ap.add_argument("--report-only-paths", nargs="*", default=list(DEFAULT_REPORT_ONLY_PATHS),
                     help="Path prefixes scanned but never able to fail the build "
                          "(default: %s)" % " ".join(DEFAULT_REPORT_ONLY_PATHS))
    ap.add_argument("--report-only-sites", default="",
                     help="Comma-separated `path:symbol:RULE` triples (symbol may be empty) that "
                          "are reported but never fail the build: the rollout grace list for a "
                          "newly added check whose known sites belong to another plan's files. "
                          "A triple that matches nothing prints a GRACE-STALE line.")
    ap.add_argument("--report-only-rules", default="",
                     help="Comma-separated rule ids reported but never failing the build.")
```
```python
    report_only_rules = tuple(r.strip() for r in args.report_only_rules.split(",") if r.strip())
    grace = []
    for raw in args.report_only_sites.split(","):
        raw = raw.strip()
        if not raw:
            continue
        parts = raw.split(":")
        if len(parts) != 3:
            print("bad --report-only-sites entry %r (want path:symbol:RULE)" % raw)
            return 2
        grace.append(tuple(parts))
    findings, summary, register, rule8a = run(args.paths, args.register, args.enforce_fnptr,
                                               base_dir, tuple(args.report_only_paths))
```
and the exit gate:
```python
    if args.fail_on_violation:
        blocking_rules = ("RULE-1", "RULE-3", "RULE-4", "RULE-4-COMPOUND", "RULE-5", "RULE-8",
                          "RULE-9")

        def graced(f):
            for path, symbol, rule in grace:
                if f["file"] == path and f["rule"] == rule and (symbol in ("", f.get("symbol", ""))):
                    return True
            return False

        for f in findings:
            if graced(f):
                f["report_only"] = True
        used = {(p, sy, r) for (p, sy, r) in grace for f in findings
                if f["file"] == p and f["rule"] == r and (sy in ("", f.get("symbol", "")))}
        for entry in grace:
            if entry not in used:
                print("GRACE-STALE: %s matches no finding -- remove it from the CI invocation"
                      % ":".join(entry))
        blocking = [f for f in findings
                    if f["rule"] in blocking_rules
                    and f["rule"] not in report_only_rules
                    and not f.get("report_only")]
        if blocking:
            print("\nBLOCKING: %d violation(s)" % len(blocking))
            return 1
    return 0
```
Also update the module docstring: RULE-3 is blocking; RULE-1 exists; report-only is per path/rule/site, not global.

- [ ] **Step 10: Run the tests to verify they pass**

Run: `python3 -m unittest tools/lint/test_power_of_10.py -v`
Expected: 10 tests, OK.

- [ ] **Step 11: Measure the real tree and record the rollout list**

Run: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation`
Expected (measured on `main` @ f4d4db2 with exactly these changes — `BLOCKING: 10 violation(s)`, rc 1):
```
components/app/sys/lt_nvs.c:365: RULE-5: function 'errlog_add' has 0 assertion(s) in 22 code lines (>20 lines requires >=2)
components/app/ui/ui.c:1111: RULE-4-COMPOUND: compound statement (switch) body is 38 code lines (> 30)
components/core/config/cfg.c:7: RULE-5: function 'cfg_defaults' has 1 assertion(s) in 40 code lines (>20 lines requires >=2)
components/core/config/cfg_json.c:30: RULE-4: function 'apply' is 64 code lines (> 60)
components/core/config/cfg_json.c:124: RULE-1: function 'walk' calls itself (rule 1: no recursion)
components/core/config/cfg_json.c:149: RULE-4: function 'cfg_to_json' is 110 code lines (> 60)
components/drivers/board_devkit_v1/board.c:234: RULE-9: unregistered function 'cmp_int' passed to qsort() (rule 9: no function pointers outside the register)
components/drivers/board_devkit_v1/board.c:284: RULE-9: unregistered function 'btn_isr' passed to gpio_isr_handler_add() (rule 9: no function pointers outside the register)
components/drivers/export_serial/export_serial.c:366: RULE-5: function 'fmt_from_str' has 0 assertion(s) in 21 code lines (>20 lines requires >=2)
components/drivers/export_serial/export_serial.c:643: RULE-5: function 'tally_cb' has 0 assertion(s) in 23 code lines (>20 lines requires >=2)
```
plus 85 `devcontroller/**` findings, all marked `[report-only]` (53 RULE-5, 20 RULE-9, 8 RULE-4-COMPOUND, 4 RULE-4), and `rule3(heap)=0` (no heap API is called anywhere in scope — verified).
Record these ten verbatim in the task report; Task 4 pastes them into the CI grace list and Task 11 deletes them. Owners: `walk` → F1b (R27), `cmp_int` → F2a (R19), `btn_isr` → this plan (Step 12), the other seven → the issue Task 14 files.

Note for the record (the audit named it): `export_serial.c:read_through_ses` is 18 physical lines / **19 statements**, so it stays one statement under the rule-5 threshold even with statement counting. It is not in the list above and is not a finding.

- [ ] **Step 12: Amend PD-6's register key (no new rows)**

`docs/power-of-10-deviations.md`, row PD-6: change the `file:symbol` cell from
`components/drivers/board_devkit_v1/board.c:board_buttons_enable_isr` to
`components/drivers/board_devkit_v1/board.c:btn_isr`, and append to that row's reviewer cell:
`; key moved to the ISR itself (F4, 2026-10-13) now that the linter sees the gpio_isr_handler_add() argument rather than only the (*name)( declarator`.
Also replace the file's stale paragraph
> Rule 9 (`--enforce-fnptr`) becomes a blocking CI check in Session 4.5.6; with the 4.5.5 rework complete, `--enforce-fnptr` reports zero unregistered `(*name)(` sites tree-wide.

with
> Rule 9 (`--enforce-fnptr`) is a blocking CI check. Since the 2026-10-13 audit fix wave the linter no longer measures only the `(*name)(` declarator spelling: it also resolves a function passed by value to a known sink (`qsort`, `bsearch`, `xTaskCreate*`, `esp_timer_create`, `gpio_isr_handler_add`, `esp_console_cmd_register`, `spi_bus_add_device`) and a function stored into a struct field or variable, so PD-6's `btn_isr` and PD-9's `pre_cb` are now checked sites rather than voluntary book-keeping. Rules 1 (direct recursion) and 3 (heap) are blocking too.

No new deviation rows are expected from this plan: run `python3 tools/lint/power_of_10.py --enforce-fnptr --json | python3 -c "import json,sys; print(len(json.load(sys.stdin)['findings']))"` before and after and confirm the register row count stays 9.

- [ ] **Step 13: Commit**

```bash
git add tools/lint/power_of_10.py tools/lint/test_power_of_10.py tools/lint/fixtures docs/power-of-10-deviations.md
git commit -m "lint(p10): close LG-1..LG-7 -- blocking rule 3 + full heap API set, #if 0 stripped before counting, statements not physical lines, fn-ptr sinks and stored values, direct recursion, devcontroller scanned report-only"
```

- [ ] **CHECKPOINT — Task 1**

```bash
printf 'Task 1 complete %s — linter gaps LG-1..LG-7 closed; 10 graced sites recorded\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 2 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 2: Venue linter — announce must be on the acquisition path (PR #101 M5) — PARALLEL

**Files:**
- Modify: `tools/lint/venue_announce.py` (arrives on `main` with F0/PR #101)
- Create: `tools/lint/test_venue_announce.py`, `tools/lint/fixtures/venue_bypass.c`

**Interfaces:**
- Consumes: `power_of_10.py`'s `LineIndex`, `find_top_level_functions`, `strip_comments_and_literals` (Task 1 leaves all three unchanged).
- Produces: `venue_announce.main(argv)` keeps its `--paths`/`--fail-on-violation` CLI and its rc contract (1 on violation with the flag, 1 on the zero-sites floor).

- [ ] **Step 1: Write the failing fixture and test**

`tools/lint/fixtures/venue_bypass.c`:
```c
/* fixture: three bypasses the pre-M5 rule accepted, and the shape that must still pass. */
void sibling_branch(int flag, const trk_venue_t *v, uint16_t id)
{
    if (flag) {
        lap_set_venue(&s_lap, v);
    } else {
        ui_post_venue(id, 0);
    }
}

void announce_before(const trk_venue_t *v)
{
    ui_post_venue(v->id, 0);
    lap_set_venue(&s_lap, v);
}

void wrong_venue(const trk_venue_t *v)
{
    lap_set_venue(&s_lap, v);
    ui_post_venue(1234, 0);
}

void correct(const trk_venue_t *v)
{
    lap_set_venue(&s_lap, v);
    ui_post_venue(v->id, 0);
}
```

`tools/lint/test_venue_announce.py`:
```python
#!/usr/bin/env python3
"""Unit tests for tools/lint/venue_announce.py (PR #101 review M5)."""
import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import venue_announce as va

FIX = "tools/lint/fixtures/venue_bypass.c"


def run(args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = va.main(args)
    return rc, buf.getvalue()


class AcquisitionPath(unittest.TestCase):
    def test_three_bypasses_are_violations_and_the_correct_shape_is_not(self):
        rc, out = run(["--paths", FIX, "--fail-on-violation"])
        self.assertIn("in sibling_branch()", out)
        self.assertIn("in announce_before()", out)
        self.assertIn("in wrong_venue()", out)
        self.assertNotIn("in correct()", out)
        self.assertIn("venue-announce: 3 violation(s)", out)
        self.assertEqual(1, rc)

    def test_the_real_tree_still_passes(self):
        rc, out = run(["--fail-on-violation"])
        self.assertIn("venue-announce: 0 violation(s)", out)
        self.assertEqual(0, rc)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools/lint/test_venue_announce.py -v`
Expected: `test_three_bypasses_...` FAILS with `venue-announce: 0 violation(s)` (today every bypass passes); `test_the_real_tree_still_passes` passes already.

- [ ] **Step 3: Tighten the rule**

In `tools/lint/venue_announce.py`, after `ANNOUNCE_RE`:
```python
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
```
In `check_file`, replace the `body is None` skip with:
```python
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
```
and replace the whole-function `ANNOUNCE_RE.search(...)` acceptance with:
```python
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
```
and the message becomes:
```python
        findings.append("%s:%d: %s() in %s() acquires a venue with no ui_post_venue() call on "
                        "that path (same block, after the set, naming the same venue) -- see "
                        "tools/lint/venue_announce.py" %
                        (relpath, lidx.line_of(m.start()), m.group(1), name))
```
Append a `Fix round 2 (PR #101 review M5, 2026-10-13)` paragraph to the module docstring recording the three bypasses now closed and the one that remains open by design (an announce inside a helper the acquisition calls).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest tools/lint/test_venue_announce.py -v`
Expected: 2 tests OK. Measured on the F0 tree: the two real acquisitions (`pipeline.c` `lap_import_rtc` in `on_fix_try_resume`, `lap_set_venue` in `pipeline_init`) both satisfy the stricter rule → `venue-announce: 0 violation(s)`, rc 0.

- [ ] **Step 5: Commit**

```bash
git add tools/lint/venue_announce.py tools/lint/test_venue_announce.py tools/lint/fixtures/venue_bypass.c
git commit -m "lint(venue): the announce must be on the acquisition path (same block, after the set, same venue) + its first unit tests (PR #101 M5)"
```

- [ ] **CHECKPOINT — Task 2**

```bash
printf 'Task 2 complete %s — venue linter requires an on-path announce\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 3 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 3: `tools/spec_api_check.py` — spec §5.2 can never drift again (R18) — PARALLEL

**Files:**
- Create: `tools/spec_api_check.py`, `tools/test_spec_api_check.py`

**Interfaces:**
- Consumes: `tools/lint/power_of_10.py:strip_comments_and_literals` (via `sys.path`).
- Produces: `python3 tools/spec_api_check.py` → rc 1 and one line per drift; `python3 tools/spec_api_check.py --emit core/<x>.h` → the declaration list that header's §5.2 block must contain. Task 9 consumes `--emit`; Task 4 runs the checker in `hygiene`.

- [ ] **Step 1: Write the failing test**

`tools/test_spec_api_check.py`:
```python
#!/usr/bin/env python3
"""Unit tests for tools/spec_api_check.py (audit §3.3 item 8: spec §5.2 drifted silently)."""
import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spec_api_check as sac

HDR = """
#ifndef X_H
#define X_H
#include <stdint.h>
typedef void (*cb_t)(int x);
typedef struct { int a; int b; } thing_t;
int  thing_do(thing_t *t, int n);
void thing_reset(thing_t *t);
#endif
"""


class Extraction(unittest.TestCase):
    def test_prototypes_typedefs_and_macros(self):
        protos, typedefs, macros = sac.api_of(HDR)
        self.assertEqual({"thing_do", "thing_reset"}, set(protos))
        self.assertEqual("int thing_do(thing_t *t, int n);", protos["thing_do"])
        self.assertEqual({"cb_t"}, typedefs)
        self.assertEqual({"X_H"}, macros)


class Drift(unittest.TestCase):
    def test_the_committed_spec_and_headers_agree(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sac.main([])
        self.assertEqual(0, rc, buf.getvalue())

    def test_emit_lists_every_prototype(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            sac.main(["--emit", "core/cfg.h"])
        self.assertIn("int cfg_validate(cfg_t *c);", buf.getvalue())
        self.assertIn("bool cfg_migrate_supported(uint8_t from_version);", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest tools/test_spec_api_check.py -v`
Expected: all three error with `ModuleNotFoundError: No module named 'spec_api_check'`.

- [ ] **Step 3: Write `tools/spec_api_check.py`**

```python
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
    text = open(os.path.join(HDR_DIR, stem + ".h"), encoding="utf-8").read()
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
    spec_text = open(args.spec, encoding="utf-8").read()
    blocks = spec_blocks(spec_text)
    findings = []
    for stem in HEADERS:
        if stem not in blocks:
            findings.append("spec §5.2: no `core/%s.h` block found in %s" % (stem, args.spec))
            continue
        hp, ht, hm = api_of(open(os.path.join(HDR_DIR, stem + ".h"), encoding="utf-8").read())
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
```

- [ ] **Step 4: Run the checker on the tree and record the drift**

Run: `python3 tools/spec_api_check.py`
Expected on `main` @ f4d4db2: `spec_api_check: 41 drift(s) across 13 §5.2 blocks`, rc 1 — including `core/core.h: STALE void core_set_assert_hook(core_assert_hook_t hook);`, `core/ses.h: STALE void ses_reader_feed(...)`, `core/cfg.h: MISSING bool cfg_migrate_supported(uint8_t from_version);`, `core/lap.h: SIGNATURE void lap_on_fix(... event_t *out, int cap, int *n_out);`. The §5.2 blocks are repaired in **Task 9** (after the wave merges), so `test_the_committed_spec_and_headers_agree` is expected RED until then.

- [ ] **Step 5: Run the unit tests**

Run: `python3 -m unittest tools/test_spec_api_check.py -v`
Expected: `Extraction` and `test_emit_lists_every_prototype` PASS; `test_the_committed_spec_and_headers_agree` FAILS with the 41-drift output. Mark it `@unittest.expectedFailure` with the comment `# repaired by Task 9 of docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md; the decorator is deleted in that task` so the suite is green on this branch, and delete the decorator in Task 9.

- [ ] **Step 6: Commit**

```bash
git add tools/spec_api_check.py tools/test_spec_api_check.py
git commit -m "tools: spec_api_check.py -- diff spec §5.2's API blocks against the real core headers (41 drifts on main today)"
```

- [ ] **CHECKPOINT — Task 3**

```bash
printf 'Task 3 complete %s — spec_api_check.py added; 41 drifts measured\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 4 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 4: CI workflows (R13, R14, drv:M13) — PARALLEL

**Files:**
- Modify: `.github/workflows/ci.yml`, `.github/workflows/firmware.yml`

**Interfaces:**
- Consumes: `tools/lint/power_of_10.py` flags from Task 1, `tools/lint/test_power_of_10.py`, `tools/lint/test_venue_announce.py` (Task 2), `tools/spec_api_check.py` + `tools/test_spec_api_check.py` (Task 3), `tools/test_sign_release.sh` + `tools/tracks/test_gen_tracks.py` (Task 5).
- Produces: job names `hygiene`, `host-tests`, `dc-host-tests`, `tools-tests`, `tracks-generated`, `static-analysis`, `coverage` (advisory), `build (moto_neo6m)`, `build (moto_sim)`, `build (devcontroller)` — the first six plus the three builds are the nine required checks Task 14 sets.

- [ ] **Step 1: `hygiene` gains the spec-API gate**

In `.github/workflows/ci.yml`, after the "ESP-IDF container tag matches .idf-version" step:
```yaml
      - name: spec §5.2 API blocks match components/core/include/core/*.h
        run: python3 tools/spec_api_check.py
```

- [ ] **Step 2: `tools-tests` runs every tool's tests**

Replace the `tools-tests` job's probe + single step with:
```yaml
  tools-tests:
    # Python/bash unit tests for tools/ host CLIs and linters. Stdlib only -- pyserial is imported
    # lazily inside devkit.py (only where a real port is opened), so none of this needs an install
    # step, and no test here needs ESP-IDF, a board, or a signing key.
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: python3 -m unittest (devkit, linters, spec API, track generator)
        run: |
          python3 -m unittest -v \
            tools/test_devkit.py \
            tools/lint/test_power_of_10.py \
            tools/lint/test_venue_announce.py \
            tools/test_spec_api_check.py \
            tools/tracks/test_gen_tracks.py
      - name: sign_release.sh guard harness (dry-run; no IDF, no key)
        run: bash tools/test_sign_release.sh
```
(`fetch-depth: 0` is required: `sign_release.sh`'s dirty guard calls `git describe --tags`.)

- [ ] **Step 3: `static-analysis` — correct the false comment and carry the rollout grace list**

Replace the job's leading comment block's two false sentences (`drv:M13`, false-claim 5: rule 3 was *not* blocking) and the linter step with:
```yaml
  static-analysis:
    # Power of 10 (Holzmann/JPL) static analysis -- design §5, amended by the 2026-10-13 audit fix
    # wave (ruling R12). BLOCKING: the custom linter is the authoritative rule 1/3/4/4-compound/5/
    # 8/9 gate and FAILS the job on any un-graced violation. Together with the build jobs' -Werror
    # set (the rule-6 -Wmissing-prototypes/-declarations, -Wshadow, -Wconversion) this gates the
    # ten rules. clang-tidy and cppcheck stay `continue-on-error: true` (advisory second analysers,
    # rule 10) -- see design §5. `static-analysis` IS a branch-protection required check as of the
    # audit fix wave (ruling R13).
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install cppcheck + clang-tidy
        run: sudo apt-get update && sudo apt-get install -y cppcheck clang-tidy
      # ROLLOUT GRACE (ruling R12, two-step): the tightened checks land before the plans that own
      # the offending files. Each entry names a finding that belongs to another plan in the
      # 2026-10-13 wave; the linter prints GRACE-STALE for any entry that stops matching, and the
      # whole flag is deleted by F4's final task once every owner has merged. Owners:
      #   cfg_json.c:walk            RULE-1           -> F1b (ruling R27, walk becomes two loops)
      #   board.c:cmp_int            RULE-9           -> F2a (ruling R19, bounded insertion sort)
      #   the remaining seven        RULE-4/4C/5      -> issue filed by F4 (statement counting)
      - name: power_of_10.py linter (BLOCKING; rules 1/3/4/4-compound/5/8/9)
        run: |
          python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation \
            --report-only-sites "components/app/sys/lt_nvs.c:errlog_add:RULE-5,components/app/ui/ui.c:handle_event:RULE-4-COMPOUND,components/core/config/cfg.c:cfg_defaults:RULE-5,components/core/config/cfg_json.c:apply:RULE-4,components/core/config/cfg_json.c:walk:RULE-1,components/core/config/cfg_json.c:cfg_to_json:RULE-4,components/drivers/board_devkit_v1/board.c:cmp_int:RULE-9,components/drivers/board_devkit_v1/board.c:btn_isr:RULE-9,components/drivers/export_serial/export_serial.c:fmt_from_str:RULE-5,components/drivers/export_serial/export_serial.c:tally_cb:RULE-5"
```
Keep the existing `venue-announce`, `cppcheck` and `clang-tidy` steps unchanged. Delete the stale "To make this a merge gate, add `static-analysis` to the branch-protection required checks" note (Task 14 does it) and the `TODO(4.5.x)` clang-tidy note's "likely alongside the rule 9 (Session 4.5.5) or blocking-flip (Session 4.5.6) work" tail, which names sessions that are long done.

> `board.c:btn_isr`'s grace entry is removed in the **same commit** as Task 1 Step 12 if that task has already merged into this branch — the PD-6 key amendment fixes it. Keep it listed only until that commit lands; the GRACE-STALE line is the reminder.

- [ ] **Step 4: Add the advisory `coverage` job (R14)**

Append to `ci.yml`:
```yaml
  coverage:
    # §22.1's "≥ 90 % lines in core/ (gcov in CI)" had no job at all (audit §3.3 item 28). This is
    # the ADVISORY line-coverage report the amended §22.1 describes: it never fails the build and
    # is not a required check -- it prints the percentage so a drop is visible in the PR.
    runs-on: ubuntu-latest
    continue-on-error: true
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install gcovr
        run: sudo apt-get update && sudo apt-get install -y gcovr
      - name: Configure + build + run the host suite with --coverage
        run: |
          cmake -S test -B test/build-cov -DCMAKE_BUILD_TYPE=Debug \
                -DCMAKE_C_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
          cmake --build test/build-cov --parallel
          ctest --test-dir test/build-cov --output-on-failure
      - name: gcovr over components/core
        run: |
          gcovr --root . --filter 'components/core/' \
                --exclude 'components/core/util/jsmn.c' \
                --exclude 'components/core/ui/fonts.c' \
                --exclude 'components/core/ui/icons.c' \
                --print-summary --xml-pretty -o coverage.xml | tee cov.txt
          echo "### core/ line coverage" >> "$GITHUB_STEP_SUMMARY"
          grep -E '^lines:' cov.txt >> "$GITHUB_STEP_SUMMARY"
      - uses: actions/upload-artifact@v4
        with:
          name: coverage-core
          path: coverage.xml
          if-no-files-found: ignore
```
`gcovr --print-summary` prints `lines: NN.N% (X out of Y)`; that line is what lands in the job summary and in the PR body when a plan quotes it.

- [ ] **Step 5: `firmware.yml` — nothing structural, one comment correction**

The three matrix entries already produce the three required `build (...)` check names. Correct the stale sentence in the devcontroller comment (`devcontroller/main/CMakeLists.txt carries its own -Werror set, so idf.py build fails on any warning just like the lap-timer builds`) only if `grep -n "Werror" devcontroller/main/CMakeLists.txt` still shows it; otherwise replace it with what the file actually does. Add, after the `Build core_selftest (ESP32)` step:
```yaml
      - name: Fail if core_selftest skipped a test/test_*.c without a documented reason
        if: steps.probe_selftest.outputs.present == 'true' && matrix.env == 'moto_neo6m'
        shell: bash
        run: |
          # memory `feedback-core-selftest-gate`: the selftest globs test/test_*.c and excludes a
          # named few. Every exclusion must be justified in the CMakeLists comment above it.
          grep -q 'list(FILTER TEST_SRCS EXCLUDE REGEX' test_apps/core_selftest/main/CMakeLists.txt
```

- [ ] **Step 6: Verify the YAML parses and the linter invocation is exact**

Run:
```bash
python3 -c "import sys;import json;print('yaml ok')" && \
python3 - <<'EOF'
import re
y = open('.github/workflows/ci.yml').read()
assert 'spec_api_check.py' in y and 'coverage:' in y and '--report-only-sites' in y
print('ci.yml wired')
EOF
```
Then run the graced linter invocation locally, exactly as CI will:
```bash
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation --report-only-sites "<the same string>"; echo $?
```
Expected: `0` (measured: the ten findings are all graced, nothing else blocks).

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/firmware.yml
git commit -m "ci: spec-API gate in hygiene, every tool's tests in tools-tests, advisory core coverage job, honest static-analysis comment + the R12 rollout grace list"
```

- [ ] **CHECKPOINT — Task 4**

```bash
printf 'Task 4 complete %s — CI: spec gate, tools-tests, coverage, grace list\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 5 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

## Session 2 — release tooling, tests, docs, DRAM (Tasks 5–8)

Four independent file groups. Task 8 builds firmware (never flashes). Stop for review after Task 8.

### Task 5: `sign_release.sh`, `gen_tracks.py`, `core_selftest` CMake (R15, drv:L24, drv:L25) — PARALLEL

**Files:**
- Modify: `tools/sign_release.sh`, `tools/tracks/gen_tracks.py`, `test_apps/core_selftest/main/CMakeLists.txt`
- Create: `tools/test_sign_release.sh`, `tools/tracks/test_gen_tracks.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `tools/sign_release.sh <env> [--allow-sim] [--dry-run]` (rc 1 on a refused build, rc 0 after a dry run); `tools/test_sign_release.sh` (rc 0 when every guard behaves); `gen_tracks.GenError` + `gen_tracks.main(argv)` returning an int rc; `tools/tracks/test_gen_tracks.py`. Task 4 runs both harnesses.

### 5a — `sign_release.sh` (SERIAL within the file)

- [ ] **Step 1: Write the failing harness**

`tools/test_sign_release.sh`:
```bash
#!/usr/bin/env bash
# tools/test_sign_release.sh -- dry-run harness for tools/sign_release.sh's §4.6/§21.5 guards
# (audit drv:M5). Exercises every refusal path without ESP-IDF, espsecure.py, or a signing key:
# --dry-run stops before the build, and a fixture build/<env>/build_config.h drives the
# authoritative post-build check. Run from the repo root; CI runs it in `tools-tests`.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PASS=0
FAIL=0
DIRT="docs/power-of-10-checklist.md"
cleanup() { git checkout -- "$DIRT" 2>/dev/null; rm -rf build/t_sign_fixture; }
trap cleanup EXIT

check() { # <name> <expected rc> <cmd...>
    local name="$1" want="$2"; shift 2
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" = "$want" ]; then PASS=$((PASS+1)); echo "ok   $name (rc $rc)"
    else FAIL=$((FAIL+1)); echo "FAIL $name: want rc $want, got $rc"; echo "$out" | sed 's/^/     /'; fi
}

grep_check() { # <name> <needle> <cmd...>
    local name="$1" needle="$2"; shift 2
    local out; out=$("$@" 2>&1)
    if printf '%s' "$out" | grep -q -- "$needle"; then PASS=$((PASS+1)); echo "ok   $name"
    else FAIL=$((FAIL+1)); echo "FAIL $name: output lacks '$needle'"; echo "$out" | sed 's/^/     /'; fi
}

# 1. a sim environment is refused without --allow-sim
check "sim env refused"            1 tools/sign_release.sh moto_sim --dry-run
grep_check "sim refusal explains"  "--allow-sim" tools/sign_release.sh moto_sim --dry-run
# 2. ... and allowed with it
check "sim env with --allow-sim"   0 tools/sign_release.sh moto_sim --allow-sim --dry-run
# 3. a non-sim environment dry-runs clean
check "prod env dry-run"           0 tools/sign_release.sh moto_neo6m --dry-run
# 4. a dirty tracked file is refused
printf '\n<!-- sign_release dirty probe -->\n' >> "$DIRT"
check "dirty tree refused"         1 tools/sign_release.sh moto_neo6m --dry-run
grep_check "dirty refusal explains" "clean tree" tools/sign_release.sh moto_neo6m --dry-run
git checkout -- "$DIRT"
# 5. the authoritative post-build check reads build_config.h, not the env name
mkdir -p build/t_sign_fixture
cat > build/t_sign_fixture/build_config.h <<'EOF'
#define CFG_GPS_SIM        1
#define CFG_IMU_SIM        1
#define CFG_HWID           "fixture_hwid"
#define CFG_FW_VERSION     "v0.0.0-fixture"
EOF
check "build_config sim refused"   1 tools/sign_release.sh t_sign_fixture --dry-run
check "build_config sim allowed"   0 tools/sign_release.sh t_sign_fixture --allow-sim --dry-run
# 6. a bad argument is a usage error
check "unknown flag"               2 tools/sign_release.sh moto_sim --nope

echo "test_sign_release: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
```
Make it executable: `chmod +x tools/test_sign_release.sh`.

- [ ] **Step 2: Run the harness to verify it fails**

Run: `bash tools/test_sign_release.sh`
Expected: every `check` fails — today `sign_release.sh` rejects `--dry-run` with `usage: tools/sign_release.sh <env>` (rc 2) and has no sim/dirty guard at all.

- [ ] **Step 3: Rewrite `tools/sign_release.sh`'s front half**

Replace lines 13–24 (argument parse through `./build.sh "$ENV_NAME" build`) with:
```bash
ENV_NAME=""
ALLOW_SIM=0
DRY_RUN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --allow-sim) ALLOW_SIM=1 ;;
        --dry-run)   DRY_RUN=1 ;;
        -h|--help)   echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]"; exit 0 ;;
        -*)          echo "unknown option $1" >&2
                     echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]" >&2; exit 2 ;;
        *)           [ -z "$ENV_NAME" ] || { echo "one environment only (got '$ENV_NAME' and '$1')" >&2; exit 2; }
                     ENV_NAME="$1" ;;
    esac
    shift
done
[ -n "$ENV_NAME" ] || { echo "usage: tools/sign_release.sh <env> [--allow-sim] [--dry-run]" >&2; exit 2; }

# §21.5: a release image is built from a clean tree. Untracked files are ignored (they cannot be
# in the image); a modified tracked file is a refusal.
if ! git diff --quiet HEAD -- . 2>/dev/null; then
    echo "refusing: the working tree has modified tracked files -- a release build requires a clean tree (spec §21.5)" >&2
    exit 1
fi
ver_probe=$(git describe --tags --match 'v*' --dirty --always 2>/dev/null || echo "")
case "$ver_probe" in
    *-dirty) echo "refusing: git describe reports '$ver_probe' -- a release build requires a clean tree (spec §21.5)" >&2; exit 1 ;;
esac

# §4.6: the sim drivers are never part of a release build. Cheap pre-check on the env name and the
# flag overrides; the authoritative check is build_config.h's CFG_GPS_SIM/CFG_IMU_SIM below.
sim_refuse() {
    echo "refusing: $1 selects a bench (sim) driver and is never part of a release build (spec §4.6); pass --allow-sim to sign a bench image anyway" >&2
    exit 1
}
if [ "$ALLOW_SIM" = 0 ]; then
    case "$ENV_NAME" in *sim*) sim_refuse "environment '$ENV_NAME'" ;; esac
    [ "${GPS:-}" != "sim" ] || sim_refuse "GPS=sim"
    [ "${IMU:-}" != "sim" ] || sim_refuse "IMU=sim"
fi

BUILD_DIR="build/${ENV_NAME}"
BIN="${BUILD_DIR}/laptimer.bin"
CFGH="${BUILD_DIR}/build_config.h"

# The authoritative sim check: whatever the build actually generated.
check_cfg_sim() {
    [ -f "$CFGH" ] || return 0
    if [ "$ALLOW_SIM" = 0 ]; then
        grep -qE '^#define[ \t]+CFG_GPS_SIM[ \t]+1' "$CFGH" && sim_refuse "build_config.h CFG_GPS_SIM=1"
        grep -qE '^#define[ \t]+CFG_IMU_SIM[ \t]+1' "$CFGH" && sim_refuse "build_config.h CFG_IMU_SIM=1"
    fi
    return 0
}

if [ "$DRY_RUN" = 1 ]; then
    check_cfg_sim
    echo "dry-run: guards passed; would run ./build.sh $ENV_NAME build && ./build.sh $ENV_NAME size, then sign $BIN"
    exit 0
fi

PRIV="keys/laptimer_priv.pem"
PUB="keys/laptimer_pub.pem"
[ -f "$PRIV" ] || { echo "missing signing key $PRIV (generate offline: espsecure.py generate_signing_key --version 1 $PRIV)" >&2; exit 1; }
[ -f "$PUB" ]  || { echo "missing public key $PUB" >&2; exit 1; }

command -v espsecure.py >/dev/null 2>&1 || source tools/idf-env.sh >/dev/null

./build.sh "$ENV_NAME" build
./build.sh "$ENV_NAME" size      # §19.1: the ≤ 0x130000 app-image gate applies to a release image
check_cfg_sim
```
Delete the now-duplicated `PRIV`/`PUB`/`espsecure` block and the old `BUILD_DIR`/`BIN`/`CFGH` assignments further down; keep everything from `[ -f "$BIN" ]` onward unchanged. Update the file header comment to document `--allow-sim`, `--dry-run`, the clean-tree rule and the `size` gate.

- [ ] **Step 4: Run the harness to verify it passes**

Run: `bash tools/test_sign_release.sh`
Expected: `test_sign_release: 9 passed, 0 failed`, rc 0. Confirm `git status --porcelain` is empty afterwards (the trap restored `docs/power-of-10-checklist.md` and removed `build/t_sign_fixture`).

### 5b — `gen_tracks.py` explicit validation (drv:L24)

- [ ] **Step 5: Write the failing test**

`tools/tracks/test_gen_tracks.py`:
```python
#!/usr/bin/env python3
"""Unit tests for tools/tracks/gen_tracks.py -- validation must survive `python3 -O` (drv:L24)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tracks

GOOD = {
    "id": 42, "name": "Fixture", "lat": -34.0, "lon": 18.5, "radius_m": 2000, "verified": True,
    "layouts": [{"id": 1, "name": "Full", "dir": 1,
                 "sf": [[-34.0000, 18.5000], [-34.0000, 18.5003]], "sectors": [], "length_m": 1000}],
}


def write(tmp, doc, name="v.json"):
    p = os.path.join(tmp, name)
    with open(p, "w") as fh:
        json.dump(doc, fh)
    return p


class Validation(unittest.TestCase):
    def test_good_venue_expands(self):
        self.assertEqual(42, gen_tracks.expand(json.loads(json.dumps(GOOD)))["id"])

    def test_short_sf_line_is_rejected(self):
        bad = json.loads(json.dumps(GOOD))
        bad["layouts"][0]["sf"] = [[-34.0, 18.5], [-34.0, 18.50001]]
        with self.assertRaises(gen_tracks.GenError) as e:
            gen_tracks.expand(bad)
        self.assertIn("outside 10", str(e.exception))

    def test_duplicate_layout_ids_rejected(self):
        bad = json.loads(json.dumps(GOOD))
        bad["layouts"].append(json.loads(json.dumps(bad["layouts"][0])))
        with self.assertRaises(gen_tracks.GenError):
            gen_tracks.expand(bad)

    def test_bundled_id_range_enforced(self):
        bad = json.loads(json.dumps(GOOD))
        bad["id"] = 1000
        with self.assertRaises(gen_tracks.GenError):
            gen_tracks.expand(bad)

    def test_duplicate_venue_ids_rejected_by_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = write(tmp, GOOD, "a.json")
            b = write(tmp, GOOD, "b.json")
            out = os.path.join(tmp, "out.c")
            self.assertEqual(2, gen_tracks.main([a, b, "-o", out]))

    def test_validation_survives_dash_O(self):
        """-O strips `assert`; the committed generator must still refuse a bad venue."""
        with tempfile.TemporaryDirectory() as tmp:
            bad = json.loads(json.dumps(GOOD))
            bad["layouts"][0]["sf"] = [[-34.0, 18.5], [-34.0, 18.5]]
            p = write(tmp, bad)
            out = os.path.join(tmp, "out.c")
            r = subprocess.run([sys.executable, "-O", "tools/tracks/gen_tracks.py", p, "-o", out],
                               capture_output=True, text=True)
            self.assertEqual(2, r.returncode, r.stdout + r.stderr)
            self.assertIn("degenerate", r.stdout + r.stderr)

    def test_the_committed_venues_validate(self):
        for f in ("tools/tracks/killarney.json", "tools/tracks/zwartkops.json"):
            with open(f) as fh:
                gen_tracks.expand(json.load(fh))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 6: Run it to verify it fails**

Run: `python3 -m unittest tools/tracks/test_gen_tracks.py -v`
Expected: `AttributeError: module 'gen_tracks' has no attribute 'GenError'` for the rejection cases; `test_validation_survives_dash_O` fails (rc 0, no message — `-O` strips every `assert`); `test_duplicate_venue_ids_rejected_by_main` fails (`main()` returns `None` and raises `AssertionError`).

- [ ] **Step 7: Replace every `assert` with an explicit check**

In `tools/tracks/gen_tracks.py`:
```python
class GenError(Exception):
    """A venue document that violates §10.2's schema. Raised instead of `assert` so validation
    survives `python3 -O` (drv:L24)."""


def need(cond, msg):
    if not cond:
        raise GenError(msg)
```
Then rewrite every `assert X, "msg"` as `need(X, "msg")` (and every bare `assert X` as `need(X, "<describe it>")`) in `check_line`, `expand`, `c_str` and `main` — including the two `assert k > 0` guards, which become
`need(k > 0, f"{v['name']}: layout 0 cannot use \"same\"/\"reverse\" (there is no previous layout)")`,
and `need(len(secs) <= MAX_SECTORS, f"{v['name']}: {len(secs)} sectors > {MAX_SECTORS}")`,
`need(len(s) < n, f"string too long for a char[{n}] field: {s}")`,
`need(len(ids) == len(set(ids)), f"duplicate venue ids {sorted(ids)}")`.
Wrap `main`'s body:
```python
def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args(argv)
    try:
        venues = [expand(json.load(open(f))) for f in sorted(a.files)]
        ids = [v["id"] for v in venues]
        need(len(ids) == len(set(ids)), f"duplicate venue ids {sorted(ids)}")
        with open(a.output, "w") as out:
            emit(venues, out)
    except GenError as e:
        print(f"gen_tracks: {e}", file=sys.stderr)
        return 2
    print(f"wrote {a.output}: {len(venues)} venues")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 8: Run the test and re-prove the generated file is unchanged**

Run:
```bash
python3 -m unittest tools/tracks/test_gen_tracks.py -v
python3 tools/tracks/gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c
git diff --exit-code components/core/tracks/trk_bundled.c
```
Expected: 7 tests OK; the regenerated `trk_bundled.c` is byte-identical (`git diff --exit-code` rc 0) — the same assertion the `tracks-generated` CI job makes.

### 5c — `core_selftest` exclusions (drv:L25)

- [ ] **Step 9: Narrow the exclusion regex to what truly cannot run on target**

Checked, file by file: `test_epd_pure.c`, `test_epd_pure_29.c`, `test_sim_profile.c` and `test_sim_clock.c` do **no** file I/O (`grep -n "fopen\|fwrite\|snapshot\|pbm"` finds nothing in any of them) — they were excluded only because they link a driver's `host/*.c` sources the core-only selftest did not have on its path. `test_ui.c`/`test_screens.c` genuinely cannot run on target (they write PBM snapshots to the host filesystem and compare byte-exact). `test_epd_pure_29.c` must stay out for a different, real reason: it needs `epd_panel.c` compiled with `EPD_FORCE_PANEL_29`, while `test_epd_pure.c` needs the same TU compiled with `EPD_FORCE_PANEL_213`, and one component compiles that source once.

In `test_apps/core_selftest/main/CMakeLists.txt` replace the exclusion comment + regex with:
```cmake
# test_ui and test_screens are host-only: they render PBM snapshots to the host filesystem
# (TEST_DIR/snapshots) and compare byte-exact, which cannot run on the ESP32.
# test_epd_pure_29 is excluded for a different reason (audit drv:L25, 2026-10-13): it needs
# host/epd_panel.c compiled with EPD_FORCE_PANEL_29, while test_epd_pure needs the same single TU
# compiled with EPD_FORCE_PANEL_213 -- a component compiles that source once, so only the owned
# 2.13" panel row is proven on the target toolchain here. Everything else that only needs Unity
# plus a pure host/*.c source now runs on target: test_epd_pure (panel table, portrait rotation,
# RAM-window rounding) and test_sim_profile / test_sim_clock (sim drag profile + gps_us clock).
list(FILTER TEST_SRCS EXCLUDE REGEX "/test_(ui|screens|epd_pure_29)\\.c$")
```
and extend the component registration to carry those pure sources:
```cmake
set(EPD_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/drivers/display_epaper_ssd1680")
set(SIM_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/drivers/sim_common")
set(LT_HAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../components/lt_hal")
idf_component_register(SRCS "main.c" "${TEST_DIR}/assert_support.c" ${WRAPPERS}
                            "${EPD_DIR}/host/epd_panel.c" "${EPD_DIR}/host/epd_rotate.c"
                            "${SIM_COMMON_DIR}/host/sim_profile.c"
                            "${SIM_COMMON_DIR}/host/sim_clock.c"
                       INCLUDE_DIRS "." "${TEST_DIR}" "${APP_INCLUDE_DIR}"
                                    "${EPD_DIR}/include" "${SIM_COMMON_DIR}/include"
                                    "${LT_HAL_DIR}/include"
                       REQUIRES core unity_vendored pthread esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RING_STRESS_N=20000ULL EPD_FORCE_PANEL_213=1)
```

- [ ] **Step 10: Build the selftest app and run the host suite**

Run:
```bash
source tools/idf-env.sh
idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build set-target esp32 build
```
Expected: rc 0, **0 warnings**, and the generated `suites_gen.h` now contains `SUITE(epd_pure)`, `SUITE(sim_profile)`, `SUITE(sim_clock)` (`grep SUITE test_apps/core_selftest/build/suites_gen.h`).
If a newly included suite fails to compile on the Xtensa toolchain, restore exactly that one file to the exclusion regex **with a one-line comment naming the compiler error** — never widen the regex back to its old shape, and record the error in the task report so Task 14 can file it.

- [ ] **Step 11: Commit**

```bash
git add tools/sign_release.sh tools/test_sign_release.sh tools/tracks/gen_tracks.py \
        tools/tracks/test_gen_tracks.py test_apps/core_selftest/main/CMakeLists.txt
git commit -m "tools: sign_release refuses sim + dirty and runs the size gate (+ dry-run harness); gen_tracks validates without assert (-O safe); core_selftest runs the pure e-paper and sim suites on target"
```

- [ ] **CHECKPOINT — Task 5**

```bash
printf 'Task 5 complete %s — sign_release guards + harness, gen_tracks, selftest suites\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 6 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 6: `test/test_trk.c` — record-decoder cases for the hostile-file paths (PR #101 M6) — PARALLEL

**Files:**
- Modify: `test/test_trk.c` (additions only; the existing 22 cases are untouched)

**Interfaces:**
- Consumes: `core/trk.h`'s `trk_user_save_venue(uint8_t index, uint8_t *buf, size_t cap, size_t *n_out)`, `trk_user_load_venue(const uint8_t *buf, size_t n)`, `TRK_USER_REC_MAX`, `TRK_USER_VENUE_HDR_LEN`, `TRK_USER_LAYOUT_HDR_LEN` (all shipped by F0/PR #101).
- Produces: five new Unity cases registered in `main()`. Nothing else depends on them.

**Ruling (design §5, PR #101 M6):** the file-level framing (`"LTRK"` magic, version byte, `count`, per-record `u16 len` + `u16 crc16`) lives in `components/app/logger/logger.c`, which is **F2b's file and has no host harness**. F4 writes the hostile-input tests it can write — against the pure record decoder `trk_user_load_venue` — and Task 14 files the issue for the file-level loader harness.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_trk.c`, before `main()`:
```c
/* PR #101 review M6 (2026-10-13): the record decoder is the only part of the /tracks/user.bin
 * path with a host harness, so every hostile byte pattern that CAN be expressed at record level
 * is pinned here. The FILE level (magic, version, count, per-record len/crc16) is logger.c's and
 * has no harness -- see the issue filed by the audit fix wave's F4 plan. */
static void test_user_load_venue_rejects_trailing_garbage(void)
{
    seed_one_user_venue();                       /* same helper the round-trip cases use */
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    int before = trk_user_count();
    s_blob[n] = 0x5A;                            /* one byte past the record's own fields */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, n + 1));
    TEST_ASSERT_EQUAL_INT(before, trk_user_count());
}

static void test_user_load_venue_rejects_n_layouts_over_cap(void)
{
    seed_one_user_venue();
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    s_blob[TRK_USER_VENUE_HDR_LEN - 1] = (uint8_t)(TRK_MAX_LAYOUTS + 1);   /* n_layouts byte */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, n));
}

static void test_user_load_venue_rejects_n_sectors_over_cap(void)
{
    seed_one_user_venue();
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    /* layout 0's n_sectors sits after the venue header and the layout's id/name/sf/dir_sign */
    const size_t n_sec_off = TRK_USER_VENUE_HDR_LEN + (2 + 24 + 4 * 8 + 1);
    s_blob[n_sec_off] = (uint8_t)(LAP_MAX_SECTORS + 1);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, n));
}

static void test_user_load_venue_rejects_venue_id_zero(void)
{
    seed_one_user_venue();
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    int before = trk_user_count();
    s_blob[0] = 0; s_blob[1] = 0;                /* id 0 -- trk_validate_venue rejects it */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load_venue(s_blob, n));
    TEST_ASSERT_EQUAL_INT(before, trk_user_count());
}

static void test_user_load_venue_terminates_an_unterminated_name(void)
{
    seed_one_user_venue();
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save_venue(0, s_blob, TRK_USER_REC_MAX, &n));
    for (size_t i = 2; i < 2 + 32; i++) { s_blob[i] = 'A'; }   /* 32 bytes, no NUL */
    TEST_ASSERT_EQUAL_INT(0, trk_user_load_venue(s_blob, n));
    const trk_venue_t *v = trk_get(SEEDED_USER_ID);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_size_t(31, strlen(v->name));             /* defensively terminated */
}
```
Register them in `main()` after `RUN_TEST(test_sim_reserved_id_excluded_from_save_and_rejected_on_load);`:
```c
    RUN_TEST(test_user_load_venue_rejects_trailing_garbage);
    RUN_TEST(test_user_load_venue_rejects_n_layouts_over_cap);
    RUN_TEST(test_user_load_venue_rejects_n_sectors_over_cap);
    RUN_TEST(test_user_load_venue_rejects_venue_id_zero);
    RUN_TEST(test_user_load_venue_terminates_an_unterminated_name);
```
`seed_one_user_venue()` / `s_blob` / `SEEDED_USER_ID`: reuse the file's existing fixture helpers and statics — read `test_user_save_venue_load_venue_round_trip_1_to_max` and name them exactly as that test does; if it builds its venue inline, extract that inline setup into `seed_one_user_venue()` (returning the id) and have the existing test call it, so there is one fixture builder and no duplication.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel && ctest --test-dir test/build -R test_trk --output-on-failure`
Expected: compilation fails until `seed_one_user_venue()` exists, then the five run. Three must pass immediately (the decoder already enforces `off != n`, `n_layouts > TRK_MAX_LAYOUTS` and `trk_validate_venue`) — **that is the point**: these are characterisation tests for an untested, security-relevant path, so a GREEN first run is the expected outcome for those three and must be reported as such, not "fixed". The n_sectors and name cases prove the two behaviours nothing else pins.

- [ ] **Step 3: Run the whole suite**

Run: `ctest --test-dir test/build --output-on-failure`
Expected: 100 % pass, 5 more cases in `test_trk`.

- [ ] **Step 4: gcc parity**

Run: `CC=$(ls /opt/homebrew/bin/gcc-* | head -1) cmake -S test -B test/build-gcc -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build-gcc && ctest --test-dir test/build-gcc --output-on-failure`
Expected: identical result, 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add test/test_trk.c
git commit -m "test(trk): hostile-record cases for trk_user_load_venue (trailing garbage, n_layouts/n_sectors over cap, id 0, unterminated name) -- PR #101 M6, record level"
```

- [ ] **CHECKPOINT — Task 6**

```bash
printf 'Task 6 complete %s — five hostile-record cases for trk_user_load_venue\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 7 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 7: README + `docs/measurements.md` (R31) — PARALLEL

**Files:**
- Modify: `README.md`, `docs/measurements.md`

**Interfaces:**
- Consumes: the merged PR bodies of #76, #77, #88, #89, #90, #94, #101 (`gh pr view <n> --json body`).
- Produces: the `docs/measurements.md` section names (`Bench day 1`, `1b`, `2`, `3`, `4`, `5`) that Task 10's roadmap lines point at.

- [ ] **Step 1: Rewrite `README.md`**

Replace the whole file with (every claim below is checked against the tree: no BLE build, `components/lt_hal/`, no web client, `build.sh` present, nine required checks after Task 14):
```markdown
# lap-timer

ESP32 GPS + IMU lap timer for track days and drag runs. Battery powered, fully offline. Sessions
are exported over the serial console today (VBO / NMEA / JSON / raw log); BLE export to RaceChrono
is designed (spec §18.2) but not built — it is sub-project C.

- Design spec (source of truth): `docs/superpowers/specs/2026-09-14-lap-timer-design.md`
- Roadmap and session plans: `docs/superpowers/plans/`
- Bench results and resource measurements: `docs/measurements.md`

## Layout

| Path | What |
|------|------|
| `components/core/` | pure C11 algorithm core (timebase, geodesy, fusion, lap/drag engines, tracks, session codec, exporters, config, renderer), no ESP-IDF, host-tested |
| `components/lt_hal/` | hardware interface headers (`hal/gps.h`, `imu.h`, `display.h`, `storage.h`, `board.h`, `conn.h`); named `lt_hal` because ESP-IDF ships a built-in component called `hal` |
| `components/drivers/` | one driver per HAL interface, selected at build time (`gps_neo6m`, `gps_sim`, `imu_mpu6050`, `imu_sim`, `sim_common`, `display_epaper_ssd1680`, `storage_internal`, `export_serial`, `board_devkit_v1`) |
| `components/app/` | ESP-IDF tasks and glue (`pipeline`, `logger`, `ui`, `supervisor`, `cmd`, `ota`, `link`, `sys`) |
| `devcontroller/` | the removable dev-kit firmware (sub-project B): WiFi AP, web UI, OTA push, session offload |
| `test/` | host unit tests (CMake + Unity) and the byte-exact PBM screen goldens |
| `test_apps/core_selftest/` | the same host tests run on the ESP32 |
| `tools/` | linters, track/font generators, replay + synth, dev-kit and OTA host CLIs, release signing |

## Host tests

Unity lives in `test/unity` as a submodule, so after cloning:

    git submodule update --init --recursive

Then:

    cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build && ctest --test-dir test/build --output-on-failure

CI compiles with gcc; run the same with `CC=$(ls /opt/homebrew/bin/gcc-* | head -1)` and
`-B test/build-gcc` before opening a PR (`brew install gcc` once).

Before a PR, also run what CI gates:

    git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin' ':!components/core/include/core/jsmn.h'
    python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
    python3 tools/lint/venue_announce.py --fail-on-violation
    python3 tools/spec_api_check.py
    python3 -m unittest tools/test_devkit.py tools/lint/test_power_of_10.py tools/lint/test_venue_announce.py tools/test_spec_api_check.py tools/tracks/test_gen_tracks.py

## Firmware

Prerequisites: `brew install cmake ninja python@3.12`, ESP-IDF v5.3.2 at `~/esp/esp-idf-v5.3.2`
(see `tools/idf-env.sh`; run `source tools/idf-env.sh` first — never a raw 5.5.x toolchain).

    ./build.sh moto_sim build                      # bench build (sim GPS + IMU); see build.sh for all environments
    ./build.sh moto_neo6m build                    # production environment (real-sensor drivers are Plan 8 stubs today)
    ./build.sh moto_neo6m flash-monitor --yes --port /dev/cu.usbserial-0001

`PANEL=ws29v2 ./build.sh moto_sim build` builds the 2.9" panel variant; the default `ws213v4` is
the owned hardware. With the dev-kit wired, USB flashing fails (shared UART0 RX) — push over the
dev-kit instead (`tools/ota_push.py`, `tools/devkit.py`), and never open the lap-timer's port
within 35 s of an OTA push (the image is still in its validation trial window).

Disconnect the battery pack before connecting USB (spec §3.2).

## Hardware

Prototype parts, quantities, order status and South African sources:
[docs/hardware/bom.md](docs/hardware/bom.md). Pin map and electrical details: spec §3.

## Branching

`main` is always releasable. One branch per roadmap session, named `s<plan>.<day>-<topic>`, merged
by squash PR once CI is green. Tags `pNN-dD` mark finished sessions, `plan-NN-done` finished plans,
`vX.Y.Z` releases.

`main` is protected: pull requests only, linear history, no force pushes, enforced for admins, and
nine required checks — `hygiene`, `host-tests`, `dc-host-tests`, `tools-tests`, `tracks-generated`,
`static-analysis` (the Power-of-10 gate), `build (moto_neo6m)`, `build (moto_sim)`,
`build (devcontroller)`.

## License

Source-available under the [PolyForm Strict License 1.0.0](LICENSE): noncommercial use only, no
distribution. Copyright (c) 2026 Bennie Swanepoel.
```

- [ ] **Step 2: Add the missing bench days to `docs/measurements.md`**

Insert the following **above** the existing `## moto_neo6m UI task boot on ESP32 (plan 04, session 4.3 …)` section (the file is newest-first). Each quotation is verbatim from the merged PR body named in its metadata line, fetched with `gh pr view <n> --json body`.

````markdown
## Bench day 5 — PR #101 fix re-test on the 2.13" panel (2026-10-10)

- Image `v0.1.0-47-g4ae6426` (`glyphs-drag-menu-closure` head), `moto_sim`, dev-kit wired. Source: PR #101 body, "Bench day 5 (2026-10-10) — the re-test of those fixes".
- > **All four bench-day-4 fixes pass on hardware:** the DRAG card's four states (#95); the gate list's label/value pairing, units and the `Speed:`/`Distance:` names (#96); a `Distance:` flip keeps the live run while a `Speed:` flip clears it and leaves the card at `NOT READY` (proven while the sim was moving, where the engine cannot arm, so `READY` could only have been the stale-snapshot bug); and the sim-boot venue announce (#98): `Layout:` cycles `Auto → Full → Auto` in words, the post-reset VENUE one-shot reads `Synthetic` / `Full`, and the footer BEST survives the cycling.
- > **#97 New track** passed its main path on the log (one-shot opens and advances per mark, the engine finishes on the closing S/F crossing and immediately times laps on the created one-sector layout); the on-screen `Track_20260915` reading and the item's secondary sub-checks remain […]. **#99 LINK glyph** is the one check not yet run: it needs the operator to unplug the dev-kit connector.
- Pre-existing findings filed the same day, none caused by that branch: #104 (`free_kb` read 0 with one open session, 472 KB after a remount), #105 (orphaned `.sum.tmp` never reaped), #106 (console INFO logging went silent until a restart), #107 (no track-table readback on the console).
- Resources: `moto_sim` free static DRAM 4072 B.

## Bench day 4 — glyphs / drag card / menu (2026-10-07)

- Image `v0.1.0-37-g4153025`, `moto_sim`, 2.13" V4 panel. Source: PR #101 body, "Bench day 4 (2026-10-07) and the fixes it forced".
- > **Passed:** the OTA progress screen (UPDATING -> VERIFYING -> REBOOTING), the rollback screen after a crash inside the trial window, the SIM/MOVING glyphs, a full drag run through `dbg sim drag` (11 gates, 224 km/h trap), a 36-minute lap soak (69 laps, no resets), storage-full behaviour, an OTA push from recovery mode, and the `lt list` timing baseline.
- > **Found four defects in this branch's own work** […] B4-F1 the DRAG card read its text from a one-bit "armed" mirror of a four-state engine […] B4-F2 every config reload re-initialised the drag engine […] B4-F3 the ui learned the venue only from the engine's venue *scan* […] B4-F4/F5/F6 the gate list drew each label far from its value […]
- Resources: `moto_sim` free static DRAM 4080 B at the gate, 4072 B after the fix wave (−8 B, one struct tail pad).

## Bench day 3 — OTA screen, glyphs, sim scenarios; tag `p07d-d1` (2026-10-05)

- Lap-timer `v0.1.0-35-gce59b01`, dev-kit `076f327`, `moto_sim`, 2.13" V4 panel. Source: PR #94 body, "Bench day 3 — PASS (2026-10-05 … tag `p07d-d1` → ce59b01)". Merged 2026-10-05.
- > **8 OTA screen**: second push (received by this image): UPDATING with bar/percent, RECEIVING → VERIFYING → REBOOTING seen […]
- > **2 OTA rollback**: `dbg crash` at ~10 s of the fresh boot → next boot `reset reason: panic`, errlog `E_OTA_ROLLBACK`, "rolled back by the bootloader", the deferred UPDATE FAILED / REVERTED one-shot right after BOOT (`scr=2 … kind=F`); seen on the display.
- > **9 glyphs**: SIM always on; MOVING on while the sim laps, gone at park […] **1 drag end-to-end**: […] `pipe: DRAG run 1 gates=11 trap=6234 cm/s` (profile vmax 223 km/h) […]
- > **4 soak**: 36 min with `dbg sim laps 20`: 69 laps, 2138 partials, 18 fulls (one per replay wrap = the still-deferred counter full), 0 resets, 0 W/E […]
- > **7 storage full** (in passing during the soak): `evicted S00540_001.log (free 124/1344 KB)`, `E_STO_FULL`, logging continued, no stall. **3 OTA in recovery**: 5× `dbg crash` → SAFE (#545) → RECOVERY (#546, console alive) → push succeeded, "safe/recovery gate cleared", normal boot #547, validated. **10** `lt list` 5.5–6.0 s with 52 sessions (≈105 ms/session, inside the 12 s window) — #91 baseline.
- Resources: host suite 47/47; lint 0; gcc-16 sweep 0/105 TUs; `moto_sim` free static DRAM 4312 → 4192 B (−120 B; recorded as the new baseline).
- Incident (not a firmware defect): the dev-kit boot-looped with `invalid header: 0xffffffff` while its flash read back intact — a GPIO12/MTDI strapping symptom from a bench wiring change; cleared once the wiring was checked.

## Bench day 2 — display closure + debt sweeps A/B; tags `p07c-d1`, `dsA-d1`, `dsB-d1` (2026-10-04/05)

One bench session covered three stacked branches on image `v0.1.0-102-g3db2231` (`moto_sim`, 2.13"
V4 panel, dev-kit wired). Sources: PR #88, #89 and #90 bodies (all merged 2026-10-05).

- **`p07c-d1` — PASS (PR #88, tag → 3db2231).**
  > **boot**: four BOOT lines; the one-shot→riding refresh is `kind=F` (ruling B-12); 0 W/E lines. **lap**: 3 timed laps (28.071/28.044/28.028 s); card with LAST/BEST/delta + BEST tag; page 1 sectors + THEO; page 2 MAX SPD 119 km/h, LEAN, LAT G, LAPS; Units→mph from the device menu → `MAX SPD mph` ≈74 […] User: "all pages look right, no ghosting". **clock**: CUR appears at the out-lap end; 1 Hz partials with the cell-sized box (104,89..128,107 / 88,89..120,107 on the tens digit). **soak** 10 min: 589 partials ≈1 Hz, 0 W/E, no WDT/reset.
  > **Bench findings** (all fixed on the stacked branch #90 […]): B-F1 a screen replacement while moving went out as a partial (426e7ff); B-F2 the partial RAM window was not mirrored like the pixel rotation → sub-rect partials hit the wrong columns (5ea298d); B-F3 the SSD1680 ping-pongs its image RAMs after a partial update → stale menu carets (f4eec2c, ef4c5db, 3db2231).
  Resources at the gate: host suite 43/43, lint 0, gcc-16 0/97 TUs, `moto_sim` free static DRAM 4416 B.
- **`dsA-d1` — PASS (PR #89, tag → 3db2231).**
  > **#37** first boot on the new blob format: 3× `E_NVS_BLOB_RESET` (args 1/2/3) in the errlog at boot #514; no `nvs blob` lines on any later boot. **#59** OTA push: `session S00525_001 closed (reason 1)` logged before the reboot […] **#73/#60** `lt delete S00491_001` → OK; again → remote 0x0703 no such session; `lt delete ../x` and `lt open ../x vbo` → ERR bad id […] **#62** 6× `dbg crash` 12 s apart: SAFE MODE at boot 532 […], RECOVERY MODE at boots 533/534 (pipeline, GPS power and ui not started; console alive, `lt status` answers), `dbg safe clear` + reset → normal boot 535. **#64/#66** `lt status` round trip 44 ms rc 0; stream flowing.
  Resources: host 44 + dev-kit 27 executables green; `moto_sim` free static DRAM 3960 B.
- **`dsB-d1` — PASS (PR #90, tag → 37c8368; lap-timer image `v0.1.0-102-g3db2231`, dev-kit 37c8368).**
  > **#87 cfg**: device-menu units toggle → `config reloaded (units 1/0, mode 0)`; remote `lt config set` round-trips and reads back; mode flip lap↔drag via the dev-kit repaints without a reboot. **reset**: `dbg reset` clears LAST/BEST/laps/CUR (B-F4: the inverted BEST tag outlived the reset — fixed 076f327 […]). **ring**: the errlog's 6 entries identical before/after a reset […]
  > **Dev-kit** […] **#65** FAILED on the first try (Sessions/Config "very flaky, usually requiring a refresh"): the real cause (bench B-F8) is the lap-timer's `list`, which reads every session summary before its length-prefixed header — 3.0–3.6 s with 42 sessions — against the dev-kit's 3 s no-bytes abort (10 of 20 relays timed out in the log) […] Fixed in a83163d + 37c8368 (ruling B-18) […] Re-test: fresh loads work without a refresh. The lap-timer's O(sessions) list cost is #91.
  Resources: host 45 + dev-kit 27 executables green; `moto_sim` free static DRAM 3960 → 4312 B (−352 B, the error ring framed in place).

## Bench day 1b — glanceable riding screens; tag `p07b-d1` (2026-09-28)

- `moto_sim` on the 2.13" V4 panel. Source: PR #77 body (merged 2026-09-28).
- > Bench gate `p07b-d1` (2026-09-28, `moto_sim` on the 2.13" V4 panel, tag `p07b-d1`): event card shows `-0.01` in 64 px with the inverted `BEST` tag, `L4 S0`, LAST/BEST rows; sector board shows the last lap's deltas (they persist across the line — bench fix 1); stats grid and DRAG lists render (stats values are the unwired fields, roadmap #58); three sim runs with 0 errors and no task WDT. Photos in the ledger.
- Resources: host LT 38/38 and dev-kit 25/25, lint 0; DRAM `.bss` +56 B; flash `.rodata` ≈ +2.8 KB net.

## Bench day 1 — e-paper display bring-up; tags `p07-d1`/`p07-d2`/`p07-d3` (2026-09-27)

- `moto_sim` on the real 2.13" V4 panel, pushed over the dev-kit. Source: PR #76 body (merged 2026-09-28).
- > Bench (2026-09-27, `moto_sim` on the real panel via the dev-kit OTA push): boot screen, BOOT → VENUE → LAP one-shots; partials on page changes while moving, forced full at 20 partials, fulls on menu entry/exit and page changes while still; throttle limits partials to 1/30 s with no fulls; externally set `SYS_DISP_DEAD` self-heals; menu open / scroll / select / long-MODE exit / 30 s auto-exit; 10-minute soak with no task WDT; **measured full ≈ 3.2 s, partial ≈ 0.7 s end-to-end**.
- > Second 10-minute soak on the final-fix image: no task WDT; the throttle now defers instead of dropping (three suppressed page changes, the deferred refresh fired by itself 30.1 s after the last partial).
- > Ladder on hardware (tag `p07-d3` = `plan-07-done`): GPIO 35 held high → three `E_DISP_BUSY_TIMEOUT` (packed streak/errno), `E_DISP_DEAD`, flag bit 4, refresh attempts stop; wire restored → reinit within 300 s, flag cleared, full refresh. Boot with the pin held high → `E_DISP_DEAD` at 6 s, then recovery within 300 s once restored […]
- Resources: DRAM reclaim on `moto_sim` 392 B → ~8.2 KB free; host LT 38/38, dev-kit 25/25, lint 0.
- Carve-out recorded at the time: the real-button debounce check (§20.8) waited for the buttons to be wired (done 2026-09-29, #78).
````

Also correct the file's opening sentence, which currently says only "Newest first":
```markdown
Bench results recorded per the spec (§22.4) and roadmap session exit criteria. Newest first. Every
bench day from 2026-09-27 on is transcribed here from the merged PR body that recorded it (the PR
number and date are given per section) — the project's evidence lives in the tree, not only in
PR descriptions and git-ignored ledgers (audit 2026-10-13 §6 item 1).
```

- [ ] **Step 3: Verify and commit**

Run: `git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin' ':!components/core/include/core/jsmn.h'` (must print nothing), then re-read each new measurements quotation against `gh pr view <n> --json body` to confirm it is verbatim. The roadmap is **not** touched here — it is rewritten wholesale in Task 10, after F2b merges.
```bash
git add README.md docs/measurements.md
git commit -m "docs: bench days 1-5 transcribed into measurements.md from the merged PR bodies (evidence in the tree, audit §6), README rewritten to the real tree"
```

- [ ] **CHECKPOINT — Task 7**

```bash
printf 'Task 7 complete %s — README rewritten, bench days 1-5 in measurements.md\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 8 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 8: BT/NimBLE off — ~56 KB of reserved static DRAM (R33) — PARALLEL

**Files:**
- Modify: `sdkconfig.defaults`, `CMakeLists.txt` (the `CONN` block's comment only)

**Interfaces:**
- Consumes: nothing.
- Produces: the measured `dram0_0_seg` origin/length and the new free-static-DRAM figures for `moto_sim` and `moto_neo6m`. **Task 9 Step 3/Step 9 writes those numbers into spec §4.8 and §21.2** (same file, serial), and Task 10's roadmap Plan 5.2 line cites them.

**Why:** the ESP32 linker reserves the Bluetooth controller's DRAM whenever `CONFIG_BT_ENABLED` is
set, whether or not a single byte of BLE code is linked. Measured on `main` @ f4d4db2
(`build/moto_sim/laptimer.map:18973`): `dram0_0_seg 0x3ffbdb5c 0x0001e6a4 rw` — origin 56,668 B
above `0x3ffb0000` and length 124,580 B instead of 180,736 B. That 124,580 B is exactly the "total
DRAM" every build reports, so the project's four-years-of-4-KB-free crunch (#53, the September
DRAM-reclaim work, the `moto_sim` ui-stack reduction) has been fighting a reservation for a stack
that is not compiled in. `conn_ble` lives only on the parked `s5.2-conn-ble` branch.

- [ ] **Step 1: Record the baseline**

```bash
grep -n "dram0_0_seg" build/moto_sim/laptimer.map | head -1
source tools/idf-env.sh
CCACHE_DISABLE=1 ./build.sh moto_sim build | tail -20
CCACHE_DISABLE=1 ./build.sh moto_neo6m build | tail -20
```
Expected baseline to quote in the task report: `dram0_0_seg 0x3ffbdb5c 0x0001e6a4 rw`; free static
DRAM `moto_sim` 4192 B, `moto_neo6m` 5616 B (design §4's wave baselines).

- [ ] **Step 2: Turn the controller off and park its options**

In `sdkconfig.defaults` replace lines 34–41 with:
```
# --- Bluetooth: OFF (ruling R33, docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md) ---
# No BLE code is compiled on main: components/drivers/conn_ble does not exist here, CONN=ble
# appends no driver directory, and the NimBLE host is parked on branch s5.2-conn-ble. With
# CONFIG_BT_ENABLED=y the linker still reserves the BT controller's DRAM -- dram0_0_seg starts at
# 0x3ffbdb5c with length 0x1e6a4 (124,580 B) instead of 0x3ffb0000 / 0x2c200 (180,736 B), i.e.
# ~56 KB of static DRAM held for a stack that is not linked. CFG_HAS_BLE (build_config.h) and the
# hwid string are unchanged, so OTA compatibility (§19.7) is unaffected.
#
# SUB-PROJECT C / branch s5.2-conn-ble MUST restore the block below when it activates BLE, and
# re-measure: the DRAM headroom this reservation was consuming is now spent elsewhere.
CONFIG_BT_ENABLED=n
#CONFIG_BT_NIMBLE_ENABLED=y
#CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
#CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
#CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
#CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
#CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y
#CONFIG_BTDM_CTRL_PINNED_TO_CORE_0=y
```
In the top-level `CMakeLists.txt`, extend the `CONN` validation block's comment with:
`# CONN=ble sets CFG_HAS_BLE (hwid + §19.7 compatibility) but appends NO driver directory: no BLE`
`# code is built on main, and CONFIG_BT_ENABLED is n (sdkconfig.defaults, ruling R33). Sub-project C`
`# adds components/drivers/conn_ble and re-enables the controller.`

- [ ] **Step 3: Rebuild both environments and verify the segment moved**

```bash
rm -rf build/moto_sim build/moto_neo6m
source tools/idf-env.sh
CCACHE_DISABLE=1 ./build.sh moto_sim build && CCACHE_DISABLE=1 ./build.sh moto_sim size
CCACHE_DISABLE=1 ./build.sh moto_neo6m build && CCACHE_DISABLE=1 ./build.sh moto_neo6m size
grep -n "dram0_0_seg" build/moto_sim/laptimer.map | head -1
grep -n "dram0_0_seg" build/moto_neo6m/laptimer.map | head -1
```
Expected: `dram0_0_seg 0x3ffb0000 0x0002c200 rw` in **both** maps (origin `0x3ffb0000`, length
`0x2c200` = 180,736 B), rc 0 and **0 warnings** on all four commands, and a free-static-DRAM figure
~56 KB above the baseline for each env. Record the exact two numbers — they are the headline of this
plan's PR and the inputs to Task 9.
If the segment does **not** move, stop: `CONFIG_BT_ENABLED` is being re-set by an env fragment
(`grep -rn "BT_ENABLED" sdkconfig* main/Kconfig.projbuild build.sh CMakeLists.txt`) and the fix is
there, not in a second `sdkconfig.defaults` edit.

- [ ] **Step 4: Prove nothing else changed**

```bash
grep -n "CFG_HAS_BLE\|CFG_HWID" build/moto_neo6m/build_config.h
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation \
  --report-only-sites "$(sed -n 's/.*--report-only-sites "\(.*\)".*/\1/p' .github/workflows/ci.yml)"   # the grace string Task 4 put in ci.yml
ctest --test-dir test/build --output-on-failure
```
Expected: `CFG_HAS_BLE 1` and `CFG_HWID "moto_neo6m_epaper"` unchanged (so a device running an older
image still accepts this one); linter rc 0; host suite 100 %. A `conn_ble`-related compile error is
impossible — `grep -rn "nimble\|esp_bt" components/ main/` returns nothing on `main`; run it to
confirm before building, and if it returns a hit, that file's owning plan is told and this task
stops.

- [ ] **Step 5: Commit**

```bash
git add sdkconfig.defaults CMakeLists.txt
git commit -m "build: CONFIG_BT_ENABLED=n -- the BT controller reserved ~56 KB of static DRAM for a stack that is not linked (dram0_0_seg 0x3ffbdb5c/0x1e6a4 -> 0x3ffb0000/0x2c200); NimBLE options parked for sub-project C (R33)"
```

- [ ] **CHECKPOINT — Task 8**

```bash
printf 'Task 8 complete %s — CONFIG_BT_ENABLED=n; dram0_0_seg 0x3ffb0000/0x2c200\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 9 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

## Session 3 — spec, roadmap, blocking flip, issues (Tasks 9–12)

Serial, and gated on other plans merging: Task 9 needs F1a+F1b+F2a+F2b+F3, Tasks 10/11 need F2b, Task 12 needs F0. Stop for review after Task 12.

### Task 9: Spec reconciliation (R3, R14, R18, app:L8/M9/M10, drv:FC-7..12, L26) — SERIAL (LAST: after F1a/F1b/F2a/F2b/F3 merge)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (**only** §4.2, §4.6, §4.8, §5.2, §11.4, §17.2, §17.7, §17.8, §18.4, §19.1, §21.2, §21.3, §21.5, §21.6, §22.1, §22.3, §23, Appendix A), `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` (§5), `docs/power-of-10-checklist.md`
- Test: `python3 tools/spec_api_check.py`

**Interfaces:**
- Consumes: `tools/spec_api_check.py --emit core/<x>.h` (Task 3); Task 8's measured `dram0_0_seg` and free-DRAM figures (for §4.8/§21.2); the merged tree of F0+F1a+F1b+F2a+F2b+F3.
- Produces: a spec that `spec_api_check.py` passes. Nothing depends on it.

**Entry condition (hard):** F1a, F1b, F2a, F2b and F3 are merged to `main` and this branch is rebased onto that `main`. The sections those plans amend (§6.4, §10.1, §10.3, §12.x, §17.1, §19.4, §19.5, §20.5, §20.6, §20.7) must already carry their edits; **do not touch them.** Verify first: `git log --oneline main | head -8` shows all five, and `git diff main...HEAD -- docs/superpowers/specs/2026-09-14-lap-timer-design.md` is empty before this task starts.

- [ ] **Step 1: §4.2 — regenerate the repository layout from the tree**

Run and paste the result into the fenced block, keeping the existing explanatory inline comments for the entries that survive:
```bash
git ls-files | awk -F/ '
  NF==1 {print $0; next}
  /^components\/core\// {print $1"/"$2"/"$3; next}
  /^components\// {print $1"/"$2"/"$3; next}
  /^(main|tools|test|test_apps|devcontroller|docs)\// {print $1"/"$2; next}
  {print $1"/"$2}' | sort -u
```
Delete every path the audit (§3.3 item 17) proved absent: `fusion/fus_calib.c`, `session/ses_crc.c`, `ui/screens_car.c`, `drivers/gps_ubx_common`, `drivers/gps_m10`, `drivers/conn_ble`, `drivers/conn_wifi`, `drivers/conn_ble_rc`, `drivers/display_oled_ssd1309`, `drivers/storage_sd`, `app/power/`, `tools/web/export.html`, `tools/gps_sim.py`, `tools/serial_export.py`, `tools/log2csv.py`, `test/test_ses.c`, `test/test_exp.c`. Add what exists and is missing: `components/drivers/sim_common/`, `components/app/link/`, `components/app/sys/`, `devcontroller/`, `tools/lint/`, `tools/sim/`, `tools/devkit.py`, `tools/ota_push.py`, `tools/test_devkit.py`, `tools/spec_api_check.py`, `tools/idf-env.sh`, `test/snapshots/`. Head the block with:
`(Generated from `git ls-files`; regenerate with the command in §23 whenever a component or tool is added. Entries marked O* are designed but not built — see §24.)` and mark every not-yet-built driver/tool from §2.2's optional list with `O*` instead of listing it as if it existed.

- [ ] **Step 2: §4.6 — flags, defaults, the `build_config.h` sample, and the sim-driver sentence**

- `PANEL` default cell: `ws29v2` → `ws213v4` (both `CMakeLists.txt:24` and `build.sh:43` default to `ws213v4`; the 2.13" V4 is the owned panel).
- Add the missing row to the flag table, between `EXPORT_SERIAL` and `PANEL`:
  `| `DEVUX` | `ON`/`OFF` | `ON` |` with the note "the interactive operator UX (`dbg` verbs and their help) inside `export_serial`; OFF leaves the prod image with only the cmd + stream + OTA-receive transport."
- Replace the sim-driver sentence (drv:FC-3 — no `/sim/*` path exists anywhere in the tree) with:
  `\`GPS=sim\` and \`IMU=sim\` select bench drivers (\`gps_sim\`, \`imu_sim\`) that implement the HAL from a capture compiled into the image (\`components/drivers/gps_sim/sim_capture.h\`) and a synthetic profile shared by both (\`components/drivers/sim_common\`) — no filesystem path is involved. They exist so the whole firmware can be developed and bench-tested before the physical sensors are available. They are never part of a release build: \`tools/sign_release.sh\` refuses an environment or a \`build_config.h\` that selects them unless \`--allow-sim\` is passed (spec §21.5).`
- Replace the `build_config.h` sample with the real generated set (from `main/build_config.h.in`):
```c
#define CFG_VARIANT_MOTO   1        /* or CFG_VARIANT_CAR */
#define CFG_VARIANT_CAR    0
#define CFG_GPS_NAME       "neo6m"
#define CFG_DISPLAY_NAME   "epaper_ssd1680"
#define CFG_GPS_SIM        0        /* 1 when the GPS driver is gps_sim */
#define CFG_IMU_SIM        0        /* 1 when the IMU driver is imu_sim */
#define CFG_PANEL_WS213V4  1        /* default panel; CFG_PANEL_WS29V2 is the alternative */
#define CFG_PANEL_WS29V2   0
#define CFG_HWID           "moto_neo6m_epaper"   /* used by the OTA hardware-id check */
#define CFG_FW_VERSION     "v0.3.1-7-gabc123"
#define CFG_HAS_PPS        0
#define CFG_HAS_SD         0
#define CFG_HAS_WIFI       0
#define CFG_HAS_BLE        1
#define CFG_HAS_BLE_RC     0
#define CFG_HAS_EXPORT_SERIAL 1
#define CFG_HAS_DEVUX      1
#define CFG_FUSED_LOG_HZ   10
#define CFG_TRK_JSON_TOKS  64       /* jsmn scratch-token cap for trk_json.c */
```
- Add after the environment table: `\`CONN=ble\` sets \`CFG_HAS_BLE\` and the NimBLE Kconfig options, but no \`conn_ble\` component is built on \`main\` (sub-project C) — the flag is inert until that sub-project lands.`

- [ ] **Step 3: §4.8 — the two missing budget rows (app:L8)**

Add to the memory-budget table, after the `Logger batch buffer` row:
```markdown
| `cmd.c` statics (`s_sess[64]` ≈ 4.6 KB, `s_json` 3072 B, `s_exp` ≈ 1.5 KB, `s_sr` ≈ 0.8 KB, `s_cw` ≈ 0.5 KB) | ~10.5 KB |
| `link.c` peer stream ring (§18, the fused-log/event fan-out drained by `link_task`) | measured in `link.c`; counted against the 124 KB `.bss` ceiling |
```
and replace the `UART RX ring | 2 KB` row with `| Console UART0 RX ring (IDF `esp_console`, fixed 256 B — not tunable) | 256 B |`.
**R33 (from Task 8):** replace the `NimBLE host + controller (when active) | ~70 KB` row with
`| NimBLE host + controller | **0 B — `CONFIG_BT_ENABLED=n` since 2026-10-13 (ruling R33); ~56 KB was reserved by the linker for a controller no build links. Sub-project C restores it and re-measures.** |`
and append to the paragraph under the table:
`Until 2026-10-13 the linker reserved the Bluetooth controller's DRAM on every build — \`dram0_0_seg\` began at \`0x3ffbdb5c\` with length \`0x1e6a4\` (124,580 B) instead of \`0x3ffb0000\` / \`0x2c200\` (180,736 B) — so the "total DRAM" every build reported, and every free-DRAM figure in \`docs/measurements.md\` before that date, was measured against a 124,580 B ceiling. With \`CONFIG_BT_ENABLED=n\` the ceiling is 180,736 B; the figures measured after the change are in Task 8's PR line and in \`docs/measurements.md\`. The 124 KB internal-DRAM figure the project used to track is this table's \`.bss\` half against that old ceiling, not an IDF baseline.`

- [ ] **Step 4: §5.2 — regenerate every API block**

For each of the thirteen headers run `python3 tools/spec_api_check.py --emit core/<x>.h` **on the merged tree** and paste its output as that header's fenced block, then re-attach the trailing `/* … */` annotations the old block carried for lines that still exist (they are documentation, and the checker compares declarations only, so a trailing comment never causes drift). The nine blocks the audit named as wrong are `core.h`, `fus.h`, `lap.h`, `drag.h`, `ses.h`, `exp.h`, `json.h`, `cfg.h`, `trk.h`; `tb.h`, `geo.h`, `types.h` and `event.h` are regenerated by the same loop so an F1a `event.h` addition (`EV_VENUE_LOST`) cannot leave them behind.

Measured on `main` @ f4d4db2 (illustrative — re-run on the merged tree, the output is the source of truth), the emitted `core.h` block is exactly:
```c
const char *core_version(void);
void core_assert_report(uint16_t code, const char *file, int line);
void core_assert_fail(uint16_t code, const char *file, int line);
```
and `cfg.h`:
```c
int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p);
int cfg_defaults(cfg_t *c);
int cfg_validate(cfg_t *c);
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap);
int cfg_to_json(const cfg_t *c, char *out, size_t cap);
int cfg_migrate(cfg_t *c, uint8_t from_version);
bool cfg_migrate_supported(uint8_t from_version);
```
Replace §5.2's preamble paragraph with:
```markdown
The blocks below are the declarations exported by `components/core/include/core/`, **generated**
from those headers by `tools/spec_api_check.py --emit core/<x>.h`. The `hygiene` CI job runs
`python3 tools/spec_api_check.py`, which fails the build when a block lists a prototype, a
function-pointer typedef or a macro the header does not have, or is missing one the header exports,
or when a signature differs. Trailing `/* … */` annotations are documentation and are not compared.
Struct and enum bodies are deliberately not reproduced here — read the header.
```

- [ ] **Step 5: §11.4 — point at the implemented rule (io:FC-3)**

Replace the section body with:
```markdown
**Superseded by Plan 7b §7** (`docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md`),
which is what `core/ui/screens_moto.c` implements: the DRAG card shows the newest gate in the big
slot with a footer of earlier gates scrolling off the left, and pages 1/2 list the gates
hit-only / best-only in two columns with a `+n` overflow. The original rule below (ascending by
`a`, maximum 4 rows, lowest dropped first, a `1/4 --` row for a run that ends without the 1/4) was
never implemented and is retained only as the design history of the 2.9"-era screen:

> Rows shown on the riding screen: `SPEED_FROM0` gates in `benches` that were hit, ascending by
> `a`, followed by the 1/4 row. Maximum 4 rows; if more than 3 benches were hit, the lowest are
> dropped first. Rows appear as gates are hit (each hit triggers a partial refresh). A run ending
> without the 1/4 shows `1/4 --`.
```

- [ ] **Step 6: §17.2, §17.7, Appendix A — the firmware's real numbers (R3, app:M9/M10)**

- §17.2's closing sentence: `` `HB_STALL_S`: pipeline 1 s, logger 5 s, ui 10 s … `` → `` `HB_STALL_S`: pipeline **5 s** (`pipeline.c`'s `PIPE_STALL_S`), logger 5 s, ui 10 s (a full refresh may take 2 s), conn 10 s, power 5 s. The pipeline figure was 1 s in the original design; the firmware has shipped 5 s since Plan 03 and that is the operating reality (ruling R3, `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`). ``
- §17.7's `E_OTA_*` block: replace the six rows with the enum's real values (`components/app/include/app/lt_err.h:40-45`), i.e. `0x0801 E_OTA_PRECOND`, `0x0802 E_OTA_HWID`, `0x0803 E_OTA_WRITE`, `0x0804 E_OTA_SIG`, `0x0805 E_OTA_VALIDATED` (info), `0x0806 E_OTA_ROLLBACK`, and add below the table: `The \`E_OTA_*\` values above are the firmware's (\`lt_err.h\`); five of the six differed from this table until ruling R3 (2026-10-13) moved the spec to the code — these codes are already in the field and in the project's operating notes, and \`lt_err.h\` cites the ruling.`
- Appendix A: `| \`OTA_VALID_UPTIME_S\` | 60 |` → `| \`OTA_VALID_UPTIME_S\` | 30 | §19.4 (ruling R3: the 30 s trial window is the bench operating rule — never reset or open the console port within ~35 s of a push) |`; `| \`HB_STALL_S\` (pipeline/logger/ui/conn/power) | 1 / 5 / 10 / 10 / 5 |` → `| \`HB_STALL_S\` (pipeline/logger/ui/conn/power) | 5 / 5 / 10 / 10 / 5 | §17.2 (ruling R3) |`. Leave `LOG_PAUSE_STILL_S` and `MOVING_SPEED_KMH` alone — F2b and F1a own those rows' behaviour.

- [ ] **Step 7: §17.8 — the UART sentence (drv:FC-12)**

Replace `…PPS ISR \`IRAM_ATTR\`; UART RX ring 2 KB absorbs 500 ms at 38400 baud.` with
`…PPS ISR \`IRAM_ATTR\`. There is no GPS UART yet (\`gps_neo6m\` is a Plan 8 stub with no \`uart_driver_install\`); the one UART ring that exists is the IDF console's fixed **256 B** on UART0, which is not Kconfig-tunable and is why \`ota recv\` over USB is pace-sensitive (§18.4, #57).`

- [ ] **Step 8: §18.4 — full rewrite (drv:H4, FC-7, FC-8)**

Replace the section's bullets with:
```markdown
- UART0, **115200 baud** (`CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200`), IDF `esp_console` REPL with
  line editing disabled. The console's RX ring is a fixed 256 B (§17.8). Consoles use **CR** line
  endings: a host tool sends a bare `\r`.
- Commands (text), as registered by `components/drivers/export_serial/export_serial.c`:
  `status`, `list`, `open <id> <vbo|nmea|json|log|sum>`, `read <offset>`, `close`, `delete <id>`,
  `config get`, `config set <json>`, `errlog`, `errlog clear`, `diag`, `tracks get`,
  `tracks put <json>`, `coredump`, `coredump clear`, plus — only when `DEVUX=ON` — `ota recv` and
  the `dbg` family.
- `dbg` verbs: **take this list verbatim from the shipped help string** (`grep -n 'dbg <'
  components/drivers/export_serial/export_serial.c`), which F2b updates in the same wave as the
  `tracks`/`coredump` work. At the time of writing it is
  `dbg <status|logtest <n>|fs|sum <id>|logck <id>|laps|rtc|mem|btn <mode|up|down|up+down> [ms]|flag set|clear <bit>|safe clear|reset|sim <drag|laps <n>|park>|crash|hang>`.
- File output framing: `---BEGIN <name> <size>---\r\n` … raw text … `---END <crc32 hex>---\r\n`.
  Binary formats (log/sum/coredump) are Base64-encoded between the markers.
- ESP-IDF logging is set to **`ESP_LOG_NONE`** for the duration of an `open` transfer and of
  `ota recv`, and restored at `close`/END (ruling R9: at `ESP_LOG_ERROR` a stray `ESP_LOGE` from
  any task landed inside the framed body, which the announced size and CRC do not cover).
- **OTA over serial is the primary bench flash path**, not an absent feature: `ota recv` on the
  device plus `tools/ota_push.py` on the host (or the dev-kit's `flash push`, `tools/devkit.py`)
  stream a signed image into the inactive slot. `idf.py flash` / `esptool.py` remain the recovery
  path over USB — and with the dev-kit wired they fail on the shared UART0 RX unless the dev-kit is
  held in reset. After any push, do not reset the device or open its port for ≥ 35 s: the image is
  inside its `OTA_VALID_UPTIME_S` trial window and an early reset rolls it back (`E_OTA_ROLLBACK`,
  0x0806).
- There is no `tools/serial_export.py`; the host-side clients are `tools/devkit.py` and
  `tools/ota_push.py`, and a plain `screen`/`minicom` session works for the text commands.
```

- [ ] **Step 9: §19.1, §21.2, §21.3, §21.5, §21.6, §22.1, §22.3, §23 (drv:FC-9..11, L26, R14, L25)**

- §19.1 last sentence: `…a bigger flash module enlarges \`storage\` only (generated variant of the CSV via \`build.sh --flash-size 8MB\`). The app image MUST be ≤ 1.25 MB including the signature block; \`build.sh size\` fails the build above 1.2 MB to keep margin.` → `…a bigger flash module would enlarge \`storage\` only, but \`build.sh\` currently **refuses** \`--flash-size 8MB\`/\`16MB\` as not wired (\`build.sh:96-101\`) — 4 MB is the only supported size. The app image MUST be ≤ 1.25 MB including the signature block; \`build.sh size\` fails the build above **0x130000 bytes (1.1875 MiB)** to keep margin, and \`tools/sign_release.sh\` runs that gate on every release build.`
- §21.2: add to the `sdkconfig.defaults` listing, replacing its `CONFIG_BT_*`/`CONFIG_BT_NIMBLE_*` lines: `CONFIG_BT_ENABLED=n` plus the sentence `Bluetooth is **off** (ruling R33): no BLE code is compiled on \`main\`, and \`CONFIG_BT_ENABLED=y\` made the linker reserve ~56 KB of static DRAM for the controller (\`dram0_0_seg\` 124,580 B instead of 180,736 B). The NimBLE options are kept as a commented block for sub-project C, which must restore them and re-measure. \`sdkconfig.defaults.car\` and \`.moto\` differ in exactly one line each (the variant symbol) and are kept.` Then replace `differ only in \`CONFIG_LAPTIMER_VARIANT_*\` Kconfig symbols used by \`app/ui\`` with `differ only in the \`CONFIG_LAPTIMER_VARIANT_*\` Kconfig symbols. **No code reads them** — \`main/Kconfig.projbuild\`'s own help text says "menuconfig visibility only"; the authoritative variant switch is \`build_config.h\`'s \`CFG_VARIANT_*\`, generated from the build flags (§4.6).`
- §21.3: `\`size\` runs \`idf.py size\` and fails if the app exceeds 0x130000 bytes` — keep, and append `(not "1.2 MB"; the two differ by 7 KB)`. Add `\`--flash-size\` accepts only \`4MB\`; 8MB/16MB are refused as not wired.`
- §21.5: keep `Release builds require a clean tree (\`sign_release.sh\` refuses \`-dirty\`)` — Task 5 makes it true — and append `, refuses an environment or \`build_config.h\` that selects a sim driver unless \`--allow-sim\` is passed, and runs \`build.sh <env> size\` before signing.`
- §21.6: delete the `web` bullet entirely (there is no `tools/web/export.html` and no web client before sub-project C) and replace the section with the real job list:
```markdown
- `hygiene`: no trailing whitespace/CRLF in tracked text, no committed secrets or build output, the
  container tag matches `.idf-version`, and `tools/spec_api_check.py` (spec §5.2 vs the headers).
- `host-tests` / `dc-host-tests`: ubuntu, cmake + ctest with sanitizers, for `test/` and
  `devcontroller/test/`.
- `tools-tests`: `python3 -m unittest` over every `tools/**` test module, plus
  `tools/test_sign_release.sh`.
- `static-analysis`: the Power-of-10 linter (`--enforce-fnptr --fail-on-violation`, BLOCKING) and
  the venue-announce linter, with `cppcheck` and `clang-tidy` advisory (design §5).
- `tracks-generated`: regenerates `trk_bundled.c` and fails if it differs from the committed file.
- `coverage`: advisory line coverage of `components/core` via gcc `--coverage` + `gcovr`; prints the
  percentage into the job summary, never fails the build, never a required check.
- `firmware` (`build (<env>)`): matrix over `moto_neo6m`, `moto_sim` and `devcontroller` in
  container `espressif/idf:<pinned>`; runs `build.sh <env> build` and `size`, builds
  `test_apps/core_selftest` on the `moto_neo6m` leg, and uploads unsigned `.bin` artifacts. Adding
  a matrix entry requires updating the branch-protection required checks in the same change.

The required checks on `main` are `hygiene`, `host-tests`, `dc-host-tests`, `tools-tests`,
`tracks-generated`, `static-analysis`, `build (moto_neo6m)`, `build (moto_sim)` and
`build (devcontroller)`.
```
- §22.1 last line: `Coverage target: ≥ 90 % lines in \`core/\` (gcov in CI).` → `Coverage target: ≥ 90 % lines in \`core/\`. This is an **advisory** target, reported by the \`coverage\` CI job (gcc \`--coverage\` + \`gcovr\`, §21.6) and never a merge gate (ruling R14).` Also update the `test_ui.c` row: `bench rows rule (§11.4)` → `DRAG card and gate lists (Plan 7b §7)`, and the `test_ses.c`/`test_exp.c` rows' file names to the ones that exist (`test_ses_frame.c`, `test_ses_records.c`, `test_exp_vbo.c`, `test_exp_nmea_json.c`).
- §22.3: replace the closing paragraph's claim with `From plan 01 onward, \`test_apps/core_selftest\` also runs the host unit-test files on the ESP32 (vendored Unity as an IDF component; stress-test iteration counts reduced under \`ESP_PLATFORM\`), so every core change is proven on the target toolchain (xtensa alignment of packed records, \`_Atomic\` support, newlib formatting, float performance) before firmware plans build on it. Three files are excluded by name and cannot be: \`test_ui.c\` and \`test_screens.c\` write PBM snapshots to the host filesystem and compare byte-exact, and \`test_epd_pure_29.c\` needs the same translation unit compiled for the other panel row (the 2.13" row is the one proven on target). Everything else — including the pure e-paper geometry and the sim profile/clock — runs there.` Also replace the `GPS simulation` bench row's procedure (`tools/gps_sim.py` does not exist) with `\`moto_sim\` build; \`dbg sim laps <n>\` replays the compiled-in capture` and its pass column with `laps appear on the display matching the \`replay\` reference`.
- §23: rewrite the tool table to the tree (drv:L26):
```markdown
| Tool | Language | Purpose | Interface |
|------|----------|---------|-----------|
| `tools/replay/` | C | offline pipeline `replay` and synthetic fixture generator `synth` (§22.2) | CLI |
| `tools/tracks/gen_tracks.py` | Python 3 | `*.json` → `trk_bundled.c`; validates schema, lengths and the 10–60 m line rule explicitly (never via `assert`, so `python3 -O` validates too) | `python3 tools/tracks/gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c` |
| `tools/fonts/gen_fonts.py` | Python 3 (Pillow) | TTF → 1-bpp glyph arrays | `python3 tools/fonts/gen_fonts.py …` |
| `tools/devkit.py` | Python 3 (pyserial) | dev-kit console client: status, `lt <cmd>` relay, session/log download, image staging and OTA push | `tools/devkit.py --port /dev/cu.usbserial-XXXX status` |
| `tools/ota_push.py` | Python 3 (pyserial) | direct `ota recv` push of a signed image over the lap-timer console | `tools/ota_push.py --port … dist/laptimer-….bin` |
| `tools/sign_release.sh` | bash | clean-tree + no-sim guards, build, `size` gate, sign, verify, `dist/` (§19.2, §21.5) | `tools/sign_release.sh moto_neo6m` |
| `tools/lint/power_of_10.py` | Python 3 | the Power-of-10 gate (rules 1/3/4/4-compound/5/8/9) over `components/**`, `main/`, and `devcontroller/` report-only | `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` |
| `tools/lint/venue_announce.py` | Python 3 | every direct venue acquisition announces to the ui on that path | `python3 tools/lint/venue_announce.py --fail-on-violation` |
| `tools/spec_api_check.py` | Python 3 | spec §5.2's API blocks vs `components/core/include/core/*.h` | `python3 tools/spec_api_check.py` (`--emit core/lap.h` to regenerate a block) |
| `tools/idf-env.sh` | bash | sources the pinned ESP-IDF (asserts `.idf-version`) | `source tools/idf-env.sh` |

Python tools pin dependencies in `tools/requirements.txt` (`pyserial`, `Pillow`, `esptool`); every
linter and generator is standard-library only and is unit-tested in the `tools-tests` CI job.
The §4.2 layout block is generated — regenerate it with the `git ls-files` command recorded there.
```

- [ ] **Step 10: P10 design §5 and the checklist (R12)**

In `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` §5, replace the custom-linter bullet and the rollout bullet with what the linter now actually does:
```markdown
- **A small custom linter** (`tools/lint/power_of_10.py`) for the project-specific gates automated
  tools do not cover well. As amended by the 2026-10-13 audit fix wave (ruling R12) it gates:
  **rule 1** direct recursion (a function whose body calls its own name); **rule 3** the full heap
  API surface this codebase could reach (`malloc`/`calloc`/`realloc`/`free`/`strdup`/`strndup`/
  `asprintf`/`vasprintf`/`aligned_alloc`/`heap_caps_*alloc`/`heap_caps_*free`/`pvPortMalloc`/
  `vPortFree`) — **blocking**, which it was not before; **rule 4** functions over 60 code lines and
  compound-statement bodies over 30; **rule 5** ≥ 2 assertions in any function over 20 code lines;
  **rule 8** the preprocessor gates; **rule 9** unregistered function pointers — the `(*name)(`
  declarator *and* a function passed by value to a known sink (`qsort`, `bsearch`, `xTaskCreate*`,
  `esp_timer_create`, `gpio_isr_handler_add`, `esp_console_cmd_register`, `spi_bus_add_device`)
  *and* a function stored into a struct field or variable. Two counting rules make rules 4/5
  un-evadable: an `#if 0` region is blanked before anything is counted (an assertion in dead code
  never counts, and an unbalanced brace there can no longer desynchronise the scanner), and a
  body's size is `max(physical code lines, top-level statements)`, so `a; b; c;` on one line counts
  three. Scope is `components/core`, `components/app`, `components/drivers`, `main` **and**
  `devcontroller/` — the last report-only, since the dev-kit firmware was never retrofitted. It
  reads the deviation register.
- **Rollout.** The analysers ran report-only through the retrofit, then flipped to blocking. A
  newly added or tightened check follows the same two-step rollout: it lands with its known
  pre-existing sites listed in `--report-only-sites path:symbol:RULE` (or, coarsely,
  `--report-only-rules`) in the CI invocation, each entry naming the plan that owns the file; the
  linter prints `GRACE-STALE` for an entry that stops matching, and the flag is deleted in a
  separate, final task once every owner has merged. The blocking gates are the custom linter
  (`--enforce-fnptr --fail-on-violation`) and the compiler `-Werror` set; `clang-tidy` and
  `cppcheck` stay advisory (rule 10's "preferably more than one analyser"). `static-analysis` **is**
  a branch-protection required check as of 2026-10-13 (ruling R13) — it was not when
  `plan-4.5-done` was tagged.
```
In `docs/power-of-10-checklist.md`, update the header sentence's automated half to name rules 1 and 3 as linter-gated, and add to the Rule 1 line: `(the linter catches direct recursion; indirect recursion is still yours to check)`; to the Rule 2 line: `(nothing automated checks loop bounds — this line is the only gate)`; to the Rule 9 line: `(the linter now also sees a function passed to a sink or stored in a struct field)`.

- [ ] **Step 11: Verify the spec against the tree**

Run:
```bash
python3 tools/spec_api_check.py; echo "rc=$?"
python3 -m unittest tools/test_spec_api_check.py -v     # remove the expectedFailure decorator first
grep -n "921600\|serial_export.py\|gps_sim.py\|log2csv.py\|tools/web/export.html\|fus_calib.c\|ses_crc.c\|screens_car.c" docs/superpowers/specs/2026-09-14-lap-timer-design.md
```
Expected: `spec_api_check: 0 drift(s) across 13 §5.2 blocks`, rc 0; 3 unittest tests OK; the `grep` prints **nothing** (every falsified name is gone). Then confirm the untouched-section rule:
`git diff main...HEAD -- docs/superpowers/specs/2026-09-14-lap-timer-design.md | grep -E "^@@" ` — every hunk must fall inside the sections this task owns; if a hunk lands in §6.4/§10.x/§12.x/§17.1/§19.4/§19.5/§20.x, revert it.

- [ ] **Step 12: Commit**

```bash
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md \
        docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md \
        docs/power-of-10-checklist.md
git commit -m "spec: reconcile §4.2/§4.6/§4.8/§5.2/§11.4/§17.2/§17.7/§17.8/§18.4/§19.1/§21.x/§22.x/§23/Appendix A with the merged tree; §5.2 blocks now generated and CI-gated (rulings R3, R14, R18)"
```

- [ ] **CHECKPOINT — Task 9**

```bash
printf 'Task 9 complete %s — spec sections reconciled; spec_api_check 0 drift\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 10 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 10: Roadmap rewritten as a state document (R31) — SERIAL (after F2b merges)

**Files:**
- Modify: `docs/superpowers/plans/2026-09-14-roadmap.md` (whole-file rewrite)

**Interfaces:**
- Consumes: Task 7's `docs/measurements.md` section names; Task 8's DRAM figures; design §7 (F5 scope) and §7b (open-issue verdicts); the merged PR numbers of F0–F4.
- Produces: the state table Task 12's issue comments and Task 14's PR body cite.

**Why:** the roadmap is written as a plan-of-record but read as a state-of-record, and it was wrong
in both directions (three bench gates still reading "pending" a week after passing, a `plan-4.5-done`
tag whose exit criterion was unmet, hardware-gated leftovers with no visible owner). R31 (expanded,
2026-10-13) rules that it becomes a state document: one status line per plan/session, every open
item with an owner.

- [ ] **Step 1: Replace the file with the state document**

Keep the per-plan session tables that record *scope* (they are the only record of what each session
set out to do) but put a status line on every one of them, and replace the head and tail sections
wholesale. The new file:

````markdown
# Lap Timer Roadmap — state of record

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md`
**Started:** 2026-09-14. **Last reconciled against the tree:** 2026-10-13 (audit
`docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` + the fix wave below).

This document is the project's **state of record**: every plan and session carries a status line, and
nothing here is a forecast. A line reads `DONE <date> — PR #n, tag <tag>` or `OPEN — owner <plan or
issue>`. Bench results live in `docs/measurements.md` (one section per bench day); this file points
at them and never duplicates them. If a claim here and the tree disagree, the tree wins and this
file is wrong — fix it in the same PR.

Work is organised by hardware availability, then into day-sized sessions. One session = one sitting,
3–5 bite-sized tasks, a green test run, a commit, a tag.

## Hardware windows

| Window | Hardware | Status | Unlocks |
|--------|----------|--------|---------|
| A | ESP32 DevKit V1 ×2 (CH340), 6 mm tactiles, dev-kit cable | **in hand** | plans 0–5.6, 7, 7b, 7c, the 2026-10-13 fix wave |
| B | TP4056, 2× 18650, holder; regulator and the rest of items 8–16, 28–29 | **partly in hand** — Plan 6 waits on the regulator BOM | plan 6 |
| C | GY-NEO6M v2, MPU6050 | **not in hand** | plans 8–9 |
| D | Waveshare 2.13" V4 SSD1680 panel | **in hand** (2.9" V2 variant builds but is unverified — `[VERIFY]`) | plan 7 (done) |

Maintained parts list: [docs/hardware/bom.md](../hardware/bom.md).

## Session protocol

1. **Start:** read the spec sections named for the session; write the session's plan document with
   the writing-plans skill (tasks, code, tests) and commit it.
2. **Execute:** subagent-driven, one fresh subagent per task, review between tasks. Stay inside the
   session's task list; a discovery that changes the spec is written into the spec in the same
   commit — and only into the sections that session owns.
3. **Finish:** `ctest` green on clang **and** gcc, both linters rc 0 (`tools/lint/power_of_10.py
   --enforce-fnptr --fail-on-violation`, `tools/lint/venue_announce.py --fail-on-violation`),
   `tools/spec_api_check.py` rc 0, the hygiene whitespace check silent, and a ccache-off clean build
   of `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 moto_sim` and `test_apps/core_selftest` with **0
   warnings**. Work lives on a session branch; open a PR, wait for the **nine** required checks,
   squash-merge, then tag `pNN-dD` and push the tag. A change that adds or renames a firmware matrix
   environment updates the branch-protection required checks in the same PR.
4. **Deferred minors:** a review finding that is parked instead of fixed is filed as a GitHub issue
   the same day, naming the session that owns it. A plan's last session runs the whole-plan review
   (opus) plus one fix wave and closes every issue that plan owns before `plan-NN-done`.
5. **Bench evidence lands in the tree, in the same PR.** A session that runs on hardware writes its
   result into `docs/measurements.md` (image version, board/panel, what passed, what failed and what
   it forced, plus the §22.4 resource figures) in the same PR that claims the gate — never only into
   the PR body or a git-ignored `.superpowers/` ledger. A bench-gate line in this file is never left
   reading "pending" after the gate has run. (Audit 2026-10-13 §6 item 1: every hardware result
   between 2026-09-17 and 2026-10-13 was off-tree.)

## Status by plan

| Plan / session | Status |
|---|---|
| **Plan 0 — dev environment** (`2026-09-14-plan-00-dev-environment.md`) | DONE 2026-09-14 — tags `p00-d1`, `p00-d2` = `plan-00-done` |
| 0.1 CI, repo, PR template / 0.2 IDF v5.3.2 pinned, hello-world flashed | DONE |
| **Plan 1 — core foundation** (`2026-09-14-plan-01-core-foundation.md`) | DONE 2026-09-15 — tags `p01-d1`..`p01-d5` = `plan-01-done` |
| 1.1 ring + geodesy / 1.2 timebase + session codec / 1.3 JSON + config / 1.4 tracks + exporters + CI / 1.5 `core_selftest` on the ESP32 + BOM | DONE — `core_selftest` PASS on hardware 2026-09-15 (`docs/measurements.md`, plan 01 session 1.5) |
| **Plan 2 — core engines** | DONE 2026-09-16 — tags `p02-d1`..`p02-d7` = `plan-02-done` |
| 2.1 synth generator / 2.2–2.3 fusion / 2.4–2.5 lap engine / 2.6 drag engine / 2.7 `replay` CLI | DONE — host only; **the 2026-10-13 audit found six engine defects the host suite never exercised** (short-dropout chord, sector-vs-S/F ordering, best-sector leakage, unknown layout ids, marginal-launch DONE, inert `cfg.lap.*`), all fixed by F1a below |
| **Plan 3 — firmware skeleton** | DONE 2026-09-17 — tags `p03-d1`..`p03-d6` = `plan-03-done` |
| 3.1–3.4 build system, board, storage + logger, sim drivers + pipeline | DONE |
| 3.5 console, RTC continuity, crash-loop safe mode | DONE — bench `docs/measurements.md` (plan 03, session 3.5). **Caveat:** the RTC-resume path has still never completed on hardware (#103, F5) |
| 3.6 bench day | DONE 2026-09-17 — `docs/measurements.md` (plan 03, session 3.6) |
| §17.2 supervision beyond heartbeats (ladders, heap/stack, `EV_FAULT`) | was **never implemented** under Plan 3 despite the tag; heap/stack/`EV_FAULT` landed in F2a (2026-10-13), the GPS/IMU/storage ladders are OPEN — owner Plan 8 |
| **Plan 4 — UI on host** | DONE 2026-09-17 — tags `p04-d1`..`p04-d3` = `plan-04-done` |
| 4.1 renderer / 4.2 riding screens / 4.3 menu + ui task | DONE — bench `docs/measurements.md` (plan 04, session 4.3) |
| **Plan 4.5 — Power of 10** | DONE 2026-09-21 — tags `p4.5-d1`..`p4.5-d6` = `plan-4.5-done`, **with one exit criterion unmet at the time** |
| 4.5.1–4.5.5 tooling + retrofit of core/app/drivers/main + the rule-9 callback rework | DONE |
| 4.5.6 blocking flip | DONE as to the CI job, **NOT as to the gate**: `static-analysis` was not a branch-protection required check, and the linter could not see rule 1, rule 3 (non-blocking), most of rule 9, or statements packed onto one line (audit §2). Closed 2026-10-13 by F4: nine required checks including `static-analysis`, and the linter now gates rules 1/3/4/4-compound/5/8/9 over `components/**`, `main/`, and `devcontroller/` report-only |
| **Plan 5 (A) — prod link** | DONE 2026-09-23 — #55 |
| 5.1 `app/cmd` / 5.3 stream + peer detect / 5.4 OTA receive / 5.5 prod-slim | DONE — tag `p5.5-d6` |
| 5.2 `conn_ble` | **OPEN — owner sub-project C.** Code-complete on branch `s5.2-conn-ble` (3 commits, based on 116d199, i.e. pre-Plan-7 `main`), never flashed, never rebased; `conn_ble/` is absent from `main`. The "fits neo6m" headroom claim was measured on that stale tree and is **unverified**. `CONN=ble` sets `CFG_HAS_BLE` for the hwid but appends no driver directory, and since 2026-10-13 `CONFIG_BT_ENABLED=n` (R33) — the branch must re-enable the NimBLE block in `sdkconfig.defaults` and re-measure. F5 rebases the branch once so it stops rotting |
| **Plan 5.5 (B) — dev controller** | DONE 2026-09-21/23 — tag `p5.5-d6` |
| **Plan 5.6 — dev-kit as primary dev interface** | DONE 2026-09-23 — PR #74, tags `p5.6-d1`..`p5.6-d4` = `plan-5.6-done` |
| **Plan 7 — e-paper display** | DONE 2026-09-28 — PR #76, tags `p07-d1`..`p07-d3` = `plan-07-done`; bench 2026-09-27 (`docs/measurements.md`, "Bench day 1"). Real-button check DONE 2026-09-29 — #78 |
| 2.9" panel verification | **OPEN — owner hardware** (`[VERIFY]`, spec §25): the `ws29v2` build compiles and is gated in CI but has never run on a panel |
| **Plan 7b — glanceable riding screens** | DONE 2026-09-28 — PR #77, tag `p07b-d1`; bench 2026-09-28 (`docs/measurements.md`, "Bench day 1b") |
| **Plan 7c — display closure** | DONE 2026-10-05 — PR #88, tag `p07c-d1` → 3db2231; **bench gate `p07c-d1` PASS 2026-10-04** (`docs/measurements.md`, "Bench day 2"). Closed #79 #25 #83 #81 #80 #84; three bench defects (B-F1/B-F2/B-F3) fixed on the stacked branch #90 |
| **Debt sweep A** | DONE 2026-10-05 — PR #89, tag `dsA-d1` → 3db2231; **bench gate `dsA-d1` PASS 2026-10-04** (`docs/measurements.md`, "Bench day 2"). Closed #37 #59 #60 #62 #73 (+#63 #64 #66) |
| **Debt sweep B** | DONE 2026-10-05 — PR #90, tag `dsB-d1` → 37c8368; **bench gate `dsB-d1` PASS 2026-10-05** (`docs/measurements.md`, "Bench day 2"). Closed #86 #87 #65 #67 #68; filed #91 |
| **OTA screen + glyphs + sim scenarios** | DONE 2026-10-05 — PR #94, tag `p07d-d1` → ce59b01; bench day 3 PASS 2026-10-05 (`docs/measurements.md`, "Bench day 3"). Filed #95–#99, #93 |
| **Glyphs / drag card / menu closure** | DONE — PR #101; bench day 4 (2026-10-07) found four defects in its own work, all fixed and re-tested on bench day 5 (2026-10-10) — `docs/measurements.md`, "Bench day 4"/"Bench day 5". Closed #95 #96 #97 #98 #99; filed #100 #102 #103 #104 #105 #106 #107. Two checks still unrun: the #99 LINK-glyph unplug and #97's on-screen name reading |
| **Plan 6 — power** (6.1 regulator + divider / 6.2 power state machine / 6.3 current measurement) | **OPEN — owner Plan 6**, waits on the window-B regulator BOM. No `app/power/` and no §16 code exists. Carries #54 (CHRG pull-up / EXT0 wake / battery pct), the `POWER` record type, the `LOW BATT` one-shot and the "Sleep now" menu item |
| **Plan 8 — real sensors** (8.1–8.5) | **OPEN — owner Plan 8**, waits on window C. `gps_neo6m.c` (47 lines) and `imu_mpu6050.c` (34 lines) are stubs that now honestly return `-ENODEV` (F2a), so `moto_neo6m` reads GPS FAIL / IMU FAIL on the BOOT screen. Carries the GPS/IMU/storage fault ladders, `CMD_GPS_LADDER_STEP`/`CMD_IMU_RECOVER`, #82 (temperature throttle producer), #11 (PPS re-acquisition, M10/O3), the `TIME_SYNC` op, and the real-IMU half of #93 |
| **Plan 9 — field validation** (9.1–9.3) | **OPEN — owner Plan 9**, after plans 6 and 8 |
| **Sub-project C — prod hardening** | **OPEN — owner C**, last (secure boot v2 + flash encryption is an irreversible eFuse burn). Carries BLE activation (5.2 above), the web/BLE export path, the authenticated `cmd` channel, and the §20.7 Export menu item |

## Fix wave — 2026-10-13 (audit close-out)

Design: `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (every ruling R1–R34).
Audit: `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md`.

| Plan | Scope | Status |
|---|---|---|
| **F0** PR #101 close-out | spec §10.1 to the shipped `"LTRK"` format, track-table ownership comment, drag snapshot on every state change, three false comments, PR-body correction | PR #101 |
| **F1a** core engines | the six timing defects (R21–R25), `cfg.lap.*` plumbed, `EV_VENUE_LOST` | — |
| **F1b** core io + ui | exporter guards (R26), `walk` recursion + bounded `ring_pop` + dead `fb->dirty` (R27), the §17.4 fault-icon table (R18/§20.5) | — |
| **F2a** supervision + OTA | OTA validation on stub-GPS builds (R1), §19.5 preconditions (R2), heap/stack/`EV_FAULT` (R6), pipeline+logger on the task WDT (R7), `ESP_LOG_NONE` during transfers (R9), honest stub returns (R28), `qsort` removed (R19) | — |
| **F2b** logger + cmd | session lifecycle, `TIME_MAP`, still-pause (R4), `tracks get/put` + `coredump` (R5), single-producer `g_fix_ring` (R8), cfg RMW lock (R10), 10 km/h menu lock (R11) | — |
| **F3** sim + dev-kit | drag-origin latch + sim clock (R29), dev-kit PSK + caps + tooling (R16, R17, R30) | — |
| **F4** process, tooling, docs | linter gaps (R12), nine required checks (R13), coverage job (R14), `sign_release` guards (R15), spec reconciliation (R3, R18), evidence in-tree (R31), `CONFIG_BT_ENABLED=n` (R33), cleanup sweep (R34) | this plan |

**Measured in the wave:** `CONFIG_BT_ENABLED=n` (R33) moved `dram0_0_seg` from `0x3ffbdb5c`/`0x1e6a4`
(124,580 B) to `0x3ffb0000`/`0x2c200` (180,736 B) — about 56 KB of static DRAM that had been reserved
for a Bluetooth controller no build links. Free static DRAM before/after per environment: see the
F4 PR and `docs/measurements.md`.

## Close-out wave — F5 (after the fix wave's bench day)

Plan document `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md` is written after that bench day
(its content depends on what the bench shows). Scope, from design §7: #91 `list` cost, #61
stream-frame CRC, #57 per-chunk OTA ACK, #103 RTC resume on the sim build, #104 `free_kb` with an
open session, #105 orphan `.sum.tmp`, #106 console INFO silence (bench confirm), #53 task stacks
(measure after F2a's stack-low check, finalise §4.3), #100 sim venue id, #92 bench checklist, #93
calibration wiring + persistence (sim-testable half), and one rebase of `s5.2-conn-ble`.

## Open issues by owner

| Owner | Issues |
|---|---|
| F5 | #57, #61, #91, #100, #103, #104, #105, #53 (measure), #93 (wiring), #106 (confirm) |
| F2a / F2b | #106 (fix candidate, R9), #107 (closed by `tracks get`), PR-101 M3/M4 |
| F4 | PR-101 M5/M6 (done), the tightened-linter findings in other plans' files, the file-level `/tracks/user.bin` harness |
| Plan 6 | #54 |
| Plan 8 | #82, #11, #93 (real-IMU validation), the fault ladders, `TIME_SYNC` |
| Sub-project C | 5.2 BLE activation, Export menu item |
| hardware | 2.9" panel `[VERIFY]` |

Per-issue verdicts (valid / stale / duplicate / fixed-by-PR) are recorded as issue comments and in
design §7b.

## Later phases (spec §24)

Plans 10+ cover WiFi (phase 2), RaceChrono live (3), M10 (4), SD (5), car variant and predictive
delta (6). Each is split into sessions when reached.

## Sequencing

```
done:      0.1 → 0.2 → 1.1 → … → 1.5 → 2.1 → … → 2.7 → 3.1 → … → 3.6 → 4.1 → … → 4.3
           → 4.5.1 → … → 4.5.6 → 5.1 → 5.3 → 5.4 → 5.5 → 5.5(B) → 5.6
           → 7.1 → 7.2 → 7.3 → 7b → 7c → dsA → dsB → (OTA screen/glyphs) → #101
now:       F0 → (F1a ∥ F1b ∥ F3 ∥ F4) → F2a → F2b → bench day → F5
window B:  6.1 → 6.2 → 6.3                      (needs the regulator BOM)
window C:  8.1 → … → 8.5 → 9.1 → 9.2 → 9.3      (needs NEO-6M + MPU6050)
last:      sub-project C (secure boot, BLE activation)
```

Plan 6 and Plan 8 are independent of each other; whichever hardware arrives first goes first.
Sub-project C stays last: its eFuse burns are irreversible.
````

- [ ] **Step 2: Check every status line against the tree**

```bash
git tag | sort                                  # every tag cited above must exist
gh pr view 74 --json number,mergedAt && gh pr view 76 --json number,mergedAt && \
gh pr view 77 --json number,mergedAt && gh pr view 88 --json number,mergedAt && \
gh pr view 89 --json number,mergedAt && gh pr view 90 --json number,mergedAt && \
gh pr view 94 --json number,mergedAt && gh pr view 101 --json number,mergedAt
gh issue list --state open --json number --jq 'map(.number) | sort | @csv'
grep -c "^## Bench day" docs/measurements.md       # 6 sections, matching the pointers above
test -d components/app/power || echo "no app/power -- the Plan 6 line is correct"
```
Expected: every tag present; every PR merged (101 merged by F0); the open-issue list exactly matches
the "Open issues by owner" table (add any issue filed after this plan was written to the right row);
`grep -c` = 6.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/plans/2026-09-14-roadmap.md
git commit -m "docs(roadmap): rewrite as a state document -- one status line per plan/session with date/PR/tag or OPEN+owner, the three stale 'pending' bench gates replaced by their pass records, Plan 4.5's unmet exit criterion recorded and closed, fix-wave and close-out sections, open issues by owner (R31)"
```

- [ ] **CHECKPOINT — Task 10**

```bash
printf 'Task 10 complete %s — roadmap rewritten as a state document\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 11 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 11: Linter blocking flip — SERIAL (after F1a/F1b/F2a/F2b merge)

**Files:**
- Modify: `.github/workflows/ci.yml` (the `--report-only-sites` argument only)

**Interfaces:**
- Consumes: the merged tree.
- Produces: a `static-analysis` job with no grace list.

- [ ] **Step 1: Re-measure the tree**

Run: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation; echo "rc=$?"`
Expected (if every owner landed its fix): rc 0. Known expected disappearances: `cfg_json.c:walk` (F1b, R27 — `walk` becomes a two-level loop), `board.c:cmp_int` (F2a, R19 — bounded insertion sort), `board.c:btn_isr` (this plan's PD-6 key amendment).

- [ ] **Step 2: Shrink or delete the grace list**

Delete every entry that no longer matches (the linter names them with `GRACE-STALE`). Any entry that still matches stays, and its CI comment line gains the issue number Task 14 filed:
```yaml
          #   components/core/config/cfg.c:cfg_defaults:RULE-5   -> #<issue> (statement counting)
```
If the list is empty, the step collapses to:
```yaml
      - name: power_of_10.py linter (BLOCKING; rules 1/3/4/4-compound/5/8/9)
        run: python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
```

- [ ] **Step 3: Prove the flip**

Run the exact CI command and confirm rc 0 and no `GRACE-STALE` line. Record in the task report: the finding count at F4's merge (10), the count now, and every site still graced with its issue number.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: drop the R12 rollout grace entries their owning plans have fixed (linter blocking flip)"
```

- [ ] **CHECKPOINT — Task 11**

```bash
printf 'Task 11 complete %s — linter grace list shrunk; blocking flip done\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 12 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 12: Open-issues audit — one verdict comment per open issue (design §7b) — SERIAL

**Files:** none in the repo (GitHub only).

**Interfaces:**
- Consumes: `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` **§7b** — the verdict table for all 21 open issues. **Use its text; do not invent a verdict.**
- Produces: the per-issue owner assignment Task 10's "Open issues by owner" table and Task 14's PR body rely on.

**Why:** the audit (§6 item 4) found issue hygiene good for bench-found defects and poor for
read-found spec gaps, and several open issues carry stale owner lines ("Owner: plan 04" on #11,
plan-04 being the host UI) or are already fixed by a merged PR. One comment per issue makes the
backlog honest in one pass.

- [ ] **Step 1: Confirm the issue set has not moved**

```bash
gh issue list --state open --limit 50 --json number,title --jq '.[] | "\(.number)\t\(.title)"' | sort -n
```
Expected: the 21 issues §7b lists — #11 #53 #54 #57 #61 #82 #91 #92 #93 #95 #96 #97 #98 #99 #100
#102 #103 #104 #105 #106 #107. If F0 has merged, #95–#99 and #102 are already **closed** by its
`Closes` lines — comment on them anyway only if they are still open; otherwise skip them and say so
in the task report. Any issue in the list that §7b does not cover is new since the design doc: add a
line to the task report and comment `valid; filed after the 2026-10-13 audit — owner <plan>` only
once the owner is agreed with the user.

- [ ] **Step 2: Post one verdict comment per issue**

Each body is the §7b row for that issue, written as a sentence: `<verdict> — <evidence>. Owner:
<owner>.` plus the fixed trailer `Audit 2026-10-13 (docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md); verdict table: docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §7b.`
Run them one at a time and read each result (a 404 means the issue was closed between Step 1 and
here — record it, do not retry blindly):

```bash
T='Audit 2026-10-13 (docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md); verdict table: docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md §7b.'

gh issue comment 11  --body "valid; the \"Owner: plan 04\" line is stale (plan 04 was the host UI) — \`tb.c\`'s min-filter is unchanged. Owner: the M10 driver (O3), after Plan 8. $T"
gh issue comment 53  --body "valid, numbers stale (they come from v0.1.0-12; the ui stack is 6144 B on every build now) and nothing measures it automatically. F2a adds the §17.2 stack-low check, so the measurement becomes possible. Owner: F5 — measure after F2a, then finalise the §4.3 sizes at high-water + 25 %. $T"
gh issue comment 54  --body "valid; F2a's \`CFG_CHRG_WIRED=0\` default removes the OTA battery-precondition spoof now (GPIO 39 has no internal pull, so an unwired pin reading low used to pass the check). The pull-up / EXT0-wake half needs the Plan 6 board. Owner: F2a (mitigation) + Plan 6 (hardware). $T"
gh issue comment 57  --body "valid, and the \"needs Plan 6 power\" rationale no longer holds — bench day 3 pushed over the dev-kit repeatedly. Implementable and bench-able with the owned hardware: per-chunk \`OTA-ACK <offset>\` after each 4 KB write, awaited by ota_push.py / linkhost_flash / flash push. Owner: F5. $T"
gh issue comment 61  --body "valid and hardware-independent: append CRC16 (\`ses_crc16\`) to every 0xFF stream frame (\`lt_stream_hdr_t\` gains \`crc16\`), dev-kit demux verifies and counts bad frames, \`selftest stream\` checks it. Owner: F5. $T"
gh issue comment 82  --body "valid; hardware-gated — the producer needs the MPU6050 temperature channel. The consumer (refresh throttle) has been in place since Plan 7. Owner: Plan 8. $T"
gh issue comment 91  --body "valid: measured 5.5–6.0 s for \`lt list\` at 52 sessions on bench day 3 (~105 ms/session). Fix: a logger-owned name→(start_utc, venue, laps, best) cache rebuilt incrementally on open/close/delete, read by \`op_list\` instead of scanning every \`.sum\`. Owner: F5. $T"
gh issue comment 92  --body "tracking issue: items 1–4 and 7–11 are closed; item 5 (menu stubs) went to PR #101 and its re-test; item 6 (real GPS) is Plan 8. Close it when \`docs/bench/next-bench-day.md\` has no open box except hardware-gated ones, which move to their owning plan. Owner: roadmap/F5. $T"
gh issue comment 93  --body "valid, and it splits in two: the wiring + persistence half is sim-testable (\`CMD_CALIB_ORIENT\` → \`fus_calib_orient_capture()\` after a 2 s still window, persisted to NVS \`lt_cal/fus\` through \`blob_wrap\`, loaded by \`pipeline_init\`, \`EV_CALIB_DONE\` + the §20.6 CALIBRATE one-shot, forward learning per fix, a \`CALIB\` record on each persist) → F5; the real-IMU validation and \`FUS_LEAN_VALID\` in production → Plan 8. This issue is amended, not closed, until both land. $T"
gh issue comment 100 --body "valid: the sim venue id should come through the synth generator (\`synth --venue-id\`) rather than a reserved constant, and the table-capacity edge belongs in \`gen_fixtures.sh\`. Owner: F5. $T"
gh issue comment 103 --body "valid: the RTC-resume path has never completed on hardware because the sim replays its capture from t0 on every boot, so post-resume fixes predate the resumed lap's start. Fix: \`sim_clock\` keeps its epoch across resets in \`RTC_NOINIT_ATTR\` (sim build only), which makes bench item 17's resume half executable and closes the §15.3 coverage gap. Owner: F5. $T"
gh issue comment 104 --body "valid: the logger's append path has no \`STO_RESERVE_PCT\` guard (only \`open_session\` does), and \`sto_info\` is not invalidated on open — hence \`free_kb\` reading 0 with one open session and 472 KB after a remount. Owner: F5 (append guard + cache invalidation). F2b must not duplicate this. $T"
gh issue comment 105 --body "valid: nothing reaps \`/sessions/*.sum.tmp\` after an interrupted summary rewrite. Fix: a bounded, name-only listing at logger boot init that unlinks them. Owner: F5. $T"
gh issue comment 106 --body "valid, root cause unknown but the most likely one is a logging-level quiesce (\`run_cmd\`/\`run_stream\`/\`ota recv\`, export_serial.c:235/327/1136) that is not restored on an abort path — ruling R9 (F2a) rewrites exactly those sites with restore-on-every-exit and sets \`ESP_LOG_NONE\` rather than \`ESP_LOG_ERROR\`. Owner: F2a (fix candidate) + F5 (bench confirm, then close). $T"
gh issue comment 107 --body "fixed by ruling R5 (F2b): the console gains \`tracks get\` (a JSON array of every venue, bundled + user, with \`\"src\"\`) and \`tracks put <json>\`, and the \`cmd\` layer gains the 0x12/0x13 ops. Closes when F2b merges. Owner: F2b. $T"
```
If F0 has not yet merged, also post the five "fixed by PR #101" verdicts; if it has, they are closed
and these are skipped:
```bash
gh issue comment 95  --body "fixed by PR #101 — the DRAG card reads NOT READY/READY/LAUNCHED/DONE from the engine's real state; bench day 5 (2026-10-10) pass. Closes at that merge. $T"
gh issue comment 96  --body "fixed by PR #101 — \`dist_units\` m|ft with cfg blob v2 migration; bench day 5 pass. Closes at that merge. $T"
gh issue comment 97  --body "fixed by PR #101 — main path bench-passed on day 5; the on-screen \`Track_YYYYMMDD\` reading and the item's sub-checks remain for the next bench day. Closes at that merge. $T"
gh issue comment 98  --body "fixed by PR #101 — Layout cycling with real names; bench day 5 pass. The RTC-resume contradiction found in review (pr:M4 — the menu reads Auto on a layout-locked lap) is a NEW finding, owned by F2b, not this issue. Closes at that merge. $T"
gh issue comment 99  --body "fixed by PR #101 — the LINK strip glyph from \`link_peer_present()\`; the unplug check is still unrun and moves to the next bench day. Closes at that merge. $T"
gh issue comment 102 --body "fixed by F0 Task 2 — the drag snapshot is published whenever \`drag_state()\` changes (a \`s_last_pub_state\` compare in \`on_raw\`), so a false start no longer leaves the card reading LAUNCHED. Owner: F0. $T"
```

- [ ] **Step 3: Report, then commit nothing**

This task writes no files. Record in the ledger: which issues got a comment, which were already
closed, and any issue §7b does not cover. If §7b and an issue's actual state disagree (e.g. an issue
was closed since the design doc was written), the issue wins — note it so the orchestrator can
correct §7b.

- [ ] **CHECKPOINT — Task 12**

```bash
printf 'Task 12 complete %s — verdict comment on every open issue (design 7b)\n' "no-commit" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 13 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

## Session 4 — cleanup and close (Tasks 13–14)

Serial. Stop for review after Task 14, before the PR is marked ready.

### Task 13: Repo cleanup sweep (R34, design §6) — SERIAL (last in the wave)

**Files:**
- Modify: `.github/workflows/ci.yml` (one new `hygiene` step), `build.sh` (the env dispatch message), `sdkconfig.defaults.car`, `sdkconfig.defaults.moto` (verify only)
- Create: `tools/dist_prune.sh`

**Interfaces:**
- Consumes: the merged tree (every other plan has deleted its own stale comments by now).
- Produces: the `hygiene` job's stale-comment check (so a future "lands in session N" comment cannot merge) and `tools/dist_prune.sh`.

- [ ] **Step 1: Grep the tree for stale forward-looking comments**

```bash
git grep -nIE 'lands in [0-9]|arrives in [0-9]|lands with the|later sessions?|stubbed until|stub until|not in plan [0-9]|Plan 04 ships no display' \
  -- components main tools docs ':!docs/reviews' ':!docs/superpowers'
```
Measured on `main` @ f4d4db2 this finds **19 hits in 11 files**, every one of them false today:
`components/app/cmd/cmd.c:5,697,838`, `components/app/include/app/lt_err.h:2,13`,
`components/app/include/app/lt_ipc.h:120`, `components/app/pipeline/pipeline.c:711`,
`components/app/supervisor/sup.c:5,144`, `components/core/lapengine/lap.c:1044`,
`components/drivers/export_serial/export_serial.c:20,969,977,1082`,
`components/drivers/gps_neo6m/gps_neo6m.c:3`, `components/drivers/imu_mpu6050/CMakeLists.txt:1`,
`components/drivers/imu_mpu6050/imu_mpu6050.c:3`, `main/app_main.c:211,238`.
Owners: cmd.c / lt_err.h / lt_ipc.h / pipeline.c / export_serial.c (console half) → **F2b**;
sup.c / gps_neo6m / imu_mpu6050 / app_main.c / export_serial.c:1082 → **F2a**; lap.c → **F1a**.
Re-run the grep on the merged tree and record what survived. For each survivor: if the file is
F4-owned, fix the comment here; otherwise file one issue naming the file, the line and the owning
plan, and add that single path to the check's exclusion list with the issue number in a comment —
never widen the pattern.

- [ ] **Step 2: Add the check to the `hygiene` job**

```yaml
      - name: No stale forward-looking comments (audit §3.2 -- comments used as a ledger)
        run: |
          # A comment that promises a future session is false the moment that session ships, and the
          # 2026-10-13 audit found 19 of them. Rewrite the comment as a true statement, or cite an
          # issue number. Exclusions carry the issue that owns the survivor.
          ! git grep -nIE 'lands in [0-9]|arrives in [0-9]|lands with the|later sessions?|stubbed until|stub until|not in plan [0-9]|Plan 04 ships no display' \
              -- components main tools docs ':!docs/reviews' ':!docs/superpowers'
```

- [ ] **Step 3: `build.sh` — name an env whose driver does not exist**

Spec §4.6's environment table stays (it is the design's env matrix), but `build.sh` must say what is
missing instead of failing inside CMake. In the env→flags dispatch, after resolving `GPS`/`IMU`/
`DISPLAY`/`STORAGE` for the named env, add:
```bash
# R34: the spec's §4.6 env table lists environments whose drivers are not written yet (m10, sd,
# oled, wifi). Say so here rather than failing with a CMake include error 20 lines later.
for _pair in "gps_${GPS}:GPS=${GPS}" "imu_${IMU}:IMU=${IMU}" "display_${DISPLAY}:DISPLAY=${DISPLAY}" "storage_${STORAGE}:STORAGE=${STORAGE}"; do
    _dir="components/drivers/${_pair%%:*}"
    if [ ! -d "$_dir" ]; then
        echo "driver not implemented: ${_pair#*:} (no $_dir)" >&2
        echo "environment '$ENV_NAME' cannot be built from this tree -- see spec §4.6 and the roadmap's plan owners." >&2
        exit 1
    fi
done
```
Verify: `./build.sh moto_m10 build` prints `driver not implemented: GPS=m10 (no components/drivers/gps_m10)` and exits 1 **before** any IDF work; `./build.sh moto_sim build` and `./build.sh moto_neo6m build` are unaffected (both drivers exist).

- [ ] **Step 4: `tools/dist_prune.sh`**

```bash
#!/usr/bin/env bash
# tools/dist_prune.sh -- move signed images older than the current HEAD description into
# dist/archive/ (R34). dist/ is git-ignored either way; this only keeps the directory readable.
# Usage: tools/dist_prune.sh [--dry-run]
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -d dist ] || { echo "no dist/ -- nothing to prune"; exit 0; }
keep="$(git describe --tags --match 'v*' --always)"
mkdir -p dist/archive
moved=0
for f in dist/*.bin dist/*.sha256; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in
        *"$keep"*) continue ;;                     # the current image stays
    esac
    if [ "${1:-}" = "--dry-run" ]; then echo "would archive $f"; else mv "$f" dist/archive/; fi
    moved=$((moved+1))
done
echo "dist_prune: $moved file(s) ${1:-archived} (kept anything matching '$keep')"
```
`chmod +x tools/dist_prune.sh`. Run `tools/dist_prune.sh --dry-run` and record the output (on
`main` today `dist/` holds 10 files, 8 of them from superseded versions). Then run it for real.
It is not wired into CI — `dist/` is git-ignored, so there is nothing for CI to check.

- [ ] **Step 5: `sdkconfig.defaults.car` / `.moto` — confirm, do not delete**

```bash
diff sdkconfig.defaults.car sdkconfig.defaults.moto
```
Measured: they differ in exactly one line (`CONFIG_LAPTIMER_VARIANT_CAR=y` vs
`CONFIG_LAPTIMER_VARIANT_MOTO=y`) and contain nothing else. Design §6 asks whether to delete them if
identical — they are **not** identical, so both stay. Record the diff in the task report and leave
the files and the CMake line alone. (Spec §21.2's text about them is corrected in Task 9.)

- [ ] **Step 6: `cppcheck --enable=unusedFunction` once, advisory**

```bash
cppcheck --enable=unusedFunction --std=c11 --quiet \
  -I components/core/include -I components/core/ui/include -I components/app/include \
  -I components/lt_hal/include -I components/drivers/export_serial/include \
  --suppress=missingInclude --suppress='*:components/core/util/jsmn.c' \
  components/core components/app components/drivers main 2>&1 | tee /tmp/unusedfn.txt
```
This is a one-off inventory, not a CI job (`unusedFunction` is unreliable across translation units
and would flood the advisory step). For every reported symbol, check it by hand
(`git grep -n "<symbol>"`): a genuinely dead function in an F4-owned file is deleted here; one in
another plan's file goes into a single issue listing them with their owners, together with whatever
`-Wunused` reported during the wave's builds. Record the symbol count in the task report.

- [ ] **Step 7: Worktrees, branches and the user's spreadsheet**

```bash
git worktree list
```
After F0 has merged, remove the merged worktree (it is the `glyphs-drag-menu-closure` checkout):
```bash
git worktree remove /Users/benji/projects/personal/lap-timer-worktrees/gdm
git worktree prune
```
Leave `s5.2-conn-ble` and its worktree alone (design §7: F5 rebases it once).
**The untracked `lattice-poc-bom(1).xlsx` at the repo root is the user's file. Do not delete, move
or commit it.** Ask the user, in exactly these terms, and act only on the answer: "There is an
untracked `lattice-poc-bom(1).xlsx` at the repo root. Three options: (a) move it to
`docs/hardware/` and commit it, (b) add it to `.gitignore` and leave it where it is, (c) leave it
untracked and unlisted. Which?" If there is no answer in this session, leave it untouched and record
the question in the ledger.

- [ ] **Step 8: Verify and commit**

```bash
git grep -nIE 'lands in [0-9]|arrives in [0-9]|lands with the|later sessions?|stubbed until|stub until|not in plan [0-9]|Plan 04 ships no display' -- components main tools docs ':!docs/reviews' ':!docs/superpowers'; echo "rc=$? (1 = clean)"
./build.sh moto_m10 build; echo "rc=$? (1 = refused with the driver message)"
CCACHE_DISABLE=1 ./build.sh moto_sim build && CCACHE_DISABLE=1 ./build.sh moto_neo6m build
bash tools/test_sign_release.sh
```
Expected: the grep finds nothing (rc 1 from `git grep` = no match); `moto_m10` is refused with the
new message; both real environments still build with 0 warnings; the sign-release harness still
passes (it calls `build.sh` only in `--dry-run`, which stops before the build, so Step 3's loop does
not affect it).
```bash
git add .github/workflows/ci.yml build.sh tools/dist_prune.sh
git commit -m "chore(R34): hygiene check for stale forward-looking comments, build.sh names an unimplemented driver instead of failing in CMake, tools/dist_prune.sh; car/moto fragments confirmed non-identical and kept"
```

- [ ] **CHECKPOINT — Task 13**

```bash
printf 'Task 13 complete %s — R34 cleanup sweep\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): `Resume plan docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md at Task 14 (ledger .superpowers/sdd/2026-10-13-fix-F4/progress.md); worktree /Users/benji/projects/personal/lap-timer-worktrees/fix-process-docs; branch fix-process-docs.`

---

### Task 14: Branch protection, filed issues, final wave gate — SERIAL (last)

**Files:** none in the repo (GitHub + the user's memory index).

- [ ] **Step 1: File the issues**

Six issues, below. Task 13 files two more from its own sweep (any stale forward-looking comment that survived in another plan's file, and the `cppcheck --enable=unusedFunction` inventory); do not duplicate them here.

```bash
gh issue create --title "Host harness for logger.c's /tracks/user.bin file framing (PR #101 M6)" --body "$(cat <<'EOF'
PR #101's review (M6) found that the untrusted half of the user-track persistence path has no host
test: `test_trk.c` covers the RECORD decoder (`trk_user_load_venue`) thoroughly, and the audit fix
wave's F4 plan added five more hostile-record cases (trailing garbage, `n_layouts`/`n_sectors` over
cap, venue id 0, an unterminated 32-byte name). What is still untested is the FILE framing in
`components/app/logger/logger.c` (`tracks_save`/`tracks_load`/`tracks_load_record`):

- bad magic (not `"LTRK"`)
- wrong version byte
- `count` larger than the records actually present
- a bad-CRC record in the middle followed by a good one (the shipped behaviour is "skip the bad
  record and keep going", which is the opposite of what spec §10.1 used to say)
- `len == 0`
- `len > TRK_USER_REC_MAX`
- a truncated file (header present, record body cut)

`logger.c` has no host harness at all, so this needs one: extract the pure framing into a
host-compilable unit (the `display_epaper_ssd1680/host/*.c` and `sim_common/host/*.c` pattern), or
add a `test/test_logger_tracks.c` that drives the framing functions against a byte array.

Owner: F2b's successor (logger.c is F2b-owned in the 2026-10-13 wave). Evidence:
`.superpowers/reviews/2026-10-13/pr-101.md` M6; audit `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` §7.6.
EOF
)"
```
```bash
gh issue create --title "Power-of-10: seven statement-count findings in files the audit fix wave did not own" --body "$(cat <<'EOF'
The tightened linter (ruling R12, `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md`)
counts a function's size as `max(physical code lines, top-level statements)`, which surfaces seven
pre-existing violations that the physical-line count hid. They live in files owned by other plans,
so F4 listed them in the `static-analysis` job's `--report-only-sites` grace list instead of
editing them. Each needs either the fix or a reviewed deviation row:

| site | rule | measured |
|---|---|---|
| `components/app/sys/lt_nvs.c:errlog_add` | RULE-5 | 18 physical / 22 statements, 0 assertions |
| `components/app/ui/ui.c:handle_event` (switch at ui.c:1111) | RULE-4-COMPOUND | 17 physical / 38 statements |
| `components/core/config/cfg.c:cfg_defaults` | RULE-5 | 15 physical / 40 statements, 1 assertion |
| `components/core/config/cfg_json.c:apply` | RULE-4 | 59 physical / 64 statements |
| `components/core/config/cfg_json.c:cfg_to_json` | RULE-4 | 49 physical / 110 statements |
| `components/drivers/export_serial/export_serial.c:fmt_from_str` | RULE-5 | 6 physical / 21 statements, 0 assertions |
| `components/drivers/export_serial/export_serial.c:tally_cb` | RULE-5 | 14 physical / 23 statements, 0 assertions |

Reproduce: `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` (no grace flag).
Each fix is a split into named helpers or two meaningful assertions — never a padding assertion
(design §3 rule 5). Delete each entry from `.github/workflows/ci.yml`'s `--report-only-sites` string
in the same PR as its fix; the linter prints `GRACE-STALE` for an entry that stops matching.
EOF
)"
```
```bash
gh issue create --title "Spec §4.3 task map is stale: no conn/power tasks, link task missing, ui stack" --body "$(cat <<'EOF'
Audit 2026-10-13 §3.3 item 29. Spec §4.3's task table lists `conn` and `power` tasks that do not
exist, omits the `link` task (priority 4, core 0, unsupervised by design — see PD-8), and gives a
`ui` stack of 6144 B that `moto_sim` does not use (reduced, #53). §4.3 is not owned by any plan in
the 2026-10-13 audit fix wave, so it is filed rather than edited: whichever plan adds the power task
(Plan 6) rewrites the table against the real `xTaskCreateStaticPinnedToCore` call sites in
`main/app_main.c` and records the per-environment stack sizes from `dbg mem`.
EOF
)"
```
```bash
gh issue create --title "Spec §17.6 boot self-test: three rows have no implementation and no owner" --body "$(cat <<'EOF'
Audit 2026-10-13 §3.3 item 27. §17.6's self-test table specifies a battery range check, a coredump
presence check (`esp_core_dump_image_check`) and an `E_SYS_PANIC` summary at boot; only the storage
probe exists. The GPS/IMU rows are legitimately Plan 8. The battery row needs the §3.3 divider and
CHRG pull-up (#54, Plan 6); the coredump rows become reachable once `COREDUMP_GET`/`COREDUMP_CLEAR`
land (ruling R5, F2b). Owner: Plan 6 for the battery row, Plan 6 bench for the coredump rows. Either
implement them or amend §17.6 with a dated marker, the way §19.5 was amended by ruling R2.
EOF
)"
```
```bash
gh issue create --title "Spec §20.7 menu: Export / Live / Diagnostics / Sessions have no owning plan" --body "$(cat <<'EOF'
Audit 2026-10-13 §3.3 item 30 and §3.2 item 14. After the 2026-10-13 wave the menu's stubs are:
Sleep now (Plan 6.2), Calibrate (#93, Plan 8), and these four with no owner at all — Export
(§18.3, needs a transport: sub-project C), Live (§10.11 predictive, O5), Diagnostics (§17.10),
Sessions (§20.7). `ui.c`'s comments blamed "plan 06"/"plan 05", both of which are merged or
unrelated. Decide per item: implement, or give §20.7 a dated "not implemented, owner X" marker so
the spec stops describing a menu the firmware does not have.
EOF
)"
```
```bash
gh issue create --title "Retire or rebase s5.2-conn-ble before any 'BLE fits on neo6m' claim is repeated" --body "$(cat <<'EOF'
Audit 2026-10-13 §2 (Plan 5.2 row). The branch `s5.2-conn-ble` is 3 commits / +564 lines based on
116d199 — a tree from before Plan 7, 30+ merges ago. It was never flashed, `conn_ble/` does not
exist on `main`, and the "code-complete, fits neo6m" claim in the project memory was measured
against that stale tree. Meanwhile `CONN=ble` on `main` sets `CFG_HAS_BLE=1` and the `CONFIG_BT_*`
Kconfig options while appending no driver directory — dead configuration (spec §4.6 now says so).
Before sub-project C repeats the headroom claim: rebase the branch onto current `main`, rebuild,
and re-measure free static DRAM — or delete the branch and start from the current tree.
EOF
)"
```

- [ ] **Step 2: Hand the branch-protection change to the user**

This requires admin rights; the executing agent must **not** run it. Put this in the PR body and in the handoff message:
```bash
gh api -X PUT repos/superbrobenji/lap-timer/branches/main/protection --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "hygiene",
      "host-tests",
      "dc-host-tests",
      "tools-tests",
      "tracks-generated",
      "static-analysis",
      "build (moto_neo6m)",
      "build (moto_sim)",
      "build (devcontroller)"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": null,
  "restrictions": null,
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_conversation_resolution": false
}
JSON
```
This is a strict superset of today's protection (`gh api repos/superbrobenji/lap-timer/branches/main/protection` shows `contexts` = hygiene, host-tests, tracks-generated, build (moto_neo6m), build (moto_sim); `enforce_admins` true; `required_linear_history` true; no review requirement) — it adds the four missing contexts and changes nothing else. Verify afterwards:
```bash
gh api repos/superbrobenji/lap-timer/branches/main/protection --jq '.required_status_checks.contexts'
```
Expected: the nine contexts above. Note the `coverage` job is deliberately **not** required.

- [ ] **Step 3: Update the project memory's stale STATE block**

Audit §3.1 item 4: `~/.claude/projects/-Users-benji-projects-personal-lap-timer/memory/lap-timer-project.md` still says main = 69055d9 / "Plans 1–7b merged". Rewrite its STATE block to the post-wave reality: main = the merge commit of this wave, Plans 1–7d + debt sweeps A/B + PR #101 + the seven audit-fix plans merged, the nine required checks, and the fact that bench evidence now lives in `docs/measurements.md`. Also correct the "blocking CI" phrase in that file to name what is actually blocking (the custom linter and the venue linter, with `clang-tidy`/`cppcheck` advisory).

- [ ] **Step 4: Run the whole gate (design §4)**

```bash
# host
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel && ctest --test-dir test/build --output-on-failure
CC=$(ls /opt/homebrew/bin/gcc-* | head -1) cmake -S test -B test/build-gcc -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build-gcc && ctest --test-dir test/build-gcc --output-on-failure
cmake -S devcontroller/test -B devcontroller/test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build devcontroller/test/build && ctest --test-dir devcontroller/test/build --output-on-failure
# tools
python3 -m unittest tools/test_devkit.py tools/lint/test_power_of_10.py tools/lint/test_venue_announce.py tools/test_spec_api_check.py tools/tracks/test_gen_tracks.py
bash tools/test_sign_release.sh
# linters + spec
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation
python3 tools/lint/venue_announce.py --fail-on-violation
python3 tools/spec_api_check.py
# generated artifacts
python3 tools/tracks/gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c && git diff --exit-code components/core/tracks/trk_bundled.c
# hygiene
git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin' ':!components/core/include/core/jsmn.h'
# firmware (ccache off, 0 warnings, report free static DRAM for each)
source tools/idf-env.sh
CCACHE_DISABLE=1 ./build.sh moto_sim build && CCACHE_DISABLE=1 ./build.sh moto_neo6m build
CCACHE_DISABLE=1 PANEL=ws29v2 ./build.sh moto_sim build
idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build build
```
Expected: ctest 100 % on both host suites and the dev-kit suite; every tool test green; all three linters rc 0; `trk_bundled.c` unchanged; hygiene silent (including Task 13's stale-comment check); all four firmware builds rc 0 with **0 warnings**.

DRAM: this plan adds no firmware `.bss`, but Task 8 raises the ceiling — `dram0_0_seg` must read `0x3ffb0000` / `0x2c200` in both maps, and the free-static-DRAM figures must be ~56 KB above the wave's pre-R33 baselines (`moto_sim` 4192 B, `moto_neo6m` 5616 B as of f4d4db2) ± whatever F1–F3 moved. Report the measured numbers for both environments, state the baselines they replace, and attribute any `.bss` delta to the owning plan, never to this one.

- [ ] **Step 5: Append the bench-day items this plan creates**

`docs/bench/next-bench-day.md` is shared; append only F4's items (no renumbering of existing ones):
```markdown
- [ ] **F4-1 signed release path.** On a clean tree: `tools/sign_release.sh moto_neo6m` completes
      (build → `size` gate → sign → verify → `dist/`); `tools/sign_release.sh moto_sim` is refused
      with the §4.6 message; `tools/sign_release.sh moto_sim --allow-sim` signs the bench image.
      Then flash the signed `moto_sim --allow-sim` image over the dev-kit and confirm it boots and
      validates (no `E_OTA_ROLLBACK` at the next reset, ≥ 35 s after the push).
- [ ] **F4-2 BT off (R33).** Flash the post-R33 `moto_sim` image and confirm, on the console:
      `dbg status` boots clean with no BT-related error, `dbg mem` reports a heap free figure
      **higher** than the pre-change bench-day-3 reading (184–185 KB class), and a 10-minute sim-lap
      run shows no new `W`/`E` lines. Record both heap figures in `docs/measurements.md`.
- [ ] **F4-3 nothing else.** Every other F4 change is host-side; it has no bench item by design.
```

- [ ] **Step 6: Open the PR**

```bash
gh pr create --base main --title "F4 — process, tooling and docs: the Power-of-10 linter sees what CI claims, nine required checks, honest spec/README/roadmap, bench evidence in the tree" --body-file .github/PULL_REQUEST_TEMPLATE.md
```
The PR body must carry: the ten linter findings measured at F4's merge and which plan owns each; the 41 → 0 §5.2 drift count; the nine required checks plus the `gh api` command the user runs; the grace entries still open with their issue numbers; the six issues filed; and the measured firmware DRAM figures. Do **not** claim any bench result this plan did not run.

- [ ] **CHECKPOINT — Task 14**

```bash
printf 'Task 14 complete %s — branch protection handed over, wave gate run, PR open\n' "$(git rev-parse --short HEAD)" >> .superpowers/sdd/2026-10-13-fix-F4/progress.md
git push -u origin fix-process-docs
```
Resume prompt (record it in the ledger; paste it after a `/clear`): All 14 tasks are complete: the F4 PR is open and the branch-protection command is in its body for the user to run. Nothing to resume.
Final ledger line: `F4 complete <sha> — PR #<n> open, protection command handed to the user`.

---

## Self-review

**Spec coverage (design doc row F4, rulings R3/R12–R15/R18/R31/R33/R34, §4 gates, §6 cleanup inventory, §7b open issues, and the four reviewer reports):**

| Requirement | Task |
|---|---|
| R12 — the linter enforces what CI claims (LG-1..LG-7) | 1; two-step rollout closed by 11 |
| R13 — `static-analysis` + `dc-host-tests` + `tools-tests` + `build (devcontroller)` become required | 4 (job names), 14 (the `gh api` call the user runs) |
| R14 — advisory `coverage` job; §22.1 target advisory; §21.6 `web` job struck | 4, 9 |
| R15 — `sign_release.sh` refuses sim and dirty, runs `size` (drv:M5) | 5a |
| R18 — spec §4.2, §4.6, §5.2, §11.4, §18.4, §19.1/§21.3, §21.2, §21.5, §23 | 9 (method built in 3) |
| R3 — Appendix A, §17.7, §17.2 numbers (app:M9/M10) | 9 |
| R31 — `measurements.md` one section per bench day; README; **roadmap rewritten as a state document** | 7 (README + measurements), 10 (roadmap) |
| R33 — `CONFIG_BT_ENABLED=n`, map verification, DRAM report, §4.8/§21.2, s5.2 note | 8 (build + measure), 9 (spec text), 10 (Plan 5.2 line) |
| R34 / design §6 — stale-comment grep as a hygiene check, `dist_prune.sh`, `build.sh` driver message, worktree removal, the user's `.xlsx` (ask, never delete), `sdkconfig.defaults.car`/`.moto` check, `cppcheck unusedFunction` | 13 |
| design §7b — a verdict comment on every open issue, with its owner | 12 |
| drv:M13 — CI gates what §17.9/§21.6 claim | 4 |
| drv:L24 — `gen_tracks.py` validates without `assert` (`-O` safe) | 5b |
| drv:L25 — `core_selftest` exclusions narrowed; §22.3 wording | 5c, 9 |
| drv:L26 — §23 tools table | 9 |
| drv:FC-5/FC-6 — the `ci.yml` and deviation-register false claims | 1 (Step 12), 4 (Step 3) |
| drv:FC-7/8 — §18.4 baud, serial OTA, command list (dbg text from F2b) | 9 |
| drv:FC-9/10/11/12 — §4.6 PANEL/flags/sample, §21.2, §19.1/§21.3, §17.8 | 9 |
| io:FC-8 — §5.2 blocks regenerated, the method defined and CI-gated | 3, 9 |
| app:L8 — §4.8 `cmd.c` statics + the link ring | 9 |
| pr:M5 — the venue linter accepted an off-path announce | 2 |
| pr:M6 — corrupt-blob coverage (record level here; file level filed) | 6, 14 |
| audit §3.1 item 4 — the memory STATE block | 14 (Step 3) |
| audit §3.3 items 27/29/30, §2 Plan 5.2 row — spec sections F4 does not own | 14 (issues), 10 (roadmap owners) |
| design §4 gates (host, linters, four firmware builds, DRAM report, bench items) | 14 |

**Gaps accepted, and why:** the GPS/IMU/storage fault ladders, `TIME_SYNC`, #54's hardware half and
#82 stay filed (design §5 and §7 own them); the §4.3 task map, §17.6 self-test rows and §20.7 menu
items are spec sections no plan in this wave owns, so Task 14 files them rather than editing them;
seven statement-count linter findings live in other plans' files and are graced with an issue, not
silently excluded.

**Placeholder scan:** no "TBD", no "implement later", no "similar to Task N". Every code step carries
the real file content; every verification step carries its command and the expected output measured
on `main` @ f4d4db2 (linter: 10 blocking findings + 85 report-only; `spec_api_check`: 41 drifts;
`dram0_0_seg`: `0x3ffbdb5c`/`0x1e6a4`; stale-comment grep: 19 hits in 11 files; `dist/`: 10 files;
`sdkconfig.defaults.car` vs `.moto`: one differing line). Where a value can only be read at execution
time (the merged tree's `--emit` output, Task 8's post-change DRAM figures, F2b's `dbg` help string),
the plan names the exact command that produces it and what to do with the result.

**Type/name consistency:** `--report-only-paths` / `--report-only-sites` / `--report-only-rules`,
`blank_dead_regions`, `count_statements`, `effective_lines`, `check_rule1_recursion`,
`check_rule9_sinks`, `FNPTR_SINKS`, `GRACE-STALE`, `spec_api_check.api_of` / `.emit` / `.main`,
`gen_tracks.GenError` / `.need`, `seed_one_user_venue`, `tools/dist_prune.sh`, the nine required-check
names and the ten graced site keys are spelled identically everywhere they appear.

**Deviation register:** no new rows. One existing row (PD-6) has its `file:symbol` key moved to the
site the tightened rule-9 check resolves. The check is
`python3 tools/lint/power_of_10.py --enforce-fnptr --json | python3 -c "import json,sys; print(json.load(sys.stdin)['register_rows'])"` → `9` before and after.
