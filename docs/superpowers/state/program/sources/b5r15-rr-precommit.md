status: DONE_WITH_FINDINGS
verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R2-PC-3 (Q-PC-23 is settled by S1 plan :5800-5807 and :6617, but the cell reads OPEN). R2-PC-1 (the claim "Q-PC-19 = Q-D32-W" is false: the 0700 mode and the placement choice are asked by no cell). R2-PC-4 (the D32 rule for one item's input failure in a multi-item run has no row).
- quality ❌: R2-PC-2 (DEC-0093 is quoted from an option the owner did not choose), R2-PC-5 (the PC.19 cell and the Q-PC-4 row disagree), R2-PC-6 (the W1 and PC.48 data are stale at 6a7b2f9 and name no sha), R2-PC-7 (no row for an unreadable write target).
- claims-executed ✅: 20 of the 25 changed or added rows were re-observed in my guarded clone at 6a7b2f9, each with a direct and a contradiction check. The 5 not run are named below.

commits: none. I edited nothing but this report.
Clone: R=/private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r15-rr-precommit, repo `$R/repo` at 6a7b2f9, branch rrprobe, hooks installed. The guard `$R/guard.sh` asserts both `pwd -P` and the toplevel. Scripts are `$R/q1.sh`, `$R/q2.py` and `$R/q3.sh`. They use `set -eu`, `PYTHONDONTWRITEBYTECODE=1` and a prefix under `$R`. At the end the clone has 0 porcelain lines and 1 worktree. The main checkout is clean, but its HEAD is now e17dc90, not 6a7b2f9. `git diff --stat 6a7b2f9 e17dc90` shows only decisions.yml and findings.yml (+1058 lines). No new DEC names Q-PC, Q-D32-W or R-r15 (checked with grep).

## Gates run (clone at 6a7b2f9, fresh prefix each)
- `/usr/bin/python3 tools/gates/run_all.py` -> rc=0, PASS 100, FAIL 0. The clone has no review packages: PC.51.
- `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` -> `Ran 658 tests in 210.341s OK (skipped=1)`.
- `tools/.venv/bin/python3` (main checkout), same command -> `Ran 658 tests in 234.929s OK (skipped=1)`.

## Spec lines at 6a7b2f9
- Direct: `sed -n "${n}p" <spec> | cut -c1-12` for 45..49 -> `| D31 | Gene`, `| D32 | Defa`, `| D33 | Clas`, `| D34 | Mech`, `| D35 | Draf`.
- So D31 is :45, D32 :46, D34 :48 and D35 :49, as the Task says. The D32 wording quoted in PC.29, .36 (a) and .73 is the exact prefix of :46.
- Contradiction: :47 is D33, not D32.

## Check 1: each R-PC-* finding
- R-PC-1 fixed. Direct: `grep -c 'else commit mode' draft` -> 0. PC.53 now matches W5.
- R-PC-2 fixed as R-r15-1 states. W4 no longer says "never an existing path (D32)" (`grep -c` -> 0). The cells cite D32 or Q-D32-W. The DEC-0093 quote it carries is wrong: R2-PC-2.
- R-PC-3: PC.19 is fixed. The Q-PC-4 row is kept, with a note row. The cell and the row now disagree: R2-PC-5.
- R-PC-4 fixed. PC.21 names Q-FH-13 and Q-PC-1. Helper draft :175 Q-FH-13 b reads "the pre-commit export covers them": consistent.
- R-PC-5 fixed: PC.71, plus the Q-PC-15 b cost (diff against draft.before.md).
- R-PC-6 fixed: items I10b, I11 and I12. I checked every cited line with `grep -n '' | sed -n`: run_all.py :19, :37, :39, :42, :77-81; pre_tool_use.py :76, :77, :229, :247, :276, :327, :329; git-pre-push.sh :18, :38, :39; install-hooks.sh :16-19, :36, :41-48; git-pre-commit.sh :28, :30-32, :35, :52-57, :60. All hold.
- R-PC-7 fixed: the surface paragraph says WORK-TREE, plus PC.72 and PC.73.
- R-PC-8, R-PC-9 and R-PC-10 fixed. My re-observations are under check 2.
- R-PC-11 fixed for PC.55 (D34 contains "tree committed"), PC.62 (DEC-0005's chosen plan-design-plus-prepush contains "additionally blocks hook bypasses (--no-veri...") and PC.60. PC.66 is wrong: R2-PC-3.
- R-PC-12: the fix cites Q-D32-W, as R-r15-1 directs, not the reviewer's Q-PC-19. See R2-PC-1.
- R-PC-13 fixed: PC.74. Direct: `ls tools/gates | grep -c` -> 0, S3 47, S4 35. §8 :230 quote: `sed -n 230p` -> "register/claims/comments/p10 on staged files, plus the D34 set".
- R-PC-14 fixed (diff): Q-PC-3 a, Q-PC-6 a/b/c and Q-PC-9 a/b now carry costs.
- R-PC-15 fixed: PC.75. The N/A count went 11 -> 10 (diff).
- Line cite: S2 plan :7185 holds. `sed -n 7185p | grep -o 'Owner task S2-b5-process-code[^.]*'` -> "Owner task S2-b5-process-code (row B5-PROC-2): the D31 skeleton generator, the D32 shared fail-closed helper ... and the D34 pre-commit gate". At f1057e5 the same text is :7221.

## Check 2: changed and added rows
`diff` of the `^| PC.` rows between `r15-fix-precommit/draft.before.md` and the draft:
- changed (17): PC.7, .15, .18, .19, .21, .28, .29, .31, .35, .36, .47, .53, .55, .60, .62, .66, .67.
- new (8): PC.70-.77.
- The draft has 77 rows, 25 Q rows and 144 lines, as the fixer reports.

| row | claim | direct (my output) | contradiction (my output) | verdict |
|---|---|---|---|---|
| PC.53 | today, no argv check | clean: `--pre-push`, `--pre-push `, `x`, `''`, none -> each rc=0, PASS 10 | untracked yaml.py: `--pre-push` -> rc=1 `FAIL pre-push.uncommitted`; `--pre-push ` and `x` -> rc=1 `FAIL pre-commit.unstaged` (commit mode) | PASS |
| PC.7 | (a) index-only chmod refused; (b) with the disk chmod it passes | (a) rc=1 `... MM tools/gates/gate_plan.py`; clone core.filemode=true | (b) rc=0, PASS 10, ` 1 file changed, 0 insertions(+), 0 deletions(-)`, index mode 100755 | PASS |
| PC.18 | the clean filter keeps the marker out of HEAD | (a) status `[]` -> rc=1 `nothing to commit` (the hook printed PASS 10 first); (b) `[M  README.md]` -> rc=0, HEAD marker 0 | no filter: the same append staged -> rc=0, HEAD marker 1 | PASS |
| PC.19 | checkout-index applies smudge; the index blob is raw | smudge export `SMUDGED:# lap-timer` vs `git show :README.md` `# lap-timer` | no filter: export `# lap-timer` | PASS |
| PC.21 | core.excludesFile unset | `git config --show-origin --get-all core.excludesFile` -> unset; core.attributesFile unset | the `--show-origin --get-all` form reads all scopes: none set | PASS |
| PC.28/.70 | mktemp gives 0700 through a symlink | `/var/folders/54/.../T//strict-delivery-pre-push.68UR0C mode=drwx------ realpath=/private/var/folders` | `/var` and `/tmp` are symlinks (`ls -ld` first char `l`) | PASS |
| PC.35 | a staged sourceless yaml.pyc gives a Traceback, no named line | `100644 df3e1ab2e`; rc=1 `Traceback ... run_all.py", line 21`, PASS 0 | the same without the pyc -> rc=0, PASS 10 | PASS |
| PC.60 | root-commit tree -> pre-commit.missing | root 787b52d: `ls ... No such file`; rc=1 `FAIL pre-commit.missing: this tree has no tools/gates/hooks/git-pre-commit.sh ...` | at HEAD (PC.35 contradiction) -> rc=0 | PASS |
| PC.15 | unborn repo -> pre-commit.missing | `$R/unborn`, `git init`, hooks installed -> rc=1 `FAIL pre-commit.missing` | with a stub script `exit 0` in the tree -> rc=0, HEAD a29f77d | PASS |
| PC.55 | the project-dir variable is unset; the committing tree is gated | the variable set to the main checkout, untracked yaml.py in the clone -> rc=1 `?? tools/gates/yaml.py` | the main checkout has no untracked yaml.py, so the FAIL is the clone's | PASS |
| PC.62 | bypass flag -> exit 2 | T2 -> rc=2 `FAIL hook.bypass: --no-verify after a gated git verb` | T1, the same verb with no flag -> rc=0, 0 stderr lines; T2c `-n` -> rc=2 | PASS |
| PC.71 | user-site PyYAML; `-I` loses it | `~/Library/Python/3.9/lib/python/site-packages/yaml/__init__.py` | `-I` and `-s` -> `ModuleNotFoundError: No module named 'yaml'`; venv `-I` -> `.../tools/.venv/lib/python3.12/site-packages/yaml/__init__.py` | PASS |
| PC.72 | the shim runs the work-tree copy | install-hooks.sh:41 `script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-@HOOK@.sh"`, :46 `exec sh "$script" "$@"` | PC.7a: a work tree that differs from the index on tools/gates is refused (`MM`), so today the I3 guard keeps the two equal | PASS |
| PC.73 | a symlinked hook script is caught only as a type change | rc=1 `... T tools/gates/hooks/git-pre-commit.sh` | a regular file -> rc=0, PASS 10 | PASS |
| PC.74 | no comments or p10 gate exists | `ls tools/gates \| grep -c` -> 0 | plans: S3 47, S4 35 hits | PASS |
| PC.75 | the plain export breaks register's history read | `run_all --root <export> --only register` -> rc=1 `FAIL register.input: git cat-file -e 151f2bb^{commit}: fatal: not a git repository` | `--root` = the clone -> rc=0, FAIL 0 | PASS |
| PC.76 | the Bash branch gates the main checkout with the clone's code | T3 (cwd clone, a leading cd to the main checkout) -> rc=0, 0 stderr lines | the clone's gate_claims.py broken in its work tree, T3 again -> rc=2, 7 stderr lines (the clone's code ran) | PASS |
| PC.77 | pre-push refs: not-head vs HEAD | HEAD~1 -> rc=1 `FAIL pre-push.not-head: refs/heads/x (f1057e5) is not the checked-out HEAD` | HEAD -> rc=0, PASS 10 | PASS |
| PC.47 | marked f1057e5, not re-run | at 6a7b2f9: both suites `Ran 658 ... OK (skipped=1)` | f1057e5 (round-14 review): 660, failures=2 | PASS as labelled (values differ at 6a7b2f9) |

Rows not run (5):
- PC.29, .31, .36 and .67: nothing exists yet, and mktemp always creates a new directory.
- PC.66: it needs git's hook-path key set, which the traps forbid. Its settled-by cell was checked instead: R2-PC-3.

Whitelist (check 5):
- W1: `git ls-files -s | awk '{print $1}' | sort | uniq -c` -> 702 100644, 15 100755, 1 160000. The draft says 701: R2-PC-6.
- W2: pyc, `__pycache__` and `.so` -> 0 entries; flagged entries -> 0.

## Check 3: questions
- `diff <(grep '^| Q-' before) <(grep '^| Q-' after)`: the rows of Q-PC-1..18 are unchanged except the costs added to Q-PC-3, -6 and -9 and the Q-PC-15 b cost (R-PC-14, R-PC-5). None is answered.
- Two issues:
  - Q-PC-19's options now reach no cell: R2-PC-1.
  - PC.19 restates Q-PC-4 with two options: R2-PC-5.
- Every Q id cited in a row exists in the questions table (grep counts). Q-PC-19 is cited once, in PC.28.

## Check 4: R-r15-1 against spec :46 and DEC-0093 (divergences, both quotes)
1. R-r15-1 (fix-common): "DEC-0093 chose below-root-plus-args, "no change for /tmp paths"". DEC-0093 (decisions.yml :865-867): "no change for /tmp paths" is the cost of option `below-root`. The chosen `below-root-plus-args` reads "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root" (answer: "below-root-plus-args (owner in chat, 2026-10-27 ...)"). The conclusion still follows from the chosen text; the quote does not. -> R2-PC-2.
2. Spec :46: "an input or write target that is unreadable ... FAILs". R-r15-1: "Not settled by D32 ...: a target or parent that is unwritable". An unreadable write target is settled, and no cell separates it out. -> R2-PC-7.
3. R-r15-1: "e.g. Q-FH-10, Q-FH-11, Q-FH-23, Q-PC-19, Q-PC-20". The draft's Q-PC-20 reads "Isolation of the four D34 commands: run all four after a red one, or stop at the first?". That is not about write targets. The fixer was right to keep it.
4. R-r15-1: "replace it in the cell with Q-D32-W and keep the old id's row ... marked "= Q-D32-W"". Q-PC-19's options (a realpath + fresh 0700 / b under the git dir / c no rule) are not Q-D32-W's options ((a) yes FAIL / (b) no). -> R2-PC-1.
- The other R-r15-1 "settled" items (wrong kind, a below-root component, the path argument itself, the multi-item write target) match :46.

## Check 5: the fixer's divergences
1. Q-PC-4 kept: agreed. The remaining inconsistency is R2-PC-5.
2. Q-PC-20 kept: agreed (check 4 item 3).
3. The 0700 mode is not in Q-D32-W: agreed, and it is a defect: R2-PC-1.

## Findings
- R2-PC-3 MEDIUM, PC.66 and Q-PC-23 (check 3: an OPEN cell that a plan line settles).
  - Draft Q-PC-23: "a rule no spec line, plan line or DEC states".
  - S1 plan :5800 `def test_a_hooks_path_outside_the_repository_is_refused(self):`, :5806 `self.assertIn("install-hooks: refusing: the hooks directory", p.stderr)`, :6617 `*) echo "install-hooks: refusing: the hooks directory $hooks (...) is outside $common; it may be shared with other repositories" >&2; exit 1 ;;`.
  - Command: `grep -n 'refusing\|is outside' docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md`.
  - Replacement (hypothesis): PC.66 settled by "S1 plan :5800-5807, :6617 (refuse)". Q-PC-23 is retired under D32 ("only choices it does not settle are owner questions"). The fix rules say "never answer one", so whether a fix round may retire a question that is shown to be settled is for the controller to rule.
- R2-PC-1 MEDIUM, PC.28, .31, .36 (b), .70, W4 and Q-PC-19.
  - The row says "Q-PC-19 | = Q-D32-W" and PC.28 says "OPEN Q-D32-W (= Q-PC-19 ...)".
  - Q-D32-W asks whether the FAILs cover these cases ((a) yes / (b) no). Q-PC-19 asks which rule applies, with the options "a. realpath TMPDIR once, then require mktemp's fresh 0700 directory / b. place them under the git dir ... / c. no rule beyond mktemp's own failure".
  - An answer to Q-D32-W leaves PC.28's 0700 mode, W4's "new, 0700" and the placement unasked. Q-PC-19's rows column (PC.27-.31, .36, W4) points at cells that cite only Q-PC-7, D32 or Q-D32-W.
  - Command: `sed -n '/^| PC\./p' draft | grep -o 'Q-PC-19' | wc -l` -> 1.
  - Replacement (hypothesis): those cells read "OPEN Q-D32-W; Q-PC-19". The Q-PC-19 row reads "related to Q-D32-W: Q-D32-W decides the FAIL, Q-PC-19 the placement and mode". This diverges from R-r15-1, which is the controller's ruling.
- R2-PC-4 LOW, PC.67, and PC.37/.41/.44 (a missing D32 row).
  - Spec :46: "in a multi-item run one item's input failure FAILs that item and the others still run".
  - PC.67: "D32 covers an input failure, not a red test". Yet no row states what D32 settles for an input failure (a missing interpreter, or a failed export or prefix) among the four commands.
  - Replacement (hypothesis): PC.67 adds "(b) one interpreter missing: that command FAILs `pre-commit.input`, the others still run (D32, if the four commands are items; that reading is OPEN Q-PC-20)".
- R2-PC-2 LOW, PC.31 and PC.70 (settled-by cells).
  - Both cells quote "DEC-0093 ... "no change for /tmp paths"", and that is the unchosen option's cost (check 4 item 1).
  - Command: `sed -n '861,870p' docs/superpowers/state/decisions.yml`.
  - Replacement: "DEC-0093 below-root-plus-args: "below the root, plus a path argument that is itself a symlink"".
- R2-PC-5 LOW, PC.19 and Q-PC-4.
  - The cell reads "a filter or attribute config present: ignored or refused, OPEN Q-PC-4", which is options a and c. The Q-PC-4 row still offers "b apply only the tracked `.gitattributes`".
  - The rules diverge. R-PC-3: "Q-PC-4 narrows to ...". fix-common: "Never answer one; never narrow one". D32: "only choices it does not settle are owner questions".
  - Replacement (hypothesis): the cell reads "OPEN Q-PC-4 (see the Q-PC-4 note row)", without restating the options. The controller or owner rules on narrowing.
- R2-PC-6 LOW, W1, PC.48 and Q-PC-16 (D35: a today cell must hold at its stated base).
  - W1 "701 100644" vs mine at 6a7b2f9: 702.
  - PC.48 "suite 142 s + 156 s" (no sha) vs mine at 6a7b2f9: 210 s + 235 s. The machine was possibly loaded.
  - Q-PC-16 says "about 300 s per commit (measured)".
  - Replacement: name f1057e5 in W1 and PC.48, or the 6a7b2f9 values. The question text stays as it is.
- R2-PC-7 LOW, PC.31 and PC.36 (b): no row for an unreadable write target (check 4 item 2).
  - Replacement: PC.36 adds "(c) an existing target or a used directory that cannot be read (mode 000): FAIL `pre-commit.input` naming the path, D32".

New questions: none. The questions above go to the controller about its rules; they are not owner questions numbered Q-PC.

## Unrelated findings
- UF-R2-PC-1. run_all.py:80 `names = ... [n.strip() for n in ns.only.split(",") if n.strip()]`.
  - Reproducer (clone): `run_all.py --only ' register '` -> rc=0, PASS 6. Contradiction: `--only registerx` -> rc=1 `FAIL run_all.unknown`.
  - Class: a CLI name list is matched after a strip, not by fullmatch (D32 "names and values match as whole strings").
  - D33 idiom hits (`split(",")` with strip, non-test): make_review_package.py:184 (the same CLI idiom); gate_dispatch.py:71, gate_plan.py:155, :222, :233 and gate_register.py:129 (grammar fields: whether D32 applies is for each gate's table).
  - Not part of the PC hook surface: the hook passes a fixed `--only`, and under D34 none at all.
- UF-PC-R1 and UF-PC-1 still stand: PC.35 reproduced, and the CI line was not re-checked.

## Process note
One read-only `grep` pattern of mine spelled git's hook-path key (the S1 plan search for Q-PC-23). The trap rule says to describe it in words. The command wrote nothing and was not denied.

denials: none. background jobs: none. budget: about 100k of 250k tokens and about 35 of 100 minutes (my own estimate; the harness count is authoritative).
