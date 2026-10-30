status: DONE_WITH_FINDINGS
commits: none (task reviewer; the only file I wrote is this report)
verdict: spec: ❌ quality: ❌ claims-executed: ✅
- spec ❌: R3-FH-1 (FH.77 takes one side of a divergence between two approved sheet answers, Q-PC-17 (b) and Q-PC-1 (c); the Q-FH-18 row does not record Q-PC-1 (c)). R3-FH-2 (FH.28 states an id, `<pfx>.encoding`, that Q-FH-8 (a) does not state and that FH.24 keeps OPEN under Q-FH-6).
- quality ❌: R3-FH-3..R3-FH-6.
- claims-executed ✅: I re-ran every ‡ cell (FH.10b, 41, 42b, 44, 48, 53, 54d, the R-FH-12 fix note) and the whitelist loop at 2e43f13. I also checked every line cite the round added (spec, decisions.yml, sheet, plan and code). All reproduce; the outputs are below.

Guard (DEC-0032): $S = /private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r17-rev/b5r17-rr2-helper.
- Clone `$S/repo` made with `git clone -q --no-hardlinks` and `checkout -q 2e43f13`.
- Every probe ($S/bin/p1.sh..p4.sh) sources $S/bin/guard.sh, which runs `set -eu` and asserts that `pwd -P` and `git rev-parse --show-toplevel` lie under $S. Probes ran with PYTHONDONTWRITEBYTECODE=1 and a fresh PYTHONPYCACHEPREFIX under $S/pyc.
- The clone ends with 0 porcelain lines after each probe.
- The main checkout was only read. It is at 2e43f13 with 0 porcelain lines.

## Gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d $S/pyc/x.XXXX) /usr/bin/python3 tools/gates/run_all.py` in the clone at 2e43f13 -> rc=0, 100 PASS, 0 FAIL. The fixer's 208 PASS was in the main checkout, which holds git-ignored packages.
- No other gate applies: the change is a workspace text draft (git-ignored: `git check-ignore -q` -> ignored).

## Diff of the round
`diff r17-pre/b5r14-draft-helper.md <draft>`:
- 32 FH rows changed: FH.1, 3, 4, 5, 10, 11, 12, 18, 21, 27, 28, 30, 33, 37, 38, 39, 41, 42, 43, 44, 48, 50, 51, 52, 53, 54, 58, 64, 70, 76, 77, 78.
- 7 rows added: FH.10b, 42b, 43b, 53b, 54b, 54c, 54d.
- Also changed: the header, the skeleton, the whitelist, the Open questions table, the R-FH-12 fix row, and the appended Fix round 2 table.
- Counts: `wc -l` -> 296; `grep -c '^| FH\.'` -> 85. Every FH row has 7 cells (python pipe count excluding `\|` -> no exceptions).

