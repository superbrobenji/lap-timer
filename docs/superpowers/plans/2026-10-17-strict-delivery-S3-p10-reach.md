# Strict Delivery S3 — Power of 10 Full Reach and the Exception Register — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Three blocks, one PR each: **b7** (Tasks 1–3), **b8** (Tasks 4–6), **b9** (Tasks 7–9); tags `strict-delivery-b7`, `-b8`, `-b9`.

**Goal:** Make `tools/lint/power_of_10.py` see what spec §9 says it sees (rules 1, 2, 3, 5, 9 at full reach, every C tree, no grace), wrap it as `tools/gates/gate_p10.py`, and turn `docs/power-of-10-deviations.md` into the two-tier, scored, expiring register of D14. Every existing violation the new reach surfaces becomes a findings-register row; none is fixed inline.

**Architecture:** The linter keeps its single-file, stdlib-first shape (PyYAML imported lazily for the two YAML inputs). New whole-scan-set passes (call graph, task entries, heap `#define` wrappers, function-pointer typedefs, pointed-to sets) run in `run()` next to the existing per-file checks. A violation that has an open row in `docs/superpowers/state/findings.yml` (its `repro` is the site key `p10-site:<file>:<symbol>:<RULE-n>`) is `[tracked]` and does not block; an open row whose site no longer violates is `TRACKED-STALE` and blocks. That is the only way existing code stays green between this plan and the S5 conformance sweep. The grace list goes in b9.

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6.0.2 from `tools/requirements.txt`), `python3 -m unittest`, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (§9, D13, D14, §13 rollout step 4). Umbrella: `docs/superpowers/plans/2026-10-17-strict-delivery.md` (Gate contract, `config.yml` `exceptions`, CLI conventions, S3 row). Background: `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` §2–§5; audit `docs/reviews/2026-10-16-audit-post-fix-wave.md` rows TCI-T1..T4 (§4 lines 387, 416, 460, 461) and §2.4 LG-2/3/4/6/7 (lines 188–193).

## Global Constraints

- Block = PR, ≤ 3 tasks; block ends in a clean tree; one block per context; the session stops after each block for the owner's `/clear` (spec §3.4–§3.5).
- Merge only on all-green under `grant:strict-delivery` (DEC-0001); tag `strict-delivery-b<N>` on the squash commit.
- Model map: implementers sonnet, task reviewers opus, final reviewer opus, final verifier Fable, exception reviewer Fable (umbrella Global Constraints).
- Zero gate failures at every commit once the gate exists; every Python failure path exits non-zero; a missing input is a FAIL (rc 2 from the linter, `FAIL p10.input` from the gate), never "nothing to do".
- **Owner ruling 3 (umbrella "Owner rulings on the writers' questions"):** a P10 violation in any file a task touches is FIXED in that task — the task is incomplete until every file it touches is clean under the full-reach linter; violations in untouched files are tracked as open findings rows (owner `{kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}`, added with the command in "Filing surfaced violations") until the conformance sweep; a stale or missing tracking row blocks. `gate_p10` enforces it: `FAIL p10.touched.<file>:<line>` for a violation in the staged/changed set (never excused by a row), `PASS p10.tracked.<id>` for a tracked violation elsewhere.
- **This plan touches no C/H file in a scanned tree.** Its C edits are fixtures under `tools/lint/fixtures/` (outside `DEFAULT_PATHS`). The per-file host-test allowance comment is therefore NOT added to existing test files here (that would touch 15 files carrying 252 non-heap violations, measured): their 49 rule-3 sites are filed as tracked rows, and the sweep adds the comment when it cleans each file. If a task must touch a scanned C file after all, its "touched-file rule" bullet applies: fix, never track.
- Pinned = production-path test + mutation transcript (spec D7): tests call `power_of_10.main([...])` / `gate_p10.main([...])` on fixtures, never a helper alone.
- No claim trusted (spec D3): every line number below was read on `pre-bench-wrap-up` @ `24aa0c7`; the implementer re-reads each one before editing (`sed -n`), and a mismatch is corrected here and ledgered.
- Hardware: nothing here flashes or opens a serial port.

## Review Focus

1. **A tracked violation in a touched file** must fail (ruling 3): `test_tracked_violation_in_an_untouched_file_passes_and_in_a_touched_file_fails`, `test_base_ref_adds_the_branch_diff_to_the_touched_set` (Task 6). Also **a cross-file rule run on a subset.** `gate_p10 --paths a.c` (pre-commit) scans only `a.c`, so the call graph, rule-9 `fn_defs` and pointed-to sets see one file; a stale-row check must ignore rows whose file was not scanned. Pinned by `test_stale_row_in_a_scanned_file_blocks_and_unscanned_file_is_ignored` (Task 1) and `test_paths_scope_to_the_named_c_files_only` (Task 6). CI always runs every tree.
2. **A findings row the S1 schema rejects.** `cites[].sha` and `history[].sha` must be quoted strings (a 7-digit sha parses as int), `owner.task` must be non-empty, `evidence` holds only shas or files — so the site key lives in `repro`. Pinned by `test_emit_findings_numbers_after_the_highest_s3_id_and_matches_the_schema` (Task 1) and by running `gate_register` after every filing.
3. **A wrapper that hides an allocator.** `#define ALLOC2(n) ALLOC(n)` with `#define ALLOC malloc`; a member call `p->free(x)` must not fire. Pinned by `test_r3_negative_…` / `test_r3_positive_…` (Task 3).
4. **A local variable shadowing a function name** (`int step = 0; x = step;`) must not become a rule-9 value finding, while `.pre_cb = pre_cb` must. Pinned by `test_r9_positive_…` (Task 5) and the existing `test_lg3_sink_and_stored_value_are_findings`.
5. **A register row that lies about its pointed-to set.** PD-5 lists 13 callbacks; a 14th `register_cmd(.., cmd_new)` must fail both as a value finding and as `REGISTER-SET`. Pinned by `test_a_callback_missing_from_the_listed_set_fails` (Task 7).

## Preconditions (check before Task 1; a FAIL stops the block)

S1 and S2 are merged (tags `strict-delivery-b1`..`b6`). Verify:
```bash
git tag -l 'strict-delivery-b[1-6]' | wc -l                          # 6
ls tools/gates/gate_register.py tools/gates/gate_checkpoint.py tools/gates/run_all.py tools/gates/config.yml
grep -n '^GATES' tools/gates/run_all.py                               # S1 Task 6 shape: GATES = [...]
grep -n 'PyYAML' tools/requirements.txt                               # PyYAML==6.0.2
python3 -c "import yaml; print(yaml.__version__)"                    # 6.0.x
python3 tools/gates/run_all.py                                        # PASS lines only
python3 -m unittest tools/lint/test_power_of_10.py                    # Ran 19 tests ... OK
grep -n 'exceptions:' -A2 tools/gates/config.yml                      # the umbrella scales, verbatim
```

**Owner decision needed (stop and ask before b7 merges):** spec D13 says "zero grace", yet rollout step 4 says surfaced violations "become register rows, not inline fixes" and the merge rule needs all-green. This plan resolves that by *tracking* (an open findings row keeps its site from blocking; a stale row blocks). That is an S3 design choice the spec does not spell out. File it as decision class `finding-class` with options "tracked rows (this plan)" / "CI red until S5" and wait for the owner's answer before the b7 PR is marked ready.

## Register rows for this plan's tasks (the b7 start step)

Before Task 1, the controller adds rows `SD-S3-T1`..`SD-S3-T9` exactly as S2 did for `SD-S2-T*` (S2 plan "Step 2" of its block start): `source: docs/superpowers/plans/2026-10-17-strict-delivery-S3-p10-reach.md`, `severity: INFO`, `class: "strict-delivery rollout step 4: <task title>"`, `cites: [{file: docs/superpowers/specs/2026-10-17-strict-delivery-design.md, lines: "227-241", sha: "<HEAD>"}]`, `owner: {kind: plan, plan: strict-delivery, task: "S3-T<n>"}`, `state: planned`, history `open` → `planned`. Then `python3 tools/gates/gate_register.py` (PASS only), commit `chore(state): register rows SD-S3-T1..T9 for strict-delivery S3`, and append `- b7 start <sha> rows SD-S3-T1..T9` to `docs/superpowers/state/strict-delivery/ledger.md` (commit `chore(state): b7 start`).

## Filing surfaced violations (used by Tasks 2, 3, 5, 6, 8)

After a task's code makes the linter see more, every untracked blocking site in the scanned trees is appended to the findings register by the linter itself, then the register gate re-checks it:

```bash
# b7/b8: the CI flag set of that block (Task 1 Step 7 / Task 6 Step 9 define it); b9: no grace flag
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation \
    --report-only-sites "$GRACE" \
    --findings docs/superpowers/state/findings.yml \
    --emit-findings --append-to docs/superpowers/state/findings.yml
python3 tools/gates/gate_register.py                  # PASS lines only
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation --report-only-sites "$GRACE" \
    --findings docs/superpowers/state/findings.yml ; echo rc=$?      # rc=0
```
`$GRACE` is the five-triple string from `.github/workflows/ci.yml:177` (until Task 8 deletes it):
```bash
GRACE="$(grep -o 'report-only-sites "[^"]*"' .github/workflows/ci.yml | cut -d'"' -f2)"
```
Each emitted row has this shape (one per `file:symbol:RULE` site; ids continue after the highest `S3-P10-<n>`):
```yaml
- id: S3-P10-12
  source: agent
  severity: MEDIUM            # components/** and main/**; LOW elsewhere
  class: "components/app/cmd/cmd.c:126: RULE-2: loop 'while (off < len)' has no statically visible bound (rule 2: a literal or #define/enum limit in the condition, or a bound assertion on len)"
  siblings: []
  cites: [{file: components/app/cmd/cmd.c, lines: "126", sha: "<short HEAD>"}]
  repro: "p10-site:components/app/cmd/cmd.c:emit_bytes:RULE-2"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<short HEAD>", by: agent}]
```
Commit the rows in the same commit as the code that surfaced them (the commit is red without them). Ledger the count: `- S3-T<n> filed S3-P10-<a>..<b> (<count> rows)`.

**Expected counts** (measured on a prototype of this plan's code against `24aa0c7`; the implementer records the real count, and a difference of more than 10 % is a ledgered surprise, not a stop):

| Block / task | Rule | Trees | Sites (rows) |
|---|---|---|---|
| b7 T1 | 1 (call graph, goto) | default trees (devcontroller still report-only) | 0 (927 functions, 0 cycles; the 3 `goto`s are in devcontroller) |
| b7 T2 | 2 | components/*, main | 67 (core 32, app 22, drivers 13) |
| b7 T3 | 3 | components/*, main | 0 |
| b8 T4 | 5 (tautology, conditional) | components/*, main | 2 (`sizeof == sizeof` asserts are constants) |
| b8 T5 | 9 (typedef params/fields, values, pointed-to) | components/*, main | 41 (app 26: 24 `cmd_emit_fn` params + field + `cmd.h`; drivers 15: 13 console callbacks, `ser_emit`, `sframe_emit`) |
| b8 T6 | all rules, newly scanned trees | devcontroller, test, test_apps, tools/replay, lt_hal | ≈ 593 (devcontroller 225, test ≈ 151, test_apps ≈ 9, tools/replay ≈ 208; no allowance comments added — ruling 3) |
| b9 T7/T8 | PD-7 unregistered; grace deleted | firmware | +1 (former PD-7 typedef site) and −13 closed (console callbacks covered by PD-5) in T7; +5 (G1..G5) in T8 |

---

## File structure

| File | Task | Responsibility |
|---|---|---|
| `tools/lint/power_of_10.py` | 1–8 | the linter (all rule passes, tracking, register) |
| `tools/lint/test_power_of_10.py` | 1–8 | production-path tests on fixtures |
| `tools/lint/fixtures/r1_neg.c`, `r1_neg_externc.h`, `r1_pos.c`, `r1_xfile_a.c`, `r1_xfile_b.c` | 1 | rule-1 probe pair (+ TCI-T4 p1/p6 probes) |
| `tools/lint/fixtures/findings_{tracked,stale,pinned,bad_site,numbered}.yml` | 1 | tracking inputs |
| `tools/lint/fixtures/r2_neg.c`, `r2_pos.c`, `register_r2.md` | 2 | rule-2 probe pair |
| `tools/lint/fixtures/r3_neg.c`, `r3_pos.c`, `register_r3.md` | 3 | rule-3 probe pair (TCI-T3 p2) |
| `tools/lint/fixtures/r5_neg.c`, `r5_pos.h` | 4 | rule-5 probe pair (TCI-T3 p5/p7) |
| `tools/lint/fixtures/r9_neg.c`, `r9_pos.c`, `register_r9.md`, `r9_console.c` | 5 | rule-9 probe pair (TCI-T4 p4, TCI-T2 shape) |
| `tools/lint/fixtures/r3_allow_test.c`, `r3_allow_misplaced.c`, `r5_unity.c` | 6 | allowance pair, Unity asserts |
| `tools/gates/gate_p10.py`, `tools/gates/tests/test_gate_p10.py` | 6 (8) | the gate |
| `tools/gates/run_all.py` | 6 | `"p10"` in `GATES` |
| `tools/gates/hooks/pre_tool_use.py`, `tools/gates/hooks/git-pre-commit.sh` | 6 | p10 on commit (staged files) |
| `.github/workflows/ci.yml`, `.github/workflows/gates.yml` | 1, 6, 8 | linter step → gate; grace deleted |
| `docs/power-of-10-deviations.md` | 8 | twelve-column register |
| `tools/lint/fixtures/register_*.md` | 7 | fixture registers in the new format |
| `docs/superpowers/state/findings.yml` | 2, 3, 5, 6, 8 | surfaced violations |
| `README.md`, `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md`, `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `docs/power-of-10-checklist.md` | 9 | docs say what the linter does |

---

## Block b7 — rules 1, 2, 3 (Tasks 1–3), branch `block/strict-delivery-b7`

### Task 1: Rule 1 over a call graph, `goto`/`longjmp`, findings-register tracking

**Findings:** SD-S3-T1 (closes the rule-1 half of TCI-T4 and LG-6; TCI-T1's hazard by construction)

**Files:**
- Modify: `tools/lint/power_of_10.py` — `find_top_level_functions` (:300-352), replace `check_rule1_recursion` (:588-598), `run()` (:807-902), `render_text()` (:905-941), `main()` (:944-1029)
- Create: `tools/lint/fixtures/r1_neg.c`, `r1_neg_externc.h`, `r1_pos.c`, `r1_xfile_a.c`, `r1_xfile_b.c`, `findings_tracked.yml`, `findings_stale.yml`, `findings_pinned.yml`, `findings_bad_site.yml`, `findings_numbered.yml`
- Modify: `tools/lint/fixtures/lg6_recursion.c:11` (its bounded loop gets a constant cap so the Rollout tests keep testing rule 1 only once rule 2 lands)
- Modify: `tools/lint/test_power_of_10.py` (append classes `Rule1CallGraph`, `Tracking`)
- Modify: `.github/workflows/ci.yml:133-140` (tools-tests: install requirements), `:174-177` (static-analysis: install requirements, pass `--findings`)

**Interfaces:**
- Consumes: S1 `docs/superpowers/state/findings.yml` (top-level YAML list, bootstrapped `[]`), `tools/requirements.txt` with PyYAML.
- Produces (used by Tasks 2–8): `symbol_at(funcs, pos) -> str`; every finding dict carries `symbol`; `run(...)` returns 5 values `(findings, summary, register, rule8a, scanned)` where `scanned` is the sorted POSIX relpaths; `InputError`; `load_findings_rows(path) -> list`; `tracked_sites(rows) -> {(file, symbol, rule): id}`; `next_s3_number(rows) -> int`; `append_rows(path, text)`; `site_key(f)`; `emit_findings_yaml(blocking, first_n, sha) -> str`; CLI `--findings PATH`, `--emit-findings`, `--append-to PATH`; output markers ` [tracked <id>]`, `TRACKED-STALE: <id> p10-site:<key> …`, JSON key `tracked_stale`; blocking rules tuple now includes `RULE-2`; `units` list and `comp_of` map inside `run()` for whole-set passes.

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Write the fixtures**

`tools/lint/fixtures/r1_neg.c`:
```c
/* probe (rule 1, negative -- must FAIL): indirect recursion, goto, longjmp (TCI-T4 p1). */
#include <setjmp.h>
static jmp_buf s_env;
static int is_odd(int n);
static int is_even(int n) { if (n == 0) { return 1; } return is_odd(n - 1); }
static int is_odd(int n) { if (n == 0) { return 0; } return is_even(n - 1); }
int r1_cleanup(int a)
{
    if (a < 0) { goto out; }
    a = a + 1;
out:
    return a;
}
void r1_bail(void) { longjmp(s_env, 1); }
int r1_entry(int n) { return is_even(n) + r1_cleanup(n); }
```
`tools/lint/fixtures/r1_neg_externc.h`:
```c
/* probe (rule 1, negative -- must FAIL): recursion inside an extern "C" block (TCI-T4 p6). */
#ifdef __cplusplus
extern "C" {
#endif
static inline int r1_inline_rec(int n) { return n ? r1_inline_rec(n - 1) : 0; }
#ifdef __cplusplus
}
#endif
```
`tools/lint/fixtures/r1_pos.c`:
```c
/* probe (rule 1, positive -- must PASS): a call chain without a cycle; a member call that
 * shares the enclosing function's name is a function-pointer call, not recursion. */
struct ops { int (*step)(int); };
static int leaf(int n) { return n + 1; }
static int middle(int n) { return leaf(n) * 2; }
int step(const struct ops *o, int n) { return o->step(n) + middle(n); }
```
`tools/lint/fixtures/r1_xfile_a.c`:
```c
/* probe (rule 1): one half of a cycle that crosses files. */
int pong(int n);
int ping(int n) { return n > 0 ? pong(n - 1) : 0; }
```
`tools/lint/fixtures/r1_xfile_b.c`:
```c
/* probe (rule 1): the other half of the cross-file cycle. */
int ping(int n);
int pong(int n) { return n > 0 ? ping(n - 1) : 0; }
```
`tools/lint/fixtures/findings_tracked.yml`:
```yaml
# fixture: findings-register rows the linter's --findings reads (id, state, evidence only)
- id: TST-P10-1
  state: open
  repro: "p10-site:tools/lint/fixtures/lg6_recursion.c:walk:RULE-1"
```
`tools/lint/fixtures/findings_stale.yml`:
```yaml
# fixture: one live row, one stale row in a scanned file, one row in an unscanned file
- id: TST-P10-1
  state: open
  repro: "p10-site:tools/lint/fixtures/lg6_recursion.c:walk:RULE-1"
- id: TST-P10-2
  state: in-progress
  repro: "p10-site:tools/lint/fixtures/lg6_recursion.c:gone:RULE-1"
- id: TST-P10-3
  state: open
  repro: "p10-site:tools/lint/fixtures/lg1_heap_only.c:g:RULE-3"
```
`tools/lint/fixtures/findings_pinned.yml`:
```yaml
# fixture: a pinned row no longer covers its site -- a recurrence blocks
- id: TST-P10-4
  state: pinned
  repro: "p10-site:tools/lint/fixtures/lg6_recursion.c:walk:RULE-1"
```
`tools/lint/fixtures/findings_bad_site.yml`:
```yaml
# fixture: an evidence entry that is not file:symbol:RULE
- id: TST-P10-5
  state: open
  repro: "p10-site:nonsense"
```
`tools/lint/fixtures/findings_numbered.yml`:
```yaml
# fixture: the highest S3-P10 number is 41, so --emit-findings starts at 42
- id: S3-P10-41
  state: closed
  evidence: []
- id: S3-P10-G1
  state: open
  evidence: []
```
In `tools/lint/fixtures/lg6_recursion.c` change line 11 from `    for (int i = 0; i < d; i++) { t += i; }` to `    for (int i = 0; i < d && i < 64; i++) { t += i; }`.

- [ ] **Step 2: Write the failing tests** — append before `if __name__ == "__main__":` in `tools/lint/test_power_of_10.py`:

```python
class Rule1CallGraph(unittest.TestCase):
    def test_r1_negative_indirect_recursion_goto_longjmp_fail(self):
        rc, out = run(["--paths", FIX + "/r1_neg.c", "--register", NONE, "--fail-on-violation"])
        self.assertIn("function 'is_even' is on a call cycle is_even -> is_odd -> is_even", out)
        self.assertIn("function 'is_odd' is on a call cycle", out)
        self.assertIn("RULE-1: 'goto' (rule 1: simple control flow)", out)
        self.assertIn("RULE-1: 'longjmp(' (rule 1: simple control flow)", out)
        self.assertEqual(4, len(findings(out)), out)
        self.assertEqual(1, rc)

    def test_r1_negative_recursion_inside_extern_c_block_fails(self):
        rc, out = run(["--paths", FIX + "/r1_neg_externc.h", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("function 'r1_inline_rec' calls itself", out)
        self.assertEqual(1, rc)

    def test_r1_positive_chain_and_member_call_pass(self):
        rc, out = run(["--paths", FIX + "/r1_pos.c", "--register", NONE, "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_r1_cycle_across_two_files_is_seen(self):
        rc, out = run(["--paths", FIX + "/r1_xfile_a.c", FIX + "/r1_xfile_b.c", "--register",
                       NONE, "--fail-on-violation"])
        self.assertIn("function 'ping' is on a call cycle", out)
        self.assertIn("function 'pong' is on a call cycle", out)
        self.assertEqual(1, rc)


class Tracking(unittest.TestCase):
    """--findings: an open findings-register row tracks a violation; a stale row blocks."""

    def test_open_row_tracks_the_violation_and_does_not_block(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--findings", FIX + "/findings_tracked.yml"])
        self.assertIn("calls itself (rule 1: no recursion) [tracked TST-P10-1]", out)
        self.assertEqual(0, rc)

    def test_stale_row_in_a_scanned_file_blocks_and_unscanned_file_is_ignored(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--findings", FIX + "/findings_stale.yml"])
        self.assertIn("TRACKED-STALE: TST-P10-2 p10-site:" + FIX + "/lg6_recursion.c:gone:RULE-1",
                      out)
        self.assertNotIn("TST-P10-3", out)
        self.assertEqual(1, rc)

    def test_pinned_row_does_not_track_a_recurrence(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--findings", FIX + "/findings_pinned.yml"])
        self.assertNotIn("[tracked", out)
        self.assertEqual(1, rc)

    def test_missing_findings_register_is_rc_2(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--findings", FIX + "/does_not_exist.yml"])
        self.assertIn("INPUT-ERROR: findings register", out)
        self.assertEqual(2, rc)

    def test_malformed_site_is_rc_2(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation", "--findings", FIX + "/findings_bad_site.yml"])
        self.assertIn("INPUT-ERROR: row TST-P10-5", out)
        self.assertEqual(2, rc)

    def test_stale_row_is_in_json_and_json_stays_parseable(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE, "--json",
                       "--findings", FIX + "/findings_stale.yml"])
        data = json.loads(out)
        self.assertEqual([{"id": "TST-P10-2", "site": FIX + "/lg6_recursion.c:gone:RULE-1"}],
                         data["tracked_stale"])
        self.assertEqual(0, rc)

    def test_emit_findings_numbers_after_the_highest_s3_id_and_matches_the_schema(self):
        import yaml
        rc, out = run(["--paths", FIX + "/r1_neg.c", "--register", NONE, "--fail-on-violation",
                       "--findings", FIX + "/findings_numbered.yml", "--emit-findings"])
        rows = yaml.safe_load(out)
        self.assertEqual(0, rc)
        self.assertEqual(["S3-P10-42", "S3-P10-43", "S3-P10-44", "S3-P10-45"],
                         [r["id"] for r in rows])
        r = rows[0]
        self.assertEqual({"kind": "plan", "task": "conformance", "plan":
                          "docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md"},
                         r["owner"])
        self.assertEqual("open", r["state"])
        self.assertEqual("p10-site:" + FIX + "/r1_neg.c:is_even:RULE-1", r["repro"])
        self.assertEqual([], r["evidence"])
        self.assertIsInstance(r["cites"][0]["sha"], str)
        self.assertTrue(r["class"].startswith(FIX + "/r1_neg.c:5: RULE-1: function 'is_even'"))
        self.assertEqual(r["state"], r["history"][-1]["state"])
        for key in ("id", "source", "severity", "class", "siblings", "cites", "repro", "owner",
                    "state", "pin", "ruling", "evidence", "history"):
            self.assertIn(key, r)

    def test_append_to_an_empty_register_replaces_the_empty_list_and_reparses(self):
        import shutil, tempfile, yaml
        tmp = tempfile.mkdtemp()
        try:
            reg = os.path.join(tmp, "findings.yml")
            with open(reg, "w") as f:
                f.write("# findings register\n[]\n")
            rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                           "--fail-on-violation", "--findings", reg, "--emit-findings",
                           "--append-to", reg])
            self.assertEqual(0, rc, out)
            self.assertIn("appended 1 row(s)", out)
            with open(reg) as f:
                rows = yaml.safe_load(f)
            self.assertEqual(["S3-P10-1"], [r["id"] for r in rows])
            rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                           "--fail-on-violation", "--findings", reg])
            self.assertIn("[tracked S3-P10-1]", out)
            self.assertEqual(0, rc)
        finally:
            shutil.rmtree(tmp)
```

- [ ] **Step 3: Run them and see them fail**

Run: `python3 -m unittest tools/lint/test_power_of_10.py -v 2>&1 | tail -5`
Expected: FAIL/ERROR in every new test (`is on a call cycle` not found; `unrecognized arguments: --findings`); the 19 old tests pass.

- [ ] **Step 4: Implement**

4a. `find_top_level_functions` — `extern "C" {` is transparent (current :308-352):
```diff
@@ -308,6 +374,12 @@
     i = 0
     while i < n:
         c = stripped[i]
+        if c == "{" and depth == 0 and _ident_before(stripped, i)[0] == "extern":
+            # `extern "C" {` (the string is blanked): a linkage block, not a scope -- functions
+            # inside it are still top-level definitions (TCI-T4 probe p6_externc.h)
+            stack.append({"is_func": False, "transparent": True})
+            i += 1
+            continue
         if c == "{":
             is_func = False
             name = None
@@ -341,12 +413,12 @@
                           "param_open": param_open, "param_close": param_close})
             depth += 1
         elif c == "}":
-            depth -= 1
-            if stack:
-                entry = stack.pop()
-                if entry["is_func"]:
-                    funcs.append((entry["name"], entry["name_start"], entry["open"], i,
-                                  entry["param_open"], entry["param_close"]))
+            entry = stack.pop() if stack else {"is_func": False}
+            if not entry.get("transparent"):
+                depth -= 1
+            if entry["is_func"]:
+                funcs.append((entry["name"], entry["name_start"], entry["open"], i,
+                              entry["param_open"], entry["param_close"]))
         i += 1
     funcs.sort(key=lambda f: f[2])
     return funcs
```

4b. Insert above `_BRACE_ONLY_RE = re.compile(...)` (:373):
```python
def symbol_at(funcs, pos):
    """The site symbol for a finding at `pos`: the function whose body or parameter list holds
    it, else "" (file scope). Every finding carries one, so the findings register can key a
    tracked violation as `file:symbol:RULE`."""
    name = _enclosing_function_by_body(funcs, pos)
    if name is None:
        name = _enclosing_function_name(funcs, pos)
    return name or ""
```

4c. Replace `check_rule1_recursion` (:588-598, quoted here) …
```python
def check_rule1_recursion(stripped, relpath, lidx, findings, register, funcs):
    """RULE-1 (LG-6): direct recursion -- a function body that calls its own name. Indirect
    recursion stays with the review checklist (clang-tidy misc-no-recursion is advisory)."""
    for name, _name_start, open_idx, close_idx, _po, _pc in funcs:
        ...
```
… with:
```python
_JUMP_RE = re.compile(r"(?<![A-Za-z0-9_])(?:(goto)\b|(setjmp|longjmp|sigsetjmp|siglongjmp|"
                      r"_setjmp|_longjmp)\s*\()")
def check_rule1_jumps(stripped, relpath, lidx, findings, register, funcs):
    """RULE-1: no `goto`, no setjmp/longjmp family (design §3 rule 1, spec §9)."""
    for m in _JUMP_RE.finditer(stripped):
        word = m.group(1) or m.group(2) + "("
        symbol = symbol_at(funcs, m.start())
        if register.is_registered(1, relpath, symbol):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(m.start()), "rule": "RULE-1",
                         "symbol": symbol,
                         "message": "'%s' (rule 1: simple control flow)" % word})

_CALL_RE = re.compile(r"(?<![A-Za-z0-9_.>])([A-Za-z_][A-Za-z0-9_]*)\s*\(")
def _sccs(graph):
    """Tarjan's strongly-connected components, iterative (no Python recursion limit)."""
    index, low, on, stack, out, counter = {}, {}, set(), [], [], [0]
    for root in sorted(graph):
        if root in index:
            continue
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on.add(root)
        work = [(root, iter(sorted(graph[root])))]
        while work:
            v, it = work[-1]
            pushed = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(sorted(graph[w]))))
                    pushed = True
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            if pushed:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                out.append(sorted(comp))
    return out

