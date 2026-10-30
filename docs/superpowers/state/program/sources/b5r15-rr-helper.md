status: DONE_WITH_FINDINGS
commits: none (task reviewer; I edited nothing but this report)
verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R2-FH-1 (FH.53 reads OPEN, but DEC-0068 a settles its "not a repository" value, and the today cell does not observe that value; the value fails open), R2-FH-2 (the D32 quote drops "For every gate:"; the cells for make_review_package and the hooks depend on a reach that D32 does not state).
- quality ❌: R2-FH-3..R2-FH-10.
- claims-executed ✅: I re-ran all 23 rows the fix round changed or added (†), plus the whitelist data, the skeleton cites and every spec, DEC and plan cite, in my own guarded clone at 6a7b2f9. Commands and outputs are below.

Guard: clone $S/repo, where $S = /private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r15-rr-helper. It was made with `git clone -q --no-hardlinks` and `checkout -q 6a7b2f9`. Every probe ($S/bin/p1.sh .. p4.sh) has `set -eu` and asserts that `pwd -P` and the toplevel lie under $S. All probes ran with PYTHONDONTWRITEBYTECODE=1 and PYTHONPYCACHEPREFIX under $S. At the end the clone is at 6a7b2f9 and `git status --porcelain --untracked-files=all` prints 0 lines. The main checkout was only read; it is now at e17dc90, and `git diff --stat 6a7b2f9 e17dc90 -- tools/gates` is empty.

## gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) /usr/bin/python3 tools/gates/run_all.py` (clone, 6a7b2f9) -> rc=0, 100 PASS. The clone holds no git-ignored packages.
- No other gate list applies: this is a plan-text draft with no code.
- Gate code is unchanged: `git diff --stat f1057e5 6a7b2f9 -- tools/gates/*.py tools/gates/lib tools/gates/hooks` -> empty.

## Base facts verified
| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| D31 :45, D32 :46, D35 :49 at 6a7b2f9 | `grep -n '^\| D3[1-5] ' spec` | :47 is D33 | 45 D31, 46 D32, 47 D33, 48 D34, 49 D35 | PASS |
| D32-a..d are verbatim from :46 | `sed -n 46p spec` | the omitted text | a-d verbatim; omitted: "For every gate:", the helper sentence, "Table cells cite this default; only choices it does not settle are owner questions." (R2-FH-2) | PASS for a-d / partial |
| §7 :203 "a non-directory where a directory is expected is" (FH.62) | `sed -n 203p spec` | -- | "... a non-directory where a directory is expected is: DEC-0022" | PASS |
| plan :7185 names P-1; :3795 is the make_review_package Produces line | `grep -n 'S2-b5-process-code (row B5-PROC-2)'`; `grep -n 'Produces: CLI .python3 tools/gates/make_review_package'` | -- | 7185 holds "stale bytecode (P-1: ..."; 3795 holds "writes `.superpowers/sdd/<plan>/review/<task>/{commits.txt,diff.patch,gates.txt}`" | PASS |
| DEC-0093 chosen = below-root-plus-args, "one realpath of the root" (FH.36) | decisions.yml dump | is "no change for /tmp paths" in the chosen option? | chosen cost: "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root". "no change for /tmp paths" is in option `below-root` | PASS (FH.36) |
| DEC-0082 a text (FH.73) | dump | -- | "lib refuses an id outside `[A-Za-z0-9_.-]+` and a FAIL with an empty reason (FAIL `<gate>.input`)" | PASS |
| DEC-0081 a text (FH.54) | dump | -- | "a ledger that is not a regular 100644 UTF-8 LF file at all three copies FAILs `plan.<slug>.ledger`" | PASS |
| Q-D32-W is verbatim from fix-common | python string compare | difflib | `True` | PASS |
| counts: 226 lines, 78 rows, 23 Q-FH ids | `wc -l`; `grep -c '^\| FH\.'`; `grep -c "^\| Q-FH-$i \|"` for i in 1..23 | -- | 226; 78; every id 1..23 once | PASS |

