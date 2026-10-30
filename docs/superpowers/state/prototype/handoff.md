---
plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md
spec: docs/superpowers/specs/2026-09-14-lap-timer-design.md
branch: block/prototype-b1
head: "667e0d7"
base: "667e0d7"
ledger: docs/superpowers/state/prototype/ledger.md
resume_line: 6
block: 1
next_task: "b1 Task 1"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings: []
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

Stream B handoff (playbook `docs/superpowers/playbooks/two-streams.md`), written at the 2026-10-29 replan; stream B has not started.

`head`, `base` and `resume_line` name `667e0d7` and the ledger draft's 6 lines; the session commit that lands this handoff moves the b5 tip, and the first stream B session sets `base` and `head` to the commit it branches from. `block: 1` is the plan's block b1 (DEC-0156: the prototype blocks are b1..b6, ledger line `CHECKPOINT b1 <sha>`). `grant: DEC-0155` (grant:prototype-field-ready = yes): a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch, each next block branching from the previous block's tip (DEC-0154), then when every check passes, on the owner's sentence "merge when all checks pass".

Resume prompt (a fresh session, after `/clear`):
```
Invoke the strict-delivery skill. Read, in this order: docs/superpowers/playbooks/two-streams.md, docs/superpowers/state/program/board.md, docs/superpowers/state/prototype/handoff.md, and docs/superpowers/plans/2026-10-29-prototype-field-ready.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Create block/prototype-b1 from the tip of block/strict-delivery-b5 in the worktree ../lap-timer-worktrees/prototype and run block b1 (Tasks 1-3) with superpowers:subagent-driven-development, models per tools/gates/config.yml, lean path (DEC-0145). Never flash without the owner's "ready". Stop at b1's checkpoint for /clear.
```
