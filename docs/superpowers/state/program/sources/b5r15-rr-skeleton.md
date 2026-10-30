status: DONE_WITH_FINDINGS

Task reviewer SG, b5 round 15 item 4. This is the re-review of table SG (B5-PROC-2, the D31 skeleton generator) after text fix round 1.
- Draft: `b5r14-draft-skeleton.md`, 146 lines, 81 SG rows, 21 Q rows. The column-count check over every row is clean.
- Base: 6a7b2f9. The checkout HEAD is e17dc90; I reviewed against the base the Task names.
- Clone: guarded clone `<R>/repo`, where `<R>` = scratchpad/r15-rr-skeleton (DEC-0032 guard in every write script). At the end `git status --porcelain --untracked-files=all --ignored | wc -l` -> 0.

Verdict: spec: ❌ quality: ❌ claims-executed: ❌
- spec ❌: some (item, dimension) pairs still have no row and no N/A, and the plan :7185 names `core.attributesFile` and `filter.*`, which the draft never mentions (R2-SG-2). SG.52b quotes DEC-0093 text from an option the owner rejected (R2-SG-1).
- quality ❌: Q-SG-4, Q-SG-18 and Q-FH-19 are declared equal although their options differ (R2-SG-11). SG.59 points to the wrong row (R2-SG-3). Two options have no cost (R2-SG-9). SG.52 has gaps (R2-SG-6).
- claims-executed ❌: I re-ran all 39 changed and added rows. SG.63, .64, .66, .75 and .77 read OPEN or carry no command, yet I could observe all five (R2-SG-5). The install-hooks skeleton is incomplete and wrong in places (R2-SG-7). SG.27 is short (R2-SG-8) and SG.39 cites the wrong line (R2-SG-10). Every other re-run cell held.

## commits
none (review only; I edited nothing but this report).

## gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<R>/pfx2 /usr/bin/python3 tools/gates/run_all.py` in the clone at 6a7b2f9 -> rc=0, 100 PASS, 0 FAIL.
  - The clone has no git-ignored review packages; the brief's 208 counts the checkout's packages.
- The Task gives no gate list. The draft is plan text with no code, so I did not run the unittest suite.

## spec lines (base 6a7b2f9)
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| D31 :45, D32 :46, D34 :48, D35 :49 | `sed -n {45,46,48,49}p <spec>` | `sed -n 47p` (must be D33), `sed -n 38p` (must be D24) | `\| D31 \|`, `\| D32 \|`, `\| D34 \|`, `\| D35 \|`; :47 `\| D33 \|`; :38 `\| D24 \|` | PASS |
| plan :7185 (subsection :7183) | `grep -n 'Owner task S2-b5-process-code' <plan>` -> 7185; `sed -n 7183p` -> `#### S2-b5-process, S2-b5-process-code and S2-b5-sweep (DEC-0088)` | `sed -n 7221p` -> `  - *structure*: ...` (no longer this text) | as stated | PASS |
| findings.yml :1739 is the B5-PROC-2 class | `sed -n 1739p` -> `class: 'process code for DEC-0088: the D31 skeleton generator ...` | `grep -n 'id: B5-PROC-2'` -> 1736 | as stated | PASS |
| gate code unchanged f1057e5..6a7b2f9 | `git diff --stat f1057e5 6a7b2f9 -- tools/gates/*.py tools/gates/lib tools/gates/hooks` (shell-expanded) -> empty | `git diff --stat ... -- tools/gates ':!tools/gates/tests'` -> empty; quoted `'tools/gates/*.py'` -> 2 test files only (test_gate_dispatch.py, test_hooks.py) | as stated, and the fixer's quoted-pathspec divergence holds | PASS |

