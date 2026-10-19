status: DONE

# b4 scoped re-review of the fix wave (B4-FIX-1), range 26148d6..7e5684c

All executions ran in the scratch clone `scratchpad/rereview/repo`, checked out at 7e5684c. At the end `git status --short` there printed nothing. The live checkout was not touched, apart from writing this file. The scripts are in `scratchpad/rereview/` (probe.py, cfgmut.py, mut.py, mut2.py, d3.py, pins.py, pintry.sh, pin4.sh, stale.sh).

## Commits
- ae4108c fix(gates): refuse fork dispatches; pin F2-F5; config-check requires DEC-0012
- 7e5684c docs(plan): S2 pin steps to ruling 3; Task 1-3 blocks re-synced to the tip

Both commits carry the trailer `Co-Authored-By: Claude Opus 5.5`. Item H holds: `git diff --name-only 26148d6..7e5684c | grep -E 'config.yml|spec|settings|S1|plans/2026-10-17-strict-delivery.md'` returned rc 1, so none of those files changed. B4-FIX-1 is still `planned`.

## Gates run (clone at 7e5684c)
- `env -u CLAUDE_PROJECT_DIR PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` gave `Ran 361 tests in 121.977s OK`, rc 0, and 0 lines matching "warn".
- The same command with `tools/.venv/bin/python3` (3.12.14) gave `Ran 361 tests in 131.182s OK`, rc 0, and 0 "warn" lines.
- `python3 tools/gates/run_all.py` printed 29 PASS lines and no other lines, rc 0.
- `python3 tools/gates/gate_dispatch.py --config-check` printed 14 PASS lines, including `dispatch.config.fable_roles` and `dispatch.config.model_rank`, rc 0.
- `git diff --check 26148d6..7e5684c` printed nothing, rc 0.
- I extracted the ci.yml stale-comment step with yaml and ran it under bash: rc 0.
- The 361 - 348 = 13 new tests match the ledger line ("test_gate_dispatch 10, test_hooks 3"). I counted the added `def test_` lines in the diff: 10 + 3.

## Per-finding verdicts
- **F1 / ruling 4 (fork refusal): ADDRESSED.**
  - Direct check: probe.py ran the hook as a subprocess with an Agent event, the fixture repo and an otherwise good prompt. `fork`, `FORK`, ` Fork `, `fork\n`, `\tfork` and `\xa0fork\xa0` each gave rc 2 with `FAIL dispatch.fork: a fork runs on the session model with the session context; dispatch a general-purpose agent with a brief`.
  - Contradiction: `general-purpose`, `Explore`, a missing value (None), `""`, `forked`, `-fork` and `fork-x` each gave `0 ''`.
  - Argument injection: `--prompts-dir=/nonexist` and `--prompt` passed as the value gave `0 ''`. The `--subagent-type=<v>` form keeps each one a single value.
  - Non-string values: `5`, `True`, `[]`, `{}` and `['fork']` each gave rc 2 with `FAIL dispatch.fork: tool_input.subagent_type ... is not a string`.
  - A fork with a garbage prompt gives `FAIL dispatch.fork` as the first line, then `FAIL dispatch.grammar`. A fork with no tool model gives rc 2 with dispatch.fork first.
  - Design judgement: the case-insensitive, blank-stripped match and the refusal of non-string values both fail closed. The worst they cost is refusing a value that the Agent tool would not have run as a fork anyway. I accept both.
- **F2 (implementer on a pinned row): ADDRESSED.** `test_implementer_on_a_pinned_or_merged_row_fails` drives `main()` on a fixture with pinned, merged and in-progress rows.
  - Mutant widening the set to `+"pinned"` (step 10): FAILED (failures=1).
  - Extra mutant of my own widening it to `+"merged"` (mut2 step 10b): FAILED (failures=1).
- **F3 (unregistered claim): ADDRESSED.** `test_unregistered_claim_fails`. Mutant `reason = None if c is None` (step 11): FAILED (failures=1).
- **F4 (budget with trailing text): ADDRESSED.** `test_budget_with_trailing_text_fails`. Mutant dropping `$` (step 12): FAILED (failures=1).
- **F5 (vacuous non-ASCII test): ADDRESSED.** The test now asserts `gate_decisions.REF.findall(...) == []` on fullwidth and Arabic-Indic digits, and it keeps its `main()` run.
  - REF set to `DEC-\d{4}(?!\d)` (step 13): FAILED (1).
  - REF set to `DEC-\d{4}` (step 14): FAILED (1).
  - Restored: OK.
