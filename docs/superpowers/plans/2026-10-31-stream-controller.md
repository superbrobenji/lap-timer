# Stream controller ladder (stream A) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. One block, one PR, three tasks, on its own branch `block/stream-controller` from the `block/program-tooling` tip that holds this plan, in the worktree `../lap-timer-worktrees/stream-controller`; the PR's base is `block/program-tooling` and it merges into it under DEC-0001 (DEC-0190). Runs after program tooling b1 pauses at its Task 1 boundary (DEC-0184), in a fresh context. Steps use checkbox (`- [ ]`) syntax.

**Goal:** the owner's terminal runs one program session that dispatches one stream controller per stream block, relays owner questions and merges; each stream controller runs its block from start to checkpoint as a subagent and reports once (DEC-0181). Stream B moves to it after its next clear.

**Architecture:** The ladder, as DEC-0181's chosen option states it: "level 1 program session (the terminal: dispatches stream controllers, relays owner questions, merges on the owner sentence, runs syncs); level 2 stream controller (new opus role, one block of one stream, start to checkpoint); level 3 sub-controller (existing, optional); level 4 workers unchanged; every block runs in its own worktree so the program session checkout, whose hooks every agent runs, stays at a stable commit; gate_dispatch refuses a stream controller dispatched by a subagent". Budget (DEC-0182, relay-on-budget at 1M tokens / 180 min): at 80 % the stream controller checkpoints at a task boundary and the program session re-dispatches a fresh one from the handoff. Owner questions (DEC-0183 halt-and-return): it writes the question to decisions.yml, halts NEEDS_DECISION and returns; a stream A stream controller returns the question for the session to write while stream A blocks run at once (DEC-0195), and stream B's writes its own range on its own branch (DEC-0245). The program session stops for the owner's clear at each sync or after every second stream-controller hand-back, whichever first (DEC-0189). Two stream A blocks may run at once, each in its own worktree; D22's clean tree means the block's own worktree and PR (DEC-0194).

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6, as `tools/gates/`), `python3 -m unittest` through each script's `main()` or the hook's entry point on fixtures, Claude Code hooks.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (D22, §3.1, §3.4, §3.5, §3.6, §5, §6 step 8); playbook `docs/superpowers/playbooks/two-streams.md` (opening, "Session kinds").

**Estimate:** 1.5 days. Report to the owner at once past 2.25 days.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34); zero warnings, zero gate failures. Session-written spec, CLAUDE.md, strict-block and plan text is committed, and `~/.claude` text installed, only after a task review of that exact diff (spec D34).
- Block = PR, three tasks, one implementer and one opus task review per task; at the block end one opus final reviewer and one final verifier over the whole block; the session merges under DEC-0001 (DEC-0190).
- FAIL-emitting code gets an input-domain table at plan time, committed with this plan (DEC-0178 change 1): `docs/superpowers/state/stream-controller/T2-input-domain.md`; a cell found missing during the build is a question to the controller. D30 is on (DEC-0178 change 7); a deleted test is restored and fixed (DEC-0103).
- Shared files: this block and program tooling Task 2 both change `gate_dispatch.py`, `hooks/pre_tool_use.py`, `tests/test_gate_dispatch.py`, `tests/test_hooks.py`, `SKILL.md` and `controller.md`; Task 2 waits for this block's merge (DEC-0184). The option-line block edits the playbook's "Ownership" and "Shared files"; this block edits other sections; a rebase conflict between them halts the second merge and goes to the owner.
- Decision rows: while stream A blocks run at once, only the session writes stream A's `decisions.yml` rows, on `block/program-tooling` (DEC-0195); stream B's stream controller writes rows in its own range on its own branch (DEC-0245).
- No personal identifier (email, name, handle) in any outbound request header, URL or payload (DEC-0143); scratch clones use no git config overrides.
- Hardware: nothing in this plan flashes or opens a serial port.

## Review Focus

1. **A ladder the gates do not hold.** A stream controller dispatched by a subagent, a stream controller dispatch whose budget exceeds the config trigger, and a role name outside `config.yml` are each a named FAIL.
2. **Text that disagrees with itself.** CLAUDE.md, the spec, the skill, the prompt blocks and the playbook state one ladder, one budget, one lifetime rule and one merge rule; every changed sentence is checked against DEC-0181..DEC-0184, DEC-0189, DEC-0190, DEC-0194, DEC-0195, DEC-0197 and DEC-0245.

## Block b1 -- stream controller (Tasks 1-3), branch `block/stream-controller`

Block start: create `docs/superpowers/state/stream-controller/ledger.md` with line 1 `plan: docs/superpowers/plans/2026-10-31-stream-controller.md`, register one planned row per task (ids `PROG-SC-T1`..`PROG-SC-T3`; owner plan `stream-controller`, task `T1`..`T3`), and push the branch.

### Task 1: the ladder in the spec, CLAUDE.md and the playbook

**Files:**
- Modify: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (D22, §3.1, §3.4, §3.5, §3.6, §5 role table, §6 step 8)
- Modify: `CLAUDE.md` (the block line)
- Modify: `docs/superpowers/playbooks/two-streams.md` (opening sentence, "Session kinds", the parallel-terminals sentence)

