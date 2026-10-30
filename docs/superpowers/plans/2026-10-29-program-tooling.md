# Program tooling -- board, context contract, index, drift check (stream A, first block) -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: strict-delivery, then superpowers:subagent-driven-development. One block, one PR, three tasks, on its own branch `block/program-tooling` from the b5 tip; the PR's base is `block/strict-delivery-b5` and it merges before the b5 lean close (DEC-0148, DEC-0153). Lean path (umbrella "Replan 2026-10-29", DEC-0145). Steps use checkbox (`- [ ]`) syntax.

**Goal:** a new session, and every agent it dispatches, reads every file its work depends on, and every file fact it writes is checkable (DEC-0133). Drift is a missed file or an invented fact.

**Architecture:** One read list per session kind and dispatch role in `docs/superpowers/state/program/context.yml`. `tools/program/load_context.py` prints only that list (path, line range of at most 2000 lines and at most the byte cap program-tooling Task 1 measured, blob sha, why), under 16 KB. The session reads each entry with the Read tool; a PostToolUse hook on Read writes a git-ignored receipt (path, blob sha, line range read); a PreToolUse hook refuses Edit, Write, NotebookEdit, Agent and the Bash commit verbs until the receipt covers the list, and refuses them when the session has no expected list; on the session's own Edit/Write/NotebookEdit of a read-list file, PreToolUse stores the file's blob sha keyed by the tool-use id and PostToolUse refreshes the receipt only when that stored sha equals the receipt's. `tools/program/check_cites.py` checks `path:line@sha "quote"` citations; `tools/program/drift_check.py` runs the playbook's drift checks. `docs/INDEX.md` lists every tracked document on its current path (DEC-0146).

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6, as `tools/gates/`), `python3 -m unittest` through each script's `main()` on fixtures, Claude Code hooks (`.claude/settings.json`), git.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (verification method, §6); playbook `docs/superpowers/playbooks/two-streams.md` (context contract, drift checks, stated limits).

**Estimate:** 2 days. Report to the owner at once past 3 days.

## Global Constraints

- Every commit passes `run_all` and the gate suite on both interpreters first (spec D34); zero warnings, zero gate failures.
- Block = PR, three tasks, one implementer and one opus task review per task; at the block end one opus final reviewer and one final verifier over the whole block (DEC-0145); merge under grant:strict-delivery (DEC-0001), which covers this block's PR (DEC-0153).
- Tests run through each script's `main()` or the hook's entry point on a fixture repository, never a helper in isolation.
- The loader's printed output stays under 16 KB; every read-list range is at most 2000 lines (the Read tool's per-call limit) and at most the byte cap program-tooling Task 1 measured (at 314b46d the first 2000 lines of the S2 plan are 114,712 bytes and `docs/superpowers/state/decisions.yml`, 1,631 lines, is 134,307 bytes, so lines alone do not bound a Read); Task 1 writes the measured number here, into the playbook's "Context contract" and into the strict-delivery handoff trap.
- No personal identifier in any request or file the tools write; scratch clones use no git config overrides (DEC-0143).
- Hardware: nothing in this plan flashes or opens a serial port.

## Review Focus