## Rows run / total: 23 / 78 (all changed and added rows), plus whitelist and skeleton
Not re-run: the 55 rows the fix round left unchanged (FH.3-7, 9-27, 30-32, 34-35, 37, 45-47, 49-52, 55-61, 63, 65-70, 72, 74-76). The re-review scope is changed and added rows first. The round-14 reviewer observed those rows at f1057e5, and the gate code is identical at 6a7b2f9.

`GP` = `/usr/bin/python3 $S/repo/tools/gates/gate_plan.py`; `P` = the F0 plan; `M` = `make_review_package.py strict-delivery t HEAD~1 HEAD --only claims`; baseline `GP --plan P` -> rc=1 lines=53 fails=35.

| row | direct | contradiction | output excerpt | verdict |
|---|---|---|---|---|
| FH.1 | `cd docs && GP(abs) --plan superpowers/plans/<P>` | the literal relative script path; the root-relative `--plan P` from docs | `FAIL plan.file: superpowers/plans/...: no such plan file`; relative script -> `can't open file`; root-relative -> baseline | PASS |
| FH.2 | `GP --plan $S/repo/P` | counts differ from baseline | baseline | PASS |
| FH.8 | `ln -s $S/out/o.md olink.md; GP --plan olink.md` | -- | baseline (followed) | PASS |
| FH.28 | `register: \377 B5-NOPE-1` in docs/reviews/zz.md; gate_register | the same file without 0xff | `(found 'register: � B5-NOPE-1')` vs `(found 'register: B5-NOPE-1')` | PASS |
| FH.29 | VT, FF, NEL, U+2028 in place of the LF before Goal | a space | 4× `PASS plan.header.goal`; space -> `FAIL plan.header.goal` | PASS |
| FH.33 | `GP --root $S/rootlink --plan P` | the real root | both baseline (the link is accepted) | PASS |
| FH.36 | `GP --root /tmp/<scratch>/repo --plan P` | -- | baseline | PASS |
| FH.38 | claims.yml -> $S/out3/claims.yml (cites `["docs/superpowers/state/findings.yml"]`); `gate_claims.py --mark-stale --base HEAD~1` | the same row in a regular claims.yml | `PASS claims.marked-stale.C-demo-1`, outside file `state: stale`, `stat` -> Symbolic Link; regular -> stale too. diff.patch -> $S/out3/o.txt: rc=0, outside now `diff --git a/docs/su...` | PASS. Note: the fixer's p.sh FH.37 fixture uses dict cites, which give `FAIL claims.input: ... invalid ... row(s)`; the cell's "citing findings.yml" holds with list cites |
| FH.39 | diff.patch a directory; then a FIFO | -- | `cannot write ...: Is a directory` rc=1; FIFO -> `TIMEOUT 20s (hang)` | PASS |
| FH.40 | diff.patch `STALE` | -- | rc=0, now `diff --git a/do` | PASS |
| FH.41 | `.superpowers` removed | -- | rc=0, `review/t` created | PASS |
| FH.42 | diff.patch 444 | parent 555 with files present; parent 555 empty | 444 -> `Permission denied` rc=1. Parent 555 with files present -> rc=0 `gates PASS` (written). Parent 555 empty -> `Permission denied`. The cell has no observation for the parent case (R2-FH-3) | PASS (444) |
| FH.43 | `ulimit -f 4; trap '' XFSZ; M` | -- | `File too large` rc=1; `wc -c` 4096 | PASS |
| FH.44 | task `t\n` | `T` then `t`; `..` | `wrote .../review/t` + `od -c` `t \n`. `T` then `t` -> a single directory `T` (APFS). `..` -> `task '..' is not a directory name (^[A-Za-z0-9][A-Za-z0-9._-]*$)` (R2-FH-5) | PASS (LF) |
| FH.48 | `* binary` in info/attributes; `git diff HEAD~1 HEAD \| grep -c '^Binary files'` | `* eol=crlf`, `* text`, `* export-subst`, none | binary 6. eol, text and export-subst -> 0, diff sha 98c18a3f (= no attributes), show sha unchanged, plan baseline. The draft says "not run" (R2-FH-3) | PASS (binary) |
| FH.53 | `GP --root $S/nogit --plan docs/x.md` | registers present in a non-git root: `GP --root $S/ng2 --in-flight`; `gate_register.py --root $S/ng2` | `FAIL plan.input: ...findings.yml: No such file or directory`. With registers: gate_plan `rc=0 lines=69 fails=0` (fail-open); register `FAIL register.input: git cat-file -e ...: fatal: not a git repository` (R2-FH-1) | cell holds / row not observed |
| FH.54 | `rev-parse --abbrev-ref HEAD` -> HEAD; `GP --in-flight`; `git rm --cached` ledger | -- | detached: rc=0, 69 PASS; index-absent: rc=0, 69 PASS | PASS |
| FH.62 | `.superpowers/sdd/pfile` a file; `gate_review_package --all` | `pfile/review` a file | `PASS review_package.all`; control -> `FAIL review_package.pfile.input: ... is not a directory` | PASS |
| FH.64 | FIFO `state/zz/ledger.md`; `M` | a directory ledger; no extra ledger | all three: rc=0 `gates PASS` (silent skip) | PASS. R2-FH-6 on the fix note |
| FH.71 | findings.yml 000; run_all | -- | claims, plan, register and review_package 1 FAIL each; decisions 5 PASS, dispatch.config 15 PASS | PASS |
| FH.73 | `--scan sub/x.md sub/f.md` (f a FIFO) | -- | `FAIL decisions.scan.sub/f.md: not a regular file` | PASS |
| FH.77 | `head -1 tools/gates/hooks/git-pre-commit.sh` | -- | `#!/bin/sh` | PASS |
| FH.78 | `ls -l <main>/tools/.venv/bin/python3` | `git ls-files tools/.venv \| wc -l` | `python3 -> python3.12`; 0 tracked | PASS |
| whitelist | main checkout: 120000 count; non-regular find; unanchored and anchored Binary grep; the byte loop over the draft's file set | BOM/CR detector on a planted `\xef\xbb\xbfx\r\n` file in $S/wl | 0; 0; 3 vs 0; files=110 bom=0 cr=0 empty=0 nolf=0 bad=0; the detector flags efbbbf and cr=1 | PASS (R2-FH-8 on the command form) |
| R-FH-7 skeleton | `sed -n <line>p` for the 21 added cites | the grep-1 hits not cited by line (script over $S/g1.txt, 328 hits; 73 pure-string calls skipped) | all 21 cites hold. 76 uncited hits: mostly `re.compile` definitions whose use sites are cited, plus 3 filesystem calls (R2-FH-4) | partial |

