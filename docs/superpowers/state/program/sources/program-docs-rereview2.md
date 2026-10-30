status: DONE

Final re-review of the 2026-10-29 replan drafts (row PROG-1) after fix rounds 2 and 2b. Read-only. Brief: .superpowers/sdd/program/program-docs-rereview2-brief.md. HEAD 667e0d7 on block/strict-delivery-b5; the checkout was clean before and after (`git status --porcelain --untracked-files=all` -> 0 lines). Probes ran only in a scratch clone `<scratchpad>/rr2/clone` under the DEC-0032 guard: `set -eu`, `pwd -P` = `git rev-parse --show-toplevel`, and a case match on `/private/tmp/claude-501/*/scratchpad/rr2/clone`. No git config overrides were used. The clone has been deleted.

Verdict:
- Spec ✅: R-1..R-8 are closed, F1-F22 still hold, DEC-0158..0163 are applied, and no draft sentence contradicts DEC-0130..0163 (evidence in checks 1-2).
- Quality ❌: RR2-1..RR2-5 below.
- Claims-executed ✅: the claims table below.

## commits
none (read-only review).

## gates run (scratch clone at 667e0d7, branch block/strict-delivery-b5)
Setup:
- Every draft is placed at the target the fix1 report gives: the umbrella section inserted before "## Checkpoint protocol for this plan" (umbrella :351), the 34 sources plus MANIFEST under docs/superpowers/state/program/sources/, the handoffs, the ledger and the transcript. All of it is staged.
- The git-ignored `.superpowers/sdd/strict-delivery/review/` is copied in.
- PROG-1 is set closed in findings.yml:
  - `state: closed`, `ruling: DEC-0158`;
  - `evidence: [docs/superpowers/state/program/sources/program-docs-review.md, 667e0d7]`;
  - history extended with `open@667e0d7` and `closed@667e0d7`.

| command | result |
|---|---|
| `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) python3 tools/gates/run_all.py`, PROG-1 unchanged (planned) | rc 0, 239 lines, 0 non-PASS |
| same, PROG-1 closed as above (landing shape: only the prototype plan in flight) | rc 0, 239 lines, 0 non-PASS |
| `python3 tools/gates/gate_register.py` (PROG-1 closed) | rc 0; PASS schema, owner, duplicate, transition, evidence, report |
| `gate_plan.py --plan docs/superpowers/plans/2026-10-29-program-tooling.md` | rc 0, 28 PASS, 0 other |
| `gate_plan.py --plan .../2026-10-29-prototype-field-ready.md` | rc 0, 31 PASS, 0 other |
| `gate_plan.py --plan .../2026-10-29-docs-reorganisation.md` | rc 0, 31 PASS, 0 other |
| run_all with all three plans in flight (temporary ledgers with line 1 `plan: ...` for program-tooling and docs-reorganisation) | rc 0, 298 lines, 0 non-PASS (28 program-tooling, 31 prototype, 31 docs-reorganisation, 69 strict-delivery) |
| `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` (Python 3.9.6, same state, three plans in flight) | Ran 658 tests in 136.9 s, OK (skipped=1) |
| `tools/.venv/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` (Python 3.12.14) | Ran 658 tests in 149.7 s, OK (skipped=1) |
| PROG-1 transcript replayed (mutations/PROG-1.txt steps 1-3) | step 1: `1`, PASS; step 2 (umbrella from 667e0d7, playbook removed): `0`, FAIL; step 3 (`git checkout --` from the index): `1`, PASS. Matches the transcript |

### What PROG-1's closed row needs to pass gate_register (check 5)
Each requirement below was checked directly (the row passed), and by contradiction in the clone.

| requirement | source | contradiction probe -> output |
|---|---|---|
| History ends `... planned, open, closed`. planned -> closed is illegal (FINDING_TRANSITIONS) | registers.py:13-16, :204-211 | open@667e0d7 removed -> `FAIL register.transition.PROG-1: transition: planned -> closed is illegal (from planned: ['in-progress', 'open', 'deferred'])` |
| Every history entry is exactly {state, sha, by}, and each sha is an existing commit. The landing commit cannot name itself, so the new entries carry an existing sha (667e0d7 or later) | registers.py:187-199, finding_commit_errors :440-446 | (form only) |
| `evidence` is a non-empty list | registers.py:262-263 | `evidence: []` -> `FAIL register.evidence.PROG-1: evidence: closed needs evidence (commits or report paths)` |
| Each evidence entry is an existing commit sha or a path git tracks (staged counts, via `git ls-files`) | registers.py:222-227 | an untracked report path -> `FAIL register.evidence.PROG-1: evidence: '.../program-docs-rereview2.md' is neither a commit sha nor a file tracked at the repo root` |
| An evidence list holding only the tracked report path also passes | | the sha removed -> all PASS |
| `ruling` is null or matches `DEC-NNNN`. It is not required for closed; DEC-0158 passes | registers.py:183-184 | |
| `pin: null`: no pin is needed for closed | | |

## claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| Every MANIFEST row's sha256 and bytes match its file. The rows equal the file set | python hashlib over the 34 rows; os.walk set compare | an extra or missing file would make the set compare False | rows 34, files 34, `rowset==files True`, 34 `OK` | PASS |
| Verbatim rows are byte-equal to their workspace originals; redacted rows' original sha256 match | per-row byte compare plus hashlib of the original | a verbatim row that differs would print `UNLISTED_DIFF`; a mismatched original would print `OSHA_MISMATCH` | 22 verbatim; 12 `differs osha_ok`; no UNLISTED_DIFF, no OSHA_MISMATCH | PASS |
| The redactions are exactly the 24 the table lists (23 lines; d30-investigation :5 carries two kinds) | `diff orig import \| grep '^[0-9]'` per redacted file | each changed line is re-derived by replacing the home prefix, the encoded segment or the email in the original; any other change would print OTHER | the changed lines equal the table's file:line set; every line `OK`; tracks-research :165 shows the WPMC email -> `WPMC contact (their website)` | PASS |
| No owner email, home path or encoded user-name path in the drafts; no third-party email | `grep -rnoE` for emails (all), `/Users/<x>`, `-Users-<x>-`, and the owner's name forms | the owner email or an encoded user-name segment would produce a hit | emails 0; `/Users/`/`-Users-<name>-` 0 (only `-Users-~-`); the name appears only in the owner's GitHub handle: sources/b5r15-budget/dec-0122.yml:9 (kept under DEC-0161) and draft/decs-0132-0140.yml (no target, not placed) | PASS |
| No tracked draft cites a `.superpowers/` file. Location mentions remain | `grep -rn '\.superpowers' *.md mutations prototype-state` in draft/ | | 4 hits, all locations: playbook :46 "to a git-ignored copy under `.superpowers/context/`"; tooling :52 "the loader writes `git show <ref>:<path>` to `.superpowers/context/`"; tooling :54 "(git-ignored, under `.superpowers/context/`)"; docs-reorg :42 "plus the files under the git-ignored `.superpowers/reviews/` that the audits cite". MANIFEST's origin column is required by the brief | PASS |
| Every `docs/superpowers/state/program/sources/...` path a draft cites exists (brace patterns expanded) | grep + `eval echo` + `test -e` | a missing import would print MISSING | 21 `ok`, 0 MISSING | PASS |
| Every other backticked doc path in the placed drafts exists, or is created by a plan task | `test -e` in the clone | | absent only docs/hardware/wiring.md (prototype T2 creates it), docs/INDEX.md and context.yml (tooling T1) | PASS |
| Handoff front-matter keys follow the umbrella format, in order | yaml keys vs umbrella :271-:292 | | 18 keys plan..traps, in order; prototype keys equal strict's | PASS |
| findings_in_flight = every row not closed/merged at HEAD (88) | python dict compare against findings.yml | an extra or missing row or state makes the compare False | `88 88 True` | PASS |
| Ledger has 191 lines at HEAD (resume_line) | `wc -l` | | 191 | PASS |
| Prototype ledger draft has 6 lines (resume_line 6) | `cat -n` | | 6 | PASS |
| decisions.yml 1028-1303 holds the 16 open D33 rows | `sed -n 1028p;1303p`; open_decisions | a 17th unanswered row or an id outside 0106..0121 would show | 16 unanswered = DEC-0106..0121; `- id: DEC-0106` at :1028, `answered_at: null` at :1303, DEC-0122 at :1304 | PASS |
| Umbrella "Dispatch prompt grammar" is :237-:253 at 667e0d7 | `sed -n 237p;253p` | | heading at :237; "`gate_dispatch` rules:" at :253 | PASS |
| Umbrella :334 quotes are verbatim | `sed -n 334p \| grep -o` | | both quotes found | PASS |
| S3 :35, :38, :47, :1362, :2464, :2483, :2948, :3084 say what the umbrella states (file is `...-S3-p10-reach.md`) | `sed -n` each | | as quoted (tags b1..b6; the gate_checkpoint.py ls; the owner decision; the handoff "umbrella front matter"; "Same steps as b7's block end"; Task 8 after Task 7; twelve-column HEADER; Step 4 done in Task 7 Step 6) | PASS |
| S1 :7339-7535 holds the eight strict-block copies | awk over the prompt headings | | controller :7339 ... audit-verifier :7513, closing fence :7535 | PASS |
| board.c:48 PIN_GPS_PWR GPIO26 "drive LOW = GPS on" | `sed -n 48p` | | `#define PIN_GPS_PWR GPIO_NUM_26 /* RTC GPIO; drive LOW = GPS on, ...` | PASS |
| controller.md is 5281 bytes | `wc -c` | | 5281 | PASS |
| 91 commits main..6ba57dc; S2 4,831 -> 8,695 lines; S2 first 2000 lines 114,712 bytes and decisions.yml 1,631 lines / 134,307 bytes at 314b46d | `git rev-list --count`; `wc -l`; `head -2000 \| wc -c` | | 91; 4831/8695; 114712; 1631 134307 | PASS |
| Every open or planned findings row is named, or falls in a named range, in the umbrella deferred list or run-now steps (F15 dimension) | regex id and range expansion vs findings.yml | | uncovered: SD-S2-T8/T9 (in "SD-S2-T7..T9", a form the regex missed, checked by eye) and PROG-1 itself | PASS |
| check-skill.sh does not depend on its location | `cat -n` | | it reads only `$HOME/.claude/skills/strict-delivery` | PASS |
| docs-reorg specs layout covers all 13 specs once | `ls docs/superpowers/specs` vs the patterns | | 11 product + 1 P10 + 1 strict-delivery | PASS |
| Audit 2 §5 counts are 6 cannot-run, 6 false-pass, 15 rewrite | audit :34-35 | | as stated | PASS |

