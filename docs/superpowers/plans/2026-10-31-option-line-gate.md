# Option-line gate and decision-id ranges (stream A, pilot block for stream B) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. One block, one PR, two tasks (DEC-0180), on its own branch `block/option-line-gate` from the `block/program-tooling` tip that holds this plan, in the worktree `../lap-timer-worktrees/option-line-gate`; the PR's base is `block/program-tooling` and it merges into it under DEC-0001 (DEC-0190). Run by an opus sub-controller dispatched by the session beside other stream A work (DEC-0180, DEC-0194). Steps use checkbox (`- [ ]`) syntax.

**Goal:** stream B's b1 can resume (DEC-0325 script-gate-first): a script checks DEC-0326's chosen option, "every DEC use in the plan and docs/hardware quotes its option line", and stream B's decision ids DEC-0300..DEC-0499 can land in `decisions.yml` without a red gate suite (DEC-0314 test-allows-ranges).

**Architecture:** `gate_decisions` gains a range check: decision ids run without a gap from DEC-0001 (stream A) and from DEC-0300 (stream B), and no id lies outside DEC-0001..DEC-0499 (DEC-0193 no-gap-per-range; playbook "Shared files" ranges); the registers test's contiguity assertion follows it. `tools/gates/gate_decision_use.py --root <dir> [--source <yml>]... [--json] <files>` (the `--json` option as every gate, spec :203; paths relative to the root, DEC-0230; the root required, a work-tree top, DEC-0231) reads the decision registers given (default `docs/superpowers/state/decisions.yml`; stream B adds `docs/superpowers/state/prototype/pending-decisions.yml` until its rows land in `decisions.yml`) and prints one named FAIL per DEC id use in the files that has no citation of that decision's chosen option line on the same line (DEC-0192 same-line). It is not added to `run_all` GATES in this block: stream A's tree holds older copies of stream-B files that stream B rewrites on its branch, so wiring it into `run_all` or CI is a sync decision.

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6, as `tools/gates/`), `python3 -m unittest` through each script's `main()` on fixtures, git.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (verification method, D24-D34, §6); playbook `docs/superpowers/playbooks/two-streams.md` ("Ownership", "Shared files"); stream B's prototype of the check: its D30 investigation section 3 (`.superpowers/sdd/prototype/reviews/b1-d30-3-investigation.md` in `../lap-timer-worktrees/prototype`, git-ignored; the sub-controller imports the section into this block's ledger at block start).

**Estimate:** 1 day. Report to the owner at once past 1.5 days.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34); zero warnings, zero gate failures.
- Block = PR, two tasks, one implementer and one opus task review per task; at the block end one opus final reviewer and one final verifier over the whole block; the sub-controller stops at `gh pr ready` and the session merges under DEC-0001 (DEC-0190).
- Both tasks add FAIL lines, so each has an input-domain table written at plan time and committed with this plan (DEC-0178 change 1): `docs/superpowers/state/option-line-gate/T1-input-domain.md` and `T2-input-domain.md` in the same directory. A cell found missing during the build is a question to the controller, not the implementer's choice.
- D30 is on (DEC-0178 change 7): the first re-review finding outside the classes already recorded halts the fix loop for an investigator. A deleted test is restored and fixed, never dropped (DEC-0103).
- Files: besides its tasks' Files, the block writes only its own state (the ledger, the tables, the mutation transcripts under `docs/superpowers/state/option-line-gate/`), its own register rows and stream A's board row at the checkpoint; it never touches the hooks, `.claude/` or `~/.claude` (DEC-0180).
- Decision rows: only the session writes `decisions.yml`, on `block/program-tooling`; the sub-controller halts and returns an owner question, and the block cites the id after the session's commit (DEC-0195).
- Order: this block branches after program tooling Task 1 is approved, so it holds Task 1's playbook edits. The stream controller block also edits the playbook, in other sections (opening, "Session kinds", the parallel-terminals sentence; this block edits "Ownership" and "Shared files"); a rebase conflict between them halts the second merge and goes to the owner.
- Tests run through each script's `main()` on a fixture repository, never a helper in isolation.
- No personal identifier (email, name, handle) in any outbound request header, URL or payload (DEC-0143); scratch clones use no git config overrides.
- Hardware: nothing in this plan flashes or opens a serial port.

## Review Focus

