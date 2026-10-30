---
plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md
spec: docs/superpowers/specs/2026-09-14-lap-timer-design.md
branch: block/prototype-b1
head: "6df7981"
base: "39a3e8a"
ledger: docs/superpowers/state/prototype/ledger.md
resume_line: 19
block: 1
next_task: "b1 fix wave for Tasks 1-2 after the owner answers DEC-0300..DEC-0314, then b1 Task 3"
findings_in_flight: ["PT-B1-T1: in-progress", "PT-B1-T2: in-progress", "PT-B1-T3: planned"]
claims_verified_at_head: []
live_subagents: []
open_decisions: [DEC-0300, DEC-0301, DEC-0302, DEC-0303, DEC-0304, DEC-0305, DEC-0306, DEC-0307, DEC-0308, DEC-0309, DEC-0310, DEC-0311, DEC-0312, DEC-0313, DEC-0314]
rulings: ["b1 packages carry mutation-<id>.txt from a git-ignored task checker replayed by the controller (ledger :14; under DEC-0300)", "withdrawn: no transcript for tracker rows (ledger :13)", "withdrawn: BOM not built = not in hand (ledger :15; DEC-0309)"]
grant: DEC-0155
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "block/prototype-b1 does not exist yet: the first stream B session creates it from the tip of block/strict-delivery-b5 (main has neither DEC-0132..DEC-0157 nor the b5 gates the plan must pass)"
  - "never flash without the owner's explicit 'ready'; after an OTA push never open the lap-timer port for 35 s; one 5 V source at a time (DEC-0135)"
  - "with the dev-kit wired, USB esptool fails on the shared UART0 RX: flash via the dev-kit OTA push, or hold the dev-kit in reset for USB"
  - "ESP32 consoles use CR line endings: host tools send a bare \\r"
  - "build only via source tools/idf-env.sh (pins IDF 5.3.2); zero warnings; every firmware gate list builds test_apps/core_selftest"
  - "files outside stream B's ownership (playbook 'Ownership'), ci.yml included, are raised at the next sync, never edited"
  - "stream B ids: decisions DEC-0300..DEC-0499, findings prefix PT-; registers are append-only (playbook 'Shared files')"
  - "the Agent hook refuses a dispatch that names an unanswered DEC id or breaks the umbrella dispatch grammar: run gate_dispatch.py --prompt <file> --tool-model <m> first"
  - "every commit, text-only included, first passes run_all and the gate suite on both interpreters (spec D34)"
---

Stream B handoff (playbook `docs/superpowers/playbooks/two-streams.md`). State at 2026-10-30 17:45: block b1 stopped under spec D30, not at its checkpoint.

b1 ran Tasks 1 and 2 on `block/prototype-b1` (worktree `../lap-timer-worktrees/prototype`, pushed): scope table 8a020bc + fix 6c3f445, wiring doc and BOM statuses bee8f5a + fix 6df7981. Both fix rounds introduced defects outside their recorded classes, so fixes stopped (D30); the read-only investigation is imported in the ledger (2026-10-30 entries), with the open review findings. Task 3 (b2-b6 tasks in full) has not started. The next session first needs the owner's answers to DEC-0300..DEC-0314, staged in `docs/superpowers/state/prototype/pending-decisions.yml` (decisions.yml rejects them until DEC-0314 is answered) and asked at a sync: DEC-0300..0302 decide how planning-text tasks finish and how fix rounds are shaped; the rest are scope, BOM and hardware facts the fix wave writes in. Review packages and reports are git-ignored under `.superpowers/sdd/prototype/` in the worktree.

`grant: DEC-0155` (grant:prototype-field-ready = yes): a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch (DEC-0154), then when every check passes, on the owner's sentence "merge when all checks pass".

Resume prompt (a fresh session, after `/clear`, once DEC-0300..DEC-0314 are answered):
```
Invoke the strict-delivery skill. Read, in this order: docs/superpowers/playbooks/two-streams.md, docs/superpowers/state/program/board.md, docs/superpowers/state/prototype/handoff.md, docs/superpowers/state/prototype/ledger.md and docs/superpowers/plans/2026-10-29-prototype-field-ready.md. Enter the worktree ../lap-timer-worktrees/prototype (branch block/prototype-b1). Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Apply the owner's answers to DEC-0300..DEC-0314, run one fix wave for b1 Tasks 1-2 shaped by DEC-0300..DEC-0302, then b1 Task 3, with superpowers:subagent-driven-development, models per tools/gates/config.yml, lean path (DEC-0145). Never flash without the owner's "ready". Stop at b1's checkpoint for /clear.
```