def check_rule1_callgraph(units, register):
    """RULE-1 over the whole scan set (spec §9): direct AND indirect recursion. `units` is a list
    of (relpath, stripped, lidx, funcs). A call `name(` resolves to the definition in the same
    file when there is one (a `static` helper), else to every scanned definition of that name.
    Member calls (`s.op(`/`s->op(`) are not edges -- those are function pointers (rule 9)."""
    defs = {}
    for relpath, _s, _l, funcs in units:
        for f in funcs:
            defs.setdefault(f[0], []).append(relpath)
    graph, first_call, where = {}, {}, {}
    for relpath, stripped, lidx, funcs in units:
        for name, name_start, open_idx, close_idx, _po, _pc in funcs:
            node = (relpath, name)
            where[node] = lidx.line_of(name_start if name_start is not None else open_idx)
            edges = graph.setdefault(node, set())
            for m in _CALL_RE.finditer(stripped, open_idx, close_idx):
                callee = m.group(1)
                if callee in _RESERVED or callee not in defs:
                    continue
                targets = [relpath] if relpath in defs[callee] else defs[callee]
                for t in targets:
                    edges.add((t, callee))
                    first_call.setdefault((node, (t, callee)), lidx.line_of(m.start()))
    findings = []
    for comp in _sccs(graph):
        if len(comp) == 1 and comp[0] not in graph[comp[0]]:
            continue
        for node in comp:
            relpath, name = node
            if register.is_registered(1, relpath, name):
                continue
            if len(comp) == 1:
                findings.append({"file": relpath, "line": first_call[(node, node)],
                                 "rule": "RULE-1", "symbol": name,
                                 "message": "function '%s' calls itself (rule 1: no recursion)" % name})
            else:
                cycle = " -> ".join(n for _f, n in comp) + " -> " + comp[0][1]
                findings.append({"file": relpath, "line": where[node], "rule": "RULE-1",
                                 "symbol": name,
                                 "message": "function '%s' is on a call cycle %s (rule 1: no "
                                            "recursion)" % (name, cycle)})
    return findings
```

4d. Insert after `run()` (after :902), before `render_text`:
```python
TRACKABLE_STATES = ("open", "planned", "in-progress", "deferred")
SITE_PREFIX = "p10-site:"
class InputError(Exception):
    """A missing/unreadable input: main() prints it and returns 2 (fail closed)."""

def load_findings_rows(path):
    """The findings register's rows (a top-level YAML list, or a mapping with a `findings`
    list). Raises InputError for a missing, unparsable or wrongly shaped file."""
    if not os.path.isfile(path):
        raise InputError("findings register %s not found" % path)
    try:
        import yaml
    except ImportError as exc:
        raise InputError("PyYAML missing (pip install -r tools/requirements.txt): %s" % exc)
    with open(path, "r", encoding="utf-8") as f:
        try:
            data = yaml.safe_load(f)
        except yaml.YAMLError as exc:
            raise InputError("findings register %s: %s" % (path, exc))
    rows = data.get("findings", data) if isinstance(data, dict) else data
    if rows is None:
        rows = []
    if not isinstance(rows, list):
        raise InputError("findings register %s is not a YAML list" % path)
    return rows

def tracked_sites(rows):
    """{(file, symbol, rule): finding_id} for every row in a trackable state whose `repro` is
    a p10 site key `p10-site:<file>:<symbol>:<RULE-n>`. Raises InputError for a malformed site string."""
    tracked = {}
    for row in rows:
        if not isinstance(row, dict) or row.get("state") not in TRACKABLE_STATES:
            continue
        ev = row.get("repro")
        if isinstance(ev, str) and ev.startswith(SITE_PREFIX):
            parts = ev[len(SITE_PREFIX):].rsplit(":", 2)
            if len(parts) != 3 or not parts[0] or not parts[2].startswith("RULE-"):
                raise InputError("row %s: bad repro %r (want p10-site:file:symbol:RULE-n)"
                                 % (row.get("id"), ev))
            tracked[tuple(parts)] = row.get("id")
    return tracked

def next_s3_number(rows):
    """1 + the highest numeric S3-P10-<n> id in the register (1 when there is none)."""
    nums = [int(m.group(1)) for r in rows if isinstance(r, dict)
            for m in [re.match(r"^S3-P10-(\d+)$", str(r.get("id", "")))] if m]
    return max(nums) + 1 if nums else 1

def append_rows(path, text):
    """Append emitted rows to a top-level-list findings register (an empty `[]` body is
    replaced), then re-parse it; raises InputError if the result does not load."""
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    if re.search(r"^findings\s*:", body, re.MULTILINE):
        raise InputError("%s is a mapping; append under its findings: key by hand" % path)
    if body.rstrip().endswith("[]"):
        body = body.rstrip()[:-2]
    body = body.rstrip("\n")
    with open(path, "w", encoding="utf-8") as f:
        f.write((body + "\n" if body.strip() else body) + text + "\n")
    load_findings_rows(path)

def site_key(f):
    return (f["file"].replace(os.sep, "/"), f.get("symbol", ""), f["rule"])

def emit_findings_yaml(blocking, first_n, sha):
    """YAML rows (strict-delivery umbrella finding schema) for every untracked blocking site,
    one row per file:symbol:RULE, numbered S3-P10-<first_n>.. (see --emit-findings/--append-to)."""
    seen, out, n = set(), [], first_n
    for f in blocking:
        key = site_key(f)
        if key in seen:
            continue
        seen.add(key)
        sev = "MEDIUM" if key[0].startswith(("components/", "main/")) else "LOW"
        cls = "%s:%d: %s: %s" % (key[0], f["line"], f["rule"], f["message"])
        out.append("\n".join([
            "- id: S3-P10-%d" % n,
            "  source: agent",
            "  severity: %s" % sev,
            "  class: %s" % json.dumps(cls),
            "  siblings: []",
            "  cites: [{file: %s, lines: \"%d\", sha: \"%s\"}]" % (key[0], f["line"], sha),
            "  repro: %s" % json.dumps(SITE_PREFIX + ":".join(key)),
            "  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}",
            "  state: open",
            "  pin: null",
            "  ruling: null",
            "  evidence: []",
            "  history: [{state: open, sha: \"%s\", by: agent}]" % sha,
        ]))
        n += 1
    return "\n".join(out)
```

4e. `run()` — collect units, run the call graph, return the scanned set:
```diff
@@ run(): before the per-file loop
     rule8a_directives = 0
     rule8a_headers = 0
+    units = []          # (relpath, stripped, lidx, funcs) for the whole-scan-set passes
+    comp_of = {}        # relpath -> component
@@ the rule-1 call inside the loop
         rule1_findings = []
-        check_rule1_recursion(stripped, relpath, lidx, rule1_findings, register, funcs)
+        check_rule1_jumps(stripped, relpath, lidx, rule1_findings, register, funcs)
         comp["rule1"] += len(rule1_findings)
         all_findings.extend(rule1_findings)
+        units.append((relpath, stripped, lidx, funcs))
+        comp_of[relpath] = component
@@ after the loop, before the sort
+    for f in check_rule1_callgraph(units, register):
+        per_component[comp_of[f["file"]]]["rule1"] += 1
+        if any(f["file"].replace(os.sep, "/").startswith(p) for p in report_only_paths):
+            f["report_only"] = True
+        all_findings.append(f)
+
     all_findings.sort(key=lambda f: (f["file"], f["line"], f["rule"]))
@@ the return
-    return all_findings, summary, register, rule8a
+    scanned = sorted(rel.replace(os.sep, "/") for _c, _p, rel in files)
+    return all_findings, summary, register, rule8a, scanned
```

4f. `render_text` — the tracked marker, after the `mark = ...` line (:908):
```diff
         mark = " [report-only]" if (f.get("report_only") or f["rule"] in report_only_rules) else ""
+        if f.get("tracked"):
+            mark += " [tracked %s]" % f["tracked"]
```

4g. `main()` — new flags after `--report-only-rules` (:965-966), tracking after the grace parse, the emit path, `TRACKED-STALE` (all exact):
```diff
     ap.add_argument("--report-only-rules", default="",
                      help="Comma-separated rule ids reported but never failing the build.")
+    ap.add_argument("--findings", default=None,
+                     help="Findings register (YAML). A violation whose file:symbol:RULE has an "
+                          "open row (repro `p10-site:...`) is [tracked] and does not block; an "
+                          "open row whose scanned site no longer violates is TRACKED-STALE (blocks).")
+    ap.add_argument("--emit-findings", action="store_true",
+                     help="Print only YAML findings rows (S3-P10-<n>, numbered after the highest "
+                          "in --findings) for every untracked blocking site, then exit 0.")
+    ap.add_argument("--append-to", default=None, metavar="FINDINGS_YML",
+                     help="With --emit-findings: append the rows to this register instead of "
+                          "printing them, and re-parse it.")
     args = ap.parse_args(argv)
@@ after the grace-list parse loop (:972-980)
-    findings, summary, register, rule8a = run(args.paths, args.register, args.enforce_fnptr,
-                                               base_dir, tuple(args.report_only_paths))
+    try:
+        rows = load_findings_rows(args.findings) if args.findings else []
+        tracked = tracked_sites(rows)
+    except InputError as exc:
+        print("INPUT-ERROR: %s" % exc)
+        return 2
+    findings, summary, register, rule8a, scanned = run(args.paths, args.register,
+                                                        args.enforce_fnptr, base_dir,
+                                                        tuple(args.report_only_paths))
@@ the marking loop (:996-998)
     for f in findings:
         if graced(f) or f["rule"] in report_only_rules:
             f["report_only"] = True
+        if site_key(f) in tracked:
+            f["tracked"] = tracked[site_key(f)]
+    hit = {site_key(f) for f in findings}
+    scanned_set = set(scanned)
+    stale = sorted((fid, key) for key, fid in tracked.items()
+                   if key[0] in scanned_set and key not in hit)
+    blocking_rules = ("RULE-1", "RULE-2", "RULE-3", "RULE-4", "RULE-4-COMPOUND", "RULE-5",
+                      "RULE-8", "RULE-9")
+    blocking = [f for f in findings
+                if f["rule"] in blocking_rules
+                and f["rule"] not in report_only_rules
+                and not f.get("report_only")
+                and not f.get("tracked")]
+
+    if args.emit_findings:
+        import subprocess
+        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
+                             text=True, check=False).stdout.strip()
+        if not sha:
+            print("INPUT-ERROR: git rev-parse HEAD failed")
+            return 2
+        text = emit_findings_yaml(blocking, next_s3_number(rows), sha)
+        if args.append_to and text:
+            try:
+                append_rows(args.append_to, text)
+            except InputError as exc:
+                print("INPUT-ERROR: %s" % exc)
+                return 2
+            print("appended %d row(s) to %s" % (text.count("- id: "), args.append_to))
+        elif text:
+            print(text)
+        return 0
@@ the JSON dict
             "findings": findings,
+            "tracked_stale": [{"id": fid, "site": ":".join(key)} for fid, key in stale],
@@ the exit block (:1013-1029)
+    for fid, key in ([] if args.json else stale):
+        print("TRACKED-STALE: %s %s%s matches no finding -- advance the row (fixed-unpinned/"
+              "pinned) or restore the site" % (fid, SITE_PREFIX, ":".join(key)))
     if args.fail_on_violation:
