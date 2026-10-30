status: DONE_WITH_FINDINGS
verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R3-PC-1 (PC.15's new verdict contradicts PC.60 for its own stated value). R3-PC-2 (applying Q-PC-22 (a) needs a rule it does not state: the export's copy cannot write the export, so some other code must; no cell or question covers this). R3-PC-5 (two approved answers point Q-FH-18 in opposite directions; the draft records no divergence).
- quality ❌: R3-PC-3 (PC.58 values name a variable git does not pass and leave out two it does), R3-PC-4 (Q-PC-26's D33 siblings are not named), R3-PC-6 (no P3 row applies Q-PC-19 (a)), R3-PC-7 (two stale conditionals on answered questions).
- claims-executed ✅: every R2-PC fix, every sheet citation (39 quotes, checked by script and by a mutation of that script), and every changed today cell (W1, PC.46, PC.48, plus the Q-PC-25 premise) were re-run at 2e43f13 in my guarded clone. Details below.

## commits
None. The only file I wrote outside the clone is this report.
Clone: `R=/private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r17-rev/b5r17-rr2-precommit`, `$R/repo` at 2e43f13, branch rrprobe2, hooks installed. `$R/guard.sh` asserts that `pwd -P` and the toplevel both lie under `$R`. Every Python run used `PYTHONDONTWRITEBYTECODE=1` and a fresh `PYTHONPYCACHEPREFIX` under `$R`. Other probe repos: `$R/envrepo`, `$R/envwt`, `$R/exp`, `$R/wt`, `$R/exp1`, `$R/exp2`. The main checkout is clean (`git status --porcelain --untracked-files=all | wc -l` -> 0) and its HEAD is 2e43f13.

## gates run
- clone, `/usr/bin/python3 tools/gates/run_all.py` -> rc=0, PASS 100, FAIL 0.
- clone, venv `python3 tools/gates/run_all.py` -> rc=0, PASS 100.
- clone, `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` -> `Ran 658 tests in 161.701s OK (skipped=1)` (load 3.22).
- clone, venv `-W error -m unittest discover ...` -> `Ran 658 tests in 173.448s OK (skipped=1)`
- main checkout, `python3 tools/gates/run_all.py` (read-only, prefix under `$R`) -> rc=0, PASS 208, FAIL 0.

## Check 1: each R2-PC fix
| finding | direct | contradiction | verdict |
|---|---|---|---|
| R2-PC-1 | The Q-PC-19 row reads "Related to Q-D32-W, not equal (R2-PC-1)". PC.28 cites "DEC-0129 sheet Q-PC-19 (a) "require mktemp's fresh 0700 directory"". The rows column is now `PC.27, .28, .70, W4`. `grep '^\| PC\.' \| grep -c Q-PC-19` -> 3 (PC.27, .28, .70). | The pre-round copy had "Q-PC-19 \| = Q-D32-W". Every row in the column now cites the id. | holds. Residual: R3-PC-6 (the question names the prefix directories, and no P3 row cites it). |
| R2-PC-2 | `grep '^\| PC\.' draft \| grep -c 'no change for /tmp'` -> 0. PC.36 and PC.70 quote "below the root, plus a path argument that is itself a symlink". That is a substring of decisions.yml :867 (`sed -n 861,870p`). | The words "no change for /tmp paths" appear at :865 only, in the cost of option `below-root`. The answer at :869 is `below-root-plus-args`. | holds |
| R2-PC-3 | S1 plan `sed -n '5800p;5806p;6617p'` gives the test name, the assertIn and the refusal line, all verbatim as PC.66 cites them. Clone `sed -n 16,19p install-hooks.sh` -> `*) echo "install-hooks: refusing: the hooks directory $hooks (core.hooksPath?) is outside $common; ...; exit 1 ;;`. | The sheet's Q-PC-23 row is SETTLED and selects "a (keep)". PC.66's expected value is the refusal, so it matches. | holds |
| R2-PC-4 | PC.67 (b) reads "that interpreter's commands FAIL `pre-commit.input` naming the path and the others still run". Spec :46 holds "in a multi-item run one item's input failure FAILs that item and the others still run". | The pre-round PC.67 had no (b) value. | holds |
| R2-PC-5 | PC.19 cites "DEC-0129 sheet Q-PC-4 (a) "export raw blobs with attributes and filters pinned off"" and restates no other option. | Sheet Q-PC-4 recommendation `a` (script REC check -> 0 mismatches). | holds |
| R2-PC-6 | Clone W1: `git ls-files -s \| awk '{print $1}' \| sort \| uniq -c` -> 704 100644, 15 100755, 1 160000. `git ls-tree -r` mode counts: f1057e5 701/15/1/0, 6a7b2f9 702/15/1/0, 2e43f13 704/15/1/0. These equal the draft. PC.48: 3.9 suite 161.7 s at load 3.22 (draft 149.0 s at 3.06), run_all rc=0, PASS 100. | `git diff --stat 6a7b2f9 2e43f13 -- tools .github \| wc -l` -> 0, so the 6a7b2f9 code cells still apply. | holds (the times are within load variance) |
| R2-PC-7 | PC.36 (c) reads "an existing target or a used directory that cannot be read (mode 000)" -> FAIL `pre-commit.input`, cited to D32 "unreadable" and SC-1. | Spec :46 begins "an input or write target that is unreadable"; the sheet's SC-1 row is SETTLED for that clause. | holds |

## Check 2: sheet answers (quote, option, no rule beyond it)
- Script `$R/q.py`: for every `DEC-0129 sheet <id> (<opt>) "<quote>"` in the PC rows and the D28 paragraph, it checks that the option equals the sheet row's recommendation and that the quote (split at `...`) is a substring of that row.
  - Direct: 39 quotes, 0 bad.
  - Contradiction: in a copy with PC.38's cite mutated to `(b) "no checkX"`, the script printed `48 REC-MISMATCH Q-PC-8 b rec= a` and `48 QUOTE-MISS`.
- `grep '^\| PC\.' \| grep -c 'OPEN Q-PC-(1..10\|12..23)'` -> 0, so no answered id still reads OPEN in a row.
- The flagged rows A1-1b, Q-FH-10, Q-FH-11 and S-D-1 are cited only in the Fix round 2 header, which says they are not cited (grep).
- The sheet's sha256 is `8bef9f62...0d299839b`, equal to DEC-0129's question (decisions.yml :1373).
- Verdict per cell: each cell's verdict is the one its option gives, except PC.15 (R3-PC-1), PC.72 and PC.60 (R3-PC-2) and PC.68 (R3-PC-5).
- PC.64 joins Q-PC-14 (a) ("covered by pre-push/CI") with Q-PC-12 (c) (pre-push runs no D34 set) into "covered by CI". I checked this as entailed: under (c) pre-push adds nothing to the D34 set, so it is not a rule beyond the answers.

## Check 3: open and new questions
- Q-PC-11: the sheet reads "no recommendation", so it is OPEN under DEC-0129's option text. It has costed options. Real.
- Q-PC-24 is real.
  - Direct: clone `git worktree add $R/wt -b wbranch2`. `git clone -q --shared $R/wt exp1` -> `wbranch2`. `git clone -q --shared <common dir> exp2` -> `rrprobe2`.
  - The option chosen, Q-PC-1 (c), names no source path, and D34 :48 names none either. It has two costed options.
- Q-PC-25 is real. Option (a)'s text lists five variables and does not say whether the rest are inherited.
  - Direct: `PYTHONNOUSERSITE=1 /usr/bin/python3 -c 'import yaml'` -> `ModuleNotFoundError: No module named 'yaml'`.
  - Contradiction: without it -> `~/Library/Python/3.9/lib/python/site-packages/yaml/__init__.py`.
  - It has three costed options.
- Q-PC-26 is real. The sheet's own Q-PC-21 (d) why says "Not SETTLED: no plan line gives the git hook a comments staged run". S4 :1026 reads "For the hook's staged run pass `["--staged"]` as the `comments` argv (S1's per-gate argv mapping ...)". It has three costed options. Sibling gap: R3-PC-4.
- Q-PC-27 is real. Q-PC-22 asks about the git hook, and Q-PC-13 (a) fixes the Bash branch's set, not its code root. pre_tool_use.py :76 and :327 are unchanged at 2e43f13 (diffstat 0). It has two costed options.
- Q-FH-18 and Q-SG-8: the sheet recommendations read "no recommendation: follows Q-PC-1 ... and Q-PC-17", so both stay OPEN. Q-FH-18 carries the divergence in R3-PC-5.
- Question rows in the table: 29 (awk count), as the fixer reports.

## Check 4: changed today cells at 2e43f13
- W1 and PC.48: see R2-PC-6 above.
- PC.46: see Q-PC-24 above. It matches the draft, with my branch names.
- PC.66's refusal text: see R2-PC-3.
- PC.57 and Q-PC-25: see above.
- PC.58 (expected cell changed): probe repo `$R/envrepo`, a pre-commit hook that logs `env | grep ^GIT_`. Results:
  - plain commit: `GIT_INDEX_FILE=.git/index`.
  - `commit -a`: `.../.git/index.lock`.
  - pathspec commit: `.../.git/next-index-24010.lock`.
  - commit from a linked worktree: `GIT_DIR=.../.git/worktrees/envwt` plus that worktree's index.
  - every commit made with `-c`: the config-parameters variable `'commit.gpgsign'='false'`.
  - No object-directory variable appears in any case.
- Inside a `clone --shared` export with the inherited index-file variable, `git ls-files -s` lists the real repo's index (f, g), not the export's (f only). So the PC.58 verdict holds only if the variables are cleared. See R3-PC-3.

## Findings (R3-PC-<n>)
- R3-PC-1 MEDIUM: PC.15 vs PC.60, a new verdict that contradicts a settled row.
  - PC.15 values: "`git init` repo, hooks installed, one file staged". Its expected cell is new this round: "accepted: the set runs on the export" (DEC-0129 sheet Q-PC-6 (a)).
  - PC.60 is settled by S1 plan :5344 "A tree without the script → `FAIL pre-commit.missing`" (checked with python `in` -> True). With Q-PC-22 (a), the copy that runs is the export's.
  - An index holding one file has no hook script, so for PC.15's stated value the export has no script and PC.60 gives FAIL `pre-commit.missing`. The sheet's own Q-PC-6 c text admits an unborn repo may have "no tools to run". The today cell (rc=1 `FAIL pre-commit.missing`) agrees with PC.60, not with PC.15's verdict.
  - Replacement (hypothesis): split PC.15. An unborn HEAD whose index holds the gate tree is accepted (Q-PC-6 (a)). An unborn HEAD whose index lacks the hook script gets FAIL `pre-commit.missing` (S1 :5344, PC.60). If the controller reads Q-PC-6 (a) as overriding :5344, that is a divergence for the owner, not for the drafter.
- R3-PC-2 MEDIUM: PC.72 and PC.60, a rule Q-PC-22 (a) does not state.
  - Q-PC-22 (a): "the export's copy (the hook judging a commit is the committed hook; ...)". PC.72: "the export's copies of the hook script and of run_all run". The surface (draft :3) says the hook itself "writes a fresh export".
  - The export's copy cannot write the export it lives in. So either the installed shim (install-hooks.sh :41-46 today: `script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-@HOOK@.sh"`, `exec sh "$script"`) or the work-tree copy must read the index and write the export before the export's copy runs. That code is not the committed code.
  - `grep -n -i 'bootstrap\|before the export\|writes the export'` over the draft and the sheet -> no row or question.
  - Proposed new question, Q-PC-28: "Under DEC-0129 sheet Q-PC-22 (a), which code reads the index and writes the export before the export's hook copy runs?" Options:
    - a: the installed shim makes the export itself (installer text change; the shim is fixed at install time and needs a re-install when it changes).
    - b: the work-tree copy makes the export, then execs the export's copy (the work tree's code runs at every commit; a broken work-tree hook blocks commits).
    - c: the shim runs the index blob of the hook script (`git show :tools/gates/hooks/<hook>`) and that copy makes the export (no work-tree code runs; a new shim form, about 2 tests).
  - The owner decides. PC.60's `[ ! -f "$script" ]` check then reads the export or the index, per the answer.
- R3-PC-5 LOW: PC.68, PC.17 and the cross-table line, a divergence between two approved answers that the draft does not record.
  - Q-PC-17 (b), whole option text: "a thin shell shim exec'ing a Python hook that uses the helper (Q-FH-18 must give an export root mode)". PC.68 quotes it without the parenthetical.
  - Q-PC-1 (c), sheet why: "c gives a toplevel (DEC-0068 a, ...) with no export mode".
  - Q-FH-18 (OPEN) row: "follows Q-PC-1 (its a = `worktree add`, then b) and Q-PC-17 (b needs an export mode)".
  - So the two answers point Q-FH-18 to a and to b. Report both quotes in the cross-table line and under Q-FH-18, and do not pick.
- R3-PC-3 LOW: PC.58's values do not match what git passes (re-run of the row whose expected cell changed).
  - The values name "the index-file and object-dir variables". Observed: no object-directory variable. Git does pass the git-dir variable for a worktree commit and the config-parameters variable for every `git -c ... commit` (see Check 4).
  - Replacement: the values list the index-file, git-dir (worktree) and config-parameters variables. The today cell records the `$R/envrepo` probe. The verdict stands as the D34 outcome.
- R3-PC-4 LOW: Q-PC-26's D33 siblings.
  - The same hook-staged-argv idiom is in S4 :1026 (comments), :1759 ("The hook's staged argv for `tools_py` is `["--staged"]`") and :2143 (bench, "hook staged argv `["--staged"]`"). Found with `grep -o "hook[^.]{0,30}staged[^.]{0,40}"` over S4.
  - Q-PC-26 names comments only. §8 :230 names comments and p10 only, so tools_py and bench are not required on staged files by §8. The question should still say whether its answer reaches them (D33: list every hit).
- R3-PC-6 LOW: there is no P3 (prefix directory) row for Q-PC-19 (a).
  - The question names "export and prefix directories". PC.28 (P2) applies "fresh 0700" and PC.27 the realpath.
  - `grep '^\| PC\.3[2-6] ' \| grep -c 0700` -> 0. Only W4 carries the rule for both. P2 has a permissions row; P3 has none (a missing row).
- R3-PC-7 LOW: stale conditionals on questions that are now answered.
  - N/A list :89: "pre-push HEAD-index path (`read-tree HEAD`, unchanged unless Q-PC-12 widens it)". Q-PC-12 is answered (c), so it does not widen: cite it.
  - Q-PC-4 note row :113: "Q-PC-4 stays open as asked". Its own id cell now reads SETTLED, and Q-PC-4 is ANSWERED.

## Unrelated findings
None new. I did not re-run UF-R2-PC-1 or UF-PC-R1.

## denials
None.

## background jobs
- bwfpq46vp (run_all on both interpreters, then the 3.9 suite): exited 0 with the results above.
- bpwe356gj (the 3.12 suite): exited 0, the result above.

## budget
About 95k of 250k tokens and about 25 of 30 minutes (my estimate; the harness count is authoritative).
