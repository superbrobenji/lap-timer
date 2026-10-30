status: DONE_WITH_FINDINGS

Fix round 1 of the 2026-10-29 replan drafts (row PROG-1). Brief: .superpowers/sdd/program/program-docs-fix1-brief.md. Workspace text only: every edit is under `.superpowers/sdd/program/draft/` (git-ignored). The checkout is untouched (`git status --short` is empty, HEAD 6ba57dc). Probes ran only in the scratch clone `scratchpad/prog1fix-clone` under the DEC-0032 guard (set -euo pipefail; `git rev-parse --show-toplevel` = `pwd -P`; the path is outside the checkout). The clone has been deleted.

## commits
none (the dispatch said no commit in the checkout).

## files written (drafts; target path in brackets)
- draft/umbrella-replan-section.md [inserted before "## Checkpoint protocol for this plan" in docs/superpowers/plans/2026-10-17-strict-delivery.md]
- draft/playbook-two-streams.md [docs/superpowers/playbooks/two-streams.md]
- draft/program-tooling-plan.md [docs/superpowers/plans/2026-10-29-program-tooling.md]
- draft/prototype-field-ready-plan.md [docs/superpowers/plans/2026-10-29-prototype-field-ready.md]
- draft/docs-reorganisation-plan.md [docs/superpowers/plans/2026-10-29-docs-reorganisation.md]
- draft/board.md [docs/superpowers/state/program/board.md]
- draft/prototype-state/ledger.md [docs/superpowers/state/prototype/ledger.md]
- draft/strict-delivery-handoff.md (new) [docs/superpowers/state/strict-delivery/handoff.md, replaces the b4 handoff]
- draft/prototype-handoff.md (new) [docs/superpowers/state/prototype/handoff.md]
- draft/mutations/PROG-1.txt (new) [docs/superpowers/state/strict-delivery/mutations/PROG-1.txt]
- draft/decs-0132-0140.yml: not touched

