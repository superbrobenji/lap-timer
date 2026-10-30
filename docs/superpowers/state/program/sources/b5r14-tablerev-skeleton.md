status: DONE_WITH_FINDINGS

Table reviewer SG, b5 round 14 item C (B5-PROC-2, D31 skeleton generator). Draft reviewed: `b5r14-draft-skeleton.md` (99 lines, 59 rows SG.1-.59, Q-SG-1..16). Base f1057e5. Guarded clone `<R>/repo` (`<R>` = scratchpad/r14-tablerev-skeleton), DEC-0032 guard on every write script; clone clean at the end (`git status --porcelain --untracked-files=all --ignored | wc -l` -> 0).

Verdict: spec: ❌ quality: ❌ claims-executed: ❌
- spec ❌: (item, dimension) pairs with no row and no N/A (R-SG-2); plan :7221 named rows missing or weakly argued N/A (R-SG-3); OPEN cells that answered DECs reach, which the FH draft cites for the same dimension (R-SG-4); SG.52 and SG.59 cite D32 for cases D32 does not word (R-SG-5, R-SG-6).
- quality ❌: no row or question for calls made through the D32 helper (R-SG-7, FH.70); 13 question options have no cost (R-SG-8).
- claims-executed ❌: I re-ran 59 of 59 rows. SG.13's "today" cell does not hold for `/usr/bin/grep` (R-SG-1). SG.18 and the `_modes_agree` cite do not hold as written, and some counts are off (R-SG-10, R-SG-11). The other cells reproduced.

## commits
none (review only; I edited nothing but this report).

## gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<R>/pfx /usr/bin/python3 tools/gates/run_all.py` in the clone at f1057e5 -> rc 0, 100 PASS, 0 FAIL. The clone has no git-ignored review packages; the brief's 208 counts the checkout's packages.
- The brief gives item C reviewers no gate list. The draft is plan text with no code, so I did not run the unittest suite.

## Check 1: skeleton completeness (re-run)
- `/usr/bin/grep -nE "$G"` gives 29 lines on gate_plan.py and 37 on make_review_package.py. The line lists match the draft.
- Manual reads: in `_package` :135-166 I counted 6 grep lines, 4 args, 4 git runs and 3 writes, as the draft says. `_ignored`/`_range` give 13 when isfile and read count separately. `plans_in_flight` has 3 grep lines (:611 :612 :621), as the draft says.
- `bash <R>/forms.sh <R>/repo` (31 forms over the 16 .py files) reproduces the draft's counts: subprocess 16, open( 15, listdir 7, glob.glob 3, os.walk 1, sys.argv 1, add_argument 23, parse_args 8, gitinfo. 19, stdin 4, os.environ 4, tempfile 2, importlib 2. It also reproduces 0 for pathlib, scandir, shutil, io.open, parse_known_args and shell=True. Three exceptions are in R-SG-10.
- AST (`<R>/ast_count.py`): 15 subprocess calls (12 List, 3 Name at gitinfo.py:30 :106 :143). make_review_package has 12 filesystem calls, gate_plan 14, and none takes a literal path. This matches the draft.
- Shell: the draft's grep is reproduced. Some commands in git-pre-commit.sh are missed (R-SG-9).

## Check 5: whitelist (re-run)
- `ls tools/gates/gate_*.py | wc -l` -> 6, equal to `run_all.GATES` (run_all.py:23, 6 names).
- The 16-file `ls` -> 16, with `__init__.py` 0 bytes.
- The shell `ls` -> 3.
- `find tools/gates -maxdepth 2 -type l | wc -l` -> 0.
- All counts hold. The 16-file count depends on ignore state (R-SG-3).