## Check 1: each R2-FH fix holds
| finding | result | direct | contradiction | output |
|---|---|---|---|---|
| R2-FH-1 | holds | p2.sh: `GP --root $S/ng2 --in-flight` (registers, plans and specs copied, no .git) | in-repo control `GP --in-flight`; `gate_register.py --root $S/ng2` | `rc=0 lines=69 fails=0` both; `FAIL register.input: git cat-file -e 151f2bb^{commit}: fatal: not a git repository` |
| R2-FH-2 | holds | `sed -n 46p spec` | `grep -c make_review_package spec` | :46 reads "Default contract (DEC-0088 O2; amends D25). For every gate: ..."; D32-a and the "rest of :46" quote are verbatim; 0 hits |
| R2-FH-3 | holds | p2.sh: `* eol=crlf`, `* text`, `* export-subst` in .git/info/attributes | no attributes | each: `^Binary files` 0, diff sha ab563e77, show sha 399bfc13, plan rc=1 lines=53 fails=35; with no attributes the same values. `* binary` at 2e43f13 -> 1, 7e62d03e. The cell's "6" is marked † (6a7b2f9, a different HEAD~1..HEAD), so it is labelled correctly. p1.sh, parent 555: files present -> rc=0 `gates PASS`; empty -> `cannot write ...review/t: Permission denied` rc=1 |
| R2-FH-4 | holds | `sed -n <l>p` | `grep -n '\.match(\|\.search('` gate_register/gate_decisions | :110 `os.path.exists(os.path.join(root, ".GIT"))`, :189 `os.path.isfile(...)`, pre_tool_use :295 `os.path.realpath(path)`, gate_register :112/:118/:178 and gate_decisions :64 `.fullmatch`; the remaining `.match` sites (:81, :115) are line grammars, filed under line grammars |
| R2-FH-5 | holds | `sed -n 4446,4448p` plan | p1.sh `MRP ..` | :4448 `NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")` beside `IMPLEMENTER_FILES` (make_review_package code); `task '..' is not a directory name (...)` rc=1 |
| R2-FH-6 | holds | p2.sh FIFO `state/zz/ledger.md`, `--only claims` | `--only gate_claims` | rc=0 `gates PASS`; rc=1 `gates FAIL`, gates.txt `FAIL run_all.unknown: no gate named 'gate_claims'` |
| R2-FH-7 | holds | `sed -n 913p;917p;943p;947p decisions.yml` | -- | 913 scope-default-config, 917 its answer, 943 drop-the-promise, 947 its answer; the quoted words are in each option |
| R2-FH-8 | holds | the draft's loop, verbatim, from the main checkout | the same tests on planted files a (BOM+CRLF), b (no LF), c (0xff), d (empty) | `files=110 bom=0 cr=0 empty=0 nolf=0 bad=0`; a bom=1 cr=1, b nolf=1, c bad=1, d empty=1 |
| R2-FH-9 | holds | sheet :228-229 vs the draft's Q-FH-10/11 rows | -- | options as the sheet carries them; Q-FH-11 "survive at no source" (see R3-FH-5) |
| R2-FH-10 | holds | p2.sh: detached clone `run_all.py` | `rev-parse --abbrev-ref HEAD` | `HEAD`; rc=0, 100 lines, 0 FAIL |

## Check 2: applied sheet answers (quote and verdict)
Sheet sha256 `8bef9f6229b34de2...` equals DEC-0129's question (decisions.yml :1373). DEC-0129's answer (:1379) is approve-except-flagged (A1-1b, Q-FH-10, Q-FH-11, S-D-1).

Line cites checked with `sed -n <l>p | cut -c1-12`: Q-D32-W :165, Q-SG-21 :166, Q-FH-24 :167, Q-PC-10 :178, Q-PC-17 :185, Q-FH-1 :219, Q-FH-9 :227, Q-FH-10 :228, Q-FH-12 :230, Q-FH-19 :237, Q-FH-21 :239, Q-SG-18 :260. All hold.

