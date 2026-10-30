status: DONE_WITH_FINDINGS
commits: none (table reviewer; I edited nothing but this report)
verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R-FH-1 (the whitelist data is false), R-FH-2/R-FH-3 (a cite quotes an option the owner did not pick; an OPEN cell that D32 settles), R-FH-8/9/10/11 (checklist pairs missing).
- quality ❌: R-FH-4 (15 "today" cells read OPEN or "not run" but can be observed; two of them hide a hang and a write outside the root), R-FH-5, R-FH-7, R-FH-12..R-FH-18.
- claims-executed ✅: I re-ran every today cell that can be observed (66 of 71) in my own guarded clone at f1057e5; details below.

Guard: clone $S/repo, where $S = /private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r14-tablerev-helper. It was made with `git clone -q --no-hardlinks` and `checkout -q f1057e5`. Every probe ($S/bin/p1.sh, p1b, p2..p7, sk.sh) starts with `set -eu` and asserts that `pwd -P` and the git toplevel lie under $S. All probes ran with PYTHONDONTWRITEBYTECODE=1. At the end the clone has a clean tree (`git status --porcelain --untracked-files=all` prints nothing) and `.git/info/attributes` is empty. No local config was left behind and no process is left over (`ps | grep r14-tablerev-helper` -> 0).

## gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) /usr/bin/python3 tools/gates/run_all.py` (clone at f1057e5) -> rc=0, 100 PASS, 0 FAIL. The brief's figure of 208 counts the main checkout, which holds git-ignored review packages; the clone holds none.
- No gate list applies to item C: it is plan text and has no code.

## Rows run / total: 66 / 71
Not run, with the reason:
- FH.55, FH.66, FH.69, FH.70, FH.71: nothing exists yet to observe (helper, export and generator are all unbuilt; FH.66 only points at other rows).
- FH.47 (populated case): needs a write under home. Only its absence was observed: `ls -l ~/.config/git/attributes` -> No such file; core.attributesFile unset.

## Claims (my observations; `GP` = /usr/bin/python3 $S/repo/tools/gates/gate_plan.py, `P` = the F0 plan)