### Check 1: R-1..R-8 closed, F1-F22 still closed (dimensions re-run over the whole set)
- R-1:
  - PROG-1.txt :2 says "evidence, not a pin" (DEC-0158), and handoff :40 gives the closure route.
  - registers.py allows that route: planned -> open -> closed passes in the clone.
  - The transcript replays.
- R-2: no tracked draft cites a workspace file, and every cited import exists (claims above).
- R-3: tooling :7, :26, :51, :82-:84 and playbook :49 all state the tool-use-id-keyed pre-edit sha, the equality refresh and the other-writer test.
- R-4: the no-expected-list case fails closed in tooling :7, :26, :82, :84 and playbook :48.
- R-5: the byte cap appears in tooling :7, :20, :27, :51-:53, playbook :45 and handoff trap :22. Task 1 Files :40-:42 lists all three targets.
- R-6: Task 2 Files :68-:72 lists the umbrella grammar, SKILL.md, controller.md and both fixtures.
- R-7: the docs-reorg plan has the branch (:18, :44), the block-start ledger (:46), the ledger rewrites (:77-:80, Step 2-3), the grant (:18, :111) and CHECKPOINT b1 (:111).
- R-8: prototype :60 reads "after strict-delivery b7/b8".
- F1-F22:
  - F1 (ledger line 1) and F2 (gate_plan 28/31/31, 298 PASS) hold.
  - F3 holds (91).
  - F4: the heading is (DEC-0132..DEC-0160) and the umbrella body cites nothing above 0160.
  - F5 and F6 hold (quotes and S3 lines verified).
  - F7 holds (DEC-0151 at :8, :42).
  - F8: one new gap, RR2-5.
  - F9 holds (DEC-0146).
  - F10 holds (13 specs).
  - F11-F13 hold, with R-3/R-4.
  - F14: the playbook rule holds, but the board and the prototype plan state a citation form their own text does not meet (RR2-3, RR2-4).
  - F15 holds (script above).
  - F16 holds (per-kind rows; the classes were re-read).
  - F17-F21 hold.
  - F22: targets as listed.

### Check 2: DEC-0158..0163 applied; no contradiction with DEC-0130..0163
- DEC-0158 close-with-evidence:
  - PROG-1.txt :2 and handoff :40 name the tracked re-review report and the landing commit, with no test.
  - Divergence: see RR2-1. The evidence's content is questioned in RR2-2.
- DEC-0159 import-sources: applied (umbrella :20, :24-:32, :40; board :7; tooling :87; prototype :43, :98).
- DEC-0160 grant-0001: docs-reorg :18, :111 and umbrella :9 name the branch and the ledger.
- DEC-0161: 11 encoded segments became `-Users-~-` and the WPMC email was replaced; the handle was kept and listed under "Identifiers kept".
- DEC-0162: the nine re-review imports are present, and the MANIFEST's last line states the snapshot rule.
- DEC-0163: b5-rca-report.md is imported verbatim with a mapping row for decisions.yml:538, and decisions.yml is unchanged (blob at HEAD).
- DEC-0130..0157: each chosen cost was read against the drafts (dump of every answer and chosen cost at HEAD).
  - Superseded costs are cited with their superseding DEC:
    - DEC-0132's "Task 8's grace deletion" by DEC-0151;
    - DEC-0134's "8.1-8.4" by DEC-0147;
    - DEC-0139's "sync point after the b5 merge" by DEC-0146.
  - No contradiction found.

### Check 3: sources
All PASS: the claims rows "MANIFEST", "Verbatim", "redactions", "identifiers" and ".superpowers". The "Tracked citations mapped to imports" table lists only decisions.yml:538, the row DEC-0163 rules on. Tracked decisions.yml :1428, :1482 and :1518 cite wiring-extract.md, tracks-research.md and program-docs-review.md, which are imported but not listed in that table. That is outside DEC-0163's question; see Unrelated findings.