## claims (claim | direct | contradiction | output | result)
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| D31 :45, D32 :46 | `sed -n 45,46p spec` | `sed -n 47p` is D33 | `| D31 |`, `| D32 |`; :47 `| D33 |` | PASS |
| SG.13: by-hand grep hides every call in a non-UTF-8 file | `/usr/bin/grep -nE 'open\(' latin2.py` | assume grep hides: rc 1 expected | `1:open("a")` rc 0 (LANG en_ZA.UTF-8; LC_ALL=C and en_US.UTF-8 the same); session `grep` function -> rc 1 no output | FAIL (R-SG-1) |
| SG.1/.2/.6/.23 | probe1.sh | a different rc | `No such file` rc 2; `Is a directory` rc 2; `Permission denied` rc 2; others printed rc 2 | PASS |
| SG.3 FIFO blocks | `perl -e 'alarm 3; exec @ARGV' /usr/bin/grep ... fifo.py` | rc 0 or 2 | rc 142 | PASS |
| SG.3 socket OPEN | bind via a relative path after chdir | bind fails | bound; `/usr/bin/grep` -> `Operation not supported on socket` rc 2 | observable (R-SG-11) |
| SG.4/.5 symlinks followed | probe1/probe2 | rc 2 | `2:open("a")` rc 0; `20:import argparse` rc 0 | PASS |
| SG.7-.10 | probe2 | other output | /etc/hosts rc 1 silent; 29; 29 with the spelling as given; NFD file found through its NFC name | PASS |
| SG.12 | `env CLAUDE_PROJECT_DIR=/nonexistent .venv python3 -I ... repo_root()`; from tools/ | other root | `/nonexistent`; `<R>/repo` | PASS |
| SG.14 CRLF; BOM OPEN | probe1 | — | CRLF `2:open("a")`; BOM `1:﻿open("a")` rc 0, `ast.parse` accepts | PASS / BOM observable (R-SG-11) |
| SG.15 | grep; `ast.parse` | — | `2:  open("x")` rc 0; SyntaxError | PASS |
| SG.16 | grep empty.py | — | rc 1, no output | PASS |
| SG.17 | grep lib/config.py | — | :14, :15 rc 0 | PASS |
| SG.18 `printf 'open("WT_ONLY")' >>` -> :172 | as written | the file has 170 lines | `171:open("WT_ONLY")` | FAIL as written (R-SG-11) |
| SG.19 deleted; mode/type OPEN | rm; chmod +x; symlink | — | `No such file` rc 2; 6 lines; 6 lines (followed) | PASS / observable (R-SG-11) |
| SG.25 once per file named | the same file twice | — | 4 lines (2 x 2) | PASS |
| SG.29 one false hit | `/usr/bin/grep -nE glob` over the 16 files | — | 6 false hits | FAIL count (R-SG-10) |
| SG.32-.35 | probe1 | — | aliased `sp.run` missed; `o("y")` missed; only `1:r = open(`; 2 false hits | PASS |
| SG.39 6 `_git` call sites | `grep -n '_git('` | — | :63 :73 :111 :148 :154 :162 | PASS |
| SG.42 `--root` not in gate_plan | `grep -c -- --root` | — | 0 | PASS |
| SG.50 order varies | session `grep` 6 runs; `/usr/bin/grep` 3 runs | — | 2 orders; argv order fixed | PASS |
| SG.57 6 clauses | sed :38 | — | 6 | PASS |
| `_modes_agree` :631 | `grep -n _modes_agree` | — | call :632, def :586; plans_in_flight ends :634 | FAIL cite (R-SG-11) |
| "import aliases 0" | `grep -E 'import [a-z_.]+ as '` | — | gate_dispatch.py:21, gate_plan.py:35 | FAIL (R-SG-10) |

Rows run: 59 of 59. Every non-OPEN cell was re-run. For the OPEN cells I checked the reason: SG.3 (socket), SG.14 (BOM) and SG.19 (mode, type) were observable. SG.11 stays OPEN (the guard allows clones only). SG.20-.22, .24, .51-.56, .58 and .59 have no existing code to observe.

## findings
**R-SG-1 HIGH, SG.13 (and the Q-SG-13 (c) cost).**
- Draft: "`grep -nE 'open\(' latin2.py` (open :1, 0xff :2, listdir :3) -> no output rc 1: every call hidden".
- Drafter report: "`/usr/bin/grep -nE 'open\(' latin2.py` ... -> rc 1, no output".
- Observed: `/usr/bin/grep -nE 'open\(' latin2.py` -> `1:open("a")` rc 0, and the full `$G` -> `1:` and `3:` rc 0. Only the session shell's `grep` function (BSD `/usr/bin/grep` is 2.6.0-FreeBSD) prints nothing with rc 1.
- Replacement (hypothesis): "`/usr/bin/grep -nE "$G" latin2.py` -> `1:open("a")`, `3:os.listdir("b")` rc 0; the session shell's grep function -> no output rc 1. The by-hand result depends on which grep runs: the skeleton names `/usr/bin/grep`." Q-SG-13 (c) loses its "hides every call" cost. The drafter's unrelated finding is wrong for `/usr/bin/grep`.

