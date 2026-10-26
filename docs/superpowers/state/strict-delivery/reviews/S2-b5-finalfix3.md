# S2-b5-finalfix3 scoped re-reviews (b5 round 10)

Pins in this file's first commit: B5-FIX-8 (area A4), B5-FIX-9 (area A2 phase 1), B5-FIX-12, -13, -15, -19, -20, -24, -25 (area A3 phase 1), each reviewed at 6aaba81 (code equal to d3ed28d) with its package transcript replayed OK/FAILED/OK there. Rows still open are listed in the reports below.

## Area A4 report (b5r10-rr-A4.md, verbatim)

status: DONE_WITH_FINDINGS

# b5 round 10 scoped re-review, area A4 (gate_dispatch, lib/__init__ Result/passed, config.yml reading, tests, README gate_dispatch section) at 6aaba81 (code = d3ed28d)

Verdict: spec: ✅ quality: ❌ claims-executed: ✅
(code matches DEC-0024 / DEC-0028; round-9 R-A4-1..3 are fixed; quality ❌ for one stale README sentence in the same section that the round-9 fix and its new test both missed: R-A4-1 below)

Per row:
- B5-FIX-8: CLEAN (all 3 siblings closed: head FAILs, 5b900bd fail-open or crashes; transcript replays OK/FAILED/OK)
- B5-FIX-10: OPEN (R-A4-1: README.md:169 still says the fallback PASS line names DEC-0018. The two DEC-0040 siblings are out of scope: area A1)

Scratch: $SCR/r10-rr-A4/ (repo = 6aaba81, old = 5b900bd, drive.py, config backups). Every write script ran the DEC-0032 guard (checked again after each cd). I never wrote to the checkout, apart from this report. At the end of the review the checkout HEAD is 6aaba81, with ` M` on gate_plan.py, gate_review_package.py, make_review_package.py and their three tests: the round-10 implementer's uncommitted work, none of it in an A4 file. Both clones are clean again (`git status --short` empty).

## Commits
- d3ed28d fix(gates): B5-FIX-5,-8,-10,-11,-14,-15,-16 -- S2-b5-finalfix3 fix round 1. A4 hunks: the gate_dispatch.py:307 docstring, the test_gate_dispatch.py :439 word list, the new test_readme_and_docstring_state_dec_0028_and_dec_0024 (:575-586), the :589-590 docstring, README :24 and :185-188.
- 6aaba81 chore(state): state only (`git diff --stat d3ed28d 6aaba81 -- tools` is empty).

## Findings raised (caused by this change; listed first)
- R-A4-1 LOW, tools/gates/README.md:169: "a `fable_roles` role on the model `fable_fallback` names, while that key is set, is `PASS dispatch.model.fallback` (naming DEC-0018) in place of the rank line". "(naming DEC-0018)" describes the reason that B5-FIX-10 / DEC-0024 removed (1a6ff7c gate_dispatch.py:118-119 reason "... unavailable (DEC-0018)"). At head the line is the bare `PASS dispatch.model.fallback`.
  - Direct: `grep -n 'DEC-0018' tools/gates/gate_dispatch.py` matches only comments and docstrings (:101, :120, :124, :307); there is no output literal.
  - Contradiction: test_fallback_on_lets_a_fable_role_run_on_opus asserts `["PASS dispatch.model.fallback"] == lines` and passes (OK).
  - The guard misses it: mutant M5, which rewrites :169 to the old `PASS dispatch.model.fallback: <role> on <model>: ... (DEC-0018)` form, SURVIVES test_gate_dispatch + test_lib. The new README test checks only :24 and :185-188, and the :436-439 section test checks only that "PASS dispatch.model.fallback" is present.
  - Fix: drop "(naming DEC-0018)", or say "(the bare id, DEC-0024; the fallback is DEC-0018)", and add an assertNotIn of "(naming DEC-0018)" (or of any `PASS <id>:` form) over the section to test_readme_and_docstring_state_dec_0028_and_dec_0024.
- Cosmetic, part of R-A4-1's fix if wanted: README.md:188 is a short wrapped line ("(DEC-0024); the session sets and clears it") because the :186-187 rewrite did not re-flow the text. No behaviour is affected.