### Check 4: handoffs
- Keys, counts and line numbers are true at HEAD (claims above).
- The resume prompts name the read order, the run_all stop rule, the branch, its base, the worktree (stream B), the block, the lean path, the hardware rule (stream B) and the stop point. With the playbook and the handoff bodies they name, that is enough for a fresh session.
- One handoff sentence diverges from the brief: RR2-1.

## findings raised (proposed rows; the controller registers them)
- **RR2-1 MEDIUM (check 4/5): the brief and the handoff disagree on which commit closes PROG-1.**
  - The two texts:
    - Brief check 5: "PROG-1 set closed with DEC-0158 evidence in findings.yml (as the landing commit will)".
    - draft/strict-delivery-handoff.md:40: "once the commit that lands these documents exists, the next register edit moves it planned -> open -> closed ... with evidence ... and the landing commit's sha, since a commit cannot name its own sha". That handoff also lists `{PROG-1: planned}` in findings_in_flight (:11).
  - DEC-0158's cost is "close PROG-1 on the tracked re-review report and the landing commit".
  - Both shapes pass gate_register: closed in the landing commit, with an existing sha such as 667e0d7 or with the report path alone (variant D above); or closed later, citing the landing sha.
  - Not picking a side. Replacement text, whichever the controller rules:
    - (a) If the landing commit closes PROG-1: handoff :11 `{PROG-1: closed}` (or drop it, as a closed row is not "in flight"; the count becomes 87). Handoff :40: "PROG-1 (the replan documents) is closed in the commit that lands them, with evidence `docs/superpowers/state/program/sources/program-docs-review.md` and `667e0d7` (DEC-0158)". PROG-1.txt :2: "Closure evidence: ... and 667e0d7".
    - (b) If a later edit closes it: the brief's check-5 setup is not the landing shape; the drafts stand as written.
- **RR2-2 MEDIUM (check 2, DEC-0158): the closure evidence is a report that does not approve the drafts.**
  - The cited report `program-docs-review.md` ends its re-review with "status: DONE_WITH_FINDINGS ... Quality ❌ (R-1..R-4 below)" (sources/program-docs-review.md, "## Re-review").
  - The resolution of R-1..R-8 is recorded only in program-docs-fix2-report.md (imported) and in this final re-review, which is not imported.
  - A reader of the closed row's evidence finds open findings and nothing that shows them closed.
  - Replacement:
    - import this report (once the finding round is clean, its final approving version) as `docs/superpowers/state/program/sources/program-docs-rereview2.md`, with a MANIFEST row (sha256, bytes, origin, redaction scan);
    - add it to PROG-1's evidence;
    - name it beside program-docs-review.md in handoff :40 and PROG-1.txt :2.

    Whether DEC-0158's "the tracked re-review report" admits a second report is for the controller to rule. If the controller rules it is not admitted, it is an owner question (DEC-0158 wording).
- **RR2-3 LOW (F14 dimension): the board states a citation form its own row breaks.**
  - The texts:
    - board.md :3: "Every citation here has the form `path:line@sha \"quote\"`".
    - Row A (:7) cites "`docs/superpowers/state/decisions.yml` lines 1028-1303" and "`.../b5r17-subcontroller-report.md` \"denials\"".
  - `grep -cE ':[0-9]+@[0-9a-f]{7}' board.md` -> 0.
  - Replacement :3: "From program tooling Task 3 on, every citation here has the form `path:line@sha \"quote\"` and is checked by `tools/program/check_cites.py`; Task 3 Step 4 rewrites the citations below into that form." Either that, or rewrite the two now, e.g. `docs/superpowers/state/decisions.yml:1028@667e0d7 "- id: DEC-0106"`.
- **RR2-4 LOW (F14 dimension): the prototype plan breaks its own citation constraint.**
  - The texts:
    - prototype plan :20: "Every file fact in a tracked document cites a tracked file as `path:line@sha \"quote\"`".
    - The plan itself cites `components/drivers/board_devkit_v1/board.c:48` with no sha (:31), and has no `@sha` citation at all (`grep -cE ':[0-9]+@[0-9a-f]{7}'` -> 0). The tracked umbrella cites "S3 :35" and similar.
  - Replacement :20: "Every file fact a stream B task writes into a tracked document cites a tracked file as `path:line@sha \"quote\"`; git-ignored research is imported first." Optionally also :31 `board.c:48@667e0d7 "drive LOW = GPS on"`.
- **RR2-5 LOW (F8 dimension, new surface from the R-7 fix): the ownership table lacks the docs-reorganisation ledger.**
  - The docs-reorg plan creates `docs/superpowers/state/docs-reorganisation/ledger.md` (:46, :77), and the playbook ownership row A names `docs/superpowers/state/strict-delivery/`, `program/`, `program-tooling/` but not `docs-reorganisation/` (`grep -n 'docs-reorganisation/' playbook` -> none).
  - Under playbook :29 a path neither row names stops the session until a sync.
  - Replacement in playbook :26: "... `docs/superpowers/state/strict-delivery/`, `program/`, `program-tooling/`, `docs-reorganisation/`, ...".

