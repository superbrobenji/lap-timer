# S2-b5-caps re-review (b5 round 15)

Pin in this file's first commit: B5-CAPS-1, reviewed CLEAN at 6a7b2f9 (registers at e17dc90) by the round-15 task reviewer (opus). Pin test test_gate_dispatch::test_budget_caps_follow_dec_0060; transcript docs/superpowers/state/strict-delivery/mutations/B5-CAPS-1.txt (replayed OK / FAILED / OK); kill check 12/12 config mutants. B5-UD-1 and B5-RD-1 fixes CLEAN; their pin form is owner question DEC-0103 (not pinned here).

## Report (b5r15-rr-caps.md, verbatim)

status: DONE

Verdict. B5-CAPS-1: CLEAN, pin: yes (evidence in checks 1-3). B5-UD-1 fix: CLEAN. B5-RD-1 fix: CLEAN. I make no call on the pin form of B5-UD-1 or B5-RD-1; that is an owner question the controller carries. 6a7b2f9 caused no defect. No R-rc15 finding raised.
spec: ✅ (class closed at 6a7b2f9; every old-cap hit is classified below) quality: ✅ (D3 blocks EQUAL; 12/12 config mutants killed) claims-executed: ✅ (all 8 claims.md rows re-run: PASS)

## commits
Reviewed: 6a7b2f9 fix(gates): B5-UD-1, B5-RD-1 -- suite green (section 12 count, DEC-0063 drops the caps text guards); register B5-CAPS-2.
Registers read at HEAD e17dc90 chore(state) (`git diff --stat 6a7b2f9 HEAD`: 2 files, decisions.yml and findings.yml, 1058+).
I made no commit.

## Setup and guard
Every script uses `set -eu` and a case-assert that `pwd -P` and `git rev-parse --show-toplevel` both lie under scratchpad/r15-rr-caps/.
Clones (all `git clone -q --no-hardlinks`):
- repo: branch tip.
- c6: checked out at 6a7b2f9.
- cf: checked out at f1057e5.
I wrote nothing to the checkout except this report.

## Check 4: the D34 set at 6a7b2f9 (clone c6)
- run_all (PYTHONDONTWRITEBYTECODE=1, PYTHONPYCACHEPREFIX set to a fresh empty dir):
  - No packages: rc=0, 100 lines, 0 non-PASS.
  - S2-b5-caps package copied in: 110 lines, 0 non-PASS.
  - All 8 checkout packages copied in: 208 lines, 0 non-PASS. This confirms the commit's "run_all 208 PASS". The figure depends on the checkout, because .superpowers/ is gitignored (.gitignore:18).
- Suite (`-W error -m unittest discover -s tools/gates/tests -t .`):
  - /usr/bin/python3 3.9.6: "Ran 658 tests in 180.882s OK (skipped=1)".
  - tools/.venv/bin/python3 3.12.14: "Ran 658 tests in 179.151s OK (skipped=1)".
- D34 Order: `git log --oneline f1057e5..6a7b2f9` prints only 6a7b2f9. f1057e5 is the commit that lands D34: its diff adds 1 `| D34` row and 1 CLAUDE.md line.
- B5-UD-1:
  - Spec §12 (:273-288) has 13 `- ` bullets, and CLAUDE.md has 13. They are equal (python check: True).
  - S1 plan :6855 reads `self.assertEqual(13, len(bullets))`.
  - The S1 insert block :6695-6858 is a substring of test_hooks.py (True).
- B5-UD-1.txt replay: step 1 OK; step 2 (perl, 1 file 1+/1-) FAILED, "AssertionError: 11 != 13"; step 3 OK. This matches the transcript.
- Contradiction pass on the 13 (assume it is wrong):
  - The count set to 12 or to 14 FAILS.
  - A 14th bullet added to spec §12 only FAILS.
  - A 14th bullet added to both spec §12 and CLAUDE.md FAILS.
  - Restored: OK.
  - The test therefore pins exactly 13, and spec/CLAUDE equality as well.
- Changed plan count lines:
  - S2 :1472 says "78 and a suite of 658": the loader count of test_gate_dispatch is 78 at 6a7b2f9 and 80 at f1057e5. True.
  - S2 :6848 says "140 and a suite of 658": the loader count of test_gate_plan is 140. True.
- B5-RD-1: the dropped test no longer exists, so the fc759c9 text at S2 :7107 (was :7143) turns nothing red. Suite green (above).

## Check 1: B5-CAPS-1 whole class at 6a7b2f9
- config.yml :2-5 holds the DEC-0060 values: controller 3/500000/180, implementer 1/300000, reviewer 1/250000.
- gate_dispatch._budget (:200-218) reads `conf["triggers"]` and is the only reader. It has no literals.
- Fixtures:
  - good_implementer.md:5 `tokens=300000 minutes=60`
  - good_reviewer.md:5 `tokens=250000 minutes=30`