-        blocking_rules = ("RULE-1", "RULE-3", "RULE-4", "RULE-4-COMPOUND", "RULE-5", "RULE-8",
-                          "RULE-9")
         used = {(p, sy, r) for (p, sy, r) in grace for f in findings
@@
-        blocking = [f for f in findings
-                    if f["rule"] in blocking_rules
-                    and f["rule"] not in report_only_rules
-                    and not f.get("report_only")]
-        if blocking:
-            print("\nBLOCKING: %d violation(s)" % len(blocking))
+        if blocking or stale:
+            if not args.json:
+                print("\nBLOCKING: %d violation(s), %d stale tracked row(s)"
+                      % (len(blocking), len(stale)))
             return 1
```
Also update the module docstring's RULE-1 paragraph (:11-12) to: `RULE-1  recursion over the call graph of the whole scan set (direct and indirect), and any goto / setjmp / longjmp.`

- [ ] **Step 5: Run the tests**

Run: `python3 -m unittest tools/lint/test_power_of_10.py 2>&1 | tail -3`
Expected: `Ran 31 tests` … `OK`.

- [ ] **Step 6: Mutation transcript** — in `check_rule1_callgraph` change `if len(comp) == 1 and comp[0] not in graph[comp[0]]:` to `if len(comp) <= 2:`; `test_r1_negative_indirect_recursion_goto_longjmp_fail` and `test_r1_cycle_across_two_files_is_seen` must FAIL; restore; PASS. Then in `tracked_sites` drop the `row.get("state") not in TRACKABLE_STATES` test; `test_pinned_row_does_not_track_a_recurrence` must FAIL; restore. Write `docs/superpowers/state/strict-delivery/mutations/SD-S3-T1.txt` in the umbrella format.

- [ ] **Step 7: CI and live tree** — in `.github/workflows/ci.yml`:
  - tools-tests (:133): insert before `- name: python3 -m unittest …` the step `- name: Install Python requirements` / `run: python3 -m pip install -r tools/requirements.txt`.
  - static-analysis: insert the same step before `- name: power_of_10.py linter …` (:174), and append ` \` + `            --findings docs/superpowers/state/findings.yml` to the `run:` (:176-177).

Run the CI line locally:
```bash
GRACE="$(grep -o 'report-only-sites "[^"]*"' .github/workflows/ci.yml | cut -d'"' -f2)"
python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation --report-only-sites "$GRACE" \
    --findings docs/superpowers/state/findings.yml | grep -E 'RULE-1|BLOCKING|TRACKED'; echo rc=$?
```
Expected: three `RULE-1: 'goto'` lines under `devcontroller/` marked `[report-only]`, no BLOCKING, and the linter rc 0 (`grep`'s own rc may be 0). Nothing to file (0 new firmware sites).

- [ ] **Step 8: Commit**
```bash
git add tools/lint/power_of_10.py tools/lint/test_power_of_10.py tools/lint/fixtures .github/workflows/ci.yml docs/superpowers/state/strict-delivery/mutations/SD-S3-T1.txt
git commit -m "feat(p10): rule 1 over the whole-scan-set call graph, goto/longjmp, findings-register tracking (S3 T1)"
```
Ledger: `- S3-T1 DONE <sha> rule-1 call graph + tracking; filed 0`.

### Task 2: Rule 2 — every loop statically bounded or bound-asserted

**Findings:** SD-S3-T2 (rule-2 half of LG-6; compliance design §3 rule 2 moves from checklist to linter)

**Files:**
- Modify: `tools/lint/power_of_10.py` (new block above `check_rule3_heap`, :601; `run()` pre-pass :813-820 and loop; `render_text` totals :934)
- Create: `tools/lint/fixtures/r2_neg.c`, `r2_pos.c`, `register_r2.md`
- Modify: `tools/lint/test_power_of_10.py` (class `Rule2Loops`), `docs/superpowers/state/findings.yml` (filed rows)

**Interfaces:**
- Consumes: Task 1 `symbol_at`, `_enclosing_function_by_body`, `_arg_spans`, `ASSERT_RE`, `_SINK_RE`, `_IDENT_ONLY_RE`.
- Produces: `is_const_expr(text) -> bool`, `split_top(text, sep) -> list`, `strip_parens(text)`, `relational(text) -> (lhs, op, rhs) | None`, `_macro_at(stripped, pos) -> str | None` (Tasks 3–5 use all of these); `check_rule2_loops(stripped, relpath, lidx, findings, register, funcs, task_entries)`; `run()` locals `fn_defs` (now always built, POSIX paths) and `task_entries {name: [defining files]}`; summary key `rule2`.

Rule (spec §9 rule 2): a `for`/`while`/`do` passes when its controlling expression has a top-level conjunct `x < LIMIT` / `x <= LIMIT` (or `LIMIT > x`) where LIMIT is built only from integer literals, ALL_CAPS identifiers (`#define`/enum by project convention) and `sizeof`; or a count-down `x > CONST` whose `for` init sets `x` to a constant; or, when the limit is a variable, an assertion in the enclosing function before the loop ends that compares one of the limit's identifiers with a constant. `while (0)` passes. An infinite form (`for (;;)`, `while (1)`, any non-zero constant) passes only in a registered task entry (passed as `xTaskCreate*` argument 0 **and** registered under rule 9) or when its own body asserts a counter against a constant limit (the R27 idiom at `components/app/cmd/cmd.c:446-450`). Known limit, ledgered: an ALL_CAPS local `const` computed at run time (`ses_frame.c:103 NEXT_MAX_STEPS`) is accepted as a constant.

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixtures**

`tools/lint/fixtures/r2_neg.c`:
```c
/* probe (rule 2, negative -- must FAIL): five loops with no statically visible bound. */
int r2_param_limit(const int *v, int n)
{
    int t = 0;
    for (int i = 0; i < n; i++) { t += v[i]; }
    return t;
}
int r2_string_scan(const char *s)
{
    int k = 0;
    while (*s) { s++; k++; }
    return k;
}
void r2_forever(volatile int *flag)
{
    for (;;) { if (*flag) { break; } }
}
int r2_countdown(int n)
{
    int t = 0;
    while (n-- > 0) { t++; }
    return t;
}
int r2_countdown_for(int n)
{
    int t = 0;
    for (int i = n; i > 0; i--) { t++; }
    return t;
}
```
`tools/lint/fixtures/r2_pos.c`:
```c
/* probe (rule 2, positive -- must PASS): every loop has a visible bound. */
#define R2_MAX 16
static unsigned char s_buf[32];
static void r2_task(void *arg);
int r2_const_limit(const int *v)
{
    int t = 0;
    for (int i = 0; i < R2_MAX; i++) { t += v[i]; }
    for (unsigned i = 0; i < sizeof s_buf / sizeof s_buf[0]; i++) { t += s_buf[i]; }
    for (int i = 8; i > 0; i--) { t++; }
    do { t = t + 0; } while (0);
    return t;
}
int r2_asserted(const int *v, int n)
{
    CORE_ASSERT_RET(n <= R2_MAX, 1, -1);
    int t = 0;
    for (int i = 0; i < n; i++) { t += v[i]; }
    return t;
}
int r2_capped_forever(void)
{
    int steps = 0;
    for (;;) {
        CORE_ASSERT_RET(steps++ < R2_MAX, 1, -1);
        if (steps == 3) { return steps; }
    }
}
static void r2_task(void *arg)
{
    (void)arg;
    for (;;) { }
}
void r2_start(void)
{
    (void)xTaskCreateStatic(r2_task, "r2", 512, 0, 1, 0, 0);
}
```
`tools/lint/fixtures/register_r2.md` (pre-b9 seven-column format; Task 7 converts it):
```
| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
| TD-5 | 9 | tools/lint/fixtures/r2_pos.c:r2_task | n/a (fixture) | n/a | n/a | fixture |
```

- [ ] **Step 2: Failing tests** (append):
```python
class Rule2Loops(unittest.TestCase):
    def test_r2_negative_five_unbounded_loops_fail(self):
        rc, out = run(["--paths", FIX + "/r2_neg.c", "--register", NONE, "--fail-on-violation"])
        got = [l for l in findings(out) if ": RULE-2: " in l]
        self.assertEqual(5, len(got), out)
        self.assertIn("loop 'while (*s)' has no statically visible bound", out)
        self.assertIn("infinite loop 'for (;;)' outside a registered task entry", out)
        self.assertIn("loop 'while (n-- > 0)'", out)
        self.assertIn("loop 'for (int i = n; i > 0; i--)'", out)
        self.assertEqual(1, rc)

    def test_r2_positive_constant_sizeof_countdown_asserted_capped_task_pass(self):
        rc, out = run(["--paths", FIX + "/r2_pos.c", "--register", FIX + "/register_r2.md",
                       "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_r2_task_entry_needs_its_register_row(self):
        rc, out = run(["--paths", FIX + "/r2_pos.c", "--register", NONE, "--fail-on-violation"])
        self.assertIn("r2_pos.c:32: RULE-2: infinite loop 'for (;;)'", out)
        self.assertEqual(1, rc)
```
Run: `python3 -m unittest tools/lint/test_power_of_10.py 2>&1 | tail -3` — Expected: 3 failures in `Rule2Loops`.

- [ ] **Step 3: Implement** — insert above `def check_rule3_heap` (:601):
```python
_LOOP_RE = re.compile(r"(?<![A-Za-z0-9_])(for|while)\s*\(")
_NUM_RE = re.compile(r"(?<![A-Za-z0-9_])(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*(?![A-Za-z0-9_])")
_SIZEOF_RE = re.compile(r"\bsizeof\s*\((?:[^()]|\([^()]*\))*\)|"
                        r"\bsizeof\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\]|\s*(?:\.|->)\s*[A-Za-z_][A-Za-z0-9_]*)*")
_CAST_RE = re.compile(r"\(\s*(?:const\s+)?(?:unsigned|signed|int|long|short|char|size_t|ssize_t|"
                      r"u?int(?:8|16|32|64)_t|float|double|uint|bool)(?:\s+(?:int|long))*\s*\)")
_CONST_TEXT_RE = re.compile(r"^[\s()0-9A-Z_+\-*/%=!<>&|~^]+$")
_WORD_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_C_WORDS = {"sizeof", "NULL", "true", "false", "int", "unsigned", "signed", "long", "short",
            "char", "float", "double", "const", "size_t", "uint8_t", "uint16_t", "uint32_t",
            "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "bool"}
def is_const_expr(text):
    """True when `text` is built only from integer literals, ALL_CAPS identifiers (a #define or
    enum constant by project convention), sizeof(...), true/false, scalar casts and arithmetic."""
    t = _SIZEOF_RE.sub("1", text)
    t = _CAST_RE.sub(" ", t)
    t = re.sub(r"\b(?:true|false)\b", "1", t)
    t = _NUM_RE.sub("1", t)
    return bool(t.strip()) and bool(_CONST_TEXT_RE.match(t)) and "NULL" not in t

def split_top(text, sep):
    """Split `text` at every `sep` ("&&" / "||" / ";") outside parentheses/brackets."""
    parts, depth, start, i, n = [], 0, 0, 0, len(text)
    while i < n:
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0 and text.startswith(sep, i):
            parts.append(text[start:i])
            i += len(sep)
            start = i
            continue
        i += 1
    parts.append(text[start:])
    return parts

def strip_parens(text):
    t = text.strip()
    while t.startswith("(") and t.endswith(")"):
        depth = 0
        for k, c in enumerate(t):
            depth += c == "("
            depth -= c == ")"
            if depth == 0 and k < len(t) - 1:
                return t
        t = t[1:-1].strip()
    return t

def relational(text):
    """(lhs, op, rhs) for the first top-level <, <=, >, >= in `text` (not <<, >>, ->), or None."""
    depth, i, n = 0, 0, len(text)
    while i < n:
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0 and c in "<>":
            if i + 1 < n and text[i + 1] == c:
                i += 2
                continue
            if c == ">" and i > 0 and text[i - 1] == "-":
                i += 1
                continue
            op = c + ("=" if i + 1 < n and text[i + 1] == "=" else "")
            return text[:i], op, text[i + len(op):]
        i += 1
    return None

def _words(text):
    return {w for w in _WORD_RE.findall(text) if w not in _C_WORDS and not w.isupper()}

def conjunct_bound(conj, init_text):
    """(True, set()) when this conjunct bounds the loop; else (False, limit identifiers)."""
    rel = relational(strip_parens(conj))
    if rel is None:
        return False, _words(conj)
    lhs, op, rhs = rel
    upper_const = (op in ("<", "<=") and is_const_expr(rhs)) or (op in (">", ">=") and is_const_expr(lhs))
    if upper_const:
        return True, set()
    lower_const = (op in ("<", "<=") and is_const_expr(lhs)) or (op in (">", ">=") and is_const_expr(rhs))
    var_side = rhs if op in ("<", "<=") else lhs
    if lower_const:
        # a count-down: bounded only when the for-init starts the variable at a constant
        for var in _words(var_side):
            m = re.search(r"(?<![A-Za-z0-9_])" + re.escape(var) + r"\s*=\s*([^,;=][^,;]*)", init_text)
            if m and is_const_expr(m.group(1)):
                return True, set()
        return False, _words(var_side)
    limit_side = rhs if op in ("<", "<=") else lhs
    return False, _words(limit_side)

def cond_bound(cond, init_text):
    """(True, set()) if the controlling expression is statically bounded, else (False, idents)."""
    disjuncts = split_top(cond, "||")
    need = set()
    for d in disjuncts:
        ok_d, idents = False, set()
        for conj in split_top(d, "&&"):
            ok, ids = conjunct_bound(conj, init_text)
            if ok:
                ok_d = True
                break
            idents |= ids
        if not ok_d:
            need |= idents
            return False, need
    return True, set()

def assertion_bounds(stripped, start, end, idents):
    """True when an assertion in stripped[start:end] compares one of `idents` against a constant."""
    for m in ASSERT_RE.finditer(stripped, start, end):
        args = _arg_spans(stripped, m.end() - 1)
        if not args:
            continue
        for conj in split_top(args[0], "&&"):
            rel = relational(strip_parens(conj))
            if rel is None:
                continue
            lhs, op, rhs = rel
            for side, other in ((lhs, rhs), (rhs, lhs)):
                if is_const_expr(other) and (not idents or _words(side) & idents):
                    return True
    return False

def _body_span(stripped, after):
    """(open, close) of the statement following a loop header ending at `after`: a braced block,
    or a single statement up to its ';'."""
    j = after
    n = len(stripped)
    while j < n and stripped[j] in " \t\r\n":
        j += 1
    if j < n and stripped[j] == "{":
        depth = 0
        for k in range(j, n):
            depth += stripped[k] == "{"
            depth -= stripped[k] == "}"
            if depth == 0:
                return j, k
        return j, n
    k = stripped.find(";", j)
    return j, (k if k >= 0 else n)

def _paren_close(stripped, open_idx):
    depth = 0
    for k in range(open_idx, len(stripped)):
        depth += stripped[k] == "("
        depth -= stripped[k] == ")"
        if depth == 0:
            return k
    return len(stripped)

def _macro_at(stripped, pos):
    """Name of the #define whose (possibly continued) definition holds `pos`, else None."""
    line_start = stripped.rfind("\n", 0, pos) + 1
    while True:
        m = re.match(r"[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", stripped[line_start:])
        if m:
            return m.group(1)
        prev_end = line_start - 1
        if prev_end <= 0:
            return None
        prev_start = stripped.rfind("\n", 0, prev_end) + 1
        if not stripped[prev_start:prev_end].rstrip().endswith("\\"):
            return None
        line_start = prev_start

def check_rule2_loops(stripped, relpath, lidx, findings, register, funcs, task_entries):
    """RULE-2: see the block comment above. `task_entries` = names passed as xTaskCreate*'s
    first argument anywhere in the scan set; one must also be registered (rule 9) to loop forever."""
    for m in _LOOP_RE.finditer(stripped):
        kw = m.group(1)
        popen = m.end() - 1
        pclose = _paren_close(stripped, popen)
        header = stripped[popen + 1:pclose]
        fn = _enclosing_function_by_body(funcs, m.start())
        symbol = fn if fn is not None else _macro_at(stripped, m.start())
        if symbol is None or register.is_registered(2, relpath, symbol):
            continue
        if kw == "for":
            clauses = split_top(header, ";")
            if len(clauses) != 3:
                continue
            init_text, cond = clauses[0], clauses[1].strip()
        else:
            init_text, cond = "", header.strip()
        body_open, body_close = _body_span(stripped, pclose + 1)
        # a `while (...)` that closes a do-block: `} while (c);` -- its body is the do-block
        prev = stripped[:m.start()].rstrip()
        is_do_tail = kw == "while" and prev.endswith("}") and stripped[pclose + 1:].lstrip().startswith(";")
        if is_do_tail:
            close = len(prev) - 1
            depth, k = 0, close
            while k >= 0:
                depth += stripped[k] == "}"
                depth -= stripped[k] == "{"
                if depth == 0:
                    break
                k -= 1
            if _ident_before(stripped, k)[0] == "do":
                body_open, body_close = k, close
        shown = " ".join(("%s (%s)" % (kw, header)).split())[:70]
        infinite = cond == "" or (is_const_expr(cond) and strip_parens(cond) not in ("0", "false", "0u", "0U"))
        if infinite:
            if fn in task_entries and any(register.is_registered(9, d, fn) for d in task_entries[fn]):
                continue
            if assertion_bounds(stripped, body_open, body_close, set()):
                continue
            findings.append({"file": relpath, "line": lidx.line_of(m.start()), "rule": "RULE-2",
                             "symbol": symbol,
                             "message": "infinite loop '%s' outside a registered task entry and "
                                        "without a bound assertion in its body (rule 2: bounded "
                                        "loops)" % shown})
            continue
        if is_const_expr(cond):
            continue                         # `while (0)`: runs at most once
        ok, idents = cond_bound(cond, init_text)
        if ok:
            continue
        fstart = funcs[[f[0] for f in funcs].index(fn)][2] if fn is not None else m.start()
        if idents and assertion_bounds(stripped, fstart, body_close, idents):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(m.start()), "rule": "RULE-2",
                         "symbol": symbol,
                         "message": "loop '%s' has no statically visible bound (rule 2: a literal "
                                    "or #define/enum limit in the condition, or a bound assertion "
                                    "on %s)" % (shown, ", ".join(sorted(idents)) or "its condition")})
```
`run()` — the pre-pass (replaces :813-820, quoted: `# rule 9 sinks may name a function defined in another scanned file: index every definition once` … `fn_defs.setdefault(fn[0], []).append(frel)`):
```python
    # whole-scan-set pre-pass: every definition (rule 9 sinks may name a function defined in
    # another file) and every task entry (xTaskCreate*'s first argument, for rule 2)
    fn_defs = {}
    task_args = set()
    for _c, fpath, frel in files:
        with open(fpath, "r", encoding="utf-8", errors="replace") as f:
            fs = blank_dead_regions(strip_comments_and_literals(f.read()))
        for fn in find_top_level_functions(fs):
            fn_defs.setdefault(fn[0], []).append(frel.replace(os.sep, "/"))
        for m in _SINK_RE.finditer(fs):
            if m.group(1).startswith("xTaskCreate"):
                args = _arg_spans(fs, m.end() - 1)
                im = _IDENT_ONLY_RE.match(args[0]) if args else None
                if im:
                    task_args.add(im.group(1))
    task_entries = {n: fn_defs[n] for n in task_args if n in fn_defs}
```
In the per-component dict (:830-833) add `"rule2": 0,` after `"rule1": 0,`; after the rule-5 block (:857-859) insert:
```python
        rule2_findings = []
        check_rule2_loops(stripped, relpath, lidx, rule2_findings, register, funcs, task_entries)
        comp["rule2"] += len(rule2_findings)
        all_findings.extend(rule2_findings)
```
`render_text` (:934) — replace `lines.append("totals: rule1(recursion)=%d" % total_rule1)` with:
```python
    total_rule2 = sum(s["rule2"] for s in summary.values())
    lines.append("totals: rule1(recursion)=%d rule2(unbounded-loop)=%d" % (total_rule1, total_rule2))
```
Docstring: add `RULE-2  every for/while/do statically bounded or bound-asserted; for(;;) only in a registered task entry or with a body bound assertion.`

- [ ] **Step 4: Tests pass** — `python3 -m unittest tools/lint/test_power_of_10.py` → `Ran 34 tests` `OK`.

- [ ] **Step 5: Mutation** — in `conjunct_bound` make `upper_const` always `True`; `test_r2_negative_five_unbounded_loops_fail` FAILS; restore. Transcript `mutations/SD-S3-T2.txt`.

- [ ] **Step 6: File the surfaced violations** — run the "Filing surfaced violations" commands. Expected `appended 67 row(s)` (±10 %), then `gate_register` PASS and the CI line rc 0.

- [ ] **Step 7: Commit**
```bash
git add tools/lint docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S3-T2.txt
git commit -m "feat(p10): rule 2 loop bounds; 67 existing sites filed to the findings register (S3 T2)"
```
Ledger: `- S3-T2 DONE <sha> rule 2; filed S3-P10-1..67`.

### Task 3: Rule 3 — full heap census, `#define` wrappers, registered pre-init sites

**Findings:** SD-S3-T3 (closes LG-2 PARTIAL and the rule-3 half of TCI-T3)

**Files:**
- Modify: `tools/lint/power_of_10.py` (`RULE3_RE` :498-503, `check_rule3_heap` :601-605, `run()`)
- Create: `tools/lint/fixtures/r3_neg.c`, `r3_pos.c`, `register_r3.md`
- Modify: `tools/lint/test_power_of_10.py` (class `Rule3Census`)

**Interfaces:**
- Consumes: Task 2 `_macro_at`; Task 1 `symbol_at`.
- Produces: `HEAP_APIS` (regex alternation), `RULE3_RE`, `heap_wrappers(texts) -> {macro: allocator}`, `check_rule3_heap(stripped, relpath, lidx, findings, register=None, funcs=(), wrappers=None)`; `run()` local `texts` (blanked text of every scanned file, reused by Task 5).

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixtures**

`tools/lint/fixtures/r3_neg.c`:
```c
/* probe (rule 3, negative -- must FAIL): the heap census of spec §9 (TCI-T3 p2 + LG-2 gaps). */
#include <stdlib.h>
#define ALLOC malloc
#define RELEASE free
#define ALLOC2(n) ALLOC(n)
void *r3_wrap(size_t n) { return ALLOC (n); }
void r3_unwrap(void *p) { RELEASE (p); }
void *r3_wrap2(size_t n) { return ALLOC2(n); }
void *r3_prefer(size_t n) { return heap_caps_malloc_prefer(n, 2, 1, 2); }
void *r3_posix(size_t n) { void *p = 0; (void)posix_memalign(&p, 16, n); return p; }
void *r3_memalign(size_t n) { return memalign(16, n); }
int r3_alloca(size_t n) { char *b = alloca(n); b[0] = 0; return b[0]; }
void *r3_realloc_prefer(size_t n) { return heap_caps_realloc_prefer(0, n, 2, 1, 2); }
void *(*r3_alloc_ptr)(size_t) = malloc;
void *r3_queue(void) { return xQueueCreate(4, 4); }
```
`tools/lint/fixtures/r3_pos.c`:
```c
/* probe (rule 3, positive -- must PASS): query APIs, static creators, member calls named like
 * allocators, and one registered pre-init site. */
#include <stdlib.h>
struct pool { void (*free)(void *); int malloc_count; };
static unsigned char s_q_storage[16];
static StaticQueue_t s_q_buf;
size_t r3_query(void) { return heap_caps_get_free_size(0) + heap_caps_get_minimum_free_size(0); }
void *r3_static(void) { return xQueueCreateStatic(4, 4, s_q_storage, &s_q_buf); }
void r3_member(struct pool *p, void *x) { p->free(x); p->malloc_count = 0; }
void *r3_init_pool(void) { return malloc(64); }
```
`tools/lint/fixtures/register_r3.md`:
```
| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
| TD-6 | 3 | tools/lint/fixtures/r3_pos.c:r3_init_pool | n/a (fixture) | n/a | n/a | fixture |
```

- [ ] **Step 2: Failing tests** (append):
```python
class Rule3Census(unittest.TestCase):
    def test_r3_negative_wrappers_prefer_memalign_alloca_value_dynamic_creator_fail(self):
        rc, out = run(["--paths", FIX + "/r3_neg.c", "--register", NONE, "--fail-on-violation"])
        got = [l.split("RULE-3: ")[1] for l in findings(out) if ": RULE-3: " in l]
        self.assertEqual([
            "dynamic memory call 'ALLOC(' through a #define wrapper of malloc (rule 3: no heap after init)",
            "dynamic memory call 'RELEASE(' through a #define wrapper of free (rule 3: no heap after init)",
            "dynamic memory call 'ALLOC2(' through a #define wrapper of malloc (rule 3: no heap after init)",
            "dynamic memory call 'heap_caps_malloc_prefer(' (rule 3: no heap after init)",
            "dynamic memory call 'posix_memalign(' (rule 3: no heap after init)",
            "dynamic memory call 'memalign(' (rule 3: no heap after init)",
            "dynamic memory call 'alloca(' (rule 3: no heap after init)",
            "dynamic memory call 'heap_caps_realloc_prefer(' (rule 3: no heap after init)",
            "allocator 'malloc' taken as a value (rule 3: no heap after init)",
            "dynamic memory call 'xQueueCreate(' (rule 3: no heap after init)",
        ], got)
        self.assertEqual(1, rc)

    def test_r3_positive_queries_static_creators_members_registered_site_pass(self):
        rc, out = run(["--paths", FIX + "/r3_pos.c", "--register", FIX + "/register_r3.md",
                       "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_r3_unregistered_pre_init_site_fails(self):
        rc, out = run(["--paths", FIX + "/r3_pos.c", "--register", NONE, "--fail-on-violation"])
        self.assertIn("r3_pos.c:10: RULE-3: dynamic memory call 'malloc('", out)
        self.assertEqual(1, rc)
```
Run → 3 failures in `Rule3Census`.

- [ ] **Step 3: Implement** — replace `RULE3_RE` (:498-503) and `check_rule3_heap` (:601-605, quoted: `def check_rule3_heap(stripped, relpath, lidx, findings): for m in RULE3_RE.finditer(stripped): ...`) with:
```python
# RULE-3 census (spec §9): every allocator this codebase could reach. `heap_caps_*` covers the
# `_prefer` variants and heap_caps_aligned_*; the FreeRTOS creators are the DYNAMIC forms (the
# project uses the *Static ones); alloca/strdupa allocate on the stack without a bound.
HEAP_APIS = (
    r"heap_caps_[a-z_]*(?:alloc|free)(?:_prefer)?|"
    r"pvPortMalloc|pvPortCalloc|vPortFree|"
    r"malloc|calloc|realloc|reallocarray|reallocf|free|cfree|"
    r"posix_memalign|memalign|aligned_alloc|valloc|pvalloc|"
    r"alloca|__builtin_alloca|strdupa|strndupa|"
    r"strdup|strndup|asprintf|vasprintf|"
    r"xTaskCreate|xTaskCreatePinnedToCore|xQueueCreate|xQueueCreateSet|"
    r"xSemaphoreCreateMutex|xSemaphoreCreateRecursiveMutex|xSemaphoreCreateBinary|"
    r"xSemaphoreCreateCounting|xEventGroupCreate|xTimerCreate|xStreamBufferCreate|"
    r"xMessageBufferCreate|xRingbufferCreate"
)
RULE3_RE = re.compile(r"(?<![A-Za-z0-9_.>])(" + HEAP_APIS + r")\s*\(")   # not `s->free(`
# an allocator taken as a value (`void *(*f)(size_t) = malloc;`, `qsort(.., free)`)
_HEAP_VALUE_RE = re.compile(r"[=,(]\s*&?\s*(" + HEAP_APIS + r")\s*(?=[,;)}])")
_DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)(\([^)]*\))?([^\n]*)",
                        re.MULTILINE)
def heap_wrappers(texts):
    """{macro: allocator} for every #define in the scan set whose replacement list names an
    allocator or another wrapper, resolved to a fixed point (a wrapper of a wrapper counts)."""
    defs = {}
    for t in texts:
        for m in _DEFINE_RE.finditer(t):
            defs[m.group(1)] = m.group(3)
    base = re.compile(r"(?<![A-Za-z0-9_])(" + HEAP_APIS + r")(?![A-Za-z0-9_])")
    wrappers = {}
    for name, body in defs.items():
        hit = base.search(body)
        if hit:
            wrappers[name] = hit.group(1)
    changed = True
    while changed:
        changed = False
        for name, body in defs.items():
            if name in wrappers:
                continue
            for w in list(wrappers):
                if re.search(r"(?<![A-Za-z0-9_])" + re.escape(w) + r"(?![A-Za-z0-9_])", body):
                    wrappers[name] = wrappers[w]
                    changed = True
                    break
    return wrappers

def check_rule3_heap(stripped, relpath, lidx, findings, register=None, funcs=(), wrappers=None):
    """RULE-3: every allocator call, allocator-as-value and call through a #define wrapper.
    A site is excused only by a rule-3 register row for its file:function (a listed pre-init
    site); file-scope hits (inside a macro body) carry the macro's name as their symbol."""
    wrappers = wrappers or {}
    hits = [(m.start(), m.group(1), "dynamic memory call '%s(' (rule 3: no heap after init)"
             % m.group(1)) for m in RULE3_RE.finditer(stripped)]
    hits += [(m.start(1), m.group(1), "allocator '%s' taken as a value (rule 3: no heap after "
              "init)" % m.group(1)) for m in _HEAP_VALUE_RE.finditer(stripped)]
    if wrappers:
        wrap_re = re.compile(r"(?<![A-Za-z0-9_])(%s)\s*\(" % "|".join(
            re.escape(w) for w in sorted(wrappers)))
        hits += [(m.start(), m.group(1), "dynamic memory call '%s(' through a #define wrapper of "
                  "%s (rule 3: no heap after init)" % (m.group(1), wrappers[m.group(1)]))
                 for m in wrap_re.finditer(stripped)
                 if _macro_at(stripped, m.start()) is None]
    for pos, _name, message in sorted(hits):
        symbol = symbol_at(funcs, pos) or (_macro_at(stripped, pos) or "")
        if register is not None and register.is_registered(3, relpath, symbol):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(pos), "rule": "RULE-3",
                         "symbol": symbol, "message": message})
```
`run()`: after the `task_entries = ...` line add
```python
    texts = []
    for _c, fpath, _frel in files:
        with open(fpath, "r", encoding="utf-8", errors="replace") as f:
            texts.append(blank_dead_regions(strip_comments_and_literals(f.read())))
    wrappers = heap_wrappers(texts)
```
and change the call to `check_rule3_heap(stripped, relpath, lidx, rule3_findings, register, funcs, wrappers)`. Docstring RULE-3 paragraph: list `HEAP_APIS`, wrappers, allocator-as-value, and "excused only by a rule-3 register row (a pre-init site)".

- [ ] **Step 4: Tests pass** — `Ran 37 tests` `OK` (the LG-1/LG-2 tests still pass unchanged).

- [ ] **Step 5: Mutation** — delete the `if wrappers:` block; `test_r3_negative_…` FAILS; restore. Transcript `mutations/SD-S3-T3.txt`.

- [ ] **Step 6: Live tree** — the CI line: expected rc 0 and `appended`-nothing from the filing command (0 firmware heap sites; devcontroller's 7 `xTaskCreate`/heap sites are still report-only until Task 6).

- [ ] **Step 7: Commit** `feat(p10): rule 3 full heap census incl. #define wrappers and dynamic FreeRTOS creators (S3 T3)`; ledger `- S3-T3 DONE <sha> rule 3; filed 0`.

### b7 block end (CHECKPOINT)

- [ ] `python3 tools/gates/run_all.py` (PASS only) and `python3 -m unittest tools/lint/test_power_of_10.py` (37 OK).
- [ ] Owner decision (Preconditions) answered; final reviewer (opus) + final verifier (Fable) with the three review packages; `gh pr ready`; all checks green; `gh pr merge --squash` under DEC-0001; tag `strict-delivery-b7`; delete branch/worktrees.
- [ ] Mark SD-S3-T1..T3 per the S2 transition rules (`pinned` with test + transcript + reviewer report path).
- [ ] Write `docs/superpowers/state/strict-delivery/handoff.md` (umbrella front matter; `plan:` this file, `block: 7`, `next_task: "4"`), append `CHECKPOINT b7 <sha>` to the ledger, commit, then run `python3 tools/gates/gate_checkpoint.py --plan strict-delivery` → PASS only. Post the resume prompt (umbrella template, `block 8, sub-plan S3, task 4`) and stop for `/clear`.

## Block b8 — rules 5 and 9, every tree, `gate_p10` (Tasks 4–6), branch `block/strict-delivery-b8`

Block start: ledger `- b8 start <sha>`; rows SD-S3-T4..T6 already exist (`planned`).

### Task 4: Rule 5 — literal tautologies and compiled-out assertions do not count

**Findings:** SD-S3-T4 (closes TCI-T3 rule-5 half and LG-4 PARTIAL: `#ifdef NEVER` and tautologies)

**Files:**
- Modify: `tools/lint/power_of_10.py` (insert above `class LineIndex:` :240; `check_rule5_assertions` :689-711)
- Create: `tools/lint/fixtures/r5_neg.c`, `r5_pos.h`
- Modify: `tools/lint/test_power_of_10.py` (class `Rule5Exclusions`), `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: Task 2 `is_const_expr` (its `_CONST_TEXT_RE` already admits `= ! < > & | ~ ^`, so `sizeof(int) == 4` is a constant), `_arg_spans`, `_DIRECTIVE_RE`, `_IF0_RE`.
- Produces: `conditional_context(text) -> list[tuple]`, `count_live_assertions(stripped, open_idx, close_idx, lidx, ctx, assert_re=None) -> int`; `check_rule5_assertions(..., funcs, ctx=None, assert_re=None, min_lines=..., min_asserts=...)` (Task 6 passes `assert_re`).

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixtures**

`tools/lint/fixtures/r5_neg.c`:
```c
/* probe (rule 5, negative -- must FAIL): assertions that cannot fail or are compiled out
 * (TCI-T3 p5, p7). */
int r5_tautology(int a)
{
    CORE_ASSERT_RET(1, 1, -1);
    CORE_ASSERT_RET(true, 1, -1);
    CORE_ASSERT_RET(sizeof(int) == 4, 1, -1);
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    return a;
}
int r5_never(int a)
{
#ifdef NEVER
    CORE_ASSERT_RET(a > 0, 1, -1);
    CORE_ASSERT_RET(a < 9, 1, -1);
#endif
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    return a;
}
```
`tools/lint/fixtures/r5_pos.h`:
```c
/* probe (rule 5, positive -- must PASS): real assertions; a function wholly inside a
 * conditional counts the assertions in its own branch; the #else of #if 0 is live; the
 * include guard is not a branch. */
#ifndef R5_POS_H
#define R5_POS_H
static inline int r5_real(int a)
{
    CORE_ASSERT_RET(a > 0, 1, -1);
    CORE_ASSERT_RET(a < 99, 1, -1);
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    return a;
}
#if CFG_HAS_X
static inline int r5_in_branch(int a)
{
    CORE_ASSERT_RET(a > 0, 1, -1);
    CORE_ASSERT_RET(a < 99, 1, -1);
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    return a;
}
#endif
static inline int r5_else_of_if0(int a)
{
#if 0
    a = 0;
#else
    CORE_ASSERT_RET(a > 0, 1, -1);
    CORE_ASSERT_RET(a < 99, 1, -1);
#endif
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    return a;
}
#endif
```

- [ ] **Step 2: Failing tests** (append):
```python
class Rule5Exclusions(unittest.TestCase):
    """TCI-T3: literal tautologies and assertions compiled out of some build do not count."""

    def test_r5_negative_tautologies_and_ifdef_never_fail(self):
        rc, out = run(["--paths", FIX + "/r5_neg.c", "--register", NONE, "--fail-on-violation"])
        self.assertIn("function 'r5_tautology' has 0 assertion(s)", out)
        self.assertIn("function 'r5_never' has 0 assertion(s)", out)
        self.assertEqual(1, rc)

    def test_r5_positive_real_branch_local_and_else_of_if0_pass(self):
        rc, out = run(["--paths", FIX + "/r5_pos.h", "--register", NONE, "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)
```
Run → `test_r5_negative_…` FAILS (`r5_tautology` has 3 assertions today).

- [ ] **Step 3: Implement** — insert above `class LineIndex:`:
```python
_GUARD_DEF_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
_IFNDEF_NAME_RE = re.compile(r"^[ \t]*#[ \t]*ifndef[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
_IF1_RE = re.compile(r"^[ \t]*#[ \t]*if[ \t]+1\b")
def conditional_context(text):
    """Per line (0-based), the tuple of line numbers of the conditional-compilation branches
    enclosing it: `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else` each open a new branch. Not
    branches: an include guard (`#ifndef X` whose next non-blank line is `#define X`), `#if 1`,
    and the `#else` of `#if 0` (always compiled; the `#if 0` body is already blanked). RULE-5
    counts an assertion only when its context equals its function body's: an assertion inside
    `#ifdef NEVER` -- or any branch the function itself does not sit in -- is compiled out of
    some build (TCI-T3 probe p7)."""
    lines = text.split("\n")
    stack, out = [], []
    for i, line in enumerate(lines):
        m = _DIRECTIVE_RE.match(line)
        kw = m.group(1) if m else None
        if kw == "if" and (_IF0_RE.match(line) or _IF1_RE.match(line)):
            stack.append("if0" if _IF0_RE.match(line) else None)
        elif kw in ("if", "ifdef", "ifndef"):
            guard = False
            g = _IFNDEF_NAME_RE.match(line)
            if g:
                for nxt in lines[i + 1:]:
                    if nxt.strip() == "":
                        continue
                    d = _GUARD_DEF_RE.match(nxt)
                    guard = bool(d and d.group(1) == g.group(1))
                    break
            stack.append(None if guard else i)
        elif kw in ("else", "elif") and stack:
            if stack[-1] == "if0" and kw == "else":
                stack[-1] = None
            elif stack[-1] is not None:
                stack[-1] = i
        elif kw == "endif" and stack:
            stack.pop()
        out.append(tuple(s for s in stack if isinstance(s, int)))
    return out

def count_live_assertions(stripped, open_idx, close_idx, lidx, ctx, assert_re=None):
    """Assertions in a body that can fail: not a literal tautology (`CORE_ASSERT_RET(1, ..)`,
    `true`, any constant expression -- TCI-T3 probe p5) and compiled in whenever the function
    is (same conditional context as the body's opening line)."""
    assert_re = assert_re or ASSERT_RE
    want = ctx[lidx.line_of(open_idx) - 1]
    n = 0
    for m in assert_re.finditer(stripped, open_idx, close_idx):
        if ctx[lidx.line_of(m.start()) - 1] != want:
            continue
        args = _arg_spans(stripped, m.end() - 1)
        if not args or is_const_expr(args[0]):
            continue
        n += 1
    return n
```
In `check_rule5_assertions`, change the signature to `def check_rule5_assertions(stripped, relpath, lidx, register, funcs, ctx=None, assert_re=None, min_lines=RULE5_MIN_LINES, min_asserts=RULE5_MIN_ASSERTS):`, add `if ctx is None: ctx = conditional_context(stripped)` after `stripped_lines = …`, and replace the two lines (:701-702)
```python
        body = stripped[open_idx:close_idx]
        n_asserts = len(ASSERT_RE.findall(body))
```
with
```python
        n_asserts = count_live_assertions(stripped, open_idx, close_idx, lidx, ctx, assert_re)
```
Docstring RULE-5: "a literal tautology (any constant expression) and an assertion outside the function's own conditional branch (`#ifdef NEVER`) do not count".

- [ ] **Step 4: Tests pass** — `Ran 39 tests` `OK` (LG-4/LG-7/`#elif 0` tests unchanged).

- [ ] **Step 5: Mutation** — make `count_live_assertions` skip the `is_const_expr` test; `test_r5_negative_…` FAILS; restore. `mutations/SD-S3-T4.txt`.

- [ ] **Step 6: File** — expected `appended 2 row(s)`: `components/app/pipeline/pipeline.c` (function at :322, its two assertions compare `sizeof` with `sizeof`) and `components/core/ui/screens_moto.c` (function at :260). Then `gate_register` PASS; CI line rc 0.

- [ ] **Step 7: Commit** `feat(p10): rule 5 ignores literal tautologies and compiled-out assertions (S3 T4)`; ledger `- S3-T4 DONE <sha>; filed S3-P10-<a>..<b> (2)`.

### Task 5: Rule 9 — typedef'd parameters and fields, every function used as a value, the pointed-to set

**Findings:** SD-S3-T5 (closes TCI-T2 and TCI-T4 rule-9 half; V-1 test rationale)

**Files:**
- Modify: `tools/lint/power_of_10.py` — `FNPTR_RE` (:505), the sink comment and `_FNVAL_ASSIGN_RE` (:516-522), replace `check_rule9_sinks` (:549-585) and `check_rule9_fnptr` (:628-641), `run()`, `render_text()`
- Create: `tools/lint/fixtures/r9_neg.c`, `r9_pos.c`, `r9_console.c`, `register_r9.md`
- Modify: `tools/lint/test_power_of_10.py` — delete `test_struct_argument_sinks_are_not_listed_as_inert_entries` (:79-83; it pins the false "caught by the stored-value check" rationale, audit V-1), add class `Rule9Reach`
- Modify: `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: Task 1 `symbol_at`; Task 2 `_C_WORDS`; Task 3 `texts`; `_ident_before`, `_arg_spans`, `_IDENT_ONLY_RE`, `_RESERVED`.
- Produces: `check_rule9_values(stripped, relpath, lidx, findings, register, funcs, fn_defs=None)`, `check_rule9_fnptr(stripped, relpath, lidx, findings, register, funcs, fntypes=None, params=None)`, `fnptr_typedefs(texts) -> {name: needs_star}`, `pointed_to_sets(units, params, fn_defs) -> ({(F, k): set}, unresolved)`, `_call_owner(stripped, pos) -> str | None`; `rule8a["pointed_to"] = {"F#k": [names]}` (printed as `rule9 pointed-to sets`, JSON under `rule8a`); `RUNNER_SITES = ("RUN_TEST",)`; `SDK_FNPTR_TYPES`. Task 7 adds the register `covers` check inside `check_rule9_values`.

Symbols (what a register row or findings `repro` keys): a function used as a value → the function's name (unchanged: PD-1..4, 6, 8, 9 keep working); a parameter of function-pointer type → its function (`export_serial.c:register_cmd`, so PD-5 is no longer a dead row); a field or variable → its own name (`cmd.c:emit` for `cw_t.emit`); a test function passed to Unity's `RUN_TEST` → `RUN_TEST` (one site per test file, not one per test).

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixtures**

`tools/lint/fixtures/r9_neg.c` (the TCI-T4 probe p4 verbatim, with this header line):
```c
/* probe (rule 9, negative -- must FAIL): nine function-pointer spellings (TCI-T4 p4). */
typedef int cb_fn(int);                 /* function TYPE typedef (no (*)) */
typedef int (*cb_ptr_t)(int);           /* classic -- expected to be caught */
static int h1(int x) { return x; }
static int h2(int x) { return x + 1; }
static cb_fn *s_a;                      /* pointer via function-type typedef */
static int (*s_tbl[2])(int) = { h1, h2 }; /* array of function pointers */
struct ops { int (*op)(int); };         /* struct member */
static struct ops s_ops = { h1 };       /* positional init of a struct member */
extern int other_file_fn(int);
static void *s_v;
static int use_cast(void *p) { return ((int (*)(int))p)(3); }   /* cast to fn ptr */
static void reg(cb_fn *f) { s_a = f; }
int probe_fnptr(void)
{
    reg(h2);                            /* passed to a non-sink function */
    s_v = (void *)h1;                   /* stored through a cast */
    s_a = other_file_fn;                /* function defined elsewhere */
    return s_a(1) + s_tbl[0](1) + s_ops.op(1) + use_cast(s_v);
}
```
`tools/lint/fixtures/r9_pos.c`:
```c
/* probe (rule 9, positive -- must PASS): direct calls, a local that shadows a function name, a
 * declarator list sharing a function's name, and one registered SDK-shaped task entry. */
static int step(int x) { return x + 1; }
static int fused(int x) { return x * 2; }
struct tally { int laps, fused, step; };
static void r9_task(void *arg) { (void)arg; }
int r9_direct(int n)
{
    int total = step(n) + fused(n);
    int k = total;
    return k;
}
int r9_shadow(int n)
{
    int step = n;
    int x = step;
    return x;
}
void r9_start(void) { (void)xTaskCreateStatic(r9_task, "r9", 512, 0, 1, 0, 0); }
```
`tools/lint/fixtures/r9_console.c`:
```c
/* probe (rule 9): the esp_console shape (TCI-T2) -- a forwarder whose typedef'd parameter is
 * stored into a struct field, called with two local callbacks. */
static int cmd_a(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int cmd_b(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static void register_cmd(const char *command, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = { .command = command, .func = func };
    (void)esp_console_cmd_register(&cmd);
}
void r9_console_start(void)
{
    register_cmd("a", cmd_a);
    register_cmd("b", cmd_b);
}
```
`tools/lint/fixtures/register_r9.md`:
```
| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
| TD-8 | 9 | tools/lint/fixtures/r9_pos.c:r9_task | n/a (fixture) | n/a | n/a | fixture |
```

- [ ] **Step 2: Failing tests** (append; delete the V-1 test):
```python
class Rule9Reach(unittest.TestCase):
    """TCI-T2/T4: typedef'd parameters and fields, every function used as a value, the
    pointed-to set."""

    def test_r9_negative_every_spelling_is_a_finding(self):
        rc, out = run(["--paths", FIX + "/r9_neg.c", "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation"])
        for needle in ("function pointer 'cb_ptr_t'", "function pointer 's_a'",
                       "function pointer 's_tbl'", "function pointer 'op'",
                       "function pointer 'use_cast'", "function pointer 'reg'",
                       "function 'h2' passed to reg()", "function 'h1' stored as a value"):
            self.assertIn("unregistered " + needle, out)
        self.assertEqual(11, len(findings(out)), out)
        self.assertIn("reg#0 -> {h2}", out)
        self.assertEqual(1, rc)

    def test_r9_positive_direct_calls_shadows_declarator_lists_registered_task_pass(self):
        rc, out = run(["--paths", FIX + "/r9_pos.c", "--register", FIX + "/register_r9.md",
                       "--enforce-fnptr", "--fail-on-violation"])
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_esp_console_shape_makes_the_forwarder_and_each_callback_a_site(self):
        rc, out = run(["--paths", FIX + "/r9_console.c", "--register", NONE, "--enforce-fnptr",
                       "--fail-on-violation"])
        self.assertIn("unregistered function pointer 'register_cmd'", out)
        self.assertIn("unregistered function 'cmd_a' passed to register_cmd()", out)
        self.assertIn("unregistered function 'cmd_b' passed to register_cmd()", out)
        self.assertIn("register_cmd#1 -> {cmd_a, cmd_b}", out)
        self.assertEqual(1, rc)
```
Run → 3 failures (2 of 11 spellings seen today; `register_cmd` unseen).

- [ ] **Step 3: Implement**

3a. Replace `FNPTR_RE = …` (:505) with:
```python
FNPTR_RE = re.compile(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]\s*)*\)\s*\(")
# `(int (*)(int))p`: an anonymous function-pointer type in a cast or prototype
_FNPTR_ANON_RE = re.compile(r"\(\s*\*\s*\)\s*\(")
# `typedef R (*NAME)(...)` and the function-TYPE form `typedef R NAME(...);`
_PTR_TYPEDEF_RE = re.compile(r"\btypedef\b[^;{}]*?\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(")
_FN_TYPEDEF_RE = re.compile(r"\btypedef\s+[^;{}()]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\((?!\s*\*)[^;{}]*\)\s*;")
# IDF/FreeRTOS function-pointer typedefs a project declaration can name (their typedefs live in
# the SDK, outside the scan set)
SDK_FNPTR_TYPES = ("esp_console_cmd_func_t", "esp_console_cmd_func_with_context_t",
                   "TaskFunction_t", "gpio_isr_t", "transaction_cb_t", "esp_timer_cb_t",
                   "intr_handler_t", "esp_event_handler_t", "TimerCallbackFunction_t")
# vendored test runners whose API takes the test function by value: the site is the runner call
# in each file (one register/findings row per file, its pointed-to set = that file's tests),
# not each test function
RUNNER_SITES = ("RUN_TEST",)
# a declarator list up to a comma: `int laps, drags,` (the next name is declared, not used)
_DECL_LIST_RE = re.compile(r"^\s*(?:(?:static|const|volatile|unsigned|signed|extern)\s+)*"
                           r"(?:(?:struct|enum|union)\s+)?[A-Za-z_][A-Za-z0-9_]*[\s\*]+"
                           r"[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\])?\s*"
                           r"(?:,\s*\**\s*[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\])?\s*)*,\s*$")
# a function designator in a VALUE position: after = , ( { ? : or `return`, optionally behind
# a cast and/or '&', and followed by , ; ) } ] : ?
_VALUE_CTX_RE = re.compile(r"(?:[=,({?:]|\breturn\b)\s*(?:\(\s*[A-Za-z_][A-Za-z0-9_\s\*]*\)\s*)?"
                           r"&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?=[,;)}\]:?])")
```
3b. Replace the comment block :516-519 and delete `_FNVAL_ASSIGN_RE` (:521-522, now unused) — new comment above `_SINK_RE`:
```python
# Not listed: esp_timer_create / esp_console_cmd_register / spi_bus_add_device take a STRUCT
# whose member carries the callback. Every function designator in a value position -- a sink
# argument, a forwarder argument like `register_cmd(.., cmd_status_c)`, a designated or
# positional initializer, an assignment, a cast -- is caught by check_rule9_values, and a
# member declared with a function-pointer typedef by check_rule9_fnptr (TCI-T2). The table now
# only feeds rule 2's task-entry discovery (xTaskCreate*'s argument 0).
```
3c. Replace `check_rule9_sinks` (:549-585) with the following, and delete the old `check_rule9_fnptr` (:628-641):
```python
def check_rule9_values(stripped, relpath, lidx, findings, register, funcs, fn_defs=None):
    """RULE-9 (LG-3, TCI-T2): a function designator used as a VALUE -- passed as an argument to
    any call (a sink like qsort/xTaskCreate*, or a project forwarder like register_cmd), stored
    by assignment or designated/positional initializer, returned, or cast -- must be registered.
    The registered symbol is the function's own name, under its defining file or this file.

    `fn_defs` maps every function name defined anywhere in the scan set to its defining files."""
    local_fns = {f[0] for f in funcs}
    if fn_defs is None:
        fn_defs = {n: [relpath] for n in local_fns}
    for m in _VALUE_CTX_RE.finditer(stripped):
        symbol = m.group(1)
        pos = m.start(1)
        if symbol not in fn_defs or symbol in _RESERVED:
            continue
        stmt = stripped[max(stripped.rfind(c, 0, pos) for c in ";{}") + 1:pos]
        if _DECL_LIST_RE.match(stmt):
            continue          # `int laps, fused, ...;`: a declarator that shares a function's name
        owner_fn = symbol_at(funcs, pos)
        if owner_fn and _locally_declared(stripped, funcs, owner_fn, symbol):
            continue          # a local variable or parameter that shadows a function name
        if any(register.is_registered(9, d, symbol) for d in fn_defs[symbol] + [relpath]):
            continue
        callee = _call_owner(stripped, pos)
        how = ("passed to %s()" % callee) if callee else "stored as a value"
        site = callee if callee in RUNNER_SITES else symbol
        if site != symbol and register.is_registered(9, relpath, site):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(pos), "rule": "RULE-9",
                         "symbol": site,
                         "message": "unregistered function '%s' %s "
                                    "(rule 9: no function pointers outside the register)"
                                    % (symbol, how)})

def _call_owner(stripped, pos):
    """Name of the call whose argument list holds `pos` (innermost unmatched '('), else None.
    A '(' preceded by a non-identifier (a cast, a grouping) is skipped."""
    depth = 0
    k = pos - 1
    while k >= 0:
        c = stripped[k]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                if c == "[":
                    return None
                name, _start = _ident_before(stripped, k)
                if name and name not in _RESERVED and name != "sizeof":
                    return name
                return None
            depth -= 1
        elif c in ";{}" and depth == 0:
            return None
        k -= 1
    return None

def _locally_declared(stripped, funcs, fn_name, name):
    """True when `name` is declared as a parameter or local variable of `fn_name`."""
    for f in funcs:
        if f[0] != fn_name:
            continue
        start = f[4] if f[4] is not None else f[2]
        decl = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)[\s\*]+" + re.escape(name) + r"\s*(?=[=;,)\[])")
        return any(m.group(1) not in ("return", "case", "goto", "else", "sizeof")
                   for m in decl.finditer(stripped, start, f[3]))
    return False

def fnptr_typedefs(texts):
    """Function-pointer type names: every `typedef R (*NAME)(..)` and function-TYPE typedef
    `typedef R NAME(..);` in the scan set, plus the SDK's (SDK_FNPTR_TYPES). Returns
    {name: needs_star} -- a function-type typedef declares a pointer only with a '*'."""
    names = {t: False for t in SDK_FNPTR_TYPES}
    for t in texts:
        for m in _PTR_TYPEDEF_RE.finditer(t):
            names[m.group(1)] = False
        for m in _FN_TYPEDEF_RE.finditer(t):
            if m.group(1) not in _C_WORDS:
                names[m.group(1)] = True
    return names

def _paren_index(stripped, open_paren, pos):
    """0-based index of the top-level comma-separated slot of the list opened at `open_paren`
    that holds `pos`."""
    depth, idx = 0, 0
    for k in range(open_paren + 1, pos):
        c = stripped[k]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            idx += 1
    return idx

def _param_slot(stripped, pos):
    """(function_name, param_index, open_paren) when `pos` sits directly in the parameter list
    of a definition or prototype (a '(' after an identifier, outside every brace), else None."""
    depth = 0
    k = pos - 1
    while k >= 0:
        c = stripped[k]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                name, _s = _ident_before(stripped, k)
                if name is None or name in _RESERVED:
                    return None
                if stripped.count("{", 0, k) - stripped.count("}", 0, k) > 0:
                    return None      # inside a body or aggregate: a call, not a parameter list
                return name, _paren_index(stripped, k, pos), k
            depth -= 1
        elif c in ";{}" and depth == 0:
            return None
        k -= 1
    return None

def check_rule9_fnptr(stripped, relpath, lidx, findings, register, funcs, fntypes=None,
                      params=None):
    """RULE-9 declarator sites (design §3 rule 9, TCI-T4): the raw `(*name)(` / `(*name[N])(`
    declarator, an anonymous `(*)(` cast, and any parameter, variable or struct field DECLARED
    with a function-pointer typedef (`esp_console_cmd_func_t func`, `cmd_emit_fn emit`,
    `cb_fn *f`). A parameter is attributed to its function (definition or prototype) -- e.g.
    `export_serial.c:register_cmd` -- a body-local to its function, anything else to its own
    identifier. `params`, when given, collects {function: {index: name}} for the pointed-to
    analysis."""
    fntypes = fntypes or {}
    sites = []
    for m in FNPTR_RE.finditer(stripped):
        sites.append((m.start(), m.group(1)))
    for m in _FNPTR_ANON_RE.finditer(stripped):
        sites.append((m.start(), None))
    if fntypes:
        typed_re = re.compile(r"(?<![A-Za-z0-9_])(%s)(?![A-Za-z0-9_])\s*(\*\s*)?(?:const\s+)?"
                              r"([A-Za-z_][A-Za-z0-9_]*)\s*(?=[,;)=\[])"
                              % "|".join(re.escape(t) for t in sorted(fntypes)))
        for m in typed_re.finditer(stripped):
            if fntypes[m.group(1)] and not m.group(2):
                continue        # `cb_fn handler;` declares a function, not a pointer
            stmt = max(stripped.rfind(c, 0, m.start()) for c in ";{}") + 1
            if re.search(r"\btypedef\b", stripped[stmt:m.start()]):
                continue        # a typedef of a typedef: the new name is collected separately
            sites.append((m.start(3), m.group(3)))
    for pos, declared in sorted(sites):
        slot = _param_slot(stripped, pos)
        if slot is not None:
            symbol = slot[0]
            if params is not None and declared:
                params.setdefault(slot[0], {})[slot[1]] = declared
        else:
            symbol = _enclosing_function_by_body(funcs, pos) or declared or symbol_at(funcs, pos)
        if register.is_registered(9, relpath, symbol):
            continue
        findings.append({"file": relpath, "line": lidx.line_of(pos), "rule": "RULE-9",
                         "symbol": symbol,
                         "message": "unregistered function pointer '%s' (rule 9: no function "
                                    "pointers outside the register)" % symbol})

def pointed_to_sets(units, params, fn_defs):
    """Rule 9's enumerable pointed-to set (spec §9): for every function F with a
    function-pointer parameter at index k, the set of functions passed there at every call
    site in the scan set, following forwarding (a caller passing its own function-pointer
    parameter on). Returns ({(F, k): set}, [unresolved (relpath, line, caller, F, k, arg)])."""
    sets = {(f, k): set() for f, ks in params.items() for k in ks}
    forwards = []           # ((F, k), (caller, j)): F.k receives whatever caller.j receives
    unresolved = []
    if not sets:
        return sets, unresolved
    call_re = re.compile(r"(?<![A-Za-z0-9_.>])(%s)\s*\(" % "|".join(
        re.escape(f) for f in sorted(params)))
    for relpath, stripped, lidx, funcs in units:
        for m in call_re.finditer(stripped):
            caller = _enclosing_function_by_body(funcs, m.start())
            if caller is None:
                continue        # a prototype or the definition itself
            fname = m.group(1)
            args = _arg_spans(stripped, m.end() - 1)
            for k in params[fname]:
                if k >= len(args):
                    continue
                a = args[k].strip()
                im = _IDENT_ONLY_RE.match(a)
                if a in ("NULL", "0") or a == "":
                    continue
                caller_params = params.get(caller, {})
                hit = [j for j, nm in caller_params.items() if im and nm == im.group(1)]
                if hit:         # the caller forwards its own function-pointer parameter
                    forwards.append(((fname, k), (caller, hit[0])))
                    continue
                if im and im.group(1) in fn_defs:
                    sets[(fname, k)].add(im.group(1))
                    continue
                unresolved.append((relpath, lidx.line_of(m.start()), caller, fname, k, a))
    for _ in range(len(forwards) + 1):
        changed = False
        for dst, src in forwards:
            before = len(sets[dst])
            sets[dst] |= sets.get(src, set())
            changed = changed or len(sets[dst]) != before
        if not changed:
            break
    return sets, unresolved
```
3d. `run()` — after `wrappers = heap_wrappers(texts)`:
```python
    fntypes = fnptr_typedefs(texts) if enforce_fnptr else {}
    params = {}             # {function: {param index: name}} -- rule 9 pointed-to analysis
```
the rule-9 calls (:883-884) become:
```python
            check_rule9_fnptr(stripped, relpath, lidx, rule9_findings, register, funcs, fntypes,
                              params)
            check_rule9_values(stripped, relpath, lidx, rule9_findings, register, funcs, fn_defs)
```
before `for f in check_rule1_callgraph(units, register):`:
```python
    pointed = {}
    if enforce_fnptr:
        pointed, unresolved = pointed_to_sets(units, params, fn_defs)
        for relpath, line, caller, fname, k, arg in unresolved:
            if register.is_registered(9, relpath, caller):
                continue
            per_component[comp_of[relpath]]["rule9"] += 1
            all_findings.append({"file": relpath, "line": line, "rule": "RULE-9",
                                 "symbol": caller,
                                 "message": "pointed-to set of '%s' parameter %d is not enumerable: "
                                            "argument '%s' (rule 9: tractable function pointers)"
                                            % (fname, k, " ".join(arg.split()))})
```
and before the `return`: `rule8a["pointed_to"] = {"%s#%d" % key: sorted(v) for key, v in sorted(pointed.items())}`.
3e. `render_text`, after the `rule8a:` line:
```python
    if rule8a.get("pointed_to"):
        lines.append("rule9 pointed-to sets (function#param -> functions passed there):")
        for key, fns in rule8a["pointed_to"].items():
            lines.append("  %s -> {%s}" % (key, ", ".join(fns)))
```
Docstring RULE-9: list the spellings (raw and array declarators, anonymous casts, typedef'd params/fields/variables incl. the SDK typedefs, every function used as a value) and "the pointed-to set of every function-pointer parameter is printed".

- [ ] **Step 4: Tests pass** — `Ran 41 tests` `OK` (LG-3 tests unchanged: still 4 findings, still silent when registered).

- [ ] **Step 5: Mutation** — in `_VALUE_CTX_RE` drop `,` from the leading class; `test_esp_console_shape_…` FAILS; restore. `mutations/SD-S3-T5.txt`.

- [ ] **Step 6: File** — expected `appended 41 row(s)`: `components/app/cmd/cmd.c` 25 (24 functions with a `cmd_emit_fn` parameter + field `emit`), `components/app/include/app/cmd.h` 1 (`cmd_dispatch`), `components/drivers/export_serial/export_serial.c` 15 (13 console callbacks, `ser_emit`, `sframe_emit`). Check the printed set `cmd_dispatch#4 -> {ser_emit, sframe_emit}` and `register_cmd#2 -> {cmd_close_c, …, cmd_tracks_c}` (13 names). `gate_register` PASS; CI line rc 0.

- [ ] **Step 7: Commit** `feat(p10): rule 9 sees typedef'd params/fields, every function used as a value, prints pointed-to sets (S3 T5)`; ledger `- S3-T5 DONE <sha>; filed … (41)`.

### Task 6: Every tree blocking, the host-test allowance, `gate_p10`, CI on the gate

**Findings:** SD-S3-T6 (closes LG-7 PARTIAL "devcontroller report-only" and the strict-delivery spec §2 "Nothing is report-only")

**Files:**
- Modify: `tools/lint/power_of_10.py` — `DEFAULT_PATHS`/`DEFAULT_REPORT_ONLY_PATHS` (:62-66), `EXEMPT_PATH_FRAGMENTS` (:74-80), `ASSERT_RE` (:504), rule-5 and rule-3 calls in `run()`, the BLOCKING print in `main()`
- Create: `tools/lint/fixtures/r3_allow_test.c`, `r3_allow_misplaced.c`, `r5_unity.c`; `tools/gates/gate_p10.py`; `tools/gates/tests/test_gate_p10.py`
- Modify: `tools/lint/test_power_of_10.py` (class `AllTrees`); `tools/gates/run_all.py` (`GATES`); `tools/gates/hooks/pre_tool_use.py` (`GATED`); `tools/gates/hooks/git-pre-commit.sh` (`--only`); `.github/workflows/gates.yml`; `.github/workflows/ci.yml:162-177`
- Modify: `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: S1 `tools.gates.lib.{CODE_ROOT, Line, Result, gate_args, repo_root, run_gate}`, `run_all.GATES` / `gate_argv(name, args)` (`--root` + `args[name]`), `pre_tool_use.GATED`, umbrella CLI row "Any gate".
- Produces: `HOST_TEST_PREFIXES`, `HOST_TEST_HEAP_ALLOWANCE = "/* p10: host-test heap allowed */"`, `TEST_ASSERT_RE`; `gate_p10.check(args) -> Result`, `gate_p10.main(argv=None)`, `gate_p10.linter_argv(paths) -> list`, `gate_p10.changed_set(root, changed, base) -> set`, `gate_p10.ROLLOUT_GRACE` (deleted in Task 8). CLI `python3 tools/gates/gate_p10.py [--json] [--root R] [--paths <files...>] [--changed <files...>] [--base <ref>]`. Line ids: `p10.touched.<file>:<line>` (FAIL; a violation in a changed file, even with a row), `p10.<file>:<line>.<RULE>` (FAIL; untracked), `p10.stale.<id>`, `p10.register.<id>` (Task 7 on), `p10.input`, `p10.rc`, `p10.clean`, `p10.tracked.<id>` (PASS, one per tracked row hit outside the changed set).

Scope note (spec §9 "every tree" vs the compliance design §1 exemption of `tools/replay/**` and `test/**`): the strict-delivery spec is later and binding, so both are scanned; `components/lt_hal` (headers only, 0 findings) is added because no tree may be silently outside. `tools/lint/fixtures/` stays outside the default paths (it is the linter's own test input).

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixtures**

`tools/lint/fixtures/r3_allow_test.c`:
```c
/* p10: host-test heap allowed */
/* probe (rule 3 allowance, positive -- must PASS when this file is under a host-test prefix). */
#include <stdlib.h>
void *t_alloc(size_t n) { return malloc(n); }
```
`tools/lint/fixtures/r3_allow_misplaced.c`:
```c
/* p10: host-test heap allowed */
/* probe (rule 3 allowance, negative -- must FAIL: not under a host-test prefix). */
#include <stdlib.h>
void *x_alloc(size_t n) { return malloc(n); }
```
`tools/lint/fixtures/r5_unity.c`:
```c
/* probe (rule 5 in a host test): Unity TEST_ASSERT_* count as the function's assertions only
 * under a host-test prefix. */
void test_long(void)
{
    int a = 1;
    a = a * 1 + 1;
    a = a * 2 + 1;
    a = a * 3 + 1;
    a = a * 4 + 1;
    a = a * 5 + 1;
    a = a * 6 + 1;
    a = a * 7 + 1;
    a = a * 8 + 1;
    a = a * 9 + 1;
    a = a * 10 + 1;
    a = a * 11 + 1;
    a = a * 12 + 1;
    a = a * 13 + 1;
    a = a * 14 + 1;
    a = a * 15 + 1;
    a = a * 16 + 1;
    a = a * 17 + 1;
    a = a * 18 + 1;
    a = a * 19 + 1;
    a = a * 20 + 1;
    TEST_ASSERT_TRUE(a > 0);
    TEST_ASSERT_EQUAL_INT(a, a);
}
```

- [ ] **Step 2: Failing tests** — append to `tools/lint/test_power_of_10.py`:
```python
class AllTrees(unittest.TestCase):
    """S3 b8: every tree scanned and blocking; the host-test rule-3 allowance."""

    def test_default_paths_cover_every_c_tree_and_nothing_is_report_only(self):
        self.assertEqual(["components/core", "components/app", "components/drivers",
                          "components/lt_hal", "main", "devcontroller", "test", "test_apps",
                          "tools/replay"], p10.DEFAULT_PATHS)
        self.assertEqual([], p10.DEFAULT_REPORT_ONLY_PATHS)
        self.assertNotIn("devcontroller/test/", p10.EXEMPT_PATH_FRAGMENTS)

    def test_allowance_inside_a_host_test_prefix_exempts_rule_3_only(self):
        saved = p10.HOST_TEST_PREFIXES
        p10.HOST_TEST_PREFIXES = (FIX + "/r3_allow_test.c",)
        try:
            rc, out = run(["--paths", FIX + "/r3_allow_test.c", "--register", NONE,
                           "--fail-on-violation"])
        finally:
            p10.HOST_TEST_PREFIXES = saved
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_allowance_outside_a_host_test_prefix_is_itself_a_finding(self):
        rc, out = run(["--paths", FIX + "/r3_allow_misplaced.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("RULE-3: host-test heap allowance outside test/", out)
        self.assertIn("RULE-3: dynamic memory call 'malloc('", out)
        self.assertEqual(1, rc)

    def test_unity_asserts_count_for_rule_5_only_in_host_test_trees(self):
        saved = p10.HOST_TEST_PREFIXES
        p10.HOST_TEST_PREFIXES = (FIX + "/r5_unity.c",)
        try:
            rc, out = run(["--paths", FIX + "/r5_unity.c", "--register", NONE,
                           "--fail-on-violation"])
        finally:
            p10.HOST_TEST_PREFIXES = saved
        self.assertEqual([], findings(out), out)
        rc2, out2 = run(["--paths", FIX + "/r5_unity.c", "--register", NONE,
                         "--fail-on-violation"])
        self.assertIn("function 'test_long' has 0 assertion(s)", out2)
        self.assertEqual((0, 1), (rc, rc2))
```
and create `tools/gates/tests/test_gate_p10.py`:
```python
#!/usr/bin/env python3
"""gate_p10 through its production path (main() on a fixture tree): clean, violation, tracked,
stale, register error, linter input error, --paths scoping."""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
from tools.gates import gate_p10  # noqa: E402

HEADER = ("| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | "
          "reviewer |\n|----|------|--------------|------|------|------|------|\n")
CLEAN_C = "int ok(int a) { return a + 1; }\n"
REC_C = "int walk(int d) { if (d <= 0) { return 0; } return walk(d - 1); }\n"


class GateP10(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)
        self.write("docs/power-of-10-deviations.md", HEADER)
        self.write("docs/superpowers/state/findings.yml", "[]\n")

    def write(self, rel, text):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)

    def gate(self, *extra):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_p10.main(["--root", self.root, *extra])
        return rc, buf.getvalue()

    def test_clean_tree_passes(self):
        self.write("components/core/a.c", CLEAN_C)
        rc, out = self.gate()
        self.assertEqual("PASS p10.clean\n", out)
        self.assertEqual(0, rc)

    def test_violation_is_one_fail_line(self):
        self.write("components/core/a.c", REC_C)
        rc, out = self.gate()
        self.assertIn("FAIL p10.components/core/a.c:1.RULE-1: function 'walk' calls itself", out)
        self.assertEqual(1, rc)

    def test_open_row_tracks_and_passes(self):
        self.write("components/core/a.c", REC_C)
        self.write("docs/superpowers/state/findings.yml",
                   '- id: S3-P10-1\n  state: open\n  repro: "p10-site:components/core/a.c:walk:RULE-1"\n')
        rc, out = self.gate()
        self.assertIn("PASS p10.tracked.S3-P10-1: open findings row, file not touched", out)
        self.assertEqual(0, rc)

    def test_tracked_violation_in_an_untouched_file_passes_and_in_a_touched_file_fails(self):
        self.write("components/core/a.c", REC_C)
        self.write("components/core/b.c", CLEAN_C)
        self.write("docs/superpowers/state/findings.yml",
                   '- id: S3-P10-1\n  state: open\n  repro: "p10-site:components/core/a.c:walk:RULE-1"\n')
        rc, out = self.gate("--changed", "components/core/b.c")
        self.assertIn("PASS p10.tracked.S3-P10-1", out)
        self.assertEqual(0, rc)
        rc, out = self.gate("--changed", "components/core/a.c")
        self.assertIn("FAIL p10.touched.components/core/a.c:1: RULE-1: function 'walk' calls itself", out)
        self.assertIn("(row S3-P10-1 does not excuse a touched file)", out)
        self.assertNotIn("PASS p10.tracked", out)
        self.assertEqual(1, rc)

    def test_base_ref_adds_the_branch_diff_to_the_touched_set(self):
        import subprocess
        self.write("components/core/a.c", REC_C)
        self.write("docs/superpowers/state/findings.yml",
                   '- id: S3-P10-1\n  state: open\n  repro: "p10-site:components/core/a.c:walk:RULE-1"\n')
        g = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c",
                                       "commit.gpgsign=false", *a], cwd=self.root, check=True,
                                      capture_output=True)
        g("init", "-q", "-b", "main"); g("add", "-A"); g("commit", "-qm", "base")
        g("checkout", "-qb", "topic")
        with open(os.path.join(self.root, "components/core/a.c"), "a") as f:
            f.write("/* touched */\n")
        g("commit", "-qam", "touch a.c")
        rc, out = self.gate("--base", "main")
        self.assertIn("FAIL p10.touched.components/core/a.c:1", out)
        self.assertEqual(1, rc)

    def test_stale_row_fails(self):
        self.write("components/core/a.c", CLEAN_C)
        self.write("docs/superpowers/state/findings.yml",
                   '- id: S3-P10-1\n  state: open\n  repro: "p10-site:components/core/a.c:ok:RULE-5"\n')
        rc, out = self.gate()
        self.assertIn("FAIL p10.stale.S3-P10-1: components/core/a.c:ok:RULE-5 matches no finding", out)
        self.assertEqual(1, rc)

    def test_missing_findings_register_is_input_fail(self):
        self.write("components/core/a.c", CLEAN_C)
        os.remove(os.path.join(self.root, "docs/superpowers/state/findings.yml"))
        rc, out = self.gate()
        self.assertIn("FAIL p10.input: linter rc 2: INPUT-ERROR: findings register", out)
        self.assertEqual(1, rc)

    def test_paths_scope_to_the_named_c_files_only(self):
        self.write("components/core/a.c", CLEAN_C)
        self.write("components/core/b.c", REC_C)
        rc, out = self.gate("--paths", "components/core/a.c", "README.md")
        self.assertNotIn("b.c", out)
        self.assertEqual(0, rc)


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/lint/test_power_of_10.py tools/gates/tests/test_gate_p10.py` → `AllTrees` fails; `test_gate_p10` errors (`No module named tools.gates.gate_p10`).

- [ ] **Step 3: Linter** — replace :62-66 with
```python
# Every C tree in the repository (spec §9 "every tree"; strict-delivery spec §2 "Nothing is
# report-only"): the firmware, the dev-kit firmware, and the host/target test harnesses.
DEFAULT_PATHS = ["components/core", "components/app", "components/drivers", "components/lt_hal",
                 "main", "devcontroller", "test", "test_apps", "tools/replay"]

# No path is report-only by default any more (LG-7 closed by S3 b8).
DEFAULT_REPORT_ONLY_PATHS = []

# Host/target test harnesses and host-only tools: a file under one of these prefixes may carry
# HOST_TEST_HEAP_ALLOWANCE once, which exempts that file from RULE-3 only (spec §9). Anywhere
# else the comment is itself a RULE-3 finding. Unity assertions count for RULE-5 here.
HOST_TEST_PREFIXES = ("test/", "test_apps/", "devcontroller/test/", "tools/replay/")
HOST_TEST_HEAP_ALLOWANCE = "/* p10: host-test heap allowed */"
```
delete `"devcontroller/test/",` from `EXEMPT_PATH_FRAGMENTS` (:79); after `ASSERT_RE` (:504) add
```python
# in a host-test tree a Unity TEST_ASSERT* is the function's assertion (RULE-5)
TEST_ASSERT_RE = re.compile(r"\b(CORE_ASSERT_RET|CORE_ASSERT_VOID|LT_ASSERT_RET|LT_ASSERT_VOID|"
                            r"TEST_ASSERT[A-Z0-9_]*)\s*\(")
```
in `run()` the rule-5 call becomes
```python
        host_test = posix_relpath.startswith(HOST_TEST_PREFIXES)
        rule5_findings = check_rule5_assertions(stripped, relpath, lidx, register, funcs,
                                                assert_re=TEST_ASSERT_RE if host_test else None)
```
and after the `check_rule3_heap(...)` call:
```python
        if HOST_TEST_HEAP_ALLOWANCE in text:
            if host_test:
                comp["rule3_allowed"] = comp.get("rule3_allowed", 0) + len(rule3_findings)
                rule3_findings = []
            else:
                rule3_findings.append({"file": relpath, "line": lidx.line_of(text.index(
                    HOST_TEST_HEAP_ALLOWANCE)), "rule": "RULE-3", "symbol": "",
                    "message": "host-test heap allowance outside %s (rule 3: no heap after init)"
                               % ", ".join(HOST_TEST_PREFIXES)})
```
In `main()` print `BLOCKING: …` only `if not args.json:` (the gate parses the JSON; a trailing text line breaks it). Docstring: scope paragraph → "every C tree; nothing report-only by default; host-test allowance".

- [ ] **Step 4: `tools/gates/gate_p10.py`**:
```python
#!/usr/bin/env python3
"""gate_p10 -- Power of 10 on every tree (strict-delivery spec §7, §9; D13): a thin wrapper that
runs tools/lint/power_of_10.py with the CI flag set and turns its JSON into gate lines.

  python3 tools/gates/gate_p10.py [--json] [--root <tree>] [--paths <files...>]

--paths limits the scan to the named .c/.h files (pre-commit: the staged ones); with no --paths,
or none of them C, every tree is scanned (DEFAULT_PATHS of the linter). One FAIL line per
untracked violation, per TRACKED-STALE row and per REGISTER-ERROR; a linter that exits 2, crashes
or prints no JSON is FAIL p10.input (fail closed).
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import CODE_ROOT, GateError, Line, Result, gate_args, repo_root, run_gate  # noqa: E402

LINTER = os.path.join(CODE_ROOT, "tools", "lint", "power_of_10.py")
FINDINGS = os.path.join("docs", "superpowers", "state", "findings.yml")
# ROLLOUT GRACE carried over from ci.yml:162-177 (ruling R12, issue #131); deleted with the
# linter flag in S3 Task 8, when the five sites become findings rows S3-P10-G1..G5.
ROLLOUT_GRACE = ",".join([
    "components/app/sys/lt_nvs.c:errlog_add:RULE-5",
    "components/core/config/cfg.c:cfg_defaults:RULE-5",
    "components/core/config/cfg_json.c:cfg_to_json:RULE-4",
    "components/drivers/export_serial/export_serial.c:fmt_from_str:RULE-5",
    "components/drivers/export_serial/export_serial.c:tally_cb:RULE-5",
])
BLOCKING = ("RULE-1", "RULE-2", "RULE-3", "RULE-4", "RULE-4-COMPOUND", "RULE-5", "RULE-8",
            "RULE-9")


def linter_argv(paths: list) -> list:
    """The CI flag set (umbrella S3 b8): every rule blocking, rule 9 on, tracked by findings.yml."""
    argv = [sys.executable, LINTER, "--enforce-fnptr", "--fail-on-violation", "--json",
            "--report-only-sites", ROLLOUT_GRACE, "--findings", FINDINGS]
    return argv + (["--paths", *paths] if paths else [])


def changed_set(root: str, changed: list, base) -> set:
    """Owner ruling 3: the files a task touches. --changed names them (the commit hook passes
    the staged files); --base <ref> adds `git diff --name-only <ref>...HEAD` (CI: origin/main)."""
    out = {c.replace(os.sep, "/") for c in changed}
    if base:
        p = subprocess.run(["git", "diff", "--name-only", "%s...HEAD" % base], cwd=root,
                           capture_output=True, text=True, check=False)
        if p.returncode != 0:
            raise GateError("git diff --name-only %s...HEAD: %s" % (base, p.stderr.strip()))
        out |= {l.strip() for l in p.stdout.splitlines() if l.strip()}
    return out


def check(args: list) -> Result:
    ap = gate_args("gate_p10")
    ap.add_argument("--paths", nargs="*", default=[])
    ap.add_argument("--changed", nargs="*", default=[])
    ap.add_argument("--base", default=None)
    ns = ap.parse_args(args)
    root = ns.root or repo_root()
    touched = changed_set(root, ns.changed, ns.base)
    paths = [p for p in ns.paths if p.endswith((".c", ".h")) and os.path.isfile(os.path.join(root, p))]
    p = subprocess.run(linter_argv(paths), cwd=root, capture_output=True, text=True, check=False)
    r = Result("p10")
    if p.returncode not in (0, 1):
        r.fail("p10.input", "linter rc %d: %s" % (p.returncode, (p.stdout + p.stderr).strip()[-400:]))
        return r
    try:
        data, _end = json.JSONDecoder().raw_decode(p.stdout.lstrip())
    except ValueError:
        r.fail("p10.input", "linter printed no JSON: %s" % (p.stdout + p.stderr).strip()[-400:])
        return r
    tracked = []
    for f in data["findings"]:
        if f["rule"] not in BLOCKING:
            continue
        if f.get("report_only"):
            continue                      # the five graced sites, until Task 8
        if f["file"] in touched:          # ruling 3: a touched file is fixed, never tracked
            r.fail("p10.touched.%s:%d" % (f["file"], f["line"]), "%s: %s%s" % (
                f["rule"], f["message"],
                " (row %s does not excuse a touched file)" % f["tracked"] if f.get("tracked") else ""))
            continue
        if f.get("tracked"):
            tracked.append(f["tracked"])
            continue
        r.fail("p10.%s:%d.%s" % (f["file"], f["line"], f["rule"]), f["message"])
    for s in data.get("tracked_stale", []):
        r.fail("p10.stale.%s" % s["id"], "%s matches no finding -- advance the row" % s["site"])
    for e in data.get("register_errors", []):
        r.fail("p10.register.%s" % e["id"], e["reason"])
    failed = any(l.status == "FAIL" for l in r.lines)
    if p.returncode == 1 and not failed and not any(
            l.id.startswith("p10.touched.") for l in r.lines):
        r.fail("p10.rc", "linter exited 1 without a parseable violation")
    if p.returncode == 0 and any(not l.id.startswith("p10.touched.") for l in r.lines
                                 if l.status == "FAIL"):
        r.fail("p10.rc", "linter exited 0 with blocking lines")
    if not failed and p.returncode == 0:
        r.passed("p10.clean")
    for fid in sorted(set(tracked)):
        r.lines.append(Line("PASS", "p10.tracked.%s" % fid, "open findings row, file not touched"))
    return r


def main(argv=None):
    return run_gate("p10", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```
`tools/gates/run_all.py`: `GATES = ["register", "claims", "decisions", …existing…, "p10"]` (append, keep order). `tools/gates/hooks/pre_tool_use.py`: add `"p10"` to both lists in `GATED` (S1: `GATED = {"commit": [...], "push": [...]}`) and pass the touched set as its per-gate argv (`args["p10"]`, consumed by S1 `gate_argv`): commit → `["--changed", *gitinfo.staged_files(root)]`, push → `["--base", "origin/main"]`. The scan is always every tree (≈ 3.5 s measured), which also avoids Review Focus 1's cross-file blind spot. `tools/gates/hooks/git-pre-commit.sh`: after the existing `run_all.py --only register,claims` line add `python3 tools/gates/gate_p10.py --changed $(git diff --cached --name-only) || exit 1`. `.github/workflows/gates.yml`: after the step that runs `run_all.py` add `- name: gate_p10 (every tree; touched = this PR)` / `run: python3 tools/gates/gate_p10.py --base origin/main` (S1's checkout has full history). `.github/workflows/ci.yml`: replace the ROLLOUT GRACE comment (:162-172) and the linter step (:174-177) with
```yaml
      - name: Install Python requirements
        run: python3 -m pip install -r tools/requirements.txt
      # Power of 10 via the gate (strict-delivery S3): every tree, every rule blocking, open findings
      # rows tracked; the grace triples live in tools/gates/gate_p10.py until S3 Task 8 deletes them.
      - name: gate_p10 (BLOCKING; rules 1/2/3/4/4-compound/5/8/9, every tree; touched = this PR)
        run: python3 tools/gates/gate_p10.py --base origin/main
```
(job id and name `static-analysis` unchanged, so branch protection keeps its required check; add `fetch-depth: 0` to that job's `actions/checkout@v4` `with:` so `origin/main...HEAD` resolves).

- [ ] **Step 5: No allowance comments in existing files (ruling 3)** — the 15 host-test files with heap calls (`test/test_lap.c`, `test_apps/core_selftest/main/main.c`, 13 under `tools/replay/`) also carry 252 non-heap violations (79 RULE-2, 117 RULE-9, 34 RULE-5, 11 RULE-8, 11 RULE-4/4-COMPOUND); adding the comment would make each a touched file to clean in this task. Their 49 rule-3 sites are filed as tracked rows in Step 8; the S5 sweep adds the comment as it cleans each file.

- [ ] **Step 6: Tests pass** — `python3 -m unittest tools/lint/test_power_of_10.py tools/gates/tests/test_gate_p10.py` → `Ran 53 tests` `OK` (lint 45, gate 8); `python3 -m unittest discover -s tools/gates/tests` OK.

- [ ] **Step 7: Mutation** — set `DEFAULT_REPORT_ONLY_PATHS = ["devcontroller/"]`; `test_default_paths_cover_every_c_tree_and_nothing_is_report_only` FAILS; restore. In `gate_p10.check` delete the `p.returncode not in (0, 1)` branch; `test_missing_findings_register_is_input_fail` FAILS; restore. `mutations/SD-S3-T6.txt`.

- [ ] **Step 8: File** — the filing command with the b8 flag set (no `--report-only-paths` needed: the default is empty). Expected ≈ 593 rows: devcontroller ≈ 225 (incl. devcontroller/test 49), test ≈ 151, test_apps ≈ 9, tools/replay ≈ 208; by rule ≈ RULE-1 3 (the `goto`s at `cmd_flash.c:480`, `cmd_shell.c:85`, `linkhost.c:605`), RULE-2 189, RULE-3 54 (49 host-test heap sites, Step 5), RULE-4 15, RULE-4-COMPOUND 12, RULE-5 132, RULE-8 4, RULE-9 184. Then `gate_register` PASS and `python3 tools/gates/gate_p10.py` → `PASS p10.clean` + one `PASS p10.tracked.<id>` per row.

- [ ] **Step 9: Commit**
```bash
git add -A tools .github docs/superpowers/state
git commit -m "feat(p10): every C tree blocking, host-test heap allowance, gate_p10 replaces the CI linter step; surfaced sites filed (S3 T6)"
```
Ledger `- S3-T6 DONE <sha>; filed S3-P10-<a>..<b> (<n>)`.

### b8 block end (CHECKPOINT)

Same steps as b7's block end with `b8`, `next_task: "7"`, tag `strict-delivery-b8`, the resume prompt for `block 9, sub-plan S3, task 7`; `gate_checkpoint --plan strict-delivery` PASS before stopping.

## Block b9 — the two-tier register, scoring, expiry, grace removed (Tasks 7–9), branch `block/strict-delivery-b9`

Block start: ledger `- b9 start <sha>`.

### Task 7: Register parser — twelve columns, tier, scores, report, owner, expiry, live site, pointed-to set

**Findings:** SD-S3-T7 (spec §9 Exceptions, D14; audit V-3 "dead register row")

**Files:**
- Modify: `tools/lint/power_of_10.py` — replace the register block (:757-799, header comment through `is_registered`); `run()` signature and register check; `check_rule9_values` (coverage); `main()` (`--config`, `register.errors`)
- Modify (rewrite in the new format): `tools/lint/fixtures/register_none.md`, `register_sinks.md`, `register_cross.md`, `register_r2.md`, `register_r3.md`, `register_r9.md`; create `register_r9_console.md`, `register_r9_console_short.md`
- Modify: `tools/lint/test_power_of_10.py` (helpers `HEADER`, `TIER_A_SCORES`, `reg_row`; classes `RegisterTwoTier`, `PointedToRegister`)

**Interfaces:**
- Consumes: `tools/gates/config.yml` `exceptions` (umbrella, verbatim): cost `blast {tooling 1, ui 2, timing 3, data-integrity 3, supervision 3}` + `verifiability [0, 2]` + `reach {transient 0, stored_or_3plus 2}`; benefit `complexity {helper 1, module 2, task_or_60_lines 3}` + `runtime [0, 2]`.
- Produces: `REGISTER_COLUMNS` = `id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry`; `load_scales(path)`; `score(cell, scale, terms) -> (total | None, error | None)`; `_git(args) -> (rc, stdout)`; `Register.load(path, scales)` raising `InputError` on a missing file / wrong header; `Register.errors [(id, reason)]`; `Register.pointed {symbol: set}`; `Register.covers(forwarder, function)`; `run(paths, register_path, enforce_fnptr, base_dir, scales=None)`; CLI `--config` (default `tools/gates/config.yml`); output `REGISTER-ERROR: <id>: <reason>`, JSON `register_errors`. Score cell grammar: `<total> = blast:<key> <v> + verifiability <v> + reach:<key> <v>` and `<total> = complexity:<key> <v> + runtime <v>[ (measured: <file>)]`. Tier cell: `A (<SDK contract>)` or `B`. Tier A: expiry `none`, report may be `n/a (tier A)`, scores informational. Tier B: benefit > cost, report file exists, expiry a commit sha that is not an ancestor of HEAD. Every row: site file exists and its symbol occurs in it; rule-9 rows carry `{…}` (`{}` = no caller), equal to the computed pointed-to set when the site's file is scanned.

Pre-b9 compatibility: none. A seven-column register is `INPUT-ERROR` rc 2 and the commit hook runs `gate_p10`, so Task 7's commit also carries the real register (Task 8 Step 4, done here), the former-PD-7 findings row and the 13 closures (the parts of Task 8 Step 5 after the G rows). Task 8 then only deletes the grace and files G1..G5.

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Fixture registers** (each file exactly):

`register_none.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
```
`register_sinks.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-1 | 9 | tools/lint/fixtures/lg3_sinks.c:cmpi | A (fixture: SDK-shaped contract) | {cmpi} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
| TD-2 | 9 | tools/lint/fixtures/lg3_sinks.c:pre_cb | A (fixture: SDK-shaped contract) | {pre_cb} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
| TD-3 | 9 | tools/lint/fixtures/lg3_sinks.c:forwarder | A (fixture: SDK-shaped contract) | {} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_cross.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-4 | 9 | tools/lint/fixtures/lg3_cross_file_def.c:cmp_elsewhere | A (fixture: SDK-shaped contract) | {cmp_elsewhere} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_r2.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-5 | 9 | tools/lint/fixtures/r2_pos.c:r2_task | A (fixture: SDK-shaped contract) | {r2_task} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_r3.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-6 | 3 | tools/lint/fixtures/r3_pos.c:r3_init_pool | A (fixture: SDK-shaped contract) | - | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_r9.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-8 | 9 | tools/lint/fixtures/r9_pos.c:r9_task | A (FreeRTOS TaskFunction_t) | {r9_task} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_r9_console.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-7 | 9 | tools/lint/fixtures/r9_console.c:register_cmd | A (IDF esp_console_cmd_func_t) | {cmd_a, cmd_b} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```
`register_r9_console_short.md`:
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| TD-7 | 9 | tools/lint/fixtures/r9_console.c:register_cmd | A (IDF esp_console_cmd_func_t) | {cmd_a} | n/a (fixture) | n/a (fixture) | 1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | fixture | none |
```

- [ ] **Step 2: Failing tests** (append):
```python
HEADER = ("| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | "
          "cost | benefit | adversarial report | owner | expiry |\n"
          "|----|------|------|------|----------------|-----------------------|--------------|"
          "------|---------|--------------------|-------|--------|\n")
TIER_A_SCORES = ("1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | "
                 "1 = complexity:helper 1 + runtime 0")
def reg_row(rid, rule, site, tier="A (fixture contract)", pts="-", scores=TIER_A_SCORES,
            report="n/a (tier A)", owner="fixture", expiry="none"):
    return "| %s | %s | %s | %s | %s | alt | why | %s | %s | %s | %s |\n" % (
        rid, rule, site, tier, pts, scores, report, owner, expiry)


class RegisterTwoTier(unittest.TestCase):
    """S3 b9: the twelve-column register -- tier, scoring, report, owner, expiry, live site."""

    def setUp(self):
        import shutil, subprocess, tempfile
        self.sp = subprocess
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.cwd = os.getcwd()
        self.addCleanup(os.chdir, self.cwd)
        shutil.copy(os.path.join(self.cwd, FIX, "lg6_recursion.c"), os.path.join(self.tmp, "a.c"))
        os.makedirs(os.path.join(self.tmp, "tools", "gates"))
        shutil.copy(os.path.join(self.cwd, "tools", "gates", "config.yml"),
                    os.path.join(self.tmp, "tools", "gates", "config.yml"))
        with open(os.path.join(self.tmp, "adv.md"), "w") as f:
            f.write("adversarial report\n")
        os.chdir(self.tmp)
        g = lambda *a: self.sp.run(["git", "-c", "user.name=t", "-c", "user.email=t@t",
                                    "-c", "commit.gpgsign=false", *a],
                                   check=True, capture_output=True, text=True).stdout.strip()
        g("init", "-q", "-b", "main")
        g("add", "-A")
        g("commit", "-qm", "base")
        self.base = g("rev-parse", "HEAD")
        g("checkout", "-qb", "side")
        g("commit", "-q", "--allow-empty", "-m", "side")
        self.side = g("rev-parse", "HEAD")
        g("checkout", "-q", "main")

    def lint(self, rows):
        with open("reg.md", "w") as f:
            f.write(HEADER + rows)
        return run(["--paths", "a.c", "--register", "reg.md", "--fail-on-violation"])

    def tier_b(self, cost, benefit, expiry=None, report="adv.md"):
        return reg_row("PD-B", 1, "a.c:walk", tier="B", scores="%s | %s" % (cost, benefit),
                       report=report, expiry=expiry or self.side)

    def test_tier_a_row_registers_the_site(self):
        rc, out = self.lint(reg_row("PD-A", 1, "a.c:walk"))
        self.assertEqual([], findings(out), out)
        self.assertEqual(0, rc)

    def test_tier_b_benefit_above_cost_registers(self):
        rc, out = self.lint(self.tier_b("1 = blast:tooling 1 + verifiability 0 + reach:transient 0",
                                        "3 = complexity:task_or_60_lines 3 + runtime 0"))
        self.assertNotIn("REGISTER-ERROR", out)
        self.assertEqual(0, rc)

    def test_tier_b_benefit_equal_to_cost_fails(self):
        rc, out = self.lint(self.tier_b("3 = blast:timing 3 + verifiability 0 + reach:transient 0",
                                        "3 = complexity:task_or_60_lines 3 + runtime 0"))
        self.assertIn("REGISTER-ERROR: PD-B: tier B benefit 3 does not exceed cost 3 (REGISTER-SCORE)", out)
        self.assertEqual(1, rc)

    def test_tier_b_benefit_below_cost_fails(self):
        rc, out = self.lint(self.tier_b(
            "6 = blast:data-integrity 3 + verifiability 1 + reach:stored_or_3plus 2",
            "3 = complexity:task_or_60_lines 3 + runtime 0"))
        self.assertIn("benefit 3 does not exceed cost 6", out)
        self.assertEqual(1, rc)

    def test_wrong_arithmetic_and_off_scale_terms_fail(self):
        rc, out = self.lint(self.tier_b("2 = blast:tooling 1 + verifiability 0 + reach:transient 0",
                                        "4 = complexity:helper 2 + runtime 2"))
        self.assertIn("terms sum to 1, not 2", out)
        self.assertIn("complexity must be one of", out)
        self.assertEqual(1, rc)

    def test_runtime_gain_needs_a_measurement_file(self):
        rc, out = self.lint(self.tier_b("1 = blast:tooling 1 + verifiability 0 + reach:transient 0",
                                        "3 = complexity:module 2 + runtime 1"))
        self.assertIn("runtime gain 1 needs '(measured: <existing file>)'", out)
        self.assertEqual(1, rc)

    def test_expired_row_fails(self):
        rc, out = self.lint(self.tier_b("1 = blast:tooling 1 + verifiability 0 + reach:transient 0",
                                        "3 = complexity:task_or_60_lines 3 + runtime 0",
                                        expiry=self.base))
        self.assertIn("expired: expiry %s is an ancestor of HEAD (REGISTER-EXPIRED)" % self.base, out)
        self.assertEqual(1, rc)

    def test_unknown_expiry_and_missing_report_fail(self):
        rc, out = self.lint(self.tier_b("1 = blast:tooling 1 + verifiability 0 + reach:transient 0",
                                        "3 = complexity:task_or_60_lines 3 + runtime 0",
                                        expiry="deadbeef", report="nope.md"))
        self.assertIn("expiry deadbeef is not a commit in this repository", out)
        self.assertIn("tier B adversarial report 'nope.md' does not exist", out)
        self.assertEqual(1, rc)

    def test_missing_site_file_and_missing_symbol_fail(self):
        rc, out = self.lint(reg_row("PD-1", 1, "gone.c:walk") + reg_row("PD-2", 1, "a.c:vanished"))
        self.assertIn("REGISTER-ERROR: PD-1: site file gone.c does not exist (REGISTER-STALE)", out)
        self.assertIn("REGISTER-ERROR: PD-2: symbol vanished not found in a.c (REGISTER-STALE)", out)
        self.assertEqual(1, rc)

    def test_tier_a_needs_expiry_none_and_tier_letter(self):
        rc, out = self.lint(reg_row("PD-1", 1, "a.c:walk", tier="C", expiry="abc1234"))
        self.assertIn("tier 'C'", out)
        self.assertIn("tier A expiry must be 'none'", out)
        self.assertEqual(1, rc)

    def test_old_seven_column_register_is_rc_2(self):
        with open("old.md", "w") as f:
            f.write("| id | rule | file:symbol | compliant alternative considered | why rejected "
                    "| benefit | reviewer |\n|---|---|---|---|---|---|---|\n")
        rc, out = run(["--paths", "a.c", "--register", "old.md", "--fail-on-violation"])
        self.assertIn("INPUT-ERROR: register old.md header is", out)
        self.assertEqual(2, rc)

    def test_missing_register_is_rc_2(self):
        rc, out = run(["--paths", "a.c", "--register", "none.md", "--fail-on-violation"])
        self.assertIn("INPUT-ERROR: deviation register none.md not found", out)
        self.assertEqual(2, rc)


class PointedToRegister(unittest.TestCase):
    """S3 b9: a tier-A forwarder row lists its pointed-to set; the linter checks it equals the
    computed set and treats the listed functions as covered."""

    def lint(self, register):
        return run(["--paths", FIX + "/r9_console.c", "--register", register, "--enforce-fnptr",
                    "--fail-on-violation"])

    def test_listed_set_equal_to_computed_covers_the_callbacks(self):
        rc, out = self.lint(FIX + "/register_r9_console.md")
        self.assertEqual([], findings(out), out)
        self.assertIn("register_cmd#1 -> {cmd_a, cmd_b}", out)
        self.assertEqual(0, rc)

    def test_a_callback_missing_from_the_listed_set_fails(self):
        rc, out = self.lint(FIX + "/register_r9_console_short.md")
        self.assertIn("pointed-to set {cmd_a} != computed {cmd_a, cmd_b} (REGISTER-SET)", out)
        self.assertIn("unregistered function 'cmd_b' passed to register_cmd()", out)
        self.assertEqual(1, rc)
```
(The temp repos use `-c commit.gpgsign=false`: the owner's global config signs commits and the test must not depend on a key.) Run → every new test fails (the old parser ignores the new columns, so the old tests still pass).

- [ ] **Step 3: Implement** — replace :757-799 with:
```python
REGISTER_COLUMNS = ["id", "rule", "site", "tier", "pointed-to set", "compliant alternative",
                    "why rejected", "cost", "benefit", "adversarial report", "owner", "expiry"]
_TERM_RE = re.compile(r"^(blast|verifiability|reach|complexity|runtime)(?::([a-z0-9_-]+))?\s+(\d+)"
                      r"(?:\s*\(measured:\s*([^)\s]+)\s*\))?$")
_SHA_RE = re.compile(r"^[0-9a-f]{7,40}$")
def load_scales(path):
    """The `exceptions` scales from tools/gates/config.yml; InputError when unreadable."""
    if not os.path.isfile(path):
        raise InputError("config %s not found (exception scales)" % path)
    try:
        import yaml
    except ImportError as exc:
        raise InputError("PyYAML missing (pip install -r tools/requirements.txt): %s" % exc)
    with open(path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    ex = cfg.get("exceptions")
    if not isinstance(ex, dict) or "cost" not in ex or "benefit" not in ex:
        raise InputError("config %s has no exceptions.cost/benefit scales" % path)
    return ex

def score(cell, scale, terms):
    """(total, error) for a 'N = term v + term v' cell checked against `scale` (one of
    exceptions.cost / exceptions.benefit) and the required term names, in order."""
    if "=" not in cell:
        return None, "score %r has no 'total = breakdown'" % cell
    total_s, breakdown = cell.split("=", 1)
    if not total_s.strip().isdigit():
        return None, "score total %r is not an integer" % total_s.strip()
    parts = [p.strip() for p in breakdown.split("+")]
    if len(parts) != len(terms):
        return None, "score %r: want terms %s" % (cell, " + ".join(terms))
    acc = 0
    for part, want in zip(parts, terms):
        m = _TERM_RE.match(part)
        if not m or m.group(1) != want:
            return None, "score term %r: want '%s ...'" % (part, want)
        key, val, measured = m.group(2), int(m.group(3)), m.group(4)
        spec = scale.get(want)
        if isinstance(spec, dict):
            if key not in spec or spec[key] != val:
                return None, "score term %r: %s must be one of %s" % (part, want, spec)
        elif isinstance(spec, list) and len(spec) == 2:
            if key is not None or not spec[0] <= val <= spec[1]:
                return None, "score term %r: %s must be %d..%d" % (part, want, spec[0], spec[1])
        else:
            return None, "config scale for %s is malformed: %r" % (want, spec)
        if want == "runtime" and val > 0 and (not measured or
                                              not os.path.isfile(measured.split("#")[0])):
            return None, "runtime gain %d needs '(measured: <existing file>)'" % val
        acc += val
    if acc != int(total_s):
        return None, "score %r: terms sum to %d, not %s" % (cell, acc, total_s.strip())
    return acc, None

def _git(args):
    import subprocess
    p = subprocess.run(["git"] + args, capture_output=True, text=True, check=False)
    return p.returncode, p.stdout.strip()

class Register:
    def __init__(self):
        self._sites = set()      # {(rule_num:int, file:str, symbol:str)}
        self.rows = []           # dict per row, keys = REGISTER_COLUMNS
        self.errors = []         # [(row id, reason)]
        self.pointed = {}        # {symbol: set} listed pointed-to sets (rule 9 rows)

    @staticmethod
    def _rule_num(cell):
        m = re.search(r"\d+", cell)
        return int(m.group()) if m else None

    def load(self, path, scales=None):
        """Parse and validate the register. A missing file or a header that is not
        REGISTER_COLUMNS raises InputError (fail closed); a bad row becomes an error."""
        if not path or not os.path.isfile(path):
            raise InputError("deviation register %s not found" % path)
        with open(path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip().startswith("|")]
        header = None
        for line in lines:
            cells = [c.strip() for c in line.strip("|").split("|")]
            if header is None:
                if [c.lower() for c in cells] != REGISTER_COLUMNS:
                    raise InputError("register %s header is %s, want %s" % (path, cells,
                                                                             REGISTER_COLUMNS))
                header = cells
                continue
            if all(set(c) <= {"-", ":"} for c in cells):
                continue
            if len(cells) != len(REGISTER_COLUMNS):
                self.errors.append((cells[0], "row has %d cells, want %d"
                                    % (len(cells), len(REGISTER_COLUMNS))))
                continue
            row = dict(zip(REGISTER_COLUMNS, cells))
            self.rows.append(row)
            self._check_row(row, scales)
        if header is None:
            raise InputError("register %s has no table" % path)

    def _check_row(self, row, scales):
        rid = row["id"]
        err = lambda reason: self.errors.append((rid, reason))
        rule_num = self._rule_num(row["rule"])
        site = row["site"].strip("`")
        if rule_num is None or ":" not in site:
            err("rule %r / site %r malformed (want N and file:symbol)" % (row["rule"], site))
            return
        file_part, symbol = (s.strip() for s in site.split(":", 1))
        self._sites.add((rule_num, file_part, symbol))
        tier = row["tier"][:1]
        if tier not in ("A", "B"):
            err("tier %r: want 'A (<SDK contract>)' or 'B'" % row["tier"])
        if not os.path.isfile(file_part):
            err("site file %s does not exist (REGISTER-STALE)" % file_part)
        else:
            with open(file_part, "r", encoding="utf-8", errors="replace") as f:
                body = strip_comments_and_literals(f.read())
            if not re.search(r"(?<![A-Za-z0-9_])" + re.escape(symbol) + r"(?![A-Za-z0-9_])", body):
                err("symbol %s not found in %s (REGISTER-STALE)" % (symbol, file_part))
        pts = row["pointed-to set"].strip()
        if rule_num == 9:
            m = re.match(r"^\{(.*)\}$", pts)
            if not m:
                err("rule-9 row needs a pointed-to set '{f, g}' ('{}' = no caller), got %r" % pts)
            else:
                self.pointed[symbol] = {p.strip() for p in m.group(1).split(",") if p.strip()}
        if not row["owner"]:
            err("owner is empty")
        if scales is not None:
            cost, e1 = score(row["cost"], scales["cost"], ["blast", "verifiability", "reach"])
            benefit, e2 = score(row["benefit"], scales["benefit"], ["complexity", "runtime"])
            for e in (e1, e2):
                if e:
                    err(e)
            if tier == "B" and cost is not None and benefit is not None and benefit <= cost:
                err("tier B benefit %d does not exceed cost %d (REGISTER-SCORE)" % (benefit, cost))
        if tier == "B":
            rep = row["adversarial report"]
            if not os.path.isfile(rep):
                err("tier B adversarial report %r does not exist" % rep)
            exp = row["expiry"]
            if not _SHA_RE.match(exp):
                err("tier B expiry %r is not a sha" % exp)
            else:
                rc, _out = _git(["rev-parse", "--verify", "--quiet", exp + "^{commit}"])
                if rc != 0:
                    err("expiry %s is not a commit in this repository" % exp)
                else:
                    rc, _out = _git(["merge-base", "--is-ancestor", exp, "HEAD"])
                    if rc == 0:
                        err("expired: expiry %s is an ancestor of HEAD (REGISTER-EXPIRED)" % exp)
        elif row["expiry"] != "none":
            err("tier A expiry must be 'none', got %r" % row["expiry"])

    def is_registered(self, rule_num, relpath, symbol):
        relpath = relpath.replace(os.sep, "/")
        return (rule_num, relpath, symbol) in self._sites

    def covers(self, forwarder, function):
        """True when a rule-9 row for `forwarder` lists `function` in its pointed-to set."""
        return function in self.pointed.get(forwarder, set())
```
`run()`:
```diff
-def run(paths, register_path, enforce_fnptr, base_dir, report_only_paths=()):
+def run(paths, register_path, enforce_fnptr, base_dir, report_only_paths=(), scales=None):
     register = Register()
-    register.load(register_path)
+    register.load(register_path, scales)
@@ inside `if enforce_fnptr:` right after `pointed, unresolved = pointed_to_sets(...)`
+        scanned_files = {u[0].replace(os.sep, "/") for u in units}
+        for row in register.rows:
+            if register._rule_num(row["rule"]) != 9 or ":" not in row["site"]:
+                continue
+            rfile, rsym = (x.strip() for x in row["site"].strip("`").split(":", 1))
+            if rfile not in scanned_files or rsym not in register.pointed:
+                continue
+            if rsym in params:
+                want = set().union(*[pointed.get((rsym, k), set()) for k in params[rsym]])
+            elif rsym in fn_defs:
+                want = {rsym}
+            else:
+                continue
+            if register.pointed[rsym] != want:
+                register.errors.append((row["id"], "pointed-to set {%s} != computed {%s} "
+                                        "(REGISTER-SET)" % (", ".join(sorted(register.pointed[rsym])),
+                                                            ", ".join(sorted(want)))))
```
`check_rule9_values`, right after `callee = _call_owner(stripped, pos)`:
```python
        if callee and register.covers(callee, symbol):
            continue          # listed in the tier-A pointed-to set of the forwarder's row
```
`main()`: add `ap.add_argument("--config", default="tools/gates/config.yml", help="Exception scales (default: tools/gates/config.yml `exceptions`)")`; wrap `scales = load_scales(args.config)` and the `run(...)` call in the existing `try/except InputError` (rc 2), passing `scales=scales`; add `"register_errors": [{"id": rid, "reason": why} for rid, why in register.errors]` to the JSON; in text mode print `REGISTER-ERROR: %s: %s` per error; the exit condition becomes `if blocking or stale or register.errors:`. (Task 8 replaces `main()` wholesale; this step only has to make Task 7's tests pass.)

- [ ] **Step 4: Tests pass** — `python3 -m unittest tools/lint/test_power_of_10.py` → `Ran 59 tests` `OK`.

- [ ] **Step 5: Mutation** — change `benefit <= cost` to `benefit < cost`; `test_tier_b_benefit_equal_to_cost_fails` FAILS; restore. Change `rc == 0` (ancestor) to `rc != 0`; `test_expired_row_fails` FAILS; restore. `mutations/SD-S3-T7.txt`.

- [ ] **Step 6: The real register** — do Task 8 Step 4 (table + prose) and, from Task 8 Step 5, the filing of the former PD-7 site and the 13-row closure script (not G1..G5, which need the grace gone). Use the CI flag set of b8 (with `--report-only-sites "$GRACE"`) for the filing run. `gate_register` PASS; `python3 tools/gates/gate_p10.py` PASS.

- [ ] **Step 7: Commit** `feat(p10): two-tier register -- scores, adversarial report, owner, expiry, live site, pointed-to set; PD-7 unregistered (S3 T7)` (add `docs/power-of-10-deviations.md` and `docs/superpowers/state/findings.yml`).

### Task 8: The real register in two tiers; grace deleted; PD-7 and the graced sites become findings rows

**Findings:** SD-S3-T8 (closes TCI-T1; spec §4.4 "the grace list file and its linter flag are deleted")

**Files:**
- Modify: `docs/power-of-10-deviations.md` (whole table and the prose :13-34)
- Modify: `tools/lint/power_of_10.py` (`main()` replaced; `run()` loses `report_only_paths`; `render_text` loses the marker; `DEFAULT_REPORT_ONLY_PATHS` deleted; docstring :44-50)
- Modify: `tools/lint/test_power_of_10.py` — delete classes `Rollout` (:121-139) and `GraceVisibility` (:142-179); add `NoGrace`
- Modify: `tools/gates/gate_p10.py` (grace out, `--config` in), `tools/gates/tests/test_gate_p10.py` (new HEADER + `test_register_error_fails`)
- Modify: `.github/workflows/ci.yml` (the comment from Task 6), `README.md:40-45`
- Modify: `docs/superpowers/state/findings.yml` (G1..G5, the former PD-7 site, 13 rows closed)

**Scoring** (`tools/gates/config.yml` scales; tier A rows are category-allowed per spec §9 — their scores are recorded but not compared; only tier B must have benefit > cost):

| Row | Tier (SDK contract) | Cost = blast + verifiability + reach | Benefit = complexity + runtime | Verdict |
|---|---|---|---|---|
| PD-1 `pipeline.c:pipeline_task` | A — FreeRTOS `TaskFunction_t` (`xTaskCreateStaticPinnedToCore` arg 0) | timing 3 + 0 + stored_or_3plus 2 (kept in the TCB) = **5** | task_or_60_lines 3 + 0 = **3** | registered (tier A) |
| PD-2 `logger.c:logger_task` | A — same | data-integrity 3 + 0 + 2 = **5** | 3 + 0 = **3** | registered (tier A) |
| PD-3 `sup.c:sup_task` | A — same | supervision 3 + 0 + 2 = **5** | 3 + 0 = **3** | registered (tier A) |
| PD-4 `ui.c:ui_task` | A — same | ui 2 + 0 + 2 = **4** | 3 + 0 = **3** | registered (tier A) |
| PD-5 `export_serial.c:register_cmd` | A — IDF `esp_console_cmd_func_t` (`esp_console_cmd_t.func`) | data-integrity 3 (the §18 export path) + 0 (set of 13 printed) + stored_or_3plus 2 (≥ 3 sites, kept in the console table) = **5** | task_or_60_lines 3 (a hand-rolled REPL is > 60 lines) + 0 = **3** | registered (tier A); pointed-to set = the 13 callbacks |
| PD-6 `board.c:btn_isr` | A — IDF `gpio_isr_t` (`gpio_isr_handler_add` arg 1) | ui 2 + 0 + 2 = **4** | task_or_60_lines 3 (a polling task) + 0 (latency not measured) = **3** | registered (tier A) |
| PD-7 `cmd.h:cmd_emit_fn` | **B** — the project's own typedef, no SDK contract | data-integrity 3 (§18.1 streaming) + verifiability 1 (stored in `cw_t.emit` for one op, read in 3 functions) + stored_or_3plus 2 = **6** | task_or_60_lines 3 (alternative (b) rewrites 13 handlers) + runtime 0 (no measurement exists) = **3** | **3 ≤ 6: NOT registered** (even with verifiability 0, 3 ≤ 5) → findings row |
| PD-8 `link.c:link_task` | A — FreeRTOS `TaskFunction_t` | tooling 1 (peer stream) + 0 + 2 = **3** | 3 + 0 = **3** | registered (tier A) |
| PD-9 `display_epaper.c:pre_cb` | A — IDF `transaction_cb_t` (`spi_device_interface_config_t.pre_cb`) | ui 2 + 0 + 2 = **4** | helper 1 (a GPIO toggle helper) + 0 = **1** | registered (tier A) |

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Failing tests** — replace `Rollout` and `GraceVisibility` with:
```python
class NoGrace(unittest.TestCase):
    """S3 b9: the grace mechanism is deleted -- each old flag is an argparse error (rc 2)."""

    def test_every_grace_flag_is_rejected(self):
        for flag, val in (("--report-only-sites", FIX + "/lg6_recursion.c:walk:RULE-1"),
                          ("--report-only-paths", FIX + "/"),
                          ("--report-only-rules", "RULE-1")):
            with self.subTest(flag=flag):
                with contextlib.redirect_stderr(io.StringIO()) as err:
                    with self.assertRaises(SystemExit) as cm:
                        p10.main(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                                  "--fail-on-violation", flag, val])
                self.assertEqual(2, cm.exception.code)
                self.assertIn("unrecognized arguments: " + flag, err.getvalue())

    def test_ungraced_blocking_finding_fails_the_build(self):
        rc, out = run(["--paths", FIX + "/lg6_recursion.c", "--register", NONE,
                       "--fail-on-violation"])
        self.assertIn("RULE-1: function 'walk' calls itself (rule 1: no recursion)", out)
        self.assertNotIn("[report-only]", out)
        self.assertEqual(1, rc)
```
and in `tools/gates/tests/test_gate_p10.py` replace `HEADER` with the twelve-column header (as in `test_power_of_10.HEADER`) and add before `test_missing_findings_register_is_input_fail`:
```python
    def test_register_error_fails(self):
        self.write("components/core/a.c", CLEAN_C)
        self.write("docs/power-of-10-deviations.md", HEADER +
                   "| PD-X | 9 | components/core/gone.c:f | A (x) | {f} | a | b | "
                   "1 = blast:tooling 1 + verifiability 0 + reach:transient 0 | "
                   "1 = complexity:helper 1 + runtime 0 | n/a | o | none |\n")
        rc, out = self.gate()
        self.assertIn("FAIL p10.register.PD-X: site file components/core/gone.c does not exist", out)
        self.assertEqual(1, rc)
```
Run → `NoGrace` fails (flags still accepted).

- [ ] **Step 2: Linter** — replace `main()` (all of it) with:
```python
def main(argv=None):
    ap = argparse.ArgumentParser(description="Power of 10 project-specific static linter "
                                             "(prints findings; --fail-on-violation gates).")
    ap.add_argument("--paths", nargs="+", default=DEFAULT_PATHS,
                     help="Roots to scan (default: %s)" % " ".join(DEFAULT_PATHS))
    ap.add_argument("--register", default="docs/power-of-10-deviations.md",
                     help="Deviation register path (default: docs/power-of-10-deviations.md)")
    ap.add_argument("--config", default="tools/gates/config.yml",
                     help="Exception scales (default: tools/gates/config.yml `exceptions`)")
    ap.add_argument("--enforce-fnptr", action="store_true",
                     help="Enable RULE-9 (no unregistered function pointer); CI passes it.")
    ap.add_argument("--fail-on-violation", action="store_true",
                     help="Exit 1 on any untracked RULE-1/2/3/4/4-COMPOUND/5/8/9 finding, any "
                          "TRACKED-STALE row, or any REGISTER-ERROR. There is no grace.")
    ap.add_argument("--json", action="store_true", help="Emit JSON instead of text.")
    ap.add_argument("--findings", default=None,
                     help="Findings register (YAML). A violation whose file:symbol:RULE has an "
                          "open row (repro `p10-site:...`) is [tracked] and does not block; an "
                          "open row whose scanned site no longer violates is TRACKED-STALE (blocks).")
    ap.add_argument("--emit-findings", action="store_true",
                     help="Print only YAML findings rows (S3-P10-<n>, numbered after the highest "
                          "in --findings) for every untracked blocking site, then exit 0.")
    ap.add_argument("--append-to", default=None, metavar="FINDINGS_YML",
                     help="With --emit-findings: append the rows to this register instead of "
                          "printing them, and re-parse it.")
    args = ap.parse_args(argv)

    base_dir = os.getcwd()
    try:
        rows = load_findings_rows(args.findings) if args.findings else []
        tracked = tracked_sites(rows)
        scales = load_scales(args.config)
        findings, summary, register, rule8a, scanned = run(args.paths, args.register,
                                                            args.enforce_fnptr, base_dir, scales)
    except InputError as exc:
        print("INPUT-ERROR: %s" % exc)
        return 2

    for f in findings:
        if site_key(f) in tracked:
            f["tracked"] = tracked[site_key(f)]
    hit = {site_key(f) for f in findings}
    scanned_set = set(scanned)
    stale = sorted((fid, key) for key, fid in tracked.items()
                   if key[0] in scanned_set and key not in hit)
    blocking_rules = ("RULE-1", "RULE-2", "RULE-3", "RULE-4", "RULE-4-COMPOUND", "RULE-5",
                      "RULE-8", "RULE-9")
    blocking = [f for f in findings if f["rule"] in blocking_rules and not f.get("tracked")]

    if args.emit_findings:
        rc, sha = _git(["rev-parse", "--short", "HEAD"])
        if rc != 0 or not sha:
            print("INPUT-ERROR: git rev-parse HEAD failed")
            return 2
        text = emit_findings_yaml(blocking, next_s3_number(rows), sha)
        if args.append_to and text:
            try:
                append_rows(args.append_to, text)
            except InputError as exc:
                print("INPUT-ERROR: %s" % exc)
                return 2
            print("appended %d row(s) to %s" % (text.count("- id: "), args.append_to))
        elif text:
            print(text)
        return 0

    if args.json:
        print(json.dumps({
            "findings": findings,
            "tracked_stale": [{"id": fid, "site": ":".join(key)} for fid, key in stale],
            "register_errors": [{"id": rid, "reason": why} for rid, why in register.errors],
            "summary": summary,
            "register_rows": len(register.rows),
            "enforce_fnptr": args.enforce_fnptr,
            "rule8a": rule8a,
        }, indent=2, sort_keys=True))
    else:
        print(render_text(findings, summary, args.enforce_fnptr, rule8a))
        print("\nregister: %d row(s) loaded from %s" % (len(register.rows), args.register))
        print("mode: %s" % ("fail-on-violation" if args.fail_on_violation else
                            "report (exit 0 unless --fail-on-violation)"))
        for fid, key in stale:
            print("TRACKED-STALE: %s %s%s matches no finding -- advance the row (fixed-unpinned/"
                  "pinned) or restore the site" % (fid, SITE_PREFIX, ":".join(key)))
        for rid, why in register.errors:
            print("REGISTER-ERROR: %s: %s" % (rid, why))

    if args.fail_on_violation and (blocking or stale or register.errors):
        if not args.json:
            print("\nBLOCKING: %d violation(s), %d stale tracked row(s), %d register error(s)"
                  % (len(blocking), len(stale), len(register.errors)))
        return 1
    return 0
```
`run()`: signature `def run(paths, register_path, enforce_fnptr, base_dir, scales=None):`; delete `n_before = …`, `ro = …`, the `if ro:` block and the `report_only_paths` marking after the call graph. `render_text(findings, summary, enforce_fnptr, rule8a)`: `mark = ""` (no `[report-only]`), summary title `-- power_of_10.py summary --`. Delete `DEFAULT_REPORT_ONLY_PATHS`. Docstring :44-50 → "`--fail-on-violation` exits 1 on any untracked finding, any TRACKED-STALE row or any REGISTER-ERROR; there is no grace (spec §4.4, D13)."

- [ ] **Step 3: Gate** — apply to `tools/gates/gate_p10.py`:
```diff
23a24
> CONFIG = os.path.join(CODE_ROOT, "tools", "gates", "config.yml")
25,33d25
< # ROLLOUT GRACE carried over from ci.yml:162-177 (ruling R12, issue #131); deleted with the
< # linter flag in S3 Task 8, when the five sites become findings rows S3-P10-G1..G5.
< ROLLOUT_GRACE = ",".join([
<     "components/app/sys/lt_nvs.c:errlog_add:RULE-5",
<     "components/core/config/cfg.c:cfg_defaults:RULE-5",
<     "components/core/config/cfg_json.c:cfg_to_json:RULE-4",
<     "components/drivers/export_serial/export_serial.c:fmt_from_str:RULE-5",
<     "components/drivers/export_serial/export_serial.c:tally_cb:RULE-5",
< ])
41c33
<             "--report-only-sites", ROLLOUT_GRACE, "--findings", FINDINGS]
---
>             "--config", CONFIG, "--findings", FINDINGS]
81,82d72
<         if f.get("report_only"):
<             continue                      # the five graced sites, until Task 8
```
(i.e. delete `ROLLOUT_GRACE` and `report_only`, add `CONFIG = os.path.join(CODE_ROOT, "tools", "gates", "config.yml")` and `"--config", CONFIG`). In `.github/workflows/ci.yml` change the comment above the gate step to `# Power of 10 via the gate (strict-delivery S3): every tree, every rule blocking, open findings rows tracked; no grace.` and in `README.md:40-45` replace the grace paragraph and command with "`python3 tools/gates/gate_p10.py` — the exact CI check; it exits 1 on any violation that has no open row in `docs/superpowers/state/findings.yml`."

- [ ] **Step 4: The register (executed in Task 7 Step 6; re-check only)** — replace the table of `docs/power-of-10-deviations.md` (:36-46) with the twelve-column header and these rows (keep each row's existing "compliant alternative" / "why rejected" prose verbatim from :38-46, abbreviated here only for the plan's width):
```
| id | rule | site | tier | pointed-to set | compliant alternative | why rejected | cost | benefit | adversarial report | owner | expiry |
|----|------|------|------|----------------|-----------------------|--------------|------|---------|--------------------|-------|--------|
| PD-1 | 9 | components/app/pipeline/pipeline.c:pipeline_task | A (FreeRTOS `TaskFunction_t`, `xTaskCreateStaticPinnedToCore` arg 0) | {pipeline_task} | <PD-1 alternative, verbatim> | <PD-1 why rejected, verbatim> | 5 = blast:timing 3 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | app layer (pipeline) | none |
| PD-2 | 9 | components/app/logger/logger.c:logger_task | A (FreeRTOS `TaskFunction_t`, `xTaskCreateStaticPinnedToCore` arg 0) | {logger_task} | … | … | 5 = blast:data-integrity 3 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | app layer (logger) | none |
| PD-3 | 9 | components/app/supervisor/sup.c:sup_task | A (FreeRTOS `TaskFunction_t`, `xTaskCreateStaticPinnedToCore` arg 0) | {sup_task} | … | … | 5 = blast:supervision 3 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | app layer (supervisor) | none |
| PD-4 | 9 | components/app/ui/ui.c:ui_task | A (FreeRTOS `TaskFunction_t`, `xTaskCreateStaticPinnedToCore` arg 0) | {ui_task} | … | … | 4 = blast:ui 2 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | app layer (ui) | none |
| PD-5 | 9 | components/drivers/export_serial/export_serial.c:register_cmd | A (IDF `esp_console_cmd_func_t`, `esp_console_cmd_t.func`) | {cmd_close_c, cmd_config_c, cmd_coredump_c, cmd_dbg, cmd_delete_c, cmd_diag_c, cmd_errlog_c, cmd_list_c, cmd_open_c, cmd_ota, cmd_read_c, cmd_status_c, cmd_tracks_c} | … | … | 5 = blast:data-integrity 3 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | drivers layer (export_serial) | none |
| PD-6 | 9 | components/drivers/board_devkit_v1/board.c:btn_isr | A (IDF `gpio_isr_t`, `gpio_isr_handler_add` arg 1) | {btn_isr} | … | … | 4 = blast:ui 2 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | drivers layer (board) | none |
| PD-8 | 9 | components/app/link/link.c:link_task | A (FreeRTOS `TaskFunction_t`, `xTaskCreateStaticPinnedToCore` arg 0) | {link_task} | … | … | 3 = blast:tooling 1 + verifiability 0 + reach:stored_or_3plus 2 | 3 = complexity:task_or_60_lines 3 + runtime 0 | n/a (tier A) | app layer (link) | none |
| PD-9 | 9 | components/drivers/display_epaper_ssd1680/display_epaper.c:pre_cb | A (IDF `transaction_cb_t`, `spi_device_interface_config_t.pre_cb`) | {pre_cb} | … | … | 4 = blast:ui 2 + verifiability 0 + reach:stored_or_3plus 2 | 1 = complexity:helper 1 + runtime 0 | n/a (tier A) | drivers layer (display) | none |
```
The `…` cells are the existing text (a `|` inside it must stay escaped as `\|`; there is none today). PD-7 is removed. Rewrite the prose :13-34 to: the two tiers and the scales (spec §9), "PD-1..6, 8, 9 are tier A (SDK contract named in the tier cell)", "PD-7 (`cmd_emit_fn`) scored benefit 3 ≤ cost 6 as tier B and is not registered; it is findings row `<id>` owned by the S5 conformance sweep", and the rule-9 paragraph replaced by the spellings Task 5 implements (fixes audit V-3's contradiction). This prototype register was run against the tree: 0 `REGISTER-ERROR`, the 13 console callbacks covered by PD-5's set.

- [ ] **Step 5: Findings rows** — append the five former grace sites exactly (replace `<sha>` with `git rev-parse --short HEAD`, quoted):
```yaml
- id: S3-P10-G1
  source: "ci.yml rollout grace (ruling R12, issue #131), removed by strict-delivery S3 b9"
  severity: MEDIUM
  class: "components/app/sys/lt_nvs.c:376: RULE-5: function 'errlog_add' has 0 assertion(s) in 21 code lines (>20 lines requires >=2)"
  siblings: []
  cites: [{file: components/app/sys/lt_nvs.c, lines: "376", sha: "<sha>"}]
  repro: "p10-site:components/app/sys/lt_nvs.c:errlog_add:RULE-5"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<sha>", by: agent}]
- id: S3-P10-G2
  source: "ci.yml rollout grace (ruling R12, issue #131), removed by strict-delivery S3 b9"
  severity: MEDIUM
  class: "components/core/config/cfg.c:7: RULE-5: function 'cfg_defaults' has 1 assertion(s) in 41 code lines (>20 lines requires >=2)"
  siblings: []
  cites: [{file: components/core/config/cfg.c, lines: "7", sha: "<sha>"}]
  repro: "p10-site:components/core/config/cfg.c:cfg_defaults:RULE-5"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<sha>", by: agent}]
- id: S3-P10-G3
  source: "ci.yml rollout grace (ruling R12, issue #131), removed by strict-delivery S3 b9"
  severity: MEDIUM
  class: "components/core/config/cfg_json.c:184: RULE-4: function 'cfg_to_json' is 112 code lines (> 60)"
  siblings: []
  cites: [{file: components/core/config/cfg_json.c, lines: "184", sha: "<sha>"}]
  repro: "p10-site:components/core/config/cfg_json.c:cfg_to_json:RULE-4"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<sha>", by: agent}]
- id: S3-P10-G4
  source: "ci.yml rollout grace (ruling R12, issue #131), removed by strict-delivery S3 b9"
  severity: MEDIUM
  class: "components/drivers/export_serial/export_serial.c:418: RULE-5: function 'fmt_from_str' has 0 assertion(s) in 21 code lines (>20 lines requires >=2)"
  siblings: []
  cites: [{file: components/drivers/export_serial/export_serial.c, lines: "418", sha: "<sha>"}]
  repro: "p10-site:components/drivers/export_serial/export_serial.c:fmt_from_str:RULE-5"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<sha>", by: agent}]
- id: S3-P10-G5
  source: "ci.yml rollout grace (ruling R12, issue #131), removed by strict-delivery S3 b9"
  severity: MEDIUM
  class: "components/drivers/export_serial/export_serial.c:717: RULE-5: function 'tally_cb' has 0 assertion(s) in 27 code lines (>20 lines requires >=2)"
  siblings: []
  cites: [{file: components/drivers/export_serial/export_serial.c, lines: "717", sha: "<sha>"}]
  repro: "p10-site:components/drivers/export_serial/export_serial.c:tally_cb:RULE-5"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "<sha>", by: agent}]
```
Verify each `class` against `python3 tools/lint/power_of_10.py --paths <file> | grep <symbol>` before writing it (they were read at `24aa0c7`; the files are untouched by this plan). Then (this part executed in Task 7 Step 6) the filing command: expected `appended 1 row(s)` — `p10-site:components/app/include/app/cmd.h:cmd_emit_fn:RULE-9`; set that row's `siblings: ["former deviation PD-7: tier B benefit 3 <= cost 6 (S3 b9 scoring)"]` and put its id into the register prose. Then close the 13 console-callback rows PD-5 now covers (they print as `TRACKED-STALE`):
```bash
SHA=$(git rev-parse --short HEAD) python3 - <<'PY'
import os, re, yaml
P = "docs/superpowers/state/findings.yml"
text = open(P).read()
rows = yaml.safe_load(text)
want = {r["id"] for r in rows if r["state"] == "open" and re.match(
    r"^p10-site:components/drivers/export_serial/export_serial\.c:cmd_[a-z_]+:RULE-9$", r.get("repro") or "")}
assert len(want) == 13, sorted(want)
sha = os.environ["SHA"]
out, cur = [], None
for line in text.splitlines():
    m = re.match(r"^- id: (\S+)$", line)
    if m:
        cur = m.group(1)
    if cur in want and line == "  state: open":
        line = "  state: closed"
    if cur in want and line == "  evidence: []":
        line = '  evidence: ["docs/power-of-10-deviations.md"]'
    if cur in want and line.startswith("  history: ["):
        line = line[:-1] + ', {state: closed, sha: "%s", by: agent}]' % sha
    out.append(line)
open(P, "w").write("\n".join(out) + "\n")
print("closed", len(want))
PY
python3 tools/gates/gate_register.py        # PASS only
python3 tools/gates/gate_p10.py             # PASS p10.clean + PASS p10.tracked.<id> lines
```

- [ ] **Step 6: Tests pass** — `python3 -m unittest tools/lint/test_power_of_10.py tools/gates/tests/test_gate_p10.py` → `Ran 63 tests` `OK` (lint 54: 59 − 7 grace tests + 2; gate 9); `python3 -m unittest discover -s tools/gates/tests` OK; `python3 tools/gates/run_all.py` PASS only.

- [ ] **Step 7: Mutation** — re-add `ap.add_argument("--report-only-sites", default="")`; `test_every_grace_flag_is_rejected` FAILS; restore. `mutations/SD-S3-T8.txt`.

- [ ] **Step 8: Commit and push**
```bash
git add -A tools .github README.md docs/power-of-10-deviations.md docs/superpowers/state
git commit -m "feat(p10): two-tier deviation register, grace list deleted; PD-7 and the 5 graced sites are findings rows (S3 T8)"
git push -u origin block/strict-delivery-b9
```

### Task 9: Docs say what the linter does; full-reach verification

**Findings:** SD-S3-T9 (audit rows R9, R10, R12 at `docs/reviews/2026-10-16-audit-post-fix-wave.md:319-322`; P8 :353)

**Files:**
- Modify: `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` §1 (:11-15), §3 rule 2 (:28), §4 (:54), §5 linter bullet and Rollout (:58-82)
- Modify: `docs/superpowers/specs/2026-09-14-lap-timer-design.md:2641` (tool table row)
- Modify: `docs/power-of-10-checklist.md:5` (the CI invocation)

- [ ] **Touched-file rule (ruling 3)** — before this task's commit run `python3 tools/gates/gate_p10.py --changed $(git diff --name-only HEAD) $(git diff --name-only --cached)`; every `FAIL p10.touched.*` is fixed in this task (never a tracking row; a behaviour change gets its own test). Expected here: none (the task touches no scanned C file).

- [ ] **Step 1: Find every stale statement** — `grep -rn 'report-only\|GRACE\|report-only-sites\|stored-value check\|direct recursion\|devcontroller.*report' README.md docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md docs/superpowers/specs/2026-09-14-lap-timer-design.md docs/power-of-10-checklist.md docs/power-of-10-deviations.md`. Each hit is either rewritten below or ledgered as historical (a dated section that describes the past is left alone).
- [ ] **Step 2: Rewrite** — compliance design: add a dated note at the top of §5, "Amended 2026-10-17 (strict-delivery S3): the linter's reach is spec 2026-10-17 §9 — rules 1 (call graph, goto/longjmp), 2 (loop bounds), 3 (census incl. wrappers), 5 (tautologies, conditional context), 9 (typedef'd params/fields, every function value, pointed-to set) on every C tree; the exception model is the two-tier register; the rollout grace is deleted; existing violations are findings rows." Replace the Rollout paragraph (:76-82) with that rule; §1 scope: "every C tree (strict-delivery spec §9 supersedes the `tools/replay/**`, `test/**` exemption); host tests may declare a per-file rule-3 allowance". lap-timer spec :2641: `| tools/gates/gate_p10.py (wraps tools/lint/power_of_10.py) | Python 3 | Power of 10 rules 1/2/3/4/4-compound/5/8/9 on every C tree, blocking | python3 tools/gates/gate_p10.py |`. Checklist :5: the same command.
- [ ] **Step 3: Full verification** — at the branch head:
```bash
python3 tools/gates/run_all.py                                   # PASS only
python3 tools/gates/gate_p10.py --json | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ok'])"   # True
python3 -m unittest discover -s tools/gates/tests && python3 -m unittest tools/lint/test_power_of_10.py
for p in .superpowers/reviews/2026-10-15/tools-probe-lint/p[12456789]*.c; do
  python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation --paths "$p" --register tools/lint/fixtures/register_none.md >/dev/null; echo "$p rc=$?"; done   # every rc=1
```
(`p3*` are rule-4 probes already caught; `p8_ifdef_brace.c` is a brace-desync probe outside this plan's rules and is ledgered as a residual of TCI-T3 owned by the S5 sweep if its rc is 0.)
- [ ] **Step 4: Commit** `docs(p10): compliance design, product spec tool row, checklist and README describe the full-reach gate (S3 T9)`.

### b9 block end (CHECKPOINT) — last block of S3

Same as b7's block end with `b9`, tag `strict-delivery-b9`; S4 follows, so `next_task: "S4-T1"` and the resume prompt `block 10, sub-plan S4, task 1`. Mark SD-S3-T7..T9 per the S2 rules; `gate_checkpoint --plan strict-delivery` PASS; stop for `/clear`.

---

## Self-review (writing-plans checklist, done at authoring)

1. **Spec coverage.** §9 Reach: rule 1 call graph + goto/longjmp (T1), rule 2 (T2), rule 3 census incl. `heap_caps_*`, `*_prefer`, `memalign`, `alloca`, `strdup`/`strndup`/`asprintf`, wrappers, listed pre-init sites (T3), rule 4 unchanged (already statement-counted, LG-5 FIXED), rule 5 tautologies + `#if 0`/`#ifdef NEVER` (T4), rules 6/7/8/10 "outside core too" — rule 8 runs on every tree from T6; rules 6/7/10 are compiler/clang-tidy matters outside this linter (not changed here; ledgered for S5), rule 9 declarators, sinks, struct fields, typedef'd params and fields, pointed-to set (T5), probe fixtures per rule with negatives that FAIL (T1–T5), every tree (T6). §9 Exceptions: all columns + pointed-to set (T7), tier A/B, benefit > cost with equality refused (T7), expired rows fail (T7), adversarial report path (T7; tier B only), grace deleted (T8). D13 "implementer runs it before reporting, reviewer re-runs, CI blocks": `gate_p10` in `run_all`, the commit hook and CI (T6). Not covered here: "every audit re-argues every row" (S4 `audit.js`).
2. **Placeholder scan.** `<sha>` / `<a>..<b>` / `<n>` are values produced at execution by the commands shown; the `…` cells in Task 8 Step 4 are "copy the existing cell verbatim".
3. **Type consistency.** `run()` returns 5 values from T1 on; `check_rule5_assertions(..., ctx=None, assert_re=None)` T4/T6; `run(..., scales=None)` T7/T8; `Register.covers` T7 used in `check_rule9_values` T7; gate line ids identical in T6 tests and T8 tests.
4. **Review Focus.** Each of the five lines has its pinning test in its owning task.
5. **Owner ruling 3.** Global Constraints states it; every task carries the touched-file step; `gate_p10` emits `FAIL p10.touched.*` / `PASS p10.tracked.<id>` with two fixture tests (Task 6); no task touches a scanned C file, so the allowance comment is deferred to the sweep (Task 6 Step 5).