## Decision needed
None is required to proceed. RR2-2's last sentence becomes an owner question (Q-rr2-1) only if the controller rules that a second report is outside DEC-0158. The options would be:
- (a) add the final re-review report as further evidence. Cost: one import and three sentences.
- (b) close on program-docs-review.md alone, as DEC-0158 literally reads. Cost: none; the evidence shows open findings with no record of their closure.

## Unrelated findings (not caused by this change)
- Tracked decisions.yml cites workspace files that have imported copies but are not listed in MANIFEST's "Tracked citations mapped to imports":
  - :1428 `.superpowers/sdd/program/wiring-extract.md`;
  - :1482 `.superpowers/sdd/program/tracks-research.md`;
  - :1518 `.superpowers/sdd/program/program-docs-review.md`.

  Reproducer: `git grep -n '\.superpowers/' HEAD -- docs/superpowers/state/decisions.yml`. Class: tracked register text citing git-ignored files. It is wider than DEC-0163, which ruled on :538 only. More tracked state files cite the workspace (findings.yml :1759, :2767; decision-sheets/b5r16.md :41, :215; ledger :189), and the fix2 report already lists the class. Owner: controller to register.

## denials
none.

## background jobs
none.

## budget used
About 120k of 250k tokens and about 40 of 60 minutes.

## Re-review (round 3)

status of this round: NEEDS_DECISION (also carries findings RR3-1..RR3-4). The whole report's first line is set to match.

This round re-checks fix round 3 ("## Round 3" in program-docs-fix2-report.md) at HEAD 667e0d7. Read-only. Probes ran in a scratch clone `<scratchpad>/rr3/clone` under the DEC-0032 guard (`set -eu`, `pwd -P` = toplevel, case match on the scratchpad path, no git config overrides), and the clone has been deleted. The checkout stayed clean (0 porcelain lines).

What I changed in this report:
- The round-2 text above had quoted an identifier fragment on two lines (:53, a part of the WPMC email; :54, the encoded user-name segment and the handle). Those are now worded generically, so this file and its pending re-import need no DEC-0161 redaction.
- The import made in round 3 (sources/program-docs-rereview2.md, MANIFEST :28 and :38) no longer matches this file. The controller re-imports it (see check (c)).

### gates run (clone at 667e0d7)
Setup:
- Every draft and source is placed as in round 2 (35 sources plus MANIFEST).
- findings.yml from the PROG-1 line to the end is replaced by draft/prog-1-closed-row.yml, then draft/prog-2-row.yml is appended.
- Everything is staged, and the review packages are copied in.

| command | result |
|---|---|
| `python3 tools/gates/gate_register.py` | rc 0; PASS schema, owner, duplicate, transition, evidence, report (89 rows; the last two are PROG-1 and PROG-2) |
| run_all, landing shape (only the prototype plan in flight) | rc 0, 239 lines, 0 non-PASS |
| run_all, all three plans in flight (temporary ledgers) | rc 0, 298 lines, 0 non-PASS |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31, 0 non-PASS |
| `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` | Ran 658 in 137.6 s, OK (skipped=1) |
| `tools/.venv/bin/python3 -W error -m unittest discover ...` | Ran 658 in 149.4 s, OK (skipped=1) |
| PROG-1 transcript, steps 1-3 | 1 PASS / 0 FAIL / 1 PASS |

### claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| The 5 new `path:line@sha "quote"` citations hold: board ×3, prototype plan :30 and :31 | for each, the quote is found in line l of `git show sha:path` | the quote on line l-1 | all 5 OK; at l-1 all 5 False | PASS |
| No bare backticked `path:line` remains in the board, the handoffs, the three plans or the playbook | `grep -noE` | | none | PASS |
| Every citation in a handoff has the form | `grep -noE '\`path\` lines N-M'` | | strict-delivery handoff :40 "`docs/superpowers/state/decisions.yml` lines 1028-1303" | FAIL -> RR3-1 |
| MANIFEST: 35 rows equal the file set; sha256 and bytes match; verbatim rows equal their originals; redacted rows' original sha256 match | python hashlib over the rows, as in round 2 | | `rows 35 files 35 True`, no BAD line. These were the originals before this round's edit to this report | PASS |
| No identifier in the drafts except the kept handle | `grep -rnoiE` emails, `/Users/x`, `-Users-x-`, name forms | | sources/program-docs-rereview2.md :53 (a fragment of the WPMC email, from my round-2 text) and :54 (handle). Fixed at the source in this report; the re-import removes both | see (c) |
| `.superpowers/` mentions | grep over the placed drafts and the prog rows | | the same 4 locations; prog-1-closed-row.yml :4 is the row's existing class text ("drafts .superpowers/sdd/program/draft/"), a location | PASS |
| No draft still says a later register edit closes PROG-1 | `grep -rn 'next register edit\|PROG-1: planned'` | | none; handoff :40 and PROG-1.txt :2 both say the landing commit closes it | PASS |
| Handoff findings_in_flight equals the not-closed rows | dict compare with both prog rows placed | | register 88, handoff 87; the difference is `{'PROG-2'}` | FAIL -> RR3-4 |
| PROG-2's owner resolves to the docs-reorganisation plan | registers.owner_errors (any text or positive int passes); gate_review_package.task_findings :300-307 matches `str(owner.plan) == <plan dir>` and `str(owner.task) == <task dir>`; the docs-reorg block start registers "owner plan `docs-reorganisation`, task `T1`..`T3`" (:46) | | the plan id `docs-reorganisation` matches the plan's own ledger dir and row convention. `task: 2` (an int) gives "2", while the plan's tasks are "T2"; every existing plan row uses a string task (e.g. 'S2-T7') | plan id: PASS; task: FAIL -> RR3-3 |