| row(s) | sheet answer | verdict the option gives vs the cell | result |
|---|---|---|---|
| FH.1 | Q-FH-1 a "the root, as today" | resolve against the root | PASS |
| FH.3-5 | Q-FH-2 b "accept" | accepted | PASS |
| FH.11, 12 | Q-FH-3 a "byte for byte per component for every argument" | FAIL; id Q-FH-6 | PASS |
| FH.18 | Q-FH-4 a "`<pfx>.input`" | FAIL `<pfx>.input` | PASS |
| FH.21 | Q-FH-5 a "FAIL always" | FAIL, empty or not | PASS |
| FH.27, 30 | Q-FH-7 a "FAIL `<pfx>.encoding`" | as stated | PASS |
| FH.28 | Q-FH-8 a "yes: reports, test files, blocks, hook files and stdin" | the cell adds the id `<pfx>.encoding` | FAIL (R3-FH-2) |
| FH.33 | Q-FH-9 SETTLED, quoted | FAIL, id Q-FH-6 (as the row's why) | PASS |
| FH.10b, 41, 42, 42b, 43, 43b | Q-D32-W (c) W1-W5, quoted with option (a) and Q-SG-21 (a) verbatim (checked against :165, :166) | W1-W4 FAIL `<pfx>.input`; W5 accepted. FH.41 keeps OPEN Q-FH-10 and FH.42 records Q-FH-11 (flagged); both record the divergence and do not pick | PASS (FH.43b today: R3-FH-3) |
| FH.43 | Q-FH-12 a "temp + rename" | no truncated target | PASS |
| FH.50-52 | Q-FH-13 a, -14 a, -15 a | as the options | PASS |
| FH.53 | DEC-0068 a (:635), quoted | FAIL `<gate>.input` | PASS |
| FH.53b | Q-FH-16 a | FAIL `<pfx>.input` | PASS (today: R3-FH-6) |
| FH.54, 54b, 54c | Q-FH-21 a; the 54b exception quotes the row's why (findings.yml :599 sibling "ledger absent at HEAD") | as stated | PASS |
| FH.58 | Q-FH-17 b | fullmatch for ids, names, shas, budgets; line grammars per table (D24 :38, the row's why) | PASS |
| FH.70 | Q-SG-18 a (= Q-FH-19 a) | the helper calls are listed | PASS |
| FH.76 | Q-FH-20 a | one entry point each | PASS |
| FH.77 | Q-PC-17 b "(Q-FH-18 must give an export root mode)" | "its export root needs an E0 mode" | FAIL (R3-FH-1) |
| FH.78 | Q-PC-10 a "accept the two W3 paths as named, resolving the link". The second W3 path is /usr/bin/python3 (precommit draft PC.39), outside the root, so it is not this table's | as stated | PASS |
| FH.37-39, 44, 64 | Q-FH-24 a | D32 cells stand; the "one spec line" absence is stated (`grep -c` -> 0) | PASS |

## Check 3: still-open and new Q-FH questions
- Q-FH-6 and Q-FH-18: the sheet reads "no recommendation" (:224, :236). Both are real, with costed options. Q-FH-18's options are stale against Q-PC-1 (c) (R3-FH-1).
- Q-FH-10 and Q-FH-11: flagged, so they stay open. Q-FH-10 has options but no costs; Q-FH-11 has no options (R3-FH-5).
- Q-FH-25 (new): two costed options. Whether it is real depends on the reach of Q-FH-12 (a) (R3-FH-4).
- Q-FH-26 (new): real.
  - Direct: the sheet's Q-FH-21 question lists "unborn or detached HEAD", and its option a is "FAIL `<pfx>.input` naming the path (one mode check per E4 read)".
  - Contradiction: under a detached HEAD the gates read regular blobs, and a mode check would find nothing to FAIL. Observed: a detached clone gives rc=0 and 100 lines.
  - So applying a to a detached HEAD needs a rule that a does not state (D25). The index states are in neither row.
  - Three costed options.

## Findings
**R3-FH-1 (MEDIUM, spec): FH.77, FH.75 and Q-FH-18 against the approved Q-PC-1 (c).**
- Sheet :185, Q-PC-17 option b (approved): "a thin shell shim exec'ing a Python hook that uses the helper (Q-FH-18 must give an export root mode)".
- Sheet :168, Q-PC-1, recommendation c (approved; not flagged): "`clone --shared` + `read-tree` of the index tree (a full toplevel ...)". Its why: "c gives a toplevel (DEC-0068 a ...) with no export mode and no admin writes".
- Draft FH.77 expected: "its export root needs an E0 mode (OPEN Q-FH-18)". This takes Q-PC-17 b's side.
- The draft never cites Q-PC-1 (c): `grep -c 'Q-PC-1 ' draft` -> 0.
- The Q-FH-18 row's option b still reads "the export is a worktree ... the export uses `git worktree add`". That is Q-PC-1's option a, which the owner did not choose.
- The two approved answers diverge; I do not pick.
- Replacement (hypothesis): FH.77 states the expected verdict without "needs an E0 mode", and the export-root question stays at Q-FH-18. Q-FH-18 quotes both lines (Q-PC-17 b's parenthetical, Q-PC-1 c's "no export mode"), its option b reads as Q-PC-1 (c) gives it, and it stays OPEN (no recommendation).

**R3-FH-2 (MEDIUM, spec): FH.28 adds an id rule.**
- FH.28 expected: "a non-UTF-8 byte FAILs `<pfx>.encoding` naming the file". Before this round it read "DEC-0067 applies, or not".
- Q-FH-8 (a) answers the reach ("yes: reports, test files, blocks, hook files and stdin"), not the id.
- FH.24 (unchanged), the same value on the other reads: "FAIL naming the file; id `<pfx>.encoding` or `<gate>.input` | DEC-0067, DEC-0085; id OPEN Q-FH-6".
- The two sources: DEC-0067 a (:626) "else FAIL naming the file (new `<pfx>.encoding`)"; DEC-0085 a (:791) "FAIL `<gate>.input` naming the file" for a file that "cannot be read or parsed".
- The table now gives one value two id verdicts.
- Replacement: FH.28 reads "FAIL naming the file; id OPEN Q-FH-6 (as FH.24)".

**R3-FH-3 (LOW, quality, D35): FH.43b's OPEN reason is false.**
- Cell: "the only outside-root writer, pre_tool_use.py:278 `tempfile.mkstemp`, makes a fresh name, so no call site writes a pre-existing outside-root target to observe".
- Contradiction, p3.sh at 2e43f13: `echo PRE-EXISTING > $S/out4/o.txt; ln -s $S/out4/o.txt review/t/diff.patch; MRP` -> rc=0 `gates PASS`. `head -c 40 o.txt` -> `diff --git a/docs/superpowers/state/deci`. make_review_package.py:82 overwrote a pre-existing file outside the root.
- FH.38 already records the same for gate_claims.py:95 (claims.yml link, outside file now `state: stale`).
- The value is observable through a link below the root, which D32-a also FAILs.
- Replacement: cite this observation, or "= FH.38", in the today cell.

**R3-FH-4 (LOW, quality): Q-FH-25 does not show why the approved answers leave it open.**
- Q-FH-12 (a), approved: "temp + rename" as the write form. The answer states no scope limit.
- W1 as (a): an unwritable target FAILs `<pfx>.input`.
- Observed in p1.sh: `mv` and `os.replace` into a 555 directory -> `Permission denied` (rc=1, PermissionError). So under Q-FH-12 a, wherever the temp file lives, the write fails: that is option a, by W1.
- Q-FH-25 option b says itself that it goes "against Q-FH-12 a's aim", which reopens an approved answer.
- Two readings:
  - (i) Q-FH-12 a covers every E3 write. Then Q-FH-25 is settled (a).
  - (ii) Q-FH-12 a covers partial writes only. That is the scope of its question, "Partial writes: the write form". Then Q-FH-25 is real.
- I do not pick. The row should state reading (ii) as its premise, and say that option b amends Q-FH-12 a.

**R3-FH-5 (LOW, quality): Q-FH-10 and Q-FH-11 are open without costed options.**
- Draft table header: "options (at least two, each with its cost)". The strict block's Decision-needed form asks for "at least two options and their cost".
- Q-FH-10: "a create (makedirs, as today) / b FAIL [no cost survives at any source]". Q-FH-11: "its a/b options and costs survive at no source".
- fix-common rule 3: flagged rows "stay in the Open questions table verbatim". The fixer followed it.
- Divergence; I do not pick. Further facts:
  - The sheet's Q-FH-10 row reads "asked only if Q-D32-W (b)", and Q-D32-W was answered (c), clause 2 as (a).
  - Q-FH-11's "which id?" is answered by W1 (a) "`<pfx>.input`" up to Q-FH-6.
  - Both rows wait on the sheet's flagged-row text round (R2-D-1/2). That round, or the controller, must restate their options and costs before they go back to the owner.

**R3-FH-6 (LOW, quality): FH.53b today reads OPEN, but one command observes it.**
- The cell reads "OPEN: no in-repo git failure was induced".
- p4.sh at 2e43f13: `printf garbage > .git/index; git ls-files` -> rc=128. `gate_plan.py --in-flight` -> rc=0, lines=69, fails=0; the first line is `PASS plan.strict-delivery.header.agentic` (fail-open).
- The index was restored afterwards and the clone has 0 porcelain lines.
- Already registered: B5-FIX-5 dimension 2 "index unreadable" (findings.yml :585).
- Replacement: put this command and output in the cell and cite B5-FIX-5.

## Claims
| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| HEAD 2e43f13, clean | `git rev-parse HEAD`; porcelain count | clone `git log --oneline -1` | 2e43f132...; 0; 2e43f13 | PASS |
| gate code unchanged since f1057e5 | (from the fixer; spot check) `run_all` 100 PASS in the clone | -- | rc=0 | PASS |
| FH.10b ‡ | p1.sh: cd `/tmp/...repo`, `MRP` | `git rev-parse --show-toplevel` | `wrote ... gates PASS`; `/private/tmp/...` | PASS |
| FH.41 † | p1.sh: no `.superpowers`, `MRP` | `ls review` | rc=0; `t` | PASS |
| FH.44 ‡ | `MRP ..`; `MRP T` then `MRP t`; `MRP $'t\n'` | `ls review`, `od -c` | rc=1 NAME refusal; both rc=0, one dir `T`; rc=0, `t \n` | PASS |
| FH.48 ‡ | p2.sh (4 attributes) | no attributes | as Check 1 | PASS |
| FH.53 ‡ | p2.sh ng2 | in-repo control; gate_register | as Check 1 | PASS |
| FH.54d ‡ | p2.sh detached run_all | abbrev-ref | rc=0, 100 lines, 0 FAIL | PASS |
| whitelist loop | verbatim run | planted files | files=110, all 0; detectors fire | PASS |
| no answered id left OPEN | `grep '^\| FH\.' \| grep -o 'OPEN Q-[A-Z0-9-]*' \| sort \| uniq -c` | the sheet's open set | Q-FH-6 ×9, -10 ×1, -11 ×1, -18 ×2, -25 ×2, -26 ×2 | PASS |
| spec D31-D35 at :45-:49 | `grep -n '^\| D3[1-5] '` | -- | 45..49 | PASS |
| decisions cites :865/:867/:869/:913/:917/:943/:947 | `sed -n` | -- | as cited | PASS |
| plan :3795, :4448, :7185 | `sed -n` | -- | the Produces line; NAME; "rows for stale bytecode (P-1: ..." | PASS |
| skeleton cites | `sed -n` per line | -- | as Check 1 | PASS |
| user_prompt_submit.py:15 stdin; GATES; hook shebang; venv link | `sed -n 15p`; `grep -n GATES`; `head -1`; `ls -l` | -- | `json.load(sys.stdin)`; six gates; `#!/bin/sh`; `python3 -> python3.12` | PASS |
| fixer's "registration not re-checked" for the non-repository fail-open | `grep -n 'not a repository\|non-repository' findings.yml` | -- | B5-FIX-5 :586, :591 (DEC-0068 a dimension 2); B5-UR-* :2650-2668 for other gates: registered | PASS |

## Findings raised
R3-FH-1, R3-FH-2 (MEDIUM); R3-FH-3, R3-FH-4, R3-FH-5, R3-FH-6 (LOW). No new Q ids. R3-FH-1 and R3-FH-4 are divergences for the controller or owner; I do not pick.

## Unrelated findings
- docs/superpowers/state/findings.yml:591 (B5-FIX-5) still reads "for a git failure inside a repository the rule is open (Q-FH-16, owner)". The sheet's Q-FH-16 (a) is now approved by DEC-0129.
  - Reproducer: `sed -n 591p findings.yml`; DEC-0129 :1379.
  - Class: register text not updated after an owner answer. This is for the controller's next state commit, not a code defect.

## Denials: none
## Background jobs: none
## Budget used: about 95k tokens of 250k; about 20 minutes of wall clock of 30 (`date` at the end of the probes: 20:11 SAST).
