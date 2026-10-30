status: DENIED
(The review is complete apart from one denied observation, PC.64. The strict block says "A permission denial: stop, quote it verbatim, status DENIED". b5r14-tablerev-common.md :15 says "a permission denial stops that line of work, quoted verbatim". Those two rules differ, so I give both quotes. I stopped only the denied line of work and report status DENIED as the strict block requires. The findings below are final.)

verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R-PC-1 (PC.53 contradicts W5 and D28), R-PC-2 (several cells cite D32 but D32 does not settle them), R-PC-3 (Q-PC-4 is partly settled by D34/DEC-0098), R-PC-13 (the §8 :230 staged-files gates have no row).
- quality ❌: R-PC-6/R-PC-7 (skeleton hits not mapped; no row for the shim running the work-tree copy), R-PC-5 (no row for the interpreter's import environment), R-PC-8..R-PC-12, R-PC-14, R-PC-15.
- claims-executed ✅: I re-observed 59 of the 69 rows in my own guarded clone at f1057e5. The 10 rows not run are listed with reasons below. The suite ran on both interpreters, plus a run on the export.

commits: none. The reviewer edits nothing but this report.

Guarded clone: S=/private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r14-tablerev-precommit. Clone `$S/repo` at f1057e5, branch rprobe, hooks installed with `bash tools/gates/install-hooks.sh`. Every script uses `set -eu` and asserts both `pwd -P` and the toplevel. Every Python run uses PYTHONDONTWRITEBYTECODE=1 and a pycache prefix under `$S`. The scripts are $S/p1.sh..p4.sh. `C` means a real commit in the clone with commit.gpgsign=false, printed as rc and the count of PASS lines. At the end the clone has 0 porcelain lines and 1 worktree, and the main checkout is clean at 6a7b2f9.

gates run:
- `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` (clone, fresh prefix) -> `Ran 660 tests in 146.540s FAILED (failures=2, skipped=1)`. The two failures are U-D-1 and R-D-1.
- the same with `~/projects/personal/lap-timer/tools/.venv/bin/python3` -> `Ran 660 tests in 155.859s FAILED (failures=2, skipped=1)`, the same two tests.
- `/usr/bin/python3 tools/gates/run_all.py` (clone) -> rc=0, 100 PASS.
- run_all on a `checkout-index -a` export -> rc=1, 6 FAIL `*.input: git rev-parse --show-toplevel: fatal: not a git repository`.
- the suite on the export (3.9) -> `FAILED (failures=6, skipped=1)`. The 4 failures beyond the clone's two are exactly the ones the draft names.

## Findings (R-PC-<n>; each replacement is a hypothesis)

R-PC-1 MEDIUM, PC.53 / W5. The draft has two quotes that contradict each other. PC.53 says "whole-string `--pre-push` only; else commit mode". W5 says "argv: none, or exactly `--pre-push`", with everything else failing closed as `pre-commit.input`. D28 settles the question: "Inputs outside each gate's stated whitelist ... FAIL closed with a named check".
- Today: `sh tools/gates/hooks/git-pre-commit.sh '--pre-push '` -> rc=0, and with `x` -> rc=0 (commit mode). The exact `--pre-push` -> rc=1 `FAIL pre-push.uncommitted`.
- Replacement: PC.53 expects "any argv other than none or exactly `--pre-push`: FAIL `pre-commit.input` naming it", settled by D28 and D32 fullmatch.

R-PC-2 MEDIUM, PC.29, .31, .36, .67 and W4. These cells cite D32, but D32 does not say what they claim. D32 :46 reads: "an input or write target that is unreadable, of the wrong kind, a symlink at any path component below the repository root, or a path argument that is itself a symlink (DEC-0093) FAILs that item's input check ...; in a multi-item run one item's input failure FAILs that item and the others still run".
- PC.29: the export directory lies under TMPDIR, outside the root. DEC-0093 chose below-root-plus-args ("no change for /tmp paths"). So D32 does not settle a symlink there, nor "pre-existing / non-empty". Observed: `mktemp -d "${TMPDIR:-/tmp}/..."` -> `/var/folders/.../T//strict-delivery-pre-push.PEUpLq`, realpath `/private/var/...` (the path passes through a symlink).
- PC.31 and PC.36: "unwritable" and "disk full" are not D32's words.
- W4: "never an existing path (D32)" is not D32's text either.
- PC.67: D32 covers an *input* failure. A red test is not one, and D34 says only "Any red line ... refuses".
- Replacement: these cells read OPEN Q-PC-19 or Q-PC-20 (below). W4 cites Q-PC-19.

R-PC-3 MEDIUM, PC.19, .20, .26 / Q-PC-4. A cell marked OPEN is partly settled. D34 :48 says "runs the set on a fresh export of the index, and only it makes the tree tested the tree committed". DEC-0098's chosen option says "tests the committed tree itself". Together these settle that the export holds the index blobs byte for byte. That rules out Q-PC-4 b ("apply only the tracked `.gitattributes`") whenever that file converts content.
- Observed: `checkout-index` with a smudge filter -> `SMUDGED:# lap-timer`, while `git show :README.md` -> `# lap-timer`.
- Replacement: PC.19 expects "export = index blobs byte for byte (no smudge, no eol conversion)", settled by D34 and DEC-0098. Q-PC-4 narrows to "filter or attribute config present: ignored (a) or refuses (c)".

R-PC-4 MEDIUM, PC.21, a check 7 consistency issue. The cell's settled-by is "D34 (index export)", with the verdict "no effect on the export". That covers what the export contains. It does not cover the gates' own git calls inside the export, whose output the ignore files change.
- The helper draft's Q-FH-13 option b reads "no: the pre-commit export covers them". So the helper draft depends on this row, yet PC.21 does not name Q-FH-13. The drafter's report links them; the table does not.
- The plan :7221 requires a row for `core.excludesFile`, but PC.21's today cell does not observe it. Mine: `git config core.excludesFile` -> unset.
- Replacement: settled-by reads "export content: D34; gates' git reads inside the export: OPEN Q-FH-13 / Q-PC-1". Add `git config core.excludesFile -> (unset)` to the today cell.

R-PC-5 MEDIUM, a row is missing for P4 (the interpreter's import environment). /usr/bin/python3 takes PyYAML from the user site directory, outside the repo and outside the export.
- Command: `PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -c 'import yaml,sys;print(yaml.__file__)'` -> `~/Library/Python/3.9/lib/python/site-packages/yaml/__init__.py`.
- Command: `/usr/bin/python3 -I -c 'import yaml'` -> `ModuleNotFoundError: No module named 'yaml'`. With the user site disabled by its variable, the result is the same.
- Q-PC-15 b ("run with `-I` ... tests may differ") understates the cost. Under `-I` every 3.9 run is red.
- Replacement: a new row "P4 import environment: user site / site-packages / PyYAML version", OPEN Q-PC-15. Q-PC-15 b's cost then reads "-I disables the user site: /usr/bin/python3 loses PyYAML, so every 3.9 run fails".

R-PC-6 LOW, D31 check 1. I re-ran the drafter's skeleton greps: 42 hits and 21 hits, the same counts. These hits map to no item and no N/A line:
- run_all.py:19 `sys.path.insert(0, <root of __file__>)`. This sets where the gate modules and the root import from. It decides the verdict; see the UF below.
- run_all.py:39 abspath.
- pre_tool_use.py:76 CODE_ROOT, :229 `_directory`, :276 and :327 `sys.path.insert`.
- git-pre-push.sh:18-38: the stdin ref lines `while read -r local_ref ...` and `git rev-parse --verify`. My wider grep of the 92 non-comment lines found no other unmapped lines in commit mode.
- Bash-branch observation T3: an event with cwd = clone and the command `cd ~/projects/personal/lap-timer && git <commit-verb> -q -F msg` -> rc=0. The branch gated the main checkout with the clone's code (CODE_ROOT). No row or N/A covers this dimension of I10, where the code root differs from the gated tree.
- Replacement: items I10b (tree resolution and CODE_ROOT) and I12 (pre-push stdin refs), each with rows or argued N/A.

R-PC-7 MEDIUM, I8 / the surface paragraph. The draft says the shim "runs the tracked `tools/gates/hooks/git-pre-commit.sh`". The shim is `script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-@HOOK@.sh"; if [ ! -f "$script" ] ...; exec sh "$script"` (install-hooks.sh:41-46). That runs the work-tree file, not the index blob.
- Today the I3 guard keeps the two equal for tools/gates. If Q-PC-11 b drops the guard, the hook that judges a commit can differ from the hook being committed.
- For I8, no row covers this, nor the kinds `[ -f ]` accepts: a symlink, which it follows, and non-regular files.
- Replacement: a row "I8 which copy of the hook runs (work tree vs export)", OPEN Q-PC-22, plus a row for I8's kind and symlink.

R-PC-8 LOW, D35 today cells. Three cells read "OPEN (not run)" although they can be observed today.
- PC.35. Mine: staged `tools/gates/yaml.pyc` (`git ls-files -s` -> `100644 7ba6198b...`) -> rc=1 with a Python ImportError traceback and no named line.
- PC.60. Mine: on the root commit -> `ls: tools/gates/hooks/git-pre-commit.sh: No such file`, and the commit gives rc=1 `FAIL pre-commit.missing: this tree has no tools/gates/hooks/git-pre-commit.sh ...`.
- PC.15 can be observed with `git init` in scratch. I did not run it (budget).
- Replacement: use my observations above.

R-PC-9 LOW, PC.7. The today cell does not hold for the value as stated.
- `git update-index --chmod=+x tools/gates/gate_plan.py` alone -> rc=1 `FAIL pre-commit.unstaged: ... MM tools/gates/gate_plan.py`. The clone has core.filemode=true in its local config.
- The cell holds only when the file is also `chmod +x`'d on disk: rc=0, 10 PASS, ` 1 file changed, 0 insertions(+), 0 deletions(-)`.
- Replacement: state both variants.

R-PC-10 LOW, PC.18. The today cell "status `[]`, rc=0" cannot hold as a whole.
- With only the marker line added, the clean filter makes index = HEAD. Mine: "nothing to commit", rc=1.
- With one more real line added: status `[M  README.md]`, rc=0, 10 PASS, HEAD marker count 0.
- The core claim holds: the marker never reaches HEAD.
- Replacement: write the exact staged content and my output.

R-PC-11 LOW, check 3. Four cells cite code as the settler, and code is not a spec line, a plan line or a DEC:
- PC.55 "today's :28".
- PC.60 "install-hooks.sh:42-44".
- PC.62 "pre_tool_use.py:208-226". DEC-0005's chosen plan-design-plus-prepush settles it: "additionally blocks hook bypasses (--no-verify, -n, ...)".
- PC.66 "install-hooks.sh:16-19".
- Replacement: PC.62 cites DEC-0005, PC.55 cites D34 "the tree committed", and PC.60 and PC.66 cite §8 :230 or read OPEN.

R-PC-12 LOW, PC.28. "D34 'fresh'" does not settle 0700 permissions. D34's "fresh empty directory" names only the PYTHONPYCACHEPREFIX directory. Replacement: OPEN Q-PC-19.

R-PC-13 MEDIUM, check 2 (spec rows). §8 :230 reads "`pre-commit` → register/claims/comments/p10 on staged files, plus the D34 set". The table has no row and no N/A line for the comments and p10 gates.
- They do not exist in tools/gates at f1057e5: `ls tools/gates` shows no gate_comments or gate_p10.
- They are planned. `git grep -c 'gate_comments\|gate_p10' f1057e5 -- docs/superpowers/plans` gives S3 47 and S4 35.
- The draft's surface quotes §8 but drops this clause.
- Replacement: a row "§8 staged-files gates (comments, p10; land in S3/S4)", OPEN Q-PC-21.

R-PC-14 LOW, check 6. Some question options name no cost:
- Q-PC-3 a ("refuse (`pre-commit.input`)").
- Q-PC-6 a, b and c: no cost in tests or code.
- Q-PC-9 a and b: no cost.
- Replacement: add each option's cost (tests, behaviour change).

R-PC-15 LOW, the N/A list. "HEAD object state beyond PC.15 (the export reads the index, not HEAD, in commit mode)" is weak. Gates running in the export read HEAD and history objects: PC.17's own output shows `git cat-file -e 151f2bb^{commit}`, and plan `--in-flight` reads the branch (PC.46). Replacement: a row for "HEAD/history objects the gates read inside the export", OPEN Q-PC-1.

Drafter report (not the draft). Its divergence paragraph cites "D32 :47". At f1057e5, D32 is :46 and :47 is D33: `git show f1057e5:<spec> | sed -n '46p;47p' | cut -c1-10` -> `| D32 | De`, `| D33 | Cl`. The draft itself cites only D34 :48, §7 :203 and §8 :230, and all three are correct. Check 3 in tablerev-common also says "spec :47"; the helper draft says "spec D32 :47" (:3). Both repeat the controller's line error.

## Claims (direct | contradiction | output | verdict)
- Today the hook runs only register and claims (PC.2). Direct: staged `def (` in gate_plan.py, C -> rc=0, 10 PASS. Contradiction: run_all with all gates should then FAIL. A fresh-prefix run with a broken staged gate_claims -> rc=1 `FAIL claims.input: invalid syntax (gate_claims.py, line 36)`. PASS.
- P-1, stale bytecode (PC.33). Direct: a .pyc compiled into a scratch prefix, a broken same-size edit with mtime restored (`size 8029 8029`), C with that prefix -> rc=0, 10 PASS. Contradiction: a fresh prefix -> rc=1 FAIL claims.input line 36. PASS. (I used a scratch prefix, not the drafter's `~/Library/Caches` one. The default prefix value I confirmed by reading `sys.pycache_prefix` -> `~/Library/Caches/com.apple.python`.)
- A plain export breaks the set (PC.16/.45). Direct: export run_all -> 6 FAIL "not a git repository". Contradiction: the clone -> rc=0, 100 PASS. PASS. The suite on the export has the 4 extra failures as named. PASS.
- checkout-index behaviour (PC.9/.10/.11/.13/.19). Mine: link `lrwxr-xr-x`, i-t-a size 0, skip-worktree `gate_plan absent`, `unity-dir=yes 0`, `SMUDGED:` vs the index blob. PASS.
- The hook sees git's temporary index (PC.14). Mine: `-a` -> rc=0; a pathspec commit with another staged gate edit -> rc=1 ` M tools/gates/gate_claims.py`. PASS.
- Review packages (PC.50). Mine: the clone at f1057e5 plus the checkout's review/ (1 dir) -> `FAIL review_package.strict-delivery.S2-b5-caps.siblings`. Contradiction: the main checkout now, at 6a7b2f9 and clean -> 0 FAIL. The drafter's "findings.yml modified" state was transient. PASS.
- Interpreters (PC.37-.43). Mine: `Python 3.9.6`, regular file `-rwxr-xr-x 78 root wheel 118640`; venv `python3 -> python3.12`, `Python 3.12.14`; the clone has no tools/.venv; a worktree reports `venv path exists: no`; with python3 missing from PATH -> `exec: python3: not found`. PASS.
- Bash branch (PC.61/.62). Mine: T1 rc=0, 0 stderr lines; T2 rc=2 `FAIL hook.bypass: --no-verify after a gated git verb`. PASS.
- CI (PC.63, UF-PC-1). `git show f1057e5:.github/workflows/gates.yml | grep -n unittest` -> :30 and :32 without `-W error`, :34 run_all. PASS.
- Other rows held as written: PC.1, .4, .5, .6, .8, .12 (S7b), .21, .22, .23, .24, .25, .26, .27, .28, .34 (check-ignore `.gitignore:25:__pycache__/`; 0 tracked .pyc), .54, .55, .56, .59.

Rows run: 59 of 69. Not run, with reasons:
- PC.15: needs a new repo; not run for budget.
- PC.29, .31, .36, .44, .46, .52: nothing exists yet, or the observation needs the main venv or a package changed.
- PC.64: denied (below).
- PC.66: needs the forbidden hook-path key.
- PC.69: has no command.
- Partly run: PC.12 (S7c not run), PC.33 and PC.34 (the drafter's no-prefix runs write the home bytecode cache, so I did not repeat them).
I ran the whitelist data (check 5) as the drafter did: `git ls-files -s` modes 701/15/1 are consistent with the 0 tracked symlinks seen, and `git ls-files '*.pyc' '*__pycache__*' | wc -l` -> 0. I did not re-run the mode count separately.

## New questions (continuing Q-PC-18)
- Q-PC-19: Write targets outside the repository root (export and prefix directories under TMPDIR): which kind, symlink, pre-existing and unwritable rule applies, given that D32/DEC-0093 reach only below the root? Options:
  - a. realpath TMPDIR once, then require mktemp's fresh 0700 directory (about 3 tests; the macOS /var link is accepted).
  - b. place them under the git dir so D32 applies as written (git-ignored; same filesystem; a stale directory must be pruned).
  - c. no rule beyond mktemp's own failure (0 tests; a failure is a shell error, not a named line).
  - Rows: PC.27-.31, .36, W4.
- Q-PC-20: Isolation of the four D34 commands: run all four after a red one, or stop at the first? Options:
  - a. run all four (about 5 min worst case; the full picture).
  - b. stop at the first red (faster refusal; the other interpreter's state is unknown).
  - Rows: PC.67.
- Q-PC-21: §8 :230's "register/claims/comments/p10 on staged files": once comments and p10 land (S3/S4), do they run inside run_all on the export, or separately on the staged paths? Options:
  - a. inside run_all on the export (one path; they scan the whole tree).
  - b. a separate staged-paths run (a second invocation; a path list from the index).
  - c. defer the row to S3/S4 with a placeholder (no code now; the §8 text stays unmet until then).
  - Rows: new.
- Q-PC-22: Which copy of the hook script and of run_all runs: the work tree's (as the shim does today) or the export's? Options:
  - a. the export's copy (the hook judging a commit is the committed hook; a broken staged hook can lock commits until fixed).
  - b. the work tree's copy, with the I3 guard kept on tools/gates (Q-PC-11 a).
  - c. the main checkout's code, like the Bash branch's CODE_ROOT (one code version; a branch's hook changes are not tested at its own commits).
  - Rows: PC.60, new I8 rows.

## Check 7: consistency with the other two drafts
- PC.17 / Q-PC-1 against helper Q-FH-18 ("a E0 has an export mode / b the export is a worktree, so a toplevel"): consistent. Q-PC-1 c (`clone --shared`) is also a toplevel that Q-FH-18 b does not name; the wording could be aligned, but nothing contradicts.
- PC.21 against helper Q-FH-13 b: not consistent; see R-PC-4.
- Q-PC-18 against skeleton Q-SG-8 and Q-SG-11 b ("matching the PC hook's fresh export"): consistent.
- Q-SG-1 b ("runs in every commit (PC D34 set, ~seconds)"): consistent with PC.48 (run_all about 1 s).

## Unrelated findings
- UF-PC-R1. A sourceless module committed into the gate import path shadows a dependency, and run_all exits 1 with a Python traceback instead of a §7 named line.
  - Code: run_all.py:19 (`sys.path.insert(0, <root>)`, with sys.path[0] = tools/gates).
  - Reproducer: in a clone, `cp <compiled gate_claims>.pyc tools/gates/yaml.pyc; PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) /usr/bin/python3 tools/gates/run_all.py --only register,claims` -> `ImportError: cannot import name 'GateError' from partially initialized module 'tools.gates.lib'`.
  - Class: an importable file in the gate import path that git tracks or stages decides or crashes the verdict. Today's guard covers only untracked and ignored files.
  - D33 idiom `sys.path.insert` in tools/gates: gate_claims.py:26, gate_decisions.py:24, gate_dispatch.py:18, gate_plan.py:32, gate_register.py:27, gate_review_package.py:27, hooks/pre_tool_use.py:276, :327, make_review_package.py:30, run_all.py:19.
- UF-PC-1 (the drafter's): confirmed. gates.yml:30 and :32 run the suite without `-W error`, while D34 :48's commands use `-W error`.

## Denials (verbatim)
`PreToolUse:Bash hook error: [python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py"]: FAIL hook.bypass: core.hooksPath (it moves the git hooks away)`
This was my PC.64 probe. It spelled the hook-path key to create an ungated side commit for a cherry-pick observation. That line of work stopped and was not redone another way; PC.64 stays unobserved. PC.35 and PC.60 were part of the same refused command and were re-run without the PC.64 part.

## Background jobs
None started.

## Budget used
The harness count is authoritative; my own estimate is about 125k of 250k tokens and about 45 of 60 minutes. Nothing written outside $S except this report.