### (a) playbook :29, the docs-move sentence
The sentence: "The docs reorganisation block (stream A) moves and rewrites references in files of both rows, stream B's included (`docs/`, `.github/workflows/firmware.yml`, the helpers under `tools/`); it runs only when stream B has no branch open, with every document moved at once rather than stream A's first (DEC-0146, DEC-0139)."

It is entailed by what the owner chose:
- DEC-0139 (chosen cost): "one move to a layout the owner approves ... with no stream branch open on the moved paths". That is one move, over the docs of both streams, under a no-open-branch condition.
- DEC-0146 (chosen cost): "the physical move (DEC-0139) right after field-readiness".
- Rewriting references in firmware.yml and the tools helpers follows from "gate paths become one config table" plus the move itself.

I find no new policy in that sentence.

The same round also added two rows to row A (playbook :26), and I check them on the same question:
- `~/.claude/skills/strict-delivery/`: entailed. DEC-0143 puts the rules "into the strict blocks in the program tooling block" (stream A), and only strict-delivery plans edit the skill.
- "the project memory notes under `~/.claude/projects/`": not entailed by any DEC. DEC-0157 (chosen cost) says "paths no row names go to the owner at a sync". With this row in place, a stream B session may not edit a memory note (playbook :23, prototype plan :19, prototype handoff trap :26). That is new ownership policy: Q-rr3-1.

### (b) the two drafted rows
- prog-1-closed-row.yml passes gate_register at HEAD with everything placed (above).
  - Contradiction (round 2): the same row with an untracked evidence path FAILs `register.evidence.PROG-1`.
  - Its evidence names sources/program-docs-rereview2.md, so that import must be the final version of this report: see (c).
- prog-2-row.yml passes gate_register (owner category PASS), but has three problems:
  - its task id does not match the plan's convention (RR3-3);
  - landing it in the replan commit makes the handoff count false (RR3-4);
  - routing it to docs-reorganisation Task 2 contradicts that task's own line, "**Findings:** none -- the move DEC-0139 orders; no register row yet" (docs-reorg plan :82). Routing a new row into a deferred plan's task is plan scope, which is owner-only: Q-rr3-2.

### (c) check 5 again
- run_all is only PASS, gate_plan --plan PASSes for each new plan, and both suites are green (table above).
- What PROG-1's closed row needs (unchanged from round 2, confirmed with the drafted row):
  - history ... planned, open, closed, each entry with an existing commit sha;
  - evidence non-empty, each entry a tracked path (staged counts) or an existing sha;
  - ruling DEC-0158 (optional), pin null.
- Two landing conditions follow from this round:
  1. Re-import this report after its last edit: sources/program-docs-rereview2.md, MANIFEST rows :28/:38 (now stale) and the main table row. This report now needs no redaction rows.
  2. MANIFEST :48 says "the re-review of fix round 3 does not exist yet"; that sentence is false once this section exists. Drop it, or replace it with the re-import row. If this report stays the final re-review, it is the closure evidence the drafts already name.

### findings raised (proposed)
- **RR3-1 LOW (the RR2-3 dimension over the whole set: a handoff citation not in the form).**
  - The texts:
    - strict-delivery handoff :40: "`docs/superpowers/state/decisions.yml` lines 1028-1303";
    - playbook drift check: "every citation in a handoff, the board or a brief has the form `path:line@sha \"quote\"`".
  - Replacement :40: "... the unanswered rows from `docs/superpowers/state/decisions.yml:1028@667e0d7 \"- id: DEC-0106\"` to `docs/superpowers/state/decisions.yml:1303@667e0d7 \"answered_at: null\"`, deferred by the replan ...". Both quotes were verified on those lines at 667e0d7.
- **RR3-2 LOW (DEC-0161 dimension, caused by my round-2 text and carried into the import).**
  - sources/program-docs-rereview2.md :53 holds a fragment of the WPMC email, and :54 the encoded user name (redacted) and the handle.
  - Fixed at the source in this report. Resolution: re-import this report (condition 1 above), with no redaction rows needed.