| claim (draft) | direct | contradiction | output excerpt | verdict |
|---|---|---|---|---|
| D31 grep has 328 hits; second grep adds pre_tool_use.py:74,:278 | sk.sh (/usr/bin/grep, same regex, same 16 files) | the wider grep (io, stdin, environ, fnmatch, readlines, read/write, fdopen, unlink, chmod, symlink, readlink, expanduser) must hit nothing new | 328; :53 (comment), :74, :278. The wider grep adds gate_plan.py:364 fnmatch.fnmatchcase and user_prompt_submit.py:15 json.load(stdin) (R-FH-7) | PASS count / FAIL completeness |
| baseline --plan P | `GP --plan P` | n/a | rc=1 lines=53 fails=35 | PASS |
| FH.1 resolves against the root | `cd docs && GP(abs) --plan superpowers/plans/<P>` | `cd docs && GP --plan P` (root-relative) reads | `FAIL plan.file: superpowers/plans/...: no such plan file`; root-relative -> baseline. The draft's literal command (relative script path) -> `can't open file '.../docs/tools/gates/gate_plan.py'` (R-FH-5) | PASS (cell) / FAIL (command) |
| FH.2 absolute path inside the root | `GP --plan $S/repo/P` | an input FAIL would change the counts | baseline 53/35 (the draft read OPEN) | observed (R-FH-4) |
| FH.3/4/5/7/9/10/11/12 accepted (read as the baseline) | probes p1.sh | the same counts as the baseline mean nothing was refused | all rc=1 lines=53 fails=35 | PASS |
| FH.8 the argument is a link to a file outside the root | `ln -s $S/out/o.md olink.md; GP --plan olink.md` | as above | baseline (followed) (the draft read OPEN) | observed (R-FH-4) |
| FH.6, 13, 14, 15, 16, 17, 18 | p1.sh | the counts differ from the baseline | each `FAIL plan.file: <arg>: no such plan file` | PASS |
| FH.19 mode 000 | `GP --plan u.md` | readable control = baseline | `FAIL plan.input: [Errno 13] Permission denied: '$S/repo/u.md'` | PASS |
| FH.23 BOM, FH.25 CRLF, FH.27 no final LF accepted; FH.24 non-UTF-8 fails; FH.26 empty | p1.sh | n/a | baseline ×3; `FAIL plan.input: 'utf-8' codec can't decode byte 0xff`; rc=1 lines=12 fails=8 | PASS |
| FH.29 splitlines splits on VT, FF, NEL, FS, U+2028 | p1b.sh replaces the LF before `**Goal:**` with each of these characters | control: a space instead fails the header | each -> `PASS plan.header.goal`; control -> `FAIL plan.header.goal` (the draft read OPEN) | observed (R-FH-4) |
| FH.30/32 accepted; FH.31 subdirectory root fails | p1.sh | n/a | baseline; baseline (`--root` through a link is accepted); `FAIL plan.input: docs/superpowers/state/findings.yml: No such file or directory` | PASS |
| FH.33 worktree, FH.34 project dir set to a subdirectory | p7.sh | n/a | `rc=0 PASS register.schema`; `FAIL register.input: ... findings.yml: No such file or directory` | PASS |
| FH.35 a root under /tmp | `GP --root /tmp/<...>/repo --plan P`; `gate_register.py --root /tmp/...` | n/a | baseline; `rc=0 PASS register.schema` (the draft read OPEN) | observed (R-FH-4) |
| FH.20/21 | p2.sh | FH.21b: a non-empty 600 directory | `FAIL register.input: docs/reviews/d: Permission denied`; empty -> `rc=0 PASS`; non-empty -> `FAIL register.input: docs/reviews/d/a.md: Permission denied` | PASS |
| FH.14 findings.yml a FIFO hangs | p2.sh, timeout 10 s | a regular file returns at once | `TIMEOUT (hang)` | PASS |
| FH.23b/25b/26b/64 on findings.yml | p2.sh | n/a | BOM, CRLF and link -> `rc=0 PASS register.schema`; empty -> `FAIL register.input: ... empty file` | PASS |
| FH.60/61 | p2.sh | n/a | `FAIL decisions.scan.s.f: not a regular file`, `FAIL decisions.scan.s.md: cannot read: Permission denied`; a link -> `rc=0` | PASS |
| FH.62/63 | p2.sh | n/a | a directory link -> `rc=0 PASS`; a FIFO -> `FAIL register.input: docs/reviews/ff.md: not a regular file` | PASS |
| FH.56/57/58/59 | p2.sh | baseline: 11 t2 lines with both packages good | one file 000 -> 1 line total; non-UTF-8 -> t2 lines 0; task link -> 6 `p.t3.` lines; --in-flight -> 1 line vs a baseline of 69 | PASS |
| FH.36 + FH.43 | p3.sh: review -> $S/out2; `* -diff` | count `^@@` lines | `rc=0 wrote ...; gates PASS`; files in $S/out2/t; `^Binary files` 7, `^@@` 0 | PASS |
| FH.37 claims.yml as a link to outside: can the write be reached? | p3.sh: a verified claim citing CLAUDE.md at HEAD~1, claims.yml -> $S/out3/claims.yml, `gate_claims.py --mark-stale --base <HEAD~1>` | the outside file would keep `state: verified` if the write were refused | `PASS claims.marked-stale.C-demo-1`; outside file now `state: stale` (the draft read "write not reached; OPEN") | observed (R-FH-4) |
| FH.38 the target is a directory or a FIFO | p3.sh: diff.patch a directory; then a FIFO | n/a | dir -> `make_review_package: cannot write ...: Is a directory` rc=1; FIFO -> `TIMEOUT (hang)` (the draft read OPEN) | observed (R-FH-4) |
| FH.37b the target is a link to outside | diff.patch -> $S/out3/o.txt | outside file still `ORIG` if refused | outside file now `diff --git a/CLAUDE.md` | observed |
| FH.39 overwrite, FH.40 parent missing, FH.41 unwritable | p3.sh | n/a | a STALE diff.patch is overwritten; `.superpowers` absent -> created; 555 -> `cannot write ...: Permission denied` rc=1, no check id | observed (R-FH-4) |
| FH.42 partial write ("needs a full disk") | p5.sh `ulimit -f 4` + SIGXFSZ ignored | n/a | `cannot write ...: File too large`; diff.patch left at 4096 of 48859 bytes | observed (R-FH-4) |
| FH.44/45/46 | p4.sh | n/a | `git show` line 1 unchanged, `--textconv` -> TEXTCONV; filter: `git diff --stat` empty, status ` M ledger.md`; `* binary` -> 7 `^Binary`; `eol=crlf` -> no plan FAIL | PASS |
| FH.48 | p4.sh | n/a | core.excludesFile and info/exclude each hide zz.py (status 0 lines); host ~/.config/git/ignore -> `**/.claude/settings.local.json` | PASS |
| FH.49 | `git config --list --show-origin` | n/a | global `core.filemode=false`, `core.autocrlf=false`, `commit.gpgsign=true`; PLAIN = 5 keys (make_review_package.py:44-45); gitinfo :56 pins 1 | PASS |
| FH.51 git failure | `GP --root $S/out3 --plan docs/x.md`; `gate_register.py --root $S/out3` | n/a | `FAIL plan.input: docs/superpowers/state/findings.yml: No such file or directory`, same for register (the draft read OPEN) | observed (R-FH-4) |
| FH.52/53/54 | p4.sh | task `t\n` | plan LF passes NAME, later refused; CR and empty refused; task `t\n` -> `wrote .../review/t\n` (a directory named `t\n` was written) | PASS + R-FH-11 |
| FH.65 run_all isolation | findings.yml 000, run_all | baseline | each of claims, plan, register and review_package gives 1 FAIL; decisions 5 PASS, dispatch 15 PASS still run (the draft read OPEN) | observed (R-FH-4) |
| FH.67 id with a slash | `gate_decisions.py --scan sub/x.md sub/f.md` | n/a | `FAIL decisions.scan.sub/f.md: not a regular file` (the draft read OPEN) | observed (R-FH-4) |
| FH.28 errors=replace reads | docs/reviews/zz.md `register: \377 ...` | control with no 0xff | `FAIL register.marker.docs/reviews/zz.md: ... (found 'register: � B5-NOPE-1')` (the draft read OPEN) | observed (R-FH-4) |
| R-B-3 path (Coverage) | p6.sh: second ledger.md a FIFO | n/a | `rc=1 wrote ...: gates FAIL`: the FIFO is skipped silently and the package is written | observed (R-FH-12) |
| whitelist: 0 tracked 120000; 0 non-regular entries | `git ls-files -s \| awk '$1==120000' \| wc -l`; `find docs tools/gates .superpowers ! -type f ! -type d \| wc -l` (main checkout) | n/a | 0; 0 | PASS |
| whitelist: 110 files, every encoding count 0 | my listing (state/*.yml, plans/*.md, state/*/ledger.md, config.yml, .superpowers/sdd/*/review/*/*) + a byte check | n/a | 110; bom 0, cr 0, empty 0, nolf 0, bad 0, nonreg 0 (the draft gives no command: R-FH-15) | PASS |
| whitelist: `grep -l 'Binary files' ...diff.patch` -> 2 "packages a forced E4 would rewrite" | same command, main checkout | anchored `grep -l '^Binary files'` | now 3 (S2-b5-caps was regenerated 17:55, after the draft at 17:49); anchored -> 0. Every hit is diff text quoting the phrase (S2-T4 :248, :497, ...) | FAIL (R-FH-1) |