1. **A use that passes without its option line.** A DEC id on a line with no citation, with a citation of the answer line (except for a row whose answer matches no option label, DEC-0217, and a row with no options, DEC-0218, which are cited by their answer line) or of a not-chosen option line, of another decision's row, with a quote absent from the cited line, or of an unanswered decision, is a named FAIL.
2. **A range rule that admits a wrong id.** A gap inside either range, an id over DEC-0499, an id between the last stream A id and DEC-0300 that breaks the stream A run, and a duplicate are each a named FAIL of `gate_decisions`.

## Block b1 -- option-line gate (Tasks 1-2), branch `block/option-line-gate`

Block start: create `docs/superpowers/state/option-line-gate/ledger.md` with line 1 `plan: docs/superpowers/plans/2026-10-31-option-line-gate.md`; register one planned row per task (ids `PROG-OLG-T1`, `PROG-OLG-T2`; owner plan `option-line-gate`, task `T1`, `T2`); import the investigation section; push the branch.

### Task 1: decision ids in two stream ranges (DEC-0314, DEC-0193)

**Files:**
- Modify: `tools/gates/gate_decisions.py`
- Modify: `tools/gates/tests/test_gate_decisions.py`
- Modify: `tools/gates/tests/test_registers.py`
- Modify: `tools/gates/tests/test_run_all.py` (its clean-tree fixtures use an empty register, which FAILs the DEC-0001 range check under DEC-0236)

**Findings:** none -- answered owner questions DEC-0314 and DEC-0193; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Find every contiguity assumption** -- grep `tools/gates/` and `tools/program/` for constructions of expected decision ids; list each hit with file:line; a hit outside this task's Files is an owner question (plan scope), not an edit.
- [ ] **Step 2: Write the failing tests** -- one per row of `T1-input-domain.md`, through `gate_decisions.main()` on fixture registers, including: ids DEC-0001..DEC-n plus DEC-0300..DEC-m pass in any row order; a gap in either run, an id over DEC-0499 and a duplicate each FAIL `decisions.range.<id>` or `decisions.duplicate.<id>`.
- [ ] **Step 3: Write the range check** -- the minimal change; the registers test's assertion at :55 checks the same rule; the live register stays green.
- [ ] **Step 4: Pin** -- one production-path test plus `docs/superpowers/state/option-line-gate/mutations/PROG-OLG-T1.txt` in the umbrella format, mutating the gap check.
- [ ] **Step 5: Run the gates** -- `run_all` only PASS; the gate suite green on both interpreters.

### Task 2: `gate_decision_use.py`

**Files:**
- Create: `tools/gates/gate_decision_use.py`
- Create: `tools/gates/tests/test_gate_decision_use.py`
- Modify: `docs/superpowers/playbooks/two-streams.md` (under "Shared files", one sentence: stream B runs the gate on its plan and its hardware docs from the pilot worktree before this block merges, then from a checkout of the program tooling branch, until a sync wires it, DEC-0198; in the "Ownership" row A, the option-line-gate and stream-controller state directories and the two 2026-10-31 plans, DEC-0196)

**Findings:** none -- answered owner questions DEC-0325, DEC-0326, DEC-0192, DEC-0196 and DEC-0198; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Write the failing tests** -- one test per row of `T2-input-domain.md`, through `gate_decision_use.main()` on fixture repositories, each red against an empty implementation for the row's stated reason.
- [ ] **Step 2: Write the gate** -- the minimal code; every input outside the table's whitelist FAILs closed with a named check.
- [ ] **Step 3: Pin** -- one production-path test plus `docs/superpowers/state/option-line-gate/mutations/PROG-OLG-T2.txt` in the umbrella format, mutating the chosen-option comparison.
- [ ] **Step 4: Run on stream B's tree** -- from this worktree (`../lap-timer-worktrees/option-line-gate` from the checkout; `../prototype` is stream B's worktree beside it), `--root ../prototype --source docs/superpowers/state/decisions.yml --source docs/superpowers/state/prototype/pending-decisions.yml` on `docs/superpowers/plans/2026-10-29-prototype-field-ready.md` and every path that `git -C ../prototype ls-files docs/hardware` lists (stream B's wiring.md included), each given relative to the root (DEC-0230); the flag list goes to the report for stream B's b1 fix round. It is evidence, not a gate result for this block.
- [ ] **Step 5: Run the gates** -- the new tests pass; `run_all` only PASS; the gate suite green on both interpreters.
- [ ] **Step 6: CHECKPOINT** -- one opus final reviewer and one final verifier over the whole block; `gh pr ready`; every check green; the sub-controller stops and reports; the session merges under DEC-0001 (DEC-0190); append `CHECKPOINT b1 <sha>` to `docs/superpowers/state/option-line-gate/ledger.md`; stream A's board row names the gate and the flag list for stream B.
