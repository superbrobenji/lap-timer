# b5 round 17 item 1: documentation and class-as-epic design (draft for owner approval)

**Fix round 1 (end of file) supersedes**: the prompt diffs in (a), the spec-text list in (b), the tracked diff in (c) and the optional D33 hunk; it adds Q-doc-18..21. Q-doc-1, -3, -12, -17, M4, mapping row 7, and the (d) rows for checkpoints and IDF built-ins are corrected in place.

Workspace text only (spec D35 drafter). No tracked file, no `~/.claude` file, no commit. Every diff below was applied and measured in a scratch clone under the DEC-0032 guard (`.../scratchpad/r17-docs/repo`, `set -eu`, `pwd -P` and `git rev-parse --show-toplevel` asserted under `r17-docs/`); prompt diffs were applied to copies (`r17-docs/pa/`, `r17-docs/opt/`).

## Header

- Base: HEAD `2e43f132314f55bbc1e7ee8c00aa22578c4ef29a` (`git rev-parse HEAD` in the checkout, tree clean: `git status --porcelain | wc -l` = 0). `git apply --check -v` of the tracked diff below against the checkout: "Checking patch CLAUDE.md... / ...strict-delivery-design.md... / ...test_hooks.py..." with no error.
- Sources read: brief `b5r17-controller-brief.md` "Item 1" (a)-(d); `docs/superpowers/state/decisions.yml` DEC-0102 (:949), DEC-0103 (:972), DEC-0104 (:991), DEC-0105 (:1009), DEC-0089 (:824), DEC-0095 (:880), DEC-0032 (:296), DEC-0130 (:1381), DEC-0131 (:1390), the open D33 sweep questions (:1028, :1046, :1063, :1081, :1100, :1118, :1136, :1151, :1168, :1184, :1201, :1220, :1237, :1255, :1272, :1288; 16 entries, `answer: null` at each); spec `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (D1-D35 at :15-:49, §4.1 :97-:116, §6 :189-:199, §12 :273-:287); `CLAUDE.md`; `docs/superpowers/state/findings.yml` (79 rows: 44 planned, 28 pinned, 7 open); `tools/gates/lib/registers.py`, `tools/gates/gate_register.py`; `tools/gates/tests/test_hooks.py:991-999`; `~/.claude/skills/strict-delivery/prompts/*.md` (read only); staged D36 `b5r15-budget/spec.patch`, `controller-staged.md`; 18 `feedback-*.md` memory files (read only).
- Verification of the tracked diff on the patched clone (D34 commands): `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<fresh> /usr/bin/python3 tools/gates/run_all.py` rc 0, 100 lines, all `PASS`; suite `-W error -m unittest discover -s tools/gates/tests -t .`: `/usr/bin/python3` "Ran 658 tests ... OK (skipped=1)", main checkout `tools/.venv/bin/python3` "Ran 658 tests ... OK (skipped=1)".

### The 5500-byte cap (where it is stated)

- `docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md:7201`: "The dispatch cap is 12,000 characters (`dispatch.max_prompt_chars`), so each block stays at or under 5,500 bytes (DEC-0095)."
- same plan `:7220` (the `check-skill.sh` block): `n=$(wc -c < "$f"); [ "$n" -le 5500 ] || { echo "$r.md is $n bytes (> 5500)"; bad=1; }`; `:7540`: "sizes between 2,800 and 5,500 bytes each (DEC-0095; ...)".
- `decisions.yml:888` DEC-0095 answer: "cap-5500 (owner in session question, 2026-10-27: picked '5500, amend plan (Recommended)'...)".
- `docs/superpowers/state/strict-delivery/ledger.md:189`: "DEC-0095 cap-5500: the strict-block size cap is 5500 bytes."
- Enforced only by the git-ignored `.superpowers/sdd/strict-delivery/check-skill.sh:13` (same line as plan :7220); no gate holds it (register row B5-UR-14: "a decided cap no gate enforces").

### Byte counts (`wc -c`, before = installed file, after = copy with the proposed hunk)

| Prompt file | Before | After (base proposal) | After (optional hunks) | Cap 5500 |
|---|---|---|---|---|
| implementer.md | 4410 | 4641 (+231) | - | OK |
| reviewer.md | 4509 | 4748 (+239) | - | OK |
| verifier.md | 4339 | 4339 (no hunk) | 4580 (+241, Q-doc-4 option plus-verifier) | OK |
| controller.md (live) | 5281 | 5281 (no hunk) | 5390 (+109, Q-doc-4 option plus-controller) | OK live |
| controller.md (staged D36, `b5r15-budget/controller-staged.md`) | 5500 | 5500 | 5609 (the same hunk applied with `patch`) | **over by 109** |
| audit-reviewer, audit-verifier, exception-reviewer, investigator | 3501, 3379, 3553, 3215 | unchanged | unchanged | OK |

Every changed copy keeps a single trailing newline (`tail -c1 | od -c` = `\n`) and every `check-skill.sh` phrase ("Verification method.", "by contradiction", "Zero bugs.", "Owner-only decisions.", "NEEDS_DECISION", "Budget.", "Background work.", "Report contract.") is still present (`grep -qF` loop, no MISSING line).

## (a) DEC-0103: a deleted test is never a fix

Owner answer verbatim (decisions.yml:989): "restore-and-fix, none of the listed options (owner in session, 2026-10-29: 'we cannot simply delete a test to \"fix\" something. the thing or the test must be fixed. if a test was deleted as a fix, it needs to be readded and fixed.'). Effect: the two tests DEC-0063 dropped in 6a7b2f9 (test_s2_plan_blocks_hold_the_dec_0060_caps, test_readme_states_no_budget_cap) are restored and fixed; this supersedes DEC-0063 drop; B5-RD-1 pins on the restored test; B5-UD-1 pins on test_claude_md_carries_spec_section_12_verbatim_and_nothing_else".

Proposed text (hunks in the tracked diff under (c)):
- spec row D37 (new, after D35; numbering: Q-doc-1);
- spec §6 step 6: the reviewer "lists every test the diff removes (D37)";
- spec §12 and `CLAUDE.md` bullet "A deleted test is never a fix ..." (whether it binds the session: Q-doc-3);
- implementer block, "Build." paragraph: "Never delete a test as a fix: fix the code or the test, and restore a test deleted as a fix (spec D37)."
- reviewer block, "Execute, never trust." item (6): "List every test function the diff removes, with its commit and reason; one removed as a fix is a finding (spec D37)."

Scope of "deleted": the base text says deleted only, as the owner's words do; "deleted or weakened" (the brief's wording) is Q-doc-2.

Implementer and reviewer prompt diffs (paths relabelled to the installed files; both apply to the files at the sha256 below):
- implementer.md sha256 before `d13756d2ee03f1b3d50addfa5b9861579f0b2b6ce18ff336dfea808ee43022c4`; reviewer.md `450fb19749adc09dac24ab622bcbff4960ca8b166db573cb0e69adc5bc5ea26c`.

```diff
--- a/~/.claude/skills/strict-delivery/prompts/implementer.md
+++ b/~/.claude/skills/strict-delivery/prompts/implementer.md
@@ -4,7 +4,7 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Build. Test first, through the production call path (the real file, or a script's main() on a fixture; never an extracted helper); watch it fail for the stated reason; then the minimal code. For every finding id in ## Findings write the mutation transcript at docs/superpowers/state/<plan>/mutations/<id>.txt in the umbrella format (fix present PASS, fix reverted FAIL, fix restored PASS). Test every sibling in the row's list or list it as an unrelated finding. Run the brief's full gate list clean, plus python3 tools/gates/run_all.py, before you report; zero warnings, zero gate failures.
+Build. Test first, through the production call path (the real file, or a script's main() on a fixture; never an extracted helper); watch it fail for the stated reason; then the minimal code. For every finding id in ## Findings write the mutation transcript at docs/superpowers/state/<plan>/mutations/<id>.txt in the umbrella format (fix present PASS, fix reverted FAIL, fix restored PASS). Test every sibling in the row's list or list it as an unrelated finding. Never delete a test as a fix: fix the code or the test, and restore a test deleted as a fix (spec D37). A row other rows name as parent (an epic, spec D38) is done only when every child is resolved and its whole-class test passes. Run the brief's full gate list clean, plus python3 tools/gates/run_all.py, before you report; zero warnings, zero gate failures.
 
 Input domain (spec D24-D28). Cover every row of the task's input-domain table with a test; fix the whole dimension a finding names, not only the reproduced value; inputs outside the gate's whitelist FAIL closed with a named check. A rule the plan and spec do not state is a question to the controller, not your choice.
 
```

```diff
--- a/~/.claude/skills/strict-delivery/prompts/reviewer.md
+++ b/~/.claude/skills/strict-delivery/prompts/reviewer.md
@@ -4,11 +4,11 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.
+Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding. (6) List every test function the diff removes, with its commit and reason; one removed as a fix is a finding (spec D37).
 
 Verdict. Spec ✅, quality ✅ and claims-executed ✅ are each stated with evidence; anything less is not approved. A defect caused by this change makes the task incomplete (status DONE_WITH_FINDINGS, the finding listed first). Read the code, the diff and the review package; never edit code yourself. Writes: `set -eu`; stop unless `pwd -P` and the git toplevel are in your scratch clone (DEC-0032).
 
-Coverage (spec D24-D28). Check the change against every row of the task's input-domain table; a missing row is a finding. A re-review re-runs the finding's dimension over the whole changed function, not the round diff, and checks shared-library changes against the spec lines they touch. Rules in a controller's brief are hypotheses. Inputs outside the whitelist must FAIL closed. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35).
+Coverage (spec D24-D28). Check the change against every row of the task's input-domain table; a missing row is a finding. A re-review re-runs the finding's dimension over the whole changed function, not the round diff, and checks shared-library changes against the spec lines they touch. Rules in a controller's brief are hypotheses. Inputs outside the whitelist must FAIL closed. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35). A defect in a register row's class dimension is reported as a child of that class's epic, naming the epic (spec D38).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
```

(Each prompt diff also carries its part (b) sentence: implementer "A row other rows name as parent (an epic, spec D38) is done only when every child is resolved and its whole-class test passes."; reviewer Coverage "A defect in a register row's class dimension is reported as a child of that class's epic, naming the epic (spec D38).")

Optional (Q-doc-4), verifier.md (sha256 before `f3127b6848b6c38860259758925e44cd2df8c7c5ab4cc0e91fdb1d498be8d6c6`):

```diff
--- a/~/.claude/skills/strict-delivery/prompts/verifier.md
+++ b/~/.claude/skills/strict-delivery/prompts/verifier.md
@@ -4,11 +4,11 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.
+Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding. (6) List every test function the branch removes, with its commit and reason; one removed as a fix is a finding (spec D37).
 
 Adversarial scope. Re-run every pin the branch claims, not a sample, each transcript at its row's pinned sha. List the paths the reviewer did not exercise (callers, error paths, the inputs named in each plan's Review Focus) and exercise each. Every reviewer PASS you cannot reproduce is a finding. Never edit code yourself. Writes: `set -eu`; stop unless `pwd -P` and the git toplevel are in your scratch clone (DEC-0032).
 
-Coverage (spec D24-D28). Check every gate against its input-domain table and whitelist; each fix against its whole dimension and function; brief rules are hypotheses. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35).
+Coverage (spec D24-D28). Check every gate against its input-domain table and whitelist; each fix against its whole dimension and function; brief rules are hypotheses. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35). A defect in a register row's class dimension is reported as a child of that class's epic, naming the epic (spec D38).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
```

Optional (Q-doc-4), controller.md live (sha256 before `b0fdc5ed276176743e00e35f5196fec33e78a75e4f53d71abdeeaa094fb98039`); over the cap once staged D36 is installed (table above):

```diff
--- a/~/.claude/skills/strict-delivery/prompts/controller.md
+++ b/~/.claude/skills/strict-delivery/prompts/controller.md
@@ -14,7 +14,7 @@
 
 Convergence (spec D30). Stop dispatching fixes when a re-review finds a new defect in this block's code outside the fix's recorded class, or the block needs a second fix wave after its final review. Dispatch a read-only investigator on where each defect entered and why the review that had it missed it; bring the owner process changes with that evidence. Never offer only freeze, continue or cap.
 
-Commit gate (spec D34, D35). Before each commit of yours, text-only included, run `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` on /usr/bin/python3 and the main checkout's tools/.venv/bin/python3, with no unstaged or untracked path in `git status --porcelain --untracked-files=all` and no line from `git ls-files -v | grep '^[a-zS]'`; a red line or a missing interpreter blocks it. After each child commit re-run the set at its sha; red = that task is incomplete. Items on the same files run serially; one drafter per gate table. A child's budget use is the task notification's count, never its self-report.
+Commit gate (spec D34, D35). Before each commit of yours, text-only included, run `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` on /usr/bin/python3 and the main checkout's tools/.venv/bin/python3, with no unstaged or untracked path in `git status --porcelain --untracked-files=all` and no line from `git ls-files -v | grep '^[a-zS]'`; a red line or a missing interpreter blocks it. After each child commit re-run the set at its sha; red = that task is incomplete. Items on the same files run serially; one drafter per gate table. A child's budget use is the task notification's count, never its self-report. Register a defect in a class's dimension as its epic's child (D38); every owner rule gets a text item (D39).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
```

Install path (spec D34 :48, unchanged): staged copies are task-reviewed as this exact diff, then saved into `~/.claude/skills/strict-delivery/prompts/`; every later dispatch must carry the new block verbatim (gate_dispatch reads `DEFAULT_PROMPTS = os.path.expanduser("~/.claude/skills/strict-delivery/prompts")`, gate_dispatch.py:41). Recording the installed hashes is row B5-UR-13 (state `planned`).

## (b) DEC-0130: class as epic

Owner answer verbatim (decisions.yml:1388): "class-as-epic (owner in session, 2026-10-29, verbatim: 'this seems like a ways of working issue we are trying to define. a class can be seen as an epic that has child rows or tickets. when reviews find bugs, then the bugs fall under that class, and the class cannot be signed off until all bugs are resolved and the full class tested and reviewed'). The design (spec row, register and gate mechanics, mapping of [the open sweep questions at decisions.yml:1028-:1288, ids redacted per the dispatch]'s rows) comes back to the owner for approval before any code".

DEC-0102 answer verbatim (decisions.yml:970): "successor-rows (owner in session, 2026-10-29: '0102 successor-rows'; DEC-0127 read-as-stored, no session recommendation)"; its option text (:961-963): "no code: the three rows stay pinned; three new open rows (R-B-13, R-A-5, R-B-1) carry the widened dimensions and cite the pinned rows; DEC-0089's 'rows reopen' is not done as worded".

### Spec text (hunks in the tracked diff under (c))

- D38 (new row): class = epic; children by `parent`; review-found defects in the dimension become children; epic pins only when every child is resolved and the whole class is tested and reviewed; `gate_register` FAILs an epic pin with an open child; D33 widening and DEC-0102 successor rows map onto epics; placement and epic class are finding-class (owner) decisions.
- §4.1: one schema line `parent: null` and one gate sentence.
- §6 step 6: "files new findings as register rows, a defect in an epic's dimension as that epic's child (D38)".
- §12 / `CLAUDE.md` bullet (Q-doc-3).
- Optional D33 hunk (Q-doc-12), applies after the tracked diff (`git apply --check` passed on the patched clone):

```diff
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -44,7 +44,7 @@
 | D30 | Convergence trigger (DEC-0061). Defects that keep arriving are a process defect, not bugs to chase. When a re-review finds a new defect in the block's code outside the fix's recorded class, or a block needs a second fix wave after its final review, the controller stops dispatching fixes, dispatches a read-only investigator (where each defect entered; why the review that had it in scope missed it; ranked root causes) and brings the owner process changes with that evidence; freeze, continue or cap are never the only options offered. |
 | D31 | Generated input domain (DEC-0088 O1). A gate task's D24 table starts from a skeleton: one row per (path or argument the gate reads or writes, checklist dimension), found from every filesystem call (`open`, `os`, `os.path`, `shutil`, `glob`, `os.walk`, `pathlib`), every subprocess call and every argparse argument. A script generates it (register row B5-PROC-2); until the script lands the drafter builds the skeleton by hand from a grep of those calls and puts the grep in the table. The drafter fills each cell or argues N/A. The D24 checklist also covers: repository state that changes git output (config, attributes, info/exclude); write targets as well as read inputs; every non-regular kind (directory, FIFO, socket); regex anchoring (a name or value matches as a whole string, never with a trailing line terminator); and, in a multi-item run, each item's isolation. Folding an owner answer re-derives every row it reaches, not only the rows that already exist. |
 | D32 | Default contract (DEC-0088 O2; amends D25). For every gate: an input or write target that is unreadable, of the wrong kind, a symlink at any path component below the repository root, or a path argument that is itself a symlink (DEC-0093) FAILs that item's input check (`<pfx>.input`, as DEC-0066) naming the path; in a multi-item run one item's input failure FAILs that item and the others still run; names and values match as whole strings (fullmatch); git commands whose output a gate reads force their output forms (pinned config and attributes). One shared fail-closed helper in `tools/gates/lib` enforces it (register row B5-PROC-2); until it lands each gate applies the same rule in place. Table cells cite this default; only choices it does not settle are owner questions. |
-| D33 | Class restatement and idiom sweep (DEC-0088 O3, DEC-0089). D26 applies to pinned rows too. When a found defect's dimension reaches a pinned row's class, widening that class is an owner question (D10) unless a DEC already widens it (DEC-0089: B5-FIX-9 the name grammar incl. line terminators; B5-FIX-6 a non-directory at any package-tree level; B5-FIX-4 V-1 repository state that changes git output); a widened row reopens, is re-reviewed and re-pinned. The controller greps `tools/gates` for the defect's code idiom, writes the pattern and every hit into the row's siblings and registers every hit in the same commit. On adoption, the controller of the next round restates every pinned row's class by dimension once (register row B5-PROC-3), each widening an owner question unless a DEC covers it. |
+| D33 | Class restatement and idiom sweep (DEC-0088 O3, DEC-0089). D26 applies to pinned rows too. When a found defect's dimension reaches a pinned row's class, widening that class is an owner question (D10) unless a DEC already widens it (DEC-0089: B5-FIX-9 the name grammar incl. line terminators; B5-FIX-6 a non-directory at any package-tree level; B5-FIX-4 V-1 repository state that changes git output); a widened row stays pinned and its widened dimension is carried by an epic (D38, DEC-0102). The controller greps `tools/gates` for the defect's code idiom, writes the pattern and every hit into the row's siblings and registers every hit in the same commit. On adoption, the controller of the next round restates every pinned row's class by dimension once (register row B5-PROC-3), each widening an owner question unless a DEC covers it. |
 | D34 | Mechanical commit gate (DEC-0088 O4; covers B5-U3, #147). Every commit, session and text-only commits included, first passes `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` for `<py>` = `/usr/bin/python3` and `tools/.venv/bin/python3` of the main checkout (`$(git rev-parse --git-common-dir)/../tools/.venv/bin/python3` in a worktree); `PYTHONPYCACHEPREFIX` names a fresh empty directory, so no cached bytecode stands in for a source. The pre-commit hook (row B5-PROC-2) runs the set on a fresh export of the index, and only it makes the tree tested the tree committed; its D24 table carries the rows for stale bytecode (P-1), the ignore files, the attribute and filter files and the global files (DEC-0101). By hand, until it lands: commit only when `git status --porcelain --untracked-files=all` lists no unstaged and no untracked path and `git ls-files -v | grep '^[a-zS]'` lists nothing (no assume-unchanged or skip-worktree entry); the by-hand run claims no equality between the tree tested and the tree committed (DEC-0101 supersedes the DEC-0098, DEC-0099 and DEC-0100 scope clauses for this rule). The review packages under `.superpowers/sdd/*/review/` (they stay git-ignored) are in no commit: the run reads them as they are in the checkout at commit time. Any red line or a missing interpreter refuses the commit. The pre-commit hook enforces it (register row B5-PROC-2); until it lands every agent runs the set by hand before each commit, and the controller re-runs it at every child commit's sha (red: that task is incomplete). Session-written spec, `CLAUDE.md`, strict-block and plan text is applied (saved, for `~/.claude` text, which takes effect on save) or committed only after a task review of that exact diff. Items that touch the same files run serially. Order (DEC-0090): after 64f7ad7 only one commit may land on a red suite, the commit that lands this row. For it `run_all` passes, the suite fails on each interpreter only the two tests U-D-1 and R-D-1 turned red, and their remaining checks are done by hand on their inputs (`test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else`: §12 bullets equal the `CLAUDE.md` bullets; `test_gate_dispatch.GateDispatch.test_s2_plan_blocks_hold_the_dec_0060_caps`: no old-cap text added to the S2 plan); it also registers U-D-1 and R-D-1 (rows B5-UD-1 and B5-RD-1, owner task S2-b5-process-code). The next commit fixes them and makes the suite green; there is no baseline exception after it. |
 | D35 | Drafting sized like review (DEC-0088 O5). A drafter is an implementer dispatch that writes plan text. One drafter per gate table, each sized under its cap with margin as D29 sizes reviews. Every "today" cell carries the command that observed it, or reads OPEN. Reviewers number new questions per area (`Q-<area>-<n>`). Budget use is the harness count from the task notification, never the child's self-report. |
 | D37 | A deleted test is never a fix (DEC-0103). A fix never deletes a test, and a decision question never offers deleting one as an option: the code or the test is fixed. A test deleted as a fix is restored and fixed; DEC-0103 supersedes DEC-0063's drop of `test_s2_plan_blocks_hold_the_dec_0060_caps` and `test_readme_states_no_budget_cap` (row B5-RD-1 pins on the restored test). Every task review lists each test function the diff removes, with its commit and reason; a removal made as a fix is a finding of that task. |
```

