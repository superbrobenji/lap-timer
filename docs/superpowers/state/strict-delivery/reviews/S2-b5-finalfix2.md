status: DONE_WITH_FINDINGS

verdict: spec: ✅ quality: ❌ claims-executed: ✅
(quality ❌ because of R1-2, a test gap in this wave's own code. R1-1 is a class question for the owner, not ruled here.)

## Commits reviewed
- c0316eb fix(gates): B5-FIX-5, B5-FIX-6 -- in-flight reads HEAD, index and working tree; --all FAILs a non-directory .superpowers/sdd (range ea19922..c0316eb)
- Head 4be15f8: `git diff --stat c0316eb..4be15f8 -- tools/gates` printed nothing, so tools/gates is byte-equal. The only plan change in that range is umbrella `2026-10-17-strict-delivery.md` (1 line, DEC-0030), outside this wave.
- Checkout read-only: `git status --short` was empty at start and after every checkout gate run. All writes went to <scratchpad>/r8-rr1/ (clones `repo` at c0316eb and `old` at ea19922, both made with --no-hardlinks; fixtures under r8-rr1/fx/; every script guarded by a pwd -P / show-toplevel prefix check).

## Six siblings
| sibling | repro via main(), c0316eb | same at ea19922 | covering test | state |
|---|---|---|---|---|
| ledger deleted in the working tree (repro a) | inflight.sh case 2, status ` D`: in flight (plan.zz.*) | PASS plan.in_flight (fail-open) | LedgerCopies.test_ledger_deleted_in_the_working_tree_is_in_flight (plus _from_the_index_only, _index_and_the_working_tree) | closed |
| ledger lines staged only (repro b, MM) | case 3: append, git add, `git show HEAD:$L > $L`, status `MM`: in flight. Case 3b (staged line is CHECKPOINT b6): in flight | PASS plan.in_flight (fail-open), both | LedgerCopies.test_ledger_lines_staged_only_is_in_flight (asserts `MM`) | closed |
| ledger absent at HEAD | cases 4 (untracked), 4b (staged), 5 (index only, `AD`), 8/9 (no HEAD): in flight | 4/4b/8/9 in flight; 5 PASS plan.in_flight (fail-open) | test_ledger_absent_at_head_is_in_flight (both subtests), test_ledger_staged_only_absent_at_head_and_in_the_working_tree_is_in_flight | closed |
| .superpowers/sdd as a file | rp.sh case 5: `FAIL review_package.input: .superpowers/sdd is not a directory` rc=1 | PASS review_package.all rc=0 | test_all_mode_sdd_that_is_a_file_fails | closed |
| <plan>/review as a file | rp.sh case 10: FAIL review_package.p.input | same FAIL (already worked before) | test_all_mode_review_that_is_a_file_fails | closed |
| README and docstring state DEC-0021 and DEC-0022 | README:25 and the gate_review_package module docstring hold both rules; they match DEC-0021 (valid-at-pin-sha) and DEC-0022 (absent-packages-pass) | DEC-0021 text missing at ea19922 (implementer RED; README diff shows the row was rewritten) | test_readme_and_docstring_state_dec_0021_and_dec_0022 | closed |
DEC-0031 widen: `.superpowers` as a file FAILs `review_package.input: .superpowers is not a directory` (rp case 2; PASS at ea19922). The `for rel in (SDD,)` mutant is killed. Covered.
V2-2 (b5-final-verify2.md:47): this is B5-FIX-5 (a)+(b), closed by cases 2 and 3. verify2's O-1 (:50, sdd as a regular file) is now B5-FIX-6, closed by rp case 5.

## Contradiction matrix, plans_in_flight (inflight.sh, output in r8-rr1/inflight.out)
In flight at c0316eb: cases 2, 3, 3b, 4, 4b, 5, 6 (rm --cached), 7 (git rm), 8, 9, 10 (CRLF work vs LF HEAD), 13 (ledger.md is a directory; FAIL plan.input traceback at ea19922), 14 (not a repo), 16 (dangling symlink; FAIL plan.input at ea19922), 19 (two ledgers, one deleted), 20 (unmerged `UU`).
Closed, PASS plan.in_flight, as intended: case 1 (all three agree), 11 (CRLF in all three), 22 (trailing blank lines after CHECKPOINT).
FAIL plan.input: case 12 (non-UTF-8 working copy). The same happens at ea19922, so it is not fail-open.
Closed although git reports an uncommitted change (R1-1): case 15 (O-1, working-tree symlink to identical bytes, ` T`), 17 (chmod +x staged, `M `), 18 (chmod +x working tree only, ` M`), 21 (state/zz replaced by a symlink to a directory holding the identical ledger; git status ` D docs/superpowers/state/zz/ledger.md` + `?? zz`). The same four are closed at ea19922.
Callers: plans_in_flight has one caller, gate_plan.check (--in-flight), reached through run_all.py:25 DEFAULT_ARGV. No Stop hook exists: `ls tools/gates/hooks` shows none, and `grep '"Stop"'` in .claude/settings*.json and ~/.claude/settings.json found nothing. gate_plan.py:14 ("the hooks use") refers forward to Tasks 7-9 (plan :3852) and is unchanged since ea19922.
_check_all matrix (rp.sh): .superpowers missing PASS; .superpowers as file FAIL; as dangling symlink FAIL; as symlink to dir PASS. sdd as file, dangling symlink or symlink to file FAIL; sdd as symlink to dir PASS; empty sdd PASS. review as file or dangling symlink FAIL .p.input; empty review PASS; .DS_Store FAIL .p..DS_Store.input. A file or dangling symlink as a plan entry in sdd PASSes (no directory is expected there; not a sibling). Every row matches README:25 and the docstring.

## Gates
| command | result |
|---|---|
| clone c0316eb: env -u CLAUDE_PROJECT_DIR PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t . | Python 3.9.6, Ran 589 tests, OK |
| same, tools/.venv/bin/python3 | Python 3.12.14, Ran 589 tests, OK |
| checkout: python3 tools/gates/run_all.py | rc 0, 167 lines, all PASS (12 are S2-b5-finalfix2, so 155 before the package, as claimed) |
| checkout: gate_plan.py --in-flight | 69 lines, all PASS |
| checkout: gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-b5-finalfix2 | 12 PASS, rc 0 |
| clone: mutation-B5-FIX-5 three steps | OK / FAILED (failures=1) / OK, matches transcript |
| clone: mutation-B5-FIX-6 three steps | OK / FAILED (failures=1) / OK, matches transcript |
| clone: extra mutants (no work enumeration; no ls-files; no ls-tree; newest-copy order reversed; review_package rel tuple without .superpowers; lexists->exists) | all killed |
| clone: mutant `_raw` isfile -> exists | SURVIVED (R1-2) |
| D3 (r8-rr1/d3.py: every fenced block in the S2 plan vs `git show <sha>:<file>`) | at c0316eb gate_plan.py = plan :4908, gate_review_package.py = :2858, test_gate_plan.py = :3941, test_gate_review_package.py = :2309, all EQUAL. Contradiction: at ea19922 none of the four match any block. README gate_plan section = plan block, diff empty. |
| test-count lines | test_gate_plan 111 and test_gate_review_package 69 (Ran 111 / Ran 69 in the clone), suite 589, run_all 155 before the package: all match the plan's added text |

## Claims (implementer report b5r7-fixwave-report.md)
| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| three copies must agree | inflight cases 2/3/5/6/7 at c0316eb | same at ea19922; perl revert step 2 | in flight / old PASS; FAILED (1) | PASS |
| staged-only test reproduces MM | read test :886-893, which asserts "MM" | case 3 by hand: status MM | MM, in flight | PASS |
| --all FAILs .superpowers/.superpowers/sdd non-dir | rp cases 2/3/5/6/7 | ea19922 same cases | FAIL rc 1 / PASS rc 0 | PASS |
| no package / empty review still PASS | rp cases 1/9/12 | rp case 13 (.DS_Store FAILs) | PASS / FAIL | PASS |
| single consumer run_all | grep plans_in_flight tools/gates (non-test) | ls hooks; grep Stop in settings | gate_plan.py:572 only; no Stop hook | PASS |
| D3 blocks equal | d3.py at c0316eb | d3.py at ea19922 | EQUAL x4 / no match x4 | PASS |
| suite 589 both interpreters | ran both | the per-module counts sum with the claimed deltas (111, 69) | OK x2 | PASS |
| 22 mutants all KILLED (incl. "F5-11 _raw without isfile") | not re-run in full; 7 independent mutants killed | isfile->exists mutant survives | see R1-2 | the claim holds only for the removal mutant; it does not cover the non-regular-file branch |

## Findings
- R1-2 LOW (caused by this wave: a missing test). tools/gates/gate_plan.py `_raw` (`if not os.path.isfile(path): return None`, added in c0316eb). Reproducer: in the clone, `perl -pi -e 's/if not os.path.isfile\(path\):/if not os.path.exists(path):/' tools/gates/gate_plan.py`, then `python3 -m unittest tools.gates.tests.test_gate_plan` prints OK, so the mutant survives. Observable effect: a working-tree ledger.md that is a directory (inflight case 13) changes from "in flight" to `FAIL plan.input: [Errno 21] Is a directory`. No test makes the work-tree copy a non-regular file. Fix: add a LedgerCopies test where ledger.md is a directory in the working tree (and optionally a dangling symlink, case 16) and assert it is in flight through main().

## Decision needed (owner): R1-1 (O-1 family), class of B5-FIX-5
Observation: the closed test compares content only. `_raw` follows symlinks and ignores the file mode, so an uncommitted typechange or mode change whose bytes are identical reads as closed (PASS plan.in_flight): inflight cases 15 (O-1, ` T`), 17 (`M ` mode staged), 18 (` M` mode working tree only), 21 (directory symlink; git itself reports ` D .../zz/ledger.md`). The row's class says "reads an uncommitted ledger change as 'not in flight'", and README says "byte-equal". These are not new: all four read closed at ea19922 too. After the commit, HEAD holds a symlink blob or no ledger.md, so the plan then reads in flight.
Options:
(a) In B5-FIX-5's class. B5-FIX-5 stays open, and the fix also compares the mode/type, e.g. lstat plus `git ls-files -s` / `ls-tree` modes, or `git diff --quiet HEAD -- <rel>`. Cost: about 10 lines + 3-4 tests + a new transcript and package in this round; delays the pin.
(b) A new register row with an owner (as V2-2 was filed for B5-FIX-4). Cost: one row now, the fix in a later wave; B5-FIX-5 can pin.
(c) Out of class and accepted: the contract is content equality (README "byte-equal"), so add one README sentence that mode and type are ignored. Cost: one doc line; a symlink/mode-only change stays invisible until it is committed.
Not ruled here.

## Observations (not findings against the wave)
- DEC-0032 guard: in this Bash tool, an inline command that began with `set -eu` kept running after a failing `mv` (mv printed "No such file or directory" and the next commands ran). The explicit `case ... exit 99` prefix checks are what protects the checkout; `set -e` alone should not be relied on. Every script in this review used the explicit checks.
- The clone holds no .superpowers packages (they are git-ignored), so run_all line counts can only be checked in the checkout.

## Denials
none.
## Background jobs
none started.
## Budget
about 75k of 110k tokens; about 25 of 35 minutes.