- **F6 (plan pin scripts): ADDRESSED.**
  - pins.py found 9 pin blocks (Tasks 1-9). Each has `pin.test` matching `registers.PIN_TEST` `^test_[A-Za-z0-9_]+(::test_[A-Za-z0-9_]+)?$`, `evidence = ["docs/superpowers/state/strict-delivery/reviews/S2-T<n>.md"]`, both added history entries `by: controller`, `git add` before `gate_register`, the ledger `- S2-T<n> DONE` line inside the block, and the commit subject `chore(state): SD-S2-T<n> pinned; ledger S2-T<n>`.
  - The T1-T3 pin names equal findings.yml: `test_gate_dispatch::test_fable_on_implementer_fails`, `test_hooks::test_missing_tool_model_blocks`, `test_gate_dispatch::test_config_check_fails_when_implementer_minimum_is_fable`.
  - pintry.sh ran every block in the clone, with the commit replaced by echo and dummy transcript and report files:
    - T1-T3: `AssertionError: pinned`, so the rows are not touched.
    - T4-T9: `gate_register` rc 0 each.
    - pin4.sh: after the T4 block the row is `pinned` with the PIN_TEST form and the reviews path. Contradiction: with the evidence file unstaged, `FAIL register.evidence.SD-S2-T4: ... neither a commit sha nor a file tracked` and `... pinned needs the reviewer's confirming report path`, rc 1.
  - `grep -c 'by the reviewer once\|reviewer copies that file'` on the plan gives 0. Plan line 28 now reads "copied there by the controller after the task review's clean verdict (b4 ruling 3)".
- **V1 (Task 1/2 blocks stale; D3): ADDRESSED.**
  - My own d3.py checks every fenced block between `### Task 1:` and `### b4 checkpoint` as a byte substring of any tracked file other than the plan. At the tip: `blocks=35 miss=0`; 12 are bash procedure blocks and every other block is a SUBSTRING.
  - The whole-file blocks plan:344 (23622 B) and plan:821 (15018 B) equal the sizes of test_gate_dispatch.py and gate_dispatch.py (`wc -c` 23622 and 15018).
  - Contradiction: the same script at 26148d6 gives `blocks=37 miss=3` (plan:344, :646, :1118), exactly the verifier's three.
  - This matches the implementer's D3 output line for line.
- **DEC-0012 rule 1 (fable_roles == fable minimums): ADDRESSED.** cfgmut.py edited the clone's config.yml:
  - `+implementer`: rc 1, `FAIL dispatch.config.fable_roles: fable_roles ['exception_reviewer', 'final_verifier', 'implementer'] != ... (fable minimums ['exception_reviewer', 'final_verifier'])`.
  - `-exception_reviewer`: rc 1.
  - Duplicate or reordered lists: rc 0, as set equality should give.
  - Mutant `elif False:` (step 15): FAILED (3).
- **DEC-0012 rule 2 (model_rank strictly ordered): ADDRESSED.**
  - These configs give rc 1 with `FAIL dispatch.config.model_rank`: opus 5 / fable 4, a tie, a float, a bool, a string, and haiku missing.
  - Gapped integers (-7/0/30/400): rc 0.
  - Mutants: step 16 (`<=`) FAILED (1); step 17 (bool exclusion removed) FAILED (1); step 18b (whole condition `True or`) FAILED (7).

## Mutation transcript re-run
I re-ran all 19 steps of `.superpowers/sdd/strict-delivery/review/S2-b4-fixwave/mutation-B4-FIX-1.txt` with mut.py.

| Steps | Result |
|---|---|
| 1 / 2 / 3 | OK / FAILED (failures=1, test_fork_dispatch_blocks; both files equal to 26148d6, `git diff 26148d6` empty) / OK |
| 4 | 5 failures |
| 5 | 2 failures |
| 6 | 1 failure |
| 7 | 9 failures |
| 8 | 1 failure |
| 9 | 1 failure |
| 10-17 | 1, 1, 1, 1, 1, 3, 1, 1 failures |
| 19 | both full suites OK (gates above) |