## Check 1: each R-FH-* finding
| finding | result | evidence |
|---|---|---|
| R-FH-1 | fixed | unanchored 3, anchored 0 (my run) |
| R-FH-2 | fixed (FH.36) | DEC-0093 chosen text as above |
| R-FH-3 | cell fixed; Q-FH-9 kept with a note (divergence 1 below) | FH.33 run |
| R-FH-4 | partly: FH.48 eol, text and export-subst still read "not run", which is neither a command nor OPEN (D35); FH.42 parent case not observed | R2-FH-3 |
| R-FH-5 | fixed | FH.1 run |
| R-FH-6 | partly: the clauses are verbatim, but "For every gate:" and the "only choices it does not settle" sentence are dropped (the fixer report says "quoted in full") | R2-FH-2 |
| R-FH-7 | partly: the listed lines were added; 3 filesystem hits remain unmapped and 4 fullmatch sites are misfiled | R2-FH-4 |
| R-FH-8 | fixed (FH.62) | run |
| R-FH-9 | fixed (FH.54, Q-FH-21); the expected verdict for a detached HEAD is unstated | R2-FH-10 |
| R-FH-10 | fixed (FH.30) | text read |
| R-FH-11 | fixed (FH.44, Q-FH-22); the `..` reason is false | R2-FH-5 |
| R-FH-12 | row fixed (FH.64); the explanation in the fix table is false | R2-FH-6 |
| R-FH-13 | fixed: every option of Q-FH-1..22 carries a cost; Q-FH-10/11/23 read "= Q-D32-W" | read |
| R-FH-14 | fixed: PC.31 reads Q-D32-W, PC.68 = FH.77 (Q-PC-17), PC.42 = FH.78 (Q-PC-10); skeleton SG.80 and the Q-SG-4 row read "= Q-FH-19 = Q-SG-18" | greps of b5r14-draft-precommit.md :41, :52, :78 and b5r14-draft-skeleton.md :94, :107 |
| R-FH-15 | partly: the command is elided (`do ...; done`) | R2-FH-8 |
| R-FH-16 | citations added; the scope was not narrowed (divergence 2); the DEC labels are wrong | R2-FH-7 |
| R-FH-17 | fixed (N/A line, plan :7185) | grep |
| R-FH-18 | fixed (FH.73) | run |