### Mechanics (design, not code)

Register schema (today `FINDING_KEYS`, registers.py:34, is an exact set: `_keys` :90-98 FAILs a missing and an unknown key alike).
- M1 `parent`: a finding row's link to its epic, a finding id string or null. Base design: child-to-parent link only (one source of truth); an epic's children are derived (rows naming it). Required-on-every-row vs optional key vs an epic-side `children` list: Q-doc-5.
- M2 What makes a row an epic: base design implicit (a row that another row names as `parent`). An explicit `kind` field (an epic that has no children yet, e.g. a freshly widened class): Q-doc-6.
- M3 Depth: base design one level (a row named as `parent` has `parent: null`; no cycles possible then). Nested epics: Q-doc-11.
- M4 Multi-part rows: B5-FIX-3 and B5-FIX-4 hold parts whose proposed dimensions differ (table below). B4-FIX-1's class holds parts too ("with it the final-review minors F2-F6 and the verifier's finding 1 ... and DEC-0012 = require-equal"), but the open question at decisions.yml:1028 proposes one dimension for the whole row, which reaches only the F1 part. One `parent` cannot express either case: Q-doc-10.

`gate_register` rules (new check ids, all in the existing `register.<category>.<id>` form; categories are the list at gate_register.py:32):
- G1 `register.schema.<id>`: `parent` is null or a string matching `FINDING_ID` (fullmatch, registers.py:21).
- G2 `register.parent.<id>` (new category; or folded into `evidence`, a naming choice for the plan): `parent` names an id present in findings.yml (the `known` set gate_register.py:177 already builds), not the row itself, and (M3) a row whose own `parent` is null.
- G3 `register.evidence.<epic>`: an epic whose state is `pinned` FAILs while any child's state is outside the resolved set. Base resolved set: `pinned`, `closed`, `merged` (`deferred` is Q-doc-7).
- G4 `register.evidence.<epic>`: the epic's pin needs what every pin needs today (registers.py:229-247: `pin {test, transcript}`, a tracked transcript, a tracked review report in `evidence`) plus the whole-class conditions. What the gate can check mechanically vs what the reviewer attests (whole-class test form, review scope): Q-doc-9.
- G5 Order: every child's `pinned`/`closed` history sha is at or before the epic's `pinned` history sha (git ancestry, as `finding_commit_errors` already resolves shas). Included only if Q-doc-9 option a or b is chosen.
- G6 A new child under an already pinned epic: G3 makes the register red at the commit that registers it. What follows (epic reopen, successor epic, or check only at the pin transition): Q-doc-8.
- Other gates the design reaches (named so the plan can place them, not changed here): `gate_review_package` needs a `mutation-<id>.txt` for every row the task owns, so an epic owned by a task needs its whole-class transcript in that task's package; `gate_merge_ready` (S4, not yet built; spec :217 "every register row for the PR `pinned`/`cl...") would hold a PR while an epic of the PR has an open child.

How D33 and DEC-0102 map onto epics:
- D33 (:47) "widening that class is an owner question (D10) unless a DEC already widens it" becomes: placing a pinned row under an epic whose class is the wider dimension is the owner question (finding-class, D10). DEC-0089's three widenings are already answered, so their epics need no new question.
- D33 "a widened row reopens, is re-reviewed and re-pinned" vs DEC-0102 successor-rows ("the three rows stay pinned"): under D38 the pinned row stays pinned as a resolved child; the re-review and re-pin of the wider class become the epic's whole-class review and pin. No `pinned -> open` transition is needed (`FINDING_TRANSITIONS`, registers.py:16: `"pinned": ["closed", "merged"]`). Whether D33's sentence is edited: Q-doc-12.
- D33 "registers every hit in the same commit": each idiom-sweep hit is registered as a child of the epic (D38 text).
- DEC-0102's successor rows R-B-13, R-A-5, R-B-1: each is an open child of the epic carrying its DEC-0089 dimension (B5-FIX-9, B5-FIX-6, B5-FIX-4 V-1 respectively). At HEAD none of the three is registered: `grep -n 'R-A-5\|R-B-1[^0-9]' findings.yml` prints nothing; `R-B-13` appears only inside the classes of B5-ID-1 and B5-ID-2 (:2280, :2312) (Notes N3).
- B5-PROC-3 (planned, owner task S2-b5-sweep; class "every pinned row (27 at 64f7ad7) has its class restated by input dimension once"): under D38 its work becomes the epic placement of the table below, approved by the owner as one mapping (Q-doc-15).