- Every count and every test name matches the transcript.
- Step 18: my first mutant (`... or True` on the order term only) gave 3 failures. The int/bool subTests still fail closed under that mutant, so it is not the transcript's edit. Making the whole condition true (step 18b) gives 7 failures, as the transcript states. Killed either way.
- 0 survivors. The clone is clean afterwards.

## New breakage in the fix diff
None found.

Observations for the controller (not findings; neither has an executable check that shows a defect in this change):
- `subagent_type` `"fork\x00"` gives rc 0. Whether the Agent tool would treat it as a fork cannot be executed here, because dispatching is forbidden. A model emitting a NUL byte in a tool argument is not a practical path.
- `--config-check` passes when `implementer: fable` and `fable_roles` gains `implementer` together. It also passes when `model_rank` carries an extra model ranked above fable (`gpt: 9`). DEC-0012's answer requires only set equality and the four-model order, and the implementation matches it exactly. Pinning the fable roster itself (spec D18) would widen the class, which is the owner's call.

## Claims (claim | direct | contradiction | output | verdict)
- **Fork is refused through the hook:** direct probe.py, values `fork`/`FORK`/` Fork `. Contradiction: mut.py step 2 (fix reverted). Output: rc 2 `FAIL dispatch.fork:`; reverted, FAILED (1). PASS.
- **Other or missing types are unchanged:** direct probe.py, `general-purpose`/`Explore`/None/`""`. Contradiction: steps 6 and 7. Output: `0 ''`; killed 1 and 9. PASS.
- **Non-string type is refused:** direct probe.py with `5`/`[]`/`{}`. Contradiction: step 9. Output: rc 2 `... is not a string`; killed 1. PASS.
- **dispatch.fork is first even on a bad grammar:** direct probe.py `garbage+fork`. Contradiction: step 7. Output: first line FAIL dispatch.fork; killed 9. PASS.
- **F2, F3 and F4 pinned:** direct, the new tests in the suite. Contradiction: steps 10/10b/11/12. Output: each FAILED (1). PASS.
- **F5 no longer vacuous:** direct, the suite. Contradiction: steps 13 and 14. Output: FAILED (1) each. PASS.
- **DEC-0012 rule 1:** direct cfgmut `+implementer`. Contradiction: step 15. Output: rc 1 with both sets named; killed 3. PASS.
- **DEC-0012 rule 2:** direct, cfgmut's six bad configs. Contradiction: steps 16/17/18b. Output: rc 1 each; killed 1/1/7. PASS.
- **run_all gives 29 PASS:** direct, run_all. Contradiction: base 28 (b4-final-review gates). Output: 29 PASS rc 0. PASS.
- **D3 35/0 at the tip:** direct d3.py at 7e5684c. Contradiction: d3.py at 26148d6. Output: 35/0 and 37/3. PASS.
- **Plan pin blocks give register-valid rows:** direct pintry.sh T4-T9. Contradiction: pin4.sh with the evidence unstaged. Output: rc 0 x6; rc 1 with 2 FAIL lines. PASS.
- **No reviewer-as-actor text:** direct `grep -c` gives 0. Contradiction: line 28 read directly. Output: line 28 names the controller. PASS.
- **README and ledger updated:** direct, the diff read (fork paragraph, DEC-0012 rules, "14 lines ... 29 PASS"). Contradiction: test-count delta 13 = 10 + 3. Output: consistent. PASS.
- **ci stale grep unchanged:** direct stale.sh. Contradiction: none beyond the step's own allow-list check. Output: rc 0. PASS.

## Findings raised
None.

## Unrelated findings
None.

## Denials
None. One of my own Bash calls was refused by the live hook (`FAIL hook.bypass: -n after a gated git verb`) because its text held a commit command plus `grep -n`. I moved the script into a file, as the brief advises. This was not a permission denial of the task.

## Background jobs
None. Both suites ran as foreground `&` children with `wait`. `pgrep -fl "unittest|mut.py|d3.py"` at the end gave rc 1 (none running).

## Budget
- Tokens: about 105k of 120k. The 80 % mark was passed during the final checks (the pin-block runs). I stopped at that step boundary and wrote this report; nothing is left half-done.
- Minutes: about 35 of 90.