**R-SG-2 MEDIUM, pairs with no row and no N/A (D24 + D31; check 2).**
- A2: git object state, index/working tree, git config, repository state, file type/permissions/symlinks, non-regular, encoding (a non-UTF-8 argv value), path form, isolation.
- I1: file type/symlinks (a symlinked lib module), non-regular, encoding, path form, git object/index (Q-SG-11 applies to lib sources too), regex anchoring.
- I2: encoding (folded into SG.59's "file type"), path form, index/working tree, regex anchoring of row detection when parsed from the spec, isolation.
- T: regex anchoring (`glob` matches `global`, SG.29).
- O1: git object/index/repository state when Q-SG-10 (b) writes inside the repo, path form of the target, isolation of one gate's rows in a multi-gate run.
- R: kind and symlink of the root (DEC-0093 option text: "runs through /tmp or /var on macOS"; FH.32, FH.35), a root that is not a repository, git config.
- Replacement: one row or one argued N/A line per pair.

**R-SG-3 MEDIUM, plan :7221 named rows (check 2).**
- Plan :7221 names "ignore files (`.git/info/exclude`, `core.excludesFile`, the global `~/.config/git/ignore`)". The N/A list argues only the global files. `.git/info/exclude` and `core.excludesFile` have neither a row nor an N/A.
- Repro: I added `tools/gates/zz_scratch.py` to `.git/info/exclude`. The whitelist's `ls ... | wc -l` -> 17, `git ls-files 'tools/gates/*.py'` (no tests) -> 16, and `git status --porcelain --untracked-files=all` -> 0 lines. So under Q-SG-3 (c) the source set changes with the ignore files, and the 16 in the whitelist holds only with no such file.
- The review packages `run_all` reads are not mentioned at all.
- The stale-bytecode N/A ("while Q-SG-2 is a source scan") is weak. The generator is itself Python that imports `tools.gates.lib` (item R "if reused"; D32 "One shared fail-closed helper in `tools/gates/lib` enforces it"), so P-1 reaches its own imports whatever Q-SG-2 says.
- Observed: a `tools/.venv/bin/python3 -I` probe with PYTHONDONTWRITEBYTECODE=1 wrote `tools/gates/__pycache__/__init__.cpython-312.pyc` (in my clone, since removed, and at 17:47 in the drafter's clone).
- Replacement: rows A1/I1 x ignore files (with Q-SG-19), R x P-1 (Q-SG-20), and a review-packages N/A line ("the generator reads no `.superpowers/sdd/*/review/`; under Q-SG-10 (b) its write target sits next to them").

**R-SG-4 MEDIUM, answered DECs left out of OPEN cells (checks 3, 7).** The drafter rules say "DEC-0066..0086 are answered too ... cite them where they settle a cell". These three reach the draft's cells:
- SG.11, SG.12 and the root half of Q-SG-16: DEC-0068 a, "root must equal the realpath of `git rev-parse --show-toplevel`, else FAIL `<gate>.input`: one lib check". FH.30-.34 cite it.
- SG.13, .14, .16 and Q-SG-13/14: DEC-0067 a, "UTF-8 without BOM, LF only, non-empty, else FAIL naming the file (new `<pfx>.encoding` ...)". FH.22-.26 cite it.
- SG.15: DEC-0085 a, "FAIL `<gate>.input` naming the file, as today, and state it in the spec's common contract". FH.19 and FH.24 cite it.

Whether they reach the generator is a scope ruling, so it goes to the owner as Q-SG-17. The cells should cite these DECs and read OPEN Q-SG-17, not stand as bare questions.

**R-SG-5 MEDIUM, SG.52 (checks 3, 7).**
- Draft: "missing parent ... unwritable ... input check FAILs | D32".
- D32 :46 says only "unreadable, of the wrong kind, a symlink at any path component below the repository root, or a path argument that is itself a symlink".
- FH: Q-FH-10 "E3 parent missing? a create (makedirs, as today) / b FAIL"; Q-FH-11 "Unwritable target: which id? ... (D32 names 'unreadable')".
- Replacement: directory, FIFO and symlink -> D32; missing parent -> OPEN Q-FH-10; unwritable -> OPEN Q-FH-11.

**R-SG-6 MEDIUM, SG.59 contradicts SG.13.**
- SG.59: "spec missing, a symlink, non-UTF-8, D24/D31 row absent | input check FAILs | D32 (file kinds)".
- SG.13: "OPEN Q-SG-13 (D32 does not say non-UTF-8 is 'unreadable')".
- Replacement: SG.59 non-UTF-8 -> OPEN Q-SG-13 / Q-SG-17.

**R-SG-7 MEDIUM, calls made through the helper (check 7).**
- FH surface: "Every gate, `make_review_package` and the hooks call it instead of `open`/`os`/`subprocess`/`re`".
- FH.70: "gates call the helper, not `open`/`os` | the generator lists helper calls as filesystem calls | OPEN Q-FH-19".
- SG has no row or question for this. Its dependency line reads only "FH (helper verdict form for SG.1-.6, .52, .59)".
- Also, FH's caller list ("the six gates ..., `make_review_package.py`, `hooks/pre_tool_use.py`, `hooks/session_start.py`, and the D34 pre-commit gate") leaves out the generator, which SG relies on for its input verdicts.
- Replacement: a T row "a call through a D32 helper entry point (E0-E5)", OPEN Q-SG-18 = Q-FH-19, and the generator added to FH's callers.

**R-SG-8 MEDIUM, options without a cost (check 6).**
- Q-SG-3 a, b, c (c states a benefit)
- Q-SG-4 b
- Q-SG-5 c
- Q-SG-7 c
- Q-SG-8 c
- Q-SG-9 c
- Q-SG-10 a, c
- Q-SG-11 c
- Q-SG-13 a, b
- Q-SG-14 a, b
- Q-SG-15 a, b, c
- Q-SG-16 a

Replacement: one cost per option (code lines or tests, what is lost).

**R-SG-9 LOW, shell skeleton (check 1).** The draft misses these in git-pre-commit.sh:
- `unset CLAUDE_PROJECT_DIR` :28 (env)
- `trap 'rm -rf "$tmp"' EXIT` :53 (runs `rm -rf`, deletes a write target)
- the index-file variable export :54-55 (it changes every later git run)
- `exec` :60

`git-pre-push.sh` and `install-hooks.sh` (the B5-PROC-2 sibling) were not grepped, though the whitelist counts them.

**R-SG-10 LOW, counts (check 1).**
- "import aliases 0" does not hold: `from tools.gates.lib import config as gconfig` at gate_dispatch.py:21 and gate_plan.py:35. The alias hides the lib I/O of SG.41.
- SG.27's value list leaves out the `os` write and delete calls seen today: `os.fdopen` pre_tool_use.py:280 and `os.unlink` :291.
- SG.29 has 6 false `glob` hits over the 16 files (gate_plan.py:45; pre_tool_use.py:17 :31 :51 :85 :215), not 1.

**R-SG-11 LOW, today cells (check 4).**
- SG.18 as written gives `171:`, not `172:`. The drafter ran `printf '\nopen("WT_ONLY")\n'` (`today2.sh:9`).
- "`_modes_agree` :631" is :632 (def :586).
- SG.36, .37 and .46 (AST counts) carry no command. `<R>/ast_count.py` reproduces them.
- SG.14 BOM and SG.19 mode/type read OPEN "not probed". The drafter rules allow OPEN only when there is nothing to observe or the action is forbidden. Observed: BOM `1:﻿open("a")` rc 0; +x -> 6 lines; symlink -> 6 lines.
- SG.3 socket: observable through a relative bind, giving `Operation not supported on socket` rc 2.

**R-SG-12 LOW, Q-SG-15 (b) cite.** It quotes "D35 'reviewers check coverage'". D35 :49 has no such words; D24 :38 does ("reviewers check coverage against it").

## new questions (verbatim)
| id | question | options (cost) | rows |
|---|---|---|---|
| Q-SG-17 | Do DEC-0067 a, DEC-0068 a and DEC-0085 a reach the generator's source reads and root (one answer with Q-FH-8)? | (a) yes: SG.11-.16 and .59 become settled and Q-SG-13/14 and the root half of Q-SG-16 retire; about 3 tests per DEC, and a subdirectory root refuses (b) only their named rows: Q-SG-13/14/16 stay open, and FH.22-.26 narrow too (Q-FH-8 b) | SG.11-.16, .59 |
| Q-SG-18 | Once gates call the D32 helper, how does the generator find their reads, writes and git runs (= Q-FH-19)? | (a) it recognises the helper's E0-E5 entry points as filesystem and subprocess calls: a list kept beside the helper, pinned by a test (b) D31's families only: gates then show about 0 direct calls and the skeleton is empty for them (c) follow the calls into lib (Q-SG-4 a/b): data flow per call site, more code | T, SG.39-.41 |
| Q-SG-19 | Under `--all` (Q-SG-3 c), is the source set taken from the filesystem or from git? | (a) `git ls-files` (the index): ignore files are moot, an untracked scratch .py is not scanned, git rows apply (b) a filesystem glob: an excluded file is scanned (17 vs 16 above) (c) refuse while any untracked or ignored .py lies under tools/gates, as make_review_package `_ignored` does: one git call, about 2 tests | A1, R-SG-3 |
| Q-SG-20 | Does the generator's own run carry the P-1 row (its imports of tools/gates/lib)? | (a) yes: run only under D34-style commands with a fresh pycache prefix (a test plus a README line) (b) N/A: its output is a draft that a reviewer re-checks (D35), but a stale lib .pyc can drop rows silently | R |

## unrelated findings
- The `-I` flag defeats the probe rule "PYTHONDONTWRITEBYTECODE=1 on every probe". `PYTHONPYCACHEPREFIX=<R>/pfx PYTHONDONTWRITEBYTECODE=1 <py> -I -c 'import sys;print(sys.flags.dont_write_bytecode, sys.pycache_prefix)'` gives `1 None` on /usr/bin/python3 3.9.6 and `0 None` on the venv 3.12.14.
  - The drafter's clone holds `tools/gates/__pycache__/__init__.cpython-312.pyc` and `lib/__pycache__/__init__.cpython-312.pyc` (17:47), which the git-ignored `__pycache__/` hides from `git status`.
  - Class: `-I` drops PYTHONPYCACHEPREFIX on both interpreters, and drops PYTHONDONTWRITEBYTECODE on 3.12.
  - This reaches PC Q-PC-15 (b) "run with `-I`", whose cost leaves out that `-I` voids D34's fresh-prefix guarantee (P-1).
  - D33 grep (`grep -rnE 'python3?[^ ]* -I\b|"-I"' tools/gates .github`) -> 0 hits.
- The session shell's `grep` is a function that prints nothing for a file with one 0xff byte and varies multi-file output order. `/usr/bin/grep` does neither. Class: the by-hand D31 skeleton does not pin its tool.
  - D33 grep (`/usr/bin/grep -nw grep tools/gates/*.sh tools/gates/hooks/*.sh`) -> 1 hit, install-hooks.sh:20. It runs under /bin/sh, which does not have the session function, so it is not affected.

## divergences (both quotes)
- tablerev-common check 3 says "as D32 words it (spec :47)", and the FH draft says "spec D32 :47". At f1057e5 D32 is :46 and :47 is D33.
- The controller brief :24 says "One opus drafter ... per artifact, serial". The drafter Task says "at the same time".

## denials
none. Disclosure: I read `~/.claude/shell-snapshots/snapshot-zsh-*.sh` once with sed to identify the grep function (it printed nothing). That was a read; I wrote nothing there.

## background jobs
none.

## budget used
About 110k of 250k tokens (my estimate; the harness count is authoritative); under 60 minutes.
