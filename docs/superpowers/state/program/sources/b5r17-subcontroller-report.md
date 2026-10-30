status: NEEDS_DECISION

Round 17 of block b5, opus sub-controller. Ran 19:39 to 20:48 SAST, about 70 of 180 minutes, so the 80 % line (22:03) was never reached. Base 2e43f13; HEAD is now ec1c3c5 and the tree is clean. One commit. Ledger: progress.md, from the line "19:39 round 17" to the line "20:48 docs re-review".

## commits
- ec1c3c5 chore(state): b5 round 17 item 3 -- B5-FIX-5 D33 siblings per DEC-0128; register B5-UR-16..23
  - D34 set run on the staged tree: run_all rc 0, 208 PASS, 0 other. Suites: 658 OK (skipped=1) on /usr/bin/python3 (139 s) and on tools/.venv (154 s). Pre-commit hook: register and claims PASS.
  - The staged diff matched the reviewed r3 patch (cmp-equal).

## Item 1: documentation and the class-as-epic design (for owner approval; no tracked edit)
- Draft: b5r17-docs-draft.md, 789 lines.
  - Spec D37 (DEC-0103), D38 (DEC-0130), D39 (DEC-0131), §4.1 parent field, §6 step 6.
  - §12/CLAUDE.md bullets and strict-block diffs.
  - Epic mechanics M1-M4 and G1-G6; the 28 pinned rows mapped; 18 feedback memories checked.
  - Option-text table; a Fix round 1 table.
- Reviews:
  - Review b5r17-docs-rev.md: spec ❌ quality ❌ claims ✅ (F1-F10).
  - Fix round (fresh opus implementer, since the drafter had used 78 % of its tokens): b5r17-docsfix-report.md, F1-F10 fixed.
  - Re-review b5r17-docs-rr.md: spec ❌ quality ❌ claims ✅. Residuals R2-1..7 are left for the owner (the brief allows one fix round):
    - R2-1: the text tells roles to register child rows before the register has a field for them.
    - R2-2: D38's wording matches Q-doc-18 (c), but the mapping table assumes (a).
    - R2-3: D37's "review lists removed tests" sentence and item (6) come from the brief, not DEC-0103.
    - R2-4 and R2-5: stale figures.
    - R2-6 and R2-7: LOW.
  - In clones, the core diff plus each Q-doc-3 option passes run_all and both suites.
- Same-commit count lines: test_hooks.py:996, S1 plan :6855 and mutations/B5-UD-1.txt:4 (13 becomes 14-16, depending on Q-doc-3).
- Prompt bytes after the change: implementer 4629, reviewer 4740, verifier 4572, live controller 5399. The staged D36 controller would be 5618, over the 5500 cap.
- Q-doc questions (verbatim in the draft, :335-351 and :781-784):
  - Q-doc-1 D-row numbers
  - Q-doc-2 DEC-0103 scope (deleted only, or weakened too)
  - Q-doc-3 which §12 bullets
  - Q-doc-4 which strict blocks
  - Q-doc-5 parent link schema
  - Q-doc-6 epic marker
  - Q-doc-7 resolved set
  - Q-doc-8 new child under a pinned epic
  - Q-doc-9 whole-class test and review
  - Q-doc-10 rows with parts in several dimensions
  - Q-doc-11 epic depth
  - Q-doc-12 D33's "widened row reopens"
  - Q-doc-13 requirement rows SD-S2-T1..T6
  - Q-doc-14 B5-CAPS-1
  - Q-doc-15 the 16 open sweep questions (decisions.yml:1028-1288)
  - Q-doc-16 D39 "a spec row" for every rule
  - Q-doc-17 homes for undocumented memories
  - Q-doc-18 which row carries an epic
  - Q-doc-19 the epic's owner task and package
  - Q-doc-20 whether a question may offer deleting a test
  - Q-doc-21 where a rule lives before its text lands
- Memory against repo text, part (d) of the draft:
  - Documented: audit-style, fix-minors, flash-ready, parallel-tasks, process-over-bug-chasing, reap-background-jobs, report-before-plan, scratch-script-guard, session-commits-gated, subagent-models, zero-warnings.
  - Partly documented: checkpoints, core-selftest-gate, prefer-idf-builtins (plan-03 :660).
  - Undocumented: always-document, class-as-epic and no-test-deletion-fix (homes are D39, D38 and D37 in this draft); merge-authorization (home: Q-doc-17).

## Item 2: restore the deleted tests (B5-RD-1, B5-UD-1): NEEDS_DECISION, no code
- The implementer stopped at STEP 0 (b5r17-impl-report.md). No spec, plan or DEC line states the acceptance rule:
  - Q-r17-1: which S2-plan and README text counts as "old-cap text" (spec :48)? Options: (a) a role-bound cap statement; (b) a fail-closed whitelist; (c) an exact-phrase list.
  - Q-r17-2: what must the README hold? Options: (a) no trigger value; (b) any value equal to config.yml; (c) old phrases only.
  - Q-r17-3: does D3 cover the partial test_hooks.py fragments? Options: (a) no; (b) each fragment must be a substring.
  - The options and costs are verbatim in the report.