**Findings:** none -- answered owner questions DEC-0181..DEC-0184, DEC-0189, DEC-0190, DEC-0194, DEC-0195 and DEC-0245; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Inventory** -- grep the three files, `SKILL.md` and `prompts/` for "one block per context", "sub-controller", "session" in its role sense and the trigger numbers; list each hit with file:line and the sentence it becomes.
- [ ] **Step 2: Write the text** -- each sentence quotes or follows the chosen option of the answer it implements: the ladder (DEC-0181), the 1M tokens / 180 min trigger with the relay at 80 % (DEC-0182), halt-and-return (DEC-0183), the program session's lifetime (DEC-0189), separate worktrees and D22's clean tree (DEC-0194), merge in the session (DEC-0181) under DEC-0001 (DEC-0190); stream A's decision rows written only by the session while stream A blocks run at once (DEC-0195), stream B's in its own range on its own branch (DEC-0245).
- [ ] **Step 3: Run the gates** -- `run_all` only PASS; the gate suite green on both interpreters.

### Task 2: the role in config and gate_dispatch

**Files:**
- Modify: `tools/gates/config.yml`
- Modify: `tools/gates/gate_dispatch.py`
- Modify: `tools/gates/hooks/pre_tool_use.py`
- Test: `tools/gates/tests/test_gate_dispatch.py`
- Test: `tools/gates/tests/test_hooks.py`
- Create: `tools/gates/tests/fixtures/dispatch/prompts/stream-controller.md`

**Findings:** none -- answered owner questions DEC-0181 and DEC-0182; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Spike** -- in a scratch clone under the DEC-0032 guard, a probe hook records `CLAUDE_PROJECT_DIR` and the hook input's agent fields for a session tool call and for a subagent's call made in another worktree; the answer goes to the report and, if hooks do not run from the session's checkout, to the controller as an owner question before Step 2.
- [ ] **Step 2: Check the table against the spike** -- if the spike contradicts a row of `T2-input-domain.md`, the row goes to the controller as a question before Step 3.
- [ ] **Step 3: Write the failing tests** -- one per row of `T2-input-domain.md`, through `gate_dispatch.main()` and the hook's entry point (test_hooks.py class AgentDispatchHook) on fixtures.
- [ ] **Step 4: Write the code** -- `models.stream_controller: opus`; `triggers.stream_controller: {tasks: 3, tokens: 1000000, minutes: 180}`; the nesting refusal as a named FAIL (DEC-0199): the hook passes `--agent-id` the JSON encoding of the hook input's `agent_id`, or of `agent_type` when `agent_id` is absent (DEC-0202), and nothing when both are absent; gate_dispatch decodes it, so a blank or non-string value reaches it unchanged and FAILs `dispatch.nesting` (DEC-0200, DEC-0201).
- [ ] **Step 5: Pin** -- one production-path test plus `docs/superpowers/state/stream-controller/mutations/PROG-SC-T2.txt` in the umbrella format, mutating the nesting refusal.
- [ ] **Step 6: Run the gates** -- `run_all` only PASS; the gate suite green on both interpreters.

### Task 3: the skill and the stream controller's strict block

**Files:**
- Modify: `~/.claude/skills/strict-delivery/SKILL.md` (roles table and pipeline step 8; staged in the task's scratch first, installed after the task review, spec D34)
- Create: `~/.claude/skills/strict-delivery/prompts/stream-controller.md` (same staging)
- Modify: `~/.claude/skills/strict-delivery/prompts/controller.md` (the program session's duties; same staging)

**Findings:** none -- answered owner questions DEC-0181..DEC-0183, DEC-0189, DEC-0195, DEC-0197 and DEC-0245; the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Write the strict block** -- its own worktree only (DEC-0181); the hooks, `.claude/` and `~/.claude/` only where its plan's task lists them (DEC-0197); the 80 % relay with the handoff committed and pushed (DEC-0182); halt-and-return (DEC-0183); no merge (DEC-0181: the program session merges); an owner question is written to decisions.yml and returned (DEC-0183): a stream A stream controller returns it for the session to write, since only the session writes stream A's rows while stream A blocks run at once (DEC-0195, DEC-0245); stream B's stream controller writes its own range on its own branch (DEC-0245); reap background jobs; the report contract.
- [ ] **Step 2: Fit the dispatch** -- the strict block plus a typical stream controller task section stays under `dispatch.max_prompt_chars` (show the count).
- [ ] **Step 3: Install after review** -- the task review reads the staged diff; the controller installs it; `gate_dispatch --prompt` on a stream controller fixture dispatch passes against the installed block.
- [ ] **Step 4: CHECKPOINT** -- one opus final reviewer and one final verifier over the whole block; `gh pr ready`; every check green; the session merges under DEC-0001 (DEC-0190); append `CHECKPOINT b1 <sha>` to `docs/superpowers/state/stream-controller/ledger.md`; stream A's board row and handoff updated (next, DEC-0191: program tooling b1 Tasks 2-3 under a stream controller, then the D30 process block); stop for `/clear`.
