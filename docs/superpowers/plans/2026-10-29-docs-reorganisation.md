# Docs reorganisation -- one uniform place (stream A) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. Lean path (umbrella "Replan 2026-10-29", DEC-0145). Runs right after the prototype is field-ready, when stream B has no branch open (DEC-0146); the index on the current paths lands earlier, in the program tooling block. Steps use checkbox (`- [ ]`) syntax.

**Goal:** every document an agent may need is tracked, in one place, listed in one index, at paths the gates read from one table: "the docs should be indexed, but also moved to a more sensible place and together" (owner, 2026-10-29, DEC-0139).

**Architecture:** the gates, hooks and tools read every document path from one paths table in `tools/gates/config.yml`; one `git mv` per approved layout row moves the files; `docs/INDEX.md` (created on the current paths by the program tooling block) is rewritten to the new paths; an index gate keeps INDEX, `context.yml` and the tree in step.

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6) for the gates, git, Markdown.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (§4 registers and state paths, §7 gates); playbook `docs/superpowers/playbooks/two-streams.md`.

**Estimate:** 1 day.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34).
- Block = PR, three tasks, on branch `block/docs-reorganisation` from `main` (playbook "Branches": later stream A blocks branch from `main`); one opus final reviewer and one final verifier over the block (DEC-0145); merge under grant:strict-delivery (DEC-0001), which covers this block's PR (DEC-0160).
- No stream B branch is open while Task 2 runs (DEC-0146).
- The layout below is a proposal: the owner approves it before Task 2.
- Hardware: nothing in this plan flashes or opens a serial port.

## Review Focus

1. **A gate that reads an old path.** After the move every gate, hook, tool and workflow reads the paths table; a grep for every old path finds no hit outside git history.
2. **A document lost in the move.** Every tracked `*.md` before the move is in `docs/INDEX.md` after it, at a path that exists.

## Proposed layout (for the owner to approve before Task 2)
Each tracked document is in exactly one source row.

| directory | holds | from |
|---|---|---|
| `docs/INDEX.md` | every tracked document: path, purpose, area | `docs/INDEX.md` (program tooling block, current paths) |
| `docs/product/specs/` | the lap-timer spec and every product design spec | `docs/superpowers/specs/2026-09-14-lap-timer-design.md`, `2026-09-20-*`, `2026-09-21-*`, `2026-09-23-*`, `2026-09-25-*`, `2026-09-27-*`, `2026-09-29-*`, `2026-09-30-*`, `2026-10-03-*`, `2026-10-13-audit-fix-wave-design.md` |
| `docs/product/plans/` | the roadmap and every product plan | `docs/superpowers/plans/` except the files the `docs/process/` row names |
| `docs/product/` | pitch, display glyphs | `docs/pitch.md`, `docs/display-glyphs.md` |
| `docs/hardware/` | BOM, wiring, dev-kit port | already there |
| `docs/bench/` | bench procedures, measurements | `docs/bench/`, `docs/measurements.md` |
| `docs/p10/` | checklist, deviation register, Power of 10 spec | `docs/power-of-10-checklist.md`, `docs/power-of-10-deviations.md`, `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` |
| `docs/process/` | strict-delivery spec, the strict-delivery plans, playbooks | `docs/superpowers/specs/2026-10-17-strict-delivery-design.md`, `docs/superpowers/plans/2026-10-17-strict-delivery*.md`, `docs/superpowers/plans/2026-10-29-program-tooling.md`, `docs/superpowers/plans/2026-10-29-docs-reorganisation.md`, `docs/superpowers/playbooks/` |
| `docs/state/` | registers, handoffs, ledgers, board, read lists, decision sheets, mutation transcripts, review reports | `docs/superpowers/state/` |
| `docs/reviews/` | audits and the reviewer evidence they cite | `docs/reviews/`, plus the files under the git-ignored `.superpowers/reviews/` that the audits cite |

## Block b1 -- docs reorganisation (Tasks 1-3), branch `block/docs-reorganisation`