- My check of its premise: config.yml:5 has the reviewer cap at 250000, which equals the old controller cap. So matching on values alone cannot be the rule.
- B5-UD-1: its mutation transcript still holds at HEAD.
- B5-CAPS-2: its class and repro become false once the tests land. That register change waits.
- Sweep (git log -p main..HEAD -- tools/gates/tests, 86 commits) found 6 removed `def test_`:
  - 6a7b2f9: the two tests above.
  - ec1ff2f: test_config_check_ranks_the_fallback_by_model_rank (DEC-0028/DEC-0024) and test_only_the_first_table_counts (umbrella :266, B5-FIX-22).
  - 605b768: test_every_backticked_path_of_a_files_item_counts and test_spans_inside_parentheses (DEC-0017).
  - The last four followed answered owner rules and were not made to turn a red test green, so they are not new rows.
  - Three of their inputs are not re-run byte for byte at HEAD; each goes through the same code branch as a test that does run.
- No review and no pins: B5-RD-1 and B5-UD-1 stay planned until the owner answers.

## Item 3: B5-FIX-5 D33 siblings (DEC-0128): committed ec1c3c5
- Grep: `git grep -n -F -e 'subprocess.run(["git"' -e 'check=False' -e '$(git ' -e 'git status' -e 'git read-tree' -- tools/gates ':!tools/gates/tests'` gives 36 lines, all opened.
- 10 hits, now in B5-FIX-5's siblings with the pattern:
  - gate_plan.py:524, :530, :556, :582 → new rows B5-UR-16..19 (dimension 2).
  - gate_review_package.py:193 and make_review_package.py:70 → B5-UR-12, which they share.
  - git-pre-commit.sh:35 → B5-UR-20 (fail-open: a failing git status reads as clean).
  - install-hooks.sh:14 → B5-UR-21.
  - git-pre-push.sh:18 → B5-UR-22 (fail-open: a deletes-only push runs no gate).
  - git-pre-push.sh:30 → B5-UR-23.
- One siblings.md cell per new sibling in the git-ignored S2-b5-finalfix2 package. The original file is saved as scratchpad/r17-pre/finalfix2-siblings-orig.md.
- B5-FIX-5 class:
  - The sentence keeping dimension-2 hits out of the siblings list is replaced.
  - The expected verdict for a git failure inside a repository is now FAIL plan.input, from DEC-0129's approved sheet row Q-FH-16 (a). The reviewer confirmed this is an owner answer, not a class change.
- Reviews:
  - Round 1, b5r17-item3-review.md: spec ❌ quality ❌ claims ✅.
    - F1 was my defect: the grep covered only *.py, while D33 says all of tools/gates.
    - F2: false class sentence. F3-F5: repro text.
  - Round 2, b5r17-item3-rr.md: spec ✅ quality ❌. N1: the OSError clause was false through lib/__init__.py:148.
  - Round 3, b5r17-item3-rr3.md: spec ✅ quality ✅ claims ✅.
  - All fixes were controller text rounds on the uncommitted diff.
- Owner items (D1-D5 in b5r17-item3-rr.md):
  - D1: does the shared B5-UR-12 meet "registers each hit as its own row" (DEC-0128)? This is also flag-review F1, and no sheet row asks it.
  - D2: should the 6 outside hits be in B5-FIX-5's list? (sheet row S-D-1)
  - D3: the new rows are owned by S2-b5-fix-plan and S2-b5-process-code, while B5-FIX-5 stays with S2-b5-finalfix2 (DEC-0125 wants one fix for the dimension).
  - D4: the proposed severities. MEDIUM for B5-UR-17, -20 and -22 (all fail-open, confirmed); LOW for the rest.
  - D5: S2-b5-process-code's plan text (:7185) names only the pre-commit gate, not install-hooks.sh or git-pre-push.sh.

## Item 4: text rounds (workspace only; one fix round and one re-review each, residuals to the owner)
- PC: fix round 2, then re-review b5r17-rr2-precommit.md (spec ❌ quality ❌ claims ✅).
  - R3-PC-1 and -2 MEDIUM; R3-PC-3..7 LOW.
  - Q-PC-11 is still open. New questions: Q-PC-24..27, plus Q-PC-28 proposed.
- FH: fix round 2, then re-review b5r17-rr2-helper.md (spec ❌ quality ❌ claims ✅).
  - R3-FH-1 MEDIUM: Q-PC-17 (b) and Q-PC-1 (c), both approved sheet answers, conflict on the export mode. R3-PC-5 found the same conflict.
  - R3-FH-2 MEDIUM; R3-FH-3..6 LOW.
  - Open: Q-FH-6, -18, -10, -11, and the new -25 and -26.
- SG: fix round 2, then re-review b5r17-rr2-skeleton.md (spec ❌ quality ❌ claims ✅).
  - R3-SG-1..9 (R3-SG-3 MEDIUM).
  - Open: Q-SG-3 and -8. New: Q-SG-22..28, plus Q-SG-29 proposed.
  - Q-SG-12 (b) was answered without a cost shown to the owner; the cost has been added since.