1. **A receipt that lies.** A Read of a range must not count for a different range, an older blob, or another path; a file changed after the Read must lock the session until it is read again, unless the session's own Edit/Write changed it, proven by the pre-edit blob sha PreToolUse stored for that tool-use id equalling the receipt's (a change by another writer between the Read and the session's Edit is not refreshed). A session with no expected list (the loader never ran) is refused, never treated as covered; the refusal names the loader command.
2. **A read list that cannot be read.** A range over 2000 lines or over the byte cap program-tooling Task 1 measured, a missing path, a missing `ref`, or a loader output over 16 KB is a named FAIL, never a silent skip.
3. **A citation that passes without its quote.** `check_cites.py` must FAIL a quote absent from that line of `git show <sha>:<path>`, a sha that is not a commit, an untracked path, and a bare `path:line` with no `@sha`.

## Block b1 -- program tooling (Tasks 1-3), branch `block/program-tooling`

Block start: create `docs/superpowers/state/program-tooling/ledger.md` with line 1 `plan: docs/superpowers/plans/2026-10-29-program-tooling.md`, register one planned row per task (owner plan `program-tooling`, task `T1`..`T3`) so each implementer dispatch has a row, and push the branch.

### Task 1: spike, the read lists, the loader and the index

**Files:**
- Create: `docs/superpowers/state/program/context.yml`
- Create: `tools/program/load_context.py`
- Create: `docs/INDEX.md`
- Modify: `docs/superpowers/plans/2026-10-29-program-tooling.md` (the measured byte cap in Global Constraints)
- Modify: `docs/superpowers/playbooks/two-streams.md` (the measured byte cap in "Context contract")
- Modify: `docs/superpowers/state/strict-delivery/handoff.md` (the measured byte cap in the Read trap)
- Test: `tools/gates/tests/test_load_context.py`

**Findings:** none -- new tooling (DEC-0133, DEC-0146); the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Spike (read-only, answer in the task report)** -- in a scratch clone under the DEC-0032 guard, a probe hook on PreToolUse and PostToolUse records what the hook input carries for a subagent's tool call compared with the session's (session id, agent id, transcript path), and what a Read's `tool_input` and `tool_response` carry for `offset`/`limit` and for a file over 2000 lines. Record whether both hook events carry the same tool-use id for one tool call (Task 2 keys the pre-edit sha on it). Measure the largest Read the tool returns whole in bytes. The answers decide Task 2's receipt scope and the byte cap per entry; write the byte cap, with the measurement, into this plan's Global Constraints, the playbook's "Context contract" and the strict-delivery handoff trap, replacing "the byte cap program-tooling Task 1 measured".
- [ ] **Step 2: Write the failing tests** -- through `load_context.main()` on fixture repositories: a missing path, a missing range, a range over 2000 lines, a range of at most 2000 lines over the measured byte cap, an entry with a `ref` (the loader writes `git show <ref>:<path>` to `.superpowers/context/` and lists that copy), a missing ref, an output over 16 KB; each a named `FAIL context.<kind>.<path>` and rc 1.
- [ ] **Step 3: Write `context.yml`** -- one read list per session kind (stream-a-block, stream-b-block, sync) and per dispatch role; each entry `path`, `lines`, `why`, optional `ref`; sections over 2000 lines or over the measured byte cap split into entries; the current handoff's `context_files` adds the task's own files.
- [ ] **Step 4: Write `load_context.py <kind>`** -- prints each entry as `path lines a-b blob <sha> -- why`; writes the session's expected list (git-ignored, under `.superpowers/context/`).
- [ ] **Step 5: Write `docs/INDEX.md`** -- every tracked `*.md` on its current path with its purpose and owning stream (playbook ownership table); `git ls-files '*.md'` minus INDEX entries is empty.
- [ ] **Step 6: Run the tests and the gates** -- the new tests pass; `run_all` only PASS; the gate suite green on both interpreters.

### Task 2: enforcement -- receipt hooks, dispatch line, session start

**Files:**
- Create: `tools/gates/hooks/post_tool_use.py`
- Modify: `tools/gates/hooks/pre_tool_use.py`
- Modify: `tools/gates/hooks/session_start.py`
- Modify: `tools/gates/gate_dispatch.py`
- Modify: `.claude/settings.json`
- Modify: `docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md`
- Modify: `docs/superpowers/state/strict-delivery/ledger.md`
- Modify: `docs/superpowers/plans/2026-10-17-strict-delivery.md` (section "Dispatch prompt grammar", :237-:253 at 667e0d7: the ## Context line)
- Modify: `~/.claude/skills/strict-delivery/SKILL.md` (the dispatch-grammar paragraph, staged first, installed after the task review, spec D34)
- Modify: `~/.claude/skills/strict-delivery/prompts/controller.md` (the "Dispatch discipline" heading list, same staging)
- Modify: `tools/gates/tests/fixtures/dispatch/good_implementer.md`
- Modify: `tools/gates/tests/fixtures/dispatch/good_reviewer.md`
- Test: `tools/gates/tests/test_hooks.py`
- Test: `tools/gates/tests/test_gate_dispatch.py`

**Findings:** none -- new tooling (DEC-0133, DEC-0143); the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Write the failing tests** -- through the hook entry points on fixture repositories: a Read of lines 1-2000 records path, blob sha and range; a Read of another range does not cover the entry; a file changed by another writer after the Read refuses the next Edit, Write, NotebookEdit, Agent and Bash commit with the missing entries named; the session's own Edit or Write of a read-list file refreshes the receipt when the pre-edit blob sha PreToolUse stored for that tool-use id equals the receipt's sha; another writer changes the file between the Read and the session's Edit, so the stored pre-edit sha differs from the receipt's and the receipt is not refreshed (the next Edit is refused); a PostToolUse with no stored sha for its tool-use id refreshes nothing; a Bash write to a read-list file does not refresh it; no expected list for this session (the loader never ran) -> Edit, Write, NotebookEdit, Agent and Bash commit refused, the refusal names the loader command; every dispatch prompt fixture in `test_gate_dispatch.py`, `test_hooks.py` and `fixtures/dispatch/` carries `## Context: <kind>`, and one without it, or with a kind not in `context.yml`, is refused by name; a subagent's receipt follows the scope Task 1's spike measured.
- [ ] **Step 2: Write `post_tool_use.py`** -- Read: record path (repo-relative), `git hash-object` of the file, and the line range returned; Edit, Write, NotebookEdit: refresh only when the pre-edit sha stored by Step 3 for this tool-use id equals the receipt's sha, then drop the stored sha. Register it in `.claude/settings.json` (PostToolUse on Read, Edit, Write, NotebookEdit) and add the PreToolUse matchers for Edit, Write and NotebookEdit.
- [ ] **Step 3: Extend `pre_tool_use.py`** -- refuse unless the receipt covers every expected entry at the file's current blob sha; the refusal names the loader command and each missing entry. No expected list for this session fails closed: Edit, Write, NotebookEdit, Agent and the Bash commit verbs are refused and the refusal names the loader command. On Edit, Write or NotebookEdit of a read-list file, store the file's current blob sha keyed by the tool-use id (git-ignored, beside the receipt) for Step 2's refresh.
- [ ] **Step 4: Extend `gate_dispatch.py`** -- a `## Context: <kind>` line is required and its kind must exist in `context.yml`; the umbrella "Dispatch prompt grammar", the skill's dispatch paragraph, the controller prompt's heading list (and its S1 plan copy, Step 6) and every dispatch prompt fixture gain the line in the same commit, with the controller prompt's byte count in the task report (5500-byte cap, DEC-0095).
- [ ] **Step 5: Extend `session_start.py`** -- prints the playbook's start-of-session reading order and the read-list kind for the branch, from the board.
- [ ] **Step 6: The DEC-0143 sentences** -- add to every installed strict block under `~/.claude/skills/strict-delivery/prompts/` (staged first, installed after the task review, spec D34): "Scratch clones use no git config overrides (no -c, no config edits beyond what the brief names)." and "No personal identifier (email, name, handle) goes into any outbound request header, URL or payload." Each block stays within the 5500-byte cap (DEC-0095; controller.md is 5281 bytes at 6ba57dc, so its margin is 219 bytes; byte counts in the task report). Apply the same sentences to the S1 plan's strict-block copies (S1 plan :7339-7535, one per role) so each copy equals its installed file (D3), run `bash docs/superpowers/state/program/sources/check-skill.sh` (imported from the workspace, DEC-0159), and ledger the new sha256 of every installed prompt file in the strict-delivery ledger (S1 T9 Step 5 re-ledger convention, B5-UR-13's class).
- [ ] **Step 7: Run the tests and the gates** -- the new tests pass; `run_all` only PASS; the gate suite green on both interpreters.

### Task 3: citations, board, drift check

**Files:**
- Create: `tools/program/check_cites.py`
- Create: `tools/program/drift_check.py`
- Modify: `docs/superpowers/state/program/board.md`
- Test: `tools/gates/tests/test_check_cites.py`
- Test: `tools/gates/tests/test_drift_check.py`

**Findings:** none -- new tooling (DEC-0133); the block start registers the task's tracker row

**Siblings:** none -- no register row is routed to this task

**Gates:** run_all

- [ ] **Step 1: Write the failing tests** -- through `check_cites.main()` on fixture repositories: a quote present on that line of `git show <sha>:<path>` passes; a quote on another line, a sha that is not a commit, a path not tracked at that sha, and a bare `path:line` with no `@sha` and quote each FAIL `cite.<file>:<n>`. Through `drift_check.main()`: each playbook drift check has a fixture that FAILs it by name.
- [ ] **Step 2: Write `check_cites.py <files>`** -- `sha` is a commit; the check reads `git show <sha>:<path>`. Run by the drift check, by `gate_dispatch` on the brief, and at commit on the board and the handoffs.
- [ ] **Step 3: Write `drift_check.py`** -- the playbook's drift checks, each a named FAIL line; the board format it reads.
- [ ] **Step 4: Run on the real board and both handoffs** -- first rewrite each board reference to an imported source (`docs/superpowers/state/program/sources/`, named by path and section) into `path:line@sha "quote"` with the replan commit's sha; then only PASS; `run_all` only PASS; the gate suite green on both interpreters.
- [ ] **Step 5: CHECKPOINT** -- the manual checkpoint of the umbrella "Checkpoint protocol for this plan" with one opus final reviewer and one final verifier over the whole block (DEC-0145); `gh pr ready`; every check green; merge into `block/strict-delivery-b5` under DEC-0001 (DEC-0153); append `CHECKPOINT b1 <sha>` to `docs/superpowers/state/program-tooling/ledger.md`; update stream A's board row and handoff (next: the b5 lean close); stop for `/clear`.