## findings applied (draft file:line)
- F1: applied. Ledger line 1 is now `plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md` (prototype-state/ledger.md:1); the heading moved below it. Check 6 run_all is rc 0.
- F2: applied. All three plans now have the agentic header, Goal, Architecture, Tech Stack, Spec, `## Global Constraints`, `## Review Focus`, `## Block` headings, and `### Task N` entries with Files, Findings, Siblings and Gates (tooling :30-:100, prototype :63-:115, docs-reorganisation :44-:105). The prototype plan writes P0's three tasks in full; its Task 3 writes the tasks for P1-P5. gate_plan --plan gives rc 0 on each plan: 28, 31 and 31 PASS, with no other line. With a ledger naming each plan (all three in flight), run_all gives rc 0, 298 PASS and 0 other.
- F3: applied. umbrella :3 now says "91 commits `main..6ba57dc`", measured at the brief's HEAD (it was 90 at 2134a43).
- F4: applied. The heading is "(DEC-0132..DEC-0151)" (umbrella :1).
- F5: applied. Both quotes are now verbatim with line numbers: rule 6 at umbrella :17 and the S3 precondition at :8.
- F6: applied (umbrella :8). S3 :38 drops gate_checkpoint.py. The S3 :1362 and :2464 block ends now write the handoff by hand in the umbrella front-matter format and run the manual checkpoint.
- F7: applied per DEC-0151 (defer-both). Umbrella :8 now runs S3 Tasks 1-6 only. The deferred row (:40) covers S3 Tasks 7, 8 (grace deletion included) and 9, and says the five grace-listed sites stay report-only.
- F8: applied (playbook :24-:35). There are ownership rows for devcontroller/, CMakeLists.txt, build.sh, README.md, docs/power-of-10-*.md, .gitignore, CLAUDE.md, docs/reviews/ and all 13 specs (strict-delivery and P10 go to A, the 11 product specs to B), plus partitions.csv, sdkconfig.defaults*, dependencies.lock, keys/, docs/pitch.md, docs/display-glyphs.md and docs/superpowers/state/README.md. ci.yml goes to A, because S3 Tasks 1 and 6 change it; stream B asks for ci.yml changes at a sync. Any path no row names belongs to no stream and is raised at the next sync. A new "Shared files" section sets the rules: registers are append-only and a rebase keeps both blocks; on the board, each block edits only its own row.
- F9: applied per DEC-0146. Umbrella step 4 (:9) moves the docs right after field-readiness; the index lands in step 1. The playbook (:37) and the docs-reorganisation header say the same, and that plan's tasks stay.
- F10: applied. The layout table now has one source row per file group with no overlaps (docs-reorganisation :29-:42): 12 product specs, 1 P10 spec and 1 strict-delivery spec, which accounts for all 13 tracked specs. Strict-delivery plans go to docs/process/ and the rest to docs/product/plans/. Task 1's sweep (:64) now also covers tools/program/, context.yml, docs/INDEX.md, tools/*.py, the bench helpers and the memory notes.
- F11: applied (playbook :44-:52, tooling plan Architecture and Task 2). The loader now prints only the read list, under 16 KB. The session Reads each entry, and a PostToolUse Read hook records path, blob sha and range. Measured in this session: 25,000 bytes of Bash output reached the context whole; a 30.1 KB output was moved to a file with a 2 KB preview.
- F12: applied. An entry has an optional `ref`. The loader writes `git show <ref>:<path>` to a git-ignored copy and lists that copy as the path to Read (playbook :45-:46).
- F13: applied. The session's own Edit, Write or NotebookEdit on a read-list file refreshes the receipt, but only when the receipt's sha matched the file before the edit. A write by someone else, or by a Bash command, needs a new Read (playbook :51; tooling Task 2 Step 1).
- F14: applied. sha is defined as a commit, read with `git show <sha>:<path>`. A path that is not tracked at that commit FAILs, and so does a bare `path:line` with no `@sha "quote"` (playbook :57). "Stated limits" (:60-:64) adds: uncited facts pass the check; tracked documents cite only tracked files. The prototype plan now names a tracked import target, tools/tracks/research-2026-10-29.md, in place of the git-ignored path.
- F15: applied.
  - (a) Deferred row for DEC-0102 successor-rows (umbrella :24).
  - (b) Rows for GitHub issues B5-U1..U5 (#145-#149) (:37) and B5-U8 (b6 row :39).
  - (c) Round-17 U1 is a deferred row (:29). The commit-signing question is on the board's pending decisions (board :7). The lean close text now restates or closes B5-CAPS-2 (umbrella :7).
  - (d) Round-15 notes are a deferred row (:30).
  - (e) The S3 :47 question is answered by DEC-0149, stated at umbrella :8.
- F16: applied. The catch-all row is split by kind, each with its own reason: exotic inputs (:32), output format and text drift (:33), B5-UF-4 per DEC-0150 (:34), process (:35).
- F17: applied per DEC-0145 (umbrella :11-:14; tooling and prototype Global Constraints).
- F18: applied per DEC-0147. Scope 1 uses UBX software modes only. Scope 2 covers 8.3 plus the 8.4 calibration only; PARK wake, low-power motion and the IMU ladder move to Plan 6 (prototype :39-:40, :51; umbrella :43). Hardware as built adds the GPIO26 line (prototype :31). The review cited board.c:47, but the define is at board.c:48; line 47 is a comment.
- F19: applied per DEC-0148. The tooling block gets its own branch, `block/program-tooling` from the b5 tip (umbrella :6, playbook :22, board :7, tooling plan header).
- F20: applied (tooling Task 2 Step 6). The step names the S1 plan's strict-block copies (S1 :7339-:7535), check-skill.sh, and a sha256 re-ledger of every installed prompt. It states the controller.md margin: 5281 of 5500 bytes, 219 left. To stay inside the cap, the "run the loader" instruction is enforced by the hook, not by a strict-block sentence.
- F21: applied as D37-D39, the numbering the round-17 draft uses (b5r17-docs-draft.md :250-:252); D36 stays with the budget rule (umbrella :7). See Q-prog-1.
- F22: applied. The fix brief lists all seven drafts and their targets, and they are placed at those targets.
- DEC-0146: docs/INDEX.md and context.yml on the current paths are in tooling Task 1.
- DEC-0149: S3 :47 is answered (tracked rows), umbrella :8.
- DEC-0150 and DEC-0151: both are in the deferred list (:34, :40).
- Handoffs: both are written in umbrella front matter. The b5 handoff has branch block/strict-delivery-b5, block 5, next_task set to the tooling block and then the lean close, `findings_in_flight` with all 88 not-closed rows at 6ba57dc, and `open_decisions: []`; the body names the 16 D33 questions only as decisions.yml lines 1028-1303. The prototype handoff names plan 2026-10-29-prototype-field-ready, branch block/prototype-p0 from the b5 tip, and next_task "P0 Task 1". Each resume prompt says to read the playbook, the board, the handoff and the plan, in that order, and to stop at the checkpoint. The new playbook section "Start of every session" (:5) says the same.

## gates run (scratch clone at 6ba57dc, every draft placed at its target and staged, git-ignored review packages copied in)
| command | result |
|---|---|
| `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) python3 tools/gates/run_all.py` | rc 0, 239 PASS, 0 other |
| `gate_plan.py --plan` program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS, 0 other |
| same prefix, `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` | Ran 658, OK (skipped=1) |
| same, `tools/.venv/bin/python3` | Ran 658, OK (skipped=1) |
| run_all with temporary ledgers putting all three plans in flight | rc 0, 298 PASS, 0 other |
| run_all after the PROG-1 mutation restore | rc 0, 239 PASS, 0 other |
| first run, before fixing two Files items | rc 1: `FAIL plan.prototype.files.3` and `plan.files.2` (docs-reorganisation), both "a second backticked span". Fixed in the drafts; the suite's test_the_repository_passes_every_installed_gate failed on the same FAIL |

## mutation transcript
draft/mutations/PROG-1.txt, in the clone: fix present PASS, fix reverted (umbrella from 6ba57dc, playbook removed) FAIL, fix restored PASS.

## claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| 91 commits main..6ba57dc | `git rev-list --count main..6ba57dc` | `main..2134a43` | 91 / 90 | PASS |
| S2 plan 4,831 -> 8,695 lines; spec D1-D35 on branch | `git show main:/6ba57dc:S2 \| wc -l`; max D row at 6ba57dc | | 4831 / 8695; 35 | PASS |
| 16 open D33 questions, decisions.yml 1028-1303 | registers.open_decisions | DEC-0122 line | 16 (DEC-0106..0121); DEC-0106 at :1028, DEC-0122 at :1304 | PASS |
| PIN_GPS_PWR at board.c:48 | grep -n | sed -n 47p | :48 define; :47 a comment | PASS (the review's :47 is wrong) |
| controller.md 5281 bytes | `wc -c` | | 5281 | PASS |
| Bash output limit | printed 25,000 bytes; gate_plan.py 30.1 KB | | whole / "Output too large (30.1KB) ... Preview (first 2KB)" | PASS |
| no register ordering rule (append-only safe) | grep order/sorted in registers.py, gate_register.py | | no id-order check | PASS |
| draft D37-D39 = DEC-0103/0130/0131 | grep b5r17-docs-draft.md | D36 in b5r15-budget only | :250-:252 | PASS |
| B5-U1..U5 = issues #145..#149 | findings.yml owner.number | | 145..149 | PASS |
| S1 strict-block copies :7339-:7535 | grep "# Strict block" and fences | | :7339 first path line, :7535 closing fence | PASS |

## Decision needed / questions (for the controller to batch for the owner)
- Q-prog-1 (F21): the spec row numbers. (a) D37-D39 as drafted, leaving D36 reserved for the budget rule (no renumbering). (b) D36-D38, which renumbers the D36 budget draft. Cost of (b): the b5r15-budget draft and its questions are re-edited.
- Q-prog-2 (DEC-0148): the PR base and grant for `block/program-tooling`. (a) Base `block/strict-delivery-b5`, so the work lands inside b5's squash. (b) Base `main`, which carries every b5 commit until b5 merges. It is also open whether grant:strict-delivery (DEC-0001) covers this plan. The draft says only "an owner merge grant that covers this block's PR".
- Q-prog-3: stream B PRs while b5 is unmerged. A P0 PR against `main` carries b5's commits. Drafted: the PR merges only after b5 has merged and the sync has rebased stream B, and the next block branches from the previous tip. The alternative is to hold all stream B PRs until b5 merges. Either way the owner decides.
- Q-prog-4: merge grant for the prototype plan. No grant exists; the handoff says `grant: none`.
- Q-prog-5 (divergence): the brief says "block P0", but the umbrella handoff format has `block: <n>`, and gate_plan only reads `CHECKPOINT b<digits>`. Drafted `block: 1  # P0`, mapping P<k> to plan block b<k+1>, with ledger line `CHECKPOINT b<k+1> P<k> <sha>`. The alternative is to rename the prototype blocks b1..b6.
- Q-prog-6 (F8): ci.yml was assigned to stream A, one of the review's two options. The other is a split by job. Paths no row names (LICENSE, for example) go to the owner at a sync.
- Note (no decision): the tooling and prototype plans route no register ids (`Findings: none -- ...`). Each block start registers one planned tracker row per task, so implementer dispatches have a row. The tooling ledger lives at docs/superpowers/state/program-tooling/ledger.md.

## Unrelated findings
- An empty, untracked directory `docs/superpowers/state/yy` exists in the checkout (`ls -la`: dated Oct 23, `git ls-files` gives nothing). It is harmless to gate_plan, which only reads `<dir>/ledger.md`. It is a stray artefact of earlier work; class: untracked state-dir litter.

## denials
None from the permission system. The project PreToolUse hook refused two Bash commands because their text held the hook's forbidden words. The first refusal, verbatim: `FAIL hook.bypass: core.hooksPath (it moves the git hooks away)` (a handoff heredoc quoting a trap line). The second: `FAIL hook.target: <scratchpad>: fatal: not a git repository` (a heredoc that held "git commit"). In both cases I wrote the file with the Write and Edit tools instead. I added no flag and made no config change. I removed a `--no-gpg-sign` line from the probe script before it ran, so no probe made a commit.

## background jobs
none.

## budget used
About 150k of 300k tokens, about 65 of 75 minutes.

## Round 2

status: DONE. The owner's answers at HEAD 314b46d (DEC-0152..DEC-0157, which settle Q-prog-1..6) are applied to the drafts. Workspace text only: no tracked edit, no commit, and `git status --short` in the checkout is empty.

### applied
- DEC-0152: D37-D39 stand. The umbrella lean-close text cites DEC-0152 (umbrella :7), and the heading is now "(DEC-0132..DEC-0157)".
- DEC-0153: the block/program-tooling PR has base `block/strict-delivery-b5` and merges under DEC-0001. Updated in the umbrella step 1, the playbook "Branches", board row A, the tooling plan (header, Global Constraints, CHECKPOINT step) and the strict-delivery handoff next_task.
- DEC-0154: stream B PRs merge only after b5 has merged and a sync has rebased stream B onto `main`. Until then the blocks accumulate on stream B's branch, each branching from the previous tip. Updated in the prototype plan's Global Constraints and b1 CHECKPOINT step, the playbook "Branches", board row B and the prototype handoff body.
- DEC-0155: the prototype handoff now has `grant: DEC-0155`. A block PR merges when every check passes, on the owner's sentence "merge when all checks pass". This is stated in the plan, the playbook and the handoff.
- DEC-0156: P0..P5 are renamed to b1..b6 everywhere: plan header, blocks table, `## Block b1` heading, task ids `b1-T1`..`b1-T3`, Task 3 "write b2-b6 tasks", CHECKPOINT line `CHECKPOINT b1 <sha>`, and branches `block/prototype-b<N>` (b1 = `block/prototype-b1`). The board, the ledger line format `- b<N>-T<n>` / `CHECKPOINT b<N> <sha>`, the handoff (`block: 1`, next_task "b1 Task 1", its resume prompt) and the playbook are renamed too. The P<k> to b<k+1> mapping text is removed. `grep -n "P[0-5]\b\|P<k>\|<k+1>\|prototype-p"` over the drafts: no hits.
- DEC-0157: the playbook ownership rows and the ci.yml and unnamed-path sentences now cite DEC-0157.
- Both handoffs now have head/base `314b46d`. Between 6ba57dc and 314b46d only decisions.yml changed (+52 lines), so findings_in_flight (88 rows) and resume_line 191 are unchanged.
- Settled notes removed: the round-1 Q-prog-1..6 questions in this report were answered by DEC-0152..0157, and no "owner decides" note remains in the drafts.

### defect in my own round-2 edit, found and fixed
A script line `open(p,'w').write(ren(open(p).read()))` truncated draft/board.md to 0 bytes, because Python opened the file for writing before reading it. I found it because a later replacement failed on board.md, and `cat` showed the file empty. I rewrote board.md in full from the round-1 content with the round-2 changes. `wc -l` then gave board.md 13 lines, and every other draft kept its expected length. No other file used that pattern.

### gates (scratch clone at 314b46d, DEC-0032 guard, all drafts placed and staged)
| command | result |
|---|---|
| run_all | rc 0, 239 PASS, 0 other |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS |
| suite /usr/bin/python3 -W error | Ran 658, OK (skipped=1) |
| suite tools/.venv/bin/python3 -W error | Ran 658, OK (skipped=1) |
| run_all, all three plans in flight (temporary ledgers) | rc 0, 298 PASS, 0 other |
| run_all after the mutation restore | rc 0, 239 PASS |

The clone has been deleted.

### mutation transcript
draft/mutations/PROG-1.txt has been re-cut for the revert source 314b46d: step 1 PASS, step 2 (umbrella from 314b46d, playbook removed) FAIL, step 3 PASS.

### denials, background jobs
none.

### budget (round 2)
About 25k of 100k tokens and about 15 of 30 minutes.