- Flagged sheet rows (A1-1b, Q-FH-10, Q-FH-11, S-D-1): b5r17-flag.md, 5 rows including the new S-D-2.
  - Proposed: Q-FH-11 SETTLED by Q-D32-W clause 1; S-D-1 SETTLED, keep B5-UR-12 (= A1-3 (a)).
  - Review b5r17-flag-rev.md: spec ❌ (F1 above), quality ❌ (F2-F6 LOW).
  - The flag fixer also points out a DEC-0129 gap: rows that were not flagged but read "no recommendation" (Q-FH-6, Q-PC-11, Q-SG-3, Q-SG-8, Q-FH-18) got no answer.
- R-r15-1's shared write-target rule was withdrawn in b5r17-fix2-common.md, since its quote of DEC-0093 was false.

## D36 compliance (each child's minutes budget ended by 22:03; durations are the harness counts)
| dispatch | budget min | harness | end by |
|---|---|---|---|
| docs drafter | 45 | 17 min | 20:24 |
| item-2 implementer | 60 | 5 min | 20:43 |
| item-3 review / rr / rr3 | 35 / 25 / 15 | 11 / 10 / 6 min | 20:25 / 20:27 / 20:30 |
| PC, FH, SG fix rounds | 40 | 14, 8, 11 min | 20:30 |
| flagged-rows fixer | 40 | 8 min | 20:30 |
| docs review / fix / rr | 45 / 35 / 30 | 13 / 13 / 11 min | 20:47 / 20:56 / 21:07 |
| FH, SG, flag, PC re-reviews | 30, 30, 25, 30 | 8, 9, 7, 8 min | 20:34 |

## D30
No code fix line ran this round, so the code trigger did not fire. Process signal for the owner: every text round still produces new findings.
- R3 findings on the tables (after the R1 and R2 rounds).
- R2 findings on the docs draft.
- My own register diff needed three review rounds: the grep scope, then a false sentence, then a false clause.
- The round-15 D30 investigation stands; I dispatched no new investigator.

## gates
- PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<fresh> python3 tools/gates/run_all.py at ec1c3c5: rc 0, 208 PASS, 0 other.
- git status --short: empty.

## claims (main ones; the commands and outputs are in the progress.md lines)
| claim | direct | contradiction | result |
|---|---|---|---|
| gate_plan's four git helpers read a failing git as data | shim git exits 128 for one subcommand: None / set() / None / True | real git: bytes / {'strict-delivery'} / 100644 / False | PASS |
| git-pre-commit.sh fails open when git status fails | shim on status: rc 0, no FAIL line | real git: rc 1, FAIL pre-commit.unstaged | PASS |
| pre-push :18 skips the gates when git fails | deletes-only push with failing git: 0 PASS lines | real git: 10 PASS lines | PASS |
| a gate_plan OSError is reported, not a defect | no git on PATH: FAIL plan.input [Errno 2] | with git: 0 non-PASS | PASS |
| the committed diff is the reviewed diff | cmp of the staged diff with the r3 patch: equal | rr3: r2→r3 holds only the 9 stated hunks | PASS |

## findings raised
- B5-UR-16..23, registered in ec1c3c5.
- Not yet registered, for the owner:
  - U1 docs/power-of-10-checklist.md:48-49 says "-Wconversion non-fatal", but the builds use -Werror (docs drafter; confirmed by the reviewer).
  - B5-CAPS-2's text becomes false once item 2 lands.

## agents and jobs
- All children exited: drafter a185af0, implementer a570b20, item-3 reviewer a1140ab (resumed twice), fixers a63d695, a8e44e5, a56e25b, ad57f1b, a468d7c, reviewers aab3178 (resumed once), a259a3b, a299ee3, a26a1b1, a2fd984.
- My background wait job b8u2prmgb: stopped with TaskStop.
- A child's background grep bum8xr8vn: stopped by that child.

## denials
- Docs reviewer (b5r17-docs-rr.md:50), verbatim: "PreToolUse:Bash hook error: [python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py"]: FAIL hook.bypass: --no-verify after a gated git verb (--no-verify; -n is --no-verify for commit, --dry-run for push)".
  - It was a scratch-clone commit. The reviewer re-ran it without the flag, then turned commit signing off for one clone command.
  - Owner question: was that "another way" around a denial? The checkout was not touched.
- None of my own.

## open items (owner)
1. Item 1: approve the draft or mark rows, covering Q-doc-1..21 and the residuals R2-1..7. Then the text commit (D34 plus a review of that exact diff) and the ~/.claude prompt install.
2. Item 2: Q-r17-1..3, before any code.
3. Item 3: D1-D5, and severity and owner-task confirmation for B5-UR-16..23.
4. Item 4:
   - The R3 residuals of PC, FH and SG, and the flag-review F1-F6.
   - The Q-PC-17 (b) versus Q-PC-1 (c) conflict.
   - The DEC-0129 no-recommendation gap.
   - The new Q-PC-24..28, Q-FH-25/26 and Q-SG-22..29.
5. Not started: B5-PROC-2 code, D36 install, Task 7 (all forbidden this round).

## budget
About 70 of 180 minutes. My own context: about 300k tokens.