## Unrelated findings
none new. U-A4-1 is known and not re-reported. Mutant M9 (drop `isinstance(rank, dict)`) survives, and it is equivalent through main() for that reason.

## Sibling table
| row | sibling | status | covering test | repro @5b900bd (old) | repro @6aaba81 through main() |
|---|---|---|---|---|---|
| B5-FIX-8 | str rank of the fallback model | closed | test_gate_dispatch::test_config_check_fails_a_fallback_other_than_opus_whatever_its_rank | config fallback sonnet, sonnet: x -> `FAIL dispatch.input: '<=' not supported between instances of 'int' and 'str'` (the model_rank line is lost); fallback_error raises TypeError | `FAIL dispatch.config.model_rank` + `FAIL dispatch.config.fable_fallback: ... must be exactly 'opus' (DEC-0028) ...` |
| B5-FIX-8 | bool rank of opus, fable or the fallback model | closed | same | opus: true -> `PASS dispatch.config.fable_fallback: opus: ...` (fail-open); fallback_error(opus=True) -> None | FAIL dispatch.config.fable_fallback; drive.py: opus=True, fable=True and sonnet=True all err |
| B5-FIX-8 | fable_fallback other than opus (DEC-0028) | closed | same | fallback_error(sonnet ranked == opus) -> None; through main sonnet:3 -> `PASS dispatch.config.fable_fallback: sonnet: ...` | err / FAIL; fable, haiku, None, 'Opus', opus==fable and float rank also err; only exact opus with int 3<4 passes (None) |
| B5-FIX-10 | passed() signature takes no reason | closed | test_lib::test_passed_takes_no_reason | n/a (verified in round 9) | OK; lib/__init__.py:41-43 `passed(self, id)` -> `Line("PASS", id, "")` |
| B5-FIX-10 | every caller passing a reason (gate_dispatch.py:118, :344, :347) | closed in code; doc OPEN (R-A4-1) | test_gate_dispatch::test_every_pass_line_is_the_bare_id, ::test_fallback_on_lets_a_fable_role_run_on_opus | old: `PASS dispatch.config.fable_fallback: opus: the fable_roles run on opus while Fable is unavailable (DEC-0018)` | `PASS dispatch.config.fable_fallback` (bare); README:169 still describes the reason |
| B5-FIX-10 | tests asserting PASS-line text | closed (producers) | test_gate_dispatch::test_fallback_on_lets_a_fable_role_run_on_opus | n/a | see the gate table: every PASS line of every gate is bare |
| B5-FIX-10 | 2 DEC-0040 siblings (GATE_LINE, its test) | not reviewed: area A1 | - | - | - |

Other PASS producers: `grep -rn 'Line('` under tools (tests excluded) finds only lib/__init__.py:39/:43 and run_all.py:70 (copies l.reason, which is "" for PASS). hooks/pre_tool_use.py:298 rewrites only the reason. make_review_package.py:163 prints a "wrote ...; gates PASS" summary, which is not a gate line.