- D3 (scratchpad/r15-rr-caps/bin/d3.py extracts each fenced block after its caption and diffs it against the file):
  - S2 :327-340 vs good_implementer.md: EQUAL.
  - S2 :345-357 vs good_reviewer.md: EQUAL.
  - S2 :364-1080 vs test_gate_dispatch.py: EQUAL (also EQUAL by `diff <(git show 6a7b2f9:plan | sed -n 364,1080p) <(git show 6a7b2f9:file)`).
  - S2 :1865-2082 (ConfigCheck append) is a substring of the file: True.
  - S1 :6695-6858 (test_hooks insert) is a substring of the file: True.
  - Umbrella :163-196 and S1 :955-988 vs config.yml: the only hunk is `34a35,37` (DEC-0018 fable_fallback comment + `fable_fallback: opus`). That is B5-UF-3, outside the triggers. The triggers lines are equal.
- Umbrella :165-167 and S1 :957-959 hold the new caps. S1 :7352 reads "3 tasks, 500k tokens, 180 minute...". S2 :20 and umbrella :20 hold the new caps. R-caps-1..3 hold.
- README: `grep -n -i -E 'trigger|\bcaps?\b|budget|[0-9]{3}k|[0-9]{5,}|minutes|hours'` on tools/gates/README.md and CLAUDE.md finds only README :24, :167, :175 and :181 (model map, the section name Budget, "the cap" = max_prompt_chars). There is no cap value.

Hit table: `git grep -E '250k|250 ?000|250,000|150k|150 ?000|150,000|120k|120 ?000|120,000|90 ?min|90-min|minutes=90|minutes: ?90' 6a7b2f9`, excluding docs/superpowers/state and docs/reviews.

| hit | class | stale? |
|---|---|---|
| config.yml:5; umbrella :167; S1 :959; S2 :20; umbrella :20 | in: new reviewer cap | no |
| S2 :349 and good_reviewer.md:5; S2 :691 and test_gate_dispatch.py:328; S2 :699/:705 and test :336/:342 | in: new cap or budget at the cap | no |
| S2 :700 and test :337 | in: old caps as must-PASS input data in the pin test | no |
| S2 :7107 (was :7143) budget inventory `tokens=150000 minutes=45` | in (plan text): a budget value under the cap | no |
| S2 :7108 WB.2 whitelist | in: new caps | no |
| S4 :4064, :4311 `const BUDGET` | budgets, not caps (as round 13) | no |
| S5 :925, :1144, :1175 | `## Budget:` values under the caps | no |
| spec :69-71 | new caps, with "(DEC-0060; was ...)" history | no |
| spec :36 D22 "250k tokens / 90 min ... 150k ... 120k" | outside the class as written | stale. Registered as B5-U9 at HEAD (cites :36 @6a7b2f9; its repro holds) |
| git mode 120000 (S1 :1077, :3247, :6082; S2 :5905, :5970, :6653, :6659, :6681, :6920, :7168; gate_plan.py; gitinfo.py; tests) | out | n/a |
| firmware literals (components/, test/, devcontroller/, tools/replay, partitions.csv, plan-01/03/5.6/F0/F1a, lap-timer spec) | out | n/a |

The hit set equals round 13's minus the dropped lines (S2 :741, :753), with the plan line numbers shifted by -36.

## Check 2: R-caps-4 (DEC-0063 drop, DEC-0091 same-d3-row)
- `grep -cE '^    def (test_s2_plan_blocks_hold_the_dec_0060_caps|test_readme_states_no_budget_cap)\('` gives:
  - At 6a7b2f9: file 0, S2 plan 0.
  - At f1057e5: 2 and 2.
- siblings.md rows 4-5 cite B5-CAPS-2. Rows 1-3 name test_budget_caps_follow_dec_0060, test_controller_budget_caps and test_budget_over_role_cap_fails; all 3 exist and run OK.
- B5-CAPS-2 cite: test_gate_dispatch.py:362-397 at f1057e5 is exactly the two dropped tests (362 `def test_s2_plan_blocks...`, 396 the last assert, 398 `def test_zero_budget_fails`).
- B5-CAPS-2 repro in c6, with the package present:
  - Plant 1: `budget="tokens=250000 minutes=90"` replaces the new line. Counts: old 1, new 0.
  - Plant 2: "250k" appended to README.
  - Result: run_all rc=0, 110 lines, 0 non-PASS; 3.9 "Ran 658 OK (skipped=1)"; 3.12 "Ran 658 OK (skipped=1)".
  - Only the D3 diff (d3.py: S2 :364-1080 DIFF) shows the plant.
  - The repro holds.

