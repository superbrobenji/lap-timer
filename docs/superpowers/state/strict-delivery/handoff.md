---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "5f9c467"
base: "e2a20af"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: 49
block: 2
next_task: "T7"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings:
  - "Tasks 4-6 ran under an opus sub-controller; the session kept CHECKPOINT b2 (final review, Fable verify, PR, merge, cleanup)"
  - "Tasks 4-6 plan text corrected to the b1 library: owner error routed to decisions.owner, DEC-0003 commit checks in gate_claims/gate_decisions with real fixture commits, gate_register conventions (forgery-safe ids, empty --root error, no PASS for skipped categories, memoised commit_exists), D3 re-sync"
  - "shared gate helpers live in lib/registers.py (row_id, memo_commit_exists, safe_text, require_regular, rel_error, PathError, OWNER_ANSWER_ERROR); no gate imports another gate's private names"
  - "open_decisions(decisions, *, owner=None) means not answered by the owner; gate_decisions uses it as its only definition"
  - "the CI job gates stays one job named gates on Python 3.9 and 3.12 (no matrix); run_all's CLI follows the lib gate conventions"
grant: DEC-0001
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "zsh does not word-split $var in for/set -- loops; run multi-line shell helpers under bash -c"
  - "macOS python3 caches bytecode under ~/Library/Caches/com.apple.python; use PYTHONDONTWRITEBYTECODE=1 for same-length mutations"
  - "Homebrew python3.12 has no PyYAML; use tools/.venv/bin/python3 for the 3.12 run"
  - "a shallow clone FAILs the register sha checks by design (DEC-0003); CI checks out with fetch-depth 0"
  - "git log main is ambiguous (top-level main/ directory): write main -- or origin/main"
  - "the brief's code is not trusted: every b1 and b2 task found and fixed defects in the plan's verbatim code; plan blocks are re-synced byte-equal after each round (spec D3), and later tasks' derived suite totals shift with every test added"
  - "run_all resolves a relative --root once and hands every gate one consistent form; gate_claims --base/--head take hex shas only (HEAD is refused)"
  - "claim cites are normalised repo-relative paths that exist at verified_at (DEC-0004): never a leading ':' (pathspec magic), a directory with exactly one trailing '/', a submodule in the file form"
  - "CHECKPOINT Step 2 packages need executed claims tables and mutation-<test>.txt transcripts, not pointers (b2 verifier I-3)"
  - "a machine sleep kills background subagents mid-run; check their output artefacts before re-dispatching"
---

Post-merge facts (verified in CHECKPOINT b2 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.

Owner item carried: the CI job `gates` runs on every PR and must be green; the owner ruled "add it to b2. never add directly to main", so the session changed no branch protection. Making `gates` a required status check on `main` remains the owner's action.