## Findings
**R2-FH-1 (MEDIUM), FH.53 (changed row), settled-by and today.**
- Draft: "non-zero exit, not a repository | FAIL `<pfx>.input`, or None | OPEN Q-FH-16 | † `GP --root $S/nogit --plan docs/x.md` -> `FAIL plan.input: docs/superpowers/state/findings.yml: No such file or directory` (the register load fails before git runs)".
- DEC-0068 a: "root must equal the realpath of `git rev-parse --show-toplevel`, else FAIL `<gate>.input`". A root that is not a repository has no toplevel, so DEC-0068 a settles that value.
- The today command never reaches git (the cell says so itself). With the registers present, `GP --root $S/ng2 --in-flight` -> `rc=0 lines=69 fails=0`: gate_plan fails open in a non-repository. gate_register gives `FAIL register.input: git cat-file -e 151f2bb^{commit}: fatal: not a git repository`.
- Replacement (hypothesis): split the row. "not a repository -> FAIL `<gate>.input` | DEC-0068 a | the observation above"; "non-zero exit inside a repository -> OPEN Q-FH-16".

**R2-FH-2 (MEDIUM), D32 quote (header :5-9) and the cells for make_review_package and the hooks.**
- Spec :46 begins "For every gate: an input or write target ...". The draft quotes clauses a-d without "For every gate:".
- `grep -n make_review_package spec` -> 0 hits. `run_all.GATES` = register, claims, decisions, dispatch, review_package, plan.
- These cells cite D32-a or D32-b for targets of make_review_package: FH.37, FH.38 (diff.patch half), FH.39, FH.44 (LF), FH.64. The surface sentence "Every gate, `make_review_package` and the hooks call it" cites D32, DEC-0066, DEC-0085 and DEC-0093, and none of them names make_review_package or the hooks. DEC-0072, -0076 and -0083 settle single make_review_package rules, not D32's reach.
- Replacement: quote "For every gate:" in D32-a, and either cite a line that puts make_review_package and the hooks under D32, or mark those cells OPEN Q-FH-24 (new, below).

**R2-FH-3 (LOW), D35 today cells left after R-FH-4 (FH.48, FH.42).**
- FH.48: "eol, text and export-subst not run". D35 (spec :49): "Every "today" cell carries the command that observed it, or reads OPEN".
- My observation: each of `* eol=crlf`, `* text` and `* export-subst` gives `^Binary files` 0 and a `git diff HEAD~1 HEAD` sha of 98c18a3f, equal to the no-attributes sha. `git show HEAD:P` sha is unchanged and the plan reads as baseline.
- FH.42 values "or parent unwritable" have no observation. Mine: parent 555 with the files present -> rc=0 `gates PASS` (overwritten in place); parent 555 empty -> `cannot write ...: Permission denied` rc=1.

**R2-FH-4 (LOW), D31 skeleton (R-FH-7 dimension re-run over the whole list).**
- Unmapped filesystem calls:
  - make_review_package.py:110 `os.path.exists(os.path.join(root, ".GIT"))`
  - make_review_package.py:189 `os.path.isfile(os.path.join(pkg, f))`
  - hooks/pre_tool_use.py:295 `os.path.realpath(path)`