## Findings

**R-FH-1 (MEDIUM), D28 whitelist, last sentence.**
- Draft: "`grep -l 'Binary files' .superpowers/sdd/*/review/*/diff.patch | wc -l` -> 2 (packages a forced E4 would rewrite)".
- Observed: today the command gives 3, and the anchored form `grep -l '^Binary files' ... | wc -l` gives 0. The hits are added text lines (`+        self.put({"diff.patch": "diff --git a/x b/x\nBinary files a/x ...`, the DIFF_BODY regex, a Q26 table row). No package holds a binary stub.
- Replacement (hypothesis): "`grep -l '^Binary files' .superpowers/sdd/*/review/*/diff.patch | wc -l` -> 0 (no package holds a binary stub; a forced E4 rewrites none)".

**R-FH-2 (MEDIUM), FH.35 settled-by.**
- Draft: "DEC-0093 (option text "no change for /tmp paths")".
- That text belongs to option `below-root`. The chosen option is `below-root-plus-args`: "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root".
- Replacement: cite the chosen text ("one realpath of the root") and DEC-0068 a.

**R-FH-3 (MEDIUM), FH.32 and Q-FH-9.**
- Draft: "`--root` a link to the toplevel | realpath first then compare, or FAIL | OPEN Q-FH-9".
- D32 (spec :46): "a path argument that is itself a symlink (DEC-0093) FAILs that item's input check (`<pfx>.input`, as DEC-0066) naming the path". `--root` is a path argument (lib/__init__.py:133).
- Observed today: `GP --root $S/rootlink --plan P` -> baseline (accepted).
- Replacement: expected "FAIL `<gate>.input` naming the path | D32 (path argument itself a symlink)"; retire Q-FH-9. Q-FH-9's option a contradicts D32.

