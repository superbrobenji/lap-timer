---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: block/strict-delivery-b4
head: "9704065"
base: "07d7b29"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: 109
block: 4
next_task: "T4"
findings_in_flight: [{SD-S2-T1: pinned}, {SD-S2-T2: pinned}, {SD-S2-T3: pinned}, {B4-FIX-1: pinned}, {B3-S4PREPUSH-1: open}]
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings:
  - "block b4 Tasks 1-3 ran under opus sub-controllers (two rounds: the first stopped on a usage-limit 429 after Task 2 fix round 1 and was resumed to finish Task 2; the second ran Task 3 after DEC-0008); the session kept CHECKPOINT b4"
  - "Tasks 1-3 plan text corrected to the shipped S1 code before dispatch (D3): no pre_tool_use.project_root (toplevel of the event cwd), S1 Agent-exits-0 assertion inverted, fixture plans file for run_all's decisions gate, _decisions uses registers.open_decisions, test_run_all GATES pin updated, pin.test in PIN_TEST form, pinned rows need a tracked reviewer report in evidence, S1 plan blocks frozen at strict-delivery-b3"
  - "the pin step is run by the controller after the clean review verdict: transcript to mutations/<id>.txt, reviewer report to reviews/<task>.md, pin + evidence + history by controller, one commit with the ledger DONE line"
  - "handle_agent refuses a dispatch whose subagent_type is fork (FAIL dispatch.fork): a fork runs on the session model with the session context (spec §5, D18)"
grant: DEC-0001
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "the Agent hook is live from b4: every dispatch follows the umbrella grammar; write the prompt to a file and run gate_dispatch.py --prompt <file> --tool-model <m> before dispatching (a placeholder strict block, a model word differing from the Agent tool's model parameter, an unanswered DEC id or a fork subagent_type is refused)"
  - "a usage-limit 429 terminates a sub-controller mid-loop: resume the same agent by message (its context survives), finish the current task through its review loop (spec §3.4), and ask the owner where the remaining tasks run (spec §3.1; DEC-0008 precedent)"
  - "an implementer dispatch needs a planned or in-progress register row: register a fix-wave row (B4-FIX-1 pattern, owner task S<n>-b<N>-fixwave) before dispatching a final-review fix wave"
  - "pinned rows need pin.test in PIN_TEST form (test_<module>::test_<name>) and a tracked reviewer report path in evidence; review reports live at docs/superpowers/state/strict-delivery/reviews/"
  - "plan whole-file blocks must equal the tip files after later tasks change them (D3): re-sync the earlier task's block, never carry deltas as extra steps (b4 verifier finding 1)"
  - "the stale-comment grep fixed fact in the b3/b4 briefs was false: compare the raw hit set with ci.yml's allow list (11 lines at 07d7b29), never with a single line"
  - "run_all prints 29 lines from b4 (15 S1 + 14 dispatch.config.*); the gate suite is 361 tests at 9704065"
  - "Claude Code loads a project .claude/settings.json without a restart: a hook script change is live at its commit"
  - "pre_tool_use.py refuses command text holding -n/--no-verify after commit/push, core.hooksPath, GIT_CONFIG_*, or HOME=/XDG_CONFIG_HOME= before a verb: put such text in files (git commit -F, probe scripts, Write tool for briefs that quote them)"
  - "run the suite after every register edit and read the verdict line (test_registers pins the live registers)"
  - "paste prompt blocks from ~/.claude/skills/strict-delivery/prompts/ into the dispatch, never from memory (gate_dispatch compares byte for byte)"
  - "every agent reaps its background jobs before reporting; the controller checks ps/ListAgents for orphans at hand-back"
  - "evidence paths and pin transcripts must be regular files tracked by git (staged in the same commit is enough for pre-commit)"
  - "git-pre-push.sh refuses any pushed sha that does not peel to HEAD (DEC-0006): push from the checked-out commit"
  - "task-brief extraction of a plan's last task includes the CHECKPOINT section and the next block's preamble: trim it before dispatch"
  - "a review loop can drift past a plan's documented limits: rule early that the stated limits stand, or ask the owner (DEC-0005)"
  - "the S4 plan's git-pre-push.sh block is bash-only under an sh shim and replaces the S1 guard: B3-S4PREPUSH-1, owner S4 Task 5"
  - "the gates CI job is still not a required status check on main (owner's action, carried from b2)"
---

Post-merge facts (verified in CHECKPOINT b4 step 7, by hand: gate_checkpoint arrives in b6): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
