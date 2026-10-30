status: DONE_WITH_FINDINGS

Task review of the 2026-10-29 replan drafts (row PROG-1), read-only. Brief: .superpowers/sdd/program/program-docs-review-brief.md. Authority: DEC-0132..DEC-0144 (the coordinator's mid-task update added DEC-0142..0144, HEAD 2134a43), DEC-0103, DEC-0130, DEC-0131. Drafts reviewed as they stood at 21:09: umbrella-replan-section.md, playbook-two-streams.md, program-tooling-plan.md, prototype-field-ready-plan.md, docs-reorganisation-plan.md, plus board.md and prototype-state/ledger.md (new, see F22).

Verdict: spec ❌ (F4, F15-F18), quality ❌ (F1, F2, F6-F14), claims-executed ✅ (every claim below was run; the false ones are findings).

## commits
none (read-only review). Checkout untouched: `git status --short` empty, HEAD 2134a43.

## gates run (scratch clone under the DEC-0032 guard: `pwd -P` = .../scratchpad/prog1-clone and equal to `git rev-parse --show-toplevel`; git-ignored .superpowers/sdd/strict-delivery/review copied in; clone deleted at the end)
| tree | command | result |
|---|---|---|
| d69dd7a, no drafts | `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) python3 tools/gates/run_all.py` | rc 0, 208 PASS, 0 other |
| d69dd7a, no drafts | same prefix, `<py> -W error -m unittest discover -s tools/gates/tests -t .` | /usr/bin/python3: Ran 658, OK (skipped=1); tools/.venv: Ran 658, OK (skipped=1) |
| 2134a43 + the five brief drafts (umbrella section inserted before "## Checkpoint protocol for this plan", playbook, three plans), staged | run_all | rc 0, 208 PASS, 0 other |
| same | suite on both interpreters | /usr/bin/python3: Ran 658 OK (skipped=1), 136.7 s; .venv: Ran 658 OK (skipped=1), 150.5 s |
| same + board.md at docs/superpowers/state/program/board.md + prototype-state/ledger.md at docs/superpowers/state/prototype/ledger.md | run_all | **rc 1: `FAIL plan.input: docs/superpowers/state/prototype/ledger.md: line 1 must be 'plan: <plan file>'`** (F1) |
| same, ledger line 1 corrected to `plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md` (contradiction) | run_all | **rc 1, 5 FAIL plan.prototype.{header.architecture, header.tech-stack, header.global-constraints, header.review-focus, tasks}** (F2) |
| same clone | `gate_plan.py --plan <each new plan>` | program-tooling 5 non-PASS, prototype 5, docs-reorganisation 7; umbrella with the section: the 2 pre-existing FAILs only (plan.tasks, plan.placeholder.L184 = B5-UF-4) |

Check 6 answer: the five files the brief names keep run_all only PASS and both suites green at 2134a43. The two extra drafts do not (F1), and any of the three new plans turns run_all red as soon as a ledger puts it in flight (F2).

## claims
| claim (draft) | direct | contradiction | output | result |
|---|---|---|---|---|
| "86 commits on block/strict-delivery-b5" | `git rev-list --count main..d69dd7a` / `main..2134a43` | count at earlier shas | 89 / 90; 2e43f13 = 86 | FAIL (F3) |
| spec D1-D23 on main, D1-D35 on branch | `git show <rev>:spec \| grep '^\| *D[0-9]+'` | max row each rev | main D23, d69dd7a D35 | PASS |
| S2 plan 4,831 to 8,695 lines | `git show main:S2 \| wc -l`, same at d69dd7a | | 4831 / 8695 | PASS |
| b5 2026-10-19..29 | `git log --date=short main..d69dd7a` first/last | | d533315 2026-10-19 "b5 start"; d69dd7a 2026-10-29 | PASS |
| owner rule 6 quote | umbrella :334 | | text is "The pre-bench fix work is **parked until this plan is complete**" (capital T, bold) | FAIL, inexact quote (F5) |
| "No other worktree or branch may exist at a checkpoint" | umbrella :334 | | verbatim | PASS |
| S3 precondition "tags strict-delivery-b1..b6" | S3 :35 | S3 :36-46 other preconditions | text "tags `strict-delivery-b1`..`b6`"; :38 also needs gate_checkpoint.py | inexact + incomplete (F5, F6) |
| deferred sources exist | `ls` of b5r14-draft-{precommit,helper,skeleton}.md, b5r15-budget/, b5r17-docs-draft.md, b5r17-subcontroller-report.md, b5r15-d30-investigation.md, decision-sheets/b5r16.md | | all present; d30 §6 at :94 has 4 ranked changes + 1 "not recommended" | PASS |
| sixteen open D33 sweep questions after DEC-0105 | parse decisions.yml answers | DEC-0105 answered | DEC-0106..0121 OPEN (16), DEC-0105 ANS | PASS |
| SD-S2-T7..T9 rows, B3-S4PREPUSH-1 | findings.yml | | present, planned/open | PASS |
| DEC-0132 lean-close list | DEC-0132 option text vs umbrella step 2 | | match (B5-FIX-5 + UR-16..19, RD-1/UD-1, UR-9) | PASS |
| DEC-0142, DEC-0144 text | decisions.yml | umbrella step 2 | match | PASS |
| DEC-0143 rules into strict blocks within 5500 | `wc -c ~/.claude/skills/strict-delivery/prompts/*.md`; sentence bytes | | controller 5281 + 198 = 5479 (21 bytes margin) | PASS, margin noted (F20) |
| audit ids CE-N2..N5, IO-N1..N4, APP-N1, DRV-N1 exist, HIGH | `grep` docs/reviews/2026-10-16-audit-post-fix-wave.md :381-393 | | all present, all HIGH | PASS |
| roadmap 8.1-8.5 | roadmap :248-252 | | present; 8.4 has more than the draft lists | PASS / see F18 |
| Killarney centre about 3.8 km off, outside 2000 m | haversine (-33.8567,18.5170) to OSM bbox and Wikipedia coords | killarney.json :2 | 3771 m / 3842 m; json radius 2000 | PASS |
| spec §10.2 example has the offset | spec :1453 heading, :1458 lat -33.8567 | | yes | PASS |
| GPS driver is a stub, no UART pins | `wc -l` gps_neo6m.c; grep | | 54 lines, `gps_reinit_uart` returns -ENODEV; no GPIO16/17 UART | PASS |
| display CLK 18 DIN 23 CS 5 DC 14 RST 13 BUSY 35; IMU 21/22/27 | board.c :48-54, display_epaper_ssd1680 :34-37 | | match | PASS |
| Kyalami 4522 m, S/F, 30 m gates | tracks-research.md :44, :46, :85 | | match | PASS |
| B branch rebase `git rebase --onto origin/main strict-delivery-b5-tip` works | umbrella checkpoint protocol pushes `strict-delivery-b<N>-tip` before merge (:353) | | the tip tag is in the protocol | PASS |
| loader receipt proves a file "was loaded into the context" | probe: print 125 KB through the Bash tool | | "Output too large (125KB). Full output saved to ... Preview (first 2KB)" | FAIL (F11) |
| loader can find the session | `env` in a Bash call | | CLAUDE_CODE_SESSION_ID set (and CLAUDE_CODE_CHILD_SESSION in a subagent) | PASS (spike still needed for child scope) |

## findings raised (proposed; the controller registers them)
F1 HIGH (check 6). draft/prototype-state/ledger.md line 1 is `# Ledger -- plan: docs/...`; gate_plan.py:93 `LEDGER_PLAN = ^plan: (\S+)\s*$`, so run_all FAILs plan.input. Replacement line 1: `plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md`, heading below it.

F2 HIGH (checks 4, 6). None of the three new plans meets gate_plan's plan format. program-tooling lacks `**Architecture:**`, `**Tech Stack:**`, `## Global Constraints`, `## Review Focus` and `### Task N` headings (it uses `## Task N`). prototype-field-ready lacks the same five. docs-reorganisation lacks seven, the agentic header and `**Spec:**` among them. A plan is in flight once its ledger exists (with a valid ledger: 5 FAIL, rc 1), so the first stream B checkpoint and every stream A block on these plans go red. Replacement: add the four header items and `### Task N` headings to each plan. The prototype skeleton needs at least P0's tasks written as `### Task N`, or an owner exemption.

F3 MEDIUM (check 3). Umbrella :3 says "86 commits": that was true at 2e43f13; d69dd7a has 89 and 2134a43 has 90. Replacement: "90 commits `main..2134a43`" (or cite the sha it is measured at).

F4 LOW (check 1). The umbrella heading reads "(DEC-0132..DEC-0140)", but its body cites DEC-0142 and DEC-0144, the prototype plan cites DEC-0141 and the tooling plan DEC-0143. Replacement: "## Replan 2026-10-29 (DEC-0132..DEC-0144)".

F5 LOW (check 3). Two quotes are not verbatim. Owner rule 6 is "The pre-bench fix work is **parked until this plan is complete**" (umbrella :334). The S3 precondition is "tags `strict-delivery-b1`..`b6`" (S3 :35). Quote both exactly, with line numbers.

F6 MEDIUM (check 4). The S3 precondition list also needs `tools/gates/gate_checkpoint.py` (S3 :38, "a FAIL stops the block"), and the b7/b8 block ends write the handoff in "umbrella front matter" (S3 :1362, :2464). Both are b6 deliverables, which are deferred. The draft changes only the tag range and the gate_checkpoint run. Replacement: "S3 :38 drops gate_checkpoint.py; S3 :1362 and :2464 write the handoff by hand in the current handoff.md format and run the manual checkpoint".

F7 HIGH (check 4). "From Task 8 only the deletion of the grace list" cannot be executed as S3 is written. Task 8 Step 1 sets the test HEADER to "the twelve-column header" (S3 :2948), which is Task 7's parser. Step 4 is "executed in Task 7 Step 6" (:3084). :2483 makes Task 8 follow Task 7's commit. Step 6's expected "Ran 63 tests" assumes Task 7. Step 5 files G1..G5. The draft names no block, no rewritten steps and no estimate for the cut. Replacement: a named task (e.g. "S3 T8g: grace deletion", in b8 after Task 6) whose steps the stream A session rewrites against the seven-column register, keeping Steps 2, 3, 5 and 7 and re-measuring the counts. Its estimate goes inside the 3 days.

F8 HIGH (check 4). The file ownership is neither disjoint nor complete.
- (a) S3 Tasks 1, 6 and 8 modify `.github/workflows/ci.yml` (S3 file table :119), but the playbook :22 gives ci.yml to stream B.
- (b) The registers (`findings.yml`, `decisions.yml`, `claims.yml`) and `board.md` are written by both streams (playbook :8, :12). ID ranges prevent id collisions, not merge conflicts on one YAML list.
- (c) Files the plans touch that neither stream owns:
  - `devcontroller/`: scope 7, offload and the dev-kit storage guard.
  - `CMakeLists.txt` and `build.sh`: the DRV-N1 sites (audit :386).
  - `README.md` and `docs/power-of-10-deviations.md`: S3 Task 8; the P10 cleanup.
  - `.gitignore`: the receipt is git-ignored.
  - `CLAUDE.md` and `docs/reviews/`.
  - The other 11 specs under docs/superpowers/specs/, e.g. 2026-09-20-dev-controller-B-design.md.

Replacement: an ownership row for each listed path. ci.yml goes to A, or is split by job with a rule. A rule for the shared registers and board: append-only, the later rebase re-sorts, conflicts are resolved at sync.

F9 MEDIUM (check 4). The docs reorganisation needs "no stream branch open on the moved paths" (playbook :24; plan :3). The move takes docs/bench, docs/hardware, the lap-timer spec and the roadmap, all stream B paths, and stream B keeps a branch open from P0 until field-ready (about 2026-11-12). So the condition cannot hold before the prototype is done, while the umbrella schedules the move "at the first sync point after the b5 merge". This is an owner timing question: Decision D-2.

F10 MEDIUM (check 4). The docs-reorganisation layout sources overlap. `docs/product/` takes the lap-timer spec, the roadmap and other product docs, while `docs/process/` takes all of `docs/superpowers/specs/` and `plans/`: 13 specs and 35 plans, most of them product plans and specs, not strict delivery. Task 1's path sweep also leaves out `tools/program/`, `docs/superpowers/state/program/context.yml`, the `tools/*.py` and bench helpers that name doc paths, and the memory notes. Replacement: one source row per file group with no overlaps, and the sweep list extended.

F11 HIGH (check 5). The receipt cannot prove that content reached the context. The Bash tool keeps only a ~2 KB preview of large output and moves the rest to a file (probe: 125 KB printed, "Preview (first 2KB)"). load_context.py "writes a receipt of every path and blob sha it printed" (tooling plan :14), and the read lists hold multi-thousand-line plans (S2 is 8,695 lines). Replacement: the loader prints at most one chunk under the tool limit per call, `--part k`, and records each chunk in the receipt. The hook requires every chunk. Alternatively, record the receipt from a PostToolUse hook on Read of each listed range.

F12 MEDIUM (check 5). A context.yml entry is `path, section, why` (tooling plan :13) with no git ref. The sync read list needs the other stream's handoff, which exists only on that branch (playbook :14 `git show origin/<branch>:<path>`). The loader fails it as a missing path. Replacement: an optional `ref` field; the loader reads `git show <ref>:<path>` and records the blob sha.

F13 MEDIUM (check 5). By Task 2's rule (tooling plan :18: refuse unless "every read-list file's current blob sha equals the receipt's"), a session that edits its own read-list files (handoff, ledger, board) is refused at its next Edit, Write or commit until it reloads. That means a reload after every ledger line. Replacement: a PostToolUse Edit/Write hook updates the receipt's sha for a file the session itself wrote, or the read lists exclude the session's own output files.

F14 MEDIUM (check 5). Three problems with the citation drift check.
- The playbook :39 says "every file fact ... carries `path:line@sha`", but check_cites.py only verifies citations that are there; an uncited fact passes, so the check cannot detect the hallucination DEC-0133 names.
- `@sha` is not defined as a commit or a blob.
- The prototype plan cites git-ignored workspace files (`.superpowers/sdd/program/tracks-research.md`) that have no sha.

Replacement: state that limit in the playbook's "Stated limits"; define sha as a commit (`git show <sha>:<path>`); tracked documents cite only tracked files (import the research first, as scope 5 already says).

F15 MEDIUM (check 2). Items missing from the deferred list:
- (a) DEC-0102 answer "successor-rows": three new open rows for R-B-13, R-A-5 and R-B-1. None is registered: `grep -n "R-A-5\|successor" findings.yml` finds nothing, and R-B-13 appears only in B5-ID-1/2, the D33 idiom sweep.
- (b) B5-U1..U5 (owner kind issue, #145..#149) and B5-U8 (owner S2-T7) fall outside the "owned by an S2-b5-* task" catch-all and outside the b6 row, which names only SD-S2-T7..T9.
- (c) Round 17:
  - Unregistered U1: docs/power-of-10-checklist.md:48-49 says "-Wconversion non-fatal" while the builds use -Werror (b5r17-subcontroller-report.md "Not yet registered").
  - The owner question about the docs reviewer's commit-signing override (same report, "denials"). DEC-0143 answers the rule but not the question.
  - B5-CAPS-2's class and repro become false once the restored tests land (same report, item 2). This lands in the lean close itself, so the lean close must restate or close B5-CAPS-2.
- (d) Round 15's two unregistered process notes (b5r15-subcontroller-report.md :54-57): `-I` ignores PYTHONDONTWRITEBYTECODE on 3.12, and the drafter's ~/Library/Caches write.
- (e) S3 :47's owner decision ("tracked rows (this plan)" / "CI red until S5", before b7 merges) is in the run-now scope but on no pending list or board.

Round 13 open items are all answered (DEC-0088..0092) and covered. Replacement: one deferred row each for (a), (b) and (d); (c) and (e) go on the board's pending owner decisions and into the lean-close text.

F16 MEDIUM (checks 1, 3). The "why deferred" cell of the catch-all row says these rows are "exotic inputs (FIFO, symlink, NFC/NFD, stale bytecode, global git config, VT/FF) a normal repository does not produce". That is false for several rows it covers:
- B5-UF-4: gate_plan --plan FAILs on the umbrella (2 FAIL, reproduced in the clone) and on S1 (39 FAIL).
- B5-FIX-10: PASS-line format against spec :191.
- B5-U9, B5-U10, B5-UF-2 and B5-UF-3: text drift.
- B5-CAPS-2, B5-PROC-1, B5-PROC-3, B5-UR-13, B5-UR-14 and B5-UR-15: process.

Replacement: "not in DEC-0132's lean-close list", with a per-row reason, or split the row by kind. Whether B5-UF-4 belongs in the lean close is Decision D-6.

F17 MEDIUM (check 1). Several rules are in no DEC:
- "One opus final reviewer and one final verifier over the whole block (not split by area)" (umbrella step 2 and tooling plan "Block end") overrides the checkpoint protocol's split by area (umbrella :353, DEC-0030).
- The lean-path bullets "one implementer and one task review per task" and "no D24 tables and no D30 rounds for developer tooling".

DEC-0132 says only "lean-path rules". Decision D-1.

F18 MEDIUM (checks 1, 4). Two scope items are narrower than the roadmap they cite.
- Scope 2 says "Real IMU (roadmap 8.3-8.4)" but lists only calibration from 8.4. Roadmap :251 also has "low-power motion mode + PIT/PARK wake integration, IMU ladder (SDA short test)", and DEC-0134 says "real GPS and IMU (roadmap 8.1-8.4)".
- Scope 1 includes "power modes" (8.2), but the GPS power switch is not built: board.c:47 PIN_GPS_PWR GPIO26 "drive LOW = GPS on"; wiring-extract.md :96 "GPS power switch (P-MOSFET on GPIO26) ... not built", :103. The hardware section leaves it out.

Decision D-3. Meanwhile add the GPIO26 line to "Hardware as built".

F19 MEDIUM (check 4). board.md row A puts the program tooling block on `block/strict-delivery-b5`, while the tooling plan says "One block, one PR, three tasks". b5 already carries its own tasks, and CLAUDE.md says "Block = PR, ≤3 tasks". Decision D-4.

F20 LOW (check 1). DEC-0143's two rules fit the installed controller.md by 21 bytes (5281 + 198 ≤ 5500). The task does not name the S1 plan's strict-block copies or the prompt hash ledger lines (B5-UR-13's class) that change with them. Replacement: list the copies and the ledger hash update in the task.

F21 LOW (check 4). The lean close's three spec rows (DEC-0144) have no D numbers. The deferred D36 budget draft holds D36, and the round-17 draft holds D37-D39. Replacement: name the numbers, e.g. D36-D38, with the D36 draft renumbered, or D37-D39 taken from the draft.

F22 INFO (process). The coordinator's update said "No other change", but playbook-two-streams.md also changed at 21:09 (4221 → 4763 bytes; new :16 "Branches: ..."), and board.md and prototype-state/ledger.md are new. The brief lists none of them and gives no target paths for the last two (assumed docs/superpowers/state/program/board.md and docs/superpowers/state/prototype/ledger.md). I reviewed all three.

## Decision needed (owner)
- D-1 Lean-path review shape. (a) Confirm one final reviewer + one verifier per block (DEC-0030's split by area lifted) and no D24/D30 for tooling. Cost: less review per block. (b) Keep the split by area. Cost: about one extra pair per task at every block end.
- D-2 Docs reorganisation timing. (a) Stream B pauses at a sync after the b5 merge (its block closed and merged into its branch) for the 1-day move. Cost: about 1 prototype day. (b) Defer the move until after field-ready. Cost: agents keep the current paths for about 2 weeks. (c) Move only stream A paths now and the rest later. Cost: two moves.
- D-3 Prototype IMU/GPS scope. (a) 8.4 calibration only, with PARK wake, low-power motion and the IMU ladder out, and GPS power modes limited to UBX software modes (no GPIO26 switch). Cost: 8.2/8.4 only partly done. (b) The full 8.4 and a GPS power switch. Cost: hardware not owned (no MOSFET) and Plan 6 coupling.
- D-4 Program tooling block placement. (a) Its own branch and PR from the b5 tip, merged before the b5 lean close. Cost: one more PR. (b) Inside b5. Cost: b5 goes past 3 tasks, against CLAUDE.md "≤3 tasks".
- D-5 S3 :47 (open since the S3 plan): tracked rows vs CI red until S5. Needed before b7 merges.
- D-6 B5-UF-4 (gate_plan FAILs the umbrella and S1 plans in a normal repository). (a) Add it to the lean close. Cost: about 0.5 day. (b) Defer it as tracked. Cost: gate_plan stays red on two in-tree plans.

## denials
none.

## background jobs
none started.

## budget used
About 130k of 250k tokens, about 50 of 60 minutes.

## Re-review

status: DONE_WITH_FINDINGS

Re-review at HEAD 314b46d. Authority: DEC-0103 and DEC-0130..DEC-0157. Drafts: every file under .superpowers/sdd/program/draft/ (umbrella section, playbook, three plans, board, prototype ledger, both handoffs, mutations/PROG-1.txt), as they stood after fix rounds 1-2 (program-docs-fix1-report.md). Verdict: spec ✅ (every DEC-0145..0157 applied; no contradiction found). Quality ❌ (R-1..R-4 below). Claims-executed ✅.

### commits
none. The checkout is untouched: HEAD 314b46d, `git status --short` empty.

### gates run (check 5)
Scratch clone `scratchpad/prog1rr-clone` at 314b46d, under the guard: `pwd -P` matched `/private/tmp/claude-501/*/scratchpad/prog1rr-clone` and equalled `git rev-parse --show-toplevel`. Every draft was placed at the target the fix report names and staged. The git-ignored review packages were copied in. The clone has been deleted.

| command | result |
|---|---|
| `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) python3 tools/gates/run_all.py` (all drafts placed; the prototype plan in flight through its draft ledger) | rc 0, 239 PASS, 0 other |
| same, with temporary ledgers for program-tooling and docs-reorganisation (all three plans in flight) | rc 0, 298 PASS, 0 other (90 PASS lines under plan.prototype / plan.program-tooling / plan.docs-reorganisation) |
| `gate_plan.py --plan` program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS, 0 other |
| `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` (same prefix, three plans in flight) | Ran 658 in 136.0 s, OK (skipped=1) |
| same with `tools/.venv/bin/python3` | Ran 658 in 148.1 s, OK (skipped=1) |
| mutations/PROG-1.txt replayed | step 1 PASS; step 2 (umbrella from 314b46d, playbook removed) FAIL; step 3 (`git checkout --` from the index) PASS. Matches the transcript |

### F1-F22 closure (each dimension re-run over the whole document set)
| id | closed? | evidence |
|---|---|---|
| F1 | yes | ledger line 1 is `plan: ...prototype-field-ready.md`; run_all rc 0 with it in flight |
| F2 | yes | gate_plan PASS 28/31/31; 298 PASS with all three plans in flight |
| F3 | yes | `git rev-list --count main..6ba57dc` = 91, as the umbrella :3 now states with its sha |
| F4 | yes | heading "(DEC-0132..DEC-0157)"; the body cites nothing above 0157 |
| F5 | yes | umbrella :17 quotes plan :334 verbatim; :8 quotes S3 :35 verbatim |
| F6 | yes | umbrella :8: drops S3 :38 gate_checkpoint.py, hand-written handoffs at :1362/:2464 |
| F7 | yes | DEC-0151 defer-both: umbrella :8 runs Tasks 1-6 only; deferred row :40 |
| F8 | yes | playbook :26-:33. Re-run over the whole tree (`git ls-tree` top level + docs/): every path the plans touch has a row. Paths no row names (.clang-*, .editorconfig, .gitattributes, .gitmodules, .idf-version, LICENSE) fall under the DEC-0157 "raise at sync" rule. No path is in both rows; the registers and the board are shared under the "Shared files" rules |
| F9 | yes | DEC-0146: umbrella :9, playbook :35, docs plan header and Global Constraints |
| F10 | yes | 13 specs: 11 product + lap-timer in docs/product/specs (counted 11 patterns cover 11 files), P10 spec in docs/p10, strict-delivery spec in docs/process; Task 1 Step 1 sweep extended |
| F11 | yes, with R-3/R-4 | Read-tool receipts. Contradiction probe: a 2674-character line (spec :48) Read with offset 48 limit 1 came back whole, so no line truncation was seen |
| F12 | yes | `ref` field; copies under `.superpowers/context/` (`.gitignore:18` ignores `.superpowers/`) |
| F13 | yes, with R-3 | own-write refresh stated (playbook :49) |
| F14 | partly | sha = commit, tracked paths only, bare `path:line` FAILs. But the tracked umbrella and board still cite git-ignored files (R-2) |
| F15 | yes | script over findings.yml at 314b46d: every open or planned row is named or in a named range in the umbrella except PROG-1 itself (SD-S2-T8/T9 are in "SD-S2-T7..T9") |
| F16 | yes | catch-all split by kind with a reason per kind (:32-:35) |
| F17 | yes | DEC-0145 at umbrella :11-:14 and both plans' Global Constraints |
| F18 | yes | DEC-0147 at prototype :31, :39-:40, :51; umbrella :43. My original citation board.c:47 was wrong: the define is at :48 (`sed -n 48p`), as the fixer said |
| F19 | yes | DEC-0148/0153: umbrella :6, playbook :21, board :7, tooling header and Step 5 |
| F20 | yes | Task 2 Step 6 names the S1 copies, check-skill.sh and the re-ledger; `wc -c` controller.md = 5281 |
| F21 | yes | DEC-0152 D37-D39, D36 reserved (umbrella :7) |
| F22 | yes | all drafts listed with targets in the fix report |

### DEC-0145..0157 (check 2)
Each chosen option's cost text was compared with the drafts, and all are applied:
- 0145: umbrella :12-:14; plans' Global Constraints.
- 0146: umbrella :6 and :9; tooling Task 1 Step 5.
- 0147: prototype :31, :39-:40.
- 0148/0153: as F19.
- 0149: umbrella :8.
- 0150: umbrella :34.
- 0151: umbrella :8 and :40.
- 0152: umbrella :7.
- 0154: playbook :21; prototype :16 and b1 Step 3; prototype handoff.
- 0155: prototype handoff `grant: DEC-0155`; playbook :21.
- 0156: `grep -n "P[0-5]\b\|prototype-p"` over the drafts gives no hits; blocks b1..b6.
- 0157: playbook :26 and :29.

No draft sentence contradicts any of them.

### handoffs (check 4)
- Both front matters carry every key of the umbrella handoff format (plan :268-:293). `findings_in_flight` is written as a list of one-key maps, which the YAML of the format also parses to.
- Strict-delivery handoff:
  - `resume_line: 191` = `wc -l` of the ledger at 314b46d.
  - 88 rows = every row not closed at 314b46d (53 planned + 28 pinned + 7 open).
  - decisions.yml :1028 is DEC-0106 and :1304 is DEC-0122, so "1028-1303" holds the 16 open D33 rows.
  - The ledger's line 1 names the S2 plan, matching `plan:`.
- Prototype handoff: `resume_line: 6` = the 6 lines of the draft ledger.
- Resume prompts: each names the read order, the gate run, the branch to create and its base, the block and the stop point. That is enough for a fresh session; the tooling plan's block start covers the register-row step.

### findings raised (proposed rows)
- **R-1 MEDIUM (check 4/5: PROG-1 cannot be pinned with this transcript).** mutations/PROG-1.txt line 2 reads `test: the PROG-1 repro (grep -c ...)`. A pin needs a test name: tools/gates/lib/registers.py:234-235, "pin test must be test_<name> or test_<name>::test_<name>". CLAUDE.md says "Pinned = production-path test + mutation transcript". As written, PROG-1 can only go planned -> open -> closed, never pinned. Replacement: a test such as `test_hooks.ProjectWiring.test_umbrella_carries_the_replan_and_the_playbook`, which asserts the `## Replan 2026-10-29` heading and the playbook file through the repository fixture, with the transcript re-cut on it. Alternatively, the owner rules PROG-1 closed with evidence (owner-only, see Q-rr-1).
- **R-2 MEDIUM (check 1/3, F14 dimension over the whole set: tracked text relies on git-ignored files).** The playbook says a git-ignored fact "does not count until it is written into a tracked document" (:3, DEC-0131) and "A tracked document cites only tracked files" (:63). Yet the tracked umbrella section cites git-ignored sources as the only sources of deferred items. `grep -o` counts:
  - `.superpowers/sdd/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints/` b5r14-draft-*: 1;
  - b5r15-budget/: 1; b5r15-d30-investigation.md: 1; b5r15-subcontroller-report.md: 2;
  - b5r17-docs-draft.md: 2; b5r17-subcontroller-report.md: 4.

  Board row A also cites `b5r17-subcontroller-report.md`. A fresh clone or another machine cannot resume the deferred list from these sources. Replacement: before the umbrella section lands, import the cited workspace files (or the cited sections) into a tracked folder, e.g. `docs/superpowers/state/strict-delivery/replan-sources/`, and cite those paths. Or, if they stay git-ignored, the playbook's rule needs an owner exception (Q-rr-2).
- **R-3 MEDIUM (check 3: the own-write refresh cannot be implemented as specified).** The refresh applies only "when the receipt's sha equalled the file's sha before the edit" (playbook :49, tooling Task 2 Steps 1-2). A PostToolUse hook runs after the edit and sees only the new file. Task 2 Step 3, the PreToolUse extension, records no pre-edit sha, so a file changed by another writer just before the session's own Edit would be refreshed as if it were the session's. Replacement: Task 2 Step 3 adds "on Edit/Write/NotebookEdit of a read-list file, PreToolUse stores the file's current blob sha with the tool-use id; PostToolUse refreshes only when that stored sha equals the receipt's", plus a test where another writer changes the file between the Read and the session's Edit.
- **R-4 MEDIUM (check 3: fail-open when no expected list exists).** The hook "refuses until the receipt covers every expected entry" (playbook :48), and the loader writes the expected list (:46). No sentence or test says what happens when the session never ran the loader, so no expected list exists. Read literally, an empty expectation is covered and nothing is refused. Replacement: Task 2 Step 1 adds a test "no expected list for this session -> Edit, Write, NotebookEdit, Agent and Bash commit refused, the refusal names the loader command", and Review Focus 1 names the case.
- **R-5 LOW (check 3: range size is fixed by lines only).** The playbook :45, tooling Global Constraints :20 and the strict-delivery handoff trap :22 cap an entry at 2000 lines, while Task 1 Step 1 measures "the byte cap per entry". The first 2000 lines of decisions.yml are 134,307 bytes and of the S2 plan 114,712 bytes (`head -2000 | wc -c`). Replacement: "at most 2000 lines and at most the byte cap b1 Task 1 measured", carried into the playbook, the constraint and the trap once measured.
- **R-6 LOW (check 4: Task 2's Files list is incomplete).** Making `## Context: <kind>` mandatory in `gate_dispatch` (Task 2 Step 4) changes the dispatch grammar text in the umbrella "Dispatch prompt grammar" (plan :237-:253) and the skill's dispatch instructions, and every dispatch prompt fixture in test_gate_dispatch.py and test_hooks.py. None is in Task 2's Files. Replacement: add them.
- **R-7 LOW (check 4: the docs-reorganisation block is incomplete).** It has no branch name, no block-start ledger, no `CHECKPOINT b1` ledger line (compare tooling :32, :101) and no named grant ("the owner's merge grant for this PR", :105 of the plan). Task 2's move must also rewrite the ledger's line 1 `plan:` path in the same commit, or gate_plan reads a missing plan. Replacement: add the branch `block/docs-reorganisation`, the ledger steps and the grant question to the next sync (Q-rr-3).
- **R-8 LOW (wording).** Prototype blocks table :60 says "P10 cleanup of touched files after b7/b8" while the plan's own blocks are b1..b6 (DEC-0156). Replacement: "after strict-delivery b7/b8", as scope :48 does.

### Decision needed (owner)
- Q-rr-1, PROG-1 pin form: (a) a production-path test plus a re-cut transcript, which costs one small test and a review; (b) closed with evidence, with no pin, so a later revert of the replan text is caught by no test.
- Q-rr-2, workspace sources: (a) import the cited workspace files into tracked state before the replan lands, which costs a large text commit (several hundred KB) and one review; (b) an exception to playbook :3 and :63 for the deferred list, which lets resuming depend on one machine's git-ignored folder.
- Q-rr-3, merge grant for the docs-reorganisation PR: (a) grant:strict-delivery (DEC-0001) as for the tooling block, at no cost now; (b) a grant asked when the move is due, which costs one more question then.

### unrelated findings
none new. The fix report's stray `docs/superpowers/state/yy` was not re-checked (out of scope).

### denials
none.

### background jobs
none.

### budget used
About 95k of 200k tokens and about 40 of 50 minutes.