### Mapping of every pinned row (28 at HEAD)

`state: pinned` rows from `yaml.safe_load(findings.yml)`: 28; the same at 64f7ad7: 27; the one difference is B5-CAPS-1 (history `pinned` at 6a7b2f9). "Covered by" names the decisions.yml line of the open D33 sweep question for the row (unanswered; their "widen" option's proposed dimension is quoted as the candidate epic class), or the answered DEC. Epic labels E1.. are placeholders; ids are the plan's to assign in `FINDING_ID` form. Effect assumes the base design (M1-M3, G3 resolved set) and owner approval of the placement.

| # | Row | Class (start, verbatim) | Covered by | Proposed epic (class quoted from the cited source) | Effect |
|---|---|---|---|---|---|
| 1 | SD-S2-T1 | 'strict-delivery rollout step 3: gate_dispatch' | DEC-0105 answered keep-requirement-rows | none (Q-doc-13) | none |
| 2 | SD-S2-T2 | '... PreToolUse Agent branch' | DEC-0105 | none (Q-doc-13) | none |
| 3 | SD-S2-T3 | '... dispatch config check and run_all registration' | DEC-0105 | none (Q-doc-13) | none |
| 4 | SD-S2-T4 | '... gate_review_package' | DEC-0105 | none (Q-doc-13) | none |
| 5 | SD-S2-T5 | '... make_review_package' | DEC-0105 | none (Q-doc-13) | none |
| 6 | SD-S2-T6 | '... gate_plan' | DEC-0105 | none (Q-doc-13) | none |
| 7 | B4-FIX-1 | 'b4 final review F1: ... handle_agent ignores tool_input.subagent_type ... with it the final-review minors F2-F6 and the verifier's finding 1 (...) and DEC-0012 = require-equal (...)' | decisions.yml:1028 | E1 "the Agent tool_input fields that decide which model and context a dispatch runs on: subagent_type (absent, null, every case or spelling of fork, an agent type whose definition fixes a model) and model (absent, null, alias), and their agreement with ## Model" | stays pinned as E1's resolved child; E1 open until its whole-class test + review; the :1028 question proposes this one dimension for the whole row, which does not reach the F2-F6, verifier finding 1 and DEC-0012 parts of the class (Q-doc-10) |
| 8 | B5-FIX-2 | 'DEC-0018 opus fallback for the Fable roles ... is not implemented' | decisions.yml:1046 | E2 "role x model x fable_fallback state (absent, null, opus, another model, a non-string) at dispatch and at --config-check, for every fable-minimum role" | resolved child of E2; the question notes "overlaps B5-FIX-8" (E2 vs E5 overlap, Q-doc-10) |
| 9 | B5-FIX-3 | 'gate_plan per-task rules ...: (a) the pin-step title rule is a bare case-insensitive substring ...; (b) per-task check ids use two forms ...' | decisions.yml:1063 | (a) E3 "the pin-step title as a text grammar: the ASCII word pin as a whole word in every case, Unicode case folds, hyphen, joiner, format and combining neighbours"; (b) E4 "the id form of every per-task check id (one grammar)" | two parts, two epics (Q-doc-10) |
| 10 | B5-FIX-4 | 'b5 final review and adversarial verify findings ...: V-1 ... V-2 ... V-3 ... V-4 ... V-5 ... F1 ...' | V-1: DEC-0089 (answered) + DEC-0102; V-2, V-3, V-5, F1: decisions.yml:1081; V-4: decisions.yml:1063 | V-1 and V-2: E6 "repository state that changes git output" (DEC-0089; :1081 "V-2 folds into V-1's dimension"); V-3: E7 "the ledger's three copies (HEAD, index, work tree) x last-line grammar (CHECKPOINT forms, block numbers, trailing text)"; V-4: E3; V-5: E8 "transcript step-2 grammar: a runnable revert command"; F1: E9 "emptiness at every package-tree level (sdd, <plan>, review/, <task>)" | five epics from one row (Q-doc-10); E6 gets the DEC-0102 successor R-B-1 as an open child (not yet registered, N3) |
| 11 | B5-FIX-6 | 'gate_review_package --all PASSes ... when .superpowers/sdd is a regular file ...' | DEC-0089 (answered) + DEC-0102 | E10 "a non-directory at any package-tree level" (DEC-0089) | resolved child; successor R-A-5 open child (not yet registered, N3); :1272 notes the overlap with E19 |
| 12 | B5-FIX-7 | 'gate_review_package rejects valid git output that make_review_package writes ...' | decisions.yml:1100 | E11 "every commits.txt and diff.patch form git produces for a range: quoted paths (every core.quotePath byte), empty and space-led subjects, binary, rename and copy headers, mode-only and type changes, submodules, deleted or empty files, merges" | resolved child; E11 open |
| 13 | B5-FIX-8 | 'gate_dispatch.fallback_error ... does not check that the fallback model's own rank is an int ...' | decisions.yml:1118 | E5 "the type and value of every model_rank entry and of fable_fallback (int, bool, str, float, None, missing, negative, duplicate) at --config-check and at dispatch time" | resolved child; the question names B5-UD-2 (planned, S2-b5-budget) as a sibling, so B5-UD-2 becomes an open child of E5 |
| 14 | B5-FIX-9 | 'make_review_package branches with no test that fails when they break ...: the package-name check accepting '/' (mutant M22) ...' | DEC-0089 (answered) + DEC-0102 | E12 "the name grammar incl. line terminators" (DEC-0089) | resolved child; successor R-B-13 open child (not yet registered, N3) |
| 15 | B5-FIX-11 | 'S2 plan text credits 'plan last' in run_all.GATES to 'session ruling 2b of b5' ...' | decisions.yml:1136 | E13 "every plan line that states the run_all GATES order or cites a ruling for it" | resolved child |
| 16 | B5-FIX-12 | 'gate_plan drops a Files-style item ... outside its **Files:** list ...' | decisions.yml:1151 | E14 "the position of a Files-style item relative to a task's structure: after any line kind (fence, field line, step, heading, blank, list item, quote, table, HTML comment), nested lists and fences" | resolved child |
| 17 | B5-FIX-13 | 'gate_plan accepts absolute Files paths and paths with a '..' component ...' | decisions.yml:1168 | E15 "the Files path form: absolute, .., ./, //, trailing slash, backslash, ~, URL, empty, whitespace, a symlink component, case and Unicode normalisation" | resolved child; the question notes "overlaps open B5-FIX-14" (planned), which would become an open child of E15 |
| 18 | B5-FIX-15 | 'pin_title accepts non-ASCII spellings and invisible joins as the word pin ...' | decisions.yml:1063 | E3 | resolved child of E3 |
| 19 | B5-FIX-17 | 'gate_review_package's pin cross-check compares only the test name after the last '::' ...' | decisions.yml:1184 | E16 "the transcript command grammar against the pin.test grammar: module path, class, dotted vs :: forms, shell quoting and escapes, environment assignments, several commands" | resolved child |
| 20 | B5-FIX-18 | 'names_test splits the transcript command on whitespace without shell parsing ...' | decisions.yml:1184 | E16 | resolved child |
| 21 | B5-FIX-19 | 'gate_plan branches with no test that fails when they break ...' | decisions.yml:1201 | E17 "every changed branch of gate_plan Task 6 and B5-FIX-5 code has a killing mutant (comparison, constant, boolean, early return)"; or none under that question's option leave-to-d27 | resolved child of E17, or no epic |
| 22 | B5-FIX-20 | 'plans_in_flight reads the working-tree ledger ... but reads the index and HEAD copies ...' | decisions.yml:1220 | E18 "the root argument form for every read in gate_plan and plans_in_flight: root = toplevel, a subdirectory, a path through a symlink, a worktree, relative or absolute" | resolved child |
| 23 | B5-FIX-21 | 'gate_review_package --all skips a .superpowers/sdd/<plan> whose review/ cannot be stat'ed ...' | decisions.yml:1237 | E20 "permissions at every package-tree level (.superpowers, sdd, <plan>, review/, <task>, each package file): unreadable, unlistable, untraversable" | resolved child; the question folds B5-RA-6 (planned) in, so B5-RA-6 becomes an open child of E20 |
| 24 | B5-FIX-22 | 'gate_review_package reads only the first table of claims.md ...' | decisions.yml:1255 | E21 "the claims.md document grammar: number and position of tables, blank and non-table lines, header and separator forms" | resolved child |
| 25 | B5-FIX-23 | 'no test covers a dangling <plan>/review symlink in gate_review_package --all ...' | decisions.yml:1272 | E19 "symlink kinds at every package-tree level (dangling, to a file, to a directory, a loop, outside the root)" | resolved child; the question notes the overlap with B5-FIX-6 (E10) |
| 26 | B5-FIX-24 | 'plans_in_flight branches with no test that fails when they break (B5-FIX-5 code) ...' | decisions.yml:1201 | E17, or none (leave-to-d27) | resolved child of E17, or no epic |
| 27 | B5-FIX-25 | 'plans_in_flight decodes each ledger with a bare .decode('utf-8') ...' | decisions.yml:1288 | E22 "the encoding of every file gate_plan reads (ledger copies, plan, Spec target, registers): non-UTF-8, BOM, CRLF, NUL, empty" | resolved child |
| 28 | B5-CAPS-1 | 'tools/gates/config.yml triggers hold the old agent caps ...; every reader of the triggers and every test, fixture and plan block that pins the old values follows' | none: pinned at 6a7b2f9, after B5-PROC-3's "27 at 64f7ad7" | no candidate class in any source; Q-doc-14 | Q-doc-14 |

Rows the open D33 sweep questions cover: 7-10 (B5-FIX-4 V-2, V-3, V-4, V-5, F1 only) and 12, 13, 15-27. Rows covered by answered DECs: 1-6 (DEC-0105), 10 V-1, 11, 14 (DEC-0089, DEC-0102). Not covered: 28.

## (c) DEC-0131: where owner rules live

Owner answer verbatim (decisions.yml:1397): "always-document (owner in session, 2026-10-29: \"don't jsut save to memory. always document. go\")"; the chosen option's text (:1394): "every owner rule is written into the repo's governing text (spec rows, CLAUDE.md where it is a session rule, the strict-delivery blocks where a role must follow it) through the pipeline; memory may point at it but never replaces it; DEC-0103 and DEC-0130 are documented first".

- Spec row D39 (new) and a §12 / `CLAUDE.md` bullet (hunks below). Whether every owner rule needs a spec row, or only one in the right home: Q-doc-16.
- `CLAUDE.md` is spec §12 verbatim. The test: `tools/gates/tests/test_hooks.py:991` `ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else`; it takes the §12 text between `## 12. ` and `\n## 13. ` (:994), keeps lines starting `- ` (:995), asserts the count (:996) and asserts the `CLAUDE.md` `- ` lines equal them in order (:999).
- Line to change in the same commit, `tools/gates/tests/test_hooks.py:996`: `        self.assertEqual(13, len(bullets))` becomes `        self.assertEqual(16, len(bullets))` for the three bullets of this draft (14 or 15 under the other Q-doc-3 options).
- Checked both ways on the patched clone: with the count line at 16, `python3 -W error -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else` "OK"; with the bullets added and the count line left at 13, "FAILED (failures=1)"; back to 16, "OK". The bullets go into §12 and `CLAUDE.md` at the same positions, since :999 compares the lists in order.
- No other test reads `CLAUDE.md` or §12 (`grep -rn 'CLAUDE.md\|strict-delivery-design' tools/gates --include='*.py'`: test_hooks.py:992/:997, test_gate_dispatch.py:477 (reads only the D18 row and two §5 rows), a fixture string in test_gate_plan.py:505, the reminder text in user_prompt_submit.py:9).

### The tracked diff (spec, CLAUDE.md, test_hooks.py; one commit under D34)

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 89d1b57..ffd2bf3 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -7,8 +7,11 @@ These override superpowers and every other skill. Source: `docs/superpowers/spec
 - Reviewers execute every claim and re-run every gate themselves; superpowers' "do not re-run the suite" does not apply here (`gate_review_package`).
 - Controller rulings only inside an approved plan, on plan-vs-spec or task-vs-task conflicts the spec settles, max 5 per block, listed at the checkpoint; everything else is a decision question for the owner (`gate_decisions`).
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
+- Owner rules live in the repo, never only in memory: a spec row, this file where a rule binds the session, the strict blocks where it binds a role (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test; never offer deleting a test as an option (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
index 0abd33a..95f2741 100644
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -47,6 +47,9 @@ This document designs the process that prevents each mechanism from recurring. I
 | D33 | Class restatement and idiom sweep (DEC-0088 O3, DEC-0089). D26 applies to pinned rows too. When a found defect's dimension reaches a pinned row's class, widening that class is an owner question (D10) unless a DEC already widens it (DEC-0089: B5-FIX-9 the name grammar incl. line terminators; B5-FIX-6 a non-directory at any package-tree level; B5-FIX-4 V-1 repository state that changes git output); a widened row reopens, is re-reviewed and re-pinned. The controller greps `tools/gates` for the defect's code idiom, writes the pattern and every hit into the row's siblings and registers every hit in the same commit. On adoption, the controller of the next round restates every pinned row's class by dimension once (register row B5-PROC-3), each widening an owner question unless a DEC covers it. |
 | D34 | Mechanical commit gate (DEC-0088 O4; covers B5-U3, #147). Every commit, session and text-only commits included, first passes `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` for `<py>` = `/usr/bin/python3` and `tools/.venv/bin/python3` of the main checkout (`$(git rev-parse --git-common-dir)/../tools/.venv/bin/python3` in a worktree); `PYTHONPYCACHEPREFIX` names a fresh empty directory, so no cached bytecode stands in for a source. The pre-commit hook (row B5-PROC-2) runs the set on a fresh export of the index, and only it makes the tree tested the tree committed; its D24 table carries the rows for stale bytecode (P-1), the ignore files, the attribute and filter files and the global files (DEC-0101). By hand, until it lands: commit only when `git status --porcelain --untracked-files=all` lists no unstaged and no untracked path and `git ls-files -v | grep '^[a-zS]'` lists nothing (no assume-unchanged or skip-worktree entry); the by-hand run claims no equality between the tree tested and the tree committed (DEC-0101 supersedes the DEC-0098, DEC-0099 and DEC-0100 scope clauses for this rule). The review packages under `.superpowers/sdd/*/review/` (they stay git-ignored) are in no commit: the run reads them as they are in the checkout at commit time. Any red line or a missing interpreter refuses the commit. The pre-commit hook enforces it (register row B5-PROC-2); until it lands every agent runs the set by hand before each commit, and the controller re-runs it at every child commit's sha (red: that task is incomplete). Session-written spec, `CLAUDE.md`, strict-block and plan text is applied (saved, for `~/.claude` text, which takes effect on save) or committed only after a task review of that exact diff. Items that touch the same files run serially. Order (DEC-0090): after 64f7ad7 only one commit may land on a red suite, the commit that lands this row. For it `run_all` passes, the suite fails on each interpreter only the two tests U-D-1 and R-D-1 turned red, and their remaining checks are done by hand on their inputs (`test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else`: §12 bullets equal the `CLAUDE.md` bullets; `test_gate_dispatch.GateDispatch.test_s2_plan_blocks_hold_the_dec_0060_caps`: no old-cap text added to the S2 plan); it also registers U-D-1 and R-D-1 (rows B5-UD-1 and B5-RD-1, owner task S2-b5-process-code). The next commit fixes them and makes the suite green; there is no baseline exception after it. |
 | D35 | Drafting sized like review (DEC-0088 O5). A drafter is an implementer dispatch that writes plan text. One drafter per gate table, each sized under its cap with margin as D29 sizes reviews. Every "today" cell carries the command that observed it, or reads OPEN. Reviewers number new questions per area (`Q-<area>-<n>`). Budget use is the harness count from the task notification, never the child's self-report. |
+| D37 | A deleted test is never a fix (DEC-0103). A fix never deletes a test, and a decision question never offers deleting one as an option: the code or the test is fixed. A test deleted as a fix is restored and fixed; DEC-0103 supersedes DEC-0063's drop of `test_s2_plan_blocks_hold_the_dec_0060_caps` and `test_readme_states_no_budget_cap` (row B5-RD-1 pins on the restored test). Every task review lists each test function the diff removes, with its commit and reason; a removal made as a fix is a finding of that task. |
+| D38 | Class as epic (DEC-0130). A finding's class (D26: its input dimension) is an epic: a register row whose child rows name it in their `parent` field. A defect a review finds in an epic's dimension is registered as a child of that epic. An epic is signed off (`pinned`) only when every child is `pinned`, `closed` or `merged` and the whole class is tested (a production-path test over every row of the dimension's D24 table, with its mutation transcript, D7) and reviewed (a re-review of the whole dimension, D26); `gate_register` FAILs an epic pin while a child is in any other state. A pinned row whose class a found defect widens stays pinned, as a resolved child of the epic that carries the wider class; the dimension's remaining work is new child rows (DEC-0102 successor-rows), and D33's idiom-sweep hits are registered as children of that epic. Placing a row under an epic and stating an epic's class are finding-class decisions (owner, D10). |
+| D39 | Owner rules are documented (DEC-0131). Every rule the owner gives, as a DEC answer or a statement in session, is written into the repo's governing text through the pipeline: a spec row; a `CLAUDE.md` bullet (§12, which `CLAUDE.md` carries verbatim) where the rule binds the session; and the strict block of every role that must follow it. Session memory may point at the documented rule and never replaces it; until the text lands, the rule's source is its entry in `decisions.yml`. |
 
 ## 2. Scope, layers, precedence
 
@@ -107,13 +110,14 @@ One entry per finding; the only place a finding exists.
   cites: [{file: components/core/session/ses_frame.c, lines: "60-80", sha: 7265059}]
   repro: .superpowers/reviews/2026-10-15/core-io-ui-verify-repro-n2-rekey.c
   owner: {kind: plan, plan: 2026-10-xx-prebench, task: 7}   # or {kind: issue, number: 140} or {kind: roadmap, plan: 8}
+  parent: null          # the epic's row id when this row is a child (D38), else null
   state: planned        # open | planned | in-progress | fixed-unpinned | pinned | deferred | closed | merged
   pin: {test: test_export_crc_bad_delta_fails_the_export, transcript: docs/superpowers/state/<plan>/mutations/A2-IO-N2.txt}
   ruling: null          # decision id when narrowed or deferred
   evidence: []          # commits, review report paths
   history: [{state: open, sha: 7265059, by: audit}, ...]
 ```
-Gates: every id in any audit or review report exists here; every entry has an owner; transitions are legal (`pinned` needs test + transcript + reviewer confirmation; `deferred` needs an owner decision id; `closed` needs evidence; `merged` set only by the post-merge script).
+Gates: every id in any audit or review report exists here; every entry has an owner; transitions are legal (`pinned` needs test + transcript + reviewer confirmation; `deferred` needs an owner decision id; `closed` needs evidence; `merged` set only by the post-merge script); `parent` names another existing row that has no `parent` itself, and a row other rows name as `parent` (an epic, D38) is `pinned` only while each of them is `pinned`, `closed` or `merged`.
 
 ### 4.2 `claims.yml`
 One entry per factual statement in a tracked document.
@@ -193,7 +197,7 @@ No pasted session history; a size cap on the prompt enforces it.
 3. **Plan or micro-plan.** Plans through writing-plans with added header blocks (register ids per task, sibling lists, pinning step, gate list, block boundaries). A micro-plan (one-line fix) carries the same fields in the brief, no plan file. `gate_plan` prints the file-overlap table and the id-coverage table (each routed id in exactly one task; every id from the source report somewhere) and fails on placeholders. Every gate task carries its D24 input-domain table (built per D31, D32 and D35) and D28 whitelist, approved with the plan.
 4. **Dispatch** through the hook (§5).
 5. **Implement, TDD.** Every commit at every step passes the D34 commit gate. Failing test first through the production call path; mutation transcript; every sibling tested or filed as its own row; full local gate run (host suite, dc-host, tools tests, P10 full reach, venue lint, spec-api check, all firmware builds at zero warnings, DRAM budget, comment pattern gate); report with each claim marked by the command that proves it.
-6. **Task review, executed.** Reviewer re-runs the gate set clean and the mutation; executes every claim in report, commits and diff comments (direct + contradiction); checks siblings against the class and coverage against the task's D24 table; treats every rule in a controller's brief as a hypothesis (D25); runs a contradiction pass on the new surface ("assume the fix is wrong for input X"); files new findings as register rows (from this change → task incomplete; unrelated → investigation dispatch + owner). Verdict needs spec ✅, quality ✅, claims-executed ✅.
+6. **Task review, executed.** Reviewer re-runs the gate set clean and the mutation; executes every claim in report, commits and diff comments (direct + contradiction); checks siblings against the class and coverage against the task's D24 table; treats every rule in a controller's brief as a hypothesis (D25); runs a contradiction pass on the new surface ("assume the fix is wrong for input X"); lists every test the diff removes (D37); files new findings as register rows, a defect in an epic's dimension as that epic's child (D38) (from this change → task incomplete; unrelated → investigation dispatch + owner). Verdict needs spec ✅, quality ✅, claims-executed ✅.
 7. **Fix loop.** Rounds 1–3 resume the implementer; 4–5 a fresh agent one tier up; a round limit hit is a decision question, not a parked finding. A fix covers its finding's whole dimension and its re-review the whole changed function (D26); a fix that needs a rule the plan lacks stops for the owner first (D25). Findings that keep arriving trigger D30. A found class is swept per D33, pinned rows included.
 8. **Block end** (§3.4).
 9. **Post-merge audit hook.** Files changed by a merged block become coverage rows for the next audit.
@@ -277,8 +281,11 @@ Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervisi
 - Reviewers execute every claim and re-run every gate themselves; superpowers' "do not re-run the suite" does not apply here (`gate_review_package`).
 - Controller rulings only inside an approved plan, on plan-vs-spec or task-vs-task conflicts the spec settles, max 5 per block, listed at the checkpoint; everything else is a decision question for the owner (`gate_decisions`).
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
+- Owner rules live in the repo, never only in memory: a spec row, this file where a rule binds the session, the strict blocks where it binds a role (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test; never offer deleting a test as an option (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/tools/gates/tests/test_hooks.py b/tools/gates/tests/test_hooks.py
index a386085..88df9f4 100644
--- a/tools/gates/tests/test_hooks.py
+++ b/tools/gates/tests/test_hooks.py
@@ -993,7 +993,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(16, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
```

## (d) Feedback memory against the repo text (read only)

Files: `ls ~/.claude/projects/-Users-~-projects-personal-lap-timer/memory/feedback-*.md` (18). Line numbers are at 2e43f13 (spec = `docs/superpowers/specs/2026-10-17-strict-delivery-design.md`) or the installed prompt files.

| Memory file | Rule (one line) | Repo text that documents it, or "undocumented" + proposed home |
|---|---|---|
| feedback-always-document.md | owner rules go into the repo's governing text; memory only points at them (DEC-0131) | undocumented; proposed home: spec D39 + §12 bullet (this draft, (c)) |
| feedback-audit-style.md | audits are strict: every claim is a hypothesis, falsify it by contradiction, re-run gates | spec :17 D3 "No claim in any document is trusted. Every claim is verified before execution by a direct check and by proof by contradiction"; `CLAUDE.md:6` "No claim in any document is trusted; verify by direct check and proof by contradiction before acting (`gate_claims`)." |
| feedback-checkpoints.md | plans carry checkpoints: session blocks of at most ~4 tasks, and a CHECKPOINT step after every task (ledger line `Task N complete <sha>` + pushed branch + exact resume prompt) | three parts. (1) Block size: spec :76 §3.2 "A plan is sliced into blocks of at most 3 tasks."; `CLAUDE.md:15` "Block = PR, ≤3 tasks"; the memory's "~4" is superseded (Notes N5). (2) Checkpoint content: spec :36 D22 and §3.3 :78-81 (status update, handoff, resume prompt), which are per block or per trigger ("triggers = 3 tasks / 250k tokens / 90 min for a controller, 1 task / 150k for an implementer"), not a CHECKPOINT step after every task. (3) The per-task ledger line and the push: undocumented (no spec line names a per-task ledger line; the six `push` hits in the spec are the hook matcher and the merge sequence); proposed home: §3.3 (Q-doc-17) |
| feedback-class-as-epic.md | a class is an epic with child rows; signed off only when every child is resolved and the whole class tested and reviewed (DEC-0130) | undocumented; proposed home: spec D38 + §4.1 + §12 bullet + reviewer/implementer blocks (this draft, (b)) |
| feedback-core-selftest-gate.md | every firmware gate list builds `test_apps/core_selftest`; host-only tests go in its exclude regex | partly: `.github/workflows/firmware.yml:28` "- name: Build core_selftest (ESP32)" and :35 "Fail if core_selftest excludes a test/test_*.c with no reason in the CMake comment"; `docs/superpowers/specs/2026-09-14-lap-timer-design.md:2618` "`test_apps/core_selftest` also runs the host unit-test files on the ESP32". The gate-list rule itself is undocumented (spec :195 §6 step 5 says only "all firmware builds at zero warnings"); proposed home: spec §6 step 5 naming the core_selftest build (Q-doc-17) |
| feedback-fix-minors.md | review minors that are bugs or missing tests are fixed in the fix round, never deferred | spec :20 D6 "no \"minor\" carve-out"; `CLAUDE.md:10` "Zero bugs: a change's own bug makes it incomplete"; implementer.md:13 "there is no \"minor\" and no \"follow-up\"" |
| feedback-flash-ready.md | never flash without the owner's "ready" (the owner holds BOOT) | `CLAUDE.md:17` "Hardware: never flash without the owner's explicit \"ready\""; implementer.md:17 "Hardware: never flash, never open a serial port." |
| feedback-merge-authorization.md | the classifier denies `gh pr merge` without the owner's chat sentence "merge when all checks pass"; ask for it, run the merge as its own command | undocumented (`git grep -n -i 'merge when\|Merge Without Review'`: no hit in docs); proposed home: spec §3.4 merge sequence sentence (session rule; a §12 bullet is the alternative) (Q-doc-17) |
| feedback-no-test-deletion-fix.md | deleting a test is never a fix; restore it and fix the code or the test (DEC-0103) | undocumented; proposed home: spec D37 + §12 bullet + implementer/reviewer blocks (this draft, (a)) |
| feedback-parallel-tasks.md | plans run disjoint-file tasks in parallel worktrees between serial scaffold and integration | spec :76 §3.2 "Parallel tasks inside a block run in worktrees only when `gate_plan` confirms disjoint file sets; each gets its own reviewer; integration into the block branch is itself a reviewed task with a full gate run." |
| feedback-prefer-idf-builtins.md | firmware uses ESP-IDF built-in components over hand-rolled code | partly (plan 03): `docs/superpowers/plans/2026-09-14-plan-03-firmware-skeleton.md:660` "Uses ESP-IDF built-ins per the user's directive: `nvs`, `esp_console`+`linenoise`, `esp_task_wdt`, ..." states it for plan 03 only; the other hits of `git grep -n -i 'IDF built-in\|built-in component'` (README.md:16, components/app/CMakeLists.txt:4, plan-03 :674/:728, fix-F4 :1833) are about component-name collisions; no spec states the rule; proposed home: a row in `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (firmware rule, not a strict-delivery rule) (Q-doc-17) |
| feedback-process-over-bug-chasing.md | non-converging reviews are a process defect: investigate, bring process changes | spec :44 D30; `CLAUDE.md:11` "Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes." |
| feedback-reap-background-jobs.md | every agent reaps its background jobs before reporting; the controller checks for orphans at hand-back | strict blocks, all 8 files (`grep -l 'Background work'` = 8), e.g. implementer.md:21 "A job you start in the background is yours until it exits". The controller's orphan check at a child's hand-back is undocumented (controller.md has only its own "Background work." line :25); proposed home: controller block (bytes: Q-doc-4 table) (Q-doc-17) |
| feedback-report-before-plan.md | after an audit, report and let the owner rule before any plan | spec :24 D10 "owner-only decisions are written as questions with options and cost"; `CLAUDE.md:9` "Owner-only: triage, spec direction, plan scope, ...". "Report first, never commit a plan the owner has not seen" is not stated as such; proposed home: spec D10 clause (Q-doc-17) |
| feedback-scratch-script-guard.md | probe scripts use `set -e` and assert the path before any write; push the block branch early | reviewer.md:9 "Writes: `set -eu`; stop unless `pwd -P` and the git toplevel are in your scratch clone (DEC-0032)." (also verifier, investigator, exception-reviewer, audit-reviewer, audit-verifier: `grep -c DEC-0032` = 1 each). Undocumented: the guard in implementer.md and controller.md (0 each), and "push the block branch early"; proposed home: implementer block for the guard, spec §3.2 for the push (Q-doc-17) |
| feedback-session-commits-gated.md | session text commits pass run_all + both suites + a review first | spec :48 D34; `CLAUDE.md:12` "Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34 ...)" |
| feedback-subagent-models.md | Fable only for the final verifier and the exception reviewer; others per config.yml; always an explicit model | spec :32 D18; `CLAUDE.md:14` "Model map by role (§5); never dispatch outside it (`gate_dispatch`)." |
| feedback-zero-warnings.md | zero compiler warnings, fixed at source, no `-Wno-*`/pragma; audit with ccache off | spec :195 §6 step 5 "all firmware builds at zero warnings"; `CLAUDE.md:16` "Power of 10 on every tree, zero grace"; `docs/power-of-10-checklist.md:48` "Rule 10 — zero warnings, multiple analysers." The no-suppression and ccache-off parts are undocumented; proposed home: `docs/power-of-10-checklist.md` rule 10 (Q-doc-17; see Unrelated U1) |

## Q-doc list (owner questions; every open choice in this draft)

- **Q-doc-1** D-row numbers. Staged D36 (`b5r15-budget/spec.patch`, DEC-0122) is not on HEAD. (a) D37-D39 as drafted; D36 lands later with its own number (cost: a gap D36 at HEAD until it lands; and the staged `b5r15-budget/spec.patch` no longer applies once this draft lands: `patch --dry-run` on the patched spec prints "1 out of 2 hunks failed" (its D36 insertion after D35), so the staged D36 text needs a rebase and a re-review; none of this if D36 lands first). (b) D36-D38 now and D36-budget renumbered D39 (cost: the staged spec.patch, controller-staged.md and DEC-0122..0124 references to "D36" all change; a re-review of the staged text).
- **Q-doc-2** DEC-0103 scope. (a) deleted tests only, the owner's words (cost: a test weakened as a fix, e.g. an assertion dropped or a skip added, is not covered). (b) deleted or weakened, with a closed definition (assertion removed, `skip`/`expectedFailure` added, expected value loosened, input narrowed) (cost: a definition text round; "loosened" and "narrowed" need reviewer judgment, only deletion and skip are greppable).
- **Q-doc-3** Which bullets go into §12/`CLAUDE.md`. (a) all three (D37, D38, D39; count 16) (cost: CLAUDE.md grows by three lines). (b) D39 only (count 14; D37 and D38 bind roles, not the session) (cost: the session's own decision questions are not told D37 in CLAUDE.md). (c) D37 and D39 (count 15) (cost: CLAUDE.md grows by two lines; the session is not told D38 in CLAUDE.md, only the spec and the role blocks carry it). (d) D38 and D39 (count 15) (cost: CLAUDE.md grows by two lines; the session's own decision questions are not told D37 in CLAUDE.md). Every option changes three lines in the same commit (Fix round 1, F1).
- **Q-doc-4** Which strict blocks carry D37/D38. (a) implementer and reviewer (the brief) (cost: the final verifier and the controller are covered only by the spec). (b) plus verifier (+241 bytes, 4580). (c) plus controller (+109 bytes: 5390 live, 5609 on staged D36, over the 5500 cap: needs a trim elsewhere or a DEC-0095 change).
- **Q-doc-5** Parent link schema. (a) `parent` required on every row (cost: one migration commit adds `parent: null` to all 79 rows and every register fixture and test-built row; registers.py:34 `FINDING_KEYS`). (b) `parent` optional (cost: `_keys` gets its first optional key, a validator change). (c) epic-side `children: [ids]` (cost: two edits per new child, the epic row and the child; a child can sit in two lists unless a check forbids it).
- **Q-doc-6** Epic marker. (a) implicit: a row named as someone's `parent` (cost: a widened class with no child yet cannot be an epic). (b) explicit `kind: epic|finding` (cost: one more key, a migration like Q-doc-5 a).
- **Q-doc-7** Resolved set for G3. (a) `pinned`, `closed`, `merged` (cost: a deferred child blocks its epic forever). (b) plus `deferred` with its owner DEC (cost: an epic signs off with known open work).
- **Q-doc-8** A new child under a pinned epic (G6). (a) add `pinned -> open` for epics only (cost: code in `FINDING_TRANSITIONS`, the evidence rule for a reopened epic; DEC-0102 chose no transition code for rows). (b) the pinned epic stays closed to new children; a successor epic with the same dimension cites it and takes the child (cost: epic chains; the class's sign-off is the latest successor). (c) G3 checks only at the history step that sets `pinned` (cost: a pinned epic can hold an open child and only `gate_merge_ready` (S4, not built) would catch it).
- **Q-doc-9** Whole-class test and review for the epic pin (G4). (a) one pin test as today, plus the reviewer's report attesting that every D24 row of the dimension is covered (cost: coverage is reviewer-checked, not gate-checked). (b) `pin.test` becomes a list, one test per D24 row (cost: schema and `PIN_TEST` change; many dimensions have no D24 table yet: D24 tables are per gate task). (c) the epic's review package maps each D24 row to a test and gate_review_package checks the map (cost: new package file and gate code).
- **Q-doc-10** Rows with parts in different dimensions (B4-FIX-1, B5-FIX-3, B5-FIX-4; also the E2/E5 overlap). (a) split each into one successor row per part, the original stays pinned (cost: one new row per part, owner tasks per DEC-0104). (b) `parent` takes a list (cost: G2/G3 per parent; a child resolves several epics). (c) the whole row goes under one epic chosen by the owner (cost: the other parts' dimensions get no epic).
- **Q-doc-11** Epic depth. (a) one level (cost: a wide class cannot group sub-dimensions). (b) nested, with a cycle check (cost: recursive G3 and a cycle check).
- **Q-doc-12** D33's "a widened row reopens, is re-reviewed and re-pinned" (spec :47). (a) edit it in the same commit (the optional hunk) (cost: this is the text of DEC-0102 option amend-d33, which the owner did not pick). (b) leave D33 as it is (cost: D33 :47 "a widened row reopens, is re-reviewed and re-pinned" keeps contradicting the answered DEC-0102 successor-rows (N1); the commit lands a spec that states a rule the register cannot follow, a spec defect under D6, until a later commit edits it). (c) edit D33 to the answered DEC-0102 words only: "a widened row stays pinned; a successor row carries the widened dimension and cites the pinned row (DEC-0102)" (cost: one more reviewed spec sentence; it says nothing about epics, so D38's mechanics stay for the approved design).
- **Q-doc-13** Requirement rows SD-S2-T1..T6 (DEC-0105 keep-requirement-rows: "their domain is the task's D24 table already"). (a) outside epics (cost: none). (b) each is the epic of its task's table and the fix rows in that task's code become children (cost: pinned epics with open children at once, e.g. B5-FIX-5 under SD-S2-T6: G3 FAILs; needs Q-doc-8 first).
- **Q-doc-14** B5-CAPS-1 (no sweep question covers it). (a) a new D33 sweep question for it (cost: one more owner question). (b) its class already names a dimension ("every reader of the triggers and every test, fixture and plan block that pins the old values"): make it an epic as worded, with B5-CAPS-2 (open) and B5-U9 (planned) as candidate children (cost: B5-CAPS-1 is pinned, so G3 FAILs at once; needs Q-doc-8). (c) leave it outside epics.
- **Q-doc-15** The 16 open sweep questions (decisions.yml:1028-:1288). (a) approval of the mapping table above answers each (widen = the epic placement; keep = no epic) (cost: one approval; the table must be reviewed first). (b) each is re-asked in epic terms on a decision sheet (cost: one more sheet round). (c) they stay as worded and are answered first, then mapped (cost: two passes).
- **Q-doc-16** D39's "a spec row" for every owner rule. (a) always a spec row, plus CLAUDE.md/blocks where they bind (cost: the spec grows with every rule). (b) the one home where it binds (spec for process, CLAUDE.md for a session rule, a block for a role rule; the firmware spec for firmware rules) (cost: a rule in a block only has no spec row to cite).
- **Q-doc-17** Homes for the undocumented memory rules in (d): core_selftest in §6 step 5; merge authorization in §3.4 (or a §12 bullet); IDF built-ins in the lap-timer spec; controller orphan check in the controller block; report-before-plan in D10; DEC-0032 guard in the implementer block and early push in §3.2; zero-warnings detail in the P10 checklist. (a) one text round documenting all of them under D39 (cost: one drafting + review round; the controller block has 219 bytes live and 0 staged). (b) only the strict-delivery ones now, firmware rules (IDF built-ins, zero-warnings detail, core_selftest) with the next firmware plan (cost: memory stays their only home until then). (c) the owner picks per rule (cost: seven owner answers before any text; each chosen home is its own drafting and review round, up to seven rounds instead of one or two).

## Notes (divergences, both quotes)

- **N1** D33 vs DEC-0102. Spec :47 D33: "a widened row reopens, is re-reviewed and re-pinned." DEC-0102 chosen option successor-rows (:961-963): "the three rows stay pinned; three new open rows (R-B-13, R-A-5, R-B-1) carry the widened dimensions ... DEC-0089's 'rows reopen' is not done as worded". Code agrees with DEC-0102 (registers.py:16 `"pinned": ["closed", "merged"]`). Q-doc-12.
- **N2** B5-PROC-3 count. B5-PROC-3 class: "every pinned row (27 at 64f7ad7)". HEAD has 28 pinned rows; the extra is B5-CAPS-1 (pinned at 6a7b2f9). Q-doc-14.
- **N3** DEC-0102 answered at 6261372, but its three successor rows are not registered at HEAD (no row's id or class names R-A-5 or R-B-1; R-B-13 appears only in B5-ID-1 and B5-ID-2's classes, which are idiom-sweep rows, not the successor). The brief's item list for round 17 does not name registering them.
- **N4** The brief says "deleted or weakened"; DEC-0103's answer says "delete" only. Q-doc-2.
- **N5** feedback-checkpoints.md: "tasks grouped into session blocks of at most ~4 tasks"; spec :76: "blocks of at most 3 tasks". The spec is the later, governing text.
- **N6** DEC-0130's answer names the open sweep questions by id (redacted in the quote above); this draft cites those questions by decisions.yml line only, per the dispatch.
- **N7** Staged D36 controller-staged.md is exactly 5500 bytes: any controller sentence (D38, D39 or the orphan check of Q-doc-17) pushes it over the DEC-0095 cap.

## Unrelated findings (for the controller to register with an owner)

- **U1** `docs/power-of-10-checklist.md:48-49`: "Clean under `-Wall -Wextra -Werror -Wshadow` (+ `-Wconversion` non-fatal)"; the memory (feedback-zero-warnings.md) says the softening was removed in PR #72 (e445978), and `git grep -l 'Wno-error' -- '*CMakeLists.txt' | wc -l` = 0. Reproducer: the two commands. Class: a governing checklist states a build flag policy the tree no longer has (stale text). Not caused by this draft.

## Fix round 1 (review b5r17-docs-rev.md, F1-F10)

Probes ran in the clone `scratchpad/r17-docsfix/repo` at 2e43f13 under the DEC-0032 guard (`set -eu`; `pwd -P` and `git rev-parse --show-toplevel` asserted under `r17-docsfix/` and not the checkout). Prompt copies in `r17-docsfix/pa` (installed, sha256 prefixes implementer d13756d2, reviewer 450fb197, verifier f3127b68, controller b0fdc5ed, staged controller efe8056f) and `r17-docsfix/pb` (patched). No `~/.claude` file was written.

| Finding | Change | Evidence |
|---|---|---|
| F1 | Every place that hard-codes the §12 bullet count is in the same-commit list below: test_hooks.py:996, S1 plan :6855, mutations/B5-UD-1.txt:4 (its step-2 revert rewritten to a count-free pattern). The count is per Q-doc-3 option (14/15/16), so it sits in the option diffs, not the core diff. | `/usr/bin/grep -rlE 'assertEqual.?.?1[0-9], len.?.?bullets' docs tools CLAUDE.md .superpowers ~/.claude/skills/strict-delivery`: tracked hits test_hooks.py, S1 plan, mutations/B5-UD-1.txt, reviews/S2-b5-caps.md (:35-36, :41-42, a review record of 6a7b2f9: no change); workspace hits are past review diffs, briefs and reports plus the S2-b5-caps package diff.patch (a fixed commit range): no change. Transcripts naming CLAUDE.md, §12 or test_hooks: B5-UD-1, SD-S2-T2, B4-FIX-1; only B5-UD-1 reads the count (the other two test AgentDispatchHook). Old step 2 on option (a): perl changes nothing (`git diff --stat` empty). New step 2: "AssertionError: 11 != 16 / FAILED (failures=1)"; step 1 and 3 "OK". Core alone (count 13): "FAILED (failures=1)"; each option a-d: "OK". |
| F2 (a) | "a decision question never offers deleting one as an option" is out of D37 and the §12 bullet; it is option text of new Q-doc-20. | `sed -n 972,990p decisions.yml`: DEC-0103's answer says only "we cannot simply delete a test to \"fix\" something ... it needs to be readded and fixed"; `/usr/bin/grep -n -iE 'delet[a-z]* .{0,40}option\|option.{0,40}delet' decisions.yml spec CLAUDE.md`: no hit. |
| F2 (b) | "until the text lands, the rule's source is its entry in decisions.yml" and "as a DEC answer or a statement in session" are out of D39; D39 now quotes DEC-0131's chosen option (:1394) for the homes. The interim source is new Q-doc-21. | DEC-0131 option (:1394): "every owner rule is written into the repo's governing text (spec rows, CLAUDE.md where it is a session rule, the strict-delivery blocks where a role must follow it) through the pipeline; memory may point at it but never replaces it"; DEC-0131 (:1390-:1398) names no interim source: `sed -n 1390,1398p decisions.yml \| grep -ci 'until\|source'` = 0. |
| F3 | The tracked text now carries only owner-given rules (DEC-0103, DEC-0130, DEC-0131 words). The `parent` field, the resolved set, the depth rule, the pinned-only-while invariant, the D24-table test form and the "stays pinned as a resolved child" sentence are out of D38 and §4.1 (the §4.1 hunk is gone) and are written as option text in the table "Text moved out of the tracked diff" below, each under its Q-doc. The same applies to D39's "a spec row" (Q-doc-16, found in this round: the old D39 text picked Q-doc-16 (a)) and to the §12 bullet set (Q-doc-3: the core diff holds only the D39 bullet, which every Q-doc-3 option includes; the D37 and D38 bullets are option diffs). | Added lines of the core diff below: `grep '^+' core.diff \| grep -c 'parent\|`pinned`, `closed`\|D24 table'` = 1, and that hit is the existing §6 step 6 phrase "coverage against the task's D24 table" on the changed line. |
| F4 | D38 and the §12 bullet keep the owner's words ("a class ... is an epic with child rows"); the identity of the row that carries a class's epic, and what a defect does when no epic row exists, are new Q-doc-18. The reviewer and verifier sentence no longer says "that class's epic": "reported as a child of that class, naming the row". The implementer sentence no longer says "a row other rows name as parent". | DEC-0130 answer (:1388): "a class can be seen as an epic that has child rows or tickets. when reviews find bugs, then the bugs fall under that class"; no line says which row carries it. |
| F5 | Q-doc-1 (a)'s cost now names the staged spec.patch rebase and re-review (edited in place). | `patch --dry-run <spec> < b5r15-budget/spec.patch`: at HEAD "patching file" rc 0; on the core diff of this round "1 out of 2 hunks failed" rc 1 (the same as on the r0 diff). |
| F6 (a) | New Q-doc-18 offers "the DEC-0102 successor row is the epic and the pinned row its resolved child". | DEC-0102 successor-rows (:961-963): "three new open rows (R-B-13, R-A-5, R-B-1) carry the widened dimensions and cite the pinned rows". |
| F6 (b) | New Q-doc-19: owner task and review package of a new epic row. | registers.py:34 `FINDING_KEYS` holds `owner`; DEC-0104 question (:991-997): "gate_review_package requires a mutation-<id>.txt in an existing package for every row its task owns". |
| F6 (c) | Q-doc-12 (b)'s cost now names the spec defect it lands (edited in place); option (c) "D33 in DEC-0102's answered words" added. The optional D33 hunk of r0 is withdrawn: its "carried by an epic (D38, DEC-0102)" picks Q-doc-18. | spec :47 "a widened row reopens, is re-reviewed and re-pinned" vs DEC-0102 :961-963 "the three rows stay pinned". |
| F7 | Partly shown false, partly fixed. False part: "F2-F6, V1 and DEC-0012 are B4-FIX-1's siblings, not its class". B4-FIX-1's `class` holds "with it the final-review minors F2-F6 and the verifier's finding 1 (...) and DEC-0012 = require-equal (...)"; they are also its `siblings`. True part: the r0 quote "plus F2-F6, V1, DEC-0012" is the question's wording (decisions.yml:1031), not the class, and the question proposes one dimension. Mapping row 7 quotes the class verbatim now; M4 and row 7's effect say the one proposed dimension does not reach those parts (edited in place). | `yaml.safe_load` of `git show HEAD:docs/superpowers/state/findings.yml`, row B4-FIX-1 `class` and `siblings` printed; `sed -n 1028,1045p decisions.yml`: one widen option, one proposed class. |
| F8 | IDF row reads "partly (plan 03)" with plan-03 :660 quoted and the other hits classed (edited in place). | `git grep -n -i 'IDF built-in\|built-in component'`: README.md:16, components/app/CMakeLists.txt:4, plan-03 :660/:674/:728, fix-F4 :1833; lap-timer-design.md:261 is not a hit (`git grep -n 'built-in' <that spec>` prints :261, whose line ends "ships a built-in"). |
| F9 | Checkpoints row split into block size, checkpoint content and the per-task ledger line plus push (edited in place); "open row B5-UR-13" now "row B5-UR-13 (state `planned`)". | memory: "a CHECKPOINT step after every task (ledger line + pushed commit + exact resume prompt)"; spec :36 D22 triggers quote; `grep -c -i push <spec>` = 6, none a per-task rule; B5-UR-13 `state: planned` (yaml load at HEAD). |
| F10 | Q-doc-3 (c) and Q-doc-17 (c) carry costs; Q-doc-3 gains (d) (edited in place). | the two Q-doc lines above. |

### Same-commit list for the §12 bullet count (F1)

Whichever Q-doc-3 option is chosen, N = the §12 bullet count (a: 16, b: 14, c: 15, d: 15), and one commit changes all of:
1. `tools/gates/tests/test_hooks.py:996` `self.assertEqual(13, len(bullets))` -> `self.assertEqual(N, len(bullets))`.
2. `docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md:6855`, the same line (the plan copy; precedent 6a7b2f9 changed both).
3. `docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt:4`: step 2's revert `s/self\.assertEqual\(13, len\(bullets\)\)/` -> `s/self\.assertEqual\([0-9]+, len\(bullets\)\)/` and "(AssertionError: 11 != 13)" -> "(AssertionError: 11 != N, N the count of section 12 bullets)". A count-free pattern means a later §12 change does not break the transcript again; the other form (name the new count) needs this line in every later §12 commit. B5-UD-1 is `planned` (not yet pinned, DEC-0103), so no pinned transcript changes.
4. No change: `reviews/S2-b5-caps.md:35-36, :41-42` (record of the 6a7b2f9 review) and the workspace hits (records).

### The tracked diff, revised (core: applies under every option)

Checked on the clone: core + option (a) applied and committed (scratch commit, signing off in the clone only): `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=<fresh> python3 tools/gates/run_all.py` rc 0, 100 lines, 0 not PASS; `/usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` "Ran 658 tests in 144.061s / OK (skipped=1)"; main checkout `tools/.venv/bin/python3` "Ran 658 tests in 155.661s / OK (skipped=1)"; `git status --porcelain --untracked-files=all` and `git ls-files -v | grep '^[a-zS]'` empty. Options (b), (c), (d) on the core: `test_claude_md_carries_spec_section_12_verbatim_and_nothing_else` "OK" each (no other test reads §12 or CLAUDE.md: r0 (c) grep); the full set was run on (a) only.

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 89d1b57..7c6f318 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -7,6 +7,7 @@ These override superpowers and every other skill. Source: `docs/superpowers/spec
 - Reviewers execute every claim and re-run every gate themselves; superpowers' "do not re-run the suite" does not apply here (`gate_review_package`).
 - Controller rulings only inside an approved plan, on plan-vs-spec or task-vs-task conflicts the spec settles, max 5 per block, listed at the checkpoint; everything else is a decision question for the owner (`gate_decisions`).
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
+- Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
diff --git a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
index 0abd33a..fc3cc1d 100644
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -47,6 +47,9 @@ This document designs the process that prevents each mechanism from recurring. I
 | D33 | Class restatement and idiom sweep (DEC-0088 O3, DEC-0089). D26 applies to pinned rows too. When a found defect's dimension reaches a pinned row's class, widening that class is an owner question (D10) unless a DEC already widens it (DEC-0089: B5-FIX-9 the name grammar incl. line terminators; B5-FIX-6 a non-directory at any package-tree level; B5-FIX-4 V-1 repository state that changes git output); a widened row reopens, is re-reviewed and re-pinned. The controller greps `tools/gates` for the defect's code idiom, writes the pattern and every hit into the row's siblings and registers every hit in the same commit. On adoption, the controller of the next round restates every pinned row's class by dimension once (register row B5-PROC-3), each widening an owner question unless a DEC covers it. |
 | D34 | Mechanical commit gate (DEC-0088 O4; covers B5-U3, #147). Every commit, session and text-only commits included, first passes `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` for `<py>` = `/usr/bin/python3` and `tools/.venv/bin/python3` of the main checkout (`$(git rev-parse --git-common-dir)/../tools/.venv/bin/python3` in a worktree); `PYTHONPYCACHEPREFIX` names a fresh empty directory, so no cached bytecode stands in for a source. The pre-commit hook (row B5-PROC-2) runs the set on a fresh export of the index, and only it makes the tree tested the tree committed; its D24 table carries the rows for stale bytecode (P-1), the ignore files, the attribute and filter files and the global files (DEC-0101). By hand, until it lands: commit only when `git status --porcelain --untracked-files=all` lists no unstaged and no untracked path and `git ls-files -v | grep '^[a-zS]'` lists nothing (no assume-unchanged or skip-worktree entry); the by-hand run claims no equality between the tree tested and the tree committed (DEC-0101 supersedes the DEC-0098, DEC-0099 and DEC-0100 scope clauses for this rule). The review packages under `.superpowers/sdd/*/review/` (they stay git-ignored) are in no commit: the run reads them as they are in the checkout at commit time. Any red line or a missing interpreter refuses the commit. The pre-commit hook enforces it (register row B5-PROC-2); until it lands every agent runs the set by hand before each commit, and the controller re-runs it at every child commit's sha (red: that task is incomplete). Session-written spec, `CLAUDE.md`, strict-block and plan text is applied (saved, for `~/.claude` text, which takes effect on save) or committed only after a task review of that exact diff. Items that touch the same files run serially. Order (DEC-0090): after 64f7ad7 only one commit may land on a red suite, the commit that lands this row. For it `run_all` passes, the suite fails on each interpreter only the two tests U-D-1 and R-D-1 turned red, and their remaining checks are done by hand on their inputs (`test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else`: §12 bullets equal the `CLAUDE.md` bullets; `test_gate_dispatch.GateDispatch.test_s2_plan_blocks_hold_the_dec_0060_caps`: no old-cap text added to the S2 plan); it also registers U-D-1 and R-D-1 (rows B5-UD-1 and B5-RD-1, owner task S2-b5-process-code). The next commit fixes them and makes the suite green; there is no baseline exception after it. |
 | D35 | Drafting sized like review (DEC-0088 O5). A drafter is an implementer dispatch that writes plan text. One drafter per gate table, each sized under its cap with margin as D29 sizes reviews. Every "today" cell carries the command that observed it, or reads OPEN. Reviewers number new questions per area (`Q-<area>-<n>`). Budget use is the harness count from the task notification, never the child's self-report. |
+| D37 | A deleted test is never a fix (DEC-0103). A fix never deletes a test: the code or the test is fixed. A test deleted as a fix is restored and fixed; DEC-0103 supersedes DEC-0063's drop of `test_s2_plan_blocks_hold_the_dec_0060_caps` and `test_readme_states_no_budget_cap` (row B5-RD-1 pins on the restored test; row B5-UD-1 pins on `test_claude_md_carries_spec_section_12_verbatim_and_nothing_else`). Every task review lists each test function the diff removes, with its commit and reason; a removal made as a fix is a finding of that task. |
+| D38 | Class as epic (DEC-0130). A finding's class (D26: its input dimension) is an epic with child rows. A defect a review finds in a class's dimension is registered as a child row of that class. The class is signed off only when every child is resolved and the whole class is tested and reviewed. Stating or widening a class stays a finding-class decision (owner, D10, D33). The register and gate mechanics and the mapping of the pinned rows come back to the owner for approval before any code (DEC-0130). |
+| D39 | Owner rules are documented (DEC-0131). Every owner rule is written into the repo's governing text through the pipeline (spec rows, `CLAUDE.md` where it is a session rule, the strict-delivery blocks where a role must follow it); `CLAUDE.md` carries §12 verbatim. Session memory may point at a documented rule but never replaces it. |
 
 ## 2. Scope, layers, precedence
 
@@ -193,7 +196,7 @@ No pasted session history; a size cap on the prompt enforces it.
 3. **Plan or micro-plan.** Plans through writing-plans with added header blocks (register ids per task, sibling lists, pinning step, gate list, block boundaries). A micro-plan (one-line fix) carries the same fields in the brief, no plan file. `gate_plan` prints the file-overlap table and the id-coverage table (each routed id in exactly one task; every id from the source report somewhere) and fails on placeholders. Every gate task carries its D24 input-domain table (built per D31, D32 and D35) and D28 whitelist, approved with the plan.
 4. **Dispatch** through the hook (§5).
 5. **Implement, TDD.** Every commit at every step passes the D34 commit gate. Failing test first through the production call path; mutation transcript; every sibling tested or filed as its own row; full local gate run (host suite, dc-host, tools tests, P10 full reach, venue lint, spec-api check, all firmware builds at zero warnings, DRAM budget, comment pattern gate); report with each claim marked by the command that proves it.
-6. **Task review, executed.** Reviewer re-runs the gate set clean and the mutation; executes every claim in report, commits and diff comments (direct + contradiction); checks siblings against the class and coverage against the task's D24 table; treats every rule in a controller's brief as a hypothesis (D25); runs a contradiction pass on the new surface ("assume the fix is wrong for input X"); files new findings as register rows (from this change → task incomplete; unrelated → investigation dispatch + owner). Verdict needs spec ✅, quality ✅, claims-executed ✅.
+6. **Task review, executed.** Reviewer re-runs the gate set clean and the mutation; executes every claim in report, commits and diff comments (direct + contradiction); checks siblings against the class and coverage against the task's D24 table; treats every rule in a controller's brief as a hypothesis (D25); runs a contradiction pass on the new surface ("assume the fix is wrong for input X"); lists every test the diff removes (D37); files new findings as register rows, a defect found in a register row's class dimension as a child row of that class (D38) (from this change → task incomplete; unrelated → investigation dispatch + owner). Verdict needs spec ✅, quality ✅, claims-executed ✅.
 7. **Fix loop.** Rounds 1–3 resume the implementer; 4–5 a fresh agent one tier up; a round limit hit is a decision question, not a parked finding. A fix covers its finding's whole dimension and its re-review the whole changed function (D26); a fix that needs a rule the plan lacks stops for the owner first (D25). Findings that keep arriving trigger D30. A found class is swept per D33, pinned rows included.
 8. **Block end** (§3.4).
 9. **Post-merge audit hook.** Files changed by a merged block become coverage rows for the next audit.
@@ -277,6 +280,7 @@ Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervisi
 - Reviewers execute every claim and re-run every gate themselves; superpowers' "do not re-run the suite" does not apply here (`gate_review_package`).
 - Controller rulings only inside an approved plan, on plan-vs-spec or task-vs-task conflicts the spec settles, max 5 per block, listed at the checkpoint; everything else is a decision question for the owner (`gate_decisions`).
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
+- Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
```

Option diffs (exactly one, on top of the core; Q-doc-3). Option (a), D37 and D38 bullets, N = 16:

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 7c6f318..0885f5d 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -9,7 +9,9 @@ These override superpowers and every other skill. Source: `docs/superpowers/spec
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
index c045e6a..e7186ba 100644
--- a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
+++ b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
@@ -6852,7 +6852,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(16, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
diff --git a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
index fc3cc1d..601936e 100644
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -282,7 +282,9 @@ Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervisi
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
index 09afe8c..d25a187 100644
--- a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
+++ b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
@@ -1,5 +1,5 @@
 finding: B5-UD-1
 test: test_hooks::test_claude_md_carries_spec_section_12_verbatim_and_nothing_else
 step 1 (fix present): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
-step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\(13, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != 13), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
+step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\([0-9]+, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != N, N the count of section 12 bullets), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
 step 3 (fix restored): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
diff --git a/tools/gates/tests/test_hooks.py b/tools/gates/tests/test_hooks.py
index a386085..88df9f4 100644
--- a/tools/gates/tests/test_hooks.py
+++ b/tools/gates/tests/test_hooks.py
@@ -993,7 +993,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(16, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
```

Option (b), no extra bullet, N = 14:

```diff
diff --git a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
index c045e6a..3bf88f7 100644
--- a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
+++ b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
@@ -6852,7 +6852,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(14, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
diff --git a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
index 09afe8c..d25a187 100644
--- a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
+++ b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
@@ -1,5 +1,5 @@
 finding: B5-UD-1
 test: test_hooks::test_claude_md_carries_spec_section_12_verbatim_and_nothing_else
 step 1 (fix present): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
-step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\(13, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != 13), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
+step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\([0-9]+, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != N, N the count of section 12 bullets), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
 step 3 (fix restored): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
diff --git a/tools/gates/tests/test_hooks.py b/tools/gates/tests/test_hooks.py
index a386085..da9e2e1 100644
--- a/tools/gates/tests/test_hooks.py
+++ b/tools/gates/tests/test_hooks.py
@@ -993,7 +993,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(14, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
```

Option (c), D37 bullet, N = 15:

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 7c6f318..928e983 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -9,6 +9,7 @@ These override superpowers and every other skill. Source: `docs/superpowers/spec
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
diff --git a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
index c045e6a..ad17281 100644
--- a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
+++ b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
@@ -6852,7 +6852,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(15, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
diff --git a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
index fc3cc1d..66c58c8 100644
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -282,6 +282,7 @@ Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervisi
 - Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
+- A deleted test is never a fix: restore it and fix the code or the test (spec D37).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
diff --git a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
index 09afe8c..d25a187 100644
--- a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
+++ b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
@@ -1,5 +1,5 @@
 finding: B5-UD-1
 test: test_hooks::test_claude_md_carries_spec_section_12_verbatim_and_nothing_else
 step 1 (fix present): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
-step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\(13, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != 13), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
+step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\([0-9]+, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != N, N the count of section 12 bullets), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
 step 3 (fix restored): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
diff --git a/tools/gates/tests/test_hooks.py b/tools/gates/tests/test_hooks.py
index a386085..c21d134 100644
--- a/tools/gates/tests/test_hooks.py
+++ b/tools/gates/tests/test_hooks.py
@@ -993,7 +993,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(15, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
```

Option (d), D38 bullet, N = 15:

```diff
diff --git a/CLAUDE.md b/CLAUDE.md
index 7c6f318..abc4b92 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -10,6 +10,7 @@ These override superpowers and every other skill. Source: `docs/superpowers/spec
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
index c045e6a..ad17281 100644
--- a/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
+++ b/docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
@@ -6852,7 +6852,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(15, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
diff --git a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
index fc3cc1d..2e4da87 100644
--- a/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
+++ b/docs/superpowers/specs/2026-10-17-strict-delivery-design.md
@@ -283,6 +283,7 @@ Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervisi
 - Owner rules are written into the repo's governing text (spec rows, this file where a rule binds the session, the strict blocks where it binds a role); memory may point at them but never replaces them (spec D39).
 - Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
 - Findings that keep arriving are a process defect (spec D30): stop fixing, investigate where they entered and why reviews missed them, bring the owner process changes.
+- A finding's class is an epic: defects found in its dimension are its child rows, and it signs off only when every child is resolved and the whole class is tested and reviewed (spec D38).
 - Every commit, the session's and text-only included, first passes `run_all` and the gate suite on both interpreters (spec D34, whose Order lets one commit after 64f7ad7, the one landing D34, land on a red suite; the pre-commit hook enforces it once row B5-PROC-2 lands); session-written spec, CLAUDE.md, strict-block and plan text is applied (saved, for `~/.claude` text) or committed only after a task review of that exact diff (spec D34).
 - Pinned = production-path test + mutation transcript (`gate_review_package`).
 - Model map by role (§5); never dispatch outside it (`gate_dispatch`).
diff --git a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
index 09afe8c..d25a187 100644
--- a/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
+++ b/docs/superpowers/state/strict-delivery/mutations/B5-UD-1.txt
@@ -1,5 +1,5 @@
 finding: B5-UD-1
 test: test_hooks::test_claude_md_carries_spec_section_12_verbatim_and_nothing_else
 step 1 (fix present): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
-step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\(13, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != 13), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
+step 2 (fix reverted: perl -pi -e 's/self\.assertEqual\([0-9]+, len\(bullets\)\)/self.assertEqual(11, len(bullets))/' tools/gates/tests/test_hooks.py puts back the bullet count of the older spec section 12 (AssertionError: 11 != N, N the count of section 12 bullets), restored by git checkout -- tools/gates/tests/test_hooks.py): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> FAIL
 step 3 (fix restored): PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tools.gates.tests.test_hooks.ProjectWiring.test_claude_md_carries_spec_section_12_verbatim_and_nothing_else -> PASS
diff --git a/tools/gates/tests/test_hooks.py b/tools/gates/tests/test_hooks.py
index a386085..c21d134 100644
--- a/tools/gates/tests/test_hooks.py
+++ b/tools/gates/tests/test_hooks.py
@@ -993,7 +993,7 @@ class ProjectWiring(unittest.TestCase):
             spec = fh.read()
         section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
         bullets = [l for l in section.splitlines() if l.startswith("- ")]
-        self.assertEqual(13, len(bullets))
+        self.assertEqual(15, len(bullets))
         with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
             claude = fh.read().splitlines()
         self.assertEqual(bullets, [l for l in claude if l.startswith("- ")])
```

### Strict-block diffs, revised (supersede r0's; same install path)

Byte counts (`wc -c`; `check-skill.sh` run on a scratch copy of the skill with only its `D=` line pointed at the copy: "skill skeleton OK" with the four live-copy changes; with the staged D36 controller plus its sentence: "controller.md is 5618 bytes (> 5500)").

| Prompt file | Before | After | Cap 5500 |
|---|---|---|---|
| implementer.md (Q-doc-4, every option) | 4410 | 4629 (+219) | OK |
| reviewer.md (every option) | 4509 | 4740 (+231) | OK |
| verifier.md (Q-doc-4 b) | 4339 | 4572 (+233) | OK |
| controller.md live (Q-doc-4 c) | 5281 | 5399 (+118) | OK |
| controller.md staged D36 (Q-doc-4 c) | 5500 | 5618 (+118) | **over by 118** |

Each copy keeps one trailing newline (`tail -c1 \| od -c` = `\n`, five files). Each diff below was re-applied with `patch` to the installed copy and `cmp` equal to the measured copy.

```diff
--- a/~/.claude/skills/strict-delivery/prompts/implementer.md
+++ b/~/.claude/skills/strict-delivery/prompts/implementer.md
@@ -4,7 +4,7 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Build. Test first, through the production call path (the real file, or a script's main() on a fixture; never an extracted helper); watch it fail for the stated reason; then the minimal code. For every finding id in ## Findings write the mutation transcript at docs/superpowers/state/<plan>/mutations/<id>.txt in the umbrella format (fix present PASS, fix reverted FAIL, fix restored PASS). Test every sibling in the row's list or list it as an unrelated finding. Run the brief's full gate list clean, plus python3 tools/gates/run_all.py, before you report; zero warnings, zero gate failures.
+Build. Test first, through the production call path (the real file, or a script's main() on a fixture; never an extracted helper); watch it fail for the stated reason; then the minimal code. For every finding id in ## Findings write the mutation transcript at docs/superpowers/state/<plan>/mutations/<id>.txt in the umbrella format (fix present PASS, fix reverted FAIL, fix restored PASS). Test every sibling in the row's list or list it as an unrelated finding. Never delete a test as a fix: fix the code or the test, and restore a test deleted as a fix (spec D37). A class is signed off only when every child row is resolved and the whole class is tested and reviewed (spec D38). Run the brief's full gate list clean, plus python3 tools/gates/run_all.py, before you report; zero warnings, zero gate failures.
 
 Input domain (spec D24-D28). Cover every row of the task's input-domain table with a test; fix the whole dimension a finding names, not only the reproduced value; inputs outside the gate's whitelist FAIL closed with a named check. A rule the plan and spec do not state is a question to the controller, not your choice.
 
--- a/~/.claude/skills/strict-delivery/prompts/reviewer.md
+++ b/~/.claude/skills/strict-delivery/prompts/reviewer.md
@@ -4,11 +4,11 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.
+Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding. (6) List every test function the diff removes, with its commit and reason; one removed as a fix is a finding (spec D37).
 
 Verdict. Spec ✅, quality ✅ and claims-executed ✅ are each stated with evidence; anything less is not approved. A defect caused by this change makes the task incomplete (status DONE_WITH_FINDINGS, the finding listed first). Read the code, the diff and the review package; never edit code yourself. Writes: `set -eu`; stop unless `pwd -P` and the git toplevel are in your scratch clone (DEC-0032).
 
-Coverage (spec D24-D28). Check the change against every row of the task's input-domain table; a missing row is a finding. A re-review re-runs the finding's dimension over the whole changed function, not the round diff, and checks shared-library changes against the spec lines they touch. Rules in a controller's brief are hypotheses. Inputs outside the whitelist must FAIL closed. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35).
+Coverage (spec D24-D28). Check the change against every row of the task's input-domain table; a missing row is a finding. A re-review re-runs the finding's dimension over the whole changed function, not the round diff, and checks shared-library changes against the spec lines they touch. Rules in a controller's brief are hypotheses. Inputs outside the whitelist must FAIL closed. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35). A defect in a register row's class dimension is reported as a child of that class, naming the row (spec D38).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
--- a/~/.claude/skills/strict-delivery/prompts/verifier.md
+++ b/~/.claude/skills/strict-delivery/prompts/verifier.md
@@ -4,11 +4,11 @@
 
 Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.
 
-Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.
+Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding. (6) List every test function the branch removes, with its commit and reason; one removed as a fix is a finding (spec D37).
 
 Adversarial scope. Re-run every pin the branch claims, not a sample, each transcript at its row's pinned sha. List the paths the reviewer did not exercise (callers, error paths, the inputs named in each plan's Review Focus) and exercise each. Every reviewer PASS you cannot reproduce is a finding. Never edit code yourself. Writes: `set -eu`; stop unless `pwd -P` and the git toplevel are in your scratch clone (DEC-0032).
 
-Coverage (spec D24-D28). Check every gate against its input-domain table and whitelist; each fix against its whole dimension and function; brief rules are hypotheses. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35).
+Coverage (spec D24-D28). Check every gate against its input-domain table and whitelist; each fix against its whole dimension and function; brief rules are hypotheses. Rows come from the D31 skeleton; cells may cite the D32 default; grep tools/gates for a found defect's idiom and list every hit (D33); number new questions Q-<area>-<n> (D35). A defect in a register row's class dimension is reported as a child of that class, naming the row (spec D38).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
--- a/~/.claude/skills/strict-delivery/prompts/controller.md
+++ b/~/.claude/skills/strict-delivery/prompts/controller.md
@@ -14,7 +14,7 @@
 
 Convergence (spec D30). Stop dispatching fixes when a re-review finds a new defect in this block's code outside the fix's recorded class, or the block needs a second fix wave after its final review. Dispatch a read-only investigator on where each defect entered and why the review that had it missed it; bring the owner process changes with that evidence. Never offer only freeze, continue or cap.
 
-Commit gate (spec D34, D35). Before each commit of yours, text-only included, run `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` on /usr/bin/python3 and the main checkout's tools/.venv/bin/python3, with no unstaged or untracked path in `git status --porcelain --untracked-files=all` and no line from `git ls-files -v | grep '^[a-zS]'`; a red line or a missing interpreter blocks it. After each child commit re-run the set at its sha; red = that task is incomplete. Items on the same files run serially; one drafter per gate table. A child's budget use is the task notification's count, never its self-report.
+Commit gate (spec D34, D35). Before each commit of yours, text-only included, run `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" python3 tools/gates/run_all.py` and `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX="$(mktemp -d)" <py> -W error -m unittest discover -s tools/gates/tests -t .` on /usr/bin/python3 and the main checkout's tools/.venv/bin/python3, with no unstaged or untracked path in `git status --porcelain --untracked-files=all` and no line from `git ls-files -v | grep '^[a-zS]'`; a red line or a missing interpreter blocks it. After each child commit re-run the set at its sha; red = that task is incomplete. Items on the same files run serially; one drafter per gate table. A child's budget use is the task notification's count, never its self-report. Register a defect in a class's dimension as a child row of that class (D38); every owner rule gets a text item (D39).
 
 Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.
 
```

### Text moved out of the tracked diff (F3): option text, by Q-doc

| r0 tracked sentence | Q-doc | Text under each option (lands only after the owner's answer) |
|---|---|---|
| §4.1 schema line `parent: null  # the epic's row id when this row is a child (D38), else null` | Q-doc-5 | (a) that line, on every row; (b) the same line marked optional ("omitted when the row has no epic"); (c) no `parent`; the epic row carries `children: [ids]` |
| D38 "a register row whose child rows name it in their `parent` field" | Q-doc-5, Q-doc-6, Q-doc-18 | (5a/5b) "child rows name their epic in `parent`"; (5c) "an epic lists its children in `children`"; (6b) adds "an epic row carries `kind: epic`"; the row that is the epic: Q-doc-18 |
| D38 / §4.1 "every child is `pinned`, `closed` or `merged`" | Q-doc-7 | (a) "every child is `pinned`, `closed` or `merged`"; (b) "... or `deferred` with its owner decision id" |
| §4.1 "`parent` names another existing row that has no `parent` itself" | Q-doc-11 | (a) that sentence; (b) "`parent` names an existing row; following `parent` never returns to the row (no cycle)" |
| §4.1 "an epic ... is `pinned` only while each of them is ..." (a standing invariant) | Q-doc-8 | (a) or (b): the invariant as worded (a new child of a pinned epic reopens it, or goes to a successor epic); (c) "an epic's history step that sets `pinned` needs every child resolved at that sha" (checked at the transition only) |
| D38 "a production-path test over every row of the dimension's D24 table, with its mutation transcript" | Q-doc-9 | (a) "one pin test, and a review report attesting each D24 row of the dimension is covered"; (b) "one test per D24 row (`pin.test` a list)"; (c) "the epic's package maps each D24 row to a test, checked by `gate_review_package`" |
| D38 "A pinned row whose class a found defect widens stays pinned, as a resolved child of the epic that carries the wider class; ... new child rows (DEC-0102 successor-rows)" | Q-doc-18, Q-doc-12 | per Q-doc-18 option; D33's own sentence per Q-doc-12 |
| D38 one `parent` (single epic per row) | Q-doc-10 | (a) one parent, multi-part rows split into successor rows; (b) "`parent` is a list"; (c) one parent chosen by the owner |
| D37 / §12 "a decision question never offers deleting one as an option" | Q-doc-20 | (a) that clause; (b) nothing |
| D39 "until the text lands, the rule's source is its entry in `decisions.yml`" | Q-doc-21 | (a) nothing; (b) that clause plus "every owner statement made in session is recorded in `decisions.yml`" |
| D39 "a spec row; a CLAUDE.md bullet ...; and the strict block of every role" (each rule in every home) | Q-doc-16 | the revised D39 quotes DEC-0131's chosen option, "(spec rows, `CLAUDE.md` where it is a session rule, the strict-delivery blocks where a role must follow it)", and leaves the reading to Q-doc-16 |
| implementer "A row other rows name as parent (an epic, spec D38) is done only when ..." | Q-doc-5, -6, -9 | revised to the owner's words: "A class is signed off only when every child row is resolved and the whole class is tested and reviewed (spec D38)." |

The mechanics M1-M4, G1-G6 and the mapping table above stay as design proposals; the mapping's "Effect" column assumes Q-doc-18 (a). Under Q-doc-18 (b), rows 10, 11 and 14 have the successor row (R-B-1, R-A-5, R-B-13) as their epic instead of E6, E10 and E12; under (c), every pinned row is its own epic and each "open child" in the Effect column hangs on the pinned row itself (needs Q-doc-8 first).

### New owner questions

- **Q-doc-18** Which row carries a class's epic (F4, F6 a). (a) a new epic row per class, holding the wider class; the pinned row and any DEC-0102 successor row are its children (the r0 base) (cost: where a successor row exists two rows carry one dimension; 22 new epic rows for the mapping; a defect in a class with no epic row needs that row created, and its class stated as an owner question, before it can be filed as a child). (b) the DEC-0102 successor row is the epic and the pinned row its resolved child; elsewhere as (a) (cost: R-B-13, R-A-5, R-B-1 must be registered first (N3); epics of two kinds, successor rows and new rows). (c) every row is its own class's epic: a defect found in its dimension is its child, as D38 words it (cost: a pinned row gains open children at once, so Q-doc-8 must be answered first; a widened class means restating the pinned row's own class, which the answered DEC-0102 did not choose for rows).
- **Q-doc-19** Owner task and review package of a new epic row (F6 b). (a) one new task id per epic, each with its own package holding the whole-class transcript (cost: up to 22 task ids and packages; each placement is an owner scope answer as DEC-0104 was). (b) the epic belongs to the task of its first open child (cost: that task's package needs the epic's whole-class `mutation-<id>.txt` too; the task grows past its D29 review size). (c) one sweep task owns every epic (B5-PROC-3's `S2-b5-sweep`) (cost: one task with up to 22 whole-class transcripts, over the 3-task block and the D29 review size: it splits again at plan time).
- **Q-doc-20** D37 and decision questions (F2 a). (a) D37 adds "a decision question never offers deleting a test as an option" (cost: the owner's option set is narrowed by rule; a test whose subject is removed with its feature is not "a fix", so the clause needs no exception, but each question writer must tell the two apart). (b) D37 binds fixes only; a question may still list deletion and the owner rejects it (cost: the path that produced the DEC-0063 drop, a question that offered deletion and was answered with it, stays open).
- **Q-doc-21** Where an owner rule lives before its text lands (F2 b). (a) D39 says nothing (cost: between the owner's statement and the text commit nothing names the binding source; the session's memory is the de-facto source, which DEC-0131 forbids as the only home). (b) "until the text lands, its `decisions.yml` entry is the source", and every owner statement made in session is recorded there (cost: a new session duty; an entry needs `question`, `options` and `asked_at` (`gate_decisions` schema) even for a statement that answers no question, or a schema change for statements).

### Notes added in this round

- **N8** The D37 sentence "Every task review lists each test function the diff removes" and the reviewer/verifier item (6) come from the brief (Item 1 (a): "the strict-block sentence for implementer and reviewer"), not from DEC-0103's words; they are the means to find a deletion, and they answer no Q-doc. Kept; the owner approves them with the draft.
- **N9** HEAD moved during this round: 2e43f13 -> ec1c3c5 ("b5 round 17 item 3 -- B5-FIX-5 D33 siblings per DEC-0128; register B5-UR-16..23"; `git show --stat ec1c3c5`: findings.yml only, 216 insertions, 4 deletions). At ec1c3c5: `git apply --check core.diff` passes (the option diffs touch no file ec1c3c5 changed); findings.yml has 87 rows (planned 52, pinned 28, open 7), so the 28 pinned rows of the mapping are unchanged; R-A-5 and R-B-1 still have no row (grep count 0) and R-B-13 is named only in the B5-ID-1/ID-2 classes, now at :2292 and :2324. The findings.yml line numbers and the 79-row count elsewhere in this draft are at 2e43f13. The real checkout is clean (`git status --porcelain --untracked-files=all` empty).