**R-FH-4 (MEDIUM), today cells D35.**
- Draft rules (b5r14-draft-common.md): "`OPEN` when nothing exists yet to observe or the observation needs an action you may not take".
- D35 (spec :49): "Every "today" cell carries the command that observed it, or reads OPEN". I report both quotes and do not pick between them.
- Cells reading "OPEN (not run)" or similar that I observed without any forbidden action: FH.2, .8, .28, .29, .35, .37, .38, .39, .40, .41, .42, .46 (binary, eol), .51, .65, .67.
- Two of them hide existing-code defects: FH.38 FIFO target -> `TIMEOUT (hang)`; FH.37 claims.yml link -> the outside file was rewritten to `state: stale`.
- FH.42 "needs a full disk" does not hold: `ulimit -f 4` gives `File too large` and leaves a truncated diff.patch (4096 of 48859 bytes).
- Replacement: the excerpts in the claims table above.

**R-FH-5 (LOW), FH.1 today command.**
- Draft: "`cd docs && GP --plan superpowers/plans/<P name>`" with "`GP` = `/usr/bin/python3 tools/gates/gate_plan.py`".
- Run literally: `can't open file '.../docs/tools/gates/gate_plan.py'`. The drafter's probe2.sh used an absolute path to the script, which reproduces the cell's output.
- Replacement: write the script path absolute in the cell.

**R-FH-6 (LOW), D32 cite form (check 3).**
- Draft line 14 quotes D32 as "...FAILs that item's input check (`<pfx>.input`) naming the path". It drops "(DEC-0093)" and ", as DEC-0066" without an ellipsis, and quotes only the first clause.
- Most cells (FH.52-65 rely on the fullmatch, isolation and git-forcing clauses) cite plain "D32" without D32's words.
- Draft line 3, the drafter prompt and the review rules all say "spec D32 :47". At f1057e5 `grep -n '^| D32 '` -> 46.
- Replacement: quote D32 in full once, at :46, and in each D32 cell name the clause it relies on, verbatim.

