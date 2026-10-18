---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "6d59f49"
base: "151f2bb"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: 105
block: 3
next_task: "S2-b4"
findings_in_flight: [B3-S4PREPUSH-1]
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings:
  - "Tasks 7-9 ran under an opus sub-controller (two rounds; the first stopped NEEDS_OWNER at the Task 8 breaker); the session kept CHECKPOINT b3"
  - "Task 7 fixture trees carry an empty docs/reviews/ (gate_register input); gitfixture.new_repo stays bare"
  - "git-pre-commit.sh refuses worktree-side changes under every path the gates read or run from (git status --porcelain, not checkout-index); pre_tool_use.py gates the tree a command names via -C or a leading cd, and a named directory that is not a repository fails closed"
  - "Task 7 ran a 6th fix round past the SDD cap for one Minor under D6"
  - ".claude/settings.json has one unfiltered Bash handler (the script decides what is gated); SessionStart reports failures on stdout and exits 0; a missing cwd is FAIL hook.input"
grant: DEC-0001
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "Claude Code loads a project .claude/settings.json without a restart: the PreToolUse hook gated every Bash call of the session that committed it, and false-blocked agents until Task 8b"
  - "pre_tool_use.py refuses command text holding -n/--no-verify after commit/push, core.hooksPath, GIT_CONFIG_*, or HOME=/XDG_CONFIG_HOME= before a verb: put such text in files (git commit -F, probe scripts)"
  - "run the suite after every register edit: test_registers pins the live registers; the session committed a red suite twice (1b2cab3, 80a1857) by running it before adding DEC-0005"
  - "paste prompt blocks from ~/.claude/skills/strict-delivery/prompts/ into the dispatch, never from memory: the fix-wave dispatch omitted the implementer's Build paragraph (gate_dispatch arrives in S2)"
  - "every agent reaps its background jobs before reporting (Background work paragraph, all eight blocks); the controller checks ps/ListAgents for orphans at hand-back (the b3 verifier left a duplicate T8 mutation run alive)"
  - "evidence paths and pin transcripts must be regular files tracked by git: a gitignored report under .superpowers/ cannot be evidence"
  - "git-pre-push.sh refuses any pushed sha that does not peel to HEAD (DEC-0006 read literally): push from the checked-out commit; a tag of an older commit is refused"
  - "task-brief extraction of a plan's last task includes the CHECKPOINT section: trim it before dispatch"
  - "a review loop can drift past a plan's documented limits (Task 8 grew a 1020-line shell parser over 5 rounds): rule early that the plan's stated limits stand, or ask the owner (DEC-0005)"
  - "the S4 plan's git-pre-push.sh block is bash-only under an sh shim and replaces the S1 guard: B3-S4PREPUSH-1, owner S4 Task 5"
  - "the gates CI job is still not a required status check on main (owner's action, carried from b2)"
---

Post-merge facts (verified in CHECKPOINT b3 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