## Check 3: package S2-b5-caps (copied into c6)
- Regenerated over 72b7018..6a7b2f9 (R-rc13-2 closed):
  - commits.txt equals `git log --format='%h %s' --abbrev=12 72b7018..6a7b2f9` (9 commits).
  - diff.patch is byte-equal to `git diff --abbrev=12 -U10 72b7018..6a7b2f9`; it differs from the 72b7018..f1057e5 range.
- gates.txt has 99 lines, all PASS: the bare run_all without the review_package line. All 5 b5 packages use the same form; S2-T6 has 98 lines.
- `gate_review_package.py --dir` prints only PASS (11 lines, including mutation.B5-CAPS-1), rc 0. Contradiction: with claims.md removed it prints "FAIL review_package.file.claims.md: ... missing or empty".
- Mutation replay: mutation-B5-CAPS-1.txt is cmp-equal to docs/superpowers/state/strict-delivery/mutations/B5-CAPS-1.txt. Step 1 OK; step 2 (perl, `git diff --stat` 1 file 3+/3-) FAILED (failures=30); step 3 OK.
- Kill check, pin test test_budget_caps_follow_dec_0060. Each mutant is one config.yml value, 1+/1-:

| mutant | value | result |
|---|---|---|
| M1 | ctl tokens 250000 | FAILED 3 |
| M2 | ctl tokens 500001 | FAILED 2 |
| M9 | ctl tokens 499999 | FAILED 3 |
| M3 | ctl minutes 90 | FAILED 3 |
| M4 | ctl minutes 181 | FAILED 2 |
| M10 | ctl minutes 179 | FAILED 3 |
| M5 | impl 150000 | FAILED 6 |
| M6 | impl 300001 | FAILED 2 |
| M11 | impl 299999 | FAILED 6 |
| M7 | rev 120000 | FAILED 21 |
| M8 | rev 250001 | FAILED 7 |
| M12 | rev 249999 | FAILED 21 |

  12/12 killed; the counts equal b5r12-impl-report.md:37.
- Round-15 addition (a), the dropped M1-M12 row: this is not a finding, for three reasons:
  - claims.md lists the claims the package makes, and the regenerated package no longer makes the M1-M12 claim.
  - The pin form (CLAUDE.md "production-path test + mutation transcript", gate_review_package) is met by the transcript.
  - Spec D27's mechanical mutation is "gate from b6".
  The re-run above now provides the 12-mutant evidence. Ledger it at the pin. If the controller reads D27 as binding on b5 packages, carrying the row is a spec-reading question for the owner, not mine to settle.
- claims.md rows re-executed:

| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| suite 658 OK (skipped=1) on both interpreters | suites above | at f1057e5 on both interpreters: the two red tests (plus readme guard) give "Ran 3, FAILED (failures=2)" (test_s2_plan_blocks: 'tokens=150000 ' found; test_hooks: 11 != 13); loader count 80 vs 78 | 658 OK x2 | PASS |
| run_all 208 lines all PASS with the package | run_all with all 8 packages | grep -vc '^PASS' | rc 0, 208, 0 | PASS |
| pin test fails on old triggers | test OK | transcript replay | OK / FAILED 30 / OK | PASS |
| siblings rows 1-3 tests exist and pass | 3 tests run | review_package --dir | OK; PASS | PASS |
| siblings rows 4-5 cite B5-CAPS-2, whose siblings are those 2 cells | siblings.md; findings.yml | dropped names in siblings.md: 0 | holds | PASS |
| DEC-0063 drop; D3 :364-1080 equal | greps 0/0; diff empty | f1057e5: 2/2, :364-1116 diff empty | holds | PASS |
| B5-CAPS-2 repro green | check 2 | plants are real (counts) | green x2 | PASS (I did not re-run the old guard on the plant) |
| gate_plan --in-flight and gate_dispatch --config-check only PASS | 69 lines, 0 non-PASS; 0 non-PASS | see note below | 0; 0 | PASS |

Note on the last row's contradiction run: setting the controller tokens to -1 still gives config-check only PASS. That is known and registered as B5-U7 (trigger validation), with B5-U8 for the tasks/reviews keys. Not new.

## Check 5: live claims
- Old caps: only spec :36 D22 is a live stale statement (B5-U9).
- B5-UF-3 (fable_fallback missing from the umbrella/S1 config blocks) is confirmed by D3 and lies outside the triggers.
- Dropped test names: hits are only in spec :48 (the D34 Order, describing how the single red commit was handled: historical), decisions.yml :585/:844/:848/:901 (questions) and findings.yml rows B5-RD-1/B5-CAPS-2 (history and class). There is no live claim that the tests exist.

## findings raised
None (no R-rc15-n).
- Unrelated: none new. B5-U7, B5-U8, B5-U9 and B5-UF-3 are re-confirmed.
- No D30 trigger is raised from this review.

## denials
None.

## background jobs
None started.

## budget used
About 40 of 45 minutes; about 110k of 250k tokens.