**R-FH-7 (MEDIUM), D31 skeleton completeness (check 1).**
- Filesystem hits the draft list maps to no item: gate_review_package.py:198 (`os.path.isfile`), :386 (`os.path.isdir`); session_start.py:38 (`os.path.islink`); gitinfo.py:22 (`os.path.exists`).
- Match sites not listed under E5 or Q-FH-17: gate_plan.py:73, :152, :364 (fnmatch.fnmatchcase, found only by the wider grep), :419, :433, :518; gate_review_package.py:70, :79, :168, :230; gate_register.py:112, :118, :178; gate_decisions.py:64; make_review_package.py:104, :128.
- hooks/user_prompt_submit.py (json.load(stdin) :15) is not named as a caller or as a no-fit site.
- Command: the draft's grep minus pure-string os.path calls -> 236 hits; 87 of them not cited by line (sk.sh + the mapping script; list above).
- Replacement: add these lines to E1, E2 and E5 (or to Q-FH-17 and Q-FH-20).

**R-FH-8 (MEDIUM), missing E2 kind row (check 2).**
- There is no row for a non-directory (file, FIFO, dangling link) where E2 expects a directory. FH.13-17 are E1.
- Spec §7 :203: "a non-directory where a directory is expected is [a missing input] (DEC-0022)".
- Coverage maps R-A-5 ("<plan> not a directory skipped") to FH.13, which is "E1 input | kind | directory".
- Replacement: row FH.72 "E2 dir | kind | file, FIFO, dangling link, socket | FAIL (missing input) | §7 :203, DEC-0022; D32 wrong kind", with a today on gate_review_package --all where `<plan>` is a file.

**R-FH-9 (MEDIUM), missing E4 object-state rows (check 2).**
- N/A list: "git object state at HEAD ... (git state reaches it only through E4 rows FH.43-51)". FH.43-51 hold no object-state value.
- Pairs without a row: HEAD blob of mode 120000 or 160000 read by `git show` (gate_plan.py:524), a path absent at HEAD, an unborn HEAD, a detached HEAD (gitinfo branch :50), index entries of E4 `ls-files -s` (intent-to-add, skip-worktree; gitinfo.py:89).
- DEC-0081 a settles the ledger's mode/type ("FAILs `plan.<slug>.ledger`"). Other E4 reads: OPEN Q-FH-21.

**R-FH-10 (LOW), text grammar.** E1 text has no EBNF row, and the N/A list does not argue one away. The D24 checklist names "text grammar written as a grammar".
- Replacement: one row writing DEC-0067 a as EBNF: `file = line, {line}; line = {char - LF}, LF; char = UTF-8 scalar - U+FEFF at offset 0`. Whether the last line needs its LF is Q-FH-7.

**R-FH-11 (MEDIUM), E3 path form (check 2).**
- No row covers the path form of the names that compose a write path (`plan`, `task`): case and Unicode on APFS, `..`, trailing LF.
- Observed: `make_review_package.py strict-delivery $'t\n' HEAD~1 HEAD --only gate_claims` -> `wrote .../review/t\n`. `ls review | od -c` shows `t \n`.
- D32 fullmatch settles the LF case (FAIL). Case and Unicode: OPEN Q-FH-22.

**R-FH-12 (LOW), Coverage.**
- Draft: "R-B-3 FH.14/.38". R-B-3 is the make_review_package.py:100 ledger scan, which skips a directory or FIFO ledger silently. FH.14 observes gate_plan and gate_register; FH.38 is a write target.
- Observed (p6.sh): a FIFO `docs/superpowers/state/zz/ledger.md` -> `rc=1 wrote ... gates FAIL`: the package is written and the FIFO is skipped silently.
- Replacement: an E2/R row for that scan, multi-item and kind, with this today.

**R-FH-13 (MEDIUM), question costs (check 6).** The rule asks for at least two options with a cost each. Options with no cost:
- Q-FH-4 b; Q-FH-5 b; Q-FH-6 a and b; Q-FH-7 b; Q-FH-8 a and b; Q-FH-9 b; Q-FH-10 a and b; Q-FH-11 a and b; Q-FH-12 b; Q-FH-16 a ("gate_plan's None paths change" is a consequence, not a cost) and b; Q-FH-17 a and b; Q-FH-18 a and b; Q-FH-19 a and b; Q-FH-20 a and b.
- Command: the draft at :98-117.

