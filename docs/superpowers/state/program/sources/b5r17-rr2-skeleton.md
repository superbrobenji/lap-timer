status: DONE_WITH_FINDINGS

Table SG (B5-PROC-2, D31 skeleton generator), re-review after text fix round 2 (b5 round 17 item 4). Draft b5r14-draft-skeleton.md (203 lines, 83 SG rows, every row 7 cells) diffed against r17-pre/b5r14-draft-skeleton.md (81 rows): rows SG.81 and SG.82 added, none removed, 72 rows changed, 17 today cells changed (SG.14 .20 .24 .26 .27 .36 .39 .51 .55 .57 .58 .63 .64 .65 .66 .75 .77). Base: checkout HEAD 2e43f13 with a clean tree (`git status --porcelain | wc -l` -> 0). Guarded clone `<R>` = scratchpad/r17-rev/b5r17-rr2-skeleton/repo at 2e43f13 (set -eu; `pwd -P` and `--show-toplevel` asserted under `<R>`'s parent; neither is the checkout).

Verdict:
- spec ❌: two coverage gaps (I1 x text grammar, I1 x empty file; R3-SG-3). SG.7's verdict leaves out its Q-SG-3 dependency (R3-SG-4).
- quality ❌: rows columns drift (R3-SG-2). The D30-4 and D30-2 dependencies are missing (R3-SG-5). Q-SG-24 quotes one side only (R3-SG-6). Q-SG-3's status is a divergence, and two open question cells are no longer verbatim (R3-SG-7). A raw U+FEFF sits in SG.14 (R3-SG-8). The interpreter path is not reproducible (R3-SG-9).
- claims-executed ✅ for every changed today cell I re-ran: all 17 changed cells plus SG.81, .82, .12, .76, .78, .79 and the shell and R N/A probes hold at 2e43f13. Two OPEN cells could have been observed (R3-SG-1).

## commits
none (review only; no tracked file touched; checkout `git status --porcelain | wc -l` -> 0 after the run).

## gates run
- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<fresh> /usr/bin/python3 tools/gates/run_all.py` in the checkout -> rc 0, 208 PASS, 0 FAIL. The fixer's 208 reproduces.
- The same in the clone -> rc 0, 100 PASS, 0 FAIL. The clone lacks the git-ignored workspace that run_all reads.
- Not run: the unittest suite on either interpreter.
  - This item changed no tracked file and made no commit; D34 binds a commit.
  - The draft is git-ignored workspace text.
  - Both interpreters here differ from the D34 pair: /opt/homebrew python3.12 has no yaml; tools/.venv is 3.12.14 and /usr/bin/python3 is 3.9.6 with user-site yaml.

## R2-SG fixes (each checked)
| R2 | holds? | direct | contradiction |
|---|---|---|---|
| R2-SG-1 | yes | `grep -n 'no change for /tmp' <draft>` -> :167 only (the fix-round history row). DEC-0093 at decisions.yml :865-869: below-root carries "no change for /tmp paths"; the answer is below-root-plus-args. SG.52b now quotes "below the root, plus a path argument that is itself a symlink" | if the misquote remained, SG.52b would hold the phrase: 0 hits in SG rows |
| R2-SG-2 | partly | SG.20 names core.attributesFile, filter.* and the global attributes file. Filter probe in `<R>`: count 4 before and after. SG.81 and SG.82 present; the N/A paragraph carries T, O1 and R lines | under the same filter `git diff --stat` -> "40 files changed", so an index read would differ while the working-tree read does not: the N/A argument holds. New gaps for I1 (R3-SG-3) |
| R2-SG-3 | yes | SG.59 reads "non-UTF-8 is SG.68"; SG.68 = I2 encoding | - |
| R2-SG-4 | no (recurred) | the rows column lists SG.68; SG.68 now cites only Q-SG-9 (a) (R3-SG-2) | rows citing Q-SG-17: SG.11-.16, .64, .75, .76 (script below) |
| R2-SG-5 | yes for the five | SG.63, .64, .66, .75 and .77 re-run (claims table) | same class in SG.11 and SG.24 (R3-SG-1) |
| R2-SG-6 | yes | SG.52 values include socket, a component below the root, and an unreadable target; `sed -n 45p <spec> \| grep -o 'directory, FIFO, socket'` -> hit | - |
| R2-SG-7 | yes | `cat -n install-hooks.sh \| sed -n 9,52p`: :18 :25 `>&2`, :29 `mkdir -p`, :33 `mv`, heredoc :36-:47 containing :41 :42 | - |
| R2-SG-8 | yes | `uniq -c` -> `1 os.path.getsize(`, at gate_review_package.py:342 | - |
| R2-SG-9 | yes | costs added and marked "not before the owner"; sheet N9 confirms that neither cost was in front of the owner | (the controller should know Q-SG-12 (b) was answered without its cost) |
| R2-SG-10 | yes | `sed -n 52,53p make_review_package.py` -> def :52, `subprocess.run` :53 | - |
| R2-SG-11 | yes | the surface reads "Q-SG-18 = Q-FH-19; Q-SG-4 is related, not equal". Sheet N2 agrees | old 3-way text only in history rows :153 :161 |

## sheet answers (DEC-0129: decisions.yml answer "approve-except-flagged"; sheet sha256 8bef9f62...839b equals the question text)
Each applied row was checked against the sheet's recommendation and option text, sections C :164-:186 and D :243-:262.

These are faithful (option quoted, the verdict it gives, no extra rule):
- Q-SG-1 (a), Q-SG-2 (a), Q-SG-4 (a), Q-SG-5 (a), Q-SG-6 (a), Q-SG-7 (c), Q-SG-9 (a), Q-SG-10 (a)
- Q-SG-11 (a), Q-SG-13 (a), Q-SG-14 (a) (for empty), Q-SG-15 (b), Q-SG-16 (a), Q-SG-17 (a), Q-SG-18 (a), Q-SG-19 (c), Q-SG-20 (a), Q-SG-21 (a)
- Q-D32-W (c): the clause text matches sheet :165 verbatim
- SC-1 (SETTLED, spec :46)
- Q-FH-24 (a)

Exceptions:
- Q-SG-12 (b) at SG.7: the verdict leaves out Q-SG-3 (R3-SG-4).
- Q-SG-14 (a) reach: R3-SG-6.

The answered table's question and option text is verbatim from round 15, apart from the marked R2-SG-9 cost additions and Q-D32-W, which is now the sheet's restatement (labelled as such). Script: a cell-wise diff of the Q rows, old vs new -> only Q-SG-1, Q-SG-12, Q-D32-W, Q-SG-3 and Q-SG-8 differ.

Flagged rows A1-1b, Q-FH-10, Q-FH-11 and S-D-1 appear in no SG cell as answers.

## new and still-open questions (are they real?)
| Q | real? | evidence |
|---|---|---|
| Q-SG-22 | yes | Q-SG-10 (a) (sheet :252) states stdout, markdown, sort, UTF-8 and `\|`; columns, ids, newline, dedup and partial rows are stated nowhere |
| Q-SG-23 | yes | Q-SG-16 (a)'s own cost: "an untracked file has no tree spelling" |
| Q-SG-24 | divergence | R3-SG-6 |
| Q-SG-25 | yes | plan :7185 "the D32 shared fail-closed helper in `tools/gates/lib`" places the helper only; D31 :45 "A script generates it" |
| Q-SG-26 | yes, incomplete | R3-SG-5 |
| Q-SG-27 | yes | Q-SG-4 (a) names no mapping. Option (b)'s cost was verified: `find_spec("tools.gates.lib.config")` -> `tools.gates.lib in sys.modules True`, `yaml True` (lib `__init__` ran) |
| Q-SG-28 | yes | D32 :46 "force their output forms (pinned config and attributes)" names no core.worktree; the SG.77 probe reproduces |
| Q-SG-3 | divergence | R3-SG-7 |
| Q-SG-8 | yes | the sheet maps Q-PC-18 a/b to Q-SG-8 a/b; Q-PC-18 was answered (c), which maps to neither |

Every new question has two or more options with a cost.

## claims (today cells re-run at 2e43f13 in `<R>`, /usr/bin/grep, bash)
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| gate code unchanged | `git diff --stat f1057e5 2e43f13 -- tools/gates ':!tools/gates/tests' \| wc -l` | the same without the exclusion shows a change exists | 0 / "2 files changed, 1 insertion(+), 37 deletions(-)" (tests only) | PASS |
| skeleton counts | `grep -cE "$G"` gate_plan / make_review_package | - | 29 / 37 | PASS |
| SG.26 | `cat <16> \| grep -cE '\bopen\('` | `ls` of the 16 files = 16, `git ls-files` 16 | 15 | PASS |
| SG.27 | `grep -ohE 'os\.[a-z_]+(\.[a-z_]+)?\(' <16> \| sort \| uniq -c` | a fs call outside the list | listdir 7, stat 2, lstat 1, makedirs 1, isfile 21, isdir 13, exists 2, lexists 2, islink 2, realpath 8, getsize 1, fdopen 1, unlink 1; the rest do no I/O (join, relpath, dirname, abspath, ...) | PASS |
| SG.29 | false `glob` hits | - | gate_plan.py:45; pre_tool_use.py:17 :31 :51 :85 :215 | PASS |
| SG.39 | `sed -n 52,53p` | - | def :52, run :53 | PASS |
| SG.65 | `grep -nE 'import [A-Za-z_.]+ as '` | - | gate_dispatch.py:21 :23, gate_plan.py:35 | PASS |
| SG.20 | filter.zz clean+smudge, `*.py filter=zz` -> `grep -c 'open\('` | `git diff --stat` under the filter -> 40 files changed | 4 -> 4; `git config --list --show-origin \| grep -iE 'attributesfile\|filter\.'` none; `~/.config/git/attributes` absent | PASS |
| R N/A | `.git/info/attributes` `* text eol=crlf` + exclude `*` -> `--show-toplevel` | - | `<R>/repo` unchanged | PASS |
| SG.63 | L=lib/config.py: count, then symlink, dir and 000 | - | 2; 2 rc 0; `Is a directory` rc 2; `Permission denied` rc 2 | PASS |
| SG.64 | `printf '# \377\n' >> $L; grep -nE` | BOM and CRLF variants | `14:` `15:` rc 0; 2; 2 | PASS |
| SG.66 | WT_ONLY append; `git status`; rm | - | `27:open("WT_ONLY")`; ` M`; `No such file` rc 2 | PASS |
| SG.75 | cwd logical `/tmp/...`, `env -u CLAUDE_PROJECT_DIR python3 -c repo_root()` | with `CLAUDE_PROJECT_DIR=/tmp/...` | `/private/tmp/.../repo` vs `/tmp/.../repo` (tools/.venv 3.12.14 and /usr/bin 3.9.6 alike) | PASS |
| SG.12 | `CLAUDE_PROJECT_DIR=/nonexistent ... repo_root()` | - | `/nonexistent` | PASS |
| SG.76 | from `<S>/norepo` | - | `GateError: git rev-parse --show-toplevel: fatal: not a git repository` | PASS |
| SG.77 | clone-local `core.worktree <S>/probe77` | after unset | `<S>/probe77` (repo_root too, though the dir does not exist) -> `<S>/repo` | PASS |
| SG.78 / .81 | exclude a scratch gate and a scratch lib .py | `git status --porcelain --untracked-files=all --ignored -- tools/gates` shows `!! tools/gates/zz_scratch.py` | ls 17, ls-files 16, status 0 (also 0 with the lib file) | PASS |
| SG.82 | `git config --list --show-origin \| grep -i excludesfile` | - | none | PASS |
| SG.79 | `python3.12 -I -c 'flags'` with env set | without `-I` | `0 None` vs `1 <prefix>` | PASS |
| SG.57 | `sed -n 38p <spec> \| grep -o 'fixed checklist ([^)]*)' \| tr ';' '\n' \| wc -l` | - | 6 | PASS |
| cites | `grep -n 'id: B5-PROC-2$'` -> 1764; :1767 class; plan :7185 holds "Owner task S2-b5-process-code" and the attribute and ignore list | - | as cited | PASS |
| install-hooks bullet | `cat -n` :9-:52 | - | as listed (see R2-SG-7) | PASS |
| hook :54-:55 | `sed -n 25,60p git-pre-commit.sh` | - | index-file assignment :54, export :55, exec :60 | PASS |
| SG.11 OPEN "guard allows clones only" | `git worktree add --detach <S>/wt` from `<R>` (pwd -P and toplevel under `<S>`) | - | `--show-toplevel` `<S>/wt`; repo_root `<S>/wt`; with `CLAUDE_PROJECT_DIR=<R>` -> `<R>` (mismatch accepted) | FAIL (R3-SG-1) |
| SG.24 OPEN | `grep -nE 'open\(\|os\.' ok.py syntax.py latin2.py` | - | all three printed, rc 0 (no item fails) | FAIL (R3-SG-1) |

## findings raised
**R3-SG-1 LOW: today cells read OPEN though they can be observed (R2-SG-5 class, re-run over all OPEN cells).**
- SG.11 reads "OPEN (no worktree made: the guard allows clones only)". That is false: a worktree of the scratch clone lies under the scratch dir and passes the DEC-0032 guard.
  - Observed: repo_root -> `<S>/wt`.
  - With `CLAUDE_PROJECT_DIR=<R>` it returns `<R>`, not the worktree toplevel, and accepts it.
- SG.24 reads "OPEN (no generator)", while its sibling SG.23 records the by-hand grep. The same grep over ok/syntax/latin2 prints all three, rc 0.
- The other OPEN cells (SG.21, .22, .51-.54, .56, .58-.62, .67-.70, .72-.74, .80, .82) name no existing input or code, so they were not re-classed.

**R3-SG-2 LOW: rows columns drift (the R2-SG-4 class recurred).**
- Q-SG-17's rows column reads "SG.11-.16, .64, .68, .75, .76". SG.68 now cites only "DEC-0129 sheet Q-SG-9 (a)".
  - A per-Q grep of the SG rows shows the rows citing Q-SG-17 are SG.11-.16, .64, .75, .76.
  - The fixer's report lists both sets: with .68 in the R2-SG-4 line, without it in the sheet table.
- Q-SG-25's rows column lists SG.82, which cites Q-FH-24 (a) but not Q-SG-25. Its option (a) also names SG.19 and .23, which are not in the rows column.

**R3-SG-3 MEDIUM: coverage gaps for I1 (D24 + D31, plan :7185 "each ... under D31/D32/D35").**
- No row and no N/A for:
  - I1 x text grammar: a followed `tools/gates/lib` module with a syntax error. Q-SG-2 (a) makes it "an input FAIL"; which importing gate's item FAILs under Q-SG-4 (a) rows-per-gate is unstated.
  - I1 x file type: an empty lib module (Q-SG-14 (a)).
- The N/A paragraph routes T's dimensions to "I1: SG.25, .63-.66, .81". None of those rows holds either value: SG.63's values are symlink, directory, FIFO and unreadable; SG.64 is encoding.
- `grep` of SG rows for syntax or empty -> SG.15, .16, .24, all item A1.
- Replacement (hypothesis): one row per pair, verdict from Q-SG-2 (a) and Q-SG-14 (a). The isolation half (one lib failure against several gates) is a new question if no line states it.

**R3-SG-4 LOW: SG.7's verdict leaves out its Q-SG-3 dependency.**
- SG.7 reads "accepted (read as a source ...)" under Q-SG-12 (b).
- The whitelist line also requires "in the gate-source set (OPEN Q-SG-3)".
- Q-SG-3 (a) "whitelist = the 6 `gate_*.py`" and (b) "fullmatched against `run_all.GATES`" would refuse `/etc/hosts`.
- So SG.7's verdict is accepted only if Q-SG-3's answer admits outside-root paths. The cell must cite Q-SG-3, as SG.78 does.

**R3-SG-5 LOW: the sheet's dependencies D30-4 and D30-2 are missing.**
- Sheet D :251 Q-SG-9 depends on "D30-4 (A; adds a subprocess-outcome dimension to D31)".
  - D30-4 (sheet A :69) reads "no recommendation", so it is still OPEN.
  - Q-SG-26's options (12 / 11 / 14) and SG.57's "11 or 12" ignore it.
- Q-SG-4 (:246) and Q-SG-18 (:260) depend on D30-2, answered (a) "The D32 helper owns every git call; a suite test bans `subprocess.run(["git"` outside lib".
- `grep -c 'D30-' <draft>` -> 0.
- Replacement: name D30-4 in Q-SG-26, SG.57 and the dimension whitelist, and D30-2 (a) at SG.36-.40 and .80, or argue each irrelevant.

**R3-SG-6 LOW: Q-SG-24 shows one side of a divergence.**
- Q-SG-24 quotes the sheet's why: "its reach to a non-empty source with no calls is a reading".
- It omits the question the owner answered: Q-SG-14 (sheet :256, draft :131) "An empty source (or one with no calls): an empty skeleton or a FAIL?", recommendation (a) FAIL.
- On one reading that answer already settles no-calls as FAIL; on the other it is open. Both quotes belong in the row. It is not mine to pick a side.

**R3-SG-7 LOW: Q-SG-3's open status is a divergence; Q-SG-3 and Q-SG-8 are no longer verbatim.**
- DEC-0129 approve-except-flagged (decisions.yml): "every row's recommendation is the owner's answer, except rows A1-1b, Q-FH-10, Q-FH-11 and S-D-1".
- Sheet Q-SG-3's recommendation: "no recommendation: follows Q-FH-24 (its a: D32 reaches every tools/gates script -> c; its b: gates only -> a)". Q-FH-24 is answered (a), so one reading gives Q-SG-3 = (c).
- b5r17-fix2-common.md: "rows whose recommendation reads 'no recommendation' (they stay OPEN)". That is a controller rule, a hypothesis under D25.
- The draft takes the controller rule's side ("the row itself stays OPEN"). This needs a ruling: proposed Q-SG-29 below.
- Fix rule 3 says open rows stay verbatim, yet the Q-SG-3 and Q-SG-8 question cells now carry an appended parenthetical. The note belongs outside the question cell.

**R3-SG-8 LOW: a raw BOM character in the draft.**
- SG.14's today cell (line 27) now holds a literal U+FEFF where round 15 had the visible `﻿`.
- `grep -c $'﻿' <draft>` -> 1, at line 27.
- The BOM evidence is invisible in the cell, and the draft carries a stray U+FEFF.

**R3-SG-9 LOW: the interpreter path cannot be reproduced.**
- SG.12, .75, .76 and .79 run `.venv/bin/python3` from the clone root.
- No `.venv` exists in the checkout root or in a fresh clone: `find <checkout> -maxdepth 3 -name .venv` -> only `tools/.venv` (git-ignored).
- The outputs reproduce with `tools/.venv/bin/python3` (3.12.14). The cells should name that path.

## decision needed (for the controller to carry; the review itself did not need it)
- **Q-SG-29**: Is Q-SG-3 answered by DEC-0129 through the sheet row's own mapping "its a ... -> c" with Q-FH-24 (a)?
  - (a) Yes: Q-SG-3 = (c) `--all` over the 16 Python files. Cost: SG.17, .21, .22, .60-.62, .78, .81, .82 settle, and SG.7 is then decided by (c)'s set, roughly 9 cells rewritten; Q-SG-19 (c) becomes live.
  - (b) No: "no recommendation" rows stay OPEN as fix-common rules. Cost: 0 text now, and the 9 cells stay OPEN until the owner answers Q-SG-3.

## unrelated findings
- None new.
- Seen again: `repo_root()` accepts CLAUDE_PROJECT_DIR unchecked (lib/__init__.py:61-63). Already registered as B5-UR-11 (findings.yml :2645); the draft could cite it at SG.12 and .75.
- Seen again: the round-14 `-I` finding. My `-I` probe wrote `tools/gates/__pycache__` and `lib/__pycache__` in the clone; both were removed (`find ... | wc -l` -> 0).

## denials
none.

## background jobs
none started.

## budget used
About 95k of 250k tokens (my estimate) and about 25 of 30 minutes.