- **RR3-3 LOW (prog-2-row.yml owner task id).**
  - The texts:
    - the row: `task: 2`;
    - the docs-reorg plan :46: "task `T1`..`T3`";
    - task_findings matches `str(task)` against the review task directory.
  - Replacement: `task: T2`, if Q-rr3-2 routes it to that task.
- **RR3-4 LOW (handoff count if PROG-2 lands with the replan).**
  - findings_in_flight has 87 rows, while the register would have 88 not closed (`{'PROG-2'}`).
  - Replacement, if PROG-2 lands in the same commit: add `{PROG-2: open}` to handoff :11 and change :40 to "... less PROG-1, plus PROG-2 (88 rows)".

## Decision needed (round 3)
- **Q-rr3-1, who owns the memory notes under `~/.claude/projects/`?** Row A now claims them (playbook :26), and no DEC assigns them (DEC-0157: unnamed paths go to the owner).
  - (a) stream A owns them. Cost: stream B sessions may not write or edit a memory note; each one waits for a sync.
  - (b) shared, like the registers: each stream adds its own notes and never edits the other's; the docs move rewrites them once with no stream B branch open. Cost: one sentence under "Shared files".
  - (c) no row: the docs-reorg block raises them at the sync before Task 2 (DEC-0157). Cost: one stop then.
- **Q-rr3-2, where PROG-2 goes (owner and plan scope).**
  - (a) route it to docs-reorganisation Task 2 (`task: T2`), and change that task's Findings line to "PROG-2" with a step that adds the three MANIFEST map rows. Cost: the docs-reorg plan grows by one row; it is fixed after field-ready.
  - (b) fix it now in the replan commit by adding three rows to MANIFEST's "Tracked citations mapped to imports", then close it with evidence. Cost: one MANIFEST edit, one more re-import check.
  - (c) owner kind issue (a GitHub issue). Cost: one issue; it is outside the plans.

## denials (round 3)
none.

## background jobs (round 3)
none.

## budget used (round 3)
About 45k of 100k tokens and about 20 of 30 minutes.

## Re-review (round 4) -- landing check

Status of this round: DONE, APPROVED. The whole report's first line is set to DONE: every finding from rounds 2-3 is closed, and Q-rr3-1/Q-rr3-2 are answered by DEC-0164/DEC-0165.

Scope: fix round 4 ("## Round 4" in program-docs-fix2-report.md) and the owner answers DEC-0164 and DEC-0165 at HEAD b922d81. `git diff --stat 667e0d7 b922d81` shows only decisions.yml (+20 lines).

How the round ran:
- Read-only, with probes in a scratch clone `<scratchpad>/rr4/clone` at b922d81 on block/strict-delivery-b5, under the DEC-0032 guard (`set -eu`, `pwd -P` = toplevel, case match on the scratchpad path, no git config overrides). The clone has been deleted, and the checkout stayed clean (0 porcelain lines).
- Setup: every draft and the 35 sources plus MANIFEST placed at their targets. findings.yml from PROG-1 to the end is replaced by prog-1-closed-row.yml, and prog-2-row.yml is appended. Everything is staged, and the review packages are copied in.

One change of my own in this report: in round 3, line :218 had again quoted the WPMC email fragment. That line is now generic, and a grep of this file for emails and name forms finds only the generic pattern `-Users-x-`. The landing re-import of this file (MANIFEST :91) therefore needs no redaction row.

### gates run (clone at b922d81)
| command | result |
|---|---|
| `python3 tools/gates/gate_register.py` | rc 0; PASS schema, owner, duplicate, transition, evidence, report |
| run_all, landing shape (only the prototype plan in flight) | rc 0, 239 lines, 0 non-PASS |
| run_all, all three plans in flight (temporary ledgers, removed afterwards) | rc 0, 298 lines, 0 non-PASS |
| gate_plan --plan program-tooling / prototype-field-ready / docs-reorganisation | rc 0 each: 28 / 31 / 31, 0 non-PASS |
| `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` | Ran 658 in 137.1 s, OK (skipped=1) |
| `tools/.venv/bin/python3 -W error -m unittest discover ...` | Ran 658 in 150.9 s, OK (skipped=1) |
| PROG-1 transcript, with step 2 taken from b922d81 | 1 PASS / 0 FAIL / 1 PASS |