Block start: create `docs/superpowers/state/docs-reorganisation/ledger.md` with line 1 `plan: docs/superpowers/plans/2026-10-29-docs-reorganisation.md`, register one planned row per task (owner plan `docs-reorganisation`, task `T1`..`T3`) so each implementer dispatch has a row, and push the branch.

### Task 1: every path a config entry

**Files:**
- Modify: `tools/gates/config.yml` (one paths table)
- Modify: `tools/gates/lib/registers.py`
- Modify: `tools/gates/gate_plan.py`
- Modify: `tools/program/load_context.py`
- Modify: `tools/program/drift_check.py`
- Test: `tools/gates/tests/test_lib.py`

**Findings:** none -- the move DEC-0139 orders; no register row yet

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: List every hard-coded document path** with file:line in `tools/gates/` (code and tests), `tools/lint/`, `tools/program/`, `tools/*.py`, the bench helpers under `tools/`, the hooks, `.github/workflows/`, `CLAUDE.md`, `docs/superpowers/state/program/context.yml`, `docs/INDEX.md`, the strict-delivery skill and its prompts, the plans' fenced commands, and the memory notes under `~/.claude/projects/` that name a document path. The list goes into the task report and the Files list above grows to every code file it names.
- [ ] **Step 2: Write the failing test** -- a fixture repository with the documents at moved paths and a paths table naming them: every gate reads the table and passes.
- [ ] **Step 3: Move the code paths behind the table** -- each listed code site reads `config.yml`; the test passes; `run_all` only PASS; the gate suite green on both interpreters.

### Task 2: the move

**Files:**
- Modify: `docs/` (one git mv per approved layout row)
- Modify: `docs/INDEX.md`
- Modify: `docs/superpowers/state/program/context.yml`
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/state/docs-reorganisation/ledger.md` (moved with the state directory; line 1 plan path rewritten)
- Modify: `docs/superpowers/state/strict-delivery/ledger.md` (moved; line 1 plan path rewritten)
- Modify: `docs/superpowers/state/program-tooling/ledger.md` (moved; line 1 plan path rewritten)
- Modify: `docs/superpowers/state/prototype/ledger.md` (moved; line 1 plan path rewritten)

**Findings:** none -- the move DEC-0139 orders; no register row yet

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all

- [ ] **Step 1: Confirm the preconditions** -- the owner approved the layout; `git branch -r` shows no open stream B branch.
- [ ] **Step 2: Move** -- `git mv` per approved row; rewrite references (Task 1's list, then a grep for every old path: zero hits outside history); import the cited evidence files into `docs/reviews/`; `docs/INDEX.md` and `context.yml` at the new paths; in the same commit, every moved ledger's line 1 `plan:` names its plan's new path (this plan's ledger included), or `gate_plan` reads a missing plan and `run_all` turns red; memory notes reduced to pointers at tracked docs.
- [ ] **Step 3: Check** -- each moved ledger's line 1 names a plan file that exists (one `test -f` per ledger, recorded in the task report); `run_all` only PASS; the gate suite green on both interpreters.

### Task 3: the index gate

**Files:**
- Create: `tools/gates/gate_index.py`
- Modify: `tools/gates/run_all.py`
- Test: `tools/gates/tests/test_gate_index.py`

**Findings:** none -- the index gate DEC-0139 orders; no register row yet

**Siblings:** none -- no register row is routed to this task

**Mode:** SERIAL

**Gates:** run_all, index

- [ ] **Step 1: Write the failing tests** -- through `gate_index.main()` on fixtures: a tracked `*.md` missing from `docs/INDEX.md`, an INDEX entry that does not exist, and a `context.yml` path that does not exist each FAIL by name.
- [ ] **Step 2: Write the gate and add it to `run_all`** -- the tests pass; `run_all` only PASS; the gate suite green on both interpreters.
- [ ] **Step 3: CHECKPOINT** -- the manual checkpoint of the umbrella "Checkpoint protocol for this plan" with one opus final reviewer and one final verifier over the block (DEC-0145); `gh pr ready`; every check green; merge under grant:strict-delivery (DEC-0001, DEC-0160); append `CHECKPOINT b1 <sha>` to the ledger at its moved path (the paths table names it); update stream A's board row and handoff; stop for `/clear`.