## Gates run (clone at 6aaba81; env -u CLAUDE_PROJECT_DIR PYTHONDONTWRITEBYTECODE=1)
| command | result |
|---|---|
| /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t . | Ran 632 tests in 100.943s / OK |
| tools/.venv/bin/python3 -W error -m unittest discover -s tools/gates/tests -t . | Ran 632 tests in 117.914s / OK |
| python3 tools/gates/run_all.py | rc=0, 129 lines, 129 PASS, 0 not matching `^PASS [^ :]+$` (the clone has only the S2-b5-finalfix3 package, so the count differs from the checkout's) |
| gate_register.py | rc=0, 6 PASS, 0 with a reason |
| gate_claims.py | rc=0, 4 PASS, 0 with a reason |
| gate_decisions.py --scan docs/superpowers/plans/*.md | rc=0, 5 PASS, 0 with a reason |
| gate_dispatch.py --config-check | rc=0, 15 PASS, 0 with a reason (README's "15 lines." holds) |
| gate_dispatch.py --prompt b5r10-rr-A4-prompt.md / b5r10-impl-prompt.md --tool-model opus --subagent-type general-purpose | every PASS bare. The FAILs on dispatch.brief / dispatch.report are expected: the clone has no untracked sdd dir |
| gate_review_package.py --all | rc=0, 30 PASS, 0 with a reason |
| gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-b5-finalfix3 | rc=0, 30 PASS, 0 with a reason |
| gate_plan.py --in-flight / --plan <S2 plan> | rc=0, 69 PASS each, 0 with a reason |

## Claims (d3ed28d / b5r9-fix1 for A4)
| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| R-A4-1 (r9): the check_config docstring states exact opus | `sed -n 305-307p gate_dispatch.py` | mutant M1 (old docstring text) | ":307 is exactly opus (DEC-0028))"; M1 killed (2 failures) | PASS |
| R-A4-2 (r9): the test docstring says the PASS line is bare | `sed -n 589-590p test_gate_dispatch.py` | the :593 assertion is bare `PASS dispatch.config.fable_fallback` and passes | "set or absent, its PASS line is the bare id (DEC-0024)" | PASS (docstring only; not mutation-testable) |
| R-A4-3 (r9): README :24 and :185-187 say exact opus and a bare PASS | grep 'at least opus\|ranked at\|fallback: absent' README | mutants M2, M3, M4 (old text back) | grep empty; M2 killed (3), M3 killed (2), M4 killed (1) | PASS |
| The README gate_dispatch section has no stale PASS-reason text | grep 'naming DEC-0018' README | mutant M5 | README:169 matches; M5 SURVIVES | FAIL -> R-A4-1 |
| fallback_error = exactly opus, int non-bool ranks, opus < fable | drive.py @6aaba81 (12 cases) | drive.py @5b900bd | head: only `opus` returns None; base: str rank RAISES TypeError, bool opus None, sonnet==opus None | PASS |
| Every PASS line any gate prints is a bare `PASS id` (DEC-0024) | every gate run (see the table) | grep for other Line/PASS producers | 0 non-bare PASS lines; no other producer | PASS |

## Transcript replays (clone @6aaba81)
- mutation-B5-FIX-8.txt: step 1 OK, step 2 (the perl `_int_rank` -> `isinstance(..., int)`, 1 file changed) FAILED, step 3 OK. Matches the transcript.
- mutation-B5-FIX-10.txt: step 1 OK, step 2 (perl gives passed() its reason parameter back, 1 file changed) FAILED, step 3 OK. Matches the transcript.

## Mutants (kill set: test_gate_dispatch + test_lib)
- M1 docstring old: killed (2)
- M2 README :24 old: killed (3)
- M3 README :186 old: killed (2)
- M4 drop the README :187 DEC-0024 sentence: killed (1)
- M5 README :169 with the reason form: SURVIVED (non-equivalent, see R-A4-1)
- M6 _int_rank admits bool: killed (2)
- M7 rank-based instead of exact opus: killed (5+1E)
- M8 < -> <=: killed (1)
- M9 drop isinstance(rank, dict): SURVIVED (equivalent through main(), see U-A4-1)
- M10 fallback PASS carries a reason: killed (7)
- M11 render prints a ": " suffix after PASS: killed (21)
- M12 int check on opus only: killed (3)

Tally: 12 mutants, 10 killed, 1 equivalent survivor, 1 non-equivalent survivor (R-A4-1).

## Decision needed
none

## Denials
none

## Background jobs
none started. The unittest / mut.py processes seen in `ps` belong to the r10-rr-A2 and r10-rr-A3 reviewers' clones, not to me.

## Budget used
about 45k tokens, about 20 minutes

## Area A3 report (b5r10-rr-A3.md, verbatim)

status: DONE_WITH_FINDINGS

# b5 round 10 scoped re-review, area A3, phase 1 (HEAD_SHA 6aaba81, code == d3ed28d)

Clone: $SCR/r10-rr-A3/repo (git clone --no-hardlinks, checkout 6aaba81; review packages copied in with cp -R). Checkout never written except this report. Old code: `git archive 5b900bd` -> $SCR/r10-rr-A3/old; pre-fix-round code: `git checkout f13930c -- tools/gates/gate_plan.py` in the clone (restored, `git status --short` empty after).

spec: ❌ quality: ❌ claims-executed: ✅
(spec/quality ❌ only for R-A3-1 (open in-class instance of B5-FIX-14) and the two surviving non-equivalent mutants R-A3-2, R-A3-3; everything else in the area is clean.)

## Commits
none (review only). Reviewed: d3ed28d fix(gates): B5-FIX-5,-8,-10,-11,-14,-15,-16 -- S2-b5-finalfix3 fix round 1; 6aaba81 is state-only for tools/ (`git diff --stat d3ed28d 6aaba81 -- tools` empty).

## Per row
- B5-FIX-5: CLEAN (7/7 siblings closed; R-A3-1 glob-name fix and R-A3-3 owner-x verified; transcript OK/FAILED/OK; M18 survivor reported as R-A3-3 below, a test gap on the new parse, fail-closed)
- B5-FIX-12: CLEAN
- B5-FIX-13: CLEAN
- B5-FIX-14: OPEN (R-A3-1 NFC/NFD spelling of one APFS file passes overlap; R-A3-2 surviving mutant on the Task-1-side directory boundary)
- B5-FIX-15: CLEAN (U+017F subtests present and pass; note: they do not kill a re.I mutant on their own, the U+0131/U+0130 subtests do)
- B5-FIX-19: CLEAN
- B5-FIX-20: CLEAN
- B5-FIX-24: CLEAN (sibling per DEC-0042: plans_in_flight() call, test_a_missing_state_dir_raises_naming_it)
- B5-FIX-25: CLEAN

## Sibling table (all covering tests exist in tools/gates/tests/test_gate_plan.py at HEAD: grep `def <name>(` for every name in both siblings.md tables, 0 missing)
| row | sibling | state | test |
|---|---|---|---|
| B5-FIX-5 | ledger deleted in the working tree | closed | test_ledger_deleted_in_the_working_tree_is_in_flight |
| B5-FIX-5 | ledger lines staged only | closed | test_ledger_lines_staged_only_is_in_flight (transcript) |
| B5-FIX-5 | ledger absent at HEAD | closed | test_ledger_absent_at_head_is_in_flight |
| B5-FIX-5 | ledger replaced by a symlink in the work tree | closed | test_ledger_replaced_by_a_symlink_in_the_work_tree_is_in_flight |
| B5-FIX-5 | ledger mode change staged | closed | test_ledger_mode_change_staged_is_in_flight (+ glob-name test, kills M14/M15) |
| B5-FIX-5 | ledger mode change in the work tree only | closed | test_ledger_mode_change_in_the_work_tree_only_is_in_flight |
| B5-FIX-5 | state dir replaced by a symlink | closed | test_state_dir_replaced_by_a_symlink_is_in_flight (kills M13) |
| B5-FIX-12 | item after a code fence / **Field:** line / a step | closed | test_a_files_style_item_outside_the_files_list_fails |
| B5-FIX-13 | absolute path / '..' component | closed | test_absolute_and_dot_dot_paths_fail |
| B5-FIX-14 | case variant | closed | test_overlap_folds_case_backslashes_and_directories (kills M8) |
| B5-FIX-14 | directory containing a file | closed in code, mutant M7 survives (R-A3-2) | test_overlap_folds_case_backslashes_and_directories, test_a_task_1_directory_holding_a_task_2_file_overlaps |
| B5-FIX-14 | backslash spelling | closed | same two tests (kill M9) |
| B5-FIX-14 | (not listed) Unicode normalisation spelling | OPEN | R-A3-1 |
| B5-FIX-15 | re.I folds U+0131/U+0130/U+017F; category Cf neighbours | closed | test_pin_title_needs_the_ascii_word_and_no_format_character_neighbour (kills M10) |
| B5-FIX-19 | fence >3 spaces / exactly max_tasks / CHECKPOINT ends Files | closed | test_a_closing_fence_indented_four_spaces_does_not_close, test_a_block_of_exactly_max_tasks_passes, test_a_checkpoint_step_ends_the_files_list |
| B5-FIX-20 | 3 root-subdirectory siblings | closed | test_root_a_subdirectory_reads_its_own_index_and_head_copies |
| B5-FIX-24 | state dir without ledger.md; plans_in_flight() missing-state raise | closed | test_a_state_dir_without_ledger_md_is_not_a_ledger, test_a_missing_state_dir_raises_naming_it |
| B5-FIX-25 | non-UTF-8 ledger names its path | closed | test_a_non_utf8_ledger_fails_naming_the_ledger |

## Gates run (clone, HEAD 6aaba81)
| command | result |
|---|---|
| env -u CLAUDE_PROJECT_DIR PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t . | Ran 632 tests ... OK |
| same with tools/.venv/bin/python3 | Ran 632 tests ... OK |
| python3 -m unittest tools.gates.tests.test_gate_plan | Ran 132 tests OK (plan line 6246 states "132 and a suite of 632 after its fix round 1": matches) |
| python3 tools/gates/run_all.py (all 7 review packages copied in) | rc 0, 197 PASS, 0 other lines (with only 2 packages: rc 0, 141 PASS) |

## Transcript replays (fix present / reverted by the transcript's perl / restored; each mutant confirmed to change the file)
mutation-B5-FIX-5 (finalfix2) PASS FAIL PASS; finalfix3: -12, -13, -14, -15, -19, -20, -24, -25 each PASS FAIL PASS. All match the packages.

## Claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| R-A3-1: glob-character state-dir name no longer reads ledger 0's mode | new test at HEAD: OK | gate_plan.py from f13930c + same test | f13930c: FAIL (name='?') "'plan.?.header.goal' not found in 'PASS plan.in_flight\n'", FAIL (name='[0]') | PASS |
| _git_mode keeps only the exact entry; pathspec literal | scratch repo with x?, x*, x[0], xa, xA(index-only 100755), xd/ledger.md/f: `git --literal-pathspecs ls-files -s -z -- <rel>` lists only the exact entry (xd: only ledger.md/f) | plain `git ls-files -s -- docs/.../x?/ledger.md` lists x*, x0, x? | as stated; ls-tree lists exact entry with or without the flag; core.ignorecase=true does not widen ls-files | PASS |
| R-A3-3: owner-x only (0645/0654 closed) | test OK; mutant M4 0o100->0o111 | M4 | KILLED (2 subtests) | PASS |
| R-A3-2: Task-1 directory holding a Task-2 file overlaps | probe B at HEAD: rc 1 "FAIL plan.overlap.1-2: PARALLEL tasks share src ~ src/beta.py, src ~ src/shared.py" | same probe at 5b900bd | rc 0 PASS plan.overlap.1-2 | PASS |
| B5-FIX-14 case variant through main() | probe D HEAD: FAIL plan.overlap.1-2 SRC/BETA.PY | 5b900bd | rc 0 PASS | PASS |
| D-A3-2 U+017F subtests | in test, pass | n/a (regression only) | | PASS |

## Mutant tally (gate_plan.py, killed by tools.gates.tests.test_gate_plan, -W error)
killed 15: M3 drop -z in _git_mode; M4 0o111; M5 drop q-side dir prefix; M6 drop p-side; M8 no casefold; M9 no backslash; M10 no Cf; M11 no M*; M12 no hyphens; M13 symlink only at leaf; M14 no index mode; M15 no HEAD mode; M16 no ls-files ledger list; M17 no HEAD ledger list; (+3 transcript mutants per row above).
survived 4:
- M1 drop `--literal-pathspecs` (gate_plan.py:556): EQUIVALENT given the exact-path filter (the plain listing's extra entries have path != rel; shown above).
- M2 `if path == rel` -> `if entry` (:559): EQUIVALENT given --literal-pathspecs (literal listing holds only rel, or rel/... when ledger.md is a directory, where copies already differ: _raw None vs `git show HEAD:./rel` tree text). Note: each of M1/M2 is covered only by the other; M1+M2 together is killed by the glob-name test.
- M7 `q.startswith(p + "/")` -> `q.startswith(p)` (:509): NON-EQUIVALENT -> R-A3-2.
- M18 `entry.partition("\t")` -> `rpartition` (:560): NON-EQUIVALENT -> R-A3-3.

## Findings raised
- R-A3-1 (LOW; B5-FIX-14, in its class "compares raw Files strings ... one file on case-insensitive APFS"; present at 5b900bd and at HEAD): gate_plan.py:501-503 `_overlap_key` folds case and backslashes but not Unicode normalisation. Reproducer: good.md Task 1 `- Create: \`src/café.py\`` (NFC), Task 2 `- Create: \`src/café.py\`` (NFD) -> HEAD rc 0, PASS plan.overlap.1-2 (5b900bd same). APFS check: writing the NFC name then `os.path.exists(<NFD name>)` -> True, 1 directory entry: one file. Fix: `unicodedata.normalize("NFC", ...)` inside _overlap_key before casefold, plus an NFC/NFD subtest in test_overlap_folds_case_backslashes_and_directories. If the controller reads this as outside B5-FIX-14's class, it is a widen question for the owner: (a) add sibling "Unicode normalisation spelling" to B5-FIX-14 (one-line fix + subtest, row stays unpinned this round) or (b) register a new LOW row with its own pin cycle (B5-FIX-14 pins now).
- R-A3-2 (LOW; test gap on B5-FIX-14 "directory containing a file"): mutant M7 at gate_plan.py:509 survives: Task 1 `src/bet` vs Task 2 `src/beta.py` is not an overlap (HEAD probe A: rc 0 PASS plan.overlap.1-2) but no test pins it, so a mutant dropping the "/" boundary on the Task-1 side would FAIL valid plans unseen. The existing `src/alph` subtest exercises only the Task-2 side. Fix: mirror subtest (Task 1 `src/bet`, expect PASS plan.overlap.1-2).
- R-A3-3 (LOW; test gap on the d3ed28d _git_mode parse, B5-FIX-5 R-A3-1 fix): mutant M18 at gate_plan.py:560 survives. Reproducer: closed committed ledger under state dir `a\tb` -> HEAD `(0, 'PASS plan.in_flight\n')`; with M18 the same ledger reads in flight (`PASS plan.a\tb.header.agentic ...`). Fail-closed, but a non-equivalent survivor on the new branch. Fix: a closed-ledger subtest whose state-dir name holds a tab.

## Unrelated findings
none.

## Notes
- The S2 plan holds no task whose **Findings:** carries a B5-FIX row (Findings/Siblings fields are SD-S2-T* only: `grep -n '^\*\*Findings:\*\*.*B5-FIX'` empty), so the gate's plan Siblings/Gates/pin check does not reach these rows; the plan's gate_plan test count (line 6246: 132, suite 632) matches the run.
- Concurrent DEC-0041 work in the checkout not reviewed (phase 2), not reported.

## Denials
none.

## Background jobs
none started.

## Budget used
~70k of 100k tokens; ~25 of 30 minutes.

## Area A2 report (b5r10-rr-A2.md, verbatim)

status: DONE_WITH_FINDINGS

# b5 round 10 scoped re-review, area A2 (make_review_package), HEAD_SHA 6aaba81 (code == d3ed28d)

verdict: spec: ❌ quality: ❌ claims-executed: ✅
(spec ❌: R-A2r10-1, an ignored plan file named by a different-case ledger line on a case-insensitive FS passes the B5-FIX-16 check and feeds gates.txt. quality ❌: R-A2r10-2 and R-A2r10-3, two non-equivalent surviving mutants.)

Per row:
- B5-FIX-7 (helper part): CLEAN
- B5-FIX-9: CLEAN
- B5-FIX-16 (siblings 1-4): OPEN (R-A2r10-1 on the plan path; R-A2r10-2 and -3 are test gaps). Sibling 5 (DEC-0039) is out of scope (phase 2).

## commits
None by me; this was a read-only review. `git diff --stat d3ed28d 6aaba81 -- tools` is empty, so the code equals d3ed28d. I reviewed the d3ed28d change to make_review_package.py:83-100 and the new tests at test_make_review_package.py:205-300.

## gates run (clone scratchpad/r10-rr-A2/repo at 6aaba81, package dirs copied in)
| command | result |
|---|---|
| env -u CLAUDE_PROJECT_DIR PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t . | Ran 632 tests, OK |
| same with tools/.venv/bin/python3 | Ran 632 tests, OK |
| python3 tools/gates/run_all.py | rc=0, 197 PASS, 0 non-PASS |
| head test_make_review_package.py run against the 5b900bd code (worktree r10-rr-A2/base) | FAILED (failures=18): 8 file_hidden, 5 line_1, 3 helper_and_gate_agree, 1 pathspec_magic, 1 packages_dir |

## sibling table
| row | sibling | state | covering test / evidence |
|---|---|---|---|
| B5-FIX-7 | quoted first diff path (helper side) | closed | test_helper_and_gate_agree_on_a_quoted_path_and_an_empty_subject[café.md]: FAILs at 5b900bd, PASS at head |
| B5-FIX-7 | empty or space-led subject (helper side) | closed | same test, [empty] and [space-led] subtests (3 FAIL at base) |
| B5-FIX-7 | helper and gate agree on every range | closed for these cases | same test. The gate side belongs to A1. |
| B5-FIX-9 | package name with a slash (M22) | closed | test_package_name_with_a_slash_is_refused; transcript replay OK/FAILED/OK |
| B5-FIX-9 | pending mutation-file hint (M29) | closed | test_pending_hint_names_the_mutation_file_until_one_exists (round 9 killed M29 and M29b; the code is unchanged since) |
| B5-FIX-16 | file hidden by .git/info/exclude | closed for fixed paths; OPEN for a case-variant plan path (R-A2r10-1) | test_file_hidden_by_info_exclude_or_gitignore_fails_before_writing (8 FAIL at base) |
| B5-FIX-16 | file hidden by .gitignore | same | same test |
| B5-FIX-16 | packages dir stays allowed (DEC-0029) | closed | test_packages_dir_and_pycache_stay_allowed, test_a_plan_in_the_packages_dir_is_excepted (sddx is refused). Probes ./.superpowers/sdd/.. and .superpowers/SDD/.. give rc 0 (exempt); .superpowers/sdd/../../docs/.. is normalised and refused. |
| B5-FIX-16 | __pycache__ stays allowed (DEC-0033) | closed per the D-2 reading (any component); see Q1 | test_packages_dir_and_pycache_stay_allowed (notes.txt, sub/x.pyc); refused-list subtests x__pycache__/y.pyc and lib/__pycache__ as a file |
| B5-FIX-16 | ignored file under another plan (DEC-0039) | out of scope (phase 2) | Pointer for phase 2: run_all.py:42 also feeds every docs/superpowers/plans/*.md to gate_decisions via PLANS_GLOB. |

## findings raised (all in this wave's code: _ignored, added at f13930c and changed at d3ed28d)

**R-A2r10-1 (LOW, fail-open; B5-FIX-16 plan path)**
- Location: make_review_package.py:94-97.
- The ledger's plan path is matched by a case-sensitive git pathspec. gate_plan opens the same path through the filesystem (gate_plan.py:146-147, :427). On a case-insensitive FS (macOS APFS; the fixture has core.ignorecase=true), the line `plan: Docs/Superpowers/plans/fx.md` makes the gate read the ignored docs/superpowers/plans/fx.md. git ls-files with several pathspecs walks the real lower-case names and lists nothing.
- Repro (bin/probe_a2.py test_p5_case; bin/probe_case.py; both go through main()):
  - Commit the ledger with that line 1; plant docs/superpowers/plans/fx.md and add it to .git/info/exclude.
  - Head with `--only register`: rc=0 "gates PASS", no Refused.
  - Head with `--only plan`: the package is written; gates.txt reads "FAIL plan.fx.header.agentic ..." computed from the ignored file's content.
  - The lower-case line gives "Refused ... docs/superpowers/plans/fx.md".
  - At 5b900bd both lines write the package (the old fail-open).
- Mechanism (scratch repo r10-rr-A2/icase, git 2.50.1):
  - `--literal-pathspecs ls-files --others --ignored --exclude-standard -- tools/gates Docs/P/fx.md` prints nothing.
  - `':(literal,icase)tools/gates' ':(literal,icase)Docs/P/fx.md'` prints docs/p/fx.md.
- Fix: pass each path as ':(literal,icase)<p>' when core.ignorecase is true, or always (the global --literal-pathspecs cannot be combined with other magic). Add a test that skips unless the FS is case-insensitive.

**R-A2r10-2 (LOW, test gap: non-equivalent survivor)**
- Mutant E: `p.split("/")[:-1]` changed to `p.split("/")[1:-1]` at :100 survives the suite.
- It is not equivalent. With the ledger plan `__pycache__/fx.md` (ignored), head gives rc=0 "gates PASS" (exempt), and mutant E gives rc=1 "Refused ... __pycache__/fx.md" (bin/probe_e.py).
- Fix: a subtest with a top-level `__pycache__/` component, once Q1 settles which result is right.

**R-A2r10-3 (LOW, test gap: non-equivalent survivor)**
- Mutant N: drop `"replace"` from `out.decode("utf-8", "replace")` at :99 survives.
- It is not equivalent on Linux, where the CI runs (.github/workflows/gates.yml:13, ubuntu-latest). There, an ignored file with a non-UTF-8 byte in its name under tools/gates is refused at head, and the mutant raises an uncaught UnicodeDecodeError out of main().
- It cannot be reached on macOS: creating such a name fails with "[Errno 92] Illegal byte sequence".
- Fix: a test that is skipUnless the FS accepts the name, or a test that mocks _git's output.

## Decision needed (owner; not decided by me)

**Q1 (DEC-0033 / D-2 scope on the plan path)**
- At head, a ledger plan under any `__pycache__` component (for example docs/superpowers/plans/__pycache__/fx.md, or __pycache__/fx.md) is exempt. gates.txt is then computed from a plan file that is not in HEAD (E-PROBE: head rc=0 gates PASS).
- Options:
  - (a) Keep the exemption: it is the literal DEC-0033 text as the round-9 controller ruling read it. Cost: that fail-open stays accepted; add the R-A2r10-2 subtest.
  - (b) Apply the __pycache__ exemption only under tools/gates and the state dir, not to the plan path. Cost: one branch and a test.

## claims
| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| code at 6aaba81 == d3ed28d | git diff --stat d3ed28d 6aaba81 -- tools | - | empty | PASS |
| suite 632 OK on both interpreters; run_all green | discover with both pys; run_all | head tests on base code | 632 OK x2; 197 PASS rc 0; base FAILED (18) | PASS |
| R-A2-2: line 1 read as gate_plan reads it | test_the_plan_named_by_line_1... (5 separators) | mutant H (split on \n only) | head OK; H KILLED (5) | PASS |
| R-A2-3: pathspecs literal | test_pathspec_magic... | mutant A (drop flag) | KILLED (1) | PASS |
| D-1: packages dir excepted, sddx refused | test_a_plan_in_the_packages_dir_is_excepted | mutants B, C | KILLED, KILLED | PASS |
| D-2: any parent __pycache__ component exempt, x__pycache__ and a file named __pycache__ refused | test_packages_dir_and_pycache_stay_allowed + refused list | mutants D, F, E | D KILLED (4), F KILLED (5), E SURVIVED | FAIL (R-A2r10-2) |
| R-A2-4: 3 round-9 survivors now killed | mutants D (=B16-anydir), F (=B16-anywhere), G (=strict-utf8) | - | KILLED (4), (5), errors=1 | PASS |
| empty ledger and no ledger handled | test_an_empty_ledger...; mutants I, K | - | KILLED, KILLED (36 errors) | PASS |
| the helper scans every path gate_plan reads for the a.plan ledger | probes ./, //, .., absolute (real and /var), trailing tab | case variant | all refused; case variant rc 0 | FAIL (R-A2r10-1) |
| plan outside the repo | probe /etc/hosts | - | rc 1, Refused with git "outside repository" (fail-closed) | PASS (note: the message is git's raw text) |
| transcripts B5-FIX-7, -9, -16 | replayed in the clone | - | 7: OK/FAILED/OK; 9: OK/FAILED/OK; 16: OK/FAILED/OK | PASS |

## mutant tally (make_review_package.py, test_make_review_package, /usr/bin/python3; source restored and `git status` clean after each)
- KILLED 12: A-no-literal, B-no-sdd, C-sdd-noslash, D-last-too, F-substr, G-strict, H-newline, I-no-empty, K-isfile-true, L-no-append, M-group0, O-no-ledger-plan.
- SURVIVED 2, both non-equivalent: E-skip-first (R-A2r10-2) and N-out-replace-strict (R-A2r10-3).

## Unrelated findings
None older than 5b900bd in this area.

## denials
None.

## background jobs
None started.

## budget used
About 75k of 100k tokens, about 25 minutes.

Scratch: scratchpad/r10-rr-A2/ holds repo (6aaba81), base (5b900bd worktree of the clone), icase (scratch repo) and bin/ (mut.py, probe_a2.py, probe_case.py, probe_e.py). I did not inspect the checkout beyond reading it.