### claims
| claim | direct | contradiction | output | result |
|---|---|---|---|---|
| DEC-0164 (shared) is applied as its cost text says: "shared like the registers: each stream adds its own notes and never edits the other's; the docs move rewrites them once with no stream B branch open; one sentence under the playbook's Shared files" | `grep -ni 'memory notes'` in the playbook | if row A still claimed the notes, a second hit would show on :26 | one hit only, Shared files :34, with the cost text nearly verbatim and "(DEC-0164)"; row A keeps the skill only | PASS |
| DEC-0165 (fix-now) is applied: "add the three rows to MANIFEST's tracked-citation map in the replan landing commit; PROG-2 lands closed with that evidence" | the PROG-2 repro grep on the placed MANIFEST; prog-2-row.yml | before round 4 the same grep gave 0 (round 3) | 3 map rows, each pointing to an existing import, and `decisions.yml:1428/1482/1518` lie inside DEC-0135/0141/0145 (`- id:` at :1426/:1480/:1516). PROG-2 is `state: closed`, `ruling: DEC-0165`, evidence MANIFEST.md, history open -> closed (legal: registers.py:14) | PASS |
| PROG-2's owner strict-delivery/program-replan is consistent with DEC-0165 | owner equal to PROG-1's (the replan landing task) | the option DEC-0165 rejected (docs-reorg-t2) would need plan docs-reorganisation T2 | landing in the replan commit = PROG-1's owner; RR3-3 moot | PASS |
| Both closed rows pass gate_register with everything placed | gate_register in the clone | round 2: an untracked evidence path FAILs `register.evidence.PROG-1`; planned -> closed FAILs `register.transition`. The fixer reports the same for PROG-2 with MANIFEST unstaged | rc 0, 6 PASS | PASS |
| RR3-4: handoff findings_in_flight equals the register's not-closed rows | dict compare | round 3: `{'PROG-2'}` differed | 89 rows, 87 not closed, handoff 87, `True` | PASS |
| RR3-1 (dimension over all handoffs, the board, plans and playbook): no non-form line citation | `grep -noE` for bare backticked `path:line` and `` `path` lines N-M `` | round 3 found handoff :40 | none | PASS |
| Every `path:line@sha "quote"` in the placed drafts holds | python: quote in line l of `git show sha:path` | the quote on line l-1 | 7 citations (board ×3, prototype plan ×2, handoff ×2): all OK; at l-1 all False | PASS |
| MANIFEST: 35 rows, sha256 and bytes match, verbatim/original hashes hold | python hashlib as in round 2 | | `rows 35 files 35 True`, no BAD line | PASS |
| No identifier in the drafts except the kept handle | `grep -rnoiE` emails, `/Users/x`, `-Users-x-`, name forms | | handle at b5r15-budget/dec-0122.yml:9 (kept, DEC-0161). Generic patterns only in the fix2 report :271 and this report. The import's :218 email fragment is gone from the workspace original (fixed above); the landing re-import carries the fix | PASS, with the landing condition below |
| `.superpowers/` mentions are locations only | grep | | playbook :47, tooling :52, :54, docs-reorg :42 (locations), and prog-1-closed-row.yml :4 (the existing class text, a location) | PASS |
| DEC ranges vs cited DECs per draft (F4 dimension) | max DEC cited vs stated range per file | | the umbrella states 0132..0160 and cites at most 0160. The strict handoff :38 "State at `667e0d7` ... (DEC-0132..DEC-0163)" is true at 667e0d7, which it names; :40 cites DEC-0165 for the landing commit. The other ranges (0132..0157) say what main lacks or what was true on 2026-10-29 | PASS |
| No DEC-0130..0165 contradicted | DEC-0164/0165 read against the playbook, MANIFEST and the rows; DEC-0130..0163 as in rounds 2-3 (unchanged drafts) | | none found | PASS |

### What PROG-1's (and PROG-2's) closed row needs (check 5)
- History ends `open, closed` with legal steps (PROG-1 planned -> open -> closed; PROG-2 open -> closed), each entry {state, sha, by} with an existing commit sha.
- Evidence is non-empty, each entry a tracked path (staged counts) or an existing sha.
- `ruling` is a DEC id (DEC-0158 / DEC-0165), and `pin` is null.
- Both drafted rows meet these.

### Verdict
Each of spec, quality and claims-executed is ✅:
- **Spec ✅.** DEC-0158..0165 are applied as their chosen costs say (rounds 2-4, claims above), and no draft contradicts DEC-0130..0165.
- **Quality ✅.** The round-2 and round-3 findings are closed, with each dimension re-run over the whole set:
  - RR2-1..RR2-5 hold: closure in the landing commit; the approving report is evidence; board, plan and handoff citations are in the form; docs-reorganisation/ is in row A.
  - RR3-1, RR3-2, RR3-4 are closed, and RR3-3 is moot under DEC-0165.
  - Every gate is green on both interpreters.
- **Claims-executed ✅.** Every claim above has a direct check and a contradiction check, all PASS.

**APPROVED for landing**, on one condition the drafts already state (MANIFEST :91): re-import `.superpowers/sdd/program/program-docs-rereview2.md` after this section, with the DEC-0143/DEC-0161 scan, and refresh its MANIFEST row (sha256, bytes) in the landing commit. Then run gate_register and run_all once more, because the row's hash changes.

### findings raised (round 4)
none.

### Decision needed (round 4)
none.

### denials, background jobs (round 4)
none.

### budget used (round 4)
About 35k of 80k tokens and about 20 of 30 minutes.