## Check (1): each R-SG-* finding
| finding | state | evidence |
|---|---|---|
| R-SG-1 | fixed | `/usr/bin/grep -nE "$G" latin2.py` -> `1:open("a")\|3:os.listdir("b")` rc 0; same under LC_ALL=C. Contradiction: had the cell still been wrong, rc 1 would be observed; it is not. |
| R-SG-2 | fixed for the pairs the review listed (SG.60-.77 plus the N/A line) | Pairs the review did not list remain (R2-SG-2). |
| R-SG-3 | partly fixed | SG.78 reproduces (`ls`=17, `ls-files`=16, `status`=0), SG.79 reproduces (`0 None` on 3.12.14, `1 None` on 3.9.6 with `-I`), and the review-packages N/A is present. The review's replacement "rows A1/I1 x ignore files" got an A1 row only: I1 has neither row nor N/A (R2-SG-2). |
| R-SG-4 | fixed | The option-a texts of DEC-0067, DEC-0068 and DEC-0085 read from decisions.yml match the cells verbatim; each is answered "a". DEC-0067 is amendment Q2, not deferred Q22-Q45 (DEC-0092). |
| R-SG-5 | fixed per R-r15-1 | SG.52b's DEC-0093 quote comes from the rejected option (R2-SG-1). SG.52 leaves out socket (R2-SG-6). |
| R-SG-6 | fixed | The new SG.59 cell refers to "SG.67" for non-UTF-8. SG.67 is the regex-anchoring row; the encoding row is SG.68 (R2-SG-3). |
| R-SG-7 | fixed (SG.80, surface line, coverage) | The 3-way equality is not supported (R2-SG-11). |
| R-SG-8 | fixed for the 13 listed options | Q-SG-12 (b) and Q-SG-1 (c) still have no cost (R2-SG-9). |
| R-SG-9 | partly fixed | git-pre-commit.sh: `unset` :28, `trap` :53, export :55, `exec` :60 all reproduced. install-hooks.sh is incomplete and misattributed (R2-SG-7). |
| R-SG-10 | fixed | aliases 3 (gate_dispatch.py:21 :23, gate_plan.py:35); 6 false glob hits (gate_plan.py:45; pre_tool_use.py:17 :31 :51 :85 :215); os writes make_review_package.py:160, pre_tool_use.py:280 :291. `os.path.getsize` is missing from the list (R2-SG-8). |
| R-SG-11 | fixed | SG.18: `wc -l` 170; `printf '\nopen("WT_ONLY")\n' >>` -> `172:`. Contradiction: without the leading `\n` -> `171:`, so the fixer's divergence holds. `_modes_agree` def :586, call :632. Socket -> `grep: s.py: Operation not supported on socket` rc 2. BOM -> bytes `357 273 277 o p e n`, rc 0, and `ast` accepts it. +x -> 6 (status ` M`); symlink -> 6 (status ` T`); deleted -> rc 2. |
| R-SG-12 | fixed | `sed -n 38p \| grep -c 'reviewers check coverage against it'` -> 1; `sed -n 49p \| grep -c 'reviewers check coverage'` -> 0. |