- Misfiled: gate_register.py:112, :118, :178 and gate_decisions.py:64 already call `.fullmatch` but appear under "names and values", not "already fullmatch" (draft :39-40).
- Command: grep 1 -> $S/g1.txt (328), a mapping script against the draft's cites -> 76 uncited, of which these 3 touch the filesystem.

**R2-FH-5 (LOW), FH.44 (added row).**
- Draft: "`..` FAIL (NAME has no `/`)" with settled-by "D32-c; OPEN Q-FH-22".
- Observed: `task '..' is not a directory name (^[A-Za-z0-9][A-Za-z0-9._-]*$)`. The refusal comes from the first-character rule, and `..` holds no `/` anyway.
- No cite settles the `..` verdict: fullmatch alone does not refuse `..`.
- Replacement: cite the NAME grammar's source or read OPEN.

**R2-FH-6 (LOW), fix-table row R-FH-12 (:213) and the fixer report.**
- Claim: "the reviewer saw rc=1 gates FAIL at f1057e5; at 6a7b2f9 the run_all set PASSes".
- Contradiction: at f1057e5 `M` (`--only claims`) with a FIFO ledger -> `rc=0 ... gates PASS` (p3.sh). At 6a7b2f9 with the reviewer's `--only gate_claims` (r14 p6.sh :9) -> `rc=1 ... gates FAIL`, `FAIL run_all.unknown: no gate named 'gate_claims'` (p4.sh).
- The rc comes from the `--only` value, not the sha. Replacement: state that.

**R2-FH-7 (LOW), Q-FH-13 cite.**
- Draft: "DEC-0098 a and DEC-0101 a put "the ignore and filter rows" in the hook's table".
- Neither decision has an option `a`. The chosen options are `scope-default-config` ("its D24 table carries the ignore and filter rows") and `drop-the-promise` ("P-1, ignore, filter and global files are its D24 rows"). The quoted words are DEC-0098's only.
- The round-14 reviewer's R-FH-16 has the same labels.

**R2-FH-8 (LOW), whitelist command (:156).**
- `bash -c 'for f in ...; do ...; done'` cannot be run as written (R-FH-15 asked for the command).
- My loop (head -c3 BOM, `grep -q $'\r'`, `-s`, tail -c1, iconv) reproduces files=110 and all counts 0. Replacement: write the loop in full.

**R2-FH-9 (LOW), questions (check 3): options dropped and a sub-question detached.**
- Q-FH-10 and Q-FH-11 now read only "= Q-D32-W". Their round-14 options, which b5r14-tablerev-skeleton.md :94 quotes as "a create (makedirs, as today) / b FAIL" and "(D32 names 'unreadable')", are gone.
- Q-D32-W (b) reads: "each stays an owner question per table". Under (b) those rows have no options left.
- FH.43 no longer cites Q-FH-12, whose row says "the write form (a: temp + rename ... / b: plain write ...) stays open here".
- fix-common rule 3 says: "Every such cell ... reads `OPEN Q-D32-W` ... keep the old id's row in your Open questions table marked "= Q-D32-W"". It does not say to delete the options. I report both and do not pick.
- Replacement (hypothesis): keep the round-14 options in Q-FH-10/11 beside "= Q-D32-W", and have FH.43 read "OPEN Q-D32-W; write form OPEN Q-FH-12".

**R2-FH-10 (LOW), FH.54 (added row).**
- The values include "unborn or detached HEAD"; the expected cell reads "ledger: FAIL `plan.<slug>.ledger` (DEC-0081 a); other E4 reads OPEN Q-FH-21".
- DEC-0081 a FAILs a ledger that is not a regular 100644 file at all three copies; a detached HEAD with a regular ledger is not such a case.
- My clone is detached and run_all gives 100 PASS. The expected verdict for a detached HEAD is left unstated. Replacement: give it its own value line.

