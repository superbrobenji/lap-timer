status: DONE_WITH_FINDINGS

Fix round 2 of the 2026-10-29 replan drafts (row PROG-1): re-review R-1..R-8 (program-docs-review.md "## Re-review") and the owner answers DEC-0158..DEC-0160. Brief: .superpowers/sdd/program/program-docs-fix2-brief.md. I edited workspace text only, all of it under `.superpowers/sdd/program/`. The checkout is untouched: `git status --porcelain --untracked-files=all` is empty and `git ls-files -v | grep '^[a-zS]'` is empty. Probes ran only in a scratch clone `<scratchpad>/fix2/clone` under the DEC-0032 guard. The guard script runs under `set -euo pipefail`, refuses a clone path outside the scratchpad, and asserts that `pwd -P` equals `git rev-parse --show-toplevel` and matches `/private/tmp/claude-501/*/scratchpad/fix2/clone`. It uses no git config overrides. The clone has been deleted.

HEAD moved during the round, from 314b46d (the brief's base) to 61f34ea ("chore(state): DEC-0158..0160 owner answers"). `git diff --stat 314b46d 61f34ea` shows only decisions.yml (+27 lines). I moved the handoffs and the transcript to 61f34ea (see item 6).

## commits
none (the dispatch said no commit in the checkout).

## files written
- Drafts edited: draft/umbrella-replan-section.md, draft/program-tooling-plan.md, draft/playbook-two-streams.md, draft/prototype-field-ready-plan.md, draft/docs-reorganisation-plan.md, draft/board.md, draft/strict-delivery-handoff.md, draft/prototype-handoff.md, draft/mutations/PROG-1.txt. The targets are the ones the fix1 report gives.
- New: draft/sources/ (32 imported files plus MANIFEST.md), target `docs/superpowers/state/program/sources/`. This report is the 33rd import, `sources/program-docs-fix2-report.md`, copied after this text was final.
- Not touched: draft/decs-0132-0140.yml. It has no placement target, because its rows are in tracked decisions.yml at HEAD. Its line 30 cites `.superpowers/sdd/program/wiring-extract.md`, and so does tracked decisions.yml:1428 (DEC-0135 question). That is committed text, outside this round's workspace-only scope (see Unrelated findings).

## items
1. **R-1 / DEC-0158: applied.**
   - mutations/PROG-1.txt:2 now reads "evidence, not a pin: PROG-1 closes with evidence on DEC-0158 (registers.py pins only a test_<name>; this file claims no pin and names no test)". It names the closure evidence (the tracked re-review report `docs/superpowers/state/program/sources/program-docs-review.md` and the landing commit) and calls the steps "the row's repro command ..., not a test".
   - strict-delivery-handoff.md:40 adds the closure route. Once the landing commit exists, the next register edit moves PROG-1 planned -> open -> closed (the transitions registers.py:13-16 allows), with that report path and the landing commit's sha as evidence, "since a commit cannot name its own sha".
   - Sibling sweep: `grep -rn "PROG-1\|pinned\|pin "` over the drafts. No other draft text says PROG-1 is or will be pinned. findings_in_flight keeps `{PROG-1: planned}`, its state at HEAD.
2. **R-2 / DEC-0159: applied.**
   - Imported, base names kept:
     - b5r14-draft-{precommit,helper,skeleton}.md (the brace pattern expanded).
     - Their re-reviews: b5r14-tablerev-{precommit,helper,skeleton}.md, b5r15-rr-{precommit,helper,skeleton}.md and b5r17-rr2-{precommit,helper,skeleton}.md. The umbrella cited these only as "their re-reviews", so I import them and name them by path; see Q-fix2-2.
     - b5r17-docs-draft.md, b5r17-subcontroller-report.md, b5r15-subcontroller-report.md and b5r15-d30-investigation.md.
     - b5r15-budget/ as a whole tree (10 files, r0/ included).
     - check-skill.sh (from `.superpowers/sdd/strict-delivery/`).
     - program-docs-review.md, program-docs-fix1-report.md, wiring-extract.md, tracks-research.md, and this report.
   - Citations repointed to `docs/superpowers/state/program/sources/`:
     - umbrella :24-:32 and :40;
     - board :7;
     - tooling Task 2 Step 6 (:87, `bash docs/superpowers/state/program/sources/check-skill.sh`);
     - prototype :43 (tracks research) and :98 (wiring extract; the "session only" wording is replaced by a tracked citation rule).
   - umbrella :20 adds a pointer to the folder and its MANIFEST (DEC-0159).
   - draft/sources/MANIFEST.md has one row per file: target path, original workspace path, sha256 as imported, bytes, and the original sha256 for each redacted file.
   - DEC-0143 scan (owner email from `git config user.email`, the account email, the home path prefix, user name, handle, any email):
     - The owner's email: 0 hits.
     - Home path prefix (the macOS Users directory plus the owner's name): 12 hits on 12 lines in 6 files, each replaced by `~` and listed in the manifest by file, line and kind. Afterwards a fixed-string grep for that prefix over sources gives 0.
     - Found and not replaced, listed in the manifest: 11 encoded user-name path segments (`-Users-<name>-` in scratchpad and `~/.claude/projects/` directory names), 1 GitHub handle (b5r15-budget/dec-0122.yml:9, which is already in tracked decisions.yml as `answered_at.by`), and 1 third-party business address (tracks-research.md:165, WPMC). See Q-fix2-1.
   - Remaining `.superpowers/` in the placed drafts: four location mentions, quoted under item 6.
3. **R-3 to R-6: applied as acceptance criteria in draft/program-tooling-plan.md.**
   - R-3:
     - Architecture :7: PreToolUse stores the pre-edit blob sha keyed by the tool-use id; PostToolUse refreshes only when it equals the receipt's.
     - Review Focus 1 (:26).
     - Task 1 Step 1 (:51): the spike records whether both hook events carry the same tool-use id.
     - Task 2 Step 1 (:82): tests for the other-writer case, the case of no stored sha, and the refresh on equality.
     - Task 2 Step 2 (:83): the refresh only on equality, then the stored sha is dropped.
     - Task 2 Step 3 (:84): the sha is stored on Edit, Write or NotebookEdit of a read-list file.
     - Playbook :49 now states the same mechanism.
   - R-4:
     - Architecture :7.
     - Review Focus 1 (:26): "A session with no expected list (the loader never ran) is refused, never treated as covered; the refusal names the loader command".
     - Task 2 Step 1 test (:82) and Step 3 fail-closed sentence (:84).
     - Playbook :48.
   - R-5:
     - "at most 2000 lines and at most the byte cap program-tooling Task 1 measured" in Architecture :7, Global Constraints :20, Review Focus 2 (:27), Task 1 Steps 2-3 (:52-:53), playbook :45 and the handoff trap :22.
     - Task 1 Step 1 (:51) writes the measured cap into those three places.
     - Task 1 Files (:40-:42) adds the plan, the playbook and the handoff.
   - R-6, Task 2 Files (:68-:72) adds:
     - the umbrella plan section "Dispatch prompt grammar" (:237-:253 at 61f34ea);
     - SKILL.md's dispatch paragraph (SKILL.md :41-:43, staged per D34);
     - controller.md's "Dispatch discipline" heading list;
     - fixtures/dispatch/good_implementer.md and good_reviewer.md.

     test_gate_dispatch.py and test_hooks.py were already listed. Step 4 (:85) makes all of them change in the same commit and records the controller byte count. The Step 1 test covers the fixtures.
   - Correction to a re-review number: R-5 says "the first 2000 lines of decisions.yml are 134,307 bytes". At 314b46d decisions.yml has only 1,631 lines (`git show 314b46d:... | wc -l`), so 134,307 bytes is the whole file. The drafts quote the S2 figure (114,712 bytes for its first 2000 lines) and state decisions.yml as "1,631 lines, is 134,307 bytes".
4. **R-7 / DEC-0160: applied (draft/docs-reorganisation-plan.md).**
   - Global Constraints :18: branch `block/docs-reorganisation` from `main`; the merge is under grant:strict-delivery (DEC-0001, DEC-0160).
   - Block heading :44 names the branch.
   - The block start (:46) creates `docs/superpowers/state/docs-reorganisation/ledger.md` with line 1 `plan: docs/superpowers/plans/2026-10-29-docs-reorganisation.md`, registers the task rows and pushes.
   - Task 2 Files :77-:80 and Step 2: every moved ledger's line 1 `plan:` names the new plan path in the same commit. That covers this plan's ledger and its siblings strict-delivery, program-tooling and prototype, whose plans also move under the layout. Step 3 adds one `test -f` per ledger.
   - CHECKPOINT :111: `gh pr ready`, all checks green, merge under DEC-0001/DEC-0160, `CHECKPOINT b1 <sha>` appended to the ledger, then the board and the handoff.
   - The umbrella step 4 (:9) names the branch and the grant. The umbrella heading is now "(DEC-0132..DEC-0160)", and the umbrella cites DEC-0159 (:20) and DEC-0160 (:9).
5. **R-8: applied.** prototype :60 now reads "P10 cleanup of touched files after strict-delivery b7/b8".
6. **Check 6: applied.** Clone at HEAD 61f34ea. Every draft was placed at its target and staged: the umbrella section inserted before "## Checkpoint protocol for this plan", the 32 sources plus MANIFEST, and the review packages copied in. The F1-F22 closures still hold under these gates.

## gates run (scratch clone at 61f34ea, all drafts and sources placed)
| command | result |
|---|---|
| `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) python3 tools/gates/run_all.py`, all three plans in flight (temporary ledgers for program-tooling and docs-reorganisation) | rc 0, 298 PASS, 0 other (90 PASS lines under plan.prototype / plan.program-tooling / plan.docs-reorganisation) |
| same, prototype plan only in flight (its draft ledger) | rc 0, 239 PASS, 0 other |
| `gate_plan.py --plan` program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS, 0 other |
| `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` (same prefix, three in flight) | Ran 658 in 134.3 s, OK (skipped=1) |
| same with the main checkout's `tools/.venv/bin/python3` | Ran 658 in 146.8 s, OK (skipped=1) |
| first run, before a fix in my own edit | rc 1: `FAIL plan.docs-reorganisation.files.2` and `FAIL plan.program-tooling.files.2`, "a second backticked span". Two Files items I added had backticks inside their parentheses. Fixed (no backticks in the parentheticals); the runs above are after the fix |
| PROG-1 transcript replayed | step 1 PASS; step 2 (umbrella from 61f34ea, playbook removed) FAIL; step 3 (`git checkout --` from the index) PASS |
| the same three gate rows, rerun after this report was copied into sources and MANIFEST regenerated | identical: run_all 298 / 239 PASS, gate_plan 28 / 31 / 31, both suites OK (658, skipped=1) |

`grep -rn '\.superpowers/'` over the placed drafts finds:
- in the umbrella "Replan" section (lines 351-395 of the placed umbrella): 0;
- in the other placed draft targets, four hits, each a location and not a citation:
  - playbook :46 "to a git-ignored copy under `.superpowers/context/`";
  - tooling :52 "the loader writes `git show <ref>:<path>` to `.superpowers/context/`";
  - tooling :54 "writes the session's expected list (git-ignored, under `.superpowers/context/`)";
  - docs-reorganisation :42 "plus the files under the git-ignored `.superpowers/reviews/` that the audits cite".

On top of those, there are workspace paths inside the imported files:
- MANIFEST.md: 33 lines, its header plus the "original workspace path" column the brief asks for;
- the verbatim sources: 85 lines in 19 files.

These sources were copied verbatim apart from the DEC-0143 redactions, so their internal workspace paths are part of the record, and MANIFEST says so. See Q-fix2-2 on whether they need importing as well.

## claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| HEAD moved to 61f34ea, only decisions.yml changed | `git log --oneline -3`; `git diff --stat 314b46d HEAD` | an unchanged HEAD would make the guard's `test` of HEAD equal 314b46d | 61f34ea; 1 file, +27 | PASS |
| 88 not-closed rows at 61f34ea equal the handoff's findings_in_flight | venv python over `git show 61f34ea:findings.yml` vs the handoff line | dict equality would be False on any extra or missing row or state | `88 88 True` | PASS |
| ledger is 191 lines at 61f34ea (resume_line) | `git show 61f34ea:.../ledger.md \| wc -l` | | 191 | PASS |
| S2 plan's first 2000 lines are 114,712 bytes; decisions.yml is 1,631 lines, 134,307 bytes, at 314b46d | `git show 314b46d:<f> \| head -2000 \| wc -c`; `wc -l` | the review's "first 2000 lines of decisions.yml" would need more than 2000 lines | 114712; 134307; 1631 | PASS |
| grammar section at umbrella :237-:253 | `sed -n '237p;253p'` | | heading at :237; the "`gate_dispatch` rules" line at :253 | PASS |
| SKILL.md dispatch paragraph :41-:43; controller.md :7 lists the headings | `sed -n 36,50p`; `grep -n Dispatch` | | "per the dispatch grammar in the umbrella plan"; "Dispatch discipline ... ## Role, ## Brief ..." | PASS |
| dispatch fixtures exist | `ls tools/gates/tests/fixtures/dispatch` | | good_implementer.md, good_reviewer.md, prompts | PASS |
| pin needs test_<name>; closed needs evidence; planned->open->closed is legal | registers.py :13-16, :233-235, :262-263 | planned->closed is not in the table | as stated | PASS |
| no owner email in the imports; 12 home-path hits replaced | `grep -rnoiE` for emails and the home prefix before; `grep -rnF` for the home prefix after | | 0 owner-email hits; 12 before, 0 after | PASS |
| MANIFEST sha256 and bytes match the files | generated by hashlib over each file | the rerun regenerates the manifest after the report import, and the gates pass on it | 33 rows after the report import | PASS |
| gate_plan reads the new Files lines | run before the fix | | rc 1 with the named FAIL, then rc 0 | PASS |

## findings raised
none new against the drafts. The defect my own edit introduced (two Files items with a second backticked span) was found by gate_plan and fixed before the final runs.

## Decision needed (for the controller to batch for the owner)
- **Q-fix2-1, identifiers that DEC-0143 and the brief leave unruled.**
  - The facts: 11 encoded user-name path segments (`-Users-<name>-...` in scratchpad and `~/.claude/projects/` directory names), 1 GitHub handle, and 1 third-party business email in the imported sources. The manifest lists each by file and line.
  - Options:
    - (a) leave them. Cost: none now. The handle is already tracked in decisions.yml (144 hits of the handle), and the user name is visible in that handle.
    - (b) replace the encoded segment with `-Users-~-` and the handle with `[owner handle]`, keeping the WPMC address. Cost: one small edit round, and the scratchpad paths no longer resolve.
    - (c) replace each scratchpad path with `<scratchpad>`. Cost: as (b), and the provenance of which session ran a probe is lost.
- **Q-fix2-2, how far the import reaches.**
  - The facts: the umbrella cited "their re-reviews" of the three b5r14 drafts with no path. I imported the nine re-review files (b5r14-tablerev-*, b5r15-rr-*, b5r17-rr2-*, 165 KB of about 830 KB). The imported files also cite further workspace files: 85 lines in 19 files.
  - Options:
    - (a) keep the nine and import nothing transitively. Cost: a deferred item that needs a further-cited workspace file imports it when the item resumes.
    - (b) drop the nine and replace "their re-reviews" with the residual rows in b5r17-subcontroller-report.md. Cost: one text round, and the re-review detail stays only in the workspace.
    - (c) import the transitive closure. Cost: likely several MB and one more review.

## Unrelated findings
- Tracked `docs/superpowers/state/decisions.yml` cites the git-ignored workspace:
  - :1428, DEC-0135 question, `.superpowers/sdd/program/wiring-extract.md`;
  - :1482, DEC-0141, `tracks-research.md`;
  - :1518, `program-docs-review.md`;
  - :538, the b5 root-cause report.

  Reproducer: `grep -n '\.superpowers/' docs/superpowers/state/decisions.yml`. Class: register text citing git-ignored files, the same dimension as R-2 but in committed register rows. The first three now have tracked copies under sources/; :538 does not.
- Tracked findings.yml PROG-1 class text (:3026) names `.superpowers/sdd/program/draft/`. That is a location, true until the drafts land.

## denials
none.

## background jobs
none.

## budget used
About 120k of 300k tokens and about 45 of 75 minutes.

## Round 2b

status: DONE. Owner answers DEC-0161..DEC-0163 (Q-fix2-1..3) are applied to draft/sources/ and MANIFEST.md. They are committed in HEAD 667e0d7, which differs from 61f34ea only in decisions.yml (+28 lines). Workspace text only: no checkout edit and no commit. `git status --short` was clean after the owner commit.

### applied
- **DEC-0161 (redact-name-and-third-party):**
  - The owner user name in encoded paths, `-Users-<name>-`, is now `-Users-~-`: 11 hits on 11 lines in 11 files.
  - The WPMC address at tracks-research.md:165 is now "WPMC contact (their website)".
  - The GitHub handle is kept (b5r15-budget/dec-0122.yml:9).
  - MANIFEST "Redactions (DEC-0143, DEC-0161)" lists all 24 replacements by file, line and kind: 12 home-path, 11 user-name and 1 email. "Identifiers kept (DEC-0161)" lists the handle.
  - A rescan of the sources for emails, the home prefix, the user name and encoded segments finds only the kept handle. The manifest generator asserts this.
- **DEC-0162 (keep-no-transitive):** the nine re-review files stay. MANIFEST ends with "Imports are a snapshot: workspace files that the imported files themselves cite are not imported (DEC-0162)."
- **DEC-0163 (import-now):** b5-rca-report.md (26,037 bytes) is imported. The DEC-0143/DEC-0161 scan finds no identifier in it, so it is a verbatim copy. The MANIFEST section "Tracked citations mapped to imports (DEC-0163)" maps `docs/superpowers/state/decisions.yml:538` to `docs/superpowers/state/program/sources/b5-rca-report.md`. decisions.yml is unchanged by me.
- **MANIFEST rows refreshed after the edits:** 34 files, this report's refreshed copy included.
  - A check script re-hashes every row. sha256 and bytes match each file.
  - Each verbatim row's file is byte-equal to its workspace original.
  - Each redacted row's original sha256 matches the workspace file, and that file differs from the import.
- **The handoffs and the PROG-1 transcript moved from 61f34ea to 667e0d7,** as did the tooling plan's grammar-line anchor. The ledger is still 191 lines and findings.yml is unchanged, so findings_in_flight (88 rows) holds. The strict-delivery handoff range is now DEC-0132..DEC-0163.

### gates (check 6 in a scratch clone under the DEC-0032 guard, all drafts and sources placed)
| command | result |
|---|---|
| run_all, all three plans in flight | rc 0, 298 PASS, 0 other |
| run_all, prototype plan only | rc 0, 239 PASS, 0 other |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS |
| suite /usr/bin/python3 -W error | Ran 658, OK (skipped=1) |
| suite tools/.venv/bin/python3 -W error | Ran 658, OK (skipped=1) |
| `.superpowers/` in the placed drafts | the same 4 location mentions (playbook :46, tooling :52 and :54, docs-reorganisation :42) |

Run order:
1. The first run used a clone at 61f34ea with the working-tree decisions.yml placed. Its blob, 6996b8c, equals `HEAD:docs/superpowers/state/decisions.yml` at 667e0d7.
2. The final run used a clone at 667e0d7, after this section and its refreshed copy were in place. It gave the same results.

### denials, background jobs
none.

### budget (round 2b)
About 25k of 100k tokens and about 20 of 30 minutes.

## Round 3

status: DONE. This round applies final re-review RR2-1..RR2-5 (program-docs-rereview2.md). HEAD is 667e0d7. Workspace text only: no checkout edit and no commit. I did not touch the reviewer's unrelated item (decisions.yml :1428/:1482/:1518).

### applied
- **RR2-1, one shape: the landing commit closes PROG-1.**
  - draft/prog-1-closed-row.yml holds the exact row: `state: closed`, `pin: null`, `ruling: DEC-0158`, `evidence: [docs/superpowers/state/program/sources/program-docs-review.md, docs/superpowers/state/program/sources/program-docs-rereview2.md]` (no commit sha), and history open, planned (ec1c3c5), open, closed (667e0d7, by session).
  - Handoff :11 drops `{PROG-1: planned}` (87 rows), and :40 says the 87 are "the 88 rows not closed at 667e0d7 less PROG-1".
  - Handoff :40 also states the closed shape and that the landing commit writes the row into findings.yml.
  - PROG-1.txt :2 says the same. No other draft mentions PROG-1 (`grep -rn PROG-1` over the drafts, sources excluded).
  - Proof, in the clone with that row in place of findings.yml :3023-end:
    - `gate_register.py`: rc 0, PASS for schema, owner, duplicate, transition, evidence and report;
    - run_all: rc 0, 239 PASS.
  - Contradiction: with program-docs-rereview2.md unstaged, gate_register gives `FAIL register.evidence.PROG-1: evidence: 'docs/superpowers/state/program/sources/program-docs-rereview2.md' is neither a commit sha nor a file tracked at the repo root`. Restaged, rc 0.
- **RR2-2:**
  - program-docs-rereview2.md is imported. The DEC-0143/DEC-0161 scan found one `-Users-<name>-` segment (:54), now `-Users-~-`, and the handle at :54, kept. Both are listed in MANIFEST.
  - The manifest has 35 rows, this report's refreshed copy included.
  - MANIFEST ends with the note "Pending import: the re-review of fix round 3 does not exist yet; once it exists the controller imports it here ...".
- **RR2-3, board: converted.** Row A cites:
  - `docs/superpowers/state/decisions.yml:1028@667e0d7 "- id: DEC-0106"` and `:1303@667e0d7 "answered_at: null"` for the D33 range;
  - `:1500@667e0d7 "a reviewer turned commit signing off with a git config override"` for the incident.

  Board :3 now says what the text does: citations of files tracked at 667e0d7 use the form. The imported sources are named by path and section, because the landing commit cannot cite its own sha. Program tooling Task 3 Step 4 (:108) rewrites them into the form. Plan and handoff paths are names of files to read, not citations.
- **RR2-4, prototype plan: converted.**
  - :30 cites `components/drivers/gps_neo6m/gps_neo6m.c:4@667e0d7 "This stub exists so moto_neo6m"`.
  - :31 cites `components/drivers/board_devkit_v1/board.c:48@667e0d7 "drive LOW = GPS on"`.
  - The :20 rule is narrowed to exactly what holds: "Every file fact a stream B task writes into a tracked document cites a tracked file as `path:line@sha "quote"` ...; this plan's own line citations have that form."
  - `grep` finds no bare backticked `path:line` left in the board or the plan.
  - All five quotes are found on their line at 667e0d7. Contradiction: board :1500 off by one line (1501), or board.c :47, gives no match.
- **RR2-5:** playbook row A adds `docs-reorganisation/` (the docs reorganisation block's ledger). It also adds `~/.claude/skills/strict-delivery/` and the memory notes under `~/.claude/projects/`, both of which the tooling and docs plans edit.
  - A script mapped every Files path of the three plans, plus the written paths named in their steps, to a row.
  - Every path maps to A or B except three:
    - the registers, governed by "Shared files";
    - `.github/workflows/` as a directory (gates.yml and ci.yml are A, firmware.yml is B);
    - `docs/` as a whole (the move).
  - A new sentence under the table (playbook :29) covers the docs move editing stream B files (`docs/`, firmware.yml, the tools helpers): it runs only when stream B has no branch open, with every document moved at once (DEC-0146, DEC-0139).

### gates (scratch clone at 667e0d7, DEC-0032 guard, all drafts and sources placed, PROG-1 closed row in place)
Final run, after this section and its copy were in place:

| command | result |
|---|---|
| run_all, prototype plan only (landing shape) | rc 0, 239 PASS, 0 other |
| gate_register | rc 0, 6 PASS |
| run_all, all three plans in flight | rc 0, 298 PASS, 0 other |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS |
| suite /usr/bin/python3 -W error | Ran 658, OK (skipped=1) |
| suite tools/.venv/bin/python3 -W error | Ran 658, OK (skipped=1) |
| PROG-1 transcript | PASS / FAIL / PASS |
| `.superpowers/` in the placed drafts | the same 4 location mentions |

### denials, background jobs
none.

### budget (round 3)
About 30k of 100k tokens and about 25 of 30 minutes.

## Round 4

status: DONE. This round applies "## Re-review (round 3)" (RR3-1..RR3-4) and owner answers DEC-0164 (shared) and DEC-0165 (fix-now). Workspace text only: no checkout edit and no commit. HEAD is 667e0d7; DEC-0164/0165 are in the working-tree decisions.yml, which the clone uses.

### applied
- **DEC-0164:** the memory notes leave playbook row A. "Shared files" gains: "Memory notes under `~/.claude/projects/`: shared like the registers: each stream adds its own notes and never edits the other's; the docs move rewrites them once, with no stream B branch open (DEC-0164)." Row A keeps the strict-delivery skill.
- **DEC-0165:**
  - MANIFEST's "Tracked citations mapped to imports" gains three rows:
    - `decisions.yml:1428` (DEC-0135) -> wiring-extract.md;
    - `:1482` (DEC-0141) -> tracks-research.md;
    - `:1518` (DEC-0145) -> program-docs-review.md.

    Each line is inside its DEC at 667e0d7 (`- id:` at :1426, :1480 and :1516). PROG-2's repro grep now gives 3.
  - draft/prog-2-row.yml is now a closed row:
    - `state: closed`, `pin: null`, `ruling: DEC-0165`;
    - `evidence: [docs/superpowers/state/program/sources/MANIFEST.md]`;
    - history open -> closed at 667e0d7. That transition is legal (registers.py:13-14).
    - The owner is `plan: strict-delivery`, `task: program-replan`, PROG-1's convention for the replan commit, because DEC-0165 lands it there, not in docs-reorganisation T2.
  - In the clone, with PROG-1 and PROG-2 placed, gate_register gives rc 0, 6 PASS. Contradiction: with MANIFEST.md unstaged, it gives `FAIL register.evidence.PROG-2: ... is neither a commit sha nor a file tracked at the repo root`.
- **RR3-1:** handoff :40 now cites `docs/superpowers/state/decisions.yml:1028@667e0d7 "- id: DEC-0106"` to `...:1303@667e0d7 "answered_at: null"`. No backticked `path` + `lines N-M` form and no bare `path:line` remain in either handoff or the board.
- **RR3-4:** after the landing commit, PROG-1 and PROG-2 are both closed, so the handoff list stays 87. :40 says so, PROG-2 included. In the clone with both rows placed: 89 rows, 87 not closed, equal to findings_in_flight (`87 87 True`).
- **Re-import:**
  - sources/program-docs-rereview2.md is re-imported as the file now stands, round-3 section included. The DEC-0143/DEC-0161 scan finds only generic patterns (`/Users/<x>`, `-Users-x-`, `-Users-<name>-`), no identifier, so the copy is verbatim and the old :54 redaction row is gone.
  - MANIFEST's "Pending import" sentence is replaced by: "If a round-4 section is appended to `.superpowers/sdd/program/program-docs-rereview2.md`, the controller re-imports that file at landing with the same DEC-0143/DEC-0161 scan and refreshes its row above."
  - MANIFEST has 35 rows (this report's refreshed copy included), all hashes rechecked.

### gates (scratch clone at 667e0d7, DEC-0032 guard, all drafts, sources and both rows placed)
Final run, after this section and its copy were in place:

| command | result |
|---|---|
| run_all, prototype plan only (landing shape) | rc 0, 239 PASS, 0 other |
| gate_register | rc 0, 6 PASS |
| run_all, all three plans in flight | rc 0, 298 PASS, 0 other |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31 PASS |
| suite /usr/bin/python3 -W error | Ran 658, OK (skipped=1) |
| suite tools/.venv/bin/python3 -W error | Ran 658, OK (skipped=1) |
| PROG-1 transcript | PASS / FAIL / PASS |
| `.superpowers/` in the placed drafts | the same 4 location mentions (the playbook one is now :47, because Shared files grew by one line) |

HEAD moved to b922d81 (DEC-0164..0165 committed) during the run; `git diff --stat 667e0d7 b922d81` shows only decisions.yml (+20 lines). The working-tree decisions.yml the clone used has blob 962c025, which equals `b922d81:docs/superpowers/state/decisions.yml`, so the placed tree equals b922d81 plus the drafts. The handoffs keep `head: "667e0d7"` and their @667e0d7 citations, which are still true at that commit; the landing session sets `head` to its own commit, as handoff :40 says. After this note was added, a rerun in a clone at b922d81 gave the same rows.

### denials, background jobs
none.

### budget (round 4)
About 25k of 100k tokens and about 20 of 30 minutes.