## Check (2): changed and added rows re-run (direct + contradiction)
- Changed rows (diff of row id to row text against the fixer's r14 backup): SG.3, .11-.16, .18, .19, .27, .29, .36, .37, .46, .48, .52, .59.
- Added rows: SG.52b, .60-.80.
- Total: 39 rows, all re-run by me. Scripts: `<R>/bin/rr1.sh`, `rr2.sh`, and the inline probe in the transcript.

| row | direct | contradiction | output | result |
|---|---|---|---|---|
| SG.3 FIFO/socket | `perl -e 'alarm 3; exec @ARGV' /usr/bin/grep ... fifo.py`; socket bound relative after `cd probe` | if grep did not block, rc 0 or 2 | rc 142; socket rc 2 `Operation not supported on socket` | PASS |
| SG.11 | the DEC-0068 a text | the cite's quote is not in decisions.yml | quote verbatim; OPEN (no worktree) is justified by the guard | PASS |
| SG.12 | `CLAUDE_PROJECT_DIR=/nonexistent .venv python3 -I -c '...repo_root()'`; from `tools/` with `env -u` | expect a different root | `/nonexistent`; `<R>/repo` | PASS |
| SG.13/.14/.15/.16 | probes above | rc 1 / no hit | `1:`/`3:` rc 0; CRLF `2:open("a")`; BOM rc 0, ast ok; syntax `2:  open("x")` and `SyntaxError invalid syntax`; empty rc 1 | PASS |
| SG.18/.19 | as R-SG-11 | as R-SG-11 | 172; 6/6/rc 2 | PASS |
| SG.27 | `grep -ohE 'os\.[a-z_]+(\.[a-z_]+)?\(' <16 files> \| sort \| uniq -c` | a fs call outside the list | listdir 7, stat 2 + lstat 1, makedirs 1, fdopen 1, unlink 1, **getsize 1** | FAIL (R2-SG-8) |
| SG.29 | glob grep | another false hit | 6 | PASS |
| SG.36/.37/.46 | `/usr/bin/python3 -B <fixer>/ast_count.py` | other counts | `{'list': 12, 'str': 0, 'var': 3}`; 12 and 14 fs calls, 0 literal | PASS |
| SG.48 + shell bullet | `/usr/bin/grep -nE "$G"` on the 3 shell files; `cat -n install-hooks.sh` | a command missing from the skeleton | pre-commit :37 :38; pre-push 0; install-hooks 0. `mkdir -p` :29 and `mv` :33 are not listed; :41 and :42 sit inside the SHIM heredoc (:36-:47) | FAIL (R2-SG-7) |
| SG.52 / .52b | D32 :46 vs the values | R-r15-1 list | socket missing; "a symlink at the target" is unqualified | FAIL (R2-SG-6); cite FAIL (R2-SG-1) |
| SG.59 | `grep -n 'SG.67' <draft>` | SG.67 is the encoding row | SG.67 = "I1 \| regex anchoring"; SG.68 = I2 encoding | FAIL (R2-SG-3) |
| SG.60-.62, .67-.74, .80 | the cites against D32 :46 and DEC-0067 a | an OPEN cell where code exists to observe | no such argument, writer, parser or helper exists; OPEN is justified; the D32 fullmatch and isolation quotes are verbatim | PASS |
| SG.63 | lib module symlinked -> `grep -cE` 2 rc 0; replaced by a directory -> `Is a directory` rc 2; mode 000 -> `Permission denied` rc 2 | `as SG.1-.6` asserts this with no command | holds, but the cell carries no command | FAIL form (R2-SG-5) |
| SG.64 | `printf '# \xff\n' >> lib/config.py; /usr/bin/grep -nE "$G"` | OPEN implies nothing to observe | rc 0, `14:` and `15:` | FAIL (R2-SG-5) |
| SG.65 | aliases grep | — | 2 module aliases | PASS |
| SG.66 | `as SG.18-.19` | no command | holds by the SG.18/.19 runs; no command | FAIL form (R2-SG-5) |
| SG.75 | `cd /tmp/...r15-rr-skeleton/repo` (logical path through the `/tmp` -> `private/tmp` link), `repo_root()` | OPEN implies nothing to observe | without the env: `/private/tmp/...` (realpath-equal); with `CLAUDE_PROJECT_DIR=/tmp/...`: `/tmp/...`, which differs from the realpath | FAIL (R2-SG-5) |
| SG.76 | `cd <R>/norepo; .venv python3 -B ... repo_root()` | rc 0 | `GateError: git rev-parse --show-toplevel: fatal: not a git repository` | PASS |
| SG.77 | clone-local `git config core.worktree <R>/probe; git rev-parse --show-toplevel` | OPEN implies nothing to observe | `<R>/probe` (`git -c core.worktree=...` left it at `<R>/repo`); unset -> `<R>/repo` | FAIL (R2-SG-5) |
| SG.78 | exclude repro | — | 17 / 16 / 0 | PASS |
| SG.79 | `PYTHONPYCACHEPREFIX=<R>/pfx <py> -I -c ...` | without `-I` | venv `0 None`; system `1 None`; venv without `-I` `1 <R>/pfx` | PASS |

Unchanged rows I also re-ran: SG.1, .2, .4-.10, .17, .23, .25, .26, .28, .39, .41, .42, .44, and the D28 whitelist (gates 6, py 16, sh 3, links 0, `run_all.GATES` 6 names). All hold except SG.39 (R2-SG-10).

Not re-run (with reason): SG.20-.22, .24, .30-.35, .38, .40, .43, .45, .47, .49-.51, .53-.56, .58.
- Most read OPEN with no code to observe.
- The rest are unchanged rows the round-14 review re-ran.
- The gate code did not change between f1057e5 and 6a7b2f9 (above).

Rows run: 61 of 81.

## Check (3): no Q-SG-* answered, narrowed or dropped
- Method: `diff <(grep '^| Q-' r14-backup) <(grep '^| Q-' draft)`.
  - Q-SG-1, -2, -6 and -12 are unchanged.
  - The others only gained costs, or the conditional note "(DEC-... settles it if it reaches the generator: Q-SG-17)".
  - Q-SG-13 (c) was reworded as R-SG-1 prescribed.
  - Q-SG-17..20 match the round-14 review's rows verbatim.
  - Nothing was dropped (16 -> 21 rows).
- Exception: the equality written into Q-SG-4 (R2-SG-11) merges two distinct questions, which is a narrowing risk.
- Q-D32-W: the question and options are identical to `b5r15-fix-common.md`.
  - md5 of question + options: SG = FH.
  - PC differs only in the option separator `/`.

## Check (4): R-r15-1 against spec :46 and DEC-0093 (divergence, both quotes; I do not pick a side)
- R-r15-1 (b5r15-fix-common.md): "DEC-0093 chose below-root-plus-args, \"no change for /tmp paths\"".
- DEC-0093 at decisions.yml:865 is the **below-root** option: `{label: below-root, cost: "below the repository root only, as DEC-0066: no change for /tmp paths; ..."}`.
  - The chosen option reads: `{label: below-root-plus-args, cost: "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root"}`.
  - Contradiction: `grep -rn 'no change for /tmp' decisions.yml` -> 1 hit, :865, the below-root line.
- The settled half of R-r15-1 matches D32 :46: wrong kind; a symlink component below the root; a path argument that is itself a symlink; multi-item isolation.
- The unsettled half ("unwritable", missing parent, partial write, pre-existing outside the root) is consistent with D32's words "unreadable, of the wrong kind".
- Open point: does DEC-0093's choice (the owner rejected any-component, "runs through /tmp or /var on macOS fail ...") settle "a symlink component outside the root that is not the path argument" as accepted? If it does, that part of Q-D32-W is settled. That is an owner reading, raised as Q-SG-21.

## Check (5): the fixer's stated divergences
| divergence | check | result |
|---|---|---|
| the round-14 unrelated finding is withdrawn for `/usr/bin/grep` | SG.13 rc 0 | holds |
| the review's 171 came from the shortened command | 171 vs 172 reproduced | holds |
| a quoted pathspec matches the tests | 2 test files | holds |
| FH's caller list lacks the generator | `grep -n -i generator b5r14-draft-helper.md` -> only FH.70 and Q-FH-19 | holds |
| Q-FH-10/-11 are marked "= Q-D32-W" in FH | FH:172 :173 | holds |

## findings
**R2-SG-1 MEDIUM, SG.52b settled-by (also R-r15-1).**
- Draft: `OPEN Q-D32-W (D32 words "unreadable" and "of the wrong kind"; DEC-0093 "no change for /tmp paths")`.
- DEC-0093's chosen option text: "below the root, plus a path argument that is itself a symlink: covers --plan outside the root; one realpath of the root". The quoted phrase belongs to the rejected below-root option (decisions.yml:865).
- tablerev-common check 3: "a DEC cite must state the chosen option's text".
- Replacement (hypothesis): `... ; DEC-0093 below-root-plus-args "below the root, plus a path argument that is itself a symlink"`. The same misquote in b5r15-fix-common.md needs a controller correction.

**R2-SG-2 MEDIUM, coverage gaps (D24 + D31, check 2).**
- Source: `/usr/bin/python3 -B <R>/bin/matrix.py <draft>` (item x dimension matrix) checked against the N/A paragraph.
- Pairs with no row and no N/A:
  - I1 x git config keys, repository state (ignore files). R-SG-3's replacement "rows A1/I1 x ignore files" got A1 only (SG.78).
  - I2 x git config keys, repository state.
  - T x git object state, index/working tree, repository state, file type/permissions/symlinks, non-regular, encoding, path form, isolation. These may be argued as "A1's rows", but that argument is not written.
  - O1 x regex anchoring.
  - R x index/working tree, repository state, text grammar, write targets, isolation.
- Plan :7185 names "attribute and filter files (`.git/info/attributes`, `filter.*`, `core.attributesFile`, the global `~/.config/git/attributes`)". `grep -n 'core.attributesFile\|filter\.' <draft>` -> 0 hits. Only SG.20 (`.gitattributes`/`.git/info/attributes`) and the global-file N/A exist.
- Replacement: one row or one argued N/A line per pair, and `core.attributesFile`/`filter.*` named in SG.20 or the N/A list.

**R2-SG-3 LOW, SG.59 cross-reference (introduced by the fix round).**
- Draft: "non-UTF-8 is SG.67".
- `grep -n 'SG.67' <draft>`: SG.67 is "I1 | regex anchoring"; the I2 encoding row is SG.68.
- Replacement: "non-UTF-8 is SG.68".

**R2-SG-4 LOW, Q-SG-17 rows column.**
- The column reads "SG.11-.16, .59". SG.59 no longer cites Q-SG-17, while SG.64 and SG.68 cite it and are not listed (`grep -n 'Q-SG-17' <draft>`).
- This is a divergence between fix-common rule 2 ("add the reviewer's Q rows ... verbatim") and the table's consistency.
- Replacement (hypothesis): keep the question verbatim and correct only the rows column to "SG.11-.16, .64, .68".

**R2-SG-5 LOW, today cells that read OPEN or carry no command, though I could observe them (D35; the R-SG-11 class re-run over the added rows).**
- SG.64: OPEN. Observed: rc 0, `14:`/`15:` hits.
- SG.75: OPEN "not probed". Observed through the `/tmp` link: the realpath without the env; the unresolved `/tmp/...` with `CLAUDE_PROJECT_DIR` set.
- SG.77: OPEN "not probed". Observed: clone-local `core.worktree` makes `--show-toplevel` print the worktree dir; the `-c` form does not.
- SG.63 "as SG.1-.6" and SG.66 "as SG.18-.19": no command.
- Replacement: those commands and outputs.

**R2-SG-6 LOW, SG.52 values.**
- D31 lists the non-regular kinds "(directory, FIFO, socket)", and R-r15-1 lists "socket or other non-regular file". SG.52 has "an existing directory or FIFO" only.
- "a symlink at the target" is not limited to below the root, and D32 settles only "a symlink at any path component below the repository root" or the path argument.
- Replacement: "an existing directory, FIFO, socket or other non-regular file at the target; a symlink at any component below the root (the target included); the target path argument itself a symlink".

**R2-SG-7 LOW, shell skeleton for install-hooks.sh (R-SG-9's dimension re-run).**
- Missing: `mkdir -p "$hooks"` :29 (a write) and `mv "$dst" "$dst.pre-strict-delivery"` :33 (a write/rename), plus `>&2` :18 :25.
- Misattributed: "runs `git rev-parse` ... :41" and "`-f` :42" are text inside the `<<'SHIM'` heredoc (:36-:47), run by the installed shim, not by install-hooks.sh.
- Command: `cat -n tools/gates/install-hooks.sh | sed -n 9,52p`.

**R2-SG-8 LOW, SG.27 value list.**
- `os.path.getsize` (gate_review_package.py:342), a stat call, is absent from the values and the today counts.
- Command: `grep -ohE 'os\.[a-z_]+(\.[a-z_]+)?\(' ... | sort | uniq -c`.

**R2-SG-9 LOW, options without a cost (R-SG-8's dimension re-run over all questions).**
- Q-SG-12 (b) "accept (fixtures under /tmp work without a repo)" states a benefit only.
- Q-SG-1 (c) "ties to Q-SG-15" states no cost.

**R2-SG-10 LOW, SG.39 today (unchanged row).**
- The cell says "grep hits only the def :53", but `sed -n 52,53p make_review_package.py` gives the def at :52; :53 is the `subprocess.run` line inside it.
- Replacement: "grep hits only the `subprocess.run` line :53 in the def :52".

**R2-SG-11 MEDIUM, the cross-table equality Q-SG-4 = Q-SG-18 = Q-FH-19 (checks 3 and 7).**
- Round-14 review: "a T row ..., OPEN Q-SG-18 = Q-FH-19".
- fix-common item 4: "(e.g. Q-FH-19 = Q-SG-4/Q-SG-18)".
- The draft writes Q-SG-4 "(= Q-FH-19 = Q-SG-18)", and FH.70 and Q-FH-19 say "= Q-SG-4, Q-SG-18". Yet the questions differ:
  - Q-SG-4 asks whether the generator follows imports into lib (lib reads that exist today, SG.41), with options a/b/c.
  - Q-SG-18 asks how helper calls are found, with options a/b/c, and its (c) refers to Q-SG-4.
  - Q-FH-19 has options a/b only.
- One owner answer cannot answer three different option sets, so the equality narrows or merges open questions.
- Replacement (hypothesis): drop "= Q-SG-4" from the equality (Q-SG-18 = Q-FH-19 only; Q-SG-4 "related: Q-SG-18 c"), and align Q-FH-19's options with Q-SG-18's. This is an FH edit, for the FH fixer.

## new questions (verbatim)
| id | question | options (cost) | rows |
|---|---|---|---|
| Q-SG-21 | Does DEC-0093's answer (below-root-plus-args; the rejected any-component option was "runs through /tmp or /var on macOS fail unless gates and tests realpath their roots first") settle that a write target with a symlink component outside the root, not itself the path argument, is accepted, so that clause leaves Q-D32-W? | (a) yes: Q-D32-W loses that clause in all three tables and the cells cite DEC-0093; 0 code, but a /tmp-through-symlink target is written silently (b) no: it stays in Q-D32-W, and the owner's answer to Q-D32-W decides it; one more clause for the owner to rule on | SG.52b (and PC.70, the FH E3 rows) |

## unrelated findings
- tools/gates/lib/__init__.py:61-63: `repo_root()` returns `CLAUDE_PROJECT_DIR` unchecked and unresolved, against DEC-0068 a "root must equal the realpath of `git rev-parse --show-toplevel`, else FAIL `<gate>.input`".
  - Reproducer: `cd /tmp/<scratch>/repo; CLAUDE_PROJECT_DIR=/tmp/<scratch>/repo .venv python3 -B -c '...repo_root()'` -> `/tmp/...`, not the realpath `/private/tmp/...`. `CLAUDE_PROJECT_DIR=/nonexistent` -> `/nonexistent`.
  - Class: an env-given root accepted with no realpath or toplevel check.
  - D33 grep (`/usr/bin/grep -rn CLAUDE_PROJECT_DIR tools/gates`, tests excluded): lib/__init__.py:60 :61, git-pre-commit.sh:28 (`unset`). Register grep (`show-toplevel\|CLAUDE_PROJECT_DIR` in findings.yml): 0 hits.
  - It may be planned DEC-0068 work. The controller should place it.
- Reproduced, not new: the round-14 `-I` finding. My SG.12 probe (`.venv python3 -I` with PYTHONDONTWRITEBYTECODE=1) wrote `tools/gates/__pycache__/__init__.cpython-312.pyc` and `lib/__pycache__/__init__.cpython-312.pyc` in the clone; both were removed.

## denials
none.

## disclosure
Two scratch writes outside my scratch dir, both removed at once:
- `/tmp/x` was written by a stray `cp` and removed in the same command; `ls` confirms it is absent.
- `/tmp/claude-501/x1` (the parent of the scratchpad) was written by a stray redirect and removed; `ls` confirms it is absent.

No tracked file was touched. The checkout is untouched apart from this report.

## background jobs
none.

## budget used
About 125k of 250k tokens (my estimate; the harness count is authoritative), about 40 of 100 minutes.