**R-FH-14 (MEDIUM), consistency with the other two drafts (check 7).**
- (a) The helper's surface (:3) names "the D34 pre-commit gate (other drafter)" as a caller. The pre-commit draft, PC.68: "a shell hook cannot import `tools/gates/lib` | OPEN Q-PC-17".
- (b) FH.41/Q-FH-11: "OPEN Q-FH-11 (D32 names "unreadable")". PC.31: "FAIL `pre-commit.input` | D32 (unwritable write target)". SG.52: "missing parent, a directory, a symlink, unwritable, a FIFO | input check FAILs | D32". FH.40 reads the missing parent as OPEN Q-FH-10. The three drafts read D32 three ways.
- (c) FH.70/Q-FH-19 ("generator lists E0-E5 calls") and the skeleton draft's SG.41/Q-SG-4 ("a file opened by a lib helper ... OPEN Q-SG-4") ask the same question under two ids. The skeleton draft names no FH dependency (`grep -c 'Q-FH' b5r14-draft-skeleton.md` -> 0).
- (d) PC.42: "`tools/.venv/bin/python3 -> python3.12` (below the root) | D32 would FAIL it; D34 requires it: OPEN Q-PC-10". The helper table has no row for a symlinked path that D34 itself mandates.
- Replacement: merge (b) into one owner question with all three readings; cross-reference Q-FH-19 = Q-SG-4; add FH rows for (a) and (d) citing Q-PC-17 and Q-PC-10.

**R-FH-15 (LOW), whitelist data without a command.**
- Draft: "over the 110 files gates read (...): BOM 0, CR 0, empty 0, no final LF 0, non-UTF-8 0". No command is given (D28: "set from data in the tree").
- I reproduced 110 and all 0 with `ls docs/superpowers/state/*.yml docs/superpowers/plans/*.md docs/superpowers/state/*/ledger.md tools/gates/config.yml .superpowers/sdd/*/review/*/*` + a byte check.
- Replacement: put that command in the draft.

**R-FH-16 (LOW), question settledness.**
- Q-FH-2 option a ("FAIL `<pfx>.input` unless normalised and inside") would void DEC-0093's chosen text "covers --plan outside the root"; the question must quote it.
- Q-FH-13: core.excludesFile is a config key, and D32 says "force their output forms (pinned config and attributes)". DEC-0098 a and DEC-0101 a place "the ignore and filter rows" in the hook's D24 table. The question must cite both and ask only about `.git/info/exclude` and the global ignore file as they reach E4.

**R-FH-17 (LOW), P-1 placement.** The plan :7221 names the P-1 row for the S2-b5-process-code tables. The draft defers it in Coverage ("the pre-commit table") but not in the N/A list with an argument. The pre-commit draft has it (PC.33, PC.34).
- Replacement: add the N/A line "P-1: the helper does not run interpreters; PC.33/.34".

**R-FH-18 (LOW), FH.67 expected verdict.**
- Draft: "path in the reason, never the id | DEC-0082 a". DEC-0082 a reads: "lib refuses an id outside `[A-Za-z0-9_.-]+` and a FAIL with an empty reason (FAIL `<gate>.input`)". DEC-0082 does not state "path in the reason".
- Today: `FAIL decisions.scan.sub/f.md: not a regular file`.
- Replacement: expected "FAIL `<gate>.input` (DEC-0082 a)"; whether the gate should move the path into the reason is a new question, or part of Q-FH-6.

## New questions (continue Q-FH)