## Check 4: R-r15-1 against spec :46 and DEC-0093 (divergences, both quotes; I do not pick)
- D-1. R-r15-1: "DEC-0093 chose below-root-plus-args, "no change for /tmp paths"". DEC-0093: the words "no change for /tmp paths" are the cost of option `below-root`; the chosen `below-root-plus-args` reads "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root". R-FH-2 is the same misquote. PC.31 (b5r14-draft-precommit.md :41) repeats it.
- D-2. R-r15-1 counts as not settled "a symlink component outside the root (TMPDIR and the like) that is not itself the path argument" (-> Q-D32-W). This draft's FH.10 settles the same shape for reads: "a component above the root is a link ... | accepted | D32-a ("below the repository root"); DEC-0093 below-root-plus-args". D32-a covers "an input or write target" in the same clause, so the two read one clause two ways.
- Consistent with :46: the ruling's settled list (wrong kind, a symlink below the root, a path argument that is itself a symlink, multi-item) uses D32's words. "unwritable", "missing parent", "partial write" and "pre-exists outside the root" have no D32 word.

## Check 5: the fixer's stated divergences (both quotes; I do not pick)
1. R-FH-3: "retire Q-FH-9". Fix rule 2: "Keep every Q-<area>-* question open ... Never answer one". Against both: table-review rule 3: "a cell marked OPEN that ... the D32 default does settle is a finding (D32 retires such questions)"; spec :46: "only choices it does not settle are owner questions". The draft now settles FH.33 by D32-a and keeps Q-FH-9 open, with "kept open for the owner" and option a "contradicts D32-a" in the question text.
2. R-FH-16: "ask only about `.git/info/exclude` and the global ignore file". Rule 2: "never narrow one". Q-FH-13's scope is kept.

## Check 7: consistency with the other drafts
- Q-FH-19 = Q-SG-4 = Q-SG-18 is written in both drafts (SG.80; the Q-SG-4 row :107).
- Q-D32-W is in PC.31, PC.36, PC.70 and SG.52b.
- FH.77/PC.68 and FH.78/PC.42 match.
- The only mismatch: D-2 above (FH.10 vs PC.70's "a symlink component outside the root ... OPEN Q-D32-W").

## New questions
| id | question | options (cost) | rows |
|---|---|---|---|
| Q-FH-24 | Does D32 ("For every gate:") reach make_review_package.py and the hooks, which `run_all.GATES` does not list and the spec never names? | a: yes, every script under tools/gates (the D32 cells for FH.37-39, FH.44 and FH.64 stand; the hooks join the helper's callers; 0 text beyond one spec line) / b: gates only (those cells read OPEN, and make_review_package and the hooks need their own rule or DEC per row; about 6 cells reopen) | FH.37-39, FH.44, FH.64, surface |

## Unrelated findings (existing code at 6a7b2f9; for registration)
1. tools/gates/gate_plan.py:524-525, :530, :557 (`_git_bytes`, `_git_ledgers`, `_git_mode` return None or empty on any git failure): `gate_plan.py --root <non-git copy with registers> --in-flight` -> `rc=0 lines=69 fails=0`. Class: a root that is not a repository (DEC-0068 a); git failure read as "in flight". `grep -c DEC-0068 findings.yml` at e17dc90 -> 0.
   - D33 hits for unchecked git rc in tools/gates: gate_plan.py:524, :530, :557, :582 (config --bool: a failure reads as true); gate_review_package.py:193 (ls-files: "a failing git prints nothing").
2. The fixer's 7 items are confirmed again by my runs: gate_claims.py:95 link write (FH.38); make_review_package.py:82 FIFO hang (FH.39); NAME `t\n` (FH.44); truncated diff.patch (FH.43); splitlines VT, FF, NEL, U+2028 (FH.29); silent FIFO or directory ledger skip (FH.64); `<plan>` a file PASSes (FH.62). The first is registered at e17dc90 (findings.yml :2375).

## Denials: none
## Background jobs: none. Timeouts went through subprocess.run(timeout=), which kills the child.
## Budget: about 155k tokens (the harness count governs); about 15 minutes of wall clock by `date` (10:46 -> about 11:00).
