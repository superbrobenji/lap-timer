---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "1f5df6d"
base: "73cd3cc"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: 31
block: 1
next_task: "T4"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings:
  - "--json only as argv[0] (umbrella CLI table corrected)"
  - "detached HEAD is a GateError in gitinfo.branch"
  - "evidence and pin-transcript paths are repo-relative"
  - "owner/validity check in the schema layer (validate_decision, grant_for, deferred)"
  - "report-dir symlinks followed once; non-directory report path fails"
  - "list-item/table ids per DEC-0002 tighten; near-miss markers FAIL"
  - "machine-independent gate messages (root-relative, no -C <root>)"
  - "asked_at/answered_at sha = main commit when the row was created"
  - "Task 3 residuals carried into one final fix wave (owner ratified all nine; >5 raised to the owner per spec D10)"
grant: DEC-0001
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C (worktree removed in b1 Step 0)", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "zsh does not word-split $var in for/set -- loops; run multi-line shell helpers under bash -c"
  - "macOS python3 caches bytecode under ~/Library/Caches/com.apple.python; use PYTHONDONTWRITEBYTECODE=1 for same-length mutations"
  - "Homebrew python3.12 has no PyYAML; use tools/.venv/bin/python3 for the 3.12 run"
  - "a shallow clone FAILs register.evidence by design (DEC-0003); CI must checkout with fetch-depth 0"
  - "git log main is ambiguous (top-level main/ directory): write main -- or origin/main"
  - "the brief's code is not trusted: every task found and fixed defects in the plan's verbatim code; plan blocks are re-synced byte-equal after each round (spec D3)"
---

Post-merge facts (verified in CHECKPOINT b1 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