| id | question | options (cost) | rows |
|---|---|---|---|
| Q-FH-21 | E4 reads of git objects that are not regular blobs (HEAD path mode 120000 or 160000, absent at HEAD, unborn or detached HEAD) | a: FAIL `<pfx>.input` naming the path (one mode check per E4 read, about 4 tests) / b: the caller decides, as gate_plan.py:524 returns None today (0 code; each gate's own table must cover it) | R-FH-9 |
| Q-FH-22 | Case and Unicode forms of names that compose an E3 write path (plan, task) on APFS | a: byte-for-byte per component, as DEC-0086 a for --dir (one listing per component, 2 tests) / b: accept what the filesystem resolves (0 code; `T` and `t` write into one package) | R-FH-11 |
| Q-FH-23 | One reading of D32 for write targets across the three tables: do "unwritable" and "missing parent" fall under D32's "unreadable, of the wrong kind" | a: yes, FAIL `<pfx>.input` (FH.40/41 settle; makedirs at make_review_package.py:160 becomes a refusal or stays as creation of a real directory; about 3 tests) / b: no, each stays an owner question per table (0 code now; three tables keep OPEN cells) | R-FH-14 b |

## Unrelated findings (existing code at f1057e5, not caused by this draft; for registration)

1. **tools/gates/gate_claims.py:95** writes claims.yml through a symlink to outside the root.
   - Repro (p3.sh): claims.yml -> $S/out3/claims.yml holding one verified claim citing CLAUDE.md at HEAD~1; `gate_claims.py --mark-stale --base <HEAD~1>` -> `PASS claims.marked-stale.C-demo-1`, and the outside file now reads `state: stale`.
   - Class: write target with a symlink below the root (D32). It is a sibling of R-B-2, which names only make_review_package.
   - `grep -c 'gate_claims.py:95' findings.yml` at 6a7b2f9 -> 0.
2. **tools/gates/make_review_package.py:82** `_write` blocks on a FIFO target.
   - Repro: `mkfifo .superpowers/sdd/strict-delivery/review/t/diff.patch; make_review_package.py strict-delivery t HEAD~1 HEAD --only gate_claims` -> hang (killed after 30 s).
   - Class: non-regular write target (D32 wrong kind). The drafter's #1 (lib/__init__.py:105 load_yaml hangs on a FIFO findings.yml) is confirmed: `TIMEOUT (hang)`.
3. **tools/gates/gate_plan.py:247** `text.splitlines()` treats VT, FF, NEL, FS and U+2028 as line ends.
   - Repro: p1b.sh -> `PASS plan.header.goal` where the Goal line follows a VT, not an LF; the control with a space -> FAIL.
   - Class: line terminators (DEC-0067 a "LF only").
   - D33 hits of `splitlines(` in tools/gates: gate_plan.py:247, :517, :627; gate_review_package.py:58, :68, :168, :271; make_review_package.py:104, :148; lib/__init__.py:51; gitinfo.py:62, :154.
4. **tools/gates/make_review_package.py:160-163** leaves a truncated package after a failed write.
   - Repro: `ulimit -f 4` -> `cannot write ...: File too large`, rc=1; diff.patch stays at 4096 of 48859 bytes.
   - Class: partial write (Q-FH-12).
5. **make_review_package.py:42** NAME (`$`-anchored, `.match`) accepts task `t\n` and writes the directory `review/t\n`.
   - Class: name grammar incl. line terminators (DEC-0089 widens B5-FIX-9). It is a sibling of R-B-13 at the task argument.
   - D33 hits of `$`-anchored patterns used with `.match`: make_review_package.py:42/:137; gate_plan.py:61/:408, :82/:156; gate_dispatch.py:39/:201.
6. **Check ids carrying a path** (DEC-0082 a grammar `[A-Za-z0-9_.-]+`).
   - Hits: gate_decisions.py:72, :79, :83 (`decisions.scan.{rel}`); gate_register.py:181 (`register.marker.<relpath>`), :188 (`register.report.{rel}.{rid}`).
   - Repro: `--scan sub/f.md` -> `FAIL decisions.scan.sub/f.md`.
   - Class: result-id grammar. DEC-0082 is answered and not yet implemented.

## Denials: none
## Background jobs: none started. Timeouts went through subprocess.run(timeout=), which kills the child; `ps` afterwards -> 0 leftover.
## Budget: about 130k tokens by my estimate (the harness count governs); about 40 minutes.
